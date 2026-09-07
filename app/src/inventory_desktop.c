/* Armory workspace: a read-only derived browser, a recipient workbench and a
   persistent inspector. Only main.c owns the guarded inventory transaction. */
#include "inventory_desktop.h"
#include "inventory_pins.h"
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
    ui->pool_scroll = ui->supply_scroll = 0;
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
            !(e->info && contains(TYPES[type_key(e->info)], start, n))) return 0;
    }
    return 1;
}
/* Search the unit itself first: empty loadouts must remain findable. Carried
   equipment uses the same ANDed item/owner/class/type tokens as By item. Type
   and usability chips still highlight slots; they do not hide recipients. */
int fe8_inventory_desktop_unit_matches(const Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *s, int unit) {
    if (!ui || !s || unit < 0 || unit >= s->unit_count ||
            unit >= FE8_INVENTORY_UNIT_CAPACITY) return 0;
    Fe8InventoryListEntry entry = {0};
    entry.unit_index = unit;
    if (matches_query(ui->query, &entry, s)) return 1;
    const Fe8InventoryUnit *u = &s->units[unit];
    for (int slot = 0; slot < FE8_INVENTORY_ITEM_SLOTS; ++slot) {
        if (!u->items[slot]) continue;
        entry.info = &u->item_info[slot];
        if (matches_query(ui->query, &entry, s)) return 1;
    }
    return 0;
}
void fe8_inventory_desktop_clear_query(Fe8InventoryUi *ui) {
    if (!ui) return;
    ui->query[0] = 0;
    ui->loadout_scroll = 0;
    changed_view(ui);
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
    ui->loadout_scroll = 0;
    changed_view(ui);
}
void fe8_inventory_desktop_backspace(Fe8InventoryUi *ui) {
    size_t n;
    if (!ui || !ui->search_active || !(n = strlen(ui->query))) return;
    do { --n; } while (n && ((unsigned char)ui->query[n] & 0xC0) == 0x80);
    ui->query[n] = 0;
    ui->loadout_scroll = 0;
    changed_view(ui);
}

/* Explain the result of the same conservative predicate used for filtering.
   A whitelist name is displayed only when the entire single-ID restriction can
   be resolved from the snapshot. Missing characters are never guessed. */
void fe8_inventory_desktop_use_reason(const Fe8InventorySnapshot *s,
    const Fe8InventoryUnit *u, const Fe8ItemInfo *i, char *out, size_t capacity) {
    const char *reason = "Use in game";
    if (!out || !capacity) return;
    if (!i || !i->id) reason = "Unknown item";
    else if (!u) reason = "Select an ally";
    else switch (fe8_inventory_item_use_state(u,i)) {
    case FE8_INVENTORY_USE_READY: reason = "Can use"; break;
    case FE8_INVENTORY_USE_RANK:
        if (i->weapon_type < 8 && !u->ranks[i->weapon_type]) {
            snprintf(out,capacity,"No %s proficiency",(const char *const[]){"gear","sword","lance","axe","bow","staff","anima","light","dark","item"}[type_key(i)]);
            return;
        }
        snprintf(out,capacity,"Needs %c · has %c",rank_letter(i->weapon_rank),
            i->weapon_type < 8 ? rank_letter(u->ranks[i->weapon_type]) : '-');
        return;
    case FE8_INVENTORY_USE_LOCKED: {
        reason = i->lock_kind == FE8_ITEM_LOCK_CHARACTER ? "Personal weapon" :
            i->lock_kind == FE8_ITEM_LOCK_CLASS ? "Class restricted" : "Use restricted";
        int count = 0, id = -1;
        for (int n=0;n<256;++n) if (i->lock_ids[n/8] & (1u << (n%8))) { ++count; id=n; }
        if (s && count == 1 && (i->lock_kind == FE8_ITEM_LOCK_CHARACTER || i->lock_kind == FE8_ITEM_LOCK_CLASS))
            for (int n=0;n<s->unit_count;++n) {
                int character = i->lock_kind == FE8_ITEM_LOCK_CHARACTER;
                if ((character ? s->units[n].character_id : s->units[n].class_id) == id) {
                    const char *name = character ? s->units[n].name : s->units[n].class_name;
                    if (name[0]) { snprintf(out,capacity,"%s only",name); return; }
                }
            }
        break;
    }
    case FE8_INVENTORY_USE_STATUS:
        reason = u->status == 3 ? "Silenced" : u->status == 2 ? "Asleep" : "Blocked by status"; break;
    case FE8_INVENTORY_USE_UNKNOWN: reason = "Restriction unknown"; break;
    default: break;
    }
    snprintf(out,capacity,"%s",reason);
}

static int entry_matches(const Fe8InventoryUi *ui, const Fe8InventorySnapshot *s,
    const Fe8InventoryListEntry *e, int filter_type) {
    const Fe8InventoryUnit *u=target(ui,s);
    return e->item && (!filter_type || !ui->type_filter || type_key(e->info)==ui->type_filter) &&
        (!ui->usable_only || (u && fe8_inventory_item_use_state(u,e->info)==FE8_INVENTORY_USE_READY)) &&
        matches_query(ui->query,e,s);
}
static void type_counts(const Fe8InventoryUi *ui, const Fe8InventorySnapshot *s, int counts[10]) {
    memset(counts,0,10*sizeof(*counts));
    for (int n=0;n<ui->pool_count;++n) if (entry_matches(ui,s,&ui->pool[n],0)) {
        ++counts[0]; ++counts[type_key(ui->pool[n].info)];
    }
    /* Unit search is independent of the item browser's remembered Supply
       scope. Count matching carried items exactly once. */
    if (ui->by_unit && ui->pool_scope==FE8_INVENTORY_POOL_SUPPLY)
        for (int n=0;n<s->unit_count;++n) for (int j=0;j<5;++j) {
            const Fe8InventoryUnit *u=&s->units[n];
            Fe8InventoryListEntry entry={{FE8_INVENTORY_ENDPOINT_UNIT,u->address,(unsigned)j},
                &u->item_info[j],u->items[j],n};
            if (entry_matches(ui,s,&entry,0)) { ++counts[0]; ++counts[type_key(entry.info)]; }
        }
}
static int supply_visible(const Fe8InventoryUi *ui, const Fe8InventorySnapshot *s, int *indices) {
    int count=0;
    for (int row=0;row<ui->pool_count;++row) {
        int n=ui->sort_descending?ui->pool_count-1-row:row;
        if (ui->pool[n].endpoint.kind==FE8_INVENTORY_ENDPOINT_SUPPLY && entry_matches(ui,s,&ui->pool[n],1)) {
            if (indices) indices[count]=n;
            ++count;
        }
    }
    return count;
}
static int unit_at_address(const Fe8InventorySnapshot *s, uint32_t address) {
    for (int n=0;n<s->unit_count;++n) if (s->units[n].address==address) return n;
    return -1;
}

void fe8_inventory_desktop_cancel_move(Fe8InventoryUi *ui) {
    if (!ui) return;
    fe8_inventory_desktop_cancel_drag(ui);
    ui->has_selection=0; ui->popup_open=0; ui->has_preview_comparison=0;
}
static void open_swap(Fe8InventoryUi *ui, const Fe8InventorySnapshot *s,
    const Fe8InventoryUnit *u, Fe8InventoryEndpoint source) {
    ui->selected=source; ui->has_selection=1;
    ui->popup_source=source; ui->popup_item=fe8_inventory_ui_endpoint_item(s,source);
    ui->popup_unit_address=u->address;
    ui->popup_anchor_x=ui->pointer_x; ui->popup_anchor_y=ui->pointer_y;
    ui->popup_open=1; ui->has_preview_comparison=0;
    snprintf(ui->status,sizeof(ui->status),"Choose what %s will exchange. Esc cancels.",u->name);
}
int fe8_inventory_desktop_comparison(const Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *s, Fe8InventoryEndpoint *endpoint) {
    if (!ui || !s || !endpoint) return 0;
    if ((ui->dragging || ui->popup_open) && ui->has_preview_comparison) *endpoint=ui->preview_comparison;
    else if (ui->has_comparison) *endpoint=ui->comparison;
    else return 0;
    const Fe8ItemInfo *i=info_at(s,*endpoint);
    return i && fe8_inventory_ui_endpoint_item(s,*endpoint) && (i->attributes&1);
}
void fe8_inventory_desktop_feedback(Fe8InventoryUi *ui, const Fe8InventorySnapshot *s,
    Fe8InventoryEndpoint from, Fe8InventoryEndpoint to, int undo) {
    const Fe8ItemInfo *a=info_at(s,from), *b=info_at(s,to);
    int owner_a=owner_index(s,from), owner_b=owner_index(s,to);
    const char *name_a=owner_a>=0?s->units[owner_a].name:"Supply";
    const char *name_b=owner_b>=0?s->units[owner_b].name:"Supply";
    if (fe8_inventory_ui_endpoint_item(s,to))
        snprintf(ui->status,sizeof(ui->status),"%s%.27s → %.27s · %.27s → %.27s",
            undo?"Undid swap: ":"Swapped: ",a?a->name:"Item",name_b,b?b->name:"Item",name_a);
    else snprintf(ui->status,sizeof(ui->status),"%s%.27s moved from %.27s to %.27s.",
        undo?"Undo: ":"",a?a->name:"Item",name_a,name_b);
    ui->flash=to; ui->flash_ticks=120;
    ui->detail=to; ui->has_detail=1; ui->detail_scroll=0;
    ui->popup_open=0; ui->has_preview_comparison=0;
}

