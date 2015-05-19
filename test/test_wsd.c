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
#include "work_stealing_deque.h"
#include <pthread.h>

#define SHARED_COUNT 5000000
wsd_work_stealing_deque_t* wsd_d2 = 0;
#define NUM_THREADS 4
int results[NUM_THREADS][SHARED_COUNT] = {};
size_t run_func_count[NUM_THREADS] = {};
uint64_t total = 0;
int done = 0;

void* run_func(void* p)
{
    intptr_t threadId = (intptr_t)p;
    while(!done) {
        void* ret = wsd_work_stealing_deque_steal(wsd_d2);
        if(ret != WSD_EMPTY && ret != WSD_ABORT) {
            __sync_add_and_fetch(&results[threadId][(intptr_t)ret], 1);
            __sync_add_and_fetch(&total, (intptr_t)ret);
            ++run_func_count[threadId];
        }
    }
    return NULL;
}

int main(int argc, char* argv[])
{
    wsd_circular_array_t* wsd_a = wsd_circular_array_create(8);
    test_assert(wsd_a);
    test_assert(wsd_circular_array_size(wsd_a) == 256);
    wsd_circular_array_put(wsd_a, 1, (void*)1);
    test_assert((void*)1 == wsd_circular_array_get(wsd_a, 1));
    wsd_circular_array_destroy(wsd_a);

    wsd_work_stealing_deque_t* wsd_d = wsd_work_stealing_deque_create();
    int i;
    for(i = 0; i < 1000; ++i) {
        wsd_work_stealing_deque_push_bottom(wsd_d, (void*)(intptr_t)i);
    }
    for(i = 1000; i > 0; --i) {
        void* item = wsd_work_stealing_deque_pop_bottom(wsd_d);
        test_assert((intptr_t)item == i-1);
    }
    wsd_work_stealing_deque_destroy(wsd_d);

    wsd_d2 = wsd_work_stealing_deque_create();
    pthread_t reader[NUM_THREADS];
    for(i = 1; i < NUM_THREADS; ++i) {
        pthread_create(&reader[i], NULL, &run_func, (void*)(intptr_t)i);
    }

    for(i = 0; i < SHARED_COUNT; ++i) {
        wsd_work_stealing_deque_push_bottom(wsd_d2, (void*)(intptr_t)i);
        if((i & 7) == 0) {
            void* val = wsd_work_stealing_deque_pop_bottom(wsd_d2);
            if(val != WSD_EMPTY && val != WSD_ABORT) {
                __sync_add_and_fetch(&results[0][(intptr_t)val], 1);
                __sync_add_and_fetch(&total, (intptr_t)val);
                ++run_func_count[0];
            }
        }
    }
    void* val = 0;
    do {
        val = wsd_work_stealing_deque_pop_bottom(wsd_d2);
        if(val != WSD_EMPTY && val != WSD_ABORT) {
            __sync_add_and_fetch(&results[0][(intptr_t)val], 1);
            __sync_add_and_fetch(&total, (intptr_t)val);
            ++run_func_count[0];
        }
    } while(val != WSD_EMPTY);

    done = 1;
    for(i = 1; i < NUM_THREADS; ++i) {
        pthread_join(reader[i], NULL);
    }

    uint64_t expected_total = 0;
    for(i = 0; i < SHARED_COUNT; ++i) {
        int sum = 0;
        int j;
        for(j = 0; j < NUM_THREADS; ++j) {
            sum += results[j][i];
        }
        test_assert(sum == 1);
        expected_total += i;
    }
    test_assert(total == expected_total);
    for(i = 0; i < NUM_THREADS; ++i) {
        test_assert(run_func_count[i] > 0);
    }
    return 0;
}
