#ifndef FE8_HOST_VIDEO_H
#define FE8_HOST_VIDEO_H

#include <SDL.h>

#include "host_settings.h"

typedef struct Fe8HostVideo {
    SDL_Window *window;
    void *backend;
    int canvas_width;
    int canvas_height;
    int vsync_active;
    enum Fe8HostShader shader;
} Fe8HostVideo;

int fe8_host_video_init(Fe8HostVideo *video, const char *title,
    int canvas_width, int canvas_height, int vsync_enabled);
int fe8_host_video_set_vsync(Fe8HostVideo *video, int enabled);
int fe8_host_video_set_shader(Fe8HostVideo *video, enum Fe8HostShader shader);
int fe8_host_video_present(Fe8HostVideo *video, const void *pixels);
void fe8_host_video_log_status(const Fe8HostVideo *video);
void fe8_host_video_deinit(Fe8HostVideo *video);

#endif
