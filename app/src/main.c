#include <SDL.h>

#include <mgba/flags.h>
#include <mgba/core/core.h>
#include <mgba/core/interface.h>
#include <mgba/core/log.h>
#include <mgba/core/serialize.h>
#include <mgba-util/image.h>
#include <mgba-util/vfs.h>

#include "address_space.h"
#include "extended_map_renderer.h"
#include "extended_presentation.h"
#include "extended_unit_renderer.h"
#include "fe8_profile.h"
#include "fe8_catalog.h"
#include "frame_alignment.h"
#include "frame_scheduler.h"
#include "host_audio.h"
#include "host_cursor.h"
#include "host_settings.h"
#include "host_video.h"
#include "macos_library.h"
#include "macos_settings.h"
#include "mouse_controller.h"
#include "prebattle_inventory.h"
#include "prebattle_inventory_ui.h"
#include "inventory_desktop.h"
#include "viewport_controller.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

enum {
    GBA_WIDTH = 240,
    GBA_HEIGHT = 160,
    CANVAS_WIDTH = 480,
    CANVAS_HEIGHT = 320,
    GBA_X = (CANVAS_WIDTH - GBA_WIDTH) / 2,
    GBA_Y = (CANVAS_HEIGHT - GBA_HEIGHT) / 2,
};

struct fe8_options {
    const char *rom_path;
    const char *state_path;
    const char *save_path;
    const char *capture_path;
    const char *terrain_capture_path;
    const char *state_out_path;
    const char *quick_state_path;
    int extensions;
    unsigned capture_after;
    int auto_continue;
    int seek_large_map;
    int realtime;
    int perf_stats;
    int mute;
    int open_inventory;
};

typedef struct Fe8PerfStats {
    uint64_t started;
    uint64_t emulation;
    uint64_t snapshot;
    uint64_t terrain;
    uint64_t alignment;
    uint64_t units;
    uint64_t presentation;
    uint64_t emulated_frames;
    uint64_t presented_frames;
} Fe8PerfStats;

struct pan_controller {
    int x;
    int y;
    int dragging;
    int moved;
    int start_canvas_x;
    int start_canvas_y;
    int start_pan_x;
    int start_pan_y;
};

static unsigned state_reload_generation;

static void usage(const char *program) {
    fprintf(stderr,
        "Usage: %s --rom GAME.gba [--state STATE.ss] [--save GAME.sav]\n"
        "       [--capture OUTPUT.bmp] [--capture-terrain OUTPUT.bmp]\n"
        "       [--capture-after FRAMES]\n"
        "       [--auto-continue] [--seek-large-map]\n"
        "       [--state-out MAP_STATE.ss] [--quick-state QUICK_STATE.ss]\n"
        "       [--realtime] [--perf-stats] [--mute] [--inventory]\n"
        "       [--no-extensions]\n", program);
}

static int parse_options(int argc, char **argv, struct fe8_options *options) {
    int i;
    memset(options, 0, sizeof(*options));
    options->extensions = 1;
    options->capture_after = 1;
    for (i = 1; i < argc; ++i) {
        const char **destination = NULL;
        if (strcmp(argv[i], "--rom") == 0)
            destination = &options->rom_path;
        else if (strcmp(argv[i], "--state") == 0)
            destination = &options->state_path;
        else if (strcmp(argv[i], "--save") == 0)
            destination = &options->save_path;
        else if (strcmp(argv[i], "--capture") == 0)
            destination = &options->capture_path;
        else if (strcmp(argv[i], "--capture-terrain") == 0)
            destination = &options->terrain_capture_path;
        else if (strcmp(argv[i], "--state-out") == 0)
            destination = &options->state_out_path;
        else if (strcmp(argv[i], "--quick-state") == 0)
            destination = &options->quick_state_path;
        else if (strcmp(argv[i], "--capture-after") == 0) {
            char *end;
            unsigned long frames;
            if (++i >= argc)
                return 0;
            frames = strtoul(argv[i], &end, 10);
            if (!*argv[i] || *end || frames > 1000000)
                return 0;
            options->capture_after = (unsigned)frames;
            continue;
        } else if (strcmp(argv[i], "--auto-continue") == 0) {
            options->auto_continue = 1;
            continue;
        } else if (strcmp(argv[i], "--seek-large-map") == 0) {
            options->auto_continue = 1;
            options->seek_large_map = 1;
            continue;
        } else if (strcmp(argv[i], "--realtime") == 0) {
            options->realtime = 1;
            continue;
        } else if (strcmp(argv[i], "--perf-stats") == 0) {
            options->perf_stats = 1;
            continue;
        } else if (strcmp(argv[i], "--mute") == 0) {
            options->mute = 1;
            continue;
        } else if (strcmp(argv[i], "--inventory") == 0) {
            options->open_inventory = 1;
            continue;
        }
        else if (strcmp(argv[i], "--no-extensions") == 0) {
            options->extensions = 0;
            continue;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            return 0;
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            return 0;
        }
        if (++i >= argc)
            return 0;
        *destination = argv[i];
    }
    return options->rom_path != NULL;
}

static void update_keyboard(
    uint32_t *keys, const Fe8HostSettings *settings, const SDL_Event *event) {
    uint32_t bit;
    if ((event->type != SDL_KEYDOWN && event->type != SDL_KEYUP) || event->key.repeat)
        return;
    bit = fe8_host_key_for_scancode(settings, event->key.keysym.scancode);
    if (!bit)
        return;
    if (event->type == SDL_KEYDOWN)
        *keys |= bit;
    else
        *keys &= ~bit;
}

static void update_hotkeys(
    uint32_t *hotkeys, const Fe8HostSettings *settings, const SDL_Event *event) {
    uint32_t bits;
    if (event->type != SDL_KEYDOWN && event->type != SDL_KEYUP)
        return;
    bits = fe8_host_hotkey_for_scancode(settings, event->key.keysym.scancode);
    if (event->type == SDL_KEYDOWN)
        *hotkeys |= bits;
    else
        *hotkeys &= ~bits;
}

static uint8_t core_read8(void *context, uint32_t address) {
    struct mCore *core = context;
    return core->busRead8(core, address);
}

static void core_write8(void *context, uint32_t address, uint8_t value) {
    struct mCore *core = context;
    core->busWrite8(core, address, value);
}

static void map_core_memory(Fe8AddressSpace *space, struct mCore *core) {
    static const uint32_t bases[] = {
        UINT32_C(0x02000000), UINT32_C(0x03000000), UINT32_C(0x05000000),
        UINT32_C(0x06000000), UINT32_C(0x08000000),
    };
    size_t i;
    fe8_address_space_init(space, core, core_read8);
    for (i = 0; i < sizeof(bases) / sizeof(bases[0]); ++i) {
        size_t size = 0;
        void *data = mCoreGetMemoryBlock(core, bases[i], &size);
        if (data && size)
            fe8_address_space_add(space, bases[i], data, size);
    }
}

static double ticks_ms(uint64_t ticks, uint64_t frequency) {
    return frequency ? (double)ticks * 1000.0 / frequency : 0.0;
}

static void print_perf_stats(const Fe8PerfStats *stats, uint64_t frequency) {
    double seconds = (double)(SDL_GetPerformanceCounter() - stats->started) / frequency;
    fprintf(stderr,
        "Performance: emulation=%.2fms snapshot=%.2fms terrain=%.2fms "
        "alignment=%.2fms units=%.2fms upload/swap=%.2fms "
        "effective=%.2ffps presentation=%.2ffps\n",
        ticks_ms(stats->emulation, frequency), ticks_ms(stats->snapshot, frequency),
        ticks_ms(stats->terrain, frequency), ticks_ms(stats->alignment, frequency),
        ticks_ms(stats->units, frequency), ticks_ms(stats->presentation, frequency),
        seconds > 0 ? stats->emulated_frames / seconds : 0.0,
        seconds > 0 ? stats->presented_frames / seconds : 0.0);
}

static void composite_framebuffer(
    const Fe8HostPixel *source, size_t source_stride, Fe8HostPixel *canvas,
    int canvas_width, int canvas_height, int destination_x, int destination_y) {
    unsigned y;
    for (y = 0; y < GBA_HEIGHT; ++y) {
        unsigned x;
        const Fe8HostPixel *source_row = source + y * source_stride;
        for (x = 0; x < GBA_WIDTH; ++x) {
            int canvas_x = destination_x + (int)x;
            int canvas_y = destination_y + (int)y;
            if (canvas_x >= 0 && canvas_y >= 0 &&
                    canvas_x < canvas_width && canvas_y < canvas_height)
                canvas[(size_t)canvas_y * canvas_width + canvas_x] = source_row[x];
        }
    }
}

static Fe8HostPixel host_pixel_from_mcolor(mColor pixel) {
#ifdef COLOR_16_BIT
    uint32_t red = M_R8(pixel);
    uint32_t green = M_G8(pixel);
    uint32_t blue = M_B8(pixel);
#else
    uint32_t red = pixel & 0xFF;
    uint32_t green = (pixel >> 8) & 0xFF;
    uint32_t blue = (pixel >> 16) & 0xFF;
#endif
    return UINT32_C(0xFF000000) | (blue << 16) | (green << 8) | red;
}

