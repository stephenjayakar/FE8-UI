#include "prebattle_inventory_ui.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define IA_WEAPON UINT32_C(1u << 0)
#define IA_STAFF UINT32_C(1u << 2)

static uint32_t scaled_pixels[960 * 640];

static void set_item(Fe8ItemInfo *info, uint8_t id, const char *name,
    uint8_t type, uint8_t rank, uint32_t attributes, int movable) {
    memset(info, 0, sizeof(*info));
    info->id = id;
    info->weapon_type = type;
    info->weapon_rank = rank;
    info->attributes = attributes;
    info->movable = movable != 0;
    strcpy(info->name, name);
}

int main(void) {
    Fe8InventoryUi ui;
    Fe8InventorySnapshot snapshot;
    Fe8InventoryListEntry entry;
    uint32_t pixels[480 * 320];
    int index;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.unit_count = 2;
    snapshot.supply_count = 2;
    snapshot.supply_capacity = 100;
    snapshot.supply_address = UINT32_C(0x0203A81C);
    snapshot.supply_display_count = 3;
    snapshot.supply_display_slots[0] = 2;
    snapshot.supply_display_slots[1] = 7;
    snapshot.supply_display_slots[2] = 8;
    snapshot.first_empty_supply = 8;

    snapshot.supply[2] = 0x1E38;
    set_item(&snapshot.supply_info[2], 0x38, "Fire", 5, 31,
        IA_WEAPON, 1);
    snapshot.supply[7] = 0x1410;
    set_item(&snapshot.supply_info[7], 0x10, "Vulnerary", 0, 0, 0, 1);

    snapshot.units[0].character_id = 0x11;
    snapshot.units[0].address = UINT32_C(0x0202BE4C);
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
    set_item(&snapshot.units[0].item_info[0], 0x1C, "Iron Sword", 0, 31,
        IA_WEAPON, 1);
    strcpy(snapshot.units[0].item_info[0].description,
        "A common sword used by many soldiers.");

    snapshot.units[1].character_id = 0x22;
    snapshot.units[1].address = UINT32_C(0x0202BE94);
    snapshot.units[1].level = 7;
    snapshot.units[1].ranks[1] = 71;
    strcpy(snapshot.units[1].name, "Seth");
    strcpy(snapshot.units[1].class_name, "Paladin");
    snapshot.units[1].items[4] = 0x052D;
    set_item(&snapshot.units[1].item_info[4], 0x2D, "Steel Lance", 1, 71,
        IA_WEAPON, 0);

    fe8_inventory_ui_init(&ui);
    fe8_inventory_ui_open(&ui, &snapshot);
    assert(fe8_inventory_ui_pool_count(&ui) == 5);
    assert(ui.pool_scope == FE8_INVENTORY_POOL_ALL);
    assert(ui.pool_sort == FE8_INVENTORY_SORT_TYPE);

    /* The default view groups real weapons by weapon type, then regular items. */
    assert(fe8_inventory_ui_pool_entry(&ui, 0, &entry));
    assert(strcmp(entry.info->name, "Iron Sword") == 0);
    assert(fe8_inventory_ui_pool_entry(&ui, 1, &entry));
    assert(strcmp(entry.info->name, "Steel Lance") == 0);
    assert(fe8_inventory_ui_pool_entry(&ui, 2, &entry));
    assert(strcmp(entry.info->name, "Fire") == 0);
    assert(fe8_inventory_ui_pool_entry(&ui, 3, &entry));
    assert(strcmp(entry.info->name, "Vulnerary") == 0);
    assert(fe8_inventory_ui_pool_entry(&ui, 4, &entry));
    assert(entry.item == 0 && entry.endpoint.slot == 8);

    assert(fe8_inventory_item_use_state(&snapshot.units[0],
        &snapshot.units[0].item_info[0]) == FE8_INVENTORY_USE_READY);
    assert(fe8_inventory_item_use_state(&snapshot.units[0],
        &snapshot.units[1].item_info[4]) == FE8_INVENTORY_USE_RANK);
    assert(fe8_inventory_item_use_state(&snapshot.units[0],
        &snapshot.supply_info[7]) == FE8_INVENTORY_USE_ITEM);

    fe8_inventory_ui_cycle_sort(&ui, &snapshot);
    assert(ui.pool_sort == FE8_INVENTORY_SORT_NAME);
    assert(fe8_inventory_ui_pool_entry(&ui, 0, &entry));
    assert(strcmp(entry.info->name, "Fire") == 0);
    assert(fe8_inventory_ui_pool_entry(&ui, 1, &entry));
    assert(strcmp(entry.info->name, "Iron Sword") == 0);

    fe8_inventory_ui_cycle_sort(&ui, &snapshot);
    assert(ui.pool_sort == FE8_INVENTORY_SORT_USES);
    assert(fe8_inventory_ui_pool_entry(&ui, 0, &entry));
    assert(strcmp(entry.info->name, "Iron Sword") == 0);
    assert(fe8_inventory_ui_pool_entry(&ui, 1, &entry));
    assert(strcmp(entry.info->name, "Fire") == 0);
    assert(fe8_inventory_ui_pool_entry(&ui, 2, &entry));
    assert(strcmp(entry.info->name, "Vulnerary") == 0);

    fe8_inventory_ui_cycle_sort(&ui, &snapshot);
    assert(ui.pool_sort == FE8_INVENTORY_SORT_OWNER);
    assert(fe8_inventory_ui_pool_entry(&ui, 0, &entry));
    assert(entry.unit_index == 0);
    assert(fe8_inventory_ui_pool_entry(&ui, 1, &entry));
    assert(entry.unit_index == 1);
    assert(fe8_inventory_ui_pool_entry(&ui, 2, &entry));
    assert(entry.unit_index == -1 && entry.endpoint.slot == 2);

    fe8_inventory_ui_toggle_scope(&ui, &snapshot);
    assert(ui.pool_scope == FE8_INVENTORY_POOL_SUPPLY);
    assert(fe8_inventory_ui_pool_count(&ui) == 3);
    assert(fe8_inventory_ui_pool_entry(&ui, 0, &entry));
    assert(entry.endpoint.kind == FE8_INVENTORY_ENDPOINT_SUPPLY);
    fe8_inventory_ui_toggle_scope(&ui, &snapshot);
    assert(fe8_inventory_ui_pool_count(&ui) == 5);

    memset(pixels, 0, sizeof(pixels));
    fe8_inventory_ui_draw(&ui, &snapshot, pixels, 480, 480, 320);
    assert(pixels[0] != 0);
    assert(pixels[479] != 0);

    /* The layout is density-independent: hit testing uses logical coordinates. */
    ui.render_scale = 2;
    memset(scaled_pixels, 0, sizeof(scaled_pixels));
    fe8_inventory_ui_draw(&ui, &snapshot, scaled_pixels, 960, 960, 640);
    assert(scaled_pixels[0] != 0);
    assert(fe8_inventory_ui_hit_test(&ui, &snapshot, 960, 640,
        305 * 2, 51 * 2, &index) == FE8_INVENTORY_HIT_POOL_SCOPE);
    assert(fe8_inventory_ui_hit_test(&ui, &snapshot, 960, 640,
        370 * 2, 51 * 2, &index) == FE8_INVENTORY_HIT_POOL_SORT);
    assert(fe8_inventory_ui_hit_test(&ui, &snapshot, 960, 640,
        330 * 2, 73 * 2, &index) == FE8_INVENTORY_HIT_POOL_ITEM);
    assert(index == 0);
    ui.render_scale = 1;

    assert(fe8_inventory_ui_hit_test(&ui, &snapshot, 480, 320,
        125, 127, &index) == FE8_INVENTORY_HIT_UNIT_ITEM);
    assert(index == 0);
    ui.selected = fe8_inventory_ui_endpoint(&ui, &snapshot,
        FE8_INVENTORY_HIT_UNIT_ITEM, 0);
    ui.has_selection = 1;
    fe8_inventory_ui_inspect(&ui, &snapshot,
        FE8_INVENTORY_HIT_UNIT_ITEM, 0);
    assert(ui.has_inspected);
    assert(ui.inspected.unit_address == snapshot.units[0].address);

    assert(fe8_inventory_ui_hit_test(&ui, &snapshot, 480, 320,
        330, 73, &index) == FE8_INVENTORY_HIT_POOL_ITEM);
    {
        Fe8InventoryEndpoint pooled = fe8_inventory_ui_endpoint(&ui, &snapshot,
            FE8_INVENTORY_HIT_POOL_ITEM, index);
        assert(pooled.kind == FE8_INVENTORY_ENDPOINT_UNIT);
        assert(pooled.unit_address == snapshot.units[0].address);
    }

    ui.current_unit = 1;
    {
        Fe8InventoryEndpoint destination = fe8_inventory_ui_endpoint(&ui,
            &snapshot, FE8_INVENTORY_HIT_UNIT_ITEM, 4);
        assert(ui.has_selection);
        assert(ui.selected.unit_address == snapshot.units[0].address);
        assert(destination.unit_address == snapshot.units[1].address);
        assert(!fe8_inventory_ui_endpoint_movable(&snapshot, destination));
    }
    assert(fe8_inventory_ui_hit_test(&ui, &snapshot, 480, 320,
        15, 15, &index) == FE8_INVENTORY_HIT_NONE);
    assert(!fe8_inventory_ui_pool_entry(&ui, 99, &entry));
    puts("pre-battle inventory UI tests passed");
    return 0;
}
