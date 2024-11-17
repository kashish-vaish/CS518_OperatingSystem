#include "my_vm.h"
#include <stdio.h>
#include <assert.h>

// Previous bitmap printing functions remain same
void print_bitmap_byte(unsigned char byte) {
    for (int i = 7; i >= 0; i--) {
        printf("%d", (byte >> i) & 1);
    }
}

void print_bitmap_range(unsigned char *bitmap, int bytes) {
    printf("Bitmap state (first %d bytes): ", bytes);
    for (int i = 0; i < bytes; i++) {
        print_bitmap_byte(bitmap[i]);
        printf(" ");
    }
    printf("\n");
}

int main() {
    printf("\n=== Testing Memory Management System ===\n\n");

    // Test 1: Initialize physical memory (previous test)
    printf("Test 1: Initializing physical memory...\n");
    set_physical_mem();
    assert(physical_memory != NULL);
    assert(physical_bitmap != NULL);
    assert(virtual_bitmap != NULL);
    printf("Memory initialized successfully\n\n");

    // Test 2: Single page allocation
    printf("Test 2: Testing single page allocation...\n");
    void *ptr1 = n_malloc(PAGE_SIZE);
    assert(ptr1 != NULL);
    printf("Single page allocated at virtual address: %p\n", ptr1);
    print_bitmap_range(virtual_bitmap, 2);
    print_bitmap_range(physical_bitmap, 2);
    
    // Test 3: Multiple page allocation
    printf("\nTest 3: Testing multiple page allocation...\n");
    void *ptr2 = n_malloc(PAGE_SIZE * 2 + 100); // Should allocate 3 pages
    assert(ptr2 != NULL);
    printf("Multiple pages allocated at virtual address: %p\n", ptr2);
    print_bitmap_range(virtual_bitmap, 2);
    print_bitmap_range(physical_bitmap, 2);

    // Test 4: Small allocation (should still allocate one page)
    printf("\nTest 4: Testing small allocation...\n");
    void *ptr3 = n_malloc(100);
    assert(ptr3 != NULL);
    printf("Small allocation at virtual address: %p\n", ptr3);
    
    // Test 5: Check page alignment
    printf("\nTest 5: Checking page alignment...\n");
    assert(((unsigned long)ptr1 & (PAGE_SIZE - 1)) == 0);
    assert(((unsigned long)ptr2 & (PAGE_SIZE - 1)) == 0);
    assert(((unsigned long)ptr3 & (PAGE_SIZE - 1)) == 0);
    printf("All allocations are page-aligned\n");

    // Test 6: Test translation for allocated pages
    printf("\nTest 6: Testing translation for allocated pages...\n");
    void *phys1 = translate(page_directory, ptr1);
    void *phys2 = translate(page_directory, ptr2);
    void *phys3 = translate(page_directory, ptr3);
    assert(phys1 != NULL);
    assert(phys2 != NULL);
    assert(phys3 != NULL);
    printf("All virtual addresses translate to physical addresses successfully\n");

    // Test 7: Test invalid/zero allocation
    printf("\nTest 7: Testing invalid allocation...\n");
    void *ptr4 = n_malloc(0);
    assert(ptr4 == NULL);
    printf("Zero-size allocation correctly returned NULL\n");

    // Printing final bitmap states
    printf("\nFinal bitmap states:\n");
    printf("Virtual bitmap: \n");
    print_bitmap_range(virtual_bitmap, 4);
    printf("Physical bitmap: \n");
    print_bitmap_range(physical_bitmap, 4);

    // Cleanup
    printf("\nCleaning up...\n");
    cleanup_physical_mem();
    printf("Cleanup completed!\n");

    printf("\nAll n_malloc tests passed successfully!\n");
    return 0;
}

//test file before n_alloc
// #include "my_vm.h"
// #include <stdio.h>
// #include <assert.h>

// // Function to print bits of a byte in bitmap
// void print_bitmap_byte(unsigned char byte) {
//     for (int i = 7; i >= 0; i--) {
//         printf("%d", (byte >> i) & 1);
//     }
// }

// // Function to print first n bytes of bitmap
// void print_bitmap_range(unsigned char *bitmap, int bytes) {
//     printf("Bitmap state (first %d bytes): ", bytes);
//     for (int i = 0; i < bytes; i++) {
//         print_bitmap_byte(bitmap[i]);
//         printf(" ");
//     }
//     printf("\n");
// }

// int main() {
//     printf("\n=== Testing Memory Management System ===\n\n");

//     // Test 1: Initialize physical memory
//     printf("Test 1: Initializing physical memory...\n");
//     set_physical_mem();
//     assert(physical_memory != NULL);
//     assert(physical_bitmap != NULL);
//     printf("Physical memory and bitmap initialized successfully\n\n");

//     // Test 2: Check initial bitmap state
//     printf("Test 2: Checking initial bitmap state...\n");
//     printf("First page should be marked as used (for page directory)\n");
//     print_bitmap_range(physical_bitmap, 2);
//     // First page should be used
//     assert(GET_BIT(physical_bitmap, 0) == 1);  
//     printf("Initial bitmap state verified\n\n");

//     // Test 3: Test get_next_avail_page
//     printf("Test 3: Testing get_next_avail_page...\n");
//     void *page1 = get_next_avail_page();
//     void *page2 = get_next_avail_page();
//     assert(page1 != NULL);
//     assert(page2 != NULL);
//     assert(page1 != page2);
//     printf("Successfully allocated two different pages\n");
//     print_bitmap_range(physical_bitmap, 2);
//     printf("\n");

