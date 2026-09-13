/* Real-ROM regression for unseen mountain palettes on Archanae chapter 1.
 * No ROM, state, or graphics fixtures are distributed. Boot and camera travel
 * use ordinary keys; the only oracle for the inferred pixels is mGBA's frame.
 * Optional capture-prefix writes local PPMs and a cold-renderer checkpoint. */
#include <mgba/flags.h>
#include <mgba/core/core.h>
#include <mgba/core/interface.h>
#include <mgba/core/log.h>
#include <mgba/core/serialize.h>
#include <mgba-util/vfs.h>
#include "address_space.h"
#include "extended_map_renderer.h"
#include "fe8_profile.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { WIDTH = 34 * 16, HEIGHT = 15 * 16, PIXELS = WIDTH * HEIGHT };
static uint8_t read8(void *context, uint32_t address) {
    struct mCore *core = context;
    return core->busRead8(core, address);
}
static void map_memory(Fe8AddressSpace *space, struct mCore *core) {
    const uint32_t bases[] = {0x02000000, 0x03000000, 0x05000000, 0x06000000, 0x08000000};
    fe8_address_space_init(space, core, read8);
    for (unsigned i = 0; i < sizeof(bases) / sizeof(bases[0]); ++i) {
        size_t size = 0;
        void *data = mCoreGetMemoryBlock(core, bases[i], &size);
        if (data && size) assert(fe8_address_space_add(space, bases[i], data, size));
    }
}
static Fe8HostPixel host_pixel(mColor pixel) {
#ifdef COLOR_16_BIT
    return UINT32_C(0xFF000000) | (M_B8(pixel) << 16) | (M_G8(pixel) << 8) | M_R8(pixel);
#else
    return UINT32_C(0xFF000000) | (pixel & UINT32_C(0xFFFFFF));
#endif
}
static void capture(const char *prefix, const char *suffix,
    const Fe8HostPixel *pixels, int width, int height) {
    char path[1024];
    if (!prefix) return;
    int n = snprintf(path, sizeof(path), "%s-%s.ppm", prefix, suffix);
    assert(n > 0 && (size_t)n < sizeof(path));
    FILE *file = fopen(path, "wb"); assert(file);
    assert(fprintf(file, "P6\n%d %d\n255\n", width, height) > 0);
    for (int i = 0; i < width * height; ++i) {
        uint8_t rgb[] = {pixels[i] & 255, (pixels[i] >> 8) & 255, (pixels[i] >> 16) & 255};
        assert(fwrite(rgb, 1, sizeof(rgb), file) == sizeof(rgb));
    }
    assert(fclose(file) == 0);
}
static void advance(struct mCore *core, unsigned frames, uint32_t keys) {
    core->setKeys(core, keys);
    for (unsigned i = 0; i < frames; ++i) core->runFrame(core);
}
static uint64_t memory_hash(struct mCore *core) {
    const uint32_t bases[] = {0x02000000, 0x03000000, 0x05000000, 0x06000000};
    uint64_t hash = UINT64_C(14695981039346656037);
    for (unsigned i = 0; i < sizeof(bases) / sizeof(bases[0]); ++i) {
        size_t size = 0;
        const uint8_t *data = mCoreGetMemoryBlock(core, bases[i], &size);
        assert(data && size);
        for (size_t j = 0; j < size; ++j) hash = (hash ^ data[j]) * UINT64_C(1099511628211);
    }
    return hash;
}
int main(int argc, char **argv) {
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: %s ARCHANAE.gba [capture-prefix]\n", argv[0]);
        return EXIT_FAILURE;
    }
    const char *prefix = argc == 3 ? argv[2] : NULL;
    struct mCore *core = mCoreFind(argv[1]); assert(core && core->init(core));
    mCoreInitConfig(core, "fe8-archanae-terrain-test");
    struct mStandardLogger logger;
    mStandardLoggerInit(&logger);
    logger.d.filter->defaultLevels = mLOG_FATAL | mLOG_ERROR;
    mLogSetDefaultLogger(&logger.d);
    struct VFile *rom = VFileOpen(argv[1], O_RDONLY);
    assert(rom && core->loadROM(core, rom));
    mColor *video = calloc(240 * 160, sizeof(*video)); assert(video);
    core->setVideoBuffer(core, video, 240);
    core->reset(core);
    Fe8AddressSpace space; map_memory(&space, core);
    Fe8MemoryReader reader = {&space, fe8_address_space_read8};
    Fe8MemoryView memory = {&space, fe8_address_space_read8};
    const Fe8Profile *profile = fe8_profile_for_rom(&reader);
    assert(strcmp(profile->profile_name, "Fire Emblem: Archanae") == 0);

    /* Deterministic input sequence for the SHA-verified Archanae profile.
     * Stop A/Start at the initial roster, then let the intro finish untouched.
     * This tests a COLD renderer on a READY map, not every loading/fade frame. */
    for (unsigned frame = 1; frame <= 4440; ++frame) {
        unsigned phase = frame % 360;
        uint32_t keys = frame <= 3840 ?
            (phase < 3 ? 1 : phase >= 180 && phase < 183 ? 8 : 0) : 0;
        advance(core, 1, keys);
    }
    Fe8Snapshot *snapshot = malloc(sizeof(*snapshot)); assert(snapshot);
    assert(fe8_extract_snapshot(&reader, profile, snapshot));
    assert(snapshot->map_width == 34 && snapshot->map_height == 15);
    assert(snapshot->chapter == 1 && snapshot->phase == 0 && snapshot->input_lock == 0);
    assert(snapshot->cursor_x == 26 && snapshot->cursor_y == 6);
    assert(snapshot->camera_x == 240 && snapshot->camera_y == 0);
    printf("Archanae: chapter 1, 34x15, cursor 26,6, camera 240,0 after 4440 boot frames\n");
    if (prefix) {
        char path[1024];
        int n = snprintf(path, sizeof(path), "%s.ss", prefix);
        assert(n > 0 && (size_t)n < sizeof(path));
        struct VFile *state = VFileOpen(path, O_RDWR | O_CREAT | O_TRUNC);
        assert(state && mCoreSaveStateNamed(core, state, SAVESTATE_ALL)); state->close(state);
    }
    Fe8PaletteMapping mapping = {0};
    Fe8TerrainCache *cache = fe8_terrain_cache_create(); assert(cache);
    Fe8MapRenderState state = {
        .map_width = 34, .map_height = 15, .camera_x = 240, .camera_y = 0,
        .base_tile_rows = snapshot->base_tile_rows, .fog_rows = snapshot->fog_rows,
        .tileset_config = profile->tileset_config, .tile_graphics = 0x06008000,
        .palette = 0x05000000, .palette_mapping = &mapping, .tile_cache = cache,
    };
    Fe8ExtendedViewport viewport = {WIDTH, HEIGHT, 240, 0};
    Fe8HostPixel *terrain = malloc(PIXELS * sizeof(*terrain));
    Fe8HostPixel *early = malloc(PIXELS * sizeof(*early));
    Fe8HostPixel *native = malloc(240 * 160 * sizeof(*native));
    uint8_t *affected = calloc(PIXELS, 1); assert(terrain && early && native && affected);
    unsigned affected_tiles = 0;
    /* Identify the real map's source-bank-4 metatiles, not a painted fixture. */
    for (unsigned y = 0; y < 15; ++y) {
        uint32_t row = core->busRead32(core, snapshot->base_tile_rows + y * 4);
        for (unsigned x = 0; x < 34; ++x) {
            uint16_t metatile = core->busRead16(core, row + x * 2);
            int needs_bank_four = 0;
            for (unsigned q = 0; q < 4; ++q)
                needs_bank_four |= (core->busRead16(core,
                    profile->tileset_config + (metatile + q) * 2) >> 12) == 4;
            if (!needs_bank_four) continue;
            ++affected_tiles;
            for (unsigned py = 0; py < 16; ++py)
                memset(affected + (y * 16 + py) * WIDTH + x * 16, 1, 16);
        }
    }
    assert(affected_tiles == 24);
    for (unsigned frame = 1; frame <= 600; ++frame) {
        advance(core, 1, 0);
        Fe8LiveState live; assert(fe8_extract_live_state(&reader, profile, &live));
        assert(live.cursor_x == 26 && live.cursor_y == 6);
        assert(live.camera_x == 240 && live.camera_y == 0 && live.input_lock == 0);
        for (unsigned i = 0; i < 240 * 160; ++i) native[i] = host_pixel(video[i]);
        uint64_t before = memory_hash(core);
        fe8_learn_palette_mapping(&memory, &state, native, 240);
        assert(fe8_render_extended_terrain(&memory, &state, viewport, terrain, WIDTH));
        assert(before == memory_hash(core));
        if (frame < 2) continue;
        /* Bank 4 must remain unconfirmed: no camera visit and no promotion of
         * inferred data to confirmed evidence. Both layers are checked. */
        assert(mapping.valid_mask[0] == 0 && mapping.valid_mask[1] == 0x000F);
        unsigned missing = 0;
        for (unsigned i = 0; i < PIXELS; ++i)
            if (affected[i] && terrain[i] == UINT32_C(0xFF101418)) ++missing;
        if (missing) fprintf(stderr, "frame %u: %u / 6144 mountain pixels remain blank\n", frame, missing);
        assert(missing == 0);
        if (frame == 2) memcpy(early, terrain, PIXELS * sizeof(*terrain));
        for (unsigned i = 0; i < PIXELS; ++i)
            if (affected[i]) assert(terrain[i] == early[i]);
        if (frame == 120 || frame == 600) {
            printf("frame %u: 24 mountain tiles filled, 0 missing pixels; cursor unchanged\n", frame);
            capture(prefix, frame == 120 ? "terrain-120" : "terrain-600", terrain, WIDTH, HEIGHT);
        }
    }

    /* Only AFTER the stationary test, expose the mountains to the real PPU.
     * Move the cursor above them to avoid covering any oracle pixels. */
    advance(core, 60, 32); advance(core, 90, 0);
    advance(core, 1, 32); advance(core, 32, 64); advance(core, 90, 0);
    Fe8LiveState live; assert(fe8_extract_live_state(&reader, profile, &live));
    assert(live.camera_x == 144 && live.camera_y == 0);
    assert(live.cursor_x == 12 && live.cursor_y == 0);
    unsigned compared = 0;
    for (unsigned i = 0; i < 240 * 160; ++i) native[i] = host_pixel(video[i]);
    for (int y = 0; y < HEIGHT; ++y) for (int x = 0; x < WIDTH; ++x) {
        if (!affected[y * WIDTH + x]) continue;
        int nx = x - live.camera_x, ny = y - live.camera_y;
        assert(nx >= 0 && nx < 240 && ny >= 0 && ny < 160);
        assert(early[y * WIDTH + x] == native[ny * 240 + nx]);
        ++compared;
    }
    assert(compared == 6144);
    printf("Native PPU oracle: 6144 / 6144 formerly missing pixels match exactly (no exclusions)\n");
    capture(prefix, "native-oracle", native, 240, 160);
    state.camera_x = live.camera_x; state.camera_y = live.camera_y;
    for (unsigned i = 0; i < 2; ++i) {
        advance(core, 1, 0);
        for (unsigned j = 0; j < 240 * 160; ++j) native[j] = host_pixel(video[j]);
        fe8_learn_palette_mapping(&memory, &state, native, 240);
    }
    assert(mapping.valid_mask[0] == 0 && mapping.valid_mask[1] == 0x001F);
    assert(mapping.bank[1][4] == ((mapping.bank[1][0] + 4) & 15));
    printf("Camera visit independently confirms bank 4 -> %u; the earlier inference agrees\n", mapping.bank[1][4]);
    free(affected); free(native); free(early); free(terrain); free(snapshot);
    fe8_terrain_cache_destroy(cache);
    mCoreConfigDeinit(&core->config); core->deinit(core);
    mLogSetDefaultLogger(NULL); mStandardLoggerDeinit(&logger); free(video);
    puts("Archanae real-ROM map-loading regression passed");
    return EXIT_SUCCESS;
}
