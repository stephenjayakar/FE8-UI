from pathlib import Path


def read(path):
    return Path(path).read_text()


def write(path, text):
    Path(path).write_text(text)


def once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one match, got {count}")
    return text.replace(old, new, 1)


def between(text, start, end, replacement, label):
    i = text.find(start)
    if i < 0:
        raise SystemExit(f"{label}: start marker not found")
    j = text.find(end, i)
    if j < 0:
        raise SystemExit(f"{label}: end marker not found")
    return text[:i] + replacement + text[j:]


# Snapshot three authentic standing-SMS animation frames when FE8's backing
# SMS buffers can be validated against the live OBJ frame.
path = "app/src/prebattle_inventory.h"
t = read(path)
t = once(
    t,
    "    FE8_MAP_SPRITE_MAX_WIDTH = 32,\n    FE8_MAP_SPRITE_MAX_HEIGHT = 32,\n    FE8_MAP_SPRITE_PALETTE_SIZE = 16,\n",
    "    FE8_MAP_SPRITE_MAX_WIDTH = 32,\n    FE8_MAP_SPRITE_MAX_HEIGHT = 32,\n    FE8_MAP_SPRITE_FRAME_COUNT = 3,\n    FE8_MAP_SPRITE_PALETTE_SIZE = 16,\n",
    "map sprite enum",
)
t = once(
    t,
    "    uint8_t map_sprite[FE8_MAP_SPRITE_MAX_WIDTH * FE8_MAP_SPRITE_MAX_HEIGHT];\n",
    "    uint8_t map_sprite[FE8_MAP_SPRITE_FRAME_COUNT]\n        [FE8_MAP_SPRITE_MAX_WIDTH * FE8_MAP_SPRITE_MAX_HEIGHT];\n",
    "map sprite storage",
)
write(path, t)

