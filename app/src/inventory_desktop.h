#ifndef FE8_INVENTORY_DESKTOP_H
#define FE8_INVENTORY_DESKTOP_H

#include "prebattle_inventory_ui.h"

/* One geometry model for painting, pointer hit testing, and regression tests.
   All coordinates are desktop points; the framebuffer uses drawable pixels. */
typedef struct Fe8InventoryDesktopLayout {
    int width, height, sidebar, pool_x, pool_width;
    int top, bottom, row_height, side_row_height, items_y, roster_y, roster_rows;
    int table_y, table_rows, deposit_y, help_y;
    int column_x[11], column_width[11];
    int search_y, filters_y, filter_height, filter_columns;
    int detail_x, detail_y, detail_width, detail_height, detail_wide;
    int action_x, action_y, action_width;
    int detail_collapsed, detail_overlay, usable_width;
    int board_x, board_y, board_width, board_row_height, board_rows;
    int identity_width, slot_width, supply_x, supply_width;
    int popup_x, popup_y, popup_width, popup_height, popup_rows_y;

    int quick_x, quick_width;
    int stats_y, stat_row_height, board_card_height;
} Fe8InventoryDesktopLayout;

int fe8_inventory_ui_scale_percent(const Fe8InventoryUi *ui, int width, int height);
float fe8_inventory_desktop_scale(const Fe8InventoryUi *ui, int width, int height);
void fe8_inventory_ui_adjust_scale(Fe8InventoryUi *ui, int direction, int width, int height);
void fe8_inventory_desktop_layout(const Fe8InventoryUi *ui, int width, int height,
    Fe8InventoryDesktopLayout *layout);
void fe8_inventory_desktop_draw(const Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *snapshot, uint32_t *pixels, int stride, int width, int height);
Fe8InventoryHitKind fe8_inventory_desktop_hit(const Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *snapshot, int width, int height, int x, int y, int *index);
void fe8_inventory_desktop_scroll(Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *snapshot, int width, int height, int x, int rows);
void fe8_inventory_desktop_scroll_at(Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *snapshot, int width, int height, int x, int y, int rows);
void fe8_inventory_ui_toggle_density(Fe8InventoryUi *ui);

/* Pure derived view. Empty supply destinations are deliberately not filtered. */
int fe8_inventory_desktop_visible(const Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *snapshot, int indices[FE8_INVENTORY_POOL_CAPACITY]);
/* Name/class or carried-equipment query match, independent of pool scope.
   Pinned units bypass this filter in the board view, not in this predicate. */
int fe8_inventory_desktop_unit_matches(const Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *snapshot, int unit);
void fe8_inventory_desktop_clear_query(Fe8InventoryUi *ui);
void fe8_inventory_desktop_text(Fe8InventoryUi *ui, const char *utf8);
void fe8_inventory_desktop_backspace(Fe8InventoryUi *ui);
/* Returns 1 when the view consumed a click without any game-memory write.
   Otherwise resolves explicit Give/Store actions to the existing guarded
   endpoint transaction in main.c. Never writes inventory or emulated memory. */
int fe8_inventory_desktop_click(Fe8InventoryUi *ui, const Fe8InventorySnapshot *snapshot,
    Fe8InventoryHitKind *kind, int *index);

/* Pointer coordinates are drawable pixels, matching hit testing. Pointer-up
   returns the same consumed/resolved convention as click(); main owns writes. */
void fe8_inventory_desktop_pointer_down(Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *snapshot, Fe8InventoryHitKind kind, int index, int x, int y);
void fe8_inventory_desktop_pointer_motion(Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *snapshot, int width, int height, int x, int y);
int fe8_inventory_desktop_pointer_up(Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *snapshot, Fe8InventoryHitKind *kind, int *index);
void fe8_inventory_desktop_cancel_drag(Fe8InventoryUi *ui);

/* Read-only explanations and comparison targets, shared by all views. */
void fe8_inventory_desktop_use_reason(const Fe8InventorySnapshot *snapshot,
    const Fe8InventoryUnit *unit, const Fe8ItemInfo *item, char *out, size_t capacity);
int fe8_inventory_desktop_comparison(const Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *snapshot, Fe8InventoryEndpoint *endpoint);
void fe8_inventory_desktop_feedback(Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *before, Fe8InventoryEndpoint from,
    Fe8InventoryEndpoint to, int undo);
void fe8_inventory_desktop_cancel_move(Fe8InventoryUi *ui);
#endif
