#include <mpmc_queue.h>
#include "test_helper.h"
#include <pthread.h>
#include <stdint.h>
#include <unistd.h>

#define NUM_THREADS 4
#define PER_THREAD_COUNT 2500000

int pushes[NUM_THREADS] = {};
int pops[NUM_THREADS] = {};
int volatile done[NUM_THREADS] = {};

mpmc_queue_t the_q;

void* push_func(void* p)
{
    intptr_t thr = (intptr_t)p;
    intptr_t i = 0;
    while(!done[thr]) {
        ++i;
        mpmc_queue_node_t* n = malloc(sizeof(mpmc_queue_node_t));
        mpmc_queue_node_init(n, (void*)i);
        while(MPMC_RETRY == mpmc_queue_push_timeout(&the_q, n, 10)) {
            sched_yield();
        }
    }
    return NULL;
}

void* pop_func(void* p)
{
    intptr_t thr = (intptr_t)p;
    intptr_t last = 0;
    (void)last;
    intptr_t counter = 0;
    while(!done[thr]) {
        mpmc_queue_node_t* head = NULL;
        while(MPMC_RETRY == mpmc_queue_fifo_flush_timeout(&the_q, &head, 10)) {
            sched_yield();
        }
        if(!head) {
            usleep(100);
        }
        while(head) {
            intptr_t i = (intptr_t)mpmc_queue_node_get_data(head);
            if(NUM_THREADS == 1) {
                assert(i > last);
                last = i;
            }
            ++counter;
            if(counter > PER_THREAD_COUNT) {
                done[thr] = 1;
            }
            mpmc_queue_node_t* old = head;
            head = head->next;
            free(old);
        }
    }
    return NULL;
}

int main()
{
    mpmc_queue_init(&the_q);

    pthread_t reader[NUM_THREADS];
    pthread_t writer[NUM_THREADS];
    int i;
    for(i = 0; i < NUM_THREADS; ++i) {
        pthread_create(&reader[i], NULL, &pop_func, (void*)(intptr_t)i);
    }
    for(i = 1; i < NUM_THREADS; ++i) {
        pthread_create(&writer[i], NULL, &push_func, (void*)(intptr_t)i);
    }

    push_func(0);

    for(i = 1; i < NUM_THREADS; ++i) {
        pthread_join(reader[i], NULL);
    }
    for(i = 1; i < NUM_THREADS; ++i) {
        pthread_join(writer[i], NULL);
    }

    return 0;
}
