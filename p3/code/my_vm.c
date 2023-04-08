#include "my_vm.h"

//////////////////////////////////////////////////////////////////////////

paging_scheme_t *paging_scheme;

void *vm_start;
pde_t *ptbr; /* Page table base register - root page dir address. */

// Orientation: Left to Right, /* Set third bit = 0010 */
char *virtual_bitmap;
char *frame_bitmap; 

List *tlb_cache;

//////////////////////////////////////////////////////////////////////////

void* pointer_to_frame_at_position(position f) {
    return (void *) (f * PGSIZE + vm_start);
}

void init_page_tables() {    
    assert(!ptbr);
    // allocate page dir
    position pgdir_frame = first_available_position(frame_bitmap);
    set_bit_at(frame_bitmap, pgdir_frame);
    ptbr = (pde_t *) pointer_to_frame_at_position(pgdir_frame);

    // allocate page tables 
    for(unsigned long pt = 0; pt < (1 << paging_scheme->page_dir); ++pt) {
        position ptable_frame = first_available_position(frame_bitmap);
        set_bit_at(frame_bitmap, ptable_frame);
        ptbr[pt] = (pde_t) pointer_to_frame_at_position(pgdir_frame);
    }

    // Note that now we have 1 + 1 << pgdir_bits # of set bits in the frame bitmap. Check invariants:
    assert(num_bits_set(frame_bitmap) == (unsigned long) (1 + (1 << paging_scheme->page_dir)));
    assert(n_bits_available(frame_bitmap, 
        (1 << paging_scheme->chars_for_bitmap) - 1 - (1 << paging_scheme->page_dir)));
}

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

    assert(!frame_bitmap);
    assert(!virtual_bitmap);
    size_t bitmap_size = (1 << (short) paging_scheme->chars_for_bitmap) * sizeof(char);
    frame_bitmap = (char *) malloc(bitmap_size);
    memset(frame_bitmap, 0, bitmap_size);
    virtual_bitmap = (char *) malloc(bitmap_size);
    memset(virtual_bitmap, 0, bitmap_size);

    assert(!tlb_cache);
    tlb_cache = (List *) malloc(sizeof(List));
    tlb_cache->size = 0;
    tlb_cache->front = NULL;

    // Allocate pgdir and remaining page tables:
    vm_start = malloc(MEMSIZE);
    memset(vm_start, 0, MEMSIZE);

    init_page_tables();

    // Register clean up function. 
    atexit(clean_my_vm);
}

///////////////////////////////////////////////////////////////////////////////////

/* Returns 1 if va1 and va2 point to the same frame, 0 otherwise.*/
int equivalent_virtual_address(void *va1, void *va2) {
    short rs = (short) paging_scheme->offset;
    return ((unsigned long) va1 >> rs) == ((unsigned long) va2 >> rs);
}

/* Searches for tlb entry with key: (virtual address) va. 
If found, removes from list and returns associated value (physical address).*/
tlb_store* search_and_remove(void *target_va) {
    if (tlb_cache->size >= 1) {
        // Matches first item?
        tlb_store *front = tlb_cache->front;
        if (equivalent_virtual_address(front->virtual_address, target_va)) {
            tlb_cache->front = front->next;
            tlb_cache->size -= 1;

            front->next = NULL;
            return front;
        }

        // Check remainder of list:
        tlb_store *prev = front;
        tlb_store *ptr = front->next;

        while (ptr && !equivalent_virtual_address(ptr->virtual_address, target_va)) {
            prev = prev->next;
            ptr = ptr->next;
        }

        if (ptr) {
            prev->next = ptr->next;
            tlb_cache->size -= 1;
            
            ptr->next = NULL;
            return ptr;
        }
    }

    return NULL;
}

void evict_lru_cached_entry() {
    if (tlb_cache->size == 0) {
        return;
    } else if (tlb_cache->size == 1) {
        tlb_cache->size -= 1;
        free(tlb_cache->front);
        tlb_cache->front = NULL;
        return;
    }
    
    // TLB cache holds more than one entry:
    tlb_store *prev = tlb_cache->front;
    tlb_store *ptr = prev->next;

    while (ptr->next) {
        prev = prev->next;
        ptr = ptr->next;
    }

    // ptr points to last node (remove this node):
    prev->next = NULL;
    tlb_cache->size -= 1;
    free(ptr);
}

/* Part 2: Add a virtual to physical page translation to the TLB.*/
void add_to_TLB(void *va, pte_t *frame) {
    if (tlb_cache->size == TLB_ENTRIES) {
        evict_lru_cached_entry();
    }

    tlb_store *item = (tlb_store *) malloc(sizeof(tlb_store));
    item->virtual_address = va;
    item->frame = frame;
    item->next = tlb_cache->front;

    tlb_cache->front = item;
    tlb_cache->size += 1;
}

