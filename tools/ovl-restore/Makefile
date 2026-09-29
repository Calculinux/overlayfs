# SPDX-License-Identifier: GPL-2.0
# Makefile for ovl-restore tool

CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -Wextra -O2
# The ioctl ABI (include/uapi/linux/overlayfs.h in this repository)
CPPFLAGS += -I$(dir $(lastword $(MAKEFILE_LIST)))../../include/uapi
LDFLAGS =

# Installation directories
PREFIX ?= /usr
DESTDIR ?=

PROG = ovl-restore
OBJS = ovl-restore.o

all: $(PROG)

$(PROG): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(PROG) $(OBJS)

install: $(PROG)
	install -D -m 0755 $(PROG) $(DESTDIR)$(PREFIX)/bin/$(PROG)

.PHONY: all clean install
