// SPDX-License-Identifier: GPL-2.0

#include <linux/mm.h>
#include <linux/hugetlb.h>

#include "internal.h"

static pud_t *get_old_pud(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;

	pgd = pgd_offset(mm, addr);
	if (pgd_none_or_clear_bad(pgd))
		return NULL;

	p4d = p4d_offset(pgd, addr);
	if (p4d_none_or_clear_bad(p4d))
		return NULL;

	pud = pud_offset(p4d, addr);
	if (pud_none_or_clear_bad(pud))
		return NULL;

	return pud;
}

static pmd_t *get_old_pmd(struct mm_struct *mm, unsigned long addr)
{
	pud_t *pud;
	pmd_t *pmd;

	pud = get_old_pud(mm, addr);
	if (!pud)
		return NULL;

	pmd = pmd_offset(pud, addr);
	if (pmd_none(*pmd))
		return NULL;

	return pmd;
}

static pud_t *alloc_new_pud(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pgd;
	p4d_t *p4d;

	pgd = pgd_offset(mm, addr);
	p4d = p4d_alloc(mm, pgd, addr);
	if (!p4d)
		return NULL;

	return pud_alloc(mm, p4d, addr);
}

static pmd_t *alloc_new_pmd(struct mm_struct *mm, unsigned long addr)
{
	pud_t *pud;
	pmd_t *pmd;

	pud = alloc_new_pud(mm, addr);
	if (!pud)
		return NULL;

	pmd = pmd_alloc(mm, pud, addr);
	if (!pmd)
		return NULL;

	VM_BUG_ON(pmd_trans_huge(*pmd));

	return pmd;
}

static void take_rmap_locks(struct vm_area_struct *vma)
{
	if (vma->vm_file)
		i_mmap_lock_write(vma->vm_file->f_mapping);
	if (vma->anon_vma)
		anon_vma_lock_write(vma->anon_vma);
}

static void drop_rmap_locks(struct vm_area_struct *vma)
{
	if (vma->anon_vma)
		anon_vma_unlock_write(vma->anon_vma);
	if (vma->vm_file)
		i_mmap_unlock_write(vma->vm_file->f_mapping);
}

static pte_t move_soft_dirty_pte(pte_t pte)
{
	if (pte_none(pte))
		return pte;

	/*
	 * Set soft dirty bit so we can notice
	 * in userspace the ptes were moved.
	 */
	if (pgtable_supports_soft_dirty()) {
		if (pte_present(pte))
			pte = pte_mksoft_dirty(pte);
		else
			pte = pte_swp_mksoft_dirty(pte);
	}

	return pte;
}

static int mremap_folio_pte_batch(struct vm_area_struct *vma, unsigned long addr,
		pte_t *ptep, pte_t pte, int max_nr)
{
	struct folio *folio;

	if (max_nr == 1)
		return 1;

	/* Avoid expensive folio lookup if we stand no chance of benefit. */
	if (pte_batch_hint(ptep, pte) == 1)
		return 1;

	folio = vm_normal_folio(vma, addr, pte);
	if (!folio || !folio_test_large(folio))
		return 1;

	return folio_pte_batch_flags(folio, NULL, ptep, &pte, max_nr, FPB_RESPECT_WRITE);
}

