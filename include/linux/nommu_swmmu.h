/* swmmu-runtime.h */
#ifndef SWMMU_RUNTIME_H
#define SWMMU_RUNTIME_H

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/maple_tree.h>
#include <linux/refcount.h>
#include <linux/gfp_types.h>
#include <vdso/limits.h>

#define SWMMU_PAGE_SIZE 4096UL

#define NOMMU_SWMMU_NONE	(0)
#define NOMMU_SWMMU_READ	(1U << 0)
#define NOMMU_SWMMU_WRITE	(1U << 1)
#define NOMMU_SWMMU_EXEC	(1U << 2)

enum nommu_swmmu_map_mode {
	NOMMU_SWMMU_MAP_AUTO,
	NOMMU_SWMMU_MAP_HINT,
	NOMMU_SWMMU_MAP_FIXED,
	NOMMU_SWMMU_MAP_FIXED_NOREPLACE,
};

struct nommu_swmmu_space;
struct page;

struct nommu_swmmu_mem_ops {
	void *(*zalloc)(size_t size, gfp_t gfp);
	void (*dealloc)(void *ptr);

	struct page *(*page_alloc)(gfp_t gfp);
	void (*page_free)(struct page *page);

	int (*page_copy)(struct page *dst,
			const struct page *src);
};

struct nommu_swmmu_space *nommu_swmmu_space_create(void);
int nommu_swmmu_space_attach(struct mm_struct *mm,
			struct nommu_swmmu_space *space);
void nommu_swmmu_space_detach(struct mm_struct *mm);
void nommu_swmmu_space_get(struct nommu_swmmu_space *space);
void nommu_swmmu_space_put(struct nommu_swmmu_space *space);

const struct nommu_swmmu_mem_ops *nommu_swmmu_arch_mem_ops(void);
#if IS_ENABLED(CONFIG_NOMMU_SWMMU_KUNIT_TEST)
struct nommu_swmmu_space *
nommu_swmmu_space_create_with_ops(
	const struct nommu_swmmu_mem_ops *mem_ops);
#endif

struct nommu_swmmu_space *nommu_swmmu_current(void);

/* Allocate a synthetic virtual-address range in the current space. */
long swmmu_alloc(size_t size);
int swmmu_free(void *address);

/*
 * Check whether a range is accessible in the current SWMMU space.
 */
int nommu_swmmu_check_access(struct nommu_swmmu_space *space,
			uintptr_t address,
			size_t size,
			int write);
int nommu_swmmu_load_u64_checked(const void *address,
				size_t size,
				u64 *value);
int nommu_swmmu_store_u64_checked(void *address,
				size_t size,
				u64 value);

/* Eagerly clone all mappings and backing pages. */
int swmmu_clone_space(struct nommu_swmmu_space *parent,
                      struct nommu_swmmu_space **child_out);

/* Compiler-generated access ABI. */
uint64_t nommu_swmmu_load_u64(const void *address, size_t size);

long nommu_swmmu_store_u64(void *address,
			size_t size,
			uint64_t value);

/* mapping API */
long nommu_swmmu_map(struct nommu_swmmu_space *space,
		     size_t size);
long nommu_swmmu_map_at(struct nommu_swmmu_space *space,
			unsigned long address,
			size_t size,
			unsigned int access,
			enum nommu_swmmu_map_mode mode);

int nommu_swmmu_unmap(struct nommu_swmmu_space *space,
		      unsigned long address,
		      size_t size);

long nommu_swmmu_remap(struct nommu_swmmu_space *space,
		       unsigned long address,
		       size_t old_size,
		       size_t new_size);

int nommu_swmmu_dup_mmap(struct mm_struct *dst,
			struct mm_struct *src);

#ifdef CONFIG_DEBUG_VM_MAPLE_TREE
/*
 * Validate the committed SWMMU/VMA state.
 *
 * The caller must hold mmap_lock. The SWMMU space read lock
 * is acquired internally.
 */
void nommu_swmmu_validate(struct mm_struct *mm);
#else
static inline void nommu_swmmu_validate(struct mm_struct *mm)
{
}
#endif

/* prctl API */
long nommu_swmmu_get_mode(struct mm_struct *mm);
int nommu_swmmu_set_mode(struct mm_struct *mm,
			unsigned long mode);

#ifdef CONFIG_KUNIT
void nommu_swmmu_kunit_set_space(struct nommu_swmmu_space *space);
void nommu_swmmu_kunit_clear_space(void);
#endif

#endif
