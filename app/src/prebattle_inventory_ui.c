#include "prebattle_inventory_ui.h"
#include "host_text.h"
#include "inventory_desktop.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define FE8_ITEM_ATTRIBUTE_WEAPON UINT32_C(1u << 0)
#define FE8_ITEM_ATTRIBUTE_STAFF UINT32_C(1u << 2)
#define FE8_ITEM_ATTRIBUTE_UNBREAKABLE UINT32_C(1u << 3)

enum {
    MARGIN = 8,
    HEADER_H = 38,
    FOOTER_H = 74,
    ROSTER_ROW_H = 30,
    POOL_ROW_H = 32,
    SECTION_HEADER_H = 22,
    POOL_HEADER_H = 40,
    SCROLLBAR_W = 5,
    UNIT_CARD_H = 72,
    USE_BADGE_W = 44,
    TYPE_BADGE_W = 34,
    USES_BOX_W = 30,
};

typedef struct Fe8InventoryUiLayout {
    int width;
    int height;
    int top;
    int bottom;
    int roster_width;
    int unit_width;
    int pool_x;
    int pool_width;
    int scope_x;
    int scope_width;
    int sort_x;
    int sort_width;
    int item_row_height;
    int detail_x;
    int detail_width;
    int name_y;
    int class_y;
} Fe8InventoryUiLayout;

static Fe8HostTextCanvas *text_canvas;
static int render_scale = 1;

static int clamp_int(int value, int minimum, int maximum) {
    if (value < minimum)
        return minimum;
    if (value > maximum)
        return maximum;
    return value;
}

static uint32_t canvas_color(uint32_t color) {
    return (color & UINT32_C(0xFF00FF00)) |
        ((color & UINT32_C(0x00FF0000)) >> 16) |
        ((color & UINT32_C(0x000000FF)) << 16);
}

static void rect(uint32_t *pixels, int stride, int width, int height,
    int x, int y, int rect_width, int rect_height, uint32_t color) {
    int row;
    x *= render_scale;
    y *= render_scale;
    rect_width *= render_scale;
    rect_height *= render_scale;
    width *= render_scale;
    height *= render_scale;
    if (x < 0) {
        rect_width += x;
        x = 0;
    }
    if (y < 0) {
        rect_height += y;
        y = 0;
    }
    if (x + rect_width > width)
        rect_width = width - x;
    if (y + rect_height > height)
        rect_height = height - y;
    if (rect_width <= 0 || rect_height <= 0)
        return;
    color = canvas_color(color);
    for (row = y; row < y + rect_height; ++row) {
        int column;
        for (column = x; column < x + rect_width; ++column)
            pixels[row * stride + column] = color;
    }
}

static void text_box(uint32_t *pixels, int stride, int width, int height,
    int x, int y, int box_width, const char *value, uint32_t color,
    int scale, int maximum_characters) {
    char clipped[160];
    size_t length;
    (void)pixels;
    (void)stride;
    (void)width;
    (void)height;
    if (!text_canvas || !value || box_width <= 0)
        return;
    length = strlen(value);
    if (maximum_characters > 0 && length > (size_t)maximum_characters)
        length = (size_t)maximum_characters;
    if (length >= sizeof(clipped))
        length = sizeof(clipped) - 1;
    memcpy(clipped, value, length);
    clipped[length] = '\0';
    fe8_host_text_draw(text_canvas, x * render_scale, y * render_scale,
        box_width * render_scale, (scale == 2 ? 20 : 14) * render_scale,
        clipped, (scale == 2 ? 16.0f : 9.5f) * render_scale, canvas_color(color),
        scale == 2 ? FE8_HOST_TEXT_SEMIBOLD : FE8_HOST_TEXT_REGULAR, 0);
}

static int body_top(void) {
    return MARGIN + HEADER_H;
}

static int body_bottom(int height) {
    /* Spend extra height on readable help, without taking any of the five
       loadout slots away at the minimum 480x320 logical canvas. */
    return height - MARGIN - clamp_int(FOOTER_H + (height - 320) / 3,
        FOOTER_H, 104);
}

static void layout_for(int width, int height, Fe8InventoryUiLayout *layout) {
    int inner_width = width - MARGIN * 2;
    int roster_width = clamp_int(inner_width * 21 / 100, 92, 126);
    int unit_width = clamp_int(inner_width * 41 / 100, 180, 260);
    int pool_width = inner_width - roster_width - unit_width;

    if (pool_width < 112) {
        int needed = 112 - pool_width;
        int unit_reduction = needed < unit_width - 164 ? needed : unit_width - 164;
        unit_width -= unit_reduction;
        needed -= unit_reduction;
        if (needed > 0)
            roster_width -= needed < roster_width - 82 ? needed : roster_width - 82;
        pool_width = inner_width - roster_width - unit_width;
    }
    memset(layout, 0, sizeof(*layout));
    layout->width = width;
    layout->height = height;
    layout->top = body_top();
    layout->bottom = body_bottom(height);
    layout->roster_width = roster_width;
    layout->unit_width = unit_width;
    layout->pool_x = MARGIN + roster_width + unit_width;
    layout->pool_width = width - MARGIN - layout->pool_x;
    layout->scope_x = layout->pool_x + 6;
    layout->scope_width = (layout->pool_width - 20) / 2;
    layout->sort_x = layout->scope_x + layout->scope_width + 4;
    layout->sort_width = layout->scope_width;
    layout->item_row_height = clamp_int(
        (layout->bottom - layout->top - UNIT_CARD_H) / FE8_INVENTORY_ITEM_SLOTS,
        24, 32);
    layout->detail_x = MARGIN + roster_width + 88;
    layout->detail_width = unit_width - 94;
    layout->name_y = layout->top + 4;
    layout->class_y = layout->top + 20;
}

static int roster_rows(const Fe8InventoryUiLayout *layout) {
    int rows = (layout->bottom - layout->top - SECTION_HEADER_H) /
        ROSTER_ROW_H;
    return rows > 0 ? rows : 0;
}

static int pool_rows(const Fe8InventoryUiLayout *layout) {
    int rows = (layout->bottom - layout->top - POOL_HEADER_H) / POOL_ROW_H;
    return rows > 0 ? rows : 0;
}

static int roster_offset(const Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *snapshot, const Fe8InventoryUiLayout *layout) {
    int maximum = snapshot->unit_count - roster_rows(layout);
    return clamp_int(ui->roster_scroll, 0, maximum > 0 ? maximum : 0);
}

static int pool_offset(const Fe8InventoryUi *ui,
    const Fe8InventoryUiLayout *layout) {
    int maximum = ui->pool_count - pool_rows(layout);
    return clamp_int(ui->pool_scroll, 0, maximum > 0 ? maximum : 0);
}

static int endpoint_equal(Fe8InventoryEndpoint first,
    Fe8InventoryEndpoint second) {
    return first.kind == second.kind &&
        first.unit_address == second.unit_address &&
        first.slot == second.slot;
}

static const Fe8InventoryUnit *target_unit(const Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *snapshot) {
    if (!ui || !snapshot || ui->current_unit < 0 ||
            ui->current_unit >= snapshot->unit_count)
        return NULL;
    return &snapshot->units[ui->current_unit];
}

