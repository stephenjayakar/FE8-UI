#include "native_hud_host.h"
#include "host_cursor.h"

#include <stdio.h>
#include <stdlib.h>

/* Inverse of pointer_mapping's aspect-fit transform. SDL mouse events have
 * already been converted to the logical canvas by the video backend. */
static Fe8HudRect image_rect(const Fe8HostVideo *video) {
    int w = video->scaling.drawable_width, h = video->scaling.drawable_height;
    if ((int64_t)w * video->canvas_height > (int64_t)h * video->canvas_width)
        w = h * video->canvas_width / video->canvas_height;
    else
        h = w * video->canvas_height / video->canvas_width;
    return (Fe8HudRect){(video->scaling.drawable_width - w) / 2,
        (video->scaling.drawable_height - h) / 2, w, h};
}
static void canvas_to_output(const Fe8HostVideo *video, int cx, int cy, int *x, int *y) {
    Fe8HudRect r = image_rect(video);
    *x = r.x + (int)((int64_t)cx * r.width / video->canvas_width);
    *y = r.y + (int)((int64_t)cy * r.height / video->canvas_height);
}
const Fe8HostPixel *fe8_hud_host_update(Fe8HudHost *host,
        const Fe8HostVideo *video, const Fe8MemoryView *memory,
        const Fe8Snapshot *snapshot, const Fe8HostPixel *frame,
        bool live_map, int frame_x, int frame_y) {
    host->overlay.pixels = NULL;
    if (!fe8_native_hud_extract(&host->hud, memory, snapshot, frame,
            FE8_HUD_WIDTH, live_map && host->enabled)) return frame;
    int w = video->scaling.drawable_width, h = video->scaling.drawable_height;
    if (w <= 0 || h <= 0 || (size_t)w > SIZE_MAX / sizeof(*host->pixels) / (size_t)h)
        goto fallback;
    size_t count = (size_t)w * h;
    if (count > host->capacity) {
        Fe8HostPixel *pixels = realloc(host->pixels, count * sizeof(*pixels));
        if (!pixels) goto fallback;
        host->pixels = pixels; host->capacity = count;
    }
    int x, y;
    canvas_to_output(video, frame_x + snapshot->cursor_display_x - snapshot->camera_x + 8,
        frame_y + snapshot->cursor_display_y - snapshot->camera_y + 8, &x, &y);
    fe8_native_hud_layout(&host->hud, w, h, x, y, host->scale_percent);
    fe8_native_hud_draw(&host->hud, host->pixels, w, w, h);
    host->overlay = (Fe8VideoOverlay){host->pixels, w, h};
    return host->hud.world;
fallback:
    fe8_native_hud_reset(&host->hud);
    return frame;
}
bool fe8_hud_host_contains(const Fe8HudHost *host, const Fe8HostVideo *video,
        int canvas_x, int canvas_y) {
    int x, y;
    if (!host->overlay.pixels) return false;
    canvas_to_output(video, canvas_x, canvas_y, &x, &y);
    return fe8_native_hud_hit_test(&host->hud, x, y);
}
void fe8_hud_host_pointer(Fe8HudHost *host, const Fe8HostVideo *video,
        int canvas_x, int canvas_y) {
    Fe8HostPixel pointer[24 * 24] = {0};
    int x, y;
    if (!host->overlay.pixels) return;
    canvas_to_output(video, canvas_x, canvas_y, &x, &y);
    fe8_host_draw_mouse_cursor(pointer, 24, 24, 24, 0, 0);
    int scale = (int)(video->scaling.base_pixel_scale + .5);
    if (scale < 1) scale = 1;
    if (scale > 8) scale = 8;
    for (int py = 0; py < 24 * scale; ++py)
        for (int px = 0; px < 24 * scale; ++px) {
            int dx = x + px, dy = y + py;
            Fe8HostPixel color = pointer[(py / scale) * 24 + px / scale];
            if ((color >> 24) && dx >= 0 && dy >= 0 &&
                    dx < host->overlay.width && dy < host->overlay.height)
                host->pixels[(size_t)dy * host->overlay.width + dx] = color;
        }
}
bool fe8_hud_host_shortcut(Fe8HudHost *host, const Fe8HostSettings *settings,
        const SDL_Event *event) {
    if (!host->overlay.pixels || event->type != SDL_KEYDOWN ||
            (event->key.keysym.mod & (KMOD_CTRL | KMOD_ALT | KMOD_GUI)) ||
            fe8_host_key_for_scancode(settings, event->key.keysym.scancode) ||
            fe8_host_hotkey_for_scancode(settings, event->key.keysym.scancode)) return false;
    switch (event->key.keysym.sym) {
    case SDLK_PLUS: case SDLK_EQUALS: case SDLK_KP_PLUS: host->scale_percent += 10; break;
    case SDLK_MINUS: case SDLK_KP_MINUS: host->scale_percent -= 10; break;
    case SDLK_0: case SDLK_KP_0: host->scale_percent = FE8_HUD_DEFAULT_SCALE; break;
    default: return false;
    }
    if (host->scale_percent < 80) host->scale_percent = 80;
    if (host->scale_percent > 200) host->scale_percent = 200;
    fprintf(stderr, "Map HUD size: %d%% (independent of map zoom)\n", host->scale_percent);
    return true;
}
Fe8HostPixel *fe8_hud_host_capture(const Fe8HudHost *host,
        const Fe8HostVideo *video, const Fe8HostPixel *canvas) {
    if (!host->overlay.pixels) return NULL;
    int w = host->overlay.width, h = host->overlay.height;
    Fe8HostPixel *pixels = malloc((size_t)w * h * sizeof(*pixels));
    if (!pixels) return NULL;
    Fe8HudRect r = image_rect(video);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        uint32_t background = UINT32_C(0xFF0C0A08);
        if (x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height)
            background = canvas[(size_t)((y - r.y) * video->canvas_height / r.height) *
                video->canvas_width + (x - r.x) * video->canvas_width / r.width];
        pixels[(size_t)y * w + x] = fe8_native_hud_over(host->pixels[(size_t)y * w + x], background);
    }
    return pixels;
}
void fe8_hud_host_deinit(Fe8HudHost *host) {
    if (!host) return;
    free(host->pixels);
    host->pixels = NULL; host->overlay.pixels = NULL; host->capacity = 0;
}
