#include "voxel_buildings.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

Fe8BuildingKind fe8_building_kind(unsigned terrain) {
    switch (terrain) {
    case 0x03: return FE8_BUILDING_VILLAGE;
    case 0x04: return FE8_BUILDING_VILLAGE_CLOSED;
    case 0x05: return FE8_BUILDING_HOUSE;
    case 0x06: return FE8_BUILDING_ARMORY;
    case 0x07: return FE8_BUILDING_VENDOR;
    case 0x08: case 0x30: return FE8_BUILDING_ARENA;
    case 0x0A: return FE8_BUILDING_FORT;
    case 0x0B: return FE8_BUILDING_CASTLE;
    case 0x23: return FE8_BUILDING_GATE;
    case 0x24: return FE8_BUILDING_CHURCH;
    case 0x38: return FE8_BUILDING_INN;
    default: return FE8_BUILDING_NONE;
    }
}
const char *fe8_building_name(Fe8BuildingKind kind) {
    static const char *const names[] = {
        "None", "House", "Village", "Closed village", "Armory", "Vendor",
        "Arena", "Fort", "Castle", "Gate", "Church", "Inn", "Unclassified roof"
    };
    return kind >= 0 && kind < FE8_BUILDING_COUNT ? names[kind] : "Unknown";
}
bool fe8_building_support(unsigned terrain) {
    return terrain == 0x22 || terrain == 0x2C || terrain == 0x2E;
}
static bool visible(const Fe8Snapshot *s, int i) {
    return !(s->flags & FE8_SNAPSHOT_FOG) || s->fog[i] != 0;
}
static uint64_t art_signature(const Fe8HostPixel *pixels, int stride, int x, int y) {
    if (!pixels) return 0;
    uint64_t hash = UINT64_C(14695981039346656037);
    uint32_t colors[4] = {0}; unsigned count = 0, edges = 0;
    for (int dy = 0; dy < 16; ++dy) for (int dx = 0; dx < 16; ++dx) {
        uint32_t c = pixels[(size_t)(y * 16 + dy) * stride + x * 16 + dx];
        hash = (hash ^ c) * UINT64_C(1099511628211);
        if (dx && c != pixels[(size_t)(y * 16 + dy) * stride + x * 16 + dx - 1]) ++edges;
        if (count < 4) {
            unsigned i; for (i = 0; i < count && colors[i] != c; ++i) {}
            if (i == count) colors[count++] = c;
        }
    }
    /* Blank/fog/flat metatiles are not identifying evidence. */
    return count >= 4 && edges >= 12 ? (hash ? hash : 1) : 0;
}
static bool same_art(const Fe8HostPixel *p, int stride, int ax, int ay, int bx, int by) {
    for (int y = 0; y < 16; ++y)
        if (memcmp(p + (size_t)(ay * 16 + y) * stride + ax * 16,
                   p + (size_t)(by * 16 + y) * stride + bx * 16, 16 * sizeof(*p))) return false;
    return true;
}
static bool distinctive(Fe8BuildingKind kind) {
    return kind == FE8_BUILDING_ARMORY || kind == FE8_BUILDING_VENDOR ||
        kind == FE8_BUILDING_ARENA || kind == FE8_BUILDING_CHURCH || kind == FE8_BUILDING_INN;
}
bool fe8_buildings_classify(Fe8BuildingLayout *out, const Fe8Snapshot *s,
        const Fe8HostPixel *pixels, int stride) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!s || !(s->flags & FE8_SNAPSHOT_TERRAIN) || !s->map_width || !s->map_height ||
            s->map_width > FE8_MAX_MAP_WIDTH || s->map_height > FE8_MAX_MAP_HEIGHT ||
            (pixels && stride < s->map_width * 16)) return false;
    int w = s->map_width, h = s->map_height, n = w * h;
    out->width = (uint16_t)w; out->height = (uint16_t)h;
    uint64_t signatures[FE8_MAX_MAP_CELLS] = {0};
    for (int i = 0; i < n; ++i) {
        Fe8BuildingKind kind = fe8_building_kind(s->terrain[i]);
        if (visible(s,i) && (distinctive(kind) || kind == FE8_BUILDING_HOUSE || s->terrain[i] >= 0x41))
            signatures[i] = art_signature(pixels, stride, i % w, i / w);
    }
    /* Semantic shop/arena IDs always win. Only generic/custom-labelled tiles
     * may borrow a class, and only from unambiguous identical live artwork. */
    for (int i = 0; i < n; ++i) {
        if (!visible(s,i)) continue;
        Fe8BuildingKind kind = fe8_building_kind(s->terrain[i]);
        unsigned evidence = kind ? FE8_BUILDING_TERRAIN : 0;
        if (signatures[i] && (kind == FE8_BUILDING_HOUSE || s->terrain[i] >= 0x41)) {
            Fe8BuildingKind match = FE8_BUILDING_NONE; bool conflict = false;
            for (int j = 0; j < n; ++j) {
                Fe8BuildingKind other = fe8_building_kind(s->terrain[j]);
                if (!distinctive(other) || signatures[i] != signatures[j] ||
                        !same_art(pixels,stride,i%w,i/w,j%w,j/w)) continue;
                if (match && match != other) conflict = true;
                match = other;
            }
            if (match && !conflict) { kind = match; evidence |= FE8_BUILDING_ART_MATCH; }
        }
        if (!kind) continue;
        Fe8Building *b = &out->objects[out->count++];
        *b = (Fe8Building){kind,(uint16_t)(i%w),(uint16_t)(i/w),1,1,
            (uint16_t)(i%w),(uint16_t)(i/w),evidence};
        out->owner[i] = out->count;
    }
    uint16_t anchors = out->count;
    int16_t best[FE8_MAX_MAP_CELLS];
    for (int i = 0; i < n; ++i) best[i] = 32767;
    /* A shop beside a village must NOT adopt that village's roof. Expansion
     * requires its own roof directly north, then a bounded connected path.
     * A multi-source partition keeps touching roofs owned by separate doors. */
    for (unsigned k = 0; k < anchors; ++k) {
        Fe8Building *b = &out->objects[k]; int ax=b->anchor_x, ay=b->anchor_y;
        if (b->kind == FE8_BUILDING_FORT || !ay ||
                !fe8_building_support(s->terrain[(ay-1)*w+ax]) || !visible(s,(ay-1)*w+ax)) continue;
        int radius = b->kind == FE8_BUILDING_CASTLE ? 4 :
            (b->kind == FE8_BUILDING_ARENA || b->kind == FE8_BUILDING_VILLAGE ||
             b->kind == FE8_BUILDING_VILLAGE_CLOSED) ? 3 : 2;
        int reach = b->kind == FE8_BUILDING_CASTLE ? 6 : 4;
        uint8_t visited[9*7] = {0}; int queue[9*7], head=0, tail=0;
        queue[tail++] = ay*w+ax; visited[radius] = 1;
        while (head < tail) {
            int i=queue[head++], x=i%w, y=i/w;
            const int dx[4]={0,-1,1,0},dy[4]={-1,0,0,1};
            for (int d=0;d<4;++d) {
                int xx=x+dx[d], yy=y+dy[d], rx=xx-ax+radius, ry=ay-yy;
                if (xx<0||xx>=w||yy<0||yy>=h||rx<0||rx>radius*2||ry<0||ry>reach) continue;
                int local=ry*(radius*2+1)+rx, j=yy*w+xx;
                if (visited[local] || !visible(s,j) || !fe8_building_support(s->terrain[j])) continue;
                /* Known castle footprint cells do not become cottage roofs. */
                if (s->terrain[j]==0x2C && b->kind!=FE8_BUILDING_CASTLE && b->kind!=FE8_BUILDING_GATE) continue;
                visited[local]=1; queue[tail++]=j;
                int score=abs(xx-ax)*3+(ay-yy)*2;
                if (score<best[j]) {best[j]=(int16_t)score;out->owner[j]=(uint16_t)(k+1);}
                else if (score==best[j] && out->owner[j]!=k+1) out->owner[j]=0;
            }
        }
    }
    /* Models cover only verified solid rectangles containing their own door.
     * Never fill bounding-box holes, consume a neighboring anchor, or erase
     * plains/roads/fog just because an L-shaped roof surrounds them. */
    for (unsigned k=0;k<anchors;++k) {
        Fe8Building *b=&out->objects[k]; int ax=b->anchor_x,ay=b->anchor_y;
        int left=0,right=w-1,area=1;
        for (int y=ay;y>=0;--y) {
            if (out->owner[y*w+ax]!=k+1) break;
            int l=ax,r=ax;
            while(l>0&&out->owner[y*w+l-1]==k+1)--l;
            while(r+1<w&&out->owner[y*w+r+1]==k+1)++r;
            if(l>left)left=l;
            if(r<right)right=r;
            int a=(right-left+1)*(ay-y+1);
            if(a>area){area=a;b->x=(uint16_t)left;b->y=(uint16_t)y;
                b->width=(uint16_t)(right-left+1);b->height=(uint16_t)(ay-y+1);}
        }
        if(area>1)b->evidence|=FE8_BUILDING_FOOTPRINT;
    }
    for(int i=0;i<n;++i)if(out->owner[i]){
        const Fe8Building *b=&out->objects[out->owner[i]-1];int x=i%w,y=i/w;
        if(x<b->x||y<b->y||x>=b->x+b->width||y>=b->y+b->height)out->owner[i]=0;
    }
    /* Unclassified roof fragments are low roof slabs, not invented shops or
     * castles. One tile per fragment prevents speculative rectangular merges. */
    for(int i=0;i<n;++i)if(!out->owner[i]&&visible(s,i)&&fe8_building_support(s->terrain[i])){
        Fe8Building *b=&out->objects[out->count++];
        *b=(Fe8Building){FE8_BUILDING_ROOF,(uint16_t)(i%w),(uint16_t)(i/w),1,1,
            (uint16_t)(i%w),(uint16_t)(i/w),0};out->owner[i]=out->count;
    }
    return true;
}

