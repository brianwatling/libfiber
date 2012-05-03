#ifndef _MPMC_LIFO_H_
#define _MPMC_LIFO_H_

#include "machine_specific.h"
#include "mpsc_fifo.h"
#include <assert.h>

typedef mpsc_fifo_node_t mpmc_lifo_node_t;

typedef union
{
    struct {
        mpmc_lifo_node_t* volatile head;
        uintptr_t volatile counter;
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
    while(1) {
        const mpmc_lifo_t snapshot = *lifo;
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
    while(1) {
        const mpmc_lifo_t snapshot = *lifo;
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

