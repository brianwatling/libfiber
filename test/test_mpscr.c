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

#include "mpsc_relaxed_fifo.h"
#include "test_helper.h"
#include <pthread.h>
#include <stdint.h>
#include <unistd.h>

#define PUSH_COUNT 10000000
#define NUM_THREADS 4

mpscr_fifo_t* fifo = NULL;
int results[PUSH_COUNT] = {};
pthread_barrier_t barrier;

void* push_func(void* p)
{
    pthread_barrier_wait(&barrier);
    intptr_t thread_id = (intptr_t)p;
    intptr_t i;
    for(i = 0; i < PUSH_COUNT; ++i) {
        spsc_node_t* const node = malloc(sizeof(spsc_node_t));
        node->data = (void*)i;
        mpscr_fifo_push(fifo, thread_id, node);
    }
    return NULL;
}

int main()
{
    pthread_barrier_init(&barrier, NULL, NUM_THREADS);
    fifo = mpscr_fifo_create(NUM_THREADS-1);
    test_assert(fifo);

    pthread_t producers[NUM_THREADS];
    intptr_t i = 0;
    for(i = 1; i < NUM_THREADS; ++i) {
        pthread_create(&producers[i], NULL, &push_func, (void*)(i-1));
    }

    pthread_barrier_wait(&barrier);

    spsc_node_t* node = NULL;
    for(i = 0; i < PUSH_COUNT * (NUM_THREADS-1); ++i) {
        while(!(node = mpscr_fifo_trypop(fifo))) {};
        ++results[(intptr_t)node->data];
        free(node);
    }

    for(i = 1; i < NUM_THREADS; ++i) {
        pthread_join(producers[i], 0);
    }

    for(i = 0; i < PUSH_COUNT; ++i) {
        test_assert(results[i] == (NUM_THREADS - 1));
    }

    printf("cleaning...\n");
    mpscr_fifo_destroy(fifo);

    return 0;
}
