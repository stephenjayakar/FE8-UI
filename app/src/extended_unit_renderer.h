#ifndef FE8_EXTENDED_UNIT_RENDERER_H
#define FE8_EXTENDED_UNIT_RENDERER_H

#include "extended_map_renderer.h"
#include "fe8_profile.h"

/* Draws FE8's complete standing SMS list (units and map-sprite effects),
 * unit markers, and a host-side cursor over extended terrain. */
unsigned fe8_render_extended_units(
    const Fe8MemoryView *memory,
    const Fe8Snapshot *snapshot,
    Fe8ExtendedViewport viewport,
    Fe8HostPixel *pixels,
    size_t stride_pixels,
    unsigned animation_frame);

/* Reconstructs FE8's BG2 movement/range layer across the host canvas. */
unsigned fe8_render_extended_move_range(
    const Fe8MemoryView *memory,
    const Fe8Snapshot *snapshot,
    Fe8ExtendedViewport viewport,
    Fe8HostPixel *pixels,
    size_t stride_pixels);

/* Detects the FE8 map-HP-bars patch from bars emitted into native OAM. */
bool fe8_detect_native_unit_hp_bars(
    const Fe8MemoryView *memory,
    const Fe8Snapshot *snapshot);

#endif
