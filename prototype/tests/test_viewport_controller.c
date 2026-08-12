#include "viewport_controller.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

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
    puts("viewport controller tests passed");
    return 0;
}
