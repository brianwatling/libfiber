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

#ifndef _FIBER_MUTEX_H_
#define _FIBER_MUTEX_H_

/*
    Author: Brian Watling
    Email: brianwatling@hotmail.com
    Website: https://github.com/brianwatling

    Description: A mutex for fibers. A mutex maintains an internal counter.
                 The counter is initially one and is atomically decremented by each fiber
                 attempting to acquire the lock. The fiber that decrements the counter
                 from 1 to 0 owns the lock. Fibers which decrement the counter
                 to a value below 0 must wait. Unlocking is done by atomically incrementing
                 the counter. The unlocker must wake up a waiter if the counter is not 1
                 after an unlock operation (ie. other fibers were waiting).
*/

#include "mpsc_fifo.h"

typedef struct fiber_mutex
{
    volatile int counter;
    mpsc_fifo_t waiters;
} fiber_mutex_t;

#ifdef __cplusplus
extern "C" {
#endif

extern int fiber_mutex_init(fiber_mutex_t* mutex);

extern int fiber_mutex_destroy(fiber_mutex_t* mutex);

extern int fiber_mutex_lock(fiber_mutex_t* mutex);

extern int fiber_mutex_trylock(fiber_mutex_t* mutex);

extern int fiber_mutex_unlock_internal(fiber_mutex_t* mutex);

extern int fiber_mutex_unlock(fiber_mutex_t* mutex);

#ifdef __cplusplus
}
#endif

#endif

