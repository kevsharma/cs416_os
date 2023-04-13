/**
 * Project 3: User Level Memory Management
 * ---------------------------------------
 *  The template for this project was 
 *  authored by Rutgers' CS416 Instructor(s).
 * 
 *  Completion of Template for purpose of assignment:
 * 	Author: Kev Sharma | kks107
 *  Author: Yash Patel | yp315
 * 
 *  Tested on [ls.cs.rutgers.edu] solely with 32 bits.
*/

#include "my_vm.h"

//////////////////////////////////////////////////////////////////////////

paging_scheme_t *paging_scheme;
void *vm_start;

pde_t *ptbr; /* Page table base register - root page dir address. */
size_t PAYLOAD_BYTES = PGSIZE - sizeof(void *);

// Orientation: Left to Right, /* Set third bit = 0010 */
char *virtual_bitmap;
char *frame_bitmap;

List *tlb_cache;
unsigned long tlb_misses = 0;
unsigned long tlb_lookups = 0;

volatile atomic_uint first_invocation = ATOMIC_VAR_INIT(0);
pthread_mutex_t library_lock;

//////////////////////////////////////////////////////////////////////////

void* pointer_to_frame_at_position(position f) {
    return (void *) (f * PGSIZE + vm_start);
}

position frame_position_from_pointer(void *pa) {
    return (position) (pa - vm_start) / PGSIZE;
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
    
    // Mark first virtual page as used because t_malloc method contract is incorrect.
    set_bit_at(virtual_bitmap, 0);
    assert(num_bits_set(virtual_bitmap) == 1);
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

    pthread_mutex_init(&library_lock, NULL);

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
    if (!is_valid_va(va) || !bit_set_at(virtual_bitmap, (unsigned long) va >> paging_scheme->offset)) {
        return NULL;
    }

    ++tlb_lookups;

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
            ++tlb_misses;
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
    /*Part 2 Code here to calculate and print the TLB miss rate*/
    double miss_rate = ((double) tlb_misses) / ((double) tlb_lookups) * 100;	
    // fprintf(stderr, "TLB misses: %lu \n", tlb_misses);
    // fprintf(stderr, "TLB lookups: %lu \n", tlb_lookups);
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
    if (!is_valid_va(va) || !bit_set_at(virtual_bitmap, (unsigned long) va >> paging_scheme->offset)) {
        return NULL;
    }

    virtual_addr_t vaddy;
    extract_from((unsigned long) va, &vaddy);
    pde_t *intermediate_dir = (pde_t *) *(ptbr + vaddy.pde_offset);
    return (intermediate_dir + vaddy.pte_offset);
    // They must dereference the pointer to get the address of the frame (pa). 
    // This returns the location of the cell pointed to by va.
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

    set_bit_at(virtual_bitmap, va_pos);
    *(translate(ptbr, va)) = (pte_t) pa; // Adds to TLB - Compulsory Miss
    return 1; // Success
}


/* Function that gets the next available page within which you can store a frame pointer. */
void *get_next_avail(int num_pages) {
    //Use virtual address bitmap to find the next free page
    position vp_position = first_available_position(virtual_bitmap);

    // Return a void * to the cell in our page table within which we 
    // may store a reference to a frame. Shift left by offset for proper va parsing.
    return (void *) ((unsigned long) vp_position << paging_scheme->offset);
}


/* Function responsible for allocating pages and used by the benchmark */
   /* 
    * HINT: Next, using get_next_avail(), check if there are free pages. If
    * free pages are available, set the bitmaps and map a new page. Note, you will 
    * have to mark which physical pages are used. 
    */
