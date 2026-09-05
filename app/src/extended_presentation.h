#ifndef FE8_EXTENDED_PRESENTATION_H
#define FE8_EXTENDED_PRESENTATION_H

#include <stdbool.h>
#include "frame_alignment.h"

typedef enum Fe8ExtendedPresentationState {
    FE8_PRESENTATION_INACTIVE,
    FE8_PRESENTATION_LIVE,
    FE8_PRESENTATION_FROZEN,
} Fe8ExtendedPresentationState;

typedef struct Fe8ExtendedPresentation {
    Fe8ExtendedPresentationState state;
    unsigned consecutive_valid_frames;
} Fe8ExtendedPresentation;

void fe8_presentation_reset(Fe8ExtendedPresentation *presentation);
Fe8ExtendedPresentationState fe8_presentation_update(
    Fe8ExtendedPresentation *presentation, bool enabled, bool tactical_valid,
    bool freeze_requested);

/* Native menus/transitions never inherit a tactical camera or cached placement. */
Fe8FramePlacement fe8_presentation_place_frame(
    const Fe8ExtendedPresentation *presentation, int canvas_width, int canvas_height,
    Fe8FramePlacement map_placement);

#endif
