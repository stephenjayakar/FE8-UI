#include "voxel_buildings.h"
#include "voxel_renderer.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Fe8Snapshot s;
static Fe8BuildingLayout layout;
static Fe8HostPixel art[128*128];
static void reset(int w,int h){memset(&s,0,sizeof(s));s.map_width=(uint8_t)w;s.map_height=(uint8_t)h;
    s.flags=FE8_SNAPSHOT_TERRAIN;memset(s.terrain,1,(size_t)w*h);}
static void tile(int x,int y,unsigned t){s.terrain[y*s.map_width+x]=(uint8_t)t;}
static void rectangle(int x,int y,int w,int h,unsigned roof){
    for(int yy=y;yy<y+h;++yy)for(int xx=x;xx<x+w;++xx)tile(xx,yy,roof);
}
static const Fe8Building *at(int x,int y){unsigned n=layout.owner[y*layout.width+x];return n?&layout.objects[n-1]:NULL;}
static uint64_t hash(const void *data,size_t n){const unsigned char *p=data;uint64_t h=1;
    while(n--)h=(h^*p++)*UINT64_C(1099511628211);return h;}
static void art_tile(int x,int y){
    const uint32_t c[]={0xFF799DBA,0xFF342F35,0xFFAFCCD8,0xFFADB05C,0xFF7890CC};
    for(int dy=0;dy<16;++dy)for(int dx=0;dx<16;++dx)
        art[(y*16+dy)*128+x*16+dx]=c[(dx+dy/3)%5];
}
typedef struct Geometry {unsigned boxes;uint64_t hash;float center_x,center_z,center_height;} Geometry;
static void record(void *ctx,float x,float y,float z,float w,float h,float d,uint32_t color){
    Geometry *g=ctx;float f[]={x,y,z,w,h,d};
    for(unsigned i=0;i<6;++i)assert(isfinite(f[i]));
    assert(w>0&&h>0&&d>0&&y>=0&&y+h<100);
    assert(color>>24==255);g->hash^=hash(f,sizeof(f))+color;++g->boxes;
    if(x<=g->center_x&&x+w>g->center_x&&z<=g->center_z&&z+d>g->center_z&&y+h>g->center_height)
        g->center_height=y+h;
}
static uint8_t ewram[0x40000],vram[0x18000],pal[0x400];
static uint8_t read8(void *ctx,uint32_t a){(void)ctx;
    if(a>=0x02000000&&a<0x02040000)return ewram[a-0x02000000];
    if(a>=0x06000000&&a<0x06018000)return vram[a-0x06000000];
    if(a>=0x05000000&&a<0x05000400)return pal[a-0x05000000];return 0;}
