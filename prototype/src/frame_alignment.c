#include "frame_alignment.h"

#include <limits.h>
#include <stdlib.h>

enum { SAMPLE_STEP = 4 };

static unsigned placement_match_percent(
    const Fe8HostPixel *frame, int frame_width, int frame_height,
    size_t frame_stride, const Fe8HostPixel *terrain,
    int terrain_width, int terrain_height, size_t terrain_stride,
    int placement_x, int placement_y) {
    unsigned matches = 0;
    unsigned samples = 0;
    int y;
    for (y = 0; y < frame_height; y += SAMPLE_STEP) {
        int terrain_y = placement_y + y;
        int x;
        if (terrain_y < 0 || terrain_y >= terrain_height)
            continue;
        for (x = 0; x < frame_width; x += SAMPLE_STEP) {
            int terrain_x = placement_x + x;
            if (terrain_x < 0 || terrain_x >= terrain_width)
                continue;
            matches += frame[(size_t)y * frame_stride + x] ==
                terrain[(size_t)terrain_y * terrain_stride + terrain_x];
            ++samples;
        }
    }
    return samples ? matches * 100 / samples : 0;
}

Fe8FramePlacement fe8_align_frame_to_terrain(
    const Fe8HostPixel *frame, int frame_width, int frame_height,
    size_t frame_stride, const Fe8HostPixel *terrain,
    int terrain_width, int terrain_height, size_t terrain_stride,
    int expected_x, int expected_y, int search_radius) {
    Fe8FramePlacement best = {expected_x, expected_y, 0};
    int best_distance = INT_MAX;
    int dy;
    if (!frame || !terrain || frame_width <= 0 || frame_height <= 0 ||
            terrain_width <= 0 || terrain_height <= 0)
        return best;
    if (search_radius < 0)
        search_radius = 0;
    for (dy = -search_radius; dy <= search_radius; ++dy) {
        int dx;
        for (dx = -search_radius; dx <= search_radius; ++dx) {
            unsigned match = placement_match_percent(
                frame, frame_width, frame_height, frame_stride,
                terrain, terrain_width, terrain_height, terrain_stride,
                expected_x + dx, expected_y + dy);
            int distance = abs(dx) + abs(dy);
            if (match > best.match_percent ||
                    (match == best.match_percent && distance < best_distance)) {
                best.x = expected_x + dx;
                best.y = expected_y + dy;
                best.match_percent = match;
                best_distance = distance;
            }
        }
    }
    return best;
}
