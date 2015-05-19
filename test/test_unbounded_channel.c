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

#include "fiber_channel.h"
#include "fiber_manager.h"
#include "test_helper.h"

fiber_unbounded_channel_t channel;
int PER_FIBER_COUNT = 100000;
int NUM_FIBERS = 100;
#define NUM_THREADS 4

int* results = NULL;

void* send_function(void* param)
{
    intptr_t i;
    for(i = 0; i < PER_FIBER_COUNT; ++i) {
        fiber_unbounded_channel_message_t* node = malloc(sizeof(*node));
        test_assert(node);
        node->data = (void*)i;
        fiber_unbounded_channel_send(&channel, node);
    }
    return NULL;
}

int main(int argc, char* argv[])
{
    fiber_manager_init(NUM_THREADS);

    if(argc > 1) {
        NUM_FIBERS = atoi(argv[1]);
    }
    if(argc > 2) {
        PER_FIBER_COUNT = atoi(argv[2]);
    }

    results = calloc(PER_FIBER_COUNT, sizeof(*results));

    fiber_signal_t signal;
    fiber_signal_init(&signal);
    //specifying an argument will make the channels spin
    fiber_unbounded_channel_init(&channel, argc > 1 ? NULL : &signal);

    fiber_t* send_fibers[NUM_FIBERS];
    int i;
    for(i = 0; i < NUM_FIBERS; ++i) {
        send_fibers[i] = fiber_create(20000, &send_function, NULL);
    }

    for(i = 0; i < NUM_FIBERS * PER_FIBER_COUNT; ++i) {
        fiber_unbounded_channel_message_t* node = fiber_unbounded_channel_receive(&channel);
        results[(intptr_t)node->data] += 1;
        free(node);
    }

    for(i = 0; i < NUM_FIBERS; ++i) {
        fiber_join(send_fibers[i], NULL);
    }

    for(i = 0; i < PER_FIBER_COUNT; ++i) {
        test_assert(results[i] == NUM_FIBERS);
    }
    fiber_unbounded_channel_destroy(&channel);
    fiber_signal_destroy(&signal);

    fiber_manager_print_stats();
    return 0;
}

