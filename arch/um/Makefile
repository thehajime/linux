# SPDX-License-Identifier: GPL-2.0

ARCH_DIR := arch/um

# select mode of UML build
UMMODE ?= kernel
include $(ARCH_DIR)/lkl/auto.conf

ifeq ($(UMMODE),kernel)
	include $(ARCH_DIR)/Makefile.um
else ifeq ($(UMMODE),library)
	include $(ARCH_DIR)/lkl/Makefile
endif

export UMMODE HEADER_ARCH HOST_DIR SRCARCH