void fe8_inventory_desktop_layout(const Fe8InventoryUi *ui, int width, int height,
    Fe8InventoryDesktopLayout *l) {
    static const int widths[11] = {0, 60, 64, 100, 34, 34, 40, 38, 34, 46, 136};
    static const int optional[] = {8, 7, 4, 9, 6, 5, 1, 10};
    float scale = fe8_inventory_desktop_scale(ui, width, height);
    int reserved = 0;
    memset(l, 0, sizeof(*l));
    l->width = (int)(width / scale + 0.0001f);
    l->height = (int)(height / scale + 0.0001f);
    l->sidebar = clamp(l->width / 5, 232, 292);
    l->pool_x = PAD + l->sidebar + GAP;
    l->top = l->height >= 600 ? 76 : 60;
    l->detail_wide = !ui->by_unit && l->width >= 1220 && l->height >= 720;
    l->detail_collapsed = !l->detail_wide;
    l->detail_width = l->detail_wide ? 280 : l->width - 2 * PAD;
    l->detail_height = l->detail_wide ? l->height - l->top - 48 : 48;
    l->detail_x = l->detail_wide ? l->width - PAD - l->detail_width : PAD;
    l->detail_y = l->detail_wide ? l->top : l->height - 44 - l->detail_height;
    l->help_y = l->detail_wide ? l->height - 36 : l->detail_y;
    l->bottom = l->help_y - GAP;
    l->pool_width = (l->detail_wide ? l->detail_x - GAP : l->width - PAD) - l->pool_x;
    l->row_height = l->height < 600 ? 24 : ui->comfortable ? 40 : 28;
    l->side_row_height = l->height < 720 ? 24 : ui->comfortable ? 36 : 32;
    /* At minimum size the compact stats grid still leaves all five item
       slots and one roster row. Taller windows use larger stat cells. */
    l->stats_y = l->top + (l->height < 600 ? 108 : l->height < 720 ? 116 : 128);
    l->stat_row_height = l->height < 600 ? 18 : l->height < 720 ? 22 : 28;
    l->items_y = l->stats_y + 2 * l->stat_row_height + (l->height < 600 ? 4 : 20);
    l->roster_y = l->items_y + 5 * l->side_row_height + 22;
    l->roster_rows = (l->bottom - l->roster_y) / l->side_row_height;
    if (l->roster_rows < 0) l->roster_rows = 0;
    l->search_y = l->top + 36;
    l->filters_y = l->search_y + 38;
    l->usable_width = l->pool_width >= 480 ? 172 : 136;
    l->filter_columns = l->pool_width >= 840 ? 10 : 5;
    l->filter_height = 26;
    l->table_y = l->filters_y + (10 / l->filter_columns) * l->filter_height + 30;
    l->deposit_y = l->bottom - 32;
    l->table_rows = (l->deposit_y - l->table_y - 4) / l->row_height;
    if (l->table_rows < 0) l->table_rows = 0;
    l->quick_width = 64;
    l->quick_x = l->pool_x + l->pool_width - GUTTER - l->quick_width;
    memcpy(l->column_width, widths, sizeof(widths));
    for (int i = 1; i < 11; ++i) reserved += l->column_width[i];
    for (unsigned i = 0; i < sizeof(optional) / sizeof(optional[0]); ++i) {
        if (l->pool_width - GUTTER - l->quick_width - reserved >= 144) break;
        reserved -= l->column_width[optional[i]]; l->column_width[optional[i]] = 0;
    }
    if (l->pool_width < 400) { reserved -= 24; l->column_width[3] -= 24; }
    l->column_width[0] = l->pool_width - GUTTER - l->quick_width - reserved;
    int x = l->pool_x;
    for (int i = 0; i < 11; ++i) { l->column_x[i] = x; x += l->column_width[i]; }

    l->board_x = PAD;
    l->supply_width = l->width >= 1000 ? 264 : 0;
    l->supply_x = l->width - PAD - l->supply_width;
    l->board_width = l->width - 2 * PAD - (l->supply_width ? l->supply_width + GAP : 0);
    l->identity_width = l->width >= 1000 ? 176 : 112;
    l->slot_width = (l->board_width - l->identity_width - 8) / 5;
    /* Keep the equipment hit area separate from the read-only stat strip.
       Short windows fit two compact loadouts, with every stat still visible. */
    l->board_card_height = l->height < 600 ? 58 : ui->comfortable ? 96 : 80;
    l->board_row_height = l->board_card_height + (l->height < 600 ? 24 : 28);
    l->board_y = l->top + (l->board_width >= 820 ? 94 : 120);
    l->board_rows = (l->deposit_y - l->board_y) / l->board_row_height;
    if (l->board_rows < 0) l->board_rows = 0;

    /* Details are a drawer on smaller windows, not permanently lost roster
       space. Opening it does not move the slots underneath or their targets. */
    if (l->detail_collapsed && ui->details_expanded) {
        l->detail_overlay = 1; l->detail_collapsed = 0; l->detail_wide = 1;
        l->detail_width = l->width >= 1000 ? 336 : l->pool_width;
        l->detail_x = l->width - PAD - l->detail_width;
        l->detail_y = l->top; l->detail_height = l->height - l->top - 48;
    }
    l->action_width = l->detail_wide ? l->detail_width - 32 : 0;
    l->action_x = l->detail_x + 16;
    l->action_y = l->detail_y + l->detail_height - 116;
    l->popup_width = 336; l->popup_height = 320;
    l->popup_x = clamp((int)(ui->popup_anchor_x / scale) + 12, PAD, l->width - PAD - l->popup_width);
    l->popup_y = clamp((int)(ui->popup_anchor_y / scale) + 12, l->top, l->height - 44 - l->popup_height);
    l->popup_rows_y = l->popup_y + 76;
}

