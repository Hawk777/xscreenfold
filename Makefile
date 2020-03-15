override CFLAGS := $(CFLAGS) -Wall -Wextra -std=c99 -D_GNU_SOURCE

xscreenfold : main.o fah-control.o poller.o xscreensaver-watch.o
	$(CC) $(CFLAGS) -o $@ $+

main.o : fah-control.h poller.h xscreensaver-watch.h

fah-control.o : poller.h

xscreensaver-watch.o : poller.h
