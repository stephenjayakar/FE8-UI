#include "prebattle_inventory.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define IA_WEAPON UINT32_C(1u << 0)
#define IA_MAGIC UINT32_C(1u << 1)
#define IA_STAFF UINT32_C(1u << 2)
#define IA_LOCK_1 UINT32_C(1u << 11)
#define IA_LOCK_3 UINT32_C(1u << 10)
#define CA_LOCK_1 UINT32_C(1u << 16)
#define CA_LOCK_3 UINT32_C(1u << 18)

static unsigned char ewram[0x40000];
static unsigned char rom[0x2000000];

static uint8_t read8(void *context, uint32_t address) {
    (void)context;
    if (address >= 0x02000000 && address < 0x02040000)
        return ewram[address - 0x02000000];
    if (address >= 0x08000000 && address < 0x0A000000)
        return rom[address - 0x08000000];
    return 0;
}

static void write8(void *context, uint32_t address, uint8_t value) {
    (void)context;
    if (address >= 0x02000000 && address < 0x02040000)
        ewram[address - 0x02000000] = value;
}

static void put16(uint32_t address, uint16_t value) {
    write8(NULL, address, (uint8_t)value);
    write8(NULL, address + 1, (uint8_t)(value >> 8));
}

static void put32(uint32_t address, uint32_t value) {
    put16(address, (uint16_t)value);
    put16(address + 2, (uint16_t)(value >> 16));
}

static void put_rom16(uint32_t address, uint16_t value) {
    rom[address - 0x08000000] = (uint8_t)value;
    rom[address - 0x08000000 + 1] = (uint8_t)(value >> 8);
}

static void put_rom32(uint32_t address, uint32_t value) {
    put_rom16(address, (uint16_t)value);
    put_rom16(address + 2, (uint16_t)(value >> 16));
}

static uint32_t item_record(const Fe8Profile *profile, uint8_t item_id) {
    return profile->inventory.item_table + (uint32_t)item_id * UINT32_C(0x24);
}

