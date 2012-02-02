#include "hazard_pointer.h"
#include "test_helper.h"
#include <pthread.h>

#define NUM_THREADS 4
#define POINTERS_PER_THREAD 2

hazard_pointer_thread_record_t* head = 0;

void* run_function(void* param)
{
    hazard_pointer_thread_record_t* my_record = hazard_pointer_thread_record_create_and_push(&head, POINTERS_PER_THREAD);
    return my_record;
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
        cur = cur->next;
        ++count;
    }
    test_assert(count == NUM_THREADS);

    return 0;
}
