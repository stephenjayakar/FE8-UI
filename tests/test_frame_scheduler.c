#include "frame_scheduler.h"

#include <assert.h>
#include <stdio.h>

int main(void) {
    assert(fe8_scheduler_batch_limit(false, 0) == 1);
    assert(fe8_scheduler_batch_limit(true, 2) == 2);
    assert(fe8_scheduler_batch_limit(true, 3) == 3);
    assert(fe8_scheduler_batch_limit(true, 4) == 4);
    assert(fe8_scheduler_batch_limit(true, 0) == 16);
    assert(!fe8_scheduler_unlimited_should_present(15, 15, 1000));
    assert(fe8_scheduler_unlimited_should_present(16, 0, 1000));
    assert(fe8_scheduler_unlimited_should_present(1, 16, 1000));
    puts("frame scheduler tests passed");
    return 0;
}
