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
#include "machine_specific.h"
#include <sys/time.h>
#include <pthread.h>

intptr_t NUM_THREADS = 4;
intptr_t COUNT = 1000000;

pthread_barrier_t barrier;
pthread_t* threads = NULL;

void warm_up(intptr_t thread_id, uint64_t count)
{
    volatile intptr_t integer = 0;
    uint64_t i;
    struct timeval start;
    struct timeval end;
    gettimeofday(&start, NULL);
    do {
        for(i = 0; i < 100000; ++i) {
            integer += 1;
        }
        gettimeofday(&end, NULL);
    } while(1000000L > ((end.tv_sec - start.tv_sec) * 1000000L + end.tv_usec - start.tv_usec));
}

void test_increment(intptr_t thread_id, uint64_t count)
{
    volatile intptr_t integer = 0;
    uint64_t i;
    for(i = 0; i < count; ++i) {
        integer += 1;
    }
}

void test_increment_flush(intptr_t thread_id, uint64_t count)
{
    volatile intptr_t integer = 0;
    uint64_t i;
    for(i = 0; i < count; ++i) {
        integer += 1;
        store_load_barrier();
    }
}

void test_atomic_increment(intptr_t thread_id, uint64_t count)
{
    volatile intptr_t integer = 0;
    uint64_t i;
    for(i = 0; i < count; ++i) {
        __sync_fetch_and_add(&integer, 1);
    }
}

void test_atomic_increment_shared(intptr_t thread_id, uint64_t count)
{
    static volatile intptr_t shared_integer = 0;
    uint64_t i;
    for(i = 0; i < count; ++i) {
        __sync_fetch_and_add(&shared_integer, 1);
    }
}

void test_cas(intptr_t thread_id, uint64_t count)
{
    volatile intptr_t integer = 0;
    uint64_t i = 0;
    while(i < count) {
        const intptr_t cur = integer;
        const intptr_t next = cur + 1;
        if(__sync_bool_compare_and_swap(&integer, cur, next)) {
            ++i;
        }
    }
}

void test_cas_shared(intptr_t thread_id, uint64_t count)
{
    static volatile intptr_t integer = 0;
    uint64_t i = 0;
    while(i < count) {
        const intptr_t cur = integer;
        const intptr_t next = cur + 1;
        if(__sync_bool_compare_and_swap(&integer, cur, next)) {
            ++i;
        }
    }
}

void test_dcas(intptr_t thread_id, uint64_t count)
{
    volatile pointer_pair_t var = {};
    uint64_t i = 0;
    while(i < count) {
        const pointer_pair_t cur = var;
        const pointer_pair_t next = {cur.low + 1, cur.high + 1};
        if(compare_and_swap2(&var, &cur, &next)) {
            ++i;
        }
    }
}

void test_dcas_shared(intptr_t thread_id, uint64_t count)
{
    static volatile pointer_pair_t var = {};
    uint64_t i = 0;
    while(i < count) {
        const pointer_pair_t cur = var;
        const pointer_pair_t next = {cur.low + 1, cur.high + 1};
        if(compare_and_swap2(&var, &cur, &next)) {
            ++i;
        }
    }
}

void test_read_shared_sync_none(intptr_t thread_id, uint64_t count)
{
    static volatile intptr_t shared_integer = 0;
    if(!thread_id) {
        uint64_t i;
        for(i = 0; i < count; ++i) {
            shared_integer += 1;
        }
    } else {
        uint64_t total = 0;
        while(shared_integer < count) {
            total += shared_integer;
        }
    }
}

void test_read_shared_sync_writer(intptr_t thread_id, uint64_t count)
{
    static volatile intptr_t shared_integer = 0;
    if(!thread_id) {
        uint64_t i;
        for(i = 0; i < count; ++i) {
            __sync_fetch_and_add(&shared_integer, 1);
        }
    } else {
        uint64_t total = 0;
        while(shared_integer < count) {
            total += shared_integer;
        }
    }
}

