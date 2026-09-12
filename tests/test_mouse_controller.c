#include "mouse_controller.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

enum {
    KEY_A = 1 << 0,
    KEY_B = 1 << 1,
    KEY_RIGHT = 1 << 4,
    KEY_DOWN = 1 << 7,
};

static uint32_t complete_animated_step(
    Fe8MouseController *mouse, Fe8LiveState *snapshot) {
    uint32_t keys = 0;
    snapshot->cursor_x = (uint8_t)mouse->issued_x;
    snapshot->cursor_y = (uint8_t)mouse->issued_y;
    snapshot->cursor_target_x = (int16_t)(mouse->issued_x * 16);
    snapshot->cursor_target_y = (int16_t)(mouse->issued_y * 16);
    while (snapshot->cursor_display_x != snapshot->cursor_target_x ||
            snapshot->cursor_display_y != snapshot->cursor_target_y) {
        if (snapshot->cursor_display_x < snapshot->cursor_target_x)
            snapshot->cursor_display_x += 8;
        else if (snapshot->cursor_display_x > snapshot->cursor_target_x)
            snapshot->cursor_display_x -= 8;
        if (snapshot->cursor_display_y < snapshot->cursor_target_y)
            snapshot->cursor_display_y += 8;
        else if (snapshot->cursor_display_y > snapshot->cursor_target_y)
            snapshot->cursor_display_y -= 8;
        keys = fe8_mouse_update(mouse, snapshot, 1);
        if (snapshot->cursor_display_x != snapshot->cursor_target_x ||
                snapshot->cursor_display_y != snapshot->cursor_target_y)
            assert(keys == KEY_B);
    }
    return keys;
}

/* Count real key edges, not held frames, just as the game does. */
static int scroll_edges(Fe8MouseController *mouse, uint32_t expected_key) {
    uint32_t previous = 0;
    int edges = 0;
    int held_frames = 0;
    for (int frame = 0; frame < 100; ++frame) {
        uint32_t keys = fe8_mouse_update(mouse, NULL, 0);
        assert(keys == 0 || keys == expected_key); /* Never A, B or a chord. */
        if (keys && !previous)
            ++edges;
        if (keys)
            assert(++held_frames <= 2);
        else
            held_frames = 0;
        previous = keys;
    }
    assert(mouse->scroll_steps == 0);
    return edges;
}

