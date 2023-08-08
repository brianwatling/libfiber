// SPDX-FileCopyrightText: 2012-2023 Brian Watling <brian@oxbo.dev>
// SPDX-License-Identifier: MIT

#ifndef _FIBER_BARRIER_H_
#define _FIBER_BARRIER_H_

#include "mpsc_fifo.h"

typedef struct fiber_barrier {
  uint32_t count;
  _Atomic uint64_t counter;
  mpsc_fifo_t waiters;
} fiber_barrier_t;

#define FIBER_BARRIER_SERIAL_FIBER (1)

#ifdef __cplusplus
extern "C" {
#endif

extern int fiber_barrier_init(fiber_barrier_t* barrier, uint32_t count);

extern void fiber_barrier_destroy(fiber_barrier_t* barrier);

extern int fiber_barrier_wait(fiber_barrier_t* barrier);

#ifdef __cplusplus
}
#endif

#endif
