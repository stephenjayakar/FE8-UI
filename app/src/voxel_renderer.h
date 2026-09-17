#ifndef FE8_VOXEL_RENDERER_H
#define FE8_VOXEL_RENDERER_H
#include "extended_map_renderer.h"
#include "fe8_profile.h"
#include "voxel_buildings.h"
#include "voxel_gpu.h"

typedef struct Fe8VoxelRenderer Fe8VoxelRenderer;
typedef struct Fe8VoxelStats {
    unsigned terrain_builds, sprite_builds, cached_sprites, columns, sprites;
    unsigned background_builds, reused_frames, ground_refreshes;
    unsigned active_unit_sprites, unit_moving;
    float active_x, active_y;
    unsigned hover_sprites; /* Native hovered-unit replacements, not hidden SMS. */
    uint64_t billboard_pixels, output_pixels;
    int render_width, render_height;
    unsigned buildings[FE8_BUILDING_COUNT];
} Fe8VoxelStats;

/* Entirely presentational: read8 is the only emulator capability accepted. */
Fe8VoxelRenderer *fe8_voxel_create(void);
void fe8_voxel_destroy(Fe8VoxelRenderer *view);
void fe8_voxel_invalidate(Fe8VoxelRenderer *view);
void fe8_voxel_camera(Fe8VoxelRenderer *view, float yaw_delta, float zoom_factor);
void fe8_voxel_pan(Fe8VoxelRenderer *view, float screen_dx, float screen_dy);
void fe8_voxel_focus(Fe8VoxelRenderer *view, float map_x, float map_y);
void fe8_voxel_home(Fe8VoxelRenderer *view);
/* Returns NULL on invalid data/allocation failure; caller retains native view.
 * Returned RGBA pixels are width*height, owned by view until the next render.
 * The scene is bounded to 1920x1080 and upscaled when needed; project/pick/pan
 * always accept/return drawable coordinates, including on Retina displays. */
/* Interactive path: returns a bounded scene, NOT a drawable-sized buffer.
 * Use stats.render_width/render_height for its dimensions. GPU presentation
 * stretches it to the drawable. Picking and panning remain drawable-based.
 * No full-resolution CPU buffer is allocated, scaled, or copied here. */
const Fe8HostPixel *fe8_voxel_render_scene(Fe8VoxelRenderer *view,
    const Fe8MemoryView *memory, const Fe8MapRenderState *map,
    const Fe8Snapshot *snapshot, int drawable_width, int drawable_height);
/* Build a world-space draw packet without software rasterization. */
const Fe8VoxelGpuFrame *fe8_voxel_build_gpu(Fe8VoxelRenderer *view,
    const Fe8MemoryView *memory, const Fe8MapRenderState *map,
    const Fe8Snapshot *snapshot, int drawable_width, int drawable_height);
/* Expand the last scene for an explicit screenshot. Owned by view. */
Fe8HostPixel *fe8_voxel_capture(Fe8VoxelRenderer *view);
Fe8HostPixel *fe8_voxel_render(Fe8VoxelRenderer *view,
    const Fe8MemoryView *memory, const Fe8MapRenderState *map,
    const Fe8Snapshot *snapshot, int width, int height);
bool fe8_voxel_pick(const Fe8VoxelRenderer *view, float screen_x, float screen_y,
    int *tile_x, int *tile_y);
bool fe8_voxel_project(const Fe8VoxelRenderer *view, float map_x, float map_y,
    float height, float *screen_x, float *screen_y);
Fe8VoxelStats fe8_voxel_stats(const Fe8VoxelRenderer *view);
const char *fe8_voxel_error(const Fe8VoxelRenderer *view);
bool fe8_voxel_building_at(const Fe8VoxelRenderer *view,int x,int y,Fe8Building *out);
#endif
