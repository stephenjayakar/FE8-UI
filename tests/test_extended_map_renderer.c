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

/* Only source banks 0 and 1 occur in the native 240-pixel-wide view.
 * Banks 2+ occur exclusively in the extended area until the camera moves. */
static Fe8MapRenderState palette_fixture(Fe8PaletteMapping *mapping,
    Fe8TerrainCache *cache, unsigned offset, bool fogged) {
    Fe8MapRenderState state = {
        .map_width = 20, .map_height = 1,
        .base_tile_rows = 0x02000100, .fog_rows = 0x02000300,
        .tileset_config = 0x02001000, .tile_graphics = 0x06008000,
        .palette = 0x05000000,
        .normal_palette_bank_offset = (uint8_t)offset,
        .fog_palette_bank_offset = (uint8_t)offset,
        .palette_mapping = mapping, .tile_cache = cache,
    };
    unsigned i;
    memset(ewram, 0, sizeof(ewram));
    memset(palette, 0, sizeof(palette));
    memset(vram, 0, sizeof(vram));
    fe8_palette_mapping_reset(mapping);
    fe8_terrain_cache_reset(cache);
    put32(ewram, 0x100, 0x02000200);
    put32(ewram, 0x300, 0x02000400);
    memset(ewram + 0x400, fogged ? 1 : 0, 20);
    for (i = 0; i < 20; ++i)
        put16(ewram, 0x200 + i * 2, (uint16_t)((i < 7 ? 0 : i < 15 ? 1 : 2) * 4));
    for (i = 0; i < 16; ++i) {
        unsigned q;
        for (q = 0; q < 4; ++q)
            put16(ewram, 0x1000 + (i * 4 + q) * 2, (uint16_t)((i << 12) | 1));
        /* Distinct, nonzero colors make the learner's winning bank unique. */
        put16(palette, (i * 16 + 1) * 2, (uint16_t)(i + 1));
    }
    memset(vram + 0x8020, 0x11, 32);
    return state;
}

static Fe8HostPixel fixture_color(unsigned bank) {
    unsigned red = (bank & 15) + 1;
    return UINT32_C(0xFF000000) | (red << 3) | (red >> 2);
}

static void fixture_native_frame(const Fe8MapRenderState *state,
    const uint8_t destinations[16], Fe8HostPixel frame[240 * 160]) {
    unsigned x, y;
    for (y = 0; y < 160; ++y) {
        for (x = 0; x < 240; ++x) {
            unsigned map_x = ((unsigned)state->camera_x + x) / 16;
            Fe8HostPixel color = UINT32_C(0xFF101418);
            if (y < 16 && map_x < state->map_width) {
                uint16_t metatile = (uint16_t)(ewram[0x200 + map_x * 2] |
                    ((uint16_t)ewram[0x201 + map_x * 2] << 8));
                color = fixture_color(destinations[metatile / 4]);
            }
            frame[y * 240 + x] = color;
        }
    }
}

static void confirm_fixture_bank(Fe8PaletteMapping *mapping,
    unsigned layer, unsigned source, unsigned destination) {
    mapping->valid_mask[layer] |= (uint16_t)(1u << source);
    mapping->bank[layer][source] = (uint8_t)destination;
}

static void test_unseen_banks_without_camera_motion(void) {
    Fe8MemoryView memory = {NULL, test_read8};
    Fe8ExtendedViewport viewport = {320, 16, 0, 0};
    static Fe8HostPixel native_frame[240 * 160];
    Fe8HostPixel output[320 * 16];
    unsigned layer, offset, use_cache;
    for (layer = 0; layer < 2; ++layer) {
        for (offset = 0; offset < 16; ++offset) {
            for (use_cache = 0; use_cache < 2; ++use_cache) {
                Fe8PaletteMapping mapping, before_render;
                Fe8TerrainCache *cache = use_cache ? fe8_terrain_cache_create() : NULL;
                Fe8MapRenderState state;
                uint8_t destinations[16];
                unsigned x, y, i;
                assert(!use_cache || cache);
                state = palette_fixture(&mapping, cache, offset, layer != 0);
                for (i = 0; i < 16; ++i)
                    destinations[i] = (uint8_t)((i + offset) & 15);
                fixture_native_frame(&state, destinations, native_frame);
                assert(fe8_learn_palette_mapping(&memory, &state, native_frame, 240) == 0);
                assert(fe8_render_extended_terrain(&memory, &state, viewport, output, 320));
                assert(output[240] == UINT32_C(0xFF101418));
                assert(fe8_learn_palette_mapping(&memory, &state, native_frame, 240) == 2);
                assert(mapping.valid_mask[layer] == 3);
                assert(mapping.valid_mask[1 - layer] == 0);
                before_render = mapping;
                assert(fe8_render_extended_terrain(&memory, &state, viewport, output, 320));
                /* Regression: main left this unseen bank dark indefinitely. */
                assert(output[240] == fixture_color(destinations[2]));
                for (y = 0; y < 16; ++y) {
                    for (x = 0; x < 320; ++x) {
                        unsigned source = x < 7 * 16 ? 0 : x < 240 ? 1 : 2;
                        assert(output[y * 320 + x] == fixture_color(destinations[source]));
                    }
                }
                assert(state.camera_x == 0 && state.camera_y == 0);
                assert(memcmp(&mapping, &before_render, sizeof(mapping)) == 0);
                assert(fe8_learn_palette_mapping(&memory, &state, native_frame, 240) == 0);
                assert(mapping.valid_mask[layer] == 3); /* inferred is not learned */
                fe8_terrain_cache_destroy(cache);
            }
        }
    }
    puts("stationary palette regression: 64 offset/fog/cache combinations passed");
}

