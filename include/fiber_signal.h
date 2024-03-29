// SPDX-FileCopyrightText: 2012-2023 Brian Watling <brian@oxbo.dev>
// SPDX-License-Identifier: MIT

#ifndef _FIBER_SIGNAL_H_
#define _FIBER_SIGNAL_H_

#include <assert.h>
#include <stdint.h>

#include "fiber.h"
#include "machine_specific.h"

// A signal can be waited on by exactly one fiber. Any number of threads can
// raise the signal.
typedef struct fiber_signal {
  _Atomic(fiber_t*) waiter;
} fiber_signal_t;

#define FIBER_SIGNAL_NO_WAITER ((fiber_t*)0)
#define FIBER_SIGNAL_RAISED ((fiber_t*)(intptr_t)-1)
#define FIBER_SIGNAL_READY_TO_WAKE ((fiber_t*)(intptr_t)-1)

static inline void fiber_signal_init(fiber_signal_t* s) {
  assert(s);
  s->waiter = FIBER_SIGNAL_NO_WAITER;
}

static inline void fiber_signal_destroy(fiber_signal_t* s) {
  // empty
}

static inline void fiber_signal_wait(fiber_signal_t* s) {
  assert(s);

  fiber_manager_t* const manager = fiber_manager_get();
  fiber_t* const this_fiber = manager->current_fiber;
  this_fiber->scratch =
      NULL;  // clear scratch before marking this fiber to be signalled
  fiber_t* expected = (fiber_t*)FIBER_SIGNAL_NO_WAITER;
  if (atomic_compare_exchange_strong_explicit(&s->waiter, &expected, this_fiber,
                                              memory_order_release,
                                              memory_order_relaxed)) {
    // the signal is not raised, we're now waiting
    assert(this_fiber->state == FIBER_STATE_RUNNING);
    this_fiber->state = FIBER_STATE_WAITING;
    // the raiser will not wake this fiber until scratch has been set to
    // FIBER_SIGNAL_READY_TO_WAKE, which the fiber manager will set after the
    // context switch
    manager->set_wait_location = (void**)&this_fiber->scratch;
    manager->set_wait_value = FIBER_SIGNAL_READY_TO_WAKE;
    fiber_manager_yield(manager);
    this_fiber->scratch = NULL;
  }
  // the signal has been raised
  s->waiter = FIBER_SIGNAL_NO_WAITER;
}

// returns 1 if a fiber was woken
static inline int fiber_signal_raise(fiber_signal_t* s) {
  assert(s);

  fiber_t* const old = (fiber_t*)atomic_exchange_explicit(
      &s->waiter, FIBER_SIGNAL_RAISED, memory_order_release);
  if (old != FIBER_SIGNAL_NO_WAITER && old != FIBER_SIGNAL_RAISED) {
    // we successfully signalled while a fiber was waiting
    s->waiter = FIBER_SIGNAL_NO_WAITER;
    fiber_manager_t* const manager = fiber_manager_get();
    while (old->scratch != FIBER_SIGNAL_READY_TO_WAKE) {
      cpu_relax();  // the other fiber is still in the process of going to sleep
      manager->signal_spin_count += 1;
    }
    old->state = FIBER_STATE_READY;
    fiber_manager_schedule(manager, old);
    return 1;
  }
  return 0;
}

// A multi-signal allows any number of fibers to wait. Any number of fibers can
// raise the signal.
typedef union fiber_multi_signal {
  struct {
    _Atomic uintptr_t counter;
    _Atomic(mpsc_fifo_node_t*) volatile head;
  } data;
  pointer_pair_t blob;
} __attribute__((__packed__)) __attribute__((__aligned__(2 * sizeof(void*))))
fiber_multi_signal_t;

#define FIBER_MULTI_SIGNAL_RAISED ((mpsc_fifo_node_t*)(intptr_t)-1)

static inline void fiber_multi_signal_init(fiber_multi_signal_t* s) {
  assert(sizeof(*s) == 2 * sizeof(void*));
  s->data.counter = 0;
  s->data.head = NULL;
}

static inline void fiber_multi_signal_destroy(fiber_multi_signal_t* s) {
  assert(!s || !s->data.head || s->data.head == FIBER_MULTI_SIGNAL_RAISED);
}