static const Fe8ItemInfo *endpoint_info(const Fe8InventorySnapshot *snapshot,
    Fe8InventoryEndpoint endpoint) {
    unsigned unit_index;
    if (!snapshot)
        return NULL;
    if (endpoint.kind == FE8_INVENTORY_ENDPOINT_SUPPLY)
        return endpoint.slot < snapshot->supply_capacity ?
            &snapshot->supply_info[endpoint.slot] : NULL;
    for (unit_index = 0; unit_index < snapshot->unit_count; ++unit_index) {
        if (snapshot->units[unit_index].address == endpoint.unit_address)
            return endpoint.slot < FE8_INVENTORY_ITEM_SLOTS ?
                &snapshot->units[unit_index].item_info[endpoint.slot] : NULL;
    }
    return NULL;
}

static int endpoint_unit_index(const Fe8InventorySnapshot *snapshot,
    Fe8InventoryEndpoint endpoint) {
    unsigned unit_index;
    if (!snapshot || endpoint.kind != FE8_INVENTORY_ENDPOINT_UNIT)
        return -1;
    for (unit_index = 0; unit_index < snapshot->unit_count; ++unit_index) {
        if (snapshot->units[unit_index].address == endpoint.unit_address)
            return (int)unit_index;
    }
    return -1;
}

uint16_t fe8_inventory_ui_endpoint_item(const Fe8InventorySnapshot *snapshot,
    Fe8InventoryEndpoint endpoint) {
    unsigned unit_index;
    if (!snapshot)
        return 0;
    if (endpoint.kind == FE8_INVENTORY_ENDPOINT_SUPPLY)
        return endpoint.slot < snapshot->supply_capacity ?
            snapshot->supply[endpoint.slot] : 0;
    for (unit_index = 0; unit_index < snapshot->unit_count; ++unit_index) {
        if (snapshot->units[unit_index].address == endpoint.unit_address)
            return endpoint.slot < FE8_INVENTORY_ITEM_SLOTS ?
                snapshot->units[unit_index].items[endpoint.slot] : 0;
    }
    return 0;
}

int fe8_inventory_ui_endpoint_movable(const Fe8InventorySnapshot *snapshot,
    Fe8InventoryEndpoint endpoint) {
    const Fe8ItemInfo *info = endpoint_info(snapshot, endpoint);
    return !info || !info->id || info->movable;
}

static Fe8InventoryListEntry entry_for_endpoint(
    const Fe8InventorySnapshot *snapshot, Fe8InventoryEndpoint endpoint) {
    Fe8InventoryListEntry entry;
    memset(&entry, 0, sizeof(entry));
    entry.endpoint = endpoint;
    entry.info = endpoint_info(snapshot, endpoint);
    entry.item = fe8_inventory_ui_endpoint_item(snapshot, endpoint);
    entry.unit_index = endpoint_unit_index(snapshot, endpoint);
    return entry;
}

static int text_compare_casefold(const char *first, const char *second) {
    unsigned char a;
    unsigned char b;
    if (!first)
        first = "";
    if (!second)
        second = "";
    do {
        a = (unsigned char)tolower((unsigned char)*first++);
        b = (unsigned char)tolower((unsigned char)*second++);
        if (a != b)
            return a < b ? -1 : 1;
    } while (a != 0);
    return 0;
}

static int entry_type_key(const Fe8InventoryListEntry *entry) {
    if (!entry->item)
        return 100;
    if (entry->info &&
            (entry->info->attributes & (FE8_ITEM_ATTRIBUTE_WEAPON |
                FE8_ITEM_ATTRIBUTE_STAFF)) != 0) {
        if (entry->info->weapon_type < FE8_INVENTORY_WEAPON_TYPES)
            return entry->info->weapon_type;
        return FE8_INVENTORY_WEAPON_TYPES;
    }
    return FE8_INVENTORY_WEAPON_TYPES + 1;
}

static int entry_uses_key(const Fe8InventoryListEntry *entry) {
    if (!entry->item)
        return -1;
    if (entry->info &&
            (entry->info->attributes & FE8_ITEM_ATTRIBUTE_UNBREAKABLE) != 0)
        return 1000;
    return entry->item >> 8;
}

static int entry_owner_key(const Fe8InventoryListEntry *entry) {
    return entry->unit_index >= 0 ? entry->unit_index : 1000;
}

static int compare_entries(const Fe8InventoryListEntry *first,
    const Fe8InventoryListEntry *second, Fe8InventorySort sort) {
    int comparison;
    int first_key;
    int second_key;
    if (!first->item || !second->item) {
        if (!first->item && !second->item)
            return first->endpoint.slot < second->endpoint.slot ? -1 :
                first->endpoint.slot > second->endpoint.slot ? 1 : 0;
        return first->item ? -1 : 1;
    }
    switch (sort) {
    case FE8_INVENTORY_SORT_NAME:
        comparison = text_compare_casefold(
            first->info ? first->info->name : "",
            second->info ? second->info->name : "");
        if (comparison)
            return comparison;
        break;
    case FE8_INVENTORY_SORT_USES:
        first_key = entry_uses_key(first);
        second_key = entry_uses_key(second);
        if (first_key != second_key)
            return first_key > second_key ? -1 : 1;
        break;
    case FE8_INVENTORY_SORT_OWNER:
        first_key = entry_owner_key(first);
        second_key = entry_owner_key(second);
        if (first_key != second_key)
            return first_key < second_key ? -1 : 1;
        if (first->endpoint.slot != second->endpoint.slot)
            return first->endpoint.slot < second->endpoint.slot ? -1 : 1;
        break;
    case FE8_INVENTORY_SORT_TYPE:
    default:
        first_key = entry_type_key(first);
        second_key = entry_type_key(second);
        if (first_key != second_key)
            return first_key < second_key ? -1 : 1;
        if (first->info && second->info &&
                first->info->weapon_rank != second->info->weapon_rank)
            return first->info->weapon_rank < second->info->weapon_rank ? -1 : 1;
        break;
    }
    comparison = text_compare_casefold(
        first->info ? first->info->name : "",
        second->info ? second->info->name : "");
    if (comparison)
        return comparison;
    first_key = entry_owner_key(first);
    second_key = entry_owner_key(second);
    if (first_key != second_key)
        return first_key < second_key ? -1 : 1;
    if (first->endpoint.slot != second->endpoint.slot)
        return first->endpoint.slot < second->endpoint.slot ? -1 : 1;
    return 0;
}

static void sort_pool(Fe8InventoryUi *ui) {
    int index;
    for (index = 1; index < ui->pool_count; ++index) {
        Fe8InventoryListEntry entry = ui->pool[index];
        int insertion = index;
        while (insertion > 0 &&
                compare_entries(&entry, &ui->pool[insertion - 1],
                    ui->pool_sort) < 0) {
            ui->pool[insertion] = ui->pool[insertion - 1];
            --insertion;
        }
        ui->pool[insertion] = entry;
    }
}

static void append_endpoint(Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *snapshot, Fe8InventoryEndpoint endpoint) {
    if (ui->pool_count >= FE8_INVENTORY_POOL_CAPACITY)
        return;
    ui->pool[ui->pool_count++] = entry_for_endpoint(snapshot, endpoint);
}

