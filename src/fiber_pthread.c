#include "fiber_mutex.h"
#include "fiber_cond.h"
#include "fiber_manager.h"
#include "fiber_barrier.h"
#define __USE_UNIX98
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

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
STATIC_ASSERT(sizeof(pthread_mutex_t) >= sizeof(fiber_mutex_t*), bad_pthread_t_size);
STATIC_ASSERT(sizeof(pthread_cond_t) >= sizeof(fiber_cond_t*), bad_pthread_t_size);
//STATIC_ASSERT(sizeof(pthread_rwlock_t) >= sizeof(fiber_t*), bad_pthread_t_size);
STATIC_ASSERT(sizeof(pthread_barrier_t) >= sizeof(fiber_barrier_t*), bad_barrier_t_size);

static pthread_mutex_t default_pthread_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t default_pthread_cond = PTHREAD_COND_INITIALIZER;
//static pthread_rwlock_t default_pthread_rwlock = PTHREAD_RWLOCK_INITIALIZER;

int pthread_cancel(pthread_t thread)
{
    //NOT IMPLEMENTED!
    return ENOTSUP;//not standard.
}

void pthread_testcancel(void)
{
    //TODO: support cancels?
}

static size_t fiber_default_stack_size = 0;
static size_t fiber_min_stack_size = 0;
static size_t fiber_system_threads = 0;

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

__attribute__((__constructor__)) void init_fiber_pthread()
{
    read_env_size_t("FIBER_SYSTEM_THREADS", 1, &fiber_system_threads);
    fiber_manager_set_total_kernel_threads(fiber_system_threads);
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
    const int ret = fiber_detach(f);
    if(FIBER_ERROR == ret) {
        return EINVAL;
    }
    return 0;
}

int pthread_equal(pthread_t thread1, pthread_t thread2)
{
    return (fiber_t*)thread1 == (fiber_t*)thread2;
}

extern void fiber_join_routine(fiber_t* the_fiber, void* result);

void pthread_exit(void * status)
{
    fiber_t* const the_fiber = fiber_manager_get()->current_fiber;
    fiber_join_routine(the_fiber, status);
    abort();
}

