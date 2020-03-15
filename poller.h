#if !defined(POLLER_H)
#define POLLER_H

#include <stdbool.h>

/**
 * @brief An object that can monitor file descriptors for readiness to read and
 * notify clients.
 */
typedef struct poller *poller_t;

/**
 * @brief Information about how a client wishes to be notified.
 */
typedef struct {
	/**
	 * @brief The function to call.
	 *
	 * @param[in] cookie The value of @ref cookie for this client.
	 * @retval true Everything is OK and polling should continue.
	 * @retval false An error occurred and should be propagated out to the top
	 * level of the program.
	 */
	bool (*cb)(void *cookie);

	/**
	 * @brief An opaque pointer to pass to @ref cb.
	 */
	void *cookie;
} poller_pollable_t;

poller_t poller_new(void);
void poller_delete(poller_t poller);
bool poller_add(poller_t poller, int fd, const poller_pollable_t *pollable);
void poller_remove(poller_t poller, int fd);
bool poller_run(poller_t poller);

#endif