static void wr16(uint8_t *p,int a,unsigned v){p[a]=(uint8_t)v;p[a+1]=(uint8_t)(v>>8);}
static void wr32(uint8_t *p,int a,uint32_t v){wr16(p,a,v);wr16(p,a+2,v>>16);}
static void ppm(const char *dir,const char *name,const uint32_t *p,int w,int h){
    char file[1024];snprintf(file,sizeof(file),"%s/%s.ppm",dir,name);FILE *f=fopen(file,"wb");assert(f);
    fprintf(f,"P6\n%d %d\n255\n",w,h);
    for(int i=0;i<w*h;++i){uint8_t c[]={(uint8_t)p[i],(uint8_t)(p[i]>>8),(uint8_t)(p[i]>>16)};assert(fwrite(c,1,3,f)==3);}fclose(f);
}
static void renderer_test(const char *dir){
    reset(17,13);s.flags|=FE8_SNAPSHOT_MAP_SPRITES;s.cursor_x=16;s.cursor_y=12;
    const unsigned kinds[]={6,7,8,3,0x0B,0x24};
    for(int i=0;i<6;++i){int x=2+(i%3)*5,y=2+(i/3)*6;rectangle(x-1,y,3,3,kinds[i]==0x0B?0x2C:0x2E);tile(x,y+2,kinds[i]);}
    Fe8MapRenderState map={.map_width=s.map_width,.map_height=s.map_height,.base_tile_rows=0x02000100,
        .tileset_config=0x02001000,.tile_graphics=0x06008000,.palette=0x05000000};
    for(int y=0;y<s.map_height;++y){wr32(ewram,0x100+y*4,0x02000200+y*s.map_width*2);
        for(int x=0;x<s.map_width;++x)wr16(ewram,0x200+(y*s.map_width+x)*2,0);}
    for(int i=0;i<4;++i)wr16(ewram,0x1000+i*2,1);
    for(int i=0;i<32;++i)vram[0x8020+i]=(uint8_t)(0x11+(i%7==0?0x11:0)+(i%11==0?0x22:0));
    wr16(pal,2,0x2EAF);wr16(pal,4,0x2ECE);wr16(pal,6,0x2AB0);wr16(pal,8,0x32D0);
    Fe8MemoryView memory={NULL,read8};Fe8VoxelRenderer *v=fe8_voxel_create();assert(v);
    uint64_t before=hash(&s,sizeof(s)),ram=hash(ewram,sizeof(ewram));
    assert(fe8_voxel_render_scene(v,&memory,&map,&s,1440,960));
    Fe8VoxelStats a=fe8_voxel_stats(v);
    assert(a.buildings[FE8_BUILDING_ARMORY]==1&&a.buildings[FE8_BUILDING_VENDOR]==1&&a.buildings[FE8_BUILDING_ARENA]==1);
    assert(a.buildings[FE8_BUILDING_CASTLE]==1&&a.buildings[FE8_BUILDING_VILLAGE]==1&&a.buildings[FE8_BUILDING_CHURCH]==1);
    assert(!a.buildings[FE8_BUILDING_HOUSE]&&!a.buildings[FE8_BUILDING_ROOF]);
    Fe8Building found;assert(fe8_voxel_building_at(v,2,4,&found)&&found.kind==FE8_BUILDING_ARMORY);
    assert(!fe8_voxel_building_at(v,-1,0,&found));
    for(int i=0;i<8;++i)assert(fe8_voxel_render_scene(v,&memory,&map,&s,1440,960));
    assert(fe8_voxel_stats(v).background_builds==a.background_builds&&fe8_voxel_stats(v).reused_frames>=8);
    tile(2,4,7);assert(fe8_voxel_render_scene(v,&memory,&map,&s,1440,960));
    assert(fe8_voxel_stats(v).background_builds==a.background_builds+1);
    assert(fe8_voxel_building_at(v,2,4,&found)&&found.kind==FE8_BUILDING_VENDOR);tile(2,4,6);
    assert(hash(&s,sizeof(s))==before&&hash(ewram,sizeof(ewram))==ram);
    if(dir){
        fe8_voxel_home(v);fe8_voxel_camera(v,.16f,1.12f);
        ppm(dir,"native-model-gallery",fe8_voxel_render(v,&memory,&map,&s,1920,1280),1920,1280);
        fe8_voxel_home(v);fe8_voxel_focus(v,12.5f,3.5f);fe8_voxel_camera(v,.20f,2.1f);
        ppm(dir,"native-arena-detail",fe8_voxel_render(v,&memory,&map,&s,1440,1000),1440,1000);
    }
    fe8_voxel_destroy(v);puts("PASS renderer dispatch, one model per footprint, retained warm cache, type-change invalidation, immutable input");
}
int main(int argc,char **argv){
    assert(fe8_building_kind(6)==FE8_BUILDING_ARMORY&&fe8_building_kind(7)==FE8_BUILDING_VENDOR);
    assert(fe8_building_kind(8)==FE8_BUILDING_ARENA&&fe8_building_kind(0x30)==FE8_BUILDING_ARENA);
    assert(!fe8_building_kind(1)&&!fe8_building_kind(0x1F)&&!fe8_building_kind(0xFF));
    for(unsigned t=0;t<0x41;++t)if(fe8_building_kind(t)){
        reset(1,1);tile(0,0,t);assert(fe8_buildings_classify(&layout,&s,NULL,0));
        assert(layout.count==1&&at(0,0)->kind==fe8_building_kind(t));
    }
    puts("PASS every canonical building ID, both arena IDs, and unknown/throne non-building cases");
    reset(8,7);rectangle(2,1,3,3,0x2E);tile(3,3,6);
    assert(fe8_buildings_classify(&layout,&s,NULL,0));assert(layout.count==1);
    const Fe8Building *b=at(3,3);assert(b->kind==FE8_BUILDING_ARMORY&&b->x==2&&b->y==1&&b->width==3&&b->height==3);
    assert(at(2,1)==b&&at(4,3)==b&&b->evidence&FE8_BUILDING_FOOTPRINT);
    tile(1,1,6);tile(3,3,3);assert(fe8_buildings_classify(&layout,&s,NULL,0));
    assert(layout.count==2&&at(1,1)->width==1&&at(2,1)->kind==FE8_BUILDING_VILLAGE);
    /* Two touching Sacred Echoes-style roofs must stay two buildings. */
    reset(8,6);rectangle(0,0,3,2,0x2E);tile(1,1,4);rectangle(3,1,3,2,0x2E);tile(4,2,5);
    assert(fe8_buildings_classify(&layout,&s,NULL,0));assert(layout.count==2);
    assert(at(1,1)->width==3&&at(1,1)->height==2&&at(4,2)->width==3&&at(4,2)->height==2);
    assert(at(2,1)!=at(3,1));
    puts("PASS complete entrance/roof ownership, armory beside a village, and touching separate footprints");
    reset(7,7);rectangle(1,1,3,3,0x2E);tile(2,3,8);tile(1,2,1);
    assert(fe8_buildings_classify(&layout,&s,NULL,0));assert(!at(1,2)&&at(2,3)->kind==FE8_BUILDING_ARENA);
    for(int y=0;y<7;++y)for(int x=0;x<7;++x)if(s.terrain[y*7+x]==1)assert(!at(x,y));
    reset(3,3);tile(1,1,6);s.flags|=FE8_SNAPSHOT_FOG;memset(s.fog,1,9);s.fog[4]=0;
    assert(fe8_buildings_classify(&layout,&s,NULL,0)&&!layout.count);
    reset(3,3);tile(1,1,0x2C);assert(fe8_buildings_classify(&layout,&s,NULL,0));assert(at(1,1)->kind==FE8_BUILDING_ROOF);
    puts("PASS no rectangular hole erasure, no fog-hidden shops, conservative unknown roof fallback");
    reset(8,8);memset(art,0,sizeof(art));tile(1,1,6);tile(4,1,5);tile(6,1,0xFE);
    art_tile(1,1);art_tile(4,1);art_tile(6,1);
    assert(fe8_buildings_classify(&layout,&s,art,128));
    assert(at(4,1)->kind==FE8_BUILDING_ARMORY&&at(4,1)->evidence&FE8_BUILDING_ART_MATCH);
    assert(at(6,1)->kind==FE8_BUILDING_ARMORY);
    tile(1,4,8);art_tile(1,4);assert(fe8_buildings_classify(&layout,&s,art,128));
    assert(at(4,1)->kind==FE8_BUILDING_HOUSE&&!at(6,1));
    memset(art,0,sizeof(art));assert(fe8_buildings_classify(&layout,&s,art,128));
    assert(at(4,1)->kind==FE8_BUILDING_HOUSE&&!at(6,1));
    puts("PASS tileset-local art signature inference, ambiguity rejection, blank-art rejection, semantic priority");
    assert(!fe8_buildings_classify(NULL,&s,art,128));assert(!fe8_buildings_classify(&layout,NULL,NULL,0));
    assert(!fe8_buildings_classify(&layout,&s,art,100));s.map_width=65;assert(!fe8_buildings_classify(&layout,&s,NULL,0));
    reset(64,64);memset(s.terrain,8,FE8_MAX_MAP_CELLS);assert(fe8_buildings_classify(&layout,&s,NULL,0));assert(layout.count==FE8_MAX_MAP_CELLS);
    uint64_t memory=hash(&s,sizeof(s));assert(fe8_buildings_classify(&layout,&s,NULL,0));assert(memory==hash(&s,sizeof(s)));
    puts("PASS malformed input, full 4096-cell map, deterministic read-only classification");
    uint64_t shapes[FE8_BUILDING_COUNT]={0};
    for(int kind=1;kind<FE8_BUILDING_COUNT;++kind){
        Fe8Building model={(Fe8BuildingKind)kind,0,0,3,3,1,2,FE8_BUILDING_TERRAIN};
        Geometry g={.center_x=24,.center_z=24};Fe8BuildingPainter p={&g,record,0xFF497FA3,0xFFABC1CE};
        fe8_building_draw(&model,&p);assert(g.boxes>0&&g.boxes<2500);shapes[kind]=g.hash;
        if(kind==FE8_BUILDING_ARENA)assert(g.center_height<1);
        for(int i=1;i<kind;++i)assert(shapes[i]!=shapes[kind]);
        model.width=model.height=1;g=(Geometry){0};fe8_building_draw(&model,&p);assert(g.boxes>0);
    }
    puts("PASS all distinct model silhouettes, open arena interior, finite positive geometry, bounded single/multi-tile recipes");
    renderer_test(argc>1?argv[1]:NULL);return 0;
}