int pthread_join(pthread_t thread, void ** status)
{
    fiber_t* const f = (fiber_t*)thread;
    if(!f) {
        return EINVAL;
    }
    if(FIBER_SUCCESS != fiber_join(f, status)) {
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

typedef struct fiber_pthread_mutex
{
    fiber_mutex_t mutex;
    int type;
    fiber_t* owner;
    int lock_counter;
} fiber_pthread_mutex_t;

int pthread_mutex_init(pthread_mutex_t * mutex, const pthread_mutexattr_t * attr)
{
    if(!mutex) {
        return EINVAL;
    }

    fiber_pthread_mutex_t* const the_mutex = (fiber_pthread_mutex_t*)malloc(sizeof(*the_mutex));
    if(!the_mutex) {
        return ENOMEM;
    }
    if(!fiber_mutex_init(&the_mutex->mutex)) {
        free(the_mutex);
        return ENOMEM;
    }
    the_mutex->owner = NULL;
    the_mutex->type = PTHREAD_MUTEX_DEFAULT;
    the_mutex->lock_counter = 0;

    if(attr) {
        int pshared = 0;
        if(pthread_mutexattr_getpshared(attr, &pshared)
           || pthread_mutexattr_gettype(attr, &the_mutex->type)
           || pshared) {//fibers do not support pshared
            fiber_mutex_destroy(&the_mutex->mutex);
            free(the_mutex);
            return EINVAL;
            //TODO: abort here?
        }
    }

    *((fiber_pthread_mutex_t**)mutex) = the_mutex;

    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t * mutex)
{
    if(!mutex) {
        return EINVAL;
    }
    fiber_pthread_mutex_t* const the_mutex = *((fiber_pthread_mutex_t**)mutex);
    if(!the_mutex) {
        return EINVAL;
    }
    fiber_mutex_destroy(&the_mutex->mutex);
    free(the_mutex);
    memcpy(mutex, &default_pthread_mutex, sizeof(*mutex));
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t * mutex)
{
    if(!mutex) {
        return EINVAL;
    }
    fiber_pthread_mutex_t* const the_mutex = *((fiber_pthread_mutex_t**)mutex);
    if(!the_mutex) {
        return EINVAL;
    }

    if(the_mutex->type == PTHREAD_MUTEX_ERRORCHECK && the_mutex->owner == fiber_manager_get()->current_fiber) {
        return EDEADLK;
    }

    if(the_mutex->type == PTHREAD_MUTEX_RECURSIVE) {
        if(the_mutex->owner == fiber_manager_get()->current_fiber) {
            the_mutex->lock_counter += 1;
            return 0;
        }
    }

    fiber_mutex_lock(&the_mutex->mutex);

    if(the_mutex->type == PTHREAD_MUTEX_RECURSIVE) {
        assert(!the_mutex->owner);
        assert(the_mutex->lock_counter == 0);
        the_mutex->lock_counter = 1;
    }
    the_mutex->owner = fiber_manager_get()->current_fiber;
    //TODO: implement priorities?
    return 0;
}

int pthread_mutex_trylock(pthread_mutex_t * mutex)
{
    fiber_pthread_mutex_t* const the_mutex = *((fiber_pthread_mutex_t**)mutex);
    if(!the_mutex) {
        return EINVAL;
    }

    if(the_mutex->type == PTHREAD_MUTEX_RECURSIVE) {
        if(the_mutex->owner == fiber_manager_get()->current_fiber) {
            the_mutex->lock_counter += 1;
            return 0;
        }
    }

    const int ret = fiber_mutex_trylock(&the_mutex->mutex);
    if(ret != FIBER_SUCCESS) {
        return EBUSY;
    }

    if(the_mutex->type == PTHREAD_MUTEX_RECURSIVE) {
        assert(!the_mutex->owner);
        assert(the_mutex->lock_counter == 0);
        the_mutex->lock_counter = 1;
    }
    the_mutex->owner = fiber_manager_get()->current_fiber;

    return 0;
}

int pthread_mutex_unlock(pthread_mutex_t * mutex)
{
    fiber_pthread_mutex_t* const the_mutex = *((fiber_pthread_mutex_t**)mutex);
    if(!the_mutex) {
        return EINVAL;
    }

    if(the_mutex->type == PTHREAD_MUTEX_ERRORCHECK || the_mutex->type == PTHREAD_MUTEX_RECURSIVE) {
        if(!the_mutex->owner || the_mutex->owner != fiber_manager_get()->current_fiber) {
            return EPERM;
        }
    }

    if(the_mutex->type == PTHREAD_MUTEX_RECURSIVE) {
        the_mutex->lock_counter -= 1;
        if(the_mutex->lock_counter) {
            return 0;
        }
    }

    the_mutex->owner = NULL;
    fiber_mutex_unlock(&the_mutex->mutex);

    return 0;
}

int pthread_cond_init(pthread_cond_t * cond, const pthread_condattr_t * attr)
{
    if(!cond) {
        return EINVAL;
    }

    if(attr) {
        int pshared = 0;
        if(pthread_condattr_getpshared(attr, &pshared) || pshared) {
            return EINVAL;
        }
    }

    fiber_cond_t* const the_cond = (fiber_cond_t*)malloc(sizeof(*the_cond));
    if(!the_cond) {
        return ENOMEM;
    }

    if(!fiber_cond_init(the_cond)) {
        return ENOMEM;
    }

    *((fiber_cond_t**)cond) = the_cond;

    return 0;
}

int pthread_cond_destroy(pthread_cond_t * cond)
{
    if(!cond) {
        return EINVAL;
    }

    fiber_cond_t* const the_cond = *((fiber_cond_t**)cond);

    if(the_cond->waiter_count) {//best effort
        return EBUSY;
    }

    fiber_cond_destroy(the_cond);
    free(the_cond);
    memcpy(cond, &default_pthread_cond, sizeof(*cond));

    return 0;
}

int pthread_cond_signal(pthread_cond_t * cond)
{
    if(!cond) {
        return EINVAL;
    }
    fiber_cond_signal(*((fiber_cond_t**)cond));
    return 0;
}

int pthread_cond_broadcast(pthread_cond_t * cond)
{
    if(!cond) {
        return EINVAL;
    }
    fiber_cond_broadcast(*((fiber_cond_t**)cond));
    return 0;
}

int pthread_cond_timedwait(pthread_cond_t * cond,  pthread_mutex_t * mutex, const struct timespec * abstime)
{
    //TODO
    assert(0 && "ERROR: timed wait not supported by fibers");
    abort();
    return 0;
}

int pthread_cond_wait(pthread_cond_t * cond, pthread_mutex_t * mutex)
{
    if(!cond) {
        return EINVAL;
    }
    fiber_cond_t* const the_cond = *((fiber_cond_t**)cond);
    fiber_pthread_mutex_t* the_mutex = *((fiber_pthread_mutex_t**)mutex);
    if(the_cond->caller_mutex && &the_mutex->mutex != the_cond->caller_mutex) {
        return EINVAL;
    }
    if(the_mutex->owner != fiber_manager_get()->current_fiber) {
        return EPERM;
    }

    the_mutex->owner = NULL;//fiber_cond_wait doesn't maintain the mutex for us!
    fiber_cond_wait(the_cond, &the_mutex->mutex);
    the_mutex->owner = fiber_manager_get()->current_fiber;//fiber_cond_wait doesn't maintain the mutex for us!
    return 0;
}

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

int pthread_setschedprio(pthread_t thread, int prio)
{
    //NOT IMPLEMENTED!
    return ENOTSUP;
}

int pthread_barrier_destroy(pthread_barrier_t* barrier)
{
    if(!barrier) {
        return EINVAL;
    }
    fiber_barrier_t* const the_barrier = *((fiber_barrier_t**)barrier);
    if(!the_barrier) {
        return EINVAL;
    }
    if(the_barrier->counter % the_barrier->count) {
        return EBUSY;
    }
    fiber_barrier_destroy(the_barrier);
    free(the_barrier);
    memset(barrier, 0, sizeof(*barrier));
    return 0;
}

int pthread_barrier_init(pthread_barrier_t* barrier, const pthread_barrierattr_t* attr, unsigned count)
{
    if(!barrier) {
        return EINVAL;
    }
    if(!count) {
        return EINVAL;
    }
    if(attr) {
        int pshared = 0;
        if(pthread_barrierattr_getpshared(attr, &pshared)
           || pshared) {
            return EINVAL;
        }
    }

    fiber_barrier_t* const the_barrier = (fiber_barrier_t*)malloc(sizeof(*the_barrier));
    if(!the_barrier) {
        return ENOMEM;
    }
    if(!fiber_barrier_init(the_barrier, count)) {
        free(the_barrier);
        return ENOMEM;
    }

    *((fiber_barrier_t**)barrier) = the_barrier;

    return 0;
}

int pthread_barrier_wait(pthread_barrier_t* barrier)
{
    if(!barrier) {
        return EINVAL;
    }
    fiber_barrier_t* const the_barrier = *((fiber_barrier_t**)barrier);
    if(!the_barrier) {
        return EINVAL;
    }

    const int ret = fiber_barrier_wait(the_barrier);
    return ret == FIBER_BARRIER_SERIAL_FIBER ? PTHREAD_BARRIER_SERIAL_THREAD : 0;
}

//TODO:
/*
< pthread_atfork
< pthread_getaffinity_np
< pthread_getcpuclockid
< pthread_getname_np
< pthread_mutex_consistent
< pthread_mutex_consistent_np
< pthread_mutex_timedlock
< pthread_rwlock_timedrdlock
< pthread_rwlock_timedwrlock
< pthread_setaffinity_np
< pthread_setname_np
< pthread_spin_destroy
< pthread_spin_init
< pthread_spin_lock
< pthread_spin_trylock
< pthread_spin_unlock
< pthread_timedjoin_np
< pthread_tryjoin_np
< pthread_yield
*/