static void test_palette_evidence_and_cache(void) {
    Fe8MemoryView memory = {NULL, test_read8};
    Fe8ExtendedViewport viewport = {320, 16, 0, 0};
    Fe8HostPixel output[320 * 16];
    Fe8PaletteMapping mapping;
    Fe8TerrainCache *cache = fe8_terrain_cache_create();
    Fe8MapRenderState state;
    assert(cache);
    state = palette_fixture(&mapping, cache, 11, false);

    /* A default offset or a single observed bank is insufficient evidence. */
    assert(fe8_render_extended_terrain(&memory, &state, viewport, output, 320));
    assert(output[240] == UINT32_C(0xFF101418));
    confirm_fixture_bank(&mapping, 0, 0, 11);
    assert(fe8_render_extended_terrain(&memory, &state, viewport, output, 320));
    assert(output[240] == UINT32_C(0xFF101418));
    confirm_fixture_bank(&mapping, 0, 1, 12);
    assert(fe8_render_extended_terrain(&memory, &state, viewport, output, 320));
    assert(output[240] == fixture_color(13));

    /* Palette animation remains live for provisionally rendered terrain. */
    put16(palette, (13 * 16 + 1) * 2, 0x7C00);
    assert(fe8_render_extended_terrain(&memory, &state, viewport, output, 320));
    assert(output[240] == UINT32_C(0xFFFF0000));
    put16(palette, (13 * 16 + 1) * 2, 14);

    /* A mixed metatile is not cacheable until ALL four banks are confirmed. */
    put16(ewram, 0x1000 + (2 * 4) * 2, 1); /* first quadrant uses observed bank 0 */
    assert(fe8_render_extended_terrain(&memory, &state, viewport, output, 320));
    assert(output[240] == fixture_color(11));
    assert(output[248] == fixture_color(13));
    fe8_palette_mapping_reset(&mapping);
    assert(fe8_render_extended_terrain(&memory, &state, viewport, output, 320));
    assert(output[0] == fixture_color(11)); /* actually observed tiles survive */
    assert(output[240] == UINT32_C(0xFF101418)); /* inferred pixels never cached */
    fe8_terrain_cache_reset(cache);
    assert(fe8_render_extended_terrain(&memory, &state, viewport, output, 320));
    assert(output[0] == UINT32_C(0xFF101418));

    /* Any disagreement disables inference, even with an agreeing majority. */
    confirm_fixture_bank(&mapping, 0, 0, 11);
    confirm_fixture_bank(&mapping, 0, 1, 12);
    confirm_fixture_bank(&mapping, 0, 3, 9);
    assert(fe8_render_extended_terrain(&memory, &state, viewport, output, 320));
    assert(output[240] == UINT32_C(0xFF101418));

    /* Normal evidence is not fog evidence; each layer has its own offset. */
    memset(ewram + 0x400, 1, 20);
    assert(fe8_render_extended_terrain(&memory, &state, viewport, output, 320));
    assert(output[0] == UINT32_C(0xFF101418));
    confirm_fixture_bank(&mapping, 1, 0, 6);
    confirm_fixture_bank(&mapping, 1, 1, 7);
    assert(fe8_render_extended_terrain(&memory, &state, viewport, output, 320));
    assert(output[240] == fixture_color(6));
    assert(output[248] == fixture_color(8));
    fe8_terrain_cache_destroy(cache);
    puts("palette evidence, animation, mixed-tile cache, reset and fog tests passed");
}

