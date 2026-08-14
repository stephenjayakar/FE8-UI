#include "extended_map_renderer.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t ewram[0x40000];
static uint8_t palette[0x400];
static uint8_t vram[0x18000];

static uint8_t test_read8(void *context, uint32_t address) {
    (void)context;
    if (address >= 0x02000000 && address < 0x02040000)
        return ewram[address - 0x02000000];
    if (address >= 0x05000000 && address < 0x05000400)
        return palette[address - 0x05000000];
    if (address >= 0x06000000 && address < 0x06018000)
        return vram[address - 0x06000000];
    return 0;
}

static void put16(uint8_t *bytes, size_t offset, uint16_t value) {
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *bytes, size_t offset, uint32_t value) {
    put16(bytes, offset, (uint16_t)value);
    put16(bytes, offset + 2, (uint16_t)(value >> 16));
}

int main(void) {
    Fe8MemoryView memory = {NULL, test_read8};
    Fe8MapRenderState state = {
        .map_width = 2,
        .map_height = 1,
        .camera_x = 0,
        .camera_y = 0,
        .base_tile_rows = 0x02000100,
        .fog_rows = 0,
        .tileset_config = 0x02001000,
        .tile_graphics = 0x06008000,
        .palette = 0x05000000,
        .normal_palette_bank_offset = 11,
        .fog_palette_bank_offset = 6,
    };
    Fe8ExtendedViewport viewport = {32, 16, 0, 0};
    Fe8HostPixel output[32 * 16];
    int map_x = -1;
    int map_y = -1;
    unsigned index;

    memset(ewram, 0, sizeof(ewram));
    memset(palette, 0, sizeof(palette));
    memset(vram, 0, sizeof(vram));
    put32(ewram, 0x100, 0x02000200);
    put16(ewram, 0x200, 0);
    put16(ewram, 0x202, 4);
    for (index = 0; index < 8; ++index)
        put16(ewram, 0x1000 + index * 2, 1);
    memset(vram + 0x8000 + 32, 0x11, 32);
    put16(palette, (11 * 16 + 1) * 2, 0x001F);

    assert(fe8_extended_state_is_sane(&state));
    assert(fe8_render_extended_terrain(&memory, &state, viewport, output, 32));
    assert(output[0] == UINT32_C(0xFF0000FF));
    assert(output[31] == UINT32_C(0xFF0000FF));
    put16(palette, (6 * 16 + 1) * 2, 0x7C00);
    state.normal_palette_bank_offset = 6;
    assert(fe8_render_extended_terrain(&memory, &state, viewport, output, 32));
    assert(output[0] == UINT32_C(0xFFFF0000));
    assert(output[31] == UINT32_C(0xFFFF0000));
    assert(fe8_canvas_to_map_tile(&state, viewport, 20, 8, &map_x, &map_y));
    assert(map_x == 1 && map_y == 0);
    assert(!fe8_canvas_to_map_tile(&state, viewport, 40, 8, &map_x, &map_y));
    puts("extended renderer tests passed");
    return 0;
}
