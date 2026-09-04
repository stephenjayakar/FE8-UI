#include "host_video.h"
#include "pointer_mapping.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Fe8HostVideoSdl {
    SDL_Renderer *renderer;
    SDL_Texture *texture;
} Fe8HostVideoSdl;

static int apply_layout(Fe8HostVideo *video, Fe8HostVideoSdl *backend) {
    SDL_Texture *texture = SDL_CreateTexture(backend->renderer,
        SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
        video->scaling.canvas_width, video->scaling.canvas_height);
    if (!texture)
        return 0;
    SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);
    SDL_DestroyTexture(backend->texture);
    backend->texture = texture;
    video->canvas_width = video->scaling.canvas_width;
    video->canvas_height = video->scaling.canvas_height;
    return SDL_RenderSetLogicalSize(backend->renderer,
        video->canvas_width, video->canvas_height) == 0;
}

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
    if (!backend->renderer)
        return 0;
    SDL_RenderSetIntegerScale(backend->renderer, SDL_FALSE);
    fe8_display_scaling_init(&video->scaling,
        canvas_width, canvas_height, 240, 160);
    video->base_canvas_width = canvas_width;
    video->base_canvas_height = canvas_height;
    {
        int output_width;
        int output_height;
        SDL_GetRendererOutputSize(backend->renderer, &output_width, &output_height);
        fe8_display_scaling_resize(&video->scaling, output_width, output_height);
    }
    if (!apply_layout(video, backend))
        return 0;
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
    if (!backend)
        return 0;
    if (SDL_UpdateTexture(backend->texture, NULL, pixels,
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

int fe8_host_video_event_to_canvas(const Fe8HostVideo *video,
    int event_x, int event_y, int *canvas_x, int *canvas_y) {
    /* The SDL renderer's event filter has already mapped these coordinates
       to its logical canvas. Applying window scaling again shifts clicks
       away from the hovered label after a resize or density change. */
    if (!video || !video->backend || !canvas_x || !canvas_y ||
            event_x < 0 || event_y < 0 ||
            event_x >= video->canvas_width || event_y >= video->canvas_height)
        return 0;
    *canvas_x = event_x;
    *canvas_y = event_y;
    return 1;
}

int fe8_host_video_adjust_zoom(
    Fe8HostVideo *video, double wheel_delta, double sensitivity) {
    Fe8HostVideoSdl *backend = video ? video->backend : NULL;
    if (!backend || !backend->renderer)
        return 0;
    if (!fe8_display_scaling_adjust(
            &video->scaling, wheel_delta, sensitivity))
        return 0;
    return apply_layout(video, backend);
}

int fe8_host_video_refresh_layout(Fe8HostVideo *video) {
    Fe8HostVideoSdl *backend = video ? video->backend : NULL;
    int output_width;
    int output_height;
    if (!backend || !backend->renderer)
        return 0;
    SDL_GetRendererOutputSize(backend->renderer, &output_width, &output_height);
    if (!fe8_display_scaling_resize(
            &video->scaling, output_width, output_height))
        return 0;
    return apply_layout(video, backend);
}

int fe8_host_video_set_content_density(Fe8HostVideo *video, int density) {
    Fe8HostVideoSdl *backend = video ? video->backend : NULL;
    int output_width;
    int output_height;
    if (!backend || density < 1 || density > 3)
        return 0;
    video->scaling.minimum_canvas_width = video->base_canvas_width * density;
    video->scaling.minimum_canvas_height = video->base_canvas_height * density;
    SDL_GetRendererOutputSize(backend->renderer, &output_width, &output_height);
    fe8_display_scaling_resize(&video->scaling, output_width, output_height);
    return apply_layout(video, backend);
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
    fprintf(stderr, "Display: window=%dx%d output=%dx%d logical=%dx%d scale=%.2fx backend=SDL\n",
        window_width, window_height, output_width, output_height,
        video->canvas_width, video->canvas_height, video->scaling.pixel_scale);
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
