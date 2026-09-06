#include "extended_presentation.h"

#include <string.h>

void fe8_presentation_reset(Fe8ExtendedPresentation *presentation) {
    if (presentation)
        memset(presentation, 0, sizeof(*presentation));
}

Fe8ExtendedPresentationState fe8_presentation_update(
    Fe8ExtendedPresentation *presentation, bool enabled, bool tactical_valid,
    bool freeze_requested) {
    if (!presentation)
        return FE8_PRESENTATION_INACTIVE;
    /* Missing/obscured map evidence is not permission to retain a panned
     * framebuffer. Only a recognized map overlay may freeze a live scene. */
    if (!enabled || (!tactical_valid && !freeze_requested)) {
        fe8_presentation_reset(presentation);
        return presentation->state;
    }
    if (presentation->state == FE8_PRESENTATION_LIVE) {
        if (freeze_requested) {
            presentation->state = FE8_PRESENTATION_FROZEN;
            presentation->consecutive_valid_frames = 0;
        }
        return presentation->state;
    }
    if (freeze_requested) {
        presentation->consecutive_valid_frames = 0;
        return presentation->state;
    }
    if (tactical_valid)
        ++presentation->consecutive_valid_frames;
    else
        presentation->consecutive_valid_frames = 0;
    if (presentation->consecutive_valid_frames >= 2) {
        presentation->state = FE8_PRESENTATION_LIVE;
        presentation->consecutive_valid_frames = 0;
    } else if (presentation->state != FE8_PRESENTATION_FROZEN) {
        presentation->state = FE8_PRESENTATION_INACTIVE;
    }
    return presentation->state;
}

Fe8FramePlacement fe8_presentation_frame_placement(
    bool extended_visible, int canvas_width, int canvas_height,
    Fe8FramePlacement map_placement) {
    if (extended_visible)
        return map_placement;
    return (Fe8FramePlacement){(canvas_width - 240) / 2,
        (canvas_height - 160) / 2, 0};
}
