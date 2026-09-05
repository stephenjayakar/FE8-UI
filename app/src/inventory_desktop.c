/* Armory workspace: a read-only derived browser, a recipient workbench and a
   persistent inspector. Only main.c owns the guarded inventory transaction. */
#include "inventory_desktop.h"
#include "host_text.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

enum { PAD = 12, GAP = 12, GUTTER = 8, SCALE_MIN = 80, SCALE_MAX = 200, SCALE_STEP = 10 };
static const uint32_t BG = 0xFF10151E, PANEL = 0xFF181F2B, RAISED = 0xFF202A38;
static const uint32_t TEXT = 0xFFECF1F7, MUTED = 0xFF9BAABC, LINE = 0xFF2B3747;
static const uint32_t ACCENT = 0xFF96E0CC, SELECTED = 0xFF233F42, GOLD = 0xFFE9C58A;
static const uint32_t WARN = 0xFFF1BA85, DANGER = 0xFFEBA6AA;
static const char *const TYPES[] = {"All", "Sword", "Lance", "Axe", "Bow", "Staff", "Anima", "Light", "Dark", "Items"};
static const uint32_t TYPE_COLORS[] = {0xFFACBDD1,0xFFADBFEF,0xFF8CCBE5,0xFFA4CEA7,
    0xFFE0B285,0xFFE6CEA0,0xFF91D2BC,0xFFE8DAA3,0xFFC8AFE9,0xFFA7BCCF};

typedef struct Painter {
    Fe8HostTextCanvas text;
    uint32_t *pixels;
    int stride, width, height;
    float scale;
} Painter;

static int clamp(int n, int lo, int hi) { return n < lo ? lo : n > hi ? hi : n; }
static int same(Fe8InventoryEndpoint a, Fe8InventoryEndpoint b) {
    return a.kind == b.kind && a.unit_address == b.unit_address && a.slot == b.slot;
}
static float density(const Fe8InventoryUi *ui) {
    return ui && ui->desktop_scale > 0 ? ui->desktop_scale : 1.0f;
}
static int maximum_scale_percent(const Fe8InventoryUi *ui, int width, int height) {
    double fit = width / (640.0 * density(ui));
    double vertical = height / (480.0 * density(ui));
    if (vertical < fit) fit = vertical;
    if (fit >= SCALE_MAX / 100.0) return SCALE_MAX;
    if (fit <= SCALE_MIN / 100.0) return SCALE_MIN;
    return (int)(fit * 100.0 + 0.0001) / SCALE_STEP * SCALE_STEP;
}
int fe8_inventory_ui_scale_percent(const Fe8InventoryUi *ui, int width, int height) {
    return clamp(ui && ui->zoom_percent > 0 ? ui->zoom_percent : 100,
        SCALE_MIN, maximum_scale_percent(ui, width, height));
}
float fe8_inventory_desktop_scale(const Fe8InventoryUi *ui, int width, int height) {
    return density(ui) * fe8_inventory_ui_scale_percent(ui, width, height) / 100.0f;
}
static void clear_hover(Fe8InventoryUi *ui) {
    ui->hover_kind = FE8_INVENTORY_HIT_NONE;
    ui->hover_unit_address = 0;
    ui->has_inspected = 0;
}
static void changed_view(Fe8InventoryUi *ui) {
    ui->pool_scroll = 0;
    clear_hover(ui);
}
void fe8_inventory_ui_adjust_scale(Fe8InventoryUi *ui, int direction, int width, int height) {
    int percent;
    if (!ui || !ui->active || !ui->desktop) return;
    percent = fe8_inventory_ui_scale_percent(ui, width, height);
    if (!direction) ui->zoom_percent = 100;
    else {
        int next = clamp(percent + (direction > 0 ? SCALE_STEP : -SCALE_STEP),
            SCALE_MIN, maximum_scale_percent(ui, width, height));
        if (next != percent) ui->zoom_percent = next;
    }
    clear_hover(ui);
    percent = fe8_inventory_ui_scale_percent(ui, width, height);
    snprintf(ui->status, sizeof(ui->status), "UI scale %d%%%s", percent,
        direction > 0 && percent == maximum_scale_percent(ui, width, height) && percent < SCALE_MAX ?
        " (window limit). Enlarge the window for more." : ". +/- to resize, 0 to reset.");
}
void fe8_inventory_ui_toggle_density(Fe8InventoryUi *ui) {
    if (!ui) return;
    ui->comfortable = !ui->comfortable;
    clear_hover(ui);
    snprintf(ui->status, sizeof(ui->status), "%s rows. Your item selection is unchanged.",
        ui->comfortable ? "Comfortable" : "Compact");
}
static const Fe8InventoryUnit *target(const Fe8InventoryUi *ui, const Fe8InventorySnapshot *s) {
    return ui && s && ui->current_unit >= 0 && ui->current_unit < s->unit_count ?
        &s->units[ui->current_unit] : NULL;
}
static int owner_index(const Fe8InventorySnapshot *s, Fe8InventoryEndpoint e) {
    if (e.kind == FE8_INVENTORY_ENDPOINT_UNIT)
        for (int i = 0; i < s->unit_count; ++i) if (s->units[i].address == e.unit_address) return i;
    return -1;
}
static const Fe8ItemInfo *info_at(const Fe8InventorySnapshot *s, Fe8InventoryEndpoint e) {
    int owner;
    if (e.kind == FE8_INVENTORY_ENDPOINT_SUPPLY)
        return e.unit_address == s->supply_address && e.slot < s->supply_capacity &&
            e.slot < FE8_SUPPLY_MAX_CAPACITY ? &s->supply_info[e.slot] : NULL;
    owner = owner_index(s, e);
    return owner >= 0 && e.slot < FE8_INVENTORY_ITEM_SLOTS ? &s->units[owner].item_info[e.slot] : NULL;
}
static int type_key(const Fe8ItemInfo *info) {
    return info && (info->attributes & 5) ? info->weapon_type < 8 ? info->weapon_type + 1 : 9 : 9;
}
static char rank_letter(int r) {
    return r >= 251 ? 'S' : r >= 181 ? 'A' : r >= 121 ? 'B' : r >= 71 ? 'C' : r >= 31 ? 'D' : r ? 'E' : '-';
}
static int occupied(const Fe8InventoryUnit *u) {
    int n = 0;
    if (u) for (int j = 0; j < FE8_INVENTORY_ITEM_SLOTS; ++j) n += u->items[j] != 0;
    return n;
}
static int free_slot(const Fe8InventoryUnit *u) {
    if (u) for (int j = 0; j < FE8_INVENTORY_ITEM_SLOTS; ++j) if (!u->items[j]) return j;
    return -1;
}
static int deposit_index(const Fe8InventoryUi *ui) {
    for (int i = 0; i < ui->pool_count; ++i)
        if (!ui->pool[i].item && ui->pool[i].endpoint.kind == FE8_INVENTORY_ENDPOINT_SUPPLY) return i;
    return -1;
}
static int offset(int stored, int count, int rows) { return clamp(stored, 0, count > rows ? count - rows : 0); }
static int contains(const char *haystack, const char *needle, size_t length) {
    for (; *haystack; ++haystack) {
        size_t j = 0;
        while (j < length && haystack[j] &&
            tolower((unsigned char)haystack[j]) == tolower((unsigned char)needle[j])) ++j;
        if (j == length) return 1;
    }
    return !length;
}
static int matches_query(const char *query, const Fe8InventoryListEntry *e, const Fe8InventorySnapshot *s) {
    const char *owner = e->unit_index >= 0 && e->unit_index < s->unit_count ? s->units[e->unit_index].name : "Supply";
    const char *class_name = e->unit_index >= 0 && e->unit_index < s->unit_count ? s->units[e->unit_index].class_name : "Convoy";
    while (*query) {
        const char *start;
        size_t n;
        while (*query && isspace((unsigned char)*query)) ++query;
        start = query;
        while (*query && !isspace((unsigned char)*query)) ++query;
        n = (size_t)(query - start);
        if (n && !contains(e->info ? e->info->name : "", start, n) &&
            !contains(owner, start, n) && !contains(class_name, start, n) &&
            !contains(TYPES[type_key(e->info)], start, n)) return 0;
    }
    return 1;
}
int fe8_inventory_desktop_visible(const Fe8InventoryUi *ui, const Fe8InventorySnapshot *s,
    int indices[FE8_INVENTORY_POOL_CAPACITY]) {
    int count = 0;
    const Fe8InventoryUnit *u = target(ui, s);
    if (!ui || !s) return 0;
    for (int row = 0; row < ui->pool_count && row < FE8_INVENTORY_POOL_CAPACITY; ++row) {
        int i = ui->sort_descending ? ui->pool_count - 1 - row : row;
        const Fe8InventoryListEntry *e = &ui->pool[i];
        if (!e->item || (ui->type_filter && type_key(e->info) != ui->type_filter)) continue;
        /* "Usable" means the rank/lock/status check says Ready, not merely
           transferable. Consumables remain in Items, without invented use rules. */
        if (ui->usable_only && (!u || fe8_inventory_item_use_state(u, e->info) != FE8_INVENTORY_USE_READY)) continue;
        if (!matches_query(ui->query, e, s)) continue;
        if (indices) indices[count] = i;
        ++count;
    }
    return count;
}
void fe8_inventory_desktop_text(Fe8InventoryUi *ui, const char *utf8) {
    size_t length;
    if (!ui || !utf8 || !ui->search_active) return;
    length = strlen(ui->query);
    /* SDL supplies complete UTF-8 sequences. Never truncate inside one. */
    while (*utf8) {
        unsigned char c = (unsigned char)*utf8;
        size_t n = c < 128 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
        if (!n || c < 32 || c == 127) { ++utf8; continue; }
        size_t j;
        for (j = 1; j < n && utf8[j] && ((unsigned char)utf8[j] & 0xC0) == 0x80; ++j) {}
        if (j != n) { ++utf8; continue; }
        if (length + n >= sizeof(ui->query)) break;
        memcpy(ui->query + length, utf8, n);
        length += n; utf8 += n;
    }
    ui->query[length] = 0;
    changed_view(ui);
}
void fe8_inventory_desktop_backspace(Fe8InventoryUi *ui) {
    size_t n;
    if (!ui || !ui->search_active || !(n = strlen(ui->query))) return;
    do { --n; } while (n && ((unsigned char)ui->query[n] & 0xC0) == 0x80);
    ui->query[n] = 0;
    changed_view(ui);
}

