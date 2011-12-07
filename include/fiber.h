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
    mpsc_node_t* volatile mpsc_node;
    int volatile detach_state;
    struct fiber* volatile join_info;
    struct fiber* volatile next;
} fiber_t;

#ifdef __cplusplus
extern "C" {
#endif

#define FIBER_DEFAULT_STACK_SIZE 102400
#define FIBER_MIN_STACK_SIZE 1024

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

