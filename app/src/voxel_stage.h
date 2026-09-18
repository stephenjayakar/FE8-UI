#ifndef FE8_VOXEL_STAGE_H
#define FE8_VOXEL_STAGE_H
#include "address_space.h"
#include "voxel_renderer.h"

/* Owns a read-only copy of the last verified world's inputs, not a screenshot.
 * Full-screen native scenes reuse this world with the SAME interactive camera.
 * Never sample battle/menu VRAM as terrain or invent emulated combat results. */
typedef struct Fe8VoxelStage Fe8VoxelStage;
Fe8VoxelStage *fe8_voxel_stage_create(void);
void fe8_voxel_stage_destroy(Fe8VoxelStage *stage);
/* Reset/ROM replacement withdraws the old world; use a neutral 3D stage until
 * a new map validates. Ordinary scene transitions never call reset. */
void fe8_voxel_stage_reset(Fe8VoxelStage *stage);
bool fe8_voxel_stage_remember(Fe8VoxelStage *stage, const Fe8AddressSpace *space,
    const Fe8MapRenderState *map, const Fe8Snapshot *snapshot);
const Fe8MemoryView *fe8_voxel_stage_memory(const Fe8VoxelStage *stage);
const Fe8MapRenderState *fe8_voxel_stage_map(const Fe8VoxelStage *stage);
const Fe8Snapshot *fe8_voxel_stage_snapshot(const Fe8VoxelStage *stage);
bool fe8_voxel_stage_has_map(const Fe8VoxelStage *stage);
#endif
