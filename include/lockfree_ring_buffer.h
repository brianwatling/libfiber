#ifndef _LOCK_FREE_RING_BUFFER_H_
#define _LOCK_FREE_RING_BUFFER_H_

/*
    Author: Brian Watling
    Email: brianwatling@hotmail.com
    Website: https://github.com/brianwatling
*/

#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <malloc.h>

#include "machine_specific.h"

typedef struct lockfree_ring_buffer
{
    //high and low are generally used together; no point putting them on separate cache lines
    volatile uint64_t high;
    char _cache_padding1[CACHE_SIZE - sizeof(uint64_t)];
    volatile uint64_t low;
    char _cache_padding2[CACHE_SIZE - sizeof(uint64_t)];
    size_t size;
    void* buffer[];
} lockfree_ring_buffer_t;

static inline lockfree_ring_buffer_t* lockfree_ring_buffer_create(size_t size)
{
    assert(size);
    const size_t required_size = sizeof(lockfree_ring_buffer_t) + size * sizeof(void*);
    lockfree_ring_buffer_t* const ret = (lockfree_ring_buffer_t*)calloc(1, required_size);
    if(ret) {
        ret->size = size;
    }
    return ret;
}

static inline void lockfree_ring_buffer_destroy(lockfree_ring_buffer_t* rb)
{
    free(rb);
}

static inline size_t lockfree_ring_buffer_size(const lockfree_ring_buffer_t* rb)
{
    assert(rb);
    const uint64_t high = rb->high;
    load_load_barrier();//read high first; make it look less than or equal to its actual size
    const int64_t size = high - rb->low;
    return size >= 0 ? size : 0;
}

static inline int lockfree_ring_buffer_trypush(lockfree_ring_buffer_t* rb, void* in)
{
    assert(rb);
    assert(in);//can't store NULLs; we rely on a NULL to indicate a spot in the buffer has not been written yet

    const uint64_t low = rb->low;
    load_load_barrier();//read low first; this means the buffer will appear larger or equal to its actual size
    const uint64_t high = rb->high;
    const uint64_t index = high % rb->size;
    if(!rb->buffer[index]
       && high - low < rb->size
       && __sync_bool_compare_and_swap(&rb->high, high, high + 1)) {
        rb->buffer[index] = in;
        return 1;
    }
    return 0;
}

static inline void lockfree_ring_buffer_push(lockfree_ring_buffer_t* rb, void* in)
{
    while(!lockfree_ring_buffer_trypush(rb, in)) {
        if(rb->high - rb->low >= rb->size) {
            cpu_relax();//the buffer is full
        }
    };
}

static inline void* lockfree_ring_buffer_trypop(lockfree_ring_buffer_t* rb)
{
    assert(rb);
    const uint64_t high = rb->high;
    load_load_barrier();//read high first; this means the buffer will appear smaller or equal to its actual size
    const uint64_t low = rb->low;
    const uint64_t index = low % rb->size;
    void* const ret = rb->buffer[index];
    if(ret
       && high > low
       && __sync_bool_compare_and_swap(&rb->low, low, low + 1)) {
        rb->buffer[index] = 0;
        return ret;
    }
    return NULL;
}

static inline void* lockfree_ring_buffer_pop(lockfree_ring_buffer_t* rb)
{
    void* ret;
    while(!(ret = lockfree_ring_buffer_trypop(rb))) {
        if(rb->high <= rb->low) {
            cpu_relax();//the buffer is empty
        }
    }
    return ret;
}

#endif

