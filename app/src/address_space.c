#include "address_space.h"

#include <mgba/core/core.h>
#include <mgba-util/sha1.h>

#include <string.h>

#define FE8_ROM_BASE UINT32_C(0x08000000)
#define FE8_GBA_ROM_WINDOW_SIZE ((size_t)UINT32_C(0x02000000))

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
    if (base == FE8_ROM_BASE) {
        /* mGBA expands non-power-of-two ROMs to its full 32 MiB mapped
         * cartridge window. Hashing that block includes emulator-added zero
         * padding and does not match the ROM file's SHA-1. map_core_memory()
         * supplies the backing mCore as fallback_context, so ask the public
         * checksum API for the pristine image whenever expansion occurred. */
        if (size == FE8_GBA_ROM_WINDOW_SIZE && space->fallback_context) {
            struct mCore *core = space->fallback_context;
            if (core->checksum) {
                core->checksum(core, space->rom_sha1, mCHECKSUM_SHA1);
                space->rom_sha1_valid = true;
            }
        }
        if (!space->rom_sha1_valid) {
            sha1Buffer(data, size, space->rom_sha1);
            space->rom_sha1_valid = true;
        }
    }
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

const uint8_t *fe8_address_space_rom_sha1(const Fe8AddressSpace *space) {
    return space && space->rom_sha1_valid ? space->rom_sha1 : NULL;
}
