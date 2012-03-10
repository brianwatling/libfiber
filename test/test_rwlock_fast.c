#include "fiber_barrier.h"
#include "fiber_manager.h"
#include "test_helper.h"

//21 bits counters; supports up to roughly 2 million readers or writers
typedef union
{
    struct
    {
        unsigned int write_locked : 1;
        unsigned int reader_count : 21;
        unsigned int waiting_readers : 21;
        unsigned int waiting_writers : 21;
    } __attribute__ ((packed)) state;
    uint64_t blob;
} fiber_rwlock_state_t;

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

STATIC_ASSERT(sizeof(fiber_rwlock_state_t) == sizeof(uint64_t), state_is_not_sized_properly);

typedef struct fiber_rwlock
{
    fiber_rwlock_state_t state;
    mpsc_fifo_t write_waiters;
    mpsc_fifo_t read_waiters;
} fiber_rwlock_t;

int fiber_rwlock_init(fiber_rwlock_t* rwlock)
{
    assert(rwlock);
    if(!mpsc_fifo_init(&rwlock->write_waiters)
       || !mpsc_fifo_init(&rwlock->read_waiters)) {
        mpsc_fifo_destroy(&rwlock->write_waiters);
        mpsc_fifo_destroy(&rwlock->read_waiters);
        return FIBER_ERROR;
    }
    rwlock->state.blob = 0;
    write_barrier();
    return FIBER_SUCCESS;
}

void fiber_rwlock_destroy(fiber_rwlock_t* rwlock)
{
    if(rwlock) {
        mpsc_fifo_destroy(&rwlock->write_waiters);
        mpsc_fifo_destroy(&rwlock->read_waiters);
    }
}

int fiber_rwlock_rdlock(fiber_rwlock_t* rwlock)
{
    assert(rwlock);

    fiber_rwlock_state_t current_state;
    while(1) {
        const uint64_t snapshot = rwlock->state.blob;
        current_state.blob = snapshot;
        if(current_state.state.waiting_writers || current_state.state.write_locked || current_state.state.waiting_readers) {
            current_state.state.waiting_readers += 1;
            if(__sync_bool_compare_and_swap(&rwlock->state.blob, snapshot, current_state.blob)) {
                //currently write locked or a writer is waiting - be friendly and wait
                fiber_manager_wait_in_mpsc_queue(fiber_manager_get(), &rwlock->read_waiters);
                break;
            }
        } else {
            current_state.state.reader_count += 1;
            if(__sync_bool_compare_and_swap(&rwlock->state.blob, snapshot, current_state.blob)) {
                //currently read locked
                break;
            }
        }
    }
    return FIBER_SUCCESS;
}

int fiber_rwlock_wrlock(fiber_rwlock_t* rwlock)
{
    assert(rwlock);

    fiber_rwlock_state_t current_state;
    while(1) {
        const uint64_t snapshot = rwlock->state.blob;
        current_state.blob = snapshot;
        if(current_state.blob != 0) {
            current_state.state.waiting_writers += 1;
            if(__sync_bool_compare_and_swap(&rwlock->state.blob, snapshot, current_state.blob)) {
                //currently locked or a reader is waiting - be friendly and wait
                fiber_manager_wait_in_mpsc_queue(fiber_manager_get(), &rwlock->write_waiters);
                break;
            }
        } else {
            current_state.state.write_locked = 1;
            if(__sync_bool_compare_and_swap(&rwlock->state.blob, snapshot, current_state.blob)) {
                //currently write locked
                break;
            }
        }
    }
    return FIBER_SUCCESS;
}

int fiber_rwlock_tryrdlock(fiber_rwlock_t* rwlock)
{
    assert(rwlock);

    fiber_rwlock_state_t current_state;
    while(1) {
        const uint64_t snapshot = rwlock->state.blob;
        current_state.blob = snapshot;
        if(current_state.state.waiting_writers || current_state.state.write_locked || current_state.state.waiting_readers) {
            return FIBER_ERROR;
        }
        current_state.state.reader_count += 1;
        if(__sync_bool_compare_and_swap(&rwlock->state.blob, snapshot, current_state.blob)) {
            break;
        }
    }
    return FIBER_SUCCESS;
}

int fiber_rwlock_trywrlock(fiber_rwlock_t* rwlock)
{
    assert(rwlock);

    fiber_rwlock_state_t current_state;
    while(1) {
        const uint64_t snapshot = rwlock->state.blob;
        current_state.blob = snapshot;
        if(current_state.blob != 0) {
            return FIBER_ERROR;
        }
        current_state.state.write_locked = 1;
        if(__sync_bool_compare_and_swap(&rwlock->state.blob, snapshot, current_state.blob)) {
            break;
        }
    }
    return FIBER_SUCCESS;
}

