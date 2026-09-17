#include "voxel_renderer.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t rom[0x1000],iwram[0x8000],ewram[0x40000],vram[0x18000],palette[0x400],oam[1024],io[1024];
static uint8_t read8(void *unused,uint32_t a){
    (void)unused;
    if(a>=0x03000000&&a<0x03008000)return iwram[a-0x03000000];
    if(a>=0x08000000&&a<0x08001000)return rom[a-0x08000000];
    if(a>=0x02000000&&a<0x02040000)return ewram[a-0x02000000];
    if(a>=0x06000000&&a<0x06018000)return vram[a-0x06000000];
    if(a>=0x04000000&&a<0x04000400)return io[a-0x04000000];
    if(a>=0x07000000&&a<0x07000400)return oam[a-0x07000000];
    if(a>=0x05000000&&a<0x05000400)return palette[a-0x05000000];
    return 0;
}
static void wr16(uint8_t *p,unsigned a,unsigned v){p[a]=(uint8_t)v;p[a+1]=(uint8_t)(v>>8);}
static void wr32(uint8_t *p,unsigned a,uint32_t v){wr16(p,a,v);wr16(p,a+2,v>>16);}
static uint64_t digest(const void *data,size_t n){const uint8_t *p=data;uint64_t h=1;while(n--)h=(h^*p++)*UINT64_C(1099511628211);return h;}
int main(void){
    Fe8MemoryView memory={NULL,read8};
    Fe8MapRenderState map={.map_width=6,.map_height=6,.base_tile_rows=0x02000100,
        .tileset_config=0x02001000,.tile_graphics=0x06008000,.palette=0x05000000};
    Fe8Snapshot *s=calloc(1,sizeof(*s));assert(s);s->map_width=s->map_height=6;s->flags=FE8_SNAPSHOT_TERRAIN|FE8_SNAPSHOT_MAP_SPRITES;
    memset(s->terrain,1,36);s->terrain[7]=5;s->terrain[9]=12;s->terrain[10]=0x2E;
    for(int y=0;y<6;++y){wr32(ewram,0x100+y*4,0x02000200+y*12);for(int x=0;x<6;++x)wr16(ewram,0x200+(y*6+x)*2,0);}
    for(int i=0;i<4;++i)wr16(ewram,0x1000+i*2,1);
    memset(vram+0x8020,0x11,32);wr16(palette,2,0x17A8);
    Fe8VoxelRenderer *v=fe8_voxel_create();assert(v);
    assert(!fe8_voxel_render(NULL,&memory,&map,s,640,480));
    assert(!fe8_voxel_render(v,NULL,&map,s,640,480));
    assert(!fe8_voxel_render(v,&memory,&map,s,16385,480));
    assert(!fe8_voxel_render(v,&memory,&map,s,640,16385));
    assert(!fe8_voxel_render(v,&memory,&map,s,-1,480));
    s->map_sprite_count=100;assert(!fe8_voxel_render(v,&memory,&map,s,640,480));s->map_sprite_count=0;
    puts("PASS invalid renderer, memory, dimensions and sprite count");
    uint64_t ram=digest(ewram,sizeof(ewram)),vr=digest(vram,sizeof(vram)),pal=digest(palette,sizeof(palette));
    uint32_t *pixels=fe8_voxel_render(v,&memory,&map,s,640,480);assert(pixels);uint64_t a=digest(pixels,640*480*4);
    assert(fe8_voxel_stats(v).terrain_builds==1);
    for(int i=0;i<4;++i){assert(fe8_voxel_render(v,&memory,&map,s,640,480));assert(digest(pixels,640*480*4)==a);}
    assert(fe8_voxel_stats(v).terrain_builds==1);
    assert(ram==digest(ewram,sizeof(ewram))&&vr==digest(vram,sizeof(vram))&&pal==digest(palette,sizeof(palette)));
    puts("PASS deterministic cache reuse and read-only memory");
    for(int i=0;i<6;++i){
        fe8_voxel_camera(v,.15f,1);assert(fe8_voxel_render(v,&memory,&map,s,640,480));
        for(int y=0;y<6;++y)for(int x=0;x<6;++x){float sx,sy;int xx,yy;assert(fe8_voxel_project(v,x+.5f,y+.5f,0,&sx,&sy));assert(fe8_voxel_pick(v,sx,sy,&xx,&yy)&&xx==x&&yy==y);}
    }
    puts("PASS all 36 ground tiles round-trip at six yaw angles");
    unsigned builds=fe8_voxel_stats(v).terrain_builds;wr16(palette,2,0x33B4);
    assert(fe8_voxel_render(v,&memory,&map,s,640,480));assert(fe8_voxel_stats(v).terrain_builds==builds+1);
    s->terrain[7]=1;assert(fe8_voxel_render(v,&memory,&map,s,640,480));assert(fe8_voxel_stats(v).terrain_builds==builds+2);
    puts("PASS palette and terrain changes regenerate geometry");
    /* A synthetic sprite exists solely in native OBJ VRAM. Alpha index zero
       must remain absent; changing native pixels must invalidate its mesh. */
    wr16(palette,0x202,0x7C00);
    for(int ty=0;ty<2;++ty)for(int tx=0;tx<2;++tx)memset(vram+0x10000+(ty*32+tx)*32,0x11,32);
    s->map_sprite_count=1;s->map_sprites[0]=(Fe8VisibleMapSprite){48,48,0,0};
    fe8_voxel_home(v);assert(fe8_voxel_render(v,&memory,&map,s,640,480));
    assert(fe8_voxel_stats(v).sprites==1&&fe8_voxel_stats(v).sprite_builds==1);
    assert(fe8_voxel_render(v,&memory,&map,s,640,480));assert(fe8_voxel_stats(v).sprite_builds==1);
    wr16(palette,0x202,0x001F);assert(fe8_voxel_render(v,&memory,&map,s,640,480));assert(fe8_voxel_stats(v).sprite_builds==2);
    float sx,sy;int x,y;assert(fe8_voxel_project(v,3.5f,3.8f,10,&sx,&sy));
    assert(fe8_voxel_pick(v,sx,sy,&x,&y)&&x==3&&y==3);
    puts("PASS sprite cache invalidation and raised-unit picking");
    s->map_sprites[0].config=0x80;assert(fe8_voxel_render(v,&memory,&map,s,640,480));assert(fe8_voxel_stats(v).sprites==0);
    s->map_sprites[0].config=0;
    memset(vram+0x10000,0,0x8000);assert(fe8_voxel_render(v,&memory,&map,s,640,480));assert(fe8_voxel_stats(v).sprites==0);
    s->map_sprites[0].config=7;assert(fe8_voxel_render(v,&memory,&map,s,640,480));assert(fe8_voxel_stats(v).sprites==0);
    puts("PASS native hidden flag, transparency and unsupported sprite layouts");
    fe8_voxel_camera(v,NAN,1);fe8_voxel_camera(v,0,-1);fe8_voxel_pan(v,INFINITY,0);
    assert(fe8_voxel_render(v,&memory,&map,s,800,600));
    fe8_voxel_invalidate(v);assert(!fe8_voxel_pick(v,100,100,&x,&y));assert(fe8_voxel_render(v,&memory,&map,s,640,480));
    assert(!fe8_voxel_pick(v,NAN,20,&x,&y)&&!fe8_voxel_pick(v,-1,20,&x,&y));
    puts("PASS resize, invalid camera input and state invalidation");
    s->map_sprite_count = 0;
    const int sizes[][2] = {{3024,1964}, {3456,2234}, {5120,2880}, {2880,5120}};
    for (unsigned n=0; n<sizeof(sizes)/sizeof(sizes[0]); ++n) {
        const int w=sizes[n][0], h=sizes[n][1];
        fe8_voxel_home(v);
        uint32_t *output=fe8_voxel_render(v,&memory,&map,s,w,h);
        assert(output && !fe8_voxel_error(v));
        Fe8VoxelStats stats=fe8_voxel_stats(v);
        assert(stats.render_width<=1920 && stats.render_height<=1080);
        assert(output[0]>>24 && output[(size_t)w*h-1]>>24);
        for (int ty=0; ty<6; ++ty) for (int tx=0; tx<6; ++tx) {
            assert(fe8_voxel_project(v,tx+.5f,ty+.5f,0,&sx,&sy));
            assert(fe8_voxel_pick(v,sx,sy,&x,&y) && x==tx && y==ty);
        }
        /* Panning accepts drawable pixels, not the smaller scene coordinates. */
        float before_x,before_y;
        assert(fe8_voxel_project(v,3.5f,3.5f,0,&before_x,&before_y));
        fe8_voxel_pan(v,20,15);
        assert(fe8_voxel_render(v,&memory,&map,s,w,h));
        assert(fe8_voxel_project(v,3.5f,3.5f,0,&sx,&sy));
        assert(fabsf(sx-before_x-20)<.02f && fabsf(sy-before_y-15)<.02f);
        assert(!fe8_voxel_pick(v,(float)w,0,&x,&y));
    }
    assert(!fe8_voxel_render(v,&memory,&map,s,16384,16384));
    assert(fe8_voxel_error(v) && !fe8_voxel_pick(v,10,10,&x,&y));
    assert(fe8_voxel_render(v,&memory,&map,s,640,480));
    puts("PASS Retina, 5K, portrait output, scaled picking/panning, and failure recovery");
    fe8_voxel_destroy(v);
    v=fe8_voxel_create();assert(v);
    memset(s->terrain,1,36);
    s->map_sprite_count=1;
    s->map_sprites[0]=(Fe8VisibleMapSprite){48,48,0,0};
    wr16(palette,0x202,0x7C1F); /* Exact unlit magenta, absent from terrain. */
    for(int ty=0;ty<2;++ty)for(int tx=0;tx<2;++tx)
        memset(vram+0x10000+(ty*32+tx)*32,0x11,32);
    for(int angle=0;angle<3;++angle){
        fe8_voxel_home(v);fe8_voxel_camera(v,(float)(angle-1)*1.2f+.32f,1);
        const uint32_t *scene=fe8_voxel_render_scene(v,&memory,&map,s,640,480);assert(scene);
        int minx=640,miny=480,maxx=-1,maxy=-1;
        for(int yy=0;yy<480;++yy)for(int xx=0;xx<640;++xx)
            if(scene[yy*640+xx]==UINT32_C(0xFFFF00FF)){
                if(xx<minx)minx=xx;if(xx>maxx)maxx=xx;
                if(yy<miny)miny=yy;if(yy>maxy)maxy=yy;
            }
        assert(maxx>minx&&maxy>miny);
        /* The upright square must not become a thin side-on relief at +/-yaw. */
        assert(abs((maxx-minx)-(maxy-miny))<=1);
        assert(fe8_voxel_project(v,3.5f,3.9375f,.1f,&sx,&sy));
        assert(fabsf((minx+maxx+1)*.5f-sx)<1.f&&fabsf(maxy+1-sy)<1.1f);
        assert(fe8_voxel_pick(v,(float)(minx+maxx)/2,miny+2.f,&x,&y)&&x==3&&y==3);
    }
    puts("PASS billboards face all camera angles with exact palette, square aspect, grounded feet and alpha-hit picking");
    uint64_t uploads=fe8_voxel_stats(v).output_pixels;
    const uint32_t *scene=fe8_voxel_render_scene(v,&memory,&map,s,5120,2880);assert(scene);
    Fe8VoxelStats stats=fe8_voxel_stats(v);
    assert(stats.render_width==1920&&stats.render_height==1080&&stats.output_pixels==uploads);
    size_t scene_bytes=(size_t)stats.render_width*stats.render_height*4;
    uint64_t signature=digest(scene,scene_bytes);
    assert(fe8_voxel_render_scene(v,&memory,&map,s,5120,2880)==scene);
    assert(fe8_voxel_stats(v).reused_frames==stats.reused_frames+1);
    assert(fe8_voxel_stats(v).billboard_pixels==stats.billboard_pixels);
    assert(fe8_voxel_stats(v).output_pixels==uploads);
    assert(digest(scene,scene_bytes)==signature);
    vram[0x10000]^=0x11; /* Native animation changes must not get cached away. */
    assert(fe8_voxel_render_scene(v,&memory,&map,s,5120,2880));
    assert(digest(scene,scene_bytes)!=signature);
    assert(fe8_voxel_stats(v).background_builds==stats.background_builds);
    puts("PASS 5K interactive path never CPU-upscales; identical frames reuse raster; sprite animation redraws without rebuilding terrain");
    assert(fe8_voxel_render_scene(v,&memory,&map,s,640,480));
    uint32_t *capture=fe8_voxel_capture(v);assert(capture);uint32_t first=capture[0];capture[0]^=0xFFFFFF;
    scene=fe8_voxel_render_scene(v,&memory,&map,s,640,480);assert(scene&&scene[0]==first);
    puts("PASS mutable captures cannot corrupt a reused scene");
    /* Separate water graphics/palette from the house material. Palette
     * animation updates flat ground without re-rasterizing cached scenery. */
    s->terrain[7]=5;s->terrain[34]=0x15;
    wr16(ewram,0x200+34*2,4);
    for(int q=0;q<4;++q)wr16(ewram,0x1008+q*2,2);
    memset(vram+0x8040,0x22,32);wr16(palette,4,0x7E00);
    assert(fe8_voxel_render_scene(v,&memory,&map,s,640,480));
    unsigned backgrounds=fe8_voxel_stats(v).background_builds;
    unsigned refreshes=fe8_voxel_stats(v).ground_refreshes;
    wr16(palette,4,0x3E00);
    scene=fe8_voxel_render_scene(v,&memory,&map,s,640,480);assert(scene);
    assert(fe8_voxel_stats(v).background_builds==backgrounds);
    assert(fe8_voxel_stats(v).ground_refreshes==refreshes+1);
    uint32_t *cached=malloc(640*480*4);assert(cached);memcpy(cached,scene,640*480*4);
    fe8_voxel_invalidate(v);
    scene=fe8_voxel_render_scene(v,&memory,&map,s,640,480);assert(scene);
    assert(!memcmp(cached,scene,640*480*4));
    backgrounds=fe8_voxel_stats(v).background_builds;
    wr16(palette,2,0x7FFF);
    assert(fe8_voxel_render_scene(v,&memory,&map,s,640,480));
    assert(fe8_voxel_stats(v).background_builds==backgrounds+1);
    free(cached);
    puts("PASS animated-ground cache is pixel-identical to full regeneration; changed building materials rebuild scenery");
    s->flags&=~FE8_SNAPSHOT_MAP_SPRITES;
    s->visible_unit_count=FE8_MAX_VISIBLE_UNITS;
    wr16(ewram,0x5008,0);ewram[0x500B]=0;
    for(unsigned i=0;i<s->visible_unit_count;++i){
        s->visible_units[i].map_sprite_handle=0x02005000;
        s->visible_units[i].x=3;s->visible_units[i].y=3;
    }
    assert(fe8_voxel_render_scene(v,&memory,&map,s,640,480));
    assert(fe8_voxel_stats(v).sprites==FE8_MAX_VISIBLE_UNITS);
    puts("PASS all 128 actors in the compatibility sprite path");
    /* The highlighted ally's SMS hide flag is a native animation handoff,
     * not an invisible unit. Only matching live OBJ evidence may replace it. */
    memset(s->terrain,1,36);s->flags=FE8_SNAPSHOT_TERRAIN|FE8_SNAPSHOT_MAP_SPRITES|FE8_SNAPSHOT_UNIT_MAP;
    s->cursor_x=s->cursor_y=3;s->map_sprite_count=1;s->visible_unit_count=1;
    s->map_sprites[0]=(Fe8VisibleMapSprite){48,48,0xC000,0x80};
    s->visible_units[0]=(Fe8VisibleUnit){.unit_id=1,.x=3,.y=3,.map_sprite_handle=0x02005000};
    s->unit_map[21]=1;wr16(ewram,0x5004,48);wr16(ewram,0x5006,48);
    wr16(ewram,0x5008,0xC000);ewram[0x500B]=0x80;
    for(int n=0;n<128;++n)wr16(oam,n*8,0x0200);
    wr16(io,0,0x1F00);
    wr16(oam,0,32);wr16(oam,2,0x8028);wr16(oam,4,0xCB80);
    wr16(palette,0x382,0x7C1F);
    for(int ty=0;ty<4;++ty)for(int tx=0;tx<4;++tx)
        memset(vram+0x17000+(ty*32+tx)*32,0x11,32);
    fe8_voxel_home(v);fe8_voxel_invalidate(v);
    assert(fe8_voxel_render_scene(v,&memory,&map,s,640,480));
    assert(fe8_voxel_stats(v).sprites==1&&fe8_voxel_stats(v).hover_sprites==1);
    assert(fe8_voxel_project(v,3.5f,3.9375f,12,&sx,&sy));
    assert(fe8_voxel_pick(v,sx,sy,&x,&y)&&x==3&&y==3);
    uint64_t hovered=digest(fe8_voxel_render_scene(v,&memory,&map,s,640,480),640*480*4);
    vram[0x17000]=0; /* first native animation pixel pair */
    assert(digest(fe8_voxel_render_scene(v,&memory,&map,s,640,480),640*480*4)!=hovered);
    uint64_t unflipped=digest(fe8_voxel_render_scene(v,&memory,&map,s,640,480),640*480*4);
    wr16(oam,2,0x9028); /* native horizontal flip */
    assert(digest(fe8_voxel_render_scene(v,&memory,&map,s,640,480),640*480*4)!=unflipped);
    wr16(oam,2,0xA028); /* native vertical flip */
    assert(digest(fe8_voxel_render_scene(v,&memory,&map,s,640,480),640*480*4)!=unflipped);
    wr16(oam,2,0x802A); /* Archanae's idle archer shifts two pixels sideways. */
    assert(fe8_voxel_render_scene(v,&memory,&map,s,640,480));
    assert(fe8_voxel_stats(v).hover_sprites==1&&fe8_voxel_stats(v).sprites==1);
    wr16(oam,2,0x8028);
    Fe8Snapshot saved=*s;
    for(unsigned reject=0;reject<12;++reject) {
        *s=saved;
        switch(reject) {
        case 0:s->cursor_x=2;break;
        case 1:s->visible_unit_count=0;break;
        case 2:s->visible_units[0].faction=0x80;break;
        case 3:s->visible_units[0].state=0x200;break; /* fog */
        case 4:s->visible_units[0].state=4;break; /* dead */
        case 5:s->visible_units[0].state=1;break; /* hidden */
        case 6:s->input_lock=1;break;
        case 7:s->game_state_bits=1;break;
        case 8:s->phase=0x80;break;
        case 9:s->unit_map[21]=2;break;
        case 10:s->visible_units[0].map_sprite_handle=0x08000000;break;
        case 11:s->flags&=~FE8_SNAPSHOT_UNIT_MAP;break;
        }
        if(reject==7)assert(!fe8_voxel_render_scene(v,&memory,&map,s,640,480));
        else {
            assert(fe8_voxel_render_scene(v,&memory,&map,s,640,480));
            assert(!fe8_voxel_stats(v).hover_sprites&&!fe8_voxel_stats(v).sprites);
        }
    }
    *s=saved;
    for(unsigned bad=0;bad<6;++bad) {
        wr16(oam,0,bad==0?0x0220:bad==1?0x0120:bad==2?0x0420:32);
        wr16(oam,2,bad==3?0x8020:0x8028);
        wr16(oam,4,bad==4?0xDB80:0xCB80);
        wr16(io,0,bad==5?0x1F40:0x1F00);
        assert(fe8_voxel_render_scene(v,&memory,&map,s,640,480));
        assert(!fe8_voxel_stats(v).hover_sprites&&!fe8_voxel_stats(v).sprites);
    }
    wr16(io,0,0x1F00);wr16(oam,0,32);wr16(oam,2,0x8028);wr16(oam,4,0xCB80);
    assert(fe8_voxel_render_scene(v,&memory,&map,s,640,480));
    assert(fe8_voxel_stats(v).hover_sprites==1&&fe8_voxel_stats(v).sprites==1);
    puts("PASS highlighted native MU billboard, animation/flip/picking, and rejection of hidden/fog/dead/mismatched actors");
    /* A selected/travelling image is accepted only through its exact native
     * owner/config/AP chain. Mutating the game state is never needed. */
    s->game_state_bits=3;s->active_unit_address=0x02006000;s->visible_unit_count=0;
    wr32(ewram,0x6000,0x08000100);wr32(ewram,0x6004,0x08000180);
    ewram[0x600B]=1;wr32(ewram,0x600C,1);ewram[0x6010]=ewram[0x6011]=3;ewram[0x6013]=20;
    wr32(ewram,0x603C,0x02005000);
    wr32(ewram,0x7000,0x08000080);wr32(ewram,0x702C,0x02006000);
    wr32(ewram,0x7030,0x03000100);wr32(ewram,0x7034,0x03001000);
    ewram[0x703F]=1;wr16(ewram,0x704C,48*16);wr16(ewram,0x704E,48*16);
    iwram[0x1000]=1;iwram[0x1001]=12;wr16(iwram,0x1002,0x380);wr32(iwram,0x1048,0x02007000);
    wr32(iwram,0x100,0x08000100);wr32(iwram,0x110,0x08000200);wr16(iwram,0x122,0xCB80);
    wr16(rom,0x200,1);wr16(rom,0x202,0xE0);wr16(rom,0x204,0x81F0);
    assert(fe8_voxel_render_scene(v,&memory,&map,s,640,480));
    assert(fe8_voxel_stats(v).sprites==1 && fe8_voxel_stats(v).active_unit_sprites==1);
    uint64_t state_digest=digest(ewram,sizeof(ewram)), internal_digest=digest(iwram,sizeof(iwram));
    for(unsigned n=0;n<8;++n)assert(fe8_voxel_render_scene(v,&memory,&map,s,640,480));
    assert(state_digest==digest(ewram,sizeof(ewram)) && internal_digest==digest(iwram,sizeof(iwram)));
    ewram[0x703F]=2;s->game_state_bits=0;wr16(ewram,0x704C,50*16);
    assert(fe8_voxel_render_scene(v,&memory,&map,s,640,480));
    assert(fe8_voxel_stats(v).unit_moving && fabsf(fe8_voxel_stats(v).active_x-3.625f)<.001f);
    const Fe8VoxelGpuFrame *gpu=fe8_voxel_build_gpu(v,&memory,&map,s,3456,2234);assert(gpu);
    assert(gpu->width<=1920&&gpu->height<=1080&&gpu->billboards.count==6&&gpu->scenery.count>0);
    uint64_t geometry=gpu->scenery.revision,geometry_hash=digest(gpu->scenery.vertices,gpu->scenery.count*sizeof(Fe8GpuVertex));
    uint64_t raster=fe8_voxel_stats(v).billboard_pixels,upscales=fe8_voxel_stats(v).output_pixels;
    float transform[16];memcpy(transform,gpu->transform,sizeof(transform));
    fe8_voxel_camera(v,.15f,1);gpu=fe8_voxel_build_gpu(v,&memory,&map,s,3456,2234);assert(gpu);
    assert(gpu->scenery.revision==geometry && geometry_hash==digest(gpu->scenery.vertices,gpu->scenery.count*sizeof(Fe8GpuVertex)));
    assert(memcmp(transform,gpu->transform,sizeof(transform)));
    assert(fe8_voxel_stats(v).billboard_pixels==raster&&fe8_voxel_stats(v).output_pixels==upscales);
    uint64_t atlas_revision=gpu->atlas_revision;vram[0x17000]^=0x11;
    gpu=fe8_voxel_build_gpu(v,&memory,&map,s,3456,2234);assert(gpu&&gpu->atlas_revision>atlas_revision);
    for(unsigned i=0;i<16;++i)assert(isfinite(gpu->transform[i]));
    assert(fe8_voxel_project(v,3.625f,3.9375f,12,&sx,&sy));
    assert(fe8_voxel_pick(v,sx,sy,&x,&y)&&x==3&&y==3);
    assert(!fe8_voxel_capture(v)); /* GPU pixels are captured by the presenter, not fake CPU re-renders. */
    /* Native Wait: MU retires before its grey standing handle is installed. */
    wr16(ewram,0x704C,48*16);
    assert(fe8_voxel_build_gpu(v,&memory,&map,s,640,480));
    s->map_sprite_count=0;wr32(ewram,0x600C,2);wr32(ewram,0x603C,0);wr32(ewram,0x7000,0);
    assert(fe8_voxel_build_gpu(v,&memory,&map,s,640,480));
    assert(fe8_voxel_stats(v).sprites==1&&fe8_voxel_stats(v).active_unit_sprites==1);
    s->unit_map[21]=2;
    assert(fe8_voxel_build_gpu(v,&memory,&map,s,640,480));assert(fe8_voxel_stats(v).sprites==0);
    s->unit_map[21]=1;wr16(oam,4,0xDB80);
    assert(!fe8_voxel_build_gpu(v,&memory,&map,s,640,480));
    wr16(oam,4,0xCB80);wr32(ewram,0x7000,0x08000080);
    wr32(ewram,0x600C,1);wr32(ewram,0x603C,0x02005000);s->map_sprite_count=1;
    puts("PASS acted-unit Wait handoff uses only its exact current PPU image and owner tile");
    s->game_state_bits=3;
    for(unsigned reject=0;reject<10;++reject){
        switch(reject){
        case 0:wr32(iwram,0x1048,0);break; /* no reciprocal ownership */
        case 1:ewram[0x7040]=1;break;
        case 2:ewram[0x703F]=7;break; /* death fade */
        case 3:wr32(ewram,0x600C,5);break; /* dead */
        case 4:wr32(ewram,0x600C,0x21);break; /* rescued */
        case 5:wr16(rom,0x200,2);break; /* unknown multi-object AP */
        case 6:wr16(rom,0x202,0x21E0);break; /* affine/8bpp */
        case 7:iwram[0x1001]=13;break; /* wrong palette */
        case 8:ewram[0x600B]=0x80;break; /* another faction */
        case 9:wr32(iwram,0x110,0x0FFFFFF0);break;
        }
        assert(!fe8_voxel_build_gpu(v,&memory,&map,s,640,480));
        wr32(iwram,0x1048,0x02007000);ewram[0x7040]=0;ewram[0x703F]=2;
        wr32(ewram,0x600C,1);wr16(rom,0x200,1);wr16(rom,0x202,0xE0);
        iwram[0x1001]=12;ewram[0x600B]=1;wr32(iwram,0x110,0x08000200);
    }
    assert(fe8_voxel_build_gpu(v,&memory,&map,s,640,480));
    assert(fe8_voxel_render_scene(v,&memory,&map,s,640,480));
    puts("PASS owned active-unit selection/walking, negative ownership cases, GPU mesh/atlas caching, picking and CPU fallback");

    fe8_voxel_destroy(v);fe8_voxel_destroy(NULL);free(s);return 0;
}
