#include "inventory_desktop.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Deliberately synthetic loadouts. Optional portrait input is local-only; no
   ROM, save, extracted sprite, or copyrighted asset is checked into the repo. */
static Fe8InventorySnapshot s, before;
static void fixture(void) {
    static const char *const names[]={"Marth","Caeda","Abel","Cain","Jeigan","Gordin","Draug","Wendell","Ogma","Barst","Lena","Merric"};
    static const char *const classes[]={"Lord","Pegasus Knight","Cavalier","Cavalier","Paladin","Archer","Knight","Sage","Mercenary","Fighter","Cleric","Mage"};
    static const struct {const char *name,*description;int type,rank,mt,hit,wt,uses,attributes;} items[]={
        {"Iron Sword","A dependable, lightweight sword. Easy to handle and useful for preserving stronger weapons.",0,1,5,90,5,46,1},
        {"Steel Sword","A powerful sword with a heavy blade. Compare its weight with the recipient's constitution before choosing a loadout.",0,31,8,75,10,30,1},
        {"Silver Sword","A finely crafted silver blade with high might. Requires an experienced sword user.",0,181,13,80,8,20,1},
        {"Iron Lance","A reliable lance for everyday use. Its long reach is represented by the weapon triangle, not additional attack range.",1,1,7,80,8,45,1},
        {"Silver Lance","A lance forged from silver. High might makes it valuable against durable opponents, but its limited uses reward careful planning.",1,181,14,75,10,20,1},
        {"Javelin","A throwing lance. Can strike at close range or from two spaces away.",1,1,6,65,11,20,1},
        {"Iron Axe","A heavy iron axe. Strong and inexpensive, with lower accuracy than most swords.",2,1,8,75,10,45,1},
        {"Iron Bow","A standard bow. Effective against flying units. Cannot attack adjacent enemies.",3,1,6,85,5,45,1},
        {"Heal","Restores an ally's HP. Staff rank and the user's status determine whether it is ready to use.",4,1,0,100,0,30,4},
        {"Fire","A basic anima spell. Its modest weight makes it an accessible choice for a mage.",5,1,5,90,4,40,3},
        {"Vulnerary","Restores HP when used. Items are not included in Ready only: the frontend does not infer consumable-use rules.",8,0,0,0,0,3,0},
        {"Rapier","A personal weapon. Its lock is respected independently from sword rank. This fixture deliberately marks it locked.",0,1,7,95,5,28,8193},
        {"Learned spell","A learned spell occupying an inventory slot. Fixed equipment cannot be moved, given away, or stored.",5,1,5,90,3,0,11},
    };
    memset(&s,0,sizeof(s));s.unit_count=12;s.supply_address=0x0203B200;s.supply_capacity=100;
    s.supply_count=34;s.supply_display_count=35;s.first_empty_supply=34;
    for(int u=0;u<12;++u) {
        Fe8InventoryUnit *unit=&s.units[u];unit->address=0x0202BE4C+u*0x48;
        snprintf(unit->name,sizeof(unit->name),"%s",names[u]);snprintf(unit->class_name,sizeof(unit->class_name),"%s",classes[u]);
        snprintf(unit->description,sizeof(unit->description),"A preview biography for %s. These loadouts demonstrate the UI and are not taken from a save game.",names[u]);
        strcpy(unit->class_description,"A class description drawn into the independently scrollable inspector.");
        unit->level=3+u;unit->hp=unit->max_hp=22+u;unit->constitution=7+u%5;
        unit->ranks[0]=u==4?181:31;unit->ranks[1]=u==4?181:u==1?71:31;
        if(u==7||u==10)unit->ranks[4]=71;
        if(u==11)unit->ranks[5]=71;
        for(int j=0;j<3;++j) {
            int id=(u*3+j)%13;Fe8ItemInfo *i=&unit->item_info[j];
            i->id=(uint8_t)(id+1);i->weapon_type=(uint8_t)items[id].type;i->weapon_rank=(uint8_t)items[id].rank;
            i->might=(uint8_t)items[id].mt;i->hit=(uint8_t)items[id].hit;i->weight=(uint8_t)items[id].wt;
            i->max_uses=(uint8_t)items[id].uses;i->attributes=(uint32_t)items[id].attributes;i->movable=id!=12;
            i->min_range=id==7?2:1;i->max_range=id==5||id==7?2:1;
            strcpy(i->name,items[id].name);strcpy(i->description,items[id].description);
            unit->items[j]=(uint16_t)(((items[id].uses?items[id].uses:1)<<8)|i->id);
        }
    }
    for(int j=0;j<34;++j) {
        int id=j%12;
        for(int u=0;u<12;++u)for(int k=0;k<3;++k)if(s.units[u].item_info[k].id==id+1)s.supply_info[j]=s.units[u].item_info[k];
        s.supply[j]=(uint16_t)((s.supply_info[j].max_uses<<8)|(id+1));s.supply_display_slots[j]=(uint16_t)j;
    }
    s.supply_display_slots[34]=34;
    before=s;
}
static Fe8InventoryUi open_ui(void) {
    Fe8InventoryUi ui;fe8_inventory_ui_init(&ui);fe8_inventory_ui_open(&ui,&s);
    ui.desktop=1;ui.desktop_scale=1;return ui;
}
static int click(Fe8InventoryUi *ui,Fe8InventoryHitKind kind,int index) {
    return fe8_inventory_desktop_click(ui,&s,&kind,&index);
}
static int index_named(Fe8InventoryUi *ui,const char *name) {
    for(int j=0;j<ui->pool_count;++j)if(ui->pool[j].item&&strcmp(ui->pool[j].info->name,name)==0)return j;
    assert(!"fixture item missing");return -1;
}
static void view_and_actions(void) {
    Fe8InventoryUi ui=open_ui();int indices[FE8_INVENTORY_POOL_CAPACITY];
    assert(fe8_inventory_desktop_visible(&ui,&s,indices)==70);
    int silver=index_named(&ui,"Silver Lance");
    assert(click(&ui,FE8_INVENTORY_HIT_POOL_ITEM,silver));
    assert(ui.has_detail&&!ui.has_selection);
    Fe8InventoryEndpoint pinned=ui.detail;
    click(&ui,FE8_INVENTORY_HIT_SEARCH,0);fe8_inventory_desktop_text(&ui,"  sIlVeR  lAnCe ");
    int count=fe8_inventory_desktop_visible(&ui,&s,indices);assert(count>0&&count<70);
    for(int j=0;j<count;++j)assert(strcmp(ui.pool[indices[j]].info->name,"Silver Lance")==0);
    click(&ui,FE8_INVENTORY_HIT_FILTER,1);assert(fe8_inventory_desktop_visible(&ui,&s,indices)==0);
    assert(ui.has_detail&&ui.detail.slot==pinned.slot&&ui.detail.unit_address==pinned.unit_address);
    click(&ui,FE8_INVENTORY_HIT_RESET,0);assert(fe8_inventory_desktop_visible(&ui,&s,indices)==70);
    click(&ui,FE8_INVENTORY_HIT_USABLE,0);
    count=fe8_inventory_desktop_visible(&ui,&s,indices);
    for(int j=0;j<count;++j)assert(fe8_inventory_item_use_state(&s.units[ui.current_unit],ui.pool[indices[j]].info)==FE8_INVENTORY_USE_READY);
    click(&ui,FE8_INVENTORY_HIT_RESET,0);
    assert(click(&ui,FE8_INVENTORY_HIT_MOVE,0));assert(ui.has_selection);
    assert(click(&ui,FE8_INVENTORY_HIT_CANCEL,0));assert(!ui.has_selection);
    ui.current_unit=0;
    Fe8InventoryHitKind kind=FE8_INVENTORY_HIT_GIVE;int slot=-1;
    assert(!fe8_inventory_desktop_click(&ui,&s,&kind,&slot));
    assert(kind==FE8_INVENTORY_HIT_UNIT_ITEM&&slot==3&&ui.has_selection);
    assert(ui.selected.slot==pinned.slot&&ui.selected.unit_address==pinned.unit_address);
    click(&ui,FE8_INVENTORY_HIT_CANCEL,0);
    int fixed=index_named(&ui,"Learned spell");click(&ui,FE8_INVENTORY_HIT_POOL_ITEM,fixed);
    click(&ui,FE8_INVENTORY_HIT_MOVE,0);assert(!ui.has_selection);
    kind=FE8_INVENTORY_HIT_STORE;assert(fe8_inventory_desktop_click(&ui,&s,&kind,&slot));assert(!ui.has_selection);
    assert(memcmp(&s,&before,sizeof(s))==0);
}
static int equal(Fe8InventoryEndpoint a, Fe8InventoryEndpoint b) {
    return a.kind==b.kind && a.unit_address==b.unit_address && a.slot==b.slot;
}
static void arm_drag(Fe8InventoryUi *ui, int pool_index, float dpi) {
    ui->desktop_scale=dpi;
    fe8_inventory_desktop_pointer_down(ui,&s,FE8_INVENTORY_HIT_POOL_ITEM,pool_index,400,240);
    assert(ui->drag_armed && !ui->dragging);
    /* A little click jitter must never become a transfer. */
    fe8_inventory_desktop_pointer_motion(ui,&s,(int)(1280*dpi),(int)(800*dpi),400+(int)(4*dpi),240);
    assert(!ui->dragging && !ui->has_selection);
    fe8_inventory_desktop_pointer_motion(ui,&s,(int)(1280*dpi),(int)(800*dpi),400+(int)(6*dpi),240);
    assert(ui->dragging && ui->has_selection);
}
static void quick_actions_and_drag(void) {
    Fe8InventoryUi ui=open_ui();
    int silver=index_named(&ui,"Silver Lance"), index;
    Fe8InventoryEndpoint source=ui.pool[silver].endpoint;
    Fe8InventoryHitKind kind=FE8_INVENTORY_HIT_QUICK_POOL;
    index=silver;
    assert(!fe8_inventory_desktop_click(&ui,&s,&kind,&index));
    assert(equal(ui.selected,source) && kind==FE8_INVENTORY_HIT_UNIT_ITEM && index==3);
    ui=open_ui();kind=FE8_INVENTORY_HIT_QUICK_UNIT;index=0;
    assert(!fe8_inventory_desktop_click(&ui,&s,&kind,&index));
    assert(kind==FE8_INVENTORY_HIT_POOL_ITEM && ui.pool[index].item==0);
    assert(ui.pool[index].endpoint.slot==s.first_empty_supply);
    ui=open_ui();kind=FE8_INVENTORY_HIT_QUICK_POOL;index=index_named(&ui,"Learned spell");
    assert(fe8_inventory_desktop_click(&ui,&s,&kind,&index));assert(!ui.has_selection);
    /* One local Swap action enters explicit slot choice when the ally is full. */
    ui=open_ui();s.units[0].items[3]=s.units[0].items[4]=s.units[0].items[0];
    s.units[0].item_info[3]=s.units[0].item_info[4]=s.units[0].item_info[0];
    kind=FE8_INVENTORY_HIT_QUICK_POOL;index=silver;
    assert(fe8_inventory_desktop_click(&ui,&s,&kind,&index));assert(ui.has_selection);
    kind=FE8_INVENTORY_HIT_UNIT_ITEM;index=0;
    assert(!fe8_inventory_desktop_click(&ui,&s,&kind,&index));
    assert(equal(ui.selected,source));s=before;
    /* Click release, self-drop, background drop and fixed destination are no-ops. */
    ui=open_ui();fe8_inventory_desktop_pointer_down(&ui,&s,FE8_INVENTORY_HIT_POOL_ITEM,silver,400,240);
    kind=FE8_INVENTORY_HIT_POOL_ITEM;index=silver;
    assert(fe8_inventory_desktop_pointer_up(&ui,&s,&kind,&index));
    assert(!ui.dragging && !ui.has_selection && !ui.drag_armed);
    arm_drag(&ui,silver,1);kind=FE8_INVENTORY_HIT_POOL_ITEM;index=silver;
    assert(fe8_inventory_desktop_pointer_up(&ui,&s,&kind,&index));assert(!ui.has_selection);
    arm_drag(&ui,silver,1);kind=FE8_INVENTORY_HIT_NONE;index=-1;
    assert(fe8_inventory_desktop_pointer_up(&ui,&s,&kind,&index));assert(!ui.has_selection);
    arm_drag(&ui,silver,1);kind=FE8_INVENTORY_HIT_POOL_ITEM;index=index_named(&ui,"Learned spell");
    assert(fe8_inventory_desktop_pointer_up(&ui,&s,&kind,&index));assert(!ui.has_selection);
    /* Sorting after pointer-down cannot change the canonical source. */
    arm_drag(&ui,silver,2);fe8_inventory_ui_cycle_sort(&ui,&s);
    kind=FE8_INVENTORY_HIT_ROSTER;index=0;
    assert(!fe8_inventory_desktop_pointer_up(&ui,&s,&kind,&index));
    assert(kind==FE8_INVENTORY_HIT_UNIT_ITEM && index==3 && equal(ui.selected,source));
    ui=open_ui();arm_drag(&ui,silver,1.25f);kind=FE8_INVENTORY_HIT_UNIT_ITEM;index=0;
    assert(!fe8_inventory_desktop_pointer_up(&ui,&s,&kind,&index));
    assert(equal(ui.selected,source)); /* Occupied slot = intentional swap. */
    ui=open_ui();arm_drag(&ui,silver,1);fe8_inventory_desktop_cancel_drag(&ui);
    assert(!ui.has_selection && !ui.drag_armed && !ui.dragging);
    /* A snapshot changed during a gesture must never move the replacement. */
    arm_drag(&ui,silver,1);
    if(source.kind==FE8_INVENTORY_ENDPOINT_SUPPLY) s.supply[source.slot]^=0x100;
    else {
        for(int j=0;j<s.unit_count;++j)if(s.units[j].address==source.unit_address)s.units[j].items[source.slot]^=0x100;
    }
    kind=FE8_INVENTORY_HIT_UNIT_ITEM;index=3;
    assert(fe8_inventory_desktop_pointer_up(&ui,&s,&kind,&index));assert(!ui.has_selection);
    s=before;
    ui=open_ui();int fixed=index_named(&ui,"Learned spell");
    fe8_inventory_desktop_pointer_down(&ui,&s,FE8_INVENTORY_HIT_POOL_ITEM,fixed,400,240);
    assert(!ui.drag_armed);
    /* Dropping on the source's owner is a no-op, not sticky move mode. */
    ui=open_ui();
    fe8_inventory_desktop_pointer_down(&ui,&s,FE8_INVENTORY_HIT_UNIT_ITEM,0,40,200);
    fe8_inventory_desktop_pointer_motion(&ui,&s,1440,900,40,430);
    kind=FE8_INVENTORY_HIT_ROSTER;index=0;
    assert(fe8_inventory_desktop_pointer_up(&ui,&s,&kind,&index));
    assert(!ui.has_selection&&!ui.drag_armed);
    /* Public gesture resolution also rejects a vanished destination. */
    fe8_inventory_desktop_pointer_down(&ui,&s,FE8_INVENTORY_HIT_UNIT_ITEM,0,40,200);
    fe8_inventory_desktop_pointer_motion(&ui,&s,1440,900,40,430);
    kind=FE8_INVENTORY_HIT_POOL_ITEM;index=ui.pool_count;
    assert(fe8_inventory_desktop_pointer_up(&ui,&s,&kind,&index));assert(!ui.has_selection);
    assert(memcmp(&s,&before,sizeof(s))==0);
}

