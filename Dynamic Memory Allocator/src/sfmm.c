#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "debug.h"
#include "sfmm.h"
#include "errno.h"

/*
 * Function Proptotypes
 */
void setup_quick_and_free_lists();
sf_block* check_quick_lists(size_t size);
sf_block* check_free_lists(size_t size);
int mem_grow();
void* coalescing(sf_header* block_header);
void add_to_free_list(sf_header* block_header);

int is_valid_header(void* ptr);
int belongs_to_quick_list(double size);
void add_to_quick_list(sf_header* block_ptr);
void check_flush(int bin_num);


int first_page_flag = 1;		// Global variable to check if first page added to heap.

void *sf_malloc(size_t size) {
	size_t block_size = size;
	if(size == 0) {
        return NULL;
    }

    else if(size < 24) {
        if(size < 0){
            sf_errno = ENOMEM;
            return NULL;
        }
        size = 24;
    }

	size += 8;						// Include header size to requested size.
    if (size%16 != 0) {
        size += 16 - (size % 16);	// make requested size to be multiple of 16 bytes.
    }

    if (first_page_flag) {
    	setup_quick_and_free_lists();
    }

    sf_block* found_mem_block = check_quick_lists(size); // Check quick list for a mem block with requested size.
    if(found_mem_block != NULL) {
        return found_mem_block -> body.payload;
    }

    // If requested memory block is found in free list,
    found_mem_block = check_free_lists(size);
    if(found_mem_block != NULL) {
        return found_mem_block -> body.payload;
    }

    // If we couldn't find a memory block with required size, request a page to our heap.
    if (mem_grow() != -1) {
        first_page_flag = 0;
        // If we successfully added a page to our heap, we call sf_malloc with same size again.
        // Becase this time we know that there is a available block to allocaet in the free list.
        found_mem_block = sf_malloc(block_size);
        return found_mem_block;
    } else {
        sf_errno = ENOMEM;
        return NULL;
    }
}

void sf_free(void *pp) {
    sf_header* block_ptr = (sf_header*)pp;      // Cast void pointer to row pointer.

	if(!is_valid_header(block_ptr))
		abort();

    sf_header* block_header = --block_ptr;      // Get the header field of this block

    size_t mem_size = (*block_header^MAGIC) & ~0x6;	// Get the memory Size of block

    // Find out where would this block would go after freeing it.
    if(belongs_to_quick_list(mem_size)) {
        add_to_quick_list(block_header);
    }
    else {
    	// perform coalescing before sending it to freelist.
    	sf_header* block_ptr2 = block_header;
    	*block_ptr2 = ((*block_header^MAGIC) & ~THIS_BLOCK_ALLOCATED)^MAGIC;		// Set this block's header alloc. bit to 0.

    	block_ptr2 = block_ptr2 + ((*block_header^MAGIC) & ~0x6)/8 - 1;
    	*block_ptr2 = ((*block_header^MAGIC) & ~THIS_BLOCK_ALLOCATED)^MAGIC;		// Set this block's footer alloc. bit to 0.
        void* free_block_to_add = coalescing(block_header);
        add_to_free_list(free_block_to_add);
    }

}

