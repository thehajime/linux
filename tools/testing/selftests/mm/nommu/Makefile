# SPDX-License-Identifier: GPL-2.0
# Makefile for mm/nommu selftests

TEST_GEN_PROGS += nommu_mmap_test
TEST_GEN_PROGS += nommu_mremap_test
TEST_GEN_PROGS += nommu_swmmu_test

$(OUTPUT)/nommu_swmmu_test: nommu_swmmu_test.c nommu_swmmu_user.c
$(OUTPUT)/nommu_swmmu_test: CFLAGS += \
	-I$(top_srcdir) $(KHDR_INCLUDES) $(TOOLS_INCLUDES)


# gcc-plugins
NOMMU_SELFTEST_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

SWMMU_SRCTREE := $(strip $(top_srcdir))
ifeq ($(SWMMU_SRCTREE),)
SWMMU_SRCTREE := $(abspath $(NOMMU_SELFTEST_DIR)/../../../../..)
endif

SWMMU_BUILD_PLUGIN ?= 0
SWMMU_HOSTCXX ?= g++

SWMMU_PLUGIN_SOURCE := $(SWMMU_SRCTREE)/scripts/gcc-plugins/swmmu_plugin.c
SWMMU_PLUGIN_HEADERS := $(wildcard $(SWMMU_SRCTREE)/scripts/gcc-plugins/*.h)

ifeq ($(SWMMU_BUILD_PLUGIN),1)

SWMMU_PLUGIN ?= $(OUTPUT)/swmmu_plugin.so
SWMMU_GCC_PLUGIN_DIR := \
	$(shell $(SWMMU_HOSTCXX) -print-file-name=plugin)
SWMMU_PLUGIN_INCLUDE := \
	$(SWMMU_GCC_PLUGIN_DIR)/include

$(SWMMU_PLUGIN): $(SWMMU_PLUGIN_SOURCE) $(SWMMU_PLUGIN_HEADERS)
	@test -d "$(SWMMU_PLUGIN_INCLUDE)" || { \
		echo "error: GCC plugin headers not found:" \
			"$(SWMMU_PLUGIN_INCLUDE)" >&2; \
		exit 1; \
	}
	@mkdir -p "$(dir $@)"
	$(SWMMU_HOSTCXX) \
		-shared \
		-fPIC \
		-fno-rtti \
		-fno-exceptions \
		-fno-lto \
		-std=gnu++11 \
		-I"$(SWMMU_PLUGIN_INCLUDE)" \
		-I"$(SWMMU_SRCTREE)/scripts/gcc-plugins" \
		-o "$@" \
		"$(SWMMU_PLUGIN_SOURCE)"

$(OUTPUT)/nommu_swmmu_test: $(SWMMU_PLUGIN)

endif

ifneq ($(strip $(SWMMU_PLUGIN)),)
$(OUTPUT)/nommu_swmmu_test: CFLAGS += -fplugin=$(SWMMU_PLUGIN)
endif

.PHONY: swmmu-plugin

swmmu-plugin:
ifeq ($(SWMMU_BUILD_PLUGIN),1)
	$(MAKE) $(SWMMU_PLUGIN)
else
	@echo "set SWMMU_BUILD_PLUGIN=1 to build the SWMMU GCC plugin"
	@false
endif

include ../../lib.mk
