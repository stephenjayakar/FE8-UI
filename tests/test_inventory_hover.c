#include "prebattle_inventory_ui.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Fe8InventorySnapshot snapshot;
static Fe8InventorySnapshot original;

/* Find the actual interactive surface instead of duplicating the renderer's
   private layout formula in the tests. Every hover then goes through hit_test. */
static void point_for(const Fe8InventoryUi *ui, int width, int height,
    Fe8InventoryHitKind kind, int wanted_index, int *x_out, int *y_out) {
    int x;
    int y;
    for (y = 0; y < height; ++y) {
        for (x = 0; x < width; ++x) {
            int index = -1;
            if (fe8_inventory_ui_hit_test(ui, &snapshot, width, height,
                    x, y, &index) == kind && index == wanted_index) {
                *x_out = x;
                *y_out = y;
                return;
            }
        }
    }
    fprintf(stderr, "Missing hit %d:%d at %dx%d (density %d)\n",
        kind, wanted_index, width, height, ui->render_scale);
    abort();
}

static void hover_at(Fe8InventoryUi *ui, int width, int height, int x, int y) {
    int index = -1;
    Fe8InventoryHitKind kind = fe8_inventory_ui_hit_test(ui, &snapshot,
        width, height, x, y, &index);
    fe8_inventory_ui_inspect(ui, &snapshot, kind, index);
}

static void fixture(void) {
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.unit_count = 2;
    snapshot.supply_capacity = 200;
    snapshot.first_empty_supply = 0;
    snapshot.supply_address = 0x0203B200;
    snapshot.supply_display_count = 1;
    for (int i = 0; i < 2; ++i) {
        Fe8InventoryUnit *unit = &snapshot.units[i];
        unit->address = 0x0202BE4C + i * 0x48;
        unit->level = 5;
        unit->hp = unit->max_hp = 24;
        strcpy(unit->name, i ? "Second character" : "First character");
        strcpy(unit->description, i ? "Another character's biography." :
            "A character with a long biography. The description must wrap within "
            "the help panel rather than overlapping the shortcut bar or being "
            "mistaken for an item description.");
        strcpy(unit->class_name, i ? "Pegasus Knight" : "Fighter");
        strcpy(unit->class_description, i ? "A flying class." : "A foot soldier.");
        unit->items[0] = 0x1401;
        unit->item_info[0].id = 1;
        unit->item_info[0].movable = true;
        strcpy(unit->item_info[0].name, "Test item");
        strcpy(unit->item_info[0].description, "The item's description.");
    }
    original = snapshot;
}

static void check_size(int width, int height, int density) {
    Fe8InventoryUi ui;
    const char *title;
    const char *help;
    int x;
    int y;
    int index;
    int stride = width + 11;
    size_t count = (size_t)stride * height + 32;
    uint32_t *pixels = malloc(count * sizeof(*pixels));
    assert(pixels);
    for (size_t i = 0; i < count; ++i)
        pixels[i] = 0x12345678;
    fe8_inventory_ui_init(&ui);
    fe8_inventory_ui_open(&ui, &snapshot);
    ui.render_scale = density;
    ui.selected = fe8_inventory_ui_endpoint(&ui, &snapshot,
        FE8_INVENTORY_HIT_UNIT_ITEM, 0);
    ui.has_selection = 1;

    point_for(&ui, width, height, FE8_INVENTORY_HIT_UNIT_NAME, 0, &x, &y);
    hover_at(&ui, width, height, x, y);
    help = fe8_inventory_ui_unit_help(&ui, &snapshot, &title);
    assert(help && strcmp(help, snapshot.units[0].description) == 0);
    assert(strcmp(title, "First character") == 0);
    assert(ui.has_selection && ui.selected.unit_address == snapshot.units[0].address);
    assert(!ui.has_inspected);
    fe8_inventory_ui_draw(&ui, &snapshot, pixels, stride, width, height);
    assert(pixels[0] != 0x12345678);

    /* The hit starts at the visible label, not at the portrait or stat rows. */
    assert(fe8_inventory_ui_hit_test(&ui, &snapshot, width, height,
        x - 1, y, &index) != FE8_INVENTORY_HIT_UNIT_NAME);
    point_for(&ui, width, height, FE8_INVENTORY_HIT_UNIT_CLASS, 0, &x, &y);
    hover_at(&ui, width, height, x, y);
    assert(strcmp(fe8_inventory_ui_unit_help(&ui, &snapshot, &title),
        "A foot soldier.") == 0);
    assert(strcmp(title, "Fighter") == 0);
    fe8_inventory_ui_draw(&ui, &snapshot, pixels, stride, width, height);

    point_for(&ui, width, height, FE8_INVENTORY_HIT_ROSTER, 1, &x, &y);
    hover_at(&ui, width, height, x, y);
    assert(strcmp(fe8_inventory_ui_unit_help(&ui, &snapshot, &title),
        "Another character's biography.") == 0);
    assert(ui.current_unit == 0); /* Inspecting is not selecting or transferring. */
    point_for(&ui, width, height, FE8_INVENTORY_HIT_ROSTER_CLASS, 1, &x, &y);
    hover_at(&ui, width, height, x, y);
    assert(strcmp(fe8_inventory_ui_unit_help(&ui, &snapshot, &title),
        "A flying class.") == 0);
    assert(strcmp(title, "Pegasus Knight") == 0);

    /* Same pointer, different target: the next frame must resolve the new class. */
    point_for(&ui, width, height, FE8_INVENTORY_HIT_UNIT_CLASS, 0, &x, &y);
    ui.current_unit = 1;
    hover_at(&ui, width, height, x, y);
    assert(strcmp(fe8_inventory_ui_unit_help(&ui, &snapshot, &title),
        "A flying class.") == 0);
    assert(ui.has_selection && ui.selected.unit_address == snapshot.units[0].address);

    point_for(&ui, width, height, FE8_INVENTORY_HIT_UNIT_ITEM, 0, &x, &y);
    hover_at(&ui, width, height, x, y);
    assert(ui.has_inspected && ui.inspected.unit_address == snapshot.units[1].address);
    assert(!fe8_inventory_ui_unit_help(&ui, &snapshot, &title) && !title);
    fe8_inventory_ui_draw(&ui, &snapshot, pixels, stride, width, height);
    hover_at(&ui, width, height, -1, -1); /* Window leave / margin. */
    assert(!ui.has_inspected && ui.hover_kind == FE8_INVENTORY_HIT_NONE);
    assert(ui.has_selection);

    for (int row = 0; row < height; ++row)
        for (int col = width; col < stride; ++col)
            assert(pixels[row * stride + col] == 0x12345678);
    for (size_t i = (size_t)stride * height; i < count; ++i)
        assert(pixels[i] == 0x12345678);
    assert(memcmp(&original, &snapshot, sizeof(snapshot)) == 0);
    free(pixels);
}

