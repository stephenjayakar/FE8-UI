#include "address_space.h"
#include "fe8_profile.h"

#include <mgba/core/core.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GBA_ROM_WINDOW_SIZE ((size_t)UINT32_C(0x02000000))

static const uint8_t s_archanae_sha1[FE8_ROM_SHA1_SIZE] = {
    0x22, 0x0D, 0x1D, 0x6B, 0x5F, 0x56, 0xC9, 0xE2, 0x5E, 0xB6,
    0x66, 0xA7, 0xF3, 0xDD, 0x7C, 0x48, 0x6A, 0x75, 0x94, 0x17,
};

static uint8_t fallback_read8(void *context, uint32_t address) {
    (void)context;
    (void)address;
    return 0;
}

static void archanae_checksum(
    const struct mCore *core, void *output, enum mCoreChecksumType type) {
    (void)core;
    assert(type == mCHECKSUM_SHA1);
    memcpy(output, s_archanae_sha1, sizeof(s_archanae_sha1));
}

int main(void) {
    uint8_t *mapped_rom = calloc(GBA_ROM_WINDOW_SIZE, 1);
    struct mCore core = {0};
    Fe8AddressSpace space;
    Fe8MemoryReader memory;
    const Fe8Profile *profile;
    const uint8_t *digest;

    assert(mapped_rom != NULL);
    memcpy(mapped_rom + 0xA0, "FIREEMBLEM2E", 12);
    memcpy(mapped_rom + 0xAC, "BE8E", 4);
    memcpy(mapped_rom + 0xB0, "01", 2);

    /* Reproduce mGBA's non-power-of-two ROM path: the mapped block is a
     * zero-padded 32 MiB window, while core->checksum returns the SHA-1 of the
     * original ROM file. The profile must use the latter. */
    core.checksum = archanae_checksum;
    fe8_address_space_init(&space, &core, fallback_read8);
    assert(fe8_address_space_add(
        &space, UINT32_C(0x08000000), mapped_rom, GBA_ROM_WINDOW_SIZE));
    digest = fe8_address_space_rom_sha1(&space);
    assert(digest != NULL);
    assert(memcmp(digest, s_archanae_sha1, sizeof(s_archanae_sha1)) == 0);

    memory.context = &space;
    memory.read8 = fe8_address_space_read8;
    profile = fe8_profile_for_rom(&memory);
    assert(strcmp(profile->profile_name, "Fire Emblem: Archanae") == 0);
    assert(profile->inventory.item_table == UINT32_C(0x09AA54F8));
    assert(profile->inventory.get_convoy_items == UINT32_C(0x08031500));
    assert(profile->inventory.convoy_capacity == 200);
    assert(profile->inventory.immovable_item_attributes == 0);
    assert(profile->convoy_items == UINT32_C(0x0203B200));

    space.rom_sha1[0] ^= 1;
    profile = fe8_profile_for_rom(&memory);
    assert(strcmp(profile->profile_name, "Fire Emblem 8 (FE8U)") == 0);

    free(mapped_rom);
    puts("Archanae profile hash tests passed");
    return 0;
}
