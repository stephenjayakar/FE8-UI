#ifndef FE8_EXTENDED_MAP_RENDERER_H
#define FE8_EXTENDED_MAP_RENDERER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Pixel layout is 0xAABBGGRR, i.e. RGBA bytes on little-endian hosts. */
typedef uint32_t Fe8HostPixel;

#ifndef FE8_READ8_DEFINED
#define FE8_READ8_DEFINED
typedef uint8_t (*Fe8Read8)(void *context, uint32_t address);
#endif

typedef struct Fe8MemoryView {
    void *context;
    Fe8Read8 read8;
} Fe8MemoryView;

typedef struct Fe8PaletteMapping {
    uint8_t bank[2][16];
    uint16_t valid_mask[2];
    uint8_t pending_bank[2][16];
    uint8_t confirmation_frames[2][16];
} Fe8PaletteMapping;

typedef struct Fe8TerrainCache Fe8TerrainCache;

typedef struct Fe8MapRenderState {
    uint16_t map_width;
    uint16_t map_height;
    int16_t camera_x;
    int16_t camera_y;
    uint32_t base_tile_rows;
    uint32_t fog_rows;
    uint32_t tileset_config;
    uint32_t tile_graphics;
    uint32_t palette;
    uint8_t normal_palette_bank_offset;
    uint8_t fog_palette_bank_offset;
    Fe8PaletteMapping *palette_mapping;
    Fe8TerrainCache *tile_cache;
} Fe8MapRenderState;

typedef struct Fe8ExtendedViewport {
    int width;
    int height;
    int gba_x;
    int gba_y;
} Fe8ExtendedViewport;

bool fe8_extended_state_is_sane(const Fe8MapRenderState *state);

/*
 * Renders FE8's logical terrain at the current camera position. The caller
 * overlays mGBA's 240x160 framebuffer at viewport.gba_x/.gba_y afterwards.
 */
bool fe8_render_extended_terrain(
    const Fe8MemoryView *memory,
    const Fe8MapRenderState *state,
    Fe8ExtendedViewport viewport,
    Fe8HostPixel *pixels,
    size_t stride_pixels);

void fe8_palette_mapping_reset(Fe8PaletteMapping *mapping);
unsigned fe8_learn_palette_mapping(
    const Fe8MemoryView *memory,
    const Fe8MapRenderState *state,
    const Fe8HostPixel *native_frame,
    size_t frame_stride);

Fe8TerrainCache *fe8_terrain_cache_create(void);
void fe8_terrain_cache_reset(Fe8TerrainCache *cache);
void fe8_terrain_cache_destroy(Fe8TerrainCache *cache);

/* Converts a host-canvas point to the map tile under it. */
bool fe8_canvas_to_map_tile(
    const Fe8MapRenderState *state,
    Fe8ExtendedViewport viewport,
    int canvas_x,
    int canvas_y,
    int *map_x,
    int *map_y);

#endif
