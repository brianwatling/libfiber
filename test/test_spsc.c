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

#include <spsc_fifo.h>
#include "test_helper.h"
#include <pthread.h>
#include <stdint.h>
#include <unistd.h>

#define PUSH_COUNT 10000000

pthread_barrier_t barrier;
spsc_fifo_t fifo;

void* pop_func(void* p)
{
    pthread_barrier_wait(&barrier);
    intptr_t i;
    spsc_node_t* node = NULL;
    for(i = 0; i < PUSH_COUNT; ++i) {
        while(!(node = spsc_fifo_trypop(&fifo))) {};
        test_assert((intptr_t)node->data == i);
        free(node);
    }
    return NULL;
}

int main()
{
    pthread_barrier_init(&barrier, NULL, 2);
    test_assert(spsc_fifo_init(&fifo));

    pthread_t consumer;
    pthread_create(&consumer, NULL, &pop_func, NULL);

    pthread_barrier_wait(&barrier);

    intptr_t i;
    for(i = 0; i < PUSH_COUNT; ++i) {
        spsc_node_t* const node = malloc(sizeof(spsc_node_t));
        node->data = (void*)i;
        spsc_fifo_push(&fifo, node);
    }

    pthread_join(consumer, NULL);

    printf("cleaning...\n");
    spsc_fifo_destroy(&fifo);

    return 0;
}

