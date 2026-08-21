#include "prebattle_inventory_ui.h"
#include "host_text.h"

#include <stdio.h>
#include <string.h>

#define FE8_ITEM_ATTRIBUTE_UNBREAKABLE UINT32_C(1u << 3)

enum {
    MARGIN = 8,
    HEADER_H = 38,
    FOOTER_H = 54,
    ROSTER_W = 104,
    UNIT_W = 176,
    ROW_H = 22,
    ITEM_ROW_H = 28,
    POOL_ROW_H = 30,
    SECTION_HEADER_H = 18,
    UNIT_CARD_H = 72,
    BADGE_W = 44,
};

static Fe8HostTextCanvas *text_canvas;
static int render_scale = 1;

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
    char clipped[128];
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
        clipped, (scale == 2 ? 16.0f : 9.5f) * render_scale, color,
        scale == 2 ? FE8_HOST_TEXT_SEMIBOLD : FE8_HOST_TEXT_REGULAR, 0);
}

static int body_top(void) {
    return MARGIN + HEADER_H;
}

static int body_bottom(int height) {
    return height - MARGIN - FOOTER_H;
}

static int roster_rows(int height) {
    int rows = (body_bottom(height) - body_top() - SECTION_HEADER_H) / ROW_H;
    return rows > 0 ? rows : 1;
}

static int pool_x(void) {
    return MARGIN + ROSTER_W + UNIT_W;
}

static int pool_rows(int height) {
    int rows = (body_bottom(height) - body_top() - SECTION_HEADER_H) / POOL_ROW_H;
    return rows > 0 ? rows : 1;
}

static int endpoint_equal(Fe8InventoryEndpoint first, Fe8InventoryEndpoint second) {
    return first.kind == second.kind && first.unit_address == second.unit_address &&
        first.slot == second.slot;
}

static const Fe8InventoryUnit *target_unit(const Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *snapshot) {
    if (!ui || !snapshot || ui->current_unit < 0 ||
            ui->current_unit >= snapshot->unit_count)
        return NULL;
    return &snapshot->units[ui->current_unit];
}

void fe8_inventory_ui_init(Fe8InventoryUi *ui) {
    memset(ui, 0, sizeof(*ui));
    ui->render_scale = 1;
}

void fe8_inventory_ui_open(Fe8InventoryUi *ui) {
    ui->active = 1;
    ui->has_selection = 0;
    ui->has_inspected = 0;
    snprintf(ui->status, sizeof(ui->status),
        "Choose from All items, then click a target slot");
}

int fe8_inventory_ui_all_item_count(const Fe8InventorySnapshot *snapshot) {
    int count = 0;
    unsigned unit_index;
    if (!snapshot)
        return 0;
    for (unit_index = 0; unit_index < snapshot->unit_count; ++unit_index) {
        unsigned slot;
        for (slot = 0; slot < FE8_INVENTORY_ITEM_SLOTS; ++slot) {
            if (snapshot->units[unit_index].items[slot])
                ++count;
        }
    }
    return count + snapshot->supply_display_count;
}

int fe8_inventory_ui_all_item_entry(const Fe8InventorySnapshot *snapshot,
    int index, Fe8InventoryListEntry *entry) {
    int current = 0;
    unsigned unit_index;
    if (!snapshot || !entry || index < 0)
        return 0;
    memset(entry, 0, sizeof(*entry));
    entry->unit_index = -1;
    for (unit_index = 0; unit_index < snapshot->unit_count; ++unit_index) {
        unsigned slot;
        for (slot = 0; slot < FE8_INVENTORY_ITEM_SLOTS; ++slot) {
            if (!snapshot->units[unit_index].items[slot])
                continue;
            if (current == index) {
                entry->endpoint.kind = FE8_INVENTORY_ENDPOINT_UNIT;
                entry->endpoint.unit_address = snapshot->units[unit_index].address;
                entry->endpoint.slot = slot;
                entry->info = &snapshot->units[unit_index].item_info[slot];
                entry->item = snapshot->units[unit_index].items[slot];
                entry->unit_index = (int)unit_index;
                return 1;
            }
            ++current;
        }
    }
    for (unit_index = 0; unit_index < snapshot->supply_display_count; ++unit_index) {
        unsigned slot = snapshot->supply_display_slots[unit_index];
        if (current == index) {
            entry->endpoint.kind = FE8_INVENTORY_ENDPOINT_SUPPLY;
            entry->endpoint.unit_address = snapshot->supply_address;
            entry->endpoint.slot = slot;
            entry->info = &snapshot->supply_info[slot];
            entry->item = snapshot->supply[slot];
            return 1;
        }
        ++current;
    }
    return 0;
}

