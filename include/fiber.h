#ifndef _FIBER_FIBER_H_
#define _FIBER_FIBER_H_

#include "fiber_context.h"

typedef int fiber_state_t;

#define FIBER_STATE_NONE (0)
#define FIBER_STATE_RUNNING (1)
#define FIBER_STATE_WAITING (2)
#define FIBER_STATE_DONE (3)

typedef struct fiber
{
    volatile fiber_state_t state;
    struct fiber* volatile next;
    fiber_run_function_t run_function;
    void* param;
    fiber_context_t context;
} fiber_t;

#ifdef __cplusplus
extern "C" {
#endif

extern int fiber_create(fiber_t* dest, size_t stack_size, fiber_run_function_t run, void* param);

extern int fiber_create_from_thread(fiber_t* dest);

extern void fiber_destroy(fiber_t* f);

extern void fiber_join(fiber_t* f);

#ifdef __cplusplus
}
#endif

#endif

