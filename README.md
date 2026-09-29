# Calculinux overlayfs

overlayfs built as an out-of-tree `overlay.ko`, carrying the Calculinux
upper-layer ioctls (`OVL_IOC_UPPER_STATE`, `OVL_IOC_IS_RESTORABLE`,
`OVL_IOC_RESTORE_LOWER`) that `calculinux-update` uses to reconcile
packages installed in the overlay with a new base image. See
[docs/ioctl.md](docs/ioctl.md).

## Branches

`main` holds what does not depend on the kernel version:

- `include/uapi/linux/overlayfs.h`: the ioctl ABI
- `tools/ovl-restore/`: command-line tool for the ioctls
- `docs/ioctl.md`: how the ioctls behave

The module itself lives on one branch per kernel series. The first commit
on each is the unmodified `fs/overlayfs` from the kernel it targets, and
the Calculinux changes are the commits on top, so `git log -p <import>..`
shows the whole delta.

| Branch | Kernel | Used by |
|---|---|---|
| `linux-6.1` | Calculinux/luckfox-linux-6.1-rk3506 (Rockchip 6.1) | `luckfox-lyra` |
| `linux-6.12` | linux-yocto v6.12.38 (walnascar) | `calculinux-qemuarm` / calculinux-emulator |

Each module branch carries a copy of the uapi header; keep it identical to
the one on `main`.

## ovl-restore

```bash
make -C tools/ovl-restore
ovl-restore --state /etc /etc/hostname      # path, none|whiteout|upper, flags, mode
ovl-restore --test /usr /usr/bin/foo
ovl-restore /usr /usr/bin/foo
```
