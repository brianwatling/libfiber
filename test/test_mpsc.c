#include <mpsc_fifo.h>
#include "test_helper.h"
#include <pthread.h>
#include <stdint.h>
#include <unistd.h>

#define PUSH_COUNT 10000000
#define NUM_THREADS 4

mpsc_fifo_t fifo;
int results[PUSH_COUNT] = {};
pthread_barrier_t barrier;

void* push_func(void* p)
{
    pthread_barrier_wait(&barrier);
    intptr_t i;
    for(i = 0; i < PUSH_COUNT; ++i) {
        mpsc_node_t* const node = malloc(sizeof(mpsc_node_t));
        node->data = (void*)i;
        mpsc_fifo_push(&fifo, node);
    }
    return NULL;
}

int main()
{
    pthread_barrier_init(&barrier, NULL, NUM_THREADS);
    mpsc_fifo_init(&fifo);

    pthread_t producers[NUM_THREADS];
    intptr_t i = 0;
    for(i = 1; i < NUM_THREADS; ++i) {
        pthread_create(&producers[i], NULL, &push_func, NULL);
    }

    pthread_barrier_wait(&barrier);

    mpsc_node_t* node = NULL;
    for(i = 0; i < PUSH_COUNT * (NUM_THREADS-1); ++i) {
        void* data = NULL;
        while(!mpsc_fifo_peek(&fifo, &data)) {};
        node = mpsc_fifo_pop(&fifo);
        test_assert(node);
        test_assert(node->data == data);
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
    mpsc_fifo_destroy(&fifo);

    return 0;
}
