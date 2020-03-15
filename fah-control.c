#include "fah-control.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <sys/types.h>

struct fah_control {
	/**
	 * @brief The socket.
	 */
	int sock;

	/**
	 * @brief The poller.
	 */
	poller_t poller;

	/**
	 * @brief The pollable for the read size of the socket.
	 */
	poller_pollable_t sock_pollable;

	/**
	 * @brief The bitmask of slots to manipulate.
	 */
	unsigned int *slot_bits;

	/**
	 * @brief The number of integers in @ref slot_bits.
	 */
	unsigned int slot_bits_words;
};

/**
 * @brief The number of bits per unsigned integer.
 */
static const unsigned int fah_control_bits_per_word = sizeof(unsigned int) * CHAR_BIT;

/**
 * @brief The poll callback for read readiness on the socket.
 *
 * @param[in] cookie The watch.
 * @retval true Everything is OK and polling should continue.
 * @retval false An error occurred and should be propagated out to the top
 * level of the program.
 */
static bool fah_control_poll(void *cookie) {
	fah_control_t fah = cookie;
	char buffer[256];
	ssize_t rc = recv(fah->sock, buffer, sizeof(buffer), MSG_DONTWAIT);
	if(rc > 0) {
		// Ignore the received data.
		return true;
	} else if(rc == 0) {
		// FAH died?
		errno = ECONNRESET;
		return false;
	} else if(errno == EAGAIN || errno == EWOULDBLOCK) {
		// Nothing ready yet.
		return true;
	} else {
		// Error.
		return false;
	}
}

/**
 * @brief Sends a string to a socket.
 *
 * @param[in] sock The socket.
 * @param[in] string The string to send.
 * @retval true The string was sent.
 * @retval false An error occurred.
 */
static bool fah_control_send_string(int sock, const char *string) {
	size_t remaining = strlen(string);
	while(remaining) {
		ssize_t rc = send(sock, string, remaining, 0);
		if(rc < 0) {
			return false;
		} else {
			string += rc;
			remaining -= rc;
		}
	}
	return true;
}

/**
 * @brief Connects to Folding@Home.
 *
 * @param[in] poller The poller to register with.
 */
fah_control_t fah_control_new(poller_t poller) {
	bool ok = false;
	fah_control_t ret = malloc(sizeof(*ret));
	if(ret) {
		ret->poller = poller;
		ret->sock_pollable.cb = &fah_control_poll;
		ret->sock_pollable.cookie = ret;
		ret->slot_bits = 0;
		ret->slot_bits_words = 0;
		ret->sock = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);
		if(ret->sock >= 0) {
			struct sockaddr_in sa;
			memset(&sa, 0, sizeof(sa));
			sa.sin_family = AF_INET;
			sa.sin_addr.s_addr = htonl(0x7F000001);
			sa.sin_port = htons(36330);
			if(connect(ret->sock, (const struct sockaddr *) &sa, sizeof(sa)) >= 0) {
				if(poller_add(poller, ret->sock, &ret->sock_pollable)) {
					ok = true;
				}
			}
			if(!ok) {
				int tmp = errno;
				close(ret->sock);
				errno = tmp;
			}
		}
		if(!ok) {
			int tmp = errno;
			free(ret);
			ret = 0;
			errno = tmp;
		}
	}
	return ret;
}

/**
 * @brief Disconnects from Folding@Home.
 *
 * @param[in] fah The connection to destroy, or a null pointer to do nothing.
 */
void fah_control_delete(fah_control_t fah) {
	if(fah) {
		poller_remove(fah->poller, fah->sock);
		close(fah->sock);
		free(fah->slot_bits);
		free(fah);
	}
}

/**
 * @brief Adds a slot to the list of slots this connection should pause and
 * unpause.
 *
 * If no slots are added, a call to @ref fah_control_send will pause or unpause
 * all slots. If one or more slots are added, only those slots will be
 * controlled.
 *
 * @param[in] fah The connection to modify.
 * @param[in] slot The slot to add.
 * @retval true The slot was added to the set.
 * @retval false An error occurred.
 */
bool fah_control_slot_add(fah_control_t fah, unsigned int slot) {
	unsigned int word = slot / fah_control_bits_per_word;
	unsigned int bit = slot % fah_control_bits_per_word;
	if(fah->slot_bits_words <= word) {
		unsigned int new_words = word + 1;
		unsigned int *new = realloc(fah->slot_bits, new_words * sizeof(unsigned int));
		if(!new) {
			return false;
		}
		memset(new + fah->slot_bits_words, 0, (new_words - fah->slot_bits_words) * sizeof(unsigned int));
		fah->slot_bits = new;
		fah->slot_bits_words = new_words;
	}
	fah->slot_bits[word] |= 1U << bit;
	return true;
}

/**
 * @brief Starts or stops Folding@Home.
 *
 * @param[in] fah The connection.
 * @param[in] run @c true to run work, or @c false to pause it.
 * @retval true The command was sent.
 * @retval false An error occurred.
 */
bool fah_control_send(fah_control_t fah, bool run) {
	const char *command = run ? "unpause" : "pause";
	char buffer[256];

	if(fah->slot_bits) {
		for(unsigned int word = 0; word != fah->slot_bits_words; ++word) {
			for(unsigned int bit = 0; bit != fah_control_bits_per_word; ++bit) {
				if(fah->slot_bits[word] & (1U << bit)) {
					unsigned int slot = word * fah_control_bits_per_word + bit;
					sprintf(buffer, "%s %u\n", command, slot);
					if(!fah_control_send_string(fah->sock, buffer)) {
						return false;
					}
				}
			}
		}
		return true;
	} else {
		strcpy(buffer, command);
		strcat(buffer, "\n");
		return fah_control_send_string(fah->sock, buffer);
	}
}
