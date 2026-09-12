#include "native_hud.h"

#include <stdlib.h>
#include <string.h>

#define W FE8_HUD_WIDTH
#define H FE8_HUD_HEIGHT
#define VRAM UINT32_C(0x06000000)
#define PAL UINT32_C(0x05000000)
#define OAM UINT32_C(0x07000000)
#define IO UINT32_C(0x04000000)
#define PRESENT UINT32_C(0x80000000)
#define SEMI UINT32_C(0x40000000)
/* Sample: RGB555, priority/tie order, hardware layer number, present/semi. */
#define SAMPLE(c, rank, layer) (PRESENT | (c) | ((rank) << 16) | ((layer) << 24))
#define RANK(s) (((s) >> 16) & 255)
#define LAYER(s) (((s) >> 24) & 7)

typedef struct HudPpu {
    const Fe8MemoryView *memory;
    uint16_t control, bg[4], x[4], y[4], blend, alpha;
    uint16_t palette[512];
} HudPpu;

static uint16_t read16(const Fe8MemoryView *m, uint32_t address) {
    return m->read8(m->context, address) |
        ((uint16_t)m->read8(m->context, address + 1) << 8);
}
static int minimum(int a, int b) { return a < b ? a : b; }
static int maximum(int a, int b) { return a > b ? a : b; }
static int clamp(int n, int lo, int hi) { return maximum(lo, minimum(n, hi)); }
static bool inside(Fe8HudRect r, int x, int y) {
    return x >= r.x && y >= r.y && x < r.x + r.width && y < r.y + r.height;
}
static bool overlap(Fe8HudRect a, Fe8HudRect b) {
    return a.x < b.x + b.width && a.x + a.width > b.x &&
        a.y < b.y + b.height && a.y + a.height > b.y;
}
static uint32_t color8(unsigned color) {
    /* mGBA expands RGB555 by replicating the high bits, then blends RGB8. */
    uint32_t rgb = ((color & 31) << 3) | (((color >> 5) & 31) << 11) |
        (((color >> 10) & 31) << 19);
    return UINT32_C(0xFF000000) | rgb | ((rgb >> 5) & 0x070707);
}
static uint32_t mix8(unsigned a, unsigned b, unsigned eva, unsigned evb) {
    uint32_t ca = color8(a), cb = color8(b), result = UINT32_C(0xFF000000);
    for (unsigned shift = 0; shift < 24; shift += 8)
        result |= (unsigned)minimum(255,
            (((ca >> shift) & 255) * eva + ((cb >> shift) & 255) * evb) >> 4) << shift;
    return result;
}
static uint32_t bg_sample(const HudPpu *ppu, unsigned bg, int x, int y) {
    if (!(ppu->control & (1u << (8 + bg)))) return 0;
    unsigned sx = (x + ppu->x[bg]) & 255, sy = (y + ppu->y[bg]) & 255;
    unsigned cnt = ppu->bg[bg];
    uint32_t map = VRAM + ((cnt >> 8) & 31) * 2048;
    unsigned entry = read16(ppu->memory, map + (sy / 8 * 32 + sx / 8) * 2);
    unsigned px = sx & 7, py = sy & 7;
    if (entry & 0x400) px = 7 - px;
    if (entry & 0x800) py = 7 - py;
    uint32_t address = VRAM + ((cnt >> 2) & 3) * 16384 +
        (entry & 1023) * 32 + py * 4 + px / 2;
    unsigned packed = ppu->memory->read8(ppu->memory->context, address);
    unsigned index = (packed >> ((px & 1) * 4)) & 15;
    return index ? SAMPLE(ppu->palette[(entry >> 12) * 16 + index],
        (cnt & 3) * 8 + bg + 1, bg) : 0;
}
static void insert(uint32_t sample, uint32_t *first, uint32_t *second) {
    if (!(sample & PRESENT)) return;
    if (!( *first & PRESENT) || RANK(sample) < RANK(*first)) {
        *second = *first; *first = sample;
    } else if (!(*second & PRESENT) || RANK(sample) < RANK(*second)) {
        *second = sample;
    }
}
static uint32_t resolved8(const HudPpu *ppu, uint32_t first, uint32_t second) {
    bool alpha = ((ppu->blend >> 6) & 3) == 1 &&
        (ppu->blend & (1u << LAYER(first)));
    if (((first & SEMI) || alpha) &&
            (ppu->blend & (1u << (8 + LAYER(second)))))
        return mix8(first & 0x7FFF, second & 0x7FFF,
            minimum(ppu->alpha & 31, 16), minimum((ppu->alpha >> 8) & 31, 16));
    return color8(first & 0x7FFF);
}
static int panel_at(const Fe8NativeHud *hud, int x, int y) {
    for (unsigned n = 0; n < hud->count; ++n)
        if (inside(hud->panels[n].source, x, y)) return (int)n;
    return -1;
}

