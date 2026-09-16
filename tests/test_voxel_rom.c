/* Live-ROM renderer contract. ROMs, states and decoded pixels stay local. */
#include <mgba/flags.h>
#include <mgba/core/core.h>
#include <mgba/core/interface.h>
#include <mgba/core/serialize.h>
#include <mgba/core/log.h>
#include <mgba-util/vfs.h>
#include "address_space.h"
#include "voxel_renderer.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static uint8_t read8(void *ctx,uint32_t a){struct mCore *c=ctx;return c->busRead8(c,a);}
static void step(struct mCore *c,unsigned n,unsigned keys){c->setKeys(c,keys);while(n--)c->runFrame(c);}
static void ppm(const char *prefix,const char *tag,const uint32_t *p,int w,int h){
    if(!prefix)return;
    char path[1024];snprintf(path,sizeof(path),"%s-%s.ppm",prefix,tag);
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n%d %d\n255\n",w,h);
    for(int i=0;i<w*h;++i){uint8_t c[3]={p[i]&255,(p[i]>>8)&255,(p[i]>>16)&255};assert(fwrite(c,1,3,f)==3);}fclose(f);
}
int main(int argc,char **argv){
    assert(argc>=2);const char *prefix=argc>2?argv[2]:NULL;
    struct mCore *c=mCoreFind(argv[1]);assert(c&&c->init(c));mCoreInitConfig(c,"fe8-voxel-test");
    struct mStandardLogger logger;mStandardLoggerInit(&logger);logger.d.filter->defaultLevels=mLOG_FATAL|mLOG_ERROR;mLogSetDefaultLogger(&logger.d);
    struct VFile *rom=VFileOpen(argv[1],O_RDONLY);assert(rom&&c->loadROM(c,rom));
    mColor *video=calloc(240*160,sizeof(*video));assert(video);c->setVideoBuffer(c,video,240);c->reset(c);
    Fe8AddressSpace space;fe8_address_space_init(&space,c,read8);
    const uint32_t bases[]={0x02000000,0x03000000,0x05000000,0x06000000,0x08000000};
    for(unsigned i=0;i<5;++i){size_t n=0;void *p=mCoreGetMemoryBlock(c,bases[i],&n);assert(p&&fe8_address_space_add(&space,bases[i],p,n));}
    Fe8MemoryReader reader={&space,fe8_address_space_read8};Fe8MemoryView memory={&space,fe8_address_space_read8};
    const Fe8Profile *profile=fe8_profile_for_rom(&reader);
    Fe8Snapshot *s=calloc(1,sizeof(*s));assert(s);
    if(strstr(profile->profile_name,"Archanae")){
        for(unsigned frame=1;frame<=4440;++frame){unsigned p=frame%360;step(c,1,frame<=3840?(p<3?1:p>=180&&p<183?8:0):0);}
    }else{
        unsigned stable=0;
        for(unsigned frame=0;frame<20000&&stable<90;++frame){
            unsigned p=frame%360;step(c,1,stable?0:p<3?1:p>=180&&p<183?8:0);
            if(fe8_extract_snapshot(&reader,profile,s)&&s->input_lock==0&&s->phase==0&&s->visible_unit_count>=3)++stable;else stable=0;
        }
        assert(stable==90);step(c,30,0);
    }
    assert(fe8_extract_snapshot(&reader,profile,s));
    printf("%s: chapter %u map %ux%u units %u SMS %u lock %u\n",profile->profile_name,s->chapter,s->map_width,s->map_height,s->visible_unit_count,s->map_sprite_count,s->input_lock);
    if(prefix){
        char path[1024];snprintf(path,sizeof(path),"%s.ss",prefix);struct VFile *f=VFileOpen(path,O_RDWR|O_CREAT|O_TRUNC);assert(f&&mCoreSaveStateNamed(c,f,SAVESTATE_ALL));f->close(f);
        snprintf(path,sizeof(path),"%s-terrain.txt",prefix);FILE *out=fopen(path,"w");assert(out);
        for(int y=0;y<s->map_height;++y){for(int x=0;x<s->map_width;++x)fprintf(out,"%02X ",s->terrain[y*s->map_width+x]);fputc('\n',out);}fclose(out);
    }
    Fe8PaletteMapping pal={0};Fe8TerrainCache *cache=fe8_terrain_cache_create();assert(cache);
    Fe8MapRenderState map={.map_width=s->map_width,.map_height=s->map_height,.camera_x=s->camera_x,.camera_y=s->camera_y,
        .base_tile_rows=s->base_tile_rows,.fog_rows=s->fog_rows,.tileset_config=profile->tileset_config,
        .tile_graphics=0x06008000,.palette=0x05000000,.palette_mapping=&pal,.tile_cache=cache};
    uint32_t frame[240*160];
    for(int t=0;t<3;++t){step(c,1,0);for(int i=0;i<240*160;++i)frame[i]=0xFF000000u|(video[i]&0xFFFFFFu);fe8_learn_palette_mapping(&memory,&map,frame,240);}
    assert(fe8_extract_snapshot(&reader,profile,s));
    Fe8VoxelRenderer *view=fe8_voxel_create();assert(view);
    size_t size=c->stateSize(c);void *before=malloc(size),*after=malloc(size);assert(before&&after&&c->saveState(c,before));
    clock_t start=clock();uint32_t *image=fe8_voxel_render(view,&memory,&map,s,1440,960);assert(image);
    double cold=1000.*(clock()-start)/CLOCKS_PER_SEC;
    assert(c->saveState(c,after)&&memcmp(before,after,size)==0);puts("PASS render does not mutate serialized emulator state");
    ppm(prefix,"overview",image,1440,960);
    Fe8VoxelStats first=fe8_voxel_stats(view);assert(first.sprites>0&&first.columns>0);
    start=clock();for(int i=0;i<30;++i)assert(fe8_voxel_render(view,&memory,&map,s,1440,960));
    double warm=1000.*(clock()-start)/CLOCKS_PER_SEC/30;
    Fe8VoxelStats next=fe8_voxel_stats(view);assert(first.terrain_builds==next.terrain_builds&&first.sprite_builds==next.sprite_builds&&next.cached_sprites>first.cached_sprites);
    puts("PASS unchanged pixels reuse terrain and sprite geometry");
    unsigned saved_sprite_count=s->map_sprite_count;
    s->map_sprite_count=0; /* test the ground transform without actor occlusion */
    for(int turn=0;turn<4;++turn){
        fe8_voxel_camera(view,.2f,1);assert(fe8_voxel_render(view,&memory,&map,s,1440,960));
        for(int y=0;y<s->map_height;++y)for(int x=0;x<s->map_width;++x){
            float sx,sy;assert(fe8_voxel_project(view,x+.5f,y+.5f,0,&sx,&sy));
            int px,py;if(sx>=0&&sy>=0&&sx<1440&&sy<960){if(!fe8_voxel_pick(view,sx,sy,&px,&py)||px!=x||py!=y){fprintf(stderr,"pick fail turn %d tile %d,%d screen %f,%f got %d,%d\n",turn,x,y,sx,sy,px,py);abort();}}
        }
    }
    s->map_sprite_count=(uint16_t)saved_sprite_count;
    puts("PASS every visible tile round-trips through all four camera orientations");
    fe8_voxel_home(view);fe8_voxel_camera(view,.15f,2.4f);fe8_voxel_focus(view,s->cursor_x+.5f,s->cursor_y+.5f);
    image=fe8_voxel_render(view,&memory,&map,s,1440,960);assert(image);ppm(prefix,"detail",image,1440,960);
    unsigned builds=fe8_voxel_stats(view).terrain_builds;
    s->terrain[0]^=1;assert(fe8_voxel_render(view,&memory,&map,s,1440,960));assert(fe8_voxel_stats(view).terrain_builds==builds+1);s->terrain[0]^=1;
    puts("PASS terrain semantic changes invalidate generation cache");
    assert(!fe8_voxel_render(view,&memory,&map,s,0,960));assert(!fe8_voxel_render(view,&memory,&map,s,100000,100000));
    unsigned width=s->map_width;s->map_width=65;assert(!fe8_voxel_render(view,&memory,&map,s,1440,960));s->map_width=width;
    assert(!fe8_voxel_pick(view,-1,0,(int*)&width,(int*)&width));puts("PASS malformed dimensions and offscreen input fail closed");
    fe8_voxel_invalidate(view);assert(!fe8_voxel_pick(view,10,10,(int*)&width,(int*)&width));assert(fe8_voxel_render(view,&memory,&map,s,960,640));
    assert(c->saveState(c,after)&&!memcmp(before,after,size));puts("PASS invalidation, resize, orbit, zoom and pan remain read-only");
    printf("Renderer CPU at 1440x960: cold %.2f ms; 30-frame warm mean %.2f ms; columns %u; sprites %u\n",cold,warm,first.columns,first.sprites);
    fe8_voxel_destroy(view);fe8_terrain_cache_destroy(cache);free(before);free(after);free(s);
    mCoreConfigDeinit(&c->config);c->deinit(c);free(video);mLogSetDefaultLogger(NULL);mStandardLoggerDeinit(&logger);return 0;
}