void fe8_inventory_ui_rebuild(Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *snapshot) {
    unsigned unit_index;
    if (!ui)
        return;
    ui->pool_count = 0;
    ui->hover_kind = FE8_INVENTORY_HIT_NONE;
    ui->hover_unit_address = 0;
    ui->has_inspected = 0;
    if (!snapshot) {
        ui->pool_scroll = 0;
        return;
    }
    if (ui->current_unit < 0 || ui->current_unit >= snapshot->unit_count)
        ui->current_unit = snapshot->unit_count ? 0 : -1;
    if (ui->pool_scope == FE8_INVENTORY_POOL_ALL) {
        for (unit_index = 0; unit_index < snapshot->unit_count; ++unit_index) {
            unsigned slot;
            for (slot = 0; slot < FE8_INVENTORY_ITEM_SLOTS; ++slot) {
                if (snapshot->units[unit_index].items[slot]) {
                    Fe8InventoryEndpoint endpoint = {
                        FE8_INVENTORY_ENDPOINT_UNIT,
                        snapshot->units[unit_index].address,
                        slot,
                    };
                    append_endpoint(ui, snapshot, endpoint);
                }
            }
        }
    }
    for (unit_index = 0; unit_index < snapshot->supply_display_count;
            ++unit_index) {
        Fe8InventoryEndpoint endpoint = {
            FE8_INVENTORY_ENDPOINT_SUPPLY,
            snapshot->supply_address,
            snapshot->supply_display_slots[unit_index],
        };
        append_endpoint(ui, snapshot, endpoint);
    }
    sort_pool(ui);
    if (ui->pool_scroll >= ui->pool_count)
        ui->pool_scroll = ui->pool_count > 0 ? ui->pool_count - 1 : 0;
    if (ui->has_inspected &&
            !fe8_inventory_ui_endpoint_item(snapshot, ui->inspected))
        ui->has_inspected = 0;
}

void fe8_inventory_ui_init(Fe8InventoryUi *ui) {
    memset(ui, 0, sizeof(*ui));
    ui->current_unit = 0;
    ui->render_scale = 1;
    ui->pool_scope = FE8_INVENTORY_POOL_ALL;
    ui->pool_sort = FE8_INVENTORY_SORT_TYPE;
}

void fe8_inventory_ui_open(Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *snapshot) {
    if (!ui)
        return;
    ui->active = 1;
    ui->roster_scroll = 0;
    ui->pool_scroll = 0;
    ui->current_unit = snapshot && snapshot->unit_count ? 0 : -1;
    ui->pool_scope = FE8_INVENTORY_POOL_ALL;
    ui->pool_sort = FE8_INVENTORY_SORT_TYPE;
    ui->has_selection = 0;
    ui->has_inspected = 0;
    fe8_inventory_ui_rebuild(ui, snapshot);
    snprintf(ui->status, sizeof(ui->status),
        "Choose a target, then pick or place any item");
}

const char *fe8_inventory_ui_scope_name(Fe8InventoryPoolScope scope) {
    return scope == FE8_INVENTORY_POOL_SUPPLY ? "Supply" : "All";
}

const char *fe8_inventory_ui_sort_name(Fe8InventorySort sort) {
    switch (sort) {
    case FE8_INVENTORY_SORT_NAME:
        return "Name";
    case FE8_INVENTORY_SORT_USES:
        return "Uses";
    case FE8_INVENTORY_SORT_OWNER:
        return "Owner";
    case FE8_INVENTORY_SORT_TYPE:
    default:
        return "Type";
    }
}

void fe8_inventory_ui_toggle_scope(Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *snapshot) {
    if (!ui)
        return;
    ui->pool_scope = ui->pool_scope == FE8_INVENTORY_POOL_ALL ?
        FE8_INVENTORY_POOL_SUPPLY : FE8_INVENTORY_POOL_ALL;
    ui->pool_scroll = 0;
    fe8_inventory_ui_rebuild(ui, snapshot);
    snprintf(ui->status, sizeof(ui->status), "Showing %s items",
        ui->pool_scope == FE8_INVENTORY_POOL_ALL ? "all carried and supply" :
            "supply");
}

void fe8_inventory_ui_cycle_sort(Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *snapshot) {
    if (!ui)
        return;
    ui->pool_sort = (Fe8InventorySort)((ui->pool_sort + 1) %
        FE8_INVENTORY_SORT_COUNT);
    ui->pool_scroll = 0;
    fe8_inventory_ui_rebuild(ui, snapshot);
    snprintf(ui->status, sizeof(ui->status), "Sorted by %s",
        fe8_inventory_ui_sort_name(ui->pool_sort));
}

int fe8_inventory_ui_pool_count(const Fe8InventoryUi *ui) {
    return ui ? ui->pool_count : 0;
}

int fe8_inventory_ui_pool_entry(const Fe8InventoryUi *ui, int index,
    Fe8InventoryListEntry *entry) {
    if (!ui || !entry || index < 0 || index >= ui->pool_count)
        return 0;
    *entry = ui->pool[index];
    return 1;
}

void fe8_inventory_ui_scroll(Fe8InventoryUi *ui, int rows,
    const Fe8InventorySnapshot *snapshot, int width, int height, int pointer_x) {
    if (ui && ui->desktop) {
        fe8_inventory_desktop_scroll(ui, snapshot, width, height, pointer_x, rows);
        return;
    }
    Fe8InventoryUiLayout layout;
    int *value;
    int maximum;
    int scale;
    (void)snapshot;
    if (!ui)
        return;
    scale = ui->render_scale ? ui->render_scale : 1;
    width /= scale;
    height /= scale;
    pointer_x /= scale;
    layout_for(width, height, &layout);
    if (pointer_x >= width - MARGIN || pointer_x < MARGIN)
        return;
    if (pointer_x >= layout.pool_x) {
        ui->pool_scroll = pool_offset(ui, &layout);
        value = &ui->pool_scroll;
        maximum = ui->pool_count - pool_rows(&layout);
    } else if (snapshot && pointer_x < MARGIN + layout.roster_width) {
        ui->roster_scroll = roster_offset(ui, snapshot, &layout);
        value = &ui->roster_scroll;
        maximum = snapshot ? snapshot->unit_count - roster_rows(&layout) : 0;
    }
    else {
        return;
    }
    if (maximum < 0)
        maximum = 0;
    *value += rows;
    if (*value < 0)
        *value = 0;
    if (*value > maximum)
        *value = maximum;
}

