#include "fiber_channel.h"
#include "fiber_manager.h"
#include "test_helper.h"

fiber_bounded_channel_t* channel = NULL;
#define PER_FIBER_COUNT 100000
#define NUM_FIBERS 100
#define NUM_THREADS 2

int results[PER_FIBER_COUNT] = {};

void* send_function(void* param)
{
    intptr_t i;
    for(i = 1; i < PER_FIBER_COUNT + 1; ++i) {
        fiber_bounded_channel_send(channel, (void*)i);
    }
    return NULL;
}

int main(int argc, char* argv[])
{
    fiber_manager_init(NUM_THREADS);

    fiber_signal_t signal;
    fiber_signal_init(&signal);
    //specifying an argument will make the channels spin
    channel = fiber_bounded_channel_create(10, argc > 1 ? NULL : &signal);

    fiber_t* send_fibers[NUM_FIBERS];
    int i;
    for(i = 0; i < NUM_FIBERS; ++i) {
        send_fibers[i] = fiber_create(20000, &send_function, NULL);
    }

    for(i = 0; i < NUM_FIBERS * PER_FIBER_COUNT; ++i) {
        intptr_t result = (intptr_t)fiber_bounded_channel_receive(channel);
        results[result - 1] += 1;
    }

    for(i = 0; i < NUM_FIBERS; ++i) {
        fiber_join(send_fibers[i], NULL);
    }

    for(i = 0; i < PER_FIBER_COUNT; ++i) {
        test_assert(results[i] == NUM_FIBERS);
    }
    fiber_bounded_channel_destroy(channel);
    fiber_signal_destroy(&signal);

    return 0;
}

