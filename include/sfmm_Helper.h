#ifndef SFMM_HELPER_H
#define SFMM_HELPER_H
#include "sfmm.h"

#define MINMUM_BLOCK_SIZE 32
#define PROLOGUE_SIZE     32
#define UNUSED_PADDING    8
#define EPILOGUE_SIZE     8
#define ALLIGNMENT_SIZE   16

sf_header pack_header_info(size_t payload_size, size_t block_size, unsigned int alloc, unsigned int prev_alloc);

void init_heap();

// calculate the total size of the block according to the request from the user.
size_t get_block_size(size_t payload);

// get the payload size of a block according the the block header infomation.
size_t get_payload_size(sf_block* blockptr);

// get the total size of a block from the header of that block
size_t get_block_size_from_header(sf_header header);

// get the block prev_alloc info from the block header
int get_prev_alloc_info(sf_block *blockptr);

// get the block alloc info from the block header
int get_alloc_info(sf_block *blockptr);

// get the total block size info from the block header
size_t get_full_block_size(sf_block *blockptr);

// check if a block being allocated need to be splitted. without creating a splinter
// @param block_size_request: is the block size that payload add header and footer
int is_split_need(sf_block *blockptr, size_t block_size_request);

// split the block. place the remaining part to it belonged free list. 
// if it's wilderness block, still place it at wilderness free list
// return the pointer point the allocate block
sf_block* split_block(sf_block *blockptr, size_t payload_size, int is_wilderness);

// check if a block needs to coalesce with previous block.
int is_coalesce_prev_need(sf_block *blockptr);

// check if a block needs to coalesce with next block.
int is_coalesce_next_need(sf_block *blockptr);

// coalesce the free block to the previous free block.
void coalesce_prev_block(sf_block *blockptr, int is_prev_block_wilderness);

// clalesce the free block to the next free block.
void coalesce_next_block(sf_block *blockptr);

// coalesce the free block to the previous free block and next free block
void coalesce_prev_next_block(sf_block *blockptr);

void setup_epilogue();

// when free a memory, check if a pointer passed to free() is a valid pointer
int is_valid_pointer(sf_block *ptr);

// find the correct index of the free list to allocate the block
// if no big enough block available, return -1;
int find_index_alloc(size_t block_size);

// find the correct index of the free list to insert or remove the block
int find_index_insert_or_remove(size_t block_size, int is_wilderness);

// insert the free block to the correct index of free lists.
// @param index: the index of the free lists that we want to insert the block to
void insert_free_block_to_freelists(sf_block* blockptr, int index, int is_wilderness);

// remove a block from the free lists
// @param index: the index of the free lists that we want to remove the block from.
void remove_block_from_free_lists(sf_block* blockptr, int index);

#endif
