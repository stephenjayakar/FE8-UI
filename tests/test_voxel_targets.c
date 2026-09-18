#include "voxel_targets.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned char ewram[0x40000], iwram[0x8000], rom[256];
static uint8_t read8(void *unused,uint32_t a) {
    (void)unused;
    if(a>=0x02000000&&a<0x02040000)return ewram[a-0x02000000];
    if(a>=0x03000000&&a<0x03008000)return iwram[a-0x03000000];
    if(a>=0x08000000&&a<0x08000100)return rom[a-0x08000000];
    return 0;
}
static void put(uint32_t a,uint32_t value) {
    unsigned char *p=a>=0x08000000?rom+a-0x08000000:
        a>=0x03000000?iwram+a-0x03000000:ewram+a-0x02000000;
    for(unsigned i=0;i<4;++i)p[i]=(unsigned char)(value>>(8*i));
}
static void fixture(void) {
    memset(ewram,0,sizeof(ewram));memset(iwram,0,sizeof(iwram));memset(rom,0,sizeof(rom));
    const uint32_t p=0x03001000,s=0x08000000,info=0x08000050;
    put(s,0x0B);put(s+8,3);put(s+12,0x080000A1);put(s+16,0x1000E);
    put(s+24,2);put(s+28,0x080000B1);put(s+32,0x0C);
    put(info+0x14,0x080000C1);
    put(p,s);put(p+4,s+16);put(p+0x0C,0x080000A1);put(p+0x14,3);
    put(p+0x2C,info);put(p+0x30,0x02001000);put(p+0x34,1);
    for(unsigned i=0;i<3;++i){uint32_t node=0x02001000+i*12;
        put(node,(i+2)|3u<<8|(129+i)<<16);
        put(node+4,0x02001000+(i+1)%3*12);put(node+8,0x02001000+(i+2)%3*12);
    }
}
int main(void) {
    Fe8MemoryReader memory={NULL,read8};Fe8Snapshot snapshot={.map_width=16,.map_height=12,.input_lock=1};
    Fe8VoxelTargets targets={0};Fe8TargetClick click={0};fixture();
    fe8_voxel_targets_read(&targets,&memory,&snapshot);
    assert(targets.active&&targets.count==3&&targets.targets[0].unit==129);
    assert(!fe8_voxel_target_click(&click,&targets,9,9)&&!click.active);
    assert(fe8_voxel_target_click(&click,&targets,4,3));
    assert(!fe8_voxel_target_keys(&click,&memory,&snapshot));
    assert(fe8_voxel_target_keys(&click,&memory,&snapshot)==(1u<<6));
    put(targets.proc+0x30,0x0200100C); /* The GAME, not the renderer, changes targets. */
    assert(!fe8_voxel_target_keys(&click,&memory,&snapshot));
    assert(fe8_voxel_target_keys(&click,&memory,&snapshot)==(1u<<6));
    put(targets.proc+0x30,0x02001018);
    assert(!fe8_voxel_target_keys(&click,&memory,&snapshot));
    assert(fe8_voxel_target_keys(&click,&memory,&snapshot)==1&&!click.active);
    /* No A or B on a stale/frozen/malformed target, scene change or wrong owner. */
    for(unsigned test=0;test<8;++test){
        fixture();snapshot.phase=0;snapshot.input_lock=1;snapshot.flags=0;
        targets=(Fe8VoxelTargets){0};fe8_voxel_targets_read(&targets,&memory,&snapshot);assert(targets.active);
        assert(fe8_voxel_target_click(&click,&targets,4,3));
        switch(test){
        case 0:put(targets.proc+0x34,0x41);break;
        case 1:put(0x02001018+8,0);break;
        case 2:put(0x02001018,4|4u<<8|131u<<16);break;
        case 3:snapshot.phase=128;break;
        case 4:snapshot.input_lock=0;break;
        case 5:put(targets.proc+0x2C,0x08000070);break;
        case 6:put(targets.proc+4,0);break;
        case 7:snapshot.flags=FE8_SNAPSHOT_FOG;break;
        }
        assert(!fe8_voxel_target_keys(&click,&memory,&snapshot)&&!click.active);
    }
    fixture();snapshot=(Fe8Snapshot){.map_width=16,.map_height=12,.input_lock=1};
    targets=(Fe8VoxelTargets){0};fe8_voxel_targets_read(&targets,&memory,&snapshot);assert(targets.active);
    assert(fe8_voxel_target_click(&click,&targets,4,3));
    for(unsigned i=0;i<181;++i)assert(fe8_voxel_target_keys(&click,&memory,&snapshot)!=1);
    assert(!click.active); /* Rejected native movement cannot issue an unbounded input stream. */
    puts("PASS exact target ring discovery, native-only cycling/confirm, stale/fog/frozen/owner rejection and bounded cancellation");
    return 0;
}