void *sf_realloc(void *pp, size_t rsize) {
    sf_header* block_ptr = pp;

    if (!is_valid_header(block_ptr)) {	// Validate the block.
    	sf_errno = EINVAL;
        abort();
    }

    block_ptr--;                // Move pointer to header.

    if(rsize == 0){ 			// if requested size is 0, free the block
        sf_free(pp);
        return pp;
    }

    size_t new_block_size = rsize;
    size_t block_size = ((*block_ptr^MAGIC) & ~0x6);

    if(rsize < 24) {
        if(rsize < 0){
            sf_errno = ENOMEM;
            return NULL;
        }
        new_block_size = 24;
    }

    new_block_size += 8;                      // Include header size to requested size.
    if (new_block_size%16 != 0) {
        new_block_size += 16 - (new_block_size % 16);   // make requested size to be multiple of 16 bytes.
    }

    // We need to expand block size
    if (new_block_size > block_size) {
        sf_header* new_block_header = sf_malloc(rsize);		// Allocate new block that has paylaod of requested size
        if(new_block_header == NULL){ 						// if sf_malloc returns null, sf_realloc should also return null
            return NULL;
        }
        new_block_header--;
        sf_header reallocated_header = ((*new_block_header^MAGIC));
        memcpy(new_block_header, block_ptr, block_size);	// Copy the previous block to next block, till the end of first block.
        *new_block_header = (reallocated_header^MAGIC);		// Copy the header value back to generated block.
        sf_free(pp);										// Free prevoius block and add it to freelist.
        return ++new_block_header;
    }
	// We need to shrink block size.
    else {
        // Check if the remaining splinter's size is greater than 32.
        if ((block_size - (new_block_size) >= 32)) {
            // Splitter block goes to top part.
            *block_ptr = (((*block_ptr^MAGIC) & PREV_BLOCK_ALLOCATED) + (block_size - new_block_size))^MAGIC;   // Update splitter block header.
            sf_header* splinter_header = block_ptr;
            block_ptr += (block_size - new_block_size)/8 - 1;
            *block_ptr = ((*splinter_header^MAGIC)^MAGIC);   // Update splinter block footer.

            block_ptr++;
            sf_header* shrunk_header = block_ptr;
            *shrunk_header = (new_block_size + THIS_BLOCK_ALLOCATED)^MAGIC; // Update header of shrunk block

		    block_ptr = coalescing(splinter_header);          // Perform coalescing, if neccessary.
            add_to_free_list(block_ptr);        			  // Add this splitter block to free list.

            return ++shrunk_header;
        }
        // If remaining splinter's size is less than 32, then we don't need to perform coalesing. Return same pointer back.
        return pp;
    }
}

/*
 * This method sets quicklist's length to 0 to indicate that this list has no block in it. List's first field
 * set to nothing.
 */
void setup_quick_and_free_lists() {
	for (int index = 0; index < NUM_QUICK_LISTS; index++) {
	    sf_quick_lists[index].length = 0;
    }

    // In each free list, there is a dummy. It should look itself.
    for (int index = 0; index < NUM_FREE_LISTS; index++) {
        sf_free_list_heads[index].body.links.prev = &sf_free_list_heads[index];
        sf_free_list_heads[index].body.links.next = &sf_free_list_heads[index];
    }
}

/*
 * This method checks quick lists to find a memory block that satisfies the size requirment.
 */
sf_block* check_quick_lists(size_t size) {
	// Compare requested size with largest mem. in the quick list.
    if (size > 176) {
        return NULL;
    }
    else {
        if (sf_quick_lists[(size-32)/16].length == 0) {	// Check if list contains any available block
            return NULL;
        }
        // If such memory block exist, remove it from the top of stack.
        else {
            sf_header* block_pointer;								// Block pointer for pointer arithmetic.
            block_pointer = &(sf_quick_lists[ (size-32)/16 ].first->header);	// Get the header of first mem. block in the list.
            block_pointer--;                                        // Move pointer to previous blocks footer.
            sf_block* block_to_return = (sf_block*) block_pointer;  // Construct a sf_block pointer for the found memory block.
            sf_quick_lists[ (size-32)/16 ].length--;
            sf_quick_lists[ (size-32)/16 ].first = sf_quick_lists[ (size-32)/16 ].first -> body.links.next; // Remove block form the top.
            return block_to_return;
        }
    }
}

/*
 * This method checks free list heads to find a memory block that satisfies size requirment.
 */
