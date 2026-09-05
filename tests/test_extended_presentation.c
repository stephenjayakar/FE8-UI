#include "extended_presentation.h"

#include <assert.h>
#include <stdio.h>

static void assert_centered(const Fe8ExtendedPresentation *presentation,
    int width, int height, Fe8FramePlacement stale) {
    Fe8FramePlacement placed = fe8_presentation_place_frame(presentation, width, height, stale);
    assert(placed.x == (width - 240) / 2);
    assert(placed.y == (height - 160) / 2);
    assert(placed.match_percent == 0);
    assert(placed.x >= 0 && placed.y >= 0);
    assert(placed.x + 240 <= width && placed.y + 160 <= height);
}

static void test_native_frame_placement(void) {
    const int sizes[][2] = {{240, 160}, {480, 320}, {641, 401}, {1280, 720}};
    const Fe8FramePlacement stale[] = {{-200, -100, 95}, {700, 500, 90}, {24, 8, 82}};
    Fe8ExtendedPresentation presentation = {0};
    unsigned i, j;
    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        for (j = 0; j < sizeof(stale) / sizeof(stale[0]); ++j) {
            fe8_presentation_reset(&presentation);
            assert_centered(&presentation, sizes[i][0], sizes[i][1], stale[j]);
            assert_centered(NULL, sizes[i][0], sizes[i][1], stale[j]);
            fe8_presentation_update(&presentation, true, true, false);
            assert_centered(&presentation, sizes[i][0], sizes[i][1], stale[j]);
            fe8_presentation_update(&presentation, true, true, false);
            Fe8FramePlacement live = fe8_presentation_place_frame(
                &presentation, sizes[i][0], sizes[i][1], stale[j]);
            assert(live.x == stale[j].x && live.y == stale[j].y);
            assert(live.match_percent == stale[j].match_percent);
            /* Native prep/item screens keep map data in RAM, but no longer
             * match the terrain. No cached or off-screen anchor may leak out. */
            fe8_presentation_update(&presentation, true, false, false);
            for (int menu_frame = 0; menu_frame < 120; ++menu_frame) {
                fe8_presentation_update(&presentation, true, false, false);
                assert_centered(&presentation, sizes[i][0], sizes[i][1], stale[j]);
            }
            /* Recovery uses two current valid frames, not the old camera. */
            fe8_presentation_update(&presentation, true, true, false);
            assert_centered(&presentation, sizes[i][0], sizes[i][1], stale[j]);
            fe8_presentation_update(&presentation, true, true, false);
            assert(presentation.state == FE8_PRESENTATION_LIVE);
            fe8_presentation_update(&presentation, true, true, true);
            assert_centered(&presentation, sizes[i][0], sizes[i][1], stale[j]);
            fe8_presentation_update(&presentation, false, true, false);
            assert_centered(&presentation, sizes[i][0], sizes[i][1], stale[j]);
            assert(presentation.consecutive_valid_frames == 0);
        }
    }
}

int main(void) {
    test_native_frame_placement();
    Fe8ExtendedPresentation presentation = {0};
    assert(fe8_presentation_update(&presentation, true, true, false) ==
        FE8_PRESENTATION_INACTIVE);
    assert(fe8_presentation_update(&presentation, true, true, false) ==
        FE8_PRESENTATION_LIVE);
    assert(fe8_presentation_update(&presentation, true, true, true) ==
        FE8_PRESENTATION_FROZEN);
    assert(fe8_presentation_update(&presentation, true, true, true) ==
        FE8_PRESENTATION_FROZEN);
    assert(fe8_presentation_update(&presentation, true, true, false) ==
        FE8_PRESENTATION_FROZEN);
    assert(fe8_presentation_update(&presentation, true, true, false) ==
        FE8_PRESENTATION_LIVE);
    assert(fe8_presentation_update(&presentation, true, false, false) ==
        FE8_PRESENTATION_FROZEN);
    assert(fe8_presentation_update(&presentation, true, true, false) ==
        FE8_PRESENTATION_FROZEN);
    assert(fe8_presentation_update(&presentation, true, false, false) ==
        FE8_PRESENTATION_FROZEN);
    assert(fe8_presentation_update(&presentation, true, true, false) ==
        FE8_PRESENTATION_FROZEN);
    assert(fe8_presentation_update(&presentation, true, true, false) ==
        FE8_PRESENTATION_LIVE);
    assert(fe8_presentation_update(&presentation, false, true, false) ==
        FE8_PRESENTATION_INACTIVE);
    puts("extended presentation tests passed");
    return 0;
}
