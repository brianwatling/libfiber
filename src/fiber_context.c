#include "fiber_context.h"
#include "machine_specific.h"
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <string.h>
#ifdef FIBER_CONTEXT_MALLOC
#include <stdlib.h>
#endif

#ifdef USE_VALGRIND
#include <valgrind/valgrind.h>

#define STACK_REGISTER(context, stack_location, stack_size)                                                     \
    do {                                                                                                        \
        (context)->ctx_stack_id = VALGRIND_STACK_REGISTER(stack_location, (char*) stack_location + stack_size); \
    } while(0)
#define STACK_DEREGISTER(context)                              \
    do {                                                       \
        VALGRIND_STACK_DEREGISTER((context)->ctx_stack_id); \
    } while(0)
#else
#define STACK_REGISTER(context, stackLoc, stackSize) do {} while(0)
#define STACK_DEREGISTER(context) do {} while(0)
#endif //USE_VALGRIND

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

static long fiberPageSize = 0;
static size_t fiber_round_to_page_size(size_t size)
{
    if(!fiberPageSize) {
        fiberPageSize = sysconf(_SC_PAGESIZE);
        fiberPageSize -= 50;//account for overhead for page info strucures (i don't know the actual size, this is a guess)
    }
    //minimum of 2 pages, we'll use one as a sentinel
    const size_t numPages = size / fiberPageSize + 1;
    const size_t numPagesAfterMin = numPages >= 2 ? numPages : 2;
    return fiberPageSize * numPagesAfterMin;
}