sf_block* check_free_lists(size_t size) {
	sf_block* block_to_return;

    int list_location = -1;
    int mem_block_flag = 0;
    int list_upper_range = 32;

    for(int i=0; i<10; i++) {
        if(size <= list_upper_range) {
            list_location = i;
            break;
        } else {
        	list_upper_range *= 2;
        }
    }

    if (list_location == -1) list_location = 9;     	// If requested size is too large, just look at last list.

    for (int i = list_location; i < 10; i++) {          // Start checking each list
        sf_block* list_dummy = &sf_free_list_heads[i];
        for (
            sf_block* mem_block = list_dummy -> body.links.next;  // Set a sf_block pointer to first mem block after dummy.
            mem_block != list_dummy;                // While mem_block is not equal to dummy, keep loop going.
            mem_block = mem_block -> body.links.next
        ) {
            if ((((mem_block -> header)^MAGIC) & ~(0x6)) >= size) {    	// If this mem block satisfies size requirment,
                mem_block_flag = 1;               				// Need to break out of outter loop.

                block_to_return = mem_block;            // Save this mem. block
                (mem_block -> body.links.prev) -> body.links.next = mem_block -> body.links.next;   // Remove mem_block from the free list.
                (mem_block -> body.links.next) -> body.links.prev = mem_block -> body.links.prev;
                break;
            }
        }
        if (mem_block_flag == 1) {
            break;
        }
    }

    // If we couldn't find a mem block with satisfactory size, return NULL.
    if (mem_block_flag == 0) {
        return NULL;
    }

    sf_header* block_pointer = &(block_to_return -> header);// Place a pointer to this block's header;

    if ((((*block_pointer^MAGIC) & ~0x6) - size) < 32) { 							// If the found mem. block is perfect fit for requested size,
        *block_pointer = ((*block_pointer^MAGIC) | THIS_BLOCK_ALLOCATED)^MAGIC;		// set header of mem. block's allocated bit to 1.
        block_pointer += ((*block_pointer^MAGIC) & ~0x6)/8;   						// move block pointer to next block's header
        *block_pointer = ((*block_pointer^MAGIC) | PREV_BLOCK_ALLOCATED)^MAGIC;		// Set next block's previos block alloc. bit to 1.

        return block_to_return;
    }
    // If we will have some splitters with the found mem. block,
    else {
        sf_header* splinter_header = block_pointer;                 	// save the starting address of the splinter
        *splinter_header = ((*splinter_header^MAGIC) - size)^MAGIC;		// Decrement the found mem. block's size by requested size.
        block_pointer += ((*splinter_header^MAGIC) & ~0x6) / 8 - 1;   	// Move the pointer to the footer of the splinter
        sf_footer* splinter_footer = block_pointer;
        *splinter_footer = ((*splinter_header^MAGIC))^MAGIC;				// Update the footer of the splinter

        block_pointer++;                                    			// move the pointer to the header of the block to be allocated
        *block_pointer = (size + THIS_BLOCK_ALLOCATED)^MAGIC;           // update header of allocated block, set allocation bit
        block_pointer += size/8;                            			// move to the header of the next block

        *block_pointer = ((*block_pointer^MAGIC) | PREV_BLOCK_ALLOCATED)^MAGIC;		// set prev_allocated bit of header to 1
        block_to_return = (sf_block*)splinter_footer;					// Construct a sf_block struct for for found mem. block for bottom part of free mem block.

        if(((*block_pointer^MAGIC) & THIS_BLOCK_ALLOCATED) != THIS_BLOCK_ALLOCATED){ 		// if the block is not allocated, update its footer
            block_pointer += ((*block_pointer^MAGIC) & ~0x6) / 8 - 1;   // move the pointer to the footer of the block
            *block_pointer = ((*block_pointer^MAGIC) | PREV_BLOCK_ALLOCATED)^MAGIC;	// set prev_allocated bit of footer to 1
        }

        add_to_free_list(splinter_header);                        		// put the splinter in free list
        return block_to_return;
    }
}

/*
 *	This method performs coalescing on passed block. This method can be called from sf_mem_grow(), sf_free(), and flush().
 * 	It checks adjecent memory block in memory and if they are free, they are removed from their location free list. Then
 *  they are coalesed them with passed block and returns passed header, with possibly different memory address.
 */
