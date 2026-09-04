#ifndef FE8_INVENTORY_DESKTOP_H
#define FE8_INVENTORY_DESKTOP_H

#include "prebattle_inventory_ui.h"

/* All geometry is in desktop points. The framebuffer is in drawable pixels. */
typedef struct Fe8InventoryDesktopLayout {
    int width, height, sidebar, pool_x, pool_width;
    int top, bottom, row_height, side_row_height, items_y, roster_y, roster_rows;
    int table_y, table_rows, deposit_y, help_y;
    int column_x[11], column_width[11];
} Fe8InventoryDesktopLayout;

void fe8_inventory_desktop_layout(const Fe8InventoryUi *ui, int width, int height,
    Fe8InventoryDesktopLayout *layout);
void fe8_inventory_desktop_draw(const Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *snapshot, uint32_t *pixels,
    int stride, int width, int height);
Fe8InventoryHitKind fe8_inventory_desktop_hit(const Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *snapshot, int width, int height,
    int x, int y, int *index);
void fe8_inventory_desktop_scroll(Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *snapshot, int width, int height, int x, int rows);
void fe8_inventory_ui_toggle_density(Fe8InventoryUi *ui);

#endif