path = "app/src/prebattle_inventory.c"
t = read(path)
t = once(
    t,
    "#define FE8_OBJ_TILE_BYTES UINT32_C(32)\n#define FE8_OBJ_TILE_ROW_STRIDE UINT32_C(32)\n",
    "#define FE8_OBJ_TILE_BYTES UINT32_C(32)\n#define FE8_OBJ_TILE_ROW_STRIDE UINT32_C(32)\n#define FE8_OBJ_VRAM_BYTES UINT32_C(0x8000)\n#define FE8_SMS_GFX_TILE_BASE UINT32_C(0x80)\n#define FE8_SMS_GFX_FRAME_BYTES UINT32_C(0x2000)\n#define FE8_SMS_GFX_COUNTER_BYTES UINT32_C(8)\n",
    "sprite sheet constants",
)
replacement = r'''static bool decode_map_sprite(const Fe8MemoryReader *memory,
    uint32_t sheet_address, unsigned first_tile, unsigned tile_count,
    unsigned tile_base, unsigned width, unsigned height,
    uint8_t pixels[FE8_MAP_SPRITE_MAX_WIDTH * FE8_MAP_SPRITE_MAX_HEIGHT],
    bool *visible) {
    bool any = false;
    memset(pixels, 0,
        FE8_MAP_SPRITE_MAX_WIDTH * FE8_MAP_SPRITE_MAX_HEIGHT * sizeof(*pixels));
    for (unsigned y = 0; y < height; ++y) {
        unsigned tile_y = y >> 3;
        unsigned pixel_y = y & 7;
        for (unsigned x = 0; x < width; ++x) {
            unsigned tile_x = x >> 3;
            unsigned tile = (tile_base + tile_y * FE8_OBJ_TILE_ROW_STRIDE + tile_x) & 0x3FF;
            uint32_t packed_address;
            uint8_t packed;
            uint8_t color_index;
            if (tile < first_tile || tile - first_tile >= tile_count)
                return false;
            packed_address = sheet_address +
                (tile - first_tile) * FE8_OBJ_TILE_BYTES + pixel_y * 4 + (x & 7) / 2;
            packed = read8(memory, packed_address);
            color_index = (x & 1) ? packed >> 4 : packed & 0x0F;
            pixels[y * FE8_MAP_SPRITE_MAX_WIDTH + x] = color_index;
            if (color_index != 0)
                any = true;
        }
    }
    if (visible)
        *visible = any;
    return true;
}

static bool extract_map_sprite(const Fe8MemoryReader *memory,
    const Fe8Profile *profile, uint32_t unit_address, Fe8InventoryUnit *unit) {
    uint32_t handle = read32(memory, unit_address + FE8_UNIT_MAP_SPRITE_HANDLE_OFFSET);
    uint16_t oam2;
    uint8_t config;
    unsigned width;
    unsigned height;
    unsigned tile_base;
    unsigned palette_bank;
    bool live_visible = false;
    bool animation_valid = false;
    uint8_t live[FE8_MAP_SPRITE_MAX_WIDTH * FE8_MAP_SPRITE_MAX_HEIGHT];

    unit->map_sprite_width = 0;
    unit->map_sprite_height = 0;
    memset(unit->map_sprite, 0, sizeof(unit->map_sprite));
    memset(unit->map_sprite_palette, 0, sizeof(unit->map_sprite_palette));
    if (!valid_range(handle, 12, FE8_EWRAM_START, FE8_EWRAM_END))
        return false;

    oam2 = read16(memory, handle + 8);
    config = read8(memory, handle + 0x0B);
    if ((config & 0x80) != 0)
        return false;
    switch (config & 0x0F) {
    case 0:
    case 3:
        width = 16;
        height = 16;
        break;
    case 1:
    case 4:
        width = 16;
        height = 32;
        break;
    case 2:
    case 5:
        width = 32;
        height = 32;
        break;
    default:
        return false;
    }

    tile_base = oam2 & 0x3FF;
    palette_bank = (oam2 >> 12) & 0x0F;
    unit->map_sprite_width = (uint8_t)width;
    unit->map_sprite_height = (uint8_t)height;
    for (unsigned index = 1; index < FE8_MAP_SPRITE_PALETTE_SIZE; ++index) {
        unit->map_sprite_palette[index] = gba_color(read16(memory,
            FE8_OBJ_PALETTE + (palette_bank * 16 + index) * 2));
    }
    if (!decode_map_sprite(memory, FE8_OBJ_VRAM, 0,
            FE8_OBJ_VRAM_BYTES / FE8_OBJ_TILE_BYTES, tile_base, width, height,
            live, &live_visible) || !live_visible)
        return false;

    /* FE8 keeps three standing-map-sprite sheets immediately before the SMS
       counters/handle array and swaps them into OBJ VRAM on a 72-frame cycle.
       Hacks can repoint that RAM, so derive it from the profile and only trust
       it after one of its frames exactly matches the live sprite we can see. */
    if (profile && profile->sms_handle_array >= FE8_EWRAM_START +
            FE8_MAP_SPRITE_FRAME_COUNT * FE8_SMS_GFX_FRAME_BYTES +
            FE8_SMS_GFX_COUNTER_BYTES) {
        uint32_t sheet = profile->sms_handle_array - FE8_SMS_GFX_COUNTER_BYTES -
            FE8_MAP_SPRITE_FRAME_COUNT * FE8_SMS_GFX_FRAME_BYTES;
        if (valid_range(sheet,
                FE8_MAP_SPRITE_FRAME_COUNT * FE8_SMS_GFX_FRAME_BYTES,
                FE8_EWRAM_START, FE8_EWRAM_END)) {
            bool all_frames_valid = true;
            bool live_match = false;
            for (unsigned frame = 0; frame < FE8_MAP_SPRITE_FRAME_COUNT; ++frame) {
                bool frame_visible = false;
                if (!decode_map_sprite(memory,
                        sheet + frame * FE8_SMS_GFX_FRAME_BYTES,
                        FE8_SMS_GFX_TILE_BASE,
                        FE8_SMS_GFX_FRAME_BYTES / FE8_OBJ_TILE_BYTES,
                        tile_base, width, height, unit->map_sprite[frame],
                        &frame_visible) || !frame_visible) {
                    all_frames_valid = false;
                    break;
                }
                if (memcmp(unit->map_sprite[frame], live, sizeof(live)) == 0)
                    live_match = true;
            }
            animation_valid = all_frames_valid && live_match;
        }
    }
    if (!animation_valid) {
        for (unsigned frame = 0; frame < FE8_MAP_SPRITE_FRAME_COUNT; ++frame)
            memcpy(unit->map_sprite[frame], live, sizeof(live));
    }
    return true;
}

static void copy_map_sprite(Fe8InventoryUnit *target,
    const Fe8InventoryUnit *source) {
    memcpy(target->map_sprite, source->map_sprite, sizeof(target->map_sprite));
    memcpy(target->map_sprite_palette, source->map_sprite_palette,
        sizeof(target->map_sprite_palette));
    target->map_sprite_width = source->map_sprite_width;
    target->map_sprite_height = source->map_sprite_height;
    target->map_sprite_valid = source->map_sprite_valid;
}

'''
t = between(
    t,
    "static bool extract_map_sprite(",
    "static uint8_t clamp_stat",
    replacement,
    "map sprite extraction",
)
t = once(
    t,
    "unit->map_sprite_valid = extract_map_sprite(memory, address, unit);",
    "unit->map_sprite_valid = extract_map_sprite(memory, profile, address, unit);",
    "map sprite call",
)
write(path, t)

