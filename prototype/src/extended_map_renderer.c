#include "extended_map_renderer.h"

#include <limits.h>

enum {
    GBA_WIDTH = 240,
    GBA_HEIGHT = 160,
    MAP_TILE_SIZE = 16,
    SUBTILE_SIZE = 8,
    GBA_TILE_BYTES = 32,
};

static uint16_t read16(const Fe8MemoryView *memory, uint32_t address) {
    return (uint16_t)(memory->read8(memory->context, address) |
        ((uint16_t)memory->read8(memory->context, address + 1) << 8));
}

static uint32_t read32(const Fe8MemoryView *memory, uint32_t address) {
    return (uint32_t)read16(memory, address) |
        ((uint32_t)read16(memory, address + 2) << 16);
}

static int floor_div(int value, int divisor) {
    int quotient = value / divisor;
    int remainder = value % divisor;
    return quotient - (remainder < 0);
}

static Fe8HostPixel gba_color(uint16_t color) {
    uint32_t red = color & 31;
    uint32_t green = (color >> 5) & 31;
    uint32_t blue = (color >> 10) & 31;
    red = (red << 3) | (red >> 2);
    green = (green << 3) | (green >> 2);
    blue = (blue << 3) | (blue >> 2);
    return UINT32_C(0xFF000000) | (blue << 16) | (green << 8) | red;
}

bool fe8_extended_state_is_sane(const Fe8MapRenderState *state) {
    return state && state->map_width > 0 && state->map_width <= 64 &&
        state->map_height > 0 && state->map_height <= 64 &&
        state->base_tile_rows >= UINT32_C(0x02000000) &&
        state->base_tile_rows < UINT32_C(0x02040000) &&
        state->tileset_config >= UINT32_C(0x02000000) &&
        state->tileset_config < UINT32_C(0x02040000) &&
        state->tile_graphics >= UINT32_C(0x06000000) &&
        state->tile_graphics < UINT32_C(0x06018000) &&
        state->palette >= UINT32_C(0x05000000) &&
        state->palette < UINT32_C(0x05000400);
}

static Fe8HostPixel terrain_pixel(
    const Fe8MemoryView *memory,
    const Fe8MapRenderState *state,
    int world_x,
    int world_y) {
    int map_x = floor_div(world_x, MAP_TILE_SIZE);
    int map_y = floor_div(world_y, MAP_TILE_SIZE);
    int tile_pixel_x;
    int tile_pixel_y;
    uint32_t row;
    uint16_t metatile;
    unsigned quadrant;
    uint16_t entry;
    unsigned pixel_x;
    unsigned pixel_y;
    unsigned tile_number;
    unsigned palette_bank;
    uint32_t packed_address;
    uint8_t packed;
    unsigned color_index;
    uint16_t color;

    if (map_x < 0 || map_y < 0 || map_x >= state->map_width || map_y >= state->map_height)
        return UINT32_C(0xFF101418);

    row = read32(memory, state->base_tile_rows + (uint32_t)map_y * 4);
    if (row < UINT32_C(0x02000000) || row >= UINT32_C(0x02040000))
        return UINT32_C(0xFFFF00FF);
    metatile = read16(memory, row + (uint32_t)map_x * 2);

    tile_pixel_x = world_x - map_x * MAP_TILE_SIZE;
    tile_pixel_y = world_y - map_y * MAP_TILE_SIZE;
    quadrant = (unsigned)(tile_pixel_x / SUBTILE_SIZE) +
        (unsigned)(tile_pixel_y / SUBTILE_SIZE) * 2;
    entry = read16(memory, state->tileset_config + (uint32_t)(metatile + quadrant) * 2);

    pixel_x = (unsigned)tile_pixel_x & 7;
    pixel_y = (unsigned)tile_pixel_y & 7;
    if (entry & 0x0400)
        pixel_x = 7 - pixel_x;
    if (entry & 0x0800)
        pixel_y = 7 - pixel_y;

    tile_number = entry & 0x03FF;
    {
        bool fogged = false;
        if (state->fog_rows) {
            uint32_t fog_row = read32(memory, state->fog_rows + (uint32_t)map_y * 4);
            if (fog_row >= UINT32_C(0x02000000) && fog_row < UINT32_C(0x02040000))
                fogged = memory->read8(memory->context, fog_row + (uint32_t)map_x) != 0;
        }
        /* DisplayBmTile adds palette 6 (fog) or 11 to each config entry. */
        palette_bank = (((entry >> 12) & 0xF) + (fogged ? 6 : 11)) & 0xF;
    }

    packed_address = state->tile_graphics + tile_number * GBA_TILE_BYTES + pixel_y * 4 + pixel_x / 2;
    packed = memory->read8(memory->context, packed_address);
    color_index = (pixel_x & 1) ? packed >> 4 : packed & 0xF;
    color = read16(memory, state->palette + (palette_bank * 16 + color_index) * 2);
    return gba_color(color);
}

bool fe8_render_extended_terrain(
    const Fe8MemoryView *memory,
    const Fe8MapRenderState *state,
    Fe8ExtendedViewport viewport,
    Fe8HostPixel *pixels,
    size_t stride_pixels) {
    int x;
    int y;

    if (!memory || !memory->read8 || !pixels || !fe8_extended_state_is_sane(state) ||
        viewport.width <= 0 || viewport.height <= 0 || stride_pixels < (size_t)viewport.width)
        return false;

    for (y = 0; y < viewport.height; ++y) {
        int world_y = state->camera_y + y - viewport.gba_y;
        for (x = 0; x < viewport.width; ++x) {
            int world_x = state->camera_x + x - viewport.gba_x;
            pixels[(size_t)y * stride_pixels + x] = terrain_pixel(memory, state, world_x, world_y);
        }
    }
    return true;
}

bool fe8_canvas_to_map_tile(
    const Fe8MapRenderState *state,
    Fe8ExtendedViewport viewport,
    int canvas_x,
    int canvas_y,
    int *map_x,
    int *map_y) {
    int x;
    int y;
    if (!state || !map_x || !map_y)
        return false;
    x = floor_div(state->camera_x + canvas_x - viewport.gba_x, MAP_TILE_SIZE);
    y = floor_div(state->camera_y + canvas_y - viewport.gba_y, MAP_TILE_SIZE);
    if (x < 0 || y < 0 || x >= state->map_width || y >= state->map_height)
        return false;
    *map_x = x;
    *map_y = y;
    return true;
}
