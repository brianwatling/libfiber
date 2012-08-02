#include "lockfree_ring_buffer.h"
#include "test_helper.h"
#include <pthread.h>

int64_t time_diff(const struct timespec* start, const struct timespec* end) {
    return (end->tv_sec * 1000000000LL + end->tv_nsec) - (start->tv_sec * 1000000000LL + start->tv_nsec);
}

#define PER_THREAD_COUNT 30000000
#define NUM_THREADS 2

lockfree_ring_buffer_t* rb;
pthread_barrier_t barrier;

void* write_function(void* param)
{
    pthread_barrier_wait(&barrier);
    intptr_t i;
    for(i = 1; i <= PER_THREAD_COUNT; ++i) {
        lockfree_ring_buffer_push(rb, (void*)i);
    }
    return NULL;
}

void* read_function(void* param)
{
    pthread_barrier_wait(&barrier);
    struct timespec last;
    clock_gettime(CLOCK_MONOTONIC, &last);
    intptr_t i;
    for(i = 1; i <= PER_THREAD_COUNT; ++i) {
        lockfree_ring_buffer_pop(rb);
        if(i % 10000000 == 0) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            printf("%p Received 10000000 in %lf seconds\n", param, 0.000000001 * time_diff(&last, &now));
            last = now;
        }
    }
    return NULL;
}

int main()
{
    rb = lockfree_ring_buffer_create(12);
    pthread_barrier_init(&barrier, NULL, NUM_THREADS * 2);

    pthread_t threads[NUM_THREADS * 2];
    intptr_t i;
    for(i = 0; i < NUM_THREADS; ++i) {
        pthread_create(&threads[i * 2], NULL, &read_function, (void*)i + 1);
        pthread_create(&threads[i * 2 + 1], NULL, &write_function, NULL);
    }

    for(i = 0; i < NUM_THREADS * 2; ++i) {
        pthread_join(threads[i], NULL);
    }

    lockfree_ring_buffer_destroy(rb);

    return 0;
}

