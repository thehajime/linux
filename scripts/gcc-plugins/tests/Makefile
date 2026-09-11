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
DUMP_DIR ?= /tmp/swmmu-provenance
DUMP_DIR_ABS := $(abspath $(DUMP_DIR))
TEST_SOURCE_ABS := $(abspath $(TEST_SOURCE))
PLUGIN_ABS := $(abspath $(PLUGIN))
DUMP_ASM := $(DUMP_DIR_ABS)/swmmu_provenance.s

UNKNOWN_SOURCE := $(TEST_DIR)/swmmu_provenance_unknown.c
UNKNOWN_SOURCE_ABS = $(abspath $(UNKNOWN_SOURCE))
UNKNOWN_LOG := $(DUMP_DIR_ABS)/swmmu_provenance_unknown.log
UNKNOWN_ASM := $(DUMP_DIR_ABS)/swmmu_provenance_unknown.s


.PHONY: all test compile dump dump-unknown-boundary \
	test-negative test-negative-return clean

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
	check_call typed_pointer_array_load nommu_swmmu_load_u64; \
	check_call typed_phi_load nommu_swmmu_load_u64; \
	check_call typed_direct_phi_load nommu_swmmu_load_u64; \
	check_call typed_parameter_caller typed_load;

test-negative:
	@rm -f "$(UNKNOWN_LOG)" "$(UNKNOWN_ASM)"
	@mkdir -p "$(DUMP_DIR_ABS)"
	@if $(CC) $(CFLAGS) -fno-inline -fno-lto \
		-DSWMMU_TEST_UNKNOWN_BOUNDARY \
		-fplugin="$(PLUGIN_ABS)" \
		-S -c "$(UNKNOWN_SOURCE_ABS)" \
		-o "$(UNKNOWN_ASM)" \
		>"$(UNKNOWN_LOG)" 2>&1; then \
		echo "error: unknown-boundary test unexpectedly compiled" >&2; \
		cat "$(UNKNOWN_LOG)" >&2; \
		exit 1; \
	fi
	@if ! grep -q "SWMMU pointer state is unknown" \
		"$(UNKNOWN_LOG)"; then \
		echo "error: expected boundary diagnostic was not found" >&2; \
		cat "$(UNKNOWN_LOG)" >&2; \
		exit 1; \
	fi
	@echo "ok: unknown SWMMU boundary is diagnosed"

test-negative-return:
	@rm -f "$(UNKNOWN_LOG)" "$(UNKNOWN_ASM)"
	@mkdir -p "$(DUMP_DIR_ABS)"
	@if $(CC) $(CFLAGS) -fno-inline -fno-lto \
		-DSWMMU_TEST_UNKNOWN_RETURN \
		-fplugin="$(PLUGIN_ABS)" \
		-S -c "$(UNKNOWN_SOURCE_ABS)" \
		-o "$(UNKNOWN_ASM)" \
		>"$(UNKNOWN_LOG)" 2>&1; then \
		echo "error: unknown-return test unexpectedly compiled" >&2; \
		cat "$(UNKNOWN_LOG)" >&2; \
		exit 1; \
	fi
	@if ! grep -q "SWMMU pointer state is unknown" \
		"$(UNKNOWN_LOG)"; then \
		echo "error: expected return diagnostic was not found" >&2; \
		cat "$(UNKNOWN_LOG)" >&2; \
		exit 1; \
	fi
	@echo "ok: unknown SWMMU return is diagnosed"


compile: $(TEST_ASM)

dump:
	@rm -rf "$(DUMP_DIR_ABS)"
	@mkdir -p "$(DUMP_DIR_ABS)"
	@cd "$(DUMP_DIR_ABS)" && \
		$(CC) $(CFLAGS) -fno-inline -fno-lto \
		-fplugin="$(PLUGIN_ABS)" \
		-fdump-tree-gimple \
		-fdump-tree-ssa \
		-fdump-tree-optimized \
		-S -c "$(TEST_SOURCE_ABS)" \
		-o "$(DUMP_ASM)"

dump-unknown-boundary: dump
	@set -eu; \
	found=0; \
	for file in \
		"$(DUMP_DIR_ABS)"/swmmu_provenance.c.*gimple \
		"$(DUMP_DIR_ABS)"/swmmu_provenance.c.*ssa \
		"$(DUMP_DIR_ABS)"/swmmu_provenance.c.*optimized; do \
		if test -f "$$file"; then \
			found=1; \
			echo "===== $$file ====="; \
			grep -n -A25 -B5 \
				'unknown_boundary_load' "$$file" || true; \
		fi; \
	done; \
	if test "$$found" -eq 0; then \
		echo "error: no compiler dump files found" >&2; \
		exit 1; \
	fi

$(TEST_ASM): $(TEST_SOURCE) $(PLUGIN)
	@mkdir -p "$(OUTPUT)"
	$(CC) $(CFLAGS) -fno-inline -fno-lto \
		-fplugin="$(PLUGIN)" \
		-S -c "$(TEST_SOURCE)" \
		-o "$@"

clean:
	rm -f "$(TEST_ASM)"
