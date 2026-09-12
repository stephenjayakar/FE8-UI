#ifndef FE8_NATIVE_HUD_HOST_H
#define FE8_NATIVE_HUD_HOST_H

#include "native_hud.h"
#include "host_video.h"

typedef struct Fe8HudHost {
    Fe8NativeHud hud;
    Fe8HostPixel *pixels;
    size_t capacity;
    Fe8VideoOverlay overlay;
    int scale_percent;
    bool enabled;
} Fe8HudHost;

/* Returns the original frame unless a complete, validated HUD can be drawn. */
const Fe8HostPixel *fe8_hud_host_update(Fe8HudHost *host,
    const Fe8HostVideo *video, const Fe8MemoryView *memory,
    const Fe8Snapshot *snapshot, const Fe8HostPixel *frame,
    bool live_map, int frame_x, int frame_y);
bool fe8_hud_host_contains(const Fe8HudHost *host, const Fe8HostVideo *video,
    int canvas_x, int canvas_y);
void fe8_hud_host_pointer(Fe8HudHost *host, const Fe8HostVideo *video,
    int canvas_x, int canvas_y);
/* Returns true only for unbound +/-/0 key presses while the map HUD is live. */
bool fe8_hud_host_shortcut(Fe8HudHost *host, const Fe8HostSettings *settings,
    const SDL_Event *event);
/* CPU capture of the unfiltered game plus HUD at drawable resolution. Free
 * the returned image. NULL leaves the caller's ordinary canvas capture path. */
Fe8HostPixel *fe8_hud_host_capture(const Fe8HudHost *host,
    const Fe8HostVideo *video, const Fe8HostPixel *canvas);
void fe8_hud_host_deinit(Fe8HudHost *host);
#endif