void *t_malloc(unsigned int num_bytes) {
    // Init supporting structs if called for the first time:
    while(__atomic_test_and_set(&first_invocation, __ATOMIC_SEQ_CST));

    if (!paging_scheme) {
        // Important: only one thread should ever execute set_physical_mem();
        set_physical_mem();
    }
    __atomic_store_n(&first_invocation, 0, __ATOMIC_SEQ_CST);

    /* Supporting structures have all been allocated successfully: */

    pthread_mutex_lock(&library_lock);

    if (num_bytes == 0) {
        pthread_mutex_unlock(&library_lock);
        return NULL;
    }

    unsigned long pages_requested = (num_bytes / PAYLOAD_BYTES) + ((num_bytes % PAYLOAD_BYTES) != 0);

    if (!n_bits_available(virtual_bitmap, pages_requested) || 
    !n_bits_available(frame_bitmap, pages_requested)) {
        pthread_mutex_unlock(&library_lock);
        return NULL;
    }

    /* The Virtual address of the first page (to be returned)*/
    void *first_page = NULL;
    unsigned long *requested_frames = (unsigned long *) malloc(pages_requested * sizeof(unsigned long));

    /* The links from one page to another. This is a Virtual address reference stored in the first 4 bytes
     * of a frame to document the on which virtual address the frame's payload continues.
     */
    unsigned long *links = (unsigned long *) malloc(pages_requested * sizeof(unsigned long));

    for (unsigned long i = 0; i < pages_requested; ++i) {
        // Get the virtual address to store one of the frames in.
        void *va = get_next_avail(0); // note that the offset bits are all unset.
        links[i] = (unsigned long) va;
        first_page = first_page == NULL ? va : first_page;

        // Get the physical address of the frame.
        position frame_obtained = first_available_position(frame_bitmap);
        set_bit_at(frame_bitmap, frame_obtained);
        void *pa = pointer_to_frame_at_position(frame_obtained);
        requested_frames[i] = (unsigned long) pa;
        memset(pa, 0, PGSIZE);

        int mapped_successfully = page_map(ptbr, va, pa);

        assert(mapped_successfully);
        assert(bit_set_at(virtual_bitmap, (unsigned long) va >> paging_scheme->offset));
        assert(bit_set_at(frame_bitmap, frame_obtained));
    }

    // Say we allocated frames A, B, C for a request of 10,000 bytes with a 4096 page size.
    // Copy &B into first four bytes of A; 
    // Copy &C into first four bytes of B;
    // Copy NULL into first four bytes of C (done previously using memset(pa, 0, PGSIZE))
    // Algorithm for such linking using data we kept track of previously: 
    for (unsigned long j = 0; j < (pages_requested - 1); ++j) {
        memcpy((void *) requested_frames[j], (links + j + 1), sizeof(unsigned long));
    }

    free(requested_frames);
    free(links);

    pthread_mutex_unlock(&library_lock);
    return first_page;
}

/* Valid free only if the memory from "va" to va+size is valid. */
bool valid_pages_linked_together_from(void *start, unsigned long num_links) {
    // Base step
    if (num_links == 0) {
        // start linked gracefully onwards to final page.
        return true;
    }

    if (start == NULL && num_links > 0) {
        // No page pointed to by start and yet we still have to free a remaining page.
        return false; // invalid size pointed to t_free.
    }

    // Inductive Step
    position va_pos = ((unsigned long) start) >> paging_scheme->offset;
    if (!is_valid_va(start) || !bit_set_at(virtual_bitmap, va_pos)) {
        // This virtual page is not in use. Reject!
        return false;
    }

    // Start isn't null and num_links is > 0.
    pte_t *pte_holding_frame = translate(ptbr, start);
    if (pte_holding_frame == NULL) {
        // The cell must be valid.
        return false;
    }

    void *pa = (void *) *pte_holding_frame;
    if (pa == NULL) {
        // The cell must hold a reference to a physical address.
        return false;
    }

    // Find the link.
    unsigned long new_start;
    memcpy(&new_start, pa, sizeof(void *));

    return valid_pages_linked_together_from((void *) new_start, num_links - 1);
}

void t_free_aux(void *start, unsigned long num_links) {
    // Part 1: Base case
    if (start == NULL || num_links == 0) {
        return;
    }

    // Part 1: Inductive Step
    search_and_remove(start); // Part 2: Also, remove the translation from the TLB

    pte_t *pte_holding_frame = translate(ptbr, start);
    void *pa = (void *) *(pte_holding_frame);

    // Mark the mapping from start virtual address to NULL, since we are freeing the frame.
    memset(pte_holding_frame, 0, sizeof(pte_t));

    // Find the link.
    unsigned long new_start;
    memcpy(&new_start, pa, sizeof(void *));
    
    unset_bit_at(virtual_bitmap, (position)(((unsigned long) start) >> paging_scheme->offset));
    unset_bit_at(frame_bitmap, frame_position_from_pointer(pa));

    // Overwrite the page with empty bits so it can be reused again.
    memset(pa, 0, PGSIZE);

    t_free_aux((void *) new_start, num_links - 1);
}

/* Responsible for releasing one or more memory pages using virtual address (va) */
void t_free(void *va, int size) {  
    if (size == 0 || !paging_scheme) { 
        return;
    }
    
    pthread_mutex_lock(&library_lock);

    unsigned long pages_requested = (size / PAYLOAD_BYTES) + ((size % PAYLOAD_BYTES) != 0);
    if (valid_pages_linked_together_from(va, pages_requested)) {
        /* Part 1: Free the page table entries starting from this virtual address
         * (va). Also mark the pages free in the bitmap. Perform free only if the 
         * memory from "va" to va+size is valid.
         *
         * Part 2: Also, remove the translation from the TLB
         */
        t_free_aux(va, pages_requested);
    } 

    pthread_mutex_unlock(&library_lock);  
}

