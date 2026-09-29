# OverlayFS upper-layer ioctls

Calculinux adds three ioctls to overlayfs so package tooling can see and
repair what the upper layer holds for a path:

| ioctl | Number | Purpose |
|---|---|---|
| `OVL_IOC_UPPER_STATE` | `_IOWR('O', 3, struct ovl_upper_state_args)` (`0xc0204f03`) | Report whether the upper layer has nothing, a whiteout or a real entry for a name |
| `OVL_IOC_IS_RESTORABLE` | `_IOW('O', 2, struct ovl_is_restorable_args)` (`0x40104f02`) | Check whether a restore would succeed |
| `OVL_IOC_RESTORE_LOWER` | `_IOW('O', 1, struct ovl_restore_lower_args)` (`0x40104f01`) | Remove a whiteout so the lower entry is visible again |

The definitions live in `include/uapi/linux/overlayfs.h`.

## Motivation

Calculinux keeps user-installed packages in the overlay upper layer on top
of a read-only base image. When a system update moves a package into the
base image, the overlay copy has to go:

1. `opkg remove` deletes the overlay copy's files.
2. overlayfs leaves a whiteout for every file that also exists in the lower
   layer, which hides the new base image's copy.
3. The update tool removes those whiteouts with `OVL_IOC_RESTORE_LOWER`.

Before removing anything, the tool uses `OVL_IOC_UPPER_STATE` to tell
packages whose files really are in the upper layer apart from packages that
only have a status entry.

## Calling convention

- The fd may be any file **or directory** on the overlay; normally the
  mount point opened with `O_RDONLY | O_DIRECTORY`.
- `path_ptr` points to an absolute, null-terminated path inside that same
  overlay and `path_len` is its `strlen()`. The last component must be a
  name (not empty, `.` or `..`). Reserved fields must be 0.
- A path on another filesystem returns `-EXDEV`; an overlay without an
  upper layer returns `-EROFS`.

## How paths are resolved

Looking the full path up through the overlay cannot find a whiteout:
overlay lookup stops at the whiteout and returns a negative dentry with no
upper dentry attached. So every command:

1. resolves the **parent directory** through the overlay (following
   symlinks), and checks it is on the fd's overlay;
2. looks the **final name** up directly in the parent's upper directory,
   with the mounter's credentials;
3. classifies that entry with `ovl_is_whiteout()`.

## OVL_IOC_UPPER_STATE

```c
struct ovl_upper_state_args {
	__aligned_u64 path_ptr;	/* in */
	__u32 path_len;		/* in */
	__u32 flags;		/* in: 0 */
	__u32 state;		/* out: OVL_UPPER_STATE_{NONE,WHITEOUT,UPPER} */
	__u32 state_flags;	/* out: OVL_UPPER_STATE_F_* */
	__u32 mode;		/* out: st_mode of an UPPER entry, else 0 */
	__u32 pad;		/* in: 0 */
};
```

- `NONE`: no upper entry (includes a parent that was never copied up).
- `WHITEOUT`: the upper entry is a whiteout.
- `UPPER`: a real upper entry; `mode` has its type and permissions.
- `OVL_UPPER_STATE_F_OPAQUE`: the entry is an opaque directory.
- `OVL_UPPER_STATE_F_NO_LOWER_DIR`: the parent has no lower directory
  (pure upper, or below an opaque directory), so restoring cannot reveal
  anything.

The caller needs search (`x`) permission on the parent directory.

## OVL_IOC_IS_RESTORABLE

Returns 0 for a whiteout whose parent has a lower directory, `-ENOENT` when
there is no upper entry, `-EEXIST` for a real upper entry and `-ENODATA`
when there is nothing below to reveal.

## OVL_IOC_RESTORE_LOWER

1. Locks the overlay parent directory (as an unlink through the overlay
   would) and checks the caller may write to it.
2. Fails with `-EEXIST` if the name is already visible.
3. Locks the upper directory, looks the name up **again under the lock**,
   and unlinks it only if it is still a whiteout (`-ENOENT` / `-EEXIST`
   otherwise).
4. Drops the cached negative overlay dentry and bumps the directory's
   version so a merged readdir cache is rebuilt (`ls` shows the entry).
5. Looks the name up again and returns `-ENODATA` if no lower entry shows
   through. The whiteout is gone at that point, which is harmless.

## Userspace

### ovl-restore

```bash
ovl-restore --state /etc /etc/hostname /etc/fstab   # path<TAB>state<TAB>flags<TAB>mode
ovl-restore --test /usr /usr/bin/foo
ovl-restore /usr /usr/bin/foo /usr/lib/libbar.so
```

`--state` prints one line per path: `none`, `whiteout` or `upper`, then the
flags (`opaque`, `no-lower-dir` or `-`) and the octal mode.

### Python

```python
import ctypes, fcntl, os

OVL_IOC_UPPER_STATE = 0xC0204F03  # _IOWR('O', 3, 32-byte struct)

class UpperStateArgs(ctypes.Structure):
    _fields_ = [("path_ptr", ctypes.c_uint64), ("path_len", ctypes.c_uint32),
                ("flags", ctypes.c_uint32), ("state", ctypes.c_uint32),
                ("state_flags", ctypes.c_uint32), ("mode", ctypes.c_uint32),
                ("pad", ctypes.c_uint32)]

def upper_state(mount_point, path):
    raw = path.encode()
    buf = ctypes.create_string_buffer(raw)          # keeps the NUL
    args = UpperStateArgs(path_ptr=ctypes.addressof(buf), path_len=len(raw))
    fd = os.open(mount_point, os.O_RDONLY | os.O_DIRECTORY)
    try:
        fcntl.ioctl(fd, OVL_IOC_UPPER_STATE, args)  # mutable: filled in place
    finally:
        os.close(fd)
    return args.state, args.state_flags, args.mode
```

## Testing

```bash
mkdir -p /tmp/o/lower/d /tmp/o/upper /tmp/o/work /tmp/o/merged
echo lower > /tmp/o/lower/d/f
mount -t overlay overlay -o lowerdir=/tmp/o/lower,upperdir=/tmp/o/upper,workdir=/tmp/o/work /tmp/o/merged
M=/tmp/o/merged

ovl-restore --state $M $M/d/f        # none
touch $M/d/new
ovl-restore --state $M $M/d/new      # upper, no-lower-dir is not set (d is merged)
rm $M/d/f
ovl-restore --state $M $M/d/f        # whiteout
ls $M/d                              # f is hidden
ovl-restore $M $M/d/f                # Restored
cat $M/d/f; ls $M/d                  # "lower", and f is listed again
rm $M/d/new; ovl-restore $M $M/d/new # whiteout-free upper-only name: -ENOENT
```

## Upstream

Possible upstream discussion: linux-unionfs@vger.kernel.org (maintainers
Miklos Szeredi, Amir Goldstein).
