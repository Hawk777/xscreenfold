#include "xscreensaver-watch.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

struct xscreensaver_watch {
	/**
	 * @brief The process ID of the @c xscreensaver-command process.
	 */
	pid_t pid;

	/**
	 * @brief The file descriptor of the read side of the pipe.
	 */
	int rfd;

	/**
	 * @brief The text received from the process so far.
	 */
	char buffer[256];

	/**
	 * @brief The client to notify.
	 */
	const xscreensaver_watch_watcher_t *watcher;

	/**
	 * @brief The poller.
	 */
	poller_t poller;

	/**
	 * @brief The poller client for the pipe.
	 */
	poller_pollable_t rfd_pollable;
};

/**
 * @brief The result of a single step of polling the pipe.
 */
typedef enum {
	/**
	 * @brief Some work was done and another attempt should be made.
	 */
	XSCREENSAVER_WATCH_RFD_POLL_ONCE_AGAIN,

	/**
	 * @brief No work was done; a steady state has been reached and no more
	 * attempts should be made until additional data arrives on the pipe.
	 */
	XSCREENSAVER_WATCH_RFD_POLL_ONCE_WAIT,

	/**
	 * @brief An error occurred and should be propagated to the top level of
	 * the program.
	 */
	XSCREENSAVER_WATCH_RFD_POLL_ONCE_ERROR,
} xscreensaver_watch_rfd_poll_once_t;

/**
 * @brief Spawns a child process.
 *
 * @param[in] argv The parameters to pass, the first of which is the executable
 * name.
 * @param[in] input The file descriptor to use as standard input in the child
 * process.
 * @param[in] output The file descriptor to use as standard output in the child
 * process.
 * @param[in] error The file descriptor to use as standard error in the child
 * process.
 * @return The process ID of the spawned process, or -1 on error.
 */
static pid_t xscreensaver_watch_spawn(char **argv, int input, int output, int error) {
	pid_t pid;
	int stdfds[3] = {input, output, error};
	posix_spawn_file_actions_t factions;
	int err = posix_spawn_file_actions_init(&factions);
	if(err == 0) {
		for(int i = 0; err == 0 && i != 3; ++i) {
			if(stdfds[i] != i) {
				err = posix_spawn_file_actions_adddup2(&factions, stdfds[i], i);
			}
		}
		if(err == 0) {
			posix_spawnattr_t attr;
			err = posix_spawnattr_init(&attr);
			if(err == 0) {
				err = posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGMASK);
				if(err == 0) {
					sigset_t sigs;
					if(sigemptyset(&sigs) == 0) {
						err = posix_spawnattr_setsigmask(&attr, &sigs);
						if(err == 0) {
							err = posix_spawnp(&pid, argv[0], &factions, &attr, argv, environ);
						}
					} else {
						err = errno;
					}
				}
				posix_spawnattr_destroy(&attr);
			}
		}
		posix_spawn_file_actions_destroy(&factions);
	}
	errno = err;
	return err == 0 ? pid : -1;
}

/**
 * @brief Does one step of polling for readiness on the pipe.
 *
 * @param[in] watch The watch.
 * @return The result of the operation.
 */