Fe8InventoryHitKind fe8_inventory_ui_hit_test(const Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *snapshot, int width, int height,
    int x, int y, int *index) {
    if (ui && ui->desktop)
        return fe8_inventory_desktop_hit(ui, snapshot, width, height, x, y, index);
    Fe8InventoryUiLayout layout;
    int scale;
    if (!ui || !snapshot || !index)
        return FE8_INVENTORY_HIT_NONE;
    scale = ui->render_scale ? ui->render_scale : 1;
    width /= scale;
    height /= scale;
    x /= scale;
    y /= scale;
    layout_for(width, height, &layout);
    *index = -1;
    if (!ui->active || x < MARGIN || x >= width - MARGIN ||
            y < layout.top || y >= layout.bottom)
        return FE8_INVENTORY_HIT_NONE;
    if (x < MARGIN + layout.roster_width) {
        int row_y = y - layout.top - SECTION_HEADER_H;
        if (x >= MARGIN + layout.roster_width - SCROLLBAR_W || row_y < 0 ||
                row_y >= roster_rows(&layout) * ROSTER_ROW_H)
            return FE8_INVENTORY_HIT_NONE;
        *index = roster_offset(ui, snapshot, &layout) + row_y / ROSTER_ROW_H;
        if (*index < 0 || *index >= snapshot->unit_count)
            return FE8_INVENTORY_HIT_NONE;
        return row_y % ROSTER_ROW_H >= 16 ?
            FE8_INVENTORY_HIT_ROSTER_CLASS : FE8_INVENTORY_HIT_ROSTER;
    }
    if (x < layout.pool_x) {
        int item_top = layout.top + UNIT_CARD_H;
        if (!target_unit(ui, snapshot))
            return FE8_INVENTORY_HIT_NONE;
        if (x >= layout.detail_x && x < layout.detail_x + layout.detail_width) {
            *index = ui->current_unit;
            if (y >= layout.name_y && y < layout.name_y + 14)
                return FE8_INVENTORY_HIT_UNIT_NAME;
            if (y >= layout.class_y && y < layout.class_y + 14)
                return FE8_INVENTORY_HIT_UNIT_CLASS;
        }
        if (y < item_top || y >= item_top +
                FE8_INVENTORY_ITEM_SLOTS * layout.item_row_height)
            return FE8_INVENTORY_HIT_NONE;
        *index = (y - item_top) / layout.item_row_height;
        return FE8_INVENTORY_HIT_UNIT_ITEM;
    }
    if (y >= layout.top + 20 && y < layout.top + 38) {
        if (x >= layout.scope_x && x < layout.scope_x + layout.scope_width)
            return FE8_INVENTORY_HIT_POOL_SCOPE;
        if (x >= layout.sort_x && x < layout.sort_x + layout.sort_width)
            return FE8_INVENTORY_HIT_POOL_SORT;
    }
    if (y < layout.top + POOL_HEADER_H ||
            y >= layout.top + POOL_HEADER_H + pool_rows(&layout) * POOL_ROW_H ||
            x >= width - MARGIN - SCROLLBAR_W)
        return FE8_INVENTORY_HIT_NONE;
    *index = pool_offset(ui, &layout) +
        (y - layout.top - POOL_HEADER_H) / POOL_ROW_H;
    return *index >= 0 && *index < ui->pool_count ?
        FE8_INVENTORY_HIT_POOL_ITEM : FE8_INVENTORY_HIT_NONE;
}

Fe8InventoryEndpoint fe8_inventory_ui_endpoint(const Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *snapshot, Fe8InventoryHitKind kind, int index) {
    Fe8InventoryEndpoint endpoint = {
        FE8_INVENTORY_ENDPOINT_SUPPLY,
        snapshot ? snapshot->supply_address : 0,
        0,
    };
    if (!ui || !snapshot)
        return endpoint;
    if (kind == FE8_INVENTORY_HIT_UNIT_ITEM && ui->current_unit >= 0 &&
            ui->current_unit < snapshot->unit_count && index >= 0 &&
            index < FE8_INVENTORY_ITEM_SLOTS) {
        endpoint.kind = FE8_INVENTORY_ENDPOINT_UNIT;
        endpoint.unit_address = snapshot->units[ui->current_unit].address;
        endpoint.slot = (unsigned)index;
    } else if (kind == FE8_INVENTORY_HIT_POOL_ITEM &&
            index >= 0 && index < ui->pool_count) {
        endpoint = ui->pool[index].endpoint;
    }
    return endpoint;
}

void fe8_inventory_ui_inspect(Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *snapshot, Fe8InventoryHitKind kind, int index) {
    Fe8InventoryEndpoint endpoint;
    if (!ui)
        return;
    ui->has_inspected = 0;
    ui->hover_kind = FE8_INVENTORY_HIT_NONE;
    ui->hover_unit_address = 0;
    if (!snapshot || !ui->active)
        return;
    if (kind == FE8_INVENTORY_HIT_UNIT_NAME ||
            kind == FE8_INVENTORY_HIT_UNIT_CLASS ||
            kind == FE8_INVENTORY_HIT_ROSTER ||
            kind == FE8_INVENTORY_HIT_ROSTER_CLASS) {
        if (index >= 0 && index < snapshot->unit_count) {
            ui->hover_kind = kind;
            ui->hover_unit_address = snapshot->units[index].address;
        }
        return;
    }
    if (kind == FE8_INVENTORY_HIT_POOL_SCOPE ||
            kind == FE8_INVENTORY_HIT_POOL_SORT ||
            kind == FE8_INVENTORY_HIT_DENSITY ||
            kind == FE8_INVENTORY_HIT_SORT_COLUMN) {
        ui->hover_kind = kind;
        return;
    }
    if ((kind == FE8_INVENTORY_HIT_UNIT_ITEM &&
            target_unit(ui, snapshot) && index >= 0 &&
            index < FE8_INVENTORY_ITEM_SLOTS) ||
            (kind == FE8_INVENTORY_HIT_POOL_ITEM && index >= 0 &&
             index < ui->pool_count)) {
        endpoint = fe8_inventory_ui_endpoint(ui, snapshot, kind, index);
        ui->inspected = endpoint;
        ui->hover_kind = kind;
        ui->has_inspected = 1;
    }
}

const char *fe8_inventory_ui_unit_help(const Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *snapshot, const char **title) {
    unsigned index;
    int is_class;
    if (title)
        *title = NULL;
    if (!ui || !snapshot || !ui->active)
        return NULL;
    is_class = ui->hover_kind == FE8_INVENTORY_HIT_UNIT_CLASS ||
        ui->hover_kind == FE8_INVENTORY_HIT_ROSTER_CLASS;
    if (!is_class && ui->hover_kind != FE8_INVENTORY_HIT_UNIT_NAME &&
            ui->hover_kind != FE8_INVENTORY_HIT_ROSTER)
        return NULL;
    for (index = 0; index < snapshot->unit_count; ++index) {
        const Fe8InventoryUnit *unit = &snapshot->units[index];
        const char *description;
        if (unit->address != ui->hover_unit_address)
            continue;
        if (title)
            *title = is_class ? unit->class_name : unit->name;
        description = is_class ? unit->class_description : unit->description;
        if (description[0])
            return description;
        return is_class ? "No class description is available in this ROM." :
            "No character description is available in this ROM.";
    }
    return NULL;
}

static void portrait(uint32_t *pixels, int stride, int width, int height,
    int x, int y, const Fe8InventoryUnit *unit) {
    int portrait_y;
    int portrait_x;
    if (!unit->portrait_valid) {
        rect(pixels, stride, width, height, x, y,
            FE8_PORTRAIT_WIDTH, FE8_PORTRAIT_HEIGHT, UINT32_C(0xFF253E57));
        return;
    }
    for (portrait_y = 0; portrait_y < FE8_PORTRAIT_HEIGHT; ++portrait_y) {
        for (portrait_x = 0; portrait_x < FE8_PORTRAIT_WIDTH; ++portrait_x) {
            uint8_t palette_index =
                unit->portrait[portrait_y * FE8_PORTRAIT_WIDTH + portrait_x];
            if (palette_index < FE8_PORTRAIT_PALETTE_SIZE &&
                    unit->portrait_palette[palette_index]) {
                rect(pixels, stride, width, height,
                    x + portrait_x, y + portrait_y, 1, 1,
                    canvas_color(unit->portrait_palette[palette_index]));
            }
        }
    }
}

