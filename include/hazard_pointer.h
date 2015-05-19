/*
 * Copyright (c) 2012-2015, Brian Watling and other contributors
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

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
#include <assert.h>
#include "machine_specific.h"

struct hazard_node;

typedef void (*hazard_node_gc_function)(void* gc_data, struct hazard_node* node);

typedef struct hazard_node
{
    struct hazard_node* next;
    void* gc_data;
    hazard_node_gc_function gc_function;
} hazard_node_t;

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
    hazard_node_t* hazard_pointers[];
} hazard_pointer_thread_record_t;

#ifdef __cplusplus
extern "C" {
#endif

//create a new record and fuse it into the list of records at 'head'
extern hazard_pointer_thread_record_t* hazard_pointer_thread_record_create_and_push(hazard_pointer_thread_record_t** head, size_t pointers_per_thread);

extern void hazard_pointer_thread_record_destroy_all(hazard_pointer_thread_record_t* head);

extern void hazard_pointer_thread_record_destroy(hazard_pointer_thread_record_t* hptr);

//call this when you first grab an unsafe pointer. make sure to check it's still the pointer you want.
static inline void hazard_pointer_using(hazard_pointer_thread_record_t* hptr, hazard_node_t* node, size_t n)
{
    assert(n < hptr->hazard_pointers_count);
    hptr->hazard_pointers[n] = node;
    store_load_barrier();//make sure other processors can see we're using this pointer
}

//call this when you're done with the pointer
static inline void hazard_pointer_done_using(hazard_pointer_thread_record_t* hptr, size_t n)
{
    assert(n < hptr->hazard_pointers_count);
    hptr->hazard_pointers[n] = 0;
}

extern void hazard_pointer_scan(hazard_pointer_thread_record_t* hptr);

//call this when an unsafe pointer should be cleaned up
static inline void hazard_pointer_free(hazard_pointer_thread_record_t* hptr, hazard_node_t* node)
{
    //push the node to be fully freed later
    node->next = hptr->retired_list;
    hptr->retired_list = node;
    ++hptr->retired_count;
    if(hptr->retired_count >= hptr->retire_threshold) {
        hazard_pointer_scan(hptr);
    }
}

#ifdef __cplusplus
}
#endif

#endif

