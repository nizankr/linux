# PinMigration Project

**Introducing Ability to Migrate Pinned Pages in the Linux Kernel**

By Nizan Kafman-Raz and Omer Daube  
Under the guidance of Nadav Amit and Dan Tzafrir  
Computer Science Faculty, Technion, 2024-2025

## Project Overview

This project introduces the ability to migrate pinned memory pages in the Linux kernel. Pinned pages are memory pages that are locked in physical memory and cannot be migrated, typically caused by direct memory access (DMA) operations. Our implementation enables migration of these previously immovable pages, improving memory management flexibility in the Linux kernel.

Forked from torvalds/linux (v6.6)

A repository with out testing environment: https://github.com/nizankr/PinMig_testing/

## Key Components

### Modified Kernel Files

- **mm/migrate.c**: `folio_migrate_copy` - Entry point of our migration flow
- **drivers/iommu/intel/iommu.c**: `intel_migrate_page` - Our IOMMU-specific migration handler 
- **mm/page_alias.c**: Contains our struct for page alias and its functions
- **fs/splice.c**: Modified vmsplice code to allow migrations (changed immutable maps to mutable with vmap)
