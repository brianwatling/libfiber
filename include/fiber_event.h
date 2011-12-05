#ifndef _FIBER_FIBER_EVENT_H_
#define _FIBER_FIBER_EVENT_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int fiber_event_init();

extern void fiber_event_destroy();

#define FIBER_EVENT_NONE (0)
#define FIBER_EVENT_NOTINIT (-1)
#define FIBER_EVENT_TRYAGAIN (-2)

extern int fiber_poll_events();

#define FIBER_POLL_IN (0x1)
#define FIBER_POLL_OUT (0x2)

extern int fiber_wait_for_event(int fd, uint32_t events);

extern int fiber_sleep(uint32_t seconds, uint32_t useconds);

#ifdef __cplusplus
}
#endif

#endif
