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

#ifndef _MPMC_STACK_H_
#define _MPMC_STACK_H_

#include <assert.h>
#include <stddef.h>
#include "machine_specific.h"

typedef struct mpmc_stack_node
{
    struct mpmc_stack_node* next;
    void* data;
} mpmc_stack_node_t;

typedef struct mpmc_stack
{
    mpmc_stack_node_t* volatile head;
} mpmc_stack_t;

static inline void mpmc_stack_init(mpmc_stack_t* q)
{
    assert(q);
    q->head = NULL;
}

static inline void mpmc_stack_node_init(mpmc_stack_node_t* n, void* data)
{
    assert(n);
    n->data = data;
}

static inline void* mpmc_stack_node_get_data(mpmc_stack_node_t* n)
{
    assert(n);
    return n->data;
}

static inline void mpmc_stack_push(mpmc_stack_t* q, mpmc_stack_node_t* n)
{
    assert(q);
    assert(n);
    mpmc_stack_node_t* head;
    do {
        head = q->head;
        load_load_barrier();
        n->next = head;
    } while(!__sync_bool_compare_and_swap(&q->head, head, n));
}

#define MPMC_RETRY (0)
#define MPMC_SUCCESS (1)

static inline int mpmc_stack_push_timeout(mpmc_stack_t* q, mpmc_stack_node_t* n, size_t tries)
{
    assert(q);
    assert(n);
    mpmc_stack_node_t* head;
    do {
        head = q->head;
        load_load_barrier();
        n->next = head;
        if(__sync_bool_compare_and_swap(&q->head, head, n)) {
            return MPMC_SUCCESS;
        }
        tries -= 1;
    } while(tries > 0);
    return MPMC_RETRY;
}

static inline mpmc_stack_node_t* mpmc_stack_lifo_flush(mpmc_stack_t* q)
{
    assert(q);
#ifdef FIBER_XCHG_POINTER
    return atomic_exchange_pointer((void**)&q->head, NULL);
#else
    mpmc_stack_node_t* head = q->head;
    while(!__sync_bool_compare_and_swap(&q->head, head, 0)) {
        head = q->head;
    }
    return head;
#endif
}

static inline int mpmc_stack_lifo_flush_timeout(mpmc_stack_t* q, mpmc_stack_node_t** out, size_t tries)
{
    assert(q);
    assert(out);
#ifdef FIBER_XCHG_POINTER
    if(tries > 0) {
        *out = mpmc_stack_lifo_flush(q);
        return MPMC_SUCCESS;
    }
    return MPMC_RETRY;
#else
    while(tries > 0) {
        *out = q->head;
        if(__sync_bool_compare_and_swap(&q->head, *out, 0)) {
            return MPMC_SUCCESS;
        }
        tries -= 1;
    }
    return MPMC_RETRY;
#endif
}

static inline mpmc_stack_node_t* mpmc_stack_reverse(mpmc_stack_node_t* head)
{
    mpmc_stack_node_t* fifo = NULL;
    while(head) {
        mpmc_stack_node_t* const next = head->next;
        head->next = fifo;
        fifo = head;
        head = next;
    }
    return fifo;
}

static inline mpmc_stack_node_t* mpmc_stack_fifo_flush(mpmc_stack_t* q)
{
    return mpmc_stack_reverse(mpmc_stack_lifo_flush(q));
}

static inline int mpmc_stack_fifo_flush_timeout(mpmc_stack_t* q, mpmc_stack_node_t** out, size_t tries)
{
    assert(out);
    const int ret = mpmc_stack_lifo_flush_timeout(q, out, tries);
    if(ret == MPMC_SUCCESS) {
        *out = mpmc_stack_reverse(*out);
    }
    return ret;
}

#endif

