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

#include "fiber_rwlock.h"
#include "fiber_manager.h"

#ifdef __GNUC__
#define STATIC_ASSERT_HELPER(expr, msg) \
    (!!sizeof (struct { unsigned int STATIC_ASSERTION__##msg: (expr) ? 1 : -1; }))
#define STATIC_ASSERT(expr, msg) \
    extern int (*assert_function__(void)) [STATIC_ASSERT_HELPER(expr, msg)]
#else
    #define STATIC_ASSERT(expr, msg)   \
    extern char STATIC_ASSERTION__##msg[1]; \
    extern char STATIC_ASSERTION__##msg[(expr)?1:2]
#endif /* #ifdef __GNUC__ */

STATIC_ASSERT(sizeof(fiber_rwlock_state_t) == sizeof(uint64_t), state_is_not_sized_properly);

int fiber_rwlock_init(fiber_rwlock_t* rwlock)
{
    assert(rwlock);
    if(!mpsc_fifo_init(&rwlock->write_waiters)
       || !mpsc_fifo_init(&rwlock->read_waiters)) {
        mpsc_fifo_destroy(&rwlock->write_waiters);
        mpsc_fifo_destroy(&rwlock->read_waiters);
        return FIBER_ERROR;
    }
    rwlock->state.blob = 0;
    write_barrier();
    return FIBER_SUCCESS;
}

void fiber_rwlock_destroy(fiber_rwlock_t* rwlock)
{
    if(rwlock) {
        mpsc_fifo_destroy(&rwlock->write_waiters);
        mpsc_fifo_destroy(&rwlock->read_waiters);
    }
}

int fiber_rwlock_rdlock(fiber_rwlock_t* rwlock)
{
    assert(rwlock);

    fiber_rwlock_state_t current_state;
    while(1) {
        const uint64_t snapshot = rwlock->state.blob;
        current_state.blob = snapshot;
        if(current_state.state.waiting_writers || current_state.state.write_locked || current_state.state.waiting_readers) {
            current_state.state.waiting_readers += 1;
            if(__sync_bool_compare_and_swap(&rwlock->state.blob, snapshot, current_state.blob)) {
                //currently write locked or a writer is waiting - be friendly and wait
                fiber_manager_wait_in_mpsc_queue(fiber_manager_get(), &rwlock->read_waiters);
                break;
            }
        } else {
            current_state.state.reader_count += 1;
            if(__sync_bool_compare_and_swap(&rwlock->state.blob, snapshot, current_state.blob)) {
                //currently read locked
                break;
            }
        }
    }
    return FIBER_SUCCESS;
}

int fiber_rwlock_wrlock(fiber_rwlock_t* rwlock)
{
    assert(rwlock);

    fiber_rwlock_state_t current_state;
    while(1) {
        const uint64_t snapshot = rwlock->state.blob;
        current_state.blob = snapshot;
        if(current_state.blob != 0) {
            current_state.state.waiting_writers += 1;
            if(__sync_bool_compare_and_swap(&rwlock->state.blob, snapshot, current_state.blob)) {
                //currently locked or a reader is waiting - be friendly and wait
                fiber_manager_wait_in_mpsc_queue(fiber_manager_get(), &rwlock->write_waiters);
                break;
            }
        } else {
            current_state.state.write_locked = 1;
            if(__sync_bool_compare_and_swap(&rwlock->state.blob, snapshot, current_state.blob)) {
                //currently write locked
                break;
            }
        }
    }
    return FIBER_SUCCESS;
}

int fiber_rwlock_tryrdlock(fiber_rwlock_t* rwlock)
{
    assert(rwlock);

    fiber_rwlock_state_t current_state;
    while(1) {
        const uint64_t snapshot = rwlock->state.blob;
        current_state.blob = snapshot;
        if(current_state.state.waiting_writers || current_state.state.write_locked || current_state.state.waiting_readers) {
            return FIBER_ERROR;
        }
        current_state.state.reader_count += 1;
        if(__sync_bool_compare_and_swap(&rwlock->state.blob, snapshot, current_state.blob)) {
            break;
        }
    }
    return FIBER_SUCCESS;
}

int fiber_rwlock_trywrlock(fiber_rwlock_t* rwlock)
{
    assert(rwlock);

    fiber_rwlock_state_t current_state;
    while(1) {
        const uint64_t snapshot = rwlock->state.blob;
        current_state.blob = snapshot;
        if(current_state.blob != 0) {
            return FIBER_ERROR;
        }
        current_state.state.write_locked = 1;
        if(__sync_bool_compare_and_swap(&rwlock->state.blob, snapshot, current_state.blob)) {
            break;
        }
    }
    return FIBER_SUCCESS;
}

int fiber_rwlock_rdunlock(fiber_rwlock_t* rwlock)
{
    assert(rwlock);

    fiber_rwlock_state_t current_state;
    while(1) {
        const uint64_t snapshot = rwlock->state.blob;
        current_state.blob = snapshot;
        assert(current_state.state.reader_count > 0);
        assert(!current_state.state.write_locked);
        current_state.state.reader_count -= 1;
        if(!current_state.state.reader_count) {
            //if we're the last reader then we are responsible to wake up waiters

            if(current_state.state.waiting_writers) {
                //no fiber will acquire the lock while waiting_writers != 0
                current_state.state.write_locked = 1;
                current_state.state.waiting_writers -= 1;
                if(__sync_bool_compare_and_swap(&rwlock->state.blob, snapshot, current_state.blob)) {
                    fiber_manager_wake_from_mpsc_queue(fiber_manager_get(), &rwlock->write_waiters, 1);
                    break;
                }
                continue;
            }
            if(current_state.state.waiting_readers) {
                //no fiber will acquire the lock while waiting_readers != 0
                current_state.state.reader_count = current_state.state.waiting_readers;
                current_state.state.waiting_readers = 0;
                if(__sync_bool_compare_and_swap(&rwlock->state.blob, snapshot, current_state.blob)) {
                    fiber_manager_wake_from_mpsc_queue(fiber_manager_get(), &rwlock->read_waiters, current_state.state.reader_count);
                    break;
                }
                continue;
            }
        }
        if(__sync_bool_compare_and_swap(&rwlock->state.blob, snapshot, current_state.blob)) {
            break;
        }
    }
    return FIBER_SUCCESS;
}

int fiber_rwlock_wrunlock(fiber_rwlock_t* rwlock)
{
    assert(rwlock);

    fiber_rwlock_state_t current_state;
    while(1) {
        const uint64_t snapshot = rwlock->state.blob;
        current_state.blob = snapshot;
        assert(!current_state.state.reader_count);
        assert(current_state.state.write_locked);
        current_state.state.write_locked = 0;
        //we are responsible to wake up waiters

        if(current_state.state.waiting_writers) {
            //no fiber will acquire the lock while write_locked = 1
            current_state.state.write_locked = 1;
            current_state.state.waiting_writers -= 1;
            if(__sync_bool_compare_and_swap(&rwlock->state.blob, snapshot, current_state.blob)) {
                fiber_manager_wake_from_mpsc_queue(fiber_manager_get(), &rwlock->write_waiters, 1);
                break;
            }
            continue;
        }
        if(current_state.state.waiting_readers) {
            //no fiber will acquire the lock while waiting_readers != 0
            current_state.state.reader_count = current_state.state.waiting_readers;
            current_state.state.waiting_readers = 0;
            if(__sync_bool_compare_and_swap(&rwlock->state.blob, snapshot, current_state.blob)) {
                fiber_manager_wake_from_mpsc_queue(fiber_manager_get(), &rwlock->read_waiters, current_state.state.reader_count);
                break;
            }
            continue;
        }
        if(__sync_bool_compare_and_swap(&rwlock->state.blob, snapshot, current_state.blob)) {
            break;
        }
    }
    return FIBER_SUCCESS;
}