# A host-side frame tick keeps the sprite moving while emulation is paused
# behind the inventory workspace.
path = "app/src/prebattle_inventory_ui.h"
t = read(path)
t = once(
    t,
    "    int pointer_x, pointer_y; /* Drawable pixels, converted by shared layout. */\n",
    "    int pointer_x, pointer_y; /* Drawable pixels, converted by shared layout. */\n    unsigned animation_frame; /* Host UI tick; advances while game emulation is paused. */\n",
    "UI animation tick",
)
write(path, t)

path = "app/src/prebattle_inventory_ui.c"
t = read(path)
t = once(
    t,
    "    ui->flash_ticks = 0;\n    ui->loadout_scroll = ui->supply_scroll = 0;\n",
    "    ui->flash_ticks = 0;\n    ui->animation_frame = 0;\n    ui->loadout_scroll = ui->supply_scroll = 0;\n",
    "reset animation tick",
)
write(path, t)

path = "app/src/main.c"
t = read(path)
t = once(
    t,
    "            fe8_inventory_ui_draw(&inventory_ui, &inventory_snapshot,\n                canvas, canvas_width, canvas_width, canvas_height);\n            if (inventory_ui.flash_ticks>0) --inventory_ui.flash_ticks;\n",
    "            fe8_inventory_ui_draw(&inventory_ui, &inventory_snapshot,\n                canvas, canvas_width, canvas_width, canvas_height);\n            ++inventory_ui.animation_frame;\n            if (inventory_ui.flash_ticks>0) --inventory_ui.flash_ticks;\n",
    "advance inventory animation",
)
write(path, t)

path = "app/src/inventory_desktop.c"
t = read(path)
t = once(
    t,
    "static int px(const Painter *p, int v) { return (int)(v * p->scale + 0.5f); }\n",
    "static int px(const Painter *p, int v) { return (int)(v * p->scale + 0.5f); }\nstatic int pointer_over(const Painter *p, const Fe8InventoryUi *ui,\n    int x, int y, int w, int h) {\n    if (!ui || ui->pointer_x < 0 || ui->pointer_y < 0 || w <= 0 || h <= 0) return 0;\n    return ui->pointer_x >= px(p,x) && ui->pointer_x < px(p,x+w) &&\n        ui->pointer_y >= px(p,y) && ui->pointer_y < px(p,y+h);\n}\n",
    "pointer hover helper",
)
replacement = r'''static unsigned map_sprite_frame(const Fe8InventoryUi *ui) {
    unsigned clock = ui ? ui->animation_frame % 72u : 0;
    if (clock >= 68) return 1;
    if (clock >= 36) return 2;
    if (clock >= 32) return 1;
    return 0;
}
static void map_sprite(Painter *p,const Fe8InventoryUi *ui,
    const Fe8InventoryUnit *u,int x,int y,int w,int h) {
    if(!u||!u->map_sprite_valid||!u->map_sprite_width||!u->map_sprite_height)return;
    card(p,x,y,w,h,RAISED);
    int inner_w=w-8,inner_h=h-8;
    if(inner_w<=0||inner_h<=0)return;
    /* All classes get the same badge footprint. Preserve FE8's native
       16x16/16x32/32x32 proportions instead of stretching tall sprites. */
    int draw_w=inner_w,draw_h=inner_h;
    if(draw_w*u->map_sprite_height>draw_h*u->map_sprite_width)
        draw_w=draw_h*u->map_sprite_width/u->map_sprite_height;
    else draw_h=draw_w*u->map_sprite_height/u->map_sprite_width;
    int left=x+(w-draw_w)/2,top=y+(h-draw_h)/2;
    int x0=px(p,left),y0=px(p,top),ww=px(p,left+draw_w)-x0,hh=px(p,top+draw_h)-y0;
    unsigned frame=map_sprite_frame(ui);
    if(ww<=0||hh<=0)return;
    for(int yy=0;yy<hh;++yy)for(int xx=0;xx<ww;++xx) {
        int dx=x0+xx,dy=y0+yy;
        if(dx<0||dy<0||dx>=p->width||dy>=p->height)continue;
        unsigned sx=(unsigned)xx*u->map_sprite_width/(unsigned)ww;
        unsigned sy=(unsigned)yy*u->map_sprite_height/(unsigned)hh;
        unsigned idx=u->map_sprite[frame][sy*FE8_MAP_SPRITE_MAX_WIDTH+sx];
        if(idx&&idx<FE8_MAP_SPRITE_PALETTE_SIZE)
            p->pixels[dy*p->stride+dx]=u->map_sprite_palette[idx];
    }
}
'''
t = between(
    t,
    "static void map_sprite(",
    "static void scroll_mark",
    replacement,
    "animated map sprite renderer",
)