static char rank_letter(uint8_t rank) {
    if (rank >= 251)
        return 'S';
    if (rank >= 181)
        return 'A';
    if (rank >= 121)
        return 'B';
    if (rank >= 71)
        return 'C';
    if (rank >= 31)
        return 'D';
    if (rank)
        return 'E';
    return '-';
}

static const char *weapon_type_name(uint8_t type) {
    static const char *const names[FE8_INVENTORY_WEAPON_TYPES] = {
        "Sword", "Lance", "Axe", "Bow", "Staff", "Anima", "Light", "Dark",
    };
    return type < FE8_INVENTORY_WEAPON_TYPES ? names[type] : "Gear";
}

static const char *weapon_type_short(const Fe8ItemInfo *info) {
    static const char *const names[FE8_INVENTORY_WEAPON_TYPES] = {
        "SWD", "LNC", "AXE", "BOW", "STF", "ANI", "LGT", "DRK",
    };
    if (!info || (info->attributes & (FE8_ITEM_ATTRIBUTE_WEAPON |
            FE8_ITEM_ATTRIBUTE_STAFF)) == 0)
        return "ITEM";
    return info->weapon_type < FE8_INVENTORY_WEAPON_TYPES ?
        names[info->weapon_type] : "GEAR";
}

static int item_is_weapon(const Fe8ItemInfo *info) {
    return info && (info->attributes & (FE8_ITEM_ATTRIBUTE_WEAPON |
        FE8_ITEM_ATTRIBUTE_STAFF)) != 0;
}

static void item_uses_text(char *output, size_t output_size,
    uint16_t encoded, const Fe8ItemInfo *info, int compact) {
    if (info && (info->attributes & FE8_ITEM_ATTRIBUTE_UNBREAKABLE) != 0)
        snprintf(output, output_size, "%s", compact ? "INF" : "INF uses");
    else
        snprintf(output, output_size, compact ? "%u" : "%u uses", encoded >> 8);
}

static const char *use_state_label(Fe8InventoryUseState state) {
    switch (state) {
    case FE8_INVENTORY_USE_READY:
        return "READY";
    case FE8_INVENTORY_USE_RANK:
        return "RANK";
    case FE8_INVENTORY_USE_LOCKED:
        return "LOCK";
    case FE8_INVENTORY_USE_STATUS:
        return "STATUS";
    case FE8_INVENTORY_USE_ITEM:
    default:
        return "ITEM";
    }
}

static void use_state_colors(Fe8InventoryUseState state,
    uint32_t *background, uint32_t *foreground) {
    switch (state) {
    case FE8_INVENTORY_USE_READY:
        *background = UINT32_C(0xFF1E5138);
        *foreground = UINT32_C(0xFFC9F7D8);
        break;
    case FE8_INVENTORY_USE_RANK:
        *background = UINT32_C(0xFF5B461D);
        *foreground = UINT32_C(0xFFFFE2A3);
        break;
    case FE8_INVENTORY_USE_LOCKED:
        *background = UINT32_C(0xFF592D32);
        *foreground = UINT32_C(0xFFFFC4C8);
        break;
    case FE8_INVENTORY_USE_STATUS:
        *background = UINT32_C(0xFF49365C);
        *foreground = UINT32_C(0xFFE7C9FF);
        break;
    case FE8_INVENTORY_USE_ITEM:
    default:
        *background = UINT32_C(0xFF34404D);
        *foreground = UINT32_C(0xFFD2DCE6);
        break;
    }
}

static void use_badge(uint32_t *pixels, int stride, int width, int height,
    int x, int y, Fe8InventoryUseState state) {
    uint32_t background;
    uint32_t foreground;
    use_state_colors(state, &background, &foreground);
    rect(pixels, stride, width, height, x, y, USE_BADGE_W, 14, background);
    text_box(pixels, stride, width, height, x + 3, y + 3,
        USE_BADGE_W - 6, use_state_label(state), foreground, 1, 7);
}

static void type_badge(uint32_t *pixels, int stride, int width, int height,
    int x, int y, const Fe8ItemInfo *info) {
    static const uint32_t backgrounds[] = {
        UINT32_C(0xFF5A3C32), UINT32_C(0xFF334D67), UINT32_C(0xFF5D4530),
        UINT32_C(0xFF365741), UINT32_C(0xFF67542B), UINT32_C(0xFF49395F),
        UINT32_C(0xFF6A6037), UINT32_C(0xFF403A55), UINT32_C(0xFF34404D),
    };
    int key = FE8_INVENTORY_WEAPON_TYPES;
    if (info && (info->attributes & (FE8_ITEM_ATTRIBUTE_WEAPON |
            FE8_ITEM_ATTRIBUTE_STAFF)) != 0 &&
            info->weapon_type < FE8_INVENTORY_WEAPON_TYPES)
        key = info->weapon_type;
    rect(pixels, stride, width, height, x, y, TYPE_BADGE_W, 13,
        backgrounds[key]);
    text_box(pixels, stride, width, height, x + 3, y + 2,
        TYPE_BADGE_W - 6, weapon_type_short(info), UINT32_C(0xFFF4F7FA),
        1, 4);
}

static void draw_selected_accent(uint32_t *pixels, int stride,
    int width, int height, int x, int y, int row_height, int selected) {
    if (selected)
        rect(pixels, stride, width, height, x + 2, y + 1, 3,
            row_height - 2, UINT32_C(0xFF65A9FF));
}

static void unit_item_row(uint32_t *pixels, int stride, int width, int height,
    int x, int y, int row_width, const Fe8InventoryUnit *unit,
    const Fe8ItemInfo *info, uint16_t encoded, int selected, int hovered, int row_height) {
    char details[64];
    char uses[24];
    uint32_t foreground = info->movable ?
        UINT32_C(0xFFF3F7FA) : UINT32_C(0xFF9EABC0);
    uint32_t background = selected ? UINT32_C(0xFF225EA8) :
        encoded ? (info->movable ? UINT32_C(0xFF202B38) :
            UINT32_C(0xFF292D35)) : UINT32_C(0xFF171D25);
    rect(pixels, stride, width, height, x + 2, y + 1,
        row_width - 4, row_height - 2, background);
    draw_selected_accent(pixels, stride, width, height,
        x, y, row_height, selected);
    if (hovered)
        rect(pixels, stride, width, height, x + 5, y + 1, row_width - 10, 1,
            UINT32_C(0xFF6C8BA6));
    if (!encoded) {
        text_box(pixels, stride, width, height, x + 8, y + 9,
            row_width - 16, "EMPTY SLOT", UINT32_C(0xFF73869A), 1, 16);
        return;
    }
    text_box(pixels, stride, width, height, x + 8, y + 2,
        row_width - USE_BADGE_W - 19, info->name, foreground, 1, 24);
    item_uses_text(uses, sizeof(uses), encoded, info, 0);
    if (item_is_weapon(info)) {
        snprintf(details, sizeof(details), "%s %c / %s%s",
            weapon_type_name(info->weapon_type), rank_letter(info->weapon_rank),
            uses, info->movable ? "" : " / fixed");
    } else {
        snprintf(details, sizeof(details), "%s%s",
            uses, info->movable ? "" : " / fixed");
    }
    text_box(pixels, stride, width, height, x + 8, y + 13,
        row_width - USE_BADGE_W - 19, details,
        UINT32_C(0xFFB8CCE0), 1, 28);
    use_badge(pixels, stride, width, height,
        x + row_width - USE_BADGE_W - 5, y + (row_height - 14) / 2,
        fe8_inventory_item_use_state(unit, info));
}