/* All recipes are deterministic and emitted once into the cached backdrop.
 * Colors are RGBA in the frontend's little-endian packed-pixel convention. */
static float clampf(float x,float lo,float hi){return x<lo?lo:x>hi?hi:x;}
static uint32_t tint(uint32_t c,float r,float g,float b){
    unsigned rr=(unsigned)clampf((c&255)*r,0,255),gg=(unsigned)clampf(((c>>8)&255)*g,0,255);
    unsigned bb=(unsigned)clampf(((c>>16)&255)*b,0,255);
    return 0xFF000000u|rr|gg<<8|bb<<16;
}
static uint32_t mix(uint32_t a,uint32_t b,float f){
    unsigned r=(unsigned)((a&255)*(1-f)+(b&255)*f),g=(unsigned)(((a>>8)&255)*(1-f)+((b>>8)&255)*f);
    unsigned bl=(unsigned)(((a>>16)&255)*(1-f)+((b>>16)&255)*f);
    return 0xFF000000u|r|g<<8|bl<<16;
}
static unsigned noise(unsigned x,unsigned y,unsigned z){
    unsigned n=x*73856093u^y*19349663u^z*83492791u;n^=n>>13;n*=1274126177u;return n^(n>>16);
}
static void box(const Fe8BuildingPainter *p,float x,float y,float z,float w,float h,float d,uint32_t c){
    if(w>0&&h>0&&d>0)p->box(p->context,x,y,z,w,h,d,c);
}

