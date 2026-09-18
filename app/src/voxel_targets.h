#ifndef FE8_VOXEL_TARGETS_H
#define FE8_VOXEL_TARGETS_H
#include "fe8_profile.h"
/* Optional, read-only structural recognition of FE8's native SelectTargetProc.
 * Unknown hack layouts keep native panel/key controls. No game RAM is written. */
typedef struct Fe8Target {uint32_t address; int x,y; unsigned unit;} Fe8Target;
typedef struct Fe8VoxelTargets {
    uint32_t proc,script,info;
    unsigned count,tick;
    Fe8Target targets[64]; /* Native next-link order, current target first. */
    bool active;
} Fe8VoxelTargets;
typedef struct Fe8TargetClick {
    uint32_t proc,info,node;
    int x,y;
    unsigned unit,frames;
    bool active,release;
} Fe8TargetClick;
void fe8_voxel_targets_read(Fe8VoxelTargets *targets,const Fe8MemoryReader *memory,
    const Fe8Snapshot *snapshot);
bool fe8_voxel_target_click(Fe8TargetClick *click,const Fe8VoxelTargets *targets,int x,int y);
uint32_t fe8_voxel_target_keys(Fe8TargetClick *click,const Fe8MemoryReader *memory,
    const Fe8Snapshot *snapshot);
#endif
