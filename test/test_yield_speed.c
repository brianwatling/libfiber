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
#include "fiber_barrier.h"
#include "test_helper.h"
#include <time.h>

#define PER_FIBER_COUNT 10000000
#define NUM_FIBERS 1
#define NUM_THREADS 1

fiber_barrier_t barrier;

void* run_function(void* param)
{
    fiber_barrier_wait(&barrier);
    int i;
    for(i = 0; i < PER_FIBER_COUNT; ++i) {
        fiber_yield();
    }
    return NULL;
}

long long getnsecs(struct timespec* tv)
{
    return (long long)tv->tv_sec * 1000000000LL + tv->tv_nsec;
}

int main()
{
    fiber_manager_init(NUM_THREADS);

    fiber_barrier_init(&barrier, NUM_FIBERS + 1);

    fiber_t* fibers[NUM_FIBERS] = {};
    int i;
    for(i = 0; i < NUM_FIBERS; ++i) {
        fibers[i] = fiber_create(100000, &run_function, NULL);
        if(!fibers[i]) {
            printf("failed to create fiber!\n");
            return 1;
        }
    }

    struct timespec start;
    clock_gettime(CLOCK_REALTIME, &start);

    run_function(NULL);

    for(i = 0; i < NUM_FIBERS; ++i) {
        fiber_join(fibers[i], NULL);
    }

    struct timespec end;
    clock_gettime(CLOCK_REALTIME, &end);

    long long diff = getnsecs(&end) - getnsecs(&start);
    double total_switches = (NUM_FIBERS + 1) * PER_FIBER_COUNT;
    printf("executed %lf context switches in %lld nsec (%lf seconds) = %lf switches per second\n", total_switches, diff, (double)diff / 1000000000.0, total_switches / ((double)diff / 1000000000.0));

    fiber_manager_print_stats();
    return 0;
}
