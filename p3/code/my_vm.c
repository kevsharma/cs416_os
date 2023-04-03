#include "my_vm.h"

paging_scheme_t *paging_scheme;
pde_t *ptbr; /* Page table base register - root page dir address. */
char *frame_bitmap;


/*
Function responsible for allocating and setting your physical memory 
*/
void set_physical_mem() {

    //Allocate physical memory using mmap or malloc; this is the total size of
    //your memory you are simulating

    //HINT: Also calculate the number of physical and virtual pages and allocate
    //virtual and physical bitmaps and initialize them

    assert(!paging_scheme);
    paging_scheme = (paging_scheme_t *) malloc(sizeof(paging_scheme_t));
    init_paging_scheme(paging_scheme);

    assert(!ptbr);
    size_t pgdir_size = (1 << (short) paging_scheme->page_dir) * sizeof(pde_t *);
    ptbr = (pde_t *) malloc(pgdir_size);
    memset(ptbr, 0, pgdir_size);
    
    assert(!frame_bitmap);
    size_t fb_size = (1 << (short) paging_scheme->chars_for_frame_bitmap) * sizeof(char);
    frame_bitmap = (char *) malloc(fb_size);
    memset(frame_bitmap, 0, fb_size);

    // Register clean up function. 
    atexit(clean_my_vm);
}


/*
 * Part 2: Add a virtual to physical page translation to the TLB.
 * Feel free to extend the function arguments or return type.
 */
int add_TLB(void *va, void *pa) {

    /*Part 2 HINT: Add a virtual to physical page translation to the TLB */

    return -1;
}


/*
 * Part 2: Check TLB for a valid translation.
 * Returns the physical page address.
 * Feel free to extend this function and change the return type.
 */
pte_t * check_TLB(void *va) {

    /* Part 2: TLB lookup code here */



   /*This function should return a pte_t pointer*/
}


/*
 * Part 2: Print TLB miss rate.
 * Feel free to extend the function arguments or return type.
 */
void print_TLB_missrate() {
    double miss_rate = 0;	

    /*Part 2 Code here to calculate and print the TLB miss rate*/




    fprintf(stderr, "TLB miss rate %lf \n", miss_rate);
}

virtual_addr_t extract_from(void *va) {
    // Assuming 32 bits.
    unsigned long val = (unsigned long) va;
    virtual_addr_t vaddy;
    
    // Get offset bits.
    vaddy.byte_offset = val & ((1 << paging_scheme->offset) - 1);
    
    // Get top page_dir number of bits.
    int k = 32 - paging_scheme->page_dir;
    vaddy.pde_offset = (((~0 << k) & val) >> k);

    // Get middle page_table number of bits.
    val <<= paging_scheme->page_dir;
    k = 32 - paging_scheme->page_table;
    vaddy.pte_offset = (((~0 << k) & val) >> k);

    return vaddy;
}

bool is_valid_virtual_address(virtual_addr_t vaddy) {
    // TO-DO;
    return true;
}

pte_t* fetch_frame_from(virtual_addr_t vaddy) {
    assert(is_valid_virtual_address(vaddy));
    pde_t *intermediate_dir = (pde_t *) *(ptbr + vaddy.pde_offset);
    pte_t *frame = (pte_t *) *(intermediate_dir + vaddy.pte_offset);
    return frame;
}

void* fetch_pa_from(virtual_addr_t vaddy) {
    return (void *) (fetch_frame_from(vaddy) + vaddy.byte_offset);
}


/*
The function takes a virtual address and page directories starting address and
performs translation to return the physical address
*/
pte_t *translate(pde_t *pgdir, void *va) {
    virtual_addr_t vaddy = extract_from(va);

    /* Part 1 HINT: Get the Page directory index (1st level) Then get the
    * 2nd-level-page table index using the virtual address.  Using the page
    * directory index and page table index get the physical address.
    *
    * Part 2 HINT: Check the TLB before performing the translation. If
    * translation exists, then you can return physical address from the TLB.
    */

    //If translation not successful, then return NULL
    return NULL; 
}


/*
The function takes a page directory address, virtual address, physical address
as an argument, and sets a page table entry. This function will walk the page
directory to see if there is an existing mapping for a virtual address. If the
virtual address is not present, then a new entry will be added
*/
int page_map(pde_t *pgdir, void *va, void *pa) {

    /*HINT: Similar to translate(), find the page directory (1st level)
    and page table (2nd-level) indices. If no mapping exists, set the
    virtual to physical mapping */

    return -1;
}


/* Function that gets the next available page */
void *get_next_avail(int num_pages) {
 
    //Use virtual address bitmap to find the next free page
}


