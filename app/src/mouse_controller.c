#include "mouse_controller.h"

#include <stdio.h>

enum {
    FE8_KEY_A = 0,
    FE8_KEY_B = 1,
    FE8_KEY_RIGHT = 4,
    FE8_KEY_LEFT = 5,
    FE8_KEY_UP = 6,
    FE8_KEY_DOWN = 7,
};

static void fe8_mouse_cancel_path(Fe8MouseController *mouse) {
    mouse->active = 0;
    mouse->press_frames = 0;
    mouse->release_frames = 0;
    mouse->wait_frames = 0;
    mouse->step_active = 0;
    mouse->blocked_frames = 0;
    mouse->stalled = 0;
    mouse->confirm = 0;
    mouse->safe_navigation = 0;
    mouse->retries = 0;
}

void fe8_mouse_set_target_safe(Fe8MouseController *mouse,
    int x, int y, int confirm, int safe_navigation) {
    if (mouse->active && mouse->confirm && !confirm)
        return;
    mouse->queued_head = 0;
    mouse->queued_count = 0;
    mouse->queued_release_frames = 0;
    mouse->active = 1;
    mouse->target_x = x;
    mouse->target_y = y;
    mouse->confirm = mouse->confirm || confirm;
    mouse->safe_navigation = safe_navigation != 0;
    /* An explicit click remains latched while incidental hover events arrive. */
}

void fe8_mouse_set_target(Fe8MouseController *mouse, int x, int y, int confirm) {
    fe8_mouse_set_target_safe(mouse, x, y, confirm, 0);
}

void fe8_mouse_queue_pulse(Fe8MouseController *mouse, uint32_t key) {
    unsigned tail;
    if (!mouse || !key)
        return;
    fe8_mouse_cancel_path(mouse);
    if (mouse->queued_count == FE8_MOUSE_QUEUE_CAPACITY) {
        mouse->queued_head = (mouse->queued_head + 1) % FE8_MOUSE_QUEUE_CAPACITY;
        --mouse->queued_count;
    }
    tail = (mouse->queued_head + mouse->queued_count) % FE8_MOUSE_QUEUE_CAPACITY;
    mouse->queued_pulses[tail] = key;
    ++mouse->queued_count;
}

void fe8_mouse_cancel(Fe8MouseController *mouse) {
    fe8_mouse_cancel_path(mouse);
    mouse->queued_head = 0;
    mouse->queued_count = 0;
    mouse->queued_release_frames = 0;
}

uint32_t fe8_mouse_update(
    Fe8MouseController *mouse, const Fe8LiveState *snapshot, int snapshot_valid) {
    uint32_t result = 0;
    /* Menu pulses are intentionally independent of the FE8 tactical snapshot.
       Native menus and target procs may hold the battle-map game lock while
       still polling the normal GBA key state. */
    if (mouse->queued_release_frames > 0) {
        --mouse->queued_release_frames;
        return 0;
    }
    if (mouse->queued_count > 0) {
        result = mouse->queued_pulses[mouse->queued_head];
        mouse->queued_head = (mouse->queued_head + 1) % FE8_MOUSE_QUEUE_CAPACITY;
        --mouse->queued_count;
        mouse->queued_release_frames = 1;
        return result;
    }
    /* Direct pulses (especially legacy right-click B) must work while map
       input is locked by a menu or transition. */
    if (mouse->release_frames > 0) {
        --mouse->release_frames;
        return mouse->active && !mouse->safe_navigation ?
            UINT32_C(1) << FE8_KEY_B : 0;
    }
    if (mouse->press_frames > 0) {
        --mouse->press_frames;
        return mouse->pulse_key;
    }
    if (!snapshot_valid) {
        fe8_mouse_cancel_path(mouse);
        return 0;
    }
    if (!mouse->active)
        return 0;
    /* Safe navigation is used for occupied target tiles. Target selection
       procs can keep the battle-map lock non-zero while still accepting
       D-pad/A; unlike free-map travel, do not wait on that lock. */
    if (snapshot->input_lock != 0 && !mouse->safe_navigation) {
        if (++mouse->blocked_frames > 300) {
            fprintf(stderr, "Mouse path cancelled: FE8 input remained locked\n");
            fe8_mouse_cancel_path(mouse);
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
        }
        if (mouse->step_active) {
            if (++mouse->wait_frames <= (logical_arrived ? 90 : 16))
                return mouse->safe_navigation ? 0 : UINT32_C(1) << FE8_KEY_B;
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
            return mouse->pulse_key |
                (mouse->safe_navigation ? 0 : UINT32_C(1) << FE8_KEY_B);
        }
    }
    if (snapshot->cursor_x == mouse->target_x && snapshot->cursor_y == mouse->target_y) {
        if (snapshot->cursor_display_x != mouse->target_x * 16 ||
                snapshot->cursor_display_y != mouse->target_y * 16)
            return mouse->safe_navigation ? 0 : UINT32_C(1) << FE8_KEY_B;
        mouse->active = 0;
        if (!mouse->confirm) {
            mouse->confirm = 0;
            mouse->safe_navigation = 0;
            return 0;
        }
        mouse->confirm = 0;
        mouse->safe_navigation = 0;
        result = UINT32_C(1) << FE8_KEY_A;
        fprintf(stderr, "Mouse confirm: A at %d,%d\n", mouse->target_x, mouse->target_y);
        return result;
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
    return result | (mouse->safe_navigation ? 0 : UINT32_C(1) << FE8_KEY_B);
}
