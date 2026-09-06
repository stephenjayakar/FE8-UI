#ifndef FE8_INVENTORY_HISTORY_H
#define FE8_INVENTORY_HISTORY_H
#include "prebattle_inventory.h"

enum { FE8_INVENTORY_HISTORY_CAPACITY = 32 };
typedef struct Fe8InventoryChange {
    Fe8InventoryEndpoint first, second;
    uint16_t first_item, second_item;
} Fe8InventoryChange;
typedef struct Fe8InventoryHistory {
    Fe8InventoryChange changes[FE8_INVENTORY_HISTORY_CAPACITY];
    int count;
} Fe8InventoryHistory;

/* A paused-session history. The caller clears it on resume/ROM or state load.
   Failed operations neither append nor pop history. Every undo revalidates the
   inverted expected values through the same guarded transaction as a move. */
bool fe8_inventory_history_transfer(Fe8InventoryHistory *history,
    const Fe8MemoryReader *reader, const Fe8MemoryWriter *writer,
    const Fe8Profile *profile, Fe8InventoryEndpoint first, uint16_t first_item,
    Fe8InventoryEndpoint second, uint16_t second_item);
bool fe8_inventory_history_undo(Fe8InventoryHistory *history,
    const Fe8MemoryReader *reader, const Fe8MemoryWriter *writer,
    const Fe8Profile *profile);
#endif
