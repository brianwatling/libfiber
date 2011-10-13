#include "fiber_cond.h"
#include "fiber_manager.h"
#include "test_helper.h"

int volatile counter = 1;
fiber_mutex_t mutex;
fiber_cond_t cond;
#define PER_FIBER_COUNT 1000
#define NUM_FIBERS 100
#define NUM_THREADS 4

void* run_function(void* param)
{
    intptr_t myNum = (intptr_t)param;
    int i;
    for(i = 0; i < PER_FIBER_COUNT; ++i) {
        fiber_mutex_lock(&mutex);
        while(counter < (myNum + i * NUM_FIBERS) ) {
            fiber_cond_wait(&cond, &mutex);
        }
        test_assert(counter == (myNum + i * NUM_FIBERS));
printf("%d %d %ld\n", counter, i, myNum);
        ++counter;
        fiber_cond_broadcast(&cond);
        fiber_mutex_unlock(&mutex);
    }
    return NULL;
}

int main()
{
    fiber_manager_set_total_kernel_threads(NUM_THREADS);

    fiber_mutex_init(&mutex);
    fiber_cond_init(&cond);

    fiber_t* fibers[NUM_FIBERS];
    intptr_t i;
    for(i = 1; i <= NUM_FIBERS; ++i) {
        fibers[i-1] = fiber_create(20000, &run_function, (void*)i);
    }

    for(i = 1; i <= NUM_FIBERS; ++i) {
        fiber_join(fibers[i-1]);
    }

    test_assert(counter == (NUM_FIBERS * PER_FIBER_COUNT + 1));

    return 0;
}
