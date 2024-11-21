#include "my_vm.h"
#include <sys/mman.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void *physical_memory = NULL;
unsigned char *physical_bitmap = NULL;
unsigned char *virtual_bitmap = NULL;
pde_t *page_directory = NULL;

void set_physical_mem() {
    physical_memory = malloc(MEMSIZE);
    if(physical_memory == NULL) {
        perror("Malloc failed");
        exit(1);
    }

    memset(physical_memory, 0, MEMSIZE);
    printf("Successfully allocated %lu bytes of memory\n", (unsigned long)MEMSIZE);

    size_t physical_bitmap_size = (TOTAL_PHYSICAL_PAGES + 7) / 8;
    size_t virtual_bitmap_size = (TOTAL_VIRTUAL_PAGES + 7) / 8;

    physical_bitmap = malloc(physical_bitmap_size);
    virtual_bitmap = malloc(virtual_bitmap_size);

    if(!physical_bitmap || !virtual_bitmap) {
        perror("Bitmap allocation failed");
        cleanup_physical_mem();
        exit(1);
    }

    // Explicitly clear both bitmaps
    memset(physical_bitmap, 0, physical_bitmap_size);
    memset(virtual_bitmap, 0, virtual_bitmap_size);

    // Initialize page directory at the start of physical memory
    page_directory = (pde_t *)physical_memory;
    memset(page_directory, 0, PAGE_SIZE);

    // Mark first page as used (for page directory)
    SET_BIT(physical_bitmap, 0);
    
    printf("Memory manager initialized:\n");
    printf("Total physical pages: %lu\n", TOTAL_PHYSICAL_PAGES);
    printf("Total virtual pages: %lu\n", TOTAL_VIRTUAL_PAGES);
    printf("Page directory initialized at physical address: %p\n", (void*)page_directory);
}

void* translate(pde_t *pgdir, void *va) {
    if (pgdir == NULL) {
        printf("Error: Page directory is NULL\n");
        return NULL;
    }

    uint32_t vaddr = (uint32_t)va;
    uint32_t dir_index = GET_PAGE_DIR_INDEX(vaddr);
    uint32_t page_index = GET_PAGE_TABLE_INDEX(vaddr);
    uint32_t offset = GET_OFFSET(vaddr);

    // Get the page directory entry
    pde_t *dir_entry = &pgdir[dir_index];
    if(!dir_entry->present) {
        printf("Page directory entry not present for index %u\n", dir_index);
        return NULL;
    }

    // Get the page table
    pte_t *page_table = (pte_t *)((dir_entry->page_table_addr << OFFSET_BITS) + (uint32_t)physical_memory);
    if(page_table == NULL) {
        printf("Error: Page table is NULL\n");
        return NULL;
    }

    // Get the page table entry
    pte_t *pt_entry = &page_table[page_index];
    if(!pt_entry->present) {
        printf("Page table entry not present for index %u\n", page_index);
        return NULL;
    }

    // Calculate the physical address
    uint32_t physical_page = pt_entry->physical_page_number << OFFSET_BITS;
    uint32_t physical_addr = physical_page | offset;
    
    // Convert to actual physical memory address
    void *actual_pa = physical_memory + physical_addr;
    
    printf("Translated VA %p to PA %p\n", va, actual_pa);
    return actual_pa;
}

void* get_next_avail_page() {
    // Scan through physical bitmap to find first unset bit
    for (size_t i = 0; i < TOTAL_PHYSICAL_PAGES; i++) {
        if (!GET_BIT(physical_bitmap, i)) {
            // Found a free page, mark it as used
            SET_BIT(physical_bitmap, i);
            // Return the address of this physical page
            // physical_memory + (i * PAGE_SIZE) gives us the actual address
            return (void*)(physical_memory + (i * PAGE_SIZE));
        }
    }
    // No free pages available
    return NULL;
}

