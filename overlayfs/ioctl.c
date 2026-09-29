// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Calculinux Project
 * Author: Ben Klop
 *
 * Overlayfs ioctl operations
 *
 * All commands take an absolute path to an entry inside the overlay the
 * ioctl fd belongs to (the fd may be any file or directory on that
 * overlay, typically its mount point).
 *
 * Paths are resolved by looking up the *parent* directory through the
 * overlay and then looking up the final name directly in the parent's
 * upper directory. Resolving the full path through the overlay cannot
 * work for whiteouts: overlay lookup stops at a whiteout and returns a
 * negative dentry without an upper, so the whiteout itself is never seen.
 *
 * Whiteouts are classified with ovl_upper_is_whiteout(), which also
 * recognises xattr whiteouts, matching what overlay lookup treats as one.
 */

#include <linux/fs.h>
#include <linux/limits.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/xattr.h>
/* This module's ABI header, never a copy that may be in the kernel tree */
#include "../include/uapi/linux/overlayfs.h"
#include "overlayfs.h"

/**
 * ovl_ioctl_copy_path - Validate and copy a path from userspace
 * @path_ptr: userspace pointer to a null-terminated path string
 * @path_len: strlen() of the path (not including the null terminator)
 * @pathname_out: allocated copy (caller must kfree)
 *
 * The path must be absolute, contain no embedded nulls and name an entry
 * below "/" (a trailing slash, "." or ".." as the final component is
 * rejected because it does not name a directory entry).
 */
static int ovl_ioctl_copy_path(__u64 path_ptr, __u32 path_len,
			       char **pathname_out)
{
	char *pathname;
	const char *base;

	if (path_len == 0 || path_len >= (__u32)PATH_MAX)
		return -EINVAL;

	pathname = kmalloc(path_len + 1, GFP_KERNEL);
	if (!pathname)
		return -ENOMEM;

	if (copy_from_user(pathname, u64_to_user_ptr(path_ptr), path_len + 1)) {
		kfree(pathname);
		return -EFAULT;
	}

	if (pathname[path_len] != '\0' ||
	    strnlen(pathname, path_len) != path_len ||
	    pathname[0] != '/') {
		kfree(pathname);
		return -EINVAL;
	}

	base = strrchr(pathname, '/') + 1;
	if (!*base || !strcmp(base, ".") || !strcmp(base, "..")) {
		kfree(pathname);
		return -EINVAL;
	}

	*pathname_out = pathname;
	return 0;
}

/*
 * Split @pathname into its parent directory and final component and
 * resolve the parent through the overlay. @pathname is modified while the
 * parent is looked up and restored before returning. The parent must live
 * on the same overlay as @sb.
 */
static int ovl_ioctl_lookup_parent(struct super_block *sb, char *pathname,
				   struct path *parent, struct qstr *name)
{
	char *slash = strrchr(pathname, '/');
	int err;

	name->name = slash + 1;
	name->len = strlen(slash + 1);
	name->hash = 0;

	/* "/foo" -> parent "/" */
	if (slash == pathname) {
		err = kern_path("/", LOOKUP_DIRECTORY, parent);
	} else {
		*slash = '\0';
		err = kern_path(pathname, LOOKUP_FOLLOW | LOOKUP_DIRECTORY,
				parent);
		*slash = '/';
	}
	if (err)
		return err;

	if (parent->dentry->d_sb != sb) {
		path_put(parent);
		return -EXDEV;
	}
	return 0;
}

/**
 * ovl_upper_state - Classify a name in the upper layer
 * @sb: overlay super block
 * @pathname: absolute path
 * @args: filled with state, state_flags and mode
 */
static int ovl_upper_state(struct super_block *sb, char *pathname,
			   struct ovl_upper_state_args *args)
{
	struct ovl_fs *ofs = OVL_FS(sb);
	struct path parent;
	struct qstr name;
	struct dentry *upperdir, *this;
	const struct cred *old_cred;
	int err;

	args->state = OVL_UPPER_STATE_NONE;
	args->state_flags = 0;
	args->mode = 0;

