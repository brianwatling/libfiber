// SPDX-FileCopyrightText: 2012-2023 Brian Watling <brian@oxbo.dev>
// SPDX-License-Identifier: MIT

#ifndef _SPSC_FIFO_H_
#define _SPSC_FIFO_H_

/*
    Description: A single-producer single-consumer FIFO based on "Writing
   Lock-Free Code: A Corrected Queue" by Herb Sutter. The node passed into the
   push method is owned by the FIFO until it is returned to the consumer via the
   pop method. This FIFO is wait-free. NOTE: This SPSC FIFO provides strict FIFO
   ordering

    Properties: 1. Strict FIFO
                2. Wait free
*/

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "machine_specific.h"

typedef struct spsc_node {
  void* data;
  _Atomic(struct spsc_node*) next;
} spsc_node_t;

typedef struct spsc_fifo {
  _Atomic(spsc_node_t*) head;  // consumer read items from head
  char _cache_padding1[FIBER_CACHELINE_SIZE - sizeof(spsc_node_t*)];
  _Atomic(spsc_node_t*) tail;  // producer pushes onto the tail
} spsc_fifo_t;

static inline int spsc_fifo_init(spsc_fifo_t* f) {
  assert(f);
  f->tail = (spsc_node_t*)calloc(1, sizeof(*f->tail));
  f->head = f->tail;
  if (!f->tail) {
    return 0;
  }
  return 1;
}

static inline void spsc_fifo_destroy(spsc_fifo_t* f) {
  if (f) {
    while (f->head != NULL) {
      spsc_node_t* const tmp = f->head;
      f->head = tmp->next;
      free(tmp);
    }
  }
}

// the FIFO owns new_node after pushing
static inline void spsc_fifo_push(spsc_fifo_t* f, spsc_node_t* new_node) {
  assert(f);
  assert(new_node);
  // the node must be terminated before it's visible to the
  // reader as the new tail
  atomic_store_explicit(&new_node->next, NULL, memory_order_release);

  spsc_node_t* const prev_tail =
      atomic_load_explicit(&f->tail, memory_order_acquire);
  atomic_store_explicit(&f->tail, new_node, memory_order_release);
  atomic_store_explicit(&prev_tail->next, new_node, memory_order_release);
}

// the caller owns the node after popping
static inline spsc_node_t* spsc_fifo_trypop(spsc_fifo_t* f) {
  assert(f);
  spsc_node_t* const prev_head =
      atomic_load_explicit(&f->head, memory_order_acquire);
  spsc_node_t* const prev_head_next =
      atomic_load_explicit(&prev_head->next, memory_order_acquire);
  if (prev_head_next) {
    atomic_store_explicit(&f->head, prev_head_next, memory_order_release);
    prev_head->data = prev_head_next->data;
    return prev_head;
  }
  return NULL;
}

#endif