static int move_ptes(struct pagetable_move_control *pmc,
		unsigned long extent, pmd_t *old_pmd, pmd_t *new_pmd)
{
	struct vm_area_struct *vma = pmc->old;
	bool need_clear_uffd_wp = vma_has_uffd_without_event_remap(vma);
	struct mm_struct *mm = vma->vm_mm;
	pte_t *old_ptep, *new_ptep;
	pte_t old_pte, pte;
	pmd_t dummy_pmdval;
	spinlock_t *old_ptl, *new_ptl;
	bool force_flush = false;
	unsigned long old_addr = pmc->old_addr;
	unsigned long new_addr = pmc->new_addr;
	unsigned long old_end = old_addr + extent;
	unsigned long len = old_end - old_addr;
	int max_nr_ptes;
	int nr_ptes;
	int err = 0;

	/*
	 * When need_rmap_locks is true, we take the i_mmap_rwsem and anon_vma
	 * locks to ensure that rmap will always observe either the old or the
	 * new ptes. This is the easiest way to avoid races with
	 * truncate_pagecache(), page migration, etc...
	 *
	 * When need_rmap_locks is false, we use other ways to avoid
	 * such races:
	 *
	 * - During exec() shift_arg_pages(), we use a specially tagged vma
	 *   which rmap call sites look for using vma_is_temporary_stack().
	 *
	 * - During mremap(), new_vma is often known to be placed after vma
	 *   in rmap traversal order. This ensures rmap will always observe
	 *   either the old pte, or the new pte, or both (the page table locks
	 *   serialize access to individual ptes, but only rmap traversal
	 *   order guarantees that we won't miss both the old and new ptes).
	 */
	if (pmc->need_rmap_locks)
		take_rmap_locks(vma);

	/*
	 * We don't have to worry about the ordering of src and dst
	 * pte locks because exclusive mmap_lock prevents deadlock.
	 */
	old_ptep = pte_offset_map_lock(mm, old_pmd, old_addr, &old_ptl);
	if (!old_ptep) {
		err = -EAGAIN;
		goto out;
	}
	/*
	 * Now new_pte is none, so collapse_scan_file() path can not find
	 * this by traversing file->f_mapping, so there is no concurrency with
	 * retract_page_tables(). In addition, we already hold the exclusive
	 * mmap_lock, so this new_pte page is stable, so there is no need to get
	 * pmdval and do pmd_same() check.
	 */
	new_ptep = pte_offset_map_rw_nolock(mm, new_pmd, new_addr, &dummy_pmdval,
					   &new_ptl);
	if (!new_ptep) {
		pte_unmap_unlock(old_ptep, old_ptl);
		err = -EAGAIN;
		goto out;
	}
	if (new_ptl != old_ptl)
		spin_lock_nested(new_ptl, SINGLE_DEPTH_NESTING);
	flush_tlb_batched_pending(vma->vm_mm);
	lazy_mmu_mode_enable();

	for (; old_addr < old_end; old_ptep += nr_ptes, old_addr += nr_ptes * PAGE_SIZE,
		new_ptep += nr_ptes, new_addr += nr_ptes * PAGE_SIZE) {
		VM_WARN_ON_ONCE(!pte_none(ptep_get(new_ptep)));

		nr_ptes = 1;
		max_nr_ptes = (old_end - old_addr) >> PAGE_SHIFT;
		old_pte = ptep_get(old_ptep);
		if (pte_none(old_pte))
			continue;

		/*
		 * If we are remapping a valid PTE, make sure
		 * to flush TLB before we drop the PTL for the
		 * PTE.
		 *
		 * NOTE! Both old and new PTL matter: the old one
		 * for racing with folio_mkclean(), the new one to
		 * make sure the physical page stays valid until
		 * the TLB entry for the old mapping has been
		 * flushed.
		 */
		if (pte_present(old_pte)) {
			nr_ptes = mremap_folio_pte_batch(vma, old_addr, old_ptep,
							 old_pte, max_nr_ptes);
			force_flush = true;
		}
		pte = get_and_clear_ptes(mm, old_addr, old_ptep, nr_ptes);
		pte = move_pte(pte, old_addr, new_addr);
		pte = move_soft_dirty_pte(pte);

		if (need_clear_uffd_wp && pte_is_uffd_wp_marker(pte))
			pte_clear(mm, new_addr, new_ptep);
		else {
			if (need_clear_uffd_wp) {
				if (pte_present(pte)) {
					/*
					 * See __copy_present_ptes(): normalise
					 * RWP PTEs so the destination starts
					 * accessible instead of taking a
					 * numa-hinting fault on first access.
					 */
					if (userfaultfd_rwp(vma) && pte_uffd(pte))
						pte = pte_modify(pte, vma->vm_page_prot);
					pte = pte_clear_uffd(pte);
				} else {
					pte = pte_swp_clear_uffd(pte);
				}
			}
			set_ptes(mm, new_addr, new_ptep, pte, nr_ptes);
		}
	}

	lazy_mmu_mode_disable();
	if (force_flush)
		flush_tlb_range(vma, old_end - len, old_end);
	if (new_ptl != old_ptl)
		spin_unlock(new_ptl);
	pte_unmap(new_ptep - 1);
	pte_unmap_unlock(old_ptep - 1, old_ptl);
out:
	if (pmc->need_rmap_locks)
		drop_rmap_locks(vma);
	return err;
}