	err = ovl_ioctl_lookup_parent(sb, pathname, &parent, &name);
	if (err)
		return err;

	/* The caller must be allowed to look up names in the directory */
	err = inode_permission(mnt_idmap(parent.mnt),
			       d_inode(parent.dentry), MAY_EXEC);
	if (err)
		goto out;

	/* Nothing below: removing a whiteout here could not reveal anything */
	if (!ovl_dentry_lower(parent.dentry))
		args->state_flags |= OVL_UPPER_STATE_F_NO_LOWER_DIR;

	upperdir = ovl_dentry_upper(parent.dentry);
	if (!upperdir)
		goto out;	/* parent not copied up: no upper entry */

	old_cred = ovl_override_creds(sb);
	this = lookup_one_unlocked(ovl_upper_mnt_idmap(ofs), name.name,
				   upperdir, name.len);
	if (IS_ERR(this)) {
		err = PTR_ERR(this);
	} else {
		if (ovl_upper_is_whiteout(ofs, this)) {
			args->state = OVL_UPPER_STATE_WHITEOUT;
		} else if (d_is_positive(this)) {
			struct path upperpath = {
				.mnt = ovl_upper_mnt(ofs),
				.dentry = this,
			};

			args->state = OVL_UPPER_STATE_UPPER;
			args->mode = d_inode(this)->i_mode;
			if (ovl_get_opaquedir_val(ofs, &upperpath) == 'y')
				args->state_flags |= OVL_UPPER_STATE_F_OPAQUE;
		}
		dput(this);
	}
	revert_creds(old_cred);
out:
	path_put(&parent);
	return err;
}

/**
 * ovl_restore_lower - Remove a whiteout so the lower entry becomes visible
 * @dentry: any dentry on the overlay (from the ioctl fd)
 * @pathname: absolute path
 *
 * Returns 0 once the lower entry is visible, -ENOENT if there is no upper
 * entry, -EEXIST if the upper entry is not a whiteout (or the name is
 * already visible), -ENODATA if the whiteout was removed but no lower
 * entry shows through (for example below an opaque directory).
 */
static int ovl_restore_lower(struct dentry *dentry, char *pathname)
{
	struct super_block *sb = dentry->d_sb;
	struct ovl_fs *ofs = OVL_FS(sb);
	struct path parent;
	struct qstr name;
	struct inode *dir, *udir;
	struct dentry *upperdir, *child, *this;
	const struct cred *old_cred;
	int err;

	err = ovl_want_write(dentry);
	if (err)
		return err;

	err = ovl_ioctl_lookup_parent(sb, pathname, &parent, &name);
	if (err)
		goto out_drop_write;

	dir = d_inode(parent.dentry);
	/* Same lock order as an unlink through the overlay */
	inode_lock_nested(dir, I_MUTEX_PARENT);

	/* Checked with the caller's credentials, before overriding them */
	err = inode_permission(mnt_idmap(parent.mnt), dir,
			       MAY_WRITE | MAY_EXEC);
	if (err)
		goto out_unlock;

	child = lookup_one_len(name.name, parent.dentry, name.len);
	if (IS_ERR(child)) {
		err = PTR_ERR(child);
		goto out_unlock;
	}
	if (d_is_positive(child)) {
		err = -EEXIST;
		goto out_dput_child;
	}

	upperdir = ovl_dentry_upper(parent.dentry);
	if (!upperdir) {
		err = -ENOENT;
		goto out_dput_child;
	}
	udir = d_inode(upperdir);

	old_cred = ovl_override_creds(sb);
	inode_lock_nested(udir, I_MUTEX_PARENT);
	/* Look the name up again under the lock; it may have changed */
	this = ovl_lookup_upper(ofs, name.name, upperdir, name.len);
	if (IS_ERR(this)) {
		err = PTR_ERR(this);
	} else {
		if (d_is_negative(this))
			err = -ENOENT;
		else if (!ovl_upper_is_whiteout(ofs, this))
			err = -EEXIST;
		else
			err = ovl_do_unlink(ofs, udir, this);
		dput(this);
	}
	inode_unlock(udir);
	revert_creds(old_cred);
	if (err)
		goto out_dput_child;

	/*
	 * Forget the cached negative dentry and bump the directory version
	 * so a merged readdir cache is rebuilt and lists the entry again.
	 */
	d_drop(child);
	ovl_dir_modified(parent.dentry, false);

	/* Report whether anything actually became visible */
	dput(child);
	child = lookup_one_len(name.name, parent.dentry, name.len);
	if (IS_ERR(child)) {
		err = PTR_ERR(child);
		goto out_unlock;
	}
	if (d_is_negative(child))
		err = -ENODATA;
	else
		pr_debug("restored lower layer entry %s\n", pathname);

out_dput_child:
	dput(child);
out_unlock:
	inode_unlock(dir);
	path_put(&parent);
out_drop_write:
	ovl_drop_write(dentry);
	return err;
}

