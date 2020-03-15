#include "fah-control.h"
#include "poller.h"
#include "xscreensaver-watch.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

static bool add_slots(fah_control_t fah, int argc, char **argv) {
	for(int i = 1; i < argc; ++i) {
		errno = 0;
		char *endptr;
		unsigned long ul = strtoul(argv[i], &endptr, 10);
		if(errno != 0) {
			return false;
		} else if(*endptr) {
			errno = EINVAL;
			return false;
		} else if(ul > UINT_MAX) {
			errno = EDOM;
			return false;
		}
		if(!fah_control_slot_add(fah, (unsigned int) ul)) {
			return false;
		}
	}
	return true;
}

static bool screen_saver_cb(void *cookie, bool active) {
	if(active) {
		puts("Activating Folding@Home.");
		fah_control_t fah = cookie;
		return fah_control_send(fah, true);
	} else {
		return true;
	}
}

int main(int argc, char **argv) {
	bool ok = false;
	poller_t poller = poller_new();
	if(poller) {
		fah_control_t fah = fah_control_new(poller);
		if(fah) {
			if(add_slots(fah, argc, argv)) {
				xscreensaver_watch_watcher_t watcher;
				watcher.cb = &screen_saver_cb;
				watcher.cookie = fah;
				xscreensaver_watch_t watch = xscreensaver_watch_new(poller, &watcher);
				if(watch) {
					ok = poller_run(poller);
					int tmp = errno;
					xscreensaver_watch_delete(watch);
					errno = tmp;
				}
			}
			int tmp = errno;
			fah_control_delete(fah);
			errno = tmp;
		}
		int tmp = errno;
		poller_delete(poller);
		errno = tmp;
	}
	if(!ok) {
		perror(argv[0]);
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
