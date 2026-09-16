#include "voxel_presentation.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
int main(void) {
    Fe8Snapshot *s=calloc(1,sizeof(*s)); assert(s);
    assert(fe8_voxel_scene(false,true,true,s,true,false,true)==FE8_VOXEL_OFF);
    assert(fe8_voxel_scene(true,false,true,s,true,false,true)==FE8_VOXEL_UNSUPPORTED_ROM);
    assert(fe8_voxel_scene(true,true,false,s,true,false,true)==FE8_VOXEL_EXTENSIONS_OFF);
    assert(fe8_voxel_scene(true,true,true,NULL,true,false,true)==FE8_VOXEL_WAITING_MAP);
    assert(fe8_voxel_scene(true,true,true,s,true,true,true)==FE8_VOXEL_INVENTORY);
    s->input_lock=1;
    assert(fe8_voxel_scene(true,true,true,s,true,false,true)==FE8_VOXEL_BUSY);
    s->input_lock=0;s->phase=1;
    assert(fe8_voxel_scene(true,true,true,s,true,false,true)==FE8_VOXEL_BUSY);
    s->phase=0;s->combat_panel_active=1;
    assert(fe8_voxel_scene(true,true,true,s,true,false,true)==FE8_VOXEL_BUSY);
    s->combat_panel_active=0;
    for (int bit=1;bit<=2;++bit) {
        s->game_state_bits=bit;
        assert(fe8_voxel_scene(true,true,true,s,true,false,true)==FE8_VOXEL_SELECTED);
    }
    s->game_state_bits=0;
    assert(fe8_voxel_scene(true,true,true,s,false,false,true)==FE8_VOXEL_WAITING_MAP);
    assert(fe8_voxel_scene(true,true,true,s,true,false,false)==FE8_VOXEL_WAITING_HUD);
    assert(fe8_voxel_scene(true,true,true,s,true,false,true)==FE8_VOXEL_READY);
    for (int scene=FE8_VOXEL_OFF;scene<=FE8_VOXEL_READY;++scene)
        assert(fe8_voxel_scene_label((Fe8VoxelScene)scene)[0]);
    free(s);puts("PASS every voxel activation/fallback reason and restoration");return 0;
}
