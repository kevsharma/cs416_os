#include <stdbool.h>
#include "../my_vm.h"

#define SIZE 75
#define ARRAY_SIZE 50000

int test_original() {
    printf("Allocating three arrays of %d bytes\n", ARRAY_SIZE);

    void *a = t_malloc(ARRAY_SIZE);
    int old_a = (int)a;
    void *b = t_malloc(ARRAY_SIZE);
    void *c = t_malloc(ARRAY_SIZE);
    int x = 1;
    int y, z;   
    int i =0, j=0;
    int address_a = 0, address_b = 0;
    int address_c = 0;

    printf("Addresses of the allocations: %x, %x, %x\n", (int)a, (int)b, (int)c);

    printf("Storing integers to generate a SIZExSIZE matrix\n");
    for (i = 0; i < SIZE; i++) {
        for (j = 0; j < SIZE; j++) {
            address_a = (unsigned int)a + ((i * SIZE * sizeof(int))) + (j * sizeof(int));
            address_b = (unsigned int)b + ((i * SIZE * sizeof(int))) + (j * sizeof(int));
            put_value((void *)address_a, &x, sizeof(int));
            put_value((void *)address_b, &x, sizeof(int));
        }
    } 

    printf("Fetching matrix elements stored in the arrays\n");

    for (i = 0; i < SIZE; i++) {
        for (j = 0; j < SIZE; j++) {
            address_a = (unsigned int)a + ((i * SIZE * sizeof(int))) + (j * sizeof(int));
            address_b = (unsigned int)b + ((i * SIZE * sizeof(int))) + (j * sizeof(int));
            get_value((void *)address_a, &y, sizeof(int));
            get_value( (void *)address_b, &z, sizeof(int));
            printf("%d ", y);
        }
        printf("\n");
    } 

    printf("Performing matrix multiplication with itself!\n");
    mat_mult(a, b, SIZE, c);


    for (i = 0; i < SIZE; i++) {
        for (j = 0; j < SIZE; j++) {
            address_c = (unsigned int)c + ((i * SIZE * sizeof(int))) + (j * sizeof(int));
            get_value((void *)address_c, &y, sizeof(int));
            printf("%d ", y);
        }
        printf("\n");
    }
    printf("Freeing the allocations!\n");
    t_free(a, ARRAY_SIZE);
    t_free(b, ARRAY_SIZE);
    t_free(c, ARRAY_SIZE);

    printf("Checking if allocations were freed!\n");
    a = t_malloc(ARRAY_SIZE);
    if ((int)a == old_a)
        printf("free function works\n");
    else
        printf("free function does not work\n");

    return 0;
}

void test_pagingScheme() {
    paging_scheme_t *paging_scheme = malloc(sizeof(paging_scheme_t));
    init_paging_scheme(paging_scheme);
    print_paging_scheme(paging_scheme);
}

void test_is_valid_va() {
    // Uses paging_scheme - ensure initialized first.
    // Assume 32 bits but max_bits = 30. Therefore, first 2 bits must not be set.
    assert(!is_valid_va((void *)1500212803));   // va = 01011001011010110110111001000011
    assert(is_valid_va((void *)963341891));     // va = 00111001011010110110111001000011
    assert(is_valid_va((void *)500));
    assert(!is_valid_va((void *)~0));

    printf("\nIs valid va works. \n");
}

void test_virtualaddr_extract() {
    // Uses paging_scheme - ensure initialized first.
    virtual_addr_t v;
    
    // Test 1:
    void *va = (void *) 963341891;
    // va = 00111001011010110110111001000011
    // pde_offset = 0101100101 = 357 | ignore first two bits = 101
    // pte_offset = 1010110110 = 694
    // offset = 111001000011
    extract_from((unsigned long) va, &v);
    assert(v.pde_offset == 229);
    assert(v.pte_offset == 694);
    assert(v.byte_offset == 3651);

    printf("\nVirtual address extract_from function works.\n");
}

void test_virtual_address_roundtrip() {
    virtual_addr_t v;
    
    // Test 1:
    void *va = (void *) 963341891;
    extract_from((unsigned long) va, &v);

    assert(va == reconvert(&v));
    printf("\nRoundtrip test works.\n");
}

