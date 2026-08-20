#ifndef FE8_HOST_VIDEO_H
#define FE8_HOST_VIDEO_H

#include <SDL.h>

#include "host_settings.h"
#include "display_scaling.h"

typedef struct Fe8HostVideo {
    SDL_Window *window;
    void *backend;
    int canvas_width;
    int canvas_height;
    int base_canvas_width;
    int base_canvas_height;
    Fe8DisplayScaling scaling;
    int vsync_active;
    enum Fe8HostShader shader;
} Fe8HostVideo;

int fe8_host_video_init(Fe8HostVideo *video, const char *title,
    int canvas_width, int canvas_height, int vsync_enabled);
int fe8_host_video_set_vsync(Fe8HostVideo *video, int enabled);
int fe8_host_video_set_shader(Fe8HostVideo *video, enum Fe8HostShader shader);
int fe8_host_video_present(Fe8HostVideo *video, const void *pixels);
int fe8_host_video_window_to_canvas(const Fe8HostVideo *video,
    int window_x, int window_y, int *canvas_x, int *canvas_y);
int fe8_host_video_adjust_zoom(
    Fe8HostVideo *video, double wheel_delta, double sensitivity);
int fe8_host_video_refresh_layout(Fe8HostVideo *video);
int fe8_host_video_set_content_density(Fe8HostVideo *video, int density);
void fe8_host_video_log_status(const Fe8HostVideo *video);
void fe8_host_video_deinit(Fe8HostVideo *video);

#endif
