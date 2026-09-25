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

ALL_ACCESS_SOURCE := $(TEST_DIR)/swmmu_all_access.c
ALL_ACCESS_ASM := $(OUTPUT)/swmmu_all_access.s


.PHONY: all test compile dump dump-unknown-boundary clean all-access
.PHONY: $(filter test-negative%,$(MAKECMDGOALS))

define check_swmmu_asm
	@awk -v symbol="$(1)" -v needle="$(2)" '\
		$$0 ~ "^" symbol ":" { in_function = 1; found = 0 } \
		in_function && index($$0, needle) { found = 1 } \
		in_function && $$0 ~ "\\.size[[:space:]]+" symbol { \
			if (!found) exit 1; \
			exit 0; \
		} \
		END { if (!found) exit 1 }' \
		"$(ALL_ACCESS_ASM)" || { echo "error: $(3)"; exit 1; }
endef

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
	check_native ordinary_memcpy; \
	check_native ordinary_memmove; \
	check_native ordinary_memset; \
	check_call function_marked_load nommu_swmmu_load_dynamic; \
	check_call function_marked_store nommu_swmmu_store_dynamic; \
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
	check_call swmmu_contract_memcpy nommu_swmmu_memcpy; \
	check_call swmmu_contract_memmove nommu_swmmu_memmove; \
	check_call swmmu_contract_memset nommu_swmmu_memset; \
	check_call load_from_contract_malloc nommu_swmmu_load_dynamic; \
	check_call load_from_contract_malloc_alias nommu_swmmu_load_dynamic; \
	check_call dynamic_or_ordinary_load nommu_swmmu_load_dynamic; \
	check_call store_to_contract_malloc nommu_swmmu_store_dynamic; \
	check_call load_from_malloc nommu_swmmu_load_dynamic; \
	check_call store_to_malloc nommu_swmmu_store_dynamic; \
	check_call load_from_calloc nommu_swmmu_load_dynamic; \
	check_call load_from_realloc nommu_swmmu_load_dynamic; \
	check_call load_from_realloc_swmmu nommu_swmmu_load_dynamic; \
	check_call load_from_realloc_ordinary nommu_swmmu_load_dynamic; \
	check_call svm_plain_pointer_load nommu_swmmu_load_dynamic; \
	check_call svm_plain_pointer_store nommu_swmmu_store_dynamic; \
	check_call svm_plain_field_load nommu_swmmu_load_dynamic; \
	check_call svm_plain_field_store nommu_swmmu_store_dynamic; \
	check_call svm_plain_array_load nommu_swmmu_load_dynamic; \
	check_call svm_plain_array_store nommu_swmmu_store_dynamic; \
	check_call svm_plain_cross_function_load svm_plain_pointer_load; \
	check_call svm_cross_store nommu_swmmu_store_dynamic; \
	check_call svm_cross_load nommu_swmmu_load_dynamic; \
	check_call svm_cross_function_field_load svm_cross_store; \
	check_call svm_cross_function_field_load svm_cross_load; \
	check_call svm_unknown_return_load nommu_swmmu_load_dynamic; \
	check_call svm_unknown_return_store nommu_swmmu_store_dynamic; \
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

test-negative-mixedphi:
	@rm -f "$(UNKNOWN_LOG)" "$(UNKNOWN_ASM)"
	@mkdir -p "$(DUMP_DIR_ABS)"
	@if $(CC) $(CFLAGS) -fno-inline -fno-lto \
		-DSWMMU_TEST_MIXED_PHI \
		-fplugin="$(PLUGIN_ABS)" \
		-S -c "$(UNKNOWN_SOURCE_ABS)" \
		-o "$(UNKNOWN_ASM)" \
		>"$(UNKNOWN_LOG)" 2>&1; then \
		echo "error: mixed-PHI test unexpectedly compiled" >&2; \
		cat "$(UNKNOWN_LOG)" >&2; \
		exit 1; \
	fi
	@if ! grep -q "SWMMU pointer state is unknown" \
		"$(UNKNOWN_LOG)"; then \
		echo "error: expected mixed-PHI diagnostic was not found" >&2; \
		cat "$(UNKNOWN_LOG)" >&2; \
		exit 1; \
	fi
	@echo "ok: unknown SWMMU mixed phi is diagnosed"