void fe8_inventory_desktop_layout(const Fe8InventoryUi *ui, int width, int height,
    Fe8InventoryDesktopLayout *l) {
    static const int widths[11] = {0, 60, 64, 106, 34, 34, 40, 38, 34, 46, 72};
    static const int optional[] = {8, 7, 4, 9, 6, 5, 1, 10};
    float scale = fe8_inventory_desktop_scale(ui, width, height);
    int reserved = 60; /* Pinned, local Give/Store action column. */
    memset(l, 0, sizeof(*l));
    l->width = (int)(width / scale + 0.0001f);
    l->height = (int)(height / scale + 0.0001f);
    l->sidebar = clamp(l->width / 5, 232, 292);
    l->pool_x = PAD + l->sidebar + GAP;
    l->top = l->height >= 600 ? 76 : 60;
    l->detail_wide = l->width >= 1220 && l->height >= 720;
    l->detail_width = l->detail_wide ? 280 : l->width - 2 * PAD;
    l->detail_height = l->detail_wide ? l->height - l->top - 48 : l->height >= 600 ? 152 : 100;
    l->detail_x = l->detail_wide ? l->width - PAD - l->detail_width : PAD;
    l->detail_y = l->detail_wide ? l->top : l->height - PAD - l->detail_height;
    l->help_y = l->detail_wide ? l->height - 36 : l->detail_y;
    l->bottom = l->help_y - GAP;
    l->pool_width = (l->detail_wide ? l->detail_x - GAP : l->width - PAD) - l->pool_x;
    l->row_height = l->height < 600 ? 24 : ui->comfortable ? 40 : 28;
    l->side_row_height = l->height < 720 ? 24 : ui->comfortable ? 36 : 32;
    l->items_y = l->top + 116;
    l->roster_y = l->items_y + 5 * l->side_row_height + 36;
    l->roster_rows = (l->bottom - l->roster_y) / l->side_row_height;
    if (l->roster_rows < 0) l->roster_rows = 0;
    l->search_y = l->top + 36;
    l->filters_y = l->search_y + 38;
    l->filter_columns = l->pool_width >= 680 ? 10 : 5;
    l->filter_height = 26;
    l->table_y = l->filters_y + (10 / l->filter_columns) * l->filter_height + 30;
    l->deposit_y = l->bottom - 32;
    l->table_rows = (l->deposit_y - l->table_y - 4) / l->row_height;
    if (l->table_rows < 0) l->table_rows = 0;
    memcpy(l->column_width, widths, sizeof(widths));
    for (int i = 1; i < 11; ++i) reserved += l->column_width[i];
    for (unsigned i = 0; i < sizeof(optional) / sizeof(optional[0]); ++i) {
        if (l->pool_width - GUTTER - reserved >= 160) break;
        reserved -= l->column_width[optional[i]]; l->column_width[optional[i]] = 0;
    }
    if (l->pool_width < 400) { reserved -= 24; l->column_width[3] -= 24; }
    l->column_width[0] = l->pool_width - GUTTER - reserved;
    int x = l->pool_x;
    for (int i = 0; i < 11; ++i) { l->column_x[i] = x; x += l->column_width[i]; }
    l->quick_x = x;
    l->quick_width = 60;
    l->action_width = l->detail_wide ? l->detail_width - 32 : clamp(l->width / 4, 152, 220);
    l->action_x = l->detail_x + l->detail_width - l->action_width - 16;
    l->action_y = l->detail_wide ? l->detail_y + l->detail_height - 116 : l->detail_y + 8;
}

