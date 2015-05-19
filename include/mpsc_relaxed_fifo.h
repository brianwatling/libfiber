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

#ifndef _MPSC_RELAXED_FIFO_H_
#define _MPSC_RELAXED_FIFO_H_

/*
    Author: Brian Watling
    Email: brianwatling@hotmail.com
    Website: https://github.com/brianwatling

    Description: A multi-producer single-consumer FIFO based on a single-producer
                 single-consumer FIFO . Each producer will have their own SPSC
                 FIFO. A single consumer acts as the consumer for all
                 producers. It's not required to have a thread per producer. This
                 queue is wait-free - that is, any enqueue or dequeue operation
                 is independent of other threads.
                 NOTE: this MPSC FIFO provides *per-producer* FIFO and makes a 
                 best effort attempt to round-robin pops accross all producers.

    Properties: 1. Per-producer FIFO, best effort FIFO accross producers
                2. Wait free
*/

#include "spsc_fifo.h"

typedef struct mpscr_fifo
{
    size_t counter; //this increments on each read. (counter % num_producers)
                    //indicates which producer will be popped from next
    char _cache_padding1[CACHE_SIZE - sizeof(size_t)];
    size_t num_producers;
    char _cache_padding2[CACHE_SIZE - sizeof(size_t)];
    spsc_fifo_t fifos[];
} mpscr_fifo_t;

static inline mpscr_fifo_t* mpscr_fifo_create(size_t num_producers)
{
    assert(num_producers > 0);
    mpscr_fifo_t* const ret = malloc(sizeof(*ret) + num_producers * sizeof(spsc_fifo_t));
    if(!ret) {
        return NULL;
    }
    ret->counter = 0;
    ret->num_producers = num_producers;
    size_t i;
    for(i = 0; i < num_producers; ++i) {
        if(!spsc_fifo_init(&ret->fifos[i])) {
            int j = 0;
            for(j = 0; j < i; ++j) {
                spsc_fifo_destroy(&ret->fifos[j]);
            }
            free(ret);
            return NULL;
        }
    }
    return ret;
}

static inline void mpscr_fifo_destroy(mpscr_fifo_t* f)
{
    if(f) {
        size_t i;
        for(i = 0; i < f->num_producers; ++i) {
            spsc_fifo_t* const the_fifo = &f->fifos[i];
            spsc_fifo_destroy(the_fifo);
        }
        free(f);
    }
}

//the FIFO owns new_node after pushing
static inline void mpscr_fifo_push(mpscr_fifo_t* f, size_t producer_number, spsc_node_t* new_node)
{
    assert(f);
    assert(producer_number < f->num_producers);
    assert(new_node);
    const size_t index = producer_number % f->num_producers;
    spsc_fifo_t* const the_fifo = &f->fifos[index];
    spsc_fifo_push(the_fifo, new_node);
}

//the caller owns the node after popping
static inline spsc_node_t* mpscr_fifo_trypop(mpscr_fifo_t* f)
{
    const size_t num_producers = f->num_producers;
    size_t i;
    for(i = 0; i < num_producers; ++i) {
        const size_t index = f->counter % num_producers;
        ++f->counter;
        spsc_fifo_t* const the_fifo = &f->fifos[index];
        spsc_node_t* const out = spsc_fifo_trypop(the_fifo);
        if(out) {
            return out;
        }
    }
    return NULL;
}

#endif

