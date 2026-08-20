#ifndef FE8_PREBATTLE_INVENTORY_UI_H
#define FE8_PREBATTLE_INVENTORY_UI_H

#include "prebattle_inventory.h"

#include <stdint.h>

typedef enum Fe8InventoryHitKind {
    FE8_INVENTORY_HIT_NONE,
    FE8_INVENTORY_HIT_ROSTER,
    FE8_INVENTORY_HIT_UNIT_ITEM,
    FE8_INVENTORY_HIT_SUPPLY_ITEM,
} Fe8InventoryHitKind;

typedef struct Fe8InventoryUi {
    int active;
    int roster_scroll;
    int supply_scroll;
    int current_unit;
    int render_scale;
    Fe8InventoryEndpoint selected;
    int has_selection;
    Fe8InventoryEndpoint inspected;
    int has_inspected;
    char status[96];
} Fe8InventoryUi;

void fe8_inventory_ui_init(Fe8InventoryUi *ui);
void fe8_inventory_ui_open(Fe8InventoryUi *ui);
void fe8_inventory_ui_scroll(Fe8InventoryUi *ui, int rows,
    const Fe8InventorySnapshot *snapshot, int canvas_width, int canvas_height, int pointer_x);
Fe8InventoryHitKind fe8_inventory_ui_hit_test(const Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *snapshot, int canvas_width, int canvas_height,
    int x, int y, int *index_out);
Fe8InventoryEndpoint fe8_inventory_ui_endpoint(const Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *snapshot, Fe8InventoryHitKind kind, int index);
uint16_t fe8_inventory_ui_endpoint_item(const Fe8InventorySnapshot *snapshot,
    Fe8InventoryEndpoint endpoint);
int fe8_inventory_ui_endpoint_movable(const Fe8InventorySnapshot *snapshot,
    Fe8InventoryEndpoint endpoint);
void fe8_inventory_ui_inspect(Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *snapshot, Fe8InventoryHitKind kind, int index);
void fe8_inventory_ui_draw(const Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *snapshot, uint32_t *pixels, int stride, int width, int height);

#endif
