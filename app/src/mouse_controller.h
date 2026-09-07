#ifndef FE8_MOUSE_CONTROLLER_H
#define FE8_MOUSE_CONTROLLER_H

#include "fe8_profile.h"

#include <stdint.h>

enum { FE8_MOUSE_QUEUE_CAPACITY = 16 };

typedef struct Fe8MouseController {
    int active;
    int target_x;
    int target_y;
    int confirm;
    int safe_navigation;
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
    uint32_t queued_pulses[FE8_MOUSE_QUEUE_CAPACITY];
    unsigned queued_head;
    unsigned queued_count;
    int queued_release_frames;
} Fe8MouseController;

void fe8_mouse_set_target(Fe8MouseController *mouse, int x, int y, int confirm);
void fe8_mouse_set_target_safe(Fe8MouseController *mouse,
    int x, int y, int confirm, int safe_navigation);
void fe8_mouse_queue_pulse(Fe8MouseController *mouse, uint32_t key);
void fe8_mouse_cancel(Fe8MouseController *mouse);
uint32_t fe8_mouse_update(
    Fe8MouseController *mouse, const Fe8LiveState *snapshot, int snapshot_valid);

#endif