# Sidebar: normal portrait at rest; enlarged portrait + animated map sprite only
# while the pointer is over the portrait.
t = once(
    t,
    '''        int art_w=short_art?82:108,art_h=short_art?68:96;
        int sprite_size=short_art?32:46;
        int info_x=PAD+art_w+20;
        int info_w=PAD+l->sidebar-info_x-8;
        int ranks_y=l->top+(short_art?82:104);
        portrait(p,u,PAD+8,l->top+6,art_w,art_h);
        /* The map sprite reads like a physical game-piece badge while keeping
           the portrait itself large and the identity text in a stable column. */
        map_sprite(p,u,PAD+12+art_w-sprite_size,l->top+art_h-sprite_size+2,
            sprite_size,sprite_size);
''',
    '''        int art_w=short_art?82:108,art_h=short_art?68:96;
        int art_x=PAD+8,art_y=l->top+6;
        int portrait_hover=pointer_over(p,ui,art_x,art_y,art_w,art_h);
        int info_x=PAD+art_w+20;
        int info_w=PAD+l->sidebar-info_x-8;
        int ranks_y=l->top+(short_art?82:104);
        portrait(p,u,art_x,art_y,art_w,art_h);
''',
    "sidebar base portrait",
)
t = once(
    t,
    '''        if(x==PAD+10)label(p,x,ranks_y+2,l->sidebar-20,16,"No weapon ranks",MUTED,11,0,0);
        if(l->height>=720)label(p,PAD+10,l->stats_y-18,l->sidebar-20,16,ui->stats_base || !u->effective_stats_valid?"BASE STATS":"TOTAL STATS · Hover for modifiers",MUTED,10,1,0);
''',
    '''        if(x==PAD+10)label(p,x,ranks_y+2,l->sidebar-20,16,"No weapon ranks",MUTED,11,0,0);
        if(portrait_hover) {
            int hover_w=short_art?100:132,hover_h=short_art?84:116;
            int hover_x=PAD+6,hover_y=l->top+3;
            int sprite_size=short_art?34:44;
            portrait(p,u,hover_x,hover_y,hover_w,hover_h);
            border(p,hover_x,hover_y,hover_w,hover_h,ACCENT);
            map_sprite(p,ui,u,hover_x+hover_w-sprite_size+3,
                hover_y+hover_h-sprite_size+3,sprite_size,sprite_size);
        }
        if(l->height>=720)label(p,PAD+10,l->stats_y-18,l->sidebar-20,16,ui->stats_base || !u->effective_stats_valid?"BASE STATS":"TOTAL STATS · Hover for modifiers",MUTED,10,1,0);
''',
    "sidebar hover overlay",
)

