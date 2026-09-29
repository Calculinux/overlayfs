/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ovl-restore - Inspect and restore overlayfs lower layer entries
 *
 * Copyright (C) 2025 Calculinux Project
 * Author: Ben Klop
 *
 * Uses the overlayfs ioctls from include/uapi/linux/overlayfs.h:
 *   OVL_IOC_RESTORE_LOWER  remove a whiteout so the lower entry shows again
 *   OVL_IOC_IS_RESTORABLE  check whether a restore would succeed
 *   OVL_IOC_UPPER_STATE    report what the upper layer holds for a name
 *
 * Usage:
 *   ovl-restore [--test | --state] <overlay-mount-point> <path>...
 *
 * Examples:
 *   ovl-restore /usr /usr/bin/foo /usr/lib/libbar.so
 *   ovl-restore --test /usr /usr/bin/foo
 *   ovl-restore --state /etc /etc/hostname
 *
 * --state prints one line per path, tab separated:
 *   <path> <none|whiteout|upper> <flags> <mode>
 * where flags is a comma separated list (opaque, no-lower-dir) or "-",
 * and mode is the octal st_mode of an upper entry (0 otherwise).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdint.h>

#include <linux/overlayfs.h>

enum mode { MODE_RESTORE, MODE_TEST, MODE_STATE };

static void usage(const char *progname)
{
	fprintf(stderr,
		"Usage: %s [--test | --state] <overlay-mount-point> <path>...\n"
		"\n"
		"Inspect and restore overlayfs lower layer entries.\n"
		"\n"
		"Options:\n"
		"  --test, -t   check whether the paths can be restored\n"
		"  --state, -s  print the upper layer state of each path:\n"
		"               <path>\\t<none|whiteout|upper>\\t<flags>\\t<mode>\n"
		"\n"
		"Arguments:\n"
		"  overlay-mount-point  any directory on the overlay (usually its mount point)\n"
		"  path                 absolute paths inside that overlay\n"
		"\n"
		"Examples:\n"
		"  %s /usr /usr/bin/foo /usr/lib/libbar.so\n"
		"  %s --state /etc /etc/hostname\n",
		progname, progname, progname);
	exit(2);
}

static int restore_lower(int fd, const char *path)
{
	struct ovl_restore_lower_args args = {
		.path_ptr = (uint64_t)(uintptr_t)path,
		.path_len = strlen(path),
	};

	return ioctl(fd, OVL_IOC_RESTORE_LOWER, &args);
}

static int is_restorable(int fd, const char *path)
{
	struct ovl_is_restorable_args args = {
		.path_ptr = (uint64_t)(uintptr_t)path,
		.path_len = strlen(path),
	};

	return ioctl(fd, OVL_IOC_IS_RESTORABLE, &args);
}

static int upper_state(int fd, const char *path)
{
	struct ovl_upper_state_args args = {
		.path_ptr = (uint64_t)(uintptr_t)path,
		.path_len = strlen(path),
	};
	const char *state;
	char flags[32] = "";

	if (ioctl(fd, OVL_IOC_UPPER_STATE, &args) < 0)
		return -1;

	switch (args.state) {
	case OVL_UPPER_STATE_NONE:
		state = "none";
		break;
	case OVL_UPPER_STATE_WHITEOUT:
		state = "whiteout";
		break;
	case OVL_UPPER_STATE_UPPER:
		state = "upper";
		break;
	default:
		state = "unknown";
		break;
	}
	if (args.state_flags & OVL_UPPER_STATE_F_OPAQUE)
		strcat(flags, "opaque");
	if (args.state_flags & OVL_UPPER_STATE_F_NO_LOWER_DIR)
		strcat(flags, *flags ? ",no-lower-dir" : "no-lower-dir");

	printf("%s\t%s\t%s\t%o\n", path, state, *flags ? flags : "-",
	       args.mode);
	return 0;
}

int main(int argc, char **argv)
{
	enum mode mode = MODE_RESTORE;
	const char *mount_point;
	int fd, i, start = 1, errors = 0;

	if (argc > 1 && (!strcmp(argv[1], "--test") || !strcmp(argv[1], "-t"))) {
		mode = MODE_TEST;
		start = 2;
	} else if (argc > 1 &&
		   (!strcmp(argv[1], "--state") || !strcmp(argv[1], "-s"))) {
		mode = MODE_STATE;
		start = 2;
	}

	if (argc < start + 2)
		usage(argv[0]);

	mount_point = argv[start];
	fd = open(mount_point, O_RDONLY | O_DIRECTORY);
	if (fd < 0) {
		fprintf(stderr, "Error: cannot open '%s': %s\n",
			mount_point, strerror(errno));
		return 1;
	}

	for (i = start + 1; i < argc; i++) {
		const char *path = argv[i];

		switch (mode) {
		case MODE_STATE:
			if (upper_state(fd, path) < 0) {
				fprintf(stderr, "Error: %s: %s\n", path,
					strerror(errno));
				errors++;
			}
			break;
		case MODE_TEST:
			if (is_restorable(fd, path) == 0) {
				printf("Restorable: %s\n", path);
			} else {
				printf("Not restorable: %s (%s)\n", path,
				       strerror(errno));
				errors++;
			}
			break;
		case MODE_RESTORE:
			if (restore_lower(fd, path) == 0) {
				printf("Restored: %s\n", path);
			} else {
				fprintf(stderr, "Error: failed to restore '%s': %s\n",
					path, strerror(errno));
				errors++;
			}
			break;
		}
	}

	close(fd);
	return errors ? 1 : 0;
}
