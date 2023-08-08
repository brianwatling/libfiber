// SPDX-FileCopyrightText: 2012-2023 Brian Watling <brian@oxbo.dev>
// SPDX-License-Identifier: MIT

#ifndef _MPMC_FIFO_H_
#define _MPMC_FIFO_H_

/*
    Notes: An adaption of "An optimistic approach to lock-free FIFO queues"
           by Edya Ladan-Mozes and Nir Shavit
*/

#include <assert.h>
#include <malloc.h>
#include <string.h>

#include "hazard_pointer.h"
#include "machine_specific.h"

#define MPMC_HAZARD_COUNT (2)

typedef struct mpmc_fifo_node {
  hazard_node_t hazard;
  void* value;
  struct mpmc_fifo_node* prev;
  struct mpmc_fifo_node* next;
} mpmc_fifo_node_t;

typedef struct mpmc_fifo {
  _Atomic(mpmc_fifo_node_t*) volatile head;  // consumer reads items from head
  char _cache_padding[FIBER_CACHELINE_SIZE - sizeof(mpmc_fifo_node_t*)];
  _Atomic(mpmc_fifo_node_t*) tail;           // producer pushes onto the tail
} mpmc_fifo_t;

static inline int mpmc_fifo_init(mpmc_fifo_t* fifo,
                                 mpmc_fifo_node_t* initial_node) {
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

static inline void mpmc_fifo_destroy(hazard_pointer_thread_record_t* hptr,
                                     mpmc_fifo_t* fifo) {
  assert(hptr);
  if (fifo) {
    while (fifo->head != NULL) {
      mpmc_fifo_node_t* const tmp = fifo->head;
      fifo->head = tmp->prev;
      hazard_pointer_free(hptr, &tmp->hazard);
    }
  }
}

// the FIFO owns new_node after pushing
static inline void mpmc_fifo_push(hazard_pointer_thread_record_t* hptr,
                                  mpmc_fifo_t* fifo,
                                  mpmc_fifo_node_t* new_node) {
  assert(hptr);
  assert(fifo);
  assert(new_node);
  assert(new_node->value);
  new_node->prev = NULL;
  while (1) {
    mpmc_fifo_node_t* tail =
        atomic_load_explicit(&fifo->tail, memory_order_acquire);
    hazard_pointer_using(hptr, &tail->hazard, 0);
    if (tail != atomic_load_explicit(&fifo->tail, memory_order_acquire)) {
      continue;  // tail switched while we were 'using' it
    }

    new_node->next = tail;
    if (atomic_compare_exchange_weak_explicit(&fifo->tail, &tail, new_node,
                                              memory_order_release,
                                              memory_order_relaxed)) {
      tail->prev = new_node;
      hazard_pointer_done_using(hptr, 0);
      return;
    }
  }
}

static inline void* mpmc_fifo_trypop(hazard_pointer_thread_record_t* hptr,
                                     mpmc_fifo_t* fifo) {
  assert(hptr);
  assert(fifo);
  void* ret = NULL;

  while (1) {
    mpmc_fifo_node_t* head =
        atomic_load_explicit(&fifo->head, memory_order_acquire);
    hazard_pointer_using(hptr, &head->hazard, 0);
    if (head != atomic_load_explicit(&fifo->head, memory_order_acquire)) {
      continue;  // head switched while we were 'using' it
    }

    mpmc_fifo_node_t* const prev = head->prev;
    if (!prev) {
      // empty (possibly just temporarily, let the caller decide what to do)
      hazard_pointer_done_using(hptr, 0);
      return NULL;
    }

    hazard_pointer_using(hptr, &prev->hazard, 1);
    if (head != atomic_load_explicit(&fifo->head, memory_order_acquire)) {
      continue;  // head switched while we were 'using' head->prev
    }

    // push thread has successfully updated prev
    ret = prev->value;
    if (atomic_compare_exchange_weak_explicit(&fifo->head, &head, prev,
                                              memory_order_release,
                                              memory_order_relaxed)) {
      hazard_pointer_done_using(hptr, 0);
      hazard_pointer_done_using(hptr, 1);
      hazard_pointer_free(hptr, &head->hazard);
      break;
    }
  }
  return ret;
}

// TODO: size() (?) O(n), not good for much except testing
// TODO: try_push()
// TODO: fix_list() (?) allows a pop()er to help push()er threads along by
// possibly updating nodes' prev field
// TODO: peek() (?) careful, need to hold a hazard pointer the whole time (add
// done_peek()?)

#endif
