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
    // Allocate physical memory
    physical_memory = malloc(MEMSIZE);
    if (physical_memory == NULL) {
        perror("Physical memory allocation failed");
        exit(1);
    }
    memset(physical_memory, 0, MEMSIZE);

    // Calculate bitmap sizes
    size_t physical_bitmap_size = (TOTAL_PHYSICAL_PAGES + 7) / 8;
    size_t virtual_bitmap_size = (TOTAL_VIRTUAL_PAGES + 7) / 8;

    // Allocate bitmaps
    physical_bitmap = malloc(physical_bitmap_size);
    virtual_bitmap = malloc(virtual_bitmap_size);
    if (!physical_bitmap || !virtual_bitmap) {
        perror("Bitmap allocation failed");
        cleanup_physical_mem();
        exit(1);
    }

    // Clear bitmaps
    memset(physical_bitmap, 0, physical_bitmap_size);
    memset(virtual_bitmap, 0, virtual_bitmap_size);

    // Initialize page directory
    page_directory = (pde_t *)physical_memory;
    memset(page_directory, 0, PAGE_SIZE);

    // Mark first page as used (for page directory)
    SET_BIT(physical_bitmap, 0);

    printf("Memory manager initialized:\n");
    printf("Physical memory: %p\n", physical_memory);
    printf("Page directory: %p\n", page_directory);
    printf("Total physical pages: %llu\n", (unsigned long long)TOTAL_PHYSICAL_PAGES);
    printf("Total virtual pages: %llu\n", (unsigned long long)TOTAL_VIRTUAL_PAGES);
}

void* get_next_avail_physical_page() {
    for (size_t i = 1; i < TOTAL_PHYSICAL_PAGES; i++) {
        if (!GET_BIT(physical_bitmap, i)) {
            SET_BIT(physical_bitmap, i);
            void* page_addr = (char*)physical_memory + (i * PAGE_SIZE);
            memset(page_addr, 0, PAGE_SIZE);
            return page_addr;
        }
    }
    return NULL;
}

size_t get_next_avail_virtual_page() {
    for (size_t i = 1; i < TOTAL_VIRTUAL_PAGES; i++) {
        if (!GET_BIT(virtual_bitmap, i)) {
            SET_BIT(virtual_bitmap, i);
            return i;
        }
    }
    return 0;  // 0 indicates failure
}

int map_page(void *va) {
    if (va == NULL) {
        printf("Error: NULL virtual address provided\n");
        return -1;
    }

    uint32_t vaddr = (uint32_t)va;
    uint32_t dir_index = GET_PAGE_DIR_INDEX(vaddr);
    uint32_t page_index = GET_PAGE_TABLE_INDEX(vaddr);

    printf("Mapping VA: %p (dir_index: %u, page_index: %u)\n", va, dir_index, page_index);

    // Get directory entry
    pde_t *dir_entry = &page_directory[dir_index];

    // If page table doesn't exist, create it
    if (!dir_entry->present) {
        void *new_page_table = get_next_avail_physical_page();
        if (!new_page_table) {
            printf("Error: Failed to allocate page table\n");
            return -1;
        }
        
        dir_entry->present = 1;
        dir_entry->read_write = 1;
        dir_entry->user_supervisor = 1;
        dir_entry->page_table_addr = ((uint32_t)new_page_table - (uint32_t)physical_memory) >> OFFSET_BITS;
        
        printf("Created new page table at PA: %p\n", new_page_table);
    }

    // Get page table
    pte_t *page_table = (pte_t *)((dir_entry->page_table_addr << OFFSET_BITS) + (uint32_t)physical_memory);
    pte_t *pt_entry = &page_table[page_index];

    // Allocate new physical page
    void *new_physical_page = get_next_avail_physical_page();
    if (!new_physical_page) {
        printf("Error: Failed to allocate physical page\n");
        return -1;
    }

    pt_entry->present = 1;
    pt_entry->read_write = 1;
    pt_entry->user_supervisor = 1;
    pt_entry->physical_page_number = ((uint32_t)new_physical_page - (uint32_t)physical_memory) >> OFFSET_BITS;

    printf("Mapped VA %p to PA %p\n", va, new_physical_page);
    return 0;
}

void *translate(pde_t *pgdir, void *va) {
    if (!pgdir || !va) return NULL;

    uint32_t vaddr = (uint32_t)va;
    uint32_t dir_index = GET_PAGE_DIR_INDEX(vaddr);
    uint32_t page_index = GET_PAGE_TABLE_INDEX(vaddr);
    uint32_t offset = GET_OFFSET(vaddr);

    printf("Translating VA: %p (dir_index: %u, page_index: %u, offset: %u)\n", 
           va, dir_index, page_index, offset);

    pde_t *dir_entry = &pgdir[dir_index];
    if (!dir_entry->present) {
        printf("Page directory entry not present\n");
        return NULL;
    }

    pte_t *page_table = (pte_t *)((dir_entry->page_table_addr << OFFSET_BITS) + (uint32_t)physical_memory);
    pte_t *pt_entry = &page_table[page_index];

    if (!pt_entry->present) {
        printf("Page table entry not present\n");
        return NULL;
    }

    void *phys_addr = (void *)((char *)physical_memory + 
                              (pt_entry->physical_page_number << OFFSET_BITS) + 
                              offset);
    printf("Translated VA %p to PA %p\n", va, phys_addr);
    return phys_addr;
}

void *n_malloc(size_t num_bytes) {
    if (num_bytes == 0) {
        printf("Error: Cannot allocate 0 bytes\n");
        return NULL;
    }

    // Calculate number of pages needed
    size_t num_pages = (num_bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    printf("Allocating %zu bytes (%zu pages)\n", num_bytes, num_pages);

    // Get virtual page number
    size_t vpage_num = get_next_avail_virtual_page();
    if (vpage_num == 0) {
        printf("Error: No available virtual pages\n");
        return NULL;
    }

    // Calculate virtual address
    void *va = (void *)(vpage_num * PAGE_SIZE);
    printf("Selected virtual address: %p\n", va);

    // Map the page
    if (map_page(va) != 0) {
        printf("Error: Failed to map page\n");
        CLEAR_BIT(virtual_bitmap, vpage_num);
        return NULL;
    }

    printf("Successfully allocated memory at VA: %p\n", va);
    return va;
}

void cleanup_physical_mem() {
    if (physical_memory) {
        free(physical_memory);
        physical_memory = NULL;
    }
    if (physical_bitmap) {
        free(physical_bitmap);
        physical_bitmap = NULL;
    }
    if (virtual_bitmap) {
        free(virtual_bitmap);
        virtual_bitmap = NULL;
    }
    page_directory = NULL;
}