#if !defined(XSCREENSAVER_WATCH_H)
#define XSCREENSAVER_WATCH_H

#include "poller.h"
#include <sys/types.h>

/**
 * @brief A watcher for screen saver activation.
 */
typedef struct xscreensaver_watch *xscreensaver_watch_t;

/**
 * @brief A client who wants to be notified on screen saver activity.
 */
typedef struct {
	/**
	 * @brief The function to call.
	 *
	 * @param[in] cookie The value of @ref cookie for this client.
	 * @param[in] active @c true if the screen saver activated, or @c false if
	 * it deactivated.
	 * @retval true Everything is OK and monitoring should continue.
	 * @retval false An error occurred and should be propagated out to the top
	 * level of the program.
	 */
	bool (*cb)(void *cookie, bool active);

	/**
	 * @brief An opaque pointer to pass to @ref cb.
	 */
	void *cookie;
} xscreensaver_watch_watcher_t;

xscreensaver_watch_t xscreensaver_watch_new(poller_t poller, const xscreensaver_watch_watcher_t *watcher);
void xscreensaver_watch_delete(xscreensaver_watch_t watch);

#endif
