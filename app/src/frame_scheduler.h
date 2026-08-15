#ifndef FE8_FRAME_SCHEDULER_H
#define FE8_FRAME_SCHEDULER_H

#include <stdbool.h>
#include <stdint.h>

unsigned fe8_scheduler_batch_limit(bool speedup_active, unsigned multiplier);
bool fe8_scheduler_unlimited_should_present(
    unsigned frames, uint64_t elapsed_ticks, uint64_t frequency);

#endif