void fe8_inventory_ui_scroll(Fe8InventoryUi *ui, int rows,
    const Fe8InventorySnapshot *snapshot, int width, int height, int pointer_x) {
    int *value;
    int maximum;
    int scale = ui->render_scale ? ui->render_scale : 1;
    width /= scale;
    height /= scale;
    pointer_x /= scale;
    (void)width;
    if (pointer_x >= pool_x()) {
        value = &ui->supply_scroll;
        maximum = fe8_inventory_ui_all_item_count(snapshot) - pool_rows(height);
    } else {
        value = &ui->roster_scroll;
        maximum = snapshot->unit_count - roster_rows(height);
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
    int scale = ui->render_scale ? ui->render_scale : 1;
    int top;
    int bottom;
    int px;
    if (!index)
        return FE8_INVENTORY_HIT_NONE;
    width /= scale;
    height /= scale;
    x /= scale;
    y /= scale;
    top = body_top();
    bottom = body_bottom(height);
    px = pool_x();
    if (!ui->active || x < MARGIN || x >= width - MARGIN ||
            y < top || y >= bottom)
        return FE8_INVENTORY_HIT_NONE;
    if (x < MARGIN + ROSTER_W) {
        if (y < top + SECTION_HEADER_H)
            return FE8_INVENTORY_HIT_NONE;
        *index = ui->roster_scroll + (y - top - SECTION_HEADER_H) / ROW_H;
        if (*index < 0 || *index >= snapshot->unit_count)
            return FE8_INVENTORY_HIT_NONE;
        return FE8_INVENTORY_HIT_ROSTER;
    }
    if (x < px) {
        int item_top = top + UNIT_CARD_H;
        if (y < item_top)
            return FE8_INVENTORY_HIT_NONE;
        *index = (y - item_top) / ITEM_ROW_H;
        return *index < FE8_INVENTORY_ITEM_SLOTS ?
            FE8_INVENTORY_HIT_UNIT_ITEM : FE8_INVENTORY_HIT_NONE;
    }
    if (y < top + SECTION_HEADER_H)
        return FE8_INVENTORY_HIT_NONE;
    *index = ui->supply_scroll + (y - top - SECTION_HEADER_H) / POOL_ROW_H;
    if (*index < 0 || *index >= fe8_inventory_ui_all_item_count(snapshot))
        return FE8_INVENTORY_HIT_NONE;
    return FE8_INVENTORY_HIT_ALL_ITEM;
}

Fe8InventoryEndpoint fe8_inventory_ui_endpoint(const Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *snapshot, Fe8InventoryHitKind kind, int index) {
    Fe8InventoryEndpoint endpoint = {
        FE8_INVENTORY_ENDPOINT_SUPPLY,
        snapshot ? snapshot->supply_address : 0,
        0,
    };
    if (kind == FE8_INVENTORY_HIT_UNIT_ITEM && ui->current_unit >= 0 &&
            ui->current_unit < snapshot->unit_count) {
        endpoint.kind = FE8_INVENTORY_ENDPOINT_UNIT;
        endpoint.unit_address = snapshot->units[ui->current_unit].address;
        endpoint.slot = (unsigned)index;
    } else if (kind == FE8_INVENTORY_HIT_ALL_ITEM) {
        Fe8InventoryListEntry entry;
        if (fe8_inventory_ui_all_item_entry(snapshot, index, &entry))
            endpoint = entry.endpoint;
    }
    return endpoint;
}

static const Fe8ItemInfo *endpoint_info(const Fe8InventorySnapshot *snapshot,
    Fe8InventoryEndpoint endpoint) {
    unsigned unit_index;
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

uint16_t fe8_inventory_ui_endpoint_item(const Fe8InventorySnapshot *snapshot,
    Fe8InventoryEndpoint endpoint) {
    unsigned unit_index;
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

void fe8_inventory_ui_inspect(Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *snapshot, Fe8InventoryHitKind kind, int index) {
    Fe8InventoryEndpoint endpoint;
    if (kind != FE8_INVENTORY_HIT_UNIT_ITEM &&
            kind != FE8_INVENTORY_HIT_ALL_ITEM)
        return;
    endpoint = fe8_inventory_ui_endpoint(ui, snapshot, kind, index);
    if (!fe8_inventory_ui_endpoint_item(snapshot, endpoint))
        return;
    ui->inspected = endpoint;
    ui->has_inspected = 1;
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
    return type < FE8_INVENTORY_WEAPON_TYPES ? names[type] : "Weapon";
}

static void item_uses_text(char *output, size_t output_size,
    uint16_t encoded, const Fe8ItemInfo *info, int compact) {
    if ((info->attributes & FE8_ITEM_ATTRIBUTE_UNBREAKABLE) != 0)
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
    rect(pixels, stride, width, height, x, y, BADGE_W, 14, background);
    text_box(pixels, stride, width, height, x + 3, y + 3, BADGE_W - 6,
        use_state_label(state), foreground, 1, 7);
}

static void unit_item_row(uint32_t *pixels, int stride, int width, int height,
    int x, int y, int row_width, const Fe8InventoryUnit *unit,
    const Fe8ItemInfo *info, uint16_t encoded, int selected) {
    char details[64];
    char uses[24];
    uint32_t foreground = info->movable ?
        UINT32_C(0xFFF3F7FA) : UINT32_C(0xFF9EABC0);
    uint32_t background = selected ? UINT32_C(0xFF225EA8) :
        encoded ? (info->movable ? UINT32_C(0xFF202B38) : UINT32_C(0xFF292D35)) :
        UINT32_C(0xFF171D25);
    rect(pixels, stride, width, height, x + 2, y + 1,
        row_width - 4, ITEM_ROW_H - 2, background);
    if (!encoded) {
        text_box(pixels, stride, width, height, x + 7, y + 9,
            row_width - 14, "EMPTY SLOT", UINT32_C(0xFF73869A), 1, 16);
        return;
    }
    text_box(pixels, stride, width, height, x + 7, y + 2,
        row_width - BADGE_W - 18, info->name, foreground, 1, 22);
    item_uses_text(uses, sizeof(uses), encoded, info, 0);
    if ((info->attributes & (UINT32_C(1u << 0) | UINT32_C(1u << 2))) != 0) {
        snprintf(details, sizeof(details), "%s %c / %s%s",
            weapon_type_name(info->weapon_type), rank_letter(info->weapon_rank),
            uses, info->movable ? "" : " / fixed");
    } else {
        snprintf(details, sizeof(details), "%s%s",
            uses, info->movable ? "" : " / fixed");
    }
    text_box(pixels, stride, width, height, x + 7, y + 15,
        row_width - BADGE_W - 18, details, UINT32_C(0xFFB8CCE0), 1, 24);
    use_badge(pixels, stride, width, height,
        x + row_width - BADGE_W - 5, y + 7,
        fe8_inventory_item_use_state(unit, info));
}

static void pool_item_row(uint32_t *pixels, int stride, int width, int height,
    int x, int y, int row_width, const Fe8InventorySnapshot *snapshot,
    const Fe8InventoryUnit *target, const Fe8InventoryListEntry *entry,
    int selected) {
    char owner[64];
    char uses[24];
    uint32_t background = selected ? UINT32_C(0xFF225EA8) :
        entry->item ? (entry->info->movable ? UINT32_C(0xFF202B38) :
            UINT32_C(0xFF292D35)) : UINT32_C(0xFF171D25);
    rect(pixels, stride, width, height, x + 2, y + 1,
        row_width - 4, POOL_ROW_H - 2, background);
    if (!entry->item) {
        text_box(pixels, stride, width, height, x + 7, y + 4,
            row_width - 14, "Empty supply slot", UINT32_C(0xFF9CB0C4), 1, 24);
        text_box(pixels, stride, width, height, x + 7, y + 17,
            row_width - 14, "Supply", UINT32_C(0xFF73869A), 1, 18);
        return;
    }
    text_box(pixels, stride, width, height, x + 7, y + 2,
        row_width - BADGE_W - 18, entry->info->name,
        entry->info->movable ? UINT32_C(0xFFF3F7FA) : UINT32_C(0xFF9EABC0),
        1, 22);
    if (entry->unit_index >= 0) {
        snprintf(owner, sizeof(owner), "%s / S%u%s",
            snapshot->units[entry->unit_index].name,
            entry->endpoint.slot + 1, entry->info->movable ? "" : " / fixed");
    } else {
        snprintf(owner, sizeof(owner), "Supply%s",
            entry->info->movable ? "" : " / fixed");
    }
    text_box(pixels, stride, width, height, x + 7, y + 17,
        row_width - 62, owner, UINT32_C(0xFF9CB0C4), 1, 26);
    item_uses_text(uses, sizeof(uses), entry->item, entry->info, 1);
    text_box(pixels, stride, width, height, x + row_width - 58, y + 17,
        18, uses, UINT32_C(0xFFFFD46A), 1, 3);
    if (target) {
        use_badge(pixels, stride, width, height,
            x + row_width - BADGE_W - 5, y + 2,
            fe8_inventory_item_use_state(target, entry->info));
    }
}

static void compatibility_detail(char *output, size_t output_size,
    const Fe8InventoryUnit *unit, const Fe8ItemInfo *info) {
    Fe8InventoryUseState state;
    if (!unit || !info || !info->id) {
        output[0] = '\0';
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

void fe8_inventory_ui_draw(const Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *snapshot, uint32_t *pixels,
    int stride, int width, int height) {
    int top;
    int bottom;
    int px;
    int row;
    int logical_width;
    int logical_height;
    int actual_width = width;
    int actual_height = height;
    int all_item_count;
    char buffer[128];
    const Fe8InventoryUnit *unit;
    Fe8HostTextCanvas canvas;
    render_scale = ui->render_scale ? ui->render_scale : 1;
    logical_width = width / render_scale;
    logical_height = height / render_scale;
    top = body_top();
    bottom = body_bottom(logical_height);
    px = pool_x();
    unit = target_unit(ui, snapshot);
    all_item_count = fe8_inventory_ui_all_item_count(snapshot);

    rect(pixels, stride, logical_width, logical_height,
        0, 0, logical_width, logical_height, UINT32_C(0xFF0D1117));
    rect(pixels, stride, logical_width, logical_height,
        MARGIN, MARGIN, logical_width - MARGIN * 2,
        logical_height - MARGIN * 2, UINT32_C(0xFF151B23));
    if (!fe8_host_text_begin(&canvas, pixels, stride, actual_width, actual_height)) {
        render_scale = 1;
        return;
    }
    text_canvas = &canvas;
    text_box(pixels, stride, logical_width, logical_height,
        MARGIN + 10, MARGIN + 5, 300,
        snapshot->prebattle ? "Preparation inventory" : "Inventory manager",
        UINT32_C(0xFFF4F7FA), 2, 28);
    text_box(pixels, stride, logical_width, logical_height,
        MARGIN + 10, MARGIN + 25, logical_width - MARGIN * 2 - 20,
        ui->status, UINT32_C(0xFF9CA8B7), 1, 90);

    rect(pixels, stride, logical_width, logical_height,
        MARGIN + ROSTER_W - 1, top, 1, bottom - top, UINT32_C(0xFF303A47));
    rect(pixels, stride, logical_width, logical_height,
        px - 1, top, 1, bottom - top, UINT32_C(0xFF303A47));
    text_box(pixels, stride, logical_width, logical_height,
        MARGIN + 7, top + 5, ROSTER_W - 14,
        "Target", UINT32_C(0xFF8AB4E8), 1, 20);
    for (row = 0; row < roster_rows(logical_height); ++row) {
        int index = ui->roster_scroll + row;
        int y = top + SECTION_HEADER_H + row * ROW_H;
        if (index >= snapshot->unit_count)
            break;
        if (index == ui->current_unit) {
            rect(pixels, stride, logical_width, logical_height,
                MARGIN + 2, y, ROSTER_W - 4, ROW_H, UINT32_C(0xFF253A52));
            rect(pixels, stride, logical_width, logical_height,
                MARGIN + 2, y, 3, ROW_H, UINT32_C(0xFF4B9BFF));
        }
        text_box(pixels, stride, logical_width, logical_height,
            MARGIN + 7, y + 8, ROSTER_W - 14,
            snapshot->units[index].name, UINT32_C(0xFFF4F7FA), 1, 15);
    }

    if (unit) {
        int unit_x = MARGIN + ROSTER_W;
        portrait(pixels, stride, logical_width, logical_height,
            unit_x + 4, top, unit);
        text_box(pixels, stride, logical_width, logical_height,
            unit_x + 88, top + 5, UNIT_W - 92,
            unit->name, UINT32_C(0xFFFFE49A), 1, 13);
        text_box(pixels, stride, logical_width, logical_height,
            unit_x + 88, top + 18, UNIT_W - 92,
            unit->class_name, UINT32_C(0xFFA8C8E8), 1, 13);
        snprintf(buffer, sizeof(buffer), "L%u  HP%u/%u",
            unit->level, unit->hp, unit->max_hp);
        text_box(pixels, stride, logical_width, logical_height,
            unit_x + 88, top + 33, UNIT_W - 92,
            buffer, UINT32_C(0xFFF3F7FA), 1, 16);
        snprintf(buffer, sizeof(buffer), "P%u  S%u", unit->power, unit->speed);
        text_box(pixels, stride, logical_width, logical_height,
            unit_x + 88, top + 47, UNIT_W - 92,
            buffer, UINT32_C(0xFFE7EEF5), 1, 16);
        snprintf(buffer, sizeof(buffer), "D%u  R%u", unit->defense, unit->resistance);
        text_box(pixels, stride, logical_width, logical_height,
            unit_x + 88, top + 60, UNIT_W - 92,
            buffer, UINT32_C(0xFFE7EEF5), 1, 16);
        for (row = 0; row < FE8_INVENTORY_ITEM_SLOTS; ++row) {
            Fe8InventoryEndpoint endpoint = {
                FE8_INVENTORY_ENDPOINT_UNIT,
                unit->address,
                (unsigned)row,
            };
            int selected = ui->has_selection && endpoint_equal(ui->selected, endpoint);
            unit_item_row(pixels, stride, logical_width, logical_height,
                unit_x, top + UNIT_CARD_H + row * ITEM_ROW_H, UNIT_W,
                unit, &unit->item_info[row], unit->items[row], selected);
        }
    }

    text_box(pixels, stride, logical_width, logical_height,
        px + 7, top + 5, 90, "All items", UINT32_C(0xFF8AB4E8), 1, 14);
    snprintf(buffer, sizeof(buffer), "%u items",
        (unsigned)(all_item_count -
            (snapshot->supply_display_count - snapshot->supply_count)));
    text_box(pixels, stride, logical_width, logical_height,
        logical_width - MARGIN - 50, top + 5, 45,
        buffer, UINT32_C(0xFFB8CCE0), 1, 10);
    for (row = 0; row < pool_rows(logical_height); ++row) {
        int index = ui->supply_scroll + row;
        int y = top + SECTION_HEADER_H + row * POOL_ROW_H;
        Fe8InventoryListEntry entry;
        int selected;
        if (!fe8_inventory_ui_all_item_entry(snapshot, index, &entry))
            break;
        selected = ui->has_selection && endpoint_equal(ui->selected, entry.endpoint);
        pool_item_row(pixels, stride, logical_width, logical_height,
            px, y, logical_width - MARGIN - px,
            snapshot, unit, &entry, selected);
    }

    rect(pixels, stride, logical_width, logical_height,
        MARGIN, bottom, logical_width - MARGIN * 2, 1, UINT32_C(0xFF303A47));
    {
        const Fe8ItemInfo *info = NULL;
        char detail[128];
        if (ui->has_inspected)
            info = endpoint_info(snapshot, ui->inspected);
        else if (ui->has_selection)
            info = endpoint_info(snapshot, ui->selected);
        else if (unit) {
            for (row = 0; row < FE8_INVENTORY_ITEM_SLOTS; ++row) {
                if (unit->items[row]) {
                    info = &unit->item_info[row];
                    break;
                }
            }
        }
        if (info && info->id) {
            text_box(pixels, stride, logical_width, logical_height,
                MARGIN + 9, bottom + 5, 150,
                info->name, UINT32_C(0xFFF4F7FA), 1, 30);
            compatibility_detail(detail, sizeof(detail), unit, info);
            text_box(pixels, stride, logical_width, logical_height,
                MARGIN + 160, bottom + 5,
                logical_width - MARGIN * 2 - 169,
                detail, UINT32_C(0xFFFFD890), 1, 58);
            if (info->description[0]) {
                fe8_host_text_draw(&canvas,
                    (MARGIN + 9) * render_scale, (bottom + 21) * render_scale,
                    (logical_width - MARGIN * 2 - 18) * render_scale,
                    31 * render_scale, info->description, 10.0f * render_scale,
                    UINT32_C(0xFFB9C2CE), FE8_HOST_TEXT_REGULAR, 1);
            } else {
                text_box(pixels, stride, logical_width, logical_height,
                    MARGIN + 9, bottom + 25,
                    logical_width - MARGIN * 2 - 18,
                    "No item description is available.",
                    UINT32_C(0xFF8F9AA8), 1, 80);
            }
        } else {
            text_box(pixels, stride, logical_width, logical_height,
                MARGIN + 9, bottom + 20,
                logical_width - MARGIN * 2 - 18,
                "Pick a target, then choose any unit or supply item from the shared list.",
                UINT32_C(0xFF8F9AA8), 1, 90);
        }
    }
    fe8_host_text_end(&canvas);
    text_canvas = NULL;
    render_scale = 1;
}
