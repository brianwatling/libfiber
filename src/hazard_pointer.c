#include "hazard_pointer.h"
#include "machine_specific.h"
#include <assert.h>
#include <string.h>
#include <malloc.h>
#include <stdlib.h>

hazard_pointer_thread_record_t* hazard_pointer_thread_record_create_and_push(hazard_pointer_thread_record_t** head, size_t pointers_per_thread, hazard_node_gc_t garbage_collector)
{
    assert(head);
    assert(pointers_per_thread);
    assert(garbage_collector.free_function);

    //create a new record
    const size_t sizeof_pointers = pointers_per_thread * sizeof(*((*head)->hazard_pointers));
    const size_t required_size = sizeof(hazard_pointer_thread_record_t) + sizeof_pointers;
    hazard_pointer_thread_record_t* const ret = (hazard_pointer_thread_record_t*)calloc(1, required_size);
    ret->head = head;
    ret->garbage_collector = garbage_collector;
    ret->hazard_pointers_count = pointers_per_thread;
    write_barrier();//finish all writes before exposing the record to the other threads

    //swap in the new record as the head
    hazard_pointer_thread_record_t* cur_head;
    do {
        cur_head = *head;
        load_load_barrier();
        ret->next = cur_head;

        //determine the appropriate retire_threshold. head should always have the correct retire_threshold, so this must be done before swapping ret in as head
        size_t threads = 1;//1 for this thread
        hazard_pointer_thread_record_t* cur = ret->next;
        while(cur) {
            ++threads;
            assert(cur->hazard_pointers_count == ret->hazard_pointers_count);
            cur = cur->next;
        }
        ret->retire_threshold = 2 * threads * pointers_per_thread;
    } while(!__sync_bool_compare_and_swap(head, cur_head, ret));

    //update all other threads' retire thresholds
    hazard_pointer_thread_record_t* cur = ret->next;
    while(cur) {
        __sync_add_and_fetch(&cur->retire_threshold, 2 * cur->hazard_pointers_count);//we're increasing N by 1, so R increases by 2 * K (remember R = 2 * N * K)
        cur = cur->next;
    }

    return ret;
}

void hazard_pointer_thread_record_destroy_all(hazard_pointer_thread_record_t* head)
{
    hazard_pointer_thread_record_t* cur = head;
    while(cur) {
        cur->head = &cur;
        hazard_pointer_thread_record_t* const next = cur->next;
        hazard_pointer_thread_record_destroy(cur);
        cur = next;
    }
}

void hazard_pointer_thread_record_destroy(hazard_pointer_thread_record_t* hptr)
{
    if(hptr) {
        hazard_pointer_scan(hptr);//attempt to cleanup; best effort only here. really no threads should still be using these hazard pointers, so all should be freed
        free(hptr->plist);
    }
    free(hptr);
}

void hazard_pointer_using(hazard_pointer_thread_record_t* hptr, hazard_node_t* node, size_t n)
{
    assert(n < hptr->hazard_pointers_count);
    assert(!hptr->hazard_pointers[n]);
    hptr->hazard_pointers[n] = node;
    store_load_barrier();//make sure other processor's can see we're using this pointer
}

void hazard_pointer_done_using(hazard_pointer_thread_record_t* hptr, size_t n)
{
    assert(n < hptr->hazard_pointers_count);
    hptr->hazard_pointers[n] = 0;
}

void hazard_pointer_free(hazard_pointer_thread_record_t* hptr, hazard_node_t* node)
{
    //push the node to be fully freed later
    node->next = hptr->retired_list;
    hptr->retired_list = node;
    ++hptr->retired_count;
    if(hptr->retired_count >= hptr->retire_threshold) {
        hazard_pointer_scan(hptr);
    }
}

static inline int hazard_pointer_compare(const void* one, const void* two)
{
    return one - two;
}

static int binary_search(void** haystack, ssize_t haystack_size, void* needle)
{
    assert(haystack);
    if(!haystack_size) {
        return 0;
    }
    ssize_t start = 0;
    ssize_t end = haystack_size - 1;
    while(start <= end) {
        assert(start >= 0 && start < haystack_size);
        assert(end >= 0 && end < haystack_size);
        const ssize_t middle = (start + end) / 2;
        assert(middle >= 0 && middle < haystack_size);
        void* const middle_value = haystack[middle];
        if(middle_value > needle) {
            end = middle - 1;
        } else if(middle_value < needle) {
            start = middle + 1;
        } else {
            return 1;
        }
    }
    return 0;
}

void hazard_pointer_scan(hazard_pointer_thread_record_t* hptr)
{
    assert(hptr);
    //head always has a correct retired_threshold; that is, retired_threshold = 2 * N * K
    hazard_pointer_thread_record_t* const head = *hptr->head;
    assert(head);
    const size_t max_pointers = head->retire_threshold / 2;
    if(!hptr->plist || hptr->plist_size < max_pointers) {
        free(hptr->plist);
        hptr->plist_size = max_pointers;
        hptr->plist = (hazard_node_t**)malloc(max_pointers * sizeof(*hptr->plist));
    }

    size_t index = 0;
    hazard_pointer_thread_record_t* cur_record = head;
    size_t i;
    while(cur_record) {
        assert(index < max_pointers);
        const size_t hazard_pointers_count = cur_record->hazard_pointers_count;
        hazard_node_t** const hazard_pointers = &*cur_record->hazard_pointers;
        for(i = 0; i < hazard_pointers_count; ++i) {
            hazard_node_t* const h = hazard_pointers[i];
            if(h) {
                hptr->plist[index] = h;
                ++index;
            }
        }
        cur_record = cur_record->next;
    }

    qsort(hptr->plist, index, sizeof(*hptr->plist), &hazard_pointer_compare);

    hazard_node_t* node = hptr->retired_list;
    hptr->retired_list = NULL;
    hptr->retired_count = 0;

    while(node) {
        hazard_node_t* const next = node->next;

        const int is_hazardous = binary_search((void**)hptr->plist, index, node);
        
        if(is_hazardous) {
            node->next = hptr->retired_list;
            hptr->retired_list = node;
            ++hptr->retired_count;
        } else {
            hptr->garbage_collector.free_function(hptr->garbage_collector.user_data, node);
        }
        node = next;
    }
}

