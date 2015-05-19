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

#ifndef _MPMC_FIFO_H_
#define _MPMC_FIFO_H_

/*
    Author: Brian Watling
    Email: brianwatling@hotmail.com
    Website: https://github.com/brianwatling

    Notes: An adaption of "An optimistic approach to lock-free FIFO queues"
           by Edya Ladan-Mozes and Nir Shavit
*/

#include <assert.h>
#include <malloc.h>
#include <string.h>
#include "hazard_pointer.h"
#include "machine_specific.h"

#define MPMC_HAZARD_COUNT (2)

typedef struct mpmc_fifo_node
{
    hazard_node_t hazard;
    void* value;
    struct mpmc_fifo_node* prev;
    struct mpmc_fifo_node* next;
} mpmc_fifo_node_t;

typedef struct mpmc_fifo
{
    mpmc_fifo_node_t* volatile head;//consumer reads items from head
    char _cache_padding[CACHE_SIZE - sizeof(mpmc_fifo_node_t*)];
    mpmc_fifo_node_t* volatile tail;//producer pushes onto the tail
} mpmc_fifo_t;

static inline int mpmc_fifo_init(mpmc_fifo_t* fifo, mpmc_fifo_node_t* initial_node)
{
    assert(fifo);
    assert(initial_node);
    assert(initial_node->hazard.gc_function);
    initial_node->value = NULL;
    initial_node->prev = NULL;
    initial_node->next = NULL;
    fifo->tail = initial_node;
    fifo->head = fifo->tail;
    return 1;
}

static inline void mpmc_fifo_destroy(hazard_pointer_thread_record_t* hptr, mpmc_fifo_t* fifo)
{
    assert(hptr);
    if(fifo) {
        while(fifo->head != NULL) {
            mpmc_fifo_node_t* const tmp = fifo->head;
            fifo->head = tmp->prev;
            hazard_pointer_free(hptr, &tmp->hazard);
        }
    }
}

//the FIFO owns new_node after pushing
static inline void mpmc_fifo_push(hazard_pointer_thread_record_t* hptr, mpmc_fifo_t* fifo, mpmc_fifo_node_t* new_node)
{
    assert(hptr);
    assert(fifo);
    assert(new_node);
    assert(new_node->value);
    new_node->prev = NULL;
    while(1) {
        mpmc_fifo_node_t* const tail = fifo->tail;
        hazard_pointer_using(hptr, &tail->hazard, 0);
        if(tail != fifo->tail) {
            continue;//tail switched while we were 'using' it
        }

        new_node->next = tail;
        if(__sync_bool_compare_and_swap(&fifo->tail, tail, new_node)) {
            tail->prev = new_node;
            hazard_pointer_done_using(hptr, 0);
            return;
        }
    }
}

static inline void* mpmc_fifo_trypop(hazard_pointer_thread_record_t* hptr, mpmc_fifo_t* fifo)
{
    assert(hptr);
    assert(fifo);
    void* ret = NULL;

    while(1) {
        mpmc_fifo_node_t* const head = fifo->head;
        hazard_pointer_using(hptr, &head->hazard, 0);
        if(head != fifo->head) {
            continue;//head switched while we were 'using' it
        }

        mpmc_fifo_node_t* const prev = head->prev;
        if(!prev) {
            //empty (possibly just temporarily, let the caller decide what to do)
            hazard_pointer_done_using(hptr, 0);
            return NULL;
        }

        hazard_pointer_using(hptr, &prev->hazard, 1);
        if(head != fifo->head) {
            continue;//head switched while we were 'using' head->prev
        }

        //push thread has successfully updated prev
        ret = prev->value;
        if(__sync_bool_compare_and_swap(&fifo->head, head, prev)) {
            hazard_pointer_done_using(hptr, 0);
            hazard_pointer_done_using(hptr, 1);
            hazard_pointer_free(hptr, &head->hazard);
            break;
        }
    }
    return ret;
}

//TODO: size() (?) O(n), not good for much except testing
//TODO: try_push()
//TODO: fix_list() (?) allows a pop()er to help push()er threads along by possibly updating nodes' prev field
//TODO: peek() (?) careful, need to hold a hazard pointer the whole time (add done_peek()?)

#endif

