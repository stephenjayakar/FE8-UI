#include "frame_alignment.h"

#include <assert.h>
#include <stdio.h>

enum {
    TERRAIN_WIDTH = 96,
    TERRAIN_HEIGHT = 72,
    FRAME_WIDTH = 40,
    FRAME_HEIGHT = 28,
};

static Fe8HostPixel pattern(int x, int y) {
    return UINT32_C(0xFF000000) |
        (uint32_t)((x * 37 + y * 11) & 0x00FFFFFF);
}

int main(void) {
    Fe8HostPixel terrain[TERRAIN_WIDTH * TERRAIN_HEIGHT];
    Fe8HostPixel frame[FRAME_WIDTH * FRAME_HEIGHT];
    Fe8FramePlacement placement;
    int x;
    int y;
    for (y = 0; y < TERRAIN_HEIGHT; ++y)
        for (x = 0; x < TERRAIN_WIDTH; ++x)
            terrain[y * TERRAIN_WIDTH + x] = pattern(x, y);
    for (y = 0; y < FRAME_HEIGHT; ++y)
        for (x = 0; x < FRAME_WIDTH; ++x)
            frame[y * FRAME_WIDTH + x] = terrain[(y + 21) * TERRAIN_WIDTH + x + 33];

    placement = fe8_align_frame_to_terrain(
        frame, FRAME_WIDTH, FRAME_HEIGHT, FRAME_WIDTH,
        terrain, TERRAIN_WIDTH, TERRAIN_HEIGHT, TERRAIN_WIDTH,
        37, 18, 5);
    assert(placement.x == 33);
    assert(placement.y == 21);
    assert(placement.match_percent == 100);

    /* A screen-space UI block must not overpower the terrain alignment. */
    for (y = 0; y < 8; ++y)
        for (x = 0; x < FRAME_WIDTH; ++x)
            frame[y * FRAME_WIDTH + x] = UINT32_C(0xFFFF00FF);
    placement = fe8_align_frame_to_terrain(
        frame, FRAME_WIDTH, FRAME_HEIGHT, FRAME_WIDTH,
        terrain, TERRAIN_WIDTH, TERRAIN_HEIGHT, TERRAIN_WIDTH,
        37, 18, 5);
    assert(placement.x == 33);
    assert(placement.y == 21);
    assert(placement.match_percent >= 70);

    puts("frame alignment tests passed");
    return 0;
}
