#ifndef FE8_VIEWPORT_CONTROLLER_H
#define FE8_VIEWPORT_CONTROLLER_H

#include "extended_map_renderer.h"
#include "fe8_profile.h"

void fe8_viewport_clamp_pan(
    int *pan_x, int *pan_y, const Fe8Snapshot *snapshot,
    Fe8ExtendedViewport *viewport, int gba_anchor_x, int gba_anchor_y);

void fe8_viewport_recenter_on_tile(
    int *pan_x, int *pan_y, const Fe8Snapshot *snapshot,
    Fe8ExtendedViewport *viewport, int gba_anchor_x, int gba_anchor_y,
    int map_x, int map_y);

#endif
