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

#include "fiber_barrier.h"
#include "fiber_manager.h"

int fiber_barrier_init(fiber_barrier_t* barrier, uint32_t count)
{
    assert(barrier);
    assert(count > 0);
    barrier->count = count;
    barrier->counter = 0;
    if(!mpsc_fifo_init(&barrier->waiters)) {
        return FIBER_ERROR;
    }
    write_barrier();
    return FIBER_SUCCESS;
}

void fiber_barrier_destroy(fiber_barrier_t* barrier)
{
    assert(barrier);
    mpsc_fifo_destroy(&barrier->waiters);
}

int fiber_barrier_wait(fiber_barrier_t* barrier)
{
    assert(barrier);

    uint64_t const new_value = __sync_add_and_fetch(&barrier->counter, 1);
    if(new_value % barrier->count == 0) {
        fiber_manager_wake_from_mpsc_queue(fiber_manager_get(), &barrier->waiters, barrier->count - 1);
        return FIBER_BARRIER_SERIAL_FIBER;
    } else {
        fiber_manager_wait_in_mpsc_queue(fiber_manager_get(), &barrier->waiters);
        return 0;
    }
}

