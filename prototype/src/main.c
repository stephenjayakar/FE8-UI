#include <SDL.h>

#include <mgba/flags.h>
#include <mgba/core/core.h>
#include <mgba/core/interface.h>
#include <mgba/core/log.h>
#include <mgba/core/serialize.h>
#include <mgba-util/image.h>
#include <mgba-util/vfs.h>

#include "extended_map_renderer.h"
#include "extended_unit_renderer.h"
#include "fe8_profile.h"

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

enum fe8_key {
    FE8_KEY_A = 0,
    FE8_KEY_B = 1,
    FE8_KEY_SELECT = 2,
    FE8_KEY_START = 3,
    FE8_KEY_RIGHT = 4,
    FE8_KEY_LEFT = 5,
    FE8_KEY_UP = 6,
    FE8_KEY_DOWN = 7,
    FE8_KEY_R = 8,
    FE8_KEY_L = 9,
};

struct fe8_options {
    const char *rom_path;
    const char *state_path;
    const char *save_path;
    const char *capture_path;
    const char *state_out_path;
    int extensions;
    unsigned capture_after;
    int auto_continue;
    int seek_large_map;
    int realtime;
    int mouse_target_set;
    int mouse_confirm;
    int mouse_target_x;
    int mouse_target_y;
};

struct mouse_controller {
    int active;
    int target_x;
    int target_y;
    int confirm;
    uint32_t pulse_key;
    int press_frames;
    int release_frames;
    int wait_frames;
    int issued_x;
    int issued_y;
    int retries;
    int blocked_frames;
};

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

