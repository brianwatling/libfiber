#ifndef _FIBER_FIBER_IO_H_
#define _FIBER_FIBER_IO_H_

#ifdef __cplusplus
extern "C" {
#endif

extern int fiber_io_init();

extern int fiber_io_lock_thread();

extern int fiber_io_unlock_thread();

#ifdef __cplusplus
}
#endif

#endif

