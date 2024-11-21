#include "my_vm.h"
#include <sys/mman.h>
#include <string.h>
#include <pthread.h>

void *physical_memory = NULL;
unsigned char *physical_bitmap = NULL;
unsigned char *virtual_bitmap = NULL;
pde_t *page_directory = NULL;
struct tlb tlb_store; 
pthread_mutex_t tlb_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t virtual_mem_mutex = PTHREAD_MUTEX_INITIALIZER;

unsigned long long tlb_hits = 0;
unsigned long long tlb_misses = 0;

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
    physical_memory = malloc(MEMSIZE);
    if (!physical_memory) {
        perror("Physical memory allocation failed");
        exit(1);
    }
    memset(physical_memory, 0, MEMSIZE);

    size_t physical_bitmap_size = (TOTAL_PHYSICAL_PAGES + 7) / 8;
    size_t virtual_bitmap_size = (TOTAL_VIRTUAL_PAGES + 7) / 8;

    physical_bitmap = malloc(physical_bitmap_size);
    virtual_bitmap = malloc(virtual_bitmap_size);
    
    if (!physical_bitmap || !virtual_bitmap) {
        perror("Bitmap allocation failed");
        free(physical_memory);
        exit(1);
    }

    memset(physical_bitmap, 0, physical_bitmap_size);
    memset(virtual_bitmap, 0, virtual_bitmap_size);

    // Initialize page directory
    page_directory = (pde_t *)physical_memory;
    memset(page_directory, 0, PAGE_SIZE);
    SET_BIT(physical_bitmap, 0);

    // Initialize TLB arrays
    tlb_store.vpn = (unsigned long *)calloc(TLB_ENTRIES, sizeof(unsigned long));
    tlb_store.ppn = (unsigned long *)calloc(TLB_ENTRIES, sizeof(unsigned long));
    tlb_store.valid = (unsigned char *)calloc(TLB_ENTRIES, sizeof(unsigned char));
    
    if (!tlb_store.vpn || !tlb_store.ppn || !tlb_store.valid) {
        perror("TLB allocation failed");
        cleanup_physical_mem();
        exit(1);
    }
}