int map_page(void *va) {
    uint32_t vaddr = (uint32_t)va;
    uint32_t dir_index = GET_PAGE_DIR_INDEX(vaddr);
    uint32_t page_index = GET_PAGE_TABLE_INDEX(vaddr);

    if(translate(page_directory, va) != NULL) {
        printf("Error: Virtual address %p is already mapped\n", va);
        return -1;
    }

    // Get the page directory entry
    pde_t *dir_entry = &page_directory[dir_index];

    // If the page table doesn't exist, create it
    if(!dir_entry->present) {
        void *new_page_table = get_next_avail_page();
        if(new_page_table == NULL) {
            printf("Error: Could not allocate new page table\n");
            return -1;
        }

        // Clear the new page table
        memset(new_page_table, 0, PAGE_SIZE);

        // Setup the page directory entry
        dir_entry->present = 1;
        dir_entry->read_write = 1;
        dir_entry->user_supervisor = 1;
        dir_entry->page_table_addr = ((uint32_t)new_page_table - (uint32_t)physical_memory) >> OFFSET_BITS;
        
        printf("Created new page table at physical address: %p\n", new_page_table);
    }

    // Get the page table from the directory entry
    pte_t *page_table = (pte_t *)((dir_entry->page_table_addr << OFFSET_BITS) + (uint32_t)physical_memory);
    
    // Get the page table entry
    pte_t *pt_entry = &page_table[page_index];
    
    if(pt_entry->present) {
        printf("Error: Page table entry is already present\n");
        return -1;
    }

    // Allocate a new physical page
    void *new_physical_page = get_next_avail_page();
    if(new_physical_page == NULL) {
        printf("Error: Could not allocate new physical page\n");
        return -1;
    }

    // Clear the new physical page
    memset(new_physical_page, 0, PAGE_SIZE);

    // Setup the page table entry
    pt_entry->present = 1;
    pt_entry->read_write = 1;
    pt_entry->user_supervisor = 1;
    pt_entry->physical_page_number = ((uint32_t)new_physical_page - (uint32_t)physical_memory) >> OFFSET_BITS;

    printf("Mapped VA %p to PA %p\n", va, new_physical_page);
    return 0;
}

/* Helper function to find next available virtual page */
static void* find_next_virt_page() {
    // Start from index 1 to avoid address 0
    for (size_t i = 1; i < TOTAL_VIRTUAL_PAGES; i++) {
        if (!GET_BIT(virtual_bitmap, i)) {
            SET_BIT(virtual_bitmap, i);
            printf("Found single page at index: %zu\n", i);
            return (void*)(i * PAGE_SIZE);
        }
    }
    return NULL;
}

/* Helper function to find multiple contiguous virtual pages
   Returns starting virtual address if found, NULL otherwise */
static void* find_contiguous_virt_pages(int num_pages) {
    size_t count = 0;
    size_t start_page = 0;
    
    printf("Searching for %d contiguous pages\n", num_pages);
    
    // Start from index 1 to avoid address 0
    for (size_t i = 1; i < TOTAL_VIRTUAL_PAGES; i++) {
        if (!GET_BIT(virtual_bitmap, i)) {
            if (count == 0) {
                start_page = i;
                printf("Found potential start page at index: %zu\n", i);
            }
            count++;
            if (count == num_pages) {
                printf("Found %zu contiguous pages starting at page %zu\n", count, start_page);
                // Mark all pages as used
                for (size_t j = start_page; j < start_page + num_pages; j++) {
                    SET_BIT(virtual_bitmap, j);
                }
                return (void*)(start_page * PAGE_SIZE);
            }
        } else {
            count = 0;
        }
    }
    
    printf("Failed to find %d contiguous pages. Last count: %zu\n", num_pages, count);
    return NULL;
}

