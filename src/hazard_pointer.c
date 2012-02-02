#include "hazard_pointer.h"
#include "machine_specific.h"
#include <assert.h>
#include <string.h>

hazard_pointer_thread_record_t* hazard_pointer_thread_record_create_and_push(hazard_pointer_thread_record_t** head, size_t pointers_per_thread)
{
    assert(head);
    assert(pointers_per_thread);

    //create a new record
    const size_t sizeof_pointers = pointers_per_thread * sizeof(hazard_pointer_t);
    const size_t required_size = sizeof(hazard_pointer_thread_record_t) + sizeof_pointers;
    hazard_pointer_thread_record_t* const ret = (hazard_pointer_thread_record_t*)malloc(required_size);
    ret->next = 0;
    ret->retire_threshold = 2 * pointers_per_thread;//assume we're the only thread, we'll increase this later
    ret->retired_count = 0;
    ret->retired_list = NULL;
    ret->hazard_pointers_count = pointers_per_thread;
    memset(ret->hazard_pointers, 0, sizeof_pointers);
    write_barrier();//finish all writes before exposing the record to the other threads

    //swap in the new record as the head
    hazard_pointer_thread_record_t* cur_head;
    do {
        cur_head = *head;
        load_load_barrier();
        ret->next = cur_head;
    } while(!__sync_bool_compare_and_swap(head, cur_head, ret));

    //update all other threads' retire thresholds
    size_t other_threads = 0;
    hazard_pointer_thread_record_t* cur = ret->next;
    while(cur) {
        __sync_add_and_fetch(&cur->retire_threshold, 2 * cur->hazard_pointers_count);//we're increasing N by 1, so R increases by 2 * K (remember R = 2 * N * K)
        cur = cur->next;
        ++other_threads;
    }

    //now fix up our own retire_threshold
    __sync_add_and_fetch(&ret->retire_threshold, 2 * other_threads * pointers_per_thread);

    return ret;
}

void hazard_pointer_retire(hazard_pointer_thread_record_t* hptr, hazard_pointer_node_t* node)
{
}

