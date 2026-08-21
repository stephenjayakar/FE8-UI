#include "viewport_controller.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int map_origin_x(const Fe8Snapshot *snapshot,
    const Fe8ExtendedViewport *viewport) {
    return snapshot->camera_x - viewport->gba_x;
}

static int map_origin_y(const Fe8Snapshot *snapshot,
    const Fe8ExtendedViewport *viewport) {
    return snapshot->camera_y - viewport->gba_y;
}

int main(void) {
    Fe8Snapshot snapshot;
    Fe8ExtendedViewport viewport = {480, 320, 120, 80};
    int pan_x = 0;
    int pan_y = 0;
    int map_x;
    int map_y;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.map_width = 28;
    snapshot.map_height = 24;
    snapshot.camera_x = 208;
    snapshot.camera_y = 0;

    fe8_viewport_clamp_pan(&pan_x, &pan_y, &snapshot, &viewport, 120, 80);
    assert(viewport.gba_x == 224); /* 448px map is centered in 480px canvas. */
    assert(viewport.gba_y == 0);

    fe8_viewport_recenter_on_tile(
        &pan_x, &pan_y, &snapshot, &viewport, 120, 80, 14, 23);
    assert(viewport.gba_y == -64); /* Bottom edge: origin clamped to 64px. */
    assert(fe8_canvas_to_map_tile(
        &(Fe8MapRenderState){.map_width = 28, .map_height = 24,
            .camera_x = 208, .camera_y = 0},
        viewport, 240, 312, &map_x, &map_y));
    assert(map_y == 23);

    fe8_viewport_recenter_on_tile(
        &pan_x, &pan_y, &snapshot, &viewport, 120, 80, 14, 0);
    assert(viewport.gba_y == 0); /* Top edge: origin clamped to zero. */

    /* On a large map, native camera motion first moves the 240x160 frame
     * through the extra canvas area while the extended world stays still. */
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.map_width = 64;
    snapshot.map_height = 64;
    snapshot.camera_x = 160;
    snapshot.camera_y = 160;
    viewport = (Fe8ExtendedViewport){480, 320, 120, 80};
    pan_x = 0;
    pan_y = 0;
    fe8_viewport_clamp_pan(&pan_x, &pan_y, &snapshot, &viewport, 120, 80);
    assert(map_origin_x(&snapshot, &viewport) == 40);
    assert(map_origin_y(&snapshot, &viewport) == 80);

    snapshot.camera_x = 224;
    snapshot.camera_y = 208;
    fe8_viewport_follow_cursor_camera(
        &pan_x, &pan_y, &snapshot, &viewport, 120, 80, 160, 160);
    assert(viewport.gba_x == 184);
    assert(viewport.gba_y == 128);
    assert(map_origin_x(&snapshot, &viewport) == 40);
    assert(map_origin_y(&snapshot, &viewport) == 80);

    /* Once the native frame reaches the outer edge, only the excess camera
     * motion pans the extended world. */
    snapshot.camera_x = 320;
    snapshot.camera_y = 288;
    fe8_viewport_follow_cursor_camera(
        &pan_x, &pan_y, &snapshot, &viewport, 120, 80, 224, 208);
    assert(viewport.gba_x == 240);
    assert(viewport.gba_y == 160);
    assert(map_origin_x(&snapshot, &viewport) == 80);
    assert(map_origin_y(&snapshot, &viewport) == 128);

    /* Reversing direction moves the native frame back into the canvas before
     * moving the extended world again. */
    snapshot.camera_x = 304;
    snapshot.camera_y = 272;
    fe8_viewport_follow_cursor_camera(
        &pan_x, &pan_y, &snapshot, &viewport, 120, 80, 320, 288);
    assert(viewport.gba_x == 224);
    assert(viewport.gba_y == 144);
    assert(map_origin_x(&snapshot, &viewport) == 80);
    assert(map_origin_y(&snapshot, &viewport) == 128);

    /* Archanae's 34x15 opening map has 64 horizontal pixels beyond the
     * extended canvas. Crossing from the right edge should consume the full
     * 240-pixel frame travel before the extended map begins to pan. */
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.map_width = 34;
    snapshot.map_height = 15;
    snapshot.camera_x = 304;
    viewport = (Fe8ExtendedViewport){480, 320, 120, 80};
    pan_x = 0;
    pan_y = 0;
    fe8_viewport_clamp_pan(&pan_x, &pan_y, &snapshot, &viewport, 120, 80);
    assert(viewport.gba_x == 240);
    assert(viewport.gba_y == 40);
    assert(map_origin_x(&snapshot, &viewport) == 64);
    assert(map_origin_y(&snapshot, &viewport) == -40);

    snapshot.camera_x = 64;
    fe8_viewport_follow_cursor_camera(
        &pan_x, &pan_y, &snapshot, &viewport, 120, 80, 304, 0);
    assert(viewport.gba_x == 0);
    assert(map_origin_x(&snapshot, &viewport) == 64);

    snapshot.camera_x = 48;
    fe8_viewport_follow_cursor_camera(
        &pan_x, &pan_y, &snapshot, &viewport, 120, 80, 64, 0);
    assert(viewport.gba_x == 0);
    assert(map_origin_x(&snapshot, &viewport) == 48);

    puts("viewport controller tests passed");
    return 0;
}
