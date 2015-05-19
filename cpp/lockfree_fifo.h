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

#ifndef LOCKFREE_FIFO_H_
#define LOCKFREE_FIFO_H_

#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include <type_traits>

namespace lockfree
{

namespace progress_type
{
struct wait_free {};
struct lock_free {};
};

namespace concurrency
{
struct single {};
struct multiple {};
};

template<typename ConcurrencyTraits, typename ProgressTraits>
struct fifo_traits
{
    typedef ConcurrencyTraits concurrency_traits;
    typedef ProgressTraits progress_traits;
};

/*
producer    consumer
s wf        s wf     (spsc)
s wf        m lf     (dist or modified mpmc_fifo)
m wf        s wf     (mpsc)
m wf        m lf     (modified dist)

strict:
m lf        s wf     (modified mpmc_fifo)
m lf        m lf     (mpmc_fifo)

wait strategy
spin
relax
yield
sleep
condition variable?? (need to make sure the fifo is still empty after acquiring the lock)

** each wait strategy should have a next_strategy which is used after attempting the first strategy
*/

template<typename T, typename ProducerTraits, typename ConsumerTraits, typename Allocator>
struct fifo_impl
{};

template<typename T, typename TAllocator>
struct fifo_node
{
    typedef typename TAllocator::template rebind<fifo_node>::other allocator_type;
    typedef T value_type;
    typedef typename allocator_type::pointer pointer;

    fifo_node()
    : next(nullptr)
    {}

    fifo_node(T const& data)
    : next(nullptr), data(data)
    {}

    fifo_node(T&& data)
    : next(nullptr), data(std::move(data))
    {}

    fifo_node(fifo_node const& other)
    {
        next.store(other.next.load(std::memory_order_relaxed), std::memory_order_relaxed);
        data = other.data;
    }

    std::atomic<pointer> next;
    T data;
};

template<typename Traits, typename NodeType>
struct push_impl
{};

template<typename NodeType>
struct push_impl<fifo_traits<concurrency::single, progress_type::wait_free>, NodeType>
{
    typedef typename NodeType::value_type value_type;
    typedef typename NodeType::pointer node_pointer;

    push_impl(typename NodeType::allocator_type& allocator)
    : allocator(allocator)
    {
        tail = alloc_node(value_type());
    }

    node_pointer alloc_node(const value_type& value)
    {
        node_pointer const result = allocator.allocate(1, nullptr);
        allocator.construct(result, value);
        return result;
    }

    bool try_push(node_pointer newNode)
    {
        node_pointer const oldTail = tail;
        tail = newNode;
        oldTail->next.store(newNode, std::memory_order_release);
        return true;
    }

    typename NodeType::allocator_type& allocator;
    node_pointer tail;
};

template<typename NodeType>
struct push_impl<fifo_traits<concurrency::multiple, progress_type::wait_free>, NodeType>
{
    typedef typename NodeType::value_type value_type;
    typedef typename NodeType::pointer node_pointer;

    push_impl(typename NodeType::allocator_type& allocator)
    : allocator(allocator)
    {
        tail = alloc_node(value_type());
    }

    node_pointer alloc_node(const value_type& value)
    {
        node_pointer const result = allocator.allocate(1, nullptr);
        allocator.construct(result, value);
        return result;
    }

    bool try_push(node_pointer newNode)
    {
        node_pointer const oldTail = tail.exchange(newNode, std::memory_order_seq_cst);
        oldTail->next.store(newNode, std::memory_order_release);
        return true;
    }

    typename NodeType::allocator_type& allocator;
    std::atomic<node_pointer> tail;
};

enum class PopResult
{
    Empty,
    Failure,
    Success,
};

template<typename Traits, typename NodeType>
struct pop_impl
{};

template<typename NodeType>
struct pop_impl<fifo_traits<concurrency::single, progress_type::wait_free>, NodeType>
{
    typedef typename NodeType::value_type value_type;
    typedef typename NodeType::pointer node_pointer;

    pop_impl(typename NodeType::allocator_type& allocator, node_pointer tail)
    : allocator(allocator), head(tail)
    {}

    PopResult try_pop(value_type& dest)
    {
        node_pointer const oldHead = head;
        node_pointer const oldHeadNext = reinterpret_cast<node_pointer>(oldHead->next.load(std::memory_order_acquire));
        if(oldHeadNext) {
            head = oldHeadNext;
            dest = std::move(oldHeadNext->data);
            allocator.destroy(oldHead);
            allocator.deallocate(oldHead, 1);
            return PopResult::Success;
        }
        return PopResult::Empty;
    }

    typename NodeType::allocator_type& allocator;
    node_pointer head;
};

template<typename T, typename TagType>
class tagged_pointer;

template<typename T, typename TagType = uintptr_t>
struct tagged_pointer_snapshot
{
    friend class tagged_pointer<T, TagType>;

    T value() const
    {
        return v;
    }

private:
    tagged_pointer_snapshot(TagType t, T v)
    : t(t), v(v)
    {}

    static_assert(sizeof(T) + sizeof(TagType) == 2 * sizeof(void*), "");
    TagType t;
    T v;
} __attribute__((__aligned__(sizeof(void*) * 2)));

template<typename T, typename TagType = uintptr_t>
struct tagged_pointer
{
    typedef tagged_pointer_snapshot<T, TagType> snapshot_type;

