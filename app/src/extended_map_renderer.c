#include "extended_map_renderer.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum {
    GBA_WIDTH = 240,
    GBA_HEIGHT = 160,
    MAP_TILE_SIZE = 16,
    SUBTILE_SIZE = 8,
    GBA_TILE_BYTES = 32,
};

struct Fe8TerrainCache {
    uint8_t valid[64 * 64];
    uint16_t metatile[64 * 64];
    uint8_t fogged[64 * 64];
    Fe8HostPixel pixels[64 * 64][MAP_TILE_SIZE * MAP_TILE_SIZE];
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

void fe8_palette_mapping_reset(Fe8PaletteMapping *mapping) {
    if (mapping)
        memset(mapping, 0, sizeof(*mapping));
}

Fe8TerrainCache *fe8_terrain_cache_create(void) {
    return calloc(1, sizeof(Fe8TerrainCache));
}

void fe8_terrain_cache_reset(Fe8TerrainCache *cache) {
    if (cache) {
        memset(cache->valid, 0, sizeof(cache->valid));
    }
}

void fe8_terrain_cache_destroy(Fe8TerrainCache *cache) {
    free(cache);
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

static bool mapped_bank(const Fe8MapRenderState *state, bool fogged,
    unsigned source_bank, unsigned *destination_bank) {
    unsigned layer = fogged ? 1 : 0;
    if (!state->palette_mapping) {
        *destination_bank = (source_bank + (fogged ?
            state->fog_palette_bank_offset : state->normal_palette_bank_offset)) & 0xF;
        return true;
    }
    if (!(state->palette_mapping->valid_mask[layer] & (UINT16_C(1) << source_bank)))
        return false;
    *destination_bank = state->palette_mapping->bank[layer][source_bank];
    return true;
}

static void draw_subtile_to_tile(const Fe8MemoryView *memory,
    const Fe8MapRenderState *state, uint16_t entry, bool fogged,
    int destination_x, int destination_y, Fe8HostPixel *pixels) {
    unsigned tile_number = entry & 0x03FF;
    unsigned palette_bank = 0;
    unsigned output_y;
    (void)mapped_bank(state, fogged, (entry >> 12) & 0xF, &palette_bank);
    for (output_y = 0; output_y < SUBTILE_SIZE; ++output_y) {
        unsigned source_y = (entry & 0x0800) ? 7 - output_y : output_y;
        unsigned output_x;
        for (output_x = 0; output_x < SUBTILE_SIZE; ++output_x) {
            unsigned source_x = (entry & 0x0400) ? 7 - output_x : output_x;
            uint32_t packed_address;
            uint8_t packed;
            unsigned color_index;
            uint16_t color;
            packed_address = state->tile_graphics + tile_number * GBA_TILE_BYTES +
                source_y * 4 + source_x / 2;
            packed = memory->read8(memory->context, packed_address);
            color_index = (source_x & 1) ? packed >> 4 : packed & 0xF;
            color = read16(memory,
                state->palette + (palette_bank * 16 + color_index) * 2);
            pixels[(size_t)(destination_y + (int)output_y) * MAP_TILE_SIZE +
                destination_x + (int)output_x] = gba_color(color);
        }
    }
}

static void blit_tile(const Fe8HostPixel *tile, int destination_x, int destination_y,
    Fe8ExtendedViewport viewport, Fe8HostPixel *pixels, size_t stride) {
    int y;
    for (y = 0; y < MAP_TILE_SIZE; ++y) {
        int canvas_y = destination_y + y;
        int x;
        if (canvas_y < 0 || canvas_y >= viewport.height)
            continue;
        for (x = 0; x < MAP_TILE_SIZE; ++x) {
            int canvas_x = destination_x + x;
            if (canvas_x >= 0 && canvas_x < viewport.width)
                pixels[(size_t)canvas_y * stride + canvas_x] =
                    tile[y * MAP_TILE_SIZE + x];
        }
    }
}

static bool tile_banks_resolved(const Fe8MapRenderState *state,
    uint16_t metatile, const uint16_t entries[4], bool fogged) {
    unsigned quadrant;
    if (!state->palette_mapping)
        return true;
    (void)metatile;
    for (quadrant = 0; quadrant < 4; ++quadrant) {
        unsigned ignored;
        if (!mapped_bank(state, fogged, (entries[quadrant] >> 12) & 0xF, &ignored))
            return false;
    }
    return true;
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
            uint16_t entries[4];
            bool fogged = fog_row >= UINT32_C(0x02000000) &&
                fog_row < UINT32_C(0x02040000) &&
                memory->read8(memory->context, fog_row + (uint32_t)map_x) != 0;
            int tile_x = map_x * MAP_TILE_SIZE - state->camera_x + viewport.gba_x;
            int tile_y = map_y * MAP_TILE_SIZE - state->camera_y + viewport.gba_y;
            Fe8HostPixel local_tile[MAP_TILE_SIZE * MAP_TILE_SIZE];
            Fe8HostPixel *tile = local_tile;
            size_t cache_index = (size_t)map_y * state->map_width + map_x;
            unsigned quadrant;
            for (quadrant = 0; quadrant < 4; ++quadrant)
                entries[quadrant] = read16(memory,
                    state->tileset_config + (uint32_t)(metatile + quadrant) * 2);
            if (tile_banks_resolved(state, metatile, entries, fogged)) {
                if (state->tile_cache)
                    tile = state->tile_cache->pixels[cache_index];
                for (quadrant = 0; quadrant < 4; ++quadrant)
                    draw_subtile_to_tile(memory, state, entries[quadrant], fogged,
                        (int)(quadrant & 1) * SUBTILE_SIZE,
                        (int)(quadrant >> 1) * SUBTILE_SIZE, tile);
                if (state->tile_cache) {
                    state->tile_cache->valid[cache_index] = 1;
                    state->tile_cache->metatile[cache_index] = metatile;
                    state->tile_cache->fogged[cache_index] = fogged;
                }
            } else if (state->tile_cache && state->tile_cache->valid[cache_index] &&
                    state->tile_cache->metatile[cache_index] == metatile &&
                    state->tile_cache->fogged[cache_index] == fogged) {
                tile = state->tile_cache->pixels[cache_index];
            } else {
                for (quadrant = 0; quadrant < MAP_TILE_SIZE * MAP_TILE_SIZE; ++quadrant)
                    local_tile[quadrant] = UINT32_C(0xFF101418);
            }
            blit_tile(tile, tile_x, tile_y, viewport, pixels, stride_pixels);
        }
    }
    return true;
}

