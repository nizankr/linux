#ifndef __LINUX_PAGE_ALIAS_H
#define __LINUX_PAGE_ALIAS_H

#include <linux/jump_label.h>
#include <linux/iommu.h>

#ifdef CONFIG_PAGE_ALIAS
extern void __set_page_alias(struct page *page);
extern struct page_ext_operations page_alias_ops;
struct page *alias_vmap_to_page(void *p);
void *alias_vmap(struct page *pages);
void alias_vunmap(void *p);
void alias_page_close(struct page *page);
void add_to_alias_rmap(struct page *page, void *ptr);
void check_migration_at_start(struct list_head *from);
int is_alias_rmap_empty(struct page *page);
void *get_alias_rmap(struct page *page);
int start_pinned_migration(struct page *page);
void end_pinned_migration(struct page *page);
int get_alias_refcount(struct page *page); //need to be atomic in the future
// void alias_iommu_remove_rmap(unsigned long iova_pfn);
void alias_iommu_create_rmap(struct iommu_domain *domain, unsigned long phys_pfn, unsigned long iov_pfn);
int is_alias_dma_page(struct page *page);
int call_dma_migrate_page(struct page *page, bool prepare, struct folio *folio);
int is_alias_kernel_page(struct page *page);
void alias_iommu_free_rmap(unsigned long phys_pfn);

static inline void set_page_alias(struct page *page)
{
	__set_page_alias(page);
}

#endif
#endif
