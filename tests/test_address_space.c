#include "address_space.h"

#include <assert.h>
#include <stdio.h>

static uint8_t fallback(void *context, uint32_t address) {
    unsigned *calls = context;
    ++*calls;
    return (uint8_t)(address ^ 0xA5);
}

int main(void) {
    static const uint8_t bytes[] = {1, 2, 3, 4};
    Fe8AddressSpace space;
    unsigned calls = 0;
    fe8_address_space_init(&space, &calls, fallback);
    assert(fe8_address_space_add(&space, UINT32_C(0x02000000), bytes, sizeof(bytes)));
    assert(fe8_address_space_read8(&space, UINT32_C(0x02000000)) == 1);
    assert(fe8_address_space_read8(&space, UINT32_C(0x02000003)) == 4);
    assert(calls == 0);
    assert(fe8_address_space_read8(&space, UINT32_C(0x02000004)) ==
        (uint8_t)(UINT32_C(0x02000004) ^ 0xA5));
    assert(calls == 1);
    assert(!fe8_address_space_add(&space, UINT32_MAX - 1, bytes, sizeof(bytes)));
    puts("address space tests passed");
    return 0;
}
