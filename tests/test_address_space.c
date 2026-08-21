#include "address_space.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t fallback(void *context, uint32_t address) {
    unsigned *calls = context;
    ++*calls;
    return (uint8_t)(address ^ 0xA5);
}

int main(void) {
    static const uint8_t bytes[] = {1, 2, 3, 4};
    static const uint8_t rom[] = {'a', 'b', 'c'};
    static const uint8_t abc_sha1[FE8_ROM_SHA1_SIZE] = {
        0xA9, 0x99, 0x3E, 0x36, 0x47, 0x06, 0x81, 0x6A, 0xBA, 0x3E,
        0x25, 0x71, 0x78, 0x50, 0xC2, 0x6C, 0x9C, 0xD0, 0xD8, 0x9D,
    };
    Fe8AddressSpace space;
    const uint8_t *digest;
    unsigned calls = 0;
    fe8_address_space_init(&space, &calls, fallback);
    assert(fe8_address_space_rom_sha1(&space) == NULL);
    assert(fe8_address_space_add(&space, UINT32_C(0x02000000), bytes, sizeof(bytes)));
    assert(fe8_address_space_read8(&space, UINT32_C(0x02000000)) == 1);
    assert(fe8_address_space_read8(&space, UINT32_C(0x02000003)) == 4);
    assert(calls == 0);
    assert(fe8_address_space_read8(&space, UINT32_C(0x02000004)) ==
        (uint8_t)(UINT32_C(0x02000004) ^ 0xA5));
    assert(calls == 1);
    assert(fe8_address_space_add(&space, UINT32_C(0x08000000), rom, sizeof(rom)));
    digest = fe8_address_space_rom_sha1(&space);
    assert(digest != NULL && memcmp(digest, abc_sha1, sizeof(abc_sha1)) == 0);
    assert(!fe8_address_space_add(&space, UINT32_MAX - 1, bytes, sizeof(bytes)));
    puts("address space tests passed");
    return 0;
}
