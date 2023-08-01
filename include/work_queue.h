// SPDX-FileCopyrightText: 2012-2023 Brian Watling <brian@oxbo.dev>
// SPDX-License-Identifier: MIT

#ifndef _WORK_QUEUE_H_
#define _WORK_QUEUE_H_

#include "mpsc_fifo.h"

typedef struct work_queue {
  mpsc_fifo_t fifo;
  volatile int64_t in_count;
  char _cache_padding1[FIBER_CACHELINE_SIZE - sizeof(int64_t)];
  volatile int64_t out_count;
} work_queue_t;

typedef mpsc_fifo_node_t work_queue_item_t;

#define WORK_QUEUE_START_WORKING (1)
#define WORK_QUEUE_QUEUED (0)

#define WORK_QUEUE_MORE_WORK (1)
#define WORK_QUEUE_EMPTY (0)

#ifdef __cplusplus
extern "C" {
#endif

int work_queue_init(work_queue_t* wq);

void work_queue_destroy(work_queue_t* wq);

// the work queue owns item after pushing
// WORK_QUEUE_START_WORKING is returned if the caller should begin working on
// the queued items. the caller should call get_work() until WORK_QUEUE_EMPTY is
// returned. WORK_QUEUE_QUEUED is returned is the work item is queued and will
// be processed by another thread.
int work_queue_push(work_queue_t* wq, work_queue_item_t* item);

// caller owns *out afterwards.
// returns WORK_QUEUE_MORE_WORK until the work queue is empty.
// WORK_QUEUE_EMPTY is returned when the queue is empty.
int work_queue_get_work(work_queue_t* wq, work_queue_item_t** out);

#ifdef __cplusplus
}
#endif

#endif
