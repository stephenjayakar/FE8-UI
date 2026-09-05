#include "prebattle_inventory.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t rom[0x2000];
static uint8_t read8(void *unused, uint32_t address) {
    (void)unused;
    if (address == 0x09FFFFFF) return 1; /* Truncated list at ROM boundary. */
    return address >= 0x08000000 && address < 0x08002000 ? rom[address - 0x08000000] : 0;
}
static void put32(unsigned offset, uint32_t value) {
    for (int j=0;j<4;++j) rom[offset+j]=(uint8_t)(value>>(8*j));
}
static Fe8ItemInfo decode(Fe8Catalog *catalog) {
    Fe8MemoryReader reader={NULL,read8}; Fe8ItemInfo item;
    assert(fe8_catalog_item(&reader,catalog,0x01C2,&item));
    return item;
}
int main(void) {
    Fe8Catalog c={0}; Fe8InventoryUnit u={0}; Fe8ItemInfo item;
    c.valid=true; c.item_table=0x08000000; c.weapon_lock_table=0x08001C00;
    unsigned record=0xC2*0x24;
    rom[record+6]=0xC2; put32(record+8,0x0A000019);
    put32(0x1C00+10*4,0x08001A00);
    rom[0x1A00]=1; rom[0x1A01]=0x36;
    item=decode(&c);
    assert(item.movable && item.lock_kind==FE8_ITEM_LOCK_CHARACTER);
    u.character_id=1;u.ranks[0]=255;
    assert(fe8_inventory_item_use_state(&u,&item)==FE8_INVENTORY_USE_LOCKED);
    u.character_id=0x36;
    assert(fe8_inventory_item_use_state(&u,&item)==FE8_INVENTORY_USE_READY);
    /* IDs, not name strings, decide personal eligibility. */
    strcpy(u.name,"Marth");
    assert(fe8_inventory_item_use_state(&u,&item)==FE8_INVENTORY_USE_READY);
    rom[0x1A00]=3;item=decode(&c);
    assert(item.lock_kind==FE8_ITEM_LOCK_CLASS);
    assert(fe8_inventory_item_use_state(&u,&item)==FE8_INVENTORY_USE_LOCKED);
    u.class_id=0x36;
    assert(fe8_inventory_item_use_state(&u,&item)==FE8_INVENTORY_USE_READY);
    rom[0x1A01]=0;item=decode(&c); /* Empty whitelist denies everyone. */
    assert(fe8_inventory_item_use_state(&u,&item)==FE8_INVENTORY_USE_LOCKED);
    for (int mode=0;mode<=2;mode+=2) {
        rom[0x1A00]=(uint8_t)mode;item=decode(&c);
        assert(item.lock_kind==FE8_ITEM_LOCK_NONE);
    }
    rom[0x1A00]=4;item=decode(&c);assert(item.lock_kind==FE8_ITEM_LOCK_UNKNOWN);
    assert(fe8_inventory_item_use_state(&u,&item)==FE8_INVENTORY_USE_UNKNOWN);
    rom[0x1A00]=1;memset(rom+0x1A01,1,256);
    item=decode(&c);assert(item.lock_kind==FE8_ITEM_LOCK_UNKNOWN);
    put32(0x1C00+10*4,0x09FFFFFF);item=decode(&c);assert(item.lock_kind==FE8_ITEM_LOCK_UNKNOWN);
    put32(0x1C00+10*4,0x02000000);item=decode(&c);assert(item.lock_kind==FE8_ITEM_LOCK_UNKNOWN);
    put32(0x1C00+10*4,0);item=decode(&c);assert(item.lock_kind==FE8_ITEM_LOCK_NONE);
    c.weapon_lock_table=0x09FFFFFC;item=decode(&c);assert(item.lock_kind==FE8_ITEM_LOCK_UNKNOWN);
    c.weapon_lock_table=0;item=decode(&c);assert(item.lock_kind==FE8_ITEM_LOCK_NONE);
    puts("Profile-scoped character/class locks, null/invalid tables and bounded decoding passed");
    return 0;
}
