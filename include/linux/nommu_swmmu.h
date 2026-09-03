/* swmmu-runtime.h */
#ifndef SWMMU_RUNTIME_H
#define SWMMU_RUNTIME_H

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/maple_tree.h>
#include <linux/refcount.h>
#include <linux/gfp_types.h>
#include <linux/rwsem.h>
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

struct nommu_swmmu_backing {
	refcount_t refs;
	struct page **pages;
	size_t page_count;
};

struct nommu_swmmu_vma {
	struct nommu_swmmu_backing *backing;
	unsigned long page_offset;
	size_t page_count;
	unsigned int access;
};

struct nommu_swmmu_space {
	struct rw_semaphore lock;
	struct mm_struct *mm;
	refcount_t users;
	const struct nommu_swmmu_mem_ops *ops;
};


enum nommu_swmmu_vma_tx_type {
	NOMMU_SWMMU_VMA_TX_RESIZE,
	NOMMU_SWMMU_VMA_TX_SPLIT,
	NOMMU_SWMMU_VMA_TX_EXPAND,
};

struct nommu_swmmu_vma_tx {
	enum nommu_swmmu_vma_tx_type type;

	struct nommu_swmmu_vma *vma_data;
	struct nommu_swmmu_vma *new_vma_data;

	struct nommu_swmmu_backing *old_backing;
	struct nommu_swmmu_backing *new_backing;

	size_t old_page_count;
	size_t new_page_count;

	unsigned long old_page_offset;
	size_t split_page_count;

	bool backing_ref_held;

};

struct vm_area_struct;

int nommu_swmmu_expand_prepare(struct nommu_swmmu_space *space,
				struct vm_area_struct *vma,
				unsigned long new_end,
				struct nommu_swmmu_vma_tx *tx);

void nommu_swmmu_vma_expand_commit(struct vm_area_struct *target,
				   struct nommu_swmmu_vma_tx *tx);

void nommu_swmmu_vma_expand_abort(struct nommu_swmmu_space *space,
				struct nommu_swmmu_vma_tx *tx);


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
int nommu_swmmu_unmap(struct nommu_swmmu_space *space,
		      unsigned long address,
		      size_t size);

long nommu_swmmu_remap(struct nommu_swmmu_space *space,
		       unsigned long address,
		       size_t old_size,
		       size_t new_size);

int nommu_swmmu_dup_mmap(struct mm_struct *dst,
			struct mm_struct *src);

struct vm_area_struct;
void nommu_swmmu_vma_close(struct mm_struct *mm,
			struct vm_area_struct *vma);

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
