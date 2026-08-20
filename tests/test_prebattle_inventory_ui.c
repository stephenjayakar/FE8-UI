#include "prebattle_inventory_ui.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    Fe8InventoryUi ui;
    Fe8InventorySnapshot snapshot;
    uint32_t pixels[480 * 320];
    int index;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.unit_count = 2;
    snapshot.supply_capacity = 100;
    snapshot.supply_address = 0x0203A81C;
    snapshot.supply_display_count = 1;
    snapshot.supply_display_slots[0] = 7;
    snapshot.supply[7] = 0x1410;
    strcpy(snapshot.supply_info[7].name, "Vulnerary");
    snapshot.supply_info[7].movable = true;
    snapshot.units[0].character_id = 0x11;
    snapshot.units[0].address = 0x0202BE4C;
    snapshot.units[0].level = 12;
    strcpy(snapshot.units[0].name, "Eirika");
    strcpy(snapshot.units[0].class_name, "Lord");
    snapshot.units[0].items[0] = 0x281C;
    strcpy(snapshot.units[0].item_info[0].name, "Iron Sword");
    strcpy(snapshot.units[0].item_info[0].description,
        "A common sword used by many soldiers.");
    snapshot.units[0].item_info[0].movable = true;
    snapshot.units[1].character_id = 0x22;
    snapshot.units[1].address = 0x0202BE94;
    snapshot.units[1].level = 7;
    snapshot.units[1].items[4] = 0x052D;
    snapshot.units[1].item_info[4].id = 0x2D;
    snapshot.units[1].item_info[4].movable = false;
    fe8_inventory_ui_init(&ui);
    fe8_inventory_ui_open(&ui);
    memset(pixels, 0, sizeof(pixels));
    fe8_inventory_ui_draw(&ui, &snapshot, pixels, 480, 480, 320);
    assert(pixels[0] != 0);
    assert(fe8_inventory_ui_hit_test(&ui, &snapshot, 480, 320,
        8 + 124 + 10, 8 + 36 + 88 + 9, &index) == FE8_INVENTORY_HIT_UNIT_ITEM);
    assert(index == 0);
    ui.selected = fe8_inventory_ui_endpoint(&ui, &snapshot,
        FE8_INVENTORY_HIT_UNIT_ITEM, 0);
    ui.has_selection = 1;
    fe8_inventory_ui_inspect(&ui, &snapshot, FE8_INVENTORY_HIT_UNIT_ITEM, 0);
    assert(ui.has_inspected);
    assert(ui.inspected.unit_address == snapshot.units[0].address);
    ui.current_unit = 1;
    {
        Fe8InventoryEndpoint destination = fe8_inventory_ui_endpoint(&ui, &snapshot,
            FE8_INVENTORY_HIT_UNIT_ITEM, 4);
        assert(ui.has_selection);
        assert(ui.selected.unit_address == snapshot.units[0].address);
        assert(destination.unit_address == snapshot.units[1].address);
        assert(!fe8_inventory_ui_endpoint_movable(&snapshot, destination));
    }
    assert(fe8_inventory_ui_hit_test(&ui, &snapshot, 480, 320,
        15, 15, &index) == FE8_INVENTORY_HIT_NONE);
    assert(fe8_inventory_ui_hit_test(&ui, &snapshot, 480, 320,
        8 + 124 + 300 + 10, 8 + 36 + 18 + 9, &index) ==
        FE8_INVENTORY_HIT_SUPPLY_ITEM);
    assert(index == 7);
    puts("pre-battle inventory UI tests passed");
    return 0;
}