void fe8_native_hud_reset(Fe8NativeHud *hud) {
    if (!hud) return;
    hud->count = 0;
    hud->menu_latched = false;
}

/* Recognize whole, rectangular FE8 BG1 windows. Shape + palette + live scene
 * are deliberately stricter than "any nonzero BG0/1 tile is UI". In particular,
 * inventory, dialogue, battle forecasts and ROM-specific submenus fall back. */
static bool find_panels(Fe8NativeHud *hud, const HudPpu *ppu,
        const Fe8Snapshot *snapshot) {
    uint16_t tiles[600];
    uint8_t seen[600] = {0};
    unsigned queue[600];
    uint32_t base = VRAM + ((ppu->bg[1] >> 8) & 31) * 2048;
    bool kinds[4] = {false};
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 30; ++x)
            tiles[y * 30 + x] = read16(ppu->memory, base + (y * 32 + x) * 2);
    for (int cell = 0; cell < 600; ++cell) {
        if (seen[cell] || !(tiles[cell] & 1023)) continue;
        unsigned bank = tiles[cell] >> 12, count = 0, end = 1;
        int left = cell % 30, right = left, top = cell / 30, bottom = top;
        queue[0] = (unsigned)cell; seen[cell] = 1;
        while (count < end) {
            unsigned p = queue[count++];
            int x = p % 30, y = p / 30;
            left = minimum(left, x); right = maximum(right, x);
            top = minimum(top, y); bottom = maximum(bottom, y);
            int neighbours[4] = {x ? (int)p - 1 : -1, x < 29 ? (int)p + 1 : -1,
                y ? (int)p - 30 : -1, y < 19 ? (int)p + 30 : -1};
            for (int n = 0; n < 4; ++n) {
                int q = neighbours[n];
                if (q < 0 || seen[q] || !(tiles[q] & 1023) || tiles[q] >> 12 != bank) continue;
                seen[q] = 1; queue[end++] = (unsigned)q;
            }
        }
        int width = right - left + 1, height = bottom - top + 1;
        if (end != (unsigned)(width * height) || hud->count == FE8_HUD_MAX_PANELS)
            return false;
        Fe8HudKind kind;
        if (snapshot->input_lock == 0) {
            bool edge = left == 0 || right == 29;
            if (bank == 3 && edge && width >= 10 && width <= 20 &&
                    (height == 6 || height == 8) && (top == 0 || bottom == 19))
                kind = FE8_HUD_UNIT;
            else if (bank == 1 && edge && width == 7 && height == 6 &&
                    (top == 0 || bottom == 19))
                kind = FE8_HUD_TERRAIN;
            else if (bank == 1 && edge && width == 11 &&
                    (height == 4 || height == 6) && (top == 0 || bottom == 19))
                kind = FE8_HUD_OBJECTIVE;
            else return false;
        } else if (snapshot->input_lock == 1 && snapshot->active_unit_address &&
                bank == 1 && width >= 5 && width <= 16 && height >= 4 && height <= 16) {
            kind = FE8_HUD_ACTION;
        } else return false;
        if (kinds[kind]) return false;
        kinds[kind] = true;
        Fe8HudRect source = {left * 8, top * 8, width * 8, height * 8};
        if (kind == FE8_HUD_ACTION) {
            /* Include the animated hand's left overhang without its position
             * changing the menu's bounds on each animation frame. */
            int gutter = minimum(source.x, 8);
            source.x -= gutter; source.width += gutter;
        }
        hud->panels[hud->count++] = (Fe8HudPanel){kind, source, {0}};
    }
    return hud->count != 0 && (snapshot->input_lock == 0 || hud->count == 1);
}

