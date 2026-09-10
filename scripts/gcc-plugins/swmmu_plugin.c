#include "gcc-common.h"
#include "stringpool.h"
#include "diagnostic.h"
#include "attribs.h"

#include "function.h"
#include "basic-block.h"
#include "value-range.h"
#include "tree-ssanames.h"
#include "cgraph.h"

#include "gimplify.h"
#include "gimplify-me.h"
#include "gimple-pretty-print.h"
#include "tree-cfg.h"

#include <cstring>

#define SWMMU_LOAD_NAME  "nommu_swmmu_load_u64"
#define SWMMU_STORE_NAME "nommu_swmmu_store_u64"

int plugin_is_GPL_compatible;

static tree swmmu_load_decl;
static tree swmmu_store_decl;

static tree
find_function_decl(const char *name)
{
	symtab_node *node;

	FOR_EACH_SYMBOL(node) {
		tree decl = node->decl;

		if (!decl || TREE_CODE(decl) != FUNCTION_DECL)
			continue;

		if (!DECL_NAME(decl))
			continue;

		if (!strcmp(IDENTIFIER_POINTER(DECL_NAME(decl)), name))
			return decl;
	}

	return NULL_TREE;
}

static void
protect_runtime_decl(tree decl)
{
	DECL_UNINLINABLE(decl) = 1;
	DECL_PRESERVE_P(decl) = 1;
	TREE_USED(decl) = 1;
}

static void
init_runtime_decls(void)
{
	if (swmmu_load_decl && swmmu_store_decl)
		return;

	swmmu_load_decl = find_function_decl(SWMMU_LOAD_NAME);
	swmmu_store_decl = find_function_decl(SWMMU_STORE_NAME);

	if (swmmu_load_decl)
		protect_runtime_decl(swmmu_load_decl);

	if (swmmu_store_decl)
		protect_runtime_decl(swmmu_store_decl);

	if (!swmmu_load_decl || !swmmu_store_decl) {
		error_at(UNKNOWN_LOCATION,
			"swmmu runtime declarations are missing; "
			"include <linux/nommu_swmmu.h>");
		return;
	}

	fprintf(stderr, "[swmmu] found runtime declarations\n");
}

static tree
handle_swmmu_attribute(tree *node,
		tree name ATTRIBUTE_UNUSED,
		tree args ATTRIBUTE_UNUSED,
		int flags ATTRIBUTE_UNUSED,
		bool *no_add_attrs)
{
	if (TREE_CODE(*node) != FUNCTION_DECL) {
		*no_add_attrs = true;
		warning(OPT_Wattributes,
			"%qE attribute only applies to functions",
			get_identifier("swmmu"));
	}

	return NULL_TREE;
}

static tree
handle_swmmu_ptr_attribute(tree *node,
			   tree name ATTRIBUTE_UNUSED,
			   tree args ATTRIBUTE_UNUSED,
			   int flags ATTRIBUTE_UNUSED,
			   bool *no_add_attrs)
{
	if (!POINTER_TYPE_P(*node)) {
		*no_add_attrs = true;
		warning(OPT_Wattributes,
			"%qE attribute only applies to pointer types",
			get_identifier("swmmu_ptr"));
	}

	return NULL_TREE;
}

static void
register_swmmu_attributes(void *, void *)
{
	static const attribute_spec swmmu_attribute = {
		.name = "swmmu",
		.min_length = 0,
		.max_length = 0,
		.decl_required = true,
		.type_required = false,
		.function_type_required = false,
		.affects_type_identity = false,
		.handler = handle_swmmu_attribute,
		.exclude = nullptr,
	};

	static const attribute_spec swmmu_ptr_attribute = {
		.name = "swmmu_ptr",
		.min_length = 0,
		.max_length = 0,
		.decl_required = false,
		.type_required = true,
		.function_type_required = false,
		.affects_type_identity = true,
		.handler = handle_swmmu_ptr_attribute,
		.exclude = nullptr,
	};

	register_attribute(&swmmu_attribute);
	register_attribute(&swmmu_ptr_attribute);
}


static bool
is_swmmu_function(function *fn)
{
	tree attrs = DECL_ATTRIBUTES(fn->decl);

	return lookup_attribute("swmmu", attrs) != NULL_TREE;
}

static bool
is_swmmu_lvalue(tree expr)
{
	switch (TREE_CODE(expr)) {
	case MEM_REF:
	case TARGET_MEM_REF:
	case COMPONENT_REF:
	case ARRAY_REF:
	case INDIRECT_REF:
		return true;
	default:
		return false;
	}
}

