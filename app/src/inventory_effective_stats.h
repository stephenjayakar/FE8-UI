#ifndef FE8_INVENTORY_EFFECTIVE_STATS_H
#define FE8_INVENTORY_EFFECTIVE_STATS_H

#include "prebattle_inventory.h"

struct mCore;
typedef struct Fe8StatEvaluator Fe8StatEvaluator;

/* A private, in-memory emulator, never the running game's CPU. Only verified
   ROM revisions may execute profile-specific unit-stat getters. */
Fe8StatEvaluator *fe8_stat_evaluator_create(struct mCore *source);
void fe8_stat_evaluator_destroy(Fe8StatEvaluator *evaluator);

/* Call on a coherent paused snapshot, after opening, transfers and undo, not
   every painted frame. Raw fields remain unchanged. Failure clears native
   validity so callers can explicitly display base stats instead of stale totals. */
bool fe8_stat_evaluator_refresh(Fe8StatEvaluator *evaluator,
    struct mCore *source, Fe8InventorySnapshot *snapshot);

#endif
