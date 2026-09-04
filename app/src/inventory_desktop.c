#include "inventory_desktop.h"
#include "host_text.h"

#include <stdio.h>
#include <string.h>

enum { PAD = 12, GAP = 12, HEADER = 60, GUTTER = 8 };

static const uint32_t BG = 0xFF111820, PANEL = 0xFF19232E;
static const uint32_t TEXT = 0xFFE4ECF3, MUTED = 0xFF93A7BB;
static const uint32_t LINE = 0xFF2D3B4A, ACCENT = 0xFF92D5C3;

typedef struct Painter {
    Fe8HostTextCanvas text;
    uint32_t *pixels;
    int stride, width, height;
    float scale;
} Painter;

static int clamp(int n, int lo, int hi) { return n < lo ? lo : n > hi ? hi : n; }
static float scale_for(const Fe8InventoryUi *ui) {
    return ui && ui->desktop_scale > 0 ? ui->desktop_scale : 1.0f;
}
static uint32_t abgr(uint32_t c) {
    return (c & 0xFF00FF00) | ((c & 0xFF0000) >> 16) | ((c & 0xFF) << 16);
}
static int px(const Painter *p, int v) { return (int)(v * p->scale + 0.5f); }
static void fill(Painter *p, int x, int y, int w, int h, uint32_t c) {
    int x0 = clamp(px(p, x), 0, p->width), y0 = clamp(px(p, y), 0, p->height);
    int x1 = clamp(px(p, x + w), 0, p->width), y1 = clamp(px(p, y + h), 0, p->height);
    c = abgr(c);
    for (int yy = y0; yy < y1; ++yy)
        for (int xx = x0; xx < x1; ++xx) p->pixels[yy * p->stride + xx] = c;
}
static void label(Painter *p, int x, int y, int w, int h,
    const char *value, uint32_t c, float size, int bold, int wrap) {
    if (w <= 0 || h <= 0) return;
    fe8_host_text_draw(&p->text, px(p, x), px(p, y),
        px(p, x + w) - px(p, x), px(p, y + h) - px(p, y), value,
        size * p->scale, abgr(c), bold ? FE8_HOST_TEXT_SEMIBOLD : FE8_HOST_TEXT_REGULAR, wrap);
}
static int same(Fe8InventoryEndpoint a, Fe8InventoryEndpoint b) {
    return a.kind == b.kind && a.unit_address == b.unit_address && a.slot == b.slot;
}
static const Fe8InventoryUnit *target(const Fe8InventoryUi *ui, const Fe8InventorySnapshot *s) {
    return ui->current_unit >= 0 && ui->current_unit < s->unit_count ?
        &s->units[ui->current_unit] : NULL;
}
static int deposit_index(const Fe8InventoryUi *ui) {
    return ui->pool_count && !ui->pool[ui->pool_count - 1].item ? ui->pool_count - 1 : -1;
}
static int item_count(const Fe8InventoryUi *ui) {
    return ui->pool_count - (deposit_index(ui) >= 0);
}
static int offset(int stored, int count, int rows) {
    return clamp(stored, 0, count > rows ? count - rows : 0);
}

