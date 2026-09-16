/* Runtime voxel terrain and camera-facing sprite billboards for FE8.
 * No model files, network, ROM mutation, or replacement game state.
 * Inferred terrain depth is NOT original game geometry. */
#include "voxel_renderer.h"
#include "extended_unit_renderer.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Bound the expensive software scene, not the display. Retina/5K/6K output
 * still receives full-size RGBA pixels and a full-resolution native HUD. */
enum { MAX_OUTPUT_PIXELS = 8192 * 4320, SCENE_WIDTH = 1920, SCENE_HEIGHT = 1080,
       SPRITE_CACHE = 128, MAX_SPRITE_PIXELS = 32 * 32 };
typedef struct Point { float x, y, z; } Point;
typedef struct SpriteImage {
    uint64_t hash, age;
    uint32_t colors[MAX_SPRITE_PIXELS];
    int width, height, bottom;
} SpriteImage;
struct Fe8VoxelRenderer {
    Fe8HostPixel *pixels, *backdrop, *terrain, *ground, *output;
    size_t output_capacity;
    int output_width, output_height;
    const char *error;
    float *depth, *backdepth;
    uint16_t *unit_hits, drawing_unit;
    uint32_t *ground_hits;
    bool drawing_background;
    uint8_t *heights, *shadow;
    size_t capacity, terrain_capacity;
    int width, height, mw, mh;
    float yaw, pitch, zoom, pan_x, pan_z, target_x, target_z, scale;
    float cy, sy, cp, sp;
    uint64_t terrain_hash, object_hash, scene_hash, ticks;
    bool dirty, ready;
    SpriteImage *sprites;
    Fe8VoxelStats stats;
};
static float clampf(float a, float lo, float hi) { return a < lo ? lo : a > hi ? hi : a; }
static uint32_t tint(uint32_t c, float r, float g, float b) {
    unsigned rr=(unsigned)clampf((c&255)*r,0,255),gg=(unsigned)clampf(((c>>8)&255)*g,0,255);
    unsigned bb=(unsigned)clampf(((c>>16)&255)*b,0,255);
    return 0xFF000000u | rr | gg<<8 | bb<<16;
}
static uint32_t mix(uint32_t a, uint32_t b, float f) {
    unsigned r=(unsigned)((a&255)*(1-f)+(b&255)*f);
    unsigned g=(unsigned)(((a>>8)&255)*(1-f)+((b>>8)&255)*f);
    unsigned bl=(unsigned)(((a>>16)&255)*(1-f)+((b>>16)&255)*f);
    return 0xFF000000u | r | g<<8 | bl<<16;
}
static uint16_t rd16(const Fe8MemoryView *m, uint32_t a) {
    return m->read8(m->context,a)|(uint16_t)m->read8(m->context,a+1)<<8;
}
static uint32_t rd32(const Fe8MemoryView *m, uint32_t a) {
    return rd16(m,a)|(uint32_t)rd16(m,a+2)<<16;
}
static uint64_t bytehash(uint64_t h, unsigned b) { return (h^b)*UINT64_C(1099511628211); }
static uint64_t byteshash(uint64_t h, const void *p, size_t n) {
    const unsigned char *s=p; for(size_t i=0;i<n;++i) h=bytehash(h,s[i]); return h;
}
static Point project(const Fe8VoxelRenderer *v, float x, float y, float z) {
    x-=v->target_x; z-=v->target_z;
    float across=v->cy*x-v->sy*z, along=v->sy*x+v->cy*z;
    return (Point){v->width*.5f+across*v->scale,
        v->height*.53f+(along*v->sp-y*v->cp)*v->scale, along*v->cp+y*v->sp};
}
static void unproject(const Fe8VoxelRenderer *v,float x,float y,float *wx,float *wz) {
    float across=(x-v->width*.5f)/v->scale;
    float along=(y-v->height*.53f)/(v->scale*v->sp);
    *wx=v->target_x+v->cy*across+v->sy*along;
    *wz=v->target_z-v->sy*across+v->cy*along;
}
/* Incremental barycentric rasterization with a genuine per-pixel depth buffer.
 * Terrain neighbour faces and cube back faces are culled at emission. */
