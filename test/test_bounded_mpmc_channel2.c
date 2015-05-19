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

int send_count = 100000000;

int64_t time_diff(const struct timespec* start, const struct timespec* end) {
    return (end->tv_sec * 1000000000LL + end->tv_nsec) - (start->tv_sec * 1000000000LL + start->tv_nsec);
}

void receiver(fiber_multi_channel_t* ch) {
    fiber_t* this_fiber = fiber_manager_get()->current_fiber;
    struct timespec last;
    clock_gettime(CLOCK_MONOTONIC, &last);
    int i;
    for(i = 0; i < send_count; ++i) {
        intptr_t n = (intptr_t)fiber_multi_channel_receive(ch);
        if(n % 10000000 == 0) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            printf("%p Received 10000000 in %lf seconds\n", this_fiber, 0.000000001 * time_diff(&last, &now));
            last = now;
        }
    }
}

void sender(fiber_multi_channel_t* ch) {
    intptr_t n = 1;
    int i;
    for(i = 0; i < send_count; ++i) {
        fiber_multi_channel_send(ch, (void*)n);
        n += 1;
    }
}

fiber_multi_channel_t* ch1 = NULL;

int main(int argc, char* argv[]) {
    int num_threads = 4;
    int count = 2;
    if(argc > 1) {
        count = atoi(argv[1]);
    }
    if(argc > 2) {
        send_count = atoi(argv[2]);
    }
    if(argc > 3) {
        num_threads = atoi(argv[3]);
    }
    fiber_manager_init(num_threads);

    ch1 = fiber_multi_channel_create(10, 0);

    fiber_t** fibers = calloc(count, sizeof(fiber_t*));
    int i;
    for(i = 0; i < count; ++i) {
        fibers[i] = fiber_create(1024, (fiber_run_function_t)&receiver, (void*)ch1);
        fiber_create(1024, (fiber_run_function_t)&sender, (void*)ch1);
    }

    for(i = 0; i < count; ++i) {
        fiber_join(fibers[i], NULL);
    }

    fiber_manager_print_stats();
    return 0;
}

