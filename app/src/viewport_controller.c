#include "viewport_controller.h"

enum {
    MAP_TILE_SIZE = 16,
    NATIVE_FRAME_WIDTH = 240,
    NATIVE_FRAME_HEIGHT = 160,
};

static int clamp_int(int value, int minimum, int maximum) {
    if (value < minimum)
        return minimum;
    if (value > maximum)
        return maximum;
    return value;
}

static int advance_frame_within_canvas(
    int position, int delta, int minimum, int maximum) {
    int advanced = position + delta;
    if (minimum > maximum)
        return (minimum + maximum) / 2;
    if (position < minimum) {
        if (delta <= 0)
            return position;
        return advanced > maximum ? maximum : advanced;
    }
    if (position > maximum) {
        if (delta >= 0)
            return position;
        return advanced < minimum ? minimum : advanced;
    }
    return clamp_int(advanced, minimum, maximum);
}

void fe8_viewport_clamp_pan(
    int *pan_x, int *pan_y, const Fe8Snapshot *snapshot,
    Fe8ExtendedViewport *viewport, int gba_anchor_x, int gba_anchor_y) {
    int base_x = snapshot->camera_x - gba_anchor_x;
    int base_y = snapshot->camera_y - gba_anchor_y;
    int map_width = snapshot->map_width * MAP_TILE_SIZE;
    int map_height = snapshot->map_height * MAP_TILE_SIZE;
    int origin_x = base_x + *pan_x;
    int origin_y = base_y + *pan_y;
    if (map_width <= viewport->width)
        origin_x = (map_width - viewport->width) / 2;
    else
        origin_x = clamp_int(origin_x, 0, map_width - viewport->width);
    if (map_height <= viewport->height)
        origin_y = (map_height - viewport->height) / 2;
    else
        origin_y = clamp_int(origin_y, 0, map_height - viewport->height);
    *pan_x = origin_x - base_x;
    *pan_y = origin_y - base_y;
    viewport->gba_x = gba_anchor_x - *pan_x;
    viewport->gba_y = gba_anchor_y - *pan_y;
}

void fe8_viewport_follow_cursor_camera(
    int *pan_x, int *pan_y, const Fe8Snapshot *snapshot,
    Fe8ExtendedViewport *viewport, int gba_anchor_x, int gba_anchor_y,
    int previous_camera_x, int previous_camera_y) {
    int frame_x = advance_frame_within_canvas(
        viewport->gba_x, snapshot->camera_x - previous_camera_x,
        0, viewport->width - NATIVE_FRAME_WIDTH);
    int frame_y = advance_frame_within_canvas(
        viewport->gba_y, snapshot->camera_y - previous_camera_y,
        0, viewport->height - NATIVE_FRAME_HEIGHT);
    *pan_x = gba_anchor_x - frame_x;
    *pan_y = gba_anchor_y - frame_y;
    fe8_viewport_clamp_pan(
        pan_x, pan_y, snapshot, viewport, gba_anchor_x, gba_anchor_y);
}

void fe8_viewport_recenter_on_tile(
    int *pan_x, int *pan_y, const Fe8Snapshot *snapshot,
    Fe8ExtendedViewport *viewport, int gba_anchor_x, int gba_anchor_y,
    int map_x, int map_y) {
    int desired_origin_x = map_x * MAP_TILE_SIZE + MAP_TILE_SIZE / 2 - viewport->width / 2;
    int desired_origin_y = map_y * MAP_TILE_SIZE + MAP_TILE_SIZE / 2 - viewport->height / 2;
    *pan_x = desired_origin_x - (snapshot->camera_x - gba_anchor_x);
    *pan_y = desired_origin_y - (snapshot->camera_y - gba_anchor_y);
    fe8_viewport_clamp_pan(
        pan_x, pan_y, snapshot, viewport, gba_anchor_x, gba_anchor_y);
}
