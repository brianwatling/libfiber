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

#ifndef _FIBER_FIBER_H_
#define _FIBER_FIBER_H_

#include <stdint.h>
#include "fiber_context.h"
#include "mpsc_fifo.h"

typedef int fiber_state_t;

struct fiber_manager;

#define FIBER_STATE_RUNNING (1)
#define FIBER_STATE_READY (2)
#define FIBER_STATE_WAITING (3)
#define FIBER_STATE_DONE (4)
#define FIBER_STATE_SAVING_STATE_TO_WAIT (5)

#define FIBER_DETACH_NONE (0)
#define FIBER_DETACH_WAIT_FOR_JOINER (1)
#define FIBER_DETACH_WAIT_TO_JOIN (2)
#define FIBER_DETACH_DETACHED (3)

typedef struct fiber
{
    volatile fiber_state_t state;
    fiber_run_function_t run_function;
    void* param;
    uint64_t volatile id;/* not unique globally, only within this fiber instance. used for joining */
    fiber_context_t context;
    void* volatile result;
    mpsc_fifo_node_t* volatile mpsc_fifo_node;
    int volatile detach_state;
    struct fiber* volatile join_info;
    void* volatile scratch;//to be used by internal fiber mechanisms. be sure mechanisms do not conflict! (ie. only use scratch while a fiber is sleeping/waiting)
} fiber_t;

#ifdef __cplusplus
extern "C" {
#endif

#define FIBER_DEFAULT_STACK_SIZE (102400)
#define FIBER_MIN_STACK_SIZE (1024)

extern fiber_t* fiber_create(size_t stack_size, fiber_run_function_t run, void* param);

extern fiber_t* fiber_create_no_sched(size_t stack_size, fiber_run_function_t run, void* param);

extern fiber_t* fiber_create_from_thread();

extern int fiber_join(fiber_t* f, void** result);

extern int fiber_tryjoin(fiber_t* f, void** result);

extern int fiber_yield();

extern int fiber_detach(fiber_t* f);

#ifdef __cplusplus
}
#endif

#endif