void fe8_inventory_desktop_layout(const Fe8InventoryUi *ui, int width, int height,
    Fe8InventoryDesktopLayout *l) {
    /* Item, Type, Uses, Owner, Rank, Mt, Hit, Crit, Wt, Range, Use. */
    static const int widths[11] = {0, 64, 62, 114, 38, 36, 42, 38, 34, 52, 78};
    int reserved = 0;
    memset(l, 0, sizeof(*l));
    l->width = (int)(width / scale_for(ui));
    l->height = (int)(height / scale_for(ui));
    l->sidebar = clamp(l->width / 4, 232, 292);
    l->pool_x = PAD + l->sidebar + GAP;
    l->pool_width = l->width - PAD - l->pool_x;
    l->top = HEADER;
    l->help_y = l->height - 112;
    l->bottom = l->help_y - GAP;
    l->row_height = ui->comfortable ? 32 : 24;
    l->side_row_height = ui->comfortable && l->height >= 560 ? 32 : 24;
    l->items_y = l->top + 116;
    l->roster_y = l->items_y + 5 * l->side_row_height + 36;
    l->roster_rows = (l->bottom - l->roster_y) / l->side_row_height;
    if (l->roster_rows < 0) l->roster_rows = 0;
    l->table_y = l->top + 62;
    l->deposit_y = l->bottom - 28;
    l->table_rows = (l->deposit_y - l->table_y - 4) / l->row_height;
    if (l->table_rows < 0) l->table_rows = 0;
    memcpy(l->column_width, widths, sizeof(widths));
    /* Preserve the useful identity/ownership/uses columns first. Optional
       combat columns move into the hover summary on narrow windows. */
    for (int i = 1; i < 11; ++i) reserved += l->column_width[i];
    static const int optional[] = {8, 7, 4, 6, 5, 9};
    for (unsigned i = 0; i < sizeof(optional) / sizeof(optional[0]); ++i) {
        if (l->pool_width - GUTTER - reserved >= 168) break;
        int col = optional[i];
        reserved -= l->column_width[col]; l->column_width[col] = 0;
    }
    if (l->pool_width < 550) { reserved -= 28; l->column_width[3] = 86; }
    if (l->pool_width - GUTTER - reserved < 120) {
        reserved -= l->column_width[1]; l->column_width[1] = 0;
    }
    if (l->pool_width - GUTTER - reserved < 100) {
        reserved -= l->column_width[10]; l->column_width[10] = 0;
    }
    l->column_width[0] = l->pool_width - GUTTER - reserved;
    int x = l->pool_x;
    for (int i = 0; i < 11; ++i) {
        l->column_x[i] = x;
        x += l->column_width[i];
    }
}

void fe8_inventory_ui_toggle_density(Fe8InventoryUi *ui) {
    if (!ui) return;
    ui->comfortable = !ui->comfortable;
    ui->hover_kind = FE8_INVENTORY_HIT_NONE;
    ui->has_inspected = 0;
    snprintf(ui->status, sizeof(ui->status), "%s rows. Item selection is unchanged.",
        ui->comfortable ? "Comfortable" : "Compact");
}

void fe8_inventory_desktop_scroll(Fe8InventoryUi *ui, const Fe8InventorySnapshot *s,
    int width, int height, int x, int rows) {
    Fe8InventoryDesktopLayout l;
    fe8_inventory_desktop_layout(ui, width, height, &l);
    x = (int)(x / scale_for(ui));
    if (x >= l.pool_x && x < l.width - PAD)
        ui->pool_scroll = offset(offset(ui->pool_scroll, item_count(ui), l.table_rows) + rows,
            item_count(ui), l.table_rows);
    else if (x >= PAD && x < PAD + l.sidebar)
        ui->roster_scroll = offset(offset(ui->roster_scroll, s->unit_count, l.roster_rows) + rows,
            s->unit_count, l.roster_rows);
}