#ifdef CONFIG_HAVE_MOVE_PMD
static bool move_normal_pmd(struct pagetable_move_control *pmc,
			pmd_t *old_pmd, pmd_t *new_pmd)
{
	spinlock_t *old_ptl, *new_ptl;
	struct vm_area_struct *vma = pmc->old;
	struct mm_struct *mm = vma->vm_mm;
	bool res = false;
	pmd_t pmd;

	if (!arch_supports_page_table_move())
		return false;
	if (!uffd_supports_page_table_move(pmc))
		return false;
	/*
	 * The destination pmd shouldn't be established, free_pgtables()
	 * should have released it.
	 *
	 * However, there's a case during execve() where we use mremap
	 * to move the initial stack, and in that case the target area
	 * may overlap the source area (always moving down).
	 *
	 * If everything is PMD-aligned, that works fine, as moving
	 * each pmd down will clear the source pmd. But if we first
	 * have a few 4kB-only pages that get moved down, and then
	 * hit the "now the rest is PMD-aligned, let's do everything
	 * one pmd at a time", we will still have the old (now empty
	 * of any 4kB pages, but still there) PMD in the page table
	 * tree.
	 *
	 * Warn on it once - because we really should try to figure
	 * out how to do this better - but then say "I won't move
	 * this pmd".
	 *
	 * One alternative might be to just unmap the target pmd at
	 * this point, and verify that it really is empty. We'll see.
	 */
	if (WARN_ON_ONCE(!pmd_none(*new_pmd)))
		return false;

	/*
	 * We don't have to worry about the ordering of src and dst
	 * ptlocks because exclusive mmap_lock prevents deadlock.
	 */
	old_ptl = pmd_lock(mm, old_pmd);
	new_ptl = pmd_lockptr(mm, new_pmd);
	if (new_ptl != old_ptl)
		spin_lock_nested(new_ptl, SINGLE_DEPTH_NESTING);

	pmd = *old_pmd;

	/* Racing with collapse? */
	if (unlikely(!pmd_present(pmd) || pmd_leaf(pmd)))
		goto out_unlock;
	/* Clear the pmd */
	pmd_clear(old_pmd);
	res = true;

	VM_BUG_ON(!pmd_none(*new_pmd));

	pmd_populate(mm, new_pmd, pmd_pgtable(pmd));
	flush_tlb_range(vma, pmc->old_addr, pmc->old_addr + PMD_SIZE);
out_unlock:
	if (new_ptl != old_ptl)
		spin_unlock(new_ptl);
	spin_unlock(old_ptl);

	return res;
}
#else
static inline bool move_normal_pmd(struct pagetable_move_control *pmc,
		pmd_t *old_pmd, pmd_t *new_pmd)
{
	return false;
}
#endif

#if CONFIG_PGTABLE_LEVELS > 2 && defined(CONFIG_HAVE_MOVE_PUD)
static bool move_normal_pud(struct pagetable_move_control *pmc,
		pud_t *old_pud, pud_t *new_pud)
{
	spinlock_t *old_ptl, *new_ptl;
	struct vm_area_struct *vma = pmc->old;
	struct mm_struct *mm = vma->vm_mm;
	pud_t pud;

	if (!arch_supports_page_table_move())
		return false;
	if (!uffd_supports_page_table_move(pmc))
		return false;
	/*
	 * The destination pud shouldn't be established, free_pgtables()
	 * should have released it.
	 */
	if (WARN_ON_ONCE(!pud_none(*new_pud)))
		return false;

	/*
	 * We don't have to worry about the ordering of src and dst
	 * ptlocks because exclusive mmap_lock prevents deadlock.
	 */
	old_ptl = pud_lock(mm, old_pud);
	new_ptl = pud_lockptr(mm, new_pud);
	if (new_ptl != old_ptl)
		spin_lock_nested(new_ptl, SINGLE_DEPTH_NESTING);

	/* Clear the pud */
	pud = *old_pud;
	pud_clear(old_pud);

	VM_BUG_ON(!pud_none(*new_pud));

	pud_populate(mm, new_pud, pud_pgtable(pud));
	flush_tlb_range(vma, pmc->old_addr, pmc->old_addr + PUD_SIZE);
	if (new_ptl != old_ptl)
		spin_unlock(new_ptl);
	spin_unlock(old_ptl);

	return true;
}
#else
static inline bool move_normal_pud(struct pagetable_move_control *pmc,
		pud_t *old_pud, pud_t *new_pud)
{
	return false;
}
#endif

