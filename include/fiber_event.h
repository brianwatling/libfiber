#ifndef _FIBER_FIBER_EVENT_H_
#define _FIBER_FIBER_EVENT_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FIBER_POLL_IN (0x1)
#define FIBER_POLL_OUT (0x2)

extern int fiber_init_events();

extern int fiber_wait_for_event(int fd, uint32_t events);

extern int fiber_sleep(uint32_t seconds, uint32_t useconds);

#ifdef __cplusplus
}
#endif

#endif
