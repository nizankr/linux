# PinMigration Project

**Introducing Ability to Migrate Pinned Pages in the Linux Kernel**

By Nizan Kafman-Raz and Omer Daube  
Under the guidance of Nadav Amit and Dan Tzafrir  
Computer Science Faculty, Technion, 2024-2025

## Project Overview

This project introduces the ability to migrate pinned memory pages in the Linux kernel. Pinned pages are memory pages that are locked in physical memory and cannot be migrated, typically caused by direct memory access (DMA) operations. Our implementation enables migration of these previously immovable pages, improving memory management flexibility in the Linux kernel.

Forked from torvalds/linux (v6.6)

A repository with out testing environment: https://github.com/nizankr/PinMig_testing/

## Modified Kernel Files

### `mm/migrate.c`: Migration Code
-  Modified `folio_migrate_copy` and added a check for pinned pages. If pinned, we determine the type of pinning:
    - **Kernel pinning**: Calls `kernel_migrate_pinned_page_prepare` (retrieves PTE, flushes TLB, clears access bit) and `kernel_migrate_pinned_page_commit` (uses `cmpxchg` to update the PTE).
    - **DMA pinning**: Calls `call_dma_migrate_page`, which eventually invokes `intel_migrate_pages`.
- Modified `folio_migrate_mapping` to handle extra references:
  - Updated `folio_expected_refs` to account for cases where `GUP_PIN_COUNTING_BIAS` occurs (when `pin_user_pages` is used instead of `get_user_pages`).
  - Implemented a fallback mechanism for migration failures.

### `fs/splice.c`: Supporting Migration in `iter_to_pipe`
- Modified `iter_to_pipe` to store the vmap of the page instead of the page itself, using `vmap_ptr` in the `pipe_buffer` structure.
- Introduced wrapper functions (prefixed with `splice_`) to interact with `page_alias.c`, adding null checks where necessary.

### `drivers/iommu/intel/iommu.c`: IOMMU-Specific Migration
- Added `intel_migrate_page`, the migration function for the IOMMU case:
  - Similar to the kernel migration flow but split into two phases: commit and prepare.
- Added dirty and access bits to `drivers/iommu/intel/iommu.h` to support this.
- Modified:
  - `dma_pte_clear_level`: Iterates over all PTEs being unmapped and calls our reverse mapping removal function.
  - `__domain_mapping`: Iterates over all PTEs being mapped and calls our reverse mapping creation function.

### `mm/page_alias.c`: Page Alias Management
- Main file containing our aliasing functions, including:
  - **Kernel migration helpers**:
    - `alias_vmap`: Creates a new vmap.
    - `alias_vunmap`: Releases a vmap.
    - `alias_vmap_to_page`: Returns the page and "locks" it to prevent migration.
  - **IOMMU migration helpers**:
    - `alias_iommu_create_rmap`: Creates a reverse mapping for IOMMU.
    - `alias_iommu_free_rmap`: Frees the IOMMU reverse mapping.