static void convert_framebuffer(
    const mColor *source, size_t source_stride, Fe8HostPixel *destination) {
    unsigned y;
    for (y = 0; y < GBA_HEIGHT; ++y) {
        unsigned x;
        for (x = 0; x < GBA_WIDTH; ++x)
            destination[(size_t)y * GBA_WIDTH + x] =
                host_pixel_from_mcolor(source[(size_t)y * source_stride + x]);
    }
}

static int snapshot_path_mode(const Fe8Snapshot *snapshot) {
    return snapshot && snapshot->active_unit_address != 0 &&
        (snapshot->game_state_bits & (1u << 1)) != 0;
}

static int snapshot_cursor_controls_camera(const Fe8Snapshot *snapshot) {
    return snapshot && snapshot->input_lock == 0 && snapshot->phase == 0 &&
        !snapshot->combat_panel_active;
}

static void set_mouse_map_target(Fe8MouseController *mouse,
    const Fe8Snapshot *snapshot, int x, int y, int confirm) {
    (void)snapshot;
    fe8_mouse_set_target(mouse, x, y, confirm);
}

static void pace_frame(uint64_t *deadline, uint64_t period, uint64_t frequency) {
    uint64_t now;
    *deadline += period;
    now = SDL_GetPerformanceCounter();
    if (*deadline > now) {
        uint64_t remaining = *deadline - now;
        uint32_t delay_ms = (uint32_t)(remaining * 1000 / frequency);
        if (delay_ms > 1)
            SDL_Delay(delay_ms - 1);
        while (SDL_GetPerformanceCounter() < *deadline)
            SDL_Delay(0);
    } else if (now - *deadline > period * 4) {
        *deadline = now;
    }
}

static void set_speed_up_mode(int requested, int *active,
    Fe8HostVideo *video, Fe8HostAudio *audio, int audio_initialized,
    const Fe8HostSettings *settings, uint64_t *frame_deadline) {
    if (requested == *active)
        return;
    *active = requested;
    if (!fe8_host_video_set_vsync(video,
            settings->vsync_enabled && !*active))
        fprintf(stderr, "Unable to change VSync for speed-up: %s\n", SDL_GetError());
    if (audio_initialized)
        fe8_host_audio_set_enabled(audio, settings->audio_enabled && !*active);
    *frame_deadline = SDL_GetPerformanceCounter();
    fprintf(stderr, "Speed Up: %s (%s)\n",
        *active ? "on" : "off",
        fe8_host_speedup_name(settings->speedup_rate));
}

static int load_state(struct mCore *core, const char *path) {
    struct VFile *state = VFileOpen(path, O_RDONLY);
    int success;
    if (!state) {
        fprintf(stderr, "Unable to open state '%s': %s\n", path, strerror(errno));
        return 0;
    }
    success = mCoreLoadStateNamed(core, state, SAVESTATE_ALL);
    state->close(state);
    if (!success) {
        FILE *png = fopen(path, "rb");
        static const uint8_t signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
        uint8_t header[8];
        success = 0;
        if (png && fread(header, 1, sizeof(header), png) == sizeof(header) &&
                memcmp(header, signature, sizeof(signature)) == 0) {
            while (fread(header, 1, sizeof(header), png) == sizeof(header)) {
                uint32_t length = ((uint32_t)header[0] << 24) |
                    ((uint32_t)header[1] << 16) | ((uint32_t)header[2] << 8) | header[3];
                if (length > UINT32_C(16) * 1024 * 1024)
                    break;
                if (memcmp(header + 4, "gbAs", 4) == 0) {
                    uint8_t *compressed = malloc(length);
                    size_t state_size = core->stateSize(core);
                    uint8_t *raw = malloc(state_size);
                    uLongf raw_size = state_size;
                    if (compressed && raw && fread(compressed, 1, length, png) == length &&
                            uncompress(raw, &raw_size, compressed, length) == Z_OK &&
                            raw_size == state_size) {
                        success = core->loadState(core, raw);
                    }
                    free(raw);
                    free(compressed);
                    break;
                }
                if (fseek(png, (long)length + 4, SEEK_CUR) != 0)
                    break;
            }
        }
        if (png)
            fclose(png);
        if (success)
            fprintf(stderr, "Loaded compatible gbAs core payload from '%s'\n", path);
        else
            fprintf(stderr, "Unable to load mGBA state '%s'\n", path);
    }
    return success;
}

static int save_state(struct mCore *core, const char *path) {
    struct VFile *state = VFileOpen(path, O_RDWR | O_CREAT | O_TRUNC);
    int success;
    if (!state)
        return 0;
    success = mCoreSaveStateNamed(core, state, SAVESTATE_ALL);
    state->close(state);
    return success;
}

static int host_save_state(void *context, const char *path) {
    int success = save_state(context, path);
    fprintf(stderr, "%s state: %s\n", success ? "Saved" : "Unable to save", path);
    return success;
}

static int host_load_state(void *context, const char *path) {
    int success = load_state(context, path);
    if (success) {
        ++state_reload_generation;
        fprintf(stderr, "Loaded state: %s\n", path);
    }
    return success;
}

static struct mCore *find_core(const char *path) {
    struct VFile *rom = VFileOpen(path, O_RDONLY);
    struct mCore *core;
    if (!rom)
        return NULL;
    core = mCoreFindVF(rom);
    rom->close(rom);
    return core;
}

static int load_rom(struct mCore *core, const char *path) {
    struct VFile *rom = VFileOpen(path, O_RDONLY);
    if (!rom)
        return 0;
    if (!core->isROM(rom) || !core->loadROM(core, rom)) {
        rom->close(rom);
        return 0;
    }
    return 1;
}

static int load_save(struct mCore *core, const char *path) {
    struct VFile *save = VFileOpen(path, O_RDWR | O_CREAT);
    if (!save)
        return 0;
    if (!core->loadSave(core, save)) {
        save->close(save);
        return 0;
    }
    return 1;
}

static int save_canvas_bmp(const char *path, Fe8HostPixel *canvas,
    int canvas_width, int canvas_height) {
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(
        canvas, canvas_width, canvas_height, 32,
        canvas_width * (int)sizeof(*canvas), SDL_PIXELFORMAT_RGBA32);
    int result;
    if (!surface)
        return 0;
    result = SDL_SaveBMP(surface, path) == 0;
    SDL_FreeSurface(surface);
    return result;
}

static int resize_canvas(Fe8HostPixel **canvas,
    int *canvas_width, int *canvas_height,
    Fe8ExtendedViewport *viewport, const Fe8HostVideo *video) {
    Fe8HostPixel *resized;
    if (*canvas_width == video->canvas_width &&
            *canvas_height == video->canvas_height)
        return 1;
    resized = realloc(*canvas,
        (size_t)video->canvas_width * video->canvas_height * sizeof(**canvas));
    if (!resized)
        return 0;
    *canvas = resized;
    *canvas_width = video->canvas_width;
    *canvas_height = video->canvas_height;
    viewport->width = *canvas_width;
    viewport->height = *canvas_height;
    return 1;
}

static float inventory_point_scale(const Fe8HostVideo *video) {
    int width = 0, height = 0;
    SDL_GetWindowSize(video->window, &width, &height);
    return width > 0 ? (float)video->canvas_width / width : 1.0f;
}

static int set_inventory_presentation(Fe8HostVideo *video,
    Fe8HostPixel **canvas, int *canvas_width, int *canvas_height,
    Fe8ExtendedViewport *viewport, int *gba_x, int *gba_y,
    Fe8InventoryUi *ui, int enabled, enum Fe8HostShader game_shader) {
    int density = enabled ? 0 : 1;
    if (enabled && !ui->desktop) {
        SDL_GetWindowMinimumSize(video->window, &ui->previous_min_width, &ui->previous_min_height);
        SDL_SetWindowMinimumSize(video->window, 640, 480);
    } else if (!enabled && ui->desktop) {
        SDL_SetWindowMinimumSize(video->window, ui->previous_min_width, ui->previous_min_height);
    }
    if (!fe8_host_video_set_content_density(video, density) ||
            !resize_canvas(canvas, canvas_width, canvas_height, viewport, video))
        return 0;
    ui->desktop = enabled;
    ui->desktop_scale = inventory_point_scale(video);
    ui->render_scale = 1;
    *gba_x = (*canvas_width - GBA_WIDTH) / 2;
    *gba_y = (*canvas_height - GBA_HEIGHT) / 2;
    viewport->gba_x = *gba_x;
    viewport->gba_y = *gba_y;
    return fe8_host_video_set_shader(video,
        enabled ? FE8_HOST_SHADER_OFF : game_shader);
}

static uint32_t scripted_continue_keys(unsigned frame) {
    struct scripted_press { unsigned frame; uint32_t key; };
    static const struct scripted_press presses[] = {
        {90, UINT32_C(1) << FE8_HOST_A},
        {210, UINT32_C(1) << FE8_HOST_START},
        {330, UINT32_C(1) << FE8_HOST_START},
        {450, UINT32_C(1) << FE8_HOST_A},
        {570, UINT32_C(1) << FE8_HOST_A},
        {690, UINT32_C(1) << FE8_HOST_A},
        {850, UINT32_C(1) << FE8_HOST_START},
        {1000, UINT32_C(1) << FE8_HOST_A},
        {1120, UINT32_C(1) << FE8_HOST_A},
        {1240, UINT32_C(1) << FE8_HOST_A},
    };
    size_t i;
    for (i = 0; i < sizeof(presses) / sizeof(presses[0]); ++i)
        if (frame >= presses[i].frame && frame < presses[i].frame + 3)
            return presses[i].key;
    if (frame >= 1420 && frame % 360 < 3)
        return UINT32_C(1) << FE8_HOST_A;
    if (frame >= 1420 && frame % 360 >= 180 && frame % 360 < 183)
        return UINT32_C(1) << FE8_HOST_START;
    return 0;
}

