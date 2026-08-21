#ifndef FE8_VIEWPORT_CONTROLLER_H
#define FE8_VIEWPORT_CONTROLLER_H

#include "extended_map_renderer.h"
#include "fe8_profile.h"

void fe8_viewport_clamp_pan(
    int *pan_x, int *pan_y, const Fe8Snapshot *snapshot,
    Fe8ExtendedViewport *viewport, int gba_anchor_x, int gba_anchor_y);

/*
 * Lets FE8's native camera move its 240x160 frame across the extended
 * canvas before moving the extended world. Because FE8 already scrolls its
 * camera when the cursor reaches the native edge band, this applies the same
 * behavior to the outer edge of the extended canvas.
 */
void fe8_viewport_follow_cursor_camera(
    int *pan_x, int *pan_y, const Fe8Snapshot *snapshot,
    Fe8ExtendedViewport *viewport, int gba_anchor_x, int gba_anchor_y,
    int previous_camera_x, int previous_camera_y);

void fe8_viewport_recenter_on_tile(
    int *pan_x, int *pan_y, const Fe8Snapshot *snapshot,
    Fe8ExtendedViewport *viewport, int gba_anchor_x, int gba_anchor_y,
    int map_x, int map_y);

#endif
