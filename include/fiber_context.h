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

#ifndef _FIBER_CONTEXT_H_
#define _FIBER_CONTEXT_H_

#include <stddef.h>

#define FIBER_ERROR (0)
#define FIBER_SUCCESS (1)

typedef void* (*fiber_run_function_t)(void*);

#ifdef FIBER_STACK_SPLIT
typedef void* splitstack_context_t[10];
#endif

typedef struct fiber_context
{
    void* ctx_stack;
    size_t ctx_stack_size;
  /* The void** type makes sense because it we just want a pointer to
     the memory location (in the fiber's stack) where the stackpointer
     is saved (in the fiber's stack). */
    void** ctx_stack_pointer;
    unsigned int ctx_stack_id;
    int is_thread;
#ifdef FIBER_STACK_SPLIT
    splitstack_context_t splitstack_context;
#endif
} fiber_context_t;

#ifdef __cplusplus
extern "C" {
#endif

extern int fiber_context_init(fiber_context_t* context, size_t stack_size, fiber_run_function_t run_function, void* param);

extern int fiber_context_init_from_thread(fiber_context_t* context);

extern void fiber_context_swap(fiber_context_t* from_context, fiber_context_t* to_context);

extern void fiber_context_destroy(fiber_context_t* context);

#ifdef __cplusplus
}
#endif

#endif

