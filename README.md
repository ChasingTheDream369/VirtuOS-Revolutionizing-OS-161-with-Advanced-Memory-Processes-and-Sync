# VirtuOS: Revolutionizing OS/161 with Advanced Memory, Processes, and Sync

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

Welcome to VirtuOS, a project that takes OS/161 to the next level by introducing advanced memory management, dynamic processes, and powerful synchronization mechanisms. This project aims to enhance the capabilities of OS/161, making it a versatile and feature-rich educational operating system.

## 🚀 Features

 **Hierarchical Page Tables**: Enjoy enhanced memory management with multi-level page tables that optimize virtual-to-physical address translations.

 **Dynamic Memory Processes**: Allocate memory efficiently as needed, improving system resource utilization.

 **Synchronization Powerhouse**: Harness synchronization primitives like mutexes, semaphores, and more to manage concurrent operations seamlessly.

 **Advanced TLB Management**: Manage your Translation Lookaside Buffer effectively for optimal virtual memory translation.

 **Interactive Virtual Memory Mapping**: Empower your programs to navigate the virtual-to-physical address mapping effortlessly.

 **Smooth Context Switching**: Facilitate seamless process context switching with improved TLB handling.

 **User-Friendly Debugging**: Identify and resolve issues with user-friendly debugging and trace capabilities.
    
## 📚 Getting Started

Follow these steps to set up and start using VirtuOS:

1. **Clone the repository**:
   ```shell
   git clone https://github.com/your-username/VirtuOS.git
   cd VirtuOS
   ```

2. **Configure and build the project**:

    ```bash
    ./configure
    bmake
    bmake install
    ```
3. **Build the kernel for VirtuOS**:

   ```bash
    cd kern/conf
    ./config VIRTUOS
   ```

4. **Build and install the kernel**:

    ```bash
    cd ../compile/VIRTUOS
    bmake depend
    bmake
    bmake install
   ```
    
5. **Run the kernel**:

   ```bash
   cd ../../..
   sys161 kernel
   ```
Refer to the [Wiki]:https://wiki.cse.unsw.edu.au/cs3231cgi/2021t1/Asst3 for comprehensive testing options and debugging guidance.

## 💡 Usage and Examples

## Hierarchical Page Tables

```c
// Example code for setting up hierarchical page tables
#include <vm.h>

// Create a new address space
struct addrspace *as = as_create();
if (as == NULL) {
    panic("Failed to create address space");
}

// Define regions and allocate pages
as_define_region(as, ...);
as_prepare_load(as);

// Load the pages into memory
for (size_t i = 0; i < as->as_region_count; i++) {
    struct region *region = &as->as_regions[i];
    for (vaddr_t va = region->vbase; va < region->vbase + region->size; va += PAGE_SIZE) {
        vm_fault(as, va, false, VM_FAULT_READ);
    }
}
```

## Dynamic Memory Processes

```c
// Example code for dynamic memory allocation within a process
#include <types.h>
#include <synch.h>

// Create a lock for synchronization
struct lock *malloc_lock;

int main() {
    // Initialize the malloc lock
    malloc_lock = lock_create("malloc_lock");

    // Allocate memory dynamically
    lock_acquire(malloc_lock);
    void *ptr = kmalloc(sizeof(int) * 10);
    lock_release(malloc_lock);

    // Use the allocated memory
    if (ptr != NULL) {
        int *int_array = (int *)ptr;
        // ... manipulate int_array ...
        kfree(ptr);
    }

    return 0;
}
```

## Synchronization Powerhouse

```c
// Example code demonstrating mutex usage
#include <types.h>
#include <synch.h>

// Create a mutex
struct mutex *my_mutex;

void my_thread(void *arg) {
    // Lock the mutex
    mutex_lock(my_mutex);

    // Critical section
    // ... perform synchronized operations ...

    // Unlock the mutex
    mutex_unlock(my_mutex);
}

int main() {
    // Initialize the mutex
    my_mutex = mutex_create("my_mutex");

    // Create and run threads
    // ... create threads and run them ...

    // Clean up resources
    mutex_destroy(my_mutex);

    return 0;
}
```
🌟 Contribute

If you'd like to contribute to VirtuOS, feel free to open an issue or submit a pull request. We welcome your suggestions and improvements!

📝 License

VirtuOS is licensed under the MIT License.
