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

#include "fiber_mutex.h"
#include "fiber_manager.h"

int fiber_mutex_init(fiber_mutex_t* mutex)
{
    assert(mutex);
    mutex->counter = 1;
    if(!mpsc_fifo_init(&mutex->waiters)) {
        return FIBER_ERROR;
    }
    write_barrier();
    return FIBER_SUCCESS;
}

int fiber_mutex_destroy(fiber_mutex_t* mutex)
{
    assert(mutex);
    mutex->counter = 1;
    mpsc_fifo_destroy(&mutex->waiters);
    return FIBER_SUCCESS;
}

int fiber_mutex_lock(fiber_mutex_t* mutex)
{
    assert(mutex);

    const int val = __sync_sub_and_fetch(&mutex->counter, 1);
    if(val == 0) {
        //we just got the lock, there was no contention
        return FIBER_SUCCESS;
    }

    //we failed to acquire the lock (there's contention). we'll wait.
    fiber_manager_t* const manager = fiber_manager_get();
    manager->lock_contention_count += 1;
    fiber_manager_wait_in_mpsc_queue(manager, &mutex->waiters);

    return FIBER_SUCCESS;
}

int fiber_mutex_trylock(fiber_mutex_t* mutex)
{
    assert(mutex);

    if(__sync_bool_compare_and_swap(&mutex->counter, 1, 0)) {
        //we just got the lock, there was no contention
        return FIBER_SUCCESS;
    }
    return FIBER_ERROR;
}

int fiber_mutex_unlock_internal(fiber_mutex_t* mutex)
{
    assert(mutex);

    //assumption: the atomic operation below provides read/write ordering (ie. read and writes performed before unlocking actually occur before unlocking)

    //unlock and wake a waiting fiber if there is one
    const int new_val = __sync_add_and_fetch(&mutex->counter, 1);
    if (new_val != 1) {
        fiber_manager_wake_from_mpsc_queue(fiber_manager_get(), &mutex->waiters, 1);
        return 1;
    }
    
    return 0;
}

int fiber_mutex_unlock(fiber_mutex_t* mutex)
{
    const int contended = fiber_mutex_unlock_internal(mutex);
    if(contended) {
        //the lock was contended - be nice and let the waiter run
        fiber_yield();
    }

    return FIBER_SUCCESS;
}