static void triangle(Fe8VoxelRenderer *v,Point a,Point b,Point c,uint32_t color) {
    float det=(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
    if(fabsf(det)<.0001f)return;
    int x0=(int)floorf(fminf(a.x,fminf(b.x,c.x))),x1=(int)ceilf(fmaxf(a.x,fmaxf(b.x,c.x)));
    int y0=(int)floorf(fminf(a.y,fminf(b.y,c.y))),y1=(int)ceilf(fmaxf(a.y,fmaxf(b.y,c.y)));
    if(x1<0||y1<0||x0>=v->width||y0>=v->height)return;
    if(x0<0)x0=0;
    if(y0<0)y0=0;
    if(x1>=v->width)x1=v->width-1;
    if(y1>=v->height)y1=v->height-1;
    float ux=(c.y-a.y)/det,uy=-(c.x-a.x)/det;
    float vx=-(b.y-a.y)/det,vy=(b.x-a.x)/det;
    float ur=((x0+.5f-a.x)*(c.y-a.y)-(y0+.5f-a.y)*(c.x-a.x))/det;
    float vr=((b.x-a.x)*(y0+.5f-a.y)-(b.y-a.y)*(x0+.5f-a.x))/det;
    for(int y=y0;y<=y1;++y,ur+=uy,vr+=vy){
        float u=ur,w=vr;
        for(int x=x0;x<=x1;++x,u+=ux,w+=vx){
            if(u<-.00001f||w<-.00001f||u+w>1.00001f)continue;
            float d=a.z+u*(b.z-a.z)+w*(c.z-a.z);
            size_t i=(size_t)y*v->width+x;
            if(d>=v->depth[i]){
                v->depth[i]=d;v->pixels[i]=color;v->unit_hits[i]=v->drawing_unit;
                if(v->drawing_background)v->ground_hits[i]=0;
            }
        }
    }
}
static void quad(Fe8VoxelRenderer *v,Point a,Point b,Point c,Point d,uint32_t color) {
    triangle(v,a,b,c,color); triangle(v,a,c,d,color);
}
static void box(Fe8VoxelRenderer *v,float x,float y,float z,float w,float h,float d,uint32_t c) {
    Point p[8];
    for(int i=0;i<8;++i)p[i]=project(v,x+(i&1?w:0),y+(i&2?h:0),z+(i&4?d:0));
    quad(v,p[2],p[3],p[7],p[6],tint(c,1.10f,1.07f,.98f));
    if(v->sy>=0)quad(v,p[1],p[5],p[7],p[3],tint(c,.71f,.75f,.79f));
    else quad(v,p[0],p[2],p[6],p[4],tint(c,.95f,.92f,.83f));
    if(v->cy>=0)quad(v,p[4],p[6],p[7],p[5],tint(c,.86f,.87f,.87f));
    else quad(v,p[0],p[1],p[3],p[2],tint(c,1.02f,.96f,.84f));
}
static bool green(uint32_t c) {
    int r=c&255,g=(c>>8)&255,b=(c>>16)&255;
    return g>r*1.08f && g>b*1.12f;
}
static bool roof(unsigned t) {return t==0x22||t==0x2C||t==0x2E;}
static bool structure(unsigned t) {
    return (t>=3&&t<=8)||t==0x0B||t==0x19||t==0x1A||t==0x1B||t==0x22||t==0x23||t==0x24;
}
static bool canopy(unsigned t) { return t==0x0C||t==0x0D; }
static bool watery(unsigned t){ return t==0x10||t==0x15||t==0x16||t==0x3C; }
static bool resize(Fe8VoxelRenderer *v,int w,int h,int mw,int mh) {
    if(w<1||h<1||w>16384||h>16384||(size_t)w*h>MAX_OUTPUT_PIXELS||
            mw<1||mh<1||mw>1024||mh>1024) {
        v->error = "unsupported viewport or map dimensions";
        return false;
    }
    const int output_w = w, output_h = h;
    double scale = fmin(1.0, fmin((double)SCENE_WIDTH / w, (double)SCENE_HEIGHT / h));
    w = (int)(w * scale); h = (int)(h * scale);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    v->error = "unable to allocate voxel scene";
    size_t n=(size_t)w*h,tn=(size_t)mw*mh;
    if(n>v->capacity){
        uint32_t *p=malloc(n*4),*b=malloc(n*4);float *d=malloc(n*sizeof(float)),*bd=malloc(n*sizeof(float));
        uint16_t *hits=calloc(n,sizeof(*hits));
        uint32_t *ground_hits=calloc(n,sizeof(*ground_hits));
        if(!p||!b||!d||!bd||!hits||!ground_hits){free(p);free(b);free(d);free(bd);free(hits);free(ground_hits);return false;}
        free(v->pixels);free(v->backdrop);free(v->depth);free(v->backdepth);free(v->unit_hits);free(v->ground_hits);
        v->pixels=p;v->backdrop=b;v->depth=d;v->backdepth=bd;v->unit_hits=hits;v->ground_hits=ground_hits;v->capacity=n;
    }
    if(tn>v->terrain_capacity){
        uint32_t *t=malloc(tn*4),*g=malloc(tn*4);uint8_t *he=malloc(tn),*sh=malloc(tn);
        if(!t||!g||!he||!sh){free(t);free(g);free(he);free(sh);return false;}
        free(v->terrain);free(v->ground);free(v->heights);free(v->shadow);
        v->terrain=t;v->ground=g;v->heights=he;v->shadow=sh;v->terrain_capacity=tn;
    }
    if(v->width!=w||v->height!=h)v->dirty=true;
    if(v->mw!=mw||v->mh!=mh){v->terrain_hash=0;v->pan_x=v->pan_z=0;v->dirty=true;}
    v->width=w;v->height=h;v->mw=mw;v->mh=mh;
    v->output_width=output_w;v->output_height=output_h;
    v->stats.render_width=w;v->stats.render_height=h;
    v->error=NULL;
    return true;
}
static uint64_t map_hash(const Fe8MemoryView *m,const Fe8MapRenderState *map,const Fe8Snapshot *s) {
    uint64_t h=UINT64_C(14695981039346656037);
    h=byteshash(h,&s->chapter,sizeof(s->chapter));
    h=byteshash(h,s->terrain,(size_t)s->map_width*s->map_height);
    if(s->flags&FE8_SNAPSHOT_FOG)h=byteshash(h,s->fog,(size_t)s->map_width*s->map_height);
    for(unsigned y=0;y<map->map_height;++y){
        uint32_t row=rd32(m,map->base_tile_rows+y*4);
        for(unsigned x=0;x<map->map_width;++x){
            unsigned metatile=rd16(m,row+x*2);h=bytehash(h,metatile);
            for(unsigned q=0;q<4;++q)h=bytehash(h,rd16(m,map->tileset_config+(metatile+q)*2));
        }
    }
    for(unsigned a=0;a<0x8000;++a)h=bytehash(h,m->read8(m->context,map->tile_graphics+a));
    for(unsigned a=0;a<512;++a)h=bytehash(h,m->read8(m->context,map->palette+a));
    h=bytehash(h,map->normal_palette_bank_offset);h=bytehash(h,map->fog_palette_bank_offset);
    if(map->palette_mapping){
        h=byteshash(h,map->palette_mapping->bank,sizeof(map->palette_mapping->bank));
        h=byteshash(h,map->palette_mapping->valid_mask,sizeof(map->palette_mapping->valid_mask));
    }
    return h?h:1;
}
/* Animated water/plains must not rasterize every tree and castle again.
 * Hash all object material footprints, including entire inferred rectangles,
 * so any geometry/material/terrain/fog change still invalidates scenery. */
static uint64_t object_hash(const Fe8VoxelRenderer *v,const Fe8Snapshot *s) {
    uint8_t covered[FE8_MAX_MAP_CELLS]={0};
    size_t cells=(size_t)s->map_width*s->map_height;
    uint64_t hash=byteshash(UINT64_C(14695981039346656037),s->terrain,cells);
    hash=byteshash(hash,&s->chapter,sizeof(s->chapter));
    hash=byteshash(hash,&v->mw,sizeof(v->mw));
    hash=byteshash(hash,&v->mh,sizeof(v->mh));
    if(s->flags&FE8_SNAPSHOT_FOG)hash=byteshash(hash,s->fog,cells);
    for(int z=0;z<s->map_height;++z)for(int x=0;x<s->map_width;++x){
        if(covered[z*s->map_width+x])continue;
        unsigned t=s->terrain[z*s->map_width+x];
        if(roof(t)){
            int x1=x,z1=z;
            while(x1+1<s->map_width&&s->terrain[z*s->map_width+x1+1]==t)++x1;
            while(z1+1<s->map_height&&s->terrain[(z1+1)*s->map_width+x]==t)++z1;
            for(int j=z;j<=z1;++j)for(int i=x;i<=x1;++i)covered[j*s->map_width+i]=1;
        }else if(structure(t)||canopy(t)||t==0x0A||t==0x1D||t==0x20||t==0x21||
                t==0x39||t==0x33||t==0x13||t==0x14)covered[z*s->map_width+x]=1;
    }
    for(int z=0;z<v->mh;++z)for(int x=0;x<s->map_width;++x)
        if(covered[(z/16)*s->map_width+x])
            hash=byteshash(hash,v->terrain+(size_t)z*v->mw+x*16,16*sizeof(*v->terrain));
    return hash?hash:1;
}
static void terrain_heights(Fe8VoxelRenderer *v,const Fe8Snapshot *s) {
    int w=v->mw,h=v->mh;size_t n=(size_t)w*h;
    memset(v->heights,0,n);memset(v->shadow,0,n);v->stats.columns=0;
    /* Infer roof ridges over connected roof cells, rather than repeating a
     * miniature house on every tile of one large building. */
    for(int z=0;z<h;++z)for(int x=0;x<w;++x){
        int tx=x/16,tz=z/16;unsigned t=s->terrain[tz*s->map_width+tx];
        uint32_t c=v->terrain[z*w+x];int r=c&255,g=(c>>8)&255,b=(c>>16)&255;
        float y=0;
        if(roof(t)){
            int left=tx,right=tx;
            while(left>0&&roof(s->terrain[tz*s->map_width+left-1]))--left;
            while(right+1<s->map_width&&roof(s->terrain[tz*s->map_width+right+1]))++right;
            float half=(right-left+1)*8.f,dist=fabsf(x+.5f-(left*16+half));
            y=13+fminf(16,half-dist)*.72f;
            if(green(c))y=0;
        }else if(structure(t)){
            y=10+(float)((r+g+b)/3)/65;
            if(t==0x1A||t==0x1B)y=20;
            if(green(c))y=0;
            if(t>=3&&t<=8&&z%16>10)y*=.65f;
            if(t==0x19)y=6;
        }else if(canopy(t)){
            if(green(c)){
                float a=(x%16-7.5f)/9.f,bb=(z%16-7.5f)/9.f;
                y=7+10*sqrtf(fmaxf(0,1-a*a*.65f-bb*bb*.65f))+(g-r)*.022f;
                if(g>180 || r>148)y=0;
            }
        }else if(t==0x0A){
            int a=x%16,bb=z%16;
            if(!green(c))y=(a<3||a>12||bb<3||bb>12)?7:2;
        }else if(t==0x1D)y=22;
        else if(t==0x20||t==0x21)y=green(c)?0:4;
        else if(t==0x39||t==0x33)y=green(c)?0:6;
        else if(t==0x13||t==0x14)y=1;
        if(roof(t))y=fmaxf(y,1); /* generated building replaces its full footprint */
        if(t==5||t==6||t==7||t==8||t==0x24)y=fmaxf(y,1);
        v->heights[z*w+x]=(uint8_t)clampf(roundf(y),0,48);
        v->stats.columns+=y>0;
    }
    /* Ground shadow atlas. A few soft, directional taps from every elevated
     * column give contact shadows without baking arbitrary shadows into ROM. */
    for(int z=0;z<h;++z)for(int x=0;x<w;++x){
        unsigned ht=v->heights[z*w+x]; if(!ht)continue;
        for(unsigned step=0;step<=ht;step+=2){
            int xx=x+(int)(step*.65f),zz=z+(int)(step*.42f);
            if(xx>=w||zz>=h)break;
            v->shadow[zz*w+xx]=145;
        }
    }
}
static float shadow_at(const Fe8VoxelRenderer *v,int x,int z) {
    unsigned sum=0,count=0;
    for(int dz=-1;dz<=1;++dz)for(int dx=-1;dx<=1;++dx){
        int xx=x+dx,zz=z+dz;if(xx<0||zz<0||xx>=v->mw||zz>=v->mh)continue;
        sum+=v->shadow[zz*v->mw+xx];++count;
    }
    return count?1.f-sum/(count*420.f):1;
}
/* Procedural object grammar: positions and footprints come exclusively from
 * live terrain. Materials are sampled from that same ROM's decoded pixels.
 * The missing sides/roofs are inferred, not claimed to be recovered assets. */
static unsigned noise(unsigned x,unsigned y,unsigned z) {
    unsigned n=x*73856093u^y*19349663u^z*83492791u;
    n^=n>>13;n*=1274126177u;return n^(n>>16);
}
static uint32_t material(const Fe8VoxelRenderer *v,int x,int z,int w,int h,int kind,uint32_t fallback) {
    unsigned rr=0,gg=0,bb=0,n=0;
    for(int j=z;j<z+h&&j<v->mh;++j)for(int i=x;i<x+w&&i<v->mw;++i){
        if(i<0||j<0)continue;
        uint32_t c=v->terrain[j*v->mw+i];
        unsigned r=c&255,g=(c>>8)&255,b=(c>>16)&255;
        bool match=kind==0?(green(c)&&g<180&&g>65):kind==1?(r>g*1.22f&&r>b*1.02f&&r>75):(r>100&&g>85&&r>b*1.2f&&g>b*1.1f&&!green(c));
        if(match){rr+=r;gg+=g;bb+=b;++n;}
    }
    return n?0xFF000000u|rr/n|(gg/n)<<8|(bb/n)<<16:fallback;
}
static void tree_object(Fe8VoxelRenderer *v,int tx,int tz) {
    float x=tx*16+8,z=tz*16+8;uint32_t leaves=material(v,tx*16,tz*16,16,16,0,0xFF478A59);
    box(v,x-1,0,z-1,2,12,2,0xFF405B70);
    box(v,x-3,.1f,z-1,6,1,2,0xFF405B70);
    box(v,x-1,.1f,z-3,2,1,6,0xFF405B70);
    const float step=1.5f;
    for(int iz=-5;iz<=5;++iz)for(int iy=0;iy<=9;++iy)for(int ix=-5;ix<=5;++ix){
        float fx=ix*step,fy=iy*step-6.2f,fz=iz*step;
        float r=fx*fx/40+fy*fy/48+fz*fz/37;
        unsigned seed=noise((unsigned)(ix+tx*11),(unsigned)(iy+17),(unsigned)(iz+tz*13));
        if(r>1.f+(seed%9)*.012f||r<.55f)continue;
        float shade=.82f+(seed%100)*.0032f+iy*.016f;
        box(v,x+fx-step/2,8+iy*step,z+fz-step/2,step,step,step,tint(leaves,shade,shade,shade));
    }
}
static void house_object(Fe8VoxelRenderer *v,float x,float z,float w,float d,
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
static void tower_object(Fe8VoxelRenderer *v,float x,float z,float w,float h,uint32_t stone) {
    box(v,x,0,z,w,h,w,stone);
    box(v,x-.5f,h-2,z-.5f,w+1,1,w+1,tint(stone,1.12f,1.10f,1.04f));
    for(int k=0;k<(int)w;k+=3){
        box(v,x+k,h,z,1.7f,2,1.7f,stone);box(v,x+k,h,z+w-1.7f,1.7f,2,1.7f,stone);
        box(v,x,h,z+k,1.7f,2,1.7f,stone);box(v,x+w-1.7f,h,z+k,1.7f,2,1.7f,stone);
    }
    box(v,x+w*.5f-.65f,h*.55f,z+w+.04f,1.3f,3,.25f,tint(stone,.32f,.37f,.42f));
    for(int j=2;j<h-3;j+=3)box(v,x,j,z+w+.02f,w,.15f,.06f,tint(stone,.78f,.80f,.82f));
}
static void map_objects(Fe8VoxelRenderer *v,const Fe8Snapshot *s) {
    uint8_t done[FE8_MAX_MAP_CELLS]={0};
    for(int z=0;z<s->map_height;++z)for(int x=0;x<s->map_width;++x){
        unsigned t=s->terrain[z*s->map_width+x];
        if(canopy(t)){tree_object(v,x,z);continue;}
        if(done[z*s->map_width+x])continue;
        if(roof(t)){
            int x1=x,z1=z;
            while(x1+1<s->map_width&&s->terrain[z*s->map_width+x1+1]==t)++x1;
            while(z1+1<s->map_height&&s->terrain[(z1+1)*s->map_width+x]==t)++z1;
            for(int j=z;j<=z1;++j)for(int i=x;i<=x1;++i)done[j*s->map_width+i]=1;
            int w=(x1-x+1)*16,d=(z1-z+1)*16;
            uint32_t red=material(v,x*16,z*16,w,d,1,0xFF447FAD),wall=material(v,x*16,z*16,w,d,2,0xFFABC1CE);
            if(t==0x2C){
                uint32_t stone=mix(wall,0xFFC0BDAE,.45f);
                box(v,x*16+6,0,z*16+6,w-12,9,d-12,stone);
                house_object(v,x*16+w*.3f,z*16+d*.25f,w*.40f,d*.40f,red,wall,(unsigned)x);
                for(int a=0;a<2;++a)for(int b=0;b<2;++b)
                    tower_object(v,x*16+3+a*(w-13),z*16+3+b*(d-13),10,18,stone);
                box(v,x*16+w*.5f-3,0,z*16+d-6,6,7,1,tint(stone,.32f,.37f,.42f));
            }else{
                house_object(v,x*16+w*.17f,z*16+d*.13f,w*.65f,d*.60f,red,wall,(unsigned)(x+z*17));
                if(w>=32){
                    for(int k=0;k<w;k+=3){
                        box(v,x*16+k,.05f,z*16+d-2,1,3,1,0xFF526B80);
                        box(v,x*16+k,.05f,z*16+1,1,3,1,0xFF526B80);
                    }
                    box(v,x*16,1.8f,z*16+d-2,w,.6f,.65f,0xFF526B80);
                }
            }
        }else if(t==0x0A){
            uint32_t stone=material(v,x*16,z*16,16,16,2,0xFF99AEB7);
            box(v,x*16+1,0,z*16+1,14,1.5f,14,stone);
            for(int k=0;k<4;++k){
                box(v,x*16+1+k*3.5f,1.5f,z*16+1,2,3,2,stone);
                box(v,x*16+1+k*3.5f,1.5f,z*16+13,2,3,2,stone);
                box(v,x*16+1,1.5f,z*16+1+k*3.5f,2,3,2,stone);
                box(v,x*16+13,1.5f,z*16+1+k*3.5f,2,3,2,stone);
            }
        }else if(t==5||t==6||t==7||t==8||t==0x24){
            uint32_t red=material(v,x*16,z*16,16,16,1,0xFF497FA3),wall=material(v,x*16,z*16,16,16,2,0xFFABC1CE);
            house_object(v,x*16+2,z*16+2,12,10,red,wall,(unsigned)(x+z*17));
        }
    }
}

static uint32_t substrate(const Fe8VoxelRenderer *v,const Fe8Snapshot *s,int x,int z) {
    int tx=x/16,tz=z/16;
    for(int radius=1;radius<6;++radius)for(int dz=-radius;dz<=radius;++dz)for(int dx=-radius;dx<=radius;++dx){
        if(abs(dx)!=radius&&abs(dz)!=radius)continue;
        int xx=tx+dx,zz=tz+dz;
        if(xx<0||zz<0||xx>=s->map_width||zz>=s->map_height)continue;
        if(s->terrain[zz*s->map_width+xx]!=1)continue;
        return v->terrain[(zz*16+z%16)*v->mw+xx*16+x%16];
    }
    return 0xFF83A98A;
}
/* Cache substrate searches, shadow filtering and material tint at map-pixel
 * resolution. Camera motion only samples this atlas, never repeats the search
 * once per output pixel. Invalidated with exactly the same ROM inputs. */
static void prepare_ground(Fe8VoxelRenderer *v,const Fe8Snapshot *s) {
    for(int z=0;z<v->mh;++z)for(int x=0;x<v->mw;++x){
        size_t i=(size_t)z*v->mw+x;
        uint32_t c=v->heights[i]?substrate(v,s,x,z):v->terrain[i];
        float sh=shadow_at(v,x,z);
        c=tint(c,sh*1.01f,sh*1.015f,sh*.98f);
        if(watery(s->terrain[(z/16)*s->map_width+x/16]))c=mix(c,0xFFAF9374,.12f);
        v->ground[i]=c;
    }
}
static void background(Fe8VoxelRenderer *v,const Fe8Snapshot *s) {
    int w=v->width,h=v->height;
    v->drawing_background=true;
    memset(v->ground_hits,0,(size_t)w*h*sizeof(*v->ground_hits));
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){
        float nx=(x-w*.5f)/w,ny=(y-h*.45f)/h;
        uint32_t c=mix(0xFF302A23,0xFF15120F,clampf(nx*nx+ny*ny,0,1));
        size_t i=(size_t)y*w+x;v->pixels[i]=c;v->depth[i]=-FLT_MAX;
    }
    /* An unobtrusive cut edge makes the flat, original map read as a diorama. */
    box(v,0,-5,0,(float)v->mw,4.7f,(float)v->mh,0xFF393D36);
    box(v,0,-.4f,0,(float)v->mw,.4f,(float)v->mh,0xFF748969);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){
        float wx,wz;unproject(v,x+.5f,y+.5f,&wx,&wz);
        if(wx<0||wz<0||wx>=v->mw||wz>=v->mh)continue;
        int ix=(int)wx,iz=(int)wz;size_t ti=(size_t)iz*v->mw+ix,i=(size_t)y*w+x;
        v->pixels[i]=v->ground[ti];v->depth[i]=project(v,wx,0,wz).z;
        v->ground_hits[i]=(uint32_t)(ti+1);
    }
    /* Heightfield surface only: internal faces are never emitted. */
    for(int z=0;z<v->mh;++z)for(int x=0;x<v->mw;++x){
        unsigned t=s->terrain[(z/16)*s->map_width+x/16];
        if(roof(t)||canopy(t)||(t>=3&&t<=8)||t==0x0B||t==0x0A||t==0x24)continue;
        int ht=v->heights[z*v->mw+x];if(!ht)continue;
        uint32_t c=v->terrain[z*v->mw+x];
        quad(v,project(v,x,ht,z),project(v,x+1,ht,z),project(v,x+1,ht,z+1),project(v,x,ht,z+1),tint(c,1.06f,1.03f,.96f));
        int nx=x+(v->sy>=0?1:-1),nz=z+(v->cy>=0?1:-1);
        int hx=nx>=0&&nx<v->mw?v->heights[z*v->mw+nx]:0;
        int hz=nz>=0&&nz<v->mh?v->heights[nz*v->mw+x]:0;
        float fx=x+(v->sy>=0?1:0),fz=z+(v->cy>=0?1:0);
        if(hx<ht)quad(v,project(v,fx,hx,z),project(v,fx,ht,z),project(v,fx,ht,z+1),project(v,fx,hx,z+1),tint(c,.68f,.73f,.80f));
        if(hz<ht)quad(v,project(v,x,hz,fz),project(v,x+1,hz,fz),project(v,x+1,ht,fz),project(v,x,ht,fz),tint(c,.83f,.84f,.86f));
    }
    map_objects(v,s);
    memcpy(v->backdrop,v->pixels,(size_t)w*h*4);memcpy(v->backdepth,v->depth,(size_t)w*h*sizeof(float));
    v->dirty=false;v->drawing_background=false;
    ++v->stats.background_builds;
}
static uint32_t gba_color(unsigned c) {
    unsigned r=c&31,g=(c>>5)&31,b=(c>>10)&31;
    return 0xFF000000u | ((r<<3)|(r>>2)) | ((g<<3)|(g>>2))<<8 | ((b<<3)|(b>>2))<<16;
}
/* FE8 replaces a hovered ally's standing SMS with a 32x32 MU (moving-unit)
 * OBJ even before selection. Its standing hide bit does NOT hide the unit.
 * Recover only a live native OBJ belonging to the visible cursor occupant;
 * never clear hide flags globally or resurrect dead/fog-hidden/event actors.
 * Matching the standing OBJ too covers the one-frame SMS -> MU upload handoff.
 * This is read-only and deliberately does not extend into selected-unit travel. */