#if defined(CONFIG_TRANSPARENT_HUGEPAGE) && defined(CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD)
static bool move_huge_pud(struct pagetable_move_control *pmc,
		pud_t *old_pud, pud_t *new_pud)
{
	spinlock_t *old_ptl, *new_ptl;
	struct vm_area_struct *vma = pmc->old;
	struct mm_struct *mm = vma->vm_mm;
	pud_t pud;

	/*
	 * The destination pud shouldn't be established, free_pgtables()
	 * should have released it.
	 */
	if (WARN_ON_ONCE(!pud_none(*new_pud)))
		return false;

	/*
	 * We don't have to worry about the ordering of src and dst
	 * ptlocks because exclusive mmap_lock prevents deadlock.
	 */
	old_ptl = pud_lock(mm, old_pud);
	new_ptl = pud_lockptr(mm, new_pud);
	if (new_ptl != old_ptl)
		spin_lock_nested(new_ptl, SINGLE_DEPTH_NESTING);

	/* Clear the pud */
	pud = *old_pud;
	pud_clear(old_pud);

	VM_BUG_ON(!pud_none(*new_pud));

	/* Set the new pud */
	/* mark soft_ditry when we add pud level soft dirty support */
	set_pud_at(mm, pmc->new_addr, new_pud, pud);
	flush_pud_tlb_range(vma, pmc->old_addr, pmc->old_addr + HPAGE_PUD_SIZE);
	if (new_ptl != old_ptl)
		spin_unlock(new_ptl);
	spin_unlock(old_ptl);

	return true;
}
#else
static bool move_huge_pud(struct pagetable_move_control *pmc,
		pud_t *old_pud, pud_t *new_pud)

{
	WARN_ON_ONCE(1);
	return false;

}
#endif

enum pgt_entry {
	NORMAL_PMD,
	HPAGE_PMD,
	NORMAL_PUD,
	HPAGE_PUD,
};

/*
 * Returns an extent of the corresponding size for the pgt_entry specified if
 * valid. Else returns a smaller extent bounded by the end of the source and
 * destination pgt_entry.
 */
static __always_inline unsigned long get_extent(enum pgt_entry entry,
						struct pagetable_move_control *pmc)
{
	unsigned long next, extent, mask, size;
	unsigned long old_addr = pmc->old_addr;
	unsigned long old_end = pmc->old_end;
	unsigned long new_addr = pmc->new_addr;

	switch (entry) {
	case HPAGE_PMD:
	case NORMAL_PMD:
		mask = PMD_MASK;
		size = PMD_SIZE;
		break;
	case HPAGE_PUD:
	case NORMAL_PUD:
		mask = PUD_MASK;
		size = PUD_SIZE;
		break;
	default:
		BUILD_BUG();
		break;
	}

	next = (old_addr + size) & mask;
	/* even if next overflowed, extent below will be ok */
	extent = next - old_addr;
	if (extent > old_end - old_addr)
		extent = old_end - old_addr;
	next = (new_addr + size) & mask;
	if (extent > next - new_addr)
		extent = next - new_addr;
	return extent;
}

/*
 * Should move_pgt_entry() acquire the rmap locks? This is either expressed in
 * the PMC, or overridden in the case of normal, larger page tables.
 */