void test_read_shared_sync_all(intptr_t thread_id, uint64_t count)
{
    static volatile intptr_t shared_integer = 0;
    if(!thread_id) {
        uint64_t i;
        for(i = 0; i < count; ++i) {
            __sync_fetch_and_add(&shared_integer, 1);
        }
    } else {
        uint64_t total = 0;
        while(shared_integer < count) {
            store_load_barrier();//not needed, but let's test it.
            total += shared_integer;
        }
    }
}

void test_exchange(intptr_t thread_id, uint64_t count)
{
    void* p = 0;
    uint64_t i;
    for(i = 0; i < count; ++i) {
        atomic_exchange_pointer(&p, 0);
    }
}

void test_exchange_shared(intptr_t thread_id, uint64_t count)
{
    static void* p = 0;
    uint64_t i;
    for(i = 0; i < count; ++i) {
        atomic_exchange_pointer(&p, 0);
    }
}

typedef void (*test_function_t)(intptr_t thread_id, uint64_t count);

#define ADD_FUNCTION(x) {#x, x}

typedef struct
{
    const char* name;
    test_function_t function;
    uint64_t total_microseconds;
} test_function_info_t;

test_function_info_t test_functions[] = {
    ADD_FUNCTION(warm_up),
    ADD_FUNCTION(test_increment),
    ADD_FUNCTION(test_increment_flush),
    ADD_FUNCTION(test_atomic_increment),
    ADD_FUNCTION(test_atomic_increment_shared),
    ADD_FUNCTION(test_cas),
    ADD_FUNCTION(test_cas_shared),
    ADD_FUNCTION(test_dcas),
    ADD_FUNCTION(test_dcas_shared),
    ADD_FUNCTION(test_read_shared_sync_none),
    ADD_FUNCTION(test_read_shared_sync_writer),
    ADD_FUNCTION(test_read_shared_sync_all),
    ADD_FUNCTION(test_exchange),
    ADD_FUNCTION(test_exchange_shared),
    {NULL, NULL}
};

void* test_driver(void* p)
{
    intptr_t thread_id = (intptr_t)p;

    struct timeval start;
    struct timeval end;

    test_function_info_t* current_test = &test_functions[0];
    while(current_test->name) {
        pthread_barrier_wait(&barrier);
        if(!thread_id) {
            printf("\nstarting test: %s\n", current_test->name);
        }
        pthread_barrier_wait(&barrier);
        gettimeofday(&start, NULL);
        current_test->function(thread_id, COUNT);
        gettimeofday(&end, NULL);

        const uint64_t microseconds = (end.tv_sec - start.tv_sec) * 1000000L + end.tv_usec - start.tv_usec;
        const double ops_per_second = ((double)COUNT) * 1000000.0 / microseconds;
        printf("thread %" PRIdPTR " finished in %" PRIu64 " microseconds = %lf ops per second\n", thread_id, microseconds, ops_per_second);
        __sync_add_and_fetch(&current_test->total_microseconds, microseconds);

        ++current_test;
    }

    return NULL;
}

int main(int argc, char* argv[])
{
    if(argc > 1) {
        NUM_THREADS = atoi(argv[1]);
    }
    if(argc > 2) {
        COUNT = atoi(argv[2]);
    }

    pthread_barrier_init(&barrier, NULL, NUM_THREADS);
    threads = malloc(NUM_THREADS * sizeof(*threads));
    intptr_t i;
    for(i = 1; i < NUM_THREADS; ++i) {
        pthread_create(&threads[i], NULL, &test_driver, (void*)i);
    }

    test_driver(0);

    for(i = 1; i < NUM_THREADS; ++i) {
        pthread_join(threads[i], NULL);
    }

    printf("\n");
    test_function_info_t* current_test = &test_functions[0];
    while(current_test->name) {
        const double avg_microseconds = ((double)current_test->total_microseconds) / NUM_THREADS;
        const double ops_per_second = ((double)NUM_THREADS * COUNT) * 1000000.0 / avg_microseconds;
        printf("total: %s finished in %" PRIu64 " microseconds = %lf ops per second\n", current_test->name, current_test->total_microseconds, ops_per_second);
        ++current_test;
    }

    return 0;
}

