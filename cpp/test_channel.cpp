// SPDX-FileCopyrightText: 2012-2023 Brian Watling <brian@oxbo.dev>
// SPDX-License-Identifier: MIT

#include <fiber_barrier.h>
#include <fiber_manager.h>
#include <sys/time.h>

#include <iostream>

#include "channel.hpp"

#define NUM_THREADS 2
#define PER_FIBER_COUNT 3000000

using namespace fiberpp;

enum EventType {
  One,
  Two,
};

typedef UnboundedMultiProducerChannel<EventType, int> ChannelType;
typedef UnboundedSingleProducerChannel<EventType, int> SingleChannelType;

fiber_barrier_t barrier;

void* run_function(void* param) {
  fiber_barrier_wait(&barrier);
  ChannelType* const eventSource = reinterpret_cast<ChannelType*>(param);
  for (int i = 0; i < PER_FIBER_COUNT; ++i) {
    ChannelType::event* e = eventSource->allocEvent(One);
    e->setPayload(i);
    if (eventSource->send(e)) {
      fiber_yield();
    }
  }
}

void* single_function(void* param) {
  fiber_barrier_wait(&barrier);
  SingleChannelType* const eventSource =
      reinterpret_cast<SingleChannelType*>(param);
  for (int i = 0; i < PER_FIBER_COUNT; ++i) {
    SingleChannelType::event* e = eventSource->allocEvent(One);
    e->setPayload(i);
    if (eventSource->send(e)) {
      fiber_yield();
    }
  }
}

uint64_t getUsecs(const timeval& t) { return t.tv_sec * 1000000LL + t.tv_usec; }

int main() {
  Signal signal;
  fiber_barrier_init(&barrier, 4);

  fiber_manager_init(NUM_THREADS);

  ChannelType channelOne(signal);
  ChannelType channelTwo(signal);
  ChannelType channelThree(signal);
  ChannelType* channelsToSelect[] = {&channelOne, &channelTwo, &channelThree};
  ChannelSelector<ChannelType> selector(3, channelsToSelect, signal);

  fiber_create(102400, &run_function, &channelOne);
  fiber_create(102400, &run_function, &channelTwo);
  fiber_create(102400, &run_function, &channelThree);

  timeval begin, end;
  fiber_barrier_wait(&barrier);
  gettimeofday(&begin, NULL);
  for (int i = 0; i < 3 * PER_FIBER_COUNT; ++i) {
    ChannelType::event* e = selector.receive();
    // std::cout << "received: " << e->getPayload() << std::endl;
    selector.freeEvent(e);
  }
  gettimeofday(&end, NULL);
  std::cout << "multi sender, many channel took: "
            << (getUsecs(end) - getUsecs(begin)) << " usecs" << std::endl;

  ChannelType manyChannel(signal);
  fiber_create(102400, &run_function, &manyChannel);
  fiber_create(102400, &run_function, &manyChannel);
  fiber_create(102400, &run_function, &manyChannel);
  fiber_barrier_wait(&barrier);
  gettimeofday(&begin, NULL);
  for (int i = 0; i < 3 * PER_FIBER_COUNT; ++i) {
    ChannelType::event* e = manyChannel.receive();
    // std::cout << "received: " << e->getPayload() << std::endl;
    manyChannel.freeEvent(e);
  }
  gettimeofday(&end, NULL);
  std::cout << "multi sender, same channel took: "
            << (getUsecs(end) - getUsecs(begin)) << " usecs" << std::endl;

  SingleChannelType singleChannelOne(signal);
  SingleChannelType singleChannelTwo(signal);
  SingleChannelType singleChannelThree(signal);
  SingleChannelType* singleChannelsToSelect[] = {
      &singleChannelOne, &singleChannelTwo, &singleChannelThree};
  ChannelSelector<SingleChannelType> singleSelector(3, singleChannelsToSelect,
                                                    signal);
  fiber_create(102400, &single_function, &singleChannelOne);
  fiber_create(102400, &single_function, &singleChannelTwo);
  fiber_create(102400, &single_function, &singleChannelThree);
  fiber_barrier_wait(&barrier);
  gettimeofday(&begin, NULL);
  for (int i = 0; i < 3 * PER_FIBER_COUNT; ++i) {
    SingleChannelType::event* e = singleSelector.receive();
    // std::cout << "received: " << e->getPayload() << std::endl;
    singleSelector.freeEvent(e);
  }
  gettimeofday(&end, NULL);
  std::cout << "single sender, many channel took: "
            << (getUsecs(end) - getUsecs(begin)) << " usecs" << std::endl;

  return 0;
}
