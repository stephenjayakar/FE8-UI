#ifndef FE8_MOUSE_CONTROLLER_H
#define FE8_MOUSE_CONTROLLER_H

#include "fe8_profile.h"

#include <stdint.h>

typedef struct Fe8MouseController {
    int active;
    int target_x;
    int target_y;
    int confirm;
    uint32_t pulse_key;
    int press_frames;
    int release_frames;
    int wait_frames;
    int issued_x;
    int issued_y;
    int step_active;
    int retries;
    int blocked_frames;
    int stalled;
    double scroll_fraction;
    int scroll_steps;
    int scroll_direction;
    int scroll_press_frames;
    int scroll_release_frames;
    int scroll_idle_frames;
    uint32_t scroll_key;
} Fe8MouseController;

/* Match native A/B routing; keep map zoom independent of menu navigation. */
int fe8_mouse_native_ui(
    const Fe8LiveState *snapshot, int snapshot_valid, int map_active);
/* Positive wheel units select Up, negative units select Down. */
void fe8_mouse_scroll(Fe8MouseController *mouse, double wheel_delta);
void fe8_mouse_cancel_scroll(Fe8MouseController *mouse);

void fe8_mouse_set_target(Fe8MouseController *mouse, int x, int y, int confirm);
void fe8_mouse_cancel(Fe8MouseController *mouse);
uint32_t fe8_mouse_update(
    Fe8MouseController *mouse, const Fe8LiveState *snapshot, int snapshot_valid);

#endif
