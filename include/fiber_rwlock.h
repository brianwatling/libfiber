// SPDX-FileCopyrightText: 2012-2023 Brian Watling <brian@oxbo.dev>
// SPDX-License-Identifier: MIT

#ifndef _FIBER_RWLOCK_H_
#define _FIBER_RWLOCK_H_

#include "mpsc_fifo.h"

// 21 bits counters; supports up to roughly 2 million readers or writers
typedef union {
  struct {
    unsigned int write_locked : 1;
    unsigned int reader_count : 21;
    unsigned int waiting_readers : 21;
    unsigned int waiting_writers : 21;
  } __attribute__((packed)) state;
  uint64_t blob;
} __attribute__((packed)) fiber_rwlock_state_t;

typedef struct fiber_rwlock {
  fiber_rwlock_state_t state;
  mpsc_fifo_t write_waiters;
  mpsc_fifo_t read_waiters;
} fiber_rwlock_t;

#ifdef __cplusplus
extern "C" {
#endif

extern int fiber_rwlock_init(fiber_rwlock_t* rwlock);

extern void fiber_rwlock_destroy(fiber_rwlock_t* rwlock);

extern int fiber_rwlock_rdlock(fiber_rwlock_t* rwlock);

extern int fiber_rwlock_wrlock(fiber_rwlock_t* rwlock);

extern int fiber_rwlock_tryrdlock(fiber_rwlock_t* rwlock);

extern int fiber_rwlock_trywrlock(fiber_rwlock_t* rwlock);

extern int fiber_rwlock_rdunlock(fiber_rwlock_t* rwlock);

extern int fiber_rwlock_wrunlock(fiber_rwlock_t* rwlock);

#ifdef __cplusplus
}
#endif

#endif
