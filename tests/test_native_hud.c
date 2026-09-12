#include "native_hud.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t io[1024], vram[0x18000], palette[1024], oam[1024];
static Fe8HostPixel frame[240 * 160], original[240 * 160];
static Fe8NativeHud hud;
static Fe8Snapshot snapshot;
static uint8_t read8(void *context, uint32_t address) {
    (void)context;
    if (address >= 0x04000000 && address < 0x04000400) return io[address-0x04000000];
    if (address >= 0x05000000 && address < 0x05000400) return palette[address-0x05000000];
    if (address >= 0x06000000 && address < 0x06018000) return vram[address-0x06000000];
    if (address >= 0x07000000 && address < 0x07000400) return oam[address-0x07000000];
    assert(!"read outside PPU memory"); return 0;
}
static const Fe8MemoryView memory = {NULL, read8};
static void put16(uint8_t *bytes, unsigned offset, unsigned value) {
    bytes[offset] = value; bytes[offset+1] = value >> 8;
}
static void fixture(void) {
    memset(io,0,sizeof io); memset(vram,0,sizeof vram);
    memset(palette,0,sizeof palette); memset(oam,0,sizeof oam);
    memset(&hud,0,sizeof hud); memset(&snapshot,0,sizeof snapshot);
    put16(io,0,0x1F00);
    for (unsigned bg=0;bg<4;++bg) put16(io,8+bg*2,((12+bg)<<8)|bg|(bg==3?8:0));
    for (unsigned n=0;n<128;++n) put16(oam,n*8,0x0200);
    put16(palette,0,0x001F);                 /* red world backdrop */
    put16(palette,(16+1)*2,0x7C00);          /* blue terrain panel */
    memset(vram+32,0x11,32);                /* opaque 4bpp tile */
    for (unsigned y=14;y<20;++y) for (unsigned x=23;x<30;++x)
        put16(vram,0x6800+(y*32+x)*2,0x1001);
    for (int y=0;y<160;++y) for (int x=0;x<240;++x)
        frame[y*240+x] = x>=184&&y>=112 ? 0xFFFF0000 : 0xFF0000FF;
    memcpy(original,frame,sizeof frame);
}
static bool extract(bool live) {
    return fe8_native_hud_extract(&hud,&memory,&snapshot,frame,240,live);
}
static void expect_fallback(void) {
    assert(!extract(true)); assert(hud.count==0); assert(!hud.menu_latched);
    assert(memcmp(frame,original,sizeof frame)==0);
}
static void extraction_and_fallback(void) {
    fixture(); assert(extract(true)); assert(hud.count==1);
    assert(hud.panels[0].kind==FE8_HUD_TERRAIN);
    for (unsigned p=0;p<240*160;++p) assert(hud.world[p]==0xFF0000FF);
    assert(hud.atlas[112*240+184]==0xFFFF0000); assert(hud.atlas[0]==0);
    assert(memcmp(frame,original,sizeof frame)==0);
    assert(!extract(false)); assert(!hud.count);
    fixture(); snapshot.combat_panel_active=1; expect_fallback();
    fixture(); snapshot.phase=0x80; expect_fallback();
    fixture(); snapshot.input_lock=2; expect_fallback();
    fixture(); snapshot.game_state_bits=2; expect_fallback();
    fixture(); put16(io,0,0x3F00); expect_fallback(); /* window effect */
    fixture(); put16(io,0x10,1); expect_fallback(); /* sliding native panel */
    fixture(); put16(io,0xA,0x0D81); expect_fallback(); /* 8bpp BG */
    fixture(); put16(io,0x50,0x80); expect_fallback(); /* brightness effect */
    fixture(); put16(vram,0x6800+(14*32+23)*2,0); expect_fallback(); /* partial window */
    fixture(); put16(vram,0x6800+(3*32+3)*2,0x1001); expect_fallback(); /* unknown UI */
    fixture(); put16(vram,0x6000+2,1); frame[8]=0xFFFF0000;
    memcpy(original,frame,sizeof frame); expect_fallback(); /* UI outside recognized panel */
    fixture(); memset(frame,0xCC,sizeof frame); memcpy(original,frame,sizeof frame);
    expect_fallback(); /* stale palette/VRAM must not strip a mismatched frame */
    fixture(); put16(oam,0,0x0100|112); put16(oam,2,184); expect_fallback(); /* affine OBJ */
}
static void alpha_and_world_sprites(void) {
    fixture();
    put16(io,0x50,0x3C42); put16(io,0x52,0x030D);
    for (int y=112;y<160;++y) for (int x=184;x<240;++x) frame[y*240+x]=0xFFCF002F;
    assert(extract(true));
    uint32_t recomposed=fe8_native_hud_over(hud.atlas[112*240+184],hud.world[112*240+184]);
    assert(((recomposed>>16)&255)>=206 && ((recomposed>>16)&255)<=208);
    assert((recomposed&255)>=47 && (recomposed&255)<=49);
    assert(fe8_native_hud_over(0,0xFF123456)==0xFF123456);
    assert(fe8_native_hud_over(0xFF987654,0)==0xFF987654);
    fixture();
    /* A unit hidden by the original panel must be revealed, not erased. */
    memset(vram+0x10000+32,0x11,32); put16(palette,(256+1)*2,0x03E0);
    put16(oam,0,120); put16(oam,2,192); put16(oam,4,(2<<10)|1);
    assert(extract(true)); assert(hud.world[120*240+192]==0xFF00FF00);
    /* Known map-cursor corners stay in world space; the panel underneath is
     * reconstructed too, avoiding holes after it moves to the host corner. */
    memset(vram+0x10000+64,0x11,32); put16(oam,4,2);
    for (int y=120;y<128;++y) for(int x=192;x<200;++x) frame[y*240+x]=0xFF00FF00;
    assert(extract(true)); assert(hud.world[120*240+192]==0xFF00FF00);
    assert(hud.atlas[120*240+192]==0xFFFF0000);
    /* Foreground cursor OBJ is a blend target, but the backdrop underneath
     * the panel is not. It must not make the recovered window translucent. */
    put16(io,0x50,0x1C42); put16(io,0x52,0x030D);
    assert(extract(true)); assert(hud.atlas[120*240+192]==0xFFFF0000);
    /* Unknown foreground sprite in a panel: preserve canonical presentation. */
    put16(oam,4,3); memcpy(original,frame,sizeof frame); expect_fallback();
}
/* Opening/closing a native unit screen changes BLDCNT from 0x3C42 to
 * 0x1C42 in both supplied ROMs: the backdrop stops being a second target,
 * while terrain and units still blend. Check every world-target combination,
 * including different lower layers within the same detached panel. */
