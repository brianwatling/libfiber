#ifndef _FIBER_TEST_HELPER_H_
#define _FIBER_TEST_HELPER_H_

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#define test_assert(expr) \
    do { \
        if(!(expr)) { \
            fprintf(stderr, "%s:%d TEST FAILED: %s\n", __FILE__, __LINE__, #expr);\
            *(int*)0 = 0; \
        } \
    } while(0)

#endif

