#include "test_helper.h"
#include "fiber_manager.h"
#include "fiber_barrier.h"
#include "mpsc_fifo.h"

int volatile counter = 0;
#define PER_FIBER_COUNT 100000
#define NUM_FIBERS 100
#define NUM_THREADS 4

mpsc_fifo_t fifo;

//TODO: productize this
typedef struct fiber_work_queue
{
    mpsc_fifo_t fifo;
    volatile int64_t in_count;
    char _cache_padding1[CACHE_SIZE - sizeof(int64_t)];
    volatile int64_t out_count;
} fiber_work_queue_t;

typedef mpsc_fifo_node_t fiber_work_queue_item_t;

int fiber_work_queue_init(fiber_work_queue_t* wq)
{
    assert(wq);
    wq->in_count = 0;
    wq->out_count = 0;
    if(!mpsc_fifo_init(&wq->fifo)) {
        return FIBER_ERROR;
    }
    return FIBER_SUCCESS;
}

void fiber_work_queue_destroy(fiber_work_queue_t* wq)
{
    if(wq) {
        mpsc_fifo_destroy(&wq->fifo);
    }
}

#define FIBER_WORK_QUEUE_START_WORKING (1)
#define FIBER_WORK_QUEUE_QUEUED (0)

//the work queue owns item after pushing
int fiber_work_queue_push(fiber_work_queue_t* wq, fiber_work_queue_item_t* item)
{
    assert(wq);
    assert(item);
    int ret = FIBER_WORK_QUEUE_QUEUED;
    const int64_t in_count = __sync_add_and_fetch(&wq->in_count, 1);
    if(in_count == 1) {
        //we got here first; we'll be the worker
        ret = FIBER_WORK_QUEUE_START_WORKING;
    }
    mpsc_fifo_push(&wq->fifo, item);
    return ret;
}

#define FIBER_WORK_QUEUE_MORE_WORK (1)
#define FIBER_WORK_QUEUE_EMPTY (0)

//caller owns *out afterwards.
//returns the amount of work on the queue (including the item returned)
int fiber_work_queue_get_work(fiber_work_queue_t* wq, fiber_work_queue_item_t** out)
{
    assert(wq);
    assert(out);
    while(!(*out = mpsc_fifo_trypop(&wq->fifo))) {
        if(wq->out_count == wq->in_count) {
            const int64_t old_out_count = wq->out_count;
            wq->out_count = 0;
            const int64_t new_in_count = __sync_sub_and_fetch(&wq->in_count, old_out_count);
            if(new_in_count == 0) {
                return FIBER_WORK_QUEUE_EMPTY;
            }
        }
    }
    wq->out_count += 1;
    return FIBER_WORK_QUEUE_MORE_WORK;
}

fiber_work_queue_t work_queue;
fiber_barrier_t barrier;

void* run_function(void* param)
{
    fiber_work_queue_item_t* node = NULL;
    int i;
    for(i = 0; i < PER_FIBER_COUNT; ++i) {
        fiber_work_queue_item_t* next = node;
        node = calloc(1, sizeof(*node));
        test_assert(node);
        node->next = next;
    }
    fiber_barrier_wait(&barrier);

    for(i = 0; i < PER_FIBER_COUNT; ++i) {
        fiber_work_queue_item_t* const to_push = node;
        node = node->next;
        const int push_ret = fiber_work_queue_push(&work_queue, to_push);
        if(push_ret == FIBER_WORK_QUEUE_START_WORKING) {
            fiber_work_queue_item_t* work;
            while(fiber_work_queue_get_work(&work_queue, &work) != FIBER_WORK_QUEUE_EMPTY) {
                ++counter;
                free(work);
            }
        }
        fiber_yield();
    }
    return NULL;
}

int main()
{
    fiber_manager_init(NUM_THREADS);

    fiber_barrier_init(&barrier, NUM_FIBERS);
    fiber_work_queue_init(&work_queue);

    fiber_t* fibers[NUM_FIBERS];
    int i;
    for(i = 0; i < NUM_FIBERS; ++i) {
        fibers[i] = fiber_create(20000, &run_function, NULL);
    }

    for(i = 0; i < NUM_FIBERS; ++i) {
        fiber_join(fibers[i], NULL);
    }

    test_assert(counter == NUM_FIBERS * PER_FIBER_COUNT);
    fiber_work_queue_destroy(&work_queue);

    return 0;
}
