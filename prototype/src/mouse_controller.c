#include "mouse_controller.h"

#include <stdio.h>

enum {
    FE8_KEY_A = 0,
    FE8_KEY_RIGHT = 4,
    FE8_KEY_LEFT = 5,
    FE8_KEY_UP = 6,
    FE8_KEY_DOWN = 7,
};

void fe8_mouse_set_target(Fe8MouseController *mouse, int x, int y, int confirm) {
    mouse->active = 1;
    mouse->target_x = x;
    mouse->target_y = y;
    mouse->confirm = confirm;
}

void fe8_mouse_cancel(Fe8MouseController *mouse) {
    mouse->active = 0;
    mouse->press_frames = 0;
    mouse->release_frames = 0;
    mouse->wait_frames = 0;
    mouse->blocked_frames = 0;
}

uint32_t fe8_mouse_update(
    Fe8MouseController *mouse, const Fe8Snapshot *snapshot, int snapshot_valid) {
    uint32_t result = 0;
    if (!snapshot_valid) {
        fe8_mouse_cancel(mouse);
        return 0;
    }
    if (snapshot->input_lock != 0) {
        if (++mouse->blocked_frames > 300) {
            fprintf(stderr, "Mouse path cancelled: FE8 input remained locked\n");
            fe8_mouse_cancel(mouse);
        }
        return 0;
    }
    mouse->blocked_frames = 0;
    if (mouse->press_frames > 0) {
        --mouse->press_frames;
        return mouse->pulse_key;
    }
    if (mouse->release_frames > 0) {
        --mouse->release_frames;
        return 0;
    }
    if (!mouse->active)
        return 0;
    if (mouse->wait_frames > 0) {
        if (snapshot->cursor_x != mouse->issued_x || snapshot->cursor_y != mouse->issued_y) {
            mouse->wait_frames = 0;
            mouse->release_frames = 2;
            mouse->retries = 0;
            return 0;
        }
        --mouse->wait_frames;
        if (mouse->wait_frames > 0)
            return 0;
        if (++mouse->retries > 3) {
            fprintf(stderr, "Mouse path cancelled: FE8 cursor did not acknowledge input\n");
            mouse->active = 0;
            return 0;
        }
    }
    if (snapshot->cursor_x == mouse->target_x && snapshot->cursor_y == mouse->target_y) {
        mouse->active = 0;
        if (!mouse->confirm)
            return 0;
        result = UINT32_C(1) << FE8_KEY_A;
        fprintf(stderr, "Mouse confirm: A at %d,%d\n", mouse->target_x, mouse->target_y);
    } else if (snapshot->cursor_x < mouse->target_x)
        result = UINT32_C(1) << FE8_KEY_RIGHT;
    else if (snapshot->cursor_x > mouse->target_x)
        result = UINT32_C(1) << FE8_KEY_LEFT;
    else if (snapshot->cursor_y < mouse->target_y)
        result = UINT32_C(1) << FE8_KEY_DOWN;
    else
        result = UINT32_C(1) << FE8_KEY_UP;
    mouse->pulse_key = result;
    mouse->press_frames = 2;
    mouse->wait_frames = 16;
    mouse->issued_x = snapshot->cursor_x;
    mouse->issued_y = snapshot->cursor_y;
    return result;
}
