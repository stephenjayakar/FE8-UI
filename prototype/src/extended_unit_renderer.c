#include "extended_unit_renderer.h"

enum {
    OBJ_VRAM = 0x06010000,
    OBJ_PALETTE = 0x05000200,
    OBJ_TILE_BYTES = 32,
    OBJ_TILE_ROW_STRIDE = 32,
    MAP_TILE_SIZE = 16,
    UNIT_STATE_RESCUING = 1 << 4,
    CHARACTER_ATTRIBUTE_BOSS = 1 << 15,
    BOSS_ICON_TILE = 0x10,
};

static uint16_t read16(const Fe8MemoryView *memory, uint32_t address) {
    return (uint16_t)(memory->read8(memory->context, address) |
        ((uint16_t)memory->read8(memory->context, address + 1) << 8));
}

static uint32_t gba_color(uint16_t color) {
    uint32_t red = color & 31;
    uint32_t green = (color >> 5) & 31;
    uint32_t blue = (color >> 10) & 31;
    red = (red << 3) | (red >> 2);
    green = (green << 3) | (green >> 2);
    blue = (blue << 3) | (blue >> 2);
    return UINT32_C(0xFF000000) | (blue << 16) | (green << 8) | red;
}

static bool valid_handle(uint32_t address) {
    return address >= UINT32_C(0x02000000) && address <= UINT32_C(0x0203FFF4);
}

static void draw_obj(
    const Fe8MemoryView *memory, Fe8HostPixel *pixels, size_t stride,
    int canvas_width, int canvas_height, int left, int top,
    int width, int height, unsigned tile_base, unsigned palette_bank) {
    int y;
    for (y = 0; y < height; ++y) {
        int x;
        int tile_y = y >> 3;
        int pixel_y = y & 7;
        for (x = 0; x < width; ++x) {
            int destination_x = left + x;
            int destination_y = top + y;
            unsigned tile_x;
            unsigned tile;
            uint32_t packed_address;
            uint8_t packed;
            unsigned color_index;
            uint16_t color;
            if (destination_x < 0 || destination_y < 0 ||
                    destination_x >= canvas_width || destination_y >= canvas_height)
                continue;
            tile_x = (unsigned)x >> 3;
            tile = (tile_base + (unsigned)tile_y * OBJ_TILE_ROW_STRIDE + tile_x) & 0x3FF;
            packed_address = OBJ_VRAM + tile * OBJ_TILE_BYTES +
                (unsigned)pixel_y * 4 + ((unsigned)x & 7) / 2;
            packed = memory->read8(memory->context, packed_address);
            color_index = (x & 1) ? packed >> 4 : packed & 0xF;
            if (color_index == 0)
                continue;
            color = read16(memory, OBJ_PALETTE +
                (palette_bank * 16 + color_index) * 2);
            pixels[(size_t)destination_y * stride + destination_x] = gba_color(color);
        }
    }
}

static void draw_cursor(
    Fe8HostPixel *pixels, size_t stride, int canvas_width, int canvas_height,
    int left, int top, unsigned animation_frame) {
    static const Fe8HostPixel colors[2] = {
        UINT32_C(0xFF30D8FF), UINT32_C(0xFFFFFFFF)
    };
    Fe8HostPixel color = colors[(animation_frame / 12) & 1];
    int i;
    for (i = 0; i < 6; ++i) {
        int points[8][2] = {
            {left + i, top}, {left, top + i},
            {left + 15 - i, top}, {left + 15, top + i},
            {left + i, top + 15}, {left, top + 15 - i},
            {left + 15 - i, top + 15}, {left + 15, top + 15 - i},
        };
        int point;
        for (point = 0; point < 8; ++point) {
            int x = points[point][0];
            int y = points[point][1];
            if (x >= 0 && y >= 0 && x < canvas_width && y < canvas_height)
                pixels[(size_t)y * stride + x] = color;
        }
    }
}

unsigned fe8_render_extended_units(
    const Fe8MemoryView *memory, const Fe8Snapshot *snapshot,
    Fe8ExtendedViewport viewport, Fe8HostPixel *pixels, size_t stride_pixels,
    unsigned animation_frame) {
    unsigned rendered = 0;
    uint16_t i;
    if (!memory || !memory->read8 || !snapshot || !pixels ||
            stride_pixels < (size_t)viewport.width)
        return 0;
    for (i = 0; i < snapshot->visible_unit_count; ++i) {
        const Fe8VisibleUnit *unit = &snapshot->visible_units[i];
        uint32_t handle = unit->map_sprite_handle;
        uint16_t oam2;
        uint8_t config;
        int width;
        int height;
        int left;
        int top;
        if (!valid_handle(handle))
            continue;
        oam2 = read16(memory, handle + 8);
        config = memory->read8(memory->context, handle + 0x0B);
        if (config & 0x80)
            continue;
        switch (config & 0xF) {
        case 0: case 3: width = 16; height = 16; break;
        case 1: case 4: width = 16; height = 32; break;
        case 2: case 5: width = 32; height = 32; break;
        default: continue;
        }
        left = unit->x * MAP_TILE_SIZE - snapshot->camera_x + viewport.gba_x;
        top = unit->y * MAP_TILE_SIZE - snapshot->camera_y + viewport.gba_y;
        if (width == 32)
            left -= 8;
        if (height == 32)
            top -= 16;
        if (config & 0x40)
            left += (animation_frame >> 1) & 2;
        draw_obj(memory, pixels, stride_pixels, viewport.width, viewport.height,
            left, top, width, height, oam2 & 0x3FF, (oam2 >> 12) & 0xF);
        /* FE8's PutUnitSpriteIconsOam draws the boss marker as a separate
         * blinking 8x8 OBJ at tile-relative (+9, +7), using OBJ tile 0x10
         * and palette zero. The native framebuffer supplies it inside the
         * GBA viewport; reconstruct it here for units in the extended area. */
        if (unit->faction != 0 &&
                (unit->attributes & CHARACTER_ATTRIBUTE_BOSS) != 0 &&
                (unit->state & UNIT_STATE_RESCUING) == 0 &&
                animation_frame % 32 < 20) {
            int tile_left = unit->x * MAP_TILE_SIZE - snapshot->camera_x +
                viewport.gba_x;
            int tile_top = unit->y * MAP_TILE_SIZE - snapshot->camera_y +
                viewport.gba_y;
            draw_obj(memory, pixels, stride_pixels,
                viewport.width, viewport.height,
                tile_left + 9, tile_top + 7, 8, 8,
                BOSS_ICON_TILE, 0);
        }
        ++rendered;
    }
    draw_cursor(pixels, stride_pixels, viewport.width, viewport.height,
        snapshot->cursor_display_x - snapshot->camera_x + viewport.gba_x,
        snapshot->cursor_display_y - snapshot->camera_y + viewport.gba_y,
        animation_frame);
    return rendered;
}
