#include "poller.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/epoll.h>

struct poller {
	/**
	 * @brief The epoll object.
	 */
	int epollfd;

	/**
	 * @brief The number of clients registered on this poller.
	 */
	unsigned int count;
};

/**
 * @brief Constructs a new poller.
 *
 * @return The poller, or a null pointer on error.
 */
poller_t poller_new(void) {
	poller_t ret = malloc(sizeof(*ret));
	if(ret) {
		ret->epollfd = epoll_create1(EPOLL_CLOEXEC);
		if(ret->epollfd >= 0) {
			ret->count = 0;
		} else {
			int tmp = errno;
			free(ret);
			ret = 0;
			errno = tmp;
		}
	}
	return ret;
}

/**
 * @brief Deletes a poller.
 *
 * @param[in] poller The poller to delete, or a null pointer to do nothing.
 */
void poller_delete(poller_t poller) {
	if(poller) {
		close(poller->epollfd);
		free(poller);
	}
}

/**
 * @brief Registers a client with a poller.
 *
 * @param[in] poller The poller to register with.
 * @param[in] fd The file descriptor to monitor.
 * @param[in] pollable The notification to invoke when @p fd is readable.
 * @retval true The client was registered.
 * @retval false An error occurred.
 */
bool poller_add(poller_t poller, int fd, const poller_pollable_t *pollable) {
	struct epoll_event event;
	event.events = EPOLLIN;
	event.data.ptr = (void *) pollable;
	bool ok = epoll_ctl(poller->epollfd, EPOLL_CTL_ADD, fd, &event) == 0;
	if(ok) {
		++poller->count;
	}
	return ok;
}

/**
 * @brief Unregisters a client with a poller.
 *
 * @param[in] poller The poller to unregister with.
 * @param[in] fd The file descriptor to stop monitoring.
 */
void poller_remove(poller_t poller, int fd) {
	if(epoll_ctl(poller->epollfd, EPOLL_CTL_DEL, fd, 0) < 0) {
		abort();
	}
	--poller->count;
}

/**
 * @brief Runs a poller, notifying clients.
 *
 * @param[in] poller The poller to run.
 * @retval true All clients were unregistered normally.
 * @retval false An error occurred, either in the mechanics of polling or in a
 * client.
 */
bool poller_run(poller_t poller) {
	while(poller->count) {
		struct epoll_event event;
		int ready = epoll_wait(poller->epollfd, &event, 1, -1);
		if(ready < 0) {
			return false;
		}
		assert(ready == 1);
		const poller_pollable_t *pollable = event.data.ptr;
		if(!pollable->cb(pollable->cookie)) {
			return false;
		}
	}
	return true;
}
