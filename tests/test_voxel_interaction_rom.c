/* Optional end-to-end read-only presentation regression. All cursor movement
 * and hover/selection transitions are produced by FE8 through normal keys.
 * User ROMs stay local; nothing is written to the cartridge or save files. */
#include <mgba/flags.h>
#include <mgba/core/core.h>
#include <mgba/core/interface.h>
#include <mgba/core/log.h>
#include <mgba-util/vfs.h>
#include "address_space.h"
#include "extended_presentation.h"
#include "extended_unit_renderer.h"
#include "mouse_controller.h"
#include "native_hud.h"
#include "voxel_presentation.h"
#include "voxel_renderer.h"
#include <assert.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Test {
    struct mCore *core;
    const Fe8Profile *profile;
    Fe8MemoryReader reader;
    Fe8MemoryView memory;
    Fe8Snapshot snapshot;
    Fe8NativeHud hud;
    Fe8MapRenderState map;
    Fe8ExtendedPresentation presentation;
    Fe8VoxelRenderer *voxel;
    mColor video[240*160];
    Fe8HostPixel frame[240*160], terrain[480*320];
    unsigned frames, live_frames, hover_frames;
    int previous_camera_x, previous_camera_y;
    bool anchor_valid;
    float anchor_x, anchor_y;
    void *before, *after;
    size_t state_bytes;
} Test;
static uint8_t read8(void *context, uint32_t address) {
    struct mCore *core=context;
    if (address>=0x04000000 && address<0x04000400)
        return core->rawRead8(core,address,-1);
    return core->busRead8(core,address);
}
static Fe8VoxelScene sample(Test *t, unsigned keys, bool require_live) {
    t->core->setKeys(t->core,keys);t->core->runFrame(t->core);++t->frames;
    assert(fe8_extract_snapshot(&t->reader,t->profile,&t->snapshot));
    Fe8Snapshot *s=&t->snapshot;
    for(unsigned i=0;i<240*160;++i)t->frame[i]=0xFF000000u|(t->video[i]&0xFFFFFFu);
    t->map.map_width=s->map_width;t->map.map_height=s->map_height;
    t->map.camera_x=s->camera_x;t->map.camera_y=s->camera_y;
    t->map.base_tile_rows=s->base_tile_rows;t->map.fog_rows=s->fog_rows;
    fe8_learn_palette_mapping(&t->memory,&t->map,t->frame,240);
    bool rendered=fe8_render_extended_terrain(&t->memory,&t->map,
        (Fe8ExtendedViewport){480,320,120,80},t->terrain,480);
    bool moving=t->previous_camera_x!=s->camera_x || t->previous_camera_y!=s->camera_y;
    Fe8FramePlacement placement=fe8_align_frame_to_terrain(t->frame,240,160,240,
        t->terrain,480,320,480,120,80,moving?8:0);
    bool compatible=placement.match_percent>=15 || fe8_extended_move_range_is_active(s);
    bool validated=fe8_presentation_update(&t->presentation,rendered,compatible,
        s->combat_panel_active)==FE8_PRESENTATION_LIVE;
    t->previous_camera_x=s->camera_x;t->previous_camera_y=s->camera_y;
    bool verify_state=t->frames%31==0;
    if(verify_state)assert(t->core->saveState(t->core,t->before));
    bool hud=fe8_native_hud_extract(&t->hud,&t->memory,s,t->frame,240,validated);
    Fe8VoxelScene scene=fe8_voxel_scene(true,true,true,s,validated,false,hud);
    if(require_live && scene!=FE8_VOXEL_READY) {
        fprintf(stderr,"frame %u cursor %u,%u: %s; match %u, HUD %d, lock %u bits %02x\n",
            t->frames,s->cursor_x,s->cursor_y,fe8_voxel_scene_label(scene),
            placement.match_percent,hud,s->input_lock,s->game_state_bits);
        abort();
    }
    if(scene==FE8_VOXEL_READY) {
        assert(fe8_voxel_render_scene(t->voxel,&t->memory,&t->map,s,640,480));
        ++t->live_frames;
        Fe8VoxelStats stats=fe8_voxel_stats(t->voxel);
        t->hover_frames+=stats.hover_sprites!=0;
        if(require_live && stats.sprites!=s->map_sprite_count) {
            fprintf(stderr,"frame %u cursor %u,%u: drew %u/%u actors, hover replacements %u\n",
                t->frames,s->cursor_x,s->cursor_y,stats.sprites,s->map_sprite_count,stats.hover_sprites);
            abort();
        }
        float x,y;assert(fe8_voxel_project(t->voxel,4.5f,4.5f,0,&x,&y));
        if(t->anchor_valid)assert(fabsf(x-t->anchor_x)<.001f&&fabsf(y-t->anchor_y)<.001f);
        t->anchor_x=x;t->anchor_y=y;t->anchor_valid=true;
    }
    if(verify_state) {
        assert(t->core->saveState(t->core,t->after));
        assert(!memcmp(t->before,t->after,t->state_bytes));
    }
    return scene;
}
static void frames(Test *t,unsigned n,unsigned keys,bool live) {
    while(n--)sample(t,keys,live);
}
static void pointer_target(Test *t,int x,int y) {
    Fe8MouseController mouse={0};fe8_mouse_set_target(&mouse,x,y,0);
    for(unsigned n=0;mouse.active&&n<600;++n) {
        Fe8LiveState live;
        assert(fe8_extract_live_state(&t->reader,t->profile,&live));
        sample(t,fe8_mouse_update(&mouse,&live,true),true);
        assert(!mouse.stalled);
    }
    assert(!mouse.active);
    assert(t->snapshot.cursor_x==x&&t->snapshot.cursor_y==y);
    frames(t,30,0,true); /* Includes the five-frame native hover MU handoff. */
}
int main(int argc,char **argv) {
    assert(argc==2);
    Test *t=calloc(1,sizeof(*t));assert(t);
    t->core=mCoreFind(argv[1]);assert(t->core&&t->core->init(t->core));
    mCoreInitConfig(t->core,"fe8-voxel-interaction-test");
    struct mStandardLogger logger;mStandardLoggerInit(&logger);
    logger.d.filter->defaultLevels=mLOG_FATAL|mLOG_ERROR;mLogSetDefaultLogger(&logger.d);
    struct VFile *rom=VFileOpen(argv[1],O_RDONLY);assert(rom&&t->core->loadROM(t->core,rom));
    t->core->setVideoBuffer(t->core,t->video,240);t->core->reset(t->core);
    Fe8AddressSpace space;fe8_address_space_init(&space,t->core,read8);
    const uint32_t bases[]={0x02000000,0x03000000,0x05000000,0x06000000,0x07000000,0x08000000};
    for(unsigned i=0;i<sizeof(bases)/sizeof(bases[0]);++i) {
        size_t n=0;void *p=mCoreGetMemoryBlock(t->core,bases[i],&n);
        assert(p&&fe8_address_space_add(&space,bases[i],p,n));
    }
    t->reader=(Fe8MemoryReader){&space,fe8_address_space_read8};
    t->memory=(Fe8MemoryView){&space,fe8_address_space_read8};
    t->profile=fe8_profile_for_rom(&t->reader);
    unsigned stable=0;
    for(unsigned i=0;i<20000&&stable<90;++i) {
        unsigned p=i%360;t->core->setKeys(t->core,stable?0:p<3?1:p>=180&&p<183?8:0);
        t->core->runFrame(t->core);
        if(fe8_extract_snapshot(&t->reader,t->profile,&t->snapshot)&&
                t->snapshot.input_lock==0&&t->snapshot.phase==0&&t->snapshot.visible_unit_count>=3)++stable;
        else stable=0;
    }
    assert(stable==90);
    t->state_bytes=t->core->stateSize(t->core);
    t->before=malloc(t->state_bytes);t->after=malloc(t->state_bytes);
    void *checkpoint=malloc(t->state_bytes);assert(t->before&&t->after&&checkpoint);
    Fe8PaletteMapping mapping={0};
    t->map.tileset_config=t->profile->tileset_config;t->map.tile_graphics=0x06008000;
    t->map.palette=0x05000000;t->map.palette_mapping=&mapping;
    t->map.tile_cache=fe8_terrain_cache_create();t->voxel=fe8_voxel_create();
    assert(t->map.tile_cache&&t->voxel);
    frames(t,40,0,false);frames(t,90,0,true);
    assert(t->core->saveState(t->core,checkpoint));
    Fe8Snapshot initial=t->snapshot;
    /* Held keyboard cursor repeat crosses camera-scroll and HUD-swap edges. */
    const unsigned directions[]={0x10,0x80,0x20,0x40};
    for(unsigned i=0;i<4;++i) {frames(t,180,directions[i],true);frames(t,20,0,true);}
    /* The actual mouse controller sends fast B+D-pad pulses, not RAM writes. */
    for(unsigned i=0;i<initial.visible_unit_count;++i) {
        const Fe8VisibleUnit *u=&initial.visible_units[i];
        if(!u->faction && !(u->state&2))pointer_target(t,u->x,u->y);
    }
    assert(t->hover_frames>=60);
    puts("PASS every keyboard/mouse cursor frame stays voxel; native hover handoffs retain all actors; camera is stationary");
    /* Selection/action menus must still use native rendering, then reacquire. */
    assert(t->core->loadState(t->core,checkpoint));fe8_voxel_invalidate(t->voxel);
    frames(t,10,0,false);frames(t,3,1,false);frames(t,30,0,false);
    assert(sample(t,0,false)!=FE8_VOXEL_READY);
    frames(t,3,1,false);frames(t,30,0,false);
    assert(sample(t,0,false)!=FE8_VOXEL_READY);
    frames(t,3,2,false);frames(t,30,0,false);frames(t,3,2,false);frames(t,45,0,false);
    frames(t,30,0,true);
    printf("%s: %u frames, %u live, %u native-hover frames; no idle cursor fallback, no actor loss, selection/cancel restoration and serialized-state immutability passed\n",
        t->profile->profile_name,t->frames,t->live_frames,t->hover_frames);
    fe8_voxel_destroy(t->voxel);fe8_terrain_cache_destroy(t->map.tile_cache);
    free(checkpoint);free(t->before);free(t->after);
    mCoreConfigDeinit(&t->core->config);t->core->deinit(t->core);
    mLogSetDefaultLogger(NULL);mStandardLoggerDeinit(&logger);free(t);return 0;
}
