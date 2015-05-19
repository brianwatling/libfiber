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

#ifndef _MPMC_LIFO_H_
#define _MPMC_LIFO_H_

#include "machine_specific.h"
#include "mpsc_fifo.h"
#include <assert.h>

typedef mpsc_fifo_node_t mpmc_lifo_node_t;

typedef union
{
    struct {
        uintptr_t volatile counter;
        mpmc_lifo_node_t* volatile head;
    } data;
    pointer_pair_t blob;
} __attribute__ ((__packed__)) mpmc_lifo_t;

#define MPMC_LIFO_INITIALIZER {}

static inline void mpmc_lifo_init(mpmc_lifo_t* lifo)
{
    assert(lifo);
    assert(sizeof(*lifo) == sizeof(pointer_pair_t));
    lifo->data.head = NULL;
    lifo->data.counter = 0;
}

static inline void mpmc_lifo_destroy(mpmc_lifo_t* lifo)
{
    if(lifo) {
        while(lifo->data.head) {
            mpmc_lifo_node_t* const old = lifo->data.head;
            lifo->data.head = lifo->data.head->next;
            free(old);
        }
    }
}

static inline void mpmc_lifo_push(mpmc_lifo_t* lifo, mpmc_lifo_node_t* node)
{
    assert(lifo);
    assert(node);
    mpmc_lifo_t snapshot;
    while(1) {
        snapshot.data.counter = lifo->data.counter;
        load_load_barrier();//read the counter first - this ensures nothing changes while we're trying to push
        snapshot.data.head = lifo->data.head;
        node->next = snapshot.data.head;
        mpmc_lifo_t temp;
        temp.data.head = node;
        temp.data.counter = snapshot.data.counter + 1;
        if(compare_and_swap2(&lifo->blob, &snapshot.blob, &temp.blob)) {
            return;
        }
    }
}

static inline mpmc_lifo_node_t* mpmc_lifo_pop(mpmc_lifo_t* lifo)
{
    assert(lifo);
    mpmc_lifo_t snapshot;
    while(1) {
        snapshot.data.counter = lifo->data.counter;
        load_load_barrier();//read the counter first - this ensures nothing changes while we're trying to pop
        snapshot.data.head = lifo->data.head;
        if(!snapshot.data.head) {
            return NULL;
        }
        mpmc_lifo_t temp;
        temp.data.head = snapshot.data.head->next;
        temp.data.counter = snapshot.data.counter + 1;
        if(compare_and_swap2(&lifo->blob, &snapshot.blob, &temp.blob)) {
            return snapshot.data.head;
        }
    }
}

#endif

