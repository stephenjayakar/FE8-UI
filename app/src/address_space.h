#ifndef FE8_ADDRESS_SPACE_H
#define FE8_ADDRESS_SPACE_H

#include "fe8_profile.h"

#include <stddef.h>
#include <stdint.h>

enum { FE8_ADDRESS_SPACE_MAX_BLOCKS = 8 };

typedef struct Fe8AddressBlock {
    uint32_t base;
    size_t size;
    const uint8_t *data;
} Fe8AddressBlock;

typedef struct Fe8AddressSpace {
    Fe8AddressBlock blocks[FE8_ADDRESS_SPACE_MAX_BLOCKS];
    size_t block_count;
    void *fallback_context;
    Fe8Read8 fallback_read8;
} Fe8AddressSpace;

void fe8_address_space_init(
    Fe8AddressSpace *space, void *fallback_context, Fe8Read8 fallback_read8);
bool fe8_address_space_add(
    Fe8AddressSpace *space, uint32_t base, const void *data, size_t size);
uint8_t fe8_address_space_read8(void *context, uint32_t address);

#endif