void* coalescing(sf_header* block_header) {
	sf_header* block_pointer = block_header;					// Create a blokc pointer in memory for ease of use.
	sf_footer* current_block_footer = block_pointer + ((*block_pointer^MAGIC) & ~0x6)/8 - 1;	// Save curent block's footer.
	block_pointer = block_header;								// Move block pointer back to current block's header.
	sf_footer* prev_footer = --block_pointer;					// Save previous block's footer
	block_pointer = current_block_footer;

	sf_header* next_header = ++block_pointer;					// Save next block's header

	size_t current_block_size = (*block_header^MAGIC) & ~0x6;			// Save the current block's size
	size_t prev_block_size = (*prev_footer^MAGIC) & ~0x6;				// Save the prev block's size
	size_t next_block_size = (*next_header^MAGIC) & ~0x6;				// Save the next block's size

	// If prev block is free, first remove it from sf_free_list_heads. Later, perform coalesing to it,
	// update proper header and footer fields. Since prev block is free, we dont need to change this block bit
	// or prev block bit.
	if (((*block_header^MAGIC) & PREV_BLOCK_ALLOCATED) != PREV_BLOCK_ALLOCATED) {
		block_pointer = prev_footer;							// Set block pointer to prev block footer.
		block_pointer -= prev_block_size/8;						// Move pointer to prev footer of previous block struct field.
		sf_block* prev_block = (sf_block*)(block_pointer);		// Get prev block as a sf_block structure.
		// Remove previoys block from its location in sf_free_list_heads, so that we can safely coalesce.
		(prev_block -> body.links.prev) -> body.links.next = prev_block -> body.links.next;
		(prev_block -> body.links.next) -> body.links.prev = prev_block -> body.links.prev;
		//Now, move onto coalescing part.
		block_pointer = prev_footer;
		sf_header* prev_header = (block_pointer-(prev_block_size/8-1));		// Save prev block's header.
		*prev_header = ((*prev_header^MAGIC) + current_block_size)^MAGIC;					// Update size of the prev header.
		*current_block_footer = ((*prev_header^MAGIC))^MAGIC;	// update size of current block footer.
		block_header = prev_header;								// Current block header needs to be looking at prev. block header address after coalescing.
		current_block_size += prev_block_size;					// update current block size.
	}

	// If next block is free, perform coalesing with it, update proper header and footer fields.
	if (((*next_header^MAGIC) & THIS_BLOCK_ALLOCATED) != THIS_BLOCK_ALLOCATED) {
		block_pointer = current_block_footer;					// Set block pointer to current block's footer.
		sf_block* next_block = (sf_block*)(block_pointer);		// Get next block as a sf_block structure.
		// Remove next block from its location in free list heads.
		(next_block -> body.links.prev) -> body.links.next = next_block -> body.links.next;
		(next_block -> body.links.next) -> body.links.prev = next_block -> body.links.prev;
		// Now move onto coalescing part.
		block_pointer = next_header;
		sf_footer* next_footer = (block_pointer+next_block_size/8-1);		//Save next block's footer.
		*next_footer = ((*next_footer^MAGIC) + current_block_size)^MAGIC;	// Update size of next block's footer.
		current_block_footer = next_footer;						// Current block footer needs to be looking at next block's footer address after coalescing.
		*block_header = ((*block_header^MAGIC) + next_block_size)^MAGIC;		// Update current block header size.
		current_block_size += next_block_size;					// Update current block size.
	}

	block_pointer = block_header;
	block_pointer += current_block_size/8;
	*block_pointer = ((*block_pointer^MAGIC) & ~PREV_BLOCK_ALLOCATED)^MAGIC;		// Regardless of coalescing status, update next block's prev bit to not allocated.
	return block_header;
}

/*
 *	Purpose of this method is to add passed block header to free list. This method gets called right after coalesing(), or
 *  after mem_grow().
 */
void add_to_free_list(sf_header* block_header) {
    sf_header* block_pointer = block_header;
    sf_block* current_block = (sf_block*)(--block_pointer);
    size_t current_block_size = ((*block_header^MAGIC) & ~0x6);   // get the current block size

    // Find which list this mem. block resides in free_list.
    int list_location = -1;
    int list_upper_range = 32;

    for(int i=0; i<10; i++) {
        if(current_block_size <= list_upper_range) {
            list_location = i;
            break;
        } else {
        	list_upper_range *= 2;
        }
    }

    if(list_location == -1){	// If size is too large, just add it to last list.
        list_location = 9;
    }

    // Find proper dummy node in free list.
    sf_block* dummy = &sf_free_list_heads[list_location];
    // Place current block into doubly linked list, right afte dummy node.
    current_block -> body.links.next = dummy -> body.links.next;
    current_block -> body.links.prev = dummy;
    (dummy -> body.links.next) -> body.links.prev = current_block;
    dummy -> body.links.next = current_block;
}

