#include <pthread.h>
#include "test_helper.h"

int volatile counter = 1;
pthread_mutex_t mutex;
pthread_cond_t cond;
#define PER_FIBER_COUNT 1000
#define NUM_FIBERS 100
#define NUM_THREADS 4

void* run_function(void* param)
{
    intptr_t myNum = (intptr_t)param;
    int i;
    for(i = 0; i < PER_FIBER_COUNT; ++i) {
        pthread_mutex_lock(&mutex);
        while(counter < (myNum + i * NUM_FIBERS) ) {
            pthread_cond_wait(&cond, &mutex);
        }
        test_assert(counter == (myNum + i * NUM_FIBERS));
//printf("%d %d %ld\n", counter, i, myNum);
        ++counter;
        pthread_mutex_unlock(&mutex);
        pthread_cond_broadcast(&cond);
    }
    return NULL;
}

int main()
{
    //fiber_manager_set_total_kernel_threads(NUM_THREADS);

    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&cond, NULL);

    pthread_t fibers[NUM_FIBERS];
    intptr_t i;
    for(i = 1; i <= NUM_FIBERS; ++i) {
        pthread_create(&fibers[i-1], NULL, &run_function, (void*)i);
    }

    for(i = 1; i <= NUM_FIBERS; ++i) {
        pthread_join(fibers[i-1], NULL);
    }

    test_assert(counter == (NUM_FIBERS * PER_FIBER_COUNT + 1));

    return 0;
}
