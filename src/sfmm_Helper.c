#include <stdio.h>
#include "sfmm_Helper.h"

sf_header pack_header_info(size_t payload_size, size_t block_size, unsigned int alloc, unsigned int prev_alloc) {
    sf_header header = payload_size << 32;
    header = header | block_size | (alloc * 0x8) | (prev_alloc * 0x4);
    
    return header;
}

void init_heap() {
    sf_mem_grow();

    // set up prologue
    sf_block *prologue = (sf_block *)sf_mem_start();
    sf_header prologue_header = pack_header_info(0, MINMUM_BLOCK_SIZE, 1, 0);
    prologue->header = prologue_header;
    prologue->body.links.next = NULL;
    prologue->body.links.prev = NULL;
    prologue->prev_footer = 0;

    sf_footer *prologue_footer = (sf_footer *)((char *)prologue + MINMUM_BLOCK_SIZE);
    *prologue_footer = prologue->header;

    setup_epilogue();

    // set up free_block
    sf_block *free_block = (sf_block *)(prologue_footer);
    size_t free_block_size = PAGE_SZ - UNUSED_PADDING - PROLOGUE_SIZE - EPILOGUE_SIZE;
    free_block->header = pack_header_info(0, free_block_size, 0, 1);
    free_block->prev_footer = *prologue_footer;
    free_block->body.links.next = &sf_free_list_heads[NUM_FREE_LISTS - 1];
    free_block->body.links.prev = &sf_free_list_heads[NUM_FREE_LISTS - 1];

    sf_footer *free_block_footer = (sf_footer *)((char *)free_block + free_block_size);
    *free_block_footer = free_block->header;
    
    // set up free lists
    for(int i = 0; i < NUM_FREE_LISTS - 1; i++) {
        sf_free_list_heads[i].header = 0;
        sf_free_list_heads[i].prev_footer = 0;
        sf_free_list_heads[i].body.links.next = &sf_free_list_heads[i];
        sf_free_list_heads[i].body.links.prev = &sf_free_list_heads[i];
    }
    sf_free_list_heads[NUM_FREE_LISTS - 1].header = 0;
    sf_free_list_heads[NUM_FREE_LISTS - 1].prev_footer = 0;
    sf_free_list_heads[NUM_FREE_LISTS - 1].body.links.next = free_block;
    sf_free_list_heads[NUM_FREE_LISTS - 1].body.links.prev = free_block;
}

size_t get_block_size(size_t payload) {
    size_t block_size = payload + sizeof(sf_header) + sizeof(sf_footer);

    if(block_size % ALLIGNMENT_SIZE != 0) {
        block_size = block_size + ALLIGNMENT_SIZE - (block_size % ALLIGNMENT_SIZE);
    }
    return block_size;
}

size_t get_payload_size(sf_block* blockptr) {
    size_t payload_size = (blockptr->header) >> 32;
    return payload_size;
}

int get_prev_alloc_info(sf_block *blockptr) {
    int prev_alloc_info = (((blockptr->header << 60) >> 60) & 0x4) >> 2;
    return prev_alloc_info;
}

int get_alloc_info(sf_block *blockptr) {
    int alloc_info = (((blockptr->header << 60) >> 60) & 0x8) >> 3;
    return alloc_info;
}

size_t get_full_block_size(sf_block *blockptr) {
    size_t block_size = (((blockptr->header << 32) >> 36) << 4);
    return block_size; 
}

int is_split_need(sf_block *blockptr, size_t block_size_request) {
    size_t full_block_size = get_full_block_size(blockptr);
    if((full_block_size - block_size_request) >= MINMUM_BLOCK_SIZE) return 1;
    else return 0;
}

sf_block* split_block(sf_block *blockptr, size_t payload_size, int is_wilderness) {
    size_t alloc_block_size = get_block_size(payload_size);
    size_t full_block_size = get_full_block_size(blockptr);

    // update alloc block header and footer
    int prev_alloc_info = get_prev_alloc_info(blockptr);
    blockptr->header = pack_header_info(payload_size, alloc_block_size, 1, prev_alloc_info);
    sf_footer *alloc_block_footer = (sf_footer *)((char *)blockptr + alloc_block_size);
    *alloc_block_footer = blockptr->header;

    // update free block address and free block's header and footer
    sf_block *free_block = (sf_block *)((char *)blockptr + alloc_block_size);
    free_block->prev_footer = blockptr->header;
    size_t free_block_size = full_block_size - alloc_block_size;
    free_block->header = pack_header_info(0, free_block_size, 0, 1);
    sf_footer *free_block_footer = (sf_footer *)((char *)free_block + free_block_size);
    *free_block_footer = free_block->header;

    // remove the free block from the free list
    int remove_index = find_index_insert_or_remove(full_block_size, is_wilderness);
    remove_block_from_free_lists(blockptr, remove_index);

    // put the free block at the correct index of free lists.
    int insert_index = find_index_insert_or_remove(free_block_size, is_wilderness);
    insert_free_block_to_freelists(free_block, insert_index, is_wilderness);

    return blockptr;
}

