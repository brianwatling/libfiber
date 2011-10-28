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
        while(!(node = spsc_fifo_pop(&fifo))) {};
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

