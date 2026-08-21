#include "fe8_catalog.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t rom[0x1000];

static uint8_t read8(void *context, uint32_t address) {
    (void)context;
    if (address >= UINT32_C(0x08000000) && address < UINT32_C(0x08001000))
        return rom[address - UINT32_C(0x08000000)];
    return 0;
}

static void put32(uint32_t address, uint32_t value) {
    size_t offset = address - UINT32_C(0x08000000);
    rom[offset] = (uint8_t)value;
    rom[offset + 1] = (uint8_t)(value >> 8);
    rom[offset + 2] = (uint8_t)(value >> 16);
    rom[offset + 3] = (uint8_t)(value >> 24);
}

int main(void) {
    static const char encoded[] = "Eff: {|}";
    Fe8MemoryReader memory = {NULL, read8};
    Fe8Catalog catalog = {0};
    char decoded[128];

    memset(rom, 0, sizeof(rom));
    catalog.valid = true;
    catalog.message_table = UINT32_C(0x08000100);
    put32(UINT32_C(0x08000104), UINT32_C(0x88000200));
    memcpy(rom + 0x200, encoded, sizeof(encoded));

    assert(fe8_catalog_text(&memory, &catalog, 1, decoded, sizeof(decoded)));
    assert(strcmp(decoded, "Eff: Cavalry, Armored, Fliers") == 0);

    puts("FE8 catalog effectiveness-glyph tests passed");
    return 0;
}
