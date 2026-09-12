#ifndef FE8_NATIVE_HUD_H
#define FE8_NATIVE_HUD_H

#include "extended_map_renderer.h"
#include "fe8_profile.h"

/* The atlas is in GBA pixels; destinations are in drawable pixels, not map
 * canvas pixels. Never scale the atlas down with the map's zoom. */
enum { FE8_HUD_WIDTH = 240, FE8_HUD_HEIGHT = 160, FE8_HUD_MAX_PANELS = 4,
       FE8_HUD_DEFAULT_SCALE = 150 };
typedef enum Fe8HudKind {
    FE8_HUD_UNIT, FE8_HUD_TERRAIN, FE8_HUD_OBJECTIVE, FE8_HUD_ACTION
} Fe8HudKind;
typedef struct Fe8HudRect { int x, y, width, height; } Fe8HudRect;
typedef struct Fe8HudPanel {
    Fe8HudKind kind;
    Fe8HudRect source, destination;
} Fe8HudPanel;
typedef struct Fe8NativeHud {
    Fe8HostPixel atlas[FE8_HUD_WIDTH * FE8_HUD_HEIGHT];
    Fe8HostPixel world[FE8_HUD_WIDTH * FE8_HUD_HEIGHT];
    Fe8HudPanel panels[FE8_HUD_MAX_PANELS];
    unsigned count;
    bool menu_latched;
    double menu_x, menu_y;
} Fe8NativeHud;

void fe8_native_hud_reset(Fe8NativeHud *hud);
/* read8 must expose the *raw* I/O register shadow (mCore.rawRead8), not GBA
 * bus open-bus values for write-only scroll/blend registers. Purely read-only.
 * False leaves the canonical frame untouched; nothing may be stripped then. */
bool fe8_native_hud_extract(Fe8NativeHud *hud, const Fe8MemoryView *memory,
    const Fe8Snapshot *snapshot, const Fe8HostPixel *frame, size_t stride,
    bool live_map);
/* cursor_x/y are drawable coordinates. Action menus latch once per opening. */
void fe8_native_hud_layout(Fe8NativeHud *hud, int width, int height,
    double cursor_x, double cursor_y, int scale_percent);
bool fe8_native_hud_hit_test(const Fe8NativeHud *hud, int x, int y);
/* Clears and draws a transparent, full-resolution presentation layer. */
void fe8_native_hud_draw(const Fe8NativeHud *hud, Fe8HostPixel *pixels,
    size_t stride, int width, int height);
/* Straight-alpha composition, also shared by deterministic capture tests. */
Fe8HostPixel fe8_native_hud_over(Fe8HostPixel foreground, Fe8HostPixel background);

#endif
