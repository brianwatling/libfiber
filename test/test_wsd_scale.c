#include "work_stealing_deque.h"
#include <pthread.h>
#include "test_helper.h"
#include <stdlib.h>

#define NUM_THREADS 4
#define PER_THREAD_COUNT 100000000

typedef struct thread_data
{
    long long push_count;
    long long pop_count;
    long long steal_count;
    long long empty_count;
    long long attempt_count;
} __attribute__((__aligned__(CACHE_SIZE))) thread_data_t;

wsd_work_stealing_deque_t* fifo[NUM_THREADS] = {};
thread_data_t data[NUM_THREADS] = {};

typedef struct node
{
    struct node* next;
    void* data;
} node_t;

void* run_function(void* param)
{
    intptr_t thread_id = (intptr_t)param;
    unsigned int seed = thread_id;

    node_t* local_nodes = NULL;
    wsd_work_stealing_deque_t* my_fifo = fifo[thread_id];
    thread_data_t* my_data = &(data[thread_id]);

    intptr_t i;
    for(i = 0; i < PER_THREAD_COUNT; ++i) {
        const int action = rand_r(&seed) % 3;
//printf("action: %d\n", action);
        if(action == 0) {
            node_t* n;
            do {
                n = (node_t*)wsd_work_stealing_deque_pop_bottom(my_fifo);
                if(n != WSD_EMPTY && n != WSD_ABORT) {
                    n->next = local_nodes;
                    local_nodes = n;
                    ++my_data->pop_count;
                }
            } while(n == WSD_ABORT);
        } else if(action == 1) {
            node_t* n = NULL;
            if(local_nodes) {
                n = local_nodes;
                local_nodes = local_nodes->next;
            } else {
                n = calloc(1, sizeof(*n));
            }
            n->data = (void*)i;
            wsd_work_stealing_deque_push_bottom(my_fifo, n);
            ++my_data->push_count;
        } else if(action == 2) {
            intptr_t j = thread_id + 1;
            intptr_t tries = NUM_THREADS - 1;//don't steal from yourself
            int stole = 0;
            while(tries > 0) {
                wsd_work_stealing_deque_t* const steal_fifo = fifo[j % NUM_THREADS];
                node_t* n;
                do {
                    n = (node_t*)wsd_work_stealing_deque_steal(steal_fifo);
                    ++my_data->attempt_count;
                } while(n == WSD_ABORT);
                if(n != WSD_EMPTY) {
                    n->next = local_nodes;
                    local_nodes = n;
                    stole = 1;
                    ++my_data->steal_count;
                    break;//stole one
                }
                --tries;
                ++j;
                if(!stole) {
                    ++my_data->empty_count;
                }
            }
        }
    }
    return NULL;
}

int main()
{
    intptr_t i;
    for(i = 0; i < NUM_THREADS; ++i) {
        fifo[i] = wsd_work_stealing_deque_create();
    }

    pthread_t threads[NUM_THREADS];
    for(i = 1; i < NUM_THREADS; ++i) {
        pthread_create(&(threads[i]), NULL, &run_function, (void*)i);
    }

    run_function(NULL);

    for(i = 1; i < NUM_THREADS; ++i) {
        pthread_join(threads[i], NULL);
    }

    thread_data_t total = {};
    for(i = 0; i < NUM_THREADS; ++i) {
        wsd_work_stealing_deque_destroy(fifo[i]);
        printf("thread %d - push: %lld pop: %lld steal: %lld attempt: %lld empty: %lld\n", (int)i, data[i].push_count, data[i].pop_count, data[i].steal_count, data[i].attempt_count, data[i].empty_count);
        total.push_count += data[i].push_count;
        total.pop_count += data[i].pop_count;
        total.steal_count += data[i].steal_count;
        total.empty_count += data[i].empty_count;
        total.attempt_count += data[i].attempt_count;
    }
    printf("\ntotal - push: %lld pop: %lld steal: %lld attempt: %lld empty: %lld\n", total.push_count, total.pop_count, total.steal_count, total.attempt_count, total.empty_count);

    return 0;
}