    tagged_pointer()
    : value(), tag(0)
    {}

    tagged_pointer(T value)
    : value(value), tag(0)
    {}

    snapshot_type snapshot() const
    {
        TagType const t = tag.load(std::memory_order_acquire);
        T const v = value;
        return snapshot_type(t, v);
    }

    bool compare_and_swap(snapshot_type const& original, T updated)
    {
        #if defined(ARCH_x86)
            return __sync_bool_compare_and_swap(reinterpret_cast<uint64_t*>(&tag), *reinterpret_cast<const uint64_t*>(&original), *reinterpret_cast<uint64_t*>(&updated));
        #elif defined(ARCH_x86_64)
            char result;
            __asm__ __volatile__ (
                "lock cmpxchg16b %1\n\t"
                "setz %0"
                :  "=q" (result)
                 , "+m" (tag)
                :  "d" (original.v)
                 , "a" (original.t)
                 , "c" (updated)
                 , "b" (original.t + 1)
                : "cc"
            );
            return result;
        #else
            #error please define a compare_and_swap2()
        #endif
    }

private:
    tagged_pointer(TagType tag, T value)
    : tag(tag), value(value)
    {}

    static_assert(sizeof(T) + sizeof(TagType) == 2 * sizeof(void*), "");
    std::atomic<TagType> tag;
    T value;
} __attribute__((__aligned__(sizeof(void*) * 2)));

template<typename NodeType>
struct pop_impl<fifo_traits<concurrency::multiple, progress_type::lock_free>, NodeType>
{
    typedef typename NodeType::value_type value_type;
    typedef typename NodeType::pointer node_pointer;
    typedef tagged_pointer<node_pointer> head_pointer;

    pop_impl(typename NodeType::allocator_type& allocator, node_pointer tail)
    : allocator(allocator), head(tail)
    {}

    PopResult try_pop(value_type& dest)
    {
        typename head_pointer::snapshot_type const snapshot = head.snapshot();

        node_pointer const oldHead = snapshot.value();
        node_pointer const oldHeadNext = reinterpret_cast<node_pointer>(oldHead->next.load(std::memory_order_acquire));
        if(oldHeadNext) {
            static_assert(std::is_pod<value_type>::value, "must use POD type, since pop may access freed memory");
            dest = oldHeadNext->data;
            if(head.compare_and_swap(snapshot, oldHeadNext)) {
                allocator.destroy(oldHead);
                allocator.deallocate(oldHead, 1);
                return PopResult::Success;
            }
            return PopResult::Failure;
        }
        return PopResult::Empty;
    }

    typename NodeType::allocator_type& allocator;
    head_pointer head;
};

namespace wait_strategy
{

struct none
{
    void wait()
    {}
};

template<size_t Count = 256, typename NextStrategy = none>
struct spin
{
    spin()
    : counter(Count)
    {}

    void wait()
    {
        --counter;
        if(!counter) {
            next.wait();
            counter = Count;
        }
    }

    NextStrategy next;
    size_t counter;
};

struct relax
{
    void wait()
    {
        #if defined(ARCH_x86) || defined(ARCH_x86_64)
            __asm__ __volatile__ (
                "pause": : : "memory"
            );
        #else
        #warning no cpu_relax() defined for this architecture. please consider defining one if possible.
        #endif
    }
};

struct yield
{
    void wait()
    {
        std::this_thread::yield();
    }
};

template<typename DurationType = std::chrono::microseconds, size_t count = 1>
struct sleep
{
    void wait()
    {
        std::this_thread::sleep_for(DurationType(count));
    }
};

} // namespace wait_strategy

template<typename T, typename ProducerTraits, typename ConsumerTraits, typename BackoffStrategy = wait_strategy::spin<128, wait_strategy::relax>, typename PopWaitStrategy = wait_strategy::spin<128, wait_strategy::relax>, typename Allocator = std::allocator<T>, size_t CacheSize = 64>
class fifo
{
    typedef fifo_node<T, Allocator> node_type;
    typedef push_impl<ProducerTraits, node_type> PushImplementation;
    typedef pop_impl<ConsumerTraits, node_type> PopImplementation;
    typedef typename node_type::pointer node_pointer;
public:
    fifo()
    : pusher(allocator), popper(allocator, pusher.tail)
    {}

    void push(T const& value)
    {
        node_pointer node = pusher.alloc_node(value);
        BackoffStrategy backoff;
        while(!pusher.try_push(node)) {
            backoff.wait();
        }
    }

    T pop()
    {
        PopWaitStrategy waitStrategy;
        BackoffStrategy backoff;
        T ret;
        while(1) {
            PopResult const result = popper.try_pop(ret);
            switch(result) {
            case PopResult::Success:
                return std::move(ret);
            case PopResult::Failure:
                backoff.wait();
                break;
            case PopResult::Empty:
                waitStrategy.wait();
                break;
            };
        }
    }

private:
    typename node_type::allocator_type allocator;
    PushImplementation pusher;
    char _pusher_padding[CacheSize - sizeof(pusher)];
    PopImplementation popper;
    char _popper_padding[CacheSize - sizeof(popper)];
};

} // namespace lockfree

#endif