static int in(int x, int y, int left, int top, int w, int h) {
    return x >= left && y >= top && x < left + w && y < top + h;
}
Fe8InventoryHitKind fe8_inventory_desktop_hit(const Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *s, int width, int height, int x, int y, int *index) {
    Fe8InventoryDesktopLayout l;
    int visible[FE8_INVENTORY_POOL_CAPACITY], n;
    float scale;
    if (!ui || !s || !index) return FE8_INVENTORY_HIT_NONE;
    *index = -1;
    if (!ui->active || x < 0 || y < 0 || x >= width || y >= height) return FE8_INVENTORY_HIT_NONE;
    fe8_inventory_desktop_layout(ui, width, height, &l);
    if (l.width < 640 || l.height < 480) return FE8_INVENTORY_HIT_NONE;
    scale = fe8_inventory_desktop_scale(ui, width, height);
    x = (int)(x / scale); y = (int)(y / scale);
    if (in(x,y,l.width-88,14,76,28)) return FE8_INVENTORY_HIT_CLOSE;
    if (in(x,y,l.action_x,l.action_y,l.action_width,28)) return FE8_INVENTORY_HIT_GIVE;
    if (in(x,y,l.action_x,l.action_y+32,l.action_width,28)) return ui->has_selection ? FE8_INVENTORY_HIT_CANCEL : FE8_INVENTORY_HIT_MOVE;
    if (in(x,y,l.action_x,l.action_y+64,l.action_width,28)) return FE8_INVENTORY_HIT_STORE;
    if (x < PAD || x >= l.width - PAD || y < l.top || y >= l.bottom) return FE8_INVENTORY_HIT_NONE;
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
            *index = offset(ui->roster_scroll, s->unit_count, l.roster_rows) + (y - l.roster_y) / l.side_row_height;
            if (*index >= s->unit_count) return FE8_INVENTORY_HIT_NONE;
            return x >= PAD + l.sidebar * 42 / 100 ? FE8_INVENTORY_HIT_ROSTER_CLASS : FE8_INVENTORY_HIT_ROSTER;
        }
        return FE8_INVENTORY_HIT_NONE;
    }
    if (x < l.pool_x || x >= l.pool_x + l.pool_width - GUTTER) return FE8_INVENTORY_HIT_NONE;
    if (y < l.top + 28) {
        if (x < l.pool_x + 112) return FE8_INVENTORY_HIT_POOL_SCOPE;
        if (x < l.pool_x + 224) return FE8_INVENTORY_HIT_POOL_SORT;
        if (x >= l.pool_x + l.pool_width - 104) return FE8_INVENTORY_HIT_DENSITY;
    }
    if (in(x,y,l.pool_x,l.search_y,l.pool_width-116,30)) return FE8_INVENTORY_HIT_SEARCH;
    if (in(x,y,l.pool_x+l.pool_width-112,l.search_y,104,30)) return FE8_INVENTORY_HIT_USABLE;
    if (y >= l.filters_y && y < l.filters_y + 10 / l.filter_columns * l.filter_height) {
        int cell = l.pool_width / l.filter_columns;
        int col = (x - l.pool_x) / cell;
        int row = (y - l.filters_y) / l.filter_height;
        if (col < l.filter_columns) { *index = row * l.filter_columns + col; return FE8_INVENTORY_HIT_FILTER; }
    }
    if (y >= l.table_y - 26 && y < l.table_y) {
        static const int sorts[4] = {FE8_INVENTORY_SORT_NAME, FE8_INVENTORY_SORT_TYPE,
            FE8_INVENTORY_SORT_USES, FE8_INVENTORY_SORT_OWNER};
        for (int col = 0; col < 4; ++col)
            if (l.column_width[col] && x >= l.column_x[col] && x < l.column_x[col] + l.column_width[col]) {
                *index = sorts[col]; return FE8_INVENTORY_HIT_SORT_COLUMN;
            }
    }
    if (y >= l.deposit_y) {
        if (x >= l.pool_x + l.pool_width - 100 && (ui->query[0] || ui->type_filter || ui->usable_only)) return FE8_INVENTORY_HIT_RESET;
        *index = deposit_index(ui);
        return *index >= 0 ? FE8_INVENTORY_HIT_POOL_ITEM : FE8_INVENTORY_HIT_NONE;
    }
    n = fe8_inventory_desktop_visible(ui, s, visible);
    if (y >= l.table_y && y < l.table_y + l.table_rows * l.row_height) {
        int row = offset(ui->pool_scroll, n, l.table_rows) + (y - l.table_y) / l.row_height;
        if (row < n) {
            *index = visible[row];
            return x >= l.quick_x ? FE8_INVENTORY_HIT_QUICK_POOL : FE8_INVENTORY_HIT_POOL_ITEM;
        }
    }
    return FE8_INVENTORY_HIT_NONE;
}
void fe8_inventory_desktop_scroll(Fe8InventoryUi *ui, const Fe8InventorySnapshot *s,
    int width, int height, int x, int rows) {
    Fe8InventoryDesktopLayout l;
    if (!ui || !s) return;
    fe8_inventory_desktop_layout(ui, width, height, &l);
    x = (int)(x / fe8_inventory_desktop_scale(ui, width, height));
    if (x >= l.pool_x && x < l.pool_x + l.pool_width) {
        int n = fe8_inventory_desktop_visible(ui, s, NULL);
        ui->pool_scroll = offset(offset(ui->pool_scroll, n, l.table_rows) + clamp(rows,-10000,10000), n, l.table_rows);
    } else if (x >= PAD && x < PAD + l.sidebar)
        ui->roster_scroll = offset(offset(ui->roster_scroll, s->unit_count, l.roster_rows) + clamp(rows,-10000,10000), s->unit_count, l.roster_rows);
    clear_hover(ui);
}
void fe8_inventory_desktop_scroll_at(Fe8InventoryUi *ui, const Fe8InventorySnapshot *s,
    int width, int height, int x, int y, int rows) {
    Fe8InventoryDesktopLayout l;
    float scale;
    if (!ui || !s) return;
    fe8_inventory_desktop_layout(ui,width,height,&l);
    scale = fe8_inventory_desktop_scale(ui,width,height);
    if (in((int)(x/scale),(int)(y/scale),l.detail_x,l.detail_y,l.detail_width,l.detail_height)) {
        ui->detail_scroll = clamp(ui->detail_scroll + clamp(rows,-100,100),0,24);
    } else fe8_inventory_desktop_scroll(ui,s,width,height,x,rows);
}

