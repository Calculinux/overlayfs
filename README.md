# Calculinux overlayfs

overlayfs built as an out-of-tree `overlay.ko`, carrying the Calculinux
upper-layer ioctls (`OVL_IOC_UPPER_STATE`, `OVL_IOC_IS_RESTORABLE`,
`OVL_IOC_RESTORE_LOWER`) that `calculinux-update` uses to reconcile
packages installed in the overlay with a new base image.

Each branch tracks one kernel series. The first commit on a branch is the
unmodified `fs/overlayfs` from the kernel that branch targets; Calculinux
changes are the commits on top, so `git log -p <import>..` shows the whole
delta.

| Branch | Kernel | Used by |
|---|---|---|
| `linux-6.12` | linux-yocto v6.12.38 (walnascar) | `calculinux-qemuarm` / calculinux-emulator |

The Luckfox Lyra's 6.1 Rockchip kernel carries the same change in-tree
(Calculinux/luckfox-linux-6.1-rk3506).

## Layout

- `overlayfs/`: `fs/overlayfs` from the target kernel, plus `ioctl.c`
- `include/uapi/linux/overlayfs.h`: the ioctl ABI
- `Kbuild`, `Makefile`: out-of-tree build; `Kbuild` also sets the
  `CONFIG_OVERLAY_FS_*` feature options

## Building

The kernel must have `CONFIG_OVERLAY_FS=n`.

```bash
make KERNEL_SRC=/path/to/kernel/build ARCH=arm CROSS_COMPILE=arm-poky-linux-musleabi-
make KERNEL_SRC=... modules_install INSTALL_MOD_PATH=/path/to/rootfs
```

`mount -t overlay` loads the module on demand (`fs-overlay` alias).

## Updating to a new kernel release

1. Replace `overlayfs/` with the new kernel's `fs/overlayfs` in a commit
   that touches nothing else (on a new branch for a new series).
2. Re-apply the Calculinux commits and fix API drift.
3. Build against the new kernel and run the emulator update tests.

## ABI

See `include/uapi/linux/overlayfs.h`. The ioctls may be issued on any file
or directory of the overlay:

| ioctl | Number |
|---|---|
| `OVL_IOC_RESTORE_LOWER` | `0x40104f01` |
| `OVL_IOC_IS_RESTORABLE` | `0x40104f02` |
| `OVL_IOC_UPPER_STATE` | `0xc0204f03` |
