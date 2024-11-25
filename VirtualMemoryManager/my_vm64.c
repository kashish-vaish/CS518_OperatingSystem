#include "my_vm.h"
#include <sys/mman.h>
#include <string.h>
#include <math.h>

// Initialize global variables
void *physical_memory = NULL;
unsigned char *physical_bitmap = NULL;
unsigned char *virtual_bitmap = NULL;
pde_t *page_directory = NULL;
struct tlb tlb_store = {NULL, NULL, NULL};
pthread_mutex_t tlb_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t virtual_mem_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t init_mutex = PTHREAD_MUTEX_INITIALIZER;

unsigned long long tlb_hits = 0;
unsigned long long tlb_misses = 0;
int memory_initialized = 0;

// Dynamic page size variables
unsigned long PGSIZE = BASE_PGSIZE;
unsigned int OFFSET_BITS = 12;
unsigned int PAGE_TABLE_BITS = 10;
unsigned int PAGE_DIR_BITS = 10;
unsigned long OFFSET_MASK = 0xFFF;
unsigned long PAGE_TABLE_MASK = 0x3FF;
unsigned long PAGE_DIR_MASK = 0x3FF;

int initialize_page_size(unsigned long page_size) {
    if (memory_initialized) {
        fprintf(stderr, "Cannot change page size after memory initialization\n");
        return -1;
    }
    
    if (page_size < BASE_PGSIZE || (page_size & (page_size - 1)) != 0) {
        fprintf(stderr, "Page size must be a power of 2 and at least 4KB\n");
        return -1;
    }

    PGSIZE = page_size;
    OFFSET_BITS = (unsigned int)log2(page_size);
    
    // Split remaining bits between directory and table
    unsigned int remaining_bits = 32 - OFFSET_BITS;
    PAGE_TABLE_BITS = remaining_bits / 2;
    PAGE_DIR_BITS = remaining_bits - PAGE_TABLE_BITS;
    
    // Update masks
    OFFSET_MASK = page_size - 1;
    PAGE_TABLE_MASK = (1UL << PAGE_TABLE_BITS) - 1;
    PAGE_DIR_MASK = (1UL << PAGE_DIR_BITS) - 1;
    
    return 0;
}

// Helper functions for address translation
unsigned long get_page_dir_index(void* va) {
    return ((unsigned long)va >> (PAGE_TABLE_BITS + OFFSET_BITS)) & PAGE_DIR_MASK;
}

unsigned long get_page_table_index(void* va) {
    return ((unsigned long)va >> OFFSET_BITS) & PAGE_TABLE_MASK;
}

unsigned long get_page_offset(void* va) {
    return (unsigned long)va & OFFSET_MASK;
}

unsigned long get_vpn(void* va) {
    return (unsigned long)va >> OFFSET_BITS;
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
    if (tlb_store.vpn) free(tlb_store.vpn);
    if (tlb_store.ppn) free(tlb_store.ppn);
    if (tlb_store.valid) free(tlb_store.valid);
    tlb_store.vpn = NULL;
    tlb_store.ppn = NULL;
    tlb_store.valid = NULL;
}

void set_physical_mem() {
    pthread_mutex_lock(&init_mutex);
    
    if (memory_initialized) {
        pthread_mutex_unlock(&init_mutex);
        return;
    }

    physical_memory = malloc(MEMSIZE);
    if (!physical_memory) {
        perror("Physical memory allocation failed");
        pthread_mutex_unlock(&init_mutex);
        exit(1);
    }
    memset(physical_memory, 0, MEMSIZE);

    size_t physical_bitmap_size = (TOTAL_PHYSICAL_PAGES + 7) / 8;
    size_t virtual_bitmap_size = (TOTAL_VIRTUAL_PAGES + 7) / 8;

    physical_bitmap = malloc(physical_bitmap_size);
    virtual_bitmap = malloc(virtual_bitmap_size);
    
    if (!physical_bitmap || !virtual_bitmap) {
        perror("Bitmap allocation failed");
        cleanup_physical_mem();
        pthread_mutex_unlock(&init_mutex);
        exit(1);
    }

    memset(physical_bitmap, 0, physical_bitmap_size);
    memset(virtual_bitmap, 0, virtual_bitmap_size);

    page_directory = (pde_t *)physical_memory;
    memset(page_directory, 0, PAGE_SIZE);
    SET_BIT(physical_bitmap, 0);

    tlb_store.vpn = (unsigned long *)calloc(TLB_ENTRIES, sizeof(unsigned long));
    tlb_store.ppn = (unsigned long *)calloc(TLB_ENTRIES, sizeof(unsigned long));
    tlb_store.valid = (unsigned char *)calloc(TLB_ENTRIES, sizeof(unsigned char));
    
    if (!tlb_store.vpn || !tlb_store.ppn || !tlb_store.valid) {
        perror("TLB allocation failed");
        cleanup_physical_mem();
        pthread_mutex_unlock(&init_mutex);
        exit(1);
    }

    memory_initialized = 1;
    pthread_mutex_unlock(&init_mutex);
}

