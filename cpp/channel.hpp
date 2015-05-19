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

#ifndef CHANNEL_HPP_
#define CHANNEL_HPP_

#include <stdexcept>
#include <fiber_channel.h>
#include <signal.hpp>

namespace fiberpp {

template<typename EventDescriptorType, typename EventPayloadType>
class UnboundedMultiProducerChannel
{
private:
    struct event_wrapper;

public:
    typedef EventDescriptorType event_descriptor;
    typedef EventPayloadType event_payload;

    struct event
    {
        friend class UnboundedMultiProducerChannel;
    public:
        event(event_descriptor type, event_wrapper* wrapper)
        : type(type), wrapper(wrapper)
        {}

        const event_descriptor& getType() const
        {
            return type;
        }

        event_payload& getPayload()
        {
            return payload;
        }

        const event_payload& getPayload() const
        {
            return payload;
        }

        void setPayload(const event_payload& p)
        {
            payload = p;
        }

    private:
        event_descriptor type;
        event_wrapper* wrapper;
        event_payload payload;
    };

    UnboundedMultiProducerChannel(fiber_signal_t* signal)
    {
        if(!fiber_unbounded_channel_init(&channel, signal)) {
            throw std::runtime_error("fiberpp::UnboundedMultiProducerEventSource() - error creating channel");
        }
    }

    ~UnboundedMultiProducerChannel()
    {
        fiber_unbounded_channel_destroy(&channel);
    }

    //returns true if a fiber was scheduled as a result of sending the event
    bool send(event* e)
    {
        return fiber_unbounded_channel_send(&channel, e->wrapper);
    }

    event* receive()
    {
        event_wrapper* const wrapper = reinterpret_cast<event_wrapper*>(fiber_unbounded_channel_receive(&channel));
        event* const e = &reinterpret_cast<event_wrapper*>(wrapper->data)->e;
        e->wrapper = wrapper;
        return e;
    }

    event* tryReceive()
    {
        event_wrapper* const wrapper = reinterpret_cast<event_wrapper*>(fiber_unbounded_channel_try_receive(&channel));
        if(!wrapper) {
            return NULL;
        }
        event* const e = &reinterpret_cast<event_wrapper*>(wrapper->data)->e;
        e->wrapper = wrapper;
        return e;
    }

private:

    struct event_wrapper : public fiber_unbounded_channel_message_t
    {
        event_wrapper(const event_descriptor& type)
        : e(type, this)
        {
            this->data = this;
        }

        event e;
    };

    fiber_unbounded_channel_t channel;

public:

    static event* allocEvent(const event_descriptor& type)
    {
        event_wrapper* const wrapper = reinterpret_cast<event_wrapper*>(malloc(sizeof(*wrapper)));
        if(!wrapper) {
            throw std::bad_alloc();
        }
        try {
            new (wrapper) event_wrapper(type);
        } catch(...) {
            free(wrapper);
            throw;
        }
        return &wrapper->e;
    }

    static void freeEvent(event* e)
    {
        event_wrapper* const wrapper = e->wrapper;
        e->~event();
        free(wrapper);
    }
};

template<typename EventDescriptorType, typename EventPayloadType>
class UnboundedSingleProducerChannel
{
private:
    struct event_wrapper;

public:
    typedef EventDescriptorType event_descriptor;
    typedef EventPayloadType event_payload;

    struct event
    {
        friend class UnboundedSingleProducerChannel;
    public:
        event(event_descriptor type, event_wrapper* wrapper)
        : type(type), wrapper(wrapper)
        {}

        const event_descriptor& getType() const
        {
            return type;
        }

        event_payload& getPayload()
        {
            return payload;
        }

        const event_payload& getPayload() const
        {
            return payload;
        }

        void setPayload(const event_payload& p)
        {
            payload = p;
        }

    private:
        event_descriptor type;
        event_wrapper* wrapper;
        event_payload payload;
    };

    UnboundedSingleProducerChannel(fiber_signal_t* signal)
    {
        if(!fiber_unbounded_sp_channel_init(&channel, signal)) {
            throw std::runtime_error("fiberpp::UnboundedMultiProducerEventSource() - error creating channel");
        }
    }

    ~UnboundedSingleProducerChannel()
    {
        fiber_unbounded_sp_channel_destroy(&channel);
    }

    //returns true if a fiber was scheduled as a result of sending the event
    bool send(event* e)
    {
        return fiber_unbounded_sp_channel_send(&channel, e->wrapper);
    }

    event* receive()
    {
        event_wrapper* const wrapper = reinterpret_cast<event_wrapper*>(fiber_unbounded_sp_channel_receive(&channel));
        event* const e = &reinterpret_cast<event_wrapper*>(wrapper->data)->e;
        e->wrapper = wrapper;
        return e;
    }

    event* tryReceive()
    {
        event_wrapper* const wrapper = reinterpret_cast<event_wrapper*>(fiber_unbounded_sp_channel_try_receive(&channel));
        if(!wrapper) {
            return NULL;
        }
        event* const e = &reinterpret_cast<event_wrapper*>(wrapper->data)->e;
        e->wrapper = wrapper;
        return e;
    }

private:

    struct event_wrapper : public fiber_unbounded_sp_channel_message_t
    {
        event_wrapper(const event_descriptor& type)
        : e(type, this)
        {
            this->data = this;
        }

        event e;
    };

    fiber_unbounded_sp_channel_t channel;

public:

    static event* allocEvent(const event_descriptor& type)
    {
        event_wrapper* const wrapper = reinterpret_cast<event_wrapper*>(malloc(sizeof(*wrapper)));
        if(!wrapper) {
            throw std::bad_alloc();
        }
        try {
            new (wrapper) event_wrapper(type);
        } catch(...) {
            free(wrapper);
            throw;
        }
        return &wrapper->e;
    }

    static void freeEvent(event* e)
    {
        event_wrapper* const wrapper = e->wrapper;
        e->~event();
        free(wrapper);
    }
};

template<typename ChannelType>
class ChannelSelector
{
public:
    typedef typename ChannelType::event event;
    typedef typename ChannelType::event_descriptor event_descriptor;
    typedef typename ChannelType::event_payload event_payload;

    ChannelSelector(size_t numChannels, ChannelType** channels, fiber_signal_t* signal)
    : signal(signal), numChannels(numChannels), channels(channels)
    {
        if(!numChannels || !channels) {
            throw std::runtime_error("fiberpp::ChannelSelector() - must provide channels");
        }
        for(size_t i = 0; i < numChannels; ++i) {
            if(!channels[i]) {
                throw std::runtime_error("fiberpp::ChannelSelector() - NULL channel provided");
            }
        }
    }

    event* receive()
    {
        while(1) {
            for(size_t i = 0; i < numChannels; ++i) {
                event* const ret = channels[i]->tryReceive();
                if(ret) {
                    return ret;
                }
            }
            if(signal) {
                fiber_signal_wait(signal);
            }
        }
        return NULL;
    }

    event* tryReceive()
    {
        for(size_t i = 0; i < numChannels; ++i) {
            event* const ret = channels[i]->tryReceive();
            if(ret) {
                return ret;
            }
        }
        return NULL;
    }

    static event* allocEvent(const event_descriptor& type)
    {
        return ChannelType::allocEvent();
    }

    static void freeEvent(event* e)
    {
        ChannelType::freeEvent(e);
    }

private:
    fiber_signal_t* signal;
    size_t numChannels;
    ChannelType** channels;
};

};

#endif

