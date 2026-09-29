# Calculinux overlayfs module

This branch builds overlayfs as an out-of-tree `overlay.ko` with the
Calculinux upper-layer ioctls, for the kernel series in the branch name.
The ioctl ABI, the `ovl-restore` tool and the documentation are on the
`main` branch; the list of branches is in its README.

The first commit on this branch is the unmodified `fs/overlayfs` from the
target kernel; `git log -p <import>..` shows the Calculinux delta.

## Layout

- `overlayfs/`: `fs/overlayfs` from the target kernel, plus `ioctl.c`
- `include/uapi/linux/overlayfs.h`: copy of the ABI header on `main`
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
3. Build against the new kernel and run
   `.github/scripts/qemu-overlay-test.py` from meta-calculinux in the
   emulator (or on the device for kernels the emulator does not run).
