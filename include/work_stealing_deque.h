#ifndef _WORK_STEALING_DEQUE_H
#define _WORK_STEALING_DEQUE_H
/*
    Author: Brian Watling
    Email: brianwatling@hotmail.com
    Website: https://github.com/brianwatling

    Description: A work stealing deque based on the paper "Dynamic Circular
                 Work-Stealing Deque" by David Chase and Yossi Lev
*/

#include <stddef.h>
#include <stdint.h>
#include <assert.h>

#ifndef WSD_CACHE_SIZE
#define WSD_CACHE_SIZE (64)
#endif

#define WSD_ALIGNED __attribute__ ((aligned (WSD_CACHE_SIZE)))

#ifdef WSD_ALL_ALIGNED
#define WSD_CIRCULAR_ARRAY_ELEM_ALIGN __attribute__ ((aligned (WSD_CACHE_SIZE)))
#else
#define WSD_CIRCULAR_ARRAY_ELEM_ALIGN
#endif

typedef struct wsd_circular_array_elem
{
    void* data;
} WSD_CIRCULAR_ARRAY_ELEM_ALIGN wsd_circular_array_elem_t;

typedef struct wsd_circular_array
{
    size_t log_size;
    size_t size;
    size_t size_minus_one;/* if we limit size to a power of 2,
                             i & size_minus_one can be used to index
                             instead of i % size */
    wsd_circular_array_elem_t data[];
} wsd_circular_array_t;

#define WSD_EMPTY ((void*)-1)
#define WSD_ABORT ((void*)-2)

typedef struct wsd_work_stealing_deque
{
    WSD_ALIGNED volatile uint64_t top;
    WSD_ALIGNED volatile uint64_t bottom;
    WSD_ALIGNED wsd_circular_array_t* volatile underlying_array;
} wsd_work_stealing_deque_t;

#ifdef __cplusplus
extern "C" {
#endif

/* the internal array will have size: 2^log_size */
extern wsd_circular_array_t* wsd_circular_array_create(size_t log_size);

extern void wsd_circular_array_destroy(wsd_circular_array_t* a);

static inline size_t wsd_circular_array_size(wsd_circular_array_t* a)
{
    assert(a);
    return a->size;
}

static inline void* wsd_circular_array_get(wsd_circular_array_t* a, uint64_t i)
{
    assert(a);
    assert(a->data);
    return a->data[i & a->size_minus_one].data;
}

static inline void wsd_circular_array_put(wsd_circular_array_t* a, uint64_t i, void* p)
{
    assert(a);
    assert(a->data);
    a->data[i & a->size_minus_one].data = p;
}

extern wsd_circular_array_t* wsd_circular_array_grow(wsd_circular_array_t* a, uint64_t start, uint64_t end);

extern wsd_work_stealing_deque_t* wsd_work_stealing_deque_create();

extern void wsd_work_stealing_deque_destroy(wsd_work_stealing_deque_t* d);

static inline size_t wsd_work_stealing_deque_size(wsd_work_stealing_deque_t* d)
{
    assert(d);
    const int64_t size = d->bottom - d->top;
    if(size < 0) {
        return 0;
    }
    return size;
}

extern void wsd_work_stealing_deque_push_bottom(wsd_work_stealing_deque_t* d, void* p);

extern void* wsd_work_stealing_deque_pop_bottom(wsd_work_stealing_deque_t* d);

extern void* wsd_work_stealing_deque_steal(wsd_work_stealing_deque_t* d);

#ifdef __cplusplus
}
#endif

#endif

