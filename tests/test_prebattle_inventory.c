#include "prebattle_inventory.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define IA_WEAPON UINT32_C(1u << 0)
#define IA_MAGIC UINT32_C(1u << 1)
#define IA_LOCK_1 UINT32_C(1u << 11)
#define UA_LOCK_1 UINT32_C(1u << 16)

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
    rom[0x809B10 + 0x24 + 6] = 1;
    rom[0x809B10 + 0x24 + 7] = 0;
    put_rom32(0x08809B10 + 0x24 + 8, IA_WEAPON);
    rom[0x809B10 + 0x24 + 0x1C] = 31;
    put_rom16(0x08809B10 + 0x24, 3);
    put_rom16(0x08809B10 + 0x24 + 2, 5);
    rom[0x809B10 + 0x24 + 0x14] = 40;
    rom[0x809B10 + 0x24 + 0x15] = 7;
    rom[0x809B10 + 0x24 + 0x19] = 0x12;
    /* Populate every encoded item used by the swap tests. With Sacred
       Echoes-style immovable-attribute validation enabled, an absent item
       record is rejected rather than treated as transferable. */
    rom[0x809B10 + 0x1C * 0x24 + 6] = 0x1C;
    rom[0x809B10 + 0x1C * 0x24 + 7] = 0;
    put_rom32(0x08809B10 + 0x1C * 0x24 + 8, IA_WEAPON);
    rom[0x809B10 + 0x1C * 0x24 + 0x1C] = 31;
    rom[0x809B10 + 0x2D * 0x24 + 6] = 0x2D;
    rom[0x809B10 + 0x35 * 0x24 + 6] = 0x35;
    rom[0x809B10 + 0x38 * 0x24 + 6] = 0x38;
    put_rom16(0x08809B10 + 0x38 * 0x24, 4);
    put_rom32(0x08809B10 + 0x38 * 0x24 + 8, 2);
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
    put32(first + 4, 0x08010100);
    put32(second + 4, 0x08010100);
    put_rom16(character_a, 1);
    put_rom16(character_b, 1);
    put_rom32(character_a + 0x28, UA_LOCK_1);
    put_rom16(0x08010100, 2);
    put_rom32(0x08010100 + 0x28, UINT32_C(1u << 17));
    rom[character_a - 0x08000000 + 4] = 0x11;
    rom[character_b - 0x08000000 + 4] = 0x22;
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
    write8(NULL, first + 0x30, 3);
    rom[character_a - 0x08000000 + 0x13] = 3;
    rom[0x10100 + 0x11] = 5;
    rom[0x10100 + 0x12] = 6;
    write8(NULL, second + 8, 7);
    put16(first + 0x1E, 0x281C);
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
    assert(snapshot.units[0].ranks[0] == 31 && snapshot.units[0].status == 3);
    assert((snapshot.units[0].attributes & UA_LOCK_1) != 0);
    assert((snapshot.units[0].attributes & UINT32_C(1u << 17)) != 0);
    assert(snapshot.units[0].items[0] == 0x281C);
    assert(snapshot.units[1].items[1] == 0x052D);
    assert(snapshot.supply_count == 1 && snapshot.supply[0] == 0x1435);
    assert(snapshot.supply_address == 0x0203B200 && snapshot.supply_capacity == 200);
    assert(snapshot.supply_display_count == 2);
    assert(snapshot.supply_display_slots[0] == 0 && snapshot.supply_display_slots[1] == 1);
    assert(fe8_inventory_item_use_state(&snapshot.units[0],
        &snapshot.units[0].item_info[0]) == FE8_INVENTORY_USE_READY);
    {
        Fe8ItemInfo ranked = snapshot.units[0].item_info[0];
        Fe8ItemInfo locked = snapshot.units[0].item_info[0];
        Fe8ItemInfo silenced = snapshot.units[0].item_info[0];
        ranked.weapon_rank = 71;
        locked.attributes |= IA_LOCK_1;
        silenced.attributes |= IA_MAGIC;
        assert(fe8_inventory_item_use_state(&snapshot.units[0], &ranked) ==
            FE8_INVENTORY_USE_RANK);
        assert(fe8_inventory_item_use_state(&snapshot.units[1], &locked) ==
            FE8_INVENTORY_USE_LOCKED);
        assert(fe8_inventory_item_use_state(&snapshot.units[0], &silenced) ==
            FE8_INVENTORY_USE_STATUS);
    }
    assert(fe8_swap_prebattle_items(&memory, &writer, &profile,
        first, 0, 0x281C, second, 1, 0x052D));
    assert(read8(NULL, first + 0x1E) == 0x2D);
    assert(read8(NULL, second + 0x20) == 0x1C);

    assert(!fe8_swap_prebattle_items(&memory, &writer, &profile,
        first, 0, 0xFFFF, second, 1, 0x281C));
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