static void usage(const char *program) {
    fprintf(stderr,
        "Usage: %s --rom GAME.gba [--state STATE.ss] [--save GAME.sav]\n"
        "       [--capture OUTPUT.bmp] [--capture-after FRAMES]\n"
        "       [--auto-continue] [--seek-large-map] [--mouse-target X,Y]\n"
        "       [--mouse-click X,Y]\n"
        "       [--state-out MAP_STATE.ss] [--realtime] [--no-extensions]\n", program);
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
        else if (strcmp(argv[i], "--state-out") == 0)
            destination = &options->state_out_path;
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
        } else if (strcmp(argv[i], "--mouse-target") == 0 ||
                strcmp(argv[i], "--mouse-click") == 0) {
            char *comma;
            char *end;
            options->mouse_confirm = strcmp(argv[i], "--mouse-click") == 0;
            if (++i >= argc)
                return 0;
            comma = strchr(argv[i], ',');
            if (!comma)
                return 0;
            options->mouse_target_x = (int)strtol(argv[i], &end, 10);
            if (end != comma)
                return 0;
            options->mouse_target_y = (int)strtol(comma + 1, &end, 10);
            if (*end)
                return 0;
            options->mouse_target_set = 1;
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

static uint32_t key_for_scancode(SDL_Scancode scancode) {
    switch (scancode) {
    case SDL_SCANCODE_Z: return UINT32_C(1) << FE8_KEY_A;
    case SDL_SCANCODE_X: return UINT32_C(1) << FE8_KEY_B;
    case SDL_SCANCODE_BACKSPACE: return UINT32_C(1) << FE8_KEY_SELECT;
    case SDL_SCANCODE_RETURN: return UINT32_C(1) << FE8_KEY_START;
    case SDL_SCANCODE_RIGHT: return UINT32_C(1) << FE8_KEY_RIGHT;
    case SDL_SCANCODE_LEFT: return UINT32_C(1) << FE8_KEY_LEFT;
    case SDL_SCANCODE_UP: return UINT32_C(1) << FE8_KEY_UP;
    case SDL_SCANCODE_DOWN: return UINT32_C(1) << FE8_KEY_DOWN;
    case SDL_SCANCODE_S: return UINT32_C(1) << FE8_KEY_R;
    case SDL_SCANCODE_A: return UINT32_C(1) << FE8_KEY_L;
    default: return 0;
    }
}

static void update_keyboard(uint32_t *keys, const SDL_Event *event) {
    uint32_t bit;
    if ((event->type != SDL_KEYDOWN && event->type != SDL_KEYUP) || event->key.repeat)
        return;
    bit = key_for_scancode(event->key.keysym.scancode);
    if (!bit)
        return;
    if (event->type == SDL_KEYDOWN)
        *keys |= bit;
    else
        *keys &= ~bit;
}

static uint8_t core_read8(void *context, uint32_t address) {
    struct mCore *core = context;
    return core->busRead8(core, address);
}

static void composite_framebuffer(
    const mColor *source, size_t source_stride, Fe8HostPixel *canvas,
    int destination_x, int destination_y) {
    unsigned y;
    for (y = 0; y < GBA_HEIGHT; ++y) {
        unsigned x;
        const mColor *source_row = source + y * source_stride;
        for (x = 0; x < GBA_WIDTH; ++x) {
            int canvas_x = destination_x + (int)x;
            int canvas_y = destination_y + (int)y;
            mColor pixel = source_row[x];
#ifdef COLOR_16_BIT
            uint32_t red = M_R8(pixel);
            uint32_t green = M_G8(pixel);
            uint32_t blue = M_B8(pixel);
#else
            uint32_t red = pixel & 0xFF;
            uint32_t green = (pixel >> 8) & 0xFF;
            uint32_t blue = (pixel >> 16) & 0xFF;
#endif
            if (canvas_x >= 0 && canvas_y >= 0 &&
                    canvas_x < CANVAS_WIDTH && canvas_y < CANVAS_HEIGHT)
                canvas[(size_t)canvas_y * CANVAS_WIDTH + canvas_x] =
                    UINT32_C(0xFF000000) | (blue << 16) | (green << 8) | red;
        }
    }
}

static int present_frame(
    SDL_Renderer *renderer, SDL_Texture *texture, const Fe8HostPixel *canvas) {
    if (SDL_UpdateTexture(texture, NULL, canvas, CANVAS_WIDTH * (int)sizeof(*canvas)) != 0)
        return 0;
    SDL_SetRenderDrawColor(renderer, 8, 10, 12, 255);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
    return 1;
}

static int window_to_canvas(
    SDL_Renderer *renderer, int window_x, int window_y, int *canvas_x, int *canvas_y) {
    float logical_x;
    float logical_y;
    SDL_RenderWindowToLogical(renderer, window_x, window_y, &logical_x, &logical_y);
    if (logical_x < 0 || logical_y < 0 || logical_x >= CANVAS_WIDTH || logical_y >= CANVAS_HEIGHT)
        return 0;
    *canvas_x = (int)logical_x;
    *canvas_y = (int)logical_y;
    return 1;
}

static int clamp_int(int value, int minimum, int maximum) {
    if (value < minimum)
        return minimum;
    if (value > maximum)
        return maximum;
    return value;
}

static void clamp_pan(
    struct pan_controller *pan, const Fe8Snapshot *snapshot,
    Fe8ExtendedViewport *viewport) {
    int base_x = snapshot->camera_x - GBA_X;
    int base_y = snapshot->camera_y - GBA_Y;
    int map_width = snapshot->map_width * 16;
    int map_height = snapshot->map_height * 16;
    int origin_x = base_x + pan->x;
    int origin_y = base_y + pan->y;
    if (map_width <= CANVAS_WIDTH)
        origin_x = (map_width - CANVAS_WIDTH) / 2;
    else
        origin_x = clamp_int(origin_x, 0, map_width - CANVAS_WIDTH);
    if (map_height <= CANVAS_HEIGHT)
        origin_y = (map_height - CANVAS_HEIGHT) / 2;
    else
        origin_y = clamp_int(origin_y, 0, map_height - CANVAS_HEIGHT);
    pan->x = origin_x - base_x;
    pan->y = origin_y - base_y;
    viewport->gba_x = GBA_X - pan->x;
    viewport->gba_y = GBA_Y - pan->y;
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
    struct VFile *save = VFileOpen(path, O_RDWR);
    if (!save)
        return 0;
    if (!core->loadSave(core, save)) {
        save->close(save);
        return 0;
    }
    return 1;
}

static int save_canvas_bmp(const char *path, Fe8HostPixel *canvas) {
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(
        canvas, CANVAS_WIDTH, CANVAS_HEIGHT, 32,
        CANVAS_WIDTH * (int)sizeof(*canvas), SDL_PIXELFORMAT_RGBA32);
    int result;
    if (!surface)
        return 0;
    result = SDL_SaveBMP(surface, path) == 0;
    SDL_FreeSurface(surface);
    return result;
}

static uint32_t update_mouse_controller(
    struct mouse_controller *mouse, const Fe8Snapshot *snapshot, int snapshot_valid) {
    uint32_t result = 0;
    if (!snapshot_valid) {
        mouse->active = 0;
        mouse->press_frames = 0;
        mouse->release_frames = 0;
        mouse->wait_frames = 0;
        mouse->blocked_frames = 0;
        return 0;
    }
    if (snapshot->input_lock != 0) {
        if (++mouse->blocked_frames > 300) {
            fprintf(stderr, "Mouse path cancelled: FE8 input remained locked\n");
            mouse->active = 0;
            mouse->press_frames = 0;
            mouse->release_frames = 0;
            mouse->wait_frames = 0;
        }
        return 0;
    }
    mouse->blocked_frames = 0;
    if (mouse->press_frames > 0) {
        --mouse->press_frames;
        return mouse->pulse_key;
    }
    if (mouse->release_frames > 0) {
        --mouse->release_frames;
        return 0;
    }
    if (!mouse->active)
        return 0;
    if (mouse->wait_frames > 0) {
        if (snapshot->cursor_x != mouse->issued_x || snapshot->cursor_y != mouse->issued_y) {
            mouse->wait_frames = 0;
            mouse->release_frames = 2;
            mouse->retries = 0;
            return 0;
        }
        --mouse->wait_frames;
        if (mouse->wait_frames > 0)
            return 0;
        if (++mouse->retries > 3) {
            fprintf(stderr, "Mouse path cancelled: FE8 cursor did not acknowledge input\n");
            mouse->active = 0;
            return 0;
        }
    }
    if (snapshot->cursor_x == mouse->target_x && snapshot->cursor_y == mouse->target_y) {
        mouse->active = 0;
        if (!mouse->confirm)
            return 0;
        result = UINT32_C(1) << FE8_KEY_A;
        fprintf(stderr, "Mouse confirm: A at %d,%d\n", mouse->target_x, mouse->target_y);
    } else if (snapshot->cursor_x < mouse->target_x)
        result = UINT32_C(1) << FE8_KEY_RIGHT;
    else if (snapshot->cursor_x > mouse->target_x)
        result = UINT32_C(1) << FE8_KEY_LEFT;
    else if (snapshot->cursor_y < mouse->target_y)
        result = UINT32_C(1) << FE8_KEY_DOWN;
    else
        result = UINT32_C(1) << FE8_KEY_UP;
    mouse->pulse_key = result;
    mouse->press_frames = 2;
    mouse->wait_frames = 16;
    mouse->issued_x = snapshot->cursor_x;
    mouse->issued_y = snapshot->cursor_y;
    return result;
}

static uint32_t scripted_continue_keys(unsigned frame) {
    struct scripted_press { unsigned frame; uint32_t key; };
    static const struct scripted_press presses[] = {
        {90, UINT32_C(1) << FE8_KEY_A},
        {210, UINT32_C(1) << FE8_KEY_START},
        {330, UINT32_C(1) << FE8_KEY_START},
        {450, UINT32_C(1) << FE8_KEY_A},
        {570, UINT32_C(1) << FE8_KEY_A},
        {690, UINT32_C(1) << FE8_KEY_A},
        {850, UINT32_C(1) << FE8_KEY_START},
        {1000, UINT32_C(1) << FE8_KEY_A},
        {1120, UINT32_C(1) << FE8_KEY_A},
        {1240, UINT32_C(1) << FE8_KEY_A},
    };
    size_t i;
    for (i = 0; i < sizeof(presses) / sizeof(presses[0]); ++i)
        if (frame >= presses[i].frame && frame < presses[i].frame + 3)
            return presses[i].key;
    if (frame >= 1420 && frame % 360 < 3)
        return UINT32_C(1) << FE8_KEY_A;
    if (frame >= 1420 && frame % 360 >= 180 && frame % 360 < 183)
        return UINT32_C(1) << FE8_KEY_START;
    return 0;
}

int main(int argc, char **argv) {
    struct fe8_options options;
    struct mCore *core = NULL;
    const Fe8Profile *profile = fe8u_profile();
    Fe8MemoryReader profile_memory;
    Fe8MemoryView render_memory;
    Fe8Snapshot snapshot;
    Fe8MapRenderState map_state = {0};
    Fe8ExtendedViewport viewport = {CANVAS_WIDTH, CANVAS_HEIGHT, GBA_X, GBA_Y};
    struct mouse_controller mouse = {0};
    struct pan_controller pan = {0};
    mColor *video_buffer = NULL;
    Fe8HostPixel *canvas = NULL;
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *texture = NULL;
    uint32_t keyboard_keys = 0;
    size_t video_stride = GBA_WIDTH;
    unsigned width = 0;
    unsigned height = 0;
    int running = 1;
    int core_initialized = 0;
    int sdl_initialized = 0;
    int family_match = 0;
    int snapshot_valid = 0;
    int extension_active = 0;
    unsigned rendered_units = 0;
    int reported_profile = 0;
    int injected_mouse_target = 0;
    unsigned frame_count = 0;
    unsigned large_map_ready_frames = 0;
    int large_map_ready = 0;
    int state_out_saved = 0;
    int native_frame_visible = 0;
    unsigned native_frame_until = 0;
    uint64_t frame_deadline = 0;
    uint64_t frame_period = 0;
    uint64_t performance_frequency = 0;
    int exit_code = EXIT_FAILURE;
    struct mStandardLogger logger = {0};

    if (!parse_options(argc, argv, &options)) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
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
    canvas = malloc((size_t)CANVAS_WIDTH * CANVAS_HEIGHT * sizeof(*canvas));
    if (!video_buffer || !canvas)
        goto cleanup;
    core->setVideoBuffer(core, video_buffer, video_stride);
    if (options.save_path && !load_save(core, options.save_path))
        fprintf(stderr, "Warning: unable to load save '%s'\n", options.save_path);
    core->reset(core);
    fprintf(stderr, "Core reset complete (state bytes=%zu)\n", core->stateSize(core));
    if (options.state_path && !load_state(core, options.state_path))
        goto cleanup;

    profile_memory.context = core;
    profile_memory.read8 = core_read8;
    render_memory.context = core;
    render_memory.read8 = core_read8;
    family_match = options.extensions && fe8_detect_fe8u_family(&profile_memory);
    fprintf(stderr, "FE8 extensions: %s\n", family_match ?
        (fe8_detect_retail_fe8u(&profile_memory) ? "retail-layout profile" : "FE8U-family structural profile") :
        "disabled (unknown ROM or --no-extensions)");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        goto cleanup;
    }
    sdl_initialized = 1;
    window = SDL_CreateWindow("FE8 extended-map prototype", SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, CANVAS_WIDTH * 2, CANVAS_HEIGHT * 2,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_MAXIMIZED);
    renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED) : NULL;
    texture = renderer ? SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
        SDL_TEXTUREACCESS_STREAMING, CANVAS_WIDTH, CANVAS_HEIGHT) : NULL;
    if (!window || !renderer || !texture) {
        fprintf(stderr, "SDL setup failed: %s\n", SDL_GetError());
        goto cleanup;
    }
    SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);
    if (SDL_RenderSetLogicalSize(renderer, CANVAS_WIDTH, CANVAS_HEIGHT) != 0) {
        fprintf(stderr, "Unable to configure aspect-preserving logical canvas: %s\n", SDL_GetError());
        goto cleanup;
    }
    SDL_RenderSetIntegerScale(renderer, SDL_FALSE);
    performance_frequency = SDL_GetPerformanceFrequency();
    frame_period = (uint64_t)((double)performance_frequency * core->frameCycles(core) /
        core->frequency(core) + 0.5);
    frame_deadline = SDL_GetPerformanceCounter();
    {
        int window_width;
        int window_height;
        int output_width;
        int output_height;
        float logical_x;
        float logical_y;
        SDL_GetWindowSize(window, &window_width, &window_height);
        SDL_GetRendererOutputSize(renderer, &output_width, &output_height);
        SDL_RenderWindowToLogical(renderer, window_width / 2, window_height / 2,
            &logical_x, &logical_y);
        fprintf(stderr,
            "Display: window=%dx%d output=%dx%d center=%.1f,%.1f logical=%dx%d\n",
            window_width, window_height, output_width, output_height,
            logical_x, logical_y, CANVAS_WIDTH, CANVAS_HEIGHT);
        fprintf(stderr, "Frame pacing: %.3f fps (display refresh independent)\n",
            (double)core->frequency(core) / core->frameCycles(core));
    }

    while (running) {
        SDL_Event event;
        snapshot_valid = family_match && fe8_extract_snapshot(&profile_memory, profile, &snapshot);
        if (snapshot_valid) {
            map_state.map_width = snapshot.map_width;
            map_state.map_height = snapshot.map_height;
            map_state.camera_x = snapshot.camera_x;
            map_state.camera_y = snapshot.camera_y;
            clamp_pan(&pan, &snapshot, &viewport);
        }
        if (options.mouse_target_set && snapshot_valid && !injected_mouse_target) {
            mouse.active = 1;
            mouse.target_x = options.mouse_target_x;
            mouse.target_y = options.mouse_target_y;
            mouse.confirm = options.mouse_confirm;
            injected_mouse_target = 1;
            fprintf(stderr, "Mouse-path test: cursor %u,%u -> target %d,%d\n",
                snapshot.cursor_x, snapshot.cursor_y, mouse.target_x, mouse.target_y);
        }
        if (!large_map_ready && snapshot_valid && snapshot.input_lock == 0 &&
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
            if (event.type == SDL_QUIT || (event.type == SDL_KEYDOWN &&
                    event.key.keysym.scancode == SDL_SCANCODE_ESCAPE)) {
                running = 0;
            } else if (event.type == SDL_KEYDOWN && !event.key.repeat &&
                    event.key.keysym.scancode == SDL_SCANCODE_H) {
                native_frame_visible = !native_frame_visible;
                fprintf(stderr, "Native FE8 HUD: %s\n",
                    native_frame_visible ? "visible" : "hidden");
            } else if (event.type == SDL_MOUSEBUTTONDOWN && snapshot_valid) {
                int canvas_x;
                int canvas_y;
                int shift = (SDL_GetModState() & KMOD_SHIFT) != 0;
                if (shift && event.button.button == SDL_BUTTON_LEFT &&
                        window_to_canvas(renderer, event.button.x, event.button.y,
                            &canvas_x, &canvas_y)) {
                    pan.dragging = 1;
                    pan.moved = 0;
                    pan.start_canvas_x = canvas_x;
                    pan.start_canvas_y = canvas_y;
                    pan.start_pan_x = pan.x;
                    pan.start_pan_y = pan.y;
                    mouse.active = 0;
                } else if (event.button.button == SDL_BUTTON_RIGHT) {
                    mouse.pulse_key = UINT32_C(1) << FE8_KEY_B;
                    mouse.press_frames = 2;
                    mouse.active = 0;
                } else if (event.button.button == SDL_BUTTON_LEFT &&
                        window_to_canvas(renderer, event.button.x, event.button.y,
                            &canvas_x, &canvas_y) &&
                        fe8_canvas_to_map_tile(&map_state, viewport, canvas_x, canvas_y,
                            &mouse.target_x, &mouse.target_y)) {
                    mouse.active = 1;
                    mouse.confirm = 1;
                    native_frame_until = frame_count + 180;
                    fprintf(stderr, "Mouse click: target=%d,%d lock=%u\n",
                        mouse.target_x, mouse.target_y, snapshot.input_lock);
                }
            } else if (event.type == SDL_MOUSEBUTTONUP &&
                    event.button.button == SDL_BUTTON_LEFT && pan.dragging) {
                int canvas_x;
                int canvas_y;
                if (!pan.moved && window_to_canvas(renderer, event.button.x, event.button.y,
                        &canvas_x, &canvas_y)) {
                    int map_x;
                    int map_y;
                    if (fe8_canvas_to_map_tile(&map_state, viewport, canvas_x, canvas_y,
                            &map_x, &map_y)) {
                        int desired_origin_x = map_x * 16 + 8 - CANVAS_WIDTH / 2;
                        int desired_origin_y = map_y * 16 + 8 - CANVAS_HEIGHT / 2;
                        pan.x = desired_origin_x - (snapshot.camera_x - GBA_X);
                        pan.y = desired_origin_y - (snapshot.camera_y - GBA_Y);
                    }
                }
                pan.dragging = 0;
                clamp_pan(&pan, &snapshot, &viewport);
            } else if (event.type == SDL_MOUSEMOTION && snapshot_valid) {
                int canvas_x;
                int canvas_y;
                if (!window_to_canvas(renderer, event.motion.x, event.motion.y,
                        &canvas_x, &canvas_y)) {
                    continue;
                }
                if (pan.dragging) {
                    int dx = canvas_x - pan.start_canvas_x;
                    int dy = canvas_y - pan.start_canvas_y;
                    pan.x = pan.start_pan_x - dx;
                    pan.y = pan.start_pan_y - dy;
                    pan.moved = pan.moved || abs(dx) >= 2 || abs(dy) >= 2;
                    clamp_pan(&pan, &snapshot, &viewport);
                } else if ((!mouse.active || !mouse.confirm) && fe8_canvas_to_map_tile(
                        &map_state, viewport, canvas_x, canvas_y,
                        &mouse.target_x, &mouse.target_y)) {
                    if (mouse.target_x != snapshot.cursor_x ||
                            mouse.target_y != snapshot.cursor_y) {
                        mouse.active = 1;
                        mouse.confirm = 0;
                    }
                }
            } else {
                update_keyboard(&keyboard_keys, &event);
            }
        }

        ++frame_count;
        core->setKeys(core, keyboard_keys |
            (options.auto_continue && !large_map_ready ? scripted_continue_keys(frame_count) : 0) |
            update_mouse_controller(&mouse, &snapshot, snapshot_valid));
        core->runFrame(core);
        snapshot_valid = family_match && fe8_extract_snapshot(&profile_memory, profile, &snapshot);
        extension_active = 0;
        rendered_units = 0;
        if (snapshot_valid) {
            clamp_pan(&pan, &snapshot, &viewport);
            map_state.map_width = snapshot.map_width;
            map_state.map_height = snapshot.map_height;
            map_state.camera_x = snapshot.camera_x;
            map_state.camera_y = snapshot.camera_y;
            map_state.base_tile_rows = snapshot.base_tile_rows;
            map_state.fog_rows = snapshot.fog_rows;
            map_state.tileset_config = profile->tileset_config;
            map_state.tile_graphics = UINT32_C(0x06008000);
            map_state.palette = UINT32_C(0x05000000);
            extension_active = fe8_render_extended_terrain(
                &render_memory, &map_state, viewport, canvas, CANVAS_WIDTH);
            if (extension_active)
                rendered_units = fe8_render_extended_units(&render_memory, &snapshot, viewport,
                    canvas, CANVAS_WIDTH, frame_count);
        }
        if (!extension_active) {
            size_t index;
            for (index = 0; index < (size_t)CANVAS_WIDTH * CANVAS_HEIGHT; ++index)
                canvas[index] = UINT32_C(0xFF101418);
            if (family_match && !reported_profile) {
                fprintf(stderr, "Extended renderer inactive: no validated tactical-map state\n");
                reported_profile = 1;
            }
        }
        if (!extension_active || native_frame_visible || frame_count < native_frame_until)
            composite_framebuffer(video_buffer, video_stride, canvas,
                viewport.gba_x, viewport.gba_y);
        if (!present_frame(renderer, texture, canvas)) {
            fprintf(stderr, "SDL presentation failed: %s\n", SDL_GetError());
            running = 0;
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
                ((!options.seek_large_map && frame_count >= options.capture_after) ||
                 (options.seek_large_map && large_map_ready) ||
                 (options.seek_large_map && frame_count >= 3600))) {
            if (!save_canvas_bmp(options.capture_path, canvas))
                fprintf(stderr, "Unable to save capture '%s': %s\n", options.capture_path, SDL_GetError());
            else
                fprintf(stderr, "Saved capture: %s (extended=%s, map=%ux%u, units=%u)\n",
                    options.capture_path, extension_active ? "yes" : "no",
                    snapshot_valid ? snapshot.map_width : 0,
                    snapshot_valid ? snapshot.map_height : 0, rendered_units);
            if (snapshot_valid)
                fprintf(stderr, "Final FE8 cursor: %u,%u\n", snapshot.cursor_x, snapshot.cursor_y);
            running = 0;
        }
        if ((!options.capture_path || options.realtime) && running)
            pace_frame(&frame_deadline, frame_period, performance_frequency);
    }
    exit_code = EXIT_SUCCESS;

cleanup:
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    if (sdl_initialized)
        SDL_Quit();
    if (core_initialized)
        core->deinit(core);
    mLogSetDefaultLogger(NULL);
    if (logger.d.filter)
        mStandardLoggerDeinit(&logger);
    free(canvas);
    free(video_buffer);
    return exit_code;
}
