#include "inventory_pins.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Fe8InventorySnapshot snapshot, original;
static void fixture(void) {
    memset(&snapshot,0,sizeof(snapshot));
    snapshot.unit_count=24; snapshot.supply_address=0x0203B200;
    snapshot.supply_capacity=200; snapshot.first_empty_supply=0;
    snapshot.supply_display_count=1;
    for(int n=0;n<snapshot.unit_count;++n) {
        Fe8InventoryUnit *u=&snapshot.units[n];
        u->address=0x0202BE4C+n*0x48;u->character_id=n+1;u->class_id=1;
        snprintf(u->name,sizeof(u->name),"Ally %02d",n+1);strcpy(u->class_name,"Knight");
        u->level=5;u->exp=25;u->hp=u->max_hp=20;u->power=8;u->skill=9;u->speed=10;
        u->luck=5;u->defense=7;u->resistance=3;u->constitution=8;u->movement=5;
        u->ranks[0]=100;
        for(int j=0;j<(n==8?5:3);++j) {
            Fe8ItemInfo *i=&u->item_info[j];i->id=(uint8_t)(1+n*5+j);
            i->attributes=1;i->movable=true;i->max_uses=40;i->weapon_rank=1;
            i->weapon_type=0;i->might=5;i->hit=90;i->weight=5;
            strcpy(i->name,j?"Steel Sword":"Iron Sword");u->items[j]=(uint16_t)(0x2800+i->id);
        }
        for(int j=0;j<FE8_STAT_COUNT;++j)u->effective_stats[j]=fe8_inventory_stat_base(u,(Fe8UnitStat)j);
        u->effective_stats[FE8_STAT_SPEED]+=3;u->effective_stats_valid=true;
    }
    snapshot.units[8].item_info[4].movable=false;
    original=snapshot;
}
static Fe8InventoryUi opened(void) {
    Fe8InventoryUi ui;fe8_inventory_ui_init(&ui);fe8_inventory_ui_open(&ui,&snapshot);
    ui.desktop=1;ui.desktop_scale=1;ui.by_unit=1;ui.pointer_x=ui.pointer_y=-1;return ui;
}
static int click(Fe8InventoryUi *ui,Fe8InventoryHitKind kind,int index) {
    return fe8_inventory_desktop_click(ui,&snapshot,&kind,&index);
}
static void pin(Fe8InventoryUi *ui,int unit) {
    assert(click(ui,FE8_INVENTORY_HIT_PIN_UNIT,unit));
}
static Fe8InventoryHitKind hit(Fe8InventoryUi *ui,int w,int h,int x,int y,int *index) {
    float scale=fe8_inventory_desktop_scale(ui,w,h);
    return fe8_inventory_desktop_hit(ui,&snapshot,w,h,(int)(x*scale+.5f),(int)(y*scale+.5f),index);
}
static void lifecycle(void) {
    Fe8InventoryUi ui=opened();ui.current_unit=4;ui.undo_count=7;
    ui.has_selection=ui.has_detail=ui.has_comparison=1;
    ui.selected=ui.detail=ui.comparison=(Fe8InventoryEndpoint){FE8_INVENTORY_ENDPOINT_UNIT,snapshot.units[4].address,1};
    pin(&ui,8);pin(&ui,1);pin(&ui,12);
    assert(ui.pinned_count==3 && ui.pinned_units[0].address==snapshot.units[8].address);
    assert(ui.current_unit==4 && ui.undo_count==7 && ui.has_selection && ui.has_detail && ui.has_comparison);
    assert(ui.selected.unit_address==snapshot.units[4].address && ui.selected.slot==1);
    fe8_inventory_ui_cycle_sort(&ui,&snapshot);ui.pool_scope=FE8_INVENTORY_POOL_SUPPLY;
    strcpy(ui.query,"not present");ui.usable_only=1;fe8_inventory_ui_rebuild(&ui,&snapshot);
    assert(ui.pinned_count==3);
    assert(click(&ui,FE8_INVENTORY_HIT_VIEW_ITEMS,0));assert(ui.pinned_count==3);
    assert(click(&ui,FE8_INVENTORY_HIT_VIEW_UNITS,0));assert(ui.pinned_count==3);
    Fe8InventoryUnit tmp=snapshot.units[1];snapshot.units[1]=snapshot.units[5];snapshot.units[5]=tmp;
    fe8_inventory_ui_rebuild(&ui,&snapshot);
    assert(fe8_inventory_unit_pinned(&ui,&snapshot.units[5]));
    assert(!fe8_inventory_unit_pinned(&ui,&snapshot.units[1]));
    snapshot.units[8].character_id=255; /* Address reused by a different character. */
    fe8_inventory_ui_rebuild(&ui,&snapshot);assert(ui.pinned_count==2);
    snapshot=original;fe8_inventory_ui_rebuild(&ui,&snapshot);
    assert(ui.pinned_count==2 && !fe8_inventory_unit_pinned(&ui,&snapshot.units[8]));
    pin(&ui,1);assert(ui.pinned_count==1 && fe8_inventory_unit_pinned(&ui,&snapshot.units[12]));
    pin(&ui,-1);pin(&ui,62);assert(ui.pinned_count==1);
    fe8_inventory_ui_open(&ui,&snapshot);assert(!ui.pinned_count && !ui.pinned_scroll);
    pin(&ui,1);fe8_inventory_ui_rebuild(&ui,NULL);assert(!ui.pinned_count);
    assert(!memcmp(&snapshot,&original,sizeof(snapshot)));
}
static void check_rows(Fe8InventoryUi *ui,const Fe8InventoryDesktopLayout *l,
    const Fe8InventoryBoardView *v,int width,int height) {
    int seen[FE8_INVENTORY_UNIT_CAPACITY]={0};
    for(int n=0;n<v->pinned_count;++n)assert(!seen[v->pinned[n]]++);
    for(int n=0;n<v->other_count;++n)assert(!seen[v->others[n]]++);
    for(int n=0;n<snapshot.unit_count;++n)
        assert(seen[n]==(fe8_inventory_unit_pinned(ui,&snapshot.units[n]) ||
            fe8_inventory_desktop_unit_matches(ui,&snapshot,n)));
    for(int group=0;group<2;++group) {
        int rows=group?v->other_rows:v->pinned_rows;
        int start=group?v->other_start:v->pinned_start;
        int top=group?v->other_y:v->top;
        for(int row=0;row<rows && start+row<(group?v->other_count:v->pinned_count);++row) {
            int n=group?v->others[start+row]:v->pinned[start+row];
            int y=top+row*v->row_height,index=-1;
            for(int slot=0;slot<5;++slot) {
                int x=l->board_x+l->identity_width+slot*l->slot_width+12;
                Fe8InventoryHitKind kind=hit(ui,width,height,x,y+30,&index);
                assert(kind==FE8_INVENTORY_HIT_LOADOUT_ITEM && index==n*5+slot);
                Fe8InventoryEndpoint e=fe8_inventory_ui_endpoint(ui,&snapshot,kind,index);
                assert(e.unit_address==snapshot.units[n].address && e.slot==(unsigned)slot);
            }
            assert(hit(ui,width,height,l->board_x+l->identity_width-30,y+12,&index)==FE8_INVENTORY_HIT_PIN_UNIT && index==n);
            assert(hit(ui,width,height,l->board_x+96+30,y+v->card_height+6,&index)==FE8_INVENTORY_HIT_ROSTER && index==n);
        }
    }
    if(v->other_count)assert(v->other_y+v->other_rows*v->row_height<=l->deposit_y);
}
static void matrix(int width,int height,float dpi,int zoom,int comfortable) {
    Fe8InventoryUi ui=opened();ui.desktop_scale=dpi;ui.zoom_percent=zoom;ui.comfortable=comfortable;
    pin(&ui,1);pin(&ui,8);pin(&ui,12);pin(&ui,4);pin(&ui,20);
    Fe8InventoryDesktopLayout l;fe8_inventory_desktop_layout(&ui,width,height,&l);
    Fe8InventoryBoardView before,after;fe8_inventory_board_view(&ui,&snapshot,&l,&before);
    assert(before.pinned_rows>=1 && before.other_rows>=1);
    check_rows(&ui,&l,&before,width,height);
    size_t stride=width+7,total=stride*height+16;
    uint32_t *pixels=malloc(total*sizeof(*pixels)),*initial=malloc(total*sizeof(*initial));assert(pixels && initial);
    for(size_t n=0;n<total;++n)pixels[n]=0x12345678;
    fe8_inventory_desktop_draw(&ui,&snapshot,pixels,(int)stride,width,height);
    memcpy(initial,pixels,total*sizeof(*pixels));
    float scale=fe8_inventory_desktop_scale(&ui,width,height);
    /* The wheel above a frozen loadout still moves only the other roster. */
    fe8_inventory_desktop_scroll_at(&ui,&snapshot,width,height,(int)((l.board_x+30)*scale),
        (int)((before.top+20)*scale),9999);
    fe8_inventory_board_view(&ui,&snapshot,&l,&after);
    assert(after.other_start>before.other_start && after.pinned_start==before.pinned_start);
    assert(!memcmp(after.pinned,before.pinned,sizeof(before.pinned)));
    check_rows(&ui,&l,&after,width,height);
    fe8_inventory_desktop_draw(&ui,&snapshot,pixels,(int)stride,width,height);
    int top=(int)(before.top*scale+1),bottom=(int)((before.top+before.pinned_rows*before.row_height)*scale-1);
    for(int y=top;y<bottom;++y)assert(!memcmp(initial+y*stride+(int)(l.board_x*scale+1),pixels+y*stride+(int)(l.board_x*scale+1),(size_t)(l.board_width*scale-3)*4));
    for(int y=0;y<height;++y)for(int x=width;x<(int)stride;++x)assert(pixels[y*stride+x]==0x12345678);
    for(size_t n=stride*height;n<total;++n)assert(pixels[n]==0x12345678);
    int index=-1,right=l.board_x+l.board_width;
    if(after.pinned_count>after.pinned_rows) {
        int scroll=ui.loadout_scroll;
        assert(hit(&ui,width,height,right-200,l.board_y-15,&index)==FE8_INVENTORY_HIT_PIN_PAGE);
        assert(click(&ui,FE8_INVENTORY_HIT_PIN_PAGE,index));
        fe8_inventory_board_view(&ui,&snapshot,&l,&after);
        assert(after.pinned_start<before.pinned_start && ui.loadout_scroll==scroll);
        check_rows(&ui,&l,&after,width,height);
    }
    if(l.supply_width) {
        int scroll=ui.loadout_scroll;
        fe8_inventory_desktop_scroll(&ui,&snapshot,width,height,(int)((l.supply_x+10)*scale),3);
        assert(ui.loadout_scroll==scroll);
    }
    ui.type_filter=8;ui.usable_only=1;strcpy(ui.query,"no matching equipment");
    fe8_inventory_ui_toggle_scope(&ui,&snapshot);
    fe8_inventory_board_view(&ui,&snapshot,&l,&after);check_rows(&ui,&l,&after,width,height);
    assert(after.pinned_count==5 && after.other_count==0 && after.other_total==19);
    assert(after.pinned_rows==before.pinned_rows && after.row_height==before.row_height);
    assert(hit(&ui,width,height,right-60,l.board_y-15,&index)==FE8_INVENTORY_HIT_UNPIN_ALL);
    assert(click(&ui,FE8_INVENTORY_HIT_UNPIN_ALL,index));assert(!ui.pinned_count);
    assert(!memcmp(&snapshot,&original,sizeof(snapshot)));free(initial);free(pixels);
}
static void gestures(void) {
    Fe8InventoryUi ui=opened();pin(&ui,8);pin(&ui,1);
    Fe8InventoryDesktopLayout l;fe8_inventory_desktop_layout(&ui,1440,900,&l);
    fe8_inventory_desktop_scroll(&ui,&snapshot,1440,900,30,8);
    Fe8InventoryBoardView v;fe8_inventory_board_view(&ui,&snapshot,&l,&v);
    int source=v.others[v.other_start];int x=l.board_x+l.identity_width+18,y=v.other_y+30;
    fe8_inventory_desktop_pointer_down(&ui,&snapshot,FE8_INVENTORY_HIT_LOADOUT_ITEM,source*5,x,y);
    int dest=v.pinned[v.pinned_start];int dy=v.top+30,dx=x+l.slot_width;
    fe8_inventory_desktop_pointer_motion(&ui,&snapshot,1440,900,dx,dy);
    assert(ui.dragging && ui.drag_hover_index==dest*5+1);
    Fe8InventoryHitKind kind;int index;kind=hit(&ui,1440,900,dx,dy,&index);
    assert(!fe8_inventory_desktop_pointer_up(&ui,&snapshot,&kind,&index));
    Fe8InventoryEndpoint e=fe8_inventory_ui_endpoint(&ui,&snapshot,kind,index);
    assert(e.unit_address==snapshot.units[dest].address && e.slot==1);
    assert(ui.selected.unit_address==snapshot.units[source].address);
    /* Full pinned ally: drop opens its local swap picker. Pin button becomes
       an ally drop target during a gesture, never a pin-toggle side effect. */
    fe8_inventory_desktop_cancel_move(&ui);
    fe8_inventory_desktop_pointer_down(&ui,&snapshot,FE8_INVENTORY_HIT_LOADOUT_ITEM,source*5,x,y);
    fe8_inventory_desktop_pointer_motion(&ui,&snapshot,1440,900,l.board_x+l.identity_width-30,dy);
    kind=hit(&ui,1440,900,l.board_x+l.identity_width-30,dy,&index);
    assert(kind==FE8_INVENTORY_HIT_ROSTER && index==8);
    assert(fe8_inventory_desktop_pointer_up(&ui,&snapshot,&kind,&index));
    assert(ui.popup_open && ui.popup_unit_address==snapshot.units[8].address && ui.pinned_count==2);
    kind=FE8_INVENTORY_HIT_SWAP_SLOT;index=4;
    assert(fe8_inventory_desktop_click(&ui,&snapshot,&kind,&index)); /* Fixed destination. */
    fe8_inventory_desktop_cancel_move(&ui);
    for(int n=0;n<snapshot.unit_count;++n)if(!fe8_inventory_unit_pinned(&ui,&snapshot.units[n]))pin(&ui,n);
    assert(ui.pinned_count==snapshot.unit_count);
    fe8_inventory_board_view(&ui,&snapshot,&l,&v);
    assert(!v.other_count && !v.other_rows && v.pinned_rows>0);
    assert(!memcmp(&snapshot,&original,sizeof(snapshot)));
}
static void capture(const char *dir,const char *name,Fe8InventoryUi *ui,int w,int h) {
    uint32_t *pixels=calloc((size_t)w*h,sizeof(*pixels));assert(pixels);
    fe8_inventory_desktop_draw(ui,&snapshot,pixels,w,w,h);
    char path[1024];assert(snprintf(path,sizeof(path),"%s/%s.ppm",dir,name)<(int)sizeof(path));
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n%d %d\n255\n",w,h);
    for(int n=0;n<w*h;++n){unsigned char rgb[]={pixels[n],pixels[n]>>8,pixels[n]>>16};assert(fwrite(rgb,1,3,f)==3);}
    assert(!fclose(f));free(pixels);
}
int main(int argc,char **argv) {
    fixture();lifecycle();
    const int sizes[][4]={{640,480,1,100},{960,640,1,130},{1280,800,1,100},{1440,900,1,100},{1920,1280,2,120},{3840,2160,2,150}};
    for(unsigned n=0;n<sizeof(sizes)/sizeof(*sizes);++n)for(int density=0;density<2;++density)
        matrix(sizes[n][0],sizes[n][1],(float)sizes[n][2],sizes[n][3],density);
    gestures();
    if(argc>1){Fe8InventoryUi ui=opened();pin(&ui,1);pin(&ui,8);
        capture(argv[1],"pinned-before-scroll",&ui,1440,900);
        fe8_inventory_desktop_scroll(&ui,&snapshot,1440,900,30,10);
        capture(argv[1],"pinned-after-scroll",&ui,1440,900);
        capture(argv[1],"pinned-minimum",&ui,640,480);}
    puts("Pinned roster: sticky pixels, canonical endpoints, paging, resize, filters, transfers and session reset passed");
    return 0;
}