static int in(int x, int y, int left, int top, int w, int h) {
    return x >= left && y >= top && x < left + w && y < top + h;
}
static Fe8InventoryHitKind board_hit(const Fe8InventoryUi *ui,const Fe8InventorySnapshot *s,
    const Fe8InventoryDesktopLayout *l,int x,int y,int *index) {
    if (in(x,y,l->board_x,l->top,l->board_width-180,30)) return FE8_INVENTORY_HIT_SEARCH;
    if (in(x,y,l->board_x+l->board_width-172,l->top,164,30)) return FE8_INVENTORY_HIT_USABLE;
    int columns=l->board_width>=820?10:5,cell=l->board_width/columns;
    if (in(x,y,l->board_x,l->top+38,l->board_width,(10/columns)*26)) {
        int col=(x-l->board_x)/cell;
        if (col<columns) { *index=((y-l->top-38)/26)*columns+col; return FE8_INVENTORY_HIT_FILTER; }
    }
    Fe8InventoryBoardView v;
    fe8_inventory_board_view(ui,s,l,&v);
    int right=l->board_x+l->board_width, sy=0;
    if (v.pinned_count && in(x,y,l->board_x,l->board_y-22,l->board_width,22)) {
        if (ui->dragging) return FE8_INVENTORY_HIT_NONE;
        if (x>=right-82) return FE8_INVENTORY_HIT_UNPIN_ALL;
        if (v.pinned_count>v.pinned_rows && v.pinned_rows>0) {
            if (in(x,y,right-212,l->board_y-22,28,20)) {
                *index=offset(v.pinned_start-v.pinned_rows,v.pinned_count,v.pinned_rows);
                return FE8_INVENTORY_HIT_PIN_PAGE;
            }
            if (in(x,y,right-118,l->board_y-22,28,20)) {
                *index=offset(v.pinned_start+v.pinned_rows,v.pinned_count,v.pinned_rows);
                return FE8_INVENTORY_HIT_PIN_PAGE;
            }
        }
        return FE8_INVENTORY_HIT_NONE;
    }
    int unit=fe8_inventory_board_unit_at(&v,y,&sy);
    if (unit>=0 && in(x,y,l->board_x,sy,l->board_width,v.row_height)) {
        if (!ui->dragging && in(x,y,l->board_x+l->identity_width-48,sy+6,42,18)) {
            *index=unit; return FE8_INVENTORY_HIT_PIN_UNIT;
        }
        if (x<l->board_x+l->identity_width || y>=sy+v.card_height) {
            *index=unit; return FE8_INVENTORY_HIT_ROSTER;
        }
        int slot=(x-l->board_x-l->identity_width)/l->slot_width;
        int sx=l->board_x+l->identity_width+slot*l->slot_width;
        if (slot<5 && in(x,y,sx,sy+4,l->slot_width-6,v.card_height-8)) {
            *index=unit*5+slot;
            return !ui->dragging && !ui->has_selection && s->units[unit].items[slot] &&
                (s->units[unit].item_info[slot].attributes&1) && in(x,y,sx+l->slot_width-32,sy+8,22,18) ?
                FE8_INVENTORY_HIT_COMPARE : FE8_INVENTORY_HIT_LOADOUT_ITEM;
        }
    }
    if (l->supply_width && in(x,y,l->supply_x,l->board_y,l->supply_width,l->deposit_y-l->board_y)) {
        int indices[FE8_INVENTORY_POOL_CAPACITY],n=supply_visible(ui,s,indices);
        int rows=(l->deposit_y-l->board_y)/32;
        int row=offset(ui->supply_scroll,n,rows)+(y-l->board_y)/32;
        if (row<n && y<l->board_y+rows*32) {
            *index=indices[row];
            return !ui->dragging && x>=l->supply_x+l->supply_width-56 ? FE8_INVENTORY_HIT_QUICK_POOL : FE8_INVENTORY_HIT_POOL_ITEM;
        }
    }
    if (in(x,y,PAD,l->deposit_y,l->width-2*PAD,30)) {
        if (!ui->has_selection && x<l->board_x+l->board_width-180 && (ui->query[0]||ui->type_filter||ui->usable_only)) return FE8_INVENTORY_HIT_RESET;
        if (ui->has_selection) { *index=deposit_index(ui); return *index>=0?FE8_INVENTORY_HIT_POOL_ITEM:FE8_INVENTORY_HIT_NONE; }
        return FE8_INVENTORY_HIT_SUPPLY_VIEW;
    }
    return FE8_INVENTORY_HIT_NONE;
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
    if (ui->popup_open && ui->has_selection) {
        if (in(x,y,l.popup_x+12,l.popup_rows_y,l.popup_width-24,200)) {
            *index=(y-l.popup_rows_y)/40; return FE8_INVENTORY_HIT_SWAP_SLOT;
        }
        return FE8_INVENTORY_HIT_POPUP_CANCEL; /* Modal: never click through. */
    }
    if (in(x,y,160,14,86,30)) return FE8_INVENTORY_HIT_VIEW_ITEMS;
    if (in(x,y,250,14,86,30)) return FE8_INVENTORY_HIT_VIEW_UNITS;
    if (in(x,y,344,14,98,30)) return FE8_INVENTORY_HIT_STAT_MODE;
    if (in(x,y,l.width-130,l.height-34,118,28)) return ui->undo_count>0 ? FE8_INVENTORY_HIT_UNDO : FE8_INVENTORY_HIT_NONE;
    if ((l.detail_collapsed || l.detail_overlay) && in(x,y,l.width-194,14,98,28)) return FE8_INVENTORY_HIT_DETAILS;
    if (in(x,y,l.width-88,14,76,28)) return FE8_INVENTORY_HIT_CLOSE;
    if (l.detail_collapsed && in(x,y,l.detail_x,l.detail_y,l.detail_width,l.detail_height)) return FE8_INVENTORY_HIT_DETAILS;
    if (!l.detail_collapsed && in(x,y,l.action_x,l.action_y,l.action_width,28)) return FE8_INVENTORY_HIT_GIVE;
    if (!l.detail_collapsed && in(x,y,l.action_x,l.action_y+32,l.action_width,28)) return ui->has_selection ? FE8_INVENTORY_HIT_CANCEL : FE8_INVENTORY_HIT_MOVE;
    if (!l.detail_collapsed && in(x,y,l.action_x,l.action_y+64,l.action_width,28)) return FE8_INVENTORY_HIT_STORE;
    if (l.detail_overlay && in(x,y,l.detail_x,l.detail_y,l.detail_width,l.detail_height)) return FE8_INVENTORY_HIT_NONE;
    if (ui->by_unit) return board_hit(ui,s,&l,x,y,index);
    if (x < PAD || x >= l.width - PAD || y < l.top || y >= l.bottom) return FE8_INVENTORY_HIT_NONE;
    if (x < PAD + l.sidebar - GUTTER) {
        if (target(ui, s)) {
            if (x >= PAD + 94 && y >= l.top + 6 && y < l.top + 46) {
                *index = ui->current_unit;
                return y < l.top + 26 ? FE8_INVENTORY_HIT_UNIT_NAME : FE8_INVENTORY_HIT_UNIT_CLASS;
            }
            if (y >= l.items_y && y < l.items_y + 5 * l.side_row_height) {
                *index = (y - l.items_y) / l.side_row_height;
                if (!ui->dragging && !ui->has_selection && target(ui,s)->items[*index] &&
                    (target(ui,s)->item_info[*index].attributes&1) && x >= PAD+l.sidebar-88 && x < PAD+l.sidebar-66) {
                    *index += ui->current_unit*5; return FE8_INVENTORY_HIT_COMPARE;
                }
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
    if (in(x,y,l.pool_x,l.search_y,l.pool_width-l.usable_width-12,30)) return FE8_INVENTORY_HIT_SEARCH;
    if (in(x,y,l.pool_x+l.pool_width-l.usable_width,l.search_y,l.usable_width-8,30)) return FE8_INVENTORY_HIT_USABLE;
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
    if (ui->popup_open) return;
    if (ui->by_unit) {
        if (l.supply_width && x>=l.supply_x) {
            int n=supply_visible(ui,s,NULL), visible=(l.deposit_y-l.board_y)/32;
            ui->supply_scroll=offset(offset(ui->supply_scroll,n,visible)+clamp(rows,-10000,10000),n,visible);
        } else if (x>=PAD && x<l.board_x+l.board_width) {
            Fe8InventoryBoardView v;
            fe8_inventory_board_view(ui,s,&l,&v);
            /* Wheel scrolling never moves the frozen section, even above it.
               Overflow pins use explicit paging instead of stealing the wheel. */
            ui->loadout_scroll=offset(v.other_start+clamp(rows,-10000,10000),v.other_count,v.other_rows);
        }
        clear_hover(ui); return;
    }
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
    if (ui->popup_open) return;
    if (!l.detail_collapsed && in((int)(x/scale),(int)(y/scale),l.detail_x,l.detail_y,l.detail_width,l.detail_height)) {
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
    case FE8_INVENTORY_HIT_PIN_UNIT: {
        if (ui->dragging) return 1;
        int change=fe8_inventory_pin_toggle(ui,s,*index);
        if (change) snprintf(ui->status,sizeof(ui->status),change>0?
            "%s pinned above the scrolling roster. Pins clear when you close the Armory.":
            "%s returned to the scrolling roster.",s->units[*index].name);
        clear_hover(ui); return 1;
    }
    case FE8_INVENTORY_HIT_UNPIN_ALL:
        if (ui->dragging) return 1;
        ui->pinned_count=ui->pinned_scroll=0;
        memset(ui->pinned_units,0,sizeof(ui->pinned_units));
        ui->loadout_scroll=0;
        snprintf(ui->status,sizeof(ui->status),"All units unpinned. No equipment changed.");
        clear_hover(ui); return 1;
    case FE8_INVENTORY_HIT_PIN_PAGE:
        if (ui->dragging) return 1;
        ui->pinned_scroll=clamp(*index,0,ui->pinned_count>0?ui->pinned_count-1:0);
        clear_hover(ui); return 1;
    case FE8_INVENTORY_HIT_STAT_MODE:
        ui->stats_base=!ui->stats_base;
        snprintf(ui->status,sizeof(ui->status),"%s",ui->stats_base?
            "Base stats: stored unit values, without active equipment or skill modifiers.":
            "Total stats: native equipment, skills and penalties. Hover a stat for its base and net modifier.");
        clear_hover(ui); return 1;
    case FE8_INVENTORY_HIT_VIEW_ITEMS:
    case FE8_INVENTORY_HIT_VIEW_UNITS:
        fe8_inventory_desktop_cancel_move(ui);
        ui->by_unit=*kind==FE8_INVENTORY_HIT_VIEW_UNITS;
        clear_hover(ui); return 1;
    case FE8_INVENTORY_HIT_DETAILS:
        ui->details_expanded=!ui->details_expanded;
        fe8_inventory_desktop_cancel_move(ui); clear_hover(ui); return 1;
    case FE8_INVENTORY_HIT_SUPPLY_VIEW:
        ui->by_unit=0; ui->pool_scope=FE8_INVENTORY_POOL_SUPPLY;
        fe8_inventory_ui_rebuild(ui,s); changed_view(ui); return 1;
    case FE8_INVENTORY_HIT_COMPARE:
        if (*index>=0 && *index<s->unit_count*5) {
            e=fe8_inventory_ui_endpoint(ui,s,*kind,*index);
            const Fe8ItemInfo *i=info_at(s,e);
            if (i && fe8_inventory_ui_endpoint_item(s,e) && (i->attributes&1)) {
                ui->comparison=e; ui->has_comparison=1;
                snprintf(ui->status,sizeof(ui->status),"Comparing with %s's %s. Select a candidate to see the tradeoffs.",s->units[*index/5].name,i->name);
            }
        }
        return 1;
    case FE8_INVENTORY_HIT_LOADOUT_ITEM:
        if (*index<0 || *index>=s->unit_count*5) return 1;
        e=fe8_inventory_ui_endpoint(ui,s,*kind,*index);
        if (!ui->has_selection) {
            if (fe8_inventory_ui_endpoint_item(s,e)) { ui->detail=e; ui->has_detail=1; ui->detail_scroll=0; }
            return 1;
        }
        ui->current_unit=*index/5; *index%=5; *kind=FE8_INVENTORY_HIT_UNIT_ITEM;
        return fe8_inventory_desktop_click(ui,s,kind,index);
    case FE8_INVENTORY_HIT_SWAP_SLOT: {
        int unit=unit_at_address(s,ui->popup_unit_address);
        if (!ui->popup_open || unit<0 || *index<0 || *index>=5 ||
            fe8_inventory_ui_endpoint_item(s,ui->popup_source)!=ui->popup_item) {
            fe8_inventory_desktop_cancel_move(ui);
            snprintf(ui->status,sizeof(ui->status),"Swap cancelled: the inventory changed."); return 1;
        }
        if (s->units[unit].items[*index] && !s->units[unit].item_info[*index].movable) {
            snprintf(ui->status,sizeof(ui->status),"Fixed equipment cannot be exchanged."); return 1;
        }
        ui->selected=ui->popup_source; ui->has_selection=1; ui->popup_open=0;
        ui->has_preview_comparison=0; ui->current_unit=unit;
        *kind=FE8_INVENTORY_HIT_UNIT_ITEM;
        return fe8_inventory_desktop_click(ui,s,kind,index);
    }
    case FE8_INVENTORY_HIT_POPUP_CANCEL:
        fe8_inventory_desktop_cancel_move(ui);
        snprintf(ui->status,sizeof(ui->status),"Swap cancelled. No items changed."); return 1;
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
        ui->type_filter = 0; ui->usable_only = 0; fe8_inventory_desktop_clear_query(ui); return 1;
    case FE8_INVENTORY_HIT_CANCEL:
        fe8_inventory_desktop_cancel_move(ui);
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
                open_swap(ui,s,u,e);
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
            if (!ui->undo_count) snprintf(ui->status,sizeof(ui->status),"Double-click to transfer, or drag to a slot, ally, or supply.");
        } else snprintf(ui->status,sizeof(ui->status),"Drag an item here, or use Give beside an item.");
        return 1;
    case FE8_INVENTORY_HIT_ROSTER:
    case FE8_INVENTORY_HIT_ROSTER_CLASS:
        if (*index >= 0 && *index < s->unit_count) {
            if (ui->usable_only) changed_view(ui);
            else clear_hover(ui);
            ui->detail_scroll = 0;
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
    ui->pointer_x=x; ui->pointer_y=y;
    fe8_inventory_desktop_cancel_drag(ui);
    if (ui->has_selection) return; /* Click-move mode handles the destination. */
    if (kind == FE8_INVENTORY_HIT_POOL_ITEM) {
        if (index < 0 || index >= ui->pool_count) return;
    } else if (kind == FE8_INVENTORY_HIT_LOADOUT_ITEM) {
        if (index<0 || index>=s->unit_count*5) return;
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
    if (!ui || !s) return;
    ui->pointer_x=x; ui->pointer_y=y;
    int preview_index=-1;
    Fe8InventoryHitKind preview=fe8_inventory_desktop_hit(ui,s,width,height,x,y,&preview_index);
    ui->has_preview_comparison=0;
    if (preview==FE8_INVENTORY_HIT_SWAP_SLOT) {
        int n=unit_at_address(s,ui->popup_unit_address);
        if (n>=0) { preview=FE8_INVENTORY_HIT_LOADOUT_ITEM; preview_index=n*5+preview_index; }
    }
    if (preview==FE8_INVENTORY_HIT_UNIT_ITEM || preview==FE8_INVENTORY_HIT_LOADOUT_ITEM) {
        ui->preview_comparison=fe8_inventory_ui_endpoint(ui,s,preview,preview_index);
        ui->has_preview_comparison=fe8_inventory_ui_endpoint_item(s,ui->preview_comparison)!=0;
    }
    if (!ui->drag_armed) return;
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
    if (*kind==FE8_INVENTORY_HIT_QUICK_POOL) *kind=FE8_INVENTORY_HIT_POOL_ITEM;
    if (*kind==FE8_INVENTORY_HIT_LOADOUT_ITEM && (*index<0 || *index>=s->unit_count*5)) {
        fe8_inventory_desktop_cancel_move(ui); return 1;
    }
    if (*kind == FE8_INVENTORY_HIT_LOADOUT_ITEM || *kind == FE8_INVENTORY_HIT_UNIT_ITEM || *kind == FE8_INVENTORY_HIT_POOL_ITEM ||
        *kind == FE8_INVENTORY_HIT_ROSTER || *kind == FE8_INVENTORY_HIT_ROSTER_CLASS) {
        if (*kind == FE8_INVENTORY_HIT_LOADOUT_ITEM || *kind == FE8_INVENTORY_HIT_UNIT_ITEM || *kind == FE8_INVENTORY_HIT_POOL_ITEM) {
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
    int small=h<=22, padding=small?3:10;
    label(p,x+padding,y+(h-(small?14:16))/2,w-2*padding,small?14:20,
        s,enabled?(active?ACCENT:TEXT):MUTED,small?10:12,active,0);
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
    if(!u)return "Select ally";
    switch(fe8_inventory_item_use_state(u,i)) {
    case FE8_INVENTORY_USE_READY:*c=ACCENT;return "Can use";
    case FE8_INVENTORY_USE_RANK:*c=WARN;return "Needs rank";
    case FE8_INVENTORY_USE_LOCKED:*c=DANGER;return "Restricted";
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
/* Unit values from the coherent snapshot, not item stats or forecasts.
   Pow is intentionally neutral: a profile may use a shared strength/magic
   field. Never infer a separate magic value or stat cap from weapon ranks. */
static void draw_unit_stats(Painter *p, const Fe8InventoryUi *ui, const Fe8InventoryUnit *u,
    int x, int y, int width, int columns, int row_height) {
    static const char *const names[] = {"Pow", "Skl", "Spd", "Lck", "Def", "Res", "Con", "Mov"};

    for (int n = 0; n < 8; ++n) {
        int left = x + (n % columns) * width / columns;
        int right = x + ((n % columns) + 1) * width / columns;
        int top = y + (n / columns) * row_height;
        int inset = right - left < 60 ? 2 : 4;
        int value_width = 25; /* Up to 255, without truncating valid values. */
        int value_x = right - value_width - inset;
        char value[8];
        int base=fe8_inventory_stat_base(u,(Fe8UnitStat)n);
        int total=fe8_inventory_stat_value(u,(Fe8UnitStat)n,ui->stats_base);
        uint32_t color=total>base?ACCENT:total<base?DANGER:TEXT;
        snprintf(value, sizeof(value), "%d", total);
        label(p, left + inset, top + (row_height - 14) / 2,
            value_x - left - inset - 2, 14, names[n], MUTED, 9, 0, 0);
        label(p, value_x, top + (row_height - 16) / 2,
            value_width, 16, value, color, row_height < 22 ? 11 : 12, 1, 0);
        if (n % columns < columns - 1)
            fill(p, right - 1, top + 4, 1, row_height - 8, LINE);
    }
}
static void experience(char *out, size_t capacity, const Fe8InventoryUnit *u) {
    /* FE8 uses 0xFF when experience is disabled, not 255 experience points. */
    if (u->exp == UINT8_MAX) snprintf(out, capacity, "Lv %u · EXP --", u->level);
    else snprintf(out, capacity, "Lv %u · EXP %u", u->level, u->exp);
}
static void draw_sidebar(Painter *p,const Fe8InventoryUi *ui,const Fe8InventorySnapshot *s,
    const Fe8InventoryDesktopLayout *l) {
    const Fe8InventoryUnit *u=target(ui,s);
    char b[160];
    card(p,PAD,l->top,l->sidebar,l->bottom-l->top,PANEL);
    if(u) {
        portrait(p,u,PAD+8,l->top+6,l->height<600?72:80,l->height<600?64:72);
        label(p,PAD+94,l->top+6,l->sidebar-104,24,u->name,TEXT,19,1,0);
        label(p,PAD+94,l->top+30,l->sidebar-104,18,u->class_name,MUTED,12,0,0);
        experience(b,sizeof(b),u);
        label(p,PAD+94,l->top+51,l->sidebar-104,16,b,MUTED,10,0,0);
        snprintf(b,sizeof(b),"HP %u / %d",u->hp,fe8_inventory_stat_value(u,FE8_STAT_MAX_HP,ui->stats_base));
        label(p,PAD+94,l->top+68,l->sidebar-104,16,b,ACCENT,11,1,0);
        int x=PAD+10;
        for(int t=0;t<8;++t)if(u->ranks[t]&&x<PAD+l->sidebar-34) {
            snprintf(b,sizeof(b),"%.2s %c",TYPES[t+1],rank_letter(u->ranks[t]));
            badge(p,x,l->top+86,44,b,TYPE_COLORS[t+1]);x+=48;
        }
        if(x==PAD+10)label(p,x,l->top+88,l->sidebar-20,16,"No weapon ranks",MUTED,11,0,0);
        if(l->height>=720)label(p,PAD+10,l->stats_y-18,l->sidebar-20,16,ui->stats_base || !u->effective_stats_valid?"BASE STATS":"TOTAL STATS · Hover for modifiers",MUTED,10,1,0);
        card(p,PAD+6,l->stats_y,l->sidebar-18,2*l->stat_row_height,RAISED);
        draw_unit_stats(p,ui,u,PAD+10,l->stats_y,l->sidebar-24,4,l->stat_row_height);
        snprintf(b,sizeof(b),"LOADOUT   %d / 5",occupied(u));
        if(l->height>=600)label(p,PAD+10,l->items_y-16,l->sidebar-20,16,b,MUTED,10,1,0);
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
                label(p,PAD+32,y+4,l->sidebar-126,22,i->name,TEXT,12,inspected,0);
                if(i->attributes&1)label(p,PAD+l->sidebar-88,y+5,22,18,"vs",
                    ui->has_comparison&&same(ui->comparison,e)?GOLD:MUTED,10,1,0);
                if(ui->flash_ticks && same(ui->flash,e))border(p,PAD+6,y+1,l->sidebar-18,l->side_row_height-2,GOLD);
                uses(b,sizeof(b),u->items[j],i);
                label(p,PAD+l->sidebar-62,y+5,48,18,b,!i->movable?MUTED:c,10,0,0);
                durability(p,PAD+32,y+l->side_row_height-4,l->sidebar-100,u->items[j],i);
            } else {
                label(p,PAD+32,y+4,l->sidebar-56,20,ui->has_selection?"+ Place item here":"+ Empty slot",ui->has_selection?ACCENT:MUTED,12,0,0);
            }
        }
    } else label(p,PAD+16,l->top+20,l->sidebar-32,80,"No allies available",MUTED,16,1,1);
    int start=offset(ui->roster_scroll,s->unit_count,l->roster_rows);
    Fe8InventoryEndpoint e; const Fe8ItemInfo *selected_info=pinned(ui,s,&e)?info_at(s,e):NULL;
    if(selected_info)snprintf(b,sizeof(b),"CAN USE %s?",selected_info->name);
    else snprintf(b,sizeof(b),"RECIPIENTS   %u",s->unit_count);
    label(p,PAD+10,l->roster_y-20,l->sidebar-20,18,b,MUTED,9,1,0);
    for(int row=0;row<l->roster_rows&&start+row<s->unit_count;++row) {
        int idx=start+row,y=l->roster_y+row*l->side_row_height;
        const Fe8InventoryUnit *r=&s->units[idx];
        if(idx==ui->current_unit){card(p,PAD+6,y,l->sidebar-18,l->side_row_height-2,SELECTED);fill(p,PAD+6,y+3,2,l->side_row_height-8,ACCENT);}
        if(ui->dragging && (ui->drag_hover_kind==FE8_INVENTORY_HIT_ROSTER || ui->drag_hover_kind==FE8_INVENTORY_HIT_ROSTER_CLASS) && ui->drag_hover_index==idx)
            border(p,PAD+6,y,l->sidebar-18,l->side_row_height-2,ACCENT);
        int split=PAD+l->sidebar*42/100;
        label(p,PAD+12,y+4,split-PAD-16,20,r->name,idx==ui->current_unit?ACCENT:TEXT,12,idx==ui->current_unit,0);
        if(selected_info) {
            uint32_t c;use_label(r,selected_info,&c);
            fe8_inventory_desktop_use_reason(s,r,selected_info,b,sizeof(b));
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
    card(p,l->pool_x,l->search_y,l->pool_width-l->usable_width-12,30,RAISED);
    if(ui->search_active)border(p,l->pool_x,l->search_y,l->pool_width-l->usable_width-12,30,ACCENT);
    snprintf(b,sizeof(b),"%s%s",ui->query[0]?ui->query:"Search items, owners...",ui->search_active?" |":"");
    label(p,l->pool_x+10,l->search_y+6,l->pool_width-l->usable_width-32,20,b,ui->query[0]?TEXT:MUTED,12,0,0);
    snprintf(b,sizeof(b),"Usable by %s",u?u->name:"ally");
    button(p,l->pool_x+l->pool_width-l->usable_width,l->search_y,l->usable_width-8,30,b,ui->usable_only,u!=NULL);
    int counts[10];type_counts(ui,s,counts);
    int cell=l->pool_width/l->filter_columns;
    for(int i=0;i<10;++i) {
        int x=l->pool_x+i%l->filter_columns*cell,y=l->filters_y+i/l->filter_columns*l->filter_height;
        int active=ui->type_filter==i;
        if(active)card(p,x,y,cell-4,23,SELECTED);
        snprintf(b,sizeof(b),"%s %d",TYPES[i],counts[i]);
        label(p,x+7,y+4,cell-12,18,b,active?ACCENT:counts[i]?MUTED:0xFF69798D,11,active,0);
    }
    static const char *const heads[]={"Item","Type","Uses","Owner","Rk","Mt","Hit","Crit","Wt","Rng","Usability"};
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
        uint32_t use_color;use_label(u,i,&use_color);
        for(int col=1;col<11;++col)if(l->column_width[col]) {
            uint32_t c=MUTED;
            b[0]=0;
            switch(col) {
            case 1:snprintf(b,sizeof(b),"%s",TYPES[type_key(i)]);c=TYPE_COLORS[type_key(i)];break;
            case 2:uses(b,sizeof(b),e->item,i);c=(i&&i->max_uses&&(e->item>>8)*4<=i->max_uses&&!(i->attributes&8))?WARN:TEXT;break;
            case 3:snprintf(b,sizeof(b),"%s",e->unit_index>=0?s->units[e->unit_index].name:"Supply");c=TEXT;break;
            case 4:snprintf(b,sizeof(b),"%c",i?rank_letter(i->weapon_rank):'-');break;
            case 5:snprintf(b,sizeof(b),"%u",i?i->might:0);break;
            case 6:snprintf(b,sizeof(b),"%u",i?i->hit:0);break;
            case 7:snprintf(b,sizeof(b),"%u",i?i->crit:0);break;
            case 8:snprintf(b,sizeof(b),"%u",i?i->weight:0);break;
            case 9:snprintf(b,sizeof(b),"%u-%u",i?i->min_range:0,i?i->max_range:0);break;
            case 10:fe8_inventory_desktop_use_reason(s,u,i,b,sizeof(b));c=use_color;break;
            }
            label(p,l->column_x[col]+7,text_y,l->column_width[col]-10,20,b,c,11,0,0);
        }
        int own=u && e->endpoint.kind==FE8_INVENTORY_ENDPOINT_UNIT && e->endpoint.unit_address==u->address;
        int enabled=i && i->movable && (own?deposit_index(ui)>=0:u!=NULL);
        Fe8InventoryUseState readiness=fe8_inventory_item_use_state(u,i);
        int carry=!own && readiness!=FE8_INVENTORY_USE_READY && readiness!=FE8_INVENTORY_USE_ITEM;
        const char *action=!i||!i->movable?"Fixed":own?(deposit_index(ui)<0?"Full":"Store"):u&&free_slot(u)<0?"Swap":carry?"Carry":"Give";
        card(p,l->quick_x+2,y+2,l->quick_width-6,l->row_height-4,enabled?SELECTED:RAISED);
        label(p,l->quick_x+8,text_y,l->quick_width-14,20,action,enabled?(carry?GOLD:ACCENT):MUTED,11,1,0);
        if(ui->dragging && ui->drag_hover_kind==FE8_INVENTORY_HIT_POOL_ITEM && ui->drag_hover_index==visible[start+row])
            border(p,l->pool_x,y,l->pool_width-GUTTER,l->row_height,i&&i->movable?ACCENT:DANGER);
        durability(p,l->column_x[2]+7,y+l->row_height-4,l->column_width[2]-14,e->item,i);
    }
    if(!n) {
        label(p,l->pool_x+18,l->table_y+12,l->pool_width-36,30,"No matching items",TEXT,18,1,0);
        label(p,l->pool_x+18,l->table_y+46,l->pool_width-36,70,"Try another type, turn off the usability filter, or clear your search. Browsing never changes equipment.",MUTED,12,0,1);
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
static void draw_comparison(Painter *p,const Fe8InventoryUi *ui,const Fe8InventorySnapshot *s,
    const Fe8ItemInfo *candidate,int x,int y,int w,int compact) {
    Fe8InventoryEndpoint e;
    if (!candidate || !(candidate->attributes&1)) return;
    if (!fe8_inventory_desktop_comparison(ui,s,&e)) {
        label(p,x,y,w,compact?28:42,"Choose a carried weapon's vs control to compare.",MUTED,11,0,1);
        return;
    }
    const Fe8ItemInfo *base=info_at(s,e);
    int owner=owner_index(s,e);
    char b[128];
    snprintf(b,sizeof(b),"vs %s's %s",owner>=0?s->units[owner].name:"Supply",base->name);
    label(p,x,y,w,20,b,MUTED,11,1,0);
    const char *names[]={"MIGHT","HIT","WEIGHT"};
    int from[]={base->might,base->hit,base->weight};
    int to[]={candidate->might,candidate->hit,candidate->weight};
    if(compact) {
        for(int n=0;n<3;++n) {
            int delta=to[n]-from[n];
            snprintf(b,sizeof(b),"%s %+d",n==0?"Mt":n==1?"Hit":"Wt",delta);
            label(p,x+n*w/3,y+22,w/3,20,b,n==2||!delta?MUTED:delta>0?ACCENT:DANGER,12,1,0);
        }
        return;
    }
    for(int n=0;n<3;++n) {
        int xx=x+n*w/3,delta=to[n]-from[n];
        card(p,xx,y+26,w/3-6,70,RAISED);
        label(p,xx+8,y+33,w/3-16,14,names[n],MUTED,9,1,0);
        snprintf(b,sizeof(b),"%d→%d",from[n],to[n]);
        label(p,xx+8,y+51,w/3-16,20,b,TEXT,11,1,0);
        snprintf(b,sizeof(b),"%+d",delta);
        label(p,xx+8,y+74,w/3-16,18,b,n==2||!delta?MUTED:delta>0?ACCENT:DANGER,11,1,0);
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
    char b[256],reason[96];
    int x=l->detail_x+16,y=l->detail_y+12,w=l->detail_width-32;
    uint32_t use_color; use_label(u,i,&use_color);
    fe8_inventory_desktop_use_reason(s,u,i,reason,sizeof(reason));
    card(p,l->detail_x,l->detail_y,l->detail_width,l->detail_height,PANEL);
    if(l->detail_overlay)border(p,l->detail_x,l->detail_y,l->detail_width,l->detail_height,LINE);
    if(l->detail_collapsed) {
        int own=owner_index(s,e);
        if(i && encoded) {
            snprintf(b,sizeof(b),"%s · %s",i->name,own>=0?s->units[own].name:"Supply");
            label(p,x,y-4,w-110,22,b,TEXT,14,1,0);
            Fe8InventoryEndpoint base;
            if(fe8_inventory_desktop_comparison(ui,s,&base) && (i->attributes&1)) {
                const Fe8ItemInfo *a=info_at(s,base);
                snprintf(b,sizeof(b),"vs %s · Mt %+d   Hit %+d   Wt %+d · Raw item stats",a->name,
                    (int)i->might-a->might,(int)i->hit-a->hit,(int)i->weight-a->weight);
            } else snprintf(b,sizeof(b),"%s%s%s · Click vs on a weapon to compare",reason,u?" · For ":"",u?u->name:"");
            label(p,x,y+19,w-110,18,b,MUTED,11,0,0);
        } else {
            label(p,x,y-2,w-110,22,"Select an item to inspect",TEXT,14,1,0);
            label(p,x,y+20,w-110,18,"Drag to a slot, ally or Supply. Use vs to choose a comparison.",MUTED,11,0,0);
        }
        button(p,l->detail_x+l->detail_width-108,l->detail_y+9,96,30,"Details",0,1);
        return;
    }
    const char *title=NULL,*help=fe8_inventory_ui_unit_help(ui,s,&title);
    int short_panel=l->detail_height<610;
    if(!i || !encoded) {
        label(p,x,y,w,18,help?"ALLY DETAILS":"ITEM DETAILS",MUTED,10,1,0);
        label(p,x,y+30,w,64,help?title:"Choose an item",TEXT,22,1,1);
        description(p,x,y+102,w,l->action_y-y-118,help?help:
            "Browse the army's equipment or drag between loadout slots. Inspecting an item never changes the inventory.",ui->detail_scroll);
    } else {
        label(p,x,y,w,16,ui->has_selection?"MOVING ITEM":"ITEM DETAILS",ui->has_selection?ACCENT:MUTED,10,1,0);
        label(p,x,y+22,w,32,i->name,TEXT,short_panel?20:23,1,0);
        int own=owner_index(s,e);
        snprintf(b,sizeof(b),"%s · %s%s",TYPES[type_key(i)],own>=0?s->units[own].name:"Supply",i->movable?"":" · Fixed");
        label(p,x,y+58,w,20,b,MUTED,11,0,0);
        uses(b,sizeof(b),encoded,i);
        char line[128];snprintf(line,sizeof(line),"%.24s uses%s",b,(i->attributes&8)?" · Unbreakable":"");
        label(p,x,y+82,w,20,line,MUTED,11,0,0);
        durability(p,x,y+106,w,encoded,i);
        card(p,x,y+120,w,short_panel?34:52,RAISED);
        snprintf(b,sizeof(b),"FOR %s",u?u->name:"RECIPIENT");
        if(!short_panel)label(p,x+10,y+125,w-20,14,b,MUTED,9,1,0);
        label(p,x+10,y+(short_panel?120:143),w-20,22,reason,use_color,13,1,0);
        int next=y+(short_panel?150:188);
        if(i->attributes&5) {
            if(short_panel) {
                snprintf(b,sizeof(b),"Mt %u   Hit %u   Crit %u   Wt %u",i->might,i->hit,i->crit,i->weight);
                label(p,x,next,w,20,b,TEXT,11,1,0);
                snprintf(b,sizeof(b),"Range %u-%u · Rank %c",i->min_range,i->max_range,rank_letter(i->weapon_rank));
                label(p,x,next+22,w,20,b,MUTED,11,0,0); next+=48;
            } else {
                const char *names[]={"MIGHT","HIT","CRIT","WEIGHT","RANGE","RANK"};
                for(int t=0;t<6;++t) {
                    int cx=x+(t%3)*(w/3),cy=next+(t/3)*58;
                    card(p,cx,cy,w/3-6,52,RAISED);
                    label(p,cx+8,cy+7,w/3-16,14,names[t],MUTED,9,1,0);
                    switch(t) {
                    case 0:snprintf(b,sizeof(b),"%u",i->might);break;
                    case 1:snprintf(b,sizeof(b),"%u",i->hit);break;
                    case 2:snprintf(b,sizeof(b),"%u",i->crit);break;
                    case 3:snprintf(b,sizeof(b),"%u",i->weight);break;
                    case 4:snprintf(b,sizeof(b),"%u-%u",i->min_range,i->max_range);break;
                    default:snprintf(b,sizeof(b),"%c",rank_letter(i->weapon_rank));break;
                    }
                    label(p,cx+8,cy+24,w/3-16,26,b,TEXT,18,1,0);
                }
                next+=126;
            }
        }
        /* Leave the description and actions reachable even in a 640x480
           drawer. Comparison is supplemental, never a combat forecast. */
        if((i->attributes&1) && l->action_y-next>120) {
            Fe8InventoryEndpoint comp;
            int has=fe8_inventory_desktop_comparison(ui,s,&comp);
            int compact=l->action_y-next<220;
            draw_comparison(p,ui,s,i,x,next,w,compact);
            next+=has?(compact?52:106):48;
        }
        if(l->action_y-next>28) description(p,x,next,w,l->action_y-next-12,
            i->description[0]?i->description:"No description provided by this ROM.",ui->detail_scroll);
    }
    int movable=actionable && i && i->movable;
    int own=u && e.kind==FE8_INVENTORY_ENDPOINT_UNIT && e.unit_address==u->address;
    int can_give=movable && u && !own;
    int can_store=movable && e.kind!=FE8_INVENTORY_ENDPOINT_SUPPLY && deposit_index(ui)>=0;
    if(can_give)snprintf(b,sizeof(b),"%s %s",free_slot(u)<0?"Swap with":
        (i->attributes&5)&&fe8_inventory_item_use_state(u,i)!=FE8_INVENTORY_USE_READY?"Carry on":"Give to",u->name);
    else if(own)snprintf(b,sizeof(b),"Carried by %s",u->name);
    else snprintf(b,sizeof(b),"Give to recipient");
    button(p,l->action_x,l->action_y,l->action_width,28,b,1,can_give);
    button(p,l->action_x,l->action_y+32,l->action_width,28,ui->has_selection?"Cancel move":"Move / swap...",0,ui->has_selection||movable);
    button(p,l->action_x,l->action_y+64,l->action_width,28,deposit_index(ui)<0?"Supply full":"Store in supply",0,can_store);
    label(p,l->action_x,l->action_y+98,l->action_width,16,"Carrying and using are separate · Scroll details",MUTED,9,0,0);
}

static void draw_board_unit(Painter *p, const Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *s, const Fe8InventoryDesktopLayout *l,
    int n, int y, int is_pinned) {
    char b[192], reason[80];
    int tight=l->board_card_height<58;
    if (is_pinned) {
        card(p,l->board_x+3,y+2,l->board_width-9,l->board_row_height-3,0xFF1D3038);
        fill(p,l->board_x+3,y+7,2,l->board_row_height-14,ACCENT);
    }
    const Fe8InventoryUnit *u=&s->units[n];
    if(n==ui->current_unit)card(p,l->board_x+4,y+4,l->identity_width-10,l->board_card_height-8,SELECTED);
    int compact=l->identity_width<140;
    if(!compact)portrait(p,u,l->board_x+9,y+10,40,36);
    int name_x=l->board_x+(compact?10:56);
    label(p,name_x,y+(tight?3:l->board_card_height<80?6:10),l->identity_width-(compact?64:108),20,u->name,n==ui->current_unit?ACCENT:TEXT,tight?11:13,1,0);
    button(p,l->board_x+l->identity_width-48,y+6,42,18,is_pinned?"Unpin":"Pin",is_pinned,1);
    label(p,name_x,y+(l->board_card_height<80?24:30),l->identity_width-(compact?18:62),tight?12:16,u->class_name,MUTED,tight?9:10,0,0);
    experience(b,sizeof(b),u);
    label(p,l->board_x+10,y+(tight?36:l->board_card_height<80?40:48),l->identity_width-20,tight?12:16,b,MUTED,tight?8:10,0,0);
    if(l->board_card_height>=80) {
        snprintf(b,sizeof(b),"%d / 5 items",occupied(u));
        label(p,l->board_x+10,y+65,l->identity_width-20,14,b,MUTED,9,0,0);
    }
    int stats_y=y+l->board_card_height,stats_h=l->board_row_height-l->board_card_height;
    /* A stat strip spans the whole ally row; clicking or dropping here
       targets the ally, never the item slot above it. */
    card(p,l->board_x+6,stats_y,l->board_width-16,stats_h-2,
        n==ui->current_unit?SELECTED:PANEL);
    label(p,l->board_x+12,stats_y+(stats_h-16)/2,20,16,"HP",MUTED,9,0,0);
    snprintf(b,sizeof(b),"%u/%d",u->hp,fe8_inventory_stat_value(u,FE8_STAT_MAX_HP,ui->stats_base));
    label(p,l->board_x+33,stats_y+(stats_h-16)/2,56,16,b,ACCENT,11,1,0);
    draw_unit_stats(p,ui,u,l->board_x+96,stats_y,l->board_width-112,8,stats_h);
    for(int j=0;j<5;++j) {
        int x=l->board_x+l->identity_width+j*l->slot_width,w=l->slot_width-6;
        Fe8InventoryEndpoint e={FE8_INVENTORY_ENDPOINT_UNIT,u->address,(unsigned)j};
        const Fe8ItemInfo *i=&u->item_info[j];
        Fe8InventoryListEntry entry={e,i,u->items[j],n};
        int match=entry_matches(ui,s,&entry,1);
        int chosen=ui->has_detail&&same(ui->detail,e);
        int source=ui->has_selection&&same(ui->selected,e);
        int compared=ui->has_comparison&&same(ui->comparison,e);
        int drop=ui->dragging&&ui->drag_hover_kind==FE8_INVENTORY_HIT_LOADOUT_ITEM&&ui->drag_hover_index==n*5+j;
        card(p,x,y+4,w,l->board_card_height-8,chosen||source?SELECTED:RAISED);
        if(chosen||source)fill(p,x,y+8,2,l->board_card_height-16,ACCENT);
        if(compared)border(p,x,y+4,w,l->board_card_height-8,GOLD);
        if(ui->flash_ticks && same(ui->flash,e))border(p,x,y+4,w,l->board_card_height-8,GOLD);
        if(drop)border(p,x,y+4,w,l->board_card_height-8,!u->items[j]||i->movable?ACCENT:DANGER);
        if(u->items[j]) {
            label(p,x+8,y+(tight?4:l->board_card_height<80?6:12),w-((i->attributes&1)?38:16),tight?24:28,i->name,match?TEXT:MUTED,tight?10:w<130?11:12,chosen,1);
            if(i->attributes&1)label(p,x+w-22,y+10,20,18,"vs",compared?GOLD:MUTED,10,1,0);
            uses(b,sizeof(b),u->items[j],i);
            label(p,x+8,y+(tight?28:l->board_card_height<80?32:42),w-16,tight?16:18,b,match?TYPE_COLORS[type_key(i)]:MUTED,tight?10:11,1,0);
            if(w>=130) {
                uint32_t color;use_label(u,i,&color);
                fe8_inventory_desktop_use_reason(s,u,i,reason,sizeof(reason));
                label(p,x+62,y+(tight?28:l->board_card_height<80?32:42),w-68,tight?16:18,i->movable?reason:"Fixed",color,10,0,0);
            }
            durability(p,x+8,y+l->board_card_height-(tight?4:l->board_card_height<80?6:14),w-16,u->items[j],i);
            if(l->board_card_height>=96)label(p,x+8,y+63,w-16,18,TYPES[type_key(i)],MUTED,10,0,0);
        } else {
            label(p,x+8,y+24,w-16,20,ui->has_selection?"+ Drop here":"+ Empty",ui->has_selection?ACCENT:MUTED,12,0,0);
        }
    }
}

static void draw_board(Painter *p,const Fe8InventoryUi *ui,const Fe8InventorySnapshot *s,
    const Fe8InventoryDesktopLayout *l) {
    char b[192]; int counts[10]; type_counts(ui,s,counts);
    card(p,l->board_x,l->top,l->board_width,l->bottom-l->top,PANEL);
    int search_w=l->board_width-180;
    card(p,l->board_x,l->top,search_w,30,RAISED);
    if(ui->search_active)border(p,l->board_x,l->top,search_w,30,ACCENT);
    snprintf(b,sizeof(b),"%s%s",ui->query[0]?ui->query:"Find units, classes or equipment...",ui->search_active?" |":"");
    label(p,l->board_x+10,l->top+6,search_w-20,20,b,ui->query[0]?TEXT:MUTED,12,0,0);
    const Fe8InventoryUnit *recipient=target(ui,s);
    snprintf(b,sizeof(b),"Usable by %s",recipient?recipient->name:"ally");
    button(p,l->board_x+l->board_width-172,l->top,164,30,b,ui->usable_only,recipient!=NULL);
    int cols=l->board_width>=820?10:5,cell=l->board_width/cols;
    for(int n=0;n<10;++n) {
        int x=l->board_x+(n%cols)*cell,y=l->top+38+(n/cols)*26;
        if(ui->type_filter==n)card(p,x,y,cell-4,23,SELECTED);
        snprintf(b,sizeof(b),"%s %d",TYPES[n],counts[n]);
        label(p,x+7,y+4,cell-12,18,b,ui->type_filter==n?ACCENT:counts[n]?MUTED:0xFF69798D,11,ui->type_filter==n,0);
    }
    label(p,l->board_x+10,l->board_y-20,l->identity_width-14,18,"ALLY / RECIPIENT",MUTED,9,1,0);
    for(int slot=0;slot<5;++slot) {
        snprintf(b,sizeof(b),"SLOT %d",slot+1);
        label(p,l->board_x+l->identity_width+slot*l->slot_width+8,l->board_y-20,l->slot_width-16,18,b,MUTED,9,1,0);
    }
    Fe8InventoryBoardView v;
    fe8_inventory_board_view(ui,s,l,&v);
    Fe8InventoryDesktopLayout row_layout=*l;
    row_layout.board_row_height=v.row_height;
    row_layout.board_card_height=v.card_height;
    if (v.pinned_count) {
        int right=l->board_x+l->board_width, header=l->board_y-22;
        fill(p,l->board_x,header,l->board_width,22,PANEL);
        snprintf(b,sizeof(b),"PINNED · %d%s",v.pinned_count,l->board_width>=800?" · Stays visible while you scroll":"");
        label(p,l->board_x+10,header+2,l->board_width-230,18,b,ACCENT,10,1,0);
        button(p,right-82,header,76,20,"Unpin all",0,1);
        if (v.pinned_count>v.pinned_rows && v.pinned_rows>0) {
            button(p,right-212,header,28,20,"<",0,v.pinned_start>0);
            snprintf(b,sizeof(b),"%d–%d / %d",v.pinned_start+1,v.pinned_start+v.pinned_rows,v.pinned_count);
            label(p,right-180,header+3,60,16,b,MUTED,9,0,0);
            button(p,right-118,header,28,20,">",0,v.pinned_start+v.pinned_rows<v.pinned_count);
        }
        for (int row=0;row<v.pinned_rows;++row)
            draw_board_unit(p,ui,s,&row_layout,v.pinned[v.pinned_start+row],v.top+row*v.row_height,1);
        if (v.other_total) {
            int yy=v.other_y-22;
            fill(p,l->board_x+6,yy,l->board_width-14,1,LINE);
            if (ui->query[0]) snprintf(b,sizeof(b),"MATCHING UNITS · %d / %d",v.other_count,v.other_total);
            else snprintf(b,sizeof(b),"OTHER UNITS · %d",v.other_count);
            label(p,l->board_x+10,yy+4,180,16,b,MUTED,9,1,0);
            label(p,l->board_x+190,yy+4,l->board_width-206,16,"Scroll roster · Pins stay above",MUTED,9,0,0);
        }
    }
    for (int row=0;row<v.other_rows && v.other_start+row<v.other_count;++row)
        draw_board_unit(p,ui,s,&row_layout,v.others[v.other_start+row],v.other_y+row*v.row_height,0);
    scroll_mark(p,l->board_x+l->board_width-4,v.other_y,v.other_rows*v.row_height,
        v.other_count,v.other_rows,v.other_start);
    if (!v.other_count && v.other_total && v.other_rows) {
        int h=l->deposit_y-v.other_y;
        label(p,l->board_x+18,v.other_y+8,l->board_width-36,26,
            v.match_count ? "Matching units are pinned above" : "No matching units",TEXT,16,1,0);
        if (h>=64) label(p,l->board_x+18,v.other_y+38,l->board_width-36,h-42,
            "Try a unit name, class or carried item. Clear filters below to show the roster.",MUTED,12,0,1);
    }
    if(l->supply_width) {
        card(p,l->supply_x,l->top,l->supply_width,l->bottom-l->top,PANEL);
        label(p,l->supply_x+14,l->top+8,150,26,"Supply",TEXT,20,1,0);
        snprintf(b,sizeof(b),"%u / %u",s->supply_count,s->supply_capacity);
        label(p,l->supply_x+l->supply_width-88,l->top+12,74,20,b,GOLD,11,1,0);
        snprintf(b,sizeof(b),"Give to %s",recipient?recipient->name:"selected ally");
        label(p,l->supply_x+14,l->top+43,l->supply_width-28,24,b,ACCENT,12,1,0);
        label(p,l->supply_x+14,l->board_y-20,l->supply_width-28,18,"OR DRAG TO ANY LOADOUT SLOT",MUTED,9,1,0);
        int indices[FE8_INVENTORY_POOL_CAPACITY],n=supply_visible(ui,s,indices);
        int rows=(l->deposit_y-l->board_y)/32,first=offset(ui->supply_scroll,n,rows);
        for(int row=0;row<rows && first+row<n;++row) {
            const Fe8InventoryListEntry *e=&ui->pool[indices[first+row]];
            int y=l->board_y+row*32;
            int selected=ui->has_detail&&same(ui->detail,e->endpoint);
            if(row%2||selected)fill(p,l->supply_x+4,y,l->supply_width-8,31,selected?SELECTED:RAISED);
            label(p,l->supply_x+12,y+5,l->supply_width-74,20,e->info->name,TEXT,12,selected,0);
            int enabled=e->info->movable&&recipient;
            int carry=recipient && (e->info->attributes&5) && fe8_inventory_item_use_state(recipient,e->info)!=FE8_INVENTORY_USE_READY;
            label(p,l->supply_x+l->supply_width-52,y+5,44,20,!e->info->movable?"Fixed":recipient&&free_slot(recipient)<0?"Swap":carry?"Carry":"Give",enabled?(carry?WARN:ACCENT):MUTED,11,1,0);
            durability(p,l->supply_x+12,y+27,l->supply_width-76,e->item,e->info);
        }
        if(!n) {
            label(p,l->supply_x+18,l->board_y+14,l->supply_width-36,28,s->supply_count?"No matching items":"Supply is empty",TEXT,16,1,0);
            label(p,l->supply_x+18,l->board_y+54,l->supply_width-36,76,"Drag carried equipment to the Supply bar below to store it.",MUTED,12,0,1);
        }
        scroll_mark(p,l->supply_x+l->supply_width-4,l->board_y,rows*32,n,rows,first);
    }
    fill(p,PAD,l->deposit_y,l->width-2*PAD,1,LINE);
    if (ui->query[0]) {
        if (v.pinned_count) snprintf(b,sizeof(b),"%d match%s · %d pinned · Clear filters",v.match_count,v.match_count==1?"":"es",v.pinned_count);
        else snprintf(b,sizeof(b),"%d matching unit%s · Clear filters",v.match_count,v.match_count==1?"":"s");
    } else snprintf(b,sizeof(b),"%s",ui->type_filter||ui->usable_only?"Matching items highlighted · Clear filters":"All five slots stay visible · Drag between allies");
    label(p,PAD+8,l->deposit_y+8,l->board_width-196,20,b,MUTED,11,0,0);
    int sx=l->supply_width?l->supply_x:l->board_x+l->board_width-188;
    int sw=l->supply_width?l->supply_width:188;
    card(p,sx,l->deposit_y+2,sw,28,SELECTED);
    snprintf(b,sizeof(b),"%s",deposit_index(ui)<0?"Supply full":ui->has_selection?"Drop to store in Supply":"Browse Supply →");
    label(p,sx+12,l->deposit_y+8,sw-24,20,b,deposit_index(ui)<0?MUTED:ACCENT,11,1,0);
}
static void draw_swap(Painter *p,const Fe8InventoryUi *ui,const Fe8InventorySnapshot *s,
    const Fe8InventoryDesktopLayout *l) {
    if(!ui->popup_open||!ui->has_selection)return;
    int n=unit_at_address(s,ui->popup_unit_address);
    if(n<0)return;
    const Fe8InventoryUnit *u=&s->units[n];
    const Fe8ItemInfo *source=info_at(s,ui->popup_source);
    int from=owner_index(s,ui->popup_source);
    const char *owner=from>=0?s->units[from].name:"Supply";
    int x=l->popup_x,y=l->popup_y,w=l->popup_width,h=l->popup_height;
    /* A modal chooser consumes outside clicks. Dim the underlying workspace so
       it does not look like those destinations are still active. Keep padding
       pixels untouched for both full framebuffers and sub-canvas renderers. */
    fe8_host_text_end(&p->text);
    for (int yy=0;yy<p->height;++yy) for (int xx=0;xx<p->width;++xx) {
        uint32_t c=p->pixels[yy*p->stride+xx];
        unsigned r=(c&255)*3/5,g=((c>>8)&255)*3/5,b=((c>>16)&255)*3/5;
        p->pixels[yy*p->stride+xx]=(c&0xFF000000)|(b<<16)|(g<<8)|r;
    }
    if(!fe8_host_text_begin(&p->text,p->pixels,p->stride,p->width,p->height))return;
    card(p,x+4,y+5,w,h,BG);card(p,x,y,w,h,RAISED);border(p,x,y,w,h,ACCENT);
    char b[160];snprintf(b,sizeof(b),"Swap with %s",u->name);
    label(p,x+16,y+12,w-64,26,b,TEXT,20,1,0);
    label(p,x+w-30,y+15,20,20,"×",MUTED,17,0,0);
    snprintf(b,sizeof(b),"%s → %s",source?source->name:"Item",u->name);
    label(p,x+16,y+44,w-32,22,b,ACCENT,12,1,0);
    int hover=(int)(ui->pointer_y/p->scale)-l->popup_rows_y;
    for(int j=0;j<5;++j) {
        int yy=l->popup_rows_y+j*40;
        const Fe8ItemInfo *i=&u->item_info[j];
        int enabled=!u->items[j]||i->movable;
        int active=hover>=j*40&&hover<(j+1)*40 &&
            (int)(ui->pointer_x/p->scale)>=x+12 && (int)(ui->pointer_x/p->scale)<x+w-12;
        card(p,x+12,yy,w-24,37,active?SELECTED:PANEL);
        snprintf(b,sizeof(b),"%d  %s",j+1,u->items[j]?i->name:"Empty slot");
        label(p,x+22,yy+4,w-48,20,b,enabled?TEXT:MUTED,12,1,0);
        snprintf(b,sizeof(b),"%s%s%s",!enabled?"Fixed · cannot exchange":u->items[j]?"→ ":"Give without exchange",enabled&&u->items[j]?owner:"","");
        label(p,x+38,yy+22,w-64,16,b,enabled?MUTED:DANGER,10,0,0);
    }
    label(p,x+16,y+h-27,w-32,18,"Choose an exchange · Esc or outside click cancels",MUTED,10,0,0);
}

/* Geometry-only hover leaves selection, transfers and stat values unchanged. */
static void draw_stat_help(Painter *p, const Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *s, const Fe8InventoryDesktopLayout *l) {
    static const char *const names[]={"Power","Skill","Speed","Luck","Defense",
        "Resistance","Constitution","Movement","Maximum HP"};
    if(ui->popup_open || ui->dragging || l->detail_overlay) return;
    int x=(int)(ui->pointer_x/p->scale),y=(int)(ui->pointer_y/p->scale),stat=-1;
    const Fe8InventoryUnit *u=NULL;
    if(!ui->by_unit) {
        u=target(ui,s);
        if(u && in(x,y,PAD+10,l->stats_y,l->sidebar-24,2*l->stat_row_height))
            stat=(y-l->stats_y)/l->stat_row_height*4+(x-PAD-10)*4/(l->sidebar-24);
        else if(u && in(x,y,PAD+94,l->top+68,l->sidebar-104,16)) stat=FE8_STAT_MAX_HP;
    } else if(in(x,y,l->board_x,l->board_y,l->board_width,l->deposit_y-l->board_y)) {
        Fe8InventoryBoardView v;
        fe8_inventory_board_view(ui,s,l,&v);
        int yy=0, n=fe8_inventory_board_unit_at(&v,y,&yy);
        int top=yy+v.card_height;
        if(n>=0 && y>=top) {
            u=&s->units[n];
            if(in(x,y,l->board_x+96,top,l->board_width-112,v.row_height-v.card_height))
                stat=(x-l->board_x-96)*8/(l->board_width-112);
            else if(in(x,y,l->board_x+12,top,77,v.row_height-v.card_height)) stat=FE8_STAT_MAX_HP;
        }
    }
    int header=in(x,y,344,14,98,30);
    if(!header && (!u || stat<0 || stat>=FE8_STAT_COUNT)) return;
    int left=clamp(x+12,12,l->width-352),top=clamp(y+18,48,l->height-116);
    char title[96],detail[160];
    const char *note;
    if(header) {
        snprintf(title,sizeof(title),"Base / total unit stats");
        snprintf(detail,sizeof(detail),"Click to switch. Hover a stat for its net modifier.");
        note="Totals use the ROM's current stat-screen rules. Base only means no verified calculation is available.";
    } else {
        int base=fe8_inventory_stat_base(u,(Fe8UnitStat)stat);
        int total=fe8_inventory_stat_value(u,(Fe8UnitStat)stat,false);
        snprintf(title,sizeof(title),"%s · %s",u->name,names[stat]);
        if(u->effective_stats_valid) {
            snprintf(detail,sizeof(detail),"Total %d = Base %d %+d",total,base,total-base);
            note="Net gear, skills and active effects. Not a combat forecast; battle-only effects are excluded.";
        } else {
            snprintf(detail,sizeof(detail),"Base %d · Total unavailable",base);
            note="This ROM or game state has no verified calculation. Equipment and skill bonuses are not included.";
        }
    }
    card(p,left,top,340,104,RAISED);border(p,left,top,340,104,LINE);
    label(p,left+12,top+9,316,20,title,TEXT,13,1,0);
    label(p,left+12,top+33,316,20,detail,ACCENT,12,1,0);
    label(p,left+12,top+59,316,40,note,MUTED,11,0,1);
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
    button(&p,160,14,86,30,"By item",!ui->by_unit,1);
    button(&p,250,14,86,30,"By unit",ui->by_unit,1);
    unsigned verified=0;
    for(unsigned n=0;n<s->unit_count;++n) verified+=s->units[n].effective_stats_valid;
    button(&p,344,14,98,30,ui->stats_base?"Base stats":!verified?"Base only":
        verified==s->unit_count?"Total stats":"Mixed stats",!ui->stats_base&&verified,1);
    if(l.width>=900) {
        snprintf(b,sizeof(b),"%u allies · Supply %u / %u",s->unit_count,s->supply_count,s->supply_capacity);
        label(&p,l.width-444,19,236,22,b,GOLD,12,1,0);
    }
    if(l.detail_collapsed || l.detail_overlay)button(&p,l.width-194,14,98,28,ui->details_expanded?"Hide details":"Details",ui->details_expanded,1);
    button(&p,l.width-88,14,76,28,"Close",0,1);
    if(l.height>=600) {
        label(&p,PAD+4,54,ui->by_unit?l.width-32:l.sidebar,18,ui->by_unit?"ARMY LOADOUTS · Pin allies to keep their loadouts visible":"RECIPIENT / LOADOUT",MUTED,10,1,0);
        if(!ui->by_unit)label(&p,l.pool_x,54,l.pool_width,18,ui->has_selection?"MOVING ITEM · Choose a destination or cancel":"Click to inspect · Double-click to transfer · Drag to move",ui->has_selection?ACCENT:MUTED,10,1,0);
    }
    if(ui->by_unit) draw_board(&p,ui,s,&l);
    else { draw_sidebar(&p,ui,s,&l); draw_pool(&p,ui,s,&l); }
    draw_detail(&p,ui,s,&l);
    label(&p,PAD+4,l.height-29,l.width-158,22,ui->status[0]?ui->status:"Your game is paused. Drag to transfer; use vs to compare.",ui->has_selection?ACCENT:MUTED,11,0,0);
    snprintf(b,sizeof(b),"Undo %d · U",ui->undo_count);
    button(&p,l.width-130,l.height-34,118,28,b,0,ui->undo_count>0);
    if(ui->dragging) {
        const Fe8ItemInfo *drag=info_at(s,ui->drag_source);
        int x=clamp((int)(ui->drag_x/p.scale)+14,8,l.width-224);
        int y=clamp((int)(ui->drag_y/p.scale)+16,8,l.height-54);
        card(&p,x,y,216,46,SELECTED);border(&p,x,y,216,46,ACCENT);
        label(&p,x+10,y+6,196,20,drag?drag->name:"Moving item",TEXT,13,1,0);
        label(&p,x+10,y+27,196,16,"Drop to transfer · Esc cancels",ACCENT,10,0,0);
    }
    draw_stat_help(&p,ui,s,&l);
    draw_swap(&p,ui,s,&l);
    fe8_host_text_end(&p.text);
}
