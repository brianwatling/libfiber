#include "fiber_channel.h"
#include "fiber_manager.h"
#include "test_helper.h"

fiber_unbounded_channel_t channel;
#define PER_FIBER_COUNT 100000
#define NUM_FIBERS 100
#define NUM_THREADS 2

int results[PER_FIBER_COUNT] = {};

void* send_function(void* param)
{
    intptr_t i;
    for(i = 0; i < PER_FIBER_COUNT; ++i) {
        fiber_unbounded_channel_message_t* node = malloc(sizeof(*node));
        test_assert(node);
        node->data = (void*)i;
        fiber_unbounded_channel_send(&channel, node);
    }
    return NULL;
}

int main()
{
    fiber_manager_init(NUM_THREADS);

    fiber_unbounded_channel_init(&channel);

    fiber_t* send_fibers[NUM_FIBERS];
    int i;
    for(i = 0; i < NUM_FIBERS; ++i) {
        send_fibers[i] = fiber_create(20000, &send_function, NULL);
    }

    for(i = 0; i < NUM_FIBERS * PER_FIBER_COUNT; ++i) {
        fiber_unbounded_channel_message_t* node = fiber_unbounded_channel_receive(&channel);
        results[(intptr_t)node->data] += 1;
        free(node);
    }

    for(i = 0; i < NUM_FIBERS; ++i) {
        fiber_join(send_fibers[i], NULL);
    }

    for(i = 0; i < PER_FIBER_COUNT; ++i) {
        test_assert(results[i] == NUM_FIBERS);
    }
    fiber_unbounded_channel_destroy(&channel);

    return 0;
}

