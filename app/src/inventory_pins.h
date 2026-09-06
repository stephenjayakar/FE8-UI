#ifndef FE8_INVENTORY_PINS_H
#define FE8_INVENTORY_PINS_H

#include "inventory_desktop.h"

/* A derived board, not a reordered snapshot. Indices are consumed immediately;
   pins themselves use address + character ID so a reused unit slot is not pinned. */
typedef struct Fe8InventoryBoardView {
    int pinned[FE8_INVENTORY_UNIT_CAPACITY];
    int others[FE8_INVENTORY_UNIT_CAPACITY];
    int pinned_count, other_count;
    int pinned_rows, other_rows, pinned_start, other_start;
    int top, other_y, row_height, card_height;
} Fe8InventoryBoardView;

int fe8_inventory_unit_pinned(const Fe8InventoryUi *ui, const Fe8InventoryUnit *unit);
void fe8_inventory_pins_reconcile(Fe8InventoryUi *ui, const Fe8InventorySnapshot *snapshot);
/* 1 = pinned, -1 = unpinned, 0 = invalid. Never changes selection or game data. */
int fe8_inventory_pin_toggle(Fe8InventoryUi *ui, const Fe8InventorySnapshot *snapshot, int unit);
void fe8_inventory_board_view(const Fe8InventoryUi *ui, const Fe8InventorySnapshot *snapshot,
    const Fe8InventoryDesktopLayout *layout, Fe8InventoryBoardView *view);
/* Canonical snapshot index at logical y; headers, gaps and blank rows return -1. */
int fe8_inventory_board_unit_at(const Fe8InventoryBoardView *view, int y, int *row_y);
#endif
