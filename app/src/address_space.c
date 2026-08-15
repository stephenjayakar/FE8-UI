#include "address_space.h"

#include <string.h>

void fe8_address_space_init(
    Fe8AddressSpace *space, void *fallback_context, Fe8Read8 fallback_read8) {
    memset(space, 0, sizeof(*space));
    space->fallback_context = fallback_context;
    space->fallback_read8 = fallback_read8;
}

bool fe8_address_space_add(
    Fe8AddressSpace *space, uint32_t base, const void *data, size_t size) {
    Fe8AddressBlock *block;
    if (!space || !data || size == 0 || space->block_count >= FE8_ADDRESS_SPACE_MAX_BLOCKS)
        return false;
    if ((uint64_t)base + size > (uint64_t)UINT32_MAX + 1)
        return false;
    block = &space->blocks[space->block_count++];
    block->base = base;
    block->data = data;
    block->size = size;
    return true;
}

uint8_t fe8_address_space_read8(void *context, uint32_t address) {
    Fe8AddressSpace *space = context;
    size_t i;
    if (!space)
        return 0;
    for (i = 0; i < space->block_count; ++i) {
        const Fe8AddressBlock *block = &space->blocks[i];
        if (address >= block->base &&
                (uint64_t)address - block->base < block->size)
            return block->data[address - block->base];
    }
    return space->fallback_read8 ?
        space->fallback_read8(space->fallback_context, address) : 0;
}
