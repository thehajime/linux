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
#include "hash-map.h"

#include <cstring>

#define SWMMU_LOAD_NAME  "nommu_swmmu_load_u64"
#define SWMMU_STORE_NAME "nommu_swmmu_store_u64"
#define SWMMU_MEMCPY_NAME "nommu_swmmu_memcpy"
#define SWMMU_MEMMOVE_NAME "nommu_swmmu_memmove"
#define SWMMU_MEMSET_NAME "nommu_swmmu_memset"

#ifdef SWMMU_PLUGIN_DEBUG
#define debug_print(...) fprintf(__VA_ARGS__)
#else
#define debug_print(...)
#endif

int plugin_is_GPL_compatible;

static tree swmmu_load_decl;
static tree swmmu_store_decl;
static tree swmmu_memcpy_decl;
static tree swmmu_memmove_decl;
static tree swmmu_memset_decl;


enum swmmu_pointer_state {
	SWMMU_POINTER_ORDINARY,
	SWMMU_POINTER_SWMMU,
	SWMMU_POINTER_UNKNOWN,
};

enum swmmu_memop_kind {
	SWMMU_MEMOP_NONE,
	SWMMU_MEMOP_MEMCPY,
	SWMMU_MEMOP_MEMMOVE,
	SWMMU_MEMOP_MEMSET,
};

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