static void test_nonuniform_mapping_correction(void) {
    Fe8MemoryView memory = {NULL, test_read8};
    Fe8ExtendedViewport viewport = {320, 16, 0, 0};
    static Fe8HostPixel native_frame[240 * 160];
    Fe8HostPixel output[320 * 16];
    Fe8PaletteMapping mapping;
    Fe8TerrainCache *cache = fe8_terrain_cache_create();
    Fe8MapRenderState state;
    uint8_t destinations[16];
    unsigned i;
    assert(cache);
    state = palette_fixture(&mapping, cache, 11, false);
    put16(ewram, 0x200 + 19 * 2, 3 * 4); /* another unseen bank */
    for (i = 0; i < 16; ++i)
        destinations[i] = (uint8_t)((i + 11) & 15);
    destinations[2] = 5; /* custom, nonuniform remap not yet on screen */
    fixture_native_frame(&state, destinations, native_frame);
    assert(fe8_learn_palette_mapping(&memory, &state, native_frame, 240) == 0);
    assert(fe8_learn_palette_mapping(&memory, &state, native_frame, 240) == 2);
    assert(fe8_render_extended_terrain(&memory, &state, viewport, output, 320));
    assert(output[240] == fixture_color(13)); /* provisional standard offset */
    assert(output[304] == fixture_color(14));

    /* Expose bank 2 to the native view. It must still learn independently,
     * overwrite the provisional choice, and withdraw inference for bank 3. */
    state.camera_x = 16;
    fixture_native_frame(&state, destinations, native_frame);
    assert(fe8_learn_palette_mapping(&memory, &state, native_frame, 240) == 0);
    assert(fe8_learn_palette_mapping(&memory, &state, native_frame, 240) == 1);
    assert(mapping.valid_mask[0] == 7 && mapping.bank[0][2] == 5);
    state.camera_x = 0;
    assert(fe8_render_extended_terrain(&memory, &state, viewport, output, 320));
    assert(output[240] == fixture_color(5));
    assert(output[304] == UINT32_C(0xFF101418)); /* no poisoned cache */

    /* Previously observed custom tiles take precedence over new inference. */
    fe8_palette_mapping_reset(&mapping);
    confirm_fixture_bank(&mapping, 0, 0, 11);
    confirm_fixture_bank(&mapping, 0, 1, 12);
    assert(fe8_render_extended_terrain(&memory, &state, viewport, output, 320));
    assert(output[240] == fixture_color(5)); /* keep observed bank 2, not 13 */
    assert(output[304] == fixture_color(14));
    fe8_terrain_cache_destroy(cache);
    puts("nonuniform palette relearning and validated-cache precedence tests passed");
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
    static Fe8HostPixel native_frame[240 * 160];
    Fe8PaletteMapping mapping = {0};
    Fe8TerrainCache *cache;

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

    /* Infer a source-bank mapping independently, requiring two frames. */
    for (index = 0; index < 8; ++index)
        put16(ewram, 0x1000 + index * 2, UINT16_C(0x2001));
    put16(palette, (7 * 16 + 1) * 2, 0x03E0);
    for (index = 0; index < 240 * 160; ++index)
        native_frame[index] = UINT32_C(0xFF101418);
    for (map_y = 0; map_y < 16; ++map_y)
        for (map_x = 0; map_x < 32; ++map_x)
            native_frame[map_y * 240 + map_x] = UINT32_C(0xFF00FF00);
    state.palette_mapping = &mapping;
    cache = fe8_terrain_cache_create();
    assert(cache);
    state.tile_cache = cache;
    assert(fe8_learn_palette_mapping(&memory, &state, native_frame, 240) == 0);
    assert(fe8_learn_palette_mapping(&memory, &state, native_frame, 240) == 1);
    assert((mapping.valid_mask[0] & (1u << 2)) != 0);
    assert(mapping.bank[0][2] == 7);

    assert(fe8_render_extended_terrain(&memory, &state, viewport, output, 32));
    assert(output[0] == UINT32_C(0xFF00FF00));
    fe8_palette_mapping_reset(&mapping);
    assert(fe8_render_extended_terrain(&memory, &state, viewport, output, 32));
    assert(output[0] == UINT32_C(0xFF00FF00)); /* last validated tile */
    fe8_terrain_cache_reset(cache);
    assert(fe8_render_extended_terrain(&memory, &state, viewport, output, 32));
    assert(output[0] == UINT32_C(0xFF101418)); /* neutral, never guessed */
    fe8_terrain_cache_destroy(cache);
    test_unseen_banks_without_camera_motion();
    test_palette_evidence_and_cache();
    test_nonuniform_mapping_correction();
    puts("extended renderer tests passed");
    return 0;
}
