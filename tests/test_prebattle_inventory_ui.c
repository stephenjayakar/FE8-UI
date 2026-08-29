#include "prebattle_inventory_ui.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define IA_WEAPON UINT32_C(1u << 0)

static uint32_t scaled_pixels[960 * 640];

int main(void) {
    Fe8InventoryUi ui;
    Fe8InventorySnapshot snapshot;
    Fe8InventoryListEntry entry;
    uint32_t pixels[480 * 320];
    int index;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.unit_count = 2;
    snapshot.supply_count = 1;
    snapshot.supply_capacity = 100;
    snapshot.supply_address = 0x0203A81C;
    snapshot.supply_display_count = 2;
    snapshot.supply_display_slots[0] = 7;
    snapshot.supply_display_slots[1] = 8;
    snapshot.first_empty_supply = 8;
    snapshot.supply[7] = 0x1410;
    snapshot.supply_info[7].id = 0x10;
    strcpy(snapshot.supply_info[7].name, "Vulnerary");
    snapshot.supply_info[7].movable = true;

    snapshot.units[0].character_id = 0x11;
    snapshot.units[0].address = 0x0202BE4C;
    snapshot.units[0].level = 12;
    snapshot.units[0].hp = 20;
    snapshot.units[0].max_hp = 24;
    snapshot.units[0].power = 8;
    snapshot.units[0].speed = 11;
    snapshot.units[0].defense = 6;
    snapshot.units[0].resistance = 3;
    snapshot.units[0].ranks[0] = 31;
    strcpy(snapshot.units[0].name, "Eirika");
    strcpy(snapshot.units[0].class_name, "Lord");
    snapshot.units[0].items[0] = 0x281C;
    snapshot.units[0].item_info[0].id = 0x1C;
    snapshot.units[0].item_info[0].weapon_type = 0;
    snapshot.units[0].item_info[0].weapon_rank = 31;
    snapshot.units[0].item_info[0].attributes = IA_WEAPON;
    strcpy(snapshot.units[0].item_info[0].name, "Iron Sword");
    strcpy(snapshot.units[0].item_info[0].description,
        "A common sword used by many soldiers.");
    snapshot.units[0].item_info[0].movable = true;

    snapshot.units[1].character_id = 0x22;
    snapshot.units[1].address = 0x0202BE94;
    snapshot.units[1].level = 7;
    snapshot.units[1].ranks[1] = 71;
    strcpy(snapshot.units[1].name, "Seth");
    strcpy(snapshot.units[1].class_name, "Paladin");
    snapshot.units[1].items[4] = 0x052D;
    snapshot.units[1].item_info[4].id = 0x2D;
    snapshot.units[1].item_info[4].weapon_type = 1;
    snapshot.units[1].item_info[4].weapon_rank = 71;
    snapshot.units[1].item_info[4].attributes = IA_WEAPON;
    strcpy(snapshot.units[1].item_info[4].name, "Steel Lance");
    snapshot.units[1].item_info[4].movable = false;

    assert(fe8_inventory_ui_all_item_count(&snapshot) == 4);
    assert(fe8_inventory_ui_all_item_entry(&snapshot, 0, &entry));
    assert(entry.unit_index == 0 && entry.endpoint.slot == 0 && entry.item == 0x281C);
    assert(fe8_inventory_ui_all_item_entry(&snapshot, 1, &entry));
    assert(entry.unit_index == 1 && entry.endpoint.slot == 4 && entry.item == 0x052D);
    assert(fe8_inventory_ui_all_item_entry(&snapshot, 2, &entry));
    assert(entry.unit_index == -1 && entry.endpoint.slot == 7 && entry.item == 0x1410);
    assert(fe8_inventory_ui_all_item_entry(&snapshot, 3, &entry));
    assert(entry.unit_index == -1 && entry.endpoint.slot == 8 && entry.item == 0);
    assert(!fe8_inventory_ui_all_item_entry(&snapshot, 4, &entry));
    assert(fe8_inventory_item_use_state(&snapshot.units[0],
        &snapshot.units[0].item_info[0]) == FE8_INVENTORY_USE_READY);
    assert(fe8_inventory_item_use_state(&snapshot.units[0],
        &snapshot.units[1].item_info[4]) == FE8_INVENTORY_USE_RANK);
    assert(fe8_inventory_item_use_state(&snapshot.units[0],
        &snapshot.supply_info[7]) == FE8_INVENTORY_USE_ITEM);

    fe8_inventory_ui_init(&ui);
    fe8_inventory_ui_open(&ui);
    memset(pixels, 0, sizeof(pixels));
    fe8_inventory_ui_draw(&ui, &snapshot, pixels, 480, 480, 320);
    assert(pixels[0] != 0);
    ui.render_scale = 2;
    memset(scaled_pixels, 0, sizeof(scaled_pixels));
    fe8_inventory_ui_draw(&ui, &snapshot, scaled_pixels, 960, 960, 640);
    assert(scaled_pixels[0] != 0);
    assert(fe8_inventory_ui_hit_test(&ui, &snapshot, 960, 640,
        (8 + 104 + 176 + 10) * 2, (8 + 38 + 18 + 9) * 2, &index) ==
        FE8_INVENTORY_HIT_ALL_ITEM);
    assert(index == 0);
    ui.render_scale = 1;

    assert(fe8_inventory_ui_hit_test(&ui, &snapshot, 480, 320,
        8 + 104 + 10, 8 + 38 + 72 + 9, &index) == FE8_INVENTORY_HIT_UNIT_ITEM);
    assert(index == 0);
    ui.selected = fe8_inventory_ui_endpoint(&ui, &snapshot,
        FE8_INVENTORY_HIT_UNIT_ITEM, 0);
    ui.has_selection = 1;
    fe8_inventory_ui_inspect(&ui, &snapshot, FE8_INVENTORY_HIT_UNIT_ITEM, 0);
    assert(ui.has_inspected);
    assert(ui.inspected.unit_address == snapshot.units[0].address);

    assert(fe8_inventory_ui_hit_test(&ui, &snapshot, 480, 320,
        8 + 104 + 176 + 10, 8 + 38 + 18 + 9, &index) ==
        FE8_INVENTORY_HIT_ALL_ITEM);
    assert(index == 0);
    assert(fe8_inventory_ui_hit_test(&ui, &snapshot, 480, 320,
        8 + 104 + 176 + 10, 8 + 38 + 18 + 30 + 9, &index) ==
        FE8_INVENTORY_HIT_SUPPLY_ITEM);
    assert(index == 1);
    {
        Fe8InventoryEndpoint pooled = fe8_inventory_ui_endpoint(&ui, &snapshot,
            FE8_INVENTORY_HIT_ALL_ITEM, index);
        assert(pooled.kind == FE8_INVENTORY_ENDPOINT_UNIT);
        assert(pooled.unit_address == snapshot.units[1].address);
        assert(pooled.slot == 4);
        assert(!fe8_inventory_ui_endpoint_movable(&snapshot, pooled));
    }

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
    puts("pre-battle inventory UI tests passed");
    return 0;
}
