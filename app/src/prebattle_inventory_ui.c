#include "prebattle_inventory_ui.h"
#include "host_text.h"

#include <stdio.h>
#include <string.h>

enum {
    MARGIN = 8, HEADER_H = 38, FOOTER_H = 56,
    ROSTER_W = 124, UNIT_W = 300, ROW_H = 22, ITEM_ROW_H = 28,
    SECTION_HEADER_H = 18, UNIT_CARD_H = 70,
};

static Fe8HostTextCanvas *text_canvas;
static int render_scale = 1;

static uint32_t canvas_color(uint32_t color) {
    return (color & UINT32_C(0xFF00FF00)) |
        ((color & UINT32_C(0x00FF0000)) >> 16) |
        ((color & UINT32_C(0x000000FF)) << 16);
}

static void rect(uint32_t *p,int s,int w,int h,int x,int y,int rw,int rh,uint32_t c) {
    int yy;x*=render_scale;y*=render_scale;rw*=render_scale;rh*=render_scale;w*=render_scale;h*=render_scale;
    if(x<0){rw+=x;x=0;}if(y<0){rh+=y;y=0;}if(x+rw>w)rw=w-x;if(y+rh>h)rh=h-y;
    c=canvas_color(c);
    for(yy=y;yy<y+rh;yy++){int xx;for(xx=x;xx<x+rw;xx++)p[yy*s+xx]=c;}
}
static void text(uint32_t *p,int s,int w,int h,int x,int y,const char *v,uint32_t c,int scale,int max) {
    char clipped[128]; size_t length;
    (void)p; (void)s; (void)w; (void)h;
    if (!text_canvas || !v) return;
    length = strlen(v);
    if (max > 0 && length > (size_t)max) length = (size_t)max;
    if (length >= sizeof(clipped)) length = sizeof(clipped) - 1;
    memcpy(clipped, v, length); clipped[length] = '\0';
    fe8_host_text_draw(text_canvas, x*render_scale, y*render_scale,
        (w-x-MARGIN)*render_scale, (scale==2?20:14)*render_scale,
        clipped, (scale==2?16.0f:9.5f)*render_scale, c,
        scale == 2 ? FE8_HOST_TEXT_SEMIBOLD : FE8_HOST_TEXT_REGULAR, 0);
}
static int body_top(void){return MARGIN+HEADER_H;}
static int body_bottom(int h){return h-MARGIN-FOOTER_H;}
static int roster_rows(int h){int n=(body_bottom(h)-body_top()-SECTION_HEADER_H)/ROW_H;return n>0?n:1;}
static int supply_x(int w){(void)w;return MARGIN+ROSTER_W+UNIT_W;}
static int supply_rows(int h){int n=(body_bottom(h)-body_top()-SECTION_HEADER_H)/ROW_H;return n>0?n:1;}

void fe8_inventory_ui_init(Fe8InventoryUi *ui){memset(ui,0,sizeof(*ui));ui->render_scale=1;}
void fe8_inventory_ui_open(Fe8InventoryUi *ui){ui->active=1;ui->has_selection=0;ui->has_inspected=0;
    snprintf(ui->status,sizeof(ui->status),"Select an item, then choose its destination");}

void fe8_inventory_ui_scroll(Fe8InventoryUi *ui,int rows,const Fe8InventorySnapshot *snap,
    int width,int height,int pointer_x){int *value;int maximum;int scale=ui->render_scale?ui->render_scale:1;
    width/=scale;height/=scale;pointer_x/=scale;
    if(pointer_x>=supply_x(width)){value=&ui->supply_scroll;maximum=snap->supply_display_count-supply_rows(height);}
    else{value=&ui->roster_scroll;maximum=snap->unit_count-roster_rows(height);}
    if(maximum<0)maximum=0;
    *value+=rows;
    if(*value<0)*value=0;
    if(*value>maximum)*value=maximum;
}