test-negative-memops:
	@rm -f "$(UNKNOWN_LOG)" "$(UNKNOWN_ASM)"
	@mkdir -p "$(DUMP_DIR_ABS)"
	@if $(CC) $(CFLAGS) -fno-inline -fno-lto \
		-DSWMMU_TEST_MEMOPS \
		-fplugin="$(PLUGIN_ABS)" \
		-S -c "$(UNKNOWN_SOURCE_ABS)" \
		-o "$(UNKNOWN_ASM)" \
		>"$(UNKNOWN_LOG)" 2>&1; then \
		echo "error: SWMMU memory-operation test unexpectedly compiled" \
			>&2; \
		cat "$(UNKNOWN_LOG)" >&2; \
		exit 1; \
	fi
	@if ! grep -q \
		"SWMMU pointer state is unknown at an unannotated function boundary" \
		"$(UNKNOWN_LOG)"; then \
		echo "error: expected memory-operation diagnostic was not found" \
			>&2; \
		cat "$(UNKNOWN_LOG)" >&2; \
		exit 1; \
	fi
	@echo "ok: unannotated SWMMU memory operations are diagnosed"

test-negative-realloc:
	@rm -f "$(UNKNOWN_LOG)" "$(UNKNOWN_ASM)"
	@mkdir -p "$(DUMP_DIR_ABS)"
	@if $(CC) $(CFLAGS) -fno-inline -fno-lto \
		-DSWMMU_TEST_REALLOC_UNKNOWN \
		-fplugin="$(PLUGIN_ABS)" \
		-S -c "$(UNKNOWN_SOURCE_ABS)" \
		-o "$(UNKNOWN_ASM)" \
		>"$(UNKNOWN_LOG)" 2>&1; then \
		echo "error: SWMMU realloc test unexpectedly compiled" \
			>&2; \
		cat "$(UNKNOWN_LOG)" >&2; \
		exit 1; \
	fi
	@if ! grep -q \
		"SWMMU pointer state is unknown at realloc boundary" \
		"$(UNKNOWN_LOG)"; then \
		echo "error: expected realloc diagnostic was not found" \
			>&2; \
		cat "$(UNKNOWN_LOG)" >&2; \
		exit 1; \
	fi
	@echo "ok: unannotated SWMMU realloc are diagnosed"

$(ALL_ACCESS_ASM): $(ALL_ACCESS_SOURCE) $(PLUGIN)
	@mkdir -p $(OUTPUT)
	$(CC) $(CFLAGS) -fno-inline -fno-builtin \
		-fplugin=$(PLUGIN) \
		-fplugin-arg-swmmu_plugin-all-access=1 \
		-S -c $(ALL_ACCESS_SOURCE) \
		-o $(ALL_ACCESS_ASM)

