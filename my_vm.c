#include "my_vm.h"
#include <sys/mman.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void *physical_memory = NULL;
unsigned char *physical_bitmap = NULL;
unsigned char *virtual_bitmap = NULL;
pde_t *page_directory = NULL;

void set_physical_mem(){
    physical_memory = malloc(MEMSIZE);

    if(physical_memory == NULL){
        perror("Malloc failed");
        exit(1);
    }

    memset(physical_memory, 0, MEMSIZE);
    printf("Successfully allocated %lu bytes of memory\n",(unsigned long)MEMSIZE);

    size_t physical_bitmap_size = (TOTAL_PHYSICAL_PAGES+7)/8;
    size_t virtual_bitmap_size = (TOTAL_VIRTUAL_PAGES+7)/8;

    physical_bitmap = malloc(physical_bitmap_size);
    virtual_bitmap = malloc(virtual_bitmap_size);

    if(!physical_bitmap || !virtual_bitmap){
        perror("Bitmap allocation failed");
        cleanup_physical_mem();
        exit(1);
    }

    memset(physical_bitmap, 0, physical_bitmap_size);
    memset(virtual_bitmap, 0, virtual_bitmap_size);

    page_directory = (pde_t *)physical_memory;
    memset(page_directory, 0, PAGE_SIZE);

    SET_BIT(physical_bitmap, 0);

    printf("Memory manager initialized:\n");
    printf("Total physical pages: %lu\n", TOTAL_PHYSICAL_PAGES);
    printf("Total virtual pages: %lu\n", TOTAL_VIRTUAL_PAGES);
    printf("Page directory initialized at physical address: %p\n", (void*)page_directory);

}

void* translate(pde_t *pgdir, void *va){
    uint32_t vaddr = (uint32_t)va;
    uint32_t dir_index = GET_PAGE_DIR_INDEX(vaddr);
    uint32_t page_index = GET_PAGE_TABLE_INDEX(vaddr);
    uint32_t offset = GET_OFFSET(vaddr);

    pde_t *dir_entry = &pgdir[dir_index];
    if(!dir_entry->present){
        printf("Page directory not present.\n");
        return NULL;
    }

    pte_t *page_table = (pte_t *)(dir_entry->page_table_addr << OFFSET_BITS);

    pte_t *pt_entry = &page_table[page_index];
    if(!pt_entry->present){
        printf("Page Table Entry not present.\n");
        return NULL;
    }

    uint32_t physical_page = pt_entry->physical_page_number << OFFSET_BITS;
    uint32_t physical_addr = physical_page | offset;

    return (void *)physical_addr;

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

int map_page(void *va){
    uint32_t vaddr = (uint32_t)va;
    uint32_t dir_index = GET_PAGE_DIR_INDEX(vaddr);
    uint32_t page_index = GET_PAGE_TABLE_INDEX(vaddr);

    if(translate(page_directory, va) != NULL){
        printf("Error: Virtual address %p is already mapped\n", va);
        return -1;
    }

    pde_t *dir_entry = &page_directory[dir_index];

    if(!dir_entry->present){
        void *new_page_table =get_next_avail_page();
        if (new_page_table == NULL) {
            printf("Error: Could not allocate new page table\n");
            return -1;
        }

        memset(new_page_table, 0, PAGE_SIZE);
        dir_entry->present = 1;
        dir_entry->read_write = 1;
        dir_entry->user_supervisor = 1;

        dir_entry->page_table_addr = ((uint32_t)new_page_table - (uint32_t)physical_memory) >> OFFSET_BITS;
    }

     pte_t *page_table = (pte_t *)((dir_entry->page_table_addr << OFFSET_BITS) + (uint32_t)physical_memory);

     pte_t *pt_entry = &page_table[page_index];
    
    if (pt_entry->present) {
        printf("Error: Page table entry is already present\n");
        return -1;
    }

    void *new_physical_page = get_next_avail_page();
    if (new_physical_page == NULL) {
        printf("Error: Could not allocate new physical page\n");
        return -1;
    }

    memset(new_physical_page, 0, PAGE_SIZE);

    pt_entry->present = 1;
    pt_entry->read_write = 1;
    pt_entry->user_supervisor = 1;
    pt_entry->physical_page_number = ((uint32_t)new_physical_page - (uint32_t)physical_memory) >> OFFSET_BITS;

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