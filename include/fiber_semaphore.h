// SPDX-FileCopyrightText: 2012-2023 Brian Watling <brian@oxbo.dev>
// SPDX-License-Identifier: MIT

#ifndef _FIBER_SEMAPHORE_H_
#define _FIBER_SEMAPHORE_H_

#include "mpmc_fifo.h"

typedef struct fiber_semaphore {
  volatile int counter;
  mpmc_fifo_t waiters;
} fiber_semaphore_t;

#ifdef __cplusplus
extern "C" {
#endif

extern int fiber_semaphore_init(fiber_semaphore_t* semaphore, int value);

extern int fiber_semaphore_destroy(fiber_semaphore_t* semaphore);

extern int fiber_semaphore_wait(fiber_semaphore_t* semaphore);

extern int fiber_semaphore_trywait(fiber_semaphore_t* semaphore);

extern int fiber_semaphore_post(fiber_semaphore_t* semaphore);

extern int fiber_semaphore_getvalue(fiber_semaphore_t* semaphore);

#ifdef __cplusplus
}
#endif

#endif