Fe8InventoryHitKind fe8_inventory_ui_hit_test(const Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *snap,int width,int height,int x,int y,int *index){
    int scale=ui->render_scale?ui->render_scale:1;int top,sx,bottom;(void)snap;
    width/=scale;height/=scale;x/=scale;y/=scale;top=body_top();bottom=body_bottom(height);sx=supply_x(width);
    if(!ui->active||x<MARGIN||x>=width-MARGIN||y<top||y>=bottom)return FE8_INVENTORY_HIT_NONE;
    if(x<MARGIN+ROSTER_W){if(y<top+SECTION_HEADER_H)return FE8_INVENTORY_HIT_NONE;
        *index=ui->roster_scroll+(y-top-SECTION_HEADER_H)/ROW_H;return FE8_INVENTORY_HIT_ROSTER;}
    if(x<sx){int item_top=top+UNIT_CARD_H;if(y<item_top)return FE8_INVENTORY_HIT_NONE;*index=(y-item_top)/ITEM_ROW_H;
        return *index<FE8_INVENTORY_ITEM_SLOTS?FE8_INVENTORY_HIT_UNIT_ITEM:FE8_INVENTORY_HIT_NONE;}
    if(y<top+SECTION_HEADER_H)return FE8_INVENTORY_HIT_NONE;
    *index=ui->supply_scroll+(y-top-SECTION_HEADER_H)/ROW_H;
    if(*index>=snap->supply_display_count)return FE8_INVENTORY_HIT_NONE;
    *index=snap->supply_display_slots[*index];return FE8_INVENTORY_HIT_SUPPLY_ITEM;
}

Fe8InventoryEndpoint fe8_inventory_ui_endpoint(const Fe8InventoryUi *ui,
    const Fe8InventorySnapshot *snap,Fe8InventoryHitKind kind,int index){
    Fe8InventoryEndpoint e={FE8_INVENTORY_ENDPOINT_SUPPLY,0,0};
    if(kind==FE8_INVENTORY_HIT_UNIT_ITEM&&ui->current_unit>=0&&ui->current_unit<snap->unit_count){
        e.kind=FE8_INVENTORY_ENDPOINT_UNIT;e.unit_address=snap->units[ui->current_unit].address;e.slot=(unsigned)index;
    }else{e.unit_address=snap->supply_address;e.slot=(unsigned)index;}return e;
}
static const Fe8ItemInfo *endpoint_info(const Fe8InventorySnapshot *snap,
    Fe8InventoryEndpoint e);
