#include <linux/interval_tree.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/rbtree.h>
#include <linux/vmalloc.h>
#include <linux/pagemap.h>
#include <linux/page_ext.h>
#include <linux/mm.h>
#include <linux/types.h>
#include <linux/migrate.h>
#include "../drivers/iommu/intel/iommu.h"
#include <asm/page.h>
#include <linux/page_alias.h>
#include <linux/page-flags.h>
#include <linux/debugfs.h>
#include "internal.h"
#include <linux/mmdebug.h>



/* ***************************************************
   ************* PAGE ALIAS DEFINITIONS **************
   *************************************************** */


#define UNALIASED_YET -2
#define BEING_MIGRATED -1
#define READY_TO_MOVE 0


struct iommu_rmap {
	struct iommu_domain *domain;
	unsigned long phys_pfn;
	unsigned long iov_pfn;
};

struct iommu_rmap empty_rmap = {
	.domain = NULL,
	.phys_pfn = 0
};

int iommu_rmap_empty(struct iommu_rmap a){
	return !(a.domain);
}

struct page_alias {
	atomic_t do_not_move; 
	atomic_t kernel_ref_count;
	int iommu_ref_count;
	void* kernel_rmap;
	struct iommu_rmap iommu_rmap;
	spinlock_t lock;
};

static inline struct page_alias *get_page_alias(struct page_ext *page_ext)
{
	return page_ext_data(page_ext, &page_alias_ops);
}

static __init bool need_page_alias(void)
{
	return true;
}

static __init void init_page_alias(void)
{
	printk(KERN_INFO "Initializing PageAlias, Omer Daube and Nizan Kafman-Raz\n");
}

struct page_ext_operations page_alias_ops = {
	.size = sizeof(struct page_alias),
	.need = need_page_alias,
	.init = init_page_alias,
};

static noinline void __set_page_ext_alias(struct page_ext *page_ext)
{
	//Initialize the page alias struct
	struct page_alias *page_alias;
	page_alias = get_page_alias(page_ext);
	atomic_set(&page_alias->do_not_move, UNALIASED_YET);
	atomic_set(&page_alias->kernel_ref_count, 0);
	spin_lock_init(&page_alias->lock);
	page_alias->iommu_ref_count = 0;
	page_alias->iommu_rmap = empty_rmap;
	page_alias->kernel_rmap = NULL;
}


noinline void __set_page_alias(struct page *page)
{
	struct page_ext *page_ext;
	page_ext = page_ext_get(page);
	if (unlikely(!page_ext))
		return;
	__set_page_ext_alias(page_ext);
	page_ext_put(page_ext);
}

/* ***************************************************
   **************** HELPER FUNCTIONS *****************
   *************************************************** */

/*
 * Function: set_page_alias
 * Description: This function is used to set the alias of a page.
 * Parameters:
 *  page: The page to set the alias of.
 */
int get_alias_refcount(struct page *page)
{
	struct page_ext *page_ext = page_ext_get(page);
	struct page_alias *page_alias = page_ext_data(page_ext, &page_alias_ops);
	int kernel_refs = atomic_read(&(page_alias->kernel_ref_count));
	int iommu_refs = page_alias->iommu_ref_count;
	page_ext_put(page_ext);
	// We are only returning iommu_refs if kernel_refs is zero, 
	// because then the only mapping is the IOMMU, as we assumed.
	return (kernel_refs != 0) ? kernel_refs : iommu_refs ; 
}

/* 
 * Function: is_alias_rmap_empty
 * Description: This function is used to check if a page has no rmaps.
 * Parameters:
 *  page: The page to check.
 * Returns: Whether the page has no rmaps, so returns 1 if empty, else 0.
 */
int is_alias_rmap_empty(struct page *page) {
	struct page_ext *page_ext = page_ext_get(page);
	struct page_alias *page_alias = page_ext_data(page_ext, &page_alias_ops);
	int ret = (iommu_rmap_empty(page_alias->iommu_rmap) && !(page_alias->kernel_rmap));
	page_ext_put(page_ext);
	return ret;
}

/* 
 * Function: is_alias_dma_page
 * Description: This function is used to check if a page is dma pinned.
 * Parameters:
 *  page: The page to check.
 * Returns: Whether the page is dma/IOMMU pinned, so returns 1 if dma, else 0.
 */
int is_alias_dma_page(struct page *page) {
	struct page_ext *page_ext = page_ext_get(page);
	struct page_alias *page_alias = page_ext_data(page_ext, &page_alias_ops);
	int ret = !iommu_rmap_empty(page_alias->iommu_rmap);
	page_ext_put(page_ext);
	return ret;
}

/* 
 * Function: is_alias_kernel_page
 * Description: This function is used to check if a page is kernel pinned.
 * Parameters:
 *  page: The page to check.
 * Returns: Whether the page is kernel pinned, so returns 1 if kernel, else 0.
 */