static void utf8_and_order(void) {
    Fe8InventoryUi ui=open_ui();int a[FE8_INVENTORY_POOL_CAPACITY],b[FE8_INVENTORY_POOL_CAPACITY];
    click(&ui,FE8_INVENTORY_HIT_SEARCH,0);fe8_inventory_desktop_text(&ui,"Aé");assert(strcmp(ui.query,"Aé")==0);
    fe8_inventory_desktop_backspace(&ui);assert(strcmp(ui.query,"A")==0);
    char long_text[300];memset(long_text,'x',299);long_text[299]=0;fe8_inventory_desktop_text(&ui,long_text);assert(strlen(ui.query)==63);
    click(&ui,FE8_INVENTORY_HIT_RESET,0);
    int n=fe8_inventory_desktop_visible(&ui,&s,a);ui.sort_descending=1;
    assert(fe8_inventory_desktop_visible(&ui,&s,b)==n);
    for(int j=0;j<n;++j)assert(a[j]==b[n-j-1]);
    click(&ui,FE8_INVENTORY_HIT_SEARCH,0);fe8_inventory_desktop_text(&ui,"Caeda lance");
    n=fe8_inventory_desktop_visible(&ui,&s,a);assert(n>0);
    for(int j=0;j<n;++j)assert(ui.pool[a[j]].unit_index==1&&ui.pool[a[j]].info->weapon_type==1);
}
static void geometry(int w,int h,float dpi,int zoom,int comfy) {
    Fe8InventoryUi ui=open_ui();Fe8InventoryDesktopLayout l;int indices[FE8_INVENTORY_POOL_CAPACITY];
    ui.desktop_scale=dpi;ui.zoom_percent=zoom;ui.comfortable=comfy;
    fe8_inventory_desktop_layout(&ui,w,h,&l);
    assert(l.roster_rows>0&&l.table_rows>0&&l.column_width[0]>=100);
    assert(l.detail_y+l.detail_height<=l.height);
    assert(l.action_x>=l.detail_x&&l.action_x+l.action_width<=l.detail_x+l.detail_width);
    assert(l.action_y+92<=l.detail_y+l.detail_height);
    ui.type_filter=2;
    int n=fe8_inventory_desktop_visible(&ui,&s,indices);assert(n>0);
    float scale=fe8_inventory_desktop_scale(&ui,w,h);
    int index=-1;
    Fe8InventoryHitKind kind=fe8_inventory_desktop_hit(&ui,&s,w,h,(int)((l.pool_x+12)*scale+.5f),(int)((l.table_y+3)*scale+.5f),&index);
    assert(kind==FE8_INVENTORY_HIT_POOL_ITEM&&index==indices[0]);
    kind=fe8_inventory_desktop_hit(&ui,&s,w,h,(int)((l.quick_x+12)*scale+.5f),(int)((l.table_y+3)*scale+.5f),&index);
    assert(kind==FE8_INVENTORY_HIT_QUICK_POOL&&index==indices[0]);
    assert(l.quick_x+l.quick_width<=l.pool_x+l.pool_width);
    fe8_inventory_desktop_scroll(&ui,&s,w,h,(int)((l.pool_x+10)*scale),9999);
    int max=n>l.table_rows?n-l.table_rows:0;assert(ui.pool_scroll==max);
    int stride=w+7;size_t cells=(size_t)stride*h+16;
    uint32_t *pixels=malloc(cells*sizeof(*pixels));assert(pixels);
    for(size_t j=0;j<cells;++j)pixels[j]=0x12345678;
    ui.has_detail=1;ui.detail=ui.pool[indices[0]].endpoint;
    fe8_inventory_desktop_draw(&ui,&s,pixels,stride,w,h);
    for(int y=0;y<h;++y)for(int x=w;x<stride;++x)assert(pixels[y*stride+x]==0x12345678);
    for(size_t j=(size_t)stride*h;j<cells;++j)assert(pixels[j]==0x12345678);
    free(pixels);assert(memcmp(&s,&before,sizeof(s))==0);
}
static void screenshots(const char *dir,const char *portraits) {
    /* Keep preview loadouts plausible without changing the adversarial test fixture. */
    s.units[4].items[0]=s.units[1].items[0];
    s.units[4].item_info[0]=s.units[1].item_info[0];
    if(portraits) {
        FILE *f=fopen(portraits,"rb");assert(f);
        for(int j=0;j<s.unit_count;++j) {
            assert(fread(s.units[j].portrait_palette,4,16,f)==16);
            assert(fread(s.units[j].portrait,1,80*72,f)==80*72);s.units[j].portrait_valid=true;
        }
        fclose(f);
    }
    static const int dimensions[][2]={{1440,900},{1440,900},{960,640},{640,480}};
    static const char *const files[]={"armory-overview.ppm","armory-filtered.ppm","armory-compact.ppm","armory-minimum.ppm"};
    for(int shot=0;shot<4;++shot) {
        int w=dimensions[shot][0],h=dimensions[shot][1];Fe8InventoryUi ui=open_ui();
        ui.comfortable=1;ui.current_unit=4;
        click(&ui,FE8_INVENTORY_HIT_POOL_ITEM,index_named(&ui,"Silver Lance"));
        if(shot==0) {
            int visible[FE8_INVENTORY_POOL_CAPACITY];
            int count=fe8_inventory_desktop_visible(&ui,&s,visible);
            for(int row=0;row<count;++row) if(visible[row]==index_named(&ui,"Silver Lance"))
                ui.pool_scroll=row>3?row-3:0;
        }
        if(shot==1) {ui.type_filter=2;ui.usable_only=1;strcpy(ui.query,"lance");}
        if(shot==2) {click(&ui,FE8_INVENTORY_HIT_MOVE,0);ui.current_unit=1;}
        uint32_t *pixels=calloc((size_t)w*h,4);assert(pixels);
        fe8_inventory_desktop_draw(&ui,&s,pixels,w,w,h);
        char path[1024];snprintf(path,sizeof(path),"%s/%s",dir,files[shot]);FILE *f=fopen(path,"wb");assert(f);
        fprintf(f,"P6\n%d %d\n255\n",w,h);
        for(int j=0;j<w*h;++j){unsigned char rgb[3]={(unsigned char)pixels[j],(unsigned char)(pixels[j]>>8),(unsigned char)(pixels[j]>>16)};assert(fwrite(rgb,1,3,f)==3);}
        fclose(f);free(pixels);
    }
}
int main(int argc,char **argv) {
    fixture();view_and_actions();utf8_and_order();quick_actions_and_drag();
    geometry(640,480,1,100,0);geometry(640,480,1,200,1);
    geometry(960,640,1,100,1);geometry(1280,800,1,100,0);
    geometry(1440,900,1,100,1);geometry(1920,1200,1.5f,100,1);
    geometry(2560,1600,2,100,0);geometry(880,660,1.25f,110,1);
    for(int zoom=80;zoom<=200;zoom+=10)geometry(1280,960,1,zoom,1);
    if(argc>1)screenshots(argv[1],argc>2?argv[2]:NULL);
    puts("Workspace search, filters, UTF-8, endpoint mapping, explicit actions, scaling and framebuffer guards passed.");
    return 0;
}