int TLB_add(void *va, void *pa) {
    pthread_mutex_lock(&tlb_mutex);
    
    unsigned long vpn = GET_VPN(va);
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
    
    unsigned long vpn = GET_VPN(va);
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

void print_TLB_missrate() {
    pthread_mutex_lock(&tlb_mutex);
    double total = tlb_hits + tlb_misses;
    double miss_rate = total > 0 ? (tlb_misses / total) * 100.0 : 0.0;
    fprintf(stderr, "TLB miss rate %lf\n", miss_rate);
    pthread_mutex_unlock(&tlb_mutex);
}

pte_t *translate(pde_t *pgdir, void *va) {
    pte_t *tlb_result = TLB_check(va);
    if (tlb_result) return tlb_result;

    unsigned long vaddr = (unsigned long)va;
    unsigned long dir_idx = GET_PAGE_DIR_INDEX(vaddr);
    unsigned long page_idx = GET_PAGE_TABLE_INDEX(vaddr);
    unsigned long offset = GET_OFFSET(vaddr);

    pde_t *dir_entry = &pgdir[dir_idx];
    if (!(*dir_entry & 0x1)) return NULL;  // Present bit check

    pte_t *page_table = (pte_t *)((*dir_entry & ~0xFFF) + (unsigned long)physical_memory);
    pte_t *pt_entry = &page_table[page_idx];
    
    if (!(*pt_entry & 0x1)) return NULL;  // Present bit check

    void *pa = physical_memory + (*pt_entry & ~0xFFF) + offset;
    TLB_add(va, pa);
    return (pte_t *)pa;
}

int map_page(pde_t *pgdir, void *va, void *pa) {
    unsigned long vaddr = (unsigned long)va;
    unsigned long dir_idx = GET_PAGE_DIR_INDEX(vaddr);
    unsigned long page_idx = GET_PAGE_TABLE_INDEX(vaddr);

    pde_t *dir_entry = &pgdir[dir_idx];
    
    if (!(*dir_entry & 0x1)) {
        void *new_pt = get_next_avail(1);
        if (!new_pt) return -1;
        
        memset(new_pt, 0, PAGE_SIZE);
        *dir_entry = ((unsigned long)new_pt - (unsigned long)physical_memory) | 0x7;
    }

    pte_t *page_table = (pte_t *)((*dir_entry & ~0xFFF) + (unsigned long)physical_memory);
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
    if (!physical_memory) set_physical_mem();
    
    unsigned int num_pages = (num_bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    void *va = NULL;
    
    pthread_mutex_lock(&virtual_mem_mutex);
    for (size_t i = 1; i < TOTAL_VIRTUAL_PAGES; i++) {
        if (!GET_BIT(virtual_bitmap, i)) {
            int found = 1;
            for (int j = 1; j < num_pages && (i + j) < TOTAL_VIRTUAL_PAGES; j++) {
                if (GET_BIT(virtual_bitmap, i + j)) {
                    found = 0;
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
        void *current_va = (void *)((char *)va + (i * PAGE_SIZE));
        unsigned long vaddr = (unsigned long)current_va;
        unsigned long dir_idx = GET_PAGE_DIR_INDEX(vaddr);
        unsigned long page_idx = GET_PAGE_TABLE_INDEX(vaddr);
        
        // Get directory entry
        pde_t *dir_entry = &page_directory[dir_idx];
        if (!(*dir_entry & 0x1)) continue;  // Not present
        
        // Get page table
        pte_t *page_table = (pte_t *)((*dir_entry & ~0xFFF) + (unsigned long)physical_memory);
        pte_t *pt_entry = &page_table[page_idx];
        
        if (*pt_entry & 0x1) {  // If page is present
            // Get physical page number and clear physical bitmap
            unsigned long ppn = (*pt_entry & ~0xFFF) >> OFFSET_BITS;
            CLEAR_BIT(physical_bitmap, ppn);
            
            // Clear page table entry
            *pt_entry = 0;
            
            // Clear TLB entry if present
            pthread_mutex_lock(&tlb_mutex);
            for (int j = 0; j < TLB_ENTRIES; j++) {
                if (tlb_store.valid[j] && tlb_store.vpn[j] == GET_VPN(current_va)) {
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
    
    unsigned long offset = GET_OFFSET((unsigned long)va);
    int remaining = size;
    int src_offset = 0;
    
    while (remaining > 0) {
        void *curr_va = va + src_offset;
        pte_t *pa = translate(page_directory, curr_va);
        if (!pa) return -1;
        
        int chunk = PAGE_SIZE - offset;
        if (chunk > remaining) chunk = remaining;
        
        memcpy(pa, val + src_offset, chunk);
        remaining -= chunk;
        src_offset += chunk;
        offset = 0;
    }
    
    return 0;
}

void get_data(void *va, void *val, int size) {
    if (!va || !val || size <= 0) return;
    
    unsigned long offset = GET_OFFSET((unsigned long)va);
    int remaining = size;
    int dst_offset = 0;
    
    while (remaining > 0) {
        void *curr_va = va + dst_offset;
        pte_t *pa = translate(page_directory, curr_va);
        if (!pa) return;
        
        int chunk = PAGE_SIZE - offset;
        if (chunk > remaining) chunk = remaining;
        
        memcpy(val + dst_offset, pa, chunk);
        remaining -= chunk;
        dst_offset += chunk;
        offset = 0;
    }
}

void mat_mult(void *mat1, void *mat2, int size, void *answer) {
    int *buffer1 = malloc(size * size * sizeof(int));
    int *buffer2 = malloc(size * size * sizeof(int));
    int *result = malloc(size * size * sizeof(int));
    
    if (!buffer1 || !buffer2 || !result) {
        free(buffer1);
        free(buffer2);
        free(result);
        return;
    }
    
    get_data(mat1, buffer1, size * size * sizeof(int));
    get_data(mat2, buffer2, size * size * sizeof(int));
    
    for (int i = 0; i < size; i++) {
        for (int j = 0; j < size; j++) {
            result[i * size + j] = 0;
            for (int k = 0; k < size; k++) {
                result[i * size + j] += buffer1[i * size + k] * buffer2[k * size + j];
            }
        }
    }
    
    put_data(answer, result, size * size * sizeof(int));
    
    free(buffer1);
    free(buffer2);
    free(result);
}


