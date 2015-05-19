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

#include "lockfree_fifo.h"
#include <iostream>
#include <list>

using namespace lockfree;

int y = 0;
struct blah
{
    blah()
    : x(y++)
    {
        //std::cout << "blah() " << x << std::endl;
    }

    ~blah()
    {
        //std::cout << "~blah() " << x << std::endl;
    }

    blah(const blah&)
    : x(y++)
    {
        //std::cout << "blah(const blah&) " << x << std::endl;
    }

    blah(blah&&)
    : x(y++)
    {
        //std::cout << "blah(blah&&) " << x << std::endl;
    }

    blah& operator=(const blah&)
    {
        //std::cout << "operator=(const blah&)" << std::endl;
        return *this;
    }

    blah& operator=(blah&&)
    {
        //std::cout << "operator=(blah&&)" << std::endl;
        return *this;
    }

    int x;
};

int main()
{
    {
        std::cout << "fifo ctor" << std::endl;
        fifo<blah, fifo_traits<concurrency::single, progress_type::wait_free>, fifo_traits<concurrency::single, progress_type::wait_free>> l;
        std::cout << "push" << std::endl;
        l.push(blah());
        std::cout << "pop" << std::endl;
        l.pop();
    }

/*    {
        std::cout << "fifo ctor" << std::endl;
        fifo<blah, fifo_traits<concurrency::multiple, progress_type::wait_free>, fifo_traits<concurrency::single, progress_type::wait_free>> l;
        std::cout << "push" << std::endl;
        l.push(blah());
        std::cout << "pop" << std::endl;
        l.pop();
    }

    {
        std::cout << "fifo ctor" << std::endl;
        fifo<blah, fifo_traits<concurrency::multiple, progress_type::wait_free>, fifo_traits<concurrency::multiple, progress_type::lock_free>, wait_strategy::none, wait_strategy::none> l;
        std::cout << "push" << std::endl;
        l.push(blah());
        std::cout << "pop" << std::endl;
        l.pop();
    }*/

    std::atomic<int> x(0);
    int y = x.load(std::memory_order_acquire);

    return 0;
}

