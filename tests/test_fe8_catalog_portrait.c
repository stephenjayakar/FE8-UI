#include "fe8_catalog.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define ROM_BASE UINT32_C(0x08000000)
#define ROM_SIZE UINT32_C(0x02000000)

static uint8_t rom[ROM_SIZE];

static uint8_t read8(void *context, uint32_t address) {
    (void)context;
    if (address >= ROM_BASE && address - ROM_BASE < ROM_SIZE)
        return rom[address - ROM_BASE];
    return 0;
}

static void put16(uint32_t address, uint16_t value) {
    rom[address - ROM_BASE] = (uint8_t)value;
    rom[address - ROM_BASE + 1] = (uint8_t)(value >> 8);
}

static void put32(uint32_t address, uint32_t value) {
    put16(address, (uint16_t)value);
    put16(address + 2, (uint16_t)(value >> 16));
}

static void put_raw(uint32_t address, const uint8_t *data, size_t size) {
    put32(address, (uint32_t)(size + 4) << 8);
    memcpy(rom + address - ROM_BASE + 4, data, size);
}

static void put_lz_literals(uint32_t address, uint8_t value, size_t size) {
    size_t written = 0;
    put32(address, UINT32_C(0x10) | ((uint32_t)size << 8));
    address += 4;
    while (written < size) {
        size_t count = size - written;
        size_t index;
        if (count > 8)
            count = 8;
        rom[address++ - ROM_BASE] = 0;
        for (index = 0; index < count; ++index)
            rom[address++ - ROM_BASE] = value;
        written += count;
    }
}

static void set_index(uint8_t *tiles, unsigned tile, unsigned x, unsigned y,
    uint8_t index) {
    size_t offset = (size_t)tile * 32 + (size_t)y * 4 + x / 2;
    if (x & 1)
        tiles[offset] = (uint8_t)((tiles[offset] & 0x0F) | (index << 4));
    else
        tiles[offset] = (uint8_t)((tiles[offset] & 0xF0) | index);
}

int main(void) {
    enum {
        TABLE = 0x08010000,
        FULL = 0x08012000,
        CHIBI = 0x08014000,
        PALETTE = 0x08015000,
        FULL_LZ = 0x08016000,
    };
    Fe8MemoryReader memory = {NULL, read8};
    Fe8Catalog catalog = {0};
    uint8_t full_tiles[128 * 32] = {0};
    uint8_t pixels[FE8_PORTRAIT_WIDTH * FE8_PORTRAIT_HEIGHT];
    uint32_t colors[FE8_PORTRAIT_PALETTE_SIZE];
    uint32_t record = TABLE + 28;

    catalog.portrait_table_bias = TABLE;
    catalog.valid = true;
    put32(record, FULL);
    put32(record + 4, CHIBI);
    put32(record + 8, PALETTE);

    /* PutFace80x72 is the native x=8..87, y=0..71 window of the 96x80 mug.
       The upper corners are transparent; side pieces begin at y=48. */
    set_index(full_tiles, 0x00, 0, 0, 1);  /* output (8, 0) */
    set_index(full_tiles, 0x07, 7, 0, 2);  /* output (71, 0) */
    set_index(full_tiles, 0x15, 0, 0, 3);  /* output (0, 48) */
    set_index(full_tiles, 0x16, 7, 0, 4);  /* output (79, 48) */
    set_index(full_tiles, 0x55, 0, 7, 1);  /* output (0, 71) */
    set_index(full_tiles, 0x56, 7, 7, 2);  /* output (79, 71) */
    set_index(full_tiles, 0x10, 0, 7, 3);  /* output (8, 71) */
    set_index(full_tiles, 0x53, 7, 7, 4);  /* output (71, 71) */
    put_raw(FULL, full_tiles, sizeof(full_tiles));
    put_lz_literals(CHIBI, 0x55, 16 * 32);

    put16(PALETTE + 2, UINT16_C(0x001F));  /* red */
    put16(PALETTE + 4, UINT16_C(0x03E0));  /* green */
    put16(PALETTE + 6, UINT16_C(0x7C00));  /* blue */
    put16(PALETTE + 8, UINT16_C(0x7FFF));  /* white */
    put16(PALETTE + 10, UINT16_C(0x4210));

    assert(fe8_catalog_portrait(&memory, &catalog, 1, pixels, colors));
    assert(pixels[0] == 0);
    assert(pixels[FE8_PORTRAIT_WIDTH - 1] == 0);
    assert(pixels[8] == 1);
    assert(pixels[71] == 2);
    assert(pixels[48 * FE8_PORTRAIT_WIDTH] == 3);
    assert(pixels[48 * FE8_PORTRAIT_WIDTH + 79] == 4);
    assert(pixels[71 * FE8_PORTRAIT_WIDTH] == 1);
    assert(pixels[71 * FE8_PORTRAIT_WIDTH + 79] == 2);
    assert(pixels[71 * FE8_PORTRAIT_WIDTH + 8] == 3);
    assert(pixels[71 * FE8_PORTRAIT_WIDTH + 71] == 4);
    assert(pixels[FE8_PORTRAIT_WIDTH + 1] == 0);
    assert(colors[0] == 0);
    assert(colors[1] == UINT32_C(0xFF0000FF));
    assert(colors[2] == UINT32_C(0xFF00FF00));
    assert(colors[3] == UINT32_C(0xFFFF0000));
    assert(colors[4] == UINT32_C(0xFFFFFFFF));

    /* The +0 full-mug field takes precedence over the different +4 chibi. */
    assert(pixels[8] != 5);

    /* Full mugs may use the same LZ77 container as other FE8 graphics. */
    put_lz_literals(FULL_LZ, 0x66, 128 * 32);
    put32(record, FULL_LZ);
    assert(fe8_catalog_portrait(&memory, &catalog, 1, pixels, colors));
    assert(pixels[0] == 0);
    assert(pixels[8] == 6);
    assert(pixels[48 * FE8_PORTRAIT_WIDTH] == 6);
    assert(pixels[FE8_PORTRAIT_WIDTH * FE8_PORTRAIT_HEIGHT - 1] == 6);

    /* Hacks without a usable full mug still retain the old chibi fallback. */
    put32(record, 0);
    assert(fe8_catalog_portrait(&memory, &catalog, 1, pixels, colors));
    assert(pixels[0] == 0);
    assert(pixels[4 * FE8_PORTRAIT_WIDTH + 8] == 5);
    assert(pixels[4 * FE8_PORTRAIT_WIDTH + 9] == 5);
    assert(pixels[67 * FE8_PORTRAIT_WIDTH + 71] == 5);
    assert(pixels[68 * FE8_PORTRAIT_WIDTH + 8] == 0);

    put32(record + 4, 0);
    assert(!fe8_catalog_portrait(&memory, &catalog, 1, pixels, colors));
    assert(pixels[0] == 0);

    puts("catalog portrait tests passed");
    return 0;
}
