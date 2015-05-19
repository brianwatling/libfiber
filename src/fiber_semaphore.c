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

#include "fiber_semaphore.h"
#include "fiber_manager.h"

int fiber_semaphore_init(fiber_semaphore_t* semaphore, int value)
{
    assert(semaphore);
    semaphore->counter = value;
    mpmc_fifo_node_t* const initial_node = fiber_manager_get_mpmc_node();
    if(!mpmc_fifo_init(&semaphore->waiters, initial_node)) {
        fiber_manager_return_mpmc_node(initial_node);
        return FIBER_ERROR;
    }
    write_barrier();
    return FIBER_SUCCESS;
}

int fiber_semaphore_destroy(fiber_semaphore_t* semaphore)
{
    assert(semaphore);
    semaphore->counter = 0;
    mpmc_fifo_destroy(fiber_manager_get_hazard_record(fiber_manager_get()), &semaphore->waiters);
    return FIBER_SUCCESS;
}

int fiber_semaphore_wait(fiber_semaphore_t* semaphore)
{
    assert(semaphore);

    const int val = __sync_sub_and_fetch(&semaphore->counter, 1);
    if(val >= 0) {
        //we just got in, there was no contention
        return FIBER_SUCCESS;
    }

    //we didn't get in, we'll wait
    fiber_manager_wait_in_mpmc_queue(fiber_manager_get(), &semaphore->waiters);

    return FIBER_SUCCESS;
}

int fiber_semaphore_trywait(fiber_semaphore_t* semaphore)
{
    assert(semaphore);

    int counter;
    while((counter = semaphore->counter) > 0) {
        if(__sync_bool_compare_and_swap(&semaphore->counter, counter, counter - 1)) {
            return FIBER_SUCCESS;
        }
    }
    return FIBER_ERROR;
}

//returns 1 if another fiber was woken after releasing the semaphore, 0 otherwise
int fiber_semaphore_post_internal(fiber_semaphore_t* semaphore)
{
    assert(semaphore);

    //assumption: the atomic operations below provide read/write ordering (ie. read and writes performed before posting actually occur before posting)

    int prev_counter;
    do {
        while((prev_counter = semaphore->counter) < 0) {
            //another fiber is waiting; attempt to schedule it to take this fiber's place
            if(fiber_manager_wake_from_mpmc_queue(fiber_manager_get(), &semaphore->waiters, 0)) {
                __sync_add_and_fetch(&semaphore->counter, 1);
                return 1;
            }
        }
    } while(!__sync_bool_compare_and_swap(&semaphore->counter, prev_counter, prev_counter + 1));

    return 0;
}

int fiber_semaphore_post(fiber_semaphore_t* semaphore)
{
    const int had_waiters = fiber_semaphore_post_internal(semaphore);
    if(had_waiters) {
        //the semaphore was contended - be nice and let the waiter run
        fiber_yield();
    }
    return FIBER_SUCCESS;
}

int fiber_semaphore_getvalue(fiber_semaphore_t* semaphore)
{
    assert(semaphore);
    return semaphore->counter;
}

