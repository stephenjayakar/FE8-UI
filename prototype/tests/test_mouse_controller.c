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

static void settle_press(Fe8MouseController *mouse, Fe8Snapshot *snapshot) {
    int i;
    for (i = 0; i < 2; ++i)
        (void)fe8_mouse_update(mouse, snapshot, 1);
    if (mouse->pulse_key == KEY_RIGHT)
        ++snapshot->cursor_x;
    else if (mouse->pulse_key == KEY_DOWN)
        ++snapshot->cursor_y;
    for (i = 0; i < 4; ++i)
        (void)fe8_mouse_update(mouse, snapshot, 1);
}

int main(void) {
    Fe8MouseController mouse;
    Fe8Snapshot snapshot;
    uint32_t keys;
    memset(&mouse, 0, sizeof(mouse));
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.map_width = 10;
    snapshot.map_height = 10;

    fe8_mouse_set_target(&mouse, 2, 1, 1);
    keys = fe8_mouse_update(&mouse, &snapshot, 1);
    assert(keys == KEY_RIGHT);
    settle_press(&mouse, &snapshot);
    keys = fe8_mouse_update(&mouse, &snapshot, 1);
    assert(keys == KEY_RIGHT);
    settle_press(&mouse, &snapshot);
    keys = fe8_mouse_update(&mouse, &snapshot, 1);
    assert(keys == KEY_DOWN);
    settle_press(&mouse, &snapshot);
    keys = fe8_mouse_update(&mouse, &snapshot, 1);
    assert(keys == KEY_A);
    assert(!mouse.active);

    fe8_mouse_cancel(&mouse);
    fe8_mouse_set_target(&mouse, 3, 1, 0);
    snapshot.input_lock = 1;
    assert(fe8_mouse_update(&mouse, &snapshot, 1) == 0);
    assert(mouse.active); /* A temporary lock pauses rather than discarding. */
    snapshot.input_lock = 0;
    assert(fe8_mouse_update(&mouse, &snapshot, 1) == KEY_RIGHT);

    fe8_mouse_cancel(&mouse);
    assert(!mouse.active && mouse.press_frames == 0);
    mouse.pulse_key = KEY_B;
    mouse.press_frames = 2;
    snapshot.input_lock = 1;
    assert(fe8_mouse_update(&mouse, &snapshot, 1) == KEY_B);
    assert(fe8_mouse_update(&mouse, &snapshot, 0) == KEY_B);

    memset(&mouse, 0, sizeof(mouse));
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.map_width = 10;
    snapshot.map_height = 10;
    fe8_mouse_set_target(&mouse, 4, 0, 0);
    for (int frame = 0; frame < 100 && !mouse.teleport_requested; ++frame)
        (void)fe8_mouse_update(&mouse, &snapshot, 1);
    assert(mouse.teleport_requested);
    assert(mouse.active && mouse.target_x == 4 && mouse.target_y == 0);
    fe8_mouse_set_target(&mouse, 2, 2, 0);
    assert(!mouse.teleport_requested);
    puts("mouse controller tests passed");
    return 0;
}