/*
 * This method adds a new page to the end of the heap.
 * Header adn footer of new page is constructed here.
 * 8 bytes reserved paddings are alos set here.
 *
 * @return -1 if unsuccessful, 1 otherwise
 */
int mem_grow() {
    size_t page_size = PAGE_SZ; 				// Every new page will have 8 bytes padding cutoff.

    // If this page is the first page added to heap,
    if(first_page_flag) {
        page_size -= 16;                     		// If this page is the first page that is added to heap, we need to reserve 16 bytes for header and footer padding.
        sf_header* new_page_ptr = sf_mem_grow();	// Add new page to heap. Save its starting address.

        if(new_page_ptr == NULL){   // if sf_mem_grow() returns NULL, return -1
    		return -1;
    	}

    	// Last 8 bytes of allocated page will be 8 bytes padding.
        sf_footer* reserved_footer = sf_mem_end();
        reserved_footer--;

        // these are going to be 8 bytes paddings, set this block alloc. bit to prevent coalescing.
        *new_page_ptr = THIS_BLOCK_ALLOCATED^MAGIC;
        *reserved_footer = THIS_BLOCK_ALLOCATED^MAGIC;

        // Now construct header and footer for this page.
        new_page_ptr++;
        sf_header* new_page_header = new_page_ptr;
        *new_page_header = ((page_size) + PREV_BLOCK_ALLOCATED)^MAGIC;  // size of the page +  prev. alloc. bit set, to prevent coalescing.

        new_page_ptr += (page_size / 8) - 1; 							// Move new page cursor to footer field of page.
        *new_page_ptr = ((page_size) + PREV_BLOCK_ALLOCATED)^MAGIC;		// size of the page +  prev. alloc. bit set.

        // Now add this consturcted page to free list.
        add_to_free_list(new_page_header);
        return 1;
    }

    // There exist some other pages in hte heap. Check previous footer to get more info about last mem block.
    else {
    	sf_footer* reserved_footer = sf_mem_end();						// Save the 8 bytes padding at the end.
    	reserved_footer--;
    	sf_header* new_page_header = reserved_footer;					// New page header will be added to 8 Byte padding area of last page.

    	// If previous block is allocated, No need for coalesing
    	if( ((*reserved_footer^MAGIC) & PREV_BLOCK_ALLOCATED) == PREV_BLOCK_ALLOCATED) {
    		new_page_header = sf_mem_grow();							// Add new page to heap.

    		if(new_page_header == NULL){
    			return -1;
    		}

            new_page_header--;
    		// Go to last row of newly added page, and set 8 bytes padding to here.
    		reserved_footer = sf_mem_end();
    		reserved_footer--;
    		*reserved_footer = (THIS_BLOCK_ALLOCATED^MAGIC);			// Set 8 byte padding to be alloc. to prevent coalescing.

    		// Now, construct header and footer for this newly created page.
    		sf_header* page_pointer = new_page_header;
    		sf_footer* new_page_footer = page_pointer + (page_size/8 -1);
    		*new_page_header = (page_size + PREV_BLOCK_ALLOCATED)^MAGIC;
    		*new_page_footer = (page_size + PREV_BLOCK_ALLOCATED)^MAGIC;

    		// Now add this newly created page to free list.
    		add_to_free_list(new_page_header);

    		return 1;
    	}
    	// Previous block is free, we need to perform coalescing.
    	else {
    		// sf_mem_grow creates a new page in the heap. We need to go back a row, overwrite the padding from previous block, then
    		// add a padding to end of this block.
    		new_page_header = sf_mem_grow();

			if(new_page_header == NULL){
    			return -1;
    		}
            new_page_header--;


    		reserved_footer = sf_mem_end();
    		reserved_footer--;
    		*reserved_footer = (THIS_BLOCK_ALLOCATED^MAGIC);

    		sf_header* free_block_ptr = new_page_header;
    		*new_page_header = (page_size^MAGIC);						// Update header of new page.

    		sf_header* free_block_footer = free_block_ptr + (page_size/8 -1);
    		*free_block_footer = (page_size^MAGIC);						// Update footer of new page.

    		new_page_header = coalescing(new_page_header);				// Perform coalescing on this block.
    		add_to_free_list(new_page_header);
    		return 1;
    	}
    }
}

