#include "extended_presentation.h"

#include <assert.h>
#include <stdio.h>

static void acquire_map(Fe8ExtendedPresentation *presentation) {
    assert(fe8_presentation_update(presentation, true, true, false) ==
        FE8_PRESENTATION_INACTIVE);
    assert(fe8_presentation_update(presentation, true, true, false) ==
        FE8_PRESENTATION_LIVE);
}

static void test_map_and_overlay(void) {
    Fe8ExtendedPresentation presentation = {0};
    acquire_map(&presentation);
    assert(fe8_presentation_update(&presentation, true, true, true) ==
        FE8_PRESENTATION_FROZEN);
    /* A positively identified combat panel may obscure the terrain. */
    assert(fe8_presentation_update(&presentation, true, false, true) ==
        FE8_PRESENTATION_FROZEN);
    assert(fe8_presentation_update(&presentation, true, true, false) ==
        FE8_PRESENTATION_FROZEN);
    assert(fe8_presentation_update(&presentation, true, true, false) ==
        FE8_PRESENTATION_LIVE);
    /* A native inventory/menu is not a frozen tactical scene. */
    assert(fe8_presentation_update(&presentation, true, false, false) ==
        FE8_PRESENTATION_INACTIVE);
    assert(presentation.consecutive_valid_frames == 0);
    for (unsigned frame = 0; frame < 120; ++frame)
        assert(fe8_presentation_update(&presentation, true, false, false) ==
            FE8_PRESENTATION_INACTIVE);
    acquire_map(&presentation);
    /* Leaving a combat panel for an unsupported screen also centers at once. */
    assert(fe8_presentation_update(&presentation, true, true, true) ==
        FE8_PRESENTATION_FROZEN);
    assert(fe8_presentation_update(&presentation, true, false, false) ==
        FE8_PRESENTATION_INACTIVE);
    acquire_map(&presentation);
}

static void test_toggle_and_revalidation(void) {
    Fe8ExtendedPresentation presentation = {0};
    /* A freeze request cannot create an extended scene without a live frame. */
    assert(fe8_presentation_update(&presentation, true, false, true) ==
        FE8_PRESENTATION_INACTIVE);
    acquire_map(&presentation);
    assert(fe8_presentation_update(&presentation, false, true, false) ==
        FE8_PRESENTATION_INACTIVE);
    assert(presentation.consecutive_valid_frames == 0);
    acquire_map(&presentation);
    assert(fe8_presentation_update(&presentation, true, true, true) ==
        FE8_PRESENTATION_FROZEN);
    assert(fe8_presentation_update(&presentation, false, true, true) ==
        FE8_PRESENTATION_INACTIVE);
    /* A stale map snapshot/one matching transition frame must not reacquire. */
    for (unsigned frame = 0; frame < 120; ++frame) {
        assert(fe8_presentation_update(&presentation, true, true, false) ==
            FE8_PRESENTATION_INACTIVE);
        assert(fe8_presentation_update(&presentation, true, false, false) ==
            FE8_PRESENTATION_INACTIVE);
    }
    acquire_map(&presentation);
    fe8_presentation_reset(&presentation);
    assert(presentation.state == FE8_PRESENTATION_INACTIVE);
    assert(presentation.consecutive_valid_frames == 0);
    fe8_presentation_reset(NULL);
    assert(fe8_presentation_update(NULL, true, true, false) ==
        FE8_PRESENTATION_INACTIVE);
}

static void test_native_frame_centering(void) {
    static const int sizes[][2] = {
        {480, 320}, {640, 480}, {1280, 800}, {481, 321}, {240, 160}, {200, 120}
    };
    static const Fe8FramePlacement map_positions[] = {
        {-800, 1200, 95}, {1300, -900, 70}, {20, 10, 90}
    };
    for (unsigned size = 0; size < sizeof(sizes) / sizeof(sizes[0]); ++size) {
        for (unsigned pos = 0; pos < sizeof(map_positions) / sizeof(map_positions[0]); ++pos) {
            Fe8FramePlacement centered = fe8_presentation_frame_placement(
                false, sizes[size][0], sizes[size][1], map_positions[pos]);
            assert(centered.x == (sizes[size][0] - 240) / 2);
            assert(centered.y == (sizes[size][1] - 160) / 2);
            assert(centered.match_percent == 0);
            Fe8FramePlacement live = fe8_presentation_frame_placement(
                true, sizes[size][0], sizes[size][1], map_positions[pos]);
            assert(live.x == map_positions[pos].x);
            assert(live.y == map_positions[pos].y);
            assert(live.match_percent == map_positions[pos].match_percent);
        }
    }
}

int main(void) {
    test_map_and_overlay();
    test_toggle_and_revalidation();
    test_native_frame_centering();
    puts("extended presentation tests passed");
    return 0;
}
