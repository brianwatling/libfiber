#ifndef _MPSC_FIFO_H_
#define _MPSC_FIFO_H_

/*
    Author: Brian Watling
    Email: brianwatling@hotmail.com
    Website: https://github.com/brianwatling
*/

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "machine_specific.h"

typedef struct mpsc_node
{
    void* data;
    struct mpsc_node* volatile next;
} mpsc_node_t;

typedef struct mpsc_fifo
{
    mpsc_node_t* volatile head;//consumer read items from head
    char _cache_padding1[CACHE_SIZE - sizeof(mpsc_node_t*)];
    mpsc_node_t* tail;//producer pushes onto the tail
    char _cache_padding3[CACHE_SIZE - sizeof(mpsc_node_t*)];
    mpsc_node_t* divider;
} mpsc_fifo_t;

static inline int mpsc_fifo_init(mpsc_fifo_t* f)
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

static inline void mpsc_fifo_destroy(mpsc_fifo_t* f)
{
    if(f) {
        while(f->head != NULL) {
            mpsc_node_t* const tmp = f->head;
            f->head = tmp->next;
            free(tmp);
        }
    }
}

//the FIFO owns new_node after pushing
static inline void mpsc_fifo_push(mpsc_fifo_t* f, mpsc_node_t* new_node)
{
    assert(f);
    assert(new_node);
    new_node->next = NULL;
    write_barrier();//the node must be terminated before it's visible to the reader as the new tail
    mpsc_node_t* const prev_tail = atomic_exchange_pointer((void**)&f->tail, new_node);
    prev_tail->next = new_node;
}

//the caller owns the node after popping
static inline mpsc_node_t* mpsc_fifo_pop(mpsc_fifo_t* f)
{
    assert(f);
    mpsc_node_t* const prev_head = f->head;
    mpsc_node_t* const prev_head_next = prev_head->next;
    if(prev_head_next) {
        f->head = prev_head_next;
        prev_head->data = prev_head_next->data;
        return prev_head;
    }
    return NULL;
}

#endif

