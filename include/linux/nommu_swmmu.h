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
void nommu_swmmu_space_attach(struct mm_struct *mm,
			struct nommu_swmmu_space *space);
void nommu_swmmu_space_destroy(struct nommu_swmmu_space *space);
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
 * Translate one same-page range.
 *
 * The returned pointer is a host pointer to backing storage.
 */
void *swmmu_translate(struct nommu_swmmu_space *space,
                      uintptr_t address,
                      size_t size,
                      int write);

/* Eagerly clone all mappings and backing pages. */
int swmmu_clone_space(struct nommu_swmmu_space *parent,
                      struct nommu_swmmu_space **child_out);

/* Compiler-generated access ABI. */
uint64_t nommu_swmmu_load_u64(const void *address, size_t size);

void nommu_swmmu_store_u64(void *address,
                     size_t size,
                     uint64_t value);

/* host backend (FIXME: somewhere else?) */
extern const struct swmmu_backend_ops swmmu_host_backend;
#ifdef CONFIG_KUNIT
void nommu_swmmu_kunit_set_space(struct nommu_swmmu_space *space);
void nommu_swmmu_kunit_clear_space(void);
#endif

#endif
