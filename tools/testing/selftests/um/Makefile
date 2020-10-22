# SPDX-License-Identifier: GPL-2.0-only

top_srcdir = ../../../../
top_objdir = $(OUTPUT)/../../

CFLAGS := -fPIC -I$(top_objdir)/tools/um/include/ -I$(top_srcdir)/tools/um/include/ -g
CFLAGS += test.c cla.c
LDFLAGS := -pie
LDLIBS := -L$(top_objdir)/tools/um -L$(top_srcdir)/tools/um -llinux -lpthread -lrt -lutil

TEST_GEN_PROGS := boot
TEST_PROGS := disk-ext4.sh disk-vfat.sh
TEST_PROGS_EXTENDED := test.sh disk.sh
TEST_GEN_FILES := disk

include ../lib.mk
