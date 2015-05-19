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

#include "work_stealing_deque.h"
#include <stdlib.h>
#include <errno.h>
#include <string.h>

wsd_circular_array_t* wsd_circular_array_create(size_t log_size)
{
    const size_t data_size = 1 << log_size;
    wsd_circular_array_t* a = malloc(sizeof(wsd_circular_array_t) + data_size * sizeof(wsd_circular_array_elem_t));
    if(!a) {
        return NULL;
    }

    a->log_size = log_size;
    a->size = data_size;
    a->size_minus_one = a->size - 1;
    return a;
}

void wsd_circular_array_destroy(wsd_circular_array_t* a)
{
    free(a);
}

wsd_circular_array_t* wsd_circular_array_grow(wsd_circular_array_t* a, int64_t start, int64_t end)
{
    assert(a);
    assert(start <= end);
    wsd_circular_array_t* new_a = wsd_circular_array_create(a->log_size + 1);
    if(!new_a) {
        return NULL;
    }

    int64_t i;
    for(i = start; i < end; ++i) {
        wsd_circular_array_put(new_a, i, wsd_circular_array_get(a, i));
    }
    return new_a;
}

wsd_work_stealing_deque_t* wsd_work_stealing_deque_create()
{
    wsd_work_stealing_deque_t* d = malloc(sizeof(wsd_work_stealing_deque_t));
    if(!d) {
        return NULL;
    }

    d->top = 0;
    d->bottom = 0;
    d->underlying_array = wsd_circular_array_create(8);
    if(!d->underlying_array) {
        free(d);
        return NULL;
    }
    write_barrier();
    return d;
}

void wsd_work_stealing_deque_destroy(wsd_work_stealing_deque_t* d)
{
    if(d) {
        wsd_circular_array_destroy(d->underlying_array);
        free(d);
    }
}

void wsd_work_stealing_deque_push_bottom(wsd_work_stealing_deque_t* d, void* p)
{
    assert(d);
    const int64_t b = d->bottom;
    const int64_t t = d->top;
    wsd_circular_array_t* a = d->underlying_array;
    const int64_t size = b - t;
    if(size >= a->size_minus_one) {
        /* top is actually < bottom. the circular array API expects start < end */
        a = wsd_circular_array_grow(a, t, b);
        /* NOTE: d->underlying_array is lost. memory leak. */
        d->underlying_array = a;
    }
    wsd_circular_array_put(a, b, p);
    write_barrier();
    d->bottom = b + 1;
}

void* wsd_work_stealing_deque_pop_bottom(wsd_work_stealing_deque_t* d)
{
    assert(d);
    const int64_t b = d->bottom - 1;
    wsd_circular_array_t* const a = d->underlying_array;
    d->bottom = b;
    store_load_barrier();
    const int64_t t = d->top;
    const int64_t size = b - t;
    if(size < 0) {
        d->bottom = t;
        return WSD_EMPTY;
    }
    void* const ret = wsd_circular_array_get(a, b);
    if(size > 0) {
        return ret;
    }
    const int64_t t_plus_one = t + 1;
    if(!__sync_bool_compare_and_swap(&d->top, t, t_plus_one)) {
        d->bottom = t_plus_one;
        return WSD_ABORT;
    }
    d->bottom = t_plus_one;
    return ret;
}

void* wsd_work_stealing_deque_steal(wsd_work_stealing_deque_t* d)
{
    assert(d);
    const int64_t t = d->top;
    load_load_barrier();
    const int64_t b = d->bottom;
    wsd_circular_array_t* const a = d->underlying_array;
    const int64_t size = b - t;
    if(size <= 0) {
        return WSD_EMPTY;
    }
    void* const ret = wsd_circular_array_get(a, t);
    if(!__sync_bool_compare_and_swap(&d->top, t, t + 1)) {
        return WSD_ABORT;
    }
    return ret;
}

