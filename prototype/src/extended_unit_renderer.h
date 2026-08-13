#ifndef FE8_EXTENDED_UNIT_RENDERER_H
#define FE8_EXTENDED_UNIT_RENDERER_H

#include "extended_map_renderer.h"
#include "fe8_profile.h"

/* Draws standing map sprites and a host-side cursor over extended terrain. */
unsigned fe8_render_extended_units(
    const Fe8MemoryView *memory,
    const Fe8Snapshot *snapshot,
    Fe8ExtendedViewport viewport,
    Fe8HostPixel *pixels,
    size_t stride_pixels,
    unsigned animation_frame);

#endif
