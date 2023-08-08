// SPDX-FileCopyrightText: 2012-2023 Brian Watling <brian@oxbo.dev>
// SPDX-License-Identifier: MIT

#ifndef _MPMC_STACK_H_
#define _MPMC_STACK_H_

#include <assert.h>
#include <stddef.h>

#include "machine_specific.h"

typedef struct mpmc_stack_node {
  struct mpmc_stack_node* next;
  void* data;
} mpmc_stack_node_t;

typedef struct mpmc_stack {
  _Atomic(mpmc_stack_node_t*) head;
} mpmc_stack_t;

static inline void mpmc_stack_init(mpmc_stack_t* q) {
  assert(q);
  q->head = NULL;
}

static inline void mpmc_stack_node_init(mpmc_stack_node_t* n, void* data) {
  assert(n);
  n->data = data;
}

static inline void* mpmc_stack_node_get_data(mpmc_stack_node_t* n) {
  assert(n);
  return n->data;
}

static inline void mpmc_stack_push(mpmc_stack_t* q, mpmc_stack_node_t* n) {
  assert(q);
  assert(n);
  mpmc_stack_node_t* head =
      atomic_load_explicit(&q->head, memory_order_acquire);
  do {
    n->next = head;
  } while (!atomic_compare_exchange_weak_explicit(
      &q->head, &head, n, memory_order_release, memory_order_acquire));
}

#define MPMC_RETRY (0)
#define MPMC_SUCCESS (1)

static inline int mpmc_stack_push_timeout(mpmc_stack_t* q, mpmc_stack_node_t* n,
                                          size_t tries) {
  assert(q);
  assert(n);
  mpmc_stack_node_t* head =
      atomic_load_explicit(&q->head, memory_order_acquire);
  do {
    n->next = head;
    if (atomic_compare_exchange_weak_explicit(
            &q->head, &head, n, memory_order_release, memory_order_acquire)) {
      return MPMC_SUCCESS;
    }
    tries -= 1;
  } while (tries > 0);
  return MPMC_RETRY;
}

static inline mpmc_stack_node_t* mpmc_stack_lifo_flush(mpmc_stack_t* q) {
  assert(q);
  return atomic_exchange_explicit(&q->head, NULL, memory_order_acq_rel);
}

static inline mpmc_stack_node_t* mpmc_stack_reverse(mpmc_stack_node_t* head) {
  mpmc_stack_node_t* fifo = NULL;
  while (head) {
    mpmc_stack_node_t* const next = head->next;
    head->next = fifo;
    fifo = head;
    head = next;
  }
  return fifo;
}

static inline mpmc_stack_node_t* mpmc_stack_fifo_flush(mpmc_stack_t* q) {
  return mpmc_stack_reverse(mpmc_stack_lifo_flush(q));
}

#endif