Fe8InventoryHitKind fe8_inventory_desktop_hit(const Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *s, int width, int height, int x, int y, int *index) {
    Fe8InventoryDesktopLayout l;
    if (!ui || !s || !index) return FE8_INVENTORY_HIT_NONE;
    *index = -1;
    if (!ui->active || x < 0 || y < 0 || x >= width || y >= height)
        return FE8_INVENTORY_HIT_NONE;
    fe8_inventory_desktop_layout(ui, width, height, &l);
    if (l.width < 640 || l.height < 480) return FE8_INVENTORY_HIT_NONE;
    x = (int)(x / scale_for(ui)); y = (int)(y / scale_for(ui));
    if (x < PAD || x >= l.width - PAD || y < l.top || y >= l.bottom)
        return FE8_INVENTORY_HIT_NONE;
    if (x < PAD + l.sidebar - GUTTER) {
        if (target(ui, s)) {
            if (x >= PAD + 94 && y >= l.top + 6 && y < l.top + 46) {
                *index = ui->current_unit;
                return y < l.top + 26 ? FE8_INVENTORY_HIT_UNIT_NAME : FE8_INVENTORY_HIT_UNIT_CLASS;
            }
            if (y >= l.items_y && y < l.items_y + 5 * l.side_row_height) {
                *index = (y - l.items_y) / l.side_row_height;
                return FE8_INVENTORY_HIT_UNIT_ITEM;
            }
        }
        if (y >= l.roster_y && y < l.roster_y + l.roster_rows * l.side_row_height) {
            *index = offset(ui->roster_scroll, s->unit_count, l.roster_rows) +
                (y - l.roster_y) / l.side_row_height;
            if (*index >= s->unit_count) return FE8_INVENTORY_HIT_NONE;
            return x >= PAD + l.sidebar * 42 / 100 ?
                FE8_INVENTORY_HIT_ROSTER_CLASS : FE8_INVENTORY_HIT_ROSTER;
        }
    }
    if (x < l.pool_x || x >= l.width - PAD - GUTTER) return FE8_INVENTORY_HIT_NONE;
    if (y >= l.top && y < l.top + 28) {
        if (x < l.pool_x + 112) return FE8_INVENTORY_HIT_POOL_SCOPE;
        if (x < l.pool_x + 224) return FE8_INVENTORY_HIT_POOL_SORT;
        if (x >= l.width - PAD - 130) return FE8_INVENTORY_HIT_DENSITY;
    }
    if (y >= l.table_y - 26 && y < l.table_y) {
        static const int sorts[4] = {FE8_INVENTORY_SORT_NAME, FE8_INVENTORY_SORT_TYPE,
            FE8_INVENTORY_SORT_USES, FE8_INVENTORY_SORT_OWNER};
        for (int col = 0; col < 4; ++col) {
            if (l.column_width[col] && x >= l.column_x[col] && x < l.column_x[col] + l.column_width[col]) {
                *index = sorts[col]; return FE8_INVENTORY_HIT_SORT_COLUMN;
            }
        }
    }
    if (y >= l.deposit_y && deposit_index(ui) >= 0) {
        *index = deposit_index(ui); return FE8_INVENTORY_HIT_POOL_ITEM;
    }
    if (y >= l.table_y && y < l.table_y + l.table_rows * l.row_height) {
        *index = offset(ui->pool_scroll, item_count(ui), l.table_rows) + (y - l.table_y) / l.row_height;
        if (*index < item_count(ui)) return FE8_INVENTORY_HIT_POOL_ITEM;
    }
    return FE8_INVENTORY_HIT_NONE;
}

static char rank(uint8_t r) {
    return r >= 251 ? 'S' : r >= 181 ? 'A' : r >= 121 ? 'B' : r >= 71 ? 'C' : r >= 31 ? 'D' : r ? 'E' : '-';
}
static int weapon(const Fe8ItemInfo *i) { return i && (i->attributes & 5); }
static const char *type_name(const Fe8ItemInfo *i) {
    static const char *names[] = {"Sword", "Lance", "Axe", "Bow", "Staff", "Anima", "Light", "Dark"};
    return weapon(i) ? i->weapon_type < 8 ? names[i->weapon_type] : "Gear" : "Item";
}
static const char *use_label(const Fe8InventoryUnit *u, const Fe8ItemInfo *i, uint32_t *color) {
    *color = MUTED;
    if (!i->movable) return "Fixed";
    switch (fe8_inventory_item_use_state(u, i)) {
    case FE8_INVENTORY_USE_READY: *color = ACCENT; return "Ready";
    case FE8_INVENTORY_USE_RANK: *color = 0xFFE4C389; return "Rank";
    case FE8_INVENTORY_USE_LOCKED: *color = 0xFFE39F9F; return "Locked";
    case FE8_INVENTORY_USE_STATUS: *color = 0xFFBCACE7; return "Status";
    default: return "Item";
    }
}
static void uses(char *out, size_t n, uint16_t encoded, const Fe8ItemInfo *i) {
    if (i->attributes & 8) snprintf(out, n, "INF");
    else if (i->max_uses) snprintf(out, n, "%u/%u", encoded >> 8, i->max_uses);
    else snprintf(out, n, "%u", encoded >> 8);
}
static void thumb(Painter *p, int x, int y, int rows, int row_h, int count, int scroll) {
    if (rows <= 0 || count <= rows) return;
    int h = rows * row_h, th = clamp(h * rows / count, 12, h);
    fill(p, x, y, 3, h, LINE);
    fill(p, x, y + (h - th) * scroll / (count - rows), 3, th, MUTED);
}
static void control(Painter *p, int x, int y, int w, const char *text, int hover) {
    fill(p, x, y, w, 28, hover ? 0xFF34495B : 0xFF243342);
    label(p, x + 10, y + 6, w - 20, 18, text, TEXT, 13, 0, 0);
}
static const Fe8ItemInfo *info_for(const Fe8InventorySnapshot *s, Fe8InventoryEndpoint e) {
    if (e.kind == FE8_INVENTORY_ENDPOINT_SUPPLY && e.unit_address == s->supply_address && e.slot < s->supply_capacity)
        return &s->supply_info[e.slot];
    for (int i = 0; i < s->unit_count; ++i)
        if (e.kind == FE8_INVENTORY_ENDPOINT_UNIT && e.unit_address == s->units[i].address && e.slot < 5)
            return &s->units[i].item_info[e.slot];
    return NULL;
}

