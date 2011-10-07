#include "fiber_manager.h"
#include "test_helper.h"

void* run_function(void* param)
{
    int* value = (int*)param;
    *value += 1;
    fiber_yield();
    *value += 1;
    return NULL;
}

int main()
{
    fiber_manager_set_total_kernel_threads(1);
    int volatile value = 0;
    fiber_t* fiber1 = fiber_create(20000, &run_function, (void*)&value);

    fiber_yield();
    test_assert(value == 1);
    fiber_join(fiber1);
    test_assert(value == 2);

    fiber_t* fiber2 = fiber_create(20000, &run_function, (void*)&value);
    assert(fiber1 == fiber2);//we know fiber1 has finished - it should be re-used

    fiber_yield();
    test_assert(value == 3);
    fiber_yield();
    test_assert(value == 4);
    //at this point fiber2 should be finished.
    test_assert(fiber2->state == FIBER_STATE_DONE);

    //now, joining fiber2 should still be fine
    fiber_join(fiber2);

    return 0;
}

