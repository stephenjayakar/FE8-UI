#include "inventory_history.h"
#include <string.h>

bool fe8_inventory_history_transfer(Fe8InventoryHistory *h,
    const Fe8MemoryReader *reader, const Fe8MemoryWriter *writer,
    const Fe8Profile *profile, Fe8InventoryEndpoint a, uint16_t av,
    Fe8InventoryEndpoint b, uint16_t bv) {
    if (!h || h->count<0 || h->count>FE8_INVENTORY_HISTORY_CAPACITY || av==bv ||
        (a.kind==b.kind && a.unit_address==b.unit_address && a.slot==b.slot) ||
        !fe8_swap_inventory_endpoints(reader,writer,profile,a,av,b,bv)) return false;
    if (h->count==FE8_INVENTORY_HISTORY_CAPACITY) {
        memmove(h->changes,h->changes+1,(FE8_INVENTORY_HISTORY_CAPACITY-1)*sizeof(*h->changes));
        --h->count;
    }
    h->changes[h->count++]=(Fe8InventoryChange){a,b,av,bv};
    return true;
}
bool fe8_inventory_history_undo(Fe8InventoryHistory *h,
    const Fe8MemoryReader *reader, const Fe8MemoryWriter *writer,
    const Fe8Profile *profile) {
    if (!h || h->count<=0 || h->count>FE8_INVENTORY_HISTORY_CAPACITY) return false;
    const Fe8InventoryChange *last=&h->changes[h->count-1];
    if (!fe8_swap_inventory_endpoints(reader,writer,profile,last->first,last->second_item,
        last->second,last->first_item)) return false;
    --h->count;
    return true;
}
