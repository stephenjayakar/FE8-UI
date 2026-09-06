/* Record actual text submissions: verify the painter presents the right unit's
   fields, rather than testing a duplicate formatting helper. Native font and
   pixel rendering are exercised by the existing desktop/ROM tests. */
#include "inventory_desktop.h"
#include "host_text.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct TextCall {
    int x, y, width, height;
    char text[256];
} TextCall;
static TextCall calls[8192];
static int call_count;
static Fe8InventorySnapshot snapshot, original;

int fe8_host_text_begin(Fe8HostTextCanvas *c, uint32_t *p, int stride, int w, int h) {
    *c = (Fe8HostTextCanvas){NULL, p, stride, w, h};
    return 1;
}
void fe8_host_text_draw(Fe8HostTextCanvas *c, int x, int y, int w, int h,
    const char *text, float size, uint32_t color, Fe8HostTextWeight weight, int wrap) {
    (void)c; (void)size; (void)color; (void)weight; (void)wrap;
    assert(call_count < (int)(sizeof(calls) / sizeof(calls[0])));
    TextCall *t = &calls[call_count++];
    *t = (TextCall){x, y, w, h, {0}};
    snprintf(t->text, sizeof(t->text), "%s", text);
}
void fe8_host_text_end(Fe8HostTextCanvas *c) { (void)c; }

