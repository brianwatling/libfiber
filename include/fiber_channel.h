// SPDX-FileCopyrightText: 2012-2023 Brian Watling <brian@oxbo.dev>
// SPDX-License-Identifier: MIT

#ifndef _FIBER_CHANNEL_H_
#define _FIBER_CHANNEL_H_

#include <assert.h>
#include <malloc.h>
#include <stddef.h>
#include <stdint.h>

#include "fiber_manager.h"
#include "fiber_signal.h"
#include "machine_specific.h"
#include "mpsc_fifo.h"
#include "spsc_fifo.h"

// a bounded channel. send will spin-loop and receive will block. there can be
// many senders but only one receiver
typedef struct fiber_bounded_channel {
  // putting high and low on separate cache lines provides a slight performance
  // increase
  _Atomic uint64_t high;
  char _cache_padding1[FIBER_CACHELINE_SIZE - sizeof(uint64_t)];
  _Atomic uint64_t low;
  char _cache_padding2[FIBER_CACHELINE_SIZE - sizeof(uint64_t)];
  mpsc_fifo_t waiters;
  fiber_signal_t* ready_signal;
  uint32_t size;
  uint32_t power_of_2_mod;
  void* buffer[];
} fiber_bounded_channel_t;

// specifying a NULL signal means this channel will spin. 'signal' must out-live
// this channel
static inline fiber_bounded_channel_t* fiber_bounded_channel_create(
    uint32_t power_of_2_size, fiber_signal_t* signal) {
  assert(power_of_2_size && power_of_2_size < 32);
  const uint32_t size = 1 << power_of_2_size;
  const uint32_t required_size =
      sizeof(fiber_bounded_channel_t) + size * sizeof(void*);
  fiber_bounded_channel_t* const channel =
      (fiber_bounded_channel_t*)calloc(1, required_size);
  if (channel) {
    channel->size = size;
    channel->power_of_2_mod = size - 1;
    channel->ready_signal = signal;
    if (!mpsc_fifo_init(&channel->waiters)) {
      free(channel);
      return 0;
    }
  }
  return channel;
}

static inline void fiber_bounded_channel_destroy(
    fiber_bounded_channel_t* channel) {
  if (channel) {
    mpsc_fifo_destroy(&channel->waiters);
    free(channel);
  }
}

// returns 1 if a fiber was scheduled
static inline int fiber_bounded_channel_send(fiber_bounded_channel_t* channel,
                                             void* message) {
  assert(channel);
  assert(message);  // can't store NULLs; we rely on a NULL to indicate a spot
                    // in the buffer has not been written yet

  while (1) {
    // read low first; this means the buffer will appear
    // larger or equal to its actual size
    const uint64_t low =
        atomic_load_explicit(&channel->low, memory_order_acquire);

    uint64_t high = atomic_load_explicit(&channel->high, memory_order_acquire);
    const uint64_t index = high & channel->power_of_2_mod;
    if (!channel->buffer[index] && high - low < channel->size &&
        atomic_compare_exchange_weak_explicit(&channel->high, &high, high + 1,
                                              memory_order_release,
                                              memory_order_relaxed)) {
      channel->buffer[index] = message;
      if (channel->ready_signal) {
        return fiber_signal_raise(channel->ready_signal);
      }
      return 0;
    }
    fiber_yield();
  }
  return 0;
}

static inline void* fiber_bounded_channel_receive(
    fiber_bounded_channel_t* channel) {
  assert(channel);

  while (1) {
    // read high first; this means the buffer will appear
    // smaller or equal to its actual size
    const uint64_t high =
        atomic_load_explicit(&channel->high, memory_order_acquire);

    const uint64_t low =
        atomic_load_explicit(&channel->low, memory_order_acquire);
    const uint64_t index = low & channel->power_of_2_mod;
    void* const ret = channel->buffer[index];
    if (ret && high > low) {
      channel->buffer[index] = 0;
      atomic_store_explicit(&channel->low, low + 1, memory_order_release);
      return ret;
    }
    if (channel->ready_signal) {
      fiber_signal_wait(channel->ready_signal);
    } else {
      fiber_yield();
    }
  }
  return NULL;
}