int TLB_add(void *va, void *pa) {
    pthread_mutex_lock(&tlb_mutex);
    
    unsigned long vpn = get_vpn(va);
    unsigned long ppn = ((unsigned long)pa - (unsigned long)physical_memory) >> OFFSET_BITS;
    unsigned long index = vpn % TLB_ENTRIES;
    
    tlb_store.vpn[index] = vpn;
    tlb_store.ppn[index] = ppn;
    tlb_store.valid[index] = 1;
    
    pthread_mutex_unlock(&tlb_mutex);
    return 0;
}

pte_t *TLB_check(void *va) {
    pthread_mutex_lock(&tlb_mutex);
    
    unsigned long vpn = get_vpn(va);
    unsigned long index = vpn % TLB_ENTRIES;
    
    if (tlb_store.valid[index] && tlb_store.vpn[index] == vpn) {
        tlb_hits++;
        void *pa = physical_memory + (tlb_store.ppn[index] << OFFSET_BITS);
        pthread_mutex_unlock(&tlb_mutex);
        return (pte_t *)pa;
    }
    
    tlb_misses++;
    pthread_mutex_unlock(&tlb_mutex);
    return NULL;
}

pte_t* translate(pde_t *pgdir, void *va) {
    pte_t *tlb_result = TLB_check(va);
    if (tlb_result) return tlb_result;

    unsigned long dir_idx = get_page_dir_index(va);
    unsigned long page_idx = get_page_table_index(va);
    unsigned long offset = get_page_offset(va);

    pde_t *dir_entry = &pgdir[dir_idx];
    if (!(*dir_entry & 0x1)) return NULL;

    pte_t *page_table = (pte_t *)((*dir_entry & ~(PGSIZE-1)) + (unsigned long)physical_memory);
    pte_t *pt_entry = &page_table[page_idx];
    
    if (!(*pt_entry & 0x1)) return NULL;

    void *pa = physical_memory + (*pt_entry & ~(PGSIZE-1)) + offset;
    TLB_add(va, pa);
    return (pte_t *)pa;
}

int map_page(pde_t *pgdir, void *va, void *pa) {
    unsigned long dir_idx = get_page_dir_index(va);
    unsigned long page_idx = get_page_table_index(va);

    pde_t *dir_entry = &pgdir[dir_idx];
    
    if (!(*dir_entry & 0x1)) {
        void *new_pt = get_next_avail(1);
        if (!new_pt) return -1;
        
        memset(new_pt, 0, PAGE_SIZE);
        *dir_entry = ((unsigned long)new_pt - (unsigned long)physical_memory) | 0x7;
    }

    pte_t *page_table = (pte_t *)((*dir_entry & ~(PGSIZE-1)) + (unsigned long)physical_memory);
    pte_t *pt_entry = &page_table[page_idx];
    
    if (*pt_entry & 0x1) return -1;

    *pt_entry = ((unsigned long)pa - (unsigned long)physical_memory) | 0x7;
    return 0;
}

void *get_next_avail(int num_pages) {
    pthread_mutex_lock(&virtual_mem_mutex);
    
    for (size_t i = 1; i < TOTAL_PHYSICAL_PAGES; i++) {
        if (!GET_BIT(physical_bitmap, i)) {
            int found = 1;
            for (int j = 1; j < num_pages && (i + j) < TOTAL_PHYSICAL_PAGES; j++) {
                if (GET_BIT(physical_bitmap, i + j)) {
                    found = 0;
                    break;
                }
            }
            
            if (found) {
                for (int j = 0; j < num_pages; j++) {
                    SET_BIT(physical_bitmap, i + j);
                }
                pthread_mutex_unlock(&virtual_mem_mutex);
                return physical_memory + (i * PAGE_SIZE);
            }
        }
    }
    
    pthread_mutex_unlock(&virtual_mem_mutex);
    return NULL;
}

