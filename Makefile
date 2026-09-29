# SPDX-License-Identifier: GPL-2.0-only
#
#   make KERNEL_SRC=<kernel build dir> [modules_install INSTALL_MOD_PATH=...]
#
# See Kbuild for the overlayfs feature options.

KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build
SRC := $(shell pwd)

all:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC)

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) modules_install

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) clean

.PHONY: all modules_install clean
