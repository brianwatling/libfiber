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

#ifndef FIFO_STEAL_BUFFER_H_
#define FIFO_STEAL_BUFFER_H_

/*
    Author: Brian Watling
    Email: brianwatling@hotmail.com
    Website: https://github.com/brianwatling

    Description: A ring buffer. A single master thread can perform a wait-free
        push without any synchronization. The master thread can perform a
        wait-free pop with a single synchronized operation. Other threads can
        perform a lock-free pop using a CAS synchronization primitive.
*/

#include <assert.h>
#include <malloc.h>
#include "machine_specific.h"

typedef struct fifo_steal_buffer
{
    volatile uint64_t high;
    volatile uint64_t low;
    uint32_t size;
    uint32_t power_of_2_mod;
    void* buffer[];
} fifo_steal_buffer_t;

static inline void fifo_steal_buffer_init(fifo_steal_buffer_t* fifo, uint32_t power_of_2_size)
{
    assert(fifo);
    assert((uintptr_t)fifo % sizeof(void*) == 0);
    assert(power_of_2_size && power_of_2_size < 32);
    fifo->high = 0;
    fifo->low = 0;
    fifo->size = 1 << power_of_2_size;
    fifo->power_of_2_mod = fifo->size - 1;
}

static inline fifo_steal_buffer_t* fifo_steal_buffer_create(uint32_t power_of_2_size)
{
    assert(power_of_2_size && power_of_2_size < 32);
    const uint32_t size = 1 << power_of_2_size;
    const uint32_t required_size = sizeof(fifo_steal_buffer_t) + size * sizeof(void*);
    fifo_steal_buffer_t* const ret = (fifo_steal_buffer_t*)malloc(required_size);
    if(ret) {
        fifo_steal_buffer_init(ret, power_of_2_size);
    }
    return ret;
}

static inline void fifo_steal_buffer_destroy(fifo_steal_buffer_t* fifo)
{
    free(fifo);
}

//master thread only!
static inline int fifo_steal_buffer_push(fifo_steal_buffer_t* fifo, void* data)
{
    const uint64_t high = fifo->high;
    const uint64_t low = fifo->low;
    if(high - low < fifo->size) {
        const uint64_t index = high & fifo->power_of_2_mod;
        fifo->buffer[index] = data;
        write_barrier();
        fifo->high = high + 1;
        return 1;
    }
    return 0;
}

//master thread only!
static inline int fifo_steal_buffer_pop(fifo_steal_buffer_t* fifo, void** out)
{
    const uint64_t high = fifo->high;
    load_load_barrier();//read high first; this means the buffer will appear smaller or equal to its actual size
    if(high > fifo->low) {
        const uint64_t new_low = __sync_add_and_fetch(&fifo->low, 1);
        //recheck that no thread stole the last item
        if(new_low >= high) {
            //some other thread stole the data
            fifo->low = high;
            return 0;

        }
        const uint64_t index = (new_low - 1) & fifo->power_of_2_mod;
        *out = fifo->buffer[index];
        return 1;
    }
    return 0;
}

#define FIFO_STEAL_BUFFER_EMPTY (0)
#define FIFO_STEAL_BUFFER_ABORT (-1)
#define FIFO_STEAL_BUFFER_SUCCESS (1)

static inline int fifo_steal_buffer_steal(fifo_steal_buffer_t* fifo, void** out)
{
    const uint64_t high = fifo->high;
    load_load_barrier();//read high first; this means the buffer will appear smaller or equal to its actual size
    const uint64_t low = fifo->low;
    if(high > low) {
        const uint64_t index = low & fifo->power_of_2_mod;
        *out = fifo->buffer[index];
        if(__sync_bool_compare_and_swap(&fifo->low, low, low + 1)) {
            return FIFO_STEAL_BUFFER_SUCCESS;
        }
        return FIFO_STEAL_BUFFER_ABORT;
    }
    return FIFO_STEAL_BUFFER_EMPTY;
}

typedef struct sharded_fifo_steal_buffer
{
    uint32_t num_shards;
    uint32_t memory_per_shard;
    char _padding1[CACHE_SIZE - 2 * sizeof(uint32_t)];
    uint32_t next_shard_push;
    uint32_t next_shard_pop;
    char _padding2[CACHE_SIZE - 2 * sizeof(uint32_t)];
    fifo_steal_buffer_t shards[];
} sharded_fifo_steal_buffer_t;