void put_value_aux(void *start, void *val, int bytes_remaining) {
    /* HINT: Using the virtual address and translate(), find the physical page. Copy
     * the contents of "val" to a physical page. NOTE: The "size" value can be larger 
     * than one page. Therefore, you may have to find multiple pages using translate()
     * function.
     */

    // Base Step: 
    if (!bytes_remaining) {
        return;
    }

    // Inductive Step:
    pte_t *pte_holding_frame = translate(ptbr, start);
    void *pa = (void *) *pte_holding_frame;
    
    // Find the link.
    unsigned long new_start;
    memcpy(&new_start, pa, sizeof(void *));

    virtual_addr_t vaddy;
    extract_from((unsigned long) start, &vaddy);

    // byte_offset can go up to PGSIZE - 1 (which can be greater than PAYLOAD_BYTES)
    if (vaddy.byte_offset > PAYLOAD_BYTES) {
        // We will not write any data to this page.
        new_start |= (vaddy.byte_offset - PAYLOAD_BYTES);
        put_value_aux((void *) new_start, val, bytes_remaining);
    } else {
        pa += sizeof(void *); // Advance pa by the first bytes reserved for link pointer.
        pa += vaddy.byte_offset;
        size_t writeable_bytes = PAYLOAD_BYTES - vaddy.byte_offset;

        if (bytes_remaining <= writeable_bytes) {
            memcpy(pa, val, bytes_remaining);
        } else {
            memcpy(pa, val, writeable_bytes);
            put_value_aux((void *) new_start, val + writeable_bytes, bytes_remaining - writeable_bytes);
        }
    }
}

/* The function copies data pointed by "val" to physical
 * memory pages using virtual address (va)
 * The function returns 0 if the put is successfull and -1 otherwise.
*/
int put_value(void *va, void *val, int size) {
    pthread_mutex_lock(&library_lock);
    
    virtual_addr_t vaddy;
    extract_from((unsigned long) va, &vaddy);
    
    size += vaddy.byte_offset; // write starting at offset 100. Then if size is 4092, we need to enough pages for 4192 bytes.
    unsigned long num_pages_to_write_to = (size / PAYLOAD_BYTES) + ((size % PAYLOAD_BYTES) != 0);
    if (!valid_pages_linked_together_from(va, num_pages_to_write_to)) {
        pthread_mutex_unlock(&library_lock);
        printf("DEBUG: Not enough pages to accomodate put_value request.\n");
        return -1;
    }

    size -= vaddy.byte_offset; // put_value_aux writes bytes starting from the offset it found and consumes size properly.
    put_value_aux(va, val, size);

    pthread_mutex_unlock(&library_lock);
    return 0;
}

void get_value_aux(void *start, void *val, int bytes_remaining) {
    /* HINT: put the values pointed to by "va" inside the physical memory at given
    * "val" address. Assume you can access "val" directly by derefencing them.
    */
    
    // Base Step: 
    if (!bytes_remaining) {
        return;
    }

    // Inductive Step:
    pte_t *pte_holding_frame = translate(ptbr, start);
    void *pa = (void *) *pte_holding_frame;
    
    // Find the link.
    unsigned long new_start;
    memcpy(&new_start, pa, sizeof(void *));

    /**
     * Caution: fragile writing scheme.
     * Assume our PGSIZE is 4096 bytes.
     * The first 4 bytes are reserved to reference the continuing non-contiguous page.
     * So our PAYLOAD_BYTES are 4096 - 4 = 4092. For this example, lets suppose the 
     * request wishes to read the entire payload amount of 4092. That is, size = 4092.
     * 
     * However, what if the virtual address pointed to by start contains a non-zero offset?
     * We reserve log2(4096) = 12 bits for the offset. So it is entirely possible that 
     * the client has requested to read from an offset off of the payload start at byte 4.
     * 
     * let us say that the offset is 100 (in decimal-notation). Then we must
     * read starting from 4 + 100 = byte 104. 
     * 
     * Accordingly, we would end up only reading 4096 - 104 = 4092 - 100 = 3992 bytes
     * from this page and have to read the remaining 100 bytes from the successor 
     * non-contiguous page pointed to by new_start.
     * 
     * Note further, that if PAYLOAD BYTES is 4092, then what if the virtual address (start)
     * passed in has an offset value > 4092 but less than 4096? In this case, we must 
     * descend to the next link and begin reading from there. Suppose offset was 4095. Then
     * we begin reading from the 4095-4092 byte = 3rd byte. That is, we read from pa + 4 + 3
     * of the next linked page.
    */

    virtual_addr_t vaddy;
    extract_from((unsigned long) start, &vaddy);

    if (vaddy.byte_offset > PAYLOAD_BYTES) {
        // We will not read any data from this page.
        new_start |= (vaddy.byte_offset - PAYLOAD_BYTES);
        get_value_aux((void *) new_start, val, bytes_remaining);
    } else {
        pa += sizeof(void *); // Advance pa by the first bytes reserved for link pointer.
        pa += vaddy.byte_offset;
        size_t readable_bytes = PAYLOAD_BYTES - vaddy.byte_offset;

        if (bytes_remaining <= readable_bytes) {
            memcpy(val, pa, bytes_remaining);
        } else {
            // We will write PAYLOAD_Bytes to this val and find remaining data from next link.c
            memcpy(val, pa, readable_bytes);
            get_value_aux((void *) new_start, val + readable_bytes, bytes_remaining - readable_bytes);
        }
    }
}


