#ifndef _HAZARD_POINTER_H_
#define _HAZARD_POINTER_H_

/*
    Author: Brian Watling
    Email: brianwatling@hotmail.com
    Website: https://github.com/brianwatling

    Notes: Based on "Hazard Pointers: Safe Memory Reclamation for Lock-Free Objects"
           by Maged M. Michael
*/

/*NOTES:
    -N threads participate
    -each thread needs K hazard pointers (determined by whatever algorithm requires hazard pointers, ie. "this lock-free queue requires up to 2 hazard pointers at any given time")
    -H = N * K = total hazard pointers required
    -retire threshold R = H + Omega(H) > 2 * H 
        -R > 2 * H gives amortized BigTheta(R) time for determining which retired nodes can be reused, if using O(1) lookup in scan()
        -up to H hazard pointers can be in use at any given time, therefore R > H means we'll get at least 1 (Omega(1)) hazard pointer each time we check for reuseable nodes
        -higher R means we'll reclaim more reuseable nodes per scan but keep nodes tied up longer
        -lower R means we'll scan more often but free nodes sooner
        -picking R > 2 * H means we'll free at least 0.5 R nodes per scan (hence BigTheta(R))
    -at any given time, up to a maximum of N * R retired nodes that cannot be reused
*/

#include <stddef.h>

typedef struct hazard_node
{
    struct hazard_node* next;
} hazard_node_t;

typedef void (*hazard_node_free_function)(void* user_data, hazard_node_t* node);

typedef struct hazard_node_gc
{
    void* user_data;
    hazard_node_free_function free_function;
} hazard_node_gc_t;

typedef struct hazard_pointer_thread_record
{
    struct hazard_pointer_thread_record* volatile * head;
    struct hazard_pointer_thread_record* next;
    size_t retire_threshold;
    size_t retired_count;
    hazard_node_t* retired_list;
    size_t plist_size;
    hazard_node_t** plist;//a scratch area used in scan(); it's here to avoid malloc()ing in each scan()
    size_t hazard_pointers_count;
    hazard_node_gc_t garbage_collector;
    hazard_node_t* hazard_pointers[];
} hazard_pointer_thread_record_t;

#ifdef __cplusplus
extern "C" {
#endif

//create a new record and fuse it into the list of records at 'head'
extern hazard_pointer_thread_record_t* hazard_pointer_thread_record_create_and_push(hazard_pointer_thread_record_t** head, size_t pointers_per_thread, hazard_node_gc_t garbage_collector);

extern void hazard_pointer_thread_record_destroy(hazard_pointer_thread_record_t* hptr);

//call this when you first grab an unsafe pointer
extern void hazard_pointer_using(hazard_pointer_thread_record_t* hptr, hazard_node_t* node, size_t n);

//call this when you're done with the pointer
extern void hazard_pointer_done_using(hazard_pointer_thread_record_t* hptr, size_t n);

//call this when an unsafe pointer should be cleaned up
extern void hazard_pointer_free(hazard_pointer_thread_record_t* hptr, hazard_node_t* node);

extern void hazard_pointer_scan(hazard_pointer_thread_record_t* hptr);

#ifdef __cplusplus
}
#endif

#endif

