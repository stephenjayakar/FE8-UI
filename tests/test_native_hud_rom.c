/* Optional, read-only integration test. The ROM is supplied locally; neither
 * it nor emulator states are written or included in test/CI artifacts. */
#include <mgba/flags.h>
#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba-util/image.h>
#include <mgba-util/vfs.h>
#include "native_hud.h"

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t bus_read(void *context, uint32_t address) {
    struct mCore *core=context; return core->busRead8(core,address);
}
static uint8_t raw_read(void *context, uint32_t address) {
    struct mCore *core=context; return core->rawRead8(core,address,-1);
}
static void convert(const mColor *input, Fe8HostPixel *output) {
    for(unsigned p=0;p<240*160;++p) {
#ifdef COLOR_16_BIT
        output[p]=0xFF000000|M_R8(input[p])|(M_G8(input[p])<<8)|(M_B8(input[p])<<16);
#else
        output[p]=0xFF000000|(input[p]&0xFFFFFF);
#endif
    }
}
static void run(struct mCore *core, unsigned frames, unsigned keys) {
    core->setKeys(core,keys);
    for(unsigned n=0;n<frames;++n) core->runFrame(core);
}
int main(int argc, char **argv) {
    assert(argc==2);
    struct mCore *core=mCoreFind(argv[1]); assert(core&&core->init(core));
    mCoreInitConfig(core,"fe8-hud-test");
    struct mStandardLogger logger; mStandardLoggerInit(&logger);
    logger.d.filter->defaultLevels=mLOG_FATAL|mLOG_ERROR; mLogSetDefaultLogger(&logger.d);
    struct VFile *rom=VFileOpen(argv[1],O_RDONLY); assert(rom&&core->loadROM(core,rom));
    mColor *video=calloc(240*160,sizeof(*video));
    Fe8HostPixel *frame=malloc(240*160*sizeof(*frame));
    Fe8NativeHud *hud=calloc(1,sizeof(*hud));
    Fe8Snapshot *snapshot=calloc(1,sizeof(*snapshot));
    assert(video&&frame&&hud&&snapshot);
    core->setVideoBuffer(core,video,240); core->reset(core);
    Fe8MemoryReader reader={core,bus_read}; Fe8MemoryView memory={core,raw_read};
    const Fe8Profile *profile=fe8_profile_for_rom(&reader);
    unsigned stable=0;
    for(unsigned n=0;n<20000&&stable<90;++n) {
        unsigned p=n%360;
        run(core,1,stable?0:(p<3?1:p>=180&&p<183?8:0));
        if(fe8_extract_snapshot(&reader,profile,snapshot)&&snapshot->input_lock==0&&
                snapshot->phase==0&&snapshot->visible_unit_count>=3) ++stable;
        else stable=0;
    }
    assert(stable==90);
    run(core,30,0); /* let the chapter-opening native window animation settle */
    size_t bytes=core->stateSize(core);
    void *checkpoint=malloc(bytes),*before=malloc(bytes),*after=malloc(bytes);
    assert(checkpoint&&before&&after); assert(core->saveState(core,checkpoint));
    unsigned valid=0;
    for(unsigned n=0;n<180;++n) {
        run(core,1,0); assert(fe8_extract_snapshot(&reader,profile,snapshot)); convert(video,frame);
        assert(core->saveState(core,before));
        bool active=fe8_native_hud_extract(hud,&memory,snapshot,frame,240,true);
        assert(core->saveState(core,after));
        assert(memcmp(before,after,bytes)==0); /* extraction cannot change game state */
        if(active) {
            ++valid; assert(hud->count>=2);
            fe8_native_hud_layout(hud,960,640,480,320,150);
            Fe8HudRect positions[FE8_HUD_MAX_PANELS];
            for(unsigned p=0;p<hud->count;++p) positions[p]=hud->panels[p].destination;
            fe8_native_hud_layout(hud,960,640,900,500,150);
            for(unsigned p=0;p<hud->count;++p)
                assert(memcmp(&positions[p],&hud->panels[p].destination,sizeof positions[p])==0);
        }
    }
    assert(valid>=178);
    /* The map cursor is standing on a player unit in both supplied ROMs. */
    assert(core->loadState(core,checkpoint));
    run(core,3,1); run(core,45,0); run(core,3,1); run(core,45,0);
    assert(fe8_extract_snapshot(&reader,profile,snapshot)); convert(video,frame);
    assert(fe8_native_hud_extract(hud,&memory,snapshot,frame,240,true));
    assert(hud->count==1&&hud->panels[0].kind==FE8_HUD_ACTION);
    fe8_native_hud_layout(hud,960,640,700,320,150);
    Fe8HudRect menu=hud->panels[0].destination;
    unsigned menu_frames=0;
    for(unsigned n=0;n<90;++n) {
        run(core,1,n<3?0x80:0); /* choose next option with native Down */
        assert(fe8_extract_snapshot(&reader,profile,snapshot)); convert(video,frame);
        if(fe8_native_hud_extract(hud,&memory,snapshot,frame,240,true)) {
            ++menu_frames; fe8_native_hud_layout(hud,960,640,100,500,150);
            assert(memcmp(&menu,&hud->panels[0].destination,sizeof menu)==0);
        }
    }
    assert(menu_frames>=88);
    run(core,3,2); run(core,45,0); run(core,3,2); run(core,60,0);
    assert(fe8_extract_snapshot(&reader,profile,snapshot)); convert(video,frame);
    assert(fe8_native_hud_extract(hud,&memory,snapshot,frame,240,true));
    assert(hud->panels[0].kind!=FE8_HUD_ACTION);
    /* Native-only/unsupported presentation must not retain an old overlay. */
    assert(!fe8_native_hud_extract(hud,&memory,snapshot,frame,240,false));
    assert(!hud->count&&!hud->menu_latched);
    printf("%s: idle %u/180, menu %u/90, cancel/reacquire and full-state read-only checks passed\n",
        profile->profile_name,valid,menu_frames);
    free(checkpoint);free(before);free(after);free(snapshot);free(hud);free(frame);
    core->deinit(core);free(video);
    mLogSetDefaultLogger(NULL);mStandardLoggerDeinit(&logger);
    return 0;
}
