#ifndef MY_VM64_H_INCLUDED
#define MY_VM64_H_INCLUDED
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

// 48-bit virtual address space (as used in x86_64)
#define MAX_MEMSIZE 0x1000000000000ULL

// Physical memory size 
#define MEMSIZE (4ULL*1024*1024*1024)   

// Page size remains 4KB
#define PGSIZE 4096
#define PAGE_SIZE PGSIZE


typedef unsigned long pte_t;
typedef unsigned long pde_t;


#define SET_BIT(bitmap, index) (bitmap[(index)/8] |= (1 << ((index)%8)))
#define CLEAR_BIT(bitmap, index) (bitmap[(index)/8] &= ~(1 << ((index)%8)))
#define GET_BIT(bitmap, index) ((bitmap[(index)/8] >> ((index)%8)) & 1)

// 4-level page table constants
#define OFFSET_BITS 12
#define PAGE_TABLE_BITS 9   
#define PAGE_LEVELS 4        


#define OFFSET_MASK ((1ULL << OFFSET_BITS) - 1)
#define PAGE_TABLE_MASK ((1ULL << PAGE_TABLE_BITS) - 1)
#define PAGE_SHIFT(level) (OFFSET_BITS + (PAGE_TABLE_BITS * (level)))

// Calculate total pages
#define TOTAL_VIRTUAL_PAGES (MAX_MEMSIZE/PAGE_SIZE)
#define TOTAL_PHYSICAL_PAGES (MEMSIZE/PAGE_SIZE)


#define TLB_ENTRIES 512

struct tlb {
    unsigned long *vpn;     
    unsigned long *ppn;     
    unsigned char *valid;  
};

// Global variables
extern void *physical_memory;
extern unsigned char *physical_bitmap;
extern unsigned char *virtual_bitmap;
extern pde_t *page_directory;        
extern struct tlb tlb_store;
extern pthread_mutex_t tlb_mutex;
extern pthread_mutex_t virtual_mem_mutex;
extern pthread_mutex_t init_mutex;

// Helper macros for address translation
#define GET_L4_INDEX(va) ((unsigned long)(va) >> (PAGE_SHIFT(3)) & PAGE_TABLE_MASK)
#define GET_L3_INDEX(va) ((unsigned long)(va) >> (PAGE_SHIFT(2)) & PAGE_TABLE_MASK)
#define GET_L2_INDEX(va) ((unsigned long)(va) >> (PAGE_SHIFT(1)) & PAGE_TABLE_MASK)
#define GET_L1_INDEX(va) ((unsigned long)(va) >> (PAGE_SHIFT(0)) & PAGE_TABLE_MASK)
#define GET_OFFSET(va) ((unsigned long)(va) & OFFSET_MASK)
#define GET_VPN(va) ((unsigned long)(va) >> OFFSET_BITS)


void set_physical_mem();
pte_t* translate(pde_t *pgdir, void *va);
int map_page(pde_t *pgdir, void *va, void* pa);
void *get_next_avail(int num_pages);


void *n_malloc(unsigned int num_bytes);
void n_free(void *va, int size);


int put_data(void *va, void *val, int size);
void get_data(void *va, void *val, int size);
void mat_mult(void *mat1, void *mat2, int size, void *answer);


int TLB_add(void *va, void *pa);
pte_t *TLB_check(void *va);
void print_TLB_missrate();

#endif