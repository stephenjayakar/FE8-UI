/* Native stat-screen integration. ROMs, save states and deliberate scenario
   fixtures stay local. No synthetic skills/equipment reach a user's save. */
#include <mgba/flags.h>
#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/serialize.h>
#include <mgba/internal/gba/cart/unlicensed.h>
#include <mgba-util/vfs.h>
#include "address_space.h"
#include "inventory_effective_stats.h"
#include "inventory_desktop.h"
#include "inventory_history.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t read8(void *p,uint32_t a) { struct mCore *c=p;return c->busRead8(c,a); }
static void write8(void *p,uint32_t a,uint8_t v) { struct mCore *c=p;c->busWrite8(c,a,v); }
static void *save(struct mCore *c) { void *p=calloc(1,c->stateSize(c));assert(p && c->saveState(c,p));return p; }
static void unchanged(struct mCore *c,const void *before) {
    void *after=save(c);assert(!memcmp(before,after,c->stateSize(c)));free(after);
}
static void refresh(Fe8StatEvaluator *e,struct mCore *c,const Fe8MemoryReader *r,
    const Fe8Profile *p,const Fe8Catalog *cat,Fe8InventorySnapshot *s) {
    assert(fe8_extract_prebattle_inventory(r,p,cat,s));
    void *before=save(c);
    assert(fe8_stat_evaluator_refresh(e,c,s));unchanged(c,before);free(before);
    for(int n=0;n<s->unit_count;++n) assert(s->units[n].effective_stats_valid);
}
static void print_stats(const char *name,const Fe8InventoryUnit *u) {
    printf("%s: %s",name,u->name);
    for(int n=0;n<FE8_STAT_COUNT;++n) printf(" %d(%+d)",u->effective_stats[n],
        u->effective_stats[n]-fe8_inventory_stat_base(u,(Fe8UnitStat)n));
    puts("");fflush(stdout);
}
static void capture(const char *prefix,const char *suffix,const Fe8InventorySnapshot *s,
    int base,int board,int stat_hover) {
    if(!prefix) return;
    const int w=1280,h=800;
    uint32_t *pixels=calloc(w*h,sizeof(*pixels));assert(pixels);
    Fe8InventoryUi ui;fe8_inventory_ui_init(&ui);fe8_inventory_ui_open(&ui,s);
    ui.desktop=1;ui.desktop_scale=1;ui.stats_base=base;ui.by_unit=board;
    ui.has_detail=1;ui.detail=(Fe8InventoryEndpoint){FE8_INVENTORY_ENDPOINT_UNIT,s->units[0].address,0};
    if(stat_hover) {
        Fe8InventoryDesktopLayout l;fe8_inventory_desktop_layout(&ui,w,h,&l);
        ui.pointer_x=12+10+(l.sidebar-24)*2/4+8;ui.pointer_y=l.stats_y+8;
    }
    fe8_inventory_ui_draw(&ui,s,pixels,w,w,h);
    char path[1024];assert(snprintf(path,sizeof(path),"%s-%s.ppm",prefix,suffix)<(int)sizeof(path));
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n%d %d\n255\n",w,h);
    for(int n=0;n<w*h;++n) {unsigned char rgb[]={pixels[n],pixels[n]>>8,pixels[n]>>16};assert(fwrite(rgb,1,3,f)==3);}
    assert(!fclose(f));free(pixels);
}
static void clear_items(struct mCore *c,uint32_t unit) {
    for(int j=0;j<5;++j)c->busWrite16(c,unit+0x1E + j*2,0);
}
static void stale_caches(struct mCore *c,uint32_t unit) {
    c->busWrite32(c,0x02026BB0,unit);c->busWrite8(c,0x02026BB4,0);
    c->busWrite32(c,0x02026BC8,unit);c->busWrite8(c,0x02026BC6,0);
    c->busWrite8(c,0x02026BC7,0);
}
static enum mPlatform gba_platform(const struct mCore *core) {(void)core;return mPLATFORM_GBA;}
static void unknown_checksum(const struct mCore *core,void *data,enum mCoreChecksumType type) {
    (void)core;assert(type==mCHECKSUM_SHA1);memset(data,0,20);
}
static void safety(void) {
    /* Regression for the pinned dependency's 16-bit flags / adjacent SIO field. */
    struct GBA *gba=calloc(1,sizeof(*gba));
    struct GBASerializedState *state=calloc(1,sizeof(*state));assert(gba && state);
    gba->memory.unl.type=GBA_UNL_CART_VFAME;gba->memory.unl.vfame.sramMode=7;
    state->hw.sioNextEvent=0x11223344;
    GBAUnlCartSerialize(gba,state);assert(state->hw.sioNextEvent==0x11223344);
    gba->memory.unl.vfame.sramMode=0;GBAUnlCartDeserialize(gba,state);
    assert(gba->memory.unl.vfame.sramMode==7);free(gba);free(state);
    Fe8InventoryUnit u={0};u.speed=12;
    assert(fe8_inventory_stat_base(&u,FE8_STAT_SPEED)==12);
    assert(fe8_inventory_stat_value(&u,FE8_STAT_SPEED,false)==12);
    u.effective_stats_valid=true;u.effective_stats[FE8_STAT_SPEED]=15;
    assert(fe8_inventory_stat_value(&u,FE8_STAT_SPEED,false)==15);
    assert(fe8_inventory_stat_value(&u,FE8_STAT_SPEED,true)==12);
    assert(!fe8_inventory_stat_value(NULL,FE8_STAT_SPEED,false));
    assert(!fe8_inventory_stat_value(&u,FE8_STAT_COUNT,false));
    assert(!fe8_stat_evaluator_create(NULL));fe8_stat_evaluator_destroy(NULL);
    struct mCore unknown={0};unknown.platform=gba_platform;unknown.checksum=unknown_checksum;
    assert(!fe8_stat_evaluator_create(&unknown)); /* A family header is not sufficient. */
    Fe8InventorySnapshot *s=calloc(1,sizeof(*s));assert(s);s->unit_count=1;s->units[0]=u;
    assert(!fe8_stat_evaluator_refresh(NULL,NULL,s));assert(!s->units[0].effective_stats_valid);
    free(s);puts("Effective stat selection and unavailable-evaluator fallback passed");
}
int main(int argc,char **argv) {
    safety();if(argc==1)return 0;
    assert(argc==3 || argc==4);int arch=!strcmp(argv[2],"archanae");
    assert(arch || !strcmp(argv[2],"sacred-echoes"));
    struct mCore *c=mCoreFind(argv[1]);assert(c && c->init(c));mCoreInitConfig(c,"native-stat-test");
    struct mStandardLogger logger;mStandardLoggerInit(&logger);
    logger.d.filter->defaultLevels=mLOG_FATAL|mLOG_ERROR;mLogSetDefaultLogger(&logger.d);
    struct VFile *rom=VFileOpen(argv[1],O_RDONLY);assert(rom && c->loadROM(c,rom));
    mColor *video=calloc(240*160,sizeof(*video));assert(video);c->setVideoBuffer(c,video,240);c->reset(c);
    Fe8AddressSpace space;fe8_address_space_init(&space,c,read8);
    const uint32_t bases[]={0x02000000,0x03000000,0x08000000};
    for(unsigned n=0;n<sizeof(bases)/sizeof(bases[0]);++n) {
        size_t size=0;void *memory=mCoreGetMemoryBlock(c,bases[n],&size);
        assert(memory && fe8_address_space_add(&space,bases[n],memory,size));
    }
    Fe8MemoryReader r={&space,fe8_address_space_read8};Fe8MemoryWriter writer={c,write8};
    const Fe8Profile *p=fe8_profile_for_rom(&r);Fe8Catalog cat;assert(fe8_catalog_init(&r,p,&cat));
    assert(!strcmp(p->profile_name,arch?"Fire Emblem: Archanae":"Sacred Echoes"));
    Fe8InventorySnapshot *s=calloc(1,sizeof(*s));assert(s);
    int frame;
    for(frame=1;frame<40000;++frame) {
        int phase=frame%360;c->setKeys(c,phase<3?1:phase>=180&&phase<183?8:0);c->runFrame(c);
        if(frame%60==0 && fe8_extract_prebattle_inventory(&r,p,&cat,s) && s->unit_count>=2) break;
    }
    assert(frame<40000);c->setKeys(c,0);
    size_t ram_size=0,iram_size=0;
    void *ram=mCoreGetMemoryBlock(c,0x02000000,&ram_size),*iram=mCoreGetMemoryBlock(c,0x03000000,&iram_size);
    void *ram_before=malloc(ram_size),*iram_before=malloc(iram_size);assert(ram_before && iram_before);
    memcpy(ram_before,ram,ram_size);memcpy(iram_before,iram,iram_size);
    void *original=save(c);Fe8StatEvaluator *e=fe8_stat_evaluator_create(c);assert(e);unchanged(c,original);
    refresh(e,c,&r,p,&cat,s);uint32_t unit=s->units[0].address;
    for(int n=0;n<s->unit_count;++n)print_stats("Real roster",&s->units[n]);
    capture(argc==4?argv[3]:NULL,"totals",s,0,1,0);
    capture(argc==4?argv[3]:NULL,"base",s,1,1,0);
    capture(argc==4?argv[3]:NULL,"modifier",s,0,0,1);
    if(arch) {
        assert(s->units[0].speed==12 && s->units[0].effective_stats[FE8_STAT_SPEED]==15);
        /* Transfer the actual equipped Slim Lance to supply, then undo. */
        int slot=-1;for(int j=0;j<5;++j)if((s->units[0].items[j]&255)==0x15)slot=j;
        assert(slot>=0 && s->first_empty_supply<s->supply_capacity);
        Fe8InventoryEndpoint from={FE8_INVENTORY_ENDPOINT_UNIT,unit,(unsigned)slot};
        Fe8InventoryEndpoint to={FE8_INVENTORY_ENDPOINT_SUPPLY,s->supply_address,s->first_empty_supply};
        uint16_t item=s->units[0].items[slot];
        Fe8InventoryHistory history={0};
        assert(fe8_inventory_history_transfer(&history,&r,&writer,p,from,item,to,0));
        refresh(e,c,&r,p,&cat,s);assert(s->units[0].effective_stats[FE8_STAT_SPEED]==12);
        assert(fe8_inventory_history_undo(&history,&r,&writer,p));
        refresh(e,c,&r,p,&cat,s);assert(s->units[0].effective_stats[FE8_STAT_SPEED]==15);
        unchanged(c,original);puts("Actual Slim Lance +3 Speed removal and undo passed");
    }
    /* Isolated equipment fixtures use actual catalog entries, not ROM patches.
       Full live state is restored before returning to the initial game. */
    clear_items(c,unit);refresh(e,c,&r,p,&cat,s);Fe8InventoryUnit bare=s->units[0];
    if(arch) {
        c->busWrite16(c,unit+0x1E,0x015C); /* Energy Ring is consumed, not passive. */
        refresh(e,c,&r,p,&cat,s);assert(s->units[0].effective_stats[FE8_STAT_POWER]==bare.effective_stats[FE8_STAT_POWER]);
        c->busWrite16(c,unit+0x20,0x0181); /* Fire Emblem carried behind a nonweapon. */
        stale_caches(c,unit);refresh(e,c,&r,p,&cat,s);print_stats("Passive Fire Emblem",&s->units[0]);
        assert(s->units[0].effective_stats[FE8_STAT_LUCK]==bare.effective_stats[FE8_STAT_LUCK]+10);
        clear_items(c,unit);
        uint32_t learned=0x0203E884+s->units[0].character_id*16+1;
        for(int j=0;j<4;++j)c->busWrite8(c,learned+j,0);
        stale_caches(c,unit);refresh(e,c,&r,p,&cat,s);Fe8InventoryUnit without_skill=s->units[0];
        c->busWrite8(c,learned,0x8C); /* This ROM's actual Fury skill. */
        stale_caches(c,unit);refresh(e,c,&r,p,&cat,s);print_stats("Learned Fury",&s->units[0]);
        assert(s->units[0].effective_stats[FE8_STAT_POWER]==without_skill.effective_stats[FE8_STAT_POWER]+2);
        assert(s->units[0].effective_stats[FE8_STAT_SPEED]==without_skill.effective_stats[FE8_STAT_SPEED]+2);
        assert(s->units[0].effective_stats[FE8_STAT_DEFENSE]==without_skill.effective_stats[FE8_STAT_DEFENSE]+2);
        c->busWrite8(c,learned,0);stale_caches(c,unit);refresh(e,c,&r,p,&cat,s);
        assert(!memcmp(s->units[0].effective_stats,without_skill.effective_stats,sizeof(without_skill.effective_stats)));
        /* Rescuing is a stat-screen penalty, not an enemy-specific forecast. */
        uint32_t state=c->busRead32(c,unit+0x0C);c->busWrite32(c,unit+0x0C,state|0x10);
        refresh(e,c,&r,p,&cat,s);print_stats("Rescuing",&s->units[0]);
        assert(s->units[0].effective_stats[FE8_STAT_SPEED]==without_skill.effective_stats[FE8_STAT_SPEED]/2);
    } else {
        c->busWrite16(c,unit+0x1E,0x0065); /* Iron Shield */
        refresh(e,c,&r,p,&cat,s);print_stats("Iron Shield",&s->units[0]);
        assert(s->units[0].effective_stats[FE8_STAT_DEFENSE]==bare.effective_stats[FE8_STAT_DEFENSE]+4);
        assert(s->units[0].effective_stats[FE8_STAT_SPEED]==bare.effective_stats[FE8_STAT_SPEED]-1);
        capture(argc==4?argv[3]:NULL,"shield-fixture",s,0,0,1);
        c->busWrite16(c,unit+0x1E,0x007E); /* Speed Ring: +10 speed, +1 movement */
        refresh(e,c,&r,p,&cat,s);print_stats("Speed Ring",&s->units[0]);
        assert(s->units[0].effective_stats[FE8_STAT_SPEED]==bare.effective_stats[FE8_STAT_SPEED]+10);
        assert(s->units[0].effective_stats[FE8_STAT_MOVEMENT]==bare.effective_stats[FE8_STAT_MOVEMENT]+1);
    }
    assert(c->loadState(c,original));
    assert(!memcmp(ram_before,ram,ram_size) && !memcmp(iram_before,iram,iram_size));
    free(ram_before);free(iram_before);
    /* mGBA normalizes timing fields on loadState; assert isolation against the
       state after our explicit fixture rollback, not against pre-load timing. */
    free(original);original=save(c);
    refresh(e,c,&r,p,&cat,s);unchanged(c,original);
    s->units[0].items[0]^=0x0100;assert(!fe8_stat_evaluator_refresh(e,c,s));
    assert(!s->units[0].effective_stats_valid);unchanged(c,original);
    s->units[0].address=0x02000000;assert(!fe8_stat_evaluator_refresh(e,c,s));
    unchanged(c,original);
    fe8_stat_evaluator_destroy(e);unchanged(c,original);
    free(original);free(s);mCoreConfigDeinit(&c->config);c->deinit(c);free(video);
    mLogSetDefaultLogger(NULL);mStandardLoggerDeinit(&logger);
    puts("Native equipment, passive items, skills, penalties, recalculation and complete live-state isolation passed");
    return 0;
}
