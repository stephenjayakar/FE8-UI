#include "voxel_targets.h"
#include <string.h>
static uint8_t r8(const Fe8MemoryReader *m,uint32_t a){return m->read8(m->context,a);}
static uint16_t r16(const Fe8MemoryReader *m,uint32_t a){return r8(m,a)|(uint16_t)r8(m,a+1)<<8;}
static uint32_t r32(const Fe8MemoryReader *m,uint32_t a){return r16(m,a)|(uint32_t)r16(m,a+2)<<16;}
static bool ram(uint32_t a,unsigned n){return !(a&3)&&((a>=0x02000000&&a<=0x02040000-n)||(a>=0x03000000&&a<=0x03008000-n));}
static bool rom(uint32_t a,unsigned n){return a>=0x08000000&&a<=0x0A000000-n;}
static bool function(uint32_t a){return (a&1)&&rom(a&~1u,2);}
static bool candidate(Fe8VoxelTargets *t,const Fe8MemoryReader *m,const Fe8Snapshot *s,uint32_t proc){
    if(!ram(proc,0x3C)||r8(m,proc+0x34)!=1||r8(m,proc+0x28))return false;
    uint32_t script=r32(m,proc),info=r32(m,proc+0x2C),node=r32(m,proc+0x30);
    if(!rom(script,48)||!rom(info,32)||!ram(node,12)||r32(m,proc+4)!=script+16||
            r32(m,proc+0x0C)!=r32(m,script+12)||r32(m,proc+0x14)!=3)return false;
    /* LABEL(0), REPEAT(loop), SLEEP(1), CALL(refresh), GOTO(0), END.
     * Paired with live proc/callback/ring checks, not a fixed ROM address. */
    if(r32(m,script)!=0x0B||r32(m,script+4)||r32(m,script+8)!=3||
            !function(r32(m,script+12))||r32(m,script+16)!=0x1000E||
            r32(m,script+20)||r32(m,script+24)!=2||!function(r32(m,script+28))||
            r32(m,script+32)!=0x0C||r32(m,script+36)||r32(m,script+40)||r32(m,script+44))return false;
    for(unsigned i=0;i<8;++i){uint32_t f=r32(m,info+i*4);if(f&&!function(f))return false;}
    if(!function(r32(m,info+0x14))&&!function(r32(m,proc+0x38)))return false;
    unsigned count=0;uint32_t first=node;
    do {
        if(count==64||!ram(node,12))return false;
        int x=(int8_t)r8(m,node),y=(int8_t)r8(m,node+1);
        unsigned id=r8(m,node+2);
        if(x<0||y<0||x>=s->map_width||y>=s->map_height||
                ((s->flags&FE8_SNAPSHOT_FOG)&&!s->fog[y*s->map_width+x]))return false;
        for(unsigned i=0;i<count;++i)if(t->targets[i].address==node)return false;
        uint32_t next=r32(m,node+4),prev=r32(m,node+8);
        if(!ram(next,12)||!ram(prev,12)||r32(m,next+8)!=node||r32(m,prev+4)!=node)return false;
        t->targets[count++]=(Fe8Target){node,x,y,id};node=next;
    }while(node!=first);
    t->count=count;t->proc=proc;t->script=script;t->info=info;t->active=true;return true;
}
void fe8_voxel_targets_read(Fe8VoxelTargets *t,const Fe8MemoryReader *m,const Fe8Snapshot *s){
    if(!t)return;
    t->active=false;t->count=0;
    if(!m||!m->read8||!s||s->phase||s->combat_panel_active||!s->input_lock||s->input_lock>2)return;
    if(candidate(t,m,s,t->proc))return;
    if((t->tick++%8)!=0)return; /* Bounded discovery, not a full RAM scan every frame. */
    const uint32_t start[]={0x03000000,0x02000000},end[]={0x03008000,0x02040000};
    for(unsigned bank=0;bank<2;++bank)for(uint32_t p=start[bank];p<=end[bank]-0x3C;p+=4)
        if(candidate(t,m,s,p))return;
}
bool fe8_voxel_target_click(Fe8TargetClick *c,const Fe8VoxelTargets *t,int x,int y){
    if(!c||!t||!t->active)return false;
    for(unsigned i=0;i<t->count;++i)if(t->targets[i].x==x&&t->targets[i].y==y){
        *c=(Fe8TargetClick){.proc=t->proc,.info=t->info,.node=t->targets[i].address,
            .x=x,.y=y,.unit=t->targets[i].unit,.active=true,.release=true};return true;
    }
    return false;
}
uint32_t fe8_voxel_target_keys(Fe8TargetClick *c,const Fe8MemoryReader *m,const Fe8Snapshot *s){
    if(!c||!c->active)return 0;
    Fe8VoxelTargets current={0};
    if(!m||!m->read8||!s||s->phase||s->combat_panel_active||!s->input_lock||s->input_lock>2||
            ++c->frames>180||!candidate(&current,m,s,c->proc)||current.info!=c->info){c->active=false;return 0;}
    bool found=false;
    for(unsigned i=0;i<current.count;++i)if(current.targets[i].address==c->node&&
            current.targets[i].x==c->x&&current.targets[i].y==c->y&&current.targets[i].unit==c->unit)found=true;
    if(!found){c->active=false;return 0;}
    if(c->release){c->release=false;return 0;}
    if(current.targets[0].address==c->node){c->active=false;return 1;} /* Native A. */
    c->release=true;return 1u<<6; /* Native Up advances next; NEVER B fast-cursor. */
}
