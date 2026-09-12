#include "mouse_controller.h"

#include <math.h>
#include <stdio.h>

enum {
    FE8_KEY_A = 0,
    FE8_KEY_B = 1,
    FE8_KEY_RIGHT = 4,
    FE8_KEY_LEFT = 5,
    FE8_KEY_UP = 6,
    FE8_KEY_DOWN = 7,
    FE8_SCROLL_MAX_STEPS = 8,
    FE8_SCROLL_PRESS_FRAMES = 2,
    FE8_SCROLL_RELEASE_FRAMES = 2,
    FE8_SCROLL_IDLE_FRAMES = 18,
};

int fe8_mouse_native_ui(
    const Fe8LiveState *snapshot, int snapshot_valid, int map_active) {
    return !map_active || !snapshot_valid || snapshot->input_lock != 0;
}

void fe8_mouse_cancel_scroll(Fe8MouseController *mouse) {
    mouse->scroll_fraction = 0;
    mouse->scroll_steps = 0;
    mouse->scroll_direction = 0;
    mouse->scroll_press_frames = 0;
    mouse->scroll_release_frames = 0;
    mouse->scroll_idle_frames = 0;
    mouse->scroll_key = 0;
}

void fe8_mouse_scroll(Fe8MouseController *mouse, double wheel_delta) {
    int direction;
    int steps;
    if (!isfinite(wheel_delta) || wheel_delta == 0)
        return;
    direction = wheel_delta > 0 ? 1 : -1;
    if (mouse->active)
        fe8_mouse_cancel(mouse); /* Never carry fast-move B into a menu. */
    if (mouse->scroll_direction && mouse->scroll_direction != direction)
        fe8_mouse_cancel_scroll(mouse); /* Reversing cancels the old backlog. */
    mouse->scroll_direction = direction;
    mouse->scroll_idle_frames = 0;
    /* Bound accelerated wheel bursts before converting a double to an int. */
    if (wheel_delta > FE8_SCROLL_MAX_STEPS)
        wheel_delta = FE8_SCROLL_MAX_STEPS;
    else if (wheel_delta < -FE8_SCROLL_MAX_STEPS)
        wheel_delta = -FE8_SCROLL_MAX_STEPS;
    mouse->scroll_fraction += wheel_delta;
    steps = (int)mouse->scroll_fraction;
    mouse->scroll_fraction -= steps;
    mouse->scroll_steps += steps;
    if (mouse->scroll_steps > FE8_SCROLL_MAX_STEPS)
        mouse->scroll_steps = FE8_SCROLL_MAX_STEPS;
    else if (mouse->scroll_steps < -FE8_SCROLL_MAX_STEPS)
        mouse->scroll_steps = -FE8_SCROLL_MAX_STEPS;
    if (steps && !mouse->scroll_press_frames && !mouse->scroll_release_frames)
        mouse->scroll_release_frames = FE8_SCROLL_RELEASE_FRAMES;
}

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
    fe8_mouse_cancel_scroll(mouse);
}

uint32_t fe8_mouse_update(
    Fe8MouseController *mouse, const Fe8LiveState *snapshot, int snapshot_valid) {
    uint32_t result = 0;
    /* Direct pulses (especially right-click B) must work while map input is
       locked by a menu or transition. */
    if (mouse->release_frames > 0) {
        --mouse->release_frames;
        return mouse->active ? UINT32_C(1) << FE8_KEY_B : 0;
    }
    if (mouse->press_frames > 0) {
        --mouse->press_frames;
        return mouse->pulse_key;
    }
    /* Wheel steps are native D-pad pulses, not map travel: no B modifier and
     * no dependency on a valid map snapshot. Leave release frames between
     * notches so FE8 sees distinct key edges, even during fast-forward. */
    if (mouse->scroll_direction) {
        if (mouse->scroll_release_frames > 0) {
            --mouse->scroll_release_frames;
            return 0;
        }
        if (mouse->scroll_press_frames > 0) {
            if (--mouse->scroll_press_frames == 0)
                mouse->scroll_release_frames = FE8_SCROLL_RELEASE_FRAMES;
            return mouse->scroll_key;
        }
        if (mouse->scroll_steps) {
            int up = mouse->scroll_steps > 0;
            mouse->scroll_steps += up ? -1 : 1;
            mouse->scroll_key = UINT32_C(1) << (up ? FE8_KEY_UP : FE8_KEY_DOWN);
            mouse->scroll_press_frames = FE8_SCROLL_PRESS_FRAMES - 1;
            return mouse->scroll_key;
        }
        if (++mouse->scroll_idle_frames >= FE8_SCROLL_IDLE_FRAMES)
            fe8_mouse_cancel_scroll(mouse);
    }
    if (!snapshot_valid) {
        if (mouse->active)
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
            /* The preceding animation frame contained B without a direction,
             * so the next direction is already a fresh FE8 key edge. Continue
             * immediately instead of inserting two redundant settling frames. */
        }
        if (mouse->step_active) {
            if (++mouse->wait_frames <= (logical_arrived ? 90 : 16))
                return UINT32_C(1) << FE8_KEY_B;
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
            return mouse->pulse_key | (UINT32_C(1) << FE8_KEY_B);
        }
    }
    if (snapshot->cursor_x == mouse->target_x && snapshot->cursor_y == mouse->target_y) {
        if (snapshot->cursor_display_x != mouse->target_x * 16 ||
                snapshot->cursor_display_y != mouse->target_y * 16)
            return UINT32_C(1) << FE8_KEY_B;
        mouse->active = 0;
        if (!mouse->confirm) {
            mouse->confirm = 0;
            return 0;
        }
        mouse->confirm = 0;
        result = UINT32_C(1) << FE8_KEY_A;
        fprintf(stderr, "Mouse confirm: A at %d,%d\n", mouse->target_x, mouse->target_y);
        return result; /* Release fast-move B before confirming. */
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
    return result | (UINT32_C(1) << FE8_KEY_B);
}
