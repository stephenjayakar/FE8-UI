#include "frame_alignment.h"

#include <limits.h>
#include <stdlib.h>

enum { FINE_SAMPLE_STEP = 4, COARSE_SAMPLE_STEP = 16 };

static unsigned placement_match_percent(
    const Fe8HostPixel *frame, int frame_width, int frame_height,
    size_t frame_stride, const Fe8HostPixel *terrain,
    int terrain_width, int terrain_height, size_t terrain_stride,
    int placement_x, int placement_y, int sample_step) {
    unsigned matches = 0;
    unsigned samples = 0;
    int y;
    for (y = 0; y < frame_height; y += sample_step) {
        int terrain_y = placement_y + y;
        int x;
        if (terrain_y < 0 || terrain_y >= terrain_height)
            continue;
        for (x = 0; x < frame_width; x += sample_step) {
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
    Fe8FramePlacement coarse = {expected_x, expected_y, 0};
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
                expected_x + dx, expected_y + dy, COARSE_SAMPLE_STEP);
            int distance = abs(dx) + abs(dy);
            if (match > coarse.match_percent ||
                    (match == coarse.match_percent && distance < best_distance)) {
                coarse.x = expected_x + dx;
                coarse.y = expected_y + dy;
                coarse.match_percent = match;
                best_distance = distance;
            }
        }
    }
    best_distance = INT_MAX;
    for (dy = -1; dy <= 1; ++dy) {
        int dx;
        for (dx = -1; dx <= 1; ++dx) {
            int candidate_x = coarse.x + dx;
            int candidate_y = coarse.y + dy;
            int relative_x = candidate_x - expected_x;
            int relative_y = candidate_y - expected_y;
            unsigned match;
            int distance;
            if (abs(relative_x) > search_radius || abs(relative_y) > search_radius)
                continue;
            match = placement_match_percent(frame, frame_width, frame_height,
                frame_stride, terrain, terrain_width, terrain_height, terrain_stride,
                candidate_x, candidate_y, FINE_SAMPLE_STEP);
            distance = abs(relative_x) + abs(relative_y);
            if (match > best.match_percent ||
                    (match == best.match_percent && distance < best_distance)) {
                best.x = candidate_x;
                best.y = candidate_y;
                best.match_percent = match;
                best_distance = distance;
            }
        }
    }
    return best;
}
