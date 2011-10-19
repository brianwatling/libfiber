#ifndef _SPSC_FIFI_H_
#define _SPSC_FIFI_H_

/*
    Author: Brian Watling
    Email: brianwatling@hotmail.com
    Website: https://github.com/brianwatling

    Description: A single-producer single-consumer FIFO based on "Writing Lock-Free
                 Code: A Corrected Queue" by Herb Sutter. The node passed
                 into the push method is owned by the FIFO until it is returned
                 to the consumer via the pop method. This FIFO is wait-free.
                 NOTE: This SPSC FIFO provides strict FIFO ordering

    Properties: 1. Strict FIFO
                2. Wait free
*/

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "machine_specific.h"

typedef struct spsc_node
{
    void* data;
    struct spsc_node* volatile next;
} spsc_node_t;

typedef struct spsc_fifo
{
    spsc_node_t* head;//consumer read items from head
    char _cache_padding1[CACHE_SIZE - sizeof(spsc_node_t*)];
    spsc_node_t* tail;//producer pushes onto the tail
    char _cache_padding3[CACHE_SIZE - sizeof(spsc_node_t*)];
    spsc_node_t* divider;
} spsc_fifo_t;

static inline int spsc_fifo_init(spsc_fifo_t* f)
{
    assert(f);
    f->divider = calloc(1, sizeof(*f->divider));
    f->tail = f->divider;
    f->head = f->divider;
    if(!f->divider) {
        return 0;
    }
    return 1;
}

static inline void spsc_fifo_destroy(spsc_fifo_t* f)
{
    if(f) {
        while(f->head != NULL) {
            spsc_node_t* const tmp = f->head;
            f->head = tmp->next;
            free(tmp);
        }
    }
}

//the FIFO owns new_node after pushing
static inline void spsc_fifo_push(spsc_fifo_t* f, spsc_node_t* new_node)
{
    assert(f);
    assert(new_node);
    new_node->next = NULL;
    write_barrier();//the node must be terminated before it's visible to the reader as the new tail
    spsc_node_t* const prev_tail = f->tail;
    f->tail = new_node;
    prev_tail->next = new_node;
}

//the caller owns the node after popping
static inline spsc_node_t* spsc_fifo_pop(spsc_fifo_t* f)
{
    assert(f);
    spsc_node_t* const prev_head = f->head;
    spsc_node_t* const prev_head_next = prev_head->next;
    if(prev_head_next) {
        f->head = prev_head_next;
        prev_head->data = prev_head_next->data;
        return prev_head;
    }
    return NULL;
}

#endif