static bool should_take_rmap_locks(struct pagetable_move_control *pmc,
				   enum pgt_entry entry)
{
	switch (entry) {
	case NORMAL_PMD:
	case NORMAL_PUD:
		return true;
	default:
		return pmc->need_rmap_locks;
	}
}

/*
 * Attempts to speedup the move by moving entry at the level corresponding to
 * pgt_entry. Returns true if the move was successful, else false.
 */
static bool move_pgt_entry(struct pagetable_move_control *pmc,
			   enum pgt_entry entry, void *old_entry, void *new_entry)
{
	bool moved = false;
	bool need_rmap_locks = should_take_rmap_locks(pmc, entry);

	/* See comment in move_ptes() */
	if (need_rmap_locks)
		take_rmap_locks(pmc->old);

	switch (entry) {
	case NORMAL_PMD:
		moved = move_normal_pmd(pmc, old_entry, new_entry);
		break;
	case NORMAL_PUD:
		moved = move_normal_pud(pmc, old_entry, new_entry);
		break;
	case HPAGE_PMD:
		moved = IS_ENABLED(CONFIG_TRANSPARENT_HUGEPAGE) &&
			move_huge_pmd(pmc->old, pmc->old_addr, pmc->new_addr, old_entry,
				      new_entry);
		break;
	case HPAGE_PUD:
		moved = IS_ENABLED(CONFIG_TRANSPARENT_HUGEPAGE) &&
			move_huge_pud(pmc, old_entry, new_entry);
		break;

	default:
		WARN_ON_ONCE(1);
		break;
	}

	if (need_rmap_locks)
		drop_rmap_locks(pmc->old);

	return moved;
}

/*
 * A helper to check if aligning down is OK. The aligned address should fall
 * on *no mapping*. For the stack moving down, that's a special move within
 * the VMA that is created to span the source and destination of the move,
 * so we make an exception for it.
 */
static bool can_align_down(struct pagetable_move_control *pmc,
			   struct vm_area_struct *vma, unsigned long addr_to_align,
			   unsigned long mask)
{
	unsigned long addr_masked = addr_to_align & mask;

	/*
	 * If @addr_to_align of either source or destination is not the beginning
	 * of the corresponding VMA, we can't align down or we will destroy part
	 * of the current mapping.
	 */
	if (!pmc->for_stack && vma->vm_start != addr_to_align)
		return false;

	/* In the stack case we explicitly permit in-VMA alignment. */
	if (pmc->for_stack && addr_masked >= vma->vm_start)
		return true;

	/*
	 * Make sure the realignment doesn't cause the address to fall on an
	 * existing mapping.
	 */
	return find_vma_intersection(vma->vm_mm, addr_masked, vma->vm_start) == NULL;
}

/*
 * Determine if are in fact able to realign for efficiency to a higher page
 * table boundary.
 */
static bool can_realign_addr(struct pagetable_move_control *pmc,
			     unsigned long pagetable_mask)
{
	unsigned long align_mask = ~pagetable_mask;
	unsigned long old_align = pmc->old_addr & align_mask;
	unsigned long new_align = pmc->new_addr & align_mask;
	unsigned long pagetable_size = align_mask + 1;
	unsigned long old_align_next = pagetable_size - old_align;

	/*
	 * We don't want to have to go hunting for VMAs from the end of the old
	 * VMA to the next page table boundary, also we want to make sure the
	 * operation is worthwhile.
	 *
	 * So ensure that we only perform this realignment if the end of the
	 * range being copied reaches or crosses the page table boundary.
	 *
	 * boundary                        boundary
	 *    .<- old_align ->                .
	 *    .              |----------------.-----------|
	 *    .              |          vma   .           |
	 *    .              |----------------.-----------|
	 *    .              <----------------.----------->
	 *    .                          len_in
	 *    <------------------------------->
	 *    .         pagetable_size        .
	 *    .              <---------------->
	 *    .                old_align_next .
	 */
	if (pmc->len_in < old_align_next)
		return false;

	/* Skip if the addresses are already aligned. */
	if (old_align == 0)
		return false;

	/* Only realign if the new and old addresses are mutually aligned. */
	if (old_align != new_align)
		return false;

	/* Ensure realignment doesn't cause overlap with existing mappings. */
	if (!can_align_down(pmc, pmc->old, pmc->old_addr, pagetable_mask) ||
	    !can_align_down(pmc, pmc->new, pmc->new_addr, pagetable_mask))
		return false;

	return true;
}

