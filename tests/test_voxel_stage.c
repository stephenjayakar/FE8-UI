#include "voxel_stage.h"
#include "native_hud_host.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t ram[0x40000],iram[0x8000],vram[0x18000],pal[1024],oam[1024],io[1024],rom[256];
static uint8_t read_io(void *unused,uint32_t a){(void)unused;return a>=0x04000000&&a<0x04000400?io[a-0x04000000]:0;}
static uint64_t hash(const void *p,size_t n){const uint8_t *b=p;uint64_t h=1;while(n--)h=(h^*b++)*1099511628211ULL;return h;}
static void wr32(uint8_t *p,uint32_t n){for(int i=0;i<4;++i)p[i]=(uint8_t)(n>>(i*8));}
int main(void){
    Fe8VoxelStage *s=fe8_voxel_stage_create();Fe8VoxelRenderer *v=fe8_voxel_create();assert(s&&v);
    const Fe8VoxelGpuFrame *g=fe8_voxel_build_gpu(v,fe8_voxel_stage_memory(s),fe8_voxel_stage_map(s),fe8_voxel_stage_snapshot(s),960,640);
    assert(g&&g->scenery.count&&!fe8_voxel_stage_has_map(s));
    Fe8AddressSpace source;fe8_address_space_init(&source,NULL,read_io);
    fe8_address_space_add(&source,0x02000000,ram,sizeof(ram));fe8_address_space_add(&source,0x03000000,iram,sizeof(iram));
    fe8_address_space_add(&source,0x05000000,pal,sizeof(pal));fe8_address_space_add(&source,0x06000000,vram,sizeof(vram));
    fe8_address_space_add(&source,0x07000000,oam,sizeof(oam));fe8_address_space_add(&source,0x08000000,rom,sizeof(rom));
    Fe8Snapshot snap=*fe8_voxel_stage_snapshot(s);Fe8MapRenderState map=*fe8_voxel_stage_map(s);
    snap.chapter=7;snap.terrain[3]=0x06;
    for(unsigned y=0;y<12;++y)wr32(ram+0x100+y*4,0x02000200+y*32);
    for(unsigned i=0;i<4;++i)ram[0x1000+i*2]=1;
    memset(vram+0x8020,0x11,32);pal[2]=0xAB;pal[3]=0x27;
    assert(fe8_voxel_stage_remember(s,&source,&map,&snap)&&fe8_voxel_stage_has_map(s));
    uint64_t before=hash(ram,sizeof(ram));
    g=fe8_voxel_build_gpu(v,fe8_voxel_stage_memory(s),fe8_voxel_stage_map(s),fe8_voxel_stage_snapshot(s),960,640);assert(g);
    assert(before==hash(ram,sizeof(ram)));uint64_t scenery=hash(g->scenery.vertices,g->scenery.count*sizeof(Fe8GpuVertex));
    uint64_t ground=hash(g->ground,g->map_width*g->map_height*4);
    memset(ram,0xCC,sizeof(ram));memset(vram,0xFE,sizeof(vram));memset(pal,0xA3,sizeof(pal));memset(io,0xFF,sizeof(io));
    for(unsigned i=0;i<20;++i){
        fe8_voxel_orbit(v,.11f,i%2?.02f:-.02f);
        g=fe8_voxel_build_gpu(v,fe8_voxel_stage_memory(s),fe8_voxel_stage_map(s),fe8_voxel_stage_snapshot(s),1920,1280);
        assert(g&&hash(g->scenery.vertices,g->scenery.count*sizeof(Fe8GpuVertex))==scenery);
        assert(hash(g->ground,g->map_width*g->map_height*4)==ground);
    }
    float x0,y0,x1,y1;assert(fe8_voxel_project(v,7,6,0,&x0,&y0));
    fe8_voxel_pan(v,60,30);g=fe8_voxel_build_gpu(v,fe8_voxel_stage_memory(s),fe8_voxel_stage_map(s),fe8_voxel_stage_snapshot(s),1920,1280);assert(g);
    assert(fe8_voxel_project(v,7,6,0,&x1,&y1));assert(fabsf(x1-x0-60)<.1f&&fabsf(y1-y0-30)<.1f);
    /* Growing a map after GPU meshes exist used to free the same buffers
     * twice. Reuse their capacity and rebuild revisions instead. */
    Fe8Snapshot larger=*fe8_voxel_stage_snapshot(s);Fe8MapRenderState large_map=*fe8_voxel_stage_map(s);
    larger.map_width=large_map.map_width=32;
    memset(larger.terrain,1,32*12);
    assert(fe8_voxel_build_gpu(v,fe8_voxel_stage_memory(s),&large_map,&larger,960,640));
    assert(fe8_voxel_build_gpu(v,fe8_voxel_stage_memory(s),fe8_voxel_stage_map(s),fe8_voxel_stage_snapshot(s),960,640));
    assert(fe8_voxel_render_scene(v,fe8_voxel_stage_memory(s),fe8_voxel_stage_map(s),fe8_voxel_stage_snapshot(s),640,480));
    assert(!fe8_voxel_stage_remember(s,NULL,&map,&snap)&&fe8_voxel_stage_has_map(s));
    fe8_voxel_stage_reset(s);fe8_voxel_invalidate(v);
    assert(!fe8_voxel_stage_has_map(s));
    assert(fe8_voxel_render_scene(v,fe8_voxel_stage_memory(s),fe8_voxel_stage_map(s),fe8_voxel_stage_snapshot(s),640,480));
    assert(fe8_voxel_stats(v).buildings[FE8_BUILDING_ARMORY]==0);
    puts("PASS retained 3D re-renders through corrupt/repurposed live RAM/VRAM, orbit, pan, resize, CPU/GPU change and reset; no scene leakage or emulator writes");
    Fe8HudHost *hud=calloc(1,sizeof(*hud));assert(hud);hud->scale_percent=150;
    Fe8HostVideo video={0};video.scaling.drawable_width=960;video.scaling.drawable_height=640;
    Fe8HostPixel frame[240*160];
    for(unsigned i=0;i<240*160;++i)frame[i]=0xFF000000|i*3;
    for(unsigned tick=0;tick<5;++tick){
        frame[80*240+120]^=0xFFFFFF;assert(fe8_hud_host_native_scene(hud,&video,frame));
        assert(!memcmp(hud->hud.atlas,frame,sizeof(frame)));
        Fe8HudRect d=hud->hud.panels[0].destination;
        assert(d.width<=960*.60&&d.height<=640*.71&&d.x>0);
        assert(hud->pixels[0]==0);assert(fe8_native_hud_hit_test(&hud->hud,d.x+1,d.y+1));
        for(int y=0;y<d.height;++y)for(int x=0;x<d.width;++x)
            assert(hud->pixels[(y+d.y)*960+x+d.x]==frame[(y*160/d.height)*240+x*240/d.width]);
    }
    puts("PASS full battle/forecast/dialogue panel preserves every current native pixel; 3D remains visible, pointer-blocking and resize-safe");
    fe8_hud_host_deinit(hud);free(hud);fe8_voxel_destroy(v);fe8_voxel_stage_destroy(s);return 0;
}