/* Function responsible for allocating pages and used by the benchmark */
void *t_malloc(unsigned int num_bytes) {

    /* 
     * HINT: If the physical memory is not yet initialized, then allocate and initialize.
     */

   /* 
    * HINT: If the page directory is not initialized, then initialize the
    * page directory. Next, using get_next_avail(), check if there are free pages. If
    * free pages are available, set the bitmaps and map a new page. Note, you will 
    * have to mark which physical pages are used. 
    */

    return NULL;
}

/* Responsible for releasing one or more memory pages using virtual address (va) */
void t_free(void *va, int size) {

    /* Part 1: Free the page table entries starting from this virtual address
     * (va). Also mark the pages free in the bitmap. Perform free only if the 
     * memory from "va" to va+size is valid.
     *
     * Part 2: Also, remove the translation from the TLB
     */
    
}


/* The function copies data pointed by "val" to physical
 * memory pages using virtual address (va)
 * The function returns 0 if the put is successfull and -1 otherwise.
*/
int put_value(void *va, void *val, int size) {

    /* HINT: Using the virtual address and translate(), find the physical page. Copy
     * the contents of "val" to a physical page. NOTE: The "size" value can be larger 
     * than one page. Therefore, you may have to find multiple pages using translate()
     * function.
     */


    /*return -1 if put_value failed and 0 if put is successfull*/

}


/*Given a virtual address, this function copies the contents of the page to val*/
void get_value(void *va, void *val, int size) {

    /* HINT: put the values pointed to by "va" inside the physical memory at given
    * "val" address. Assume you can access "val" directly by derefencing them.
    */


}



/*
This function receives two matrices mat1 and mat2 as an argument with size
argument representing the number of rows and columns. After performing matrix
multiplication, copy the result to answer.
*/
void mat_mult(void *mat1, void *mat2, int size, void *answer) {

    /* Hint: You will index as [i * size + j] where  "i, j" are the indices of the
     * matrix accessed. Similar to the code in test.c, you will use get_value() to
     * load each element and perform multiplication. Take a look at test.c! In addition to 
     * getting the values from two matrices, you will perform multiplication and 
     * store the result to the "answer array"
     */
    int x, y, val_size = sizeof(int);
    int i, j, k;
    for (i = 0; i < size; i++) {
        for(j = 0; j < size; j++) {
            unsigned int a, b, c = 0;
            for (k = 0; k < size; k++) {
                int address_a = (unsigned int)mat1 + ((i * size * sizeof(int))) + (k * sizeof(int));
                int address_b = (unsigned int)mat2 + ((k * size * sizeof(int))) + (j * sizeof(int));
                get_value( (void *)address_a, &a, sizeof(int));
                get_value( (void *)address_b, &b, sizeof(int));
                // printf("Values at the index: %d, %d, %d, %d, %d\n", 
                //     a, b, size, (i * size + k), (k * size + j));
                c += (a * b);
            }
            int address_c = (unsigned int)answer + ((i * size * sizeof(int))) + (j * sizeof(int));
            // printf("This is the c: %d, address: %x!\n", c, address_c);
            put_value((void *)address_c, (void *)&c, sizeof(int));
        }
    }
}


num_bits_t num_bits_needed_to_encode(unsigned long val) {
    num_bits_t n;
    for (n = 0; val; val >>= 1, ++n);
    return n - 1;
}

void init_paging_scheme(paging_scheme_t *ps) {
    ps->va_space = 32;
    ps->pa_space = num_bits_needed_to_encode(MEMSIZE);

    ps->offset = num_bits_needed_to_encode(PGSIZE);
    ps->max_physical_pages = ps->pa_space - ps->offset;

    ps->page_table = num_bits_needed_to_encode(PGSIZE / sizeof(pte_t));
    ps->page_dir = ps->max_physical_pages - ps->page_table;
    
    ps->chars_for_frame_bitmap = ps->max_physical_pages - 3;
}

void print_paging_scheme(paging_scheme_t *ps) {
    printf("Please verify that the following is correct:\n");
    printf("============================================\n");
    printf("(*) Page Size: %d\n\n", PGSIZE);

    printf("(*) Bits for Virtual Address Space: %hd\n", ps->va_space);
    printf("(*) Bits for Physical Address Space: %hd\n\n", ps->pa_space);

    printf("(*) Offset bits into Page: %hd\n", ps->offset);
    printf("(*) Max # of physical pages: %hd\n\n", ps->max_physical_pages);

    printf("(*) Page Table # of bits: %hd\n", ps->page_table);
    printf("(*) Page Directory # of bits: %hd\n\n", ps->page_dir);
    
    printf("(*) Frame bitmap # of bits: %hd\n", ps->chars_for_frame_bitmap);
    printf("============================================\n");
}

/* Registered with atexit during vm setup. */
void clean_my_vm(void) {
    free(paging_scheme);
    free(ptbr);
    free(frame_bitmap);
}