#ifndef _FIBER_MANAGER_H_
#define _FIBER_MANAGER_H_

#include "fiber.h"

typedef struct fiber_manager
{
    fiber_t* volatile current_fiber;
    fiber_t thread_fiber;
} fiber_manager_t;

#ifdef __cplusplus
extern "C" {
#endif

extern int fiber_manager_create(fiber_manager_t** manager);

extern void fiber_manager_destroy(fiber_manager_t* manager);

extern void fiber_manager_schedule(fiber_manager_t* manager, fiber_t* the_fiber);

extern void fiber_manager_yield(fiber_manager_t* manager);

extern fiber_manager_t* fiber_manager_get();

#ifdef __cplusplus
}
#endif

#endif

