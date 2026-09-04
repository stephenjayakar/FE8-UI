#ifndef FE8_PREBATTLE_INVENTORY_UI_H
#define FE8_PREBATTLE_INVENTORY_UI_H

#include "prebattle_inventory.h"

#include <stdint.h>

enum {
    FE8_INVENTORY_POOL_CAPACITY =
        FE8_SUPPLY_MAX_CAPACITY +
        FE8_INVENTORY_UNIT_CAPACITY * FE8_INVENTORY_ITEM_SLOTS,
};

typedef enum Fe8InventoryHitKind {
    FE8_INVENTORY_HIT_NONE,
    FE8_INVENTORY_HIT_ROSTER,
    FE8_INVENTORY_HIT_UNIT_ITEM,
    FE8_INVENTORY_HIT_POOL_ITEM,
    FE8_INVENTORY_HIT_POOL_SCOPE,
    FE8_INVENTORY_HIT_POOL_SORT,
    FE8_INVENTORY_HIT_UNIT_NAME,
    FE8_INVENTORY_HIT_UNIT_CLASS,
    FE8_INVENTORY_HIT_ROSTER_CLASS,
    FE8_INVENTORY_HIT_DENSITY,
    FE8_INVENTORY_HIT_SORT_COLUMN,
    /* Source compatibility for callers that still use the old right-pane name. */
    FE8_INVENTORY_HIT_SUPPLY_ITEM = FE8_INVENTORY_HIT_POOL_ITEM,
} Fe8InventoryHitKind;

typedef enum Fe8InventoryPoolScope {
    FE8_INVENTORY_POOL_ALL,
    FE8_INVENTORY_POOL_SUPPLY,
} Fe8InventoryPoolScope;

typedef enum Fe8InventorySort {
    FE8_INVENTORY_SORT_TYPE,
    FE8_INVENTORY_SORT_NAME,
    FE8_INVENTORY_SORT_USES,
    FE8_INVENTORY_SORT_OWNER,
    FE8_INVENTORY_SORT_COUNT,
} Fe8InventorySort;

typedef struct Fe8InventoryListEntry {
    Fe8InventoryEndpoint endpoint;
    const Fe8ItemInfo *info;
    uint16_t item;
    int unit_index;
} Fe8InventoryListEntry;

typedef struct Fe8InventoryUi {
    int active;
    int roster_scroll;
    int pool_scroll;
    int current_unit;
    int render_scale; /* Legacy pixel-art layout, retained for fallback tests. */
    int desktop;
    float desktop_scale; /* Drawable pixels per desktop point, NOT game zoom. */
    int zoom_percent; /* User UI size, separate from drawable/Retina density. */
    int comfortable;
    int previous_min_width, previous_min_height;
    Fe8InventoryPoolScope pool_scope;
    Fe8InventorySort pool_sort;
    Fe8InventoryListEntry pool[FE8_INVENTORY_POOL_CAPACITY];
    int pool_count;
    Fe8InventoryEndpoint selected;
    int has_selection;
    Fe8InventoryEndpoint inspected;
    int has_inspected;
    /* Hover is separate from the item being moved. Unit help is keyed by
       address, not a roster index that could become stale after a refresh. */
    Fe8InventoryHitKind hover_kind;
    uint32_t hover_unit_address;
    char status[96];
} Fe8InventoryUi;

void fe8_inventory_ui_init(Fe8InventoryUi *ui);
void fe8_inventory_ui_open(Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *snapshot);
void fe8_inventory_ui_rebuild(Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *snapshot);
void fe8_inventory_ui_toggle_scope(Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *snapshot);
void fe8_inventory_ui_cycle_sort(Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *snapshot);
const char *fe8_inventory_ui_scope_name(Fe8InventoryPoolScope scope);
const char *fe8_inventory_ui_sort_name(Fe8InventorySort sort);
int fe8_inventory_ui_pool_count(const Fe8InventoryUi *ui);
int fe8_inventory_ui_pool_entry(const Fe8InventoryUi *ui, int index,
    Fe8InventoryListEntry *entry);
void fe8_inventory_ui_scroll(Fe8InventoryUi *ui, int rows,
    const Fe8InventorySnapshot *snapshot, int canvas_width, int canvas_height,
    int pointer_x);
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
/* Returns ROM-backed name/class help for the current hover, or NULL. */
const char *fe8_inventory_ui_unit_help(const Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *snapshot, const char **title);
void fe8_inventory_ui_draw(const Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *snapshot, uint32_t *pixels, int stride,
    int width, int height);

#endif
