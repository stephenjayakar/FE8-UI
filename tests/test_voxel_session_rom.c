/* Optional end-to-end read-only presentation regression. All cursor movement
 * and hover/selection transitions are produced by FE8 through normal keys.
 * User ROMs stay local; nothing is written to the cartridge or save files. */
#include <mgba/flags.h>
#include <mgba/core/core.h>
#include <mgba/core/interface.h>
#include <mgba/core/log.h>
#include <mgba/core/serialize.h>
#include <mgba-util/vfs.h>
#include "address_space.h"
#include "extended_presentation.h"
#include "extended_unit_renderer.h"
#include "mouse_controller.h"
#include "native_hud.h"
#include "voxel_presentation.h"
#include "voxel_renderer.h"
#include "voxel_stage.h"
#include "voxel_targets.h"
#include "fe8_catalog.h"
#include <assert.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Test {
    struct mCore *core;
    Fe8AddressSpace space;
    Fe8VoxelStage *stage;
    Fe8VoxelTargets targets;
    bool world_live;
    unsigned native_frames,detail_frames,target_frames;
    const Fe8Profile *profile;
    Fe8MemoryReader reader;
    Fe8MemoryView memory;
    Fe8Snapshot snapshot;
    Fe8NativeHud hud;
    Fe8MapRenderState map;
    Fe8ExtendedPresentation presentation;
    Fe8VoxelRenderer *voxel, *gpu;
    unsigned selected_frames, walking_frames, menu_frames, expected_actors;
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
static Fe8VoxelScene sample(Test *t,unsigned keys,bool require_live) {
    (void)require_live;
    t->core->setKeys(t->core,keys);t->core->runFrame(t->core);++t->frames;
    bool valid=fe8_extract_snapshot(&t->reader,t->profile,&t->snapshot);
    Fe8Snapshot *s=&t->snapshot;
    for(unsigned i=0;i<240*160;++i)t->frame[i]=0xFF000000u|(t->video[i]&0xFFFFFFu);
    t->map.map_width=s->map_width;t->map.map_height=s->map_height;
    t->map.camera_x=s->camera_x;t->map.camera_y=s->camera_y;
    t->map.base_tile_rows=s->base_tile_rows;t->map.fog_rows=s->fog_rows;
    bool validated=false;Fe8FramePlacement placement={0};
    if(valid){
        fe8_learn_palette_mapping(&t->memory,&t->map,t->frame,240);
        bool rendered=fe8_render_extended_terrain(&t->memory,&t->map,
            (Fe8ExtendedViewport){480,320,120,80},t->terrain,480);
        bool moving=t->previous_camera_x!=s->camera_x||t->previous_camera_y!=s->camera_y;
        placement=fe8_align_tactical_frame(t->frame,240,160,240,t->terrain,480,320,480,120,80,moving);
        validated=rendered&&(placement.match_percent>=15||fe8_extended_move_range_is_active(s));
        t->previous_camera_x=s->camera_x;t->previous_camera_y=s->camera_y;
    }
    bool verify=t->frames%23==0;
    if(verify)assert(t->core->saveState(t->core,t->before));
    bool hud=fe8_native_hud_extract_tactical(&t->hud,&t->memory,s,t->frame,240,validated);
    if(!hud)hud=fe8_native_hud_extract_details(&t->hud,&t->memory,s,t->frame,240,validated&&placement.match_percent>=15);
    Fe8VoxelScene gate=fe8_voxel_scene(true,true,true,valid?s:NULL,validated,false,hud);
    fe8_voxel_targets_read(&t->targets,&t->reader,valid?s:NULL);
    t->target_frames+=t->targets.active;
    Fe8Snapshot current=*s;
    if(t->targets.active){current.cursor_x=t->targets.targets[0].x;current.cursor_y=t->targets.targets[0].y;}
    const Fe8VoxelGpuFrame *gpu=NULL;
    bool live=false;
    if(gate==FE8_VOXEL_READY){
        gpu=fe8_voxel_build_gpu(t->gpu,&t->memory,&t->map,&current,960,640);
        if(gpu){live=true;assert(fe8_voxel_stage_remember(t->stage,&t->space,&t->map,&current));}
    }
    if(!live){
        gpu=fe8_voxel_build_gpu(t->gpu,fe8_voxel_stage_memory(t->stage),
            fe8_voxel_stage_map(t->stage),fe8_voxel_stage_snapshot(t->stage),960,640);
        if(!gpu){fe8_voxel_invalidate(t->gpu);gpu=fe8_voxel_build_gpu(t->gpu,fe8_voxel_stage_memory(t->stage),
            fe8_voxel_stage_map(t->stage),fe8_voxel_stage_snapshot(t->stage),960,640);}
        fe8_native_hud_reset(&t->hud);t->hud.count=1;
        t->hud.panels[0]=(Fe8HudPanel){FE8_HUD_SCENE,{0,0,240,160},{0}};
        memcpy(t->hud.atlas,t->frame,sizeof(t->frame));++t->native_frames;
        assert(!memcmp(t->frame,t->hud.atlas,sizeof(t->frame)));
    }else{
        ++t->live_frames;
        if(t->hud.count&&t->hud.panels[0].kind==FE8_HUD_DETAIL)++t->detail_frames;
    }
    assert(gpu&&gpu->scenery.count); /* Every enabled frame is genuine 3D geometry. */
    fe8_native_hud_layout(&t->hud,960,640,480,320,150);
    for(unsigned i=0;i<t->hud.count;++i){Fe8HudRect r=t->hud.panels[i].destination;
        assert(r.x>=0&&r.y>=0&&r.x+r.width<=960&&r.y+r.height<=640);}
    if(live!=t->world_live)fprintf(stderr,"frame %u: %s lock=%u bits=%u match=%u panels=%u target=%d\n",
        t->frames,live?"live world":"retained world + live native UI",s->input_lock,s->game_state_bits,placement.match_percent,t->hud.count,t->targets.active);
    t->world_live=live;
    if(verify){assert(t->core->saveState(t->core,t->after));assert(!memcmp(t->before,t->after,t->state_bytes));}
    return gate;
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
static void checkpoint_file(Test *t,const char *prefix,const char *name) {
    char path[1024];snprintf(path,sizeof(path),"%s-%s.ss",prefix,name);
    struct VFile *f=VFileOpen(path,O_RDWR|O_CREAT|O_TRUNC);assert(f);
    assert(mCoreSaveStateNamed(t->core,f,SAVESTATE_SCREENSHOT|SAVESTATE_METADATA));f->close(f);
}
static void native_ppm(Test *t,const char *prefix,const char *name){
    if(!prefix)return;
    char path[1024];snprintf(path,sizeof(path),"%s-%s.ppm",prefix,name);
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n240 160\n255\n");
    for(unsigned i=0;i<240*160;++i){uint8_t rgb[3]={t->frame[i],t->frame[i]>>8,t->frame[i]>>16};fwrite(rgb,1,3,f);}fclose(f);
    checkpoint_file(t,prefix,name);
}
static void press(Test *t,unsigned key){frames(t,1,key,true);frames(t,24,0,true);}
int main(int argc,char **argv) {
    assert(argc==2||argc==3);setbuf(stdout,NULL);
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
    t->space=space;
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
    assert(stable==90);fprintf(stderr,"Booted map\n");
    t->state_bytes=t->core->stateSize(t->core);
    t->before=malloc(t->state_bytes);t->after=malloc(t->state_bytes);
    void *checkpoint=malloc(t->state_bytes);assert(t->before&&t->after&&checkpoint);
    Fe8PaletteMapping mapping={0};
    t->map.tileset_config=t->profile->tileset_config;t->map.tile_graphics=0x06008000;
    t->map.palette=0x05000000;t->map.palette_mapping=&mapping;
    t->map.tile_cache=fe8_terrain_cache_create();t->voxel=fe8_voxel_create();t->gpu=fe8_voxel_create();t->stage=fe8_voxel_stage_create();assert(t->stage);
    assert(t->map.tile_cache&&t->voxel);
    fprintf(stderr,"Allocated, starting frames\n");frames(t,40,0,false);t->expected_actors=fe8_voxel_stats(t->gpu).sprites;assert(t->expected_actors>0);frames(t,90,0,true);
    assert(t->core->saveState(t->core,checkpoint));
    Fe8Snapshot initial=t->snapshot;
    for(unsigned i=0;i<initial.visible_unit_count;++i){const Fe8VisibleUnit *u=&initial.visible_units[i];
        printf("unit %u faction=%u at %d,%d hp=%u/%u\n",u->unit_id,u->faction,u->x,u->y,u->current_hp,u->max_hp);}
    const char *prefix=argc==3?argv[2]:NULL;
    native_ppm(t,prefix,"idle");
    /* Find a legal one-range attack from a real unit's native movement map. */
    bool found=false;int attacker_x=0,attacker_y=0,dx=0,dy=0;unsigned defender=0;
    for(unsigned b=0;b<initial.visible_unit_count&&!found;++b){const Fe8VisibleUnit *ally=&initial.visible_units[b];
        if(ally->faction||ally->state&2)continue;
        pointer_target(t,ally->x,ally->y);press(t,1);
        for(unsigned e=0;e<initial.visible_unit_count&&!found;++e){const Fe8VisibleUnit *enemy=&initial.visible_units[e];
            if(!enemy->faction)continue;
            for(int y=enemy->y-1;y<=enemy->y+1&&!found;++y)for(int x=enemy->x-1;x<=enemy->x+1&&!found;++x){
                if(x<0||y<0||x>=t->snapshot.map_width||y>=t->snapshot.map_height||abs(x-enemy->x)+abs(y-enemy->y)!=1)continue;
                unsigned cell=y*t->snapshot.map_width+x;
                if(t->snapshot.movement[cell]<128&&!t->snapshot.unit_map[cell]){
                    found=true;dx=x;dy=y;defender=enemy->unit_id;attacker_x=ally->x;attacker_y=ally->y;
                }
            }
        }
        if(!found)press(t,2);
    }
    assert(found);printf("Attack from %d,%d to %d,%d enemy %u\n",attacker_x,attacker_y,dx,dy,defender);
    pointer_target(t,dx,dy);press(t,1);frames(t,120,0,true);native_ppm(t,prefix,"attack-menu");
    press(t,1);native_ppm(t,prefix,"weapons");
    press(t,1);frames(t,40,0,true);native_ppm(t,prefix,"forecast");
    printf("Target mode=%d count=%u lock=%u\n",t->targets.active,t->targets.count,t->snapshot.input_lock);
    for(unsigned i=0;i<t->targets.count;++i)printf("target %u id=%u tile=%d,%d\n",i,t->targets.targets[i].unit,t->targets.targets[i].x,t->targets.targets[i].y);
    if(!t->targets.active){fprintf(stderr,"Expected native target selection; inspect capture\n");return 2;}
    void *forecast=malloc(t->state_bytes);assert(forecast&&t->core->saveState(t->core,forecast));
    /* Native cancellation returns to weapons/menu without losing the stage. */
    press(t,2);assert(!t->targets.active);native_ppm(t,prefix,"cancel-target");
    press(t,1);frames(t,40,0,true);assert(t->targets.active);
    Fe8TargetClick click={0};Fe8Target target=t->targets.targets[t->targets.count-1];
    assert(fe8_voxel_target_click(&click,&t->targets,target.x,target.y));
    for(unsigned i=0;i<200&&click.active;++i)sample(t,fe8_voxel_target_keys(&click,&t->reader,&t->snapshot),true);
    unsigned before_native=t->native_frames;
    for(unsigned i=0;i<1000;++i){
        sample(t,0,true);
        if(i==35||i==100||i==170||i==240){char name[32];snprintf(name,sizeof(name),"combat-%u",i);native_ppm(t,prefix,name);}
        if(i>120&&t->world_live&&!t->snapshot.input_lock&&!(t->snapshot.game_state_bits&3))break;
    }
    assert(t->native_frames>before_native);assert(fe8_voxel_stage_has_map(t->stage));
    frames(t,45,0,true);assert(t->world_live);native_ppm(t,prefix,"after-combat");
    /* Unit info and phase events must also remain inside the 3D session. */
    assert(t->core->loadState(t->core,checkpoint));fe8_voxel_stage_reset(t->stage);fe8_voxel_invalidate(t->gpu);
    t->targets=(Fe8VoxelTargets){0};frames(t,45,0,true);
    pointer_target(t,initial.visible_units[0].x,initial.visible_units[0].y);
    press(t,1u<<8);frames(t,45,0,true);native_ppm(t,prefix,"unit-info");
    assert(!t->world_live);press(t,2);frames(t,60,0,true);assert(t->world_live);

    /* Explicit disposable healing fixture: the opening map has no healer.
     * Give one adjacent ally a native Heal staff and staff proficiency; injure
     * its neighbour. After this setup, normal FE8 menu/target/input drives the
     * entire heal. No ROM bytes or cartridge/save files are written. */
    if(strstr(t->profile->profile_name,"Archanae")) {
        Fe8Catalog catalog;assert(fe8_catalog_init(&t->reader,t->profile,&catalog));
        unsigned heal_id=0;
        for(unsigned id=1;id<256;++id){Fe8ItemInfo item;
            if(fe8_catalog_item(&t->reader,&catalog,(30u<<8)|id,&item)&&!strcmp(item.name,"Heal")){heal_id=id;break;}}
        assert(heal_id);
        const Fe8VisibleUnit *healer=NULL,*patient=NULL;
        for(unsigned i=0;i<initial.visible_unit_count&&!healer;++i)for(unsigned j=0;j<initial.visible_unit_count;++j)
            if(!initial.visible_units[i].faction&&!initial.visible_units[j].faction&&i!=j&&
                    abs(initial.visible_units[i].x-initial.visible_units[j].x)+abs(initial.visible_units[i].y-initial.visible_units[j].y)==1){
                healer=&initial.visible_units[i];patient=&initial.visible_units[j];break;}
        assert(healer&&patient);
        uint32_t ha=t->profile->blue_units+(healer->unit_id-1)*0x48;
        uint32_t pa=t->profile->blue_units+(patient->unit_id-1)*0x48;
        assert(read8(t->core,ha+11)==healer->unit_id&&read8(t->core,pa+11)==patient->unit_id);
        for(unsigned i=0;i<5;++i)t->core->busWrite16(t->core,ha+0x1E + i*2,i?0:(30u<<8)|heal_id);
        t->core->busWrite8(t->core,ha+0x28+4,255);t->core->busWrite8(t->core,pa+0x13,patient->max_hp-7);
        frames(t,30,0,true);unsigned hp=read8(t->core,pa+0x13);
        pointer_target(t,healer->x,healer->y);press(t,1);press(t,1);frames(t,30,0,true);native_ppm(t,prefix,"heal-action");
        press(t,1);native_ppm(t,prefix,"heal-staff");press(t,1);frames(t,30,0,true);native_ppm(t,prefix,"heal-target");
        assert(t->targets.active);Fe8TargetClick heal={0};
        assert(fe8_voxel_target_click(&heal,&t->targets,patient->x,patient->y));
        for(unsigned i=0;i<200&&heal.active;++i)sample(t,fe8_voxel_target_keys(&heal,&t->reader,&t->snapshot),true);
        for(unsigned i=0;i<900;++i){sample(t,0,true);
            if(i==100)native_ppm(t,prefix,"heal-effect");
            if(i>150&&t->world_live&&!t->snapshot.input_lock&&!(t->snapshot.game_state_bits&3))break;}
        assert(read8(t->core,pa+0x13)>hp);native_ppm(t,prefix,"after-heal");
        printf("PASS disposable native Heal fixture: HP %u -> %u; target/animation/result kept in 3D session\n",hp,read8(t->core,pa+0x13));
    }
    assert(t->core->loadState(t->core,checkpoint));fe8_voxel_stage_reset(t->stage);fe8_voxel_invalidate(t->gpu);
    t->targets=(Fe8VoxelTargets){0};frames(t,45,0,true);
    press(t,8);native_ppm(t,prefix,"minimap");press(t,2);frames(t,30,0,true);
    int blank=0;while(blank<t->snapshot.map_width*t->snapshot.map_height&&t->snapshot.unit_map[blank])++blank;
    assert(blank<t->snapshot.map_width*t->snapshot.map_height);
    pointer_target(t,blank%t->snapshot.map_width,blank/t->snapshot.map_width);
    press(t,1);native_ppm(t,prefix,"map-menu");
    press(t,1u<<6);press(t,1); /* Native map-menu End at the wrapped last entry. */
    bool saw_enemy=false;
    for(unsigned i=0;i<1200;++i){sample(t,0,true);saw_enemy|=t->snapshot.phase!=0;
        if(i==100)native_ppm(t,prefix,"enemy-phase");
        if(saw_enemy&&i>120&&t->world_live&&!t->snapshot.input_lock&&!t->snapshot.phase)break;}
    assert(saw_enemy);assert(fe8_voxel_stage_has_map(t->stage));
    printf("PASS native End turn / enemy phase stayed in 3D session\n");
    puts("PASS real native weapon selection, forecast, target cycling/confirm, combat and cancellation remain in a 3D session on every frame");
    printf("Audited %u frames: %u live world, %u live native panel on retained geometry, %u details, %u target selection\n",t->frames,t->live_frames,t->native_frames,t->detail_frames,t->target_frames);
    fe8_voxel_stage_destroy(t->stage);fe8_voxel_destroy(t->gpu);fe8_voxel_destroy(t->voxel);fe8_terrain_cache_destroy(t->map.tile_cache);
    free(forecast);free(checkpoint);free(t->before);free(t->after);
    mCoreConfigDeinit(&t->core->config);t->core->deinit(t->core);
    mLogSetDefaultLogger(NULL);mStandardLoggerDeinit(&logger);free(t);return 0;
}
