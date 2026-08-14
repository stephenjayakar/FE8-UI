#ifndef FE8_FRAME_ALIGNMENT_H
#define FE8_FRAME_ALIGNMENT_H

#include "extended_map_renderer.h"

#include <stddef.h>

typedef struct Fe8FramePlacement {
    int x;
    int y;
    unsigned match_percent;
} Fe8FramePlacement;

/*
 * Finds the framebuffer placement that best matches the already-rendered
 * terrain. FE8 can update gBmSt.camera one frame before the PPU scroll reaches
 * the framebuffer; searching a small area prevents that transient mismatch
 * from moving the canonical frame to the wrong place.
 */
Fe8FramePlacement fe8_align_frame_to_terrain(
    const Fe8HostPixel *frame, int frame_width, int frame_height,
    size_t frame_stride, const Fe8HostPixel *terrain,
    int terrain_width, int terrain_height, size_t terrain_stride,
    int expected_x, int expected_y, int search_radius);

#endif
