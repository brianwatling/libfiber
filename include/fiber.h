#ifndef _FIBER_FIBER_H_
#define _FIBER_FIBER_H_

#include <stdint.h>
#include "fiber_context.h"

typedef int fiber_state_t;

#define FIBER_STATE_RUNNING (1)
#define FIBER_STATE_WAITING (2)
#define FIBER_STATE_DONE (3)

typedef struct fiber
{
    volatile fiber_state_t state;
    fiber_run_function_t run_function;
    void* param;
    uint64_t volatile id;/* not unique globally, only within this fiber instance. used for joining */
    fiber_context_t context;
} fiber_t;

#ifdef __cplusplus
extern "C" {
#endif

extern fiber_t* fiber_create(size_t stack_size, fiber_run_function_t run, void* param);

extern fiber_t* fiber_create_from_thread();

extern void fiber_join(fiber_t* f);

extern void fiber_yield();

#ifdef __cplusplus
}
#endif

#endif

