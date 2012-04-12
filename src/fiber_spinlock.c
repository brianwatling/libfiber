#include "fiber_spinlock.h"
#include "fiber_manager.h"
#include "fiber.h"
#include "sched.h"

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

STATIC_ASSERT(sizeof(fiber_spinlock_internal_t) == sizeof(uint64_t), state_is_not_sized_properly);

int fiber_spinlock_init(fiber_spinlock_t* spinlock)
{
    assert(spinlock);
    spinlock->state.blob = 0;
    write_barrier();
    return FIBER_SUCCESS;
}

int fiber_spinlock_destroy(fiber_spinlock_t* spinlock)
{
    assert(spinlock);
    return FIBER_SUCCESS;
}

int fiber_spinlock_lock(fiber_spinlock_t* spinlock)
{
    assert(spinlock);

    const uint32_t my_ticket = __sync_fetch_and_add(&spinlock->state.counters.users, 1);
    while(spinlock->state.counters.ticket != my_ticket) {
        cpu_relax();
        fiber_manager_get()->spin_count += 1;
    }
    load_load_barrier();//any future reads should happen after reading the new ticket

    return FIBER_SUCCESS;
}

int fiber_spinlock_trylock(fiber_spinlock_t* spinlock)
{
    assert(spinlock);

    fiber_spinlock_internal_t old;
    old.blob = spinlock->state.blob;
    old.counters.ticket = old.counters.users;
    fiber_spinlock_internal_t new;
    new.blob = old.blob;
    new.counters.users += 1;
    if(!__sync_bool_compare_and_swap(&spinlock->state.blob, old.blob, new.blob)) {
        return FIBER_ERROR;
    }

    return FIBER_SUCCESS;
}

int fiber_spinlock_unlock(fiber_spinlock_t* spinlock)
{
    assert(spinlock);

    write_barrier();//flush this fiber's writes before incrementing the ticket
    spinlock->state.counters.ticket = spinlock->state.counters.ticket + 1;
    load_load_barrier();//any future read should happen after reading the ticket as part of the increment

    return FIBER_SUCCESS;
}