void *n_malloc(unsigned int num_bytes) {
    if (!memory_initialized) {
        set_physical_mem();
    }
    if (num_bytes == 0) return NULL;
    
    unsigned int num_pages = (num_bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    void *va = NULL;
    
    pthread_mutex_lock(&virtual_mem_mutex);
    for (size_t i = 1; i < TOTAL_VIRTUAL_PAGES; i++) {
        if (!GET_BIT(virtual_bitmap, i)) {
            int found = 1;
            for (int j = 1; j < num_pages && (i + j) < TOTAL_VIRTUAL_PAGES; j++) {
                if (GET_BIT(virtual_bitmap, i + j)) {
                    found = 0;
                    i += j;
                    break;
                }
            }
            
            if (found) {
                for (int j = 0; j < num_pages; j++) {
                    SET_BIT(virtual_bitmap, i + j);
                }
                va = (void *)(i * PAGE_SIZE);
                break;
            }
        }
    }
    pthread_mutex_unlock(&virtual_mem_mutex);
    
    if (!va) return NULL;

    for (unsigned int i = 0; i < num_pages; i++) {
        void *pa = get_next_avail(1);
        if (!pa || map_page(page_directory, va + (i * PAGE_SIZE), pa) != 0) {
            n_free(va, i * PAGE_SIZE);
            return NULL;
        }
    }
    
    return va;
}

void n_free(void *va, int size) {
    if (!va || size <= 0) return;
    
    // Calculate number of pages
    unsigned int num_pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    unsigned long start_vpn = (unsigned long)va / PAGE_SIZE;
    
    pthread_mutex_lock(&virtual_mem_mutex);
    
    // For each page
    for (unsigned int i = 0; i < num_pages; i++) {
        void *current_va = (void *)((unsigned long)va + (i * PAGE_SIZE));
        unsigned long dir_idx = get_page_dir_index(current_va);
        unsigned long page_idx = get_page_table_index(current_va);
        
        // Get directory entry
        pde_t *dir_entry = &page_directory[dir_idx];
        if (!(*dir_entry & 0x1)) continue;  // Not present
        
        // Get page table
        pte_t *page_table = (pte_t *)((*dir_entry & ~(PGSIZE-1)) + (unsigned long)physical_memory);
        pte_t *pt_entry = &page_table[page_idx];
        
        if (*pt_entry & 0x1) {  // If page is present
            // Get physical page number and clear physical bitmap
            unsigned long ppn = (*pt_entry & ~(PGSIZE-1)) >> OFFSET_BITS;
            CLEAR_BIT(physical_bitmap, ppn);
            
            // Clear page table entry
            *pt_entry = 0;
            
            // Clear TLB entry if present
            pthread_mutex_lock(&tlb_mutex);
            unsigned long vpn = get_vpn(current_va);
            for (int j = 0; j < TLB_ENTRIES; j++) {
                if (tlb_store.valid[j] && tlb_store.vpn[j] == vpn) {
                    tlb_store.valid[j] = 0;
                }
            }
            pthread_mutex_unlock(&tlb_mutex);
        }
        
        // Clear virtual bitmap
        CLEAR_BIT(virtual_bitmap, start_vpn + i);
    }
    
    pthread_mutex_unlock(&virtual_mem_mutex);
    printf("Freed %d pages starting at virtual address %p\n", num_pages, va);
}

int put_data(void *va, void *val, int size) {
    if (!va || !val || size <= 0) return -1;
    
    unsigned long offset = get_page_offset(va);
    int remaining = size;
    int src_offset = 0;
    
    while (remaining > 0) {
        void *curr_va = (void *)((unsigned long)va + src_offset);
        pte_t *pa = translate(page_directory, curr_va);
        if (!pa) return -1;
        
        int chunk = PAGE_SIZE - offset;
        if (chunk > remaining) chunk = remaining;
        
        memcpy(pa, (char *)val + src_offset, chunk);
        remaining -= chunk;
        src_offset += chunk;
        offset = 0;
    }
    
    return 0;
}

void get_data(void *va, void *val, int size) {
    if (!va || !val || size <= 0) return;
    
    unsigned long offset = get_page_offset(va);
    int remaining = size;
    int dst_offset = 0;
    
    while (remaining > 0) {
        void *curr_va = (void *)((unsigned long)va + dst_offset);
        pte_t *pa = translate(page_directory, curr_va);
        if (!pa) return;
        
        int chunk = PAGE_SIZE - offset;
        if (chunk > remaining) chunk = remaining;
        
        memcpy((char *)val + dst_offset, pa, chunk);
        remaining -= chunk;
        dst_offset += chunk;
        offset = 0;
    }
}

void mat_mult(void *mat1, void *mat2, int size, void *answer) {
    // Allocate temporary buffers for matrix operation
    int *buffer1 = malloc(size * size * sizeof(int));
    int *buffer2 = malloc(size * size * sizeof(int));
    int *result = malloc(size * size * sizeof(int));
    
    if (!buffer1 || !buffer2 || !result) {
        free(buffer1);
        free(buffer2);
        free(result);
        return;
    }
    
    // Get matrix data from virtual memory
    get_data(mat1, buffer1, size * size * sizeof(int));
    get_data(mat2, buffer2, size * size * sizeof(int));
    
    // Perform matrix multiplication
    for (int i = 0; i < size; i++) {
        for (int j = 0; j < size; j++) {
            result[i * size + j] = 0;
            for (int k = 0; k < size; k++) {
                result[i * size + j] += buffer1[i * size + k] * buffer2[k * size + j];
            }
        }
    }
    
    // Store result back in virtual memory
    put_data(answer, result, size * size * sizeof(int));
    
    // Clean up
    free(buffer1);
    free(buffer2);
    free(result);
}

void print_TLB_missrate() {
    pthread_mutex_lock(&tlb_mutex);
    
    double total = tlb_hits + tlb_misses;
    double miss_rate = total > 0 ? (tlb_misses / total) * 100.0 : 0.0;
    
    fprintf(stderr, "Number of TLB Misses: %lld\n", tlb_misses);
    fprintf(stderr, "Number of TLB Hits: %lld\n", tlb_hits);
    fprintf(stderr, "TLB miss rate: %lf%%\n", miss_rate);
    
    pthread_mutex_unlock(&tlb_mutex);
}