/* Part 2: Check TLB for a valid translation. Returns the physical page address. */
pte_t* check_TLB(void *va) {
    if (!is_valid_va(va)) {
        return NULL;
    }

    tlb_store *target_tlb = search_and_remove(va);
    if (target_tlb) {
        // Add the frame to the front since it has become the most recently used.
        target_tlb->next = tlb_cache->front;
        tlb_cache->front = target_tlb;
        tlb_cache->size += 1;
        return target_tlb->frame;
    } else {
        // No such translation in Cache. Add to valid but uncached va.
        pte_t *target_frame = fetch_pte_from(va);
        if (target_frame) {
            add_to_TLB(va, target_frame);
            return target_frame;
        } else {
            return NULL;
        }
    }
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


///////////////////////////////////////////////////////////////////////////////////


/* Extract offsets from va. This function assumes:
 * - 32 bit virtual address space;
 * - the virtual address is valid.
 */
void extract_from(unsigned long va, virtual_addr_t *vaddy) {
    // Get offset bits.
    vaddy->byte_offset = va & ((1 << paging_scheme->offset) - 1);

    // Get middle page_table number of bits.
    va >>= paging_scheme->offset;
    vaddy->pte_offset = va & ((1 << paging_scheme->page_table) - 1);

    // Get top page_dir number of bits.
    va >>= paging_scheme->page_table;
    vaddy->pde_offset = va & ((1 << paging_scheme->page_dir) - 1);
}

void* reconvert(virtual_addr_t *vaddy) {
    return (void *) ((((vaddy->pde_offset << paging_scheme->page_table) 
    | vaddy->pte_offset) << paging_scheme->offset) | vaddy->byte_offset);
}

bool is_valid_va(void *va) {
    return (((unsigned long) va) >> paging_scheme->max_bits) == 0;
}

// To-DO ---> Test THIS (maybe after doing malloc/free).
/* If va is invalid, returns NULL. */
pte_t* fetch_pte_from(void *va) {
    if (!is_valid_va(va)) {
        return NULL;
    }

    virtual_addr_t vaddy;
    extract_from((unsigned long) va, &vaddy);
    pde_t *intermediate_dir = (pde_t *) *(ptbr + vaddy.pde_offset);
    return (intermediate_dir + vaddy.pte_offset);
}

void* fetch_pa_from(void *va) {
    pte_t *frame = fetch_pte_from(va);
    if(frame == NULL) {
        return NULL;
    }

    virtual_addr_t vaddy;
    extract_from((unsigned long) va, &vaddy);
    return (void *) (frame + vaddy.byte_offset);
}


///////////////////////////////////////////////////////////////////////////////////


/*
The function takes a virtual address and page directories starting address and
performs translation to return the physical address. Returns NULL if va is an invalid virtual address.
*/
pte_t *translate(pde_t *pgdir, void *va) {
    return check_TLB(va);
}


/*
The function takes a page directory address, virtual address, physical address
as an argument, and sets a page table entry. This function will walk the page
directory to see if there is an existing mapping for a virtual address. If the
virtual address is not present, then a new entry will be added.

0 if failed, 1 if success.
*/
int page_map(pde_t *pgdir, void *va, void *pa) {
    /*HINT: Similar to translate(), find the page directory (1st level)
    and page table (2nd-level) indices. If no mapping exists, set the
    virtual to physical mapping */
    position va_pos = ((unsigned long) va) >> paging_scheme->offset;
    if (!is_valid_va(va) || bit_set_at(virtual_bitmap, va_pos)) {
        return 0; // Fail
    }

    pte_t *addr_of_cell_containing_frame = fetch_pte_from(va);
    *addr_of_cell_containing_frame = (pte_t) pa;
    return 1; // Success
}


/* Function that gets the next available page within which you can store a frame pointer. */
void *get_next_avail(int num_pages) {
    //Use virtual address bitmap to find the next free page
    position vp_position = first_available_position(virtual_bitmap);
    set_bit_at(virtual_bitmap, vp_position);

    // Return a void * to the cell in our page table within which we 
    // may store a reference to a frame. Shift left by offset for proper va parsing.
    return (void *) ((unsigned long) vp_position << paging_scheme->offset);
}


/* Function responsible for allocating pages and used by the benchmark */
void *t_malloc(unsigned int num_bytes) {
    // Init supporting structs if called for the first time:
    if (!paging_scheme) {
        set_physical_mem();
    }

   /* 
    * HINT: Next, using get_next_avail(), check if there are free pages. If
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


///////////////////////////////////////////////////////////////////////////////////


num_bits_t num_bits_needed_to_encode(unsigned long val) {
    num_bits_t n;
    for (n = 0; val; val >>= 1, ++n);
    return n - 1;
}

void init_paging_scheme(paging_scheme_t *ps) {
    ps->va_space = 32;
    ps->pa_space = num_bits_needed_to_encode(MEMSIZE);
    ps->max_bits = ps->pa_space < ps->va_space ? ps->pa_space : ps->va_space;
    
    ps->offset = num_bits_needed_to_encode(PGSIZE);
    ps->max_pages = ps->max_bits - ps->offset;

    ps->page_table = num_bits_needed_to_encode(PGSIZE / sizeof(pte_t));
    ps->page_dir = ps->max_pages - ps->page_table;
    
    ps->chars_for_bitmap = ps->max_pages - 3;

    assert(ps->max_pages == (ps->page_dir + ps->page_table));
    assert(ps->max_bits == (ps->max_pages + ps->offset));
}

void print_paging_scheme(paging_scheme_t *ps) {
    printf("Please verify that the following is correct:\n");
    printf("============================================\n");
    printf("(*) Page Size: %d\n\n", PGSIZE);

    printf("(*) Bits for Virtual Address Space: %hd\n", ps->va_space);
    printf("(*) Bits for Physical Address Space: %hd\n\n", ps->pa_space);
    printf("(*) Max bits for addressing: %hd\n\n", ps->max_bits);

    printf("(*) Offset bits into Page: %hd\n", ps->offset);
    printf("(*) Max # of pages: %hd\n\n", ps->max_pages);

    printf("(*) Page Table # of bits: %hd\n", ps->page_table);
    printf("(*) Page Directory # of bits: %hd\n\n", ps->page_dir);
    
    printf("(*) Frame bitmap # of bits: %hd\n", ps->chars_for_bitmap);
    printf("============================================\n");
}


///////////////////////////////////////////////////////////////////////////////////

bool bit_set_at(char *bitmap, position p) {
    unsigned long dividend = p / 8;
    unsigned long remainder = p % 8;
    return bitmap[dividend] & (1 << (7 - remainder));
}

void set_bit_at(char *bitmap, position p) { 
    unsigned long dividend = p / 8;
    unsigned long remainder = p % 8;
    bitmap[dividend] |= (1 << (7 - remainder));
}

void unset_bit_at(char *bitmap, position p) {
    unsigned long dividend = p / 8;
    unsigned long remainder = p % 8;
    bitmap[dividend] &= ~(1 << (7 - remainder));
}

/* Returns position of first unset bit in a character OR -1 if all bits set.*/
position lowest_unset_bit(char c) {
    position pos = 0;
    for(pos = 0; pos <= 7; ++pos) {
        if (!(c & (1 << pos))) {
            return 7 - pos;
        }
    }

    return -1;
}

/* Returns the position of the first available bit else returns -1 if no available bits.*/
position first_available_position(char *bitmap) {
    for (unsigned long i = 0; i < (1 << paging_scheme->chars_for_bitmap); ++i) {
        position p = lowest_unset_bit(bitmap[i]);
        if (p != -1) {
            return (i * 8 + p);
        }
    }
    
    return -1;
}

/* Returns true if at least n bits unset, false otherwise. */
bool n_bits_available(char *bitmap, unsigned long n) {
    assert(n > 0);

    for(position i = 0; i < (1 << (short) paging_scheme->chars_for_bitmap); ++i) {
        for(position pos = 0; pos <= 7; ++pos) {
            // If a bit is unset, then decrement n. 
            n = (bitmap[i] & (1 << pos)) ? n : n - 1;
            if (n == 0) {
                return true;
            }
        }
    }

    return false;    
}

unsigned long num_bits_set(char *bitmap) {
    unsigned int count = 0;
    for(position i = 0; i < (1 << (short) paging_scheme->chars_for_bitmap); ++i) {
        for(position pos = 0; pos <= 7; ++pos) {
            count = (bitmap[i] & (1 << pos)) ? count + 1 : count;
        }
    }

    return count;
}

///////////////////////////////////////////////////////////////////////////////////


/* Registered with atexit during vm setup. */
void clean_my_vm(void) {
    free(paging_scheme);
    free(frame_bitmap);

    while (tlb_cache->size) {
        tlb_store *front = tlb_cache->front;
        tlb_cache->front = front->next;
        tlb_cache->size -= 1;
        free(front);
    }

    free(tlb_cache);
    free(vm_start);
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
