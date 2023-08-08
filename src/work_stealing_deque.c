// SPDX-FileCopyrightText: 2012-2023 Brian Watling <brian@oxbo.dev>
// SPDX-License-Identifier: MIT

#include "work_stealing_deque.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

wsd_circular_array_t* wsd_circular_array_create(size_t log_size) {
  const size_t data_size = 1 << log_size;
  wsd_circular_array_t* a =
      malloc(sizeof(wsd_circular_array_t) +
             data_size * sizeof(wsd_circular_array_elem_t));
  if (!a) {
    return NULL;
  }

  a->log_size = log_size;
  a->size = data_size;
  a->size_minus_one = a->size - 1;
  a->prev = NULL;
  return a;
}

void wsd_circular_array_destroy(wsd_circular_array_t* a) {
  if (a->prev) {
    wsd_circular_array_destroy(a->prev);
  }
  free(a);
}

wsd_circular_array_t* wsd_circular_array_grow(wsd_circular_array_t* a,
                                              int64_t start, int64_t end) {
  assert(a);
  assert(start <= end);
  wsd_circular_array_t* new_a = wsd_circular_array_create(a->log_size + 1);
  if (!new_a) {
    return NULL;
  }

  int64_t i;
  for (i = start; i < end; ++i) {
    wsd_circular_array_put(new_a, i, wsd_circular_array_get(a, i));
  }
  new_a->prev = a;
  return new_a;
}

wsd_work_stealing_deque_t* wsd_work_stealing_deque_create() {
  wsd_work_stealing_deque_t* d = malloc(sizeof(wsd_work_stealing_deque_t));
  if (!d) {
    return NULL;
  }

  d->top = 0;
  d->bottom = 0;
  d->underlying_array = wsd_circular_array_create(8);
  if (!d->underlying_array) {
    free(d);
    return NULL;
  }
  return d;
}

void wsd_work_stealing_deque_destroy(wsd_work_stealing_deque_t* d) {
  if (d) {
    wsd_circular_array_destroy(d->underlying_array);
    free(d);
  }
}

void wsd_work_stealing_deque_push_bottom(wsd_work_stealing_deque_t* d,
                                         void* p) {
  assert(d);
  const int64_t b = atomic_load_explicit(&d->bottom, memory_order_acquire);
  const int64_t t = atomic_load_explicit(&d->top, memory_order_acquire);
  wsd_circular_array_t* a = d->underlying_array;
  const int64_t size = b - t;
  if (size >= a->size_minus_one) {
    /* top is actually < bottom. the circular array API expects start < end */
    assert(t <= b);
    a = wsd_circular_array_grow(a, t, b);
    /* NOTE: d->underlying_array is lost. memory leak. */
    d->underlying_array = a;
  }
  wsd_circular_array_put(a, b, p);
  atomic_store_explicit(&d->bottom, b + 1, memory_order_release);
}

void* wsd_work_stealing_deque_pop_bottom(wsd_work_stealing_deque_t* d) {
  assert(d);
  const int64_t b = atomic_load_explicit(&d->bottom, memory_order_acquire) - 1;
  wsd_circular_array_t* const a = d->underlying_array;
  atomic_store_explicit(&d->bottom, b, memory_order_seq_cst);

  int64_t t = atomic_load_explicit(&d->top, memory_order_seq_cst);
  const int64_t size = b - t;
  if (size < 0) {
    atomic_store_explicit(&d->bottom, t, memory_order_release);
    return WSD_EMPTY;
  }
  void* const ret = wsd_circular_array_get(a, b);
  if (size > 0) {
    return ret;
  }
  const int64_t t_plus_one = t + 1;
  if (!atomic_compare_exchange_weak(&d->top, &t, t_plus_one)) {
    atomic_store_explicit(&d->bottom, t_plus_one, memory_order_release);
    return WSD_ABORT;
  }
  atomic_store_explicit(&d->bottom, t_plus_one, memory_order_release);
  return ret;
}

void* wsd_work_stealing_deque_steal(wsd_work_stealing_deque_t* d) {
  assert(d);
  int64_t t = atomic_load_explicit(&d->top, memory_order_acquire);

  const int64_t b = atomic_load_explicit(&d->bottom, memory_order_acquire);
  wsd_circular_array_t* const a = d->underlying_array;
  const int64_t size = b - t;
  if (size <= 0) {
    return WSD_EMPTY;
  }
  void* const ret = wsd_circular_array_get(a, t);
  if (!atomic_compare_exchange_weak(&d->top, &t, t + 1)) {
    return WSD_ABORT;
  }
  return ret;
}