static bool
is_swmmu_pointer_type(tree type)
{
	return type &&
	       POINTER_TYPE_P(type) &&
	       lookup_attribute("swmmu_ptr",
				TYPE_ATTRIBUTES(type)) != NULL_TREE;
}

static bool
is_swmmu_provenance_lvalue(tree expr)
{
	tree address;

	if (!is_swmmu_lvalue(expr))
		return false;

	/*
	 * For the initial provenance step, inspect the pointer used
	 * by the direct memory reference. This covers *p and aliases
	 * such as *alias.
	 */
	switch (TREE_CODE(expr)) {
	case MEM_REF:
	case TARGET_MEM_REF:
	case INDIRECT_REF:
		address = TREE_OPERAND(expr, 0);
		return is_swmmu_pointer_type(TREE_TYPE(address));
	default:
		return false;
	}
}

static tree
swmmu_lvalue_address(tree lvalue)
{
	tree address = build_fold_addr_expr(lvalue);

	return fold_convert(ptr_type_node, address);
}

static bool
supported_access_type(tree type, unsigned HOST_WIDE_INT *size)
{
	if (!INTEGRAL_TYPE_P(type) && !POINTER_TYPE_P(type))
		return false;

	tree size_tree = TYPE_SIZE_UNIT(type);

	if (!size_tree || !tree_fits_uhwi_p(size_tree))
		return false;

	*size = tree_to_uhwi(size_tree);

	return *size == 1 || *size == 2 || *size == 4 || *size == 8;
}

static tree
force_swmmu_operand(gimple_stmt_iterator *gsi, tree expr)
{
	return force_gimple_operand_gsi(gsi,
					expr,
					true,          /* simple_p */
					NULL_TREE,
					true,          /* insert before */
					GSI_SAME_STMT);
}

/*
 * Rewrite:
 *
 *     lhs = MEM_REF(...)
 *
 * into:
 *
 *     tmp = swmmu_load_u64(address, size);
 *     lhs = (type)tmp;
 */
static bool
rewrite_load(gimple_stmt_iterator *gsi, gassign *stmt)
{
	tree mem = gimple_assign_rhs1(stmt);
	tree type = TREE_TYPE(mem);
	unsigned HOST_WIDE_INT size;

	fprintf(stderr, "[swmmu] rewriting load\n");
	if (!is_swmmu_lvalue(mem))
		return false;

	if (gimple_has_volatile_ops(stmt)) {
		error_at(gimple_location(stmt),
			"unsupported volatile access in SWMMU function");
		return false;
	}

	if (!supported_access_type(type, &size))
		return false;

	tree address = swmmu_lvalue_address(mem);
	address = force_swmmu_operand(gsi, address);
	tree size_arg = build_int_cst(size_type_node, size);

	gcall *call = gimple_build_call(swmmu_load_decl,
					2,
					address,
					size_arg);

	tree loaded = create_tmp_var(uint64_type_node, "swmmu_value");
	gimple_call_set_lhs(call, loaded);

	gimple_set_location(call, gimple_location(stmt));
	gsi_insert_before(gsi, call, GSI_SAME_STMT);

	fprintf(stderr, "[swmmu] generated load: \n");

	tree converted = fold_convert(type, loaded);
	converted = force_swmmu_operand(gsi,
					converted);
	gimple_assign_set_rhs1(stmt, converted);

	return true;
}

/*
 * Rewrite:
 *
 *     MEM_REF(...) = value
 *
 * into:
 *
 *     swmmu_store_u64(address, size, (uint64_t)value);
 */
static bool
rewrite_store(gimple_stmt_iterator *gsi, gassign *stmt)
{
	tree mem = gimple_assign_lhs(stmt);
	tree value = gimple_assign_rhs1(stmt);
	tree type = TREE_TYPE(mem);
	unsigned HOST_WIDE_INT access_size;
	unsigned HOST_WIDE_INT value_size;

	fprintf(stderr, "[swmmu] rewriting store\n");
	if (!is_swmmu_lvalue(mem))
		return false;

	if (gimple_has_volatile_ops(stmt)) {
		error_at(gimple_location(stmt),
			"unsupported volatile access in SWMMU function");
		return false;
	}

	if (!supported_access_type(type, &access_size))
		return false;

	/*
	 * A direct MEM_REF RHS must already have been converted by
	 * rewrite_load(). Otherwise this would accidentally pass the
	 * MEM_REF tree itself as a scalar value.
	 */
	if (TREE_CODE(value) == MEM_REF)
		return false;

	if (!supported_access_type(TREE_TYPE(value), &value_size))
		return false;

	if (value_size != access_size)
		return false;

	tree address = swmmu_lvalue_address(mem);
	address = force_swmmu_operand(gsi, address);

	tree size_arg = build_int_cst(size_type_node, access_size);
	tree value_arg = fold_convert(uint64_type_node, value);
	value_arg = force_swmmu_operand(gsi, value_arg);

	gcall *call = gimple_build_call(swmmu_store_decl,
					3,
					address,
					size_arg,
					value_arg);

	gimple_set_location(call, gimple_location(stmt));
	gsi_replace(gsi, call, true);

	fprintf(stderr, "[swmmu] generated store: \n");

	return true;
}