static bool terrain_sample(const Fe8MemoryView *memory,
    const Fe8MapRenderState *state, int world_x, int world_y,
    unsigned *layer, unsigned *source_bank, unsigned *color_index,
    uint16_t *sample_metatile) {
    int map_x = floor_div(world_x, MAP_TILE_SIZE);
    int map_y = floor_div(world_y, MAP_TILE_SIZE);
    int tile_x;
    int tile_y;
    uint32_t row;
    uint32_t fog_row = 0;
    uint16_t metatile;
    unsigned quadrant;
    uint16_t entry;
    unsigned source_x;
    unsigned source_y;
    uint8_t packed;
    if (map_x < 0 || map_y < 0 || map_x >= state->map_width || map_y >= state->map_height)
        return false;
    row = read32(memory, state->base_tile_rows + (uint32_t)map_y * 4);
    if (row < UINT32_C(0x02000000) || row >= UINT32_C(0x02040000))
        return false;
    metatile = read16(memory, row + (uint32_t)map_x * 2);
    if (sample_metatile)
        *sample_metatile = metatile;
    tile_x = world_x - map_x * MAP_TILE_SIZE;
    tile_y = world_y - map_y * MAP_TILE_SIZE;
    quadrant = (unsigned)(tile_x / SUBTILE_SIZE) +
        (unsigned)(tile_y / SUBTILE_SIZE) * 2;
    entry = read16(memory, state->tileset_config + (uint32_t)(metatile + quadrant) * 2);
    source_x = (unsigned)tile_x & 7;
    source_y = (unsigned)tile_y & 7;
    if (entry & 0x0400) source_x = 7 - source_x;
    if (entry & 0x0800) source_y = 7 - source_y;
    packed = memory->read8(memory->context, state->tile_graphics +
        (entry & 0x03FF) * GBA_TILE_BYTES + source_y * 4 + source_x / 2);
    *color_index = (source_x & 1) ? packed >> 4 : packed & 0xF;
    *source_bank = (entry >> 12) & 0xF;
    if (state->fog_rows)
        fog_row = read32(memory, state->fog_rows + (uint32_t)map_y * 4);
    *layer = fog_row >= UINT32_C(0x02000000) &&
        fog_row < UINT32_C(0x02040000) &&
        memory->read8(memory->context, fog_row + (uint32_t)map_x) != 0;
    return true;
}

