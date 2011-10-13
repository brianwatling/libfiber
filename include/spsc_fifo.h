#ifndef _SPSC_FIFI_H_
#define _SPSC_FIFI_H_

/*
    Author: Brian Watling
    Email: brianwatling@hotmail.com
    Website: https://github.com/brianwatling

    Description: A single-producer single-consumer FIFO based on "Writing Lock-Free
                 Code: A Corrected Queue" by Herb Sutter. I've modified the queue
                 such that it only allocates the divider node. The node passed
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
    spsc_node_t* first;
    char _cache_padding1[CACHE_SIZE - sizeof(spsc_node_t*)];
    spsc_node_t* divider;
    char _cache_padding2[CACHE_SIZE - sizeof(spsc_node_t*)];
    spsc_node_t* last;
    char _cache_padding3[CACHE_SIZE - sizeof(spsc_node_t*)];
} spsc_fifo_t;

static inline int spsc_fifo_init(spsc_fifo_t* f)
{
    assert(f);
    memset(f, 0, sizeof(*f));
    f->divider = malloc(sizeof(*f->divider));
    if(!f->divider) {
        return 0;
    }
    memset(f->divider, 0, sizeof(*f->divider));
    f->first = f->divider;
    f->last = f->divider;
    return 1;
}

static inline void spsc_fifo_cleanup(spsc_fifo_t* f)
{
    while(f->first != NULL) {
        spsc_node_t* const tmp = f->first;
        f->first = tmp->next;
        free(tmp);
    }
}

//the FIFO owns new_node after pushing
static inline void spsc_fifo_push(spsc_fifo_t* f, spsc_node_t* new_node)
{
    assert(new_node);
    new_node->next = NULL;
    f->last->next = new_node;
    write_barrier();
    f->last = new_node;
}

//the caller owns *out after popping
static inline int spsc_fifo_pop(spsc_fifo_t* f, spsc_node_t** out)
{
    assert(out);
    if(f->divider != f->last) {
        load_load_barrier();
        void* const data = f->divider->next->data;
        f->divider = f->divider->next;

        assert(f->first != f->divider);
        *out = f->first;
        (*out)->data = data;
        f->first = f->first->next;
        return 1;
    }
    return 0;
}

#endif

