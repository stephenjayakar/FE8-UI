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
    BG_VRAM = 0x06000000,
    BG_PALETTE = 0x05000000,
    MOVE_PALETTE_BANK = 4,
    RANGE_PALETTE_BANK = 5,
    RANGE_TILE_ANIMATED = 0x280,
    RANGE_TILE_STABLE = 0x284,
    OAM = 0x07000000,
};

static const unsigned hp_bar_tiles[12] = {
    0x12, 0x14, 0x16, 0x32, 0x34, 0x36,
    0x52, 0x54, 0x56, 0x72, 0x74, 0x76,
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

static unsigned hp_bar_tile(const Fe8VisibleUnit *unit) {
    unsigned missing;
    unsigned frame;
    if (!unit || unit->max_hp == 0 || unit->current_hp == 0 ||
            unit->current_hp >= unit->max_hp)
        return 0;
    missing = unit->max_hp - unit->current_hp;
    frame = missing * 11 / unit->max_hp;
    return hp_bar_tiles[frame];
}

static Fe8HostPixel alpha_blend(Fe8HostPixel top, Fe8HostPixel bottom) {
    unsigned red = ((top & 0xFF) * 10 + (bottom & 0xFF) * 6) / 16;
    unsigned green = (((top >> 8) & 0xFF) * 10 +
        ((bottom >> 8) & 0xFF) * 6) / 16;
    unsigned blue = (((top >> 16) & 0xFF) * 10 +
        ((bottom >> 16) & 0xFF) * 6) / 16;
    if (red > 0xFF) red = 0xFF;
    if (green > 0xFF) green = 0xFF;
    if (blue > 0xFF) blue = 0xFF;
    return UINT32_C(0xFF000000) | (blue << 16) | (green << 8) | red;
}

static bool find_live_range_tiles(const Fe8MemoryView *memory,
    const Fe8Snapshot *snapshot, unsigned palette_bank, unsigned *tile_base) {
    unsigned y;
    if (snapshot->bg2_tilemap < UINT32_C(0x02000000) ||
            snapshot->bg2_tilemap > UINT32_C(0x0203F800))
        return false;
    for (y = 0; y < 31; ++y) {
        unsigned x;
        for (x = 0; x < 31; ++x) {
            uint32_t address = snapshot->bg2_tilemap + (y * 32 + x) * 2;
            uint16_t entry = read16(memory, address);
            unsigned tile = entry & 0x3FF;
            uint16_t attributes = entry & 0xFC00;
            if ((entry >> 12) != palette_bank ||
                    (tile != RANGE_TILE_ANIMATED && tile != RANGE_TILE_STABLE))
                continue;
            if (read16(memory, address + 2) == (uint16_t)(attributes | (tile + 1)) &&
                    read16(memory, address + 32 * 2) ==
                        (uint16_t)(attributes | (tile + 2)) &&
                    read16(memory, address + (32 + 1) * 2) ==
                        (uint16_t)(attributes | (tile + 3))) {
                *tile_base = tile;
                return true;
            }
        }
    }
    return false;
}

static void draw_range_tile(const Fe8MemoryView *memory,
    Fe8HostPixel *pixels, size_t stride, Fe8ExtendedViewport viewport,
    int left, int top, unsigned tile_base, unsigned palette_bank) {
    int y;
    for (y = 0; y < MAP_TILE_SIZE; ++y) {
        int x;
        int destination_y = top + y;
        if (destination_y < 0 || destination_y >= viewport.height)
            continue;
        for (x = 0; x < MAP_TILE_SIZE; ++x) {
            int destination_x = left + x;
            unsigned tile;
            unsigned source_x;
            unsigned source_y;
            uint8_t packed;
            unsigned color_index;
            uint16_t color;
            if (destination_x < 0 || destination_x >= viewport.width)
                continue;
            tile = tile_base + (unsigned)(x >> 3) + (unsigned)(y >> 3) * 2;
            source_x = (unsigned)x & 7;
            source_y = (unsigned)y & 7;
            packed = memory->read8(memory->context,
                BG_VRAM + tile * 32 + source_y * 4 + source_x / 2);
            color_index = (source_x & 1) ? packed >> 4 : packed & 0xF;
            if (color_index == 0)
                continue;
            color = read16(memory, BG_PALETTE +
                (palette_bank * 16 + color_index) * 2);
            pixels[(size_t)destination_y * stride + destination_x] =
                alpha_blend(gba_color(color),
                    pixels[(size_t)destination_y * stride + destination_x]);
        }
    }
}

unsigned fe8_render_extended_move_range(
    const Fe8MemoryView *memory, const Fe8Snapshot *snapshot,
    Fe8ExtendedViewport viewport, Fe8HostPixel *pixels, size_t stride_pixels) {
    unsigned movement_tiles = RANGE_TILE_STABLE;
    unsigned range_tiles = RANGE_TILE_STABLE;
    unsigned rendered = 0;
    uint16_t y;
    if (!memory || !memory->read8 || !snapshot || !pixels ||
            stride_pixels < (size_t)viewport.width ||
            (snapshot->game_state_bits & 1) == 0)
        return 0;
    /* BM_FLAG_0 is set and cleared with FE8's MoveLimitView process. The
     * EWRAM tilemap shadow may lag its VRAM upload by a frame, so absence of
     * a detectable quartet must not clip the full logical range map. Use the
     * stable tiles in that case and mirror an animated/live quartet when one
     * is observable. */
    (void)find_live_range_tiles(memory, snapshot,
        MOVE_PALETTE_BANK, &movement_tiles);
    (void)find_live_range_tiles(memory, snapshot,
        RANGE_PALETTE_BANK, &range_tiles);
    for (y = 0; y < snapshot->map_height; ++y) {
        uint16_t x;
        for (x = 0; x < snapshot->map_width; ++x) {
            size_t index = (size_t)y * snapshot->map_width + x;
            unsigned tile_base;
            unsigned palette_bank;
            if ((snapshot->flags & FE8_SNAPSHOT_MOVEMENT) &&
                    snapshot->movement[index] < 0x80) {
                tile_base = movement_tiles;
                palette_bank = MOVE_PALETTE_BANK;
            } else if ((snapshot->flags & FE8_SNAPSHOT_RANGE) &&
                    snapshot->range[index] != 0) {
                tile_base = range_tiles;
                palette_bank = RANGE_PALETTE_BANK;
            } else {
                continue;
            }
            draw_range_tile(memory, pixels, stride_pixels, viewport,
                x * MAP_TILE_SIZE - snapshot->camera_x + viewport.gba_x,
                y * MAP_TILE_SIZE - snapshot->camera_y + viewport.gba_y,
                tile_base, palette_bank);
            ++rendered;
        }
    }
    return rendered;
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

bool fe8_detect_native_unit_hp_bars(
    const Fe8MemoryView *memory, const Fe8Snapshot *snapshot) {
    uint16_t unit_index;
    if (!memory || !memory->read8 || !snapshot)
        return false;
    for (unit_index = 0; unit_index < snapshot->visible_unit_count; ++unit_index) {
        const Fe8VisibleUnit *unit = &snapshot->visible_units[unit_index];
        unsigned tile = hp_bar_tile(unit);
        unsigned expected_x;
        unsigned expected_y;
        unsigned entry;
        if (tile == 0)
            continue;
        /* The patch calls PutSpriteExt at tile-relative (+1, -5); its frame
         * then contributes (-1, +15), placing the 16x8 bar at (+0, +10). */
        expected_x = (unsigned)(unit->x * MAP_TILE_SIZE -
            snapshot->camera_x) & 0x1FF;
        expected_y = (unsigned)(unit->y * MAP_TILE_SIZE -
            snapshot->camera_y + 10) & 0xFF;
        for (entry = 0; entry < 128; ++entry) {
            uint32_t address = OAM + entry * 8;
            uint16_t attr0 = read16(memory, address);
            uint16_t attr1 = read16(memory, address + 2);
            uint16_t attr2 = read16(memory, address + 4);
            if ((attr0 & 0xFF) == expected_y &&
                    (attr0 & 0xC000) == 0x4000 &&
                    (attr1 & 0x1FF) == expected_x &&
                    (attr1 & 0xC000) == 0 &&
                    (attr2 & 0x3FF) == tile)
                return true;
        }
    }
    return false;
}

static void draw_unit_hp_bars(
    const Fe8MemoryView *memory, const Fe8Snapshot *snapshot,
    Fe8ExtendedViewport viewport, Fe8HostPixel *pixels, size_t stride_pixels) {
    uint16_t i;
    if ((snapshot->flags & FE8_SNAPSHOT_HP_BARS) == 0)
        return;
    for (i = 0; i < snapshot->visible_unit_count; ++i) {
        const Fe8VisibleUnit *unit = &snapshot->visible_units[i];
        unsigned tile = hp_bar_tile(unit);
        if (tile == 0)
            continue;
        draw_obj(memory, pixels, stride_pixels, viewport.width, viewport.height,
            unit->x * MAP_TILE_SIZE - snapshot->camera_x + viewport.gba_x,
            unit->y * MAP_TILE_SIZE - snapshot->camera_y + viewport.gba_y + 10,
            16, 8, tile, 0);
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

static bool draw_map_sprite(
    const Fe8MemoryView *memory, const Fe8Snapshot *snapshot,
    Fe8ExtendedViewport viewport, Fe8HostPixel *pixels, size_t stride_pixels,
    int x_display, int y_display, uint16_t oam2, uint8_t config,
    unsigned animation_frame) {
    int width;
    int height;
    int left;
    int top;
    if (config & 0x80)
        return false;
    switch (config & 0xF) {
    case 0: case 3: width = 16; height = 16; break;
    case 1: case 4: width = 16; height = 32; break;
    case 2: case 5: width = 32; height = 32; break;
    default: return false;
    }
    left = x_display - snapshot->camera_x + viewport.gba_x;
    top = y_display - snapshot->camera_y + viewport.gba_y;
    if (width == 32)
        left -= 8;
    if (height == 32)
        top -= 16;
    if (config & 0x40)
        left += (animation_frame >> 1) & 2;
    draw_obj(memory, pixels, stride_pixels, viewport.width, viewport.height,
        left, top, width, height, oam2 & 0x3FF, (oam2 >> 12) & 0xF);
    return true;
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
    if (snapshot->flags & FE8_SNAPSHOT_MAP_SPRITES) {
        for (i = 0; i < snapshot->map_sprite_count; ++i) {
            const Fe8VisibleMapSprite *sprite = &snapshot->map_sprites[i];
            rendered += draw_map_sprite(memory, snapshot, viewport, pixels,
                stride_pixels, sprite->x_display, sprite->y_display,
                sprite->oam2, sprite->config, animation_frame);
        }
    } else {
        /* Compatibility fallback for profiles that do not expose FE8's
         * complete SMS handle list. */
        for (i = 0; i < snapshot->visible_unit_count; ++i) {
            const Fe8VisibleUnit *unit = &snapshot->visible_units[i];
            uint32_t handle = unit->map_sprite_handle;
            if (!valid_handle(handle))
                continue;
            rendered += draw_map_sprite(memory, snapshot, viewport, pixels,
                stride_pixels, unit->x * MAP_TILE_SIZE,
                unit->y * MAP_TILE_SIZE, read16(memory, handle + 8),
                memory->read8(memory->context, handle + 0x0B),
                animation_frame);
        }
    }
    for (i = 0; i < snapshot->visible_unit_count; ++i) {
        const Fe8VisibleUnit *unit = &snapshot->visible_units[i];
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
    }
    draw_unit_hp_bars(memory, snapshot, viewport, pixels, stride_pixels);
    draw_cursor(pixels, stride_pixels, viewport.width, viewport.height,
        snapshot->cursor_display_x - snapshot->camera_x + viewport.gba_x,
        snapshot->cursor_display_y - snapshot->camera_y + viewport.gba_y,
        animation_frame);
    return rendered;
}
