#include "hazard_pointer.h"
#include "machine_specific.h"
#include <assert.h>
#include <string.h>
#include <malloc.h>

hazard_pointer_thread_record_t* hazard_pointer_thread_record_create_and_push(hazard_pointer_thread_record_t** head, size_t pointers_per_thread)
{
    assert(head);
    assert(pointers_per_thread);

    //create a new record
    const size_t sizeof_pointers = pointers_per_thread * sizeof(*((*head)->hazard_pointers));
    const size_t required_size = sizeof(hazard_pointer_thread_record_t) + sizeof_pointers;
    hazard_pointer_thread_record_t* const ret = (hazard_pointer_thread_record_t*)malloc(required_size);
    ret->head = head;
    ret->next = 0;
    ret->retire_threshold = 0;
    ret->retired_count = 0;
    ret->retired_list = NULL;
    ret->hazard_pointers_count = pointers_per_thread;
    memset(ret->hazard_pointers, 0, sizeof_pointers);
    write_barrier();//finish all writes before exposing the record to the other threads

    //swap in the new record as the head
    hazard_pointer_thread_record_t* cur_head;
    do {
        //determine the appropriate retire_threshold. head should always have the correct retire_threshold, so this must be done before swapping ret in as head
        size_t threads = 1;//1 for this thread
        hazard_pointer_thread_record_t* cur = ret->next;
        while(cur) {
            ++threads;
            assert(cur->hazard_pointers_count == ret->hazard_pointers_count);
            cur = cur->next;
        }
        ret->retire_threshold = 2 * threads * pointers_per_thread;

        cur_head = *head;
        load_load_barrier();
        ret->next = cur_head;
    } while(!__sync_bool_compare_and_swap(head, cur_head, ret));

    //update all other threads' retire thresholds
    hazard_pointer_thread_record_t* cur = ret->next;
    while(cur) {
        __sync_add_and_fetch(&cur->retire_threshold, 2 * cur->hazard_pointers_count);//we're increasing N by 1, so R increases by 2 * K (remember R = 2 * N * K)
        cur = cur->next;
    }

    return ret;
}

void hazard_pointer_in_use(hazard_pointer_thread_record_t* hptr, hazard_node_t* node)
{
    const size_t count = hptr->hazard_pointers_count;
    hazard_node_t** const hazard_pointers = &*hptr->hazard_pointers;
    size_t i;
    for(i = 0; i < count; ++i) {
        if(!hazard_pointers[i]) {
            hazard_pointers[i] = node;
            return;
        }
    }
}

void hazard_pointer_retire(hazard_pointer_thread_record_t* hptr, hazard_node_t* node)
{
    node->next = hptr->retired_list;//push the node
    hptr->retired_list = node;
    ++hptr->retired_count;
    if(hptr->retired_count >= hptr->retire_threshold) {
        hazard_pointer_scan(hptr);
    }
}

void hazard_pointer_scan(hazard_pointer_thread_record_t* hptr)
{
    assert(hptr);
    //head always has a correct retired_threshold; that is, retired_threshold = 2 * N * K
    hazard_pointer_thread_record_t* const head = *hptr->head;
    assert(head);
    const size_t max_pointers = head->retire_threshold / 2;
    hazard_node_t** hazards = (hazard_node_t**)malloc(max_pointers * sizeof(*hazards));
    size_t index = 0;

    hazard_pointer_thread_record_t* cur_record = head;
    size_t i;
    while(cur_record) {
        const size_t hazard_pointers_count = cur_record->hazard_pointers_count;
        hazard_node_t** const hazard_pointers = &*cur_record->hazard_pointers;
        for(i = 0; i < hazard_pointers_count; ++i) {
            hazard_node_t* const h = hazard_pointers[i];
            if(h) {
                hazards[index] = h;
                ++index;
            }
        }
        cur_record = cur_record->next;
    }

    hazard_node_t* node = hptr->retired_list;
    hptr->retired_list = NULL;
    hptr->retired_count = 0;

    while(node) {
        hazard_node_t* const next = node->next;

        //TODO: faster search (sort hazards, then binary search)
        int is_hazardous = 0;
        for(i = 0; i < index; ++i) {
            if(node == hazards[i]) {
                is_hazardous = 1;
                break;
            }
        }
        
        if(is_hazardous) {
            node->next = hptr->retired_list;
            hptr->retired_list = node;
            ++hptr->retired_count;
        } else {
            free(node);//TODO: callback function
        }
        node = next;
    }

    free(hazards);
}