static void fixture(void) {
    snapshot.unit_count = 6;
    snapshot.supply_capacity = 200;
    snapshot.supply_address = 0x0203B200;
    for (int n = 0; n < snapshot.unit_count; ++n) {
        Fe8InventoryUnit *u = &snapshot.units[n];
        u->address = 0x0202BE4C + n * 0x48;
        snprintf(u->name, sizeof(u->name), "Unit %d", n);
        strcpy(u->class_name, "Knight");
        u->level = 12 + n; u->exp = n == 1 ? 255 : n == 2 ? 99 : 0;
        u->hp = 15 + n; u->max_hp = 30 + n;
        u->power = n ? 10 + n : 255; u->skill = 20 + n;
        u->speed = 30 + n; u->luck = 40 + n;
        u->defense = 50 + n; u->resistance = n ? 60 + n : 0;
        u->constitution = 70 + n; u->movement = 80 + n;
        u->items[0] = 0x2801; u->item_info[0].id = 1;
        u->item_info[0].movable = true; u->item_info[0].attributes = 1;
        strcpy(u->item_info[0].name, "Iron Sword");
    }
    original = snapshot;
}
static int pixel(float scale, int v) { return (int)(scale * v + .5f); }
static void stats_present(const Fe8InventoryUnit *u, int top, int bottom) {
    const char *names[] = {"Pow", "Skl", "Spd", "Lck", "Def", "Res", "Con", "Mov"};
    unsigned values[] = {u->power, u->skill, u->speed, u->luck,
        u->defense, u->resistance, u->constitution, u->movement};
    for (int n = 0; n < 8; ++n) {
        int found = 0;
        char value[8]; snprintf(value, sizeof(value), "%u", values[n]);
        for (int k = 0; k + 1 < call_count; ++k) {
            const TextCall *label = &calls[k], *number = &calls[k + 1];
            if (label->y < top || label->y >= bottom || strcmp(label->text, names[n])) continue;
            assert(!strcmp(number->text, value));
            assert(number->y >= top && number->y + number->height <= bottom);
            assert(label->x + label->width <= number->x);
            assert(label->width > 0 && number->width > 0);
            ++found;
        }
        assert(found == 1);
    }
}
static void check(int w, int h, float dpi, int zoom, int comfortable, int board) {
    Fe8InventoryUi ui;
    fe8_inventory_ui_init(&ui); fe8_inventory_ui_open(&ui, &snapshot);
    ui.desktop = 1; ui.desktop_scale = dpi; ui.zoom_percent = zoom;
    ui.by_unit = board; ui.comfortable = comfortable;
    ui.has_detail = ui.has_selection = 1;
    ui.detail = ui.selected = (Fe8InventoryEndpoint){FE8_INVENTORY_ENDPOINT_UNIT, snapshot.units[0].address, 0};
    Fe8InventoryDesktopLayout l;
    fe8_inventory_desktop_layout(&ui, w, h, &l);
    float scale = fe8_inventory_desktop_scale(&ui, w, h);
    size_t stride = w + 7, count = stride * h + 16;
    uint32_t *pixels = malloc(count * sizeof(*pixels)); assert(pixels);
    for (size_t k = 0; k < count; ++k) pixels[k] = 0x12345678;
    for (int chosen = 0; chosen < 3; ++chosen) {
        ui.current_unit = chosen;
        call_count = 0;
        fe8_inventory_desktop_draw(&ui, &snapshot, pixels, (int)stride, w, h);
        if (board) {
            assert(l.board_rows > 0);
            if (w == 640 && h == 480 && zoom == 100 && dpi == 1) assert(l.board_rows == 2);
            for (int row = 0; row < l.board_rows && row < snapshot.unit_count; ++row) {
                int top = l.board_y + row * l.board_row_height + l.board_card_height;
                stats_present(&snapshot.units[row], pixel(scale, top), pixel(scale, top + l.board_row_height - l.board_card_height));
                int index;
                /* Stats select the owner (and act as an ally drop target),
                   never alias a slot despite sitting directly underneath it. */
                assert(fe8_inventory_desktop_hit(&ui, &snapshot, w, h,
                    pixel(scale, l.board_x + l.identity_width + 30), pixel(scale, top + 8), &index) == FE8_INVENTORY_HIT_ROSTER);
                assert(index == row);
            }
        } else {
            assert(l.stats_y + 2 * l.stat_row_height <= l.items_y);
            assert(l.items_y + 5 * l.side_row_height < l.roster_y);
            assert(l.roster_rows >= 1);
            stats_present(&snapshot.units[chosen], pixel(scale, l.stats_y), pixel(scale, l.stats_y + 2 * l.stat_row_height));
            int index;
            assert(fe8_inventory_desktop_hit(&ui, &snapshot, w, h,
                pixel(scale, 30), pixel(scale, l.stats_y + 8), &index) == FE8_INVENTORY_HIT_NONE);
        }
        char expected[40];
        const Fe8InventoryUnit *u = &snapshot.units[chosen];
        if (u->exp == 255) snprintf(expected, sizeof(expected), "Lv %u · EXP --", u->level);
        else snprintf(expected, sizeof(expected), "Lv %u · EXP %u", u->level, u->exp);
        int found = 0;
        for (int k = 0; k < call_count; ++k) if (!strcmp(calls[k].text, expected)) ++found;
        if (!board || chosen < l.board_rows) assert(found == 1);
        assert(ui.has_selection && ui.selected.unit_address == snapshot.units[0].address && ui.selected.slot == 0);
        assert(ui.has_detail && ui.detail.unit_address == snapshot.units[0].address);
        assert(!memcmp(&snapshot, &original, sizeof(snapshot)));
    }
    for (int y = 0; y < h; ++y)
        for (int x = w; x < (int)stride; ++x) assert(pixels[y * stride + x] == 0x12345678);
    for (size_t k = stride * h; k < count; ++k) assert(pixels[k] == 0x12345678);
    free(pixels);
}
static int has_text(const char *text) {
    for(int n=0;n<call_count;++n) if(!strcmp(calls[n].text,text))return 1;
    return 0;
}
static void effective_modes(void) {
    const int sizes[][3]={{640,480,1},{1280,800,1},{2560,1600,2}};
    for(int board=0;board<2;++board) for(unsigned size=0;size<sizeof(sizes)/sizeof(sizes[0]);++size) {
        fixture();
        for(int n=0;n<snapshot.unit_count;++n) {
            Fe8InventoryUnit *u=&snapshot.units[n];u->effective_stats_valid=true;
            for(int stat=0;stat<FE8_STAT_COUNT;++stat)u->effective_stats[stat]=fe8_inventory_stat_base(u,(Fe8UnitStat)stat);
            u->effective_stats[FE8_STAT_SPEED]+=3;u->effective_stats[FE8_STAT_DEFENSE]-=2;
            u->effective_stats[FE8_STAT_MAX_HP]+=7;
        }
        original=snapshot;
        int w=sizes[size][0],h=sizes[size][1];
        Fe8InventoryUi ui;fe8_inventory_ui_init(&ui);fe8_inventory_ui_open(&ui,&snapshot);
        ui.desktop=1;ui.desktop_scale=sizes[size][2];ui.by_unit=board;
        ui.has_selection=1;ui.selected=(Fe8InventoryEndpoint){FE8_INVENTORY_ENDPOINT_UNIT,snapshot.units[0].address,0};
        uint32_t *pixels=calloc((size_t)w*h,sizeof(*pixels));assert(pixels);
        for(int base=0;base<2;++base) {
            assert(ui.stats_base==base);
            Fe8InventoryDesktopLayout l;fe8_inventory_desktop_layout(&ui,w,h,&l);
            float scale=fe8_inventory_desktop_scale(&ui,w,h);
            call_count=0;fe8_inventory_desktop_draw(&ui,&snapshot,pixels,w,w,h);
            assert(has_text(base?"Base stats":"Total stats"));
            Fe8InventoryUnit expected=snapshot.units[0];
            if(!base){expected.speed+=3;expected.defense-=2;expected.max_hp+=7;}
            int top=board?l.board_y+l.board_card_height:l.stats_y;
            int bottom=board?l.board_y+l.board_row_height:l.stats_y+2*l.stat_row_height;
            stats_present(&expected,pixel(scale,top),pixel(scale,bottom));
            char hp[32];snprintf(hp,sizeof(hp),board?"%u/%u":"HP %u / %u",expected.hp,expected.max_hp);
            assert(has_text(hp));
            ui.pointer_x=pixel(scale,board?l.board_x+96+(l.board_width-112)*2/8+8:22+(l.sidebar-24)*2/4+8);
            ui.pointer_y=pixel(scale,top+8);
            call_count=0;fe8_inventory_desktop_draw(&ui,&snapshot,pixels,w,w,h);
            assert(has_text("Unit 0 · Speed") && has_text("Total 33 = Base 30 +3"));
            ui.pointer_x=ui.pointer_y=-1;
            int index;Fe8InventoryHitKind kind=fe8_inventory_desktop_hit(&ui,&snapshot,w,h,pixel(scale,380),pixel(scale,25),&index);
            assert(kind==FE8_INVENTORY_HIT_STAT_MODE);
            assert(fe8_inventory_desktop_click(&ui,&snapshot,&kind,&index));
            assert(ui.has_selection && ui.selected.unit_address==snapshot.units[0].address);
            assert(!memcmp(&snapshot,&original,sizeof(snapshot)));
        }
        for(int n=0;n<snapshot.unit_count;++n)snapshot.units[n].effective_stats_valid=false;
        call_count=0;fe8_inventory_desktop_draw(&ui,&snapshot,pixels,w,w,h);assert(has_text("Base only"));
        snapshot.units[1].effective_stats_valid=true;
        call_count=0;fe8_inventory_desktop_draw(&ui,&snapshot,pixels,w,w,h);assert(has_text("Mixed stats"));
        free(pixels);
    }
}

int main(void) {
    fixture();
    for (int board = 0; board < 2; ++board) for (int comfy = 0; comfy < 2; ++comfy) {
        check(640, 480, 1, 100, comfy, board);
        check(960, 640, 1, 100, comfy, board);
        check(1440, 900, 1, 100, comfy, board);
        check(2560, 1600, 2, 100, comfy, board);
        check(1920, 1200, 1.5f, 100, comfy, board);
        for (int zoom = 80; zoom <= 200; zoom += 10)
            check(1280, 960, 1, zoom, comfy, board);
    }
    effective_modes();
    puts("Total/base stat modes, modifiers, fallbacks and tooltips passed");
    puts("Unit stat values, owner selection, EXP sentinel, layout, DPI/zoom and read-only painting passed");
    return 0;
}