static bool raster_objects(const HudPpu *ppu, const Fe8NativeHud *hud,
        uint32_t *world, uint32_t *ui) {
    static const int widths[3][4] = {{8,16,32,64},{16,32,32,64},{8,8,16,32}};
    static const int heights[3][4] = {{8,16,32,64},{8,8,16,32},{16,32,32,64}};
    bool hand = false;
    memset(world, 0, W * H * sizeof(*world));
    memset(ui, 0, W * H * sizeof(*ui));
    if (!(ppu->control & 0x1000)) return hud->panels[0].kind != FE8_HUD_ACTION;
    for (int n = 127; n >= 0; --n) {
        uint16_t a0 = read16(ppu->memory, OAM + n * 8);
        uint16_t a1 = read16(ppu->memory, OAM + n * 8 + 2);
        uint16_t a2 = read16(ppu->memory, OAM + n * 8 + 4);
        if ((a0 & 0x300) == 0x200 || (a0 >> 14) == 3) continue;
        unsigned shape = a0 >> 14, size = a1 >> 14, priority = (a2 >> 10) & 3;
        int width = widths[shape][size], height = heights[shape][size];
        int left = a1 & 511, top = a0 & 255;
        if (left >= 256) left -= 512;
        if (top >= 160) top -= 256;
        Fe8HudRect rect = {left, top, width, height};
        if (!overlap(rect, (Fe8HudRect){0,0,W,H})) continue;
        /* An affine/mosaic/8bpp or OBJ-window scene is outside this compositor's
         * contract. Keep the authoritative native frame rather than guessing. */
        if (a0 & 0x3900) return false;
        bool is_ui = false;
        for (unsigned p = 0; p < hud->count; ++p) {
            if (!overlap(rect, hud->panels[p].source) || priority != 0) continue;
            unsigned tile = a2 & 1023, bank = a2 >> 12;
            bool is_hand = tile == 0 && bank == 0 && width == 16 && height == 16 &&
                hud->panels[p].kind == FE8_HUD_ACTION;
            bool digits = tile >= 0x2E0 && tile <= 0x2EF && bank == 8 && width == 8 && height == 8;
            bool icon = tile >= 0x300 && tile <= 0x37F && (bank == 4 || bank == 5) &&
                width == 16 && height == 16;
            is_ui = is_hand || digits || icon;
            hand |= is_hand;
            if (is_ui) break;
        }
        if (priority == 0 && !is_ui) {
            /* The four native map-cursor corners are world-space, even when
             * they overlap a window. Unknown foreground sprites are not safe
             * to detach: preserve the entire native presentation instead. */
            bool map_cursor = (a2 & 1023) == 2 && (a2 >> 12) == 0 && width == 8 && height == 8;
            for (unsigned p = 0; p < hud->count; ++p)
                if (overlap(rect, hud->panels[p].source) && !map_cursor) return false;
        }
        if (is_ui && (a0 & 0x400)) return false;
        uint32_t *target = is_ui ? ui : world;
        for (int y = maximum(0, top); y < minimum(H, top + height); ++y)
            for (int x = maximum(0, left); x < minimum(W, left + width); ++x) {
                int sx = x - left, sy = y - top;
                if (a1 & 0x1000) sx = width - 1 - sx;
                if (a1 & 0x2000) sy = height - 1 - sy;
                unsigned tile = ((a2 & 1023) + (sy / 8) *
                    ((ppu->control & 0x40) ? width / 8 : 32) + sx / 8) & 1023;
                unsigned packed = ppu->memory->read8(ppu->memory->context,
                    VRAM + 0x10000 + tile * 32 + sy % 8 * 4 + sx % 8 / 2);
                unsigned index = (packed >> ((sx & 1) * 4)) & 15;
                if (!index) continue;
                size_t pos = (size_t)y * W + x;
                uint32_t sample = SAMPLE(ppu->palette[256 + (a2 >> 12) * 16 + index], priority * 8, 4);
                if (a0 & 0x400) sample |= SEMI;
                if (!(target[pos] & PRESENT) || RANK(sample) <= RANK(target[pos])) target[pos] = sample;
            }
    }
    return hud->panels[0].kind != FE8_HUD_ACTION || hand;
}

