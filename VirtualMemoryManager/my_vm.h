#ifndef MY_VM_H_INCLUDED
#define MY_VM_H_INCLUDED
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

//Assume the address space is 32 bits, so the max memory size is 4GB
//Page size is 4KB

#define PGSIZE 4096

// Maximum size of virtual memory
#define MAX_MEMSIZE 4ULL*1024*1024*1024

// Size of "physical memory"
#define MEMSIZE 1024*1024*1024

// Page table and directory entry definitions
typedef unsigned long pte_t;
typedef unsigned long pde_t;

// Bit manipulation macros
#define SET_BIT(bitmap, index) (bitmap[(index)/8] |= (1 << ((index)%8)))
#define CLEAR_BIT(bitmap, index) (bitmap[(index)/8] &= ~(1 << ((index)%8)))
#define GET_BIT(bitmap, index) ((bitmap[(index)/8] >> ((index)%8)) & 1)

// Page table constants
#define OFFSET_BITS 12
#define PAGE_TABLE_BITS 10
#define PAGE_DIR_BITS 10

#define PAGE_SIZE PGSIZE
#define PAGE_TABLE_ENTRIES (1 << PAGE_TABLE_BITS)
#define OFFSET_MASK ((1 << OFFSET_BITS) - 1)
#define PAGE_TABLE_MASK ((1 << PAGE_TABLE_BITS) - 1)

#define TOTAL_VIRTUAL_PAGES (MAX_MEMSIZE/PAGE_SIZE)
#define TOTAL_PHYSICAL_PAGES (MEMSIZE/PAGE_SIZE)

// TLB configuration
#define TLB_ENTRIES 512

struct tlb {
    unsigned long *vpn;     
    unsigned long *ppn;     
    unsigned char *valid;   
};
extern struct tlb tlb_store;  

// Global variables
extern void *physical_memory;
extern unsigned char *physical_bitmap;
extern unsigned char *virtual_bitmap;
extern pde_t *page_directory;
extern pthread_mutex_t tlb_mutex;
extern pthread_mutex_t init_mutex;

// Core functions
void set_physical_mem();
pte_t* translate(pde_t *pgdir, void *va);
int map_page(pde_t *pgdir, void *va, void* pa);
void *get_next_avail(int num_pages);

// Memory management functions
void *n_malloc(unsigned int num_bytes);
void n_free(void *va, int size);

// Data operations
int put_data(void *va, void *val, int size);
void get_data(void *va, void *val, int size);
void mat_mult(void *mat1, void *mat2, int size, void *answer);

// TLB functions
int TLB_add(void *va, void *pa);
pte_t *TLB_check(void *va);
void print_TLB_missrate();

// Helper macros
#define GET_PAGE_DIR_INDEX(va) ((unsigned long)va >> (PAGE_TABLE_BITS + OFFSET_BITS))
#define GET_PAGE_TABLE_INDEX(va) (((unsigned long)va >> OFFSET_BITS) & PAGE_TABLE_MASK)
#define GET_OFFSET(va) ((unsigned long)va & OFFSET_MASK)
#define GET_VPN(va) ((unsigned long)va >> OFFSET_BITS)

#endif