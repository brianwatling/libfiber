#include "fiber_manager.h"
#include "test_helper.h"
#include "fiber_event.h"
#include "fiber_io.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#define NUM_THREADS 2
#define NUM_FIBERS 1000

volatile int done_count = 0;

void* sleep_function(void* param)
{
    int i;
    for(i = 0; i < 1000; ++i) {
        usleep(rand() % 1000 + 1000);
    }
    __sync_fetch_and_add(&done_count, 1);
    return NULL;
}

int main()
{
    fiber_manager_init(NUM_THREADS);

    fiber_t* fibers[NUM_FIBERS];
    int i;
    for(i = 0; i < NUM_FIBERS; ++i) {
        fibers[i] = fiber_create(100000, &sleep_function, NULL);
    }

    int join_count = 0;
    while(join_count < NUM_FIBERS) {
        for(i = 0; i < NUM_FIBERS; ++i) {
            if(fiber_tryjoin(fibers[i], NULL)) {
                ++join_count;
            }
        }
        usleep(10000);
        printf("tryjoin. joined: %d done %d\n", join_count, done_count);
    }

    fiber_event_destroy();

    fiber_manager_print_stats();
    return 0;
}