int is_coalesce_prev_need(sf_block *blockptr) {
    int prev_alloc = get_prev_alloc_info(blockptr);
    return prev_alloc == 0;
}

int is_coalesce_next_need(sf_block *blockptr) {
    size_t block_size = get_full_block_size(blockptr);
    sf_block *next_block = (sf_block *)((char *)blockptr + block_size);
    int next_alloc = get_alloc_info(next_block);
    return next_alloc == 0;
}

size_t get_block_size_from_header(sf_header header) {
    size_t block_size = (((header << 32) >> 32) >> 4) << 4;

    return block_size;
}

void coalesce_prev_block(sf_block *blockptr, int is_prev_block_wilderness) {
    size_t block_size = get_full_block_size(blockptr);
    sf_header prev_header = (sf_header)(blockptr->prev_footer);
    size_t prev_block_size = get_block_size_from_header(prev_header);
    sf_block *prev_block = (sf_block *)((char *)blockptr - prev_block_size);
    
    // remove the block being coalesced from the free list.
    int index_to_remove = find_index_insert_or_remove(prev_block_size, is_prev_block_wilderness);
    remove_block_from_free_lists(prev_block, index_to_remove);

    // update block header and block footer
    size_t coalesced_block_size = prev_block_size + block_size;
    int prev_alloc_info = get_prev_alloc_info(prev_block);
    prev_block->header = pack_header_info(0, coalesced_block_size, 0, prev_alloc_info);
    sf_footer* coalesced_block_footer = (sf_footer *)((char *)(prev_block) + coalesced_block_size);
    *coalesced_block_footer = prev_block->header;

    // update next block's header and footer (next block' header and footer's prev_alloc becomes to 0)
    sf_block *next_block = (sf_block *)((char *)prev_block + coalesced_block_size);
    // if next block is epilogue, we don't touch it.   
    void *epilogue_address = sf_mem_end() - sizeof(sf_header); 
    if((void *)next_block + sizeof(sf_header) != epilogue_address) {
        next_block->header = ((next_block->header) >> 3) << 3;
        size_t next_block_size = get_full_block_size(next_block);
        sf_footer *next_block_after_coalesced_footer = (sf_footer *)((char *)next_block + next_block_size);
        *next_block_after_coalesced_footer = next_block->header; 
    }       

    // insert the new coalesced block to the correct index of free 
    int is_coalesced_block_wilderness = 0;
    // void *epilogue_address = sf_mem_end() - sizeof(sf_header);
    void *coalesced_block_end_address = (void *)((char *)prev_block + coalesced_block_size + sizeof(sf_footer));
    if(coalesced_block_end_address == epilogue_address) is_coalesced_block_wilderness = 1;
    
    int index_to_insert = find_index_insert_or_remove(coalesced_block_size, is_coalesced_block_wilderness);
    insert_free_block_to_freelists(prev_block, index_to_insert, is_coalesced_block_wilderness); 
}

void coalesce_next_block(sf_block *blockptr) {
    size_t block_size = get_full_block_size(blockptr);
    sf_block *next_block = (sf_block *)((char *)blockptr + block_size);
    size_t next_block_size = get_full_block_size(next_block);

    // remove the block being coalesced from the free list.
    int is_next_block_wilderness = 0; 
    void *epilogue_address = sf_mem_end() - sizeof(sf_header);
    void *next_block_end_address = (void *)((char *)next_block + next_block_size + sizeof(sf_footer));
    if(next_block_end_address == epilogue_address) is_next_block_wilderness = 1;
    int index_to_remove = find_index_insert_or_remove(next_block_size, is_next_block_wilderness);
    remove_block_from_free_lists(next_block, index_to_remove);

    // update block header and block footer
    size_t coalesced_block_size = block_size + next_block_size;
    int prev_alloc_info = get_prev_alloc_info(blockptr);
    blockptr->header = pack_header_info(0, coalesced_block_size, 0, prev_alloc_info);
    sf_footer* coalesced_block_footer = (sf_footer *)((char *)(blockptr) + coalesced_block_size);
    *coalesced_block_footer = blockptr->header;

    // update next block's header and footer (next block' header and footer's prev_alloc becomes to 0)
    sf_block *next_block_after_coalesced = (sf_block *)((char *)blockptr + coalesced_block_size);
    // if next block is epilogue, we don't touch it.
    if((void *)next_block_after_coalesced + sizeof(sf_header) != epilogue_address) {
        next_block_after_coalesced->header = ((next_block_after_coalesced->header) >> 3) << 3;
        size_t next_block_after_coalesced_size = get_full_block_size(next_block_after_coalesced);
        sf_footer *next_block_after_coalesced_footer = (sf_footer *)((char *)next_block_after_coalesced + next_block_after_coalesced_size);
        *next_block_after_coalesced_footer = next_block_after_coalesced->header;  
    }

    // insert the new coalesced block to the correct index of free lists
    int index = find_index_insert_or_remove(coalesced_block_size, is_next_block_wilderness);
    insert_free_block_to_freelists(blockptr, index, is_next_block_wilderness);
}

