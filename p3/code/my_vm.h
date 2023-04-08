#ifndef MY_VM_H_INCLUDED
#define MY_VM_H_INCLUDED
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include<string.h>

//Assume the address space is 32 bits, so the max memory size is 4GB
//Page size is 4KB

//Add any important includes here which you may need

#define PGSIZE 4096

// Maximum size of virtual memory
#define MAX_MEMSIZE 4ULL*1024*1024*1024

// Size of "physical memory"
#define MEMSIZE 1024*1024*1024

// Represents a page table entry
typedef unsigned long pte_t;

// Represents a page directory entry
typedef unsigned long pde_t;

#define TLB_ENTRIES 512

//Structure to represents TLB
typedef struct tlb_store {
    void *virtual_address;
    pte_t *frame;

    struct tlb_store *next;
} tlb_store;

typedef struct List {
    unsigned long size;
    struct tlb_store *front;
} List;

void print_TLB_missrate();
pte_t* check_TLB(void *va);
void add_to_TLB(void *va, pte_t *frame);

void set_physical_mem();
pte_t* translate(pde_t *pgdir, void *va);
int page_map(pde_t *pgdir, void *va, void* pa);

void *t_malloc(unsigned int num_bytes);
void t_free(void *va, int size);
int put_value(void *va, void *val, int size);
void get_value(void *va, void *val, int size);

void mat_mult(void *mat1, void *mat2, int size, void *answer);

void clean_my_vm(void);


/* The number of bits needed to encode some data. */
typedef short num_bits_t;

typedef struct {
    num_bits_t va_space;            // log_2 (MAX_MEMSIZE)
    num_bits_t pa_space;            // log_2 (MEMSIZE)
    num_bits_t max_bits;            // min(va_space, pa_space)

    num_bits_t offset;              // log_2 (PGSIZE)
    num_bits_t max_pages;           // max_bits - offset
    
    num_bits_t page_table;          // log_2 (PGSIZE / sizeof(pte_t))
    num_bits_t page_dir;            // max_pages - page_table
    
    num_bits_t chars_for_bitmap;    // max_pages - 3 {2^3 per char}
} paging_scheme_t;

num_bits_t num_bits_needed_to_encode(unsigned long val);
void init_paging_scheme(paging_scheme_t *ps);
void print_paging_scheme(paging_scheme_t *ps);


typedef struct {
    unsigned long byte_offset;
    unsigned long pte_offset;
    unsigned long pde_offset;    
} virtual_addr_t;

void extract_from(unsigned long va, virtual_addr_t *vaddy);
void* reconvert(virtual_addr_t *vaddy);
bool is_valid_va(void *va);
pte_t* fetch_frame_from(void *va);
void* fetch_pa_from(void *va);


typedef unsigned long position;

void set_bit_at(char *bitmap, position p);
void unset_bit_at(char *bitmap, position p);
position lowest_unset_bit(char c);
position first_available_position(char *bitmap);
bool n_bits_available(char *bitmap, unsigned long n);
unsigned long num_bits_set(char *bitmap);

#endif