/*
 * Opportunistically realign to specified boundary for faster copy.
 *
 * Consider an mremap() of a VMA with page table boundaries as below, and no
 * preceding VMAs from the lower page table boundary to the start of the VMA,
 * with the end of the range reaching or crossing the page table boundary.
 *
 *   boundary                        boundary
 *      .              |----------------.-----------|
 *      .              |          vma   .           |
 *      .              |----------------.-----------|
 *      .         pmc->old_addr         .      pmc->old_end
 *      .              <---------------------------->
 *      .                  move these page tables
 *
 * If we proceed with moving page tables in this scenario, we will have a lot of
 * work to do traversing old page tables and establishing new ones in the
 * destination across multiple lower level page tables.
 *
 * The idea here is simply to align pmc->old_addr, pmc->new_addr down to the
 * page table boundary, so we can simply copy a single page table entry for the
 * aligned portion of the VMA instead:
 *
 *   boundary                        boundary
 *      .              |----------------.-----------|
 *      .              |          vma   .           |
 *      .              |----------------.-----------|
 * pmc->old_addr                        .      pmc->old_end
 *      <------------------------------------------->
 *      .           move these page tables
 */
static void try_realign_addr(struct pagetable_move_control *pmc,
			     unsigned long pagetable_mask)
{

	if (!can_realign_addr(pmc, pagetable_mask))
		return;

	/*
	 * Simply align to page table boundaries. Note that we do NOT update the
	 * pmc->old_end value, and since the move_page_tables() operation spans
	 * from [old_addr, old_end) (offsetting new_addr as it is performed),
	 * this simply changes the start of the copy, not the end.
	 */
	pmc->old_addr &= pagetable_mask;
	pmc->new_addr &= pagetable_mask;
}

/* Is the page table move operation done? */
static bool pmc_done(struct pagetable_move_control *pmc)
{
	return pmc->old_addr >= pmc->old_end;
}

/* Advance to the next page table, offset by extent bytes. */
static void pmc_next(struct pagetable_move_control *pmc, unsigned long extent)
{
	pmc->old_addr += extent;
	pmc->new_addr += extent;
}

/*
 * Determine how many bytes in the specified input range have had their page
 * tables moved so far.
 */
static unsigned long pmc_progress(struct pagetable_move_control *pmc)
{
	unsigned long orig_old_addr = pmc->old_end - pmc->len_in;
	unsigned long old_addr = pmc->old_addr;

	/*
	 * Prevent negative return values when {old,new}_addr was realigned but
	 * we broke out of the loop in move_page_tables() for the first PMD
	 * itself.
	 */
	return old_addr < orig_old_addr ? 0 : old_addr - orig_old_addr;
}

