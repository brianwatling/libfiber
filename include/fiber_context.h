#ifndef _FIBER_CONTEXT_H_
#define _FIBER_CONTEXT_H_

#include <stddef.h>

#define FIBER_ERROR 0
#define FIBER_SUCCESS 1

typedef void* (*fiber_run_function_t)(void*);

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
} fiber_context_t;

#ifdef __cplusplus
extern "C" {
#endif

extern int fiber_make_context(fiber_context_t* context, size_t stack_size, fiber_run_function_t run_function, void* param);

extern int fiber_make_context_from_thread(fiber_context_t* context);

extern void fiber_swap_context(fiber_context_t* from_context, fiber_context_t* to_context);

extern void fiber_destroy_context(fiber_context_t* context);

#ifdef __cplusplus
}
#endif

#endif