static int pinned(const Fe8InventoryUi *ui, const Fe8InventorySnapshot *s, Fe8InventoryEndpoint *e) {
    if (ui->has_selection) *e = ui->selected;
    else if (ui->has_detail) *e = ui->detail;
    else return 0;
    return info_at(s,*e) && fe8_inventory_ui_endpoint_item(s,*e) != 0;
}
int fe8_inventory_desktop_click(Fe8InventoryUi *ui, const Fe8InventorySnapshot *s,
    Fe8InventoryHitKind *kind, int *index) {
    Fe8InventoryEndpoint e;
    const Fe8InventoryUnit *u;
    int valid;
    if (!ui || !s || !kind || !index || !ui->desktop) return 0;
    ui->search_active = *kind == FE8_INVENTORY_HIT_SEARCH;
    u = target(ui,s);
    switch (*kind) {
    case FE8_INVENTORY_HIT_QUICK_POOL:
    case FE8_INVENTORY_HIT_QUICK_UNIT: {
        Fe8InventoryHitKind source_kind = *kind == FE8_INVENTORY_HIT_QUICK_POOL ?
            FE8_INVENTORY_HIT_POOL_ITEM : FE8_INVENTORY_HIT_UNIT_ITEM;
        if ((source_kind == FE8_INVENTORY_HIT_POOL_ITEM && (*index < 0 || *index >= ui->pool_count)) ||
            (source_kind == FE8_INVENTORY_HIT_UNIT_ITEM && (!u || *index < 0 || *index >= 5))) return 1;
        e = fe8_inventory_ui_endpoint(ui,s,source_kind,*index);
        if (!fe8_inventory_ui_endpoint_item(s,e)) return 1;
        fe8_inventory_desktop_cancel_drag(ui);
        ui->has_selection = 0;
        ui->detail = e; ui->has_detail = 1; ui->detail_scroll = 0;
        *kind = u && e.kind == FE8_INVENTORY_ENDPOINT_UNIT && e.unit_address == u->address ?
            FE8_INVENTORY_HIT_STORE : FE8_INVENTORY_HIT_GIVE;
        return fe8_inventory_desktop_click(ui,s,kind,index);
    }
    case FE8_INVENTORY_HIT_SEARCH: return 1;
    case FE8_INVENTORY_HIT_UNIT_NAME:
    case FE8_INVENTORY_HIT_UNIT_CLASS:
        if (!ui->has_selection) {
            ui->has_detail = 0;
            ui->detail_scroll = 0;
            fe8_inventory_ui_inspect(ui,s,*kind,*index);
        }
        return 1;
    case FE8_INVENTORY_HIT_FILTER:
        if (*index >= 0 && *index < 10) ui->type_filter = *index;
        changed_view(ui); return 1;
    case FE8_INVENTORY_HIT_USABLE:
        ui->usable_only = !ui->usable_only; changed_view(ui); return 1;
    case FE8_INVENTORY_HIT_RESET:
        ui->query[0] = 0; ui->type_filter = 0; ui->usable_only = 0; changed_view(ui); return 1;
    case FE8_INVENTORY_HIT_CANCEL:
        fe8_inventory_desktop_cancel_drag(ui);
        ui->has_selection = 0;
        snprintf(ui->status,sizeof(ui->status),"Move cancelled. No items changed."); return 1;
    case FE8_INVENTORY_HIT_SORT_COLUMN:
        if (*index >= 0 && *index < FE8_INVENTORY_SORT_COUNT) {
            ui->sort_descending = ui->pool_sort == (Fe8InventorySort)*index ? !ui->sort_descending : 0;
            ui->pool_sort = (Fe8InventorySort)*index;
            changed_view(ui); fe8_inventory_ui_rebuild(ui,s);
        }
        return 1;
    case FE8_INVENTORY_HIT_POOL_SORT:
        ui->sort_descending = 0; fe8_inventory_ui_cycle_sort(ui,s); return 1;
    case FE8_INVENTORY_HIT_MOVE:
    case FE8_INVENTORY_HIT_GIVE:
    case FE8_INVENTORY_HIT_STORE:
        valid = pinned(ui,s,&e);
        if (!valid || !fe8_inventory_ui_endpoint_movable(s,e)) {
            ui->has_selection = 0;
            snprintf(ui->status,sizeof(ui->status),"%s",valid ? "This is fixed equipment and cannot be moved." : "Select an item to see its actions.");
            return 1;
        }
        if (*kind == FE8_INVENTORY_HIT_MOVE) {
            ui->selected = e; ui->has_selection = 1;
            snprintf(ui->status,sizeof(ui->status),"Choose an empty slot to move, or an occupied slot to swap.");
            return 1;
        }
        if (*kind == FE8_INVENTORY_HIT_GIVE) {
            int slot = free_slot(u);
            if (!u || (e.kind == FE8_INVENTORY_ENDPOINT_UNIT && e.unit_address == u->address)) {
                ui->has_selection = 0;
                snprintf(ui->status,sizeof(ui->status),"%s",!u ? "Choose a recipient first." : "This item is already in this loadout.");
                return 1;
            }
            if (slot < 0) {
                ui->selected = e; ui->has_selection = 1;
                snprintf(ui->status,sizeof(ui->status),"%s is full. Click a loadout slot to swap, or Esc to cancel.",u->name);
                return 1;
            }
            *kind = FE8_INVENTORY_HIT_UNIT_ITEM; *index = slot;
        } else {
            int dest = deposit_index(ui);
            if (dest < 0 || e.kind == FE8_INVENTORY_ENDPOINT_SUPPLY) {
                ui->has_selection = 0;
                snprintf(ui->status,sizeof(ui->status),"%s",dest < 0 ? "Supply is full. Use Move / swap to exchange items." : "This item is already in supply."); return 1;
            }
            *kind = FE8_INVENTORY_HIT_POOL_ITEM; *index = dest;
        }
        ui->selected = e; ui->has_selection = 1;
        return 0; /* Explicit action, existing guarded memory transaction. */
    case FE8_INVENTORY_HIT_POOL_ITEM:
    case FE8_INVENTORY_HIT_UNIT_ITEM:
        if ((*kind == FE8_INVENTORY_HIT_POOL_ITEM && (*index < 0 || *index >= ui->pool_count)) ||
            (*kind == FE8_INVENTORY_HIT_UNIT_ITEM && (!u || *index < 0 || *index >= 5))) return 1;
        e = fe8_inventory_ui_endpoint(ui,s,*kind,*index);
        if (ui->has_selection) {
            if (same(e,ui->selected)) {
                ui->has_selection = 0;
                snprintf(ui->status,sizeof(ui->status),"Move cancelled. No items changed.");
                return 1;
            }
            return 0;
        }
        if (fe8_inventory_ui_endpoint_item(s,e)) {
            ui->detail = e; ui->has_detail = 1; ui->detail_scroll = 0;
            snprintf(ui->status,sizeof(ui->status),"%s · Double-click to transfer, or drag to a slot, ally, or supply.",info_at(s,e)->name);
        } else snprintf(ui->status,sizeof(ui->status),"Drag an item here, or use Give beside an item.");
        return 1;
    case FE8_INVENTORY_HIT_ROSTER:
    case FE8_INVENTORY_HIT_ROSTER_CLASS:
        if (*index >= 0 && *index < s->unit_count) {
            changed_view(ui); ui->detail_scroll = 0;
            if (ui->has_selection) {
                ui->current_unit = *index;
                *kind = FE8_INVENTORY_HIT_GIVE;
                return fe8_inventory_desktop_click(ui,s,kind,index);
            }
        }
        return 0;
    default: return 0;
    }
}

void fe8_inventory_desktop_cancel_drag(Fe8InventoryUi *ui) {
    if (!ui) return;
    if (ui->dragging) ui->has_selection = 0;
    ui->drag_armed = ui->dragging = 0;
    ui->drag_hover_kind = FE8_INVENTORY_HIT_NONE;
    ui->drag_hover_index = -1;
}

void fe8_inventory_desktop_pointer_down(Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *s, Fe8InventoryHitKind kind, int index, int x, int y) {
    Fe8InventoryEndpoint e;
    if (!ui || !s || !ui->active || !ui->desktop) return;
    fe8_inventory_desktop_cancel_drag(ui);
    if (ui->has_selection) return; /* Click-move mode handles the destination. */
    if (kind == FE8_INVENTORY_HIT_POOL_ITEM) {
        if (index < 0 || index >= ui->pool_count) return;
    } else if (kind == FE8_INVENTORY_HIT_UNIT_ITEM) {
        if (!target(ui,s) || index < 0 || index >= FE8_INVENTORY_ITEM_SLOTS) return;
    } else return;
    e = fe8_inventory_ui_endpoint(ui,s,kind,index);
    if (!fe8_inventory_ui_endpoint_item(s,e) || !fe8_inventory_ui_endpoint_movable(s,e)) return;
    ui->drag_source = e;
    ui->drag_item = fe8_inventory_ui_endpoint_item(s,e);
    ui->drag_armed = 1;
    ui->drag_start_x = ui->drag_x = x;
    ui->drag_start_y = ui->drag_y = y;
}

void fe8_inventory_desktop_pointer_motion(Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *s, int width, int height, int x, int y) {
    float scale;
    if (!ui || !s || !ui->drag_armed) return;
    scale = fe8_inventory_desktop_scale(ui,width,height);
    ui->drag_x = x; ui->drag_y = y;
    if (!ui->dragging && (x - ui->drag_start_x >= 5 * scale ||
            ui->drag_start_x - x >= 5 * scale || y - ui->drag_start_y >= 5 * scale ||
            ui->drag_start_y - y >= 5 * scale)) {
        ui->dragging = 1;
        ui->selected = ui->drag_source; ui->has_selection = 1;
        snprintf(ui->status,sizeof(ui->status),"Drop on a slot to move/swap, an ally to give, or supply to store. Esc cancels.");
    }
    if (ui->dragging) ui->drag_hover_kind = fe8_inventory_desktop_hit(ui,s,width,height,x,y,&ui->drag_hover_index);
}

int fe8_inventory_desktop_pointer_up(Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *s, Fe8InventoryHitKind *kind, int *index) {
    int dragging;
    if (!ui || !s || !kind || !index) return 1;
    dragging = ui->dragging;
    ui->drag_armed = ui->dragging = 0;
    ui->drag_hover_kind = FE8_INVENTORY_HIT_NONE;
    ui->drag_hover_index = -1;
    if (!dragging) return 1; /* Ordinary click was already handled on down. */
    if (fe8_inventory_ui_endpoint_item(s,ui->drag_source) != ui->drag_item) {
        ui->has_selection = 0;
        snprintf(ui->status,sizeof(ui->status),"Move cancelled: the source item changed.");
        return 1;
    }
    if ((*kind == FE8_INVENTORY_HIT_POOL_ITEM && (*index < 0 || *index >= ui->pool_count)) ||
        (*kind == FE8_INVENTORY_HIT_UNIT_ITEM && (!target(ui,s) || *index < 0 || *index >= FE8_INVENTORY_ITEM_SLOTS)) ||
        ((*kind == FE8_INVENTORY_HIT_ROSTER || *kind == FE8_INVENTORY_HIT_ROSTER_CLASS) &&
            (*index < 0 || *index >= s->unit_count))) {
        ui->has_selection = 0;
        snprintf(ui->status,sizeof(ui->status),"Move cancelled: the destination is unavailable.");
        return 1;
    }
    if (*kind == FE8_INVENTORY_HIT_UNIT_NAME || *kind == FE8_INVENTORY_HIT_UNIT_CLASS) {
        *kind = FE8_INVENTORY_HIT_ROSTER; *index = ui->current_unit;
    }
    if (*kind == FE8_INVENTORY_HIT_UNIT_ITEM || *kind == FE8_INVENTORY_HIT_POOL_ITEM ||
        *kind == FE8_INVENTORY_HIT_ROSTER || *kind == FE8_INVENTORY_HIT_ROSTER_CLASS) {
        if (*kind == FE8_INVENTORY_HIT_UNIT_ITEM || *kind == FE8_INVENTORY_HIT_POOL_ITEM) {
            Fe8InventoryEndpoint e = fe8_inventory_ui_endpoint(ui,s,*kind,*index);
            if (!fe8_inventory_ui_endpoint_movable(s,e)) {
                ui->has_selection = 0;
                snprintf(ui->status,sizeof(ui->status),"Fixed equipment cannot be replaced. No items changed.");
                return 1;
            }
        }
        return fe8_inventory_desktop_click(ui,s,kind,index);
    }
    ui->has_selection = 0;
    snprintf(ui->status,sizeof(ui->status),"Move cancelled. Drop on a slot, ally, or supply.");
    return 1;
}

