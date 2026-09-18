#include "voxel_stage.h"
#include <stdlib.h>
#include <string.h>

enum { RAM_SIZE=0x40000, IRAM_SIZE=0x8000, VRAM_SIZE=0x18000 };
struct Fe8VoxelStage {
    uint8_t ram[RAM_SIZE], iram[IRAM_SIZE], vram[VRAM_SIZE];
    uint8_t palette[1024], oam[1024], io[1024];
    Fe8AddressSpace space;
    Fe8MemoryView memory;
    Fe8Snapshot snapshot;
    Fe8MapRenderState map;
    Fe8PaletteMapping mapping;
    Fe8TerrainCache *cache;
    bool has_map;
};
static void wr16(uint8_t *p, unsigned value) {p[0]=(uint8_t)value;p[1]=(uint8_t)(value>>8);}
static void wr32(uint8_t *p, uint32_t value) {wr16(p,value);wr16(p+2,value>>16);}
static void address_space(Fe8VoxelStage *s) {
    fe8_address_space_init(&s->space,NULL,NULL);
    fe8_address_space_add(&s->space,0x02000000,s->ram,sizeof(s->ram));
    fe8_address_space_add(&s->space,0x03000000,s->iram,sizeof(s->iram));
    fe8_address_space_add(&s->space,0x04000000,s->io,sizeof(s->io));
    fe8_address_space_add(&s->space,0x05000000,s->palette,sizeof(s->palette));
    fe8_address_space_add(&s->space,0x06000000,s->vram,sizeof(s->vram));
    fe8_address_space_add(&s->space,0x07000000,s->oam,sizeof(s->oam));
    s->memory=(Fe8MemoryView){&s->space,fe8_address_space_read8};
}
void fe8_voxel_stage_reset(Fe8VoxelStage *s) {
    if(!s)return;
    memset(s->ram,0,sizeof(s->ram));memset(s->iram,0,sizeof(s->iram));
    memset(s->vram,0,sizeof(s->vram));memset(s->palette,0,sizeof(s->palette));
    memset(s->oam,0,sizeof(s->oam));memset(s->io,0,sizeof(s->io));
    memset(&s->snapshot,0,sizeof(s->snapshot));
    address_space(s);s->has_map=false;
    /* Asset-free neutral stage. It cannot reveal the previous chapter, fog,
     * or another ROM after a reset/load. All game pixels remain in a live panel. */
    s->snapshot.map_width=16;s->snapshot.map_height=12;
    s->snapshot.cursor_x=s->snapshot.cursor_y=255;
    s->snapshot.flags=FE8_SNAPSHOT_TERRAIN|FE8_SNAPSHOT_MAP_SPRITES;
    memset(s->snapshot.terrain,1,16*12);
    s->map=(Fe8MapRenderState){.map_width=16,.map_height=12,
        .base_tile_rows=0x02000100,.tileset_config=0x02001000,
        .tile_graphics=0x06008000,.palette=0x05000000,.tile_cache=s->cache};
    for(unsigned y=0;y<12;++y)wr32(s->ram+0x100+y*4,0x02000200+y*32);
    for(unsigned i=0;i<4;++i)wr16(s->ram+0x1000+i*2,1);
    wr16(s->palette+2,7|(10<<5)|(10<<10));
    wr16(s->palette+4,10|(14<<5)|(14<<10));
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<4;++x)
        s->vram[0x8020+y*4+x]=(uint8_t)((!x||!y)?0x22:0x11);
    fe8_terrain_cache_reset(s->cache);
}
Fe8VoxelStage *fe8_voxel_stage_create(void) {
    Fe8VoxelStage *s=calloc(1,sizeof(*s));if(!s)return NULL;
    s->cache=fe8_terrain_cache_create();if(!s->cache){free(s);return NULL;}
    fe8_voxel_stage_reset(s);return s;
}
void fe8_voxel_stage_destroy(Fe8VoxelStage *s) {
    if(s){fe8_terrain_cache_destroy(s->cache);free(s);}
}
static const Fe8AddressBlock *block(const Fe8AddressSpace *space,uint32_t base,size_t size) {
    for(size_t i=0;i<space->block_count;++i) {
        const Fe8AddressBlock *b=&space->blocks[i];
        if(b->base==base && b->data && b->size>=size)return b;
    }
    return NULL;
}
bool fe8_voxel_stage_remember(Fe8VoxelStage *s,const Fe8AddressSpace *space,
        const Fe8MapRenderState *map,const Fe8Snapshot *snapshot) {
    if(!s||!space||!map||!snapshot||!fe8_extended_state_is_sane(map)||
            map->map_width!=snapshot->map_width||map->map_height!=snapshot->map_height||
            !(snapshot->flags&FE8_SNAPSHOT_TERRAIN))return false;
    const uint32_t bases[]={0x02000000,0x03000000,0x05000000,0x06000000,0x07000000};
    void *dest[]={s->ram,s->iram,s->palette,s->vram,s->oam};
    const size_t sizes[]={sizeof(s->ram),sizeof(s->iram),sizeof(s->palette),sizeof(s->vram),sizeof(s->oam)};
    const Fe8AddressBlock *sources[5];
    for(unsigned i=0;i<5;++i)if(!(sources[i]=block(space,bases[i],sizes[i])))return false;
    /* memcpy, not hundreds of thousands of callback reads on every M1 frame. */
    for(unsigned i=0;i<5;++i)memcpy(dest[i],sources[i]->data,sizes[i]);
    for(unsigned i=0;i<sizeof(s->io);++i)s->io[i]=fe8_address_space_read8((void *)space,0x04000000+i);
    address_space(s);
    /* ROM is immutable and stays loaded for the session. No live RAM fallback. */
    for(size_t i=0;i<space->block_count;++i)if(space->blocks[i].base==0x08000000) {
        /* Already validated by the live address space. Do not SHA-1 the entire
         * 32 MiB ROM again for every presentation frame. */
        s->space.blocks[s->space.block_count++]=space->blocks[i];
        memcpy(s->space.rom_sha1,space->rom_sha1,sizeof(s->space.rom_sha1));
        s->space.rom_sha1_valid=space->rom_sha1_valid;
        break;
    }
    s->snapshot=*snapshot;s->map=*map;s->map.tile_cache=s->cache;
    if(map->palette_mapping){s->mapping=*map->palette_mapping;s->map.palette_mapping=&s->mapping;}
    fe8_terrain_cache_reset(s->cache);s->has_map=true;return true;
}
const Fe8MemoryView *fe8_voxel_stage_memory(const Fe8VoxelStage *s){return s?&s->memory:NULL;}
const Fe8MapRenderState *fe8_voxel_stage_map(const Fe8VoxelStage *s){return s?&s->map:NULL;}
const Fe8Snapshot *fe8_voxel_stage_snapshot(const Fe8VoxelStage *s){return s?&s->snapshot:NULL;}
bool fe8_voxel_stage_has_map(const Fe8VoxelStage *s){return s&&s->has_map;}