int fiber_rwlock_rdunlock(fiber_rwlock_t* rwlock)
{
    assert(rwlock);

    fiber_rwlock_state_t current_state;
    while(1) {
        const uint64_t snapshot = rwlock->state.blob;
        current_state.blob = snapshot;
        assert(current_state.state.reader_count > 0);
        assert(!current_state.state.write_locked);
        current_state.state.reader_count -= 1;
        if(!current_state.state.reader_count) {
            //if we're the last reader then we are responsible to wake up waiters

            if(current_state.state.waiting_writers) {
                //no fiber will acquire the lock while waiting_writers != 0
                current_state.state.write_locked = 1;
                current_state.state.waiting_writers -= 1;
                if(__sync_bool_compare_and_swap(&rwlock->state.blob, snapshot, current_state.blob)) {
                    fiber_manager_wake_from_mpsc_queue(fiber_manager_get(), &rwlock->write_waiters, 1);
                    break;
                }
                continue;
            }
            if(current_state.state.waiting_readers) {
                //no fiber will acquire the lock while waiting_readers != 0
                current_state.state.reader_count = current_state.state.waiting_readers;
                current_state.state.waiting_readers = 0;
                if(__sync_bool_compare_and_swap(&rwlock->state.blob, snapshot, current_state.blob)) {
                    fiber_manager_wake_from_mpsc_queue(fiber_manager_get(), &rwlock->read_waiters, current_state.state.reader_count);
                    break;
                }
                continue;
            }
        }
        if(__sync_bool_compare_and_swap(&rwlock->state.blob, snapshot, current_state.blob)) {
            break;
        }
    }
    return FIBER_SUCCESS;
}

int fiber_rwlock_wrunlock(fiber_rwlock_t* rwlock)
{
    assert(rwlock);

    fiber_rwlock_state_t current_state;
    while(1) {
        const uint64_t snapshot = rwlock->state.blob;
        current_state.blob = snapshot;
        assert(!current_state.state.reader_count);
        assert(current_state.state.write_locked);
        current_state.state.write_locked = 0;
        //we are responsible to wake up waiters

        if(current_state.state.waiting_writers) {
            //no fiber will acquire the lock while write_locked = 1
            current_state.state.write_locked = 1;
            current_state.state.waiting_writers -= 1;
            if(__sync_bool_compare_and_swap(&rwlock->state.blob, snapshot, current_state.blob)) {
                fiber_manager_wake_from_mpsc_queue(fiber_manager_get(), &rwlock->write_waiters, 1);
                break;
            }
            continue;
        }
        if(current_state.state.waiting_readers) {
            //no fiber will acquire the lock while waiting_readers != 0
            current_state.state.reader_count = current_state.state.waiting_readers;
            current_state.state.waiting_readers = 0;
            if(__sync_bool_compare_and_swap(&rwlock->state.blob, snapshot, current_state.blob)) {
                fiber_manager_wake_from_mpsc_queue(fiber_manager_get(), &rwlock->read_waiters, current_state.state.reader_count);
                break;
            }
            continue;
        }
        if(__sync_bool_compare_and_swap(&rwlock->state.blob, snapshot, current_state.blob)) {
            break;
        }
    }
    return FIBER_SUCCESS;
}

#define PER_FIBER_COUNT 100000
#define NUM_FIBERS 100
#define NUM_THREADS 2

fiber_rwlock_t mutex;
volatile int counter = 0;
#define READING 1
#define WRITING -1
#define NONE 0
int state = NONE;
fiber_barrier_t barrier;
volatile int try_wr = 0;
volatile int try_rd = 0;
volatile int count_rd = 0;
volatile int count_wr = 0;

void* run_function(void* param)
{
    fiber_barrier_wait(&barrier);
    int i;
    for(i = 0; i < PER_FIBER_COUNT || (!try_wr || !try_rd); ++i) {
        if(i % 10 == 0) {
            fiber_rwlock_wrlock(&mutex);
            __sync_fetch_and_add(&count_wr, 1);
            int old_state = atomic_exchange_int(&state, WRITING);
            test_assert(old_state == NONE);
            old_state = atomic_exchange_int(&state, NONE);
            test_assert(old_state == WRITING);
            fiber_rwlock_wrunlock(&mutex);
        } else if(i % 10 == 1 && fiber_rwlock_trywrlock(&mutex)) {
            __sync_fetch_and_add(&try_wr, 1);
            int old_state = atomic_exchange_int(&state, WRITING);
            test_assert(old_state == NONE);
            old_state = atomic_exchange_int(&state, NONE);
            test_assert(old_state == WRITING);
            fiber_rwlock_wrunlock(&mutex);
        } else if(i % 10 == 2 && fiber_rwlock_tryrdlock(&mutex)) {
            __sync_fetch_and_add(&try_rd, 1);
            int old_state = atomic_exchange_int(&state, READING);
            test_assert(old_state == NONE || old_state == READING);
            old_state = atomic_exchange_int(&state, NONE);
            test_assert(old_state == NONE || old_state == READING);
            fiber_rwlock_rdunlock(&mutex);
        } else {
            fiber_rwlock_rdlock(&mutex);
            __sync_fetch_and_add(&count_rd, 1);
            int old_state = atomic_exchange_int(&state, READING);
            test_assert(old_state == NONE || old_state == READING);
            old_state = atomic_exchange_int(&state, NONE);
            test_assert(old_state == NONE || old_state == READING);
            fiber_rwlock_rdunlock(&mutex);
        }
    }
    return NULL;
}

int main()
{
    fiber_manager_init(NUM_THREADS);

    fiber_rwlock_init(&mutex);
    fiber_barrier_init(&barrier, NUM_FIBERS);

    fiber_t* fibers[NUM_FIBERS];
    int i;
    for(i = 0; i < NUM_FIBERS; ++i) {
        fibers[i] = fiber_create(20000, &run_function, NULL);
    }

    for(i = 0; i < NUM_FIBERS; ++i) {
        fiber_join(fibers[i], NULL);
    }

    fiber_barrier_destroy(&barrier);
    fiber_rwlock_destroy(&mutex);

    printf("try_rd %d try_wr %d count_rd %d count_wr %d\n", try_rd, try_wr, count_rd, count_wr);

    return 0;
}