static void check_stale_help(void) {
    Fe8InventoryUi ui;
    const char *title;
    fe8_inventory_ui_init(&ui);
    fe8_inventory_ui_open(&ui, &snapshot);
    snapshot.units[0].description[0] = '\0';
    snapshot.units[0].class_description[0] = '\0';
    fe8_inventory_ui_inspect(&ui, &snapshot, FE8_INVENTORY_HIT_UNIT_NAME, 0);
    assert(strstr(fe8_inventory_ui_unit_help(&ui, &snapshot, &title), "No character"));
    fe8_inventory_ui_inspect(&ui, &snapshot, FE8_INVENTORY_HIT_UNIT_CLASS, 0);
    assert(strstr(fe8_inventory_ui_unit_help(&ui, &snapshot, &title), "No class"));
    snapshot.units[0].address += 0x1000;
    assert(!fe8_inventory_ui_unit_help(&ui, &snapshot, &title));
    snapshot = original;
    fe8_inventory_ui_inspect(&ui, &snapshot, FE8_INVENTORY_HIT_UNIT_NAME, 0);
    fe8_inventory_ui_rebuild(&ui, &snapshot);
    assert(!fe8_inventory_ui_unit_help(&ui, &snapshot, &title));
    for (int i = -1; i < 4; ++i) {
        if (i == 0 || i == 1)
            continue;
        fe8_inventory_ui_inspect(&ui, &snapshot, FE8_INVENTORY_HIT_UNIT_NAME, i);
        assert(!fe8_inventory_ui_unit_help(&ui, &snapshot, &title));
        fe8_inventory_ui_inspect(&ui, &snapshot, FE8_INVENTORY_HIT_UNIT_ITEM, i + 20);
        assert(!ui.has_inspected);
    }
    fe8_inventory_ui_inspect(&ui, NULL, FE8_INVENTORY_HIT_UNIT_CLASS, 0);
    assert(ui.hover_kind == FE8_INVENTORY_HIT_NONE);
    ui.active = 0;
    fe8_inventory_ui_inspect(&ui, &snapshot, FE8_INVENTORY_HIT_UNIT_NAME, 0);
    assert(!fe8_inventory_ui_unit_help(&ui, &snapshot, &title));
}

static void check_scrolling(void) {
    Fe8InventoryUi ui;
    int x;
    int y;
    int index;
    for (int i = 2; i < FE8_INVENTORY_UNIT_CAPACITY; ++i) {
        snapshot.units[i] = snapshot.units[0];
        snapshot.units[i].address += i * 0x48;
    }
    snapshot.unit_count = FE8_INVENTORY_UNIT_CAPACITY;
    fe8_inventory_ui_init(&ui);
    fe8_inventory_ui_open(&ui, &snapshot);
    point_for(&ui, 480, 320, FE8_INVENTORY_HIT_ROSTER, 0, &x, &y);
    fe8_inventory_ui_scroll(&ui, 999, &snapshot, 480, 320, x);
    hover_at(&ui, 480, 320, x, y);
    assert(ui.hover_unit_address != snapshot.units[0].address);
    /* A resize can increase visible rows without changing stored scroll.
       Hit testing and painting must both clamp to the new final page. */
    ui.pool_scroll = ui.roster_scroll = 999;
    assert(fe8_inventory_ui_hit_test(&ui, &snapshot, 800, 500, x, y, &index) ==
        FE8_INVENTORY_HIT_ROSTER);
    assert(index >= 0 && index < snapshot.unit_count - 1);
    int before = ui.roster_scroll;
    fe8_inventory_ui_scroll(&ui, -3, &snapshot, 480, 320, 200);
    assert(ui.roster_scroll == before); /* Middle card is not the roster. */
    assert(fe8_inventory_ui_hit_test(&ui, &snapshot, 480, 320,
        470, 100, &index) == FE8_INVENTORY_HIT_NONE); /* Scrollbar gutter. */
    assert(fe8_inventory_ui_hit_test(&ui, &snapshot, 480, 320,
        330, 237, &index) == FE8_INVENTORY_HIT_NONE); /* Undrawn partial row. */
    snapshot = original;
}

int main(void) {
    fixture();
    check_size(480, 320, 1);
    check_size(960, 640, 2);
    check_size(1440, 960, 3);
    check_size(800, 500, 1);
    check_size(1000, 700, 2);
    check_stale_help();
    check_scrolling();
    puts("Inventory hover, layout, density and selection regression tests passed");
    return 0;
}