static void blend_target_variants(void) {
    for (unsigned targets = 0; targets < 16; ++targets) {
        fixture();
        put16(io, 0x50, 0x0042 | (targets << 10));
        put16(io, 0x52, 0x030D);
        put16(palette, 2, 0x03E0); /* BG3 green */
        put16(palette, 4, 0x03FF); /* BG2 yellow */
        put16(palette, (256 + 1) * 2, 0x7C1F); /* OBJ magenta */
        memset(vram + 0x8000 + 32, 0x11, 32);
        memset(vram + 64, 0x22, 32);
        memset(vram + 0x10000 + 32, 0x11, 32);
        for (unsigned y = 14; y < 20; ++y) {
            for (unsigned x = 23; x < 26; ++x)
                put16(vram, 0x7800 + (y * 32 + x) * 2, 1);
            put16(vram, 0x7000 + (y * 32 + 26) * 2, 2);
        }
        put16(oam, 0, 120); put16(oam, 2, 216); put16(oam, 4, (2 << 10) | 1);
        for (int y = 112; y < 160; ++y) for (int x = 184; x < 240; ++x) {
            unsigned layer = x < 208 ? 3 : x < 216 ? 2 :
                x < 224 && y >= 120 && y < 128 ? 4 : 5;
            uint32_t world = layer == 3 ? 0xFF00FF00 : layer == 2 ? 0xFF00FFFF :
                layer == 4 ? 0xFFFF00FF : 0xFF0000FF;
            uint32_t panel = 0xFFFF0000;
            if (targets & (1u << (layer - 2))) {
                panel = 0xFF000000;
                for (unsigned shift = 0; shift < 24; shift += 8)
                    panel |= (((((0xFFFF0000u >> shift) & 255) * 13) +
                        ((world >> shift) & 255) * 3) >> 4) << shift;
            }
            frame[y * 240 + x] = panel;
        }
        memcpy(original, frame, sizeof frame);
        assert(extract(true));
        assert(memcmp(frame, original, sizeof frame) == 0);
        for (int y = 112; y < 160; ++y) for (int x = 184; x < 240; ++x) {
            unsigned layer = x < 208 ? 3 : x < 216 ? 2 :
                x < 224 && y >= 120 && y < 128 ? 4 : 5;
            unsigned pos = y * 240 + x;
            assert((hud.atlas[pos] >> 24) ==
                (targets & (1u << (layer - 2)) ? 207u : 255u));
            uint32_t actual = fe8_native_hud_over(hud.atlas[pos], hud.world[pos]);
            for (unsigned shift = 0; shift < 24; shift += 8) {
                int delta = (int)((actual >> shift) & 255) -
                    (int)((frame[pos] >> shift) & 255);
                assert(delta >= -1 && delta <= 1);
            }
        }
    }
}
static void layout(void) {
    fixture(); hud.count=3;
    hud.panels[0]=(Fe8HudPanel){FE8_HUD_UNIT,{0,0,144,48},{0}};
    hud.panels[1]=(Fe8HudPanel){FE8_HUD_OBJECTIVE,{152,0,88,32},{0}};
    hud.panels[2]=(Fe8HudPanel){FE8_HUD_TERRAIN,{184,112,56,48},{0}};
    fe8_native_hud_layout(&hud,960,640,100,100,150);
    Fe8HudRect positions[3];
    for(int p=0;p<3;++p) positions[p]=hud.panels[p].destination;
    assert(positions[0].width==432); assert(positions[0].x==12);
    fe8_native_hud_layout(&hud,960,640,900,500,150); /* moving cursor does not move HUD */
    for(int p=0;p<3;++p) assert(memcmp(&positions[p],&hud.panels[p].destination,sizeof positions[p])==0);
    assert(fe8_native_hud_hit_test(&hud,20,20)); assert(!fe8_native_hud_hit_test(&hud,500,350));
    const int sizes[][2]={{320,240},{641,481},{1280,720},{1920,1280},{100,80}};
    for(unsigned n=0;n<sizeof sizes/sizeof sizes[0];++n) {
        int w=sizes[n][0],h=sizes[n][1]; fe8_native_hud_layout(&hud,w,h,10,10,200);
        for(int p=0;p<3;++p) {
            Fe8HudRect d=hud.panels[p].destination;
            assert(d.x>=0&&d.y>=0&&d.x+d.width<=w&&d.y+d.height<=h);
        }
        assert(hud.panels[0].destination.x+hud.panels[0].destination.width<=hud.panels[1].destination.x);
    }
    hud.count=1; hud.panels[0]=(Fe8HudPanel){FE8_HUD_ACTION,{0,24,80,64},{0}};
    fe8_native_hud_layout(&hud,960,640,900,20,150);
    Fe8HudRect menu=hud.panels[0].destination;
    fe8_native_hud_layout(&hud,960,640,0,600,150);
    assert(memcmp(&menu,&hud.panels[0].destination,sizeof menu)==0);
    fe8_native_hud_reset(&hud); assert(!hud.menu_latched && !hud.count);
    /* Drawing clips negative destinations and honors a padded stride. */
    Fe8HostPixel guarded[8*10]; for(unsigned n=0;n<80;++n) guarded[n]=0xDEADBEEF;
    hud.count=1; hud.panels[0]=(Fe8HudPanel){FE8_HUD_TERRAIN,{0,0,2,2},{-1,-1,4,4}};
    hud.atlas[0]=hud.atlas[1]=hud.atlas[240]=hud.atlas[241]=0xFFFFFFFF;
    fe8_native_hud_draw(&hud,guarded+10,10,8,6);
    for(int x=0;x<10;++x) {assert(guarded[x]==0xDEADBEEF);assert(guarded[70+x]==0xDEADBEEF);}
    for(int y=1;y<7;++y) assert(guarded[y*10+8]==0xDEADBEEF&&guarded[y*10+9]==0xDEADBEEF);
    assert(guarded[10]==0xFFFFFFFF); assert(guarded[10+7]==0);
}
int main(void) {
    extraction_and_fallback(); alpha_and_world_sprites(); blend_target_variants(); layout();
    puts("native HUD extraction, fallback, sprite preservation, alpha, layout and clipping passed");
    return 0;
}