//     // Test 4: Test page mapping
//     printf("Test 4: Testing page mapping...\n");
//     // Use 0x1000 as test virtual address
//     void *va = (void*)0x1000;  
//     int result = map_page(va);
//     assert(result == 0);
//     printf("Successfully mapped virtual address 0x1000\n");

//     // Test 5: Test translation
//     printf("Test 5: Testing address translation...\n");
//     void *pa = translate(page_directory, va);
//     assert(pa != NULL);
//     printf("Successfully translated virtual address 0x1000 to physical address %p\n", pa);

//     // Test 6: Test duplicate mapping
//     printf("Test 6: Testing duplicate mapping prevention...\n");
//     result = map_page(va);
//     assert(result == -1);
//     printf("Successfully prevented duplicate mapping\n\n");

//     // Cleanup
//     printf("Cleaning up...\n");
//     cleanup_physical_mem();
//     printf("Cleanup completed!\n");

//     printf("\nAll tests passed successfully!\n");
//     return 0;
// }

//test.c after translate function
// #include "my_vm.h"
// #include <stdio.h>

// int main() {
//     printf("Testing Memory Management System\n");
//     printf("--------------------------------\n");

//     // Test 1: Initialize physical memory
//     printf("\nTest 1: Initializing physical memory...\n");
//     set_physical_mem();

//     // Test 2: Check if first bit is set in physical bitmap
//     if (GET_BIT(physical_bitmap, 0)) {
//         printf("Test 2: First bit is set in physical bitmap (Success)\n");
//     } else {
//         printf("Test 2: First bit is not set (Failed)\n");
//     }

//     // Test 3: Try setting and clearing bits
//     printf("\nTest 3: Testing bitmap operations...\n");
//     SET_BIT(physical_bitmap, 1);
//     SET_BIT(physical_bitmap, 2);
//     CLEAR_BIT(physical_bitmap, 1);

//     printf("Bit 0: %d (should be 1)\n", GET_BIT(physical_bitmap, 0));
//     printf("Bit 1: %d (should be 0)\n", GET_BIT(physical_bitmap, 1));
//     printf("Bit 2: %d (should be 1)\n", GET_BIT(physical_bitmap, 2));

//     // Cleanup
//     printf("\nCleaning up...\n");
//     cleanup_physical_mem();
//     printf("Cleanup completed!\n");

//     return 0;
// }


// // test.c Before defining translate function
// #include "my_vm.h"
// #include <stdio.h>
// #include <assert.h>

// // Function to test bitmap operations
// void test_bitmap_operations() {
//     printf("\nTesting bitmap operations...\n");
    
//     // Test setting bits
//     SET_BIT(physical_bitmap, 5);
//     SET_BIT(physical_bitmap, 7);
//     SET_BIT(physical_bitmap, 31);
    
//     // Verify bits are set
//     assert(GET_BIT(physical_bitmap, 0) == 1);  // This was set in set_physical_mem
//     assert(GET_BIT(physical_bitmap, 5) == 1);
//     assert(GET_BIT(physical_bitmap, 7) == 1);
//     assert(GET_BIT(physical_bitmap, 31) == 1);
//     assert(GET_BIT(physical_bitmap, 6) == 0);  // Should not be set
//     printf("Setting bits successful\n");

//     // Test clearing bits
//     CLEAR_BIT(physical_bitmap, 5);
//     assert(GET_BIT(physical_bitmap, 5) == 0);
//     assert(GET_BIT(physical_bitmap, 7) == 1);  // Should still be set
//     printf("Clearing bits successful\n");
// }

// // Function to test memory allocation
// void test_memory_allocation() {
//     printf("\nTesting memory allocation...\n");
    
//     // Check if physical memory was allocated
//     assert(physical_memory != NULL);
//     printf("Physical memory allocated successfully\n");

//     // Check if bitmaps were allocated
//     assert(physical_bitmap != NULL);
//     assert(virtual_bitmap != NULL);
//     printf("Bitmaps allocated successfully\n");

//     // Verify initial state of first page (should be marked as used for page directory)
//     assert(GET_BIT(physical_bitmap, 0) == 1);
//     printf("First page correctly marked as used\n");
// }

// // Function to print bitmap state (helper function)
// void print_bitmap_state(unsigned char *bitmap, int num_bits) {
//     printf("\nBitmap state (first %d bits):\n", num_bits);
//     for (int i = 0; i < num_bits; i++) {
//         printf("%d", GET_BIT(bitmap, i));
//         if ((i + 1) % 8 == 0) printf(" ");
//     }
//     printf("\n");
// }

// int main() {
//     printf("Starting Memory Management System Tests\n");
//     printf("======================================\n");

//     // Test 1: Initialize physical memory
//     printf("\nTest 1: Initializing physical memory...\n");
//     set_physical_mem();
//     printf("Memory initialization completed\n");

//     // Test 2: Test memory allocation
//     printf("\nTest 2: Memory Allocation Test\n");
//     test_memory_allocation();

//     // Test 3: Test bitmap operations
//     printf("\nTest 3: Bitmap Operations Test\n");
//     test_bitmap_operations();

//     // Print current state of physical bitmap
//     print_bitmap_state(physical_bitmap, 32);

//     // Test 4: Test cleanup
//     printf("\nTest 4: Testing cleanup...\n");
//     cleanup_physical_mem();
//     printf("Cleanup completed\n");

//     printf("\nAll tests completed successfully!\n");
//     return 0;
// }
// // #include "my_vm.h"
// // #include <stdio.h>

// // int main(){
// //     printf("Initializing physical memory...");
// //     set_physical_mem();
// //     printf("Physical memory initialized successfully");


// //     printf("Cleaning up physical memory...\n");
// //     cleanup_physical_mem();
// //     printf("Cleanup completed!\n");
// //     return 0;
// // }