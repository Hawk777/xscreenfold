Overview
========

`xscreenfold` is a program intended to run in the background of a desktop
session. It waits until `xscreensaver` activates (either due to blanking
timeout or due to an explicit lock request) and then unpauses one or more
Folding@Home slots. This is primarily useful for GPU slots, since those can
have a detrimental effect on everyday interactive usage of the system—hence,
one may manually pause the GPU slots while using the system, and `xscreenfold`
can ensure they get unpaused automatically when you step away.

`xscreenfold` does not pause the slots after the screen saver is deactivated.
This is because whether you want the slots paused or unpaused largely depends
on your workload—for example, some applications may work just fine with GPU
slots running, while others, even nominally 2D applications, may perform badly.


Usage
=====

Invoke `xscreenfold` with zero or more command-line parameters. If no
parameters are specified, `xscreenfold` will unpause all slots when your screen
saver activates. If one or more parameters are specified, they must be integer
slot numbers, and `xscreenfold` will only unpause those slots, leaving all the
other slots alone.