/* Painting helpers. All primitives clip to the supplied framebuffer, including
   non-packed strides and fractional display densities. */
static uint32_t abgr(uint32_t c) { return (c & 0xFF00FF00) | ((c & 0xFF0000) >> 16) | ((c & 0xFF) << 16); }
static int px(const Painter *p, int v) { return (int)(v * p->scale + 0.5f); }
static void fill(Painter *p,int x,int y,int w,int h,uint32_t color) {
    int x0=clamp(px(p,x),0,p->width),y0=clamp(px(p,y),0,p->height);
    int x1=clamp(px(p,x+w),0,p->width),y1=clamp(px(p,y+h),0,p->height);
    uint32_t c=abgr(color);
    for (int yy=y0;yy<y1;++yy) for(int xx=x0;xx<x1;++xx) p->pixels[yy*p->stride+xx]=c;
}
static void card(Painter *p,int x,int y,int w,int h,uint32_t c) {
    if(w<=0||h<=0)return;
    fill(p,x+3,y,w-6,h,c); fill(p,x,y+3,w,h-6,c);
    fill(p,x+1,y+1,w-2,h-2,c);
}
static void border(Painter *p,int x,int y,int w,int h,uint32_t c) {
    fill(p,x,y,w,1,c);fill(p,x,y+h-1,w,1,c);fill(p,x,y,1,h,c);fill(p,x+w-1,y,1,h,c);
}
static void label(Painter *p,int x,int y,int w,int h,const char *s,uint32_t c,float size,int bold,int wrap) {
    if(w<=0||h<=0)return;
    fe8_host_text_draw(&p->text,px(p,x),px(p,y),px(p,x+w)-px(p,x),px(p,y+h)-px(p,y),
        s,size*p->scale,abgr(c),bold?FE8_HOST_TEXT_SEMIBOLD:FE8_HOST_TEXT_REGULAR,wrap);
}
static void button(Painter *p,int x,int y,int w,int h,const char *s,int active,int enabled) {
    card(p,x,y,w,h,active&&enabled?SELECTED:RAISED);
    if(active&&enabled)fill(p,x,y+h-2,w,2,ACCENT);
    label(p,x+10,y+(h-16)/2,w-20,20,s,enabled?(active?ACCENT:TEXT):MUTED,12,active,0);
}
static void badge(Painter *p,int x,int y,int w,const char *s,uint32_t c) {
    card(p,x,y,w,20,RAISED);label(p,x+6,y+2,w-12,16,s,c,11,1,0);
}
static void durability(Painter *p,int x,int y,int w,uint16_t encoded,const Fe8ItemInfo *i) {
    if(!i||!encoded||!i->max_uses||(i->attributes&8))return;
    int remaining=encoded>>8;
    fill(p,x,y,w,2,LINE);fill(p,x,y,w*clamp(remaining,0,i->max_uses)/i->max_uses,2,
        remaining*4<=i->max_uses?WARN:MUTED);
}
static void uses(char *out,size_t n,uint16_t item,const Fe8ItemInfo *i) {
    if(i&&(i->attributes&8))snprintf(out,n,"--");
    else snprintf(out,n,"%u/%u",item>>8,i?i->max_uses:0);
}
static const char *use_label(const Fe8InventoryUnit *u,const Fe8ItemInfo *i,uint32_t *c) {
    *c=MUTED;
    if(!i)return "Unknown";
    if(!i->movable)return "Fixed";
    if(!u)return "Select ally";
    switch(fe8_inventory_item_use_state(u,i)) {
    case FE8_INVENTORY_USE_READY:*c=ACCENT;return "Ready";
    case FE8_INVENTORY_USE_RANK:*c=WARN;return "Rank";
    case FE8_INVENTORY_USE_LOCKED:*c=DANGER;return "Locked";
    case FE8_INVENTORY_USE_STATUS:*c=WARN;return "Status";
    case FE8_INVENTORY_USE_UNKNOWN:*c=WARN;return "Unknown";
    default:return "Item";
    }
}
static void portrait(Painter *p,const Fe8InventoryUnit *u,int x,int y,int w,int h) {
    card(p,x,y,w,h,RAISED);
    if(!u||!u->portrait_valid) {
        char initial[2]={u&&u->name[0]?u->name[0]:'?',0};
        label(p,x+w/3,y+h/4,w/2,h/2,initial,MUTED,h/3.0f,1,0);return;
    }
    int x0=px(p,x),y0=px(p,y),ww=px(p,x+w)-x0,hh=px(p,y+h)-y0;
    for(int yy=0;yy<hh;++yy)for(int xx=0;xx<ww;++xx) {
        int dx=x0+xx,dy=y0+yy;
        if(dx<0||dy<0||dx>=p->width||dy>=p->height)continue;
        unsigned idx=u->portrait[(yy*FE8_PORTRAIT_HEIGHT/hh)*FE8_PORTRAIT_WIDTH+xx*FE8_PORTRAIT_WIDTH/ww];
        if(idx&&idx<FE8_PORTRAIT_PALETTE_SIZE)p->pixels[dy*p->stride+dx]=u->portrait_palette[idx];
    }
}
static void scroll_mark(Painter *p,int x,int y,int h,int count,int rows,int start) {
    if(count<=rows||rows<=0||h<=0)return;
    int thumb=clamp(h*rows/count,12,h);
    fill(p,x,y,3,h,LINE);
    fill(p,x,y+(h-thumb)*start/(count-rows),3,thumb,MUTED);
}
static void draw_sidebar(Painter *p,const Fe8InventoryUi *ui,const Fe8InventorySnapshot *s,
    const Fe8InventoryDesktopLayout *l) {
    const Fe8InventoryUnit *u=target(ui,s);
    char b[160];
    card(p,PAD,l->top,l->sidebar,l->bottom-l->top,PANEL);
    if(u) {
        portrait(p,u,PAD+8,l->top+6,80,72);
        label(p,PAD+94,l->top+6,l->sidebar-104,24,u->name,TEXT,19,1,0);
        label(p,PAD+94,l->top+30,l->sidebar-104,18,u->class_name,MUTED,12,0,0);
        snprintf(b,sizeof(b),"Lv %u    HP %u/%u",u->level,u->hp,u->max_hp);
        label(p,PAD+94,l->top+53,l->sidebar-104,18,b,ACCENT,11,0,0);
        int x=PAD+10;
        for(int t=0;t<8;++t)if(u->ranks[t]&&x<PAD+l->sidebar-34) {
            snprintf(b,sizeof(b),"%.2s %c",TYPES[t+1],rank_letter(u->ranks[t]));
            badge(p,x,l->top+80,44,b,TYPE_COLORS[t+1]);x+=48;
        }
        if(x==PAD+10)label(p,x,l->top+82,l->sidebar-20,16,"No weapon ranks",MUTED,11,0,0);
        snprintf(b,sizeof(b),"LOADOUT   %d / 5",occupied(u));
        label(p,PAD+10,l->items_y-16,l->sidebar-20,16,b,MUTED,10,1,0);
        for(int j=0;j<5;++j) {
            int y=l->items_y+j*l->side_row_height;
            Fe8InventoryEndpoint e={FE8_INVENTORY_ENDPOINT_UNIT,u->address,(unsigned)j};
            int selected=ui->has_selection&&same(ui->selected,e);
            int inspected=ui->has_detail&&same(ui->detail,e);
            card(p,PAD+6,y+1,l->sidebar-18,l->side_row_height-2,selected?SELECTED:inspected?RAISED:PANEL);
            if(selected||inspected)fill(p,PAD+6,y+3,2,l->side_row_height-6,ACCENT);
            if(ui->dragging && ui->drag_hover_kind==FE8_INVENTORY_HIT_UNIT_ITEM && ui->drag_hover_index==j)
                border(p,PAD+6,y+1,l->sidebar-18,l->side_row_height-2,
                    !u->items[j]||u->item_info[j].movable?ACCENT:DANGER);
            snprintf(b,sizeof(b),"%d",j+1);
            label(p,PAD+12,y+5,18,16,b,MUTED,10,0,0);
            if(u->items[j]) {
                const Fe8ItemInfo *i=&u->item_info[j];
                uint32_t c; const char *ready=use_label(u,i,&c); (void)ready;
                label(p,PAD+32,y+4,l->sidebar-98,22,i->name,TEXT,12,inspected,0);
                uses(b,sizeof(b),u->items[j],i);
                label(p,PAD+l->sidebar-62,y+5,48,18,b,!i->movable?MUTED:c,10,0,0);
                durability(p,PAD+32,y+l->side_row_height-4,l->sidebar-100,u->items[j],i);
            } else {
                label(p,PAD+32,y+4,l->sidebar-56,20,ui->has_selection?"+ Place item here":"+ Empty slot",ui->has_selection?ACCENT:MUTED,12,0,0);
            }
        }
    } else label(p,PAD+16,l->top+20,l->sidebar-32,80,"No allies available",MUTED,16,1,1);
    snprintf(b,sizeof(b),"RECIPIENTS   %u",s->unit_count);
    label(p,PAD+10,l->roster_y-23,l->sidebar-20,18,b,MUTED,10,1,0);
    int start=offset(ui->roster_scroll,s->unit_count,l->roster_rows);
    Fe8InventoryEndpoint e; const Fe8ItemInfo *selected_info=pinned(ui,s,&e)?info_at(s,e):NULL;
    for(int row=0;row<l->roster_rows&&start+row<s->unit_count;++row) {
        int idx=start+row,y=l->roster_y+row*l->side_row_height;
        const Fe8InventoryUnit *r=&s->units[idx];
        if(idx==ui->current_unit){card(p,PAD+6,y,l->sidebar-18,l->side_row_height-2,SELECTED);fill(p,PAD+6,y+3,2,l->side_row_height-8,ACCENT);}
        if(ui->dragging && (ui->drag_hover_kind==FE8_INVENTORY_HIT_ROSTER || ui->drag_hover_kind==FE8_INVENTORY_HIT_ROSTER_CLASS) && ui->drag_hover_index==idx)
            border(p,PAD+6,y,l->sidebar-18,l->side_row_height-2,ACCENT);
        int split=PAD+l->sidebar*42/100;
        label(p,PAD+12,y+4,split-PAD-16,20,r->name,idx==ui->current_unit?ACCENT:TEXT,12,idx==ui->current_unit,0);
        if(selected_info) {
            uint32_t c;const char *state=use_label(r,selected_info,&c);
            snprintf(b,sizeof(b),"%s  %d/5",state,occupied(r));
            label(p,split,y+4,l->sidebar*58/100-12,18,b,c,11,0,0);
        } else label(p,split,y+5,l->sidebar*58/100-12,18,r->class_name,MUTED,10,0,0);
    }
    scroll_mark(p,PAD+l->sidebar-6,l->roster_y,l->roster_rows*l->side_row_height,s->unit_count,l->roster_rows,start);
}
static void draw_pool(Painter *p,const Fe8InventoryUi *ui,const Fe8InventorySnapshot *s,
    const Fe8InventoryDesktopLayout *l) {
    char b[160];
    int visible[FE8_INVENTORY_POOL_CAPACITY];
    int n=fe8_inventory_desktop_visible(ui,s,visible),start=offset(ui->pool_scroll,n,l->table_rows);
    const Fe8InventoryUnit *u=target(ui,s);
    card(p,l->pool_x,l->top,l->pool_width,l->bottom-l->top,PANEL);
    button(p,l->pool_x,l->top,106,28,ui->pool_scope==FE8_INVENTORY_POOL_ALL?"All items":"Supply only",1,1);
    snprintf(b,sizeof(b),"Sort: %s",fe8_inventory_ui_sort_name(ui->pool_sort));
    button(p,l->pool_x+112,l->top,104,28,b,0,1);
    button(p,l->pool_x+l->pool_width-104,l->top,96,28,ui->comfortable?"Comfy  D":"Compact  D",0,1);
    card(p,l->pool_x,l->search_y,l->pool_width-116,30,RAISED);
    if(ui->search_active)border(p,l->pool_x,l->search_y,l->pool_width-116,30,ACCENT);
    snprintf(b,sizeof(b),"%s%s",ui->query[0]?ui->query:"Search items, owners...",ui->search_active?" |":"");
    label(p,l->pool_x+10,l->search_y+6,l->pool_width-148,20,b,ui->query[0]?TEXT:MUTED,12,0,0);
    button(p,l->pool_x+l->pool_width-112,l->search_y,104,30,"Ready only",ui->usable_only,1);
    int cell=l->pool_width/l->filter_columns;
    for(int i=0;i<10;++i) {
        int x=l->pool_x+i%l->filter_columns*cell,y=l->filters_y+i/l->filter_columns*l->filter_height;
        int active=ui->type_filter==i;
        if(active)card(p,x,y,cell-4,23,SELECTED);
        label(p,x+7,y+4,cell-12,18,TYPES[i],active?ACCENT:MUTED,11,active,0);
    }
    static const char *const heads[]={"Item","Type","Uses","Owner","Rk","Mt","Hit","Crit","Wt","Rng","Use"};
    static const int sorts[]={FE8_INVENTORY_SORT_NAME,FE8_INVENTORY_SORT_TYPE,FE8_INVENTORY_SORT_USES,FE8_INVENTORY_SORT_OWNER};
    fill(p,l->pool_x,l->table_y-26,l->pool_width,26,RAISED);
    for(int col=0;col<11;++col)if(l->column_width[col]) {
        snprintf(b,sizeof(b),"%s%s",heads[col],col<4&&ui->pool_sort==(Fe8InventorySort)sorts[col]?((ui->sort_descending ^ (ui->pool_sort == FE8_INVENTORY_SORT_USES))?" ↓":" ↑"):"");
        label(p,l->column_x[col]+7,l->table_y-20,l->column_width[col]-10,18,b,MUTED,10,1,0);
    }
    label(p,l->quick_x+5,l->table_y-20,l->quick_width-8,18,"Move",MUTED,10,1,0);
    for(int row=0;row<l->table_rows&&start+row<n;++row) {
        const Fe8InventoryListEntry *e=&ui->pool[visible[start+row]];
        const Fe8ItemInfo *i=e->info;
        int y=l->table_y+row*l->row_height;
        int selected=ui->has_selection&&same(ui->selected,e->endpoint);
        int inspected=ui->has_detail&&same(ui->detail,e->endpoint);
        int hovered=ui->has_inspected&&same(ui->inspected,e->endpoint);
        if(selected||inspected||hovered)fill(p,l->pool_x,y,l->pool_width-GUTTER,l->row_height,selected?SELECTED:RAISED);
        else if(row%2)fill(p,l->pool_x,y,l->pool_width-GUTTER,l->row_height,0xFF1B2330);
        if(selected||inspected)fill(p,l->pool_x,y+2,2,l->row_height-4,ACCENT);
        int text_y=y+(l->row_height-18)/2;
        fill(p,l->column_x[0]+9,text_y+5,4,8,TYPE_COLORS[type_key(i)]);
        label(p,l->column_x[0]+21,text_y,l->column_width[0]-26,20,i?i->name:"Unknown item",TEXT,12,inspected,0);
        uint32_t use_color;const char *state=use_label(u,i,&use_color);
        for(int col=1;col<11;++col)if(l->column_width[col]) {
            uint32_t c=MUTED;
            b[0]=0;
            switch(col) {
            case 1:snprintf(b,sizeof(b),"%s",TYPES[type_key(i)]);c=TYPE_COLORS[type_key(i)];break;
            case 2:uses(b,sizeof(b),e->item,i);c=(i&&i->max_uses&&(e->item>>8)*4<=i->max_uses&&!(i->attributes&8))?WARN:TEXT;break;
            case 3:snprintf(b,sizeof(b),"%s",e->unit_index>=0?s->units[e->unit_index].name:"Supply");break;
            case 4:snprintf(b,sizeof(b),"%c",i?rank_letter(i->weapon_rank):'-');break;
            case 5:snprintf(b,sizeof(b),"%u",i?i->might:0);break;
            case 6:snprintf(b,sizeof(b),"%u",i?i->hit:0);break;
            case 7:snprintf(b,sizeof(b),"%u",i?i->crit:0);break;
            case 8:snprintf(b,sizeof(b),"%u",i?i->weight:0);break;
            case 9:snprintf(b,sizeof(b),"%u-%u",i?i->min_range:0,i?i->max_range:0);break;
            case 10:snprintf(b,sizeof(b),"%s",state);c=use_color;break;
            }
            label(p,l->column_x[col]+7,text_y,l->column_width[col]-10,20,b,c,11,0,0);
        }
        int own=u && e->endpoint.kind==FE8_INVENTORY_ENDPOINT_UNIT && e->endpoint.unit_address==u->address;
        int enabled=i && i->movable && (own?deposit_index(ui)>=0:u!=NULL);
        const char *action=!i||!i->movable?"Fixed":own?(deposit_index(ui)<0?"Full":"Store"):u&&free_slot(u)<0?"Swap":"Give";
        card(p,l->quick_x+2,y+2,l->quick_width-6,l->row_height-4,enabled?SELECTED:RAISED);
        label(p,l->quick_x+8,text_y,l->quick_width-14,20,action,enabled?ACCENT:MUTED,11,1,0);
        if(ui->dragging && ui->drag_hover_kind==FE8_INVENTORY_HIT_POOL_ITEM && ui->drag_hover_index==visible[start+row])
            border(p,l->pool_x,y,l->pool_width-GUTTER,l->row_height,i&&i->movable?ACCENT:DANGER);
        durability(p,l->column_x[2]+7,y+l->row_height-4,l->column_width[2]-14,e->item,i);
    }
    if(!n) {
        label(p,l->pool_x+18,l->table_y+12,l->pool_width-36,30,"No matching items",TEXT,18,1,0);
        label(p,l->pool_x+18,l->table_y+46,l->pool_width-36,70,"Try another type, turn off Ready only, or clear your search. Equipment stays unchanged while you browse.",MUTED,12,0,1);
    }
    scroll_mark(p,l->pool_x+l->pool_width-5,l->table_y,l->table_rows*l->row_height,n,l->table_rows,start);
    fill(p,l->pool_x,l->deposit_y,l->pool_width,1,LINE);
    int empty=deposit_index(ui);
    if(ui->dragging && empty>=0 && ui->drag_hover_kind==FE8_INVENTORY_HIT_POOL_ITEM && ui->drag_hover_index==empty)
        border(p,l->pool_x,l->deposit_y,l->pool_width,30,ACCENT);
    snprintf(b,sizeof(b),"%d shown   ·   %s",n,empty>=0?(ui->has_selection?"+ Place in supply":"Supply has space"):"Supply full");
    label(p,l->pool_x+10,l->deposit_y+8,l->pool_width-114,20,b,ui->has_selection&&empty>=0?ACCENT:MUTED,11,0,0);
    if(ui->query[0]||ui->type_filter||ui->usable_only)
        label(p,l->pool_x+l->pool_width-96,l->deposit_y+8,88,20,"Clear filters",ACCENT,11,1,0);
}
static void description(Painter *p,int x,int y,int w,int h,const char *value,int scroll) {
    /* A real clipped scroll viewport: long ROM descriptions and biographies
       remain readable instead of being silently truncated at small sizes. */
    Fe8HostTextCanvas c;
    int xx=clamp(px(p,x),0,p->width),yy=clamp(px(p,y),0,p->height);
    int ww=clamp(px(p,x+w),xx,p->width)-xx,hh=clamp(px(p,y+h),yy,p->height)-yy;
    if(ww<=0||hh<=0||!value)return;
    if(fe8_host_text_begin(&c,p->pixels+yy*p->stride+xx,p->stride,ww,hh)) {
        int dy=px(p,scroll*12);
        fe8_host_text_draw(&c,0,-dy,ww,hh+dy,value,12*p->scale,abgr(MUTED),FE8_HOST_TEXT_REGULAR,1);
        fe8_host_text_end(&c);
    }
}
static void draw_detail(Painter *p,const Fe8InventoryUi *ui,const Fe8InventorySnapshot *s,
    const Fe8InventoryDesktopLayout *l) {
    Fe8InventoryEndpoint e={FE8_INVENTORY_ENDPOINT_SUPPLY,0,0};
    int actionable=pinned(ui,s,&e);
    if(!actionable&&ui->has_inspected)e=ui->inspected;
    const Fe8ItemInfo *i=info_at(s,e);
    uint16_t encoded=i?fe8_inventory_ui_endpoint_item(s,e):0;
    const Fe8InventoryUnit *u=target(ui,s);
    const char *unit_title=NULL,*unit_help=fe8_inventory_ui_unit_help(ui,s,&unit_title);
    char b[256];
    int x=l->detail_x+16,y=l->detail_y+12;
    int text_width=l->detail_wide?l->detail_width-32:l->action_x-x-16;
    card(p,l->detail_x,l->detail_y,l->detail_width,l->detail_height,PANEL);
    if(!i||!encoded) {
        label(p,x,y,text_width,20,unit_help?unit_title:"Item details",TEXT,l->detail_wide?18:15,1,0);
        description(p,x,y+32,text_width,l->detail_wide?180:l->detail_height-66,
            unit_help?unit_help:"Select an item to inspect its stats, compare it with the recipient's loadout, and choose where it goes. Browsing never moves equipment.",ui->detail_scroll);
    } else {
        uint32_t state_color;const char *state=use_label(u,i,&state_color);
        if(l->detail_wide) {
            label(p,x,y,text_width,16,ui->has_selection?"CHOOSE A DESTINATION":"ITEM INSPECTOR",ui->has_selection?ACCENT:MUTED,10,1,0);
            label(p,x,y+28,text_width,32,i->name,TEXT,23,1,0);
            snprintf(b,sizeof(b),"%s   ·   Rank %c",TYPES[type_key(i)],rank_letter(i->weapon_rank));
            label(p,x,y+66,text_width,20,b,TYPE_COLORS[type_key(i)],12,0,0);
            uses(b,sizeof(b),encoded,i);
            char subtitle[96];snprintf(subtitle,sizeof(subtitle),"%.16s uses   ·   %s",b,(i->attributes&8)?"Unbreakable":"Durability");
            label(p,x,y+92,text_width,20,subtitle,MUTED,12,0,0);
            durability(p,x,y+120,text_width,encoded,i);
            if(i->attributes&5) {
                const char *names[]={"MIGHT","HIT","CRIT","WEIGHT","RANGE","RANK"};
                for(int t=0;t<6;++t) {
                    int cx=x+(t%3)*(text_width/3),cy=y+140+(t/3)*64;
                    card(p,cx,cy,text_width/3-6,56,RAISED);
                    label(p,cx+9,cy+8,text_width/3-18,16,names[t],MUTED,9,1,0);
                    switch(t) {
                    case 0:snprintf(b,sizeof(b),"%u",i->might);break;
                    case 1:snprintf(b,sizeof(b),"%u",i->hit);break;
                    case 2:snprintf(b,sizeof(b),"%u",i->crit);break;
                    case 3:snprintf(b,sizeof(b),"%u",i->weight);break;
                    case 4:snprintf(b,sizeof(b),"%u-%u",i->min_range,i->max_range);break;
                    default:snprintf(b,sizeof(b),"%c",rank_letter(i->weapon_rank));break;
                    }
                    label(p,cx+9,cy+26,text_width/3-18,26,b,TEXT,19,1,0);
                }
            }
            int info_y=y+276;
            snprintf(b,sizeof(b),"%s%s%s",state,u?" for ":"",u?u->name:"");
            label(p,x,info_y,text_width,24,b,state_color,14,1,0);
            if(u&&(i->attributes&5)&&i->weapon_type<8) {
                if (i->lock_kind == FE8_ITEM_LOCK_CHARACTER)
                    snprintf(b,sizeof(b),"Personal weapon · character restriction");
                else if (i->lock_kind == FE8_ITEM_LOCK_CLASS)
                    snprintf(b,sizeof(b),"Class-restricted weapon");
                else if (i->lock_kind == FE8_ITEM_LOCK_UNKNOWN)
                    snprintf(b,sizeof(b),"Restriction data unavailable · not marked Ready");
                else
                    snprintf(b,sizeof(b),"Needs %c %s  ·  %s has %c",rank_letter(i->weapon_rank),TYPES[type_key(i)],u->name,rank_letter(u->ranks[i->weapon_type]));
                label(p,x,info_y+28,text_width,36,b,MUTED,11,0,1);
            }
            int own=owner_index(s,e);
            snprintf(b,sizeof(b),"LOCATION   %s%s",own>=0?s->units[own].name:"Supply",!i->movable?"  ·  Fixed":"");
            label(p,x,info_y+68,text_width,22,b,MUTED,10,1,0);
            int desc_y=info_y+100;
            if(u && (i->attributes&1) && l->detail_height>=730) {
                const Fe8ItemInfo *comparison=NULL;
                for(int slot=0;slot<5;++slot) {
                    if(u->items[slot] && (u->item_info[slot].attributes&1)) { comparison=&u->item_info[slot]; break; }
                }
                if(comparison) {
                    snprintf(b,sizeof(b),"vs carried %.27s",comparison->name);
                    label(p,x,desc_y,text_width,18,b,MUTED,10,0,0);
                    snprintf(b,sizeof(b),"Mt %+d   Hit %+d   Wt %+d",(int)i->might-comparison->might,
                        (int)i->hit-comparison->hit,(int)i->weight-comparison->weight);
                    label(p,x,desc_y+20,text_width,20,b,ACCENT,12,1,0);
                    desc_y+=50;
                }
            }
            int desc_h=l->action_y-desc_y-12;
            if(desc_h>22)description(p,x,desc_y,text_width,desc_h,i->description,ui->detail_scroll);
        } else {
            snprintf(b,sizeof(b),"%s  ·  %s",i->name,state);
            label(p,x,y,text_width,22,b,TEXT,15,1,0);
            snprintf(b,sizeof(b),"%s   %c rank   Mt %u   Hit %u   Wt %u   Rng %u-%u",TYPES[type_key(i)],rank_letter(i->weapon_rank),i->might,i->hit,i->weight,i->min_range,i->max_range);
            if(l->detail_height>110)label(p,x,y+28,text_width,20,b,MUTED,11,0,0);
            int dy=l->detail_height>110?y+54:y+28;
            description(p,x,dy,text_width,l->detail_y+l->detail_height-dy-28,i->description,ui->detail_scroll);
        }
    }
    int can_move=actionable&&i&&i->movable;
    int can_give=can_move&&u&&free_slot(u)>=0&&!(e.kind==FE8_INVENTORY_ENDPOINT_UNIT&&e.unit_address==u->address);
    int can_store=can_move&&e.kind!=FE8_INVENTORY_ENDPOINT_SUPPLY&&deposit_index(ui)>=0;
    if(u&&can_give)snprintf(b,sizeof(b),"Give to %s",u->name);
    else if(u&&occupied(u)==5)snprintf(b,sizeof(b),"Loadout full · swap instead");
    else if(u&&actionable&&e.kind==FE8_INVENTORY_ENDPOINT_UNIT&&e.unit_address==u->address)snprintf(b,sizeof(b),"Already with %s",u->name);
    else snprintf(b,sizeof(b),"Give to recipient");
    button(p,l->action_x,l->action_y,l->action_width,28,b,1,can_give);
    button(p,l->action_x,l->action_y+32,l->action_width,28,ui->has_selection?"Cancel move":"Move / swap...",0,ui->has_selection||can_move);
    button(p,l->action_x,l->action_y+64,l->action_width,28,deposit_index(ui)<0?"Supply full":"Store in supply",0,can_store);
    if(l->detail_wide)label(p,l->action_x,l->action_y+98,l->action_width,16,"U Undo last move  ·  Scroll for more",MUTED,10,0,0);
    else label(p,x,l->detail_y+l->detail_height-20,text_width,18,
        ui->status,ui->has_selection?ACCENT:MUTED,10,0,0);
}
void fe8_inventory_desktop_draw(const Fe8InventoryUi *ui,const Fe8InventorySnapshot *s,
    uint32_t *pixels,int stride,int width,int height) {
    Fe8InventoryDesktopLayout l;
    Painter p;
    char b[192];
    if(!ui||!s||!pixels||width<=0||height<=0||stride<width)return;
    memset(&p,0,sizeof(p));p.pixels=pixels;p.stride=stride;p.width=width;p.height=height;
    p.scale=fe8_inventory_desktop_scale(ui,width,height);
    fe8_inventory_desktop_layout(ui,width,height,&l);
    fill(&p,0,0,l.width+1,l.height+1,BG);
    if(!fe8_host_text_begin(&p.text,pixels,stride,width,height))return;
    if(l.width<640||l.height<480) {
        label(&p,16,16,l.width-32,l.height-32,"Enlarge the window to use the Armory. Press I or Esc to return to the game.",TEXT,16,0,1);
        fe8_host_text_end(&p.text);return;
    }
    label(&p,PAD+4,14,150,36,"Armory",TEXT,l.height>=600?28:24,1,0);
    snprintf(b,sizeof(b),"%u allies   ·   Supply %u / %u",s->unit_count,s->supply_count,s->supply_capacity);
    label(&p,l.width-420,15,316,22,b,GOLD,12,1,0);
    label(&p,l.width-420,38,316,20,"/ Search   ·   +/- Size   ·   I / Esc Close",MUTED,11,0,0);
    button(&p,l.width-88,14,76,28,"Close",0,1);
    if(l.height>=600) {
        label(&p,PAD+4,54,l.sidebar,18,"EQUIP FOR",MUTED,10,1,0);
        label(&p,l.pool_x,54,l.pool_width,18,ui->has_selection?"MOVING ITEM · Choose a destination or cancel":"Click to inspect · Double-click to transfer · Drag to move",ui->has_selection?ACCENT:MUTED,10,1,0);
    }
    draw_sidebar(&p,ui,s,&l);
    draw_pool(&p,ui,s,&l);
    draw_detail(&p,ui,s,&l);
    if(l.detail_wide)label(&p,PAD+4,l.height-30,l.width-32,22,ui->status[0]?ui->status:"Click an item to inspect it. Your game is paused while the Armory is open.",ui->has_selection?ACCENT:MUTED,12,0,0);
    if(ui->dragging) {
        const Fe8ItemInfo *drag=info_at(s,ui->drag_source);
        int x=clamp((int)(ui->drag_x/p.scale)+14,8,l.width-224);
        int y=clamp((int)(ui->drag_y/p.scale)+16,8,l.height-54);
        card(&p,x,y,216,46,SELECTED);border(&p,x,y,216,46,ACCENT);
        label(&p,x+10,y+6,196,20,drag?drag->name:"Moving item",TEXT,13,1,0);
        label(&p,x+10,y+27,196,16,"Drop to transfer · Esc cancels",ACCENT,10,0,0);
    }
    fe8_host_text_end(&p.text);
}