static bool hover_sprite(const Fe8MemoryView *m, const Fe8Snapshot *s,
        Fe8VisibleMapSprite *actor, unsigned *flips) {
    if (!(actor->config & 0x80) || s->input_lock || s->phase ||
            s->combat_panel_active || (s->game_state_bits & 3) ||
            s->cursor_x >= s->map_width || s->cursor_y >= s->map_height ||
            actor->x_display != s->cursor_x * 16 ||
            actor->y_display != s->cursor_y * 16 ||
            !(s->flags & FE8_SNAPSHOT_UNIT_MAP)) return false;
    const Fe8VisibleUnit *unit = NULL;
    for (unsigned i = 0; i < s->visible_unit_count; ++i) {
        const Fe8VisibleUnit *u = &s->visible_units[i];
        /* US_HIDDEN, UNSELECTABLE, DEAD, NOT_DEPLOYED, RESCUED, FOG_HIDDEN. */
        if (u->faction || (u->state & 0x22Fu) || u->x != s->cursor_x ||
                u->y != s->cursor_y || !u->unit_id ||
                s->unit_map[u->y * s->map_width + u->x] != u->unit_id)
            continue;
        uint32_t a = u->map_sprite_handle;
        if (a < 0x02000000 || a > 0x0203FFF4 ||
                (int16_t)rd16(m,a+4) != actor->x_display ||
                (int16_t)rd16(m,a+6) != actor->y_display ||
                rd16(m,a+8) != actor->oam2 ||
                m->read8(m->context,a+11) != actor->config) continue;
        unit = u;
        break;
    }
    if (!unit || !(rd16(m,0x04000000) & 0x1000) ||
            (rd16(m,0x04000000) & 0x0040)) return false; /* 2D OBJ layout */
    int cx = unit->x * 16 + 8 - s->camera_x;
    int bottom = unit->y * 16 + 16 - s->camera_y;
    for (unsigned i = 0; i < 128; ++i) {
        uint32_t a = 0x07000000 + i * 8;
        unsigned a0 = rd16(m,a), a1 = rd16(m,a+2), a2 = rd16(m,a+4);
        /* No affine, hidden, mosaic, 8bpp, blend or OBJ-window substitutes. */
        if ((a0 & 0x3F00) || ((a2 >> 10) & 3) != 2 ||
                (a2 >> 12) != (actor->oam2 >> 12)) continue;
        int w, h, config;
        unsigned shape = a0 >> 14, size = a1 >> 14;
        if (shape == 0 && size == 1) { w = h = 16; config = 0; }
        else if (shape == 2 && size == 2) { w = 16; h = 32; config = 1; }
        else if (shape == 0 && size == 2) { w = h = 32; config = 2; }
        else continue;
        int x = a1 & 511, y = a0 & 255;
        if (x >= 256) x -= 512;
        if (y >= 160) y -= 256;
        /* Idle MU scripts can nudge their OBJ a few pixels (e.g. Archanae's
         * archer). Keep the search within this occupant's tile, never match
         * an adjacent unit, and keep the billboard's foot on its logical tile. */
        if (abs(x + w/2 - cx) > 4 || abs(y + h - bottom) > 4 ||
                x + w <= 0 || x >= 240 || y + h <= 0 || y >= 160) continue;
        actor->oam2 = (uint16_t)a2;
        actor->config = (uint8_t)config;
        *flips = a1 & 0x3000;
        return true;
    }
    return false;
}
static SpriteImage *sprite_image(Fe8VoxelRenderer *v,const Fe8MemoryView *m,unsigned oam2,unsigned config,unsigned flips) {
    int w,h;
    if(config&0x80)return NULL; /* FE8 SMS hide flag; never reveal hidden actors. */
    switch(config&15){case 0:case 3:w=h=16;break;case 1:case 4:w=16;h=32;break;case 2:case 5:w=h=32;break;default:return NULL;}
    uint32_t colors[MAX_SPRITE_PIXELS]={0};
    uint64_t hash=UINT64_C(14695981039346656037);hash=bytehash(hash,w);hash=bytehash(hash,h);
    uint32_t palette[16]={0};
    for(unsigned i=1;i<16;++i)
        palette[i]=gba_color(rd16(m,0x05000200+((oam2>>12)*16+i)*2));
    for(int y=0;y<h;++y)for(int x=0;x<w;x+=2){
        unsigned tile=((oam2&1023)+(y/8)*32+x/8)&1023;
        unsigned packed=m->read8(m->context,0x06010000+tile*32+(y%8)*4+(x%8)/2);
        int dy=(flips&0x2000)?h-1-y:y;
        int dx=(flips&0x1000)?w-1-x:x;
        colors[dy*w+dx]=palette[packed&15];
        colors[dy*w+dx+((flips&0x1000)?-1:1)]=palette[packed>>4];
    }
    hash=byteshash(hash,colors,(size_t)w*h*sizeof(*colors));
    SpriteImage *mesh=&v->sprites[0];
    for(unsigned i=0;i<SPRITE_CACHE;++i){
        SpriteImage *p=&v->sprites[i];
        if(p->hash==hash&&p->width==w&&p->height==h&&!memcmp(p->colors,colors,(size_t)w*h*4)){
            p->age=v->ticks;++v->stats.cached_sprites;return p;
        }
        if(p->age<mesh->age)mesh=p;
    }
    mesh->hash=hash;mesh->width=w;mesh->height=h;mesh->age=v->ticks;
    memcpy(mesh->colors,colors,(size_t)w*h*sizeof(*colors));
    mesh->bottom=-1;
    for(int y=h-1;y>=0&&mesh->bottom<0;--y)
        for(int x=0;x<w;++x)if(colors[y*w+x]){mesh->bottom=y;break;}
    ++v->stats.sprite_builds;return mesh;
}
static void unit_shadow(Fe8VoxelRenderer *v,float wx,float wz,float radius) {
    Point p=project(v,wx,0,wz);int r=(int)(radius*v->scale)+3;
    for(int y=(int)p.y-r;y<=(int)p.y+r;++y)for(int x=(int)p.x-r;x<=(int)p.x+r;++x){
        if(x<0||y<0||x>=v->width||y>=v->height)continue;
        float xx,zz;unproject(v,x+.5f,y+.5f,&xx,&zz);
        float dx=(xx-wx-2)/radius,dz=(zz-wz-1)/(radius*.5f),dist=dx*dx+dz*dz;
        if(dist>1)continue;
        size_t i=(size_t)y*v->width+x;
        if(v->depth[i]>project(v,xx,0,zz).z+.2f)continue;
        float shade=1-(1-dist)*.32f;v->pixels[i]=tint(v->pixels[i],shade,shade,shade);
    }
}
/* View-aligned, alpha-tested billboard: one flat sprite, no extruded pixel
 * boxes. The original palette is unlit. Its foot stays fixed on the map and
 * its image stays upright/front-facing through orbit and zoom. A constant
 * view-space depth is the depth of this camera-facing plane; terrain and
 * other units still occlude it. Transparent texels never write depth or hits. */