static inline void fiber_multi_signal_wait(fiber_multi_signal_t* s) {
  assert(s);

  fiber_manager_t* const manager = fiber_manager_get();
  fiber_t* const this_fiber = manager->current_fiber;
  this_fiber->scratch =
      NULL;  // clear scratch before marking this fiber to be signalled
  mpsc_fifo_node_t* const node = this_fiber->mpsc_fifo_node;
  assert(node);
  node->data = this_fiber;

  fiber_multi_signal_t snapshot;
  while (1) {
    // read the counter first - this ensures nothing
    // changes while we're working
    snapshot.data.counter =
        atomic_load_explicit(&s->data.counter, memory_order_acquire);

    snapshot.data.head =
        atomic_load_explicit(&s->data.head, memory_order_acquire);

    if (snapshot.data.head == FIBER_MULTI_SIGNAL_RAISED) {
      // try to switch from raised to no waiter -> on success we wake up since
      // we accepted the signal
      fiber_multi_signal_t new_value;
      new_value.data.counter = snapshot.data.counter + 1;
      new_value.data.head = NULL;
      if (compare_and_swap2(&s->blob, &snapshot.blob, &new_value.blob)) {
        break;
      }
    } else {
      // 0 or more waiters.
      // try to push self into the waiter list -> on success we sleep
      node->next = snapshot.data.head;
      fiber_multi_signal_t new_value;
      new_value.data.counter = snapshot.data.counter + 1;
      new_value.data.head = node;
      if (compare_and_swap2(&s->blob, &snapshot.blob, &new_value.blob)) {
        assert(this_fiber->state == FIBER_STATE_RUNNING);
        this_fiber->state = FIBER_STATE_WAITING;
        // the raiser will not wake this fiber until scratch has been set to
        // FIBER_SIGNAL_READY_TO_WAKE, which the fiber manager will set after
        // the context switch
        manager->set_wait_location = (void**)&this_fiber->scratch;
        manager->set_wait_value = FIBER_SIGNAL_READY_TO_WAKE;
        fiber_manager_yield(manager);
        this_fiber->scratch = NULL;
        break;
      }
    }
    cpu_relax();
  }
}

// potentially wakes a fiber. if the signal is already raised, the signal will
// be left in the raised state without waking a fiber. returns 1 if a fiber was
// woken
static inline int fiber_multi_signal_raise(fiber_multi_signal_t* s) {
  assert(s);

  fiber_multi_signal_t snapshot;
  while (1) {
    // read the counter first - this ensures nothing
    // changes while we're working
    snapshot.data.counter =
        atomic_load_explicit(&s->data.counter, memory_order_acquire);

    snapshot.data.head =
        atomic_load_explicit(&s->data.head, memory_order_acquire);

    if (!snapshot.data.head ||
        snapshot.data.head == FIBER_MULTI_SIGNAL_RAISED) {
      // raise the signal. changing from 'raised' to 'raised' is required to
      // ensure no wake ups are missed
      fiber_multi_signal_t new_value;
      new_value.data.counter = snapshot.data.counter + 1;
      new_value.data.head = FIBER_MULTI_SIGNAL_RAISED;
      if (compare_and_swap2(&s->blob, &snapshot.blob, &new_value.blob)) {
        break;
      }
    } else {
      // there's a waiter -> try to wake him
      fiber_multi_signal_t new_value;
      new_value.data.counter = snapshot.data.counter + 1;
      // TODO(bwatling): reading head->next here is unsafe because head could
      // technically be consumed and freed in parallel. This is one of the
      // reasons why keeping a free-list of fibers was nice - no fibers would
      // ever be freed and hence this type of problem could be ignored.
      new_value.data.head = snapshot.data.head->next;
      if (compare_and_swap2(&s->blob, &snapshot.blob, &new_value.blob)) {
        // we successfully signalled a waiting fiber
        fiber_t* to_wake = (fiber_t*)snapshot.data.head->data;
        to_wake->mpsc_fifo_node = snapshot.data.head;
        fiber_manager_t* const manager = fiber_manager_get();
        while (to_wake->scratch != FIBER_SIGNAL_READY_TO_WAKE) {
          cpu_relax();  // the other fiber is still in the process of going to
                        // sleep
          manager->multi_signal_spin_count += 1;
        }
        to_wake->state = FIBER_STATE_READY;
        fiber_manager_schedule(manager, to_wake);
        return 1;
      }
    }
    cpu_relax();
  }
  return 0;
}

// wakes exactly one fiber
static inline void fiber_multi_signal_raise_strict(fiber_multi_signal_t* s) {
  assert(s);

  fiber_multi_signal_t snapshot;
  while (1) {
    // read the counter first - this ensures nothing
    // changes while we're working
    snapshot.data.counter =
        atomic_load_explicit(&s->data.counter, memory_order_acquire);

    snapshot.data.head =
        atomic_load_explicit(&s->data.head, memory_order_acquire);

    if (snapshot.data.head && snapshot.data.head != FIBER_MULTI_SIGNAL_RAISED) {
      // there's a waiter -> try to wake him
      fiber_multi_signal_t new_value;
      new_value.data.counter = snapshot.data.counter + 1;
      new_value.data.head = snapshot.data.head->next;
      if (compare_and_swap2(&s->blob, &snapshot.blob, &new_value.blob)) {
        // we successfully signalled a waiting fiber
        fiber_t* to_wake = (fiber_t*)snapshot.data.head->data;
        to_wake->mpsc_fifo_node = snapshot.data.head;
        fiber_manager_t* const manager = fiber_manager_get();
        while (to_wake->scratch != FIBER_SIGNAL_READY_TO_WAKE) {
          cpu_relax();  // the other fiber is still in the process of going to
                        // sleep
          manager->multi_signal_spin_count += 1;
        }
        to_wake->state = FIBER_STATE_READY;
        fiber_manager_schedule(manager, to_wake);
        return;
      }
    }
    cpu_relax();
  }
}

#endif