bool fe8_native_hud_extract(Fe8NativeHud *hud, const Fe8MemoryView *memory,
        const Fe8Snapshot *snapshot, const Fe8HostPixel *frame, size_t stride,
        bool live_map) {
    if (!hud) return false;
    hud->count = 0;
    if (!live_map || !memory || !memory->read8 || !snapshot || !frame || stride < W ||
            snapshot->phase || snapshot->combat_panel_active || snapshot->input_lock > 1 ||
            (snapshot->game_state_bits & 2)) goto fallback;
    HudPpu ppu = {0}; ppu.memory = memory;
    ppu.control = read16(memory, IO);
    /* Only the verified regular-text tactical layout, without hardware windows. */
    if ((ppu.control & 0xE087) || (ppu.control & 0xB00) != 0xB00) goto fallback;
    for (unsigned bg = 0; bg < 4; ++bg) {
        ppu.bg[bg] = read16(memory, IO + 8 + bg * 2);
        ppu.x[bg] = read16(memory, IO + 16 + bg * 4);
        ppu.y[bg] = read16(memory, IO + 18 + bg * 4);
        if (ppu.bg[bg] & 0xC0C0) goto fallback; /* 4bpp, 256x256, no mosaic */
        if ((ppu.bg[bg] & 3) != bg) goto fallback;
        if (bg < 2 && (ppu.x[bg] || ppu.y[bg])) goto fallback;
    }
    ppu.blend = read16(memory, IO + 0x50);
    ppu.alpha = read16(memory, IO + 0x52);
    if (((ppu.blend >> 6) & 3) > 1 || (ppu.blend & 1)) goto fallback;
    for (unsigned n = 0; n < 512; ++n) ppu.palette[n] = read16(memory, PAL + n * 2);
    if (!find_panels(hud, &ppu, snapshot)) goto fallback;
    uint32_t world_obj[W * H], ui_obj[W * H];
    if (!raster_objects(&ppu, hud, world_obj, ui_obj)) goto fallback;
    unsigned checked = 0, matched = 0;
    memset(hud->atlas, 0, sizeof(hud->atlas));
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
        size_t pos = (size_t)y * W + x;
        uint32_t bg0 = bg_sample(&ppu, 0, x, y), bg1 = bg_sample(&ppu, 1, x, y);
        uint32_t top_ui = 0, next_ui = 0;
        insert(bg0, &top_ui, &next_ui); insert(bg1, &top_ui, &next_ui);
        insert(ui_obj[pos], &top_ui, &next_ui);
        hud->world[pos] = frame[(size_t)y * stride + x];
        if (!(top_ui & PRESENT)) continue;
        if (panel_at(hud, x, y) < 0) goto fallback;
        uint32_t first = SAMPLE(ppu.palette[0], 255, 5), second = first;
        insert(bg_sample(&ppu, 3, x, y), &first, &second);
        insert(bg_sample(&ppu, 2, x, y), &first, &second);
        insert(world_obj[pos], &first, &second);
        uint32_t world = resolved8(&ppu, first, second);
        /* World-space cursor corners remain in the world; also recover the
         * UI behind them so relocating a panel does not leave cursor-shaped
         * holes in its atlas. */
        uint32_t full_first = first, full_second = second;
        insert(bg0, &full_first, &full_second); insert(bg1, &full_first, &full_second);
        insert(ui_obj[pos], &full_first, &full_second);
        uint32_t expected = resolved8(&ppu, full_first, full_second);
        uint32_t actual = frame[(size_t)y * stride + x];
        bool matches = true;
        for (unsigned shift = 0; shift < 24; shift += 8)
            if (abs((int)((actual >> shift) & 255) - (int)((expected >> shift) & 255)) > 8)
                matches = false;
        ++checked; matched += matches;
        hud->world[pos] = world;
        uint32_t pixel = color8(top_ui & 0x7FFF);
        /* FE8 blends BG1 against world layers. Encode independent foreground
         * and background coefficients in a straight-alpha host pixel, even
         * when EVA + EVB is 15 rather than 16 (native action menus). */
        /* A unit-screen round trip can disable backdrop blending while still
         * blending BG1 against terrain/units. Test the actual layer below the
         * panel, not a fixed global target mask. A foreground map cursor is
         * above BG1, so skip it when reconstructing the panel behind it. */
        uint32_t below_ui = RANK(first) > RANK(top_ui) ? first : second;
        if (LAYER(top_ui) == 1 && ((ppu.blend >> 6) & 3) == 1 && (ppu.blend & 2) &&
                (ppu.blend & (1u << (8 + LAYER(below_ui))))) {
            unsigned eva = minimum(ppu.alpha & 31, 16), evb = minimum((ppu.alpha >> 8) & 31, 16);
            if (evb >= 16 || eva > 16 - evb) goto fallback;
            unsigned a = 16 - evb;
            pixel = (a * 255 / 16) << 24;
            for (unsigned shift = 0; shift < 24; shift += 8)
                pixel |= (((color8(top_ui & 0x7FFF) >> shift) & 255) * eva / a) << shift;
        }
        hud->atlas[pos] = pixel;
    }
    /* Reject stale VRAM, scanline effects, unsupported sprites and custom
     * layouts. A false positive must never remove authoritative game pixels. */
    if (checked < 64 || matched * 100 < checked * 98) goto fallback;
    if (hud->panels[0].kind != FE8_HUD_ACTION) hud->menu_latched = false;
    return true;
