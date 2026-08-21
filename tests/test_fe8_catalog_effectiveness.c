#include "fe8_catalog.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define ROM_BASE UINT32_C(0x08000000)
#define ROM_SIZE 0x1000
#define MESSAGE_TABLE UINT32_C(0x08000100)
#define ARCHANAE_ITEM_TABLE UINT32_C(0x09AA54F8)
#define STANDARD_ITEM_TABLE UINT32_C(0x09AA64F8)
#define ITEM_SIZE 0x24
#define ITEM_DATA_SIZE 0x100

static uint8_t rom[ROM_SIZE];
static uint8_t item_data[ITEM_DATA_SIZE];

static uint8_t read8(void *context, uint32_t address) {
    (void)context;
    if (address >= ROM_BASE && address - ROM_BASE < ROM_SIZE)
        return rom[address - ROM_BASE];
    if (address >= ARCHANAE_ITEM_TABLE &&
            address - ARCHANAE_ITEM_TABLE < ITEM_DATA_SIZE)
        return item_data[address - ARCHANAE_ITEM_TABLE];
    if (address >= STANDARD_ITEM_TABLE &&
            address - STANDARD_ITEM_TABLE < ITEM_DATA_SIZE)
        return item_data[address - STANDARD_ITEM_TABLE];
    return 0;
}

static void put16(uint32_t address, uint16_t value) {
    size_t offset = address - ROM_BASE;
    rom[offset] = (uint8_t)value;
    rom[offset + 1] = (uint8_t)(value >> 8);
}

static void put32(uint32_t address, uint32_t value) {
    put16(address, (uint16_t)value);
    put16(address + 2, (uint16_t)(value >> 16));
}

static void put_text(uint16_t id, uint32_t address, const char *text) {
    put32(MESSAGE_TABLE + (uint32_t)id * 4, address | UINT32_C(0x80000000));
    memcpy(rom + address - ROM_BASE, text, strlen(text) + 1);
}

static void put_item(uint8_t id, uint16_t name, uint16_t description) {
    size_t offset = (size_t)id * ITEM_SIZE;
    item_data[offset] = (uint8_t)name;
    item_data[offset + 1] = (uint8_t)(name >> 8);
    item_data[offset + 2] = (uint8_t)description;
    item_data[offset + 3] = (uint8_t)(description >> 8);
    item_data[offset + 6] = id;
}

static Fe8Catalog make_catalog(uint32_t item_table) {
    Fe8Catalog catalog = {0};
    catalog.valid = true;
    catalog.message_table = MESSAGE_TABLE;
    catalog.item_table = item_table;
    return catalog;
}

int main(void) {
    Fe8MemoryReader memory = {NULL, read8};
    Fe8Catalog catalog;
    Fe8ItemInfo item;

    memset(rom, 0, sizeof(rom));
    memset(item_data, 0, sizeof(item_data));
    put_text(1, UINT32_C(0x08000200), "Rapier");
    put_text(2, UINT32_C(0x08000220), "Marth only. Avoid +20\x01" "Eff: {|");
    put_text(3, UINT32_C(0x08000260), "Excalibur");
    put_text(4, UINT32_C(0x08000280), "Merric only. Eff: }");
    put_text(5, UINT32_C(0x080002C0), "Debug item");
    put_text(6, UINT32_C(0x080002E0), "Literal punctuation: {|}");
    put_item(1, 1, 2);
    put_item(2, 3, 4);
    put_item(3, 5, 6);

    catalog = make_catalog(ARCHANAE_ITEM_TABLE);
    assert(fe8_catalog_item(&memory, &catalog, UINT16_C(0x2801), &item));
    assert(strcmp(item.description,
        "Marth only. Avoid +20 Eff: Cavalry, Armored") == 0);
    assert(fe8_catalog_item(&memory, &catalog, UINT16_C(0x2102), &item));
    assert(strcmp(item.description, "Merric only. Eff: Fliers") == 0);
    assert(fe8_catalog_item(&memory, &catalog, UINT16_C(0x0103), &item));
    assert(strcmp(item.description, "Literal punctuation: {|}") == 0);

    catalog = make_catalog(STANDARD_ITEM_TABLE);
    assert(fe8_catalog_item(&memory, &catalog, UINT16_C(0x2801), &item));
    assert(strcmp(item.description, "Marth only. Avoid +20 Eff: {|") == 0);

    puts("FE8 catalog effectiveness-glyph tests passed");
    return 0;
}
