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

#include "test_helper.h"
#include "fiber_manager.h"
#include "fiber_barrier.h"
#include "work_queue.h"

int volatile counter = 0;
#define PER_FIBER_COUNT 100000
#define NUM_FIBERS 100
#define NUM_THREADS 4

work_queue_t work_queue;
fiber_barrier_t barrier;

void* run_function(void* param)
{
    work_queue_item_t* node = NULL;
    int i;
    for(i = 0; i < PER_FIBER_COUNT; ++i) {
        work_queue_item_t* next = node;
        node = calloc(1, sizeof(*node));
        test_assert(node);
        node->next = next;
    }
    fiber_barrier_wait(&barrier);

    for(i = 0; i < PER_FIBER_COUNT; ++i) {
        work_queue_item_t* const to_push = node;
        node = node->next;
        const int push_ret = work_queue_push(&work_queue, to_push);
        if(push_ret == WORK_QUEUE_START_WORKING) {
            work_queue_item_t* work;
            while(work_queue_get_work(&work_queue, &work) != WORK_QUEUE_EMPTY) {
                ++counter;
                free(work);
            }
        }
        fiber_yield();
    }
    return NULL;
}

int main()
{
    fiber_manager_init(NUM_THREADS);

    fiber_barrier_init(&barrier, NUM_FIBERS);
    work_queue_init(&work_queue);

    fiber_t* fibers[NUM_FIBERS];
    int i;
    for(i = 0; i < NUM_FIBERS; ++i) {
        fibers[i] = fiber_create(20000, &run_function, NULL);
    }

    for(i = 0; i < NUM_FIBERS; ++i) {
        fiber_join(fibers[i], NULL);
    }

    test_assert(counter == NUM_FIBERS * PER_FIBER_COUNT);
    work_queue_destroy(&work_queue);

    fiber_manager_print_stats();
    return 0;
}

