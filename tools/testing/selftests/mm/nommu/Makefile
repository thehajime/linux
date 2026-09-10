# SPDX-License-Identifier: GPL-2.0
# Makefile for mm/nommu selftests

TEST_GEN_PROGS += nommu_mmap_test
TEST_GEN_PROGS += nommu_mremap_test
TEST_GEN_PROGS += nommu_swmmu_test

$(OUTPUT)/nommu_swmmu_test: nommu_swmmu_test.c nommu_swmmu_user.c
$(OUTPUT)/nommu_swmmu_test: CFLAGS += \
	-I$(top_srcdir) $(KHDR_INCLUDES) $(TOOLS_INCLUDES)

SWMMU_PLUGIN ?=

ifneq ($(strip $(SWMMU_PLUGIN)),)
$(OUTPUT)/nommu_swmmu_test: CFLAGS += -fplugin=$(SWMMU_PLUGIN)
endif

include ../../lib.mk