void *n_malloc(size_t num_bytes) {
    // Calculate number of pages needed
    size_t num_pages = (num_bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    
    if (num_pages == 0) {
        printf("Error: Invalid size requested\n");
        return NULL;
    }

    printf("Allocating %zu bytes (%zu pages)\n", num_bytes, num_pages);

    // Print initial state of virtual bitmap
    printf("Initial virtual bitmap state: ");
    for (int i = 0; i < 1; i++) {
        for (int j = 7; j >= 0; j--) {
            printf("%d", (virtual_bitmap[i] >> j) & 1);
        }
        printf(" ");
    }
    printf("\n");

    // Optimize for single page allocation
    void *start_va;
    if (num_pages == 1) {
        start_va = find_next_virt_page();
    } else {
        start_va = find_contiguous_virt_pages(num_pages);
    }

    if (start_va == NULL) {
        printf("Error: Could not find %s virtual page(s)\n", 
               num_pages == 1 ? "a single" : "enough contiguous");
        return NULL;
    }

    // Map each page
    for (size_t i = 0; i < num_pages; i++) {
        void *current_va = (void*)((char*)start_va + (i * PAGE_SIZE));
        int map_result = map_page(current_va);
        printf("Mapping page %zu at address %p, result: %d\n", i, current_va, map_result);
        
        if (map_result != 0) {
            // If mapping fails, need to cleanup previously mapped pages
            printf("Error: Page mapping failed at page %zu, cleaning up...\n", i);
            
            // Cleanup: unmap previous pages and clear virtual bitmap
            for (size_t j = 0; j < i; j++) {
                size_t page_num = ((uint32_t)start_va / PAGE_SIZE) + j;
                CLEAR_BIT(virtual_bitmap, page_num);
                // Note: We'll add proper page unmapping when we implement n_free
            }
            return NULL;
        }
    }

    printf("Successfully allocated %zu pages starting at virtual address: %p\n", 
           num_pages, start_va);
    return start_va;
}

int n_free(void *va, size_t size) {
    if (va == NULL || size == 0) {
        printf("Error: Invalid arguments to n_free\n");
        return -1;
    }

    // Calculate number of pages to free
    size_t num_pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint32_t start_vpn = (uint32_t)va / PAGE_SIZE;

    printf("Freeing %zu pages starting at virtual address %p\n", num_pages, va);

    // Check if all pages were actually allocated
    for (size_t i = 0; i < num_pages; i++) {
        void* current_va = (void*)((char*)va + (i * PAGE_SIZE));
        if (translate(page_directory, current_va) == NULL) {
            printf("Error: Attempting to free unallocated page at %p\n", current_va);
            return -1;
        }
    }

    // Free each page
    for (size_t i = 0; i < num_pages; i++) {
        void* current_va = (void*)((char*)va + (i * PAGE_SIZE));
        uint32_t vaddr = (uint32_t)current_va;
        uint32_t dir_index = GET_PAGE_DIR_INDEX(vaddr);
        uint32_t page_index = GET_PAGE_TABLE_INDEX(vaddr);

        // Get directory entry
        pde_t* dir_entry = &page_directory[dir_index];
        if (!dir_entry->present) continue;

        // Get page table
        pte_t* page_table = (pte_t*)((dir_entry->page_table_addr << OFFSET_BITS) + (uint32_t)physical_memory);
        pte_t* pt_entry = &page_table[page_index];

        if (pt_entry->present) {
            // Clear the physical page in bitmap
            uint32_t phys_page_num = pt_entry->physical_page_number;
            CLEAR_BIT(physical_bitmap, phys_page_num);

            // Clear the page table entry
            memset(pt_entry, 0, sizeof(pte_t));
        }

        // Clear the virtual bitmap
        CLEAR_BIT(virtual_bitmap, start_vpn + i);
    }

    printf("Successfully freed %zu pages\n", num_pages);
    return 0;
}

int put_data(void *va, void *src, size_t bytes) {
    if (va == NULL || src == NULL || bytes == 0) {
        printf("Error: Invalid arguments to put_data\n");
        return -1;
    }

    uint32_t offset = GET_OFFSET((uint32_t)va);
    size_t bytes_remaining = bytes;
    size_t src_offset = 0;

    // Handle data that spans multiple pages
    while (bytes_remaining > 0) {
        // Get physical address for current virtual address
        void *current_va = (void*)((char*)va + src_offset);
        void *pa = translate(page_directory, current_va);
        if (pa == NULL) {
            printf("Error: Invalid virtual address in put_data\n");
            return -1;
        }

        // Calculate how many bytes we can write to this page
        size_t bytes_this_page = PAGE_SIZE - offset;
        if (bytes_this_page > bytes_remaining) {
            bytes_this_page = bytes_remaining;
        }

        // Copy data to physical memory
        memcpy(pa, (char*)src + src_offset, bytes_this_page);

        // Update counters
        bytes_remaining -= bytes_this_page;
        src_offset += bytes_this_page;
        offset = 0; 
    }

    return 0;
}

int get_data(void *va, void *dst, size_t bytes) {
    if (va == NULL || dst == NULL || bytes == 0) {
        printf("Error: Invalid arguments to get_data\n");
        return -1;
    }

    uint32_t offset = GET_OFFSET((uint32_t)va);
    size_t bytes_remaining = bytes;
    size_t dst_offset = 0;

    // Handle data that spans multiple pages
    while (bytes_remaining > 0) {
        // Get physical address for current virtual address
        void *current_va = (void*)((char*)va + dst_offset);
        void *pa = translate(page_directory, current_va);
        if (pa == NULL) {
            printf("Error: Invalid virtual address in get_data\n");
            return -1;
        }

        // Calculate how many bytes we can read from this page
        size_t bytes_this_page = PAGE_SIZE - offset;
        if (bytes_this_page > bytes_remaining) {
            bytes_this_page = bytes_remaining;
        }

        // Copy data from physical memory
        memcpy((char*)dst + dst_offset, pa, bytes_this_page);

        // Update counters
        bytes_remaining -= bytes_this_page;
        dst_offset += bytes_this_page;
        offset = 0; 
    }

    return 0;
}

int mat_mult(void *mat1, void *mat2, int size, void *answer) {
    if (mat1 == NULL || mat2 == NULL || answer == NULL || size <= 0) {
        printf("Error: Invalid arguments to mat_mult\n");
        return -1;
    }

    // Allocate temporary buffers for matrix data
    int *buffer1 = malloc(size * size * sizeof(int));
    int *buffer2 = malloc(size * size * sizeof(int));
    int *result = malloc(size * size * sizeof(int));

    if (!buffer1 || !buffer2 || !result) {
        printf("Error: Failed to allocate temporary buffers\n");
        free(buffer1);
        free(buffer2);
        free(result);
        return -1;
    }

    // Get matrix data using get_data
    if (get_data(mat1, buffer1, size * size * sizeof(int)) != 0 ||
        get_data(mat2, buffer2, size * size * sizeof(int)) != 0) {
        printf("Error: Failed to read matrix data\n");
        free(buffer1);
        free(buffer2);
        free(result);
        return -1;
    }

    // Perform matrix multiplication
    for (int i = 0; i < size; i++) {
        for (int j = 0; j < size; j++) {
            result[i * size + j] = 0;
            for (int k = 0; k < size; k++) {
                result[i * size + j] += buffer1[i * size + k] * buffer2[k * size + j];
            }
        }
    }

    // Store result using put_data
    if (put_data(answer, result, size * size * sizeof(int)) != 0) {
        printf("Error: Failed to write result\n");
        free(buffer1);
        free(buffer2);
        free(result);
        return -1;
    }

    // Clean up
    free(buffer1);
    free(buffer2);
    free(result);

    return 0;
}

void cleanup_physical_mem(){
    if (physical_memory!=NULL){
        free(physical_memory);
        physical_memory = NULL;
    }
    if (physical_bitmap != NULL) {
        free(physical_bitmap);
        physical_bitmap = NULL;
    }
    if (virtual_bitmap != NULL) {
        free(virtual_bitmap);
        virtual_bitmap = NULL;
    }
}
