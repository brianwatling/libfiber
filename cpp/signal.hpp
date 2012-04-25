#ifndef SIGNAL_HPP_
#define SIGNAL_HPP_

#include <fiber_signal.h>

namespace fiberpp {

class Signal
{
public:
    Signal()
    {
        fiber_signal_init(&signal);
    }

    ~Signal()
    {
        fiber_signal_destroy(&signal);
    }

    void wait()
    {
        fiber_signal_wait(&signal);
    }

    //returns true if another fiber was scheduled
    bool raise()
    {
        return fiber_signal_raise(&signal) > 0;
    }

    operator fiber_signal_t*()
    {
        return &signal;
    }

private:
    fiber_signal_t signal;
};

};

#endif

