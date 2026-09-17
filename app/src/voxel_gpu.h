#ifndef FE8_VOXEL_GPU_H
#define FE8_VOXEL_GPU_H

#include "extended_map_renderer.h"

/* Read-only, backend-neutral draw packet. World units are original map pixels.
 * The GPU owns rasterization, depth testing, texture sampling and blending.
 * Revisions describe content, not frames: camera motion never rebuilds scenery. */
typedef struct Fe8GpuVertex {
    float x, y, z, u, v;
    uint32_t rgba;
} Fe8GpuVertex;
typedef struct Fe8GpuMesh {
    Fe8GpuVertex *vertices;
    size_t count, capacity;
    uint64_t revision;
} Fe8GpuMesh;
typedef struct Fe8VoxelGpuFrame {
    Fe8GpuMesh scenery, overlays, billboards;
    const Fe8HostPixel *ground, *atlas;
    int map_width, map_height, width, height;
    int atlas_width, atlas_height;
    uint64_t ground_revision, atlas_revision;
    /* column-major world -> OpenGL clip space */
    float transform[16];
} Fe8VoxelGpuFrame;

/* OpenGL 3.2 implementation; requires a current SDL GL context. Dynamically
 * loaded entry points avoid a new GL link dependency on portable platforms. */
typedef struct Fe8VoxelGl Fe8VoxelGl;
Fe8VoxelGl *fe8_voxel_gl_create(void);
void fe8_voxel_gl_destroy(Fe8VoxelGl *gpu);
int fe8_voxel_gl_draw(Fe8VoxelGl *gpu, const Fe8VoxelGpuFrame *frame,
    int drawable_width, int drawable_height);
/* Explicit capture only: the interactive draw path never reads the GPU back. */
int fe8_voxel_gl_capture(Fe8VoxelGl *gpu, Fe8HostPixel *pixels,
    int width, int height);
#endif
