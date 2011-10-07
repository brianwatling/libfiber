#include "fiber_manager.h"
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>

#ifdef __GNUC__
#define STATIC_ASSERT_HELPER(expr, msg) \
    (!!sizeof (struct { unsigned int STATIC_ASSERTION__##msg: (expr) ? 1 : -1; }))
#define STATIC_ASSERT(expr, msg) \
    extern int (*assert_function__(void)) [STATIC_ASSERT_HELPER(expr, msg)]
#else
    #define STATIC_ASSERT(expr, msg)   \
    extern char STATIC_ASSERTION__##msg[1]; \
    extern char STATIC_ASSERTION__##msg[(expr)?1:2]
#endif /* #ifdef __GNUC__ */

STATIC_ASSERT(sizeof(pthread_t) == sizeof(fiber_t*), bad_pthread_t_size);

/*int pthread_attr_destroy(pthread_attr_t *)
{
}

int pthread_attr_getdetachstate(const pthread_attr_t *, int *)
{
}

int pthread_attr_getguardsize(const pthread_attr_t *, size_t *)
{
}

int pthread_attr_getinheritsched(const pthread_attr_t *, int *)
{
}

int pthread_attr_getschedparam(const pthread_attr_t *, struct sched_param *)
{
}

int pthread_attr_getschedpolicy(const pthread_attr_t *, int *)
{
}

int pthread_attr_getscope(const pthread_attr_t *, int *)
{
}

int pthread_attr_getstackaddr(const pthread_attr_t *, void **)
{
}

int pthread_attr_getstacksize(const pthread_attr_t *, size_t *)
{
}

int pthread_attr_init(pthread_attr_t *)
{
}

int pthread_attr_setdetachstate(pthread_attr_t *, int)
{
}

int pthread_attr_setguardsize(pthread_attr_t *, size_t)
{
}

int pthread_attr_setinheritsched(pthread_attr_t *, int)
{
}

int pthread_attr_setschedparam(pthread_attr_t *, const struct sched_param *)
{
}

int pthread_attr_setschedpolicy(pthread_attr_t *, int)
{
}

int pthread_attr_setscope(pthread_attr_t *, int)
{
}

int pthread_attr_setstackaddr(pthread_attr_t *, void *)
{
}

int pthread_attr_setstacksize(pthread_attr_t *, size_t)
{
}*/

int pthread_cancel(pthread_t thread)
{
    //NOT IMPLEMENTED!
    return ENOTSUP;//not standard.
}

int pthread_cond_broadcast(pthread_cond_t * cond)
{
    //TODO
    return 0;
}

int pthread_cond_destroy(pthread_cond_t * cond)
{
    //TODO
    return 0;
}

int pthread_cond_init(pthread_cond_t * cond, const pthread_condattr_t * attr)
{
    //TODO
    return 0;
}

int pthread_cond_signal(pthread_cond_t * cond)
{
    //TODO
    return 0;
}

int pthread_cond_timedwait(pthread_cond_t * cond,  pthread_mutex_t * mutex, const struct timespec * abstime)
{
    //TODO
    return 0;
}

int pthread_cond_wait(pthread_cond_t * cond, pthread_mutex_t * mutex)
{
    //TODO
    return 0;
}

/*
int pthread_condattr_destroy(pthread_condattr_t *)
{
}

int pthread_condattr_getpshared(const pthread_condattr_t *, int *)
{
}

int pthread_condattr_init(pthread_condattr_t *)
{
}

int pthread_condattr_setpshared(pthread_condattr_t *, int)
{
}
*/

static size_t fiber_default_stack_size = FIBER_DEFAULT_STACK_SIZE;
static size_t fiber_min_stack_size = FIBER_MIN_STACK_SIZE;

static void read_env_size_t(const char* name, size_t default_value, size_t* out)
{
    const char* value_str = getenv(name);
    if(value_str) {
        *out = strtol(value_str, NULL, 10);
        if(!*out) {
            *out = default_value;
        }
    } else {
        *out = default_value;
    }
}

int pthread_create(pthread_t * thread, const pthread_attr_t * attr, void *(*start_routine)(void *), void *arg)
{
    if(!thread) {
        return EINVAL;
    }

    if(!fiber_default_stack_size) {
        read_env_size_t("FIBER_DEFAULT_STACK_SIZE", FIBER_DEFAULT_STACK_SIZE, &fiber_default_stack_size);
    }
    if(!fiber_min_stack_size) {
        read_env_size_t("FIBER_MIN_STACK_SIZE", FIBER_MIN_STACK_SIZE, &fiber_min_stack_size);
    }

    size_t stack_size = fiber_default_stack_size;
    if(attr) {
        pthread_attr_getstacksize(attr, &stack_size);
    }
    if(stack_size < fiber_min_stack_size) {
        stack_size = fiber_min_stack_size;
    }

    fiber_t** const fiber_ptr = (fiber_t**)thread;
    *fiber_ptr = fiber_create(stack_size, start_routine, arg);
    if(!*fiber_ptr) {
        return EAGAIN;
    }
    return 0;
}

int pthread_detach(pthread_t thread)
{
    fiber_t* const f = (fiber_t*)thread;
    if(!f || f->detached) {
        return EINVAL;
    }
    f->detached = 1;
    return 0;
}

int pthread_equal(pthread_t thread1, pthread_t thread2)
{
    return (fiber_t*)thread1 == (fiber_t*)thread2;
}

void pthread_exit(void * status)
{
    fiber_t* const the_fiber = fiber_manager_get()->current_fiber;
    the_fiber->result = status;

    //TODO: the following code is ripped from fiber_go_function. don't copy/paste code!
    the_fiber->state = FIBER_STATE_DONE;

    while(!the_fiber->detached) {
        fiber_manager_yield(fiber_manager_get());
        usleep(1);/* be a bit nicer */
        //TODO: not busy loop here.
    }

    wsd_work_stealing_deque_push_bottom(fiber_manager_get()->done_fibers, the_fiber);
    while(1) { /* yield() may actually not switch to anything else if there's nothing else to schedule - loop here until yield() doesn't return */
        fiber_manager_yield(fiber_manager_get());
        usleep(1);/* be a bit nicer */
    }
}

int pthread_join(pthread_t thread, void ** status)
{
    fiber_t* const f = (fiber_t*)thread;
    if(!f) {
        return EINVAL;
    }
    if(FIBER_SUCCESS != fiber_join(f)) {
        return EINVAL;
    }
    return 0;
}

int pthread_key_create(pthread_key_t * key, void (*destructor)(void *))
{
    //TODO
    return 0;
}

int pthread_key_delete(pthread_key_t key)
{
    //TODO
    return 0;
}

int pthread_setspecific(pthread_key_t key, const void * value)
{
    //TODO
    return 0;
}

void *pthread_getspecific(pthread_key_t key)
{
    //TODO
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t * mutex)
{
    //TODO
    return 0;
}

int pthread_mutex_getprioceiling(const pthread_mutex_t * mutex, int *prioceiling)
{
    //TODO
    return 0;
}

int pthread_mutex_setprioceiling(pthread_mutex_t * mutex, int prioceiling, int * oldceiling)
{
    //TODO
    return 0;
}

int pthread_mutex_init(pthread_mutex_t * mutex, const pthread_mutexattr_t * attr)
{
    //TODO
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t * mutex)
{
    //TODO
    return 0;
}

int pthread_mutex_trylock(pthread_mutex_t * mutex)
{
    //TODO
    return 0;
}

int pthread_mutex_unlock(pthread_mutex_t * mutex)
{
    //TODO
    return 0;
}

/*
int pthread_mutexattr_destroy(pthread_mutexattr_t *)
{
}

int pthread_mutexattr_getprioceiling(const pthread_mutexattr_t *, int *)
{
}

int pthread_mutexattr_getprotocol(const pthread_mutexattr_t *, int *)
{
}

int pthread_mutexattr_getpshared(const pthread_mutexattr_t *, int *)
{
}

int pthread_mutexattr_gettype(const pthread_mutexattr_t *, int *)
{
}

int pthread_mutexattr_init(pthread_mutexattr_t *)
{
}

int pthread_mutexattr_setprioceiling(pthread_mutexattr_t *, int)
{
}

int pthread_mutexattr_setprotocol(pthread_mutexattr_t *, int)
{
}

int pthread_mutexattr_setpshared(pthread_mutexattr_t *, int)
{
}

int pthread_mutexattr_settype(pthread_mutexattr_t *, int)
{
}
*/

/*
int pthread_once(pthread_once_t *, void (*)(void))
{
}*/

int pthread_rwlock_destroy(pthread_rwlock_t * rwlock)
{
    //TODO
    return 0;
}

int pthread_rwlock_init(pthread_rwlock_t * rwlock, const pthread_rwlockattr_t * attr)
{
    //TODO
    return 0;
}

int pthread_rwlock_rdlock(pthread_rwlock_t * rwlock)
{
    //TODO
    return 0;
}

int pthread_rwlock_tryrdlock(pthread_rwlock_t * rwlock)
{
    //TODO
    return 0;
}

int pthread_rwlock_trywrlock(pthread_rwlock_t * rwlock)
{
    //TODO
    return 0;
}

int pthread_rwlock_unlock(pthread_rwlock_t * rwlock)
{
    //TODO
    return 0;
}

int pthread_rwlock_wrlock(pthread_rwlock_t * rwlock)
{
    //TODO
    return 0;
}

/*
int pthread_rwlockattr_destroy(pthread_rwlockattr_t *)
{
}

int pthread_rwlockattr_getpshared(const pthread_rwlockattr_t *, int *)
{
}

int pthread_rwlockattr_init(pthread_rwlockattr_t *)
{
}

int pthread_rwlockattr_setpshared(pthread_rwlockattr_t *, int)
{
}
*/

pthread_t pthread_self(void)
{
    return (pthread_t)fiber_manager_get()->current_fiber;
}

int pthread_setcancelstate(int state, int * oldstate)
{
    //NOT IMPLEMENTED!
    return 0;
}

int pthread_setcanceltype(int type, int * oldtype)
{
    //NOT IMPLEMENTED!
    return 0;
}

static int fiber_pthread_concurrency = 0;

int pthread_setconcurrency(int conc)
{
    if(conc < 0) {
        return EINVAL;
    }
    if(conc == 0) {
        //TODO: get processor count?
        conc = 1;
    }
    fiber_manager_set_total_kernel_threads(conc);
    fiber_pthread_concurrency = conc;
    return 0;
}

int pthread_getconcurrency(void)
{
    return fiber_pthread_concurrency;
}

int pthread_setschedparam(pthread_t thread, int policy, const struct sched_param * param)
{
    //NOT IMPLEMENTED!
    return ENOTSUP;
}

int pthread_getschedparam(pthread_t thread, int * policy, struct sched_param * param)
{
    //NOT IMPLEMENTED!
    return ENOTSUP;//not standard.
}

void pthread_testcancel(void)
{
    //TODO: support cancels?
}