static void draw_sprite(Fe8VoxelRenderer *v,SpriteImage *sprite,const Fe8VisibleMapSprite *s) {
    if(!sprite||sprite->bottom<0)return;
    int w=sprite->width,bottom=sprite->bottom;
    float wx=s->x_display+8.f,wz=s->y_display+15.f;
    unit_shadow(v,wx,wz,w*.37f);
    Point foot=project(v,wx,.1f,wz);
    float left=foot.x-w*.5f*v->scale,top=foot.y-(bottom+1)*v->scale;
    int x0=(int)ceilf(left-.5f),x1=(int)ceilf(left+w*v->scale-.5f);
    int y0=(int)ceilf(top-.5f),y1=(int)ceilf(foot.y-.5f);
    if(x0<0)x0=0;
    if(y0<0)y0=0;
    if(x1>v->width)x1=v->width;
    if(y1>v->height)y1=v->height;
    int tx=s->x_display/16,tz=s->y_display/16;
    uint16_t hit=(tx>=0&&tz>=0&&tx<v->mw/16&&tz<v->mh/16)?
        (uint16_t)(1+tz*(v->mw/16)+tx):0;
    float inverse=1.f/v->scale;
    for(int y=y0;y<y1;++y){
        int sy=(int)((y+.5f-top)*inverse);
        if(sy<0||sy>bottom)continue;
        for(int x=x0;x<x1;++x){
            int sx=(int)((x+.5f-left)*inverse);
            if(sx<0||sx>=w)continue;
            uint32_t color=sprite->colors[sy*w+sx];
            if(!color)continue;
            size_t i=(size_t)y*v->width+x;
            if(foot.z>=v->depth[i]){
                v->pixels[i]=color;v->depth[i]=foot.z;v->unit_hits[i]=hit;
                ++v->stats.billboard_pixels;
            }
        }
    }
    ++v->stats.sprites;
}
static void range_and_cursor(Fe8VoxelRenderer *v,const Fe8Snapshot *s,bool cursor) {
    bool range=fe8_extended_move_range_is_active(s);
    if(!cursor&&!range)return;
    for(int ty=0;ty<s->map_height;++ty)for(int tx=0;tx<s->map_width;++tx){
        unsigned i=ty*s->map_width+tx;
        if(cursor){if(tx!=s->cursor_x||ty!=s->cursor_y)continue;}
        else if(!((s->flags&FE8_SNAPSHOT_MOVEMENT)&&s->movement[i]<128)&&!((s->flags&FE8_SNAPSHOT_RANGE)&&s->range[i]))continue;
        uint32_t c=cursor?0xFFC5EEFF:((s->flags&FE8_SNAPSHOT_MOVEMENT)&&s->movement[i]<128)?0xFFFFBF63:0xFF8888E8;
        float x=tx*16.f,z=ty*16.f,y=.12f;
        if(cursor){
            for(int a=0;a<2;++a)for(int b=0;b<2;++b){
                float xx=x+(a?12:0),zz=z+(b?15:0);
                box(v,xx,y,zz,4,.4f,.9f,c);
                box(v,x+(a?15:0),y,z+(b?12:0),.9f,.4f,4,c);
            }
        }else{
            box(v,x+.5f,y,z+.5f,15,.1f,.65f,c);box(v,x+.5f,y,z+15,15,.1f,.65f,c);
            box(v,x+.5f,y,z+.5f,.65f,.1f,15,c);box(v,x+15,y,z+.5f,.65f,.1f,15,c);
        }
    }
}
Fe8VoxelRenderer *fe8_voxel_create(void) {
    Fe8VoxelRenderer *v=calloc(1,sizeof(*v));if(!v)return NULL;
    v->sprites=calloc(SPRITE_CACHE,sizeof(*v->sprites));if(!v->sprites){free(v);return NULL;}
    v->yaw=-.32f;v->pitch=.74f;v->zoom=1;v->dirty=true;return v;
}
void fe8_voxel_destroy(Fe8VoxelRenderer *v) {
    if(!v)return;
    free(v->pixels);free(v->backdrop);free(v->depth);free(v->backdepth);free(v->unit_hits);free(v->ground_hits);
    free(v->terrain);free(v->ground);free(v->heights);free(v->shadow);free(v->sprites);free(v->output);free(v);
}
void fe8_voxel_invalidate(Fe8VoxelRenderer *v){if(v){v->terrain_hash=0;v->dirty=true;v->ready=false;memset(v->sprites,0,SPRITE_CACHE*sizeof(*v->sprites));}}
void fe8_voxel_camera(Fe8VoxelRenderer *v,float yaw_delta,float zoom_factor){
    if(!v||!isfinite(yaw_delta)||!isfinite(zoom_factor)||zoom_factor<=0)return;
    v->yaw=clampf(v->yaw+yaw_delta,-1.2f,1.2f);v->zoom=clampf(v->zoom*zoom_factor,.65f,4);v->dirty=true;
}
void fe8_voxel_pan(Fe8VoxelRenderer *v,float dx,float dy){
    if(!v||!v->ready||!isfinite(dx)||!isfinite(dy))return;
    dx *= (float)v->width / v->output_width;
    dy *= (float)v->height / v->output_height;
    float x=dx/v->scale,z=dy/(v->scale*v->sp);
    v->pan_x=clampf(v->pan_x-v->cy*x-v->sy*z,-v->mw*.5f,v->mw*.5f);
    v->pan_z=clampf(v->pan_z+v->sy*x-v->cy*z,-v->mh*.5f,v->mh*.5f);v->dirty=true;
}
void fe8_voxel_focus(Fe8VoxelRenderer *v,float x,float z){
    if(!v||!isfinite(x)||!isfinite(z))return;
    v->pan_x=clampf(x*16-v->mw*.5f,-v->mw*.5f,v->mw*.5f);
    v->pan_z=clampf(z*16-v->mh*.5f,-v->mh*.5f,v->mh*.5f);v->dirty=true;
}
void fe8_voxel_home(Fe8VoxelRenderer *v){if(v){v->pan_x=v->pan_z=0;v->zoom=1;v->yaw=-.32f;v->dirty=true;}}
const Fe8HostPixel *fe8_voxel_render_scene(Fe8VoxelRenderer *v,const Fe8MemoryView *m,const Fe8MapRenderState *map,
        const Fe8Snapshot *s,int width,int height){
    if(v){v->ready=false;v->error="invalid or unavailable map data";}
    if(!v||!m||!m->read8||!map||!s||!fe8_extended_state_is_sane(map)||
       !(s->flags&FE8_SNAPSHOT_TERRAIN)||s->map_width!=map->map_width||s->map_height!=map->map_height||
       s->map_sprite_count>FE8_MAX_MAP_SPRITES||s->visible_unit_count>FE8_MAX_VISIBLE_UNITS)return NULL;
    if(!resize(v,width,height,s->map_width*16,s->map_height*16))return NULL;
    width=v->width; height=v->height;
    ++v->ticks;v->stats.sprites=0;v->stats.hover_sprites=0;
    v->cy=cosf(v->yaw);v->sy=sinf(v->yaw);v->cp=cosf(v->pitch);v->sp=sinf(v->pitch);
    v->target_x=v->mw*.5f+v->pan_x;v->target_z=v->mh*.5f+v->pan_z;
    float projected_w=v->mw*fabsf(v->cy)+v->mh*fabsf(v->sy);
    float projected_h=(v->mw*fabsf(v->sy)+v->mh*fabsf(v->cy))*v->sp+36*v->cp;
    v->scale=fminf(width*.91f/projected_w,height*.76f/projected_h)*v->zoom;
    uint64_t hash=map_hash(m,map,s);
    if(hash!=v->terrain_hash){
        Fe8MapRenderState full=*map;full.camera_x=full.camera_y=0;
        Fe8ExtendedViewport vp={v->mw,v->mh,0,0};
        if(!fe8_render_extended_terrain(m,&full,vp,v->terrain,v->mw)) {
            v->error="terrain is not ready for voxel generation";
            return NULL;
        }
        uint64_t objects=object_hash(v,s);
        if(objects!=v->object_hash){terrain_heights(v,s);v->object_hash=objects;v->dirty=true;}
        prepare_ground(v,s);
        if(!v->dirty){
            /* Scenery pixels/depth are unchanged. Refresh only visible flat
             * ground from its cached projection, then redraw live actors. */
            for(size_t i=0,n=(size_t)width*height;i<n;++i)
                if(v->ground_hits[i])v->backdrop[i]=v->ground[v->ground_hits[i]-1];
            ++v->stats.ground_refreshes;
        }
        v->terrain_hash=hash;++v->stats.terrain_builds;
    }
    /* Inspect current ROM pixels every frame, but retain the completed scene
     * while animation, positions, cursor and camera are unchanged. No stale
     * frame reuse across state invalidation, palette changes, or resize. */
    Fe8VisibleMapSprite actors[FE8_MAX_VISIBLE_UNITS];
    SpriteImage *images[FE8_MAX_VISIBLE_UNITS];
    unsigned count=0;
    if(s->flags&FE8_SNAPSHOT_MAP_SPRITES){
        count=s->map_sprite_count;
        memcpy(actors,s->map_sprites,count*sizeof(*actors));
    }else{
        for(unsigned i=0;i<s->visible_unit_count&&count<FE8_MAX_VISIBLE_UNITS;++i){
            const Fe8VisibleUnit *u=&s->visible_units[i];uint32_t a=u->map_sprite_handle;
            if(a<0x02000000||a>0x0203FFF4)continue;
            actors[count++]=(Fe8VisibleMapSprite){(int16_t)(u->x*16),(int16_t)(u->y*16),
                rd16(m,a+8),m->read8(m->context,a+11)};
        }
    }
    uint64_t scene_hash=byteshash(hash,&count,sizeof(count));
    scene_hash=byteshash(scene_hash,&s->cursor_x,sizeof(s->cursor_x));
    scene_hash=byteshash(scene_hash,&s->cursor_y,sizeof(s->cursor_y));
    bool range=fe8_extended_move_range_is_active(s);
    scene_hash=bytehash(scene_hash,range);
    if(range){
        scene_hash=byteshash(scene_hash,&s->flags,sizeof(s->flags));
        scene_hash=byteshash(scene_hash,s->movement,(size_t)s->map_width*s->map_height);
        scene_hash=byteshash(scene_hash,s->range,(size_t)s->map_width*s->map_height);
    }
    for(unsigned i=0;i<count;++i){
        unsigned flips=0;
        if(hover_sprite(m,s,&actors[i],&flips))++v->stats.hover_sprites;
        images[i]=sprite_image(v,m,actors[i].oam2,actors[i].config,flips);
        scene_hash=byteshash(scene_hash,&actors[i].x_display,sizeof(actors[i].x_display));
        scene_hash=byteshash(scene_hash,&actors[i].y_display,sizeof(actors[i].y_display));
        uint64_t image_hash=images[i]?images[i]->hash:0;
        scene_hash=byteshash(scene_hash,&image_hash,sizeof(image_hash));
        if(images[i]&&images[i]->bottom>=0)++v->stats.sprites;
    }
    if(!v->dirty&&v->scene_hash==scene_hash){
        v->ready=true;++v->stats.reused_frames;return v->pixels;
    }
    v->stats.sprites=0;
    memset(v->unit_hits,0,(size_t)width*height*sizeof(*v->unit_hits));
    if(v->dirty)background(v,s);
    else{memcpy(v->pixels,v->backdrop,(size_t)width*height*4);memcpy(v->depth,v->backdepth,(size_t)width*height*sizeof(float));}
    range_and_cursor(v,s,false);
    for(unsigned i=0;i<count;++i)draw_sprite(v,images[i],&actors[i]);
    v->scene_hash=scene_hash;
    range_and_cursor(v,s,true);v->ready=true;
    return v->pixels;
}

