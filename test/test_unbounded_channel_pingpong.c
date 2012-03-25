#include "fiber_channel.h"
#include "fiber_manager.h"
#include "test_helper.h"

fiber_unbounded_channel_t channel_one;
fiber_unbounded_channel_t channel_two;
#define PER_FIBER_COUNT 100000
#define NUM_FIBERS 100
#define NUM_THREADS 2

void* ping_function(void* param)
{
    intptr_t i;
    fiber_unbounded_channel_message_t* node = malloc(sizeof(*node));
    test_assert(node);
    for(i = 1; i <= PER_FIBER_COUNT; ++i) {
        node->data = (void*)i;
        fiber_unbounded_channel_send(&channel_one, node);
        node = (fiber_unbounded_channel_message_t*)fiber_unbounded_channel_receive(&channel_two);
    }
    return NULL;
}

void* pong_function(void* param)
{
    intptr_t i;
    for(i = 1; i <= PER_FIBER_COUNT; ++i) {
        fiber_unbounded_channel_message_t* node = fiber_unbounded_channel_receive(&channel_one);
        fiber_unbounded_channel_send(&channel_two, node);
    }
    return NULL;
}

int main()
{
    fiber_manager_init(NUM_THREADS);

    fiber_unbounded_channel_init(&channel_one);
    fiber_unbounded_channel_init(&channel_two);

    fiber_t* ping_fibers[NUM_FIBERS];
    fiber_t* pong_fibers[NUM_FIBERS];
    int i;
    for(i = 0; i < NUM_FIBERS; ++i) {
        ping_fibers[i] = fiber_create(20000, &ping_function, NULL);
        pong_fibers[i] = fiber_create(20000, &pong_function, NULL);
    }

    for(i = 0; i < NUM_FIBERS; ++i) {
        fiber_join(ping_fibers[i], NULL);
        fiber_join(pong_fibers[i], NULL);
    }

    fiber_unbounded_channel_destroy(&channel_one);
    fiber_unbounded_channel_destroy(&channel_two);

    return 0;
}

