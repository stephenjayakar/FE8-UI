#include <SDL.h>

#include <mgba/core/core.h>
#include <mgba/core/interface.h>
#include <mgba/core/serialize.h>
#include <mgba-util/vfs.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FE8_WIDTH 240u
#define FE8_HEIGHT 160u
#define WINDOW_WIDTH (FE8_WIDTH * 2u)
#define WINDOW_HEIGHT (FE8_HEIGHT * 2u)

/* GBA keypad bit positions used by mCore::setKeys. */
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
};

static void usage(const char *program) {
    fprintf(stderr, "Usage: %s --rom FE8.gba [--state STATE.ss] [--save FE8.sav]\n", program);
}

static int parse_options(int argc, char **argv, struct fe8_options *options) {
    int i;
    memset(options, 0, sizeof(*options));

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--rom") == 0) {
            if (++i >= argc) {
                return 0;
            }
            options->rom_path = argv[i];
        } else if (strcmp(argv[i], "--state") == 0) {
            if (++i >= argc) {
                return 0;
            }
            options->state_path = argv[i];
        } else if (strcmp(argv[i], "--save") == 0) {
            if (++i >= argc) {
                return 0;
            }
            options->save_path = argv[i];
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            return 0;
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            return 0;
        }
    }

    return options->rom_path != NULL;
}

static uint32_t key_for_scancode(SDL_Scancode scancode) {
    switch (scancode) {
    case SDL_SCANCODE_Z: return 1u << FE8_KEY_A;
    case SDL_SCANCODE_X: return 1u << FE8_KEY_B;
    case SDL_SCANCODE_BACKSPACE: return 1u << FE8_KEY_SELECT;
    case SDL_SCANCODE_RETURN: return 1u << FE8_KEY_START;
    case SDL_SCANCODE_RIGHT: return 1u << FE8_KEY_RIGHT;
    case SDL_SCANCODE_LEFT: return 1u << FE8_KEY_LEFT;
    case SDL_SCANCODE_UP: return 1u << FE8_KEY_UP;
    case SDL_SCANCODE_DOWN: return 1u << FE8_KEY_DOWN;
    case SDL_SCANCODE_S: return 1u << FE8_KEY_R;
    case SDL_SCANCODE_A: return 1u << FE8_KEY_L;
    default: return 0;
    }
}

static void update_keys(struct mCore *core, uint32_t *keys, const SDL_Event *event) {
    uint32_t bit;
    if (event->type != SDL_KEYDOWN && event->type != SDL_KEYUP) {
        return;
    }
    if (event->key.repeat) {
        return;
    }

    bit = key_for_scancode(event->key.keysym.scancode);
    if (!bit) {
        return;
    }
    if (event->type == SDL_KEYDOWN) {
        *keys |= bit;
    } else {
        *keys &= ~bit;
    }
    core->setKeys(core, *keys);
}

static void convert_framebuffer(const color_t *source, uint8_t *destination,
                               unsigned width, unsigned height, size_t stride) {
    unsigned y;
    for (y = 0; y < height; ++y) {
        unsigned x;
        const color_t *source_row = source + y * stride;
        uint8_t *destination_row = destination + y * width * 4u;
        for (x = 0; x < width; ++x) {
            color_t pixel = source_row[x];
#ifdef COLOR_16_BIT
            destination_row[x * 4u + 0u] = (uint8_t) M_R8(pixel);
            destination_row[x * 4u + 1u] = (uint8_t) M_G8(pixel);
            destination_row[x * 4u + 2u] = (uint8_t) M_B8(pixel);
#else
            destination_row[x * 4u + 0u] = (uint8_t) (pixel & 0xffu);
            destination_row[x * 4u + 1u] = (uint8_t) ((pixel >> 8) & 0xffu);
            destination_row[x * 4u + 2u] = (uint8_t) ((pixel >> 16) & 0xffu);
#endif
            destination_row[x * 4u + 3u] = 0xffu;
        }
    }
}

