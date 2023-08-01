// SPDX-FileCopyrightText: 2012-2023 Brian Watling <brian@oxbo.dev>
// SPDX-License-Identifier: MIT

#include <iostream>
#include <list>

#include "lockfree_fifo.h"

using namespace lockfree;

int y = 0;
struct blah {
  blah() : x(y++) {
    // std::cout << "blah() " << x << std::endl;
  }

  ~blah() {
    // std::cout << "~blah() " << x << std::endl;
  }

  blah(const blah&) : x(y++) {
    // std::cout << "blah(const blah&) " << x << std::endl;
  }

  blah(blah&&) : x(y++) {
    // std::cout << "blah(blah&&) " << x << std::endl;
  }

  blah& operator=(const blah&) {
    // std::cout << "operator=(const blah&)" << std::endl;
    return *this;
  }

  blah& operator=(blah&&) {
    // std::cout << "operator=(blah&&)" << std::endl;
    return *this;
  }

  int x;
};

int main() {
  {
    std::cout << "fifo ctor" << std::endl;
    fifo<blah, fifo_traits<concurrency::single, progress_type::wait_free>,
         fifo_traits<concurrency::single, progress_type::wait_free>>
        l;
    std::cout << "push" << std::endl;
    l.push(blah());
    std::cout << "pop" << std::endl;
    l.pop();
  }

  /*    {
          std::cout << "fifo ctor" << std::endl;
          fifo<blah, fifo_traits<concurrency::multiple,
     progress_type::wait_free>, fifo_traits<concurrency::single,
     progress_type::wait_free>> l; std::cout << "push" << std::endl;
          l.push(blah());
          std::cout << "pop" << std::endl;
          l.pop();
      }

      {
          std::cout << "fifo ctor" << std::endl;
          fifo<blah, fifo_traits<concurrency::multiple,
     progress_type::wait_free>, fifo_traits<concurrency::multiple,
     progress_type::lock_free>, wait_strategy::none, wait_strategy::none> l;
          std::cout << "push" << std::endl;
          l.push(blah());
          std::cout << "pop" << std::endl;
          l.pop();
      }*/

  std::atomic<int> x(0);
  int y = x.load(std::memory_order_acquire);

  return 0;
}
