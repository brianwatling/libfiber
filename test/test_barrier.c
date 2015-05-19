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
#include "test_helper.h"

#define PER_FIBER_COUNT 100000
#define NUM_FIBERS 1000
#define NUM_THREADS 4

int volatile counter[NUM_FIBERS] = {};
int volatile winner = 0;

fiber_barrier_t barrier;

void* run_function(void* param)
{
    intptr_t index = (intptr_t)param;
    int i;
    for(i = 0; i < PER_FIBER_COUNT; ++i) {
        test_assert(counter[index] == i);
        ++counter[index];
    }

    if(FIBER_BARRIER_SERIAL_FIBER == fiber_barrier_wait(&barrier)) {
        test_assert(__sync_bool_compare_and_swap(&winner, 0, 1));
    }

    for(i = 0; i < NUM_FIBERS; ++i) {
        test_assert(counter[i] == PER_FIBER_COUNT);
    }

    return NULL;
}

int main()
{
    fiber_manager_init(NUM_THREADS);

    fiber_barrier_init(&barrier, NUM_FIBERS);

    fiber_t* fibers[NUM_FIBERS];
    intptr_t i;
    for(i = 1; i < NUM_FIBERS; ++i) {
        fibers[i] = fiber_create(20000, &run_function, (void*)i);
    }

    run_function(NULL);

    for(i = 1; i < NUM_FIBERS; ++i) {
        fiber_join(fibers[i], NULL);
    }

    test_assert(winner == 1);

    //do it all again - the barrier should be reusable
    winner = 0;
    memset((void*)counter, 0, sizeof(counter));
    for(i = 1; i < NUM_FIBERS; ++i) {
        fibers[i] = fiber_create(20000, &run_function, (void*)i);
    }

    run_function(NULL);

    for(i = 1; i < NUM_FIBERS; ++i) {
        fiber_join(fibers[i], NULL);
    }


    fiber_barrier_destroy(&barrier);

    fiber_manager_print_stats();
    return 0;
}

