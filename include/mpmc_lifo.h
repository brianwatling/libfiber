// SPDX-FileCopyrightText: 2012-2023 Brian Watling <brian@oxbo.dev>
// SPDX-License-Identifier: MIT

#ifndef _MPMC_LIFO_H_
#define _MPMC_LIFO_H_

#include <assert.h>

#include "machine_specific.h"
#include "mpsc_fifo.h"

typedef mpsc_fifo_node_t mpmc_lifo_node_t;

typedef union {
  struct {
    _Atomic uintptr_t counter;
    _Atomic(mpmc_lifo_node_t*) volatile head;
  } data;
  pointer_pair_t blob;
} __attribute__((__packed__)) __attribute__((__aligned__(2 * sizeof(void*))))
mpmc_lifo_t;

#define MPMC_LIFO_INITIALIZER \
  {}

static inline void mpmc_lifo_init(mpmc_lifo_t* lifo) {
  assert(lifo);
  assert(sizeof(*lifo) == sizeof(pointer_pair_t));
  lifo->data.head = NULL;
  lifo->data.counter = 0;
}

static inline void mpmc_lifo_destroy(mpmc_lifo_t* lifo) {
  if (lifo) {
    while (lifo->data.head) {
      mpmc_lifo_node_t* const old = lifo->data.head;
      lifo->data.head = lifo->data.head->next;
      free(old);
    }
  }
}

static inline void mpmc_lifo_push(mpmc_lifo_t* lifo, mpmc_lifo_node_t* node) {
  assert(lifo);
  assert(node);
  mpmc_lifo_t snapshot;
  while (1) {
    // read the counter first - this ensures nothing
    // changes while we're trying to push
    snapshot.data.counter =
        atomic_load_explicit(&lifo->data.counter, memory_order_acquire);

    snapshot.data.head =
        atomic_load_explicit(&lifo->data.head, memory_order_acquire);
    node->next = snapshot.data.head;
    mpmc_lifo_t temp;
    temp.data.head = node;
    temp.data.counter = snapshot.data.counter + 1;
    if (compare_and_swap2(&lifo->blob, &snapshot.blob, &temp.blob)) {
      return;
    }
  }
}

static inline mpmc_lifo_node_t* mpmc_lifo_pop(mpmc_lifo_t* lifo) {
  assert(lifo);
  mpmc_lifo_t snapshot;
  while (1) {
    // read the counter first - this ensures nothing
    // changes while we're trying to pop
    snapshot.data.counter =
        atomic_load_explicit(&lifo->data.counter, memory_order_acquire);

    snapshot.data.head =
        atomic_load_explicit(&lifo->data.head, memory_order_acquire);
    ;
    if (!snapshot.data.head) {
      return NULL;
    }
    mpmc_lifo_t temp;
    temp.data.head = snapshot.data.head->next;
    temp.data.counter = snapshot.data.counter + 1;
    if (compare_and_swap2(&lifo->blob, &snapshot.blob, &temp.blob)) {
      return snapshot.data.head;
    }
  }
}

#endif
