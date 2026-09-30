# SPDX-License-Identifier: GPL-2.0-only
#
# Out-of-tree build of overlay.ko. The target kernel must be configured
# with CONFIG_OVERLAY_FS=n so this module is the only overlayfs.

# Take the object list from overlayfs/Makefile but build the module here,
# so the result does not depend on CONFIG_OVERLAY_FS in the kernel config.
include $(src)/overlayfs/Makefile
ovl-objs := $(addprefix overlayfs/,$(overlay-objs))
overlay-objs :=
obj-y :=
obj-m := overlay.o
overlay-y := $(ovl-objs)

# CONFIG_OVERLAY_FS_* options are absent from a kernel built without
# overlayfs, so they are set here instead (y or n). The defaults match
# the Luckfox Lyra kernel config, so the emulator mounts behave like a
# device's.
OVERLAY_FS_REDIRECT_DIR ?= y
OVERLAY_FS_REDIRECT_ALWAYS_FOLLOW ?= y
OVERLAY_FS_INDEX ?= y
OVERLAY_FS_NFS_EXPORT ?= n
OVERLAY_FS_XINO_AUTO ?= n
OVERLAY_FS_METACOPY ?= y
OVERLAY_FS_DEBUG ?= n

ovl-options := REDIRECT_DIR REDIRECT_ALWAYS_FOLLOW INDEX NFS_EXPORT \
	       XINO_AUTO METACOPY DEBUG

# 6.12's namei.c includes "../internal.h" (fs/internal.h, for
# vfs_path_lookup); resolve it against the kernel being built for.
ccflags-y += -I$(srctree)/fs/overlayfs
ccflags-y += $(foreach o,$(ovl-options),\
	$(if $(filter y,$(OVERLAY_FS_$(o))),-DCONFIG_OVERLAY_FS_$(o)=1))
