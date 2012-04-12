#include "fiber_channel.h"
#include "fiber_manager.h"
#include "test_helper.h"

fiber_bounded_channel_t* channel_one = NULL;
fiber_bounded_channel_t* channel_two = NULL;
#define PER_FIBER_COUNT 10000000
#define NUM_THREADS 2

void* ping_function(void* param)
{
    intptr_t i;
    for(i = 1; i <= PER_FIBER_COUNT; ++i) {
        fiber_bounded_channel_send(channel_one, (void*)i);
        fiber_bounded_channel_receive(channel_two);
    }
    return NULL;
}

void* pong_function(void* param)
{
    intptr_t i;
    for(i = 1; i <= PER_FIBER_COUNT; ++i) {
        fiber_bounded_channel_receive(channel_one);
        fiber_bounded_channel_send(channel_two, (void*)i);
    }
    return NULL;
}

int main(int argc, char* argv[])
{
    fiber_manager_init(NUM_THREADS);

    fiber_signal_t signal_one;
    fiber_signal_init(&signal_one);
    fiber_signal_t signal_two;
    fiber_signal_init(&signal_two);

    //specifying an argument will make the channels spin
    channel_one = fiber_bounded_channel_create(128, argc > 1 ? NULL : &signal_one);
    channel_two = fiber_bounded_channel_create(128, argc > 1 ? NULL : &signal_two);

    fiber_t* ping_fiber;
    ping_fiber = fiber_create(20000, &ping_function, NULL);

    pong_function(NULL);

    fiber_join(ping_fiber, NULL);

    fiber_bounded_channel_destroy(channel_one);
    fiber_bounded_channel_destroy(channel_two);

    fiber_signal_destroy(&signal_one);
    fiber_signal_destroy(&signal_two);

    return 0;
}

