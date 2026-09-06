#include "inventory_pins.h"

#include <string.h>

static int min(int a, int b) { return a < b ? a : b; }
static int max(int a, int b) { return a > b ? a : b; }
static int offset(int value, int count, int rows) {
    return min(max(0, value), max(0, count - rows));
}
static int pin_matches(Fe8InventoryUnitPin pin, const Fe8InventoryUnit *unit) {
    return unit->address && pin.address == unit->address && pin.character_id == unit->character_id;
}
int fe8_inventory_unit_pinned(const Fe8InventoryUi *ui, const Fe8InventoryUnit *unit) {
    if (!ui || !unit) return 0;
    for (int n = 0; n < min(ui->pinned_count, FE8_INVENTORY_UNIT_CAPACITY); ++n)
        if (pin_matches(ui->pinned_units[n], unit)) return 1;
    return 0;
}
void fe8_inventory_pins_reconcile(Fe8InventoryUi *ui, const Fe8InventorySnapshot *s) {
    if (!ui) return;
    int count = 0, old_count = min(ui->pinned_count, FE8_INVENTORY_UNIT_CAPACITY);
    for (int n = 0; s && n < old_count; ++n) {
        for (int u = 0; u < min(s->unit_count, FE8_INVENTORY_UNIT_CAPACITY); ++u) {
            if (!pin_matches(ui->pinned_units[n], &s->units[u])) continue;
            int duplicate = 0;
            for (int p = 0; p < count; ++p)
                duplicate |= pin_matches(ui->pinned_units[p], &s->units[u]);
            if (!duplicate) ui->pinned_units[count++] = ui->pinned_units[n];
            break;
        }
    }
    memset(ui->pinned_units + count, 0, (FE8_INVENTORY_UNIT_CAPACITY - count) * sizeof(ui->pinned_units[0]));
    ui->pinned_count = count;
    ui->pinned_scroll = offset(ui->pinned_scroll, count, 1);
}
int fe8_inventory_pin_toggle(Fe8InventoryUi *ui, const Fe8InventorySnapshot *s, int index) {
    if (!ui || !ui->active || !s || index < 0 || index >= s->unit_count ||
            index >= FE8_INVENTORY_UNIT_CAPACITY || !s->units[index].address) return 0;
    fe8_inventory_pins_reconcile(ui, s);
    const Fe8InventoryUnit *unit = &s->units[index];
    int before = 0;
    for (int n = 0; n < index; ++n)
        before += !fe8_inventory_unit_pinned(ui, &s->units[n]);
    for (int n = 0; n < ui->pinned_count; ++n) {
        if (!pin_matches(ui->pinned_units[n], unit)) continue;
        memmove(ui->pinned_units + n, ui->pinned_units + n + 1,
            (ui->pinned_count - n - 1) * sizeof(ui->pinned_units[0]));
        memset(ui->pinned_units + --ui->pinned_count, 0, sizeof(ui->pinned_units[0]));
        /* Keep the same unpinned unit at the top when insertion precedes it. */
        if (before <= ui->loadout_scroll) ++ui->loadout_scroll;
        if (n < ui->pinned_scroll) --ui->pinned_scroll;
        if (!ui->pinned_count) ui->pinned_scroll = 0;
        return -1;
    }
    if (ui->pinned_count >= FE8_INVENTORY_UNIT_CAPACITY) return 0;
    ui->pinned_units[ui->pinned_count++] = (Fe8InventoryUnitPin){unit->address, unit->character_id};
    if (before < ui->loadout_scroll) --ui->loadout_scroll;
    ui->pinned_scroll = ui->pinned_count - 1; /* Bring the newly pinned ally into view. */
    return 1;
}
void fe8_inventory_board_view(const Fe8InventoryUi *ui, const Fe8InventorySnapshot *s,
    const Fe8InventoryDesktopLayout *l, Fe8InventoryBoardView *v) {
    memset(v, 0, sizeof(*v));
    if (!ui || !s || !l) return;
    int used[FE8_INVENTORY_UNIT_CAPACITY] = {0};
    int count = min(s->unit_count, FE8_INVENTORY_UNIT_CAPACITY);
    for (int p = 0; p < min(ui->pinned_count, FE8_INVENTORY_UNIT_CAPACITY); ++p)
        for (int n = 0; n < count; ++n)
            if (!used[n] && pin_matches(ui->pinned_units[p], &s->units[n])) {
                used[n] = 1; v->pinned[v->pinned_count++] = n; break;
            }
    for (int n = 0; n < count; ++n)
        if (!used[n]) v->others[v->other_count++] = n;
    v->top = v->other_y = l->board_y;
    v->row_height = l->board_row_height;
    v->card_height = l->board_card_height;
    v->other_rows = l->board_rows;
    if (v->pinned_count) {
        int gap = v->other_count ? 22 : 0;
        int space = max(0, l->deposit_y - l->board_y - gap);
        /* Minimum window: slightly denser cards retain a pinned ally AND a
           scrolling ally, with all five slots and the full stat strip intact. */
        if (v->other_count && space < 2 * v->row_height) {
            v->row_height = space / 2;
            v->card_height = v->row_height - (l->board_row_height - l->board_card_height);
        }
        if (v->row_height <= 0 || v->card_height < 40) {
            v->pinned_rows = v->other_rows = 0;
            return;
        }
        int rows = space / v->row_height;
        v->pinned_rows = min(v->pinned_count, v->other_count ? max(1, rows / 2) : rows);
        v->other_rows = min(v->other_count, max(0, rows - v->pinned_rows));
        /* Do not waste a large screen when only a few unpinned allies remain. */
        v->pinned_rows = min(v->pinned_count, max(0, rows - v->other_rows));
        v->other_y = v->top + v->pinned_rows * v->row_height + gap;
    }
    v->pinned_start = offset(ui->pinned_scroll, v->pinned_count, v->pinned_rows);
    v->other_start = offset(ui->loadout_scroll, v->other_count, v->other_rows);
}
int fe8_inventory_board_unit_at(const Fe8InventoryBoardView *v, int y, int *row_y) {
    if (!v || v->row_height <= 0) return -1;
    if (y >= v->top && y < v->top + v->pinned_rows * v->row_height) {
        int row = (y - v->top) / v->row_height;
        if (row_y) *row_y = v->top + row * v->row_height;
        return v->pinned[v->pinned_start + row];
    }
    if (y >= v->other_y && y < v->other_y + v->other_rows * v->row_height) {
        int row = (y - v->other_y) / v->row_height;
        if (row + v->other_start >= v->other_count) return -1;
        if (row_y) *row_y = v->other_y + row * v->row_height;
        return v->others[v->other_start + row];
    }
    return -1;
}
