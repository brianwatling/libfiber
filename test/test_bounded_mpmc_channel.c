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

#include "fiber_multi_channel.h"
#include "fiber_manager.h"
#include "test_helper.h"
#include <time.h>

#define NUM_THREADS 4

int64_t time_diff(const struct timespec* start, const struct timespec* end) {
    return (end->tv_sec * 1000000000LL + end->tv_nsec) - (start->tv_sec * 1000000000LL + start->tv_nsec);
}

void receiver(fiber_multi_channel_t* ch) {
    struct timespec last;
    clock_gettime(CLOCK_MONOTONIC, &last);
    int i;
    for(i = 0; i < 100000000; ++i) {
        intptr_t n = (intptr_t)fiber_multi_channel_receive(ch);
        if(n % 10000000 == 0) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            printf("Received 10000000 in %lf seconds\n", 0.000000001 * time_diff(&last, &now));
            last = now;
        }
    }
}

void sender(fiber_multi_channel_t* ch) {
    intptr_t n = 1;
    int i;
    for(i = 0; i < 100000000; ++i) {
        fiber_multi_channel_send(ch, (void*)n);
        n += 1;
    }
}

int main(int argc, char* argv[]) {
    fiber_manager_init(NUM_THREADS);

    fiber_multi_channel_t* ch1 = fiber_multi_channel_create(10, 0);
    fiber_multi_channel_t* ch2 = fiber_multi_channel_create(10, 0);
    fiber_t* r1 = fiber_create(1024, (fiber_run_function_t)&receiver, (void*)ch1);
    fiber_t* r2 = fiber_create(1024, (fiber_run_function_t)&receiver, (void*)ch2);
    fiber_create(1024, (fiber_run_function_t)&sender, (void*)ch2);
    sender(ch1);

    fiber_join(r1, NULL);
    fiber_join(r2, NULL);

    fiber_manager_print_stats();
    return 0;
}

