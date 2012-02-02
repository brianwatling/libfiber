#include "hazard_pointer.h"
#include "test_helper.h"
#include <pthread.h>

//NOTE: to be run with valgrind to detect memory leaks or invalid memory access

#define PER_THREAD_COUNT 10000000
#define NUM_THREADS 4
#define POINTERS_PER_THREAD 2

hazard_pointer_thread_record_t* head = 0;

struct test_object
{
    hazard_node_t hazard_node;
    int my_data;
};

void* run_function(void* param)
{
    hazard_pointer_thread_record_t* my_record = hazard_pointer_thread_record_create_and_push(&head, POINTERS_PER_THREAD);
    int i;
    for(i = 0; i < PER_THREAD_COUNT; ++i) {
        hazard_node_t* node = (hazard_node_t*)malloc(sizeof(hazard_node_t));
        hazard_pointer_retire(my_record, node);
    }
    return NULL;
}

int main()
{
    pthread_t threads[NUM_THREADS];
    int i;
    for(i = 0; i < NUM_THREADS; ++i) {
        pthread_create(&threads[i], NULL, &run_function, NULL);
    }

    for(i = 0; i < NUM_THREADS; ++i) {
        pthread_join(threads[i], NULL);
    }

    test_assert(head);
    hazard_pointer_thread_record_t* cur = head;
    size_t count = 0;
    while(cur) {
        test_assert(cur->hazard_pointers_count == POINTERS_PER_THREAD);
        test_assert(cur->retire_threshold = 2 * NUM_THREADS * POINTERS_PER_THREAD);
        hazard_pointer_scan(cur);

        cur = cur->next;
        ++count;
    }
    test_assert(count == NUM_THREADS);

    cur = head;
    while(cur) {
        hazard_pointer_thread_record_t* to_free = cur;
        cur = cur->next;
        free(to_free);
    }
    return 0;
}
