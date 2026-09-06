#include "inventory_history.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned char ram[0x40000], before[0x40000];
static uint8_t r8(void *ctx,uint32_t address) {
    (void)ctx;return address>=0x02000000 && address<0x02040000?ram[address-0x02000000]:0;
}
static void w8(void *ctx,uint32_t address,uint8_t value) {
    (void)ctx;assert(address>=0x02000000 && address<0x02040000);ram[address-0x02000000]=value;
}
static void put(uint32_t address,uint32_t value,int count) {
    for(int i=0;i<count;++i)w8(NULL,address+i,(uint8_t)(value>>(i*8)));
}
int main(void) {
    Fe8Profile p={0};p.blue_units=0x0202BE4C;p.convoy_items=0x0203B200;p.inventory.convoy_capacity=200;
    put(p.blue_units,0x08001000,4); /* Structurally valid active roster gate. */
    Fe8MemoryReader reader={NULL,r8};Fe8MemoryWriter writer={NULL,w8};Fe8InventoryHistory h={0};
    Fe8InventoryEndpoint a={FE8_INVENTORY_ENDPOINT_UNIT,p.blue_units,0};
    Fe8InventoryEndpoint b={FE8_INVENTORY_ENDPOINT_UNIT,p.blue_units+0x48,0};
    put(p.blue_units+0x1E,0x2801,2);memcpy(before,ram,sizeof(ram));
    assert(!fe8_inventory_history_undo(&h,&reader,&writer,&p));
    assert(!fe8_inventory_history_transfer(&h,&reader,&writer,&p,a,0x2801,a,0x2801));
    for(int n=0;n<32;++n)assert(fe8_inventory_history_transfer(&h,&reader,&writer,&p,n%2?b:a,0x2801,n%2?a:b,0));
    assert(h.count==32);
    /* Stale values must not pop history or overwrite the other endpoint. */
    put(a.unit_address+0x1E,0x1234,2);unsigned char changed[sizeof(ram)];memcpy(changed,ram,sizeof(ram));
    assert(!fe8_inventory_history_undo(&h,&reader,&writer,&p));assert(h.count==32);
    assert(!memcmp(changed,ram,sizeof(ram)));put(a.unit_address+0x1E,0x2801,2);
    for(int n=0;n<32;++n)assert(fe8_inventory_history_undo(&h,&reader,&writer,&p));
    assert(!memcmp(before,ram,sizeof(ram)) && h.count==0);
    for(int n=0;n<40;++n)assert(fe8_inventory_history_transfer(&h,&reader,&writer,&p,n%2?b:a,0x2801,n%2?a:b,0));
    assert(h.count==32);
    for(int n=0;n<32;++n)assert(fe8_inventory_history_undo(&h,&reader,&writer,&p));
    assert(!memcmp(before,ram,sizeof(ram)));
    assert(!fe8_inventory_history_transfer(&h,&reader,&writer,&p,a,0x2802,b,0));assert(h.count==0);
    puts("Bounded session undo, stale-value rejection, no-op rejection and exact RAM restoration passed");
    return 0;
}
