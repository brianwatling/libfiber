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

#include "fiber_manager.h"
#include "test_helper.h"

#define PER_FIBER_COUNT 100
#define NUM_FIBERS 100
#define NUM_THREADS 10
int per_thread_count[NUM_THREADS];
int switch_count = 0;

void* run_function(void* param)
{
    fiber_manager_t* const original_manager = fiber_manager_get();
    int i;
    for(i = 0; i < PER_FIBER_COUNT; ++i) {
        fiber_manager_t* const current_manager = fiber_manager_get();
        if(current_manager != original_manager) {
            __sync_fetch_and_add(&switch_count, 1);
        }
        __sync_fetch_and_add(&per_thread_count[current_manager->id], 1);
        fiber_yield();
    }
    return NULL;
}

int main()
{
    fiber_manager_init(NUM_THREADS);

    printf("starting %d fibers with %d backing threads, running %d yields per fiber\n", NUM_FIBERS, NUM_THREADS, PER_FIBER_COUNT);
    fiber_t* fibers[NUM_FIBERS] = {};
    int i;
    for(i = 0; i < NUM_FIBERS; ++i) {
        fibers[i] = fiber_create(100000, &run_function, NULL);
        if(!fibers[i]) {
            printf("failed to create fiber!\n");
            return 1;
        }
    }

    for(i = 0; i < NUM_FIBERS; ++i) {
        fiber_join(fibers[i], NULL);
    }

    printf("SUCCESS\n");
    for(i = 0; i < NUM_THREADS; ++i) {
        printf("thread %d count: %d\n", i, per_thread_count[i]);
    }
    printf("switch_count: %d\n", switch_count);
    fflush(stdout);

    fiber_manager_print_stats();
    return 0;
}