/* Capture/compatibility path only. Interactive presentation sends the small
 * scene to the GPU and draws the native-resolution HUD as a separate layer. */
Fe8HostPixel *fe8_voxel_capture(Fe8VoxelRenderer *v) {
    if(!v||!v->ready)return NULL;
    int width=v->width,height=v->height;
    size_t count=(size_t)v->output_width*v->output_height;
    if(count>v->output_capacity){
        Fe8HostPixel *output=realloc(v->output,count*sizeof(*output));
        if(!output){v->error="unable to allocate voxel capture";return NULL;}
        v->output=output;v->output_capacity=count;
    }
    v->stats.output_pixels+=count;
    if(width==v->output_width&&height==v->output_height){
        memcpy(v->output,v->pixels,count*sizeof(*v->output));return v->output;
    }
    /* Nearest-neighbour scale preserves voxel edges. Reuse repeated rows to
     * avoid another full rasterization at Retina backing-store resolution. */
    int columns[16384];
    for (int x=0; x<v->output_width; ++x)
        columns[x]=(int)((int64_t)x*width/v->output_width);
    int previous=-1;
    for (int y=0; y<v->output_height; ++y) {
        int sy=(int)((int64_t)y*height/v->output_height);
        Fe8HostPixel *row=v->output+(size_t)y*v->output_width;
        if (sy==previous) memcpy(row,row-v->output_width,(size_t)v->output_width*sizeof(*row));
        else for (int x=0; x<v->output_width; ++x) row[x]=v->pixels[(size_t)sy*width+columns[x]];
        previous=sy;
    }
    return v->output;
}
Fe8HostPixel *fe8_voxel_render(Fe8VoxelRenderer *v,const Fe8MemoryView *m,
        const Fe8MapRenderState *map,const Fe8Snapshot *s,int w,int h) {
    return fe8_voxel_render_scene(v,m,map,s,w,h)?fe8_voxel_capture(v):NULL;
}
bool fe8_voxel_pick(const Fe8VoxelRenderer *v,float sx,float sy,int *x,int *z){
    if(!v||!v->ready||!x||!z||!isfinite(sx)||!isfinite(sy)||sx<0||sy<0||
            sx>=v->output_width||sy>=v->output_height)return false;
    sx *= (float)v->width/v->output_width;
    sy *= (float)v->height/v->output_height;
    if (sx>=v->width || sy>=v->height) return false;
    unsigned hit=v->unit_hits[(size_t)(int)sy*v->width+(int)sx];
    if(hit){--hit;*x=(int)hit%(v->mw/16);*z=(int)hit/(v->mw/16);return true;}
    float wx,wz;unproject(v,sx,sy,&wx,&wz);
    if(wx<0||wz<0||wx>=v->mw||wz>=v->mh)return false;
    *x=(int)wx/16;*z=(int)wz/16;return true;
}
bool fe8_voxel_project(const Fe8VoxelRenderer *v,float x,float z,float height,float *sx,float *sy){
    if(!v||!v->ready||!sx||!sy||!isfinite(x)||!isfinite(z)||!isfinite(height))return false;
    Point p=project(v,x*16,height,z*16);
    *sx=p.x*v->output_width/v->width;
    *sy=p.y*v->output_height/v->height;
    return true;
}
Fe8VoxelStats fe8_voxel_stats(const Fe8VoxelRenderer *v){return v?v->stats:(Fe8VoxelStats){0};}

const char *fe8_voxel_error(const Fe8VoxelRenderer *v) {
    return v ? v->error : "unable to create voxel renderer";
}