/* Hook point: replace this upload/present path with an exterior renderer. */
static int present_frame(SDL_Renderer *renderer, SDL_Texture *texture,
                         const uint8_t *rgba, unsigned width, unsigned height) {
    int window_width;
    int window_height;
    int scale;
    SDL_Rect destination;

    if (SDL_UpdateTexture(texture, NULL, rgba, (int)(width * 4u)) != 0) {
        fprintf(stderr, "SDL_UpdateTexture failed: %s\n", SDL_GetError());
        return 0;
    }
    SDL_GetRendererOutputSize(renderer, &window_width, &window_height);
    scale = window_width / (int)width;
    if (window_height / (int)height < scale) {
        scale = window_height / (int)height;
    }
    if (scale < 1) {
        scale = 1;
    }
    destination.w = (int)width * scale;
    destination.h = (int)height * scale;
    destination.x = (window_width - destination.w) / 2;
    destination.y = (window_height - destination.h) / 2;

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, &destination);
    SDL_RenderPresent(renderer);
    return 1;
}

static int load_state(struct mCore *core, const char *path) {
    struct VFile *state;
    int success;

    state = VFileOpen(path, O_RDONLY);
    if (!state) {
        fprintf(stderr, "Unable to open state '%s': %s\n", path, strerror(errno));
        return 0;
    }
    success = mCoreLoadStateNamed(core, state, SAVESTATE_ALL);
    state->close(state);
    if (!success) {
        fprintf(stderr, "Unable to load mGBA state '%s'\n", path);
    }
    return success;
}

int main(int argc, char **argv) {
    struct fe8_options options;
    struct mCore *core = NULL;
    color_t *video_buffer = NULL;
    uint8_t *rgba_buffer = NULL;
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *texture = NULL;
    unsigned width = 0;
    unsigned height = 0;
    size_t stride;
    uint32_t keys = 0;
    int running = 1;
    int exit_code = EXIT_FAILURE;

    if (!parse_options(argc, argv, &options)) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    core = mCoreFind(options.rom_path);
    if (!core) {
        fprintf(stderr, "mGBA could not identify ROM '%s'\n", options.rom_path);
        goto cleanup;
    }
    if (!core->init(core)) {
        fprintf(stderr, "mGBA core initialization failed\n");
        goto cleanup;
    }
    if (!mCoreLoadFile(core, options.rom_path)) {
        fprintf(stderr, "mGBA could not load ROM '%s'\n", options.rom_path);
        goto cleanup_core;
    }

    core->desiredVideoDimensions(core, &width, &height);
    if (width != FE8_WIDTH || height != FE8_HEIGHT) {
        fprintf(stderr, "Unexpected GBA framebuffer dimensions: %ux%u\n", width, height);
        goto cleanup_core;
    }
    stride = width;
    video_buffer = (color_t *)calloc((size_t)height * stride, sizeof(*video_buffer));
    rgba_buffer = (uint8_t *)malloc((size_t)width * height * 4u);
    if (!video_buffer || !rgba_buffer) {
        fprintf(stderr, "Unable to allocate the %ux%u framebuffer\n", width, height);
        goto cleanup_core;
    }
    core->setVideoBuffer(core, video_buffer, stride);

    if (options.save_path && !mCoreLoadSaveFile(core, options.save_path, false)) {
        fprintf(stderr, "Warning: unable to load save file '%s'\n", options.save_path);
    }
    core->reset(core);
    if (options.state_path && !load_state(core, options.state_path)) {
        goto cleanup_core;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        goto cleanup_core;
    }
    window = SDL_CreateWindow("FE8 mGBA prototype", SDL_WINDOWPOS_CENTERED,
                             SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH, WINDOW_HEIGHT,
                             SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED |
                                           SDL_RENDERER_PRESENTVSYNC) : NULL;
    texture = renderer ? SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                           SDL_TEXTUREACCESS_STREAMING, (int)width,
                                           (int)height) : NULL;
    if (!window || !renderer || !texture) {
        fprintf(stderr, "SDL setup failed: %s\n", SDL_GetError());
        goto cleanup_sdl;
    }
    SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);

    SDL_SetWindowMinimumSize(window, (int)WINDOW_WIDTH, (int)WINDOW_HEIGHT);
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = 0;
            } else if (event.type == SDL_KEYDOWN &&
                       event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                running = 0;
            } else {
                update_keys(core, &keys, &event);
            }
        }

        core->setKeys(core, keys);
        core->runFrame(core);
        convert_framebuffer(video_buffer, rgba_buffer, width, height, stride);
        if (!present_frame(renderer, texture, rgba_buffer, width, height)) {
            running = 0;
        }
    }

    exit_code = EXIT_SUCCESS;

cleanup_sdl:
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
cleanup_core:
    core->deinit(core);
cleanup:
    free(rgba_buffer);
    free(video_buffer);
    return exit_code;
}