void coalesce_prev_next_block(sf_block *blockptr) {
    size_t block_size = get_full_block_size(blockptr);
    sf_header prev_header = (sf_header)(blockptr->prev_footer);
    size_t prev_block_size = get_block_size_from_header(prev_header);

    // the start address of the coalesced free block
    sf_block *prev_block = (sf_block *)((char *)blockptr - prev_block_size);

    sf_block *next_block = (sf_block *)((char *)blockptr + block_size);
    size_t next_block_size = get_full_block_size(next_block);

    // remove the blocks being coalesced from the free list. (the previous block and the next block)
    int is_prev_block_wilderness = 0; 
    void *epilogue_address = sf_mem_end() - sizeof(sf_header);
    void *prev_block_end_address = (void *)((char *)prev_block + prev_block_size + sizeof(sf_footer));
    if(prev_block_end_address == epilogue_address) is_prev_block_wilderness = 1;
    int index_of_prev_block_to_remove = find_index_insert_or_remove(prev_block_size, is_prev_block_wilderness);
    remove_block_from_free_lists(prev_block, index_of_prev_block_to_remove);    

    int is_next_block_wilderness = 0; 
    void *next_block_end_address = (void *)((char *)next_block + next_block_size + sizeof(sf_footer));
    if(next_block_end_address == epilogue_address) is_next_block_wilderness = 1;
    int index_of_next_block_to_remove = find_index_insert_or_remove(next_block_size, is_next_block_wilderness);
    remove_block_from_free_lists(next_block, index_of_next_block_to_remove);

    // update the coalesced bloce header and footer
    size_t coalesced_block_size = prev_block_size + block_size + next_block_size;
    int prev_alloc_info = get_prev_alloc_info(prev_block);
    prev_block->header = pack_header_info(0, coalesced_block_size, 0, prev_alloc_info);
    sf_footer* coalesced_block_footer = (sf_footer *)((char *)(prev_block) + coalesced_block_size);
    *coalesced_block_footer = prev_block->header;

    // update next block's header and footer (next block' header and footer's prev_alloc becomes to 0)
    sf_block *next_block_after_coalesced = (sf_block *)((char *)prev_block + coalesced_block_size);
    // if next block is epilogue, we don't touch it.
    if((void *)next_block_after_coalesced + sizeof(sf_header) != epilogue_address) {
        next_block_after_coalesced->header = ((next_block_after_coalesced->header) >> 3) << 3;
        size_t next_block_after_coalesced_size = get_full_block_size(next_block_after_coalesced);
        sf_footer *next_block_after_coalesced_footer = (sf_footer *)((char *)next_block_after_coalesced + next_block_after_coalesced_size);
        *next_block_after_coalesced_footer = next_block_after_coalesced->header; 
    }

    // insert the new coalesced block to the correct index of free lists
    int index = find_index_insert_or_remove(coalesced_block_size, is_next_block_wilderness);
    insert_free_block_to_freelists(prev_block, index, is_next_block_wilderness);  
}

void setup_epilogue() {
    // set up epilogue
    sf_header *epilogue = (sf_header *)((char *)sf_mem_end() - sizeof(sf_header));
    *epilogue = pack_header_info(0, 0, 1, 0);
}

int is_valid_pointer(sf_block *ptr) {
    if(ptr == NULL) return 0;

    // if pointer is not 16-byte aligned, invalid.
    if(((uintptr_t)ptr & 0xF) != 0) return 0;

    // if block size is less than 32 or not a multiple of 16, invalid.
    size_t block_size = get_full_block_size(ptr);
    if((block_size < MINMUM_BLOCK_SIZE) || (block_size % ALLIGNMENT_SIZE) != 0) return 0;

    int alloc_info = get_alloc_info(ptr);
    if(alloc_info == 0) return 0;

    return 1;
}