static tree
swmmu_memop_runtime_decl(enum swmmu_memop_kind kind)
{
	switch (kind) {
	case SWMMU_MEMOP_MEMCPY:
		if (!swmmu_memcpy_decl)
			swmmu_memcpy_decl =
				find_function_decl(SWMMU_MEMCPY_NAME);

		if (swmmu_memcpy_decl)
			protect_runtime_decl(swmmu_memcpy_decl);

		return swmmu_memcpy_decl;
	case SWMMU_MEMOP_MEMMOVE:
		if (!swmmu_memmove_decl)
			swmmu_memmove_decl =
				find_function_decl(SWMMU_MEMMOVE_NAME);

		if (swmmu_memmove_decl)
			protect_runtime_decl(swmmu_memmove_decl);

		return swmmu_memmove_decl;
	case SWMMU_MEMOP_MEMSET:
		if (!swmmu_memset_decl)
			swmmu_memset_decl =
				find_function_decl(SWMMU_MEMSET_NAME);

		if (swmmu_memset_decl)
			protect_runtime_decl(swmmu_memset_decl);

		return swmmu_memset_decl;
	default:
		return NULL_TREE;
	}
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

	debug_print(stderr, "[swmmu] found runtime declarations\n");
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

static tree
handle_swmmu_memop_attribute(tree *node,
			     tree name ATTRIBUTE_UNUSED,
			     tree args,
			     int flags ATTRIBUTE_UNUSED,
			     bool *no_add_attrs)
{
	tree value;
	const char *kind;

	if (TREE_CODE(*node) != FUNCTION_DECL) {
		*no_add_attrs = true;
		warning(OPT_Wattributes,
			"%qE attribute only applies to functions",
			get_identifier("swmmu_memop"));
		return NULL_TREE;
	}

	if (!args || TREE_CODE(TREE_VALUE(args)) != STRING_CST) {
		*no_add_attrs = true;
		error_at(DECL_SOURCE_LOCATION(*node),
			 "swmmu_memop requires a string argument");
		return NULL_TREE;
	}

	value = TREE_VALUE(args);
	kind = TREE_STRING_POINTER(value);

	if (strcmp(kind, "memcpy") &&
	    strcmp(kind, "memmove") &&
	    strcmp(kind, "memset")) {
		*no_add_attrs = true;
		error_at(DECL_SOURCE_LOCATION(*node),
			 "unsupported swmmu_memop kind '%s'",
			 kind);
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

	static const attribute_spec swmmu_memop_attribute = {
		.name = "swmmu_memop",
		.min_length = 1,
		.max_length = 1,
		.decl_required = true,
		.type_required = false,
		.function_type_required = false,
		.affects_type_identity = false,
		.handler = handle_swmmu_memop_attribute,
		.exclude = nullptr,
	};

	register_attribute(&swmmu_attribute);
	register_attribute(&swmmu_ptr_attribute);
	register_attribute(&swmmu_memop_attribute);
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

static enum swmmu_pointer_state
swmmu_pointer_state_from_type(tree type)
{
	if (is_swmmu_pointer_type(type))
		return SWMMU_POINTER_SWMMU;

	return SWMMU_POINTER_ORDINARY;
}

static enum swmmu_pointer_state
swmmu_pointer_state_join(enum swmmu_pointer_state first,
			 enum swmmu_pointer_state second)
{
	if (first == second)
		return first;

	if (first == SWMMU_POINTER_UNKNOWN ||
	    second == SWMMU_POINTER_UNKNOWN)
		return SWMMU_POINTER_UNKNOWN;

	return SWMMU_POINTER_UNKNOWN;
}

static enum swmmu_pointer_state
swmmu_pointer_state_of(
	tree expr,
	hash_map<tree, enum swmmu_pointer_state> &states)
{
	enum swmmu_pointer_state *state;
	tree var;

	if (!expr)
		return SWMMU_POINTER_ORDINARY;

	/*
	 * The expression may be a VAR_DECL or an SSA_NAME depending
	 * on the pass stage. Check the exact tree first.
	 */
	state = states.get(expr);
	if (state)
		return *state;

	if (TREE_CODE(expr) == SSA_NAME) {
		var = SSA_NAME_VAR(expr);
		if (var) {
			state = states.get(var);
			if (state)
				return *state;
		}
	}

	if (!TREE_TYPE(expr) ||
	    !POINTER_TYPE_P(TREE_TYPE(expr)))
		return SWMMU_POINTER_ORDINARY;

	if (is_swmmu_pointer_type(TREE_TYPE(expr)))
		return SWMMU_POINTER_SWMMU;

	switch (TREE_CODE(expr)) {
	case POINTER_PLUS_EXPR:
	case PLUS_EXPR:
	case MINUS_EXPR:
	case NOP_EXPR:
	case CONVERT_EXPR:
	case VIEW_CONVERT_EXPR:
		return swmmu_pointer_state_of(TREE_OPERAND(expr, 0),
					      states);

	case COND_EXPR:
		return swmmu_pointer_state_join(
			swmmu_pointer_state_of(TREE_OPERAND(expr, 1),
					       states),
			swmmu_pointer_state_of(TREE_OPERAND(expr, 2),
					       states));

	default:
		return SWMMU_POINTER_ORDINARY;
	}
}

static void
swmmu_pointer_state_put(
	tree expr,
	enum swmmu_pointer_state state,
	hash_map<tree, enum swmmu_pointer_state> &states)
{
	tree var;

	states.put(expr, state);

	if (TREE_CODE(expr) != SSA_NAME)
		return;

	var = SSA_NAME_VAR(expr);
	if (var)
		states.put(var, state);
}

static enum swmmu_pointer_state
swmmu_lvalue_state(
	tree expr,
	hash_map<tree, enum swmmu_pointer_state> &states)
{
	tree address;

	if (!is_swmmu_lvalue(expr))
		return SWMMU_POINTER_ORDINARY;

	switch (TREE_CODE(expr)) {
	case MEM_REF:
	case TARGET_MEM_REF:
	case INDIRECT_REF:
		address = TREE_OPERAND(expr, 0);
		return swmmu_pointer_state_of(address, states);

	default:
		if (is_swmmu_provenance_lvalue(expr))
			return SWMMU_POINTER_SWMMU;

		return SWMMU_POINTER_ORDINARY;
	}
}

static void
swmmu_seed_pointer_states(
	function *fn,
	bool function_marked,
	hash_map<tree, enum swmmu_pointer_state> &states)
{
	tree argument;

	for (argument = DECL_ARGUMENTS(fn->decl);
	     argument;
	     argument = TREE_CHAIN(argument)) {
		tree type = TREE_TYPE(argument);
		enum swmmu_pointer_state state;

		if (!type || !POINTER_TYPE_P(type))
			continue;

		state = function_marked ?
			SWMMU_POINTER_SWMMU :
			swmmu_pointer_state_from_type(type);

		swmmu_pointer_state_put(argument, state, states);
	}
}

static void
swmmu_record_pointer_call(
	gcall *call,
	bool swmmu_context,
	hash_map<tree, enum swmmu_pointer_state> &states)
{
	tree lhs;
	enum swmmu_pointer_state state;

	lhs = gimple_call_lhs(call);
	if (!lhs || !TREE_TYPE(lhs) ||
	    !POINTER_TYPE_P(TREE_TYPE(lhs)))
		return;

	state = swmmu_pointer_state_from_type(TREE_TYPE(lhs));

	if (state == SWMMU_POINTER_ORDINARY && swmmu_context)
		state = SWMMU_POINTER_UNKNOWN;

	swmmu_pointer_state_put(lhs, state, states);
}

static void
swmmu_record_pointer_assignment(
	gassign *stmt,
	hash_map<tree, enum swmmu_pointer_state> &states)
{
	tree lhs;
	tree rhs;
	enum swmmu_pointer_state state;

	lhs = gimple_assign_lhs(stmt);
	if (!lhs || !TREE_TYPE(lhs) ||
	    !POINTER_TYPE_P(TREE_TYPE(lhs)))
		return;

	rhs = gimple_assign_rhs1(stmt);
	state = swmmu_pointer_state_of(rhs, states);

	swmmu_pointer_state_put(lhs, state, states);
}

static enum swmmu_memop_kind
swmmu_memop_kind_of(tree fndecl)
{
	tree attr;
	tree args;
	tree value;
	const char *kind;

	if (!fndecl || TREE_CODE(fndecl) != FUNCTION_DECL)
		return SWMMU_MEMOP_NONE;

	attr = lookup_attribute("swmmu_memop",
				DECL_ATTRIBUTES(fndecl));
	if (!attr)
		return SWMMU_MEMOP_NONE;

	args = TREE_VALUE(attr);
	if (!args || TREE_CODE(TREE_VALUE(args)) != STRING_CST)
		return SWMMU_MEMOP_NONE;

	value = TREE_VALUE(args);
	kind = TREE_STRING_POINTER(value);

	if (!strcmp(kind, "memcpy"))
		return SWMMU_MEMOP_MEMCPY;
	if (!strcmp(kind, "memmove"))
		return SWMMU_MEMOP_MEMMOVE;
	if (!strcmp(kind, "memset"))
		return SWMMU_MEMOP_MEMSET;

	return SWMMU_MEMOP_NONE;
}

static tree
swmmu_call_argument_type(gcall *call, unsigned int index)
{
	tree fndecl;
	tree argtypes;

	fndecl = gimple_call_fndecl(call);
	if (!fndecl)
		return NULL_TREE;

	argtypes = TYPE_ARG_TYPES(TREE_TYPE(fndecl));

	while (argtypes && argtypes != void_list_node) {
		if (!index)
			return TREE_VALUE(argtypes);

		index--;
		argtypes = TREE_CHAIN(argtypes);
	}

	return NULL_TREE;
}

static bool
swmmu_call_has_unannotated_argument(gcall *call)
{
	unsigned int i;

	for (i = 0; i < gimple_call_num_args(call); i++) {
		tree argument = gimple_call_arg(call, i);
		tree argument_type = TREE_TYPE(argument);
		tree formal_type;
		enum swmmu_pointer_state state;

		if (!POINTER_TYPE_P(argument_type))
			continue;

		state = swmmu_pointer_state_from_type(argument_type);
		if (state != SWMMU_POINTER_SWMMU)
			continue;

		formal_type = swmmu_call_argument_type(call, i);
		if (!formal_type ||
		    !is_swmmu_pointer_type(formal_type))
			return true;
	}

	return false;
}

static bool
swmmu_call_has_swmmu_argument(gcall *call)
{
	unsigned int i;

	for (i = 0; i < gimple_call_num_args(call); i++) {
		tree argument = gimple_call_arg(call, i);

		if (!TREE_TYPE(argument) ||
		    !POINTER_TYPE_P(TREE_TYPE(argument)))
			continue;

		if (swmmu_pointer_state_from_type(TREE_TYPE(argument)) ==
		    SWMMU_POINTER_SWMMU)
			return true;
	}

	return false;
}

static bool
swmmu_lower_memop_call(gcall *call,
		       enum swmmu_memop_kind kind)
{
	tree runtime_decl;

	if (kind != SWMMU_MEMOP_MEMCPY &&
		kind != SWMMU_MEMOP_MEMMOVE &&
		kind != SWMMU_MEMOP_MEMSET) {
		error_at(gimple_location(call),
			 "SWMMU memory operation lowering is not "
			 "implemented for this operation");
		return false;
	}

	runtime_decl = swmmu_memop_runtime_decl(kind);
	if (!runtime_decl) {
		error_at(gimple_location(call),
			 "SWMMU memory operation runtime declaration "
			"is missing (kind=%d)", kind);
		return false;
	}

	gimple_call_set_fndecl(call, runtime_decl);

	return true;
}

static bool
check_swmmu_call(gcall *call)
{
	tree fndecl = gimple_call_fndecl(call);

	if (swmmu_memop_kind_of(fndecl) != SWMMU_MEMOP_NONE &&
		swmmu_call_has_swmmu_argument(call))
		return swmmu_lower_memop_call(
			call,
			swmmu_memop_kind_of(fndecl));

	if (!swmmu_call_has_unannotated_argument(call))
		return true;

	error_at(gimple_location(call),
		 "SWMMU pointer state is unknown at an "
		 "unannotated function boundary");

	return false;
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

	debug_print(stderr, "[swmmu] rewriting load\n");
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

	tree loaded = make_temp_ssa_name(uint64_type_node,
					call, "swmmu_value");
	gimple_call_set_lhs(call, loaded);

	gimple_set_location(call, gimple_location(stmt));
	gsi_insert_before(gsi, call, GSI_SAME_STMT);

	debug_print(stderr, "[swmmu] generated load: \n");

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

	debug_print(stderr, "[swmmu] rewriting store\n");
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

	debug_print(stderr, "[swmmu] generated store: \n");

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

static bool
function_has_swmmu_parameter(function *fn)
{
	tree argument;

	for (argument = DECL_ARGUMENTS(fn->decl);
	     argument;
	     argument = TREE_CHAIN(argument)) {
		tree type = DECL_ARG_TYPE(argument);

		if (!type)
			type = TREE_TYPE(argument);

		if (is_swmmu_pointer_type(type))
			return true;
	}

	return false;
}

static void
swmmu_record_pointer_phi(
	gphi *phi,
	hash_map<tree, enum swmmu_pointer_state> &states)
{
	tree result;
	enum swmmu_pointer_state state;
	unsigned int i;
	unsigned int count;

	result = gimple_phi_result(phi);
	if (!result || !TREE_TYPE(result) ||
	    !POINTER_TYPE_P(TREE_TYPE(result)))
		return;

	count = gimple_phi_num_args(phi);
	if (!count) {
		state = swmmu_pointer_state_from_type(TREE_TYPE(result));
		swmmu_pointer_state_put(result, state, states);
		return;
	}

	state = swmmu_pointer_state_of(
		gimple_phi_arg_def(phi, 0), states);

	for (i = 1; i < count; i++)
		state = swmmu_pointer_state_join(
			state,
			swmmu_pointer_state_of(
				gimple_phi_arg_def(phi, i),
				states));

	swmmu_pointer_state_put(result, state, states);
}


namespace {

	const pass_data swmmu_pass_data = {
		GIMPLE_PASS,
		"swmmu",
		OPTGROUP_NONE,
		TV_NONE,
		PROP_cfg | PROP_ssa,
		0,
		0,
		0,
		TODO_update_ssa | TODO_rebuild_cgraph_edges
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
			    !function_has_swmmu_access(fn) &&
			    !function_has_swmmu_parameter(fn))
				return 0;

			bool swmmu_context;
			hash_map<tree, enum swmmu_pointer_state> states;

			swmmu_context = function_marked ||
				function_has_swmmu_parameter(fn);

			swmmu_seed_pointer_states(fn, function_marked, states);

			init_runtime_decls();

			if (!swmmu_load_decl || !swmmu_store_decl) {
				debug_print(stderr, "[swmmu] runtime declarations unavailable\n");
				return 0;
			}

			basic_block bb;

			FOR_ALL_BB_FN(bb, fn) {
				for (gphi_iterator psi = gsi_start_phis(bb);
				     !gsi_end_p(psi);
				     gsi_next(&psi)) {
					swmmu_record_pointer_phi(psi.phi(), states);
				}

				for (gimple_stmt_iterator gsi = gsi_start_bb(bb);
				     !gsi_end_p(gsi);) {
					gimple generic_stmt = gsi_stmt(gsi);

					if (gimple_code(generic_stmt) == GIMPLE_CALL) {
						gcall *call = as_a_gcall(generic_stmt);

						check_swmmu_call(call);
						swmmu_record_pointer_call(call, swmmu_context, states);

						gsi_next(&gsi);
						continue;
					}

					if (gimple_code(generic_stmt) != GIMPLE_ASSIGN) {
						gsi_next(&gsi);
						continue;
					}

					gassign *stmt = as_a_gassign(generic_stmt);
					tree lhs = gimple_assign_lhs(stmt);
					tree rhs = gimple_assign_rhs1(stmt);
					enum swmmu_pointer_state rhs_state;
					enum swmmu_pointer_state lhs_state;

					rhs_state = swmmu_lvalue_state(rhs, states);
					lhs_state = swmmu_lvalue_state(lhs, states);

					if (rhs_state == SWMMU_POINTER_UNKNOWN) {
						error_at(gimple_location(stmt),
							"SWMMU pointer state is unknown at dereference");
					} else if (function_marked ||
						rhs_state == SWMMU_POINTER_SWMMU) {
						rewrite_load(&gsi, stmt);
					}

					if (lhs_state == SWMMU_POINTER_UNKNOWN) {
						error_at(gimple_location(stmt),
							"SWMMU pointer state is unknown at dereference");
					} else if (function_marked ||
						lhs_state == SWMMU_POINTER_SWMMU) {
						rewrite_store(&gsi, stmt);
					}

					swmmu_record_pointer_assignment(stmt, states);
					gsi_next(&gsi);
				}
			}

			debug_print(stderr, "[swmmu] leave %s\n", name);
			bool invalid = verify_gimple_in_cfg(cfun, true, true);

			debug_print(stderr,
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
	pass_info.reference_pass_name = "ssa";
	pass_info.ref_pass_instance_number = 1;
	pass_info.pos_op = PASS_POS_INSERT_AFTER;

	register_callback(plugin_info->base_name,
			PLUGIN_PASS_MANAGER_SETUP,
			NULL,
			&pass_info);

	return 0;
}
