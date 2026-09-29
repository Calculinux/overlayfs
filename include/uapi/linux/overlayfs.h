/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Overlayfs filesystem user API
 *
 * The ioctls below may be issued on any file or directory of an overlay
 * mount (typically the mount point, opened with O_RDONLY | O_DIRECTORY).
 * Each takes an absolute path to an entry inside that same overlay:
 * path_ptr points to a null-terminated string, path_len is its strlen().
 * The final path component must be a name (not "", "." or "..").
 */

#ifndef _UAPI_LINUX_OVERLAYFS_H
#define _UAPI_LINUX_OVERLAYFS_H

#include <linux/types.h>
#include <linux/ioctl.h>

/*
 * OVL_IOC_RESTORE_LOWER - Remove a whiteout so the lower entry shows again
 *
 * Useful when a lower-layer file was hidden by a whiteout, e.g. after
 * removing an overlay copy of a package that the base image also ships.
 *
 * Returns:
 *   0 on success (the lower entry is now visible)
 *   -ENOENT if there is no upper entry at the path
 *   -EEXIST if the upper entry is not a whiteout, or the name is visible
 *   -ENODATA if the whiteout was removed but no lower entry shows through
 *   -EXDEV if the path is not on the fd's overlay
 *   -EINVAL for a malformed path or nonzero flags
 *   -EACCES/-EPERM if the caller may not modify the parent directory
 *   -EROFS if the overlay has no writable upper layer
 */
struct ovl_restore_lower_args {
	__aligned_u64 path_ptr;		/* Pointer to null-terminated path string */
	__u32 path_len;			/* Length of path (strlen, not including null) */
	__u32 flags;			/* Reserved, must be 0 */
};

/*
 * OVL_IOC_IS_RESTORABLE - Check whether OVL_IOC_RESTORE_LOWER would succeed
 *
 * Returns:
 *   0 if the path is a whiteout with a lower directory below it
 *   -ENOENT if there is no upper entry at the path
 *   -EEXIST if the upper entry is not a whiteout
 *   -ENODATA if the parent has no lower directory to reveal anything from
 *   other errors as for OVL_IOC_RESTORE_LOWER
 */
struct ovl_is_restorable_args {
	__aligned_u64 path_ptr;		/* Pointer to null-terminated path string */
	__u32 path_len;			/* Length of path (strlen, not including null) */
	__u32 flags;			/* Reserved, must be 0 */
};

/*
 * OVL_IOC_UPPER_STATE - Report what the upper layer holds for a name
 *
 * On success fills in state, state_flags and mode. The lookup does not
 * modify anything and does not need the overlay to be writable beyond
 * having an upper layer.
 */
#define OVL_UPPER_STATE_NONE		0	/* no upper entry */
#define OVL_UPPER_STATE_WHITEOUT	1	/* upper entry is a whiteout */
#define OVL_UPPER_STATE_UPPER		2	/* real upper entry (see mode) */

/* The upper entry is an opaque directory (hides the lower directory) */
#define OVL_UPPER_STATE_F_OPAQUE	(1U << 0)
/* The parent has no lower directory (pure upper, or below an opaque dir) */
#define OVL_UPPER_STATE_F_NO_LOWER_DIR	(1U << 1)

struct ovl_upper_state_args {
	__aligned_u64 path_ptr;		/* in: pointer to null-terminated path */
	__u32 path_len;			/* in: strlen(path) */
	__u32 flags;			/* in: reserved, must be 0 */
	__u32 state;			/* out: OVL_UPPER_STATE_* */
	__u32 state_flags;		/* out: OVL_UPPER_STATE_F_* */
	__u32 mode;			/* out: st_mode of an UPPER entry, else 0 */
	__u32 pad;			/* in: must be 0 */
};

#define OVL_IOC_RESTORE_LOWER	_IOW('O', 1, struct ovl_restore_lower_args)
#define OVL_IOC_IS_RESTORABLE	_IOW('O', 2, struct ovl_is_restorable_args)
#define OVL_IOC_UPPER_STATE	_IOWR('O', 3, struct ovl_upper_state_args)

#endif /* _UAPI_LINUX_OVERLAYFS_H */