unsigned fe8_learn_palette_mapping(const Fe8MemoryView *memory,
    const Fe8MapRenderState *state, const Fe8HostPixel *native_frame,
    size_t frame_stride) {
    unsigned samples[2][16] = {{0}};
    unsigned scores[2][16][16] = {{{0}}};
    unsigned learned = 0;
    int y;
    if (!memory || !state || !state->palette_mapping || !native_frame)
        return 0;
    for (y = 0; y < GBA_HEIGHT; y += 2) {
        int x;
        for (x = 0; x < GBA_WIDTH; x += 2) {
            unsigned layer;
            unsigned source_bank;
            unsigned color_index;
            uint16_t metatile;
            unsigned candidate;
            Fe8HostPixel observed = native_frame[(size_t)y * frame_stride + x];
            if (!terrain_sample(memory, state, state->camera_x + x,
                    state->camera_y + y, &layer, &source_bank, &color_index,
                    &metatile))
                continue;
            if (color_index == 0 ||
                    (state->palette_mapping->valid_mask[layer] &
                        (UINT16_C(1) << source_bank)))
                continue;
            ++samples[layer][source_bank];
            for (candidate = 0; candidate < 16; ++candidate) {
                uint16_t color = read16(memory, state->palette +
                    (candidate * 16 + color_index) * 2);
                if (gba_color(color) == observed)
                    ++scores[layer][source_bank][candidate];
            }
        }
    }
    {
        unsigned layer;
        for (layer = 0; layer < 2; ++layer) {
            unsigned source;
            for (source = 0; source < 16; ++source) {
                unsigned best = 0;
                unsigned best_score = 0;
                unsigned second_score = 0;
                unsigned candidate;
                unsigned count = samples[layer][source];
                if (state->palette_mapping->valid_mask[layer] &
                        (UINT16_C(1) << source))
                    continue;
                for (candidate = 0; candidate < 16; ++candidate) {
                    unsigned score = scores[layer][source][candidate];
                    if (score > best_score) {
                        second_score = best_score;
                        best_score = score;
                        best = candidate;
                    } else if (score > second_score) {
                        second_score = score;
                    }
                }
                if (count < 24 || best_score * 100 < count * 35 ||
                        (best_score - second_score) * 100 < count * 10) {
                    state->palette_mapping->confirmation_frames[layer][source] = 0;
                    continue;
                }
                if (state->palette_mapping->pending_bank[layer][source] == best)
                    ++state->palette_mapping->confirmation_frames[layer][source];
                else {
                    state->palette_mapping->pending_bank[layer][source] = (uint8_t)best;
                    state->palette_mapping->confirmation_frames[layer][source] = 1;
                }
                if (state->palette_mapping->confirmation_frames[layer][source] >= 2) {
                    state->palette_mapping->bank[layer][source] = (uint8_t)best;
                    state->palette_mapping->valid_mask[layer] |= UINT16_C(1) << source;
                    ++learned;
                }
            }
        }
    }
    return learned;
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
