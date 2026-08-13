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
    if (mouse->active && mouse->confirm && !confirm)
        return;
    mouse->active = 1;
    mouse->target_x = x;
    mouse->target_y = y;
    mouse->confirm = mouse->confirm || confirm;
    /* An explicit click remains latched while incidental hover events arrive. */
}

void fe8_mouse_cancel(Fe8MouseController *mouse) {
    mouse->active = 0;
    mouse->press_frames = 0;
    mouse->release_frames = 0;
    mouse->wait_frames = 0;
    mouse->step_active = 0;
    mouse->blocked_frames = 0;
    mouse->stalled = 0;
    mouse->confirm = 0;
    mouse->retries = 0;
}

uint32_t fe8_mouse_update(
    Fe8MouseController *mouse, const Fe8Snapshot *snapshot, int snapshot_valid) {
    uint32_t result = 0;
    /* Direct pulses (especially right-click B) must work while map input is
       locked by a menu or transition. */
    if (mouse->press_frames > 0) {
        --mouse->press_frames;
        return mouse->pulse_key;
    }
    if (mouse->release_frames > 0) {
        --mouse->release_frames;
        return 0;
    }
    if (!snapshot_valid) {
        fe8_mouse_cancel(mouse);
        return 0;
    }
    if (!mouse->active)
        return 0;
    if (snapshot->input_lock != 0) {
        if (++mouse->blocked_frames > 300) {
            fprintf(stderr, "Mouse path cancelled: FE8 input remained locked\n");
            fe8_mouse_cancel(mouse);
        }
        return 0;
    }
    mouse->blocked_frames = 0;
    if (mouse->stalled)
        return 0;
    if (mouse->step_active) {
        int logical_arrived = snapshot->cursor_x == mouse->issued_x &&
            snapshot->cursor_y == mouse->issued_y;
        int display_arrived = snapshot->cursor_display_x == mouse->issued_x * 16 &&
            snapshot->cursor_display_y == mouse->issued_y * 16 &&
            snapshot->cursor_target_x == mouse->issued_x * 16 &&
            snapshot->cursor_target_y == mouse->issued_y * 16;
        if (logical_arrived && display_arrived) {
            mouse->step_active = 0;
            mouse->wait_frames = 0;
            mouse->retries = 0;
            mouse->release_frames = 1;
            return 0;
        }
        if (++mouse->wait_frames <= (logical_arrived ? 90 : 16))
            return 0;
        mouse->wait_frames = 0;
        if (logical_arrived || ++mouse->retries > 3) {
            fprintf(stderr, "Mouse path stalled: cursor animation/input stopped at %u,%u (%d,%d px); target=%d,%d\n",
                snapshot->cursor_x, snapshot->cursor_y,
                snapshot->cursor_display_x, snapshot->cursor_display_y,
                mouse->target_x, mouse->target_y);
            mouse->stalled = 1;
            return 0;
        }
        mouse->release_frames = 1;
        return mouse->pulse_key;
    }
    if (snapshot->cursor_x == mouse->target_x && snapshot->cursor_y == mouse->target_y) {
        if (snapshot->cursor_display_x != mouse->target_x * 16 ||
                snapshot->cursor_display_y != mouse->target_y * 16)
            return 0;
        mouse->active = 0;
        if (!mouse->confirm) {
            mouse->confirm = 0;
            return 0;
        }
        mouse->confirm = 0;
        result = UINT32_C(1) << FE8_KEY_A;
        fprintf(stderr, "Mouse confirm: A at %d,%d\n", mouse->target_x, mouse->target_y);
    } else if (snapshot->cursor_x < mouse->target_x) {
        result = UINT32_C(1) << FE8_KEY_RIGHT;
        mouse->issued_x = snapshot->cursor_x + 1;
        mouse->issued_y = snapshot->cursor_y;
    } else if (snapshot->cursor_x > mouse->target_x) {
        result = UINT32_C(1) << FE8_KEY_LEFT;
        mouse->issued_x = snapshot->cursor_x - 1;
        mouse->issued_y = snapshot->cursor_y;
    } else if (snapshot->cursor_y < mouse->target_y) {
        result = UINT32_C(1) << FE8_KEY_DOWN;
        mouse->issued_x = snapshot->cursor_x;
        mouse->issued_y = snapshot->cursor_y + 1;
    } else {
        result = UINT32_C(1) << FE8_KEY_UP;
        mouse->issued_x = snapshot->cursor_x;
        mouse->issued_y = snapshot->cursor_y - 1;
    }
    mouse->pulse_key = result;
    mouse->step_active = 1;
    mouse->wait_frames = 0;
    mouse->release_frames = 1;
    return result;
}
