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

#include <fiber_context.h>
#include "test_helper.h"

int value = 0;
fiber_context_t* expected = NULL;

void* switch_to(void* param)
{
    fiber_context_t* ctx = (fiber_context_t*)param;
    test_assert(expected == ctx);
    value = 1;
    fiber_context_swap(&ctx[1], &ctx[0]);
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
    expected = &ctx[0];

    test_assert(fiber_context_init_from_thread(&ctx[0]));
    test_assert(fiber_context_init(&ctx[1], 1024, &switch_to, ctx));

    fiber_context_swap(&ctx[0], &ctx[1]);

    test_assert(value);

    fiber_context_destroy(&ctx[1]);
    fiber_context_destroy(&ctx[0]);

    printf("SUCCESS\n");
    return 0;
}