static xscreensaver_watch_rfd_poll_once_t xscreensaver_watch_rfd_poll_once(xscreensaver_watch_t watch) {
	bool did_work = false;

	// Read some data from the pipe into the buffer.
	size_t old_length = strlen(watch->buffer);
	size_t space_remaining = sizeof(watch->buffer) - old_length - 1 /* space for NUL */;
	if(space_remaining) {
		ssize_t rc = read(watch->rfd, watch->buffer + old_length, space_remaining);
		if(rc == 0) {
			// EOF received (xscreensaver-command terminated). Make a best
			// effort to reap the child, but if it fails, there’s not much we
			// can do.
			int status;
			waitpid(watch->pid, &status, 0);
			errno = ESRCH;
			return XSCREENSAVER_WATCH_RFD_POLL_ONCE_ERROR;
		} else if(rc > 0) {
			watch->buffer[old_length + rc] = '\0';
			did_work = true;
		} else if(errno != EAGAIN) {
			return XSCREENSAVER_WATCH_RFD_POLL_ONCE_ERROR;
		}
	} else {
		// No space left in the buffer, so don’t read anything.
	}

	// Eat a line if there is a complete one.
	char *nl = strchr(watch->buffer, '\n');
	if(nl) {
		bool notify = false, value;
		if(!strncmp(watch->buffer, "BLANK ", 6) || !strncmp(watch->buffer, "LOCK ", 5)) {
			notify = true;
			value = true;
		} else if(!strncmp(watch->buffer, "UNBLANK ", 8)) {
			notify = true;
			value = false;
		}
		if(notify) {
			if(!watch->watcher->cb(watch->watcher->cookie, value)) {
				return XSCREENSAVER_WATCH_RFD_POLL_ONCE_ERROR;
			}
		}
		memmove(watch->buffer, nl + 1, strlen(nl + 1) + 1 /* include the NUL */);
		did_work = true;
	}

	// If the buffer is completely full, xscreensaver-command sent way too much
	// text and we should fail.
	if(strlen(watch->buffer) == sizeof(watch->buffer) - 1) {
		errno = EPROTO;
		return XSCREENSAVER_WATCH_RFD_POLL_ONCE_ERROR;
	}

	return did_work ? XSCREENSAVER_WATCH_RFD_POLL_ONCE_AGAIN : XSCREENSAVER_WATCH_RFD_POLL_ONCE_WAIT;
}

/**
 * @brief The poll callback for read readiness on the pipe.
 *
 * @param[in] cookie The watch.
 * @retval true Everything is OK and polling should continue.
 * @retval false An error occurred and should be propagated out to the top
 * level of the program.
 */
static bool xscreensaver_watch_rfd_poll(void *cookie) {
	xscreensaver_watch_t watch = cookie;
	for(;;) {
		switch(xscreensaver_watch_rfd_poll_once(watch)) {
			case XSCREENSAVER_WATCH_RFD_POLL_ONCE_AGAIN:
				break;

			case XSCREENSAVER_WATCH_RFD_POLL_ONCE_WAIT:
				return true;

			case XSCREENSAVER_WATCH_RFD_POLL_ONCE_ERROR:
				return false;
		}
	}
}

/**
 * @brief Starts monitoring for screen saver activity.
 *
 * @param[in] poller The poller to register with for polling the file
 * descriptor and child signals.
 * @param[in] watcher The client to notify.
 * @return The watch object, or a null pointer on error.
 */
xscreensaver_watch_t xscreensaver_watch_new(poller_t poller, const xscreensaver_watch_watcher_t *watcher) {
	bool ok = false;
	xscreensaver_watch_t ret = malloc(sizeof(*ret));
	if(ret) {
		ret->buffer[0] = '\0';
		ret->watcher = watcher;
		ret->poller = poller;
		ret->rfd_pollable.cb = &xscreensaver_watch_rfd_poll;
		ret->rfd_pollable.cookie = ret;
		int nullfd = open("/dev/null", O_RDWR | O_CLOEXEC);
		if(nullfd >= 0) {
			int pipefds[2];
			if(pipe2(pipefds, O_CLOEXEC) == 0) {
				ret->rfd = pipefds[0];
				if(fcntl(ret->rfd, F_SETFL, O_NONBLOCK) == 0) {
					if(poller_add(poller, ret->rfd, &ret->rfd_pollable)) {
						char *argv[] = {"xscreensaver-command", "-watch", 0};
						ret->pid = xscreensaver_watch_spawn(argv, nullfd, pipefds[1], 2);
						if(ret->pid != -1) {
							ok = true;
						}
						if(!ok) {
							poller_remove(poller, ret->rfd);
						}
					}
				}
				int tmp = errno;
				close(pipefds[1]);
				if(!ok) {
					close(pipefds[0]);
				}
				errno = tmp;
			}
			int tmp = errno;
			close(nullfd);
			errno = tmp;
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
 * @brief Stops monitoring for screen saver activity.
 *
 * @param[in] watcher The object to destroy, or a null pointer to do nothing.
 */
void xscreensaver_watch_delete(xscreensaver_watch_t watch) {
	if(watch) {
		if(watch->pid >= 0) {
			kill(watch->pid, SIGTERM);
			// Make a best effort to reap the child, but if it fails, there’s
			// not much we can do.
			int status;
			waitpid(watch->pid, &status, 0);
		}
		poller_remove(watch->poller, watch->rfd);
		close(watch->rfd);
		free(watch);
	}
}
