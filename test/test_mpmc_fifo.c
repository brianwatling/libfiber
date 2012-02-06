#include "mpmc_fifo.h"
#include "test_helper.h"
#include <pthread.h>
#include <stdint.h>
#include <unistd.h>

#define PUSH_COUNT 1000000
#define NUM_THREADS 4

mpmc_fifo_t fifo;
int results[PUSH_COUNT] = {};
pthread_barrier_t barrier;

void release_node(void* user_data, hazard_node_t* node)
{
    free(node);
}

void* push_func(void* p)
{
    pthread_barrier_wait(&barrier);
    hazard_node_gc_t gc = { NULL, &release_node};
    hazard_pointer_thread_record_t* hptr = mpmc_fifo_add_hazard_thread_record(&fifo, gc);
    intptr_t i;
    for(i = 1; i <= PUSH_COUNT; ++i) {
        mpmc_fifo_node_t* const node = malloc(sizeof(mpmc_fifo_node_t));
        node->value = (void*)i;
        mpmc_fifo_push(hptr, &fifo, node);
    }
    return NULL;
}

void * pop_func(void* p)
{
    pthread_barrier_wait(&barrier);
    hazard_node_gc_t gc = { NULL, &release_node};
    hazard_pointer_thread_record_t* hptr = mpmc_fifo_add_hazard_thread_record(&fifo, gc);
    intptr_t i;
    for(i = 1; i <= PUSH_COUNT; ++i) {
        intptr_t const value = (intptr_t)mpmc_fifo_pop(hptr, &fifo);
        test_assert(value > 0);
        test_assert(value <= PUSH_COUNT);
        __sync_fetch_and_add(&results[value - 1], 1);
    }
    return NULL;
}

int main()
{
    pthread_barrier_init(&barrier, NULL, NUM_THREADS * 2);
    hazard_node_gc_t gc = { NULL, &release_node};
    mpmc_fifo_init(&fifo, gc, (mpmc_fifo_node_t*)malloc(sizeof(mpmc_fifo_node_t)));

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
    mpmc_fifo_destroy(&fifo);

    return 0;
}