static void pool_item_row(uint32_t *pixels, int stride, int width, int height,
    int x, int y, int row_width, const Fe8InventorySnapshot *snapshot,
    const Fe8InventoryUnit *target, const Fe8InventoryListEntry *entry,
    int selected, int hovered) {
    char owner[72];
    char uses[24];
    int name_x;
    int name_width;
    int owner_width;
    uint32_t foreground;
    uint32_t background;
    if (!entry->item) {
        background = selected ? UINT32_C(0xFF225EA8) : UINT32_C(0xFF17212B);
        rect(pixels, stride, width, height, x + 2, y + 1,
            row_width - 4, POOL_ROW_H - 2, background);
        draw_selected_accent(pixels, stride, width, height,
            x, y, POOL_ROW_H, selected);
        text_box(pixels, stride, width, height, x + 8, y + 4,
            row_width - 16, "Empty supply slot", UINT32_C(0xFFB7C7D8), 1, 28);
        text_box(pixels, stride, width, height, x + 8, y + 17,
            row_width - 16, "Store an item here", UINT32_C(0xFF73869A), 1, 24);
        return;
    }
    foreground = entry->info && entry->info->movable ?
        UINT32_C(0xFFF3F7FA) : UINT32_C(0xFF9EABC0);
    background = selected ? UINT32_C(0xFF225EA8) :
        entry->info && entry->info->movable ? UINT32_C(0xFF202B38) :
            UINT32_C(0xFF292D35);
    rect(pixels, stride, width, height, x + 2, y + 1,
        row_width - 4, POOL_ROW_H - 2, background);
    draw_selected_accent(pixels, stride, width, height,
        x, y, POOL_ROW_H, selected);
    if (hovered)
        rect(pixels, stride, width, height, x + 5, y + 1, row_width - 10, 1,
            UINT32_C(0xFF6C8BA6));
    type_badge(pixels, stride, width, height, x + 7, y + 17, entry->info);
    name_x = x + 8;
    name_width = row_width - USES_BOX_W - 22;
    text_box(pixels, stride, width, height, name_x, y + 2,
        name_width, entry->info ? entry->info->name : "Unknown item",
        foreground, 1, 28);
    item_uses_text(uses, sizeof(uses), entry->item, entry->info, 1);
    text_box(pixels, stride, width, height,
        x + row_width - USES_BOX_W - 7, y + 3, USES_BOX_W,
        uses, UINT32_C(0xFFFFD46A), 1, 4);
    if (entry->unit_index >= 0 && entry->unit_index < snapshot->unit_count) {
        snprintf(owner, sizeof(owner), "%s%s",
            snapshot->units[entry->unit_index].name,
            entry->info && entry->info->movable ? "" : " / fixed");
    } else {
        snprintf(owner, sizeof(owner), "Supply%s",
            entry->info && entry->info->movable ? "" : " / fixed");
    }
    owner_width = row_width - TYPE_BADGE_W - (target ? USE_BADGE_W : 0) - 24;
    text_box(pixels, stride, width, height, x + TYPE_BADGE_W + 11, y + 19,
        owner_width, owner, UINT32_C(0xFF9CB0C4), 1, 34);
    if (target && entry->info) {
        use_badge(pixels, stride, width, height,
            x + row_width - USE_BADGE_W - 5, y + 16,
            fe8_inventory_item_use_state(target, entry->info));
    }
}

static void compatibility_detail(char *output, size_t output_size,
    const Fe8InventoryUnit *unit, const Fe8ItemInfo *info) {
    Fe8InventoryUseState state;
    if (!info || !info->id) {
        output[0] = '\0';
        return;
    }
    if (!unit) {
        snprintf(output, output_size, "Choose a target unit");
        return;
    }
    state = fe8_inventory_item_use_state(unit, info);
    switch (state) {
    case FE8_INVENTORY_USE_READY:
        snprintf(output, output_size, "Ready for %s%s",
            unit->name, info->movable ? "" : " / fixed");
        break;
    case FE8_INVENTORY_USE_RANK:
        snprintf(output, output_size, "Needs %s %c / %s has %c%s",
            weapon_type_name(info->weapon_type), rank_letter(info->weapon_rank),
            unit->name,
            info->weapon_type < FE8_INVENTORY_WEAPON_TYPES ?
                rank_letter(unit->ranks[info->weapon_type]) : '-',
            info->movable ? "" : " / fixed");
        break;
    case FE8_INVENTORY_USE_LOCKED:
        snprintf(output, output_size, "Locked for %s%s",
            unit->name, info->movable ? "" : " / fixed");
        break;
    case FE8_INVENTORY_USE_STATUS:
        snprintf(output, output_size, "Blocked by %s's status%s",
            unit->name, info->movable ? "" : " / fixed");
        break;
    case FE8_INVENTORY_USE_ITEM:
    default:
        snprintf(output, output_size, "Item / no weapon rank required%s",
            info->movable ? "" : " / fixed");
        break;
    }
}

static void item_summary(char *output, size_t output_size,
    uint16_t encoded, const Fe8ItemInfo *info) {
    char uses[24];
    item_uses_text(uses, sizeof(uses), encoded, info, 0);
    if (item_is_weapon(info)) {
        snprintf(output, output_size,
            "%s %c / Mt %u  Hit %u  Crit %u  Wt %u  Rng %u-%u  %s",
            weapon_type_name(info->weapon_type), rank_letter(info->weapon_rank),
            info->might, info->hit, info->crit, info->weight,
            info->min_range, info->max_range, uses);
    } else {
        snprintf(output, output_size, "%s", uses);
    }
}

static void draw_control(uint32_t *pixels, int stride, int width, int height,
    int x, int y, int control_width, const char *label, int hovered) {
    rect(pixels, stride, width, height, x, y, control_width, 18,
        hovered ? UINT32_C(0xFF354C64) : UINT32_C(0xFF273545));
    text_box(pixels, stride, width, height, x + 4, y + 3,
        control_width - 8, label, UINT32_C(0xFFC9D9EA), 1, 20);
}

static void scrollbar(uint32_t *pixels, int stride, int width, int height,
    int x, int y, int rows, int row_height, int count, int offset) {
    int track_height = rows * row_height;
    int thumb_height;
    int thumb_y;
    if (count <= rows || rows <= 0)
        return;
    thumb_height = clamp_int(track_height * rows / count, 12, track_height);
    thumb_y = y + (track_height - thumb_height) * offset / (count - rows);
    rect(pixels, stride, width, height, x, y, 2, track_height,
        UINT32_C(0xFF25313F));
    rect(pixels, stride, width, height, x, thumb_y, 2, thumb_height,
        UINT32_C(0xFF6C8BA6));
}

