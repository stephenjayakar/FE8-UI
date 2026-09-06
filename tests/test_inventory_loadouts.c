#include "inventory_desktop.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Fe8InventorySnapshot snapshot, original;
static void fixture(void) {
    memset(&snapshot,0,sizeof(snapshot));
    snapshot.unit_count=4; snapshot.supply_address=0x0203B200;
    snapshot.supply_capacity=200; snapshot.supply_count=2;
    snapshot.supply_display_count=3; snapshot.first_empty_supply=2;
    const char *names[]={"Marth","Caeda","Athena","Abel"};
    for(int n=0;n<4;++n) {
        Fe8InventoryUnit *u=&snapshot.units[n];
        u->address=0x0202BE4C+n*0x48; u->character_id=n==2?0x36:n+1;
        snprintf(u->name,sizeof(u->name),"%s",names[n]);strcpy(u->class_name,"Knight");
        u->ranks[0]=n?31:1;u->level=5;u->hp=u->max_hp=20;
        u->exp=25+n*10;u->power=5+n;u->skill=7+n;u->speed=11-n;
        u->luck=4+n;u->defense=8+n;u->resistance=2+n;
        u->constitution=6+n;u->movement=5+n;
        for(int j=0;j<(n==3?5:2);++j) {
            Fe8ItemInfo *i=&u->item_info[j];i->id=(uint8_t)(1+n*5+j);i->attributes=1;
            i->movable=true;i->max_uses=40;i->weapon_rank=1;i->might=5+j;i->hit=90;i->weight=5;
            strcpy(i->name,j?"Steel Sword":"Iron Sword");
            u->items[j]=(uint16_t)(0x2800+i->id);
        }
    }
    snapshot.units[3].item_info[4].movable=false;
    for(int n=0;n<2;++n) {
        snapshot.supply_info[n]=snapshot.units[n].item_info[0];
        snapshot.supply[n]=snapshot.units[n].items[0];
        snapshot.supply_display_slots[n]=(uint16_t)n;
    }
    snapshot.supply_display_slots[2]=2;original=snapshot;
}
static Fe8InventoryUi opened(void) {
    Fe8InventoryUi ui;fe8_inventory_ui_init(&ui);fe8_inventory_ui_open(&ui,&snapshot);
    ui.desktop=1;ui.desktop_scale=1;return ui;
}
static int click(Fe8InventoryUi *ui,Fe8InventoryHitKind kind,int index) {
    return fe8_inventory_desktop_click(ui,&snapshot,&kind,&index);
}
static int same(Fe8InventoryEndpoint a,Fe8InventoryEndpoint b) {
    return a.kind==b.kind && a.unit_address==b.unit_address && a.slot==b.slot;
}
static void reasons(void) {
    char reason[96];Fe8ItemInfo item=snapshot.units[0].item_info[0];
    fe8_inventory_desktop_use_reason(&snapshot,&snapshot.units[0],&item,reason,sizeof(reason));
    assert(!strcmp(reason,"Can use"));
    item.weapon_rank=31;
    fe8_inventory_desktop_use_reason(&snapshot,&snapshot.units[0],&item,reason,sizeof(reason));
    assert(strstr(reason,"Needs D") && strstr(reason,"has E"));
    item.weapon_type=1;
    fe8_inventory_desktop_use_reason(&snapshot,&snapshot.units[0],&item,reason,sizeof(reason));
    assert(strstr(reason,"proficiency"));
    item.weapon_type=0;item.lock_kind=FE8_ITEM_LOCK_CHARACTER;item.lock_ids[0x36/8]=1u<<(0x36%8);
    fe8_inventory_desktop_use_reason(&snapshot,&snapshot.units[0],&item,reason,sizeof(reason));
    assert(!strcmp(reason,"Athena only"));
    item.lock_ids[1]=1; /* Do not invent a single-owner label for a multi-ID lock. */
    fe8_inventory_desktop_use_reason(&snapshot,&snapshot.units[0],&item,reason,sizeof(reason));
    assert(!strcmp(reason,"Personal weapon"));
    item.lock_kind=FE8_ITEM_LOCK_UNKNOWN;
    fe8_inventory_desktop_use_reason(&snapshot,&snapshot.units[0],&item,reason,sizeof(reason));
    assert(!strcmp(reason,"Restriction unknown"));
    item=snapshot.units[0].item_info[0];item.movable=false;
    fe8_inventory_desktop_use_reason(&snapshot,&snapshot.units[0],&item,reason,sizeof(reason));
    assert(!strcmp(reason,"Can use")); /* Fixed is transfer state, not usability. */
}
static void geometry(int width,int height,float dpi,int zoom,int comfortable) {
    Fe8InventoryUi ui=opened();Fe8InventoryDesktopLayout l;
    ui.by_unit=1;ui.desktop_scale=dpi;ui.zoom_percent=zoom;ui.comfortable=comfortable;
    fe8_inventory_desktop_layout(&ui,width,height,&l);
    assert(l.board_rows>=1 && l.slot_width>=90);
    float scale=fe8_inventory_desktop_scale(&ui,width,height);
    int first=0;
    for(int row=0;row<l.board_rows && row<snapshot.unit_count;++row) for(int slot=0;slot<5;++slot) {
        int x=l.board_x+l.identity_width+slot*l.slot_width+l.slot_width/2;
        int y=l.board_y+row*l.board_row_height+42,index=-1;
        Fe8InventoryHitKind kind=fe8_inventory_desktop_hit(&ui,&snapshot,width,height,
            (int)(x*scale+.5f),(int)(y*scale+.5f),&index);
        assert(kind==FE8_INVENTORY_HIT_LOADOUT_ITEM && index==row*5+slot);
        Fe8InventoryEndpoint e=fe8_inventory_ui_endpoint(&ui,&snapshot,kind,index);
        assert(e.unit_address==snapshot.units[row].address && e.slot==(unsigned)slot);
        if(!first)first=index;
    }
    (void)first;
    fe8_inventory_desktop_scroll(&ui,&snapshot,width,height,(int)(30*scale),999);
    assert(ui.loadout_scroll==(snapshot.unit_count>l.board_rows?snapshot.unit_count-l.board_rows:0));
    /* Every row is retained while filtering. Unmatched equipment is dimmed,
       not removed as a possible destination. */
    strcpy(ui.query,"nothing matches");ui.type_filter=7;
    size_t stride=width+7,total=stride*height+16;
    uint32_t *pixels=malloc(total*sizeof(*pixels));assert(pixels);
    for(int overlay=0;overlay<2;++overlay) {
        ui.details_expanded=overlay;
        for(size_t n=0;n<total;++n)pixels[n]=0x12345678;
        fe8_inventory_desktop_draw(&ui,&snapshot,pixels,(int)stride,width,height);
        for(int y=0;y<height;++y)for(int x=width;x<(int)stride;++x)assert(pixels[y*stride+x]==0x12345678);
        for(size_t n=stride*height;n<total;++n)assert(pixels[n]==0x12345678);
    }
    free(pixels);assert(!memcmp(&snapshot,&original,sizeof(snapshot)));
}
static void scope_and_modal_paint(void) {
    Fe8InventoryUi ui=opened();ui.by_unit=1;ui.status[0]=0;
    enum { W=1440,H=900,STRIDE=1447 };
    size_t total=(size_t)STRIDE*H+16;
    uint32_t *a=malloc(total*sizeof(*a)),*b=malloc(total*sizeof(*b));assert(a && b);
    for(size_t n=0;n<total;++n)a[n]=b[n]=0x12345678;
    fe8_inventory_desktop_draw(&ui,&snapshot,a,STRIDE,W,H);
    ui.pool_scope=FE8_INVENTORY_POOL_SUPPLY;fe8_inventory_ui_rebuild(&ui,&snapshot);
    fe8_inventory_desktop_draw(&ui,&snapshot,b,STRIDE,W,H);
    assert(!memcmp(a,b,total*sizeof(*a))); /* Same board counts, items and Supply. */
    ui.current_unit=3;ui.pointer_x=W-20;ui.pointer_y=H-60;
    ui.detail=(Fe8InventoryEndpoint){FE8_INVENTORY_ENDPOINT_UNIT,snapshot.units[0].address,0};ui.has_detail=1;
    click(&ui,FE8_INVENTORY_HIT_GIVE,0);assert(ui.popup_open);
    fe8_inventory_desktop_draw(&ui,&snapshot,b,STRIDE,W,H);
    assert(memcmp(a,b,(size_t)STRIDE*H*sizeof(*a)));
    for(int y=0;y<H;++y)for(int x=W;x<STRIDE;++x)assert(b[y*STRIDE+x]==0x12345678);
    for(size_t n=(size_t)STRIDE*H;n<total;++n)assert(b[n]==0x12345678);
    free(a);free(b);
}
static void controller(void) {
    Fe8InventoryUi ui=opened();Fe8InventoryEndpoint endpoint;
    click(&ui,FE8_INVENTORY_HIT_LOADOUT_ITEM,0);Fe8InventoryEndpoint pinned=ui.detail;
    click(&ui,FE8_INVENTORY_HIT_COMPARE,5);
    assert(fe8_inventory_desktop_comparison(&ui,&snapshot,&endpoint));
    assert(endpoint.unit_address==snapshot.units[1].address);
    strcpy(ui.query,"sword");ui.pool_scroll=2;
    click(&ui,FE8_INVENTORY_HIT_VIEW_UNITS,0);
    assert(ui.by_unit && !strcmp(ui.query,"sword") && same(ui.detail,pinned) && ui.pool_scroll==2);
    Fe8InventoryDesktopLayout l;fe8_inventory_desktop_layout(&ui,1440,900,&l);
    int x=l.board_x+l.identity_width+20,y=l.board_y+30;
    fe8_inventory_desktop_pointer_down(&ui,&snapshot,FE8_INVENTORY_HIT_LOADOUT_ITEM,0,x,y);
    assert(ui.drag_armed);
    int tx=x+2*l.slot_width,ty=y+l.board_row_height;
    fe8_inventory_desktop_pointer_motion(&ui,&snapshot,1440,900,tx,ty);
    assert(ui.dragging);
    int index=-1;
    Fe8InventoryHitKind hit=fe8_inventory_desktop_hit(&ui,&snapshot,1440,900,tx,ty,&index);
    assert(hit==FE8_INVENTORY_HIT_LOADOUT_ITEM && index==7);
    assert(!fe8_inventory_desktop_pointer_up(&ui,&snapshot,&hit,&index));
    assert(hit==FE8_INVENTORY_HIT_UNIT_ITEM && index==2 && ui.current_unit==1);
    assert(same(ui.selected,pinned));
    fe8_inventory_desktop_cancel_move(&ui);
    ui.current_unit=3;ui.pointer_x=1400;ui.pointer_y=850;
    ui.detail=pinned;ui.has_detail=1;
    assert(click(&ui,FE8_INVENTORY_HIT_GIVE,0));assert(ui.popup_open && ui.has_selection);
    fe8_inventory_desktop_layout(&ui,1440,900,&l);
    assert(l.popup_x>=12 && l.popup_x+l.popup_width<=1428);
    assert(l.popup_y>=l.top && l.popup_y+l.popup_height<=856);
    fe8_inventory_desktop_pointer_motion(&ui,&snapshot,1440,900,l.popup_x+30,l.popup_rows_y+45);
    assert(fe8_inventory_desktop_comparison(&ui,&snapshot,&endpoint));
    assert(endpoint.unit_address==snapshot.units[3].address && endpoint.slot==1);
    assert(click(&ui,FE8_INVENTORY_HIT_SWAP_SLOT,4));assert(ui.popup_open); /* fixed */
    hit=FE8_INVENTORY_HIT_SWAP_SLOT;index=1;
    assert(!fe8_inventory_desktop_click(&ui,&snapshot,&hit,&index));
    assert(!ui.popup_open && hit==FE8_INVENTORY_HIT_UNIT_ITEM && index==1 && ui.current_unit==3);
    assert(same(ui.selected,pinned));
    fe8_inventory_desktop_cancel_move(&ui);
    ui.detail=pinned;ui.has_detail=1;click(&ui,FE8_INVENTORY_HIT_GIVE,0);
    hit=fe8_inventory_desktop_hit(&ui,&snapshot,1440,900,172,20,&index);
    assert(hit==FE8_INVENTORY_HIT_POPUP_CANCEL);
    assert(click(&ui,hit,index));assert(!ui.popup_open && !ui.has_selection && ui.by_unit);
    ui.detail=pinned;click(&ui,FE8_INVENTORY_HIT_GIVE,0);ui.popup_item^=1;
    assert(click(&ui,FE8_INVENTORY_HIT_SWAP_SLOT,0));assert(!ui.popup_open);
    assert(!memcmp(&snapshot,&original,sizeof(snapshot)));
}
static void capture(const char *directory,const char *name,Fe8InventoryUi *ui,int width,int height) {
    char path[1024];int length=snprintf(path,sizeof(path),"%s/%s.ppm",directory,name);
    assert(length>0 && (size_t)length<sizeof(path));
    uint32_t *pixels=calloc((size_t)width*height,sizeof(*pixels));assert(pixels);
    fe8_inventory_desktop_draw(ui,&snapshot,pixels,width,width,height);
    FILE *file=fopen(path,"wb");assert(file);
    assert(fprintf(file,"P6\n%d %d\n255\n",width,height)>0);
    for(int n=0;n<width*height;++n) {
        unsigned char rgb[]={(unsigned char)pixels[n],(unsigned char)(pixels[n]>>8),(unsigned char)(pixels[n]>>16)};
        assert(fwrite(rgb,1,3,file)==3);
    }
    assert(!fclose(file));free(pixels);
}
static void captures(const char *directory) {
    Fe8InventoryUi ui=opened();ui.by_unit=1;
    ui.detail=(Fe8InventoryEndpoint){FE8_INVENTORY_ENDPOINT_UNIT,snapshot.units[0].address,0};ui.has_detail=1;
    capture(directory,"loadouts-overview",&ui,1440,900);
    capture(directory,"loadouts-minimum",&ui,640,480);
    ui.details_expanded=1;capture(directory,"loadouts-details",&ui,640,480);ui.details_expanded=0;
    ui.current_unit=3;ui.pointer_x=1100;ui.pointer_y=400;
    click(&ui,FE8_INVENTORY_HIT_GIVE,0);capture(directory,"loadouts-swap",&ui,1440,900);
    fe8_inventory_desktop_cancel_move(&ui);ui.by_unit=0;
    click(&ui,FE8_INVENTORY_HIT_COMPARE,16);
    capture(directory,"loadouts-comparison",&ui,1440,900);
}
int main(int argc,char **argv) {
    fixture();reasons();controller();scope_and_modal_paint();
    geometry(640,480,1,100,0);geometry(640,480,1,100,1);
    geometry(960,640,1,100,0);geometry(1440,900,1,100,0);
    geometry(2560,1600,2,100,1);geometry(1920,1200,1.5f,100,0);
    for(int zoom=80;zoom<=200;zoom+=10)geometry(1280,960,1,zoom,0);
    if(argc==2)captures(argv[1]);
    puts("Loadout endpoints, full-recipient picker, comparison, explanations, responsive geometry and readonly painting passed");
    return 0;
}