uint16_t fe8_inventory_ui_endpoint_item(const Fe8InventorySnapshot *snap,Fe8InventoryEndpoint e){
    unsigned i;if(e.kind==FE8_INVENTORY_ENDPOINT_SUPPLY)return e.slot<snap->supply_capacity?snap->supply[e.slot]:0;
    for(i=0;i<snap->unit_count;i++){
        if(snap->units[i].address==e.unit_address)
            return e.slot<FE8_INVENTORY_ITEM_SLOTS?snap->units[i].items[e.slot]:0;
    }
    return 0;
}
int fe8_inventory_ui_endpoint_movable(const Fe8InventorySnapshot *snap,Fe8InventoryEndpoint e){
    const Fe8ItemInfo *info=endpoint_info(snap,e);return !info||!info->id||info->movable;
}
void fe8_inventory_ui_inspect(Fe8InventoryUi *ui,const Fe8InventorySnapshot *snap,
    Fe8InventoryHitKind kind,int index){
    Fe8InventoryEndpoint endpoint;
    if(kind!=FE8_INVENTORY_HIT_UNIT_ITEM&&kind!=FE8_INVENTORY_HIT_SUPPLY_ITEM)return;
    endpoint=fe8_inventory_ui_endpoint(ui,snap,kind,index);
    if(!fe8_inventory_ui_endpoint_item(snap,endpoint))return;
    ui->inspected=endpoint;ui->has_inspected=1;
}
static const Fe8ItemInfo *endpoint_info(const Fe8InventorySnapshot *snap,Fe8InventoryEndpoint e){
    unsigned i;if(e.kind==FE8_INVENTORY_ENDPOINT_SUPPLY)return e.slot<snap->supply_capacity?&snap->supply_info[e.slot]:NULL;
    for(i=0;i<snap->unit_count;i++){
        if(snap->units[i].address==e.unit_address)
            return e.slot<FE8_INVENTORY_ITEM_SLOTS?&snap->units[i].item_info[e.slot]:NULL;
    }
    return NULL;
}
static void portrait(uint32_t *p,int s,int w,int h,int x,int y,const Fe8InventoryUnit *u){
    int py,px;if(!u->portrait_valid){rect(p,s,w,h,x,y,64,64,UINT32_C(0xFF253E57));return;}
    for(py=0;py<32;py++)for(px=0;px<32;px++){uint32_t c=u->portrait[py*32+px];if(c)rect(p,s,w,h,x+px*2,y+py*2,2,2,canvas_color(c));}
}
static char rank_letter(uint8_t rank) {
    if (rank >= 251) return 'S';
    if (rank >= 181) return 'A';
    if (rank >= 121) return 'B';
    if (rank >= 71) return 'C';
    if (rank >= 31) return 'D';
    if (rank) return 'E';
    return '-';
}
static void item_row(uint32_t *p,int s,int w,int h,int x,int y,int rw,
    const Fe8ItemInfo *info,uint16_t encoded,int selected,int dense){
    char b[96];int rh=dense?ITEM_ROW_H:ROW_H;uint32_t fg=info->movable?UINT32_C(0xFFF3F7FA):UINT32_C(0xFF9EABC0);
    uint32_t bg=selected?UINT32_C(0xFF225EA8):encoded?(info->movable?UINT32_C(0xFF202B38):UINT32_C(0xFF292D35)):UINT32_C(0xFF171D25);
    rect(p,s,w,h,x+2,y+1,rw-4,rh-2,bg);
    if(encoded){text(p,s,w,h,x+7,y+(dense?3:8),info->name,fg,1,dense?26:20);
        if(dense){snprintf(b,sizeof(b),"%u uses",encoded>>8);text(p,s,w,h,x+rw-45,y+3,b,0xFFB8CCE0,1,12);
            snprintf(b,sizeof(b),"Mt %u   Hit %u   Crit %u   Wt %u   Range %u-%u   Rank %c",
                info->might,info->hit,info->crit,info->weight,info->min_range,info->max_range,rank_letter(info->weapon_rank));
            text(p,s,w,h,x+7,y+16,b,fg,1,70);if(!info->movable)text(p,s,w,h,x+210,y+3,"Fixed spell",0xFFFFA46A,1,18);}
        else{snprintf(b,sizeof(b),"%u",encoded>>8);text(p,s,w,h,x+rw-24,y+8,b,0xFFFFD46A,1,3);}}
    else text(p,s,w,h,x+7,y+(dense?10:8),"EMPTY",0xFF73869A,1,8);
}