int is_alias_kernel_page(struct page *page) { 
	struct page_ext *page_ext = page_ext_get(page);
	struct page_alias *page_alias = page_ext_data(page_ext, &page_alias_ops);
	int ret = !!(page_alias->kernel_rmap);
	page_ext_put(page_ext);
	return ret;
}

/* 
 * Function: get_alias_rmap
 * Description: This function is used to get the rmap of a page.
 * Parameters:
 *  page: The page to get the rmap of.
 * Returns: The rmap of the page.
 */
void *get_alias_rmap(struct page *page){
	struct page_ext *page_ext = page_ext_get(page);
	struct page_alias *page_alias = page_ext_data(page_ext, &page_alias_ops);
	void* ret;
	int i = page_alias->iommu_ref_count; 
	if((page_alias->kernel_rmap) && (i)) {
		pr_info("Both kernel and iommu rmaps!\n"); 
	}
	if (page_alias->kernel_rmap) {
		// Kernel
		ret = page_alias->kernel_rmap;
	}
	else { 
		// IOMMU, not in use
		ret = phys_to_virt(page_alias->iommu_rmap.phys_pfn); 
	}
	page_ext_put(page_ext);
	return ret;
}


/* ***************************************************
   **************** KERNEL FUNCTIONS *****************
   *************************************************** */

/*
 * Function: alias_page_create
 * Description: This function is used to create a 
 * vmap for the page (only one vmap per page).
 * Parameters:
 *  page: The page to create the alias for.
 */
void *alias_vmap(struct page *page) {
	void *vmap_address, *old_rmap;
	BUG_ON(!page);
	struct page_ext *page_ext = page_ext_get(page);
	struct page_alias *page_alias = page_ext_data(page_ext, &page_alias_ops);
	vmap_address = page_alias->kernel_rmap;
	old_rmap = page_alias->kernel_rmap;
	if(atomic_inc_not_zero(&page_alias->kernel_ref_count)){
		/* could also always skip this, but it could save unecessary vmaps and vunmaps */
		BUG_ON(!vmap_address);
	} else {
		/* kernerl_ref_count was zero, so need to create a vmap */
		page_ext_put(page_ext); /* because vmap might sleep */
		vmap_address = vmap(&page, 1, VM_MAP, PAGE_KERNEL);
		page_ext = page_ext_get(page);
		page_alias = page_ext_data(page_ext, &page_alias_ops);
		if (cmpxchg(&page_alias->kernel_rmap, old_rmap, vmap_address)){
		/* someone else created the vmap before us */
			page_ext_put(page_ext);
			vunmap(vmap_address);
			page_ext = page_ext_get(page);
			page_alias = page_ext_data(page_ext, &page_alias_ops);
			vmap_address = page_alias->kernel_rmap;
			BUG_ON(!vmap_address);
		}
		atomic_inc(&page_alias->kernel_ref_count);
	}
	atomic_cmpxchg(&page_alias->do_not_move, UNALIASED_YET, READY_TO_MOVE); 
	page_ext_put(page_ext);
	BUG_ON(!is_vmalloc_addr(vmap_address));
	return vmap_address;
}

/*
 * Function: alias_vunmap
 * Description: This function is used to unmap a vmap.
 * Parameters:
 *  p: The vmap to unmap.
 */
void alias_vunmap(void *p) {
	BUG_ON(!is_vmalloc_addr(p));
	struct page* page = alias_vmap_to_page(p);
	struct page_ext *page_ext = page_ext_get(page);
	struct page_alias *page_alias = page_ext_data(page_ext, &page_alias_ops);
	if (!atomic_add_unless(&page_alias->kernel_ref_count, -1, 1)) {
		if (atomic_cmpxchg(&page_alias->kernel_ref_count, 1, 0)) {
			page_ext_put(page_ext); /* because vmap might sleep */
			vunmap(p);
		} else {
		/* Either someone else unmapped, or someone else added a referance. */
			atomic_dec(&page_alias->kernel_ref_count);
			page_ext_put(page_ext);
		}
	}
	alias_page_close(page);
	p = NULL;
}

/*
 * Function: alias_vmap_to_page
 * Description: This function is used to get the page struct of a vmap.
 * Parameters:
 *  p: The vmap to get the page struct of.
 * Returns: The page struct of the vmap.
 */
struct page *alias_vmap_to_page(void *p) {
	struct page *page;
	page = vmalloc_to_page(p);
	struct page_ext *page_ext = page_ext_get(page);
	struct page_alias *page_alias = page_ext_data(page_ext, &page_alias_ops);
	while(atomic_cmpxchg(&page_alias->do_not_move, BEING_MIGRATED, BEING_MIGRATED));
	atomic_inc(&page_alias->do_not_move); // make it hold the number of places that hold the page struct
	page_ext_put(page_ext);
	return page;
}

/*
 * Function: alias_page_close
 * Description: This function is used to close a page that was aliased (set the do_not_move parameter).
 * Parameters:
 *  page: The page to close.
 */
