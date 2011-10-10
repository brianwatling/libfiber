#include "fiber_manager.h"
#include "test_helper.h"

#define PER_FIBER_COUNT 100
#define NUM_FIBERS 5
#define NUM_THREADS 1
int per_thread_count[NUM_THREADS];
int switch_count = 0;

void* run_function(void* param)
{
    fiber_manager_t* const original_manager = fiber_manager_get();
    int i;
    for(i = 0; i < PER_FIBER_COUNT; ++i) {
        fiber_manager_t* const current_manager = fiber_manager_get();
        if(current_manager != original_manager) {
            __sync_fetch_and_add(&switch_count, 1);
        }
        __sync_fetch_and_add(&per_thread_count[current_manager->id], 1);
        fiber_yield();
    }
    return NULL;
}

int main()
{
    fiber_manager_set_total_kernel_threads(NUM_THREADS);

    printf("starting %d fibers with %d backing threads, running %d yields per fiber\n", NUM_FIBERS, NUM_THREADS, PER_FIBER_COUNT);
    fiber_t* fibers[NUM_FIBERS] = {};
    int i;
    for(i = 0; i < NUM_FIBERS; ++i) {
        fibers[i] = fiber_create(100000, &run_function, NULL);
    }

    for(i = 0; i < NUM_FIBERS; ++i) {
        fiber_join(fibers[i]);
    }

    printf("SUCCESS\n");
    for(i = 0; i < NUM_THREADS; ++i) {
        printf("thread %d count: %d\n", i, per_thread_count[i]);
    }
    printf("switch_count: %d\n", switch_count);
    fflush(stdout);
    printf("time to exit in 1 second\n");
    sleep(1);
    printf("byebye\n");
    _exit(0);
    return 0;
}