int main(void) {
    Fe8Profile profile = *fe8u_profile();
    Fe8MemoryReader memory = {NULL, read8};
    Fe8MemoryWriter writer = {NULL, write8};
    Fe8Catalog catalog;
    Fe8InventorySnapshot snapshot;
    uint32_t first = profile.blue_units;
    uint32_t second = first + 0x48;
    uint32_t character_a = 0x08010000;
    uint32_t character_b = 0x08010034;
    uint32_t class_data = 0x08010100;
    memset(ewram, 0, sizeof(ewram));
    memset(rom, 0, sizeof(rom));
    profile.convoy_items = 0x0203B200;
    profile.inventory.convoy_capacity = 200;
    profile.inventory.immovable_item_attributes = UINT32_C(0x00000006);
    put_rom32(0x080006DC, 0x08021000);
    put_rom32(0x08021000, 0x08022000);
    put_rom32(0x080006E0, 0x08023000);
    put_rom32(0x0800A2A0, 0x08020000);
    put_rom32(0x08005524, 0x08024000);
    put_rom16(0x0803159C, 0x4800);
    put_rom16(0x0803159E, 0x4770);
    put_rom32(0x080315A0, 0x0203B200);
    put_rom16(0x080315BC, 0x2BC7);
    put_rom32(0x08020000 + 4, 0x88025000);
    memcpy(rom + 0x25000, "Alice", 6);
    put_rom32(0x08020000 + 8, 0x88025010);
    memcpy(rom + 0x25010, "Fighter", 8);
    put_rom32(0x08020000 + 12, 0x88025020);
    memcpy(rom + 0x25020, "Test Blade", 11);
    put_rom32(0x08020000 + 16, 0x88025030);
    memcpy(rom + 0x25030, "Fire", 5);
    put_rom32(0x08020000 + 20, 0x88025040);
    memcpy(rom + 0x25040, "A dependable blade.", 20);
    /* Compressed text 6: "AB", a one-byte line-break command, "CD",
       then the zero-first-byte terminator. The LSB-first paths are
       00, 01, 10, and 11, encoded as 0xD8. */
    put_rom32(0x08020000 + 24, 0x08025060);
    rom[0x25060] = 0xD8;
    put_rom16(0x08022000, 1);
    put_rom16(0x08022002, 2);
    put_rom16(0x08023000 + 4, 3);
    put_rom16(0x08023000 + 6, 4);
    put_rom16(0x08023000 + 8, 5);
    put_rom16(0x08023000 + 10, 6);
    put_rom32(0x08023000 + 12, UINT32_C(0xFFFF4241));
    put_rom32(0x08023000 + 16, UINT32_C(0xFFFF0001));
    put_rom32(0x08023000 + 20, UINT32_C(0xFFFF4443));
    put_rom32(0x08023000 + 24, UINT32_C(0xFFFF0000));

    rom[item_record(&profile, 1) - 0x08000000 + 6] = 1;
    put_rom16(item_record(&profile, 1), 3);
    put_rom16(item_record(&profile, 1) + 2, 5);
    rom[item_record(&profile, 1) - 0x08000000 + 7] = 0;
    put_rom32(item_record(&profile, 1) + 8, IA_WEAPON);
    rom[item_record(&profile, 1) - 0x08000000 + 0x14] = 40;
    rom[item_record(&profile, 1) - 0x08000000 + 0x15] = 7;
    rom[item_record(&profile, 1) - 0x08000000 + 0x16] = 85;
    rom[item_record(&profile, 1) - 0x08000000 + 0x19] = 0x12;
    rom[item_record(&profile, 1) - 0x08000000 + 0x1C] = 31;

    /* Populate every encoded item used by the swap tests. With Sacred
       Echoes-style immovable-attribute validation enabled, an absent item
       record is rejected rather than treated as transferable. */
    rom[item_record(&profile, 0x2D) - 0x08000000 + 6] = 0x2D;
    rom[item_record(&profile, 0x2D) - 0x08000000 + 7] = 0;
    put_rom32(item_record(&profile, 0x2D) + 8, IA_WEAPON);
    rom[item_record(&profile, 0x2D) - 0x08000000 + 0x1C] = 71;
    rom[item_record(&profile, 0x35) - 0x08000000 + 6] = 0x35;
    rom[item_record(&profile, 0x38) - 0x08000000 + 6] = 0x38;
    put_rom16(item_record(&profile, 0x38), 4);
    put_rom32(item_record(&profile, 0x38) + 8, IA_MAGIC);

    assert(fe8_catalog_init(&memory, &profile, &catalog));
    {
        char decoded[32];
        Fe8ItemInfo item;
        assert(fe8_catalog_text(&memory, &catalog, 1, decoded, sizeof(decoded)));
        assert(strcmp(decoded, "Alice") == 0);
        assert(fe8_catalog_text(&memory, &catalog, 6, decoded, sizeof(decoded)));
        assert(strcmp(decoded, "AB CD") == 0);
        assert(fe8_catalog_item(&memory, &catalog, 0x2801, &item));
        assert(strcmp(item.name, "Test Blade") == 0 && item.uses == 40 && item.might == 7);
        assert(item.min_range == 1 && item.max_range == 2);
        assert(strcmp(item.description, "A dependable blade.") == 0);
        assert(fe8_catalog_item(&memory, &catalog, 0x2838, &item));
        assert(strcmp(item.name, "Fire") == 0 && !item.movable);
    }

    write8(NULL, profile.bm_state + 4, 1 << 4);
    write8(NULL, profile.play_state + 0x0E, 9);
    put32(first, character_a);
    put32(second, character_b);
    put32(first + 4, class_data);
    put32(second + 4, class_data);
    put_rom16(character_a, 1);
    put_rom16(character_b, 1);
    put_rom16(class_data, 2);
    rom[character_a - 0x08000000 + 4] = 0x11;
    rom[character_b - 0x08000000 + 4] = 0x22;
    put_rom32(character_a + 0x28, UINT32_C(0x00000100));
    put_rom32(character_b + 0x28, UINT32_C(0x00000400));
    put_rom32(class_data + 0x28, UINT32_C(0x00000200));
    write8(NULL, first + 8, 12);
    write8(NULL, first + 9, 44);
    write8(NULL, first + 0x12, 30);
    write8(NULL, first + 0x13, 20);
    write8(NULL, first + 0x14, 9);
    write8(NULL, first + 0x15, 8);
    write8(NULL, first + 0x16, 7);
    write8(NULL, first + 0x17, 6);
    write8(NULL, first + 0x18, 5);
    write8(NULL, first + 0x19, 4);
    write8(NULL, first + 0x1A, 1);
    write8(NULL, first + 0x1D, 2);
    write8(NULL, first + 0x28, 31);
    write8(NULL, second + 0x28, 1);
    rom[character_a - 0x08000000 + 0x13] = 3;
    rom[class_data - 0x08000000 + 0x11] = 5;
    rom[class_data - 0x08000000 + 0x12] = 6;
    write8(NULL, second + 8, 7);
    put16(first + 0x1E, 0x2801);
    put16(second + 0x20, 0x052D);
    put16(0x0203B200, 0x1435);

    assert(fe8_extract_prebattle_inventory(&memory, &profile, &catalog, &snapshot));
    assert(snapshot.chapter == 9 && snapshot.unit_count == 2);
    assert(snapshot.units[0].character_id == 0x11);
    assert(strcmp(snapshot.units[0].name, "Alice") == 0);
    assert(strcmp(snapshot.units[0].class_name, "Fighter") == 0);
    assert(snapshot.units[0].exp == 44 && snapshot.units[0].hp == 20 &&
        snapshot.units[0].max_hp == 30);
    assert(snapshot.units[0].power == 9 && snapshot.units[0].skill == 8 &&
        snapshot.units[0].speed == 7 && snapshot.units[0].defense == 6 &&
        snapshot.units[0].resistance == 5 && snapshot.units[0].luck == 4);
    assert(snapshot.units[0].constitution == 9 && snapshot.units[0].movement == 8);
    assert(snapshot.units[0].ranks[0] == 31 && snapshot.units[1].ranks[0] == 1);
    assert(snapshot.units[0].attributes == UINT32_C(0x00000300));
    assert(snapshot.units[1].attributes == UINT32_C(0x00000600));
    assert(snapshot.units[0].items[0] == 0x2801);
    assert(snapshot.units[1].items[1] == 0x052D);
    assert(snapshot.supply_count == 1 && snapshot.supply[0] == 0x1435);
    assert(snapshot.supply_address == 0x0203B200 && snapshot.supply_capacity == 200);
    assert(snapshot.supply_display_count == 2);
    assert(snapshot.supply_display_slots[0] == 0 && snapshot.supply_display_slots[1] == 1);
    assert(fe8_inventory_item_use_state(&snapshot.units[0],
        &snapshot.units[0].item_info[0]) == FE8_INVENTORY_USE_READY);
    assert(fe8_inventory_item_use_state(&snapshot.units[1],
        &snapshot.units[1].item_info[1]) == FE8_INVENTORY_USE_RANK);
    assert(fe8_inventory_item_use_state(&snapshot.units[0],
        &snapshot.supply_info[0]) == FE8_INVENTORY_USE_ITEM);
    {
        Fe8InventoryUnit unit = snapshot.units[0];
        Fe8ItemInfo item = snapshot.units[0].item_info[0];
        item.attributes |= IA_LOCK_1;
        assert(fe8_inventory_item_use_state(&unit, &item) == FE8_INVENTORY_USE_LOCKED);
        unit.attributes |= CA_LOCK_1;
        assert(fe8_inventory_item_use_state(&unit, &item) == FE8_INVENTORY_USE_READY);
        item.attributes = IA_WEAPON | IA_MAGIC;
        unit.status = 3;
        assert(fe8_inventory_item_use_state(&unit, &item) == FE8_INVENTORY_USE_STATUS);
        item.attributes = IA_STAFF;
        item.weapon_type = 4;
        item.weapon_rank = 1;
        unit.ranks[4] = 1;
        unit.status = 2;
        assert(fe8_inventory_item_use_state(&unit, &item) == FE8_INVENTORY_USE_STATUS);
        item.attributes = IA_WEAPON | IA_LOCK_3;
        item.weapon_type = 11;
        unit.status = 0;
        unit.attributes = CA_LOCK_3;
        assert(fe8_inventory_item_use_state(&unit, &item) == FE8_INVENTORY_USE_READY);
    }

    assert(fe8_swap_prebattle_items(&memory, &writer, &profile,
        first, 0, 0x2801, second, 1, 0x052D));
    assert(read8(NULL, first + 0x1E) == 0x2D);
    assert(read8(NULL, second + 0x20) == 0x01);
    assert(!fe8_swap_prebattle_items(&memory, &writer, &profile,
        first, 0, 0xFFFF, second, 1, 0x2801));

    write8(NULL, profile.bm_state + 4, 0);
    assert(fe8_extract_prebattle_inventory(&memory, &profile, &catalog, &snapshot));
    {
        Fe8InventoryEndpoint unit = {FE8_INVENTORY_ENDPOINT_UNIT, first, 0};
        Fe8InventoryEndpoint supply = {FE8_INVENTORY_ENDPOINT_SUPPLY, 0x0203B200, 0};
        assert(fe8_swap_inventory_endpoints(&memory, &writer, &profile,
            unit, 0x052D, supply, 0x1435));
        assert(!snapshot.prebattle);
        assert(read8(NULL, first + 0x1E) == 0x35);
        assert(read8(NULL, 0x0203B200) == 0x2D);
    }
    {
        Fe8InventoryEndpoint spell = {FE8_INVENTORY_ENDPOINT_UNIT, first, 2};
        Fe8InventoryEndpoint empty = {FE8_INVENTORY_ENDPOINT_UNIT, second, 2};
        put16(first + 0x22, 0x2838);
        assert(!fe8_swap_inventory_endpoints(&memory, &writer, &profile,
            spell, 0x2838, empty, 0));
        assert(read8(NULL, first + 0x22) == 0x38);
        assert(read8(NULL, second + 0x22) == 0);
    }
    puts("pre-battle inventory tests passed");
    return 0;
}