void test_simple_roundtrip_single_page() {
    void *a = t_malloc(sizeof(int));

    int x = 4;
    int ret = put_value(a, &x, sizeof(int));
    assert(ret == 0); // success;

    int x_roundtrip;
    get_value(a, &x_roundtrip, sizeof(int));
    assert(x == x_roundtrip); // read your writes

    t_free(a, sizeof(int));
}

void test_original_multithreaded_aux() {
    char file_path[512];
    sprintf(file_path, "mt_test_file%ld", pthread_self());
    FILE *fp = fopen(file_path, "w");

    fprintf(fp, "Allocating three arrays of %d bytes\n", ARRAY_SIZE);

    void *a = t_malloc(ARRAY_SIZE);
    int old_a = (int)a;
    void *b = t_malloc(ARRAY_SIZE);
    void *c = t_malloc(ARRAY_SIZE);
    int x = 1;
    int y, z;   
    int i =0, j=0;
    int address_a = 0, address_b = 0;
    int address_c = 0;

    fprintf(fp, "Addresses of the allocations: %x, %x, %x\n", (int)a, (int)b, (int)c);

    fprintf(fp, "Storing integers to generate a SIZExSIZE matrix\n");
    for (i = 0; i < SIZE; i++) {
        for (j = 0; j < SIZE; j++) {
            address_a = (unsigned int)a + ((i * SIZE * sizeof(int))) + (j * sizeof(int));
            address_b = (unsigned int)b + ((i * SIZE * sizeof(int))) + (j * sizeof(int));
            put_value((void *)address_a, &x, sizeof(int));
            put_value((void *)address_b, &x, sizeof(int));
        }
    } 

    fprintf(fp, "Fetching matrix elements stored in the arrays\n");

    for (i = 0; i < SIZE; i++) {
        for (j = 0; j < SIZE; j++) {
            address_a = (unsigned int)a + ((i * SIZE * sizeof(int))) + (j * sizeof(int));
            address_b = (unsigned int)b + ((i * SIZE * sizeof(int))) + (j * sizeof(int));
            get_value((void *)address_a, &y, sizeof(int));
            get_value( (void *)address_b, &z, sizeof(int));
            fprintf(fp, "%d ", y);
        }
        fprintf(fp, "\n");
    } 

    fprintf(fp, "Performing matrix multiplication with itself!\n");
    mat_mult(a, b, SIZE, c);


    for (i = 0; i < SIZE; i++) {
        for (j = 0; j < SIZE; j++) {
            address_c = (unsigned int)c + ((i * SIZE * sizeof(int))) + (j * sizeof(int));
            get_value((void *)address_c, &y, sizeof(int));
            fprintf(fp, "%d ", y);
        }
        fprintf(fp, "\n");
    }

    fprintf(fp, "Freeing the allocations!\n");
    t_free(a, ARRAY_SIZE);
    t_free(b, ARRAY_SIZE);
    t_free(c, ARRAY_SIZE);

    fclose(fp);
}

void test_original_multithreaded() {
    pthread_t t1, t2, t3, t4, t5;
    
    pthread_create(&t1, NULL, (void *) &test_original_multithreaded_aux, NULL);
    pthread_create(&t2, NULL, (void *) &test_original_multithreaded_aux, NULL);
    pthread_create(&t3, NULL, (void *) &test_original_multithreaded_aux, NULL);
    pthread_create(&t4, NULL, (void *) &test_original_multithreaded_aux, NULL);
    pthread_create(&t5, NULL, (void *) &test_original_multithreaded_aux, NULL);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    pthread_join(t3, NULL);
    pthread_join(t4, NULL);
    pthread_join(t5, NULL);
}

int main() {
    set_physical_mem();
    printf("Running Tests: \n\n");

    /* Put tests here: */
    // test_pagingScheme();
    // test_is_valid_va();
    // test_virtualaddr_extract();
    // test_virtual_address_roundtrip();
    // test_simple_roundtrip_single_page();
    // test_original();
    // test_original_multithreaded();

    print_TLB_missrate();
}