static inline sharded_fifo_steal_buffer_t* sharded_fifo_steal_buffer_create(uint32_t num_shards, uint32_t shard_power_of_2_size)
{
    const uint32_t shard_size = 1 << shard_power_of_2_size;
    const uint32_t memory_per_shard = sizeof(fifo_steal_buffer_t) + shard_size * sizeof(void*);
    const uint32_t total_memory = sizeof(sharded_fifo_steal_buffer_t) + num_shards * memory_per_shard;
    sharded_fifo_steal_buffer_t* ret = malloc(total_memory);
    if(ret) {
        ret->next_shard_push = 0;
        ret->next_shard_pop = 0;
        ret->num_shards = num_shards;
        ret->memory_per_shard = memory_per_shard;
        uint32_t i;
        for(i = 0; i < num_shards; ++i) {
            fifo_steal_buffer_init(&ret->shards[i], shard_power_of_2_size);
        }
    }
    return ret;
}

static inline void sharded_fifo_steal_buffer_destroy(sharded_fifo_steal_buffer_t* fifo)
{
    free(fifo);
}

static inline uint32_t sharded_fifo_steal_buffer_next_shard(sharded_fifo_steal_buffer_t* fifo, uint32_t* which)
{
    const uint32_t ret = *which;
    const uint32_t new_value = ret + 1;
    if(new_value >= fifo->num_shards) {
        *which = 0;
    } else {
        *which = new_value;
    }
    return ret;
}

static inline fifo_steal_buffer_t* sharded_fifo_steal_buffer_get_shard(sharded_fifo_steal_buffer_t* fifo, uint32_t index)
{
    char* const first_shard = (char*)&fifo->shards[0];
    char* const shard_n = first_shard + fifo->memory_per_shard * index;
    return (fifo_steal_buffer_t*)shard_n;
}

static inline int sharded_fifo_steal_buffer_push(sharded_fifo_steal_buffer_t* fifo, void* data)
{
    const uint32_t num_shards = fifo->num_shards;
    uint32_t i;
    for(i = 0; i < num_shards; ++i) {
        const uint32_t shard = sharded_fifo_steal_buffer_next_shard(fifo, &fifo->next_shard_push);
        fifo_steal_buffer_t* const shard_to_push = sharded_fifo_steal_buffer_get_shard(fifo, shard);
        if(fifo_steal_buffer_push(shard_to_push, data)) {
            return 1;
        }
    }
    return 0;
}

static inline int sharded_fifo_steal_buffer_pop(sharded_fifo_steal_buffer_t* fifo, void** out)
{
    const uint32_t num_shards = fifo->num_shards;
    uint32_t i;
    for(i = 0; i < num_shards; ++i) {
        const uint32_t shard = sharded_fifo_steal_buffer_next_shard(fifo, &fifo->next_shard_pop);
        fifo_steal_buffer_t* const shard_to_pop = sharded_fifo_steal_buffer_get_shard(fifo, shard);
        if(fifo_steal_buffer_pop(shard_to_pop, out)) {
            return 1;
        }
    }
    return 0;
}

#define SHARDED_FIFO_STEAL_BUFFER_EMPTY (0)
#define SHARDED_FIFO_STEAL_BUFFER_ABORT (-1)
#define SHARDED_FIFO_STEAL_BUFFER_SUCCESS (1)

static inline int sharded_fifo_steal_buffer_steal(sharded_fifo_steal_buffer_t* fifo, uint32_t hint, void** out)
{
    const uint32_t num_shards = fifo->num_shards;
    uint32_t i = hint;
    const uint32_t end = hint + num_shards;
    int final_ret = SHARDED_FIFO_STEAL_BUFFER_EMPTY;
    for(; i < end; ++i) {
        fifo_steal_buffer_t* const shard_to_steal = sharded_fifo_steal_buffer_get_shard(fifo, i % num_shards);
        const int ret = fifo_steal_buffer_steal(shard_to_steal, out);
        if(ret == FIFO_STEAL_BUFFER_SUCCESS) {
            return SHARDED_FIFO_STEAL_BUFFER_SUCCESS;
        } else {
            //report aborted if *any* attempt aborts - if we get nothing this means we may want to retry
            final_ret = ret == FIFO_STEAL_BUFFER_ABORT ? SHARDED_FIFO_STEAL_BUFFER_ABORT : final_ret;
        }
    }
    return final_ret;
}

#endif

