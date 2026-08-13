#include "host_video.h"
#include "pointer_mapping.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Fe8HostVideoSdl {
    SDL_Renderer *renderer;
    SDL_Texture *texture;
} Fe8HostVideoSdl;

int fe8_host_video_init(Fe8HostVideo *video, const char *title,
    int canvas_width, int canvas_height, int vsync_enabled) {
    Fe8HostVideoSdl *backend;
    memset(video, 0, sizeof(*video));
    video->window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, canvas_width * 2, canvas_height * 2,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_MAXIMIZED);
    backend = calloc(1, sizeof(*backend));
    if (!video->window || !backend)
        return 0;
    video->backend = backend;
    backend->renderer = SDL_CreateRenderer(video->window, -1,
        SDL_RENDERER_ACCELERATED | (vsync_enabled ? SDL_RENDERER_PRESENTVSYNC : 0));
    backend->texture = backend->renderer ? SDL_CreateTexture(backend->renderer,
        SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
        canvas_width, canvas_height) : NULL;
    if (!backend->renderer || !backend->texture ||
            SDL_RenderSetLogicalSize(backend->renderer,
                canvas_width, canvas_height) != 0)
        return 0;
    SDL_SetTextureScaleMode(backend->texture, SDL_ScaleModeNearest);
    SDL_RenderSetIntegerScale(backend->renderer, SDL_FALSE);
    video->canvas_width = canvas_width;
    video->canvas_height = canvas_height;
    fe8_host_video_set_vsync(video, vsync_enabled);
    return 1;
}

int fe8_host_video_set_vsync(Fe8HostVideo *video, int enabled) {
    Fe8HostVideoSdl *backend = video ? video->backend : NULL;
    if (!backend || SDL_RenderSetVSync(backend->renderer, enabled ? 1 : 0) != 0)
        return 0;
    video->vsync_active = enabled != 0;
    return 1;
}

int fe8_host_video_set_shader(Fe8HostVideo *video, enum Fe8HostShader shader) {
    if (!video || shader != FE8_HOST_SHADER_OFF)
        return 0;
    video->shader = shader;
    return 1;
}

int fe8_host_video_present(Fe8HostVideo *video, const void *pixels) {
    Fe8HostVideoSdl *backend = video ? video->backend : NULL;
    if (!backend || SDL_UpdateTexture(backend->texture, NULL, pixels,
            video->canvas_width * 4) != 0)
        return 0;
    SDL_SetRenderDrawColor(backend->renderer, 8, 10, 12, 255);
    SDL_RenderClear(backend->renderer);
    SDL_RenderCopy(backend->renderer, backend->texture, NULL, NULL);
    SDL_RenderPresent(backend->renderer);
    return 1;
}

int fe8_host_video_window_to_canvas(const Fe8HostVideo *video,
    int window_x, int window_y, int *canvas_x, int *canvas_y) {
    const Fe8HostVideoSdl *backend = video ? video->backend : NULL;
    int window_width;
    int window_height;
    int output_width;
    int output_height;
    if (!backend || !backend->renderer)
        return 0;
    SDL_GetWindowSize(video->window, &window_width, &window_height);
    SDL_GetRendererOutputSize(backend->renderer, &output_width, &output_height);
    return fe8_pointer_window_to_canvas(window_width, window_height,
        output_width, output_height, video->canvas_width, video->canvas_height,
        window_x, window_y, canvas_x, canvas_y);
}

void fe8_host_video_log_status(const Fe8HostVideo *video) {
    const Fe8HostVideoSdl *backend = video ? video->backend : NULL;
    int window_width;
    int window_height;
    int output_width;
    int output_height;
    if (!backend)
        return;
    SDL_GetWindowSize(video->window, &window_width, &window_height);
    SDL_GetRendererOutputSize(backend->renderer, &output_width, &output_height);
    fprintf(stderr, "Display: window=%dx%d output=%dx%d logical=%dx%d backend=SDL\n",
        window_width, window_height, output_width, output_height,
        video->canvas_width, video->canvas_height);
}

void fe8_host_video_deinit(Fe8HostVideo *video) {
    Fe8HostVideoSdl *backend = video ? video->backend : NULL;
    if (!video)
        return;
    if (backend) {
        SDL_DestroyTexture(backend->texture);
        SDL_DestroyRenderer(backend->renderer);
        free(backend);
    }
    SDL_DestroyWindow(video->window);
    memset(video, 0, sizeof(*video));
}