unsigned long move_page_tables(struct pagetable_move_control *pmc)
{
	unsigned long extent;
	struct mmu_notifier_range range;
	pmd_t *old_pmd, *new_pmd;
	pud_t *old_pud, *new_pud;
	struct mm_struct *mm = pmc->old->vm_mm;

	if (!pmc->len_in)
		return 0;

	if (is_vm_hugetlb_page(pmc->old))
		return move_hugetlb_page_tables(pmc->old, pmc->new, pmc->old_addr,
						pmc->new_addr, pmc->len_in);

	/*
	 * If possible, realign addresses to PMD boundary for faster copy.
	 * Only realign if the mremap copying hits a PMD boundary.
	 */
	try_realign_addr(pmc, PMD_MASK);

	flush_cache_range(pmc->old, pmc->old_addr, pmc->old_end);
	mmu_notifier_range_init(&range, MMU_NOTIFY_UNMAP, 0, mm,
				pmc->old_addr, pmc->old_end);
	mmu_notifier_invalidate_range_start(&range);

	for (; !pmc_done(pmc); pmc_next(pmc, extent)) {
		cond_resched();
		/*
		 * If extent is PUD-sized try to speed up the move by moving at the
		 * PUD level if possible.
		 */
		extent = get_extent(NORMAL_PUD, pmc);

		old_pud = get_old_pud(mm, pmc->old_addr);
		if (!old_pud)
			continue;
		new_pud = alloc_new_pud(mm, pmc->new_addr);
		if (!new_pud)
			break;
		if (pud_trans_huge(*old_pud)) {
			if (extent == HPAGE_PUD_SIZE) {
				move_pgt_entry(pmc, HPAGE_PUD, old_pud, new_pud);
				/* We ignore and continue on error? */
				continue;
			}
		} else if (IS_ENABLED(CONFIG_HAVE_MOVE_PUD) && extent == PUD_SIZE) {
			if (move_pgt_entry(pmc, NORMAL_PUD, old_pud, new_pud))
				continue;
		}

		extent = get_extent(NORMAL_PMD, pmc);
		old_pmd = get_old_pmd(mm, pmc->old_addr);
		if (!old_pmd)
			continue;
		new_pmd = alloc_new_pmd(mm, pmc->new_addr);
		if (!new_pmd)
			break;
again:
		if (pmd_is_huge(*old_pmd)) {
			if (extent == HPAGE_PMD_SIZE &&
			    move_pgt_entry(pmc, HPAGE_PMD, old_pmd, new_pmd))
				continue;
			split_huge_pmd(pmc->old, old_pmd, pmc->old_addr);
		} else if (IS_ENABLED(CONFIG_HAVE_MOVE_PMD) &&
			   extent == PMD_SIZE) {
			/*
			 * If the extent is PMD-sized, try to speed the move by
			 * moving at the PMD level if possible.
			 */
			if (move_pgt_entry(pmc, NORMAL_PMD, old_pmd, new_pmd))
				continue;
		}
		if (pmd_none(*old_pmd))
			continue;
		if (pte_alloc(pmc->new->vm_mm, new_pmd))
			break;
		if (move_ptes(pmc, extent, old_pmd, new_pmd) < 0)
			goto again;
	}

	mmu_notifier_invalidate_range_end(&range);

	return pmc_progress(pmc);
}

unsigned long vma_mmu_move_mapping(struct vma_remap_struct *vrm,
				struct pagetable_move_control *pmc)
{
	(void) vrm;

	return move_page_tables(pmc);
}

static void vma_mmu_move_rollback(struct vma_remap_struct *vrm,
				unsigned long moved_len)
{
	if (!vrm || !vrm->vma || !vrm->new_vma || !moved_len)
		return;

	PAGETABLE_MOVE(pmc, vrm->new_vma, vrm->vma,
		       vrm->new_addr, vrm->addr,
		       moved_len);

	pmc.need_rmap_locks = true;
	move_page_tables(&pmc);
}

static int vma_mmu_move_prepare(struct vma_remap_struct *vrm,
			 struct vm_area_struct *src,
			 struct vm_area_struct **dst,
			 bool *dst_linked,
			 void **state)
{
	const pgoff_t pgoff = linear_page_index(src, vrm->addr);
	const pgoff_t anon_pgoff = __linear_anon_page_index(src, vrm->addr);
	bool need_rmap_locks;
	struct vm_area_struct *vma = src;
	struct vm_area_struct *new_vma;

	new_vma = copy_vma(&vma, vrm->new_addr, vrm->new_len,
			   pgoff, anon_pgoff, &need_rmap_locks);
	if (!new_vma)
		return -ENOMEM;

	vrm->vma = vma;
	*dst = new_vma;
	*dst_linked = true;
	*state = NULL;

	return 0;
}

const struct vma_mapping_ops vma_mmu_mapping_ops = {
	.move_prepare = vma_mmu_move_prepare,
	.move_mapping = vma_mmu_move_mapping,
	.move_rollback = vma_mmu_move_rollback,
};


const struct vma_mapping_ops *vma_mapping_ops_for_mm(struct mm_struct *mm)
{
	return &vma_mmu_mapping_ops;
}
