#include "lockfree_ring_buffer.h"
#include "test_helper.h"
#include <pthread.h>

#define PER_THREAD_COUNT 10000000
#define NUM_THREADS 4

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
    intptr_t i;
    for(i = 1; i <= PER_THREAD_COUNT; ++i) {
        lockfree_ring_buffer_pop(rb);
    }
    return NULL;
}

int main()
{
    rb = lockfree_ring_buffer_create(7);
    pthread_barrier_init(&barrier, NULL, NUM_THREADS * 2);

    pthread_t threads[NUM_THREADS * 2];
    int i;
    for(i = 0; i < NUM_THREADS; ++i) {
        pthread_create(&threads[i * 2], NULL, &read_function, NULL);
        pthread_create(&threads[i * 2 + 1], NULL, &write_function, NULL);
    }

    for(i = 0; i < NUM_THREADS * 2; ++i) {
        pthread_join(threads[i], NULL);
    }

    lockfree_ring_buffer_destroy(rb);

    return 0;
}

