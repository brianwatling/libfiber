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

#include "work_queue.h"

int work_queue_init(work_queue_t* wq)
{
    assert(wq);
    wq->in_count = 0;
    wq->out_count = 0;
    if(!mpsc_fifo_init(&wq->fifo)) {
        return 0;
    }
    return 1;
}

void work_queue_destroy(work_queue_t* wq)
{
    if(wq) {
        mpsc_fifo_destroy(&wq->fifo);
    }
}

int work_queue_push(work_queue_t* wq, work_queue_item_t* item)
{
    assert(wq);
    assert(item);
    int ret = WORK_QUEUE_QUEUED;
    const int64_t in_count = __sync_add_and_fetch(&wq->in_count, 1);
    if(in_count == 1) {
        //we got here first; we'll be the worker
        ret = WORK_QUEUE_START_WORKING;
    }
    mpsc_fifo_push(&wq->fifo, item);
    return ret;
}

int work_queue_get_work(work_queue_t* wq, work_queue_item_t** out)
{
    assert(wq);
    assert(out);
    while(!(*out = mpsc_fifo_trypop(&wq->fifo))) {
        if(wq->out_count == wq->in_count) {
            const int64_t old_out_count = wq->out_count;
            wq->out_count = 0;
            const int64_t new_in_count = __sync_sub_and_fetch(&wq->in_count, old_out_count);
            if(new_in_count == 0) {
                return WORK_QUEUE_EMPTY;
            }
        }
        cpu_relax();//another thread has pushed work, but hasn't finished pushing to the mpsc_fifo
    }
    wq->out_count += 1;
    return WORK_QUEUE_MORE_WORK;
}

