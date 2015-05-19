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
#include <alloca.h>

#define NUM_THREADS 1
#define NUM_FIBERS 1

int COUNT = 10;

//this test is specifically designed to use up a bunch of stack space. compile without optimizations.
//run it like this:
//
//strace ./bin/test_split_stack 2000 2>&1 | grep mmap | wc -l
//2023
int recursive_function(int next)
{
    int buffer[1024];
    memset(buffer, 0, sizeof(buffer));
    if(next <= 0) {
        return 1;
    }
    return recursive_function(next - 1) + 1;
}

void* run_function(void* param)
{
    printf("%d\n", recursive_function(COUNT));
    return NULL;
}

int main(int argc, char* argv[])
{
    if(argc > 1) {
        COUNT = atoi(argv[1]);
    }

    fiber_manager_init(NUM_THREADS);

    fiber_t* fibers[NUM_FIBERS];
    int i;
    for(i = 0; i < NUM_FIBERS; ++i) {
        fibers[i] = fiber_create(1024, &run_function, NULL);
    }

    for(i = 0; i < NUM_FIBERS; ++i) {
        fiber_join(fibers[i], NULL);
    }

    fiber_manager_print_stats();
    return 0;
}

