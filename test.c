#include "my_vm.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

void test_single_page_allocation() {
    printf("\n=== Testing Single Page Allocation ===\n");
    
    // Allocate a single page
    void *ptr = n_malloc(100);
    printf("Allocated address: %p\n", ptr);
    assert(ptr != NULL);
    
    // Try to translate the address
    void *phys_addr = translate(page_directory, ptr);
    printf("Physical address: %p\n", phys_addr);
    assert(phys_addr != NULL);
    
    // Try writing to the memory
    memset(phys_addr, 0xAA, 100);
    printf("Successfully wrote to allocated memory\n");
}

void test_multiple_pages() {
    printf("\n=== Testing Multiple Page Allocation ===\n");
    
    // Allocate multiple pages
    void *ptr = n_malloc(PAGE_SIZE + 100);
    printf("Allocated address for multiple pages: %p\n", ptr);
    assert(ptr != NULL);
    
    // Verify translation
    void *phys_addr = translate(page_directory, ptr);
    printf("Physical address: %p\n", phys_addr);
    assert(phys_addr != NULL);
}

int main() {
    printf("Starting basic memory management tests...\n");
    
    set_physical_mem();
    
    test_single_page_allocation();
    test_multiple_pages();
    
    cleanup_physical_mem();
    printf("\nAll tests completed successfully!\n");
    return 0;
}