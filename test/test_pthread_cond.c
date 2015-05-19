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

#include <pthread.h>
#include "test_helper.h"

int volatile counter = 1;
pthread_mutex_t mutex;
pthread_cond_t cond;
#define PER_FIBER_COUNT 1000
#define NUM_FIBERS 100
#define NUM_THREADS 4

void* run_function(void* param)
{
    intptr_t myNum = (intptr_t)param;
    int i;
    for(i = 0; i < PER_FIBER_COUNT; ++i) {
        pthread_mutex_lock(&mutex);
        while(counter < (myNum + i * NUM_FIBERS)) {
            pthread_cond_wait(&cond, &mutex);
        }
        test_assert(counter == (myNum + i * NUM_FIBERS));
        ++counter;
        pthread_mutex_unlock(&mutex);
        pthread_cond_broadcast(&cond);
    }
    return NULL;
}

int main()
{
    //fiber_manager_init(NUM_THREADS);

    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&cond, NULL);

    pthread_t fibers[NUM_FIBERS];
    intptr_t i;
    for(i = 1; i <= NUM_FIBERS; ++i) {
        pthread_create(&fibers[i-1], NULL, &run_function, (void*)i);
    }

    for(i = 1; i <= NUM_FIBERS; ++i) {
        pthread_join(fibers[i-1], NULL);
    }

    test_assert(counter == (NUM_FIBERS * PER_FIBER_COUNT + 1));

    return 0;
}
