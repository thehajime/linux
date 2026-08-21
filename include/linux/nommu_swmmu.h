/* swmmu-runtime.h */
#ifndef SWMMU_RUNTIME_H
#define SWMMU_RUNTIME_H

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/maple_tree.h>
#include <linux/refcount.h>
#include <vdso/limits.h>

#define SWMMU_PAGE_SIZE 4096UL

struct nommu_swmmu_space;
struct swmmu_page;

struct swmmu_backend_ops {
	void *(*zalloc)(void *context, size_t size);
	void (*dealloc)(void *context, void *ptr);

	struct swmmu_page *(*alloc_page)(void *context);
	void (*free_page)(void *context,
			  struct swmmu_page *page);

	void *(*page_address)(void *context,
			      struct swmmu_page *page);

	int (*copy_page)(void *context,
			 struct swmmu_page *destination,
			 const struct swmmu_page *source);
};

struct nommu_swmmu_space *nommu_swmmu_space_create(const struct swmmu_backend_ops *ops,
		   void *backend_context);
void nommu_swmmu_space_destroy(struct nommu_swmmu_space *space);
struct nommu_swmmu_space *swmmu_host_space_create(void);

struct nommu_swmmu_space *nommu_swmmu_current(void);

/* Allocate a synthetic virtual-address range in the current space. */
void *swmmu_alloc(size_t size);
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