void fe8_inventory_desktop_draw(const Fe8InventoryUi *ui, const Fe8InventorySnapshot *s,
    uint32_t *pixels, int stride, int width, int height) {
    Fe8InventoryDesktopLayout l;
    Painter p = {.pixels = pixels, .stride = stride, .width = width, .height = height, .scale = scale_for(ui)};
    const Fe8InventoryUnit *u = target(ui, s);
    char b[256];
    if (!pixels || stride < width || width <= 0 || height <= 0) return;
    fe8_inventory_desktop_layout(ui, width, height, &l);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) pixels[y * stride + x] = abgr(BG);
    if (!fe8_host_text_begin(&p.text, pixels, stride, width, height)) return;
    if (l.width < 640 || l.height < 480) {
        label(&p, PAD, 20, l.width - 24, l.height - 40,
            "Enlarge the window to at least 640 x 480 to manage inventory. Press I or Escape to return to the game.",
            TEXT, 14, 0, 1);
        fe8_host_text_end(&p.text);
        return;
    }
    label(&p, PAD, 13, 180, 30, s->prebattle ? "Preparations" : "Inventory", TEXT, 22, 1, 0);
    label(&p, 200, 20, l.width - 420, 20, ui->status, ui->has_selection ? 0xFFE4C389 : MUTED, 13, 0, 0);
    snprintf(b, sizeof(b), "Supply  %u / %u", s->supply_count, s->supply_capacity);
    label(&p, l.width - 188, 20, 176, 20, b, MUTED, 13, 0, 0);
    fill(&p, PAD, l.top, l.sidebar, l.bottom - l.top, PANEL);
    fill(&p, l.pool_x, l.top + 36, l.pool_width, l.bottom - l.top - 36, PANEL);

    if (u) {
        fill(&p, PAD + 6, l.top + 4, 82, 76, 0xFF243442);
        if (u->portrait_valid) {
            for (int yy = 0; yy < FE8_PORTRAIT_HEIGHT; ++yy)
                for (int xx = 0; xx < FE8_PORTRAIT_WIDTH; ++xx) {
                    uint8_t ci = u->portrait[yy * FE8_PORTRAIT_WIDTH + xx];
                    if (ci < 16 && u->portrait_palette[ci])
                        fill(&p, PAD + 7 + xx, l.top + 6 + yy, 1, 1, abgr(u->portrait_palette[ci]));
                }
        }
        int detail_x = PAD + 94, detail_w = l.sidebar - 100;
        for (int is_class = 0; is_class < 2; ++is_class) {
            int yy = l.top + 6 + is_class * 20;
            int hover = ui->hover_unit_address == u->address && ui->hover_kind ==
                (is_class ? FE8_INVENTORY_HIT_UNIT_CLASS : FE8_INVENTORY_HIT_UNIT_NAME);
            if (hover) fill(&p, detail_x, yy, detail_w, 20, 0xFF31485A);
            label(&p, detail_x, yy + 1, detail_w, 19, is_class ? u->class_name : u->name,
                is_class ? MUTED : TEXT, is_class ? 13 : 15, !is_class, 0);
            fill(&p, detail_x, yy + 19, detail_w, 1, hover ? ACCENT : LINE);
        }
        snprintf(b, sizeof(b), "Lv %u   Exp %u", u->level, u->exp);
        label(&p, detail_x, l.top + 49, detail_w, 18, b, MUTED, 12, 0, 0);
        snprintf(b, sizeof(b), "HP %u / %u", u->hp, u->max_hp);
        label(&p, detail_x, l.top + 66, detail_w, 18, b, ACCENT, 13, 0, 0);
        snprintf(b, sizeof(b), "Pow %u   Skl %u   Spd %u   Lck %u", u->power, u->skill, u->speed, u->luck);
        label(&p, PAD + 8, l.top + 84, l.sidebar - 16, 16, b, MUTED, 12, 0, 0);
        snprintf(b, sizeof(b), "Def %u   Res %u   Con %u   Mov %u", u->defense, u->resistance, u->constitution, u->movement);
        label(&p, PAD + 8, l.top + 100, l.sidebar - 16, 16, b, MUTED, 12, 0, 0);
        for (int i = 0; i < 5; ++i) {
            Fe8InventoryEndpoint e = {FE8_INVENTORY_ENDPOINT_UNIT, u->address, (unsigned)i};
            const Fe8ItemInfo *info = &u->item_info[i];
            int y = l.items_y + i * l.side_row_height;
            int selected = ui->has_selection && same(ui->selected, e);
            int hovered = ui->has_inspected && same(ui->inspected, e);
            fill(&p, PAD + 4, y, l.sidebar - 8, l.side_row_height - 1,
                selected ? 0xFF294F63 : hovered ? 0xFF2B3D4D : i % 2 ? 0xFF1E2A36 : PANEL);
            if (selected) fill(&p, PAD + 4, y, 2, l.side_row_height - 1, ACCENT);
            snprintf(b, sizeof(b), "%d", i + 1);
            int text_y = y + (l.side_row_height - 16) / 2;
            label(&p, PAD + 10, text_y, 15, 18, b, MUTED, 12, 0, 0);
            label(&p, PAD + 27, text_y, l.sidebar - 136, 18,
                u->items[i] ? info->name : "Empty slot", u->items[i] ? TEXT : MUTED, 13, 0, 0);
            if (u->items[i]) {
                uint32_t color;
                uses(b, sizeof(b), u->items[i], info);
                label(&p, PAD + l.sidebar - 103, text_y, 52, 18, b, MUTED, 12, 0, 0);
                const char *state = use_label(u, info, &color);
                label(&p, PAD + l.sidebar - 51, text_y, 48, 18, state, color, 12, 0, 0);
            }
        }
    }
    snprintf(b, sizeof(b), "Recipients  /  %u", s->unit_count);
    label(&p, PAD + 8, l.roster_y - 25, l.sidebar - 16, 20, b, MUTED, 12, 1, 0);
    int ro = offset(ui->roster_scroll, s->unit_count, l.roster_rows);
    for (int row = 0; row < l.roster_rows && ro + row < s->unit_count; ++row) {
        const Fe8InventoryUnit *r = &s->units[ro + row];
        int y = l.roster_y + row * l.side_row_height;
        int active = ro + row == ui->current_unit;
        int hovered = ui->hover_unit_address == r->address &&
            (ui->hover_kind == FE8_INVENTORY_HIT_ROSTER || ui->hover_kind == FE8_INVENTORY_HIT_ROSTER_CLASS);
        if (active || hovered) fill(&p, PAD + 4, y, l.sidebar - 12, l.side_row_height - 1,
            active ? 0xFF294254 : 0xFF253646);
        if (active) fill(&p, PAD + 4, y, 2, l.side_row_height - 1, ACCENT);
        label(&p, PAD + 10, y + (l.side_row_height - 16) / 2, l.sidebar * 42 / 100 - 14, 18,
            r->name, active ? ACCENT : TEXT, 13, 0, 0);
        label(&p, PAD + l.sidebar * 42 / 100, y + (l.side_row_height - 16) / 2,
            l.sidebar * 58 / 100 - 12, 18, r->class_name, MUTED, 12, 0, 0);
    }
    thumb(&p, PAD + l.sidebar - 5, l.roster_y, l.roster_rows, l.side_row_height, s->unit_count, ro);

    snprintf(b, sizeof(b), "%s  [A]", ui->pool_scope == FE8_INVENTORY_POOL_ALL ? "All items" : "Supply");
    control(&p, l.pool_x, l.top, 108, b, ui->hover_kind == FE8_INVENTORY_HIT_POOL_SCOPE);
    snprintf(b, sizeof(b), "Sort: %s", fe8_inventory_ui_sort_name(ui->pool_sort));
    control(&p, l.pool_x + 112, l.top, 108, b, ui->hover_kind == FE8_INVENTORY_HIT_POOL_SORT);
    if (l.pool_width >= 550) {
        snprintf(b, sizeof(b), "%d items  /  for %s", item_count(ui), u ? u->name : "no target");
        label(&p, l.pool_x + 234, l.top + 7, l.pool_width - 370, 18, b, MUTED, 12, 0, 0);
    }
    control(&p, l.width - PAD - 126, l.top, 126,
        ui->comfortable ? "Comfortable [D]" : "Compact [D]", ui->hover_kind == FE8_INVENTORY_HIT_DENSITY);
    static const char *headers[] = {"Item", "Type", "Uses", "Owner", "Rk", "Mt", "Hit", "Crit", "Wt", "Range", "Use"};
    for (int col = 0; col < 11; ++col) {
        if (!l.column_width[col]) continue;
        static const int sorts[] = {FE8_INVENTORY_SORT_NAME, FE8_INVENTORY_SORT_TYPE, FE8_INVENTORY_SORT_USES, FE8_INVENTORY_SORT_OWNER};
        int sorted = col < 4 && (int)ui->pool_sort == sorts[col];
        snprintf(b, sizeof(b), "%s%s", headers[col], sorted ? " v" : "");
        label(&p, l.column_x[col] + 8, l.table_y - 22, l.column_width[col] - 10, 18, b,
            sorted ? ACCENT : MUTED, 12, 1, 0);
    }
    fill(&p, l.pool_x, l.table_y - 1, l.pool_width, 1, LINE);
    int po = offset(ui->pool_scroll, item_count(ui), l.table_rows);
    for (int row = 0; row < l.table_rows && po + row < item_count(ui); ++row) {
        const Fe8InventoryListEntry *e = &ui->pool[po + row];
        const Fe8ItemInfo *i = e->info;
        int y = l.table_y + row * l.row_height, text_y = y + (l.row_height - 16) / 2;
        int selected = ui->has_selection && same(ui->selected, e->endpoint);
        int hover = ui->has_inspected && same(ui->inspected, e->endpoint);
        fill(&p, l.pool_x, y, l.pool_width - GUTTER, l.row_height,
            selected ? 0xFF294F63 : hover ? 0xFF2B3D4D : row % 2 ? 0xFF1D2935 : PANEL);
        if (selected) fill(&p, l.pool_x, y, 2, l.row_height, ACCENT);
        for (int col = 0; col < 11; ++col) {
            if (!l.column_width[col]) continue;
            uint32_t color = TEXT;
            switch (col) {
            case 0: snprintf(b, sizeof(b), "%s", i->name); break;
            case 1: snprintf(b, sizeof(b), "%s", type_name(i)); color = MUTED; break;
            case 2: uses(b, sizeof(b), e->item, i); color = i->max_uses && (e->item >> 8) * 4 <= i->max_uses && !(i->attributes & 8) ? 0xFFE4C389 : MUTED; break;
            case 3: snprintf(b, sizeof(b), "%s", e->unit_index >= 0 ? s->units[e->unit_index].name : "Supply"); color = MUTED; break;
            case 4: snprintf(b, sizeof(b), "%c", weapon(i) ? rank(i->weapon_rank) : '-'); break;
            case 5: case 6: case 7: case 8:
                if (weapon(i)) snprintf(b, sizeof(b), "%u", col == 5 ? i->might : col == 6 ? i->hit : col == 7 ? i->crit : i->weight);
                else snprintf(b, sizeof(b), "-");
                break;
            case 9:
                if (!weapon(i)) snprintf(b, sizeof(b), "-");
                else if (i->min_range == i->max_range) snprintf(b, sizeof(b), "%u", i->min_range);
                else snprintf(b, sizeof(b), "%u-%u", i->min_range, i->max_range);
                break;
            default: snprintf(b, sizeof(b), "%s", use_label(u, i, &color)); break;
            }
            if (!i->movable && col != 10) color = MUTED;
            label(&p, l.column_x[col] + 8, text_y, l.column_width[col] - 10, 18, b, color, 13, 0, 0);
        }
    }
    if (!item_count(ui)) label(&p, l.pool_x + 14, l.table_y + 18, l.pool_width - 28, 24,
        "No items in this view.", MUTED, 14, 0, 0);
    thumb(&p, l.width - PAD - 5, l.table_y, l.table_rows, l.row_height, item_count(ui), po);
    int di = deposit_index(ui);
    int dh = di >= 0 && ui->has_inspected && same(ui->inspected, ui->pool[di].endpoint);
    fill(&p, l.pool_x, l.deposit_y, l.pool_width, 28, dh ? 0xFF304F51 : 0xFF223534);
    snprintf(b, sizeof(b), di >= 0 ? "+  Store in supply     %u / %u slots used" : "Supply full  /  select an existing item to swap",
        s->supply_count, s->supply_capacity);
    label(&p, l.pool_x + 10, l.deposit_y + 6, l.pool_width - 20, 18, b, di >= 0 ? ACCENT : MUTED, 13, 0, 0);

    fill(&p, PAD, l.help_y, l.width - PAD * 2, 1, LINE);
    const char *title = NULL;
    const char *help = fe8_inventory_ui_unit_help(ui, s, &title);
    const Fe8ItemInfo *i = NULL;
    if (!help && (ui->has_inspected || ui->has_selection))
        i = info_for(s, ui->has_inspected ? ui->inspected : ui->selected);
    if (help) {
        int is_class = ui->hover_kind == FE8_INVENTORY_HIT_UNIT_CLASS || ui->hover_kind == FE8_INVENTORY_HIT_ROSTER_CLASS;
        snprintf(b, sizeof(b), "%s  /  %s", is_class ? "Class" : "Character", title);
        label(&p, PAD, l.help_y + 9, l.width - 24, 22, b, ACCENT, 14, 1, 0);
    } else if (i && i->id) {
        uint32_t color;
        snprintf(b, sizeof(b), "%s  /  %s   %s", i->name, type_name(i), use_label(u, i, &color));
        label(&p, PAD, l.help_y + 9, l.width - 24, 22, b, ACCENT, 14, 1, 0);
        if (weapon(i)) {
            snprintf(b, sizeof(b), "Rank %c    Might %u    Hit %u    Crit %u    Weight %u    Range %u-%u",
                rank(i->weapon_rank), i->might, i->hit, i->crit, i->weight, i->min_range, i->max_range);
            label(&p, PAD, l.help_y + 30, l.width - 24, 18, b, MUTED, 12, 0, 0);
        }
        help = i->description[0] ? i->description : "No item description is available in this ROM.";
    } else {
        label(&p, PAD, l.help_y + 9, l.width - 24, 22, "Inspect & assign", ACCENT, 14, 1, 0);
        help = "Hover a name, class or item for details. Choose an item, then click a recipient's slot to move or swap it.";
    }
    int desc_y = l.help_y + (i && weapon(i) ? 49 : 32);
    label(&p, PAD, desc_y, l.width - 24, l.height - 26 - desc_y, help, TEXT, 13, 0, 1);
    label(&p, PAD, l.height - 21, l.width - 24, 19,
        "A  All / Supply     S  Sort     D  Density     U  Undo     Right-click  Cancel     I / Esc  Close", MUTED, 12, 0, 0);
    fe8_host_text_end(&p.text);
}
