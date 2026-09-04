#include "inventory_desktop.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Fe8InventorySnapshot s, original;
static void fixture(void) {
    s.unit_count = 62;
    s.supply_capacity = s.supply_count = s.supply_display_count = s.first_empty_supply = 200;
    s.supply_address = 0x0203B200;
    for (int n = 0; n < 62; ++n) {
        Fe8InventoryUnit *u = &s.units[n];
        u->address = 0x0202BE4C + n * 0x48;
        snprintf(u->name, sizeof(u->name), "Unit %02d", n);
        strcpy(u->description, "A character biography, independent of transfer selection.");
        strcpy(u->class_name, "Pegasus Knight");
        strcpy(u->class_description, "A flying class with a full ROM-backed description.");
        for (int j = 0; j < 5; ++j) {
            u->items[j] = 0x2801;
            u->item_info[j].id = 1;
            u->item_info[j].movable = true;
            u->item_info[j].attributes = 1;
            u->item_info[j].max_uses = 40;
            strcpy(u->item_info[j].name, "Long item name for clipping");
        }
    }
    for (int i = 0; i < 200; ++i) {
        s.supply_display_slots[i] = i;
        s.supply[i] = 0x1402;
        s.supply_info[i] = s.units[0].item_info[0];
        s.supply_info[i].id = 2;
        strcpy(s.supply_info[i].name, "Supply item");
    }
    original = s;
}
static Fe8InventoryHitKind hit(Fe8InventoryUi *ui, int w, int h, int x, int y, int *index) {
    return fe8_inventory_ui_hit_test(ui, &s, w, h,
        (int)(x * ui->desktop_scale + .5f), (int)(y * ui->desktop_scale + .5f), index);
}
static void check(int w, int h, float scale, int comfortable) {
    Fe8InventoryUi ui;
    Fe8InventoryDesktopLayout l;
    int index;
    fe8_inventory_ui_init(&ui); fe8_inventory_ui_open(&ui, &s);
    ui.desktop = 1; ui.desktop_scale = scale; ui.comfortable = comfortable;
    fe8_inventory_desktop_layout(&ui, w, h, &l);
    assert(l.table_rows > 0 && l.roster_rows > 0);
    assert(l.column_width[0] >= 100);
    if (w == 960 && scale == 1 && !comfortable) assert(l.table_rows == 15);
    if (w == 1280 && scale == 1 && !comfortable) assert(l.table_rows == 21);
    if (w / scale >= 1200) for (int i = 0; i < 11; ++i) assert(l.column_width[i] > 0);
    int end = l.pool_x;
    for (int i = 0; i < 11; ++i) {
        assert(l.column_x[i] == end);
        assert(l.column_width[i] >= 0);
        end += l.column_width[i];
    }
    assert(end <= l.width - 12);
    for (int row = 0; row < 5; ++row) {
        assert(hit(&ui, w, h, 30, l.items_y + row * l.side_row_height + 3, &index) == FE8_INVENTORY_HIT_UNIT_ITEM);
        assert(index == row);
    }
    assert(hit(&ui, w, h, 110, l.top + 10, &index) == FE8_INVENTORY_HIT_UNIT_NAME);
    fe8_inventory_ui_inspect(&ui, &s, FE8_INVENTORY_HIT_UNIT_NAME, index);
    const char *title;
    assert(strcmp(fe8_inventory_ui_unit_help(&ui, &s, &title), s.units[0].description) == 0);
    ui.selected = fe8_inventory_ui_endpoint(&ui, &s, FE8_INVENTORY_HIT_UNIT_ITEM, 0);
    ui.has_selection = 1;
    assert(hit(&ui, w, h, 110, l.top + 30, &index) == FE8_INVENTORY_HIT_UNIT_CLASS);
    fe8_inventory_ui_inspect(&ui, &s, FE8_INVENTORY_HIT_UNIT_CLASS, index);
    assert(strcmp(fe8_inventory_ui_unit_help(&ui, &s, &title), s.units[0].class_description) == 0);
    assert(ui.has_selection);
    assert(hit(&ui, w, h, l.pool_x + 15, l.table_y + 2, &index) == FE8_INVENTORY_HIT_POOL_ITEM);
    assert(index == 0);
    assert(hit(&ui, w, h, l.column_x[0] + 10, l.table_y - 10, &index) == FE8_INVENTORY_HIT_SORT_COLUMN);
    assert(index == FE8_INVENTORY_SORT_NAME);
    assert(hit(&ui, w, h, l.width - 40, l.top + 10, &index) == FE8_INVENTORY_HIT_DENSITY);
    assert(hit(&ui, w, h, l.pool_x + 10, l.deposit_y + 10, &index) == FE8_INVENTORY_HIT_NONE); /* full */
    fe8_inventory_ui_scroll(&ui, 999, &s, w, h, (int)((l.pool_x + 10) * scale));
    assert(hit(&ui, w, h, l.pool_x + 10, l.table_y + (l.table_rows - 1) * l.row_height + 3, &index) == FE8_INVENTORY_HIT_POOL_ITEM);
    assert(index == 509);
    assert(hit(&ui, w, h, l.width - 13, l.table_y + 10, &index) == FE8_INVENTORY_HIT_NONE);
    assert(hit(&ui, w, h, l.pool_x + 10, l.table_y + l.table_rows * l.row_height + 1, &index) == FE8_INVENTORY_HIT_NONE);
    fe8_inventory_ui_scroll(&ui, 999, &s, w, h, (int)(30 * scale));
    assert(hit(&ui, w, h, 30, l.roster_y + (l.roster_rows - 1) * l.side_row_height + 2, &index) == FE8_INVENTORY_HIT_ROSTER);
    assert(index == 61);
    assert(hit(&ui, w, h, 12 + l.sidebar * 42 / 100 + 2, l.roster_y + 2, &index) == FE8_INVENTORY_HIT_ROSTER_CLASS);
    fe8_inventory_ui_toggle_density(&ui);
    assert(ui.has_selection);
    fe8_inventory_ui_toggle_density(&ui);
    int stride = w + 9;
    size_t n = (size_t)stride * h + 32;
    uint32_t *pixels = malloc(n * sizeof(*pixels)); assert(pixels);
    for (size_t i = 0; i < n; ++i) pixels[i] = 0x12345678;
    fe8_inventory_ui_draw(&ui, &s, pixels, stride, w, h);
    for (int y = 0; y < h; ++y)
        for (int x = w; x < stride; ++x) assert(pixels[y * stride + x] == 0x12345678);
    for (size_t i = (size_t)stride * h; i < n; ++i) assert(pixels[i] == 0x12345678);
    assert(memcmp(&s, &original, sizeof(s)) == 0);
    free(pixels);
}
static void empty_supply(void) {
    Fe8InventoryUi ui;
    Fe8InventoryDesktopLayout l;
    int index;
    s.supply[7] = 0; s.first_empty_supply = 7; s.supply_count = 199;
    /* Extraction lists all occupied slots first, then a single empty destination. */
    for (int i = 7; i < 199; ++i) s.supply_display_slots[i] = i + 1;
    s.supply_display_slots[199] = 7;
    fe8_inventory_ui_init(&ui); fe8_inventory_ui_open(&ui, &s);
    ui.desktop = 1; ui.desktop_scale = 1;
    fe8_inventory_desktop_layout(&ui, 1280, 800, &l);
    for (int sort = 0; sort < FE8_INVENTORY_SORT_COUNT; ++sort) {
        ui.pool_sort = (Fe8InventorySort)sort;
        fe8_inventory_ui_rebuild(&ui, &s);
        assert(hit(&ui, 1280, 800, l.pool_x + 10, l.deposit_y + 5, &index) == FE8_INVENTORY_HIT_POOL_ITEM);
        Fe8InventoryEndpoint e = fe8_inventory_ui_endpoint(&ui, &s, FE8_INVENTORY_HIT_POOL_ITEM, index);
        assert(e.kind == FE8_INVENTORY_ENDPOINT_SUPPLY && e.slot == 7);
        assert(fe8_inventory_ui_endpoint_item(&s, e) == 0);
    }
    s = original;
}
int main(void) {
    fixture();
    check(640, 480, 1, 0); check(640, 480, 1, 1);
    check(960, 640, 1, 0); check(1280, 800, 1, 0); check(1280, 800, 1, 1);
    check(1920, 1080, 1.5f, 0); check(2560, 1600, 2, 0);
    check(1600, 1000, 1.25f, 0);
    empty_supply();
    puts("Desktop rows, columns, scaling, hover, sorting, selection, scroll and pinned supply passed");
    return 0;
}
