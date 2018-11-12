/*
 * Copyright (c) 2012-2015, Brian Watling and other contributors
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _DIST_FIFO_H_
#define _DIST_FIFO_H_

/*
    Author: Brian Watling
    Email: brianwatling@hotmail.com
    Website: https://github.com/brianwatling

    Notes: A "Distinguished FIFO"
        1. Many threads can pop from the fifo
        2. One distinguished thread can push in a wait free manner

    Assumptions:
        1. Reading data from a node that has already been popped is safe (even if it's reclaimed etc). Use hazard pointers if this is not a valid assumption.
        2. The CPU supports a double-word CAS operation
*/

#include <assert.h>
#include <malloc.h>
#include <string.h>
#include <stdint.h>
#include "hazard_pointer.h"
#include "machine_specific.h"
#include "mpsc_fifo.h"

typedef mpsc_fifo_node_t dist_fifo_node_t;

typedef struct dist_fifo_pointer
{
    uintptr_t counter;
    dist_fifo_node_t* volatile node;
} __attribute__ ((__packed__)) dist_fifo_pointer_t;

typedef union
{
    dist_fifo_pointer_t pointer;
    pointer_pair_t blob;
} __attribute__ ((__packed__)) __attribute__((__aligned__(2 * sizeof(void *)))) dist_fifo_pointer_wrapper_t;

typedef struct dist_fifo
{
    volatile dist_fifo_pointer_wrapper_t __attribute__ ((__aligned__(2 * sizeof(void*)))) head;//volatile is required to prevent compiler optimizations. true story.
    char _cache_padding1[CACHE_SIZE - sizeof(dist_fifo_pointer_wrapper_t)];
    dist_fifo_node_t* tail;
    char _cache_padding2[CACHE_SIZE - sizeof(dist_fifo_node_t*)];
} __attribute__((__packed__)) dist_fifo_t;

static inline int dist_fifo_init(dist_fifo_t* fifo)
{
    assert(fifo);
    assert(sizeof(dist_fifo_pointer_wrapper_t) == 2 * sizeof(void*));
    assert(sizeof(dist_fifo_pointer_t) == sizeof(pointer_pair_t));
    assert((uintptr_t)fifo % (2 * sizeof(void*)) == 0);//alignment (TODO: need a better solution than this. perhaps allocate the memory for the fifo here)
    assert(sizeof(dist_fifo_t) == 2 * CACHE_SIZE);
    memset((void*)&fifo->head, 0, sizeof(fifo->head));
    fifo->tail = (dist_fifo_node_t*)calloc(1, sizeof(*fifo->tail));
    fifo->head.pointer.node = fifo->tail;
    if(!fifo->tail) {
        return 0;
    }
    return 1;
}

static inline void dist_fifo_destroy(dist_fifo_t* fifo)
{
    if(fifo) {
        while(fifo->head.pointer.node != NULL) {
            dist_fifo_node_t* const tmp = fifo->head.pointer.node;
            fifo->head.pointer.node = tmp->next;
            free(tmp);
        }
    }
}

static inline void dist_fifo_push(dist_fifo_t* fifo, dist_fifo_node_t* new_node)
{
    assert(fifo);
    dist_fifo_node_t* const tail = fifo->tail;
    assert(new_node);
    new_node->next = NULL;
    write_barrier();//the node must be terminated before it's visible to the reader as the new tail
    tail->next = new_node;
    fifo->tail = new_node;
}

#define DIST_FIFO_EMPTY ((dist_fifo_node_t*)(0))
#define DIST_FIFO_RETRY ((dist_fifo_node_t*)(-1))

static inline dist_fifo_node_t* dist_fifo_trypop(dist_fifo_t* fifo)
{
    assert(fifo);

    dist_fifo_pointer_wrapper_t old_head;
    old_head.pointer.counter = fifo->head.pointer.counter;
    load_load_barrier();//read the counter first - this ensures nothing changes while we're trying to steal (ie. prevents ABA)
    old_head.pointer.node = fifo->head.pointer.node;

    dist_fifo_node_t* const prev_head = old_head.pointer.node;
    dist_fifo_node_t* const prev_head_next = prev_head->next;
    if(prev_head_next) {
        void* const data = prev_head_next->data;
        dist_fifo_pointer_wrapper_t new_head;
        new_head.pointer.node = prev_head_next;
        new_head.pointer.counter = old_head.pointer.counter + 1;
        if(!compare_and_swap2(&fifo->head.blob, &old_head.blob, &new_head.blob)) {
            return DIST_FIFO_RETRY;
        }
        prev_head->data = data;
        return prev_head;
    }
    return DIST_FIFO_EMPTY;
}

#endif