static void test_menu_scroll(void) {
    const uint32_t up = UINT32_C(1) << 6;
    Fe8MouseController mouse = {0};
    Fe8LiveState snapshot = {0};
    assert(!fe8_mouse_native_ui(&snapshot, 1, 1));
    snapshot.input_lock = 1;
    assert(fe8_mouse_native_ui(&snapshot, 1, 1));
    assert(fe8_mouse_native_ui(NULL, 0, 1));
    assert(fe8_mouse_native_ui(NULL, 0, 0));
    snapshot.input_lock = 0;
    assert(fe8_mouse_native_ui(&snapshot, 1, 0));

    fe8_mouse_scroll(&mouse, 1);
    assert(fe8_mouse_update(&mouse, NULL, 0) == 0);
    assert(fe8_mouse_update(&mouse, NULL, 0) == 0);
    assert(fe8_mouse_update(&mouse, NULL, 0) == up);
    assert(fe8_mouse_update(&mouse, NULL, 0) == up);
    assert(fe8_mouse_update(&mouse, NULL, 0) == 0);
    assert(fe8_mouse_update(&mouse, NULL, 0) == 0);
    assert(scroll_edges(&mouse, up) == 0);

    fe8_mouse_scroll(&mouse, -1);
    assert(scroll_edges(&mouse, KEY_DOWN) == 1);
    fe8_mouse_scroll(&mouse, 1);
    fe8_mouse_scroll(&mouse, 1);
    fe8_mouse_scroll(&mouse, 1);
    assert(scroll_edges(&mouse, up) == 3);
    fe8_mouse_scroll(&mouse, -3);
    assert(scroll_edges(&mouse, KEY_DOWN) == 3);

    /* Precise trackpad deltas accumulate across events and emulated frames. */
    fe8_mouse_scroll(&mouse, 0.25);
    assert(fe8_mouse_update(&mouse, NULL, 0) == 0);
    assert(mouse.scroll_fraction == 0.25);
    fe8_mouse_scroll(&mouse, 0.25);
    assert(fe8_mouse_update(&mouse, NULL, 0) == 0);
    fe8_mouse_scroll(&mouse, 0.5);
    assert(scroll_edges(&mouse, up) == 1);
    fe8_mouse_scroll(&mouse, -0.5);
    fe8_mouse_scroll(&mouse, -1.5);
    assert(scroll_edges(&mouse, KEY_DOWN) == 2);

    /* Idle fractions expire, and reversing discards the opposite backlog. */
    fe8_mouse_scroll(&mouse, 0.75);
    assert(scroll_edges(&mouse, up) == 0);
    fe8_mouse_scroll(&mouse, 0.5);
    assert(scroll_edges(&mouse, up) == 0);
    fe8_mouse_scroll(&mouse, 0.75);
    fe8_mouse_scroll(&mouse, -1);
    assert(scroll_edges(&mouse, KEY_DOWN) == 1);
    fe8_mouse_scroll(&mouse, 4);
    fe8_mouse_scroll(&mouse, -2);
    assert(scroll_edges(&mouse, KEY_DOWN) == 2);
    fe8_mouse_scroll(&mouse, 2);
    assert(fe8_mouse_update(&mouse, NULL, 0) == 0);
    assert(fe8_mouse_update(&mouse, NULL, 0) == 0);
    assert(fe8_mouse_update(&mouse, NULL, 0) == up);
    fe8_mouse_scroll(&mouse, -1); /* Reverse while a key is held. */
    assert(fe8_mouse_update(&mouse, NULL, 0) == 0);
    assert(fe8_mouse_update(&mouse, NULL, 0) == 0);
    assert(scroll_edges(&mouse, KEY_DOWN) == 1);

    /* Huge deltas and multiple accelerated events cannot overflow or leave
     * an unbounded queue playing long after the wheel stops. */
    fe8_mouse_scroll(&mouse, DBL_MAX);
    fe8_mouse_scroll(&mouse, DBL_MAX);
    assert(scroll_edges(&mouse, up) == 8);
    fe8_mouse_scroll(&mouse, -DBL_MAX);
    assert(scroll_edges(&mouse, KEY_DOWN) == 8);
    fe8_mouse_scroll(&mouse, NAN);
    fe8_mouse_scroll(&mouse, INFINITY);
    fe8_mouse_scroll(&mouse, -INFINITY);
    fe8_mouse_scroll(&mouse, 0); /* Includes horizontal-only wheel events. */
    assert(!mouse.scroll_direction && mouse.scroll_fraction == 0);

    /* Map travel's fast-move B must not leak into a native menu. */
    fe8_mouse_set_target(&mouse, 3, 0, 1);
    assert(fe8_mouse_update(&mouse, &snapshot, 1) == (KEY_RIGHT | KEY_B));
    fe8_mouse_scroll(&mouse, -1);
    assert(!mouse.active && !mouse.confirm);
    assert(scroll_edges(&mouse, KEY_DOWN) == 1);

    /* Clicking A/B, keyboard takeover, focus loss and state reload all use
     * the same cancellation path; no old wheel steps can enter the next UI. */
    fe8_mouse_scroll(&mouse, 3.5);
    fe8_mouse_cancel(&mouse);
    mouse.pulse_key = KEY_B;
    mouse.press_frames = 2;
    mouse.release_frames = 2;
    assert(fe8_mouse_update(&mouse, NULL, 0) == 0);
    assert(fe8_mouse_update(&mouse, NULL, 0) == 0);
    assert(fe8_mouse_update(&mouse, NULL, 0) == KEY_B);
    assert(fe8_mouse_update(&mouse, NULL, 0) == KEY_B);
    assert(scroll_edges(&mouse, up) == 0);

    /* A queued click completes before subsequent wheel input, without a
     * simultaneous confirm/direction chord. */
    mouse.pulse_key = KEY_A;
    mouse.press_frames = 2;
    fe8_mouse_scroll(&mouse, 1);
    assert(fe8_mouse_update(&mouse, NULL, 0) == KEY_A);
    assert(fe8_mouse_update(&mouse, NULL, 0) == KEY_A);
    assert(fe8_mouse_update(&mouse, NULL, 0) == 0);
    assert(fe8_mouse_update(&mouse, NULL, 0) == 0);
    assert(scroll_edges(&mouse, up) == 1);

    /* Returning to the map or choosing zoom clears scroll only, not A/B. */
    fe8_mouse_scroll(&mouse, 2.5);
    mouse.pulse_key = KEY_A;
    mouse.press_frames = 2;
    fe8_mouse_cancel_scroll(&mouse);
    assert(fe8_mouse_update(&mouse, NULL, 0) == KEY_A);
    assert(fe8_mouse_update(&mouse, NULL, 0) == KEY_A);
    assert(scroll_edges(&mouse, up) == 0);
}

