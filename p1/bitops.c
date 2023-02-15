/*
* Add NetID and names of all project partners
*   yp315  | Yash Patel
*   kks107 | Kev Sharma
*   
*   CS 416 - Undergraduate OS
*   Tested on man.cs.rutgers.edu
*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define NUM_TOP_BITS 4 //top bits to extract
#define BITMAP_SIZE 4 //size of the bitmap array
#define SET_BIT_INDEX 17 //bit index to set 
#define GET_BIT_INDEX 17 //bit index to read

static unsigned int myaddress = 4026544704;   // Binary  would be 11110000000000000011001001000000


enum ORIENTATION {
    RIGHT_TO_LEFT, /* Set third bit = 0100 */
    LEFT_TO_RIGHT /* Set third bit = 0010 */
};

enum ORIENTATION orientation = LEFT_TO_RIGHT; /* See print_bitmap*/

/* 
 * Function 1: EXTRACTING OUTER (TOP-ORDER) BITS
 */
static unsigned int get_top_bits(unsigned int value,  int num_bits)
{
	//Implement your code here
    unsigned int ns, n; /* ns: number of bits (value takes up) */
    for (n = value, ns = 0; n; n >>= 1, ++ns);
    
    int k = ns - num_bits;
    return (((~0 << k) & value) >> k);
}


/* 
 * Function 2: SETTING A BIT AT AN INDEX 
 * Function to set a bit at "index" bitmap
 */
static void set_bit_at_index(char *bitmap, int index)
{
    //Implement your code here	
    int dividend = index / 8;
    int remainder = index % 8;

    int byte_offset = orientation ? dividend : (BITMAP_SIZE-1-dividend);
    int shift_by = orientation ? (7 - remainder) : remainder;

    bitmap[byte_offset] |= (1 << shift_by);
}


/* 
 * Function 3: GETTING A BIT AT AN INDEX 
 * Function to get a bit at "index"
 */
static int get_bit_at_index(char *bitmap, int index)
{
    //Get to the location in the character bitmap array
    //Implement your code here
    int dividend = index / 8;
    int remainder = index % 8;

    int byte_offset = orientation ? dividend : (BITMAP_SIZE-1-dividend);
    int shift_by = orientation ? (7 - remainder) : remainder;
    
    return (bitmap[byte_offset] & (1 << shift_by)) > 0;
}

static void print_bitmap(char *bitmap) {
    int i;
    int max_bits = 8 * BITMAP_SIZE;

    if (orientation) { /* Prints left to right*/   
        for (i = 0; i < max_bits; ++i){
            if(i%8 ==0) printf(" ");
            printf("%d", get_bit_at_index(bitmap, i));
        }
    } else { /* Prints right to left */
        for (i = max_bits-1; i >= 0; --i){
            printf("%d", get_bit_at_index(bitmap, i));
            if(i%8 ==0) printf(" ");
        }
    }

    printf("\n");
}


int main () {

    /* 
     * Function 1: Finding value of top order (outer) bits Now let’s say we
     * need to extract just the top (outer) 4 bits (1111), which is decimal 15  
    */
    unsigned int outer_bits_value = get_top_bits (myaddress , NUM_TOP_BITS);
    printf("Function 1: Outer bits value %u \n", outer_bits_value); 

    /* 
     * Function 2 and 3: Checking if a bit is set or not
     */
    char *bitmap = (char *)malloc(BITMAP_SIZE);  //We can store 32 bits (4*8-bit per character)
    memset(bitmap, 0, BITMAP_SIZE); //clear everything

    /* 
     * Let's try to set the bit 
     */
    set_bit_at_index(bitmap, SET_BIT_INDEX);

    /* 
     * Let's try to read bit)
     */
    printf("\nFunction 3: The value at %dth location %d\n", 
            GET_BIT_INDEX, get_bit_at_index(bitmap, GET_BIT_INDEX));
    
    // print_bitmap(bitmap);

    free(bitmap);
    return 0;
}
