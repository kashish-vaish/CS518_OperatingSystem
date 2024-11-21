#ifndef MY_VM_H
#define MY_VM_H

#include <stdint.h>
#include <stdlib.h>

#define MAX_MEMSIZE (4ULL * 1024 * 1024 * 1024)
#define MEMSIZE (1024 * 1024 * 1024)
#define PAGE_SIZE 4096

#define OFFSET_BITS 12
#define PAGE_TABLE_BITS 10
#define PAGE_DIR_BITS 10

#define PAGE_TABLE_ENTRIES (1 << PAGE_TABLE_BITS)
#define OFFSET_MASK ((1 << OFFSET_BITS) - 1)
#define PAGE_TABLE_MASK ((1 << PAGE_TABLE_BITS) - 1)

#define TOTAL_VIRTUAL_PAGES (MAX_MEMSIZE/PAGE_SIZE)
#define TOTAL_PHYSICAL_PAGES (MEMSIZE/PAGE_SIZE)

extern void *physical_memory;
extern unsigned char *physical_bitmap;
extern unsigned char *virtual_bitmap;


#define SET_BIT(bitmap, index) (bitmap[(index)/8] |= (1 << ((index)%8)))
#define CLEAR_BIT(bitmap, index) (bitmap[(index)/8] &= ~(1 << ((index)%8)))
#define GET_BIT(bitmap, index) ((bitmap[(index)/8] >> ((index)%8)) & 1)

// Page Table Entry structure
typedef struct {
    uint32_t physical_page_number : 20;  
    uint32_t present : 1;                
    uint32_t read_write : 1;            
    uint32_t user_supervisor : 1;      
    uint32_t accessed : 1;             
    uint32_t dirty : 1;                
    uint32_t unused : 7;               
} pte_t;

// Page directory entry structure
typedef struct {
    uint32_t page_table_addr : 20;      
    uint32_t present : 1;               
    uint32_t read_write : 1;            
    uint32_t user_supervisor : 1;       
    uint32_t accessed : 1;              
    uint32_t unused : 8;                
} pde_t;


extern pde_t *page_directory;

void set_physical_mem();
void cleanup_physical_mem();
void* get_next_avail_page();
pte_t* get_page_table_entry(void* va);
void* translate (pde_t *pgdir, void *va);
void *n_malloc(size_t num_bytes);

int map_page(void *va);


#define GET_PAGE_DIR_INDEX(va) ((uint32_t)va >> (PAGE_TABLE_BITS + OFFSET_BITS))
#define GET_PAGE_TABLE_INDEX(va) (((uint32_t)va >> OFFSET_BITS) & PAGE_TABLE_MASK)
#define GET_OFFSET(va) ((uint32_t)va & OFFSET_MASK)

#endif 
