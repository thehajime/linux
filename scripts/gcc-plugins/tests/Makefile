# SPDX-License-Identifier: GPL-2.0
#
# Standalone compile-only tests for the SWMMU GCC plugin.

SHELL := /bin/sh

TEST_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
SRCTREE := $(abspath $(TEST_DIR)/../../..)

CC ?= gcc
CFLAGS ?= -O2

PLUGIN ?= $(SRCTREE)/build/scripts/gcc-plugins/swmmu_plugin.so
OUTPUT ?= $(SRCTREE)/build/scripts/gcc-plugins/tests

TEST_SOURCE := $(TEST_DIR)/swmmu_provenance.c
TEST_ASM := $(OUTPUT)/swmmu_provenance.s

.PHONY: all test compile clean

all: test

test: $(TEST_ASM)
	@set -eu; \
	extract_function() { \
		awk -v fn="$$1" ' \
			$$0 == fn ":" { in_fn = 1; next } \
			in_fn && $$0 ~ /^[[:space:]]*\.size[[:space:]]/ { exit } \
			in_fn { print } \
		' "$(TEST_ASM)"; \
	}; \
	check_call() { \
		fn="$$1"; \
		symbol="$$2"; \
		body="$$(extract_function "$$fn")"; \
		if test -z "$$body"; then \
			echo "error: function $$fn was not found" >&2; \
			exit 1; \
		fi; \
		if ! printf '%s\n' "$$body" | grep -q "$$symbol"; then \
			echo "error: $$fn does not call $$symbol" >&2; \
			printf '%s\n' "$$body" >&2; \
			exit 1; \
		fi; \
		echo "ok: $$fn calls $$symbol"; \
	}; \
	check_native() { \
		fn="$$1"; \
		body="$$(extract_function "$$fn")"; \
		if test -z "$$body"; then \
			echo "error: function $$fn was not found" >&2; \
			exit 1; \
		fi; \
		if printf '%s\n' "$$body" | grep -q 'nommu_swmmu_'; then \
			echo "error: $$fn unexpectedly calls SWMMU runtime" >&2; \
			printf '%s\n' "$$body" >&2; \
			exit 1; \
		fi; \
		echo "ok: $$fn remains native"; \
	}; \
	check_native ordinary_load; \
	check_call function_marked_load nommu_swmmu_load_u64; \
	check_call function_marked_store nommu_swmmu_store_u64; \
	check_call typed_load nommu_swmmu_load_u64; \
	check_call typed_alias_load nommu_swmmu_load_u64; \
	check_call typed_store nommu_swmmu_store_u64; \
	check_call typed_index_load nommu_swmmu_load_u64; \
	check_call typed_index_store nommu_swmmu_store_u64; \
	check_call typed_cast_load nommu_swmmu_load_u64; \
	check_call typed_load_through_return nommu_swmmu_load_u64; \
	check_call typed_struct_load nommu_swmmu_load_u64; \
	check_call typed_struct_store nommu_swmmu_store_u64; \
	check_call typed_pointer_array_load nommu_swmmu_load_u64

compile: $(TEST_ASM)

$(TEST_ASM): $(TEST_SOURCE) $(PLUGIN)
	@mkdir -p "$(OUTPUT)"
	$(CC) $(CFLAGS) -fno-inline -fno-lto \
		-fplugin="$(PLUGIN)" \
		-S -c "$(TEST_SOURCE)" \
		-o "$@"

clean:
	rm -f "$(TEST_ASM)"