void alias_page_close(struct page *page) {
	struct page_ext *page_ext = page_ext_get(page);
	struct page_alias *page_alias = page_ext_data(page_ext, &page_alias_ops);
	BUG_ON(atomic_read(&page_alias->do_not_move) <= READY_TO_MOVE);
	atomic_dec(&page_alias->do_not_move);
	page_ext_put(page_ext);
	put_page(page);
}

/*
 * Function: start_pinned_migration
 * Description: This function is used to start a pinned migration of a page.
 * Parameters:
 *  page: The page to start the pinned migration for.
 */
int start_pinned_migration(struct page *page) {
	int ret;
	struct page_ext *page_ext = page_ext_get(page);
	struct page_alias *page_alias = page_ext_data(page_ext, &page_alias_ops);
	ret = atomic_cmpxchg(&page_alias->do_not_move, READY_TO_MOVE, BEING_MIGRATED);
	page_ext_put(page_ext);
	return ret;
}

/*
 * Function: end_pinned_migration
 * Description: This function is used to end a pinned migration of a page.
 * Parameters:
 *  page: The page to end the pinned migration for.
 */
void end_pinned_migration(struct page *page) {
	struct page_ext *page_ext = page_ext_get(page);
	struct page_alias *page_alias =
		page_ext_data(page_ext, &page_alias_ops);
	atomic_set(&page_alias->do_not_move, READY_TO_MOVE);
	page_ext_put(page_ext);
}


/* ***************************************************
   ***************** IOMMU FUNCTIONS *****************
   *************************************************** */

/*
 * Function: alias_iommu_create_rmap
 * Description: This function is used to create an iommu rmap for a page.
 * Parameters:
 *  domain: The iommu domain to create the rmap in.
 *  phys_pfn: The physical page frame number of the page to create the rmap for.
 *  iov_pfn: The iov page frame number of the page to create the rmap for.
 */
void alias_iommu_create_rmap(struct iommu_domain *domain, unsigned long phys_pfn, unsigned long iov_pfn) {
	struct page *page = pfn_to_page(phys_pfn);
	struct iommu_rmap new_rmap = {
		.domain = domain,
		.phys_pfn = phys_pfn,
		.iov_pfn = iov_pfn
	};
	BUG_ON(!page);
	
	struct page_ext *page_ext = page_ext_get(page);
	if (!page_ext) {
		if (!pfn_valid(phys_pfn)){
			// handle case of invalid PFN (MMIO)
			return;
		}
		BUG_ON(true);
		return;
	}
	
	struct page_alias *page_alias = page_ext_data(page_ext, &page_alias_ops);
	if (!page_alias) {
		set_page_alias(page);
		page_ext_put(page_ext);
		page_ext = page_ext_get(page);
		page_alias = page_ext_data(page_ext, &page_alias_ops);
	}

	spin_lock(&page_alias->lock);  // Acquire spinlock
	
	if (page_alias->iommu_ref_count == 0) {
		page_alias->iommu_rmap = new_rmap;
		page_alias->iommu_ref_count = 1;
	} else {
		page_alias->iommu_ref_count++;
	}
	
	spin_unlock(&page_alias->lock);  // Release spinlock

	page_ext_put(page_ext);
}

/*
 * Function: alias_iommu_free_rmap
 * Description: This function is used to free an iommu rmap from a page.
 * Parameters:
 *  phys_pfn: The physical page frame number of the page to free the rmap from.
 */
void alias_iommu_free_rmap(unsigned long phys_pfn){

	struct page *page;
	struct page_ext *page_ext;
	struct page_alias *page_alias;

	page = pfn_to_page(phys_pfn);
	BUG_ON(!page);
	page_ext = page_ext_get(page);
	if (!page_ext) {
		// handle case of invalid PFN (MMIO)
		if (!pfn_valid(phys_pfn)){
			return;
		}
		return;
	}
	page_alias = page_ext_data(page_ext, &page_alias_ops);
	page_alias->iommu_ref_count = 0;
	page_alias->iommu_rmap = empty_rmap;
	page_ext_put(page_ext);
}


/*
 * Function: call_dma_migrate_page
 * Description: This function is a wrapper for the migrate_page function of the iommu_domain struct.
 * It is used to migrate a page from one location to another.
 * Parameters:
 *  page: The page to migrate
 *  prepare: Pass to the migrate_page function. If 1, prepare for migration by turning off the dirty bit. If 0, do the migration itself.
 *  folio: The new folio of the page (new location). If prepare is 1, this will be ignored.
 * Returns: 0 on success, -1 on failure.
 */
int call_dma_migrate_page(struct page *page, bool prepare, struct folio *folio) {
	struct page_ext *page_ext = page_ext_get(page);
	struct page_alias *page_alias = page_ext_data(page_ext, &page_alias_ops);
	struct iommu_rmap iommu_rmap = page_alias->iommu_rmap;
	struct iommu_domain* domain = iommu_rmap.domain;
	int ret = domain->ops->migrate_page(domain, iommu_rmap.iov_pfn, folio, prepare);
	page_ext_put(page_ext);
	return ret;
}
