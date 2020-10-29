# SPDX-License-Identifier: GPL-2.0-only

top_srcdir = ../../../../
top_objdir = $(OUTPUT)/../../

CFLAGS := -fPIC -I$(top_objdir)/tools/um/include/ -I$(top_srcdir)/tools/um/include/ -g
CFLAGS += test.c
LDFLAGS := -pie
LDLIBS := -L$(top_objdir)/tools/um -L$(top_srcdir)/tools/um -llinux -lpthread -lrt -lutil

TEST_GEN_PROGS := boot

include ../lib.mk
