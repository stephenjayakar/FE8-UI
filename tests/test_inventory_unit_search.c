#include "inventory_pins.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Fe8InventorySnapshot s, original;
static void fixture(void) {
    memset(&s,0,sizeof(s));
    s.unit_count=24; s.supply_address=0x0203B200; s.supply_capacity=200;
    s.supply_display_count=1; s.first_empty_supply=0;
    for(int n=0;n<s.unit_count;++n) {
        Fe8InventoryUnit *u=&s.units[n];
        u->address=0x0202BE4C+n*0x48;u->character_id=n+1;
        snprintf(u->name,sizeof(u->name),"Ally %02d",n+1);
        strcpy(u->class_name,n%2?"Archer":"Knight");
        u->hp=u->max_hp=20;u->level=5;u->exp=20;u->speed=10;u->power=8;
        u->skill=9;u->defense=7;u->luck=5;u->resistance=3;u->constitution=6;u->movement=5;
        u->ranks[0]=100;
        Fe8ItemInfo *i=&u->item_info[0];i->id=n+1;i->attributes=1;i->movable=true;
        i->weapon_rank=1;i->max_uses=40;strcpy(i->name,"Iron Sword");
        u->items[0]=0x2800+i->id;
    }
    strcpy(s.units[0].name,"Marth");strcpy(s.units[0].class_name,"Lord");
    strcpy(s.units[1].name,"Caeda");strcpy(s.units[1].class_name,"Pegasus Knight");
    strcpy(s.units[2].name,"Abel");strcpy(s.units[3].name,"Linde");
    strcpy(s.units[3].class_name,"Mage");s.units[3].items[0]=0; /* Stale info is not an item. */
    strcpy(s.units[18].name,"Cain");original=s;
}
static Fe8InventoryUi opened(void) {
    Fe8InventoryUi ui;fe8_inventory_ui_init(&ui);fe8_inventory_ui_open(&ui,&s);
    ui.desktop=1;ui.desktop_scale=1;ui.by_unit=1;ui.pointer_x=ui.pointer_y=-1;return ui;
}
static void query(Fe8InventoryUi *ui,const char *text) {
    fe8_inventory_desktop_clear_query(ui);ui->search_active=1;
    fe8_inventory_desktop_text(ui,text);
}
static int click(Fe8InventoryUi *ui,Fe8InventoryHitKind kind,int index) {
    return fe8_inventory_desktop_click(ui,&s,&kind,&index);
}
static void matching(void) {
    Fe8InventoryUi ui=opened();Fe8InventoryDesktopLayout l;Fe8InventoryBoardView v;
    fe8_inventory_desktop_layout(&ui,1440,900,&l);
    const struct {const char *query;int count,first;} cases[]={
        {"  MAR  ",1,0},{"cAeDa knight",1,1},{"Pegasus",1,1},{"linde",1,3},
        {"Mage",1,3},{"linde sword",0,-1},{"Items",0,-1},{"caIn Iron",1,18},
        {"nonexistent",0,-1},{" \t ",24,0},{"sword",23,0},{"knight",12,1}
    };
    for(unsigned n=0;n<sizeof(cases)/sizeof(*cases);++n) {
        query(&ui,cases[n].query);fe8_inventory_board_view(&ui,&s,&l,&v);
        assert(v.other_count==cases[n].count && v.match_count==cases[n].count);
        if(v.other_count)assert(v.others[0]==cases[n].first);
    }
    query(&ui,"linde");ui.pool_scope=FE8_INVENTORY_POOL_SUPPLY;
    ui.type_filter=8;ui.usable_only=1;fe8_inventory_ui_rebuild(&ui,&s);
    fe8_inventory_board_view(&ui,&s,&l,&v);assert(v.other_count==1 && v.others[0]==3);
    /* The original item browser keeps its own item-based search semantics. */
    ui.type_filter=0;ui.usable_only=0;ui.pool_scope=FE8_INVENTORY_POOL_ALL;
    fe8_inventory_ui_rebuild(&ui,&s);query(&ui,"Marth");
    int indices[FE8_INVENTORY_POOL_CAPACITY];assert(fe8_inventory_desktop_visible(&ui,&s,indices)==1);
    assert(ui.pool[indices[0]].unit_index==0);
    assert(!fe8_inventory_desktop_unit_matches(NULL,&s,0));
    assert(!fe8_inventory_desktop_unit_matches(&ui,&s,-1));
    assert(!fe8_inventory_desktop_unit_matches(&ui,&s,s.unit_count));
}
static void editing(void) {
    Fe8InventoryUi ui=opened();ui.current_unit=18;ui.undo_count=3;
    ui.selected=ui.detail=ui.comparison=(Fe8InventoryEndpoint){FE8_INVENTORY_ENDPOINT_UNIT,s.units[18].address,0};
    ui.has_selection=ui.has_detail=ui.has_comparison=1;
    assert(click(&ui,FE8_INVENTORY_HIT_PIN_UNIT,0));
    ui.loadout_scroll=10;ui.pool_scroll=4;ui.supply_scroll=7;ui.search_active=1;
    fe8_inventory_desktop_text(&ui,"Caed");
    assert(ui.loadout_scroll==0 && ui.pool_scroll==0 && ui.supply_scroll==0);
    ui.loadout_scroll=9;fe8_inventory_desktop_backspace(&ui);assert(ui.loadout_scroll==0);
    assert(!strcmp(ui.query,"Cae"));
    ui.loadout_scroll=9;fe8_inventory_desktop_clear_query(&ui);
    assert(!ui.query[0] && !ui.loadout_scroll && ui.search_active);
    assert(ui.pinned_count==1 && ui.current_unit==18 && ui.undo_count==3);
    assert(ui.has_selection && ui.has_detail && ui.has_comparison);
    assert(ui.selected.unit_address==s.units[18].address && ui.detail.unit_address==s.units[18].address);
    query(&ui,"Marth");ui.loadout_scroll=4;
    assert(click(&ui,FE8_INVENTORY_HIT_RESET,0));assert(!ui.query[0] && !ui.loadout_scroll);
}
static Fe8InventoryHitKind hit(Fe8InventoryUi *ui,int w,int h,int x,int y,int *index) {
    float k=fe8_inventory_desktop_scale(ui,w,h);
    return fe8_inventory_desktop_hit(ui,&s,w,h,(int)(x*k+.5f),(int)(y*k+.5f),index);
}
static void geometry(int w,int h,int dpi,int zoom,int comfortable) {
    Fe8InventoryUi ui=opened();ui.desktop_scale=(float)dpi;ui.zoom_percent=zoom;ui.comfortable=comfortable;
    assert(click(&ui,FE8_INVENTORY_HIT_PIN_UNIT,0));
    assert(click(&ui,FE8_INVENTORY_HIT_PIN_UNIT,2));
    Fe8InventoryDesktopLayout l;Fe8InventoryBoardView before,v;
    fe8_inventory_desktop_layout(&ui,w,h,&l);fe8_inventory_board_view(&ui,&s,&l,&before);
    fe8_inventory_desktop_scroll(&ui,&s,w,h,30*dpi,999);
    query(&ui,"Cain");fe8_inventory_board_view(&ui,&s,&l,&v);
    assert(v.pinned_count==2 && v.other_count==1 && v.other_total==22 && v.match_count==1);
    assert(v.others[0]==18 && !v.other_start);
    assert(v.pinned_rows==before.pinned_rows && v.row_height==before.row_height && v.pinned_start==before.pinned_start);
    assert(v.other_y==before.other_y);
    for(int slot=0;slot<5;++slot) {
        int index,x=l.board_x+l.identity_width+slot*l.slot_width+12;
        Fe8InventoryHitKind kind=hit(&ui,w,h,x,v.other_y+30,&index);
        assert(kind==FE8_INVENTORY_HIT_LOADOUT_ITEM && index==18*5+slot);
        Fe8InventoryEndpoint e=fe8_inventory_ui_endpoint(&ui,&s,kind,index);
        assert(e.unit_address==s.units[18].address && e.slot==(unsigned)slot);
    }
    assert(click(&ui,FE8_INVENTORY_HIT_PIN_UNIT,18));
    fe8_inventory_board_view(&ui,&s,&l,&v);assert(v.other_count==0 && v.match_count==1 && v.pinned_count==3);
    query(&ui,"nobody");fe8_inventory_board_view(&ui,&s,&l,&v);
    assert(!v.other_count && !v.match_count && v.pinned_count==3);
    int index;assert(hit(&ui,w,h,l.board_x+l.identity_width+12,v.other_y+30,&index)==FE8_INVENTORY_HIT_NONE);
    size_t stride=w+7,total=stride*h+16;
    uint32_t *pixels=malloc(total*4);assert(pixels);
    for(size_t n=0;n<total;++n)pixels[n]=0x12345678;
    fe8_inventory_desktop_draw(&ui,&s,pixels,(int)stride,w,h);
    for(int y=0;y<h;++y)for(int x=w;x<(int)stride;++x)assert(pixels[y*stride+x]==0x12345678);
    for(size_t n=stride*h;n<total;++n)assert(pixels[n]==0x12345678);
    free(pixels);
    query(&ui,"Knight");fe8_inventory_board_view(&ui,&s,&l,&v);
    assert(v.other_count==10); /* Abel and Cain are pinned. */
    fe8_inventory_desktop_scroll(&ui,&s,w,h,30*dpi,999);
    fe8_inventory_board_view(&ui,&s,&l,&v);
    assert(v.other_start==(v.other_count>v.other_rows?v.other_count-v.other_rows:0));
    assert(!memcmp(&s,&original,sizeof(s)));
}
static void transfer_target(void) {
    Fe8InventoryUi ui=opened();assert(click(&ui,FE8_INVENTORY_HIT_PIN_UNIT,0));query(&ui,"Cain");
    Fe8InventoryDesktopLayout l;Fe8InventoryBoardView v;
    fe8_inventory_desktop_layout(&ui,1440,900,&l);fe8_inventory_board_view(&ui,&s,&l,&v);
    int x=l.board_x+l.identity_width+18,y=v.top+30,dx=x+l.slot_width,dy=v.other_y+30,index;
    Fe8InventoryHitKind kind=hit(&ui,1440,900,x,y,&index);
    assert(kind==FE8_INVENTORY_HIT_LOADOUT_ITEM && index==0);
    fe8_inventory_desktop_pointer_down(&ui,&s,kind,index,x,y);
    fe8_inventory_desktop_pointer_motion(&ui,&s,1440,900,dx,dy);
    kind=hit(&ui,1440,900,dx,dy,&index);assert(index==18*5+1);
    assert(!fe8_inventory_desktop_pointer_up(&ui,&s,&kind,&index));
    Fe8InventoryEndpoint e=fe8_inventory_ui_endpoint(&ui,&s,kind,index);
    assert(e.unit_address==s.units[18].address && e.slot==1);
    assert(ui.selected.unit_address==s.units[0].address && ui.selected.slot==0);
    assert(!memcmp(&s,&original,sizeof(s)));
}
int main(void) {
    fixture();matching();editing();transfer_target();
    const int sizes[][4]={{640,480,1,100},{960,640,1,130},{1440,900,1,100},{1920,1280,2,120},{3840,2160,2,150}};
    for(unsigned n=0;n<sizeof(sizes)/sizeof(*sizes);++n)for(int d=0;d<2;++d)
        geometry(sizes[n][0],sizes[n][1],sizes[n][2],sizes[n][3],d);
    assert(!memcmp(&s,&original,sizeof(s)));
    puts("Unit search: filtered roster, empty inventories, stable pins, canonical drag targets and DPI passed");
    return 0;
}
