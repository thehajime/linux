#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies.
#
# Copyright (C) 2002 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
# Licensed under the GPL
#

# select defconfig based on actual architecture
ifeq ($(SUBARCH),x86)
  ifeq ($(shell uname -m),x86_64)
        KBUILD_DEFCONFIG := x86_64_defconfig
  else
        KBUILD_DEFCONFIG := i386_defconfig
  endif
else
        KBUILD_DEFCONFIG := $(SUBARCH)_defconfig
endif

ARCH_DIR := arch/um
OS := $(shell uname -s)
# We require bash because the vmlinux link and loader script cpp use bash
# features.
SHELL := /bin/bash

MODE_INCLUDE	+= -I$(srctree)/$(ARCH_DIR)/include/shared/skas

HEADER_ARCH 	:= $(SUBARCH)

ifneq ($(filter $(SUBARCH),x86 x86_64 i386),)
	HEADER_ARCH := x86
endif

HOST_DIR := arch/$(HEADER_ARCH)

ifneq ($(filter $(HEADER_ARCH),lkl),)
	HOST_DIR := $(ARCH_DIR)/$(HEADER_ARCH)
endif

include $(ARCH_DIR)/Makefile-skas
include $(HOST_DIR)/Makefile.um

core-y += $(HOST_DIR)/um/

ifneq ($(SUBARCH),lkl)
  include $(ARCH_DIR)/Makefile.um
endif

export HEADER_ARCH SUBARCH
