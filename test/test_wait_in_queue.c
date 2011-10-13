#include "fiber_manager.h"
#include "test_helper.h"

#define NUM_THREADS 4
#define NUM_FIBERS 1000
#define PER_FIBER_COUNT 10000

mpsc_fifo_t* fifo = NULL;

void* run_function(void* param)
{
    int i;
    for(i = 0; i < PER_FIBER_COUNT; ++i) {
        fiber_manager_wait_in_queue(fiber_manager_get(), fifo);
    }
    return NULL;
}

int main()
{
    fiber_manager_set_total_kernel_threads(NUM_THREADS);
    fifo = mpsc_fifo_create(fiber_manager_get_kernel_thread_count());

    fiber_t* fibers[NUM_FIBERS];
    int i;
    for(i = 0; i < NUM_FIBERS; ++i) {
        fibers[i] = fiber_create(20000, &run_function, NULL);
    }

    fiber_yield();

    fiber_manager_wake_from_queue(fiber_manager_get(), fifo, NUM_FIBERS * PER_FIBER_COUNT);

    for(i = 0; i < NUM_FIBERS; ++i) {
        fiber_join(fibers[i]);
    }

    return 0;
}
