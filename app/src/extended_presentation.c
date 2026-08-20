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
    if (!enabled) {
        fe8_presentation_reset(presentation);
        return presentation->state;
    }
    if (presentation->state == FE8_PRESENTATION_LIVE) {
        if (!tactical_valid || freeze_requested) {
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