all-access: $(ALL_ACCESS_ASM)
	@grep -A8 'swmmu_all_access_load:' $(ALL_ACCESS_ASM) \
		| grep -q 'nommu_swmmu_load_dynamic' || \
		{ echo "error: all-access load was not lowered"; exit 1; }
	@grep -A8 'swmmu_all_access_store:' $(ALL_ACCESS_ASM) \
		| grep -q 'nommu_swmmu_store_dynamic' || \
		{ echo "error: all-access store was not lowered"; exit 1; }
	@grep -A8 'swmmu_all_access_field:' $(ALL_ACCESS_ASM) \
		| grep -q 'nommu_swmmu_load_dynamic' || \
		{ echo "error: all-access field was not lowered"; exit 1; }
	@grep -A8 'swmmu_all_access_array:' $(ALL_ACCESS_ASM) \
		| grep -q 'nommu_swmmu_store_dynamic' || \
		{ echo "error: all-access array was not lowered"; exit 1; }
	@grep -A24 'swmmu_all_access_alias_load:' $(ALL_ACCESS_ASM) \
		| grep -q 'nommu_swmmu_load_dynamic' || \
		{ echo "error: all-access alias load was not lowered"; exit 1; }
	@grep -A24 'swmmu_all_access_alias_store:' $(ALL_ACCESS_ASM) \
		| grep -q 'nommu_swmmu_store_dynamic' || \
		{ echo "error: all-access alias store was not lowered"; exit 1; }
	@grep -A24 'swmmu_all_access_store_field:' $(ALL_ACCESS_ASM) \
		| grep -q 'nommu_swmmu_store_dynamic' || \
		{ echo "error: all-access field store was not lowered"; exit 1; }
	@grep -A24 'swmmu_all_access_load_field:' $(ALL_ACCESS_ASM) \
		| grep -q 'nommu_swmmu_load_dynamic' || \
		{ echo "error: all-access field load was not lowered"; exit 1; }
	@grep -A32 'swmmu_all_access_cross_function:' $(ALL_ACCESS_ASM) \
		| grep -q 'swmmu_all_access_load_field' || \
		{ echo "error: all-access cross-function call is missing"; exit 1; }
	@grep -A24 'swmmu_all_access_unknown_load:' $(ALL_ACCESS_ASM) \
		| grep -q 'nommu_swmmu_load_dynamic' || \
		{ echo "error: all-access unknown load was not lowered"; exit 1; }
	@grep -A24 'swmmu_all_access_unknown_store:' $(ALL_ACCESS_ASM) \
		| grep -q 'nommu_swmmu_store_dynamic' || \
		{ echo "error: all-access unknown store was not lowered"; exit 1; }
	@grep -A24 'swmmu_all_access_memcpy:' $(ALL_ACCESS_ASM) \
		| grep -q 'nommu_swmmu_memcpy' || \
		{ echo "error: all-access memcpy was not lowered"; exit 1; }
	@grep -A24 'swmmu_all_access_memmove:' $(ALL_ACCESS_ASM) \
		| grep -q 'nommu_swmmu_memmove' || \
		{ echo "error: all-access memmove was not lowered"; exit 1; }
	@grep -A24 'swmmu_all_access_memset:' $(ALL_ACCESS_ASM) \
		| grep -q 'nommu_swmmu_memset' || \
		{ echo "error: all-access memset was not lowered"; exit 1; }
	@grep -A24 'swmmu_all_access_struct_copy:' \
		$(ALL_ACCESS_ASM) \
		| grep -q 'nommu_swmmu_memcpy_dynamic' || \
		{ echo "error: aggregate copy was not lowered"; exit 1; }
	@awk '\
		/^swmmu_all_access_aggregate_call_argument:/{inside=1} \
		inside{print} \
		inside && /\.size[[:space:]]+swmmu_all_access_aggregate_call_argument,/{exit} \
		' $(ALL_ACCESS_ASM) \
		| grep -q 'nommu_swmmu_memcpy_dynamic' || \
		{ echo "error: aggregate call argument was not lowered"; exit 1; }
	$(call check_swmmu_asm,swmmu_global_store,nommu_swmmu_store_dynamic,global store was not lowered)
	$(call check_swmmu_asm,swmmu_global_load,nommu_swmmu_load_dynamic,global load was not lowered)
	$(call check_swmmu_asm,swmmu_global_pointer_store,nommu_swmmu_store_dynamic,global pointer store was not lowered)
	$(call check_swmmu_asm,swmmu_global_pointer_load,nommu_swmmu_load_dynamic,global pointer load was not lowered)
	$(call check_swmmu_asm,swmmu_const_hack_store,nommu_swmmu_store_dynamic,const-hack store was not lowered)
	$(call check_swmmu_asm,swmmu_const_hack_load,nommu_swmmu_load_dynamic,const-hack load was not lowered)
	$(call check_swmmu_asm,swmmu_const_hack_asm_store,nommu_swmmu_store_dynamic,const-hack asm store was not lowered)
	$(call check_swmmu_asm,swmmu_set_global_pointer,nommu_swmmu_store_dynamic,global pointer initialization was not lowered)
	$(call check_swmmu_asm,swmmu_load_global_pointer_value,nommu_swmmu_load_dynamic,global pointer dereference was not lowered)
	@echo "ok: SWMMU all-access baseline lowering"

ALL_ACCESS_BUILTIN_ASM := \
	$(OUTPUT)/swmmu_all_access_builtin.s

$(ALL_ACCESS_BUILTIN_ASM): $(ALL_ACCESS_SOURCE) $(PLUGIN)
	@mkdir -p $(OUTPUT)
	$(CC) $(CFLAGS) -fno-inline \
		-fplugin=$(PLUGIN) \
		-fplugin-arg-swmmu_plugin-all-access=1 \
		-S -c $(ALL_ACCESS_SOURCE) \
		-o $(ALL_ACCESS_BUILTIN_ASM)

.PHONY: all-access-builtins

all-access-builtins: $(ALL_ACCESS_BUILTIN_ASM)
	@grep -q 'nommu_swmmu_memcpy' \
		$(ALL_ACCESS_BUILTIN_ASM) || \
		{ echo "error: builtin memcpy was not lowered"; exit 1; }
	@grep -q 'nommu_swmmu_memmove' \
		$(ALL_ACCESS_BUILTIN_ASM) || \
		{ echo "error: builtin memmove was not lowered"; exit 1; }
	@grep -q 'nommu_swmmu_memset' \
		$(ALL_ACCESS_BUILTIN_ASM) || \
		{ echo "error: builtin memset was not lowered"; exit 1; }
	@echo "ok: SWMMU all-access builtin memory lowering"

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
