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

#include "fiber_manager.h"
#include "test_helper.h"

void* run_function(void* param)
{
    int* value = (int*)param;
    *value += 1;
    fiber_yield();
    *value += 1;
    return NULL;
}

int main()
{
    int y = 1;
    int z = atomic_exchange_int(&y, 2);
    test_assert(z == 1);
    test_assert(y == 2);

    fiber_manager_init(1);
    int volatile value = 0;
    fiber_t* fiber1 = fiber_create(20000, &run_function, (void*)&value);

    fiber_yield();
    test_assert(value == 1);
    fiber_join(fiber1, NULL);
    test_assert(value == 2);

    fiber_t* fiber2 = fiber_create(20000, &run_function, (void*)&value);

    fiber_yield();
    test_assert(value == 3);
    fiber_yield();
    test_assert(value == 4);

    //now the fiber has finished, but joining fiber2 should still be fine
    fiber_join(fiber2, NULL);

    //let fiber 2 do its maintenance (it needs to set state = DONE after we've joined it)
    fiber_yield();

    //at this point fiber2 should be finished.
    test_assert(fiber2->state == FIBER_STATE_DONE);

    fiber_manager_print_stats();
    return 0;
}

