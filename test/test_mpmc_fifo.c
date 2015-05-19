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

#include "mpmc_fifo.h"
#include "test_helper.h"
#include <pthread.h>
#include <stdint.h>
#include <unistd.h>

#define PUSH_COUNT 1000000
#define NUM_THREADS 2

mpmc_fifo_t fifo;
int results[PUSH_COUNT] = {};
pthread_barrier_t barrier;
hazard_pointer_thread_record_t* hazard_head = NULL;

void release_node(void* user_data, hazard_node_t* node)
{
    free(node);
}

void* push_func(void* p)
{
    pthread_barrier_wait(&barrier);
    hazard_pointer_thread_record_t* hptr = hazard_pointer_thread_record_create_and_push(&hazard_head, MPMC_HAZARD_COUNT);
    intptr_t i;
    for(i = 1; i <= PUSH_COUNT; ++i) {
        mpmc_fifo_node_t* const node = malloc(sizeof(mpmc_fifo_node_t));
        node->value = (void*)i;
        node->hazard.gc_data = NULL;
        node->hazard.gc_function = &release_node;
        mpmc_fifo_push(hptr, &fifo, node);
    }
    return NULL;
}

void * pop_func(void* p)
{
    pthread_barrier_wait(&barrier);
    hazard_pointer_thread_record_t* hptr = hazard_pointer_thread_record_create_and_push(&hazard_head, MPMC_HAZARD_COUNT);
    intptr_t i;
    for(i = 1; i <= PUSH_COUNT; ++i) {
        intptr_t value;
        while(!(value = (intptr_t)mpmc_fifo_trypop(hptr, &fifo))) {};
        test_assert(value > 0);
        test_assert(value <= PUSH_COUNT);
        __sync_fetch_and_add(&results[value - 1], 1);
    }
    return NULL;
}

int main()
{
    pthread_barrier_init(&barrier, NULL, NUM_THREADS * 2);
    mpmc_fifo_node_t* initial_node = (mpmc_fifo_node_t*)malloc(sizeof(mpmc_fifo_node_t));
    initial_node->hazard.gc_function = &release_node;
    initial_node->hazard.gc_data = NULL;
    mpmc_fifo_init(&fifo, initial_node);

    pthread_t producers[NUM_THREADS];
    intptr_t i = 0;
    for(i = 0; i < NUM_THREADS; ++i) {
        pthread_create(&producers[i], NULL, &push_func, NULL);
    }

    pthread_t consumers[NUM_THREADS];
    for(i = 0; i < NUM_THREADS; ++i) {
        pthread_create(&consumers[i], NULL, &pop_func, NULL);
    }

    for(i = 0; i < NUM_THREADS; ++i) {
        pthread_join(producers[i], 0);
    }

    for(i = 0; i < NUM_THREADS; ++i) {
        pthread_join(consumers[i], 0);
    }

    for(i = 0; i < PUSH_COUNT; ++i) {
        test_assert(results[i] == NUM_THREADS);
    }

    printf("cleaning...\n");
    mpmc_fifo_destroy(hazard_head, &fifo);
    hazard_pointer_thread_record_destroy_all(hazard_head);

    return 0;
}
