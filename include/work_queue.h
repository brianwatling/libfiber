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

#ifndef _WORK_QUEUE_H_
#define _WORK_QUEUE_H_

#include "mpsc_fifo.h"

typedef struct work_queue
{
    mpsc_fifo_t fifo;
    volatile int64_t in_count;
    char _cache_padding1[CACHE_SIZE - sizeof(int64_t)];
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

//the work queue owns item after pushing
//WORK_QUEUE_START_WORKING is returned if the caller should begin working on the queued items. the caller should call get_work() until WORK_QUEUE_EMPTY is returned.
//WORK_QUEUE_QUEUED is returned is the work item is queued and will be processed by another thread.
int work_queue_push(work_queue_t* wq, work_queue_item_t* item);

//caller owns *out afterwards.
//returns WORK_QUEUE_MORE_WORK until the work queue is empty.
//WORK_QUEUE_EMPTY is returned when the queue is empty.
int work_queue_get_work(work_queue_t* wq, work_queue_item_t** out);

#ifdef __cplusplus
}
#endif

#endif

