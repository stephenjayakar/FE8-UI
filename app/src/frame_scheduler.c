#include "frame_scheduler.h"

unsigned fe8_scheduler_batch_limit(bool speedup_active, unsigned multiplier) {
    if (!speedup_active)
        return 1;
    return multiplier ? multiplier : 16;
}

bool fe8_scheduler_unlimited_should_present(
    unsigned frames, uint64_t elapsed_ticks, uint64_t frequency) {
    return frames >= 16 || (frequency != 0 && elapsed_ticks >= frequency * 16 / 1000);
}
