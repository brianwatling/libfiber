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

#ifndef _FIBER_SCHEDULER_H_
#define _FIBER_SCHEDULER_H_

#include "fiber.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void* fiber_scheduler_t;

int fiber_scheduler_init(size_t num_threads);

fiber_scheduler_t* fiber_scheduler_for_thread(size_t thread_id);

void fiber_scheduler_schedule(fiber_scheduler_t* scheduler, fiber_t* the_fiber);

fiber_t* fiber_scheduler_next(fiber_scheduler_t* scheduler);

void fiber_scheduler_load_balance(fiber_scheduler_t* scheduler);

void fiber_scheduler_stats(fiber_scheduler_t* scheduler, uint64_t* steal_count, uint64_t* failed_steal_count);

#ifdef __cplusplus
}
#endif

#endif