static inline int fiber_bounded_channel_try_receive(
    fiber_bounded_channel_t* channel, void** out) {
  assert(channel);

  // read high first; this means the buffer will appear
  // smaller or equal to its actual size
  const uint64_t high =
      atomic_load_explicit(&channel->high, memory_order_acquire);

  const uint64_t low =
      atomic_load_explicit(&channel->low, memory_order_acquire);
  const uint64_t index = low & channel->power_of_2_mod;
  void* const ret = channel->buffer[index];
  if (ret && high > low) {
    channel->buffer[index] = 0;
    atomic_store_explicit(&channel->low, low + 1, memory_order_release);
    *out = ret;
    return 1;
  }
  return 0;
}

// a unbounded channel. send and receive will block. there can be many senders
// but only one receiver
typedef struct fiber_unbounded_channel {
  mpsc_fifo_t queue;
  fiber_signal_t* ready_signal;
} fiber_unbounded_channel_t;

typedef mpsc_fifo_node_t fiber_unbounded_channel_message_t;

// specifying a NULL signal means this channel will spin. 'signal' must out-live
// this channel
static inline int fiber_unbounded_channel_init(
    fiber_unbounded_channel_t* channel, fiber_signal_t* signal) {
  assert(channel);
  channel->ready_signal = signal;
  if (!mpsc_fifo_init(&channel->queue)) {
    return 0;
  }
  return 1;
}

static inline void fiber_unbounded_channel_destroy(
    fiber_unbounded_channel_t* channel) {
  if (channel) {
    mpsc_fifo_destroy(&channel->queue);
  }
}

// the channel owns message when this function returns.
// returns 1 if a fiber was scheduled
static inline int fiber_unbounded_channel_send(
    fiber_unbounded_channel_t* channel,
    fiber_unbounded_channel_message_t* message) {
  assert(channel);
  assert(message);

  mpsc_fifo_push(&channel->queue, message);
  if (channel->ready_signal) {
    return fiber_signal_raise(channel->ready_signal);
  }
  return 0;
}

// the caller owns the message when this function returns
static inline void* fiber_unbounded_channel_receive(
    fiber_unbounded_channel_t* channel) {
  assert(channel);

  fiber_unbounded_channel_message_t* ret;
  while (!(ret = mpsc_fifo_trypop(&channel->queue))) {
    if (channel->ready_signal) {
      fiber_signal_wait(channel->ready_signal);
    }
  }
  return ret;
}

static inline void* fiber_unbounded_channel_try_receive(
    fiber_unbounded_channel_t* channel) {
  assert(channel);
  return mpsc_fifo_trypop(&channel->queue);
}

// a unbounded channel. send and receive will block. there can be only one
// sender and one receiver
typedef struct fiber_unbounded_sp_channel {
  spsc_fifo_t queue;
  fiber_signal_t* ready_signal;
} fiber_unbounded_sp_channel_t;

typedef spsc_node_t fiber_unbounded_sp_channel_message_t;

// specifying a NULL signal means this channel will spin. 'signal' must out-live
// this channel
static inline int fiber_unbounded_sp_channel_init(
    fiber_unbounded_sp_channel_t* channel, fiber_signal_t* signal) {
  assert(channel);
  channel->ready_signal = signal;
  if (!spsc_fifo_init(&channel->queue)) {
    return 0;
  }
  return 1;
}

static inline void fiber_unbounded_sp_channel_destroy(
    fiber_unbounded_sp_channel_t* channel) {
  if (channel) {
    spsc_fifo_destroy(&channel->queue);
  }
}

// the channel owns message when this function returns.
// returns 1 if a fiber was scheduled
static inline int fiber_unbounded_sp_channel_send(
    fiber_unbounded_sp_channel_t* channel,
    fiber_unbounded_sp_channel_message_t* message) {
  assert(channel);
  assert(message);

  spsc_fifo_push(&channel->queue, message);
  if (channel->ready_signal) {
    return fiber_signal_raise(channel->ready_signal);
  }
  return 0;
}

// the caller owns the message when this function returns
static inline void* fiber_unbounded_sp_channel_receive(
    fiber_unbounded_sp_channel_t* channel) {
  assert(channel);

  fiber_unbounded_sp_channel_message_t* ret;
  while (!(ret = spsc_fifo_trypop(&channel->queue))) {
    if (channel->ready_signal) {
      fiber_signal_wait(channel->ready_signal);
    }
  }
  return ret;
}

static inline void* fiber_unbounded_sp_channel_try_receive(
    fiber_unbounded_sp_channel_t* channel) {
  assert(channel);
  return spsc_fifo_trypop(&channel->queue);
}

#endif
