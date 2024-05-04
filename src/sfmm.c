/**
 * Do not submit your assignment with a main function in this file.
 * If you submit with a main function in this file, you will get a zero.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "debug.h"
#include "sfmm.h"
#include "errno.h"
#include "sfmm_Helper.h"

double total_amount_of_payload = 0.0;

double total_size_of_allocated_blocks = 0.0;

double total_heap_size = 0.0;

double maximum_aggregate_payload = 0.0;

double current_aggregate_payload = 0.0;

int is_heap_init = 0;

void *sf_malloc(size_t size) {
    // To be implemented.

    // if size is 0, return NULL;
    if (size == 0) {
        return NULL;
    }

    void* start_ptr = sf_mem_start();
    void* end_ptr = sf_mem_end();

    if(start_ptr == end_ptr) {
        // initialize the heap
        init_heap();
        is_heap_init = 1;
        total_heap_size = PAGE_SZ;
    }

    // calcuate the total block size
    size_t request_block_size = get_block_size(size);

    total_amount_of_payload += size;
    total_size_of_allocated_blocks += request_block_size;

    current_aggregate_payload += size;
    if(current_aggregate_payload >= maximum_aggregate_payload) {
        maximum_aggregate_payload = current_aggregate_payload;
    }

    // check which free lists this block belongs to, if no, grow()
    int free_list_found = 0;
    while(!free_list_found) {
        int index_to_alloc = find_index_alloc(request_block_size);


        if(index_to_alloc >= 0 && index_to_alloc <= 7) {
            free_list_found = 1;

            sf_block *head_block = sf_free_list_heads[index_to_alloc].body.links.next;
            if(is_split_need(head_block, request_block_size)) {
                sf_block *blockptr = split_block(head_block, size, 0);

                return (char *)blockptr + 2 * sizeof(sf_header);
            }
            else {
                // do not split. block from free -> alloc
                // update the header and footer of the block
                int prev_alloc_info = get_prev_alloc_info(head_block);
                size_t full_block_size = get_full_block_size(head_block);
                head_block->header = pack_header_info(size, full_block_size, 1, prev_alloc_info);
                sf_footer *footer = (sf_footer *)((char *)head_block + full_block_size);
                *footer = head_block->header;

                // remove the free block from the free list
                remove_block_from_free_lists(head_block, index_to_alloc);

                return (char *)head_block + 2 * sizeof(sf_header);
            } 
        }

        else if(index_to_alloc == 8) {
            free_list_found = 1;
            sf_block *current = sf_free_list_heads[index_to_alloc].body.links.next;
            while(current != (sf_free_list_heads + 8)) {
                size_t current_block_size = get_full_block_size(current);
                if(current_block_size >= request_block_size) {
                    if(is_split_need(current, request_block_size)) {
                        sf_block *blockptr = split_block(current, size, 0);

                        return (char *)blockptr + 2 * sizeof(sf_header);
                    }
                    else {
                        // do not split. block from free -> alloc
                        // update the header and footer of the block
                        int prev_alloc_info = get_prev_alloc_info(current);
                        size_t full_block_size = get_full_block_size(current);
                        current->header = pack_header_info(size, full_block_size, 1, prev_alloc_info);
                        sf_footer *footer = (sf_footer *)((char *)current + full_block_size);
                        *footer = current->header;

                        // remove the free block from the free list
                        remove_block_from_free_lists(current, index_to_alloc);

                        return (char *)current + 2 * sizeof(sf_header);
                    }   
                } 
                else current = current->body.links.next;
            }            
        }
        // wilderness block
        else if(index_to_alloc == 9) {
            free_list_found = 1;
            sf_block *wilderness = sf_free_list_heads[9].body.links.next; 
            if(is_split_need(wilderness, request_block_size)) {
                sf_block *blockptr = split_block(wilderness, size, 1);

                return (char *)blockptr + 2 * sizeof(sf_header);
            }
            else {
                // do not split. block from free -> alloc
                // update the header and footer of the block
                int prev_alloc_info = get_prev_alloc_info(wilderness);
                size_t full_block_size = get_full_block_size(wilderness);
                wilderness->header = pack_header_info(size, full_block_size, 1, prev_alloc_info);
                sf_footer *footer = (sf_footer *)((char *)wilderness + full_block_size);
                *footer = wilderness->header;
                
                // remove the free block from the free list
                remove_block_from_free_lists(wilderness, index_to_alloc);    

                return (char *)wilderness + 2 * sizeof(sf_header);
            }  
        }
        // grow the heap, create a new page size;
        else {
            void* new_pz_ptr = sf_mem_grow();
            total_heap_size += PAGE_SZ;
            if(new_pz_ptr == NULL) {
                sf_errno = ENOMEM;
                return NULL;
            }

            sf_block *new_pz_block = (sf_block *)((char *)new_pz_ptr - 2 * sizeof(sf_header));
            setup_epilogue();
            sf_footer *prev_block_footer = (sf_footer *)new_pz_block;
            int prev_alloc_info = ((((*prev_block_footer) << 60) >> 60) & 0x8) >> 3;

            new_pz_block->header = pack_header_info(0, PAGE_SZ, 0, prev_alloc_info); 
            sf_footer *new_pz_block_footer = (sf_footer *)((char *)new_pz_block + PAGE_SZ);
            *new_pz_block_footer = new_pz_block->header;
            if(is_coalesce_prev_need(new_pz_block)) {
                coalesce_prev_block(new_pz_block, 1);
            }
            else {
                insert_free_block_to_freelists(new_pz_block, 9, 1);
            }
        }
    }

    abort();
}

void sf_free(void *pp) {
    // To be implemented.
    sf_block *blockptr_to_free = (sf_block *)((char *)pp - 2 * sizeof(sf_header));

    if(!is_valid_pointer(blockptr_to_free)) {
        abort();
    }

    size_t block_size = get_full_block_size(blockptr_to_free);
    size_t payload_size = get_payload_size(blockptr_to_free);

    total_amount_of_payload -= payload_size;
    total_size_of_allocated_blocks -= block_size;

    current_aggregate_payload -= payload_size;
    if(current_aggregate_payload >= maximum_aggregate_payload) {
        maximum_aggregate_payload = current_aggregate_payload;
    }

    // case 1: don't need to coalesce with any block    
    if(!is_coalesce_prev_need(blockptr_to_free) && !is_coalesce_next_need(blockptr_to_free)) {
        // block from alloc -> free. update this block's header and footer
        int prev_alloc_info = get_prev_alloc_info(blockptr_to_free);
        blockptr_to_free->header = pack_header_info(0, block_size, 0, prev_alloc_info);
        sf_footer *block_footer = (sf_footer *)((char *)blockptr_to_free + block_size);
        *block_footer = blockptr_to_free->header;

        // update next block's header and footer (next block' header and footer's prev_alloc becomes to 0)
        sf_block *next_block = (sf_block *)((char *)blockptr_to_free + block_size);
        // if next block is epilogue, we don't touch it.    
        void *epilogue_address = sf_mem_end() - sizeof(sf_header);   
        if((void *)next_block + sizeof(sf_header) != epilogue_address) {
            next_block->header = ((next_block->header) >> 3) << 3;
            size_t next_block_size = get_full_block_size(next_block);
            sf_footer *next_block_footer = (sf_footer *)((char *)next_block + next_block_size);
            *next_block_footer = next_block->header;
        }   

        // insert the block to the correct index of free lists.
        int index = find_index_insert_or_remove(block_size, 0);
        insert_free_block_to_freelists(blockptr_to_free, index, 0);
        return;
    }

    // case 2: coalesce with previous and next block
    if(is_coalesce_prev_need(blockptr_to_free) && is_coalesce_next_need(blockptr_to_free)) {
        coalesce_prev_next_block(blockptr_to_free);
        return;
    }
    // case 3: coalesce with previous block 
    if(is_coalesce_prev_need(blockptr_to_free)) {
        coalesce_prev_block(blockptr_to_free, 0);
        return;
    }
    // case 4: coalesce with next block
    if(is_coalesce_next_need(blockptr_to_free)) {
        coalesce_next_block(blockptr_to_free);
        return;
    }


    abort();
}

void *sf_realloc(void *pp, size_t rsize) {
    // To be implemented.

    sf_block *blockptr_to_realloc = (sf_block *)((char *)pp - 2 * sizeof(sf_header));

    if(!is_valid_pointer(blockptr_to_realloc)) {
        abort();
    }

    if(rsize == 0) {
        sf_free(pp);
        return NULL;
    }

    size_t realloc_block_size = get_block_size(rsize);
    size_t original_block_size = get_full_block_size(blockptr_to_realloc);

    // case 1: realloc to a larger size.
    if(realloc_block_size > original_block_size) {
        void *new_block_payload_ptr = sf_malloc(rsize);
        if(new_block_payload_ptr == NULL) return NULL;

        size_t payload_size = get_payload_size(blockptr_to_realloc);
        memcpy(new_block_payload_ptr, pp, payload_size);
        sf_free(pp);

        return new_block_payload_ptr;
    }
    // case 2: realloc to a same or smaller size
    else {
        // case 1: need to split the block.
        if(is_split_need(blockptr_to_realloc, realloc_block_size)) {
            size_t original_payload_size = get_payload_size(blockptr_to_realloc);

            // update alloc block header and footer
            int prev_alloc_info = get_prev_alloc_info(blockptr_to_realloc);
            blockptr_to_realloc->header = pack_header_info(rsize, realloc_block_size, 1, prev_alloc_info);
            sf_footer *alloc_block_footer = (sf_footer *)((char *)blockptr_to_realloc + realloc_block_size);
            *alloc_block_footer = blockptr_to_realloc->header;

            // update free block address and free block's header and footer
            sf_block *free_block = (sf_block *)((char *)blockptr_to_realloc + realloc_block_size);
            free_block->prev_footer = blockptr_to_realloc->header;
            size_t free_block_size = original_block_size - realloc_block_size;
            free_block->header = pack_header_info(0, free_block_size, 0, 1);
            sf_footer *free_block_footer = (sf_footer *)((char *)free_block + free_block_size);
            *free_block_footer = free_block->header;            
            
            // coalesce with the next block if next block is a free block
            if(is_coalesce_next_need(free_block)) {
                coalesce_next_block(free_block);
            }
            else{
                int index = find_index_insert_or_remove(free_block_size, 0);
                insert_free_block_to_freelists(free_block, index, 0);
            }

            total_amount_of_payload = (total_amount_of_payload + rsize - original_payload_size);
            total_size_of_allocated_blocks = (total_size_of_allocated_blocks + realloc_block_size - original_block_size);
            current_aggregate_payload = (current_aggregate_payload + rsize - original_block_size);
            if(current_aggregate_payload >= maximum_aggregate_payload) {
                maximum_aggregate_payload = current_aggregate_payload;
            }

            return (void *)((char *)blockptr_to_realloc + 2 * sizeof(sf_header));
        }
        // case 2: don't need to split the block because split will cause a splinter
        else {
            size_t original_payload_size = get_payload_size(blockptr_to_realloc);

            // update the block header and footer
            size_t new_payload_size = get_payload_size(blockptr_to_realloc);
            int prev_alloc_info = get_prev_alloc_info(blockptr_to_realloc);
            blockptr_to_realloc->header = pack_header_info(new_payload_size, original_block_size, 1, prev_alloc_info);
            sf_footer *block_footer = (sf_footer *)((char *)blockptr_to_realloc + original_block_size);
            *block_footer = blockptr_to_realloc->header;

            total_amount_of_payload = (total_amount_of_payload + rsize - original_payload_size);
            current_aggregate_payload = (current_aggregate_payload + rsize - original_block_size);
            if(current_aggregate_payload >= maximum_aggregate_payload) {
                maximum_aggregate_payload = current_aggregate_payload;
            }

            return (void *)((char *)blockptr_to_realloc + 2 * sizeof(sf_header));
        }
    }

    abort();
}

double sf_fragmentation() {
    // To be implemented.

    if(total_size_of_allocated_blocks == 0.0) return 0.0;

    return total_amount_of_payload / total_size_of_allocated_blocks;

    abort();
}

double sf_utilization() {
    // To be implemented.

    if(is_heap_init == 0) return 0.0;

    return maximum_aggregate_payload / total_heap_size;    

    abort();
}
