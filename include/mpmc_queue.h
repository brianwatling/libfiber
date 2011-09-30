#ifndef _MPMC_QUEUE_H_
#define _MPMC_QUEUE_H_

#include <assert.h>
#include <stddef.h>
#include "machine_specific.h"

typedef struct mpmc_queue_node
{
    struct mpmc_queue_node* next;
    void* data;
} mpmc_queue_node_t;

typedef struct mpmc_queue
{
    mpmc_queue_node_t* volatile head;
    char _cache_padding[CACHE_SIZE - sizeof(mpmc_queue_node_t*)];
} mpmc_queue_t;

static inline void mpmc_queue_init(mpmc_queue_t* q)
{
    assert(q);
    q->head = 0;
}

static inline void mpmc_queue_node_init(mpmc_queue_node_t* n, void* data)
{
    assert(n);
    n->data = data;
}

static inline void* mpmc_queue_node_get_data(mpmc_queue_node_t* n)
{
    assert(n);
    return n->data;
}

static inline void mpmc_queue_push(mpmc_queue_t* q, mpmc_queue_node_t* n)
{
    assert(q);
    assert(n);
    mpmc_queue_node_t* head = q->head;
    do {
        n->next = head;
    } while(!__sync_bool_compare_and_swap(&q->head, head, n));
}

#define MPMC_RETRY (0)
#define MPMC_SUCCESS (1)

static inline int mpmc_queue_push_timeout(mpmc_queue_t* q, mpmc_queue_node_t* n, size_t tries)
{
    assert(q);
    assert(n);
    mpmc_queue_node_t* head = q->head;
    do {
        n->next = head;
        if(__sync_bool_compare_and_swap(&q->head, head, n)) {
            return MPMC_SUCCESS;
        }
        tries -= 1;
    } while(tries > 0);
    return MPMC_RETRY;
}

static inline mpmc_queue_node_t* mpmc_queue_lifo_flush(mpmc_queue_t* q)
{
    assert(q);
    mpmc_queue_node_t* head = q->head;
    while(!__sync_bool_compare_and_swap(&q->head, head, 0)) {
        head = q->head;
    }
    return head;
}

static inline int mpmc_queue_lifo_flush_timeout(mpmc_queue_t* q, mpmc_queue_node_t** out, size_t tries)
{
    assert(q);
    assert(out);
    *out = q->head;
    while(tries > 0) {
        if(__sync_bool_compare_and_swap(&q->head, *out, 0)) {
            return MPMC_SUCCESS;
        }
        tries -= 1;
        *out = q->head;
    }
    return MPMC_RETRY;
}

static inline mpmc_queue_node_t* mpmc_queue_reverse(mpmc_queue_node_t* head)
{
    mpmc_queue_node_t* fifo = 0;
    while(head) {
        mpmc_queue_node_t* const next = head->next;
        head->next = fifo;
        fifo = head;
        head = next;
    }
    return fifo;
}

static inline mpmc_queue_node_t* mpmc_queue_fifo_flush(mpmc_queue_t* q)
{
    return mpmc_queue_reverse(mpmc_queue_lifo_flush(q));
}

static inline int mpmc_queue_fifo_flush_timeout(mpmc_queue_t* q, mpmc_queue_node_t** out, size_t tries)
{
    assert(out);
    const int ret = mpmc_queue_lifo_flush_timeout(q, out, tries);
    if(ret == MPMC_SUCCESS) {
        *out = mpmc_queue_reverse(*out);
    }
    return ret;
}

#endif

