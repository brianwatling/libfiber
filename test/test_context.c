#include <fiber_context.h>
#include "test_helper.h"

int value = 0;

void* switch_to(void* param)
{
    fiber_context_t* ctx = (fiber_context_t*)param;
    value = 1;
    fiber_swap_context(&ctx[1], &ctx[0]);
    return NULL;
}

int main()
{
    /*
        this test creates a coroutine and switches to it.
        the coroutine simply switches back and the program ends.
    */
    printf("testing fiber_context...\n");

    fiber_context_t ctx[2];

    test_assert(fiber_make_context_from_thread(&ctx[0]));
    test_assert(fiber_make_context(&ctx[1], 1024, &switch_to, ctx));

    fiber_swap_context(&ctx[0], &ctx[1]);

    test_assert(value);

    fiber_destroy_context(&ctx[1]);
    fiber_destroy_context(&ctx[0]);

    printf("SUCCESS\n");    
    return 0;
}