/* Legacy query: 0 only when a whiteout exists and something is below it */
static int ovl_is_restorable(struct super_block *sb, char *pathname)
{
	struct ovl_upper_state_args state;
	int err;

	err = ovl_upper_state(sb, pathname, &state);
	if (err)
		return err;
	if (state.state != OVL_UPPER_STATE_WHITEOUT)
		return state.state == OVL_UPPER_STATE_NONE ? -ENOENT : -EEXIST;
	if (state.state_flags & OVL_UPPER_STATE_F_NO_LOWER_DIR)
		return -ENODATA;
	return 0;
}

/**
 * ovl_ioctl - Handle overlay filesystem ioctl commands
 * @file: any file or directory on the overlay
 * @cmd: ioctl command
 * @arg: userspace pointer to the command's argument struct
 */
long ovl_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct dentry *dentry = file->f_path.dentry;
	void __user *argp = (void __user *)arg;
	struct ovl_restore_lower_args restore_args;
	struct ovl_is_restorable_args restorable_args;
	struct ovl_upper_state_args state_args;
	char *pathname;
	long err;

	switch (cmd) {
	case OVL_IOC_RESTORE_LOWER:
	case OVL_IOC_IS_RESTORABLE:
	case OVL_IOC_UPPER_STATE:
		/* Only meaningful with a writable upper layer */
		if (!ovl_upper_mnt(OVL_FS(dentry->d_sb)))
			return -EROFS;
		break;
	default:
		return -ENOTTY;
	}

	switch (cmd) {
	case OVL_IOC_RESTORE_LOWER:
		if (copy_from_user(&restore_args, argp, sizeof(restore_args)))
			return -EFAULT;
		if (restore_args.flags)
			return -EINVAL;
		err = ovl_ioctl_copy_path(restore_args.path_ptr,
					  restore_args.path_len, &pathname);
		if (err)
			return err;
		err = ovl_restore_lower(dentry, pathname);
		kfree(pathname);
		return err;

	case OVL_IOC_IS_RESTORABLE:
		if (copy_from_user(&restorable_args, argp,
				   sizeof(restorable_args)))
			return -EFAULT;
		if (restorable_args.flags)
			return -EINVAL;
		err = ovl_ioctl_copy_path(restorable_args.path_ptr,
					  restorable_args.path_len, &pathname);
		if (err)
			return err;
		err = ovl_is_restorable(dentry->d_sb, pathname);
		kfree(pathname);
		return err;

	default: /* OVL_IOC_UPPER_STATE */
		if (copy_from_user(&state_args, argp, sizeof(state_args)))
			return -EFAULT;
		if (state_args.flags || state_args.pad)
			return -EINVAL;
		err = ovl_ioctl_copy_path(state_args.path_ptr,
					  state_args.path_len, &pathname);
		if (err)
			return err;
		err = ovl_upper_state(dentry->d_sb, pathname, &state_args);
		kfree(pathname);
		if (err)
			return err;
		if (copy_to_user(argp, &state_args, sizeof(state_args)))
			return -EFAULT;
		return 0;
	}
}
