/* Copyright (C) 2026-present PortareOS */
/* Where PortareOS's own files live. On the device that is the filesystem
 * root. A build for another Linux system sets PL_ROOT (make ROOT=<dir>) to
 * a directory that holds the same layout: usr/config, usr/bin/runemu.sh,
 * usr/share/portarelauncher, storage/.config and so on. Programs every
 * distribution has (systemctl, bluetoothctl) and the kernel's files (/dev,
 * /sys, /proc, /etc/os-release, the timezone database) stay where they are. */
#ifndef PL_PATHS_H
#define PL_PATHS_H

#ifndef PL_ROOT
#define PL_ROOT ""
#endif

#endif
