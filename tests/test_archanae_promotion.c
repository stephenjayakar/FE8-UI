#include "prebattle_inventory.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define ROM_START UINT32_C(0x08000000)
#define ROM_SIZE UINT32_C(0x02000000)
#define EWRAM_START UINT32_C(0x02000000)
#define EWRAM_SIZE UINT32_C(0x00040000)
#define ARCHANAE_ITEM_TABLE UINT32_C(0x09AA54F8)
#define ARCHANAE_PROMOTION_TABLE UINT32_C(0x08B2B928)
#define MASTER_SEAL_CLASSES UINT32_C(0x098CC3D4)

static unsigned char ewram[EWRAM_SIZE];
static unsigned char rom[ROM_SIZE];

static uint8_t read8(void *context, uint32_t address) {
    (void)context;
    if (address >= EWRAM_START && address < EWRAM_START + EWRAM_SIZE)
        return ewram[address - EWRAM_START];
    if (address >= ROM_START && address < ROM_START + ROM_SIZE)
        return rom[address - ROM_START];
    return 0;
}

static void put16(uint32_t address, uint16_t value) {
    assert(address >= ROM_START && address + 1 < ROM_START + ROM_SIZE);
    rom[address - ROM_START] = (uint8_t)value;
    rom[address - ROM_START + 1] = (uint8_t)(value >> 8);
}

static void put32(uint32_t address, uint32_t value) {
    put16(address, (uint16_t)value);
    put16(address + 2, (uint16_t)(value >> 16));
}

static void put_ewram32(uint32_t address, uint32_t value) {
    assert(address >= EWRAM_START && address + 3 < EWRAM_START + EWRAM_SIZE);
    for (unsigned i = 0; i < 4; ++i)
        ewram[address - EWRAM_START + i] = (uint8_t)(value >> (i * 8));
}

int main(void) {
    Fe8Profile profile = {0};
    Fe8MemoryReader memory = {NULL, read8};
    Fe8Catalog catalog = {0};
    Fe8InventorySnapshot snapshot;
    const uint32_t unit = UINT32_C(0x02001000);
    const uint32_t character = UINT32_C(0x08010000);
    const uint32_t class_data = UINT32_C(0x08010100);
    const uint32_t master_record = ARCHANAE_ITEM_TABLE + UINT32_C(0x88) * UINT32_C(0x24);

    memset(ewram, 0, sizeof(ewram));
    memset(rom, 0, sizeof(rom));

    profile.blue_units = unit;
    profile.bm_state = UINT32_C(0x02002000);
    profile.play_state = UINT32_C(0x02002020);
    profile.convoy_items = UINT32_C(0x02003000);
    profile.inventory.item_table = ARCHANAE_ITEM_TABLE;
    profile.inventory.get_convoy_items = UINT32_C(0x08020000);
    profile.inventory.convoy_capacity = 1;

    put_ewram32(unit, character);
    put_ewram32(unit + 4, class_data);
    ewram[unit - EWRAM_START + 8] = 20;
    ewram[unit - EWRAM_START + 0x1E] = 0x88;
    ewram[unit - EWRAM_START + 0x1F] = 1;
    rom[character - ROM_START + 4] = 0x0D; /* Lena */
    rom[class_data - ROM_START + 4] = 0x4A; /* Cleric */

    rom[master_record - ROM_START + 6] = 0x88;
    rom[master_record - ROM_START + 0x22] = 10;
    put16(ARCHANAE_PROMOTION_TABLE, 0x88);
    put32(ARCHANAE_PROMOTION_TABLE + 4, MASTER_SEAL_CLASSES);
    put32(ARCHANAE_PROMOTION_TABLE + 8, 0);
    put16(ARCHANAE_PROMOTION_TABLE + 12, 0xFFFF);
    rom[MASTER_SEAL_CLASSES - ROM_START] = 0x4A;
    rom[MASTER_SEAL_CLASSES - ROM_START + 1] = 0;

    assert(fe8_extract_prebattle_inventory(&memory, &profile, &catalog, &snapshot));
    assert(snapshot.unit_count == 1);
    assert(snapshot.units[0].character_id == 0x0D);
    assert(snapshot.units[0].class_id == 0x4A);
    assert(snapshot.units[0].level == 20);

    Fe8ItemInfo *master = &snapshot.units[0].item_info[0];
    assert(master->id == 0x88);
    assert(master->promotion_item);
    assert(master->promotion_rules_complete);
    assert(master->promotion_level == 10);
    assert(master->promotion_class_ids[0x4A / 8] & (1u << (0x4A % 8)));
    assert(fe8_inventory_item_use_state(&snapshot.units[0], master) ==
        FE8_INVENTORY_USE_READY);

    Fe8InventoryUnit candidate = snapshot.units[0];
    candidate.level = 9;
    assert(fe8_inventory_item_use_state(&candidate, master) == FE8_INVENTORY_USE_LOCKED);
    candidate.level = 20;
    candidate.class_id = 0x4B;
    assert(fe8_inventory_item_use_state(&candidate, master) == FE8_INVENTORY_USE_LOCKED);

    /* A table entry with an extra native callback is deliberately not guessed. */
    put32(ARCHANAE_PROMOTION_TABLE + 8, UINT32_C(0x08B4098D));
    assert(fe8_extract_prebattle_inventory(&memory, &profile, &catalog, &snapshot));
    master = &snapshot.units[0].item_info[0];
    assert(master->promotion_item && !master->promotion_rules_complete);
    assert(fe8_inventory_item_use_state(&snapshot.units[0], master) ==
        FE8_INVENTORY_USE_UNKNOWN);

    puts("Archanae promotion-table decoding and Lena Master Seal eligibility passed");
    return 0;
}
