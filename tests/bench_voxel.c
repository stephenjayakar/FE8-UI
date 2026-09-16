/* Optional reproducible CPU benchmark. ROM and state paths are local inputs;
 * no assets are bundled. GPU, HUD, emulation, and upload time are NOT included. */
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
static int compare(const void *a,const void *b){double x=*(const double *)a,y=*(const double *)b;return (x>y)-(x<y);}
static const uint32_t *render(Fe8VoxelRenderer *v,const Fe8MemoryView *m,
        const Fe8MapRenderState *map,const Fe8Snapshot *s,int w,int h){
#ifdef FE8_BENCH_SCENE
    return fe8_voxel_render_scene(v,m,map,s,w,h);
#else
    return fe8_voxel_render(v,m,map,s,w,h);
#endif
}
int main(int argc,char **argv){
    if(argc!=8){fprintf(stderr,"Usage: %s ROM STATE WIDTH HEIGHT FRAMES static|animated|orbit TAG\n",argv[0]);return 2;}
    int w=atoi(argv[3]),h=atoi(argv[4]),frames=atoi(argv[5]);
    if(w<1||h<1||frames<1||frames>10000)return 2;
    const char *mode=argv[6];
    if(strcmp(mode,"static")&&strcmp(mode,"animated")&&strcmp(mode,"orbit"))return 2;
    struct mCore *c=mCoreFind(argv[1]);assert(c&&c->init(c));mCoreInitConfig(c,"fe8-voxel-benchmark");
    struct mStandardLogger logger;mStandardLoggerInit(&logger);logger.d.filter->defaultLevels=mLOG_FATAL|mLOG_ERROR;mLogSetDefaultLogger(&logger.d);
    struct VFile *rom=VFileOpen(argv[1],O_RDONLY);assert(rom&&c->loadROM(c,rom));
    mColor *video=calloc(240*160,sizeof(*video));assert(video);c->setVideoBuffer(c,video,240);c->reset(c);
    struct VFile *state=VFileOpen(argv[2],O_RDONLY);assert(state&&mCoreLoadStateNamed(c,state,SAVESTATE_ALL));state->close(state);
    Fe8AddressSpace space;fe8_address_space_init(&space,c,read8);
    const uint32_t bases[]={0x02000000,0x03000000,0x05000000,0x06000000,0x08000000};
    for(unsigned i=0;i<5;++i){size_t n=0;void *p=mCoreGetMemoryBlock(c,bases[i],&n);assert(p&&fe8_address_space_add(&space,bases[i],p,n));}
    Fe8MemoryReader reader={&space,fe8_address_space_read8};Fe8MemoryView memory={&space,fe8_address_space_read8};
    const Fe8Profile *profile=fe8_profile_for_rom(&reader);
    Fe8Snapshot *s=calloc(1,sizeof(*s));assert(s&&fe8_extract_snapshot(&reader,profile,s));
    Fe8PaletteMapping pal={0};Fe8TerrainCache *cache=fe8_terrain_cache_create();assert(cache);
    Fe8MapRenderState map={.map_width=s->map_width,.map_height=s->map_height,.camera_x=s->camera_x,.camera_y=s->camera_y,
        .base_tile_rows=s->base_tile_rows,.fog_rows=s->fog_rows,.tileset_config=profile->tileset_config,
        .tile_graphics=0x06008000,.palette=0x05000000,.palette_mapping=&pal,.tile_cache=cache};
    uint32_t frame[240*160];
    for(int t=0;t<3;++t){c->setKeys(c,0);c->runFrame(c);for(int i=0;i<240*160;++i)frame[i]=0xFF000000u|(video[i]&0xFFFFFFu);fe8_learn_palette_mapping(&memory,&map,frame,240);}
    assert(fe8_extract_snapshot(&reader,profile,s));
    Fe8VoxelRenderer *v=fe8_voxel_create();assert(v);
    clock_t start=clock();assert(render(v,&memory,&map,s,w,h));
    double cold=1000.*(clock()-start)/CLOCKS_PER_SEC;
    double *samples=malloc(frames*sizeof(*samples));assert(samples);double total=0;
    for(int i=0;i<frames;++i){
        if(strcmp(mode,"static")){
            c->setKeys(c,0);c->runFrame(c);assert(fe8_extract_snapshot(&reader,profile,s));
        }
        if(!strcmp(mode,"orbit"))fe8_voxel_camera(v,.006f,1);
        start=clock();assert(render(v,&memory,&map,s,w,h));
        samples[i]=1000.*(clock()-start)/CLOCKS_PER_SEC;total+=samples[i];
    }
    qsort(samples,frames,sizeof(*samples),compare);
    Fe8VoxelStats stats=fe8_voxel_stats(v);
    printf("%s,%s,%dx%d,%d,cold_ms=%.3f,mean_ms=%.3f,p50_ms=%.3f,p95_ms=%.3f,terrain_builds=%u,sprite_builds=%u",
        argv[7],mode,w,h,frames,cold,total/frames,samples[frames/2],samples[(frames-1)*95/100],stats.terrain_builds,stats.sprite_builds);
#ifdef FE8_BENCH_SCENE
    printf(",background_builds=%u,reused_frames=%u,cpu_output_pixels=%llu",stats.background_builds,stats.reused_frames,(unsigned long long)stats.output_pixels);
#endif
    puts("");free(samples);fe8_voxel_destroy(v);fe8_terrain_cache_destroy(cache);free(s);
    mCoreConfigDeinit(&c->config);c->deinit(c);free(video);mLogSetDefaultLogger(NULL);mStandardLoggerDeinit(&logger);return 0;
}