static bool
function_has_swmmu_access(function *fn)
{
	basic_block bb;

	FOR_ALL_BB_FN(bb, fn) {
		for (gimple_stmt_iterator gsi = gsi_start_bb(bb);
		     !gsi_end_p(gsi);
		     gsi_next(&gsi)) {
			gimple stmt = gsi_stmt(gsi);
			tree lhs;
			tree rhs;

			if (gimple_code(stmt) != GIMPLE_ASSIGN)
				continue;

			lhs = gimple_assign_lhs(stmt);
			rhs = gimple_assign_rhs1(stmt);

			if (is_swmmu_provenance_lvalue(lhs) ||
			    is_swmmu_provenance_lvalue(rhs))
				return true;
		}
	}

	return false;
}


namespace {

	const pass_data swmmu_pass_data = {
		GIMPLE_PASS,
		"swmmu",
		OPTGROUP_NONE,
		TV_NONE,
		PROP_cfg,
		0,
		0,
		0,
		TODO_rebuild_cgraph_edges
	};

	class swmmu_pass : public gimple_opt_pass {
public:
		explicit swmmu_pass(gcc::context *ctx)
			: gimple_opt_pass(swmmu_pass_data, ctx)
		{
		}

		unsigned int execute(function *fn) override
		{
			const char *name = IDENTIFIER_POINTER(DECL_NAME(fn->decl));

			bool function_marked = is_swmmu_function(fn);
			if (!function_marked &&
			    !function_has_swmmu_access(fn))
				return 0;

			init_runtime_decls();

			if (!swmmu_load_decl || !swmmu_store_decl) {
				fprintf(stderr, "[swmmu] runtime declarations unavailable\n");
				return 0;
			}

			basic_block bb;

			FOR_ALL_BB_FN(bb, fn) {
				for (gimple_stmt_iterator gsi = gsi_start_bb(bb);
				     !gsi_end_p(gsi);) {
					gimple generic_stmt = gsi_stmt(gsi);

					if (gimple_code(generic_stmt) != GIMPLE_ASSIGN) {
						gsi_next(&gsi);
						continue;
					}

					gassign *stmt = as_a_gassign(generic_stmt);
					tree lhs = gimple_assign_lhs(stmt);
					tree rhs = gimple_assign_rhs1(stmt);

					if (function_marked ||
					    is_swmmu_provenance_lvalue(rhs))
						rewrite_load(&gsi, stmt);

					if (function_marked ||
					    is_swmmu_provenance_lvalue(lhs))
						rewrite_store(&gsi, stmt);

					gsi_next(&gsi);
				}
			}

			fprintf(stderr, "[swmmu] leave %s\n", name);
			bool invalid = verify_gimple_in_cfg(cfun, true, true);

			fprintf(stderr,
				"[swmmu] GIMPLE verification for %s: %s\n",
				name,
				invalid ? "FAILED" : "OK");

			return TODO_rebuild_cgraph_edges;
		}
		opt_pass *clone() override
		{
			return new swmmu_pass(m_ctxt);
		}
	};

} /* anonymous namespace */

int
plugin_init(struct plugin_name_args *plugin_info,
	struct plugin_gcc_version *version)
{
	if (!plugin_default_version_check(version, &gcc_version))
		return 1;

	register_callback(plugin_info->base_name,
			PLUGIN_ATTRIBUTES,
			register_swmmu_attributes,
			NULL);

	static register_pass_info pass_info;

	pass_info.pass = new swmmu_pass(g);
	pass_info.reference_pass_name = "*build_cgraph_edges";
	pass_info.ref_pass_instance_number = 1;
	pass_info.pos_op = PASS_POS_INSERT_AFTER;

	register_callback(plugin_info->base_name,
			PLUGIN_PASS_MANAGER_SETUP,
			NULL,
			&pass_info);

	return 0;
}