static void house_object(const Fe8BuildingPainter *v,float x,float z,float w,float d,
        uint32_t terracotta,uint32_t wall,unsigned seed) {
    uint32_t timber=tint(wall,.44f,.39f,.35f),stone=mix(wall,0xFFBCBCB5,.4f);
    float h=10;
    box(v,x-1,0,z-1,w+2,1.3f,d+2,stone);
    box(v,x,1,z,w,h,d,wall);
    for(int row=0;row<3;++row)for(int col=0;col<(int)(w/2);++col){
        float xx=x+col*2+(row&1?1:0);if(xx+1.8f>x+w)continue;
        float f=.87f+(noise(col,row,seed)%20)*.01f;
        box(v,xx,1+row*1.1f,z+d-.05f,1.8f,.95f,.4f,tint(stone,f,f,f));
    }
    for(int k=0;k<3;++k){float xx=x+k*w*.5f;
        box(v,xx-.35f,3,z+d,.7f,8,.55f,timber);
        box(v,xx-.35f,3,z-.45f,.7f,8,.55f,timber);
    }
    box(v,x,5,z+d,w,.6f,.55f,timber);box(v,x,10.5f,z+d,w,.65f,.65f,timber);
    box(v,x-.35f,4,z,.6f,7,d,timber);
    /* Symmetric stepped gable; each roof tile is a generated voxel slab. */
    int rows=(int)ceilf((w+3)*.5f);
    for(int row=0;row<rows;++row){
        float rw=w+3-row*2,y=11+row*.8f;
        box(v,x-1.5f+row,y,z-1.3f,rw,.85f,d+2.6f,terracotta);
        for(int iz=0;iz<(int)((d+2.6f)/1.7f);++iz)for(int side=0;side<2;++side){
            float xx=side?x+w+.5f-row:x-1.5f+row;
            float f=.87f+(noise(row,iz,seed)%28)*.009f;
            box(v,xx,y+.65f,z-1.3f+iz*1.7f,1,.3f,1.55f,tint(terracotta,f,f,f));
        }
    }
    float door=x+w*.48f;
    box(v,door-1.2f,1,z+d+.3f,2.6f,5,.45f,timber);
    box(v,door-.95f,1,z+d+.58f,2,4.7f,.3f,tint(timber,1.2f,1.12f,1.05f));
    box(v,door+.5f,3,z+d+.95f,.3f,.3f,.2f,0xFF71C3E2);
    for(int side=0;side<2;++side){float xx=x+w*(side?.80f:.18f);
        box(v,xx-1.2f,6.2f,z+d+.3f,2.4f,2.8f,.4f,timber);
        box(v,xx-.9f,6.5f,z+d+.55f,1.8f,2.2f,.35f,0xFF5BB9E7);
        box(v,xx-.12f,6.45f,z+d+.92f,.24f,2.3f,.15f,timber);
        box(v,xx-1,7.4f,z+d+.92f,2,.2f,.15f,timber);
    }
    box(v,x+w*.73f,11+rows*.32f,z+d*.26f,2.2f,rows*.6f+2,2.1f,stone);
    box(v,x+w*.73f-.3f,13+rows*.92f,z+d*.26f-.3f,2.8f,.7f,2.7f,tint(stone,.8f,.8f,.8f));
}
static void tower_object(const Fe8BuildingPainter *v,float x,float z,float w,float h,uint32_t stone) {
    box(v,x,0,z,w,h,w,stone);
    box(v,x-.5f,h-2,z-.5f,w+1,1,w+1,tint(stone,1.12f,1.10f,1.04f));
    for(int k=0;k<(int)w;k+=3){
        box(v,x+k,h,z,1.7f,2,1.7f,stone);box(v,x+k,h,z+w-1.7f,1.7f,2,1.7f,stone);
        box(v,x,h,z+k,1.7f,2,1.7f,stone);box(v,x+w-1.7f,h,z+k,1.7f,2,1.7f,stone);
    }
    box(v,x+w*.5f-.65f,h*.55f,z+w+.04f,1.3f,3,.25f,tint(stone,.32f,.37f,.42f));
    for(int j=2;j<h-3;j+=3)box(v,x,j,z+w+.02f,w,.15f,.06f,tint(stone,.78f,.80f,.82f));
}
static void door(const Fe8BuildingPainter *p,float x,float z,float width,float height,uint32_t stone) {
    uint32_t dark=0xFF302D29;
    box(p,x-width*.5f,0.8f,z,width,height,.32f,dark);
    box(p,x-width*.5f-.5f,0.5f,z-.08f,.55f,height+1,.7f,stone);
    box(p,x+width*.5f,0.5f,z-.08f,.55f,height+1,.7f,stone);
    box(p,x-width*.5f-.5f,height+.8f,z-.08f,width+1,.6f,.7f,stone);
}
static void banner(const Fe8BuildingPainter *p,float x,float z,float base,float height,uint32_t cloth) {
    box(p,x,base,z,.35f,height,.35f,0xFF727D83);
    box(p,x+.35f,base+height-3.8f,z,3,3.5f,.20f,cloth);
    box(p,x+.35f,base+height-4.5f,z,2,1,.20f,cloth);
    box(p,x-.18f,base+height,z-.18f,.7f,.7f,.7f,0xFF8ED3E7);
}
static void armory(const Fe8BuildingPainter *p,float x,float z,float w,float d) {
    uint32_t stone=mix(p->wall,0xFFB8B8AC,.40f),iron=0xFF454B50;
    uint32_t roof=mix(p->wall,0xFF77887E,.65f),timber=0xFF415365;
    float h=8,front=z+d;
    box(p,x-.5f,0,z-.5f,w+1,1,d+1,stone);
    box(p,x,1,z,w,h,d,stone);
    for(int y=2;y<8;y+=2)box(p,x,y,front,w,.14f,.10f,tint(stone,.73f,.77f,.8f));
    /* A barrel-vaulted workshop, with visible iron roof ribs, not a cottage. */
    for(int i=0;i<(int)ceilf(w+2);++i){
        float dx=i+.5f-(w+2)*.5f,r=dx/((w+2)*.5f);
        float top=h+1+4.8f*sqrtf(fmaxf(0,1-r*r));
        float sw=fminf(1,w+2-i);
        box(p,x-1+i,h,z-.6f,sw,top-h,d+1.2f,roof);
        for(float rib=0;rib<=d+.5f;rib+=d*.32f)
            box(p,x-1+i,top,z-.7f+rib,sw,.45f,.65f,iron);
    }
    for(int side=0;side<2;++side)
        box(p,x+(side?w-.65f:0),1,front+.04f,.65f,8,.5f,iron);
    door(p,x+w*.52f,front+.15f,3,5,iron);
    /* Hammer/sword sign in a dark iron plaque. */
    float sign=x+w*.52f;
    box(p,sign-2,7.2f,front+.35f,4,2.8f,.45f,iron);
    box(p,sign-.25f,7.45f,front+.83f,.5f,2.15f,.12f,0xFFCDD9DF);
    box(p,sign-.9f,7.9f,front+.85f,1.8f,.32f,.13f,0xFF80C8DE);
    /* Furnace stack and a small outdoor spear rack. */
    box(p,x+w-2.3f,7,z+.6f,2.2f,9,2.2f,stone);
    box(p,x+w-2.65f,15.4f,z+.25f,2.9f,.65f,2.9f,iron);
    box(p,x+w-2.15f,16.05f,z+.75f,1.9f,.12f,1.9f,0xFF282626);
    box(p,x+.7f,1,front+.5f,2.7f,.6f,1.2f,timber);
    box(p,x+.7f,3,front+1.2f,2.7f,.5f,.4f,timber);
    for(int i=0;i<3;++i){
        float xx=x+1.1f+i*.8f;
        box(p,xx,1.5f,front+.65f,.2f,4.7f,.2f,timber);
        box(p,xx-.2f,5.8f,front+.50f,.6f,1.2f,.4f,0xFFB5C8D0);
    }
}
static void vendor(const Fe8BuildingPainter *p,float x,float z,float w,float d) {
    uint32_t timber=tint(p->wall,.40f,.37f,.32f),cloth=0xFF9B8C48;
    house_object(p,x,z,w,d,mix(p->roof,0xFF87964F,.65f),p->wall,17);
    /* Striped open-front canopy and stocked counter make the silhouette a shop. */
    float aw=w*.9f,start=x+w*.05f,front=z+d;
    for(float a=0;a<aw;a+=1.2f)for(int step=0;step<4;++step)
        box(p,start+a,7.7f-step*.35f,front+step*.8f,fminf(1.2f,aw-a),.48f,.9f,
            ((int)(a/1.2f)&1)?0xFFCCE0E7:cloth);
    for(int side=0;side<2;++side)box(p,start+side*(aw-.35f),.6f,front+2.7f,.4f,6.5f,.4f,timber);
    box(p,start+.5f,0.4f,front+1.2f,aw-1,2.5f,1.5f,timber);
    box(p,start+.3f,2.9f,front+1,aw-.6f,.45f,1.9f,mix(p->wall,0xFF689AAC,.3f));
    for(int n=0;n<4;++n){
        float xx=start+1+n*(aw-2)/4;
        uint32_t c=n&1?0xFFA191CB:0xFFABC369;
        box(p,xx,3.3f,front+1.2f,.8f,.9f,.8f,c);
        box(p,xx+.2f,4.2f,front+1.4f,.4f,.4f,.4f,0xFFC5DCE1);
    }
    box(p,x+w-2.5f,.1f,z+d*.55f,2.8f,2.6f,2.8f,timber);
    box(p,x+w-2.3f,1.2f,z+d*.55f-.08f,2.4f,.35f,.3f,0xFF6A9EB2);
}
static float octagon(float x,float z){
    x=fabsf(x);z=fabsf(z);return fmaxf(x,z)+.41421356f*fminf(x,z);
}
static void arena(const Fe8BuildingPainter *p,float x,float z,float w,float d) {
    uint32_t stone=mix(p->wall,0xFFB7C8CF,.50f),dark=tint(stone,.58f,.58f,.57f);
    uint32_t sand=0xFF83B1C8,cloth=mix(p->roof,0xFF5268A7,.65f);
    float cx=x+w*.5f,cz=z+d*.5f,rx=w*.46f,rz=d*.46f;
    float step=fmaxf(1,fminf(w,d)/22);
    /* Open octagonal amphitheatre: floor and tiered seating stay visible. */
    for(float zz=-rz;zz<rz;zz+=step)for(float xx=-rx;xx<rx;xx+=step){
        float q=octagon((xx+step*.5f)/rx,(zz+step*.5f)/rz);
        if(q>1)continue;
        float h=.6f;uint32_t c=sand;
        if(q>.47f){int tier=(int)((q-.47f)/.13f);h=2.3f+tier*2.2f;
            c=tier&1?stone:tint(stone,.87f,.88f,.87f);}
        /* Lower the near ring for a legible sand pit; retain tall back tiers. */
        if(zz>0&&h>5.7f)h=5.7f;
        if(zz>rz*.5f&&fabsf(xx)<fmaxf(1.2f,w*.09f))h=.7f;
        box(p,cx+xx,0,cz+zz,step+.03f,h,step+.03f,c);
        if(q>.86f){
            bool opening=((int)floorf((xx+rx)/step)%4==1)&&zz>rz*.25f;
            if(opening)box(p,cx+xx,1.1f,cz+zz+step*.6f,step*.82f,2.2f,step*.5f,dark);
            box(p,cx+xx,h-.4f,cz+zz,step+.08f,.7f,step+.08f,tint(stone,1.1f,1.07f,1.01f));
        }
    }
    float entry=fmaxf(2.4f,w*.18f);
    door(p,cx,cz+rz*.89f,entry,3.8f,stone);
    /* Clear the dark doorway's lower center with a sand entry ramp. */
    box(p,cx-entry*.5f,.5f,cz+rz*.65f,entry,.18f,rz*.35f,sand);
    for(int side=0;side<2;++side){
        float xx=cx+(side?1:-1)*rx*.72f;
        box(p,xx-1.1f,0,cz-1,2.2f,9,2.2f,stone);
        banner(p,xx,cz,9,6,cloth);
    }
}
static void church(const Fe8BuildingPainter *p,float x,float z,float w,float d) {
    uint32_t stone=mix(p->wall,0xFFD3D9D4,.52f),slate=0xFF8D7869;
    house_object(p,x,z,w,d,slate,stone,71);
    float tw=fminf(5,w*.4f),tx=x+w*.5f-tw*.5f,tz=z+d-tw*.45f;
    box(p,tx,1,tz,tw,27,tw,stone);
    for(int side=0;side<2;++side)box(p,tx+side*(tw-.6f),20,tz+tw,.6f,5,.4f,tint(stone,.78f,.82f,.86f));
    box(p,tx+tw*.3f,22,tz+tw+.05f,tw*.4f,3.5f,.2f,0xFF493E34);
    box(p,tx+tw*.42f,22.5f,tz+tw+.3f,tw*.16f,1.8f,.25f,0xFF79B4C9);
    for(float r=0;r<tw*.65f;r+=.6f)box(p,tx-.5f+r,28+r*1.5f,tz-.5f+r,
        tw+1-2*r,.95f,tw+1-2*r,slate);
    float peak=28+tw*.9f;
    box(p,tx+tw*.5f-.18f,peak,tz+tw*.5f-.18f,.36f,3,.36f,0xFF8FD2E5);
    box(p,tx+tw*.5f-1,peak+1.5f,tz+tw*.5f-.18f,2,.35f,.36f,0xFF8FD2E5);
    /* Small stained-glass windows on the side walls. */
    for(int n=0;n<2;++n)box(p,x+w+.05f,5,z+d*(.22f+n*.38f),.25f,3,1.7f,0xFFB898B4);
    door(p,tx+tw*.5f,tz+tw+.15f,tw*.42f,6,tint(stone,.8f,.85f,.9f));
}
static void fort(const Fe8BuildingPainter *p,float x,float z,float w,float d) {
    uint32_t stone=mix(p->wall,0xFFADB5B3,.55f);
    box(p,x,0,z,w,1,d,stone);
    box(p,x+2,1,z+2,w-4,.5f,d-4,tint(stone,.55f,.60f,.60f));
    for(int side=0;side<2;++side){
        box(p,x+side*(w-1.4f),1,z,1.4f,4,d,stone);
        box(p,x,1,z+side*(d-1.4f),w,4,1.4f,stone);
    }
    for(float k=0;k<w;k+=3){box(p,x+k,5,z,1.5f,1.2f,1.5f,stone);box(p,x+k,5,z+d-1.5f,1.5f,1.2f,1.5f,stone);}
    door(p,x+w*.5f,z+d+.02f,3,3.5f,stone);
}
static void gate(const Fe8BuildingPainter *p,float x,float z,float w,float d,bool castle) {
    uint32_t stone=mix(p->wall,0xFFC0BDAE,.5f);
    float tw=fminf(9,w*.26f),gap=w-2*tw;
    if(castle&&d>20){
        box(p,x+tw*.4f,0,z+tw*.4f,w-tw*.8f,8,d-tw*.8f,stone);
        house_object(p,x+w*.28f,z+d*.18f,w*.44f,d*.43f,p->roof,p->wall,19);
        for(int side=0;side<2;++side)tower_object(p,x+side*(w-tw),z,tw,20,stone);
    }
    float front=z+d-tw;
    for(int side=0;side<2;++side)tower_object(p,x+side*(w-tw),front,tw,castle?23:13,stone);
    float gh=castle?11:8;
    box(p,x+tw,gh,front+.4f,gap,4,tw*.7f,stone);
    /* Transparent gap below the lintel, with an iron portcullis. */
    for(float k=.5f;k<gap;k+=1.2f)box(p,x+tw+k,1,front+tw*.66f,.26f,gh,.26f,0xFF434749);
    box(p,x+tw,gh*.40f,front+tw*.66f,gap,.3f,.3f,0xFF434749);
    box(p,x+tw,gh*.74f,front+tw*.66f,gap,.3f,.3f,0xFF434749);
    if(castle)banner(p,x+tw*.5f,front+tw*.5f,23,7,0xFF566AAA);
}
static void village_fence(const Fe8BuildingPainter *p,const Fe8Building *b,float x,float z,float w,float d,bool closed){
    uint32_t timber=0xFF526B80;float entrance=b->anchor_x*16+8;
    for(float k=0;k<w;k+=3){
        box(p,x+k,.05f,z+1,.65f,3,.7f,timber);
        if(closed||fabsf(x+k-entrance)>2.5f)box(p,x+k,.05f,z+d-1,.65f,3,.7f,timber);
    }
    for(float k=0;k<d;k+=3)for(int side=0;side<2;++side)box(p,x+side*(w-.7f),.05f,z+k,.7f,3,.65f,timber);
    box(p,x,1.9f,z+1,w,.45f,.5f,timber);
    box(p,x,1.9f,z+d-1,fmaxf(0,entrance-x-2.5f),.45f,.5f,timber);
    box(p,entrance+2.5f,1.9f,z+d-1,fmaxf(0,x+w-entrance-2.5f),.45f,.5f,timber);
    if(closed){
        box(p,entrance-2.4f,.1f,z+d-1,4.8f,4,.55f,tint(timber,.85f,.85f,.85f));
        box(p,entrance-2.4f,2.7f,z+d-.4f,4.8f,.5f,.3f,0xFF698AA8);
    }
}
void fe8_building_draw(const Fe8Building *b,const Fe8BuildingPainter *p) {
    if(!b||!p||!p->box||!b->width||!b->height||b->kind<=FE8_BUILDING_NONE||b->kind>=FE8_BUILDING_COUNT ||
            b->x+b->width>FE8_MAX_MAP_WIDTH||b->y+b->height>FE8_MAX_MAP_HEIGHT)return;
    float x=b->x*16.f,z=b->y*16.f,w=b->width*16.f,d=b->height*16.f;
    float bw=fminf(30,w*.73f),bd=fminf(26,d*.67f);
    float bx=x+(w-bw)*.5f,bz=z+(d-bd)*.3f;
    switch(b->kind){
    case FE8_BUILDING_ARMORY:armory(p,bx,bz,bw,bd);break;
    case FE8_BUILDING_VENDOR:vendor(p,bx,bz,bw,bd);break;
    case FE8_BUILDING_ARENA:arena(p,x+.6f,z+.6f,w-1.2f,d-1.2f);break;
    case FE8_BUILDING_CHURCH:church(p,bx,bz,bw,bd);break;
    case FE8_BUILDING_FORT:fort(p,x+1,z+1,w-2,d-2);break;
    case FE8_BUILDING_CASTLE:gate(p,x+1,z+1,w-2,d-2,true);break;
    case FE8_BUILDING_GATE:gate(p,x+1,z+1,w-2,d-2,false);break;
    case FE8_BUILDING_ROOF:
        box(p,x+1,0,z+1,w-2,5,d-2,mix(p->wall,0xFFAEAEA7,.4f));
        box(p,x+.5f,5,z+.5f,w-1,1.5f,d-1,p->roof);break;
    default:
        house_object(p,bx,bz,bw,bd,p->roof,p->wall,(unsigned)(b->x+b->y*17));
        if(b->kind==FE8_BUILDING_VILLAGE||b->kind==FE8_BUILDING_VILLAGE_CLOSED)
            village_fence(p,b,x,z,w,d,b->kind==FE8_BUILDING_VILLAGE_CLOSED);
        if(b->kind==FE8_BUILDING_INN){
            box(p,bx+bw-1,7,bz+bd+.5f,.35f,3,.35f,0xFF526B80);
            box(p,bx+bw-1,8.8f,bz+bd+.5f,3,.3f,.35f,0xFF526B80);
            box(p,bx+bw,6.5f,bz+bd+.5f,2,2.2f,.3f,0xFF709FBC);
        }
        break;
    }
}