int belongs_to_quick_list(double size) {
    if ((size - 32)/16 < 10)
        return 1;
    else
        return 0;
}

void add_to_quick_list(sf_header* block_ptr) {
    size_t block_size = ((*block_ptr^MAGIC) & ~0x6);			// Get the block size
    int list_location = ( block_size - 32 )/16;

    check_flush(list_location);                         	// Perform flushing in list location, if required.
    sf_block* block_to_add = (sf_block*)--block_ptr;    	// Construct a block pointer with this header.

    if(sf_quick_lists[list_location].length != 0) {
        block_to_add -> body.links.next = sf_quick_lists[list_location].first; // Next field filled in memory.
    }
    else {
    	block_to_add -> body.links.next = NULL;
    }

    sf_quick_lists[list_location].first = block_to_add;    // Add created block struct to quick list.
    sf_quick_lists[list_location].length++;                // Increment such bin's length.
}
/*
 *	This method performs flushing on quick list specific location.
 */
void check_flush(int list_location) {
    sf_block* current_block;
    sf_header *block_ptr;

    // if we dont have enough space in this bin, flush it. Otherwise, do nothing.
    if(sf_quick_lists[list_location].length == 5) {
        for (int i = 0; i < 5; i++) {
            sf_quick_lists[list_location].length--;
            current_block = sf_quick_lists[list_location].first;  	// get the current block from the bin.
            // Remove the top block from quick list.
            sf_quick_lists[list_location].first = sf_quick_lists[list_location].first -> body.links.next;
           	block_ptr = &(current_block->header);					        // set block ptr header to header field of block to be coalesced.
            size_t block_size = (*block_ptr^MAGIC) & ~0x6;
            *block_ptr = ((*block_ptr^MAGIC) & ~THIS_BLOCK_ALLOCATED)^MAGIC;
            sf_header* block_header = block_ptr;
            block_ptr += (block_size)/8 -1;
            *block_ptr = (*block_header^MAGIC)^MAGIC;
            block_header = coalescing(block_header);              					        // Perform coalescing with proper blocks.
            add_to_free_list(block_header);                                 // Add this block to free list.
        }
    }
}

int is_valid_header(void* ptr) {
	if (ptr == NULL) {
        return 0;
    }
    ptr = ptr - 8;
    // Check if pointer is 16 byte alligned
    if ((((*(sf_header*)ptr)^MAGIC) & (0x9)) != 0) {
        return 0;
    }
    // Check value of header
    else {
        sf_header* header = (sf_header*)ptr;
        size_t header_size = (*header^MAGIC) & ~0x6;
        sf_footer* footer = (header + header_size/8 - 1);
        sf_header* mem_start = sf_mem_start();
        mem_start++;
        sf_footer* mem_end = sf_mem_end();
        mem_end--;

        if(header_size < 32) {
            return 0;
        }
        if(header_size % 16 != 0) {
            return 0;
        }
        if((header < mem_start) && ((footer >= mem_end))) {
            return 0;
        }
        // If this block is not allocated, reject.
        if(((*header^MAGIC) & THIS_BLOCK_ALLOCATED) == 0) {
            return 0;
        }
        // IF prev block seems to be free and,
        if (((*header^MAGIC) & PREV_BLOCK_ALLOCATED) == 0) {
            sf_footer* prev_footer = --header;
            // if previous block is not free, reject
            if (((*prev_footer^MAGIC) & THIS_BLOCK_ALLOCATED) != 0) {
                return 0;
            }
            // If previous footer says this block is free, and
            else {
                sf_header* prev_header = prev_footer - ((*prev_footer^MAGIC)/8 -1);
                // If header of previous block doesn't match footer of previous block, reject
                if ((*prev_header^MAGIC) != (*prev_footer^MAGIC)) {
                    return 0;
                }
            }
        }
        // Valid header.
        return 1;
    }
}