int main(void) {
    test_menu_scroll();
    Fe8MouseController mouse;
    Fe8LiveState snapshot;
    uint32_t keys;
    memset(&mouse, 0, sizeof(mouse));
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.map_width = 10;
    snapshot.map_height = 10;

    fe8_mouse_set_target(&mouse, 2, 1, 1);
    keys = fe8_mouse_update(&mouse, &snapshot, 1);
    assert(keys == (KEY_RIGHT | KEY_B));
    assert(mouse.step_active && mouse.issued_x == 1 && mouse.issued_y == 0);
    keys = complete_animated_step(&mouse, &snapshot);
    assert(keys == (KEY_RIGHT | KEY_B));
    keys = complete_animated_step(&mouse, &snapshot);
    assert(keys == (KEY_DOWN | KEY_B));
    keys = complete_animated_step(&mouse, &snapshot);
    assert(keys == KEY_A);
    assert(!mouse.active);

    fe8_mouse_cancel(&mouse);
    fe8_mouse_set_target(&mouse, 3, 1, 0);
    snapshot.input_lock = 1;
    assert(fe8_mouse_update(&mouse, &snapshot, 1) == 0);
    assert(mouse.active); /* A temporary lock pauses rather than discarding. */
    snapshot.input_lock = 0;
    assert(fe8_mouse_update(&mouse, &snapshot, 1) == (KEY_RIGHT | KEY_B));

    fe8_mouse_cancel(&mouse);
    assert(!mouse.active && mouse.press_frames == 0 && !mouse.confirm);
    mouse.pulse_key = KEY_B;
    mouse.press_frames = 2;
    snapshot.input_lock = 1;
    assert(fe8_mouse_update(&mouse, &snapshot, 1) == KEY_B);
    assert(fe8_mouse_update(&mouse, &snapshot, 0) == KEY_B);

    fe8_mouse_cancel(&mouse);
    mouse.pulse_key = KEY_B;
    mouse.press_frames = 2;
    mouse.release_frames = 2;
    assert(fe8_mouse_update(&mouse, &snapshot, 1) == 0);
    assert(fe8_mouse_update(&mouse, &snapshot, 1) == 0);
    assert(fe8_mouse_update(&mouse, &snapshot, 1) == KEY_B);
    assert(fe8_mouse_update(&mouse, &snapshot, 1) == KEY_B);

    memset(&mouse, 0, sizeof(mouse));
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.map_width = 10;
    snapshot.map_height = 10;
    fe8_mouse_set_target(&mouse, 4, 0, 0);
    assert(fe8_mouse_update(&mouse, &snapshot, 1) == (KEY_RIGHT | KEY_B));
    for (int frame = 0; frame < 200 && !mouse.stalled; ++frame)
        (void)fe8_mouse_update(&mouse, &snapshot, 1);
    assert(mouse.stalled);
    assert(mouse.active && mouse.target_x == 4 && mouse.target_y == 0);
    fe8_mouse_set_target(&mouse, 2, 2, 0);
    assert(mouse.stalled);
    assert(mouse.target_x == 2 && mouse.target_y == 2);

    fe8_mouse_cancel(&mouse);
    fe8_mouse_set_target(&mouse, 8, 6, 1);
    assert(mouse.active && !mouse.stalled);
    assert(mouse.target_x == 8 && mouse.target_y == 6 && mouse.confirm);
    fe8_mouse_set_target(&mouse, 4, 4, 0);
    assert(mouse.target_x == 8 && mouse.target_y == 6); /* Click stays latched. */
    fe8_mouse_cancel(&mouse);
    fe8_mouse_set_target(&mouse, 4, 4, 0);
    assert(!mouse.confirm && mouse.target_x == 4 && mouse.target_y == 4);
    puts("mouse controller tests passed");
    return 0;
}