fallback:
    fe8_native_hud_reset(hud);
    return false;
}

void fe8_native_hud_layout(Fe8NativeHud *hud, int width, int height,
        double cursor_x, double cursor_y, int scale_percent) {
    if (!hud || !hud->count || width <= 0 || height <= 0) return;
    double base = (double)width / 480;
    if ((double)height / 320 < base) base = (double)height / 320;
    double scale = base * clamp(scale_percent, 80, 200) / 100;
    int margin = maximum(4, (int)(base * 6 + .5));
    int top_width = 0, max_width = 0, max_height = 0;
    for (unsigned n = 0; n < hud->count; ++n) {
        Fe8HudPanel *p = &hud->panels[n];
        if (p->kind == FE8_HUD_UNIT || p->kind == FE8_HUD_OBJECTIVE) top_width += p->source.width;
        max_width = maximum(max_width, p->source.width);
        max_height = maximum(max_height, p->source.height);
    }
    if (top_width && scale * top_width > width - margin * 3)
        scale = (double)maximum(1, width - margin * 3) / top_width;
    if (scale * max_width > width - margin * 2) scale = (double)maximum(1,width-margin*2) / max_width;
    if (scale * max_height > height - margin * 2) scale = (double)maximum(1,height-margin*2) / max_height;
    for (unsigned n = 0; n < hud->count; ++n) {
        Fe8HudPanel *p = &hud->panels[n];
        int w = maximum(1, (int)(p->source.width * scale + .5));
        int h = maximum(1, (int)(p->source.height * scale + .5));
        int x = margin, y = margin;
        if (p->kind == FE8_HUD_TERRAIN || p->kind == FE8_HUD_OBJECTIVE) x = width - margin - w;
        if (p->kind == FE8_HUD_TERRAIN) y = height - margin - h;
        if (p->kind == FE8_HUD_ACTION) {
            if (!hud->menu_latched) {
                hud->menu_x = cursor_x / width; hud->menu_y = cursor_y / height;
                hud->menu_latched = true;
            }
            int anchor_x = (int)(hud->menu_x * width), anchor_y = (int)(hud->menu_y * height);
            int gap = maximum(margin, (int)(12 * base));
            x = anchor_x + gap;
            if (x + w > width - margin) x = anchor_x - gap - w;
            y = anchor_y - h / 2;
        }
        p->destination = (Fe8HudRect){clamp(x,0,width-w),clamp(y,0,height-h),w,h};
    }
}

bool fe8_native_hud_hit_test(const Fe8NativeHud *hud, int x, int y) {
    if (!hud) return false;
    for (unsigned n = 0; n < hud->count; ++n)
        if (inside(hud->panels[n].destination, x, y)) return true;
    return false;
}
void fe8_native_hud_draw(const Fe8NativeHud *hud, Fe8HostPixel *pixels,
        size_t stride, int width, int height) {
    if (!pixels || width <= 0 || height <= 0 || stride < (size_t)width) return;
    for (int y = 0; y < height; ++y) memset(pixels + y * stride, 0, width * sizeof(*pixels));
    if (!hud) return;
    for (unsigned n = 0; n < hud->count; ++n) {
        Fe8HudRect s = hud->panels[n].source, d = hud->panels[n].destination;
        if (d.width <= 0 || d.height <= 0) continue;
        for (int y = maximum(0,d.y); y < minimum(height,d.y+d.height); ++y)
            for (int x = maximum(0,d.x); x < minimum(width,d.x+d.width); ++x) {
                int sx = s.x + (x - d.x) * s.width / d.width;
                int sy = s.y + (y - d.y) * s.height / d.height;
                if (sx >= 0 && sy >= 0 && sx < W && sy < H)
                    pixels[(size_t)y * stride + x] = hud->atlas[sy * W + sx];
            }
    }
}
Fe8HostPixel fe8_native_hud_over(Fe8HostPixel foreground, Fe8HostPixel background) {
    unsigned alpha = foreground >> 24;
    uint32_t pixel = UINT32_C(0xFF000000);
    for (unsigned shift = 0; shift < 24; shift += 8)
        pixel |= ((((foreground >> shift) & 255) * alpha +
                   ((background >> shift) & 255) * (255 - alpha) + 127) / 255) << shift;
    return pixel;
}
