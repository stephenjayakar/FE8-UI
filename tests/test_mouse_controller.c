#include "mouse_controller.h"

#include <assert.h>
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

int main(void) {
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