static void* fiber_alloc_stack(size_t stack_size)
{
#ifdef FIBER_CONTEXT_MALLOC
    void* const ret = malloc(stack_size);
    if(!ret) {
        errno = ENOMEM;
    }
#else
    void* const ret = mmap(0, stack_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if(ret == MAP_FAILED) {
        errno = ENOMEM;
        return NULL;
    }

    if(mprotect(ret, 1, PROT_NONE)) {
        munmap(ret, stack_size);
        return NULL;
    }
#endif
    return ret;
}

static void fiber_free_stack(void* stack_ptr, size_t stack_size)
{
#ifdef FIBER_CONTEXT_MALLOC
    free(stack_ptr);
#else
    munmap(stack_ptr, stack_size);
#endif
}

#if defined(__GNUC__) && defined(__i386__) && defined(FIBER_FAST_SWITCHING)

int fiber_make_context(fiber_context_t* context, size_t stack_size, fiber_run_function_t run_function, void* param)
{
    if(!context || !stack_size || !run_function) {
        errno = EINVAL;
        return FIBER_ERROR;
    }

    context->ctx_stack_size = fiber_round_to_page_size(stack_size);
    context->ctx_stack = fiber_alloc_stack(context->ctx_stack_size);
    if(!context->ctx_stack) {
        return FIBER_ERROR;
    }

    context->ctx_stack_pointer = (void**)((char*)context->ctx_stack + context->ctx_stack_size) - 1;
    *--context->ctx_stack_pointer = param;
    *--context->ctx_stack_pointer = NULL; /*dummy return address*/
    *--context->ctx_stack_pointer = (void*)run_function;

    STACK_REGISTER(context, context->ctx_stack, context->ctx_stack_size);

    context->is_thread = 0;
    return FIBER_SUCCESS;
}

int fiber_make_context_from_thread(fiber_context_t* context)
{
    if(!context) {
        errno = EINVAL;
        return FIBER_ERROR;
    }
    memset(context, 0, sizeof(*context));
    context->is_thread = 1;
    return FIBER_SUCCESS;
}

void fiber_destroy_context(fiber_context_t* context)
{
    if(context && !context->is_thread) {
        STACK_DEREGISTER(context);
        fiber_free_stack(context->ctx_stack, context->ctx_stack_size);
    }
}

void fiber_swap_context(fiber_context_t* from_context, fiber_context_t* to_context)
{
    assert(from_context);
    assert(to_context);

    void*** const from_sp = &from_context->ctx_stack_pointer;
    void** const to_sp = to_context->ctx_stack_pointer;

    /*
        Here the machine context is saved on the stack.
        For the current fiber:
            1. ebp and the new program counter are saved to the stack (the new program counter
               will be used when the fiber resumes)
            2. the current fiber's stack pointer is saved into the 'from' context
        For the fiber to be resumed:
            1. the stack pointer is retrieved from the 'to' context. the stack is now
               switched.
            2. the new program counter is popped off the stack and jumped to. the 'to' context
               is now executing
            3. ebp is restored

        NOTES: this switch implementation works seemlessly with new contexts and
               existing contexts

        Credits:
            1. the original context switch was taken from Boost Coroutine, written as
               a Google Summer of Code project, Copyright 2006 Giovanni P. Deretta
               (http://www.crystalclearsoftware.com/soc/coroutine/). This work is subject
               to the Boost Software Licence Version 1.0 as copied below.
            2. Michael Ploujnikov for reducing the amount of context saved to the stack


        Licensing info for x86 context switch:

        Boost Software License - Version 1.0 - August 17th, 2003

        Permission is hereby granted, free of charge, to any person or organization
        obtaining a copy of the software and accompanying documentation covered by
        this license (the "Software") to use, reproduce, display, distribute,
        execute, and transmit the Software, and to prepare derivative works of the
        Software, and to permit third-parties to whom the Software is furnished to
        do so, all subject to the following:

        The copyright notices in the Software and this entire statement, including
        the above license grant, this restriction and the following disclaimer,
        must be included in all copies of the Software, in whole or in part, and
        all derivative works of the Software, unless such copies or derivative
        works are solely in the form of machine-executable object code generated by
        a source language processor.

        THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
        IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
        FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
        SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
        FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
        ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
        DEALINGS IN THE SOFTWARE.
    */

    __builtin_prefetch (to_sp, 1, 3);
    __builtin_prefetch (to_sp, 0, 3);
    __builtin_prefetch (to_sp+64/4, 1, 3);
    __builtin_prefetch (to_sp+64/4, 0, 3);
    __builtin_prefetch (to_sp+32/4, 1, 3);
    __builtin_prefetch (to_sp+32/4, 0, 3);
    __builtin_prefetch (to_sp-32/4, 1, 3);
    __builtin_prefetch (to_sp-32/4, 0, 3);
    __builtin_prefetch (to_sp-64/4, 1, 3);
    __builtin_prefetch (to_sp-64/4, 0, 3);

    __asm__ volatile
    ("\n\t pushl %%ebp"
     "\n\t pushl $0f "
     "\n\t movl %%esp, (%[from])"
     "\n\t movl %[to], %%esp"
     "\n\t popl %%ecx"
     "\n\t jmp  *%%ecx" //Future Optimization: if ecx is label '0', no need for the jmp (ie. if the fiber is alread started)
     "\n0:\t popl %%ebp"
     :
     : [from] "a" (from_sp),
       [to]   "d" (to_sp)
     : "cc",
     "%ecx",
#ifndef SHARED_LIB
     "%ebx",
#endif
     "%edi",
     "%esi",
     "%st", "%st(1)", "%st(2)", "%st(3)", "%st(4)", "%st(5)", "%st(6)", "%st(7)",
     "memory"
    );

    /* make any pending writes available to other processors */
    store_load_barrier();
}

#elif defined(__x86_64__) && defined(FIBER_FAST_SWITCHING)
#include <stdlib.h>

int fiber_make_context(fiber_context_t* context, size_t stack_size, fiber_run_function_t run_function, void* param)
{
    if(!context || !stack_size || !run_function) {
        errno = EINVAL;
        return FIBER_ERROR;
    }

    context->ctx_stack_size = fiber_round_to_page_size(stack_size);
    context->ctx_stack = fiber_alloc_stack(context->ctx_stack_size);
    if(!context->ctx_stack) {
        return FIBER_ERROR;
    }

    context->ctx_stack_pointer = (void**)((char*)context->ctx_stack + context->ctx_stack_size) - 1;
    *--context->ctx_stack_pointer = param;
    *--context->ctx_stack_pointer = NULL; /*dummy return address*/
    *--context->ctx_stack_pointer = (void*)run_function;
    *--context->ctx_stack_pointer = 0;// rbp
    *--context->ctx_stack_pointer = 0;// rbx
    *--context->ctx_stack_pointer = 0;// r12
    *--context->ctx_stack_pointer = 0;// r13
    *--context->ctx_stack_pointer = 0;// r14
    *--context->ctx_stack_pointer = 0;// r15

    STACK_REGISTER(context, context->ctx_stack, context->ctx_stack_size);

    context->is_thread = 0;
    return FIBER_SUCCESS;
}

int fiber_make_context_from_thread(fiber_context_t* context)
{
    if(!context) {
        errno = EINVAL;
        return FIBER_ERROR;
    }
    memset(context, 0, sizeof(*context));
    context->is_thread = 1;
    return FIBER_SUCCESS;
}

void fiber_destroy_context(fiber_context_t* context)
{
    if(context && !context->is_thread) {
        STACK_DEREGISTER(context);
        fiber_free_stack(context->ctx_stack, context->ctx_stack_size);
    }
}

void fiber_swap_context(fiber_context_t* from_context, fiber_context_t* to_context)
{
    assert(from_context);
    assert(to_context);
    void*** const from_sp = &from_context->ctx_stack_pointer;
    void** const to_sp = to_context->ctx_stack_pointer;

    /*
        Here the machine context is saved on the stack.

        A fiber's stack looks like this while it's not running (stop grows down here to match memory):
            param (new contexts only)
            return address (dummy, new contexts only)
            rip (after resuming)
            rbp
            rbx
            r12
            r13
            r14
            r15

        For the current fiber:
            1. calculate the rip for the resume (leaq 0f(%%rip), %%rax -> load address of the label '0' into rax)
            2. save new rip (rax), rbp, rbx, r12, r13, r14, r15
            3. the current fiber's stack pointer is saved into the 'from' context
        For the fiber to be resumed:
            1. rip is fetched off the stack early (so it's available when it's time to jump)
            2. the stack pointer is retrieved from the 'to' context. the stack is now
               switched.
            3. restore r15, r14, r13, r12, rbx, rbp for the 'to' fiber
            4. load 'param' into rdi (needed for new contexts). note that rdi is the first parameter to a function
            5. correct the stack pointer (ie. counter the 'push rax', without overwriting rip)
            6. jump to the new program counter

        NOTES: this switch implementation works seemlessly with new contexts and
               existing contexts

        Credits:
            1. a good chunk of the context switch logic/algorithm was taken from Boost Coroutine, written as
               a Google Summer of Code project, Copyright 2006 Giovanni P. Deretta
               (http://www.crystalclearsoftware.com/soc/coroutine/). This work is subject
               to the Boost Software Licence Version 1.0 as copied below.
            2. the boost coroutine switch does not write rip as far as I can tell - I added
               the leaq 0f(%%rip), %%rax / push rax logic (Brian Watling)
            3. the boost coroutine switch does not save r12 through r15, and needlessly saves
               rax and rdx - i corrected this (Brian Watling)


        Licensing info for x86_64 context switch:

        Boost Software License - Version 1.0 - August 17th, 2003

        Permission is hereby granted, free of charge, to any person or organization
        obtaining a copy of the software and accompanying documentation covered by
        this license (the "Software") to use, reproduce, display, distribute,
        execute, and transmit the Software, and to prepare derivative works of the
        Software, and to permit third-parties to whom the Software is furnished to
        do so, all subject to the following:

        The copyright notices in the Software and this entire statement, including
        the above license grant, this restriction and the following disclaimer,
        must be included in all copies of the Software, in whole or in part, and
        all derivative works of the Software, unless such copies or derivative
        works are solely in the form of machine-executable object code generated by
        a source language processor.

        THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
        IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
        FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
        SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
        FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
        ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
        DEALINGS IN THE SOFTWARE.
    */

    __builtin_prefetch ((void**)to_sp, 1, 3);
    __builtin_prefetch ((void**)to_sp, 0, 3);
    __builtin_prefetch ((void**)to_sp+64/4, 1, 3);
    __builtin_prefetch ((void**)to_sp+64/4, 0, 3);
    __builtin_prefetch ((void**)to_sp+32/4, 1, 3);
    __builtin_prefetch ((void**)to_sp+32/4, 0, 3);
    __builtin_prefetch ((void**)to_sp-32/4, 1, 3);
    __builtin_prefetch ((void**)to_sp-32/4, 0, 3);
    __builtin_prefetch ((void**)to_sp-64/4, 1, 3);
    __builtin_prefetch ((void**)to_sp-64/4, 0, 3);

    __asm__ volatile
    (
        "leaq 0f(%%rip), %%rax\n\t"
        "movq  48(%[to]), %%rcx\n\t" //quickly grab the new rip for 'to'
        "pushq %%rax\n\t" //save a new rip for after the fiber is resumed
        "pushq %%rbp\n\t"
        "pushq %%rbx\n\t"
        "pushq %%r12\n\t"
        "pushq %%r13\n\t"
        "pushq %%r14\n\t"
        "pushq %%r15\n\t"
        "movq  %%rsp, (%[from])\n\t"
        "movq  %[to], %%rsp\n\t"
        "popq  %%r15\n\t"
        "popq  %%r14\n\t"
        "popq  %%r13\n\t"
        "popq  %%r12\n\t"
        "popq  %%rbx\n\t"
        "popq  %%rbp\n\t"
        "movq  64(%[to]), %%rdi\n\t" //Future Optimization: no need to load this if the fiber has already been started
        "add   $8, %%rsp\n\t"
        "jmp   *%%rcx\n\t" //Future Optimization: if rcx is label '0', no need for the jmp (ie. if the fiber is alread started)
        "0:\n\t"
        :
        : [from] "D" (from_sp),
          [to]   "S" (to_sp)
        : "cc",
          "memory"
    );

    /* make any pending writes available to other processors */
    store_load_barrier();
}

#else
#define _XOPEN_SOURCE
#include <ucontext.h>
#include <stdlib.h>

int fiber_make_context(fiber_context_t* context, size_t stack_size, fiber_run_function_t run_function, void* param)
{
    if(stack_size < MINSIGSTKSZ)
        stack_size = MINSIGSTKSZ;

    context->ctx_stack_size = fiber_round_to_page_size(stack_size);

    context->ctx_stack_pointer = malloc(sizeof(ucontext_t));
    if(!context->ctx_stack_pointer) {
        errno = ENOMEM;
        return FIBER_ERROR;
    }
    ucontext_t* const uctx = (ucontext_t*)context->ctx_stack_pointer;
    getcontext(uctx);
    uctx->uc_link = 0;

    context->ctx_stack = fiber_alloc_stack(context->ctx_stack_size);
    if(!context->ctx_stack) {
        free(context->ctx_stack_pointer);
        return FIBER_ERROR;
    }

    uctx->uc_stack.ss_sp = (int*)context->ctx_stack;
    uctx->uc_stack.ss_size = context->ctx_stack_size;
    uctx->uc_stack.ss_flags = 0;
    makecontext(uctx, (void (*)())run_function, 1, param);

    STACK_REGISTER(context, uctx->uc_stack.ss_sp, context->ctx_stack_size);

    context->is_thread = 0;
    return FIBER_SUCCESS;
}

int fiber_make_context_from_thread(fiber_context_t* context)
{
    if(!context) {
        errno = EINVAL;
        return FIBER_ERROR;
    }
    memset(context, 0, sizeof(*context));
    context->ctx_stack_pointer = malloc(sizeof(ucontext_t));
    if(!context->ctx_stack_pointer) {
        errno = ENOMEM;
        return FIBER_ERROR;
    }
    getcontext((ucontext_t*)context->ctx_stack_pointer);
    context->is_thread = 1;
    return FIBER_SUCCESS;
}

void fiber_destroy_context(fiber_context_t* context)
{
    if(!context) {
        return;
    }
    if(!context->is_thread) {
        free(context->ctx_stack_pointer);
        STACK_DEREGISTER(context);
        fiber_free_stack(context->ctx_stack, context->ctx_stack_size);
    } else {
        /* this context was created from a thread */
        free(context->ctx_stack_pointer);
    }
}

void fiber_swap_context(fiber_context_t* from_context, fiber_context_t* to_context)
{
    swapcontext((ucontext_t*)from_context->ctx_stack_pointer, (ucontext_t*)to_context->ctx_stack_pointer);

    /* make any pending writes available to other processors */
    store_load_barrier();
}

#endif

