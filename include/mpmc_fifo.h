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
    hazard_pointer_thread_record_t* hazard_head;
    hazard_node_gc_t garbage_collector;
} mpmc_fifo_t;

//garbage_collector is bound to hptr. it can be thread-specific, the same as fifo->garbage_collector, or anything
static inline hazard_pointer_thread_record_t* mpmc_fifo_add_hazard_thread_record(mpmc_fifo_t* fifo, hazard_node_gc_t garbage_collector)
{
    return hazard_pointer_thread_record_create_and_push(&fifo->hazard_head, 2, garbage_collector);
}

static inline int mpmc_fifo_init(mpmc_fifo_t* fifo, hazard_node_gc_t garbage_collector, mpmc_fifo_node_t* initial_node)
{
    assert(fifo);
    assert(garbage_collector.free_function);
    assert(initial_node);
    memset(initial_node, 0, sizeof(*initial_node));
    fifo->tail = initial_node;
    fifo->head = fifo->tail;
    fifo->hazard_head = 0;
    fifo->garbage_collector = garbage_collector;
    return 1;
}

static inline void mpmc_fifo_destroy(mpmc_fifo_t* fifo)
{
    if(fifo) {
        hazard_pointer_thread_record_destroy_all(fifo->hazard_head);
        while(fifo->head != NULL) {
            mpmc_fifo_node_t* const tmp = fifo->head;
            fifo->head = tmp->prev;
            fifo->garbage_collector.free_function(fifo->garbage_collector.user_data, &tmp->hazard);
        }
    }
}

//the FIFO owns new_node after pushing
static inline void mpmc_fifo_push(hazard_pointer_thread_record_t* hptr, mpmc_fifo_t* fifo, mpmc_fifo_node_t* new_node)
{
    assert(new_node->value);
    new_node->prev = 0;
    while(1) {
        mpmc_fifo_node_t* const tail = fifo->tail;
        hazard_pointer_using(hptr, &tail->hazard, 0);
        load_load_barrier();
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

static inline void* mpmc_fifo_pop(hazard_pointer_thread_record_t* hptr, mpmc_fifo_t* fifo)
{
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
            //fix list?
            //or just wait???
            continue;
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

#endif

