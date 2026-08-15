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

static void fill_canvas(Fe8HostPixel *pixels, size_t stride,
    int width, int height, Fe8HostPixel color) {
    int y;
    for (y = 0; y < height; ++y) {
        int x;
        for (x = 0; x < width; ++x)
            pixels[(size_t)y * stride + x] = color;
    }
}

static void draw_subtile(const Fe8MemoryView *memory,
    const Fe8MapRenderState *state, uint16_t entry, bool fogged,
    int destination_x, int destination_y, Fe8ExtendedViewport viewport,
    Fe8HostPixel *pixels, size_t stride) {
    unsigned tile_number = entry & 0x03FF;
    unsigned palette_bank = (((entry >> 12) & 0xF) +
        (fogged ? state->fog_palette_bank_offset :
            state->normal_palette_bank_offset)) & 0xF;
    unsigned output_y;
    for (output_y = 0; output_y < SUBTILE_SIZE; ++output_y) {
        int canvas_y = destination_y + (int)output_y;
        unsigned source_y = (entry & 0x0800) ? 7 - output_y : output_y;
        unsigned output_x;
        if (canvas_y < 0 || canvas_y >= viewport.height)
            continue;
        for (output_x = 0; output_x < SUBTILE_SIZE; ++output_x) {
            int canvas_x = destination_x + (int)output_x;
            unsigned source_x = (entry & 0x0400) ? 7 - output_x : output_x;
            uint32_t packed_address;
            uint8_t packed;
            unsigned color_index;
            uint16_t color;
            if (canvas_x < 0 || canvas_x >= viewport.width)
                continue;
            packed_address = state->tile_graphics + tile_number * GBA_TILE_BYTES +
                source_y * 4 + source_x / 2;
            packed = memory->read8(memory->context, packed_address);
            color_index = (source_x & 1) ? packed >> 4 : packed & 0xF;
            color = read16(memory,
                state->palette + (palette_bank * 16 + color_index) * 2);
            pixels[(size_t)canvas_y * stride + canvas_x] = gba_color(color);
        }
    }
}

bool fe8_render_extended_terrain(
    const Fe8MemoryView *memory,
    const Fe8MapRenderState *state,
    Fe8ExtendedViewport viewport,
    Fe8HostPixel *pixels,
    size_t stride_pixels) {
    int first_map_x;
    int first_map_y;
    int last_map_x;
    int last_map_y;
    int map_y;

    if (!memory || !memory->read8 || !pixels || !fe8_extended_state_is_sane(state) ||
        viewport.width <= 0 || viewport.height <= 0 || stride_pixels < (size_t)viewport.width)
        return false;

    fill_canvas(pixels, stride_pixels, viewport.width, viewport.height,
        UINT32_C(0xFF101418));
    first_map_x = floor_div(state->camera_x - viewport.gba_x, MAP_TILE_SIZE);
    first_map_y = floor_div(state->camera_y - viewport.gba_y, MAP_TILE_SIZE);
    last_map_x = floor_div(state->camera_x + viewport.width - 1 - viewport.gba_x,
        MAP_TILE_SIZE);
    last_map_y = floor_div(state->camera_y + viewport.height - 1 - viewport.gba_y,
        MAP_TILE_SIZE);
    if (first_map_x < 0) first_map_x = 0;
    if (first_map_y < 0) first_map_y = 0;
    if (last_map_x >= state->map_width) last_map_x = state->map_width - 1;
    if (last_map_y >= state->map_height) last_map_y = state->map_height - 1;
    for (map_y = first_map_y; map_y <= last_map_y; ++map_y) {
        uint32_t row = read32(memory,
            state->base_tile_rows + (uint32_t)map_y * 4);
        uint32_t fog_row = state->fog_rows ? read32(memory,
            state->fog_rows + (uint32_t)map_y * 4) : 0;
        int map_x;
        if (row < UINT32_C(0x02000000) || row >= UINT32_C(0x02040000))
            continue;
        for (map_x = first_map_x; map_x <= last_map_x; ++map_x) {
            uint16_t metatile = read16(memory, row + (uint32_t)map_x * 2);
            bool fogged = fog_row >= UINT32_C(0x02000000) &&
                fog_row < UINT32_C(0x02040000) &&
                memory->read8(memory->context, fog_row + (uint32_t)map_x) != 0;
            int tile_x = map_x * MAP_TILE_SIZE - state->camera_x + viewport.gba_x;
            int tile_y = map_y * MAP_TILE_SIZE - state->camera_y + viewport.gba_y;
            unsigned quadrant;
            for (quadrant = 0; quadrant < 4; ++quadrant) {
                uint16_t entry = read16(memory,
                    state->tileset_config + (uint32_t)(metatile + quadrant) * 2);
                draw_subtile(memory, state, entry, fogged,
                    tile_x + (int)(quadrant & 1) * SUBTILE_SIZE,
                    tile_y + (int)(quadrant >> 1) * SUBTILE_SIZE,
                    viewport, pixels, stride_pixels);
            }
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
