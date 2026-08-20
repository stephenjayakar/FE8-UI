#include "address_space.h"
#include "fe8_profile.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    static uint8_t rom[0x200];
    static const uint8_t archanae_sha1[FE8_ROM_SHA1_SIZE] = {
        0x22, 0x0D, 0x1D, 0x6B, 0x5F, 0x56, 0xC9, 0xE2, 0x5E, 0xB6,
        0x66, 0xA7, 0xF3, 0xDD, 0x7C, 0x48, 0x6A, 0x75, 0x94, 0x17,
    };
    Fe8AddressSpace space;
    Fe8MemoryReader memory;
    const Fe8Profile *profile;

    memcpy(rom + 0xA0, "FIREEMBLEM2E", 12);
    memcpy(rom + 0xAC, "BE8E", 4);
    memcpy(rom + 0xB0, "01", 2);
    fe8_address_space_init(&space, NULL, NULL);
    assert(fe8_address_space_add(&space, UINT32_C(0x08000000), rom, sizeof(rom)));
    memcpy(space.rom_sha1, archanae_sha1, sizeof(archanae_sha1));
    space.rom_sha1_valid = true;
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

    puts("Archanae profile hash tests passed");
    return 0;
}
