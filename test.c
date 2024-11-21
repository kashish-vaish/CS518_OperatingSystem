#include "my_vm.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

void test_memory_operations() {
    printf("\n=== Testing Memory Operations ===\n");
    
    // Allocate memory
    void *ptr = n_malloc(100);
    assert(ptr != NULL);
    printf("Allocated memory at %p\n", ptr);

    // Test put_data
    char test_data[] = "Hello, Virtual Memory!";
    int result = put_data(ptr, test_data, strlen(test_data) + 1);
    assert(result == 0);
    printf("Put data successful\n");

    // Test get_data
    char buffer[100];
    result = get_data(ptr, buffer, strlen(test_data) + 1);
    assert(result == 0);
    assert(strcmp(buffer, test_data) == 0);
    printf("Get data successful: %s\n", buffer);

    // Test n_free
    result = n_free(ptr, 100);
    assert(result == 0);
    printf("Free successful\n");
}

void test_matrix_multiplication() {
    printf("\n=== Testing Matrix Multiplication ===\n");
    
    int size = 2;
    int matrix_bytes = size * size * sizeof(int);

    // Allocate matrices
    void *mat1 = n_malloc(matrix_bytes);
    void *mat2 = n_malloc(matrix_bytes);
    void *answer = n_malloc(matrix_bytes);
    assert(mat1 != NULL && mat2 != NULL && answer != NULL);

    // Initialize test matrices
    int test_mat1[] = {1, 2, 3, 4};
    int test_mat2[] = {5, 6, 7, 8};
    
    // Write matrices to virtual memory
    put_data(mat1, test_mat1, matrix_bytes);
    put_data(mat2, test_mat2, matrix_bytes);

    // Perform multiplication
    int result = mat_mult(mat1, mat2, size, answer);
    assert(result == 0);

    // Read result
    int output[4];
    get_data(answer, output, matrix_bytes);

    // Verify result
    printf("Matrix multiplication result:\n");
    for (int i = 0; i < size; i++) {
        for (int j = 0; j < size; j++) {
            printf("%d ", output[i * size + j]);
        }
        printf("\n");
    }

    // Free matrices
    n_free(mat1, matrix_bytes);
    n_free(mat2, matrix_bytes);
    n_free(answer, matrix_bytes);
}

int main() {
    printf("Starting comprehensive memory management tests...\n");
    
    set_physical_mem();
    
    test_memory_operations();
    test_matrix_multiplication();
    
    cleanup_physical_mem();
    printf("\nAll tests completed successfully!\n");
    return 0;
}