int find_index_alloc(size_t block_size) {
    int num_of_mbs = block_size / MINMUM_BLOCK_SIZE;

    if(sf_free_list_heads[0].body.links.next != (&sf_free_list_heads[0])) {
        if(num_of_mbs == 1 && (block_size % MINMUM_BLOCK_SIZE) == 0) return 0;
    }
    if((sf_free_list_heads[1].body.links.next) != (&sf_free_list_heads[1])) {
        if(num_of_mbs >= 1 && num_of_mbs <= 2) return 1;
    }
    if((sf_free_list_heads[2].body.links.next) != (&sf_free_list_heads[2])) {
        if(num_of_mbs >= 2 && num_of_mbs <= 3) return 2;
    }
    if((sf_free_list_heads[3].body.links.next) != (&sf_free_list_heads[3])) {
        if(num_of_mbs >= 3 && num_of_mbs <= 5) return 3;
    }
    if((sf_free_list_heads[4].body.links.next) != (&sf_free_list_heads[4])) {
        if(num_of_mbs >= 5 && num_of_mbs <= 8) return 4;
    }
    if((sf_free_list_heads[5].body.links.next) != (&sf_free_list_heads[5])) {
        if(num_of_mbs >= 8 && num_of_mbs <= 13) return 5;
    }
    if((sf_free_list_heads[6].body.links.next) != (&sf_free_list_heads[6])) {
        if(num_of_mbs >= 13 && num_of_mbs <= 21) return 6;
    }
    if((sf_free_list_heads[7].body.links.next) != (&sf_free_list_heads[7])) {
        if(num_of_mbs >= 21 && num_of_mbs <= 34) return 7;
    }
    if((sf_free_list_heads[8].body.links.next) != (&sf_free_list_heads[8])) {
        if(num_of_mbs >= 34) {
            int is_found = 0;
            sf_block *current = sf_free_list_heads[8].body.links.next;
            while(current != (sf_free_list_heads + 8)) {
                size_t current_block_size = get_full_block_size(current);
                if(current_block_size >= block_size) {
                    is_found = 1;
                    break;
                } 
                else current = current->body.links.next;
            }  
            if(is_found == 1) return 8;          
        }
    }
    sf_block *wilderness = sf_free_list_heads[9].body.links.next;
    if(wilderness != (&(sf_free_list_heads[9]))) {
        size_t wilderness_block_size = get_full_block_size(wilderness);
        if(wilderness_block_size >= block_size) {
            return 9;
        }
    }
    return -1; // means no big enough block available to alloc, need to call sf_mem_grow()

}

int find_index_insert_or_remove(size_t block_size, int is_wilderness) {
    if(is_wilderness) return 9;

    int num_of_mbs = block_size / MINMUM_BLOCK_SIZE;

    // index 0
    if(num_of_mbs == 1 && (block_size % MINMUM_BLOCK_SIZE) == 0) return 0;
    // index 1
    if(num_of_mbs >= 1 && num_of_mbs <= 2) return 1;
    // index 2
    if(num_of_mbs >= 2 && num_of_mbs <= 3) return 2;
    // index 3
    if(num_of_mbs >= 3 && num_of_mbs <= 5) return 3;
    // index 4
    if(num_of_mbs >= 5 && num_of_mbs <= 8) return 4;
    // index 5
    if(num_of_mbs >= 8 && num_of_mbs <= 13) return 5;
    // index 6
    if(num_of_mbs >= 13 && num_of_mbs <= 21) return 6;
    // index 7
    if(num_of_mbs >= 21 && num_of_mbs <= 34) return 7;
    // index 8
    return 8;
}

void insert_free_block_to_freelists(sf_block* blockptr, int index, int is_wilderness) {
    int index_to_insert = 0;
    if(is_wilderness) index_to_insert = 9;
    else index_to_insert = index;

    sf_block *sentinel = &sf_free_list_heads[index_to_insert];
    sf_block *next_block = sentinel->body.links.next;
    blockptr->body.links.next = next_block;
    blockptr->body.links.prev = sentinel;
    next_block->body.links.prev = blockptr;
    sentinel->body.links.next = blockptr;
}

void remove_block_from_free_lists(sf_block* blockptr, int index) {
    // check if the free list contains the block that we want to remove.
    int is_contain = 0;
    sf_block *current = sf_free_list_heads[index].body.links.next;
    while(current != (sf_free_list_heads + index)) {
        if(current == blockptr) {
            is_contain = 1;
            break;
        }
        current = current->body.links.next;
    }

    // remove the block from the free list
    if(is_contain) {
        sf_block *next_block = blockptr->body.links.next;
        sf_block *prev_block = blockptr->body.links.prev;

        prev_block->body.links.next = next_block;
        next_block->body.links.prev = prev_block;
    }
}