/*Given a virtual address, this function copies the contents of the page to val*/
void get_value(void *va, void *val, int size) {
    pthread_mutex_lock(&library_lock);
    
    virtual_addr_t vaddy;
    extract_from((unsigned long) va, &vaddy);
    
    size += vaddy.byte_offset; // read from offset 100. Then if size is 4092, we need to enough pages for 4192 bytes.
    unsigned long num_pages_to_write_to = (size / PAYLOAD_BYTES) + ((size % PAYLOAD_BYTES) != 0);

    if (!valid_pages_linked_together_from(va, num_pages_to_write_to)) {
        pthread_mutex_unlock(&library_lock);
        printf("DEBUG: Failed get_value verification check.\n");
    }

    // Next get_value_aux will read bytes starting from the offset it found, so we pass in 4092 instead of 4192.
    size -= vaddy.byte_offset;
    get_value_aux(va, val, size);
    
    pthread_mutex_unlock(&library_lock);
}


///////////////////////////////////////////////////////////////////////////////////


num_bits_t num_bits_needed_to_encode(unsigned long long val) {
    num_bits_t n;
    for (n = 0; val; val >>= 1, ++n);
    return n - 1;
}

void init_paging_scheme(paging_scheme_t *ps) {
    ps->va_space = num_bits_needed_to_encode((unsigned long long) MAX_MEMSIZE);
    ps->pa_space = num_bits_needed_to_encode((unsigned long long) MEMSIZE);
    ps->max_bits = ps->pa_space < ps->va_space ? ps->pa_space : ps->va_space;
    
    // Bits for Page Offset (data bytes).
    ps->offset = num_bits_needed_to_encode((unsigned long long) PGSIZE);
    ps->max_pages = ps->max_bits - ps->offset;

    // Bits for Page Table. 
    num_bits_t table_dir_max = num_bits_needed_to_encode((unsigned long long) PGSIZE / sizeof(unsigned long));
    ps->page_table = table_dir_max;
    
    // Bits for Page Directory.
    num_bits_t remaining = ps->max_pages - ps->page_table;
    // Pick lesser of the two for page_dir so we only have as many Virtual Pages as can be allocated at once.
    ps->page_dir = table_dir_max < remaining ? table_dir_max : remaining;
    
    ps->chars_for_bitmap = (ps->page_dir + ps->page_table) - 3;

    assert(ps->max_pages >= (ps->page_dir + ps->page_table));
    assert(ps->max_bits >= (ps->max_pages + ps->offset));
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
        if (!(c & (1 << (7 - pos)))) {
            return pos;
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
            n = (bitmap[i] & (1 << (7 - pos))) ? n : n - 1;
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
            count = (bitmap[i] & (1 << (7 - pos))) ? count + 1 : count;
        }
    }

    return count;
}

///////////////////////////////////////////////////////////////////////////////////


/* Registered with atexit during vm setup. */
void clean_my_vm(void) {
    free(vm_start);
    free(paging_scheme);

    free(frame_bitmap);
    free(virtual_bitmap);

    while (tlb_cache->size) {
        tlb_store *front = tlb_cache->front;
        tlb_cache->front = front->next;
        tlb_cache->size -= 1;
        free(front);
    }

    free(tlb_cache);

    pthread_mutex_destroy(&library_lock);
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