static void help_label(uint32_t *pixels, int stride, int width, int height,
    int x, int y, int label_width, const char *value, uint32_t color,
    int hovered) {
    if (hovered)
        rect(pixels, stride, width, height, x - 2, y - 1,
            label_width + 4, 15, UINT32_C(0xFF2D4055));
    text_box(pixels, stride, width, height, x, y, label_width,
        value, color, 1, 0);
    /* A subtle dotted underline makes name/class help discoverable without
       a question-mark glyph stealing space from long ROM-hack names. */
    for (int dot = 0; dot < label_width; dot += 4)
        rect(pixels, stride, width, height, x + dot, y + 12, 1, 1,
            hovered ? UINT32_C(0xFF9BC9F7) : UINT32_C(0xFF41546A));
}

void fe8_inventory_ui_draw(const Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *snapshot, uint32_t *pixels,
    int stride, int width, int height) {
    if (ui && snapshot && ui->desktop) {
        fe8_inventory_desktop_draw(ui, snapshot, pixels, stride, width, height);
        return;
    }
    Fe8InventoryUiLayout layout;
    Fe8HostTextCanvas canvas;
    const Fe8InventoryUnit *unit;
    int actual_width = width;
    int actual_height = height;
    int logical_width;
    int logical_height;
    int row;
    char buffer[160];
    if (!ui || !snapshot || !pixels || width <= 0 || height <= 0 || stride < width)
        return;
    render_scale = ui->render_scale > 0 ? ui->render_scale : 1;
    logical_width = width / render_scale;
    logical_height = height / render_scale;
    layout_for(logical_width, logical_height, &layout);
    unit = target_unit(ui, snapshot);

    rect(pixels, stride, logical_width, logical_height,
        0, 0, logical_width, logical_height, UINT32_C(0xFF0D141D));
    rect(pixels, stride, logical_width, logical_height,
        MARGIN, MARGIN, logical_width - MARGIN * 2,
        logical_height - MARGIN * 2, UINT32_C(0xFF161F2A));
    if (!fe8_host_text_begin(&canvas, pixels, stride, actual_width, actual_height)) {
        render_scale = 1;
        return;
    }
    text_canvas = &canvas;
    text_box(pixels, stride, logical_width, logical_height,
        MARGIN + 10, MARGIN + 4, logical_width - 164,
        snapshot->prebattle ? "Preparations" : "Inventory",
        UINT32_C(0xFFF4F7FA), 2, 0);
    snprintf(buffer, sizeof(buffer), "Supply  %u / %u",
        snapshot->supply_count, snapshot->supply_capacity);
    text_box(pixels, stride, logical_width, logical_height,
        logical_width - MARGIN - 119, MARGIN + 9, 112,
        buffer, UINT32_C(0xFF9DB9D4), 1, 0);
    text_box(pixels, stride, logical_width, logical_height,
        MARGIN + 10, MARGIN + 25, logical_width - MARGIN * 2 - 20,
        ui->status, ui->has_selection ? UINT32_C(0xFFFFD890) :
            UINT32_C(0xFF9EAFBF), 1, 0);

    rect(pixels, stride, logical_width, logical_height,
        MARGIN + layout.roster_width - 1, layout.top, 1,
        layout.bottom - layout.top, UINT32_C(0xFF303E4E));
    rect(pixels, stride, logical_width, logical_height,
        layout.pool_x - 1, layout.top, 1,
        layout.bottom - layout.top, UINT32_C(0xFF303E4E));

    snprintf(buffer, sizeof(buffer), "Roster  %u", snapshot->unit_count);
    text_box(pixels, stride, logical_width, logical_height,
        MARGIN + 7, layout.top + 5, layout.roster_width - 14,
        buffer, UINT32_C(0xFF8AB4E8), 1, 0);
    for (row = 0; row < roster_rows(&layout); ++row) {
        int index = roster_offset(ui, snapshot, &layout) + row;
        int y = layout.top + SECTION_HEADER_H + row * ROSTER_ROW_H;
        int hovered;
        if (index >= snapshot->unit_count)
            break;
        hovered = ui->hover_unit_address == snapshot->units[index].address &&
            (ui->hover_kind == FE8_INVENTORY_HIT_ROSTER ||
             ui->hover_kind == FE8_INVENTORY_HIT_ROSTER_CLASS);
        if (index == ui->current_unit || hovered) {
            rect(pixels, stride, logical_width, logical_height,
                MARGIN + 2, y + 1, layout.roster_width - 8, ROSTER_ROW_H - 2,
                index == ui->current_unit ? UINT32_C(0xFF283D53) :
                    UINT32_C(0xFF223142));
            if (index == ui->current_unit)
                rect(pixels, stride, logical_width, logical_height,
                    MARGIN + 2, y + 1, 2, ROSTER_ROW_H - 2,
                    UINT32_C(0xFF6CB6F5));
        }
        text_box(pixels, stride, logical_width, logical_height,
            MARGIN + 8, y + 4, layout.roster_width - 20,
            snapshot->units[index].name, UINT32_C(0xFFF4F7FA), 1, 0);
        text_box(pixels, stride, logical_width, logical_height,
            MARGIN + 8, y + 18, layout.roster_width - 20,
            snapshot->units[index].class_name, UINT32_C(0xFF99B0C7), 1, 0);
    }
    scrollbar(pixels, stride, logical_width, logical_height,
        MARGIN + layout.roster_width - 4, layout.top + SECTION_HEADER_H,
        roster_rows(&layout), ROSTER_ROW_H, snapshot->unit_count,
        roster_offset(ui, snapshot, &layout));

    if (unit) {
        int unit_x = MARGIN + layout.roster_width;
        int hovering_unit = ui->hover_unit_address == unit->address;
        portrait(pixels, stride, logical_width, logical_height,
            unit_x + 4, layout.top, unit);
        help_label(pixels, stride, logical_width, logical_height,
            layout.detail_x, layout.name_y, layout.detail_width,
            unit->name, UINT32_C(0xFFFFE49A), hovering_unit &&
                ui->hover_kind == FE8_INVENTORY_HIT_UNIT_NAME);
        help_label(pixels, stride, logical_width, logical_height,
            layout.detail_x, layout.class_y, layout.detail_width,
            unit->class_name, UINT32_C(0xFFACCEEF), hovering_unit &&
                ui->hover_kind == FE8_INVENTORY_HIT_UNIT_CLASS);
        snprintf(buffer, sizeof(buffer), "Lv %u  HP %u/%u",
            unit->level, unit->hp, unit->max_hp);
        text_box(pixels, stride, logical_width, logical_height,
            layout.detail_x, layout.top + 34, layout.detail_width,
            buffer, UINT32_C(0xFFF3F7FA), 1, 0);
        snprintf(buffer, sizeof(buffer), "Pow %u  Spd %u", unit->power, unit->speed);
        text_box(pixels, stride, logical_width, logical_height,
            layout.detail_x, layout.top + 47, layout.detail_width,
            buffer, UINT32_C(0xFFB8CADC), 1, 0);
        snprintf(buffer, sizeof(buffer), "Def %u  Res %u", unit->defense, unit->resistance);
        text_box(pixels, stride, logical_width, logical_height,
            layout.detail_x, layout.top + 60, layout.detail_width,
            buffer, UINT32_C(0xFFB8CADC), 1, 0);
        for (row = 0; row < FE8_INVENTORY_ITEM_SLOTS; ++row) {
            Fe8InventoryEndpoint endpoint = {
                FE8_INVENTORY_ENDPOINT_UNIT, unit->address, (unsigned)row,
            };
            int y = layout.top + UNIT_CARD_H + row * layout.item_row_height;
            int selected = ui->has_selection && endpoint_equal(ui->selected, endpoint);
            int hovered = ui->has_inspected && endpoint_equal(ui->inspected, endpoint);
            if (y + layout.item_row_height > layout.bottom)
                break;
            unit_item_row(pixels, stride, logical_width, logical_height,
                unit_x, y, layout.unit_width, unit, &unit->item_info[row],
                unit->items[row], selected, hovered, layout.item_row_height);
        }
    }

    text_box(pixels, stride, logical_width, logical_height,
        layout.pool_x + 7, layout.top + 5, layout.pool_width - 53,
        ui->pool_scope == FE8_INVENTORY_POOL_ALL ? "All items" : "Supply items",
        UINT32_C(0xFF8AB4E8), 1, 0);
    snprintf(buffer, sizeof(buffer), "%d", ui->pool_count -
        (snapshot->first_empty_supply < snapshot->supply_capacity ? 1 : 0));
    text_box(pixels, stride, logical_width, logical_height,
        layout.pool_x + layout.pool_width - 37, layout.top + 5, 30,
        buffer, UINT32_C(0xFFB8CCE0), 1, 0);
    snprintf(buffer, sizeof(buffer), "A: %s",
        fe8_inventory_ui_scope_name(ui->pool_scope));
    draw_control(pixels, stride, logical_width, logical_height,
        layout.scope_x, layout.top + 20, layout.scope_width, buffer,
        ui->hover_kind == FE8_INVENTORY_HIT_POOL_SCOPE);
    snprintf(buffer, sizeof(buffer), "S: %s",
        fe8_inventory_ui_sort_name(ui->pool_sort));
    draw_control(pixels, stride, logical_width, logical_height,
        layout.sort_x, layout.top + 20, layout.sort_width, buffer,
        ui->hover_kind == FE8_INVENTORY_HIT_POOL_SORT);
    for (row = 0; row < pool_rows(&layout); ++row) {
        int index = pool_offset(ui, &layout) + row;
        int y = layout.top + POOL_HEADER_H + row * POOL_ROW_H;
        Fe8InventoryListEntry entry;
        int selected;
        int hovered;
        if (!fe8_inventory_ui_pool_entry(ui, index, &entry))
            break;
        selected = ui->has_selection && endpoint_equal(ui->selected, entry.endpoint);
        hovered = ui->has_inspected && endpoint_equal(ui->inspected, entry.endpoint);
        pool_item_row(pixels, stride, logical_width, logical_height,
            layout.pool_x, y, layout.pool_width - SCROLLBAR_W, snapshot, unit,
            &entry, selected, hovered);
    }
    scrollbar(pixels, stride, logical_width, logical_height,
        logical_width - MARGIN - 3, layout.top + POOL_HEADER_H,
        pool_rows(&layout), POOL_ROW_H, ui->pool_count, pool_offset(ui, &layout));

    rect(pixels, stride, logical_width, logical_height,
        MARGIN, layout.bottom, logical_width - MARGIN * 2,
        logical_height - MARGIN - layout.bottom, UINT32_C(0xFF1C2A39));
    rect(pixels, stride, logical_width, logical_height,
        MARGIN, layout.bottom, logical_width - MARGIN * 2, 1,
        UINT32_C(0xFF3A526B));
    {
        const char *help_title = NULL;
        const char *help = fe8_inventory_ui_unit_help(ui, snapshot, &help_title);
        const Fe8ItemInfo *info = NULL;
        uint16_t encoded = 0;
        char detail[160];
        char summary[160];
        int description_y = layout.bottom + 23;
        if (help) {
            int is_class = ui->hover_kind == FE8_INVENTORY_HIT_UNIT_CLASS ||
                ui->hover_kind == FE8_INVENTORY_HIT_ROSTER_CLASS;
            snprintf(buffer, sizeof(buffer), "%s  /  %s",
                is_class ? "Class" : "Character", help_title);
            text_box(pixels, stride, logical_width, logical_height,
                MARGIN + 10, layout.bottom + 7, logical_width - MARGIN * 2 - 20,
                buffer, UINT32_C(0xFFFFE49A), 1, 0);
        } else {
            if (ui->has_inspected) {
                info = endpoint_info(snapshot, ui->inspected);
                encoded = fe8_inventory_ui_endpoint_item(snapshot, ui->inspected);
            } else if (ui->has_selection) {
                info = endpoint_info(snapshot, ui->selected);
                encoded = fe8_inventory_ui_endpoint_item(snapshot, ui->selected);
            }
            if (info && info->id && encoded) {
                text_box(pixels, stride, logical_width, logical_height,
                    MARGIN + 10, layout.bottom + 5, 132,
                    info->name, UINT32_C(0xFFFFE49A), 1, 0);
                compatibility_detail(detail, sizeof(detail), unit, info);
                text_box(pixels, stride, logical_width, logical_height,
                    MARGIN + 148, layout.bottom + 5,
                    logical_width - MARGIN * 2 - 158,
                    detail, UINT32_C(0xFFB8D2E9), 1, 0);
                item_summary(summary, sizeof(summary), encoded, info);
                text_box(pixels, stride, logical_width, logical_height,
                    MARGIN + 10, layout.bottom + 18,
                    logical_width - MARGIN * 2 - 20,
                    summary, UINT32_C(0xFF9FB1C4), 1, 0);
                help = info->description[0] ? info->description :
                    "No item description is available in this ROM.";
                description_y = layout.bottom + 32;
            } else {
                text_box(pixels, stride, logical_width, logical_height,
                    MARGIN + 10, layout.bottom + 7,
                    logical_width - MARGIN * 2 - 20,
                    "Hover a name, class or item to inspect it.",
                    UINT32_C(0xFFD1E1EE), 1, 0);
                help = ui->has_selection ?
                    "Your item stays selected while you browse. Click a slot to move it." :
                    "Choose a unit, then click an item and its destination slot.";
            }
        }
        fe8_host_text_draw(&canvas, (MARGIN + 10) * render_scale,
            description_y * render_scale,
            (logical_width - MARGIN * 2 - 20) * render_scale,
            (logical_height - MARGIN - 14 - description_y) * render_scale,
            help, 9.5f * render_scale, canvas_color(UINT32_C(0xFFBECEDC)),
            FE8_HOST_TEXT_REGULAR, 1);
    }
    text_box(pixels, stride, logical_width, logical_height,
        MARGIN + 10, logical_height - MARGIN - 11,
        logical_width - MARGIN * 2 - 20,
        "A: All/Supply   S: Sort   U: Undo   Right-click: Cancel   I/Esc: Close",
        UINT32_C(0xFF91A8BE), 1, 0);
    fe8_host_text_end(&canvas);
    text_canvas = NULL;
    render_scale = 1;
}