# By-unit rows get the same interaction without widening the loadout board or
# permanently consuming more identity space.
t = once(
    t,
    '''    int compact=l->identity_width<140;
    if(!compact) {
        portrait(p,u,l->board_x+9,y+7,48,44);
        map_sprite(p,u,l->board_x+31,y+27,28,28);
    }
''',
    '''    int compact=l->identity_width<140;
    int portrait_hover=0;
    if(!compact) {
        int art_x=l->board_x+9,art_y=y+7;
        portrait(p,u,art_x,art_y,48,44);
        portrait_hover=pointer_over(p,ui,art_x,art_y,48,44);
    }
''',
    "board base portrait",
)
t = once(
    t,
    '''    if(l->board_card_height>=80) {
        snprintf(b,sizeof(b),"%d / 5 items",occupied(u));
        label(p,l->board_x+10,y+65,l->identity_width-20,14,b,MUTED,9,0,0);
    }
    int stats_y=y+l->board_card_height,stats_h=l->board_row_height-l->board_card_height;
''',
    '''    if(l->board_card_height>=80) {
        snprintf(b,sizeof(b),"%d / 5 items",occupied(u));
        label(p,l->board_x+10,y+65,l->identity_width-20,14,b,MUTED,9,0,0);
    }
    if(portrait_hover) {
        int hover_w=l->board_card_height<80?58:72;
        int hover_h=l->board_card_height<80?52:66;
        int hover_x=l->board_x+7,hover_y=y+4;
        int sprite_size=l->board_card_height<80?28:34;
        portrait(p,u,hover_x,hover_y,hover_w,hover_h);
        border(p,hover_x,hover_y,hover_w,hover_h,ACCENT);
        map_sprite(p,ui,u,hover_x+hover_w-sprite_size+2,
            hover_y+hover_h-sprite_size+2,sprite_size,sprite_size);
    }
    int stats_y=y+l->board_card_height,stats_h=l->board_row_height-l->board_card_height;
''',
    "board hover overlay",
)
write(path, t)

# Synthetic coverage proves all three frames are captured from the backing SMS
# sheets (with the live VRAM frame used as validation), and that reserve
# same-class fallback keeps the full animation.
path = "tests/test_prebattle_inventory.c"
t = read(path)
t = once(
    t,
    '''    /* A live 16x16 SMSHandle for Alice: tile 0x80, blue OBJ palette 12. */
    put32(first + 0x3C, 0x02020000);
    put16(0x02020000 + 8, (uint16_t)(0xC000 | 0x0080));
    write8(NULL, 0x02020000 + 0x0B, 0);
    vram[0x10000 + 0x80 * 32] = 0x11;
    palette[0x200 + (12 * 16 + 1) * 2] = 0x1F;
    palette[0x200 + (12 * 16 + 1) * 2 + 1] = 0;
''',
    '''    /* A live 16x16 SMSHandle for Alice: tile 0x80, blue OBJ palette 12.
       FE8 keeps three 0x2000-byte standing-sprite frames immediately before
       its two counters and SMSHandle array. Frame 1 matches live VRAM here. */
    put32(first + 0x3C, 0x02020000);
    put16(0x02020000 + 8, (uint16_t)(0xC000 | 0x0080));
    write8(NULL, 0x02020000 + 0x0B, 0);
    {
        uint32_t sms_gfx = profile.sms_handle_array - 8 - 3 * 0x2000;
        ewram[sms_gfx - 0x02000000] = 0x11;
        ewram[sms_gfx - 0x02000000 + 0x2000] = 0x22;
        ewram[sms_gfx - 0x02000000 + 0x4000] = 0x33;
    }
    vram[0x10000 + 0x80 * 32] = 0x22;
    palette[0x200 + (12 * 16 + 1) * 2] = 0x1F;
    palette[0x200 + (12 * 16 + 1) * 2 + 1] = 0;
''',
    "synthetic animated sprite fixture",
)
t = once(
    t,
    '''    assert(snapshot.units[0].map_sprite[0] == 1);
    assert(snapshot.units[0].map_sprite_palette[1] == UINT32_C(0xFF0000FF));
    /* Bob has no handle, but shares Alice's class and inherits the captured SMS. */
    assert(snapshot.units[1].map_sprite_valid && snapshot.units[1].map_sprite[0] == 1);
''',
    '''    assert(snapshot.units[0].map_sprite[0][0] == 1);
    assert(snapshot.units[0].map_sprite[1][0] == 2);
    assert(snapshot.units[0].map_sprite[2][0] == 3);
    assert(snapshot.units[0].map_sprite_palette[1] == UINT32_C(0xFF0000FF));
    /* Bob has no handle, but shares Alice's class and inherits all SMS frames. */
    assert(snapshot.units[1].map_sprite_valid &&
        snapshot.units[1].map_sprite[0][0] == 1 &&
        snapshot.units[1].map_sprite[1][0] == 2 &&
        snapshot.units[1].map_sprite[2][0] == 3);
''',
    "animated sprite assertions",
)
write(path, t)
