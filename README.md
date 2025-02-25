# PinMigration Project

**Introducing Ability to Migrate Pinned Pages in the Linux Kernel**

By Nizan Kafman-Raz and Omer Daube  
Under the guidance of Nadav Amit and Dan Tzafrir  
Computer Science Faculty, Technion, 2024-2025

## Project Overview

This project introduces the ability to migrate pinned memory pages in the Linux kernel. Pinned pages are memory pages that are locked in physical memory and cannot be migrated, typically caused by direct memory access (DMA) operations. Our implementation enables migration of these previously immovable pages, improving memory management flexibility in the Linux kernel.

Forked from torvalds/linux (v6.6)

## Key Components

### Modified Kernel Files

- **mm/migrate.c**: `folio_migrate_copy` - Entry point of our migration flow
- **drivers/iommu/intel/iommu.c**: `intel_migrate_page` - Our IOMMU-specific migration handler 
- **mm/page_alias.c**: Contains our struct for page alias and its functions
- **fs/splice.c**: Modified vmsplice code to allow migrations (changed immutable maps to mutable with vmap)

### Testing Environment

The project includes a testing environment to validate page migration with device passthrough to a VM (which causes its pages to be pinned). The setup consists of:

- A VM with a Mellanox network device passed through via SR-IOV
- Scripts to configure network interfaces and disable transparent huge pages (THP)
- Instructions to monitor and trigger page migration

## Setup Instructions

### Prerequisites

- Linux machine with a network interface card that supports SR-IOV
- QEMU installed for VM creation

### Setting Up the Test Environment

1. **Disable Transparent Huge Pages**

   The script `setup_network_and_thp.sh` disables transparent huge pages and configures the network:

   ```bash
   ./setup_network_and_thp.sh
   ```

   This script:
   - Disables transparent huge pages
   - Sets up the Mellanox interface with two virtual functions (VFs)
   - Configures one VF for the VM and one for the host
   - Enables direct communication between them
   - Ensures data flows through the physical interface to create real DMA operations

   > Note: By default, the script is configured for interface `enp139s0f0np0`. Modify the variables at the top of the script if your interface has a different name.

2. **Start the VM with Device Passthrough**

   ```bash
   ./start_vm.sh
   ```

   This script launches a VM with:
   - 2GB of memory
   - Ubuntu 20.04 focal server image
   - The Mellanox VF passed through
   - Memory pre-allocation enabled (preventing lazy initialization)

3. **Monitoring and Migration**

   To verify that VM pages are pinned and then migrate them:

   ```bash
   # Find the QEMU process ID
   ps aux | grep qemu
   
   # Check current NUMA node placement
   numastat <pid>
   
   # Migrate pages from source to target node
   migratepages <pid> <src-node> <tgt-node>
   
   # Verify migration
   numastat <pid>
   ```

This approach works for both IOMMU-pinned pages and mmap-pinned pages (via the vmsplice modifications).

## Project Files

The repository includes:

- Test environment in the `setup_tests` folder:
  - `cloud-init.iso`: Configuration for the VM
  - `focal-server-cloudimg-amd64.img`: Ubuntu 20.04 VM image
  - `setup_network_and_thp.sh`: Network and THP configuration script
  - `start_vm.sh`: VM launch script with device passthrough
  - `transfer-disk.qcow2`: Additional disk for data transfer
  - `user-data`: Cloud-init user data

## Troubleshooting

- Ensure the PCI address in `setup_network_and_thp.sh` matches your actual Mellanox device
- Ensure that the kernel is run with intel_iommu=sp_off (in /etc/default/grub), to prevent superpages in the iommu


## License

Same as Linux Kernel (GPL-2.0)