void fe8_inventory_ui_draw(const Fe8InventoryUi *ui,const Fe8InventorySnapshot *snap,
    uint32_t *p,int s,int w,int h){
    int top,bottom,sx,r;char b[96];const Fe8InventoryUnit *unit=NULL;Fe8HostTextCanvas canvas;int actual_w=w,actual_h=h;
    render_scale=ui->render_scale?ui->render_scale:1;w/=render_scale;h/=render_scale;top=body_top();bottom=body_bottom(h);sx=supply_x(w);
    rect(p,s,w,h,0,0,w,h,0xFF0D1117);rect(p,s,w,h,MARGIN,MARGIN,w-MARGIN*2,h-MARGIN*2,0xFF151B23);
    if(!fe8_host_text_begin(&canvas,p,s,actual_w,actual_h)){render_scale=1;return;}text_canvas=&canvas;
    text(p,s,w,h,MARGIN+10,MARGIN+5,snap->prebattle?"Preparation inventory":"Inventory manager",0xFFF4F7FA,2,28);
    text(p,s,w,h,MARGIN+10,MARGIN+25,ui->status,0xFF9CA8B7,1,80);
    rect(p,s,w,h,MARGIN+ROSTER_W-1,top,1,bottom-top,0xFF303A47);rect(p,s,w,h,sx-1,top,1,bottom-top,0xFF303A47);
    text(p,s,w,h,MARGIN+7,top+5,"Roster",0xFF8AB4E8,1,20);
    for(r=0;r<roster_rows(h);r++){int i=ui->roster_scroll+r,y=top+SECTION_HEADER_H+r*ROW_H;if(i>=snap->unit_count)break;
        if(i==ui->current_unit){rect(p,s,w,h,MARGIN+2,y,ROSTER_W-4,ROW_H,0xFF253A52);rect(p,s,w,h,MARGIN+2,y,3,ROW_H,0xFF4B9BFF);}
        text(p,s,w,h,MARGIN+7,y+10,snap->units[i].name,0xFFF4F7FA,1,17);}
    if(ui->current_unit>=0&&ui->current_unit<snap->unit_count)unit=&snap->units[ui->current_unit];
    if(unit){int ux=MARGIN+ROSTER_W;portrait(p,s,w,h,ux+8,top+5,unit);
        text(p,s,w,h,ux+78,top+7,unit->name,0xFFFFE49A,1,20);text(p,s,w,h,ux+78,top+19,unit->class_name,0xFFA8C8E8,1,20);
        snprintf(b,sizeof(b),"LV%u EXP%u HP%u/%u",unit->level,unit->exp,unit->hp,unit->max_hp);text(p,s,w,h,ux+78,top+31,b,0xFFF3F7FA,1,27);
        snprintf(b,sizeof(b),"POW%u SKL%u SPD%u LCK%u",unit->power,unit->skill,unit->speed,unit->luck);text(p,s,w,h,ux+78,top+45,b,0xFFE7EEF5,1,31);
        snprintf(b,sizeof(b),"DEF%u RES%u CON%u MOV%u",unit->defense,unit->resistance,unit->constitution,unit->movement);text(p,s,w,h,ux+78,top+59,b,0xFFE7EEF5,1,31);
        for(r=0;r<FE8_INVENTORY_ITEM_SLOTS;r++){Fe8InventoryEndpoint e={FE8_INVENTORY_ENDPOINT_UNIT,unit->address,(unsigned)r};
            int selected=ui->has_selection&&ui->selected.kind==e.kind&&ui->selected.unit_address==e.unit_address&&ui->selected.slot==e.slot;
            item_row(p,s,w,h,ux,top+UNIT_CARD_H+r*ITEM_ROW_H,UNIT_W,&unit->item_info[r],unit->items[r],selected,1);}}
    text(p,s,w,h,sx+7,top+5,"Supply",0xFF8AB4E8,1,12);snprintf(b,sizeof(b),"%u / %u",snap->supply_count,snap->supply_capacity);
    text(p,s,w,h,w-MARGIN-38,top+5,b,0xFFB8CCE0,1,8);
    for(r=0;r<supply_rows(h);r++){int display=ui->supply_scroll+r;int i;int y=top+SECTION_HEADER_H+r*ROW_H;Fe8InventoryEndpoint e;
        if(display>=snap->supply_display_count)break;
        i=snap->supply_display_slots[display];e.kind=FE8_INVENTORY_ENDPOINT_SUPPLY;e.unit_address=snap->supply_address;e.slot=(unsigned)i;
        int selected=ui->has_selection&&ui->selected.kind==e.kind&&ui->selected.slot==e.slot;
        item_row(p,s,w,h,sx,y,w-MARGIN-sx,&snap->supply_info[i],snap->supply[i],selected,0);}
    rect(p,s,w,h,MARGIN,bottom,w-MARGIN*2,1,0xFF303A47);
    {const Fe8ItemInfo *info=NULL;if(ui->has_inspected)info=endpoint_info(snap,ui->inspected);else if(ui->has_selection)info=endpoint_info(snap,ui->selected);
    else if(unit){for(r=0;r<FE8_INVENTORY_ITEM_SLOTS;r++)if(unit->items[r]){info=&unit->item_info[r];break;}}
    if(info&&info->id){
        text(p,s,w,h,MARGIN+9,bottom+7,info->name,0xFFF4F7FA,1,40);
        if(info->description[0])fe8_host_text_draw(&canvas,(MARGIN+9)*render_scale,(bottom+23)*render_scale,
            (w-MARGIN*2-18)*render_scale,30*render_scale,info->description,10.0f*render_scale,
            0xFFB9C2CE,FE8_HOST_TEXT_REGULAR,1);
        else text(p,s,w,h,MARGIN+9,bottom+25,"No item description is available.",0xFF8F9AA8,1,80);
    }else text(p,s,w,h,MARGIN+9,bottom+21,"Hover an item for its in-game help text. Click to move it.",0xFF8F9AA8,1,90);}
    fe8_host_text_end(&canvas);text_canvas=NULL;render_scale=1;
}