int main(int argc, char **argv) {
    struct fe8_options options;
    struct mCore *core = NULL;
    const Fe8Profile *profile = fe8u_profile();
    Fe8MemoryReader profile_memory;
    Fe8MemoryWriter profile_writer;
    Fe8MemoryView render_memory;
    Fe8AddressSpace address_space;
    Fe8LiveState live_state = {0};
    Fe8Snapshot snapshot;
    Fe8MapRenderState map_state = {0};
    Fe8PaletteMapping palette_mapping = {0};
    Fe8TerrainCache *terrain_cache = NULL;
    Fe8ExtendedPresentation presentation = {0};
    Fe8ExtendedViewport viewport = {0};
    Fe8HostSettings settings;
    Fe8HostAudio audio = {0};
    Fe8HostVideo video = {0};
    Fe8MouseController mouse = {0};
    Fe8InventoryUi inventory_ui;
    Fe8Catalog inventory_catalog = {0};
    Fe8InventorySnapshot inventory_snapshot = {0};
    struct {
        int valid;
        Fe8InventoryEndpoint first;
        Fe8InventoryEndpoint second;
        uint16_t first_item;
        uint16_t second_item;
    } inventory_undo = {0};
    struct pan_controller pan = {0};
    mColor *video_buffer = NULL;
    Fe8HostPixel *host_frame = NULL;
    Fe8HostPixel *canvas = NULL;
    Fe8HostPixel *frozen_canvas = NULL;
    size_t frozen_canvas_pixels = 0;
    int frozen_canvas_width = 0;
    int frozen_canvas_height = 0;
    int frozen_valid = 0;
    Fe8FramePlacement frozen_placement = {0};
    int canvas_width = 0;
    int canvas_height = 0;
    int gba_x = GBA_X;
    int gba_y = GBA_Y;
    uint32_t keyboard_keys = 0;
    uint32_t hotkeys_down = 0;
    size_t video_stride = GBA_WIDTH;
    unsigned width = 0;
    unsigned height = 0;
    int running = 1;
    int core_initialized = 0;
    int sdl_initialized = 0;
    int audio_initialized = 0;
    int family_match = 0;
    int snapshot_valid = 0;
    int live_state_valid = 0;
    int extension_active = 0;
    unsigned rendered_map_sprites = 0;
    int reported_profile = 0;
    unsigned frame_count = 0;
    unsigned large_map_ready_frames = 0;
    int large_map_ready = 0;
    int state_out_saved = 0;
    int terrain_capture_saved = 0;
    uint64_t frame_deadline = 0;
    uint64_t frame_period = 0;
    uint64_t performance_frequency = 0;
    unsigned settings_revision = 0;
    unsigned applied_state_reload_generation = 0;
    int applied_mouse_enabled = 0;
    int visual_profile_active = 0;
    uint8_t map_identity_chapter = 0;
    uint16_t map_identity_width = 0;
    uint16_t map_identity_height = 0;
    uint32_t map_identity_rows = 0;
    uint32_t map_identity_config = 0;
    int map_identity_valid = 0;
    int hp_bars_detected = 0;
    int speed_up_active = 0;
    int previous_camera_valid = 0;
    int pointer_tile_valid = 0;
    int pointer_tile_x = 0;
    int pointer_tile_y = 0;
    int pointer_canvas_valid = 0;
    int pointer_canvas_x = 0;
    int pointer_canvas_y = 0;
    int host_pointer_visible = 0;
    int host_pointer_canvas_x = 0;
    int host_pointer_canvas_y = 0;
    int system_cursor_hidden = 0;
    int16_t previous_camera_x = 0;
    int16_t previous_camera_y = 0;
    int exit_code = EXIT_FAILURE;
    struct mStandardLogger logger = {0};
    Fe8PerfStats perf = {0};

    if (argc == 1)
        return fe8_macos_run_library(argv[0]);
    if (!parse_options(argc, argv, &options)) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    fe8_host_settings_init(&settings);
    fe8_macos_load_settings(&settings);
    if (options.mute)
        settings.audio_enabled = 0;
    fe8_inventory_ui_init(&inventory_ui);
    if (!options.extensions)
        settings.extensions_enabled = 0;
    fprintf(stderr, "Starting libmGBA core: %s\n", options.rom_path);
    core = find_core(options.rom_path);
    if (!core || !core->init(core)) {
        fprintf(stderr, "mGBA could not initialize '%s'\n", options.rom_path);
        goto cleanup;
    }
    fprintf(stderr, "mGBA core initialized\n");
    core_initialized = 1;
    mCoreInitConfig(core, "fe8-extended");
    mStandardLoggerInit(&logger);
    logger.d.filter->defaultLevels = mLOG_FATAL | mLOG_ERROR | mLOG_WARN;
    mLogSetDefaultLogger(&logger.d);
    if (!load_rom(core, options.rom_path)) {
        fprintf(stderr, "mGBA could not load ROM '%s'\n", options.rom_path);
        goto cleanup;
    }
    fprintf(stderr, "ROM loaded\n");
    core->baseVideoSize(core, &width, &height);
    if (width != GBA_WIDTH || height != GBA_HEIGHT) {
        fprintf(stderr, "Expected a 240x160 GBA framebuffer, got %ux%u\n", width, height);
        goto cleanup;
    }
    video_buffer = calloc((size_t)GBA_HEIGHT * video_stride, sizeof(*video_buffer));
    host_frame = malloc((size_t)GBA_WIDTH * GBA_HEIGHT * sizeof(*host_frame));
    terrain_cache = fe8_terrain_cache_create();
    if (!video_buffer || !host_frame || !terrain_cache)
        goto cleanup;
    core->setVideoBuffer(core, video_buffer, video_stride);
    if (options.save_path && !load_save(core, options.save_path))
        fprintf(stderr, "Warning: unable to load save '%s'\n", options.save_path);
    core->reset(core);
    fprintf(stderr, "Core reset complete (state bytes=%zu)\n", core->stateSize(core));
    if (options.state_path && !load_state(core, options.state_path))
        goto cleanup;

    map_core_memory(&address_space, core);
    profile_memory.context = &address_space;
    profile_memory.read8 = fe8_address_space_read8;
    profile = fe8_profile_for_rom(&profile_memory);
    profile_writer.context = core;
    profile_writer.write8 = core_write8;
    if (!fe8_catalog_init(&profile_memory, profile, &inventory_catalog))
        fprintf(stderr, "Inventory metadata unavailable for this ROM\n");
    render_memory.context = &address_space;
    render_memory.read8 = fe8_address_space_read8;
    family_match = settings.extensions_enabled && fe8_detect_fe8u_family(&profile_memory);
    fprintf(stderr, "FE8 extensions: %s\n", family_match ? profile->profile_name :
        (!settings.extensions_enabled ? "disabled by settings" : "disabled (unknown ROM)"));

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        goto cleanup;
    }
    sdl_initialized = 1;
    if (!fe8_host_video_init(&video, "FE8 Extended Frontend",
            CANVAS_WIDTH, CANVAS_HEIGHT,
            settings.vsync_enabled && (!options.capture_path || options.realtime))) {
        fprintf(stderr, "Video setup failed: %s\n", SDL_GetError());
        goto cleanup;
    }
    if (!resize_canvas(&canvas, &canvas_width, &canvas_height, &viewport, &video))
        goto cleanup;
    gba_x = (canvas_width - GBA_WIDTH) / 2;
    gba_y = (canvas_height - GBA_HEIGHT) / 2;
    viewport.gba_x = gba_x;
    viewport.gba_y = gba_y;
    applied_mouse_enabled = settings.mouse_enabled;
    if (!fe8_host_video_set_shader(&video, settings.shader)) {
        fprintf(stderr, "Unable to enable shader '%s'; using Off\n",
            fe8_host_shader_name(settings.shader));
        settings.shader = FE8_HOST_SHADER_OFF;
    }
    fe8_macos_install_settings_menu(&settings, core,
        host_save_state, host_load_state, options.quick_state_path);
    if (fe8_host_audio_init(&audio, core)) {
        audio_initialized = 1;
        fe8_host_audio_set_enabled(&audio, settings.audio_enabled);
    }
    settings_revision = settings.revision;
    performance_frequency = SDL_GetPerformanceFrequency();
    perf.started = SDL_GetPerformanceCounter();
    frame_period = (uint64_t)((double)performance_frequency * core->frameCycles(core) /
        core->frequency(core) + 0.5);
    frame_deadline = SDL_GetPerformanceCounter();
    fe8_host_video_log_status(&video);
    fprintf(stderr, "Frame pacing: %.3f fps, VSync=%s, shader=%s\n",
        (double)core->frequency(core) / core->frameCycles(core),
        video.vsync_active ? "enabled" : "disabled",
        fe8_host_shader_name(video.shader));

    live_state_valid = family_match &&
        fe8_extract_live_state(&profile_memory, profile, &live_state);
    snapshot_valid = family_match &&
        fe8_extract_snapshot(&profile_memory, profile, &snapshot);
    if (options.open_inventory && family_match &&
            fe8_extract_prebattle_inventory(&profile_memory, profile,
                &inventory_catalog, &inventory_snapshot)) {
        fe8_inventory_ui_open(&inventory_ui, &inventory_snapshot);
        if (!set_inventory_presentation(&video, &canvas, &canvas_width,
                &canvas_height, &viewport, &gba_x, &gba_y,
                &inventory_ui, 1, settings.shader))
            goto cleanup;
        options.open_inventory = 0;
        fprintf(stderr, "Inventory manager: auto-opened (%u units, %u supply items, supply=%08X/%u)\n",
            inventory_snapshot.unit_count, inventory_snapshot.supply_count,
            inventory_snapshot.supply_address, inventory_snapshot.supply_capacity);
    }

    while (running) {
        SDL_Event event;
        if (inventory_ui.active)
            inventory_ui.desktop_scale = inventory_point_scale(&video);
        if (state_reload_generation != applied_state_reload_generation) {
            applied_state_reload_generation = state_reload_generation;
            if (inventory_ui.active)
                set_inventory_presentation(&video, &canvas, &canvas_width,
                    &canvas_height, &viewport, &gba_x, &gba_y,
                    &inventory_ui, 0, settings.shader);
            inventory_ui.active = 0;
            inventory_undo.valid = 0;
            map_identity_valid = 0;
            fe8_palette_mapping_reset(&palette_mapping);
            fe8_terrain_cache_reset(terrain_cache);
            visual_profile_active = 0;
            fe8_presentation_reset(&presentation);
            frozen_valid = 0;
            previous_camera_valid = 0;
        }
        if (fe8_host_video_refresh_layout(&video)) {
            int old_center_world_x = snapshot_valid ?
                snapshot.camera_x + canvas_width / 2 - viewport.gba_x : 0;
            int old_center_world_y = snapshot_valid ?
                snapshot.camera_y + canvas_height / 2 - viewport.gba_y : 0;
            if (!resize_canvas(
                    &canvas, &canvas_width, &canvas_height, &viewport, &video)) {
                fprintf(stderr, "Unable to resize host canvas\n");
                running = 0;
                break;
            }
            fe8_presentation_reset(&presentation);
            visual_profile_active = 0;
            frozen_valid = 0;
            gba_x = (canvas_width - GBA_WIDTH) / 2;
            gba_y = (canvas_height - GBA_HEIGHT) / 2;
            if (snapshot_valid && !inventory_ui.active) {
                int desired_gba_x = snapshot.camera_x + canvas_width / 2 -
                    old_center_world_x;
                int desired_gba_y = snapshot.camera_y + canvas_height / 2 -
                    old_center_world_y;
                pan.x = gba_x - desired_gba_x;
                pan.y = gba_y - desired_gba_y;
                fe8_viewport_clamp_pan(
                    &pan.x, &pan.y, &snapshot, &viewport, gba_x, gba_y);
            } else {
                viewport.gba_x = gba_x;
                viewport.gba_y = gba_y;
            }
            pointer_canvas_valid = 0;
            pointer_tile_valid = 0;
        }
        if (settings.revision != settings_revision) {
            keyboard_keys = 0;
            hotkeys_down = 0;
            set_speed_up_mode(0, &speed_up_active, &video, &audio,
                audio_initialized, &settings, &frame_deadline);
            if (settings.mouse_enabled != applied_mouse_enabled) {
                fe8_mouse_cancel(&mouse);
                pan.dragging = 0;
                pointer_canvas_valid = 0;
                pointer_tile_valid = 0;
                host_pointer_visible = 0;
                if (!settings.mouse_enabled && system_cursor_hidden) {
                    SDL_ShowCursor(SDL_ENABLE);
                    system_cursor_hidden = 0;
                }
                applied_mouse_enabled = settings.mouse_enabled;
            }
            if (audio_initialized)
                fe8_host_audio_set_enabled(&audio, settings.audio_enabled);
            if (!fe8_host_video_set_vsync(&video, settings.vsync_enabled))
                fprintf(stderr, "Unable to change VSync: %s\n", SDL_GetError());
            if (!fe8_host_video_set_shader(&video, settings.shader))
                fprintf(stderr, "Unable to apply shader '%s'\n",
                    fe8_host_shader_name(settings.shader));
            family_match = settings.extensions_enabled && fe8_detect_fe8u_family(&profile_memory);
            visual_profile_active = 0;
            fe8_presentation_reset(&presentation);
            frozen_valid = 0;
            previous_camera_valid = 0;
            map_identity_valid = 0;
            fe8_palette_mapping_reset(&palette_mapping);
            fe8_terrain_cache_reset(terrain_cache);
            settings_revision = settings.revision;
            frame_deadline = SDL_GetPerformanceCounter();
            fprintf(stderr, "Settings applied: audio=%s VSync=%s extensions=%s mouse=%s shader=%s zoom=%.1f%% speed-up=%s\n",
                settings.audio_enabled ? "on" : "off",
                video.vsync_active ? "on" : "off",
                settings.extensions_enabled ? "on" : "off",
                settings.mouse_enabled ? "on" : "off",
                fe8_host_shader_name(video.shader),
                settings.zoom_sensitivity * 100.0,
                fe8_host_speedup_name(settings.speedup_rate));
        }
        live_state_valid = family_match &&
            fe8_extract_live_state(&profile_memory, profile, &live_state);
        if (options.open_inventory && family_match &&
                fe8_extract_prebattle_inventory(&profile_memory, profile,
                    &inventory_catalog, &inventory_snapshot)) {
            fe8_inventory_ui_open(&inventory_ui, &inventory_snapshot);
            if (!set_inventory_presentation(&video, &canvas, &canvas_width,
                    &canvas_height, &viewport, &gba_x, &gba_y,
                    &inventory_ui, 1, settings.shader))
                goto cleanup;
            options.open_inventory = 0;
            keyboard_keys = 0;
            fprintf(stderr,
                "Inventory manager: auto-opened (%u units, %u supply items, supply=%08X/%u)\n",
                inventory_snapshot.unit_count, inventory_snapshot.supply_count,
                inventory_snapshot.supply_address, inventory_snapshot.supply_capacity);
        }
        if (snapshot_valid) {
            map_state.map_width = snapshot.map_width;
            map_state.map_height = snapshot.map_height;
            map_state.camera_x = snapshot.camera_x;
            map_state.camera_y = snapshot.camera_y;
            fe8_viewport_clamp_pan(
                &pan.x, &pan.y, &snapshot, &viewport, gba_x, gba_y);
            if (settings.mouse_enabled && pointer_canvas_valid && !pan.dragging &&
                    visual_profile_active && snapshot.input_lock == 0 &&
                    !mouse.confirm) {
                int map_x;
                int map_y;
                if (fe8_canvas_to_map_tile(&map_state, viewport,
                        pointer_canvas_x, pointer_canvas_y, &map_x, &map_y)) {
                    pointer_tile_valid = 1;
                    pointer_tile_x = map_x;
                    pointer_tile_y = map_y;
                    if (snapshot.cursor_x != map_x || snapshot.cursor_y != map_y ||
                            snapshot.cursor_display_x != map_x * 16 ||
                            snapshot.cursor_display_y != map_y * 16)
                        set_mouse_map_target(&mouse, &snapshot, map_x, map_y, 0);
                } else {
                    pointer_canvas_valid = 0;
                    pointer_tile_valid = 0;
                }
            }
            if (mouse.stalled) {
                fprintf(stderr, "Mouse path cancelled after rejected native input; move the pointer to retry\n");
                fe8_mouse_cancel(&mouse);
                pointer_canvas_valid = 0;
                pointer_tile_valid = 0;
            }
        }
        if (!large_map_ready && snapshot_valid && visual_profile_active &&
                snapshot.input_lock == 0 &&
                (snapshot.map_width > 15 || snapshot.map_height > 10)) {
            if (large_map_ready_frames < 60)
                ++large_map_ready_frames;
            if (large_map_ready_frames == 60) {
                large_map_ready = 1;
                fprintf(stderr, "Large interactive map ready: %ux%u\n",
                    snapshot.map_width, snapshot.map_height);
            }
        } else if (!large_map_ready) {
            large_map_ready_frames = 0;
        }
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_MOUSEMOTION) {
                if ((settings.mouse_enabled || inventory_ui.active) &&
                        fe8_host_video_event_to_canvas(
                        &video, event.motion.x, event.motion.y,
                        &host_pointer_canvas_x, &host_pointer_canvas_y)) {
                    host_pointer_visible = 1;
                    if (!inventory_ui.active && !system_cursor_hidden) {
                        SDL_ShowCursor(SDL_DISABLE);
                        system_cursor_hidden = 1;
                    }
                } else {
                    host_pointer_visible = 0;
                    if (system_cursor_hidden) {
                        SDL_ShowCursor(SDL_ENABLE);
                        system_cursor_hidden = 0;
                    }
                }
            } else if (event.type == SDL_WINDOWEVENT &&
                    event.window.event == SDL_WINDOWEVENT_LEAVE) {
                host_pointer_visible = 0;
                if (system_cursor_hidden) {
                    SDL_ShowCursor(SDL_ENABLE);
                    system_cursor_hidden = 0;
                }
            } else if (event.type == SDL_WINDOWEVENT &&
                    event.window.event == SDL_WINDOWEVENT_ENTER &&
                    (settings.mouse_enabled || inventory_ui.active)) {
                int window_x;
                int window_y;
                SDL_GetMouseState(&window_x, &window_y);
                if (fe8_host_video_window_to_canvas(&video, window_x, window_y,
                        &host_pointer_canvas_x, &host_pointer_canvas_y)) {
                    host_pointer_visible = 1;
                    if (!inventory_ui.active) {
                        SDL_ShowCursor(SDL_DISABLE);
                        system_cursor_hidden = 1;
                    }
                }
            }
            if (event.type == SDL_KEYDOWN && !event.key.repeat &&
                    event.key.keysym.scancode == SDL_SCANCODE_I) {
                if (inventory_ui.active) {
                    inventory_ui.active = 0;
                    inventory_ui.has_selection = 0;
                    set_inventory_presentation(&video, &canvas, &canvas_width,
                        &canvas_height, &viewport, &gba_x, &gba_y,
                        &inventory_ui, 0, settings.shader);
                    frame_deadline = SDL_GetPerformanceCounter();
                    fprintf(stderr, "Inventory manager: closed\n");
                } else if (fe8_detect_fe8u_family(&profile_memory) &&
                        fe8_extract_prebattle_inventory(
                            &profile_memory, profile, &inventory_catalog,
                            &inventory_snapshot)) {
                    fe8_inventory_ui_open(&inventory_ui, &inventory_snapshot);
                    if (!set_inventory_presentation(&video, &canvas,
                            &canvas_width, &canvas_height, &viewport,
                            &gba_x, &gba_y, &inventory_ui, 1, settings.shader))
                        running = 0;
                    inventory_undo.valid = 0;
                    keyboard_keys = 0;
                    hotkeys_down = 0;
                    fe8_mouse_cancel(&mouse);
                    fprintf(stderr,
                        "Inventory manager: opened (%u units, %u supply items, supply=%08X/%u)\n",
                        inventory_snapshot.unit_count, inventory_snapshot.supply_count,
                        inventory_snapshot.supply_address, inventory_snapshot.supply_capacity);
                } else {
                    fprintf(stderr,
                        "Inventory manager unavailable: no active FE8 roster\n");
                }
                continue;
            }
            if (inventory_ui.active) {
                inventory_ui.desktop_scale = inventory_point_scale(&video);
                if (event.type == SDL_QUIT) {
                    running = 0;
                } else if (event.type == SDL_MOUSEMOTION) {
                    int index;
                    Fe8InventoryHitKind hit = fe8_inventory_ui_hit_test(&inventory_ui,
                        &inventory_snapshot, canvas_width, canvas_height,
                        host_pointer_canvas_x, host_pointer_canvas_y, &index);
                    fe8_inventory_ui_inspect(&inventory_ui, &inventory_snapshot,
                        hit, index);
                } else if (event.type == SDL_KEYDOWN && !event.key.repeat &&
                        event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                    inventory_ui.active = 0;
                    set_inventory_presentation(&video, &canvas, &canvas_width,
                        &canvas_height, &viewport, &gba_x, &gba_y,
                        &inventory_ui, 0, settings.shader);
                    frame_deadline = SDL_GetPerformanceCounter();
                    fprintf(stderr, "Inventory manager: closed\n");
                } else if (event.type == SDL_KEYDOWN && !event.key.repeat &&
                        event.key.keysym.scancode == SDL_SCANCODE_A) {
                    fe8_inventory_ui_toggle_scope(&inventory_ui,
                        &inventory_snapshot);
                } else if (event.type == SDL_KEYDOWN && !event.key.repeat &&
                        event.key.keysym.scancode == SDL_SCANCODE_S) {
                    fe8_inventory_ui_cycle_sort(&inventory_ui,
                        &inventory_snapshot);
                } else if (event.type == SDL_KEYDOWN && !event.key.repeat &&
                        event.key.keysym.scancode == SDL_SCANCODE_D) {
                    fe8_inventory_ui_toggle_density(&inventory_ui);
                } else if (event.type == SDL_KEYDOWN && !event.key.repeat &&
                        event.key.keysym.scancode == SDL_SCANCODE_U &&
                        inventory_undo.valid) {
                    if (fe8_swap_inventory_endpoints(&profile_memory, &profile_writer,
                            profile, inventory_undo.first, inventory_undo.second_item,
                            inventory_undo.second, inventory_undo.first_item)) {
                        if (fe8_extract_prebattle_inventory(
                                &profile_memory, profile, &inventory_catalog,
                                &inventory_snapshot))
                            fe8_inventory_ui_rebuild(&inventory_ui,
                                &inventory_snapshot);
                        snprintf(inventory_ui.status, sizeof(inventory_ui.status),
                            "Undid last swap");
                        inventory_undo.valid = 0;
                    }
                } else if (event.type == SDL_MOUSEWHEEL) {
                    int direction = event.wheel.y > 0 ? -3 :
                        event.wheel.y < 0 ? 3 : 0;
                    if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
                        direction = -direction;
                    fe8_inventory_ui_scroll(&inventory_ui, direction,
                        &inventory_snapshot, canvas_width, canvas_height,
                        host_pointer_canvas_x);
                } else if (event.type == SDL_MOUSEBUTTONDOWN &&
                        event.button.button == SDL_BUTTON_RIGHT) {
                    inventory_ui.has_selection = 0;
                    snprintf(inventory_ui.status, sizeof(inventory_ui.status),
                        "Selection cleared");
                } else if (event.type == SDL_MOUSEBUTTONDOWN &&
                        event.button.button == SDL_BUTTON_LEFT) {
                    int canvas_x;
                    int canvas_y;
                    int index;
                    if (fe8_host_video_event_to_canvas(&video,
                            event.button.x, event.button.y, &canvas_x, &canvas_y)) {
                        Fe8InventoryHitKind hit = fe8_inventory_ui_hit_test(&inventory_ui,
                            &inventory_snapshot, canvas_width, canvas_height,
                            canvas_x, canvas_y, &index);
                        if (hit == FE8_INVENTORY_HIT_DENSITY) {
                            fe8_inventory_ui_toggle_density(&inventory_ui);
                        } else if (hit == FE8_INVENTORY_HIT_SORT_COLUMN &&
                                index >= 0 && index < FE8_INVENTORY_SORT_COUNT) {
                            inventory_ui.pool_sort = (Fe8InventorySort)index;
                            inventory_ui.pool_scroll = 0;
                            fe8_inventory_ui_rebuild(&inventory_ui, &inventory_snapshot);
                        } else if (hit == FE8_INVENTORY_HIT_POOL_SCOPE) {
                            fe8_inventory_ui_toggle_scope(&inventory_ui,
                                &inventory_snapshot);
                        } else if (hit == FE8_INVENTORY_HIT_POOL_SORT) {
                            fe8_inventory_ui_cycle_sort(&inventory_ui,
                                &inventory_snapshot);
                        } else if ((hit == FE8_INVENTORY_HIT_ROSTER ||
                                hit == FE8_INVENTORY_HIT_ROSTER_CLASS) &&
                                index >= 0 && index < inventory_snapshot.unit_count) {
                            inventory_ui.current_unit = index;
                            if (inventory_ui.has_selection)
                                snprintf(inventory_ui.status, sizeof(inventory_ui.status),
                                    "Choose a destination on %s",
                                    inventory_snapshot.units[index].name);
                            else
                                snprintf(inventory_ui.status, sizeof(inventory_ui.status),
                                    "Choose an item for %s",
                                    inventory_snapshot.units[index].name);
                        } else if (hit == FE8_INVENTORY_HIT_UNIT_ITEM ||
                                hit == FE8_INVENTORY_HIT_POOL_ITEM) {
                            Fe8InventoryEndpoint endpoint = fe8_inventory_ui_endpoint(
                                &inventory_ui, &inventory_snapshot, hit, index);
                            fe8_inventory_ui_inspect(&inventory_ui,
                                &inventory_snapshot, hit, index);
                            uint16_t endpoint_item = fe8_inventory_ui_endpoint_item(
                                &inventory_snapshot, endpoint);
                            if (endpoint_item && !fe8_inventory_ui_endpoint_movable(
                                    &inventory_snapshot, endpoint)) {
                                snprintf(inventory_ui.status, sizeof(inventory_ui.status),
                                    "Learned spells are fixed and cannot be moved");
                            } else if (!inventory_ui.has_selection) {
                                inventory_ui.selected = endpoint;
                                inventory_ui.has_selection = 1;
                                snprintf(inventory_ui.status, sizeof(inventory_ui.status),
                                    "Choose a destination - right-click to cancel");
                            } else {
                                uint16_t first_item = fe8_inventory_ui_endpoint_item(
                                    &inventory_snapshot, inventory_ui.selected);
                                if (fe8_swap_inventory_endpoints(&profile_memory,
                                        &profile_writer, profile, inventory_ui.selected,
                                        first_item, endpoint, endpoint_item)) {
                                    inventory_undo.valid = 1;
                                    inventory_undo.first = inventory_ui.selected;
                                    inventory_undo.first_item = first_item;
                                    inventory_undo.second = endpoint;
                                    inventory_undo.second_item = endpoint_item;
                                    if (fe8_extract_prebattle_inventory(
                                            &profile_memory, profile, &inventory_catalog,
                                            &inventory_snapshot))
                                        fe8_inventory_ui_rebuild(&inventory_ui,
                                            &inventory_snapshot);
                                    snprintf(inventory_ui.status,
                                        sizeof(inventory_ui.status),
                                        "Moved item - press U to undo");
                                } else {
                                    snprintf(inventory_ui.status,
                                        sizeof(inventory_ui.status),
                                        "Move rejected - game state changed");
                                }
                                inventory_ui.has_selection = 0;
                            }
                        }
                    }
                }
                continue;
            }
            if (event.type == SDL_QUIT) {
                running = 0;
            } else if (event.type == SDL_MOUSEWHEEL) {
                int window_x;
                int window_y;
                int old_canvas_x;
                int old_canvas_y;
                int old_pointer_valid;
                int old_world_x = 0;
                int old_world_y = 0;
#if SDL_VERSION_ATLEAST(2, 0, 18)
                double wheel_delta = event.wheel.preciseY;
#else
                double wheel_delta = event.wheel.y;
#endif
                if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
                    wheel_delta = -wheel_delta;
                if (wheel_delta > 1.0)
                    wheel_delta = 1.0;
                else if (wheel_delta < -1.0)
                    wheel_delta = -1.0;
                SDL_GetMouseState(&window_x, &window_y);
                old_pointer_valid = fe8_host_video_window_to_canvas(&video,
                    window_x, window_y, &old_canvas_x, &old_canvas_y);
                if (old_pointer_valid && snapshot_valid) {
                    old_world_x = snapshot.camera_x + old_canvas_x - viewport.gba_x;
                    old_world_y = snapshot.camera_y + old_canvas_y - viewport.gba_y;
                }
                if (fe8_host_video_adjust_zoom(
                        &video, wheel_delta, settings.zoom_sensitivity)) {
                    int canvas_x;
                    int canvas_y;
                    if (!resize_canvas(&canvas, &canvas_width, &canvas_height,
                            &viewport, &video)) {
                        running = 0;
                        break;
                    }
                    fe8_presentation_reset(&presentation);
                    visual_profile_active = 0;
                    frozen_valid = 0;
                    gba_x = (canvas_width - GBA_WIDTH) / 2;
                    gba_y = (canvas_height - GBA_HEIGHT) / 2;
                    pointer_tile_valid = 0;
                    if (fe8_host_video_window_to_canvas(&video,
                            window_x, window_y, &canvas_x, &canvas_y)) {
                        if (old_pointer_valid && snapshot_valid) {
                            int desired_gba_x = snapshot.camera_x + canvas_x - old_world_x;
                            int desired_gba_y = snapshot.camera_y + canvas_y - old_world_y;
                            pan.x = gba_x - desired_gba_x;
                            pan.y = gba_y - desired_gba_y;
                            fe8_viewport_clamp_pan(&pan.x, &pan.y,
                                &snapshot, &viewport, gba_x, gba_y);
                        }
                        host_pointer_canvas_x = canvas_x;
                        host_pointer_canvas_y = canvas_y;
                        if (settings.mouse_enabled) {
                            host_pointer_visible = 1;
                            pointer_canvas_valid = 1;
                            pointer_canvas_x = canvas_x;
                            pointer_canvas_y = canvas_y;
                        }
                    }
                    fprintf(stderr,
                        "Display zoom: %.2fx (scale %.2fx, canvas %dx%d)\n",
                        video.scaling.zoom_factor, video.scaling.pixel_scale,
                        canvas_width, canvas_height);
                }
            } else if (event.type == SDL_MOUSEBUTTONDOWN &&
                    event.button.button == SDL_BUTTON_RIGHT && settings.mouse_enabled) {
                fe8_mouse_cancel(&mouse);
                pointer_tile_valid = 0;
                pointer_canvas_valid = 0;
                mouse.pulse_key = UINT32_C(1) << FE8_HOST_B;
                mouse.press_frames = 2;
                /* Fast mouse travel already holds B. Force a release first so
                 * FE8 observes right-click as a new cancel press. */
                mouse.release_frames = 2;
                fprintf(stderr, "Mouse right-click: B queued\n");
            } else if (event.type == SDL_MOUSEBUTTONDOWN &&
                    event.button.button == SDL_BUTTON_LEFT && settings.mouse_enabled) {
                int canvas_x;
                int canvas_y;
                int shift = (SDL_GetModState() & KMOD_SHIFT) != 0;
                if (snapshot_valid && visual_profile_active &&
                        snapshot.input_lock == 0) {
                    if (!fe8_host_video_event_to_canvas(&video,
                            event.button.x, event.button.y, &canvas_x, &canvas_y))
                        continue;
                    if (shift) {
                        pointer_canvas_valid = 0;
                        pan.dragging = 1;
                        pan.moved = 0;
                        pan.start_canvas_x = canvas_x;
                        pan.start_canvas_y = canvas_y;
                        pan.start_pan_x = pan.x;
                        pan.start_pan_y = pan.y;
                        fe8_mouse_cancel(&mouse);
                    } else {
                        int map_x;
                        int map_y;
                        if (fe8_canvas_to_map_tile(&map_state, viewport,
                                canvas_x, canvas_y, &map_x, &map_y)) {
                            pointer_canvas_valid = 1;
                            pointer_canvas_x = canvas_x;
                            pointer_canvas_y = canvas_y;
                            pointer_tile_valid = 1;
                            pointer_tile_x = map_x;
                            pointer_tile_y = map_y;
                            set_mouse_map_target(&mouse, &snapshot, map_x, map_y, 1);
                            fprintf(stderr,
                                "Mouse click: window=%d,%d canvas=%d,%d tile=%d,%d cursor=%u,%u mode=%s\n",
                                event.button.x, event.button.y, canvas_x, canvas_y,
                                map_x, map_y, snapshot.cursor_x, snapshot.cursor_y,
                                snapshot_path_mode(&snapshot) ? "path" : "idle");
                        }
                    }
                } else {
                    fe8_mouse_cancel(&mouse);
                    mouse.pulse_key = UINT32_C(1) << FE8_HOST_A;
                    mouse.press_frames = 2;
                    fprintf(stderr, "Mouse left-click: A queued for native UI\n");
                }
            } else if (event.type == SDL_MOUSEBUTTONUP &&
                    event.button.button == SDL_BUTTON_LEFT && pan.dragging &&
                    settings.mouse_enabled) {
                int canvas_x;
                int canvas_y;
                if (!pan.moved && fe8_host_video_event_to_canvas(&video,
                        event.button.x, event.button.y, &canvas_x, &canvas_y)) {
                    int map_x;
                    int map_y;
                    if (fe8_canvas_to_map_tile(&map_state, viewport,
                            canvas_x, canvas_y, &map_x, &map_y))
                        fe8_viewport_recenter_on_tile(&pan.x, &pan.y,
                            &snapshot, &viewport, gba_x, gba_y, map_x, map_y);
                }
                pan.dragging = 0;
                pointer_canvas_valid = 0;
                pointer_tile_valid = 0;
                fe8_viewport_clamp_pan(
                    &pan.x, &pan.y, &snapshot, &viewport, gba_x, gba_y);
                fprintf(stderr, "Mouse pan: map origin=%d,%d\n",
                    snapshot.camera_x - viewport.gba_x,
                    snapshot.camera_y - viewport.gba_y);
            } else if (event.type == SDL_MOUSEMOTION && settings.mouse_enabled &&
                    snapshot_valid &&
                    visual_profile_active && snapshot.input_lock == 0) {
                int canvas_x;
                int canvas_y;
                if (!fe8_host_video_event_to_canvas(&video,
                        event.motion.x, event.motion.y, &canvas_x, &canvas_y)) {
                    pointer_canvas_valid = 0;
                    pointer_tile_valid = 0;
                } else if (pan.dragging) {
                    int dx = canvas_x - pan.start_canvas_x;
                    int dy = canvas_y - pan.start_canvas_y;
                    pan.x = pan.start_pan_x - dx;
                    pan.y = pan.start_pan_y - dy;
                    pan.moved = pan.moved || abs(dx) >= 2 || abs(dy) >= 2;
                    fe8_viewport_clamp_pan(
                        &pan.x, &pan.y, &snapshot, &viewport, gba_x, gba_y);
                } else {
                    int map_x;
                    int map_y;
                    pointer_canvas_valid = 1;
                    pointer_canvas_x = canvas_x;
                    pointer_canvas_y = canvas_y;
                    if (fe8_canvas_to_map_tile(&map_state, viewport,
                            canvas_x, canvas_y, &map_x, &map_y) &&
                            (!pointer_tile_valid || map_x != pointer_tile_x ||
                             map_y != pointer_tile_y)) {
                        pointer_tile_valid = 1;
                        pointer_tile_x = map_x;
                        pointer_tile_y = map_y;
                        set_mouse_map_target(&mouse, &snapshot, map_x, map_y, 0);
                        fprintf(stderr,
                            "Mouse move: window=%d,%d canvas=%d,%d tile=%d,%d cursor=%u,%u mode=%s\n",
                            event.motion.x, event.motion.y, canvas_x, canvas_y,
                            map_x, map_y, snapshot.cursor_x, snapshot.cursor_y,
                            snapshot_path_mode(&snapshot) ? "path" : "idle");
                    }
                }
            } else {
                uint32_t pressed_hotkeys = event.type == SDL_KEYDOWN && !event.key.repeat ?
                    fe8_host_hotkey_for_scancode(&settings,
                        event.key.keysym.scancode) : 0;
                if (options.quick_state_path &&
                        (pressed_hotkeys & (UINT32_C(1) << FE8_HOST_HOTKEY_QUICK_SAVE)))
                    host_save_state(core, options.quick_state_path);
                if (options.quick_state_path &&
                        (pressed_hotkeys & (UINT32_C(1) << FE8_HOST_HOTKEY_QUICK_LOAD)))
                    host_load_state(core, options.quick_state_path);
                update_hotkeys(&hotkeys_down, &settings, &event);
                if ((event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) &&
                        fe8_host_key_for_scancode(&settings,
                            event.key.keysym.scancode) != 0) {
                    pointer_canvas_valid = 0;
                    pointer_tile_valid = 0;
                    fe8_mouse_cancel(&mouse);
                }
                update_keyboard(&keyboard_keys, &settings, &event);
                set_speed_up_mode(
                    (hotkeys_down &
                        (UINT32_C(1) << FE8_HOST_HOTKEY_SPEED_UP)) != 0,
                    &speed_up_active, &video, &audio, audio_initialized,
                    &settings, &frame_deadline);
            }
        }

        if (!inventory_ui.active) {
            unsigned multiplier = speed_up_active ?
                fe8_host_speedup_multiplier(settings.speedup_rate) : 1;
            unsigned batch_limit = fe8_scheduler_batch_limit(
                speed_up_active != 0, multiplier);
            uint64_t batch_started = SDL_GetPerformanceCounter();
            unsigned batch_index;
            for (batch_index = 0; batch_index < batch_limit; ++batch_index) {
                uint64_t stage_started = SDL_GetPerformanceCounter();
                ++frame_count;
                core->setKeys(core, keyboard_keys |
                    (options.auto_continue && !large_map_ready ?
                        scripted_continue_keys(frame_count) : 0) |
                    fe8_mouse_update(&mouse, &live_state,
                        settings.mouse_enabled && live_state_valid &&
                        visual_profile_active));
                core->runFrame(core);
                perf.emulation += SDL_GetPerformanceCounter() - stage_started;
                ++perf.emulated_frames;
                live_state_valid = family_match &&
                    fe8_extract_live_state(&profile_memory, profile, &live_state);
                if (!multiplier && fe8_scheduler_unlimited_should_present(
                        batch_index + 1, SDL_GetPerformanceCounter() - batch_started,
                        performance_frequency))
                    break;
            }
        }
        convert_framebuffer(video_buffer, video_stride, host_frame);
        if (audio_initialized)
            fe8_host_audio_drain(&audio);
        {
            uint64_t stage_started = SDL_GetPerformanceCounter();
            snapshot_valid = family_match &&
                fe8_extract_snapshot(&profile_memory, profile, &snapshot);
            perf.snapshot += SDL_GetPerformanceCounter() - stage_started;
        }
        if (snapshot_valid) {
            if (snapshot.flags & FE8_SNAPSHOT_HP_BARS)
                hp_bars_detected = 1;
            else if (!hp_bars_detected &&
                    fe8_detect_native_unit_hp_bars(&render_memory, &snapshot)) {
                hp_bars_detected = 1;
                fprintf(stderr, "Extended unit HP bars detected\n");
            }
            if (hp_bars_detected)
                snapshot.flags |= FE8_SNAPSHOT_HP_BARS;
        }
        extension_active = 0;
        rendered_map_sprites = 0;
        Fe8FramePlacement frame_placement = {gba_x, gba_y, 0};
        if (snapshot_valid) {
            int same_map = map_identity_valid &&
                map_identity_chapter == snapshot.chapter &&
                map_identity_width == snapshot.map_width &&
                map_identity_height == snapshot.map_height &&
                map_identity_rows == snapshot.base_tile_rows &&
                map_identity_config == profile->tileset_config;
            if (previous_camera_valid && same_map && visual_profile_active &&
                    snapshot_cursor_controls_camera(&snapshot))
                fe8_viewport_follow_cursor_camera(
                    &pan.x, &pan.y, &snapshot, &viewport, gba_x, gba_y,
                    previous_camera_x, previous_camera_y);
            else
                fe8_viewport_clamp_pan(
                    &pan.x, &pan.y, &snapshot, &viewport, gba_x, gba_y);
            map_state.map_width = snapshot.map_width;
            map_state.map_height = snapshot.map_height;
            map_state.camera_x = snapshot.camera_x;
            map_state.camera_y = snapshot.camera_y;
            map_state.base_tile_rows = snapshot.base_tile_rows;
            map_state.fog_rows = snapshot.fog_rows;
            map_state.tileset_config = profile->tileset_config;
            map_state.tile_graphics = UINT32_C(0x06008000);
            map_state.palette = UINT32_C(0x05000000);
            map_state.palette_mapping = &palette_mapping;
            map_state.tile_cache = terrain_cache;
            if (!map_identity_valid || map_identity_chapter != snapshot.chapter ||
                    map_identity_width != snapshot.map_width ||
                    map_identity_height != snapshot.map_height ||
                    map_identity_rows != snapshot.base_tile_rows ||
                    map_identity_config != map_state.tileset_config) {
                map_identity_chapter = snapshot.chapter;
                map_identity_width = snapshot.map_width;
                map_identity_height = snapshot.map_height;
                map_identity_rows = snapshot.base_tile_rows;
                map_identity_config = map_state.tileset_config;
                map_identity_valid = 1;
                fe8_palette_mapping_reset(&palette_mapping);
                fe8_terrain_cache_reset(terrain_cache);
                visual_profile_active = 0;
                fe8_presentation_reset(&presentation);
                frozen_valid = 0;
            }
            if (presentation.state == FE8_PRESENTATION_FROZEN && frozen_valid &&
                    frozen_canvas_width == canvas_width &&
                    frozen_canvas_height == canvas_height) {
                Fe8FramePlacement validation;
                memcpy(canvas, frozen_canvas,
                    (size_t)canvas_width * canvas_height * sizeof(*canvas));
                frame_placement = frozen_placement;
                validation = fe8_align_frame_to_terrain(
                    host_frame, GBA_WIDTH, GBA_HEIGHT, GBA_WIDTH,
                    frozen_canvas, frozen_canvas_width, frozen_canvas_height,
                    frozen_canvas_width, frozen_placement.x, frozen_placement.y, 0);
                /* FE8's BG2 movement/range layer can obscure every terrain
                 * sample. Its validated logical maps are sufficient evidence
                 * that the native frame is still the tactical map. */
                fe8_presentation_update(&presentation, true,
                    validation.match_percent >= 15 ||
                        fe8_extended_move_range_is_active(&snapshot),
                    snapshot.combat_panel_active);
                visual_profile_active = presentation.state == FE8_PRESENTATION_LIVE;
                extension_active = 1;
            } else {
                unsigned learned = fe8_learn_palette_mapping(
                    &render_memory, &map_state, host_frame, GBA_WIDTH);
                int terrain_rendered;
                int frame_compatible = 0;
                Fe8ExtendedPresentationState previous_state = presentation.state;
                if (learned)
                    fprintf(stderr,
                        "Terrain palette: learned %u bank%s (normal=%04X fog=%04X)\n",
                        learned, learned == 1 ? "" : "s",
                        palette_mapping.valid_mask[0], palette_mapping.valid_mask[1]);
                {
                    uint64_t stage_started = SDL_GetPerformanceCounter();
                    terrain_rendered = fe8_render_extended_terrain(
                        &render_memory, &map_state, viewport, canvas, canvas_width);
                    perf.terrain += SDL_GetPerformanceCounter() - stage_started;
                }
                if (terrain_rendered) {
                    int camera_moving = previous_camera_valid &&
                        (snapshot.camera_x != previous_camera_x ||
                         snapshot.camera_y != previous_camera_y);
                    uint64_t stage_started = SDL_GetPerformanceCounter();
                    frame_placement = fe8_align_frame_to_terrain(
                        host_frame, GBA_WIDTH, GBA_HEIGHT, GBA_WIDTH,
                        canvas, canvas_width, canvas_height, canvas_width,
                        viewport.gba_x, viewport.gba_y, camera_moving ? 8 : 0);
                    perf.alignment += SDL_GetPerformanceCounter() - stage_started;
                    frame_compatible = frame_placement.match_percent >= 15;
                }
                /* Do not freeze or fail to reacquire the extension merely
                 * because a large movement range covers the native viewport. */
                if (fe8_extended_move_range_is_active(&snapshot))
                    frame_compatible = 1;
                fe8_presentation_update(&presentation, terrain_rendered,
                    frame_compatible != 0, snapshot.combat_panel_active);
                visual_profile_active = presentation.state == FE8_PRESENTATION_LIVE;
                extension_active = visual_profile_active;
                if (presentation.state == FE8_PRESENTATION_FROZEN && frozen_valid) {
                    memcpy(canvas, frozen_canvas,
                        (size_t)canvas_width * canvas_height * sizeof(*canvas));
                    frame_placement = frozen_placement;
                    extension_active = 1;
                } else if (extension_active) {
                    uint64_t stage_started = SDL_GetPerformanceCounter();
                    fe8_render_extended_move_range(
                        &render_memory, &snapshot, viewport, canvas,
                        canvas_width);
                    rendered_map_sprites = fe8_render_extended_units(
                        &render_memory, &snapshot, viewport, canvas,
                        canvas_width, frame_count);
                    perf.units += SDL_GetPerformanceCounter() - stage_started;
                    if (frame_compatible) {
                        size_t pixels = (size_t)canvas_width * canvas_height;
                        if (frozen_canvas_pixels < pixels) {
                            Fe8HostPixel *resized = realloc(frozen_canvas,
                                pixels * sizeof(*frozen_canvas));
                            if (resized) {
                                frozen_canvas = resized;
                                frozen_canvas_pixels = pixels;
                            }
                        }
                        if (frozen_canvas_pixels >= pixels) {
                            memcpy(frozen_canvas, canvas,
                                pixels * sizeof(*frozen_canvas));
                            frozen_canvas_width = canvas_width;
                            frozen_canvas_height = canvas_height;
                            frozen_placement = frame_placement;
                            frozen_valid = 1;
                        }
                    }
                }
                if (previous_state != presentation.state)
                    fprintf(stderr, "Extended renderer %s (visual match %u%%)\n",
                        presentation.state == FE8_PRESENTATION_LIVE ? "active" :
                        presentation.state == FE8_PRESENTATION_FROZEN ? "frozen" :
                        "inactive", frame_placement.match_percent);
                if (previous_state == FE8_PRESENTATION_LIVE &&
                        presentation.state == FE8_PRESENTATION_FROZEN) {
                    fe8_mouse_cancel(&mouse);
                    pan.dragging = 0;
                    pointer_canvas_valid = 0;
                    pointer_tile_valid = 0;
                }
                if (options.terrain_capture_path && !terrain_capture_saved &&
                        frame_count >= options.capture_after) {
                    if (!save_canvas_bmp(options.terrain_capture_path,
                            canvas, canvas_width, canvas_height))
                        fprintf(stderr, "Unable to save terrain capture '%s': %s\n",
                            options.terrain_capture_path, SDL_GetError());
                    else
                        fprintf(stderr,
                            "Saved terrain capture: %s (map=%ux%u, rows=%08X, config=%08X, palette-banks=%04X/%04X)\n",
                            options.terrain_capture_path, snapshot.map_width,
                            snapshot.map_height, snapshot.base_tile_rows,
                            map_state.tileset_config, palette_mapping.valid_mask[0],
                            palette_mapping.valid_mask[1]);
                    terrain_capture_saved = 1;
                }
            }
            previous_camera_x = snapshot.camera_x;
            previous_camera_y = snapshot.camera_y;
            previous_camera_valid = 1;
        } else {
            previous_camera_valid = 0;
            if (frozen_valid &&
                    (presentation.state == FE8_PRESENTATION_LIVE ||
                     presentation.state == FE8_PRESENTATION_FROZEN) &&
                    frozen_canvas_width == canvas_width &&
                    frozen_canvas_height == canvas_height) {
                fe8_presentation_update(&presentation, true, false, false);
                visual_profile_active = 0;
                memcpy(canvas, frozen_canvas,
                    (size_t)canvas_width * canvas_height * sizeof(*canvas));
                frame_placement = frozen_placement;
                extension_active = 1;
            }
        }
        if (!extension_active) {
            size_t index;
            for (index = 0; index < (size_t)canvas_width * canvas_height; ++index)
                canvas[index] = UINT32_C(0xFF101418);
            if (family_match && !reported_profile) {
                fprintf(stderr, "Extended renderer inactive: no validated tactical-map state\n");
                reported_profile = 1;
            }
        }
        /* The emulated frame is authoritative for all UI, overlays, moving
         * sprites, and selected units. While the extended world is active it
         * follows the movable viewport, corrected during camera motion for
         * FE8's one-frame PPU scroll delay. A fixed center creates seams and
         * an apparent one-tile mouse offset whenever the map is centered or
         * panned. Outside a validated map, retain the normal centered frame. */
        composite_framebuffer(host_frame, GBA_WIDTH, canvas,
            canvas_width, canvas_height,
            extension_active ? frame_placement.x : gba_x,
            extension_active ? frame_placement.y : gba_y);
        if (inventory_ui.active) {
            int window_x;
            int window_y;
            int index = -1;
            Fe8InventoryHitKind hit = FE8_INVENTORY_HIT_NONE;
            /* Re-evaluate stationary pointers after scrolling, sorting, unit
               changes and resize too. A motion-only inspector leaves stale
               help (or a stale highlight) under a changed layout. */
            inventory_ui.desktop_scale = inventory_point_scale(&video);
            SDL_GetMouseState(&window_x, &window_y);
            host_pointer_visible = SDL_GetMouseFocus() == video.window &&
                fe8_host_video_window_to_canvas(&video, window_x, window_y,
                    &host_pointer_canvas_x, &host_pointer_canvas_y);
            if (host_pointer_visible)
                hit = fe8_inventory_ui_hit_test(&inventory_ui, &inventory_snapshot,
                    canvas_width, canvas_height, host_pointer_canvas_x,
                    host_pointer_canvas_y, &index);
            fe8_inventory_ui_inspect(&inventory_ui, &inventory_snapshot, hit, index);
            fe8_inventory_ui_draw(&inventory_ui, &inventory_snapshot,
                canvas, canvas_width, canvas_width, canvas_height);
        }
        if (inventory_ui.active) {
            SDL_ShowCursor(SDL_ENABLE);
            system_cursor_hidden = 0;
        }
        if (settings.mouse_enabled && !inventory_ui.active && host_pointer_visible)
            fe8_host_draw_mouse_cursor(canvas, canvas_width,
                canvas_width, canvas_height,
                host_pointer_canvas_x, host_pointer_canvas_y);
        {
            uint64_t stage_started = SDL_GetPerformanceCounter();
            int presented = fe8_host_video_present(&video, canvas);
            perf.presentation += SDL_GetPerformanceCounter() - stage_started;
            ++perf.presented_frames;
            if (!presented) {
            fprintf(stderr, "Video presentation failed: %s\n", SDL_GetError());
            running = 0;
            }
        }
        if (large_map_ready && options.state_out_path && !state_out_saved) {
            if (save_state(core, options.state_out_path)) {
                fprintf(stderr, "Saved large-map state: %s\n", options.state_out_path);
                state_out_saved = 1;
            } else {
                fprintf(stderr, "Unable to save large-map state: %s\n", options.state_out_path);
                state_out_saved = 1;
            }
        }
        if (options.capture_path &&
                ((!options.seek_large_map &&
                    (frame_count >= options.capture_after ||
                     (inventory_ui.active && perf.presented_frames >= options.capture_after))) ||
                 (options.seek_large_map && large_map_ready) ||
                 (options.seek_large_map && frame_count >= 3600))) {
            if (!save_canvas_bmp(
                    options.capture_path, canvas, canvas_width, canvas_height))
                fprintf(stderr, "Unable to save capture '%s': %s\n", options.capture_path, SDL_GetError());
            else
                fprintf(stderr, "Saved capture: %s (extended=%s, map=%ux%u, sprites=%u)\n",
                    options.capture_path, extension_active ? "yes" : "no",
                    snapshot_valid ? snapshot.map_width : 0,
                    snapshot_valid ? snapshot.map_height : 0,
                    rendered_map_sprites);
            if (snapshot_valid)
                fprintf(stderr, "Final FE8 cursor: %u,%u\n", snapshot.cursor_x, snapshot.cursor_y);
            running = 0;
        }
        if (options.terrain_capture_path && terrain_capture_saved &&
                !options.capture_path && !options.realtime)
            running = 0;
        if ((!options.capture_path || options.realtime) && running) {
            unsigned speedup_multiplier = speed_up_active ?
                fe8_host_speedup_multiplier(settings.speedup_rate) : 1;
            if (speedup_multiplier != 0)
                pace_frame(&frame_deadline, frame_period, performance_frequency);
        }
    }
    exit_code = EXIT_SUCCESS;

cleanup:
    if (options.perf_stats && perf.started)
        print_perf_stats(&perf, performance_frequency);
    if (audio_initialized)
        fe8_host_audio_deinit(&audio);
    if (system_cursor_hidden)
        SDL_ShowCursor(SDL_ENABLE);
    fe8_host_video_deinit(&video);
    if (sdl_initialized)
        SDL_Quit();
    if (core_initialized)
        core->deinit(core);
    mLogSetDefaultLogger(NULL);
    if (logger.d.filter)
        mStandardLoggerDeinit(&logger);
    free(canvas);
    free(frozen_canvas);
    fe8_terrain_cache_destroy(terrain_cache);
    free(host_frame);
    free(video_buffer);
    return exit_code;
}
