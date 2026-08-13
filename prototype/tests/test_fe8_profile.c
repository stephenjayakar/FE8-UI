#include "fe8_profile.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define RAM_BASE UINT32_C(0x02000000)
#define RAM_SIZE UINT32_C(0x00040000)
#define ROM_BASE UINT32_C(0x08000000)
#define ROM_SIZE UINT32_C(0x00000100)

static uint8_t ram[RAM_SIZE];
static uint8_t rom[ROM_SIZE];
static uint32_t synthetic_base_tiles;

static uint8_t read8(void *context, uint32_t address) {
    (void)context;
    if (address >= RAM_BASE && address - RAM_BASE < RAM_SIZE)
        return ram[address - RAM_BASE];
    if (address >= ROM_BASE && address - ROM_BASE < ROM_SIZE)
        return rom[address - ROM_BASE];
    if (address >= UINT32_C(0x0859A9D4) && address < UINT32_C(0x0859A9D8))
        return (uint8_t)(synthetic_base_tiles >> ((address - UINT32_C(0x0859A9D4)) * 8));
    if (address >= UINT32_C(0x03004E50) && address < UINT32_C(0x03004E54))
        return 0;
    return 0;
}

static void put8(uint32_t address, uint8_t value) {
    assert(address >= RAM_BASE && address - RAM_BASE < RAM_SIZE);
    ram[address - RAM_BASE] = value;
}

static void put16(uint32_t address, uint16_t value) {
    put8(address, (uint8_t)value);
    put8(address + 1, (uint8_t)(value >> 8));
}

static void put32(uint32_t address, uint32_t value) {
    put16(address, (uint16_t)value);
    put16(address + 2, (uint16_t)(value >> 16));
}

static void put_rom(uint32_t address, const char *text) {
    while (*text)
        rom[address++ - ROM_BASE] = (uint8_t)*text++;
}

static void put_rom32(uint32_t address, uint32_t value) {
    assert(address >= ROM_BASE && address - ROM_BASE + 4 <= ROM_SIZE);
    rom[address - ROM_BASE] = (uint8_t)value;
    rom[address + 1 - ROM_BASE] = (uint8_t)(value >> 8);
    rom[address + 2 - ROM_BASE] = (uint8_t)(value >> 16);
    rom[address + 3 - ROM_BASE] = (uint8_t)(value >> 24);
}

static void setup_header(void) {
    memset(rom, 0, sizeof(rom));
    put_rom(UINT32_C(0x080000A0), "FIREEMBLEM2E");
    put_rom(UINT32_C(0x080000AC), "BE8E");
    put_rom(UINT32_C(0x080000B0), "01");
    rom[0xBC] = 0;
    rom[0xBD] = 0x9D;
}

static uint32_t make_map(uint32_t handle, uint32_t rows, uint16_t width, uint16_t height, uint8_t seed) {
    uint16_t y;
    put32(handle, rows);
    for (y = 0; y < height; ++y) {
        uint32_t row = rows + 0x100 + (uint32_t)y * width;
        put32(rows + (uint32_t)y * 4, row);
        for (uint16_t x = 0; x < width; ++x)
            put8(row + x, (uint8_t)(seed + x + y * width));
    }
    return handle;
}

static void setup_state(void) {
    const Fe8Profile *profile = fe8u_profile();
    const uint16_t width = 4;
    const uint16_t height = 3;
    memset(ram, 0, sizeof(ram));
    put16(profile->map_size, width);
    put16(profile->map_size + 2, height);
    put16(profile->bm_state + 0x0C, 0x0010);
    put16(profile->bm_state + 0x0E, 0x0020);
    put16(profile->bm_state + 0x28, 0x0030);
    put16(profile->bm_state + 0x2A, 0x0040);
    put16(profile->bm_state + 0x14, 2);
    put16(profile->bm_state + 0x16, 1);
    put16(profile->bm_state + 0x1C, 32);
    put16(profile->bm_state + 0x1E, 16);
    put16(profile->bm_state + 0x20, 28);
    put16(profile->bm_state + 0x22, 16);
    put8(profile->bm_state + 0x01, 0);
    put8(profile->bm_state + 0x04, 2);
    put8(profile->play_state + 0x0E, 7);
    put8(profile->play_state + 0x0F, 0);
    make_map(profile->map_terrain, UINT32_C(0x02032000), width, height, 10);
    make_map(profile->map_unit, UINT32_C(0x02033000), width, height, 0);
    make_map(profile->map_movement, UINT32_C(0x02034000), width, height, 20);
    make_map(profile->map_range, UINT32_C(0x02035000), width, height, 30);
    make_map(profile->map_fog, UINT32_C(0x02036000), width, height, 40);
    make_map(profile->map_hidden, UINT32_C(0x02037000), width, height, 50);
    make_map(profile->map_other, UINT32_C(0x02038000), width, height, 60);
    synthetic_base_tiles = UINT32_C(0x02039000);
    for (uint16_t y = 0; y < height; ++y) {
        uint32_t row = UINT32_C(0x02039100) + (uint32_t)y * width * 2;
        put32(synthetic_base_tiles + (uint32_t)y * 4, row);
        for (uint16_t x = 0; x < width; ++x)
            put16(row + (uint32_t)x * 2, (uint16_t)(x + y * width));
    }

    /* Unit 1 is visible at (1, 2); its character pointer is a valid ROM pointer. */
    put32(profile->blue_units, UINT32_C(0x08000000));
    put8(profile->blue_units + 0x0B, 1);
    put32(profile->blue_units + 0x0C, 0);
    put8(profile->blue_units + 0x10, 1);
    put8(profile->blue_units + 0x11, 2);
    put32(profile->blue_units + 0x3C, UINT32_C(0x02031000));

    /* Unit 0x81 is an enemy; faction values must not be swapped with NPCs. */
    put32(profile->red_units, UINT32_C(0x08000040));
    put32(profile->red_units + 4, UINT32_C(0x08000070));
    put_rom32(UINT32_C(0x08000040) + 0x28, UINT32_C(1) << 15);
    put8(profile->red_units + 0x0B, 0x81);
    put32(profile->red_units + 0x0C, 0);
    put8(profile->red_units + 0x10, 3);
    put8(profile->red_units + 0x11, 2);
    put32(profile->red_units + 0x3C, UINT32_C(0x02031100));
}

static void test_identity(void) {
    Fe8MemoryReader memory = { NULL, read8 };
    setup_header();
    assert(fe8_detect_retail_fe8u(&memory));
    assert(fe8_detect_fe8u_family(&memory));
    rom[0xBD] = 0;
    assert(!fe8_detect_retail_fe8u(&memory));
    assert(fe8_detect_fe8u_family(&memory));
}

static void test_snapshot(void) {
    Fe8MemoryReader memory = { NULL, read8 };
    Fe8Snapshot snapshot;
    setup_state();
    assert(fe8_extract_snapshot(&memory, fe8u_profile(), &snapshot));
    assert(snapshot.map_width == 4 && snapshot.map_height == 3);
    assert(snapshot.camera_x == 0x10 && snapshot.camera_y == 0x20);
    assert(snapshot.cursor_x == 2 && snapshot.cursor_y == 1);
    assert(snapshot.cursor_target_x == 32 && snapshot.cursor_target_y == 16);
    assert(snapshot.cursor_display_x == 28 && snapshot.cursor_display_y == 16);
    assert(snapshot.input_lock == 0);
    assert(snapshot.game_state_bits == 2);
    assert(snapshot.terrain[0] == 10 && snapshot.terrain[11] == 21);
    assert((snapshot.flags & (FE8_SNAPSHOT_TERRAIN | FE8_SNAPSHOT_UNIT_MAP)) ==
        (FE8_SNAPSHOT_TERRAIN | FE8_SNAPSHOT_UNIT_MAP));
    assert(snapshot.visible_unit_count == 2);
    assert(snapshot.visible_units[0].unit_id == 1);
    assert(snapshot.visible_units[0].x == 1 && snapshot.visible_units[0].y == 2);
    assert(snapshot.visible_units[0].faction == 0x00);
    assert(snapshot.visible_units[0].map_sprite_handle == UINT32_C(0x02031000));
    assert(snapshot.visible_units[1].unit_id == 0x81);
    assert(snapshot.visible_units[1].faction == 0x80);
    assert((snapshot.visible_units[1].attributes & (UINT32_C(1) << 15)) != 0);
}

static void test_rejects_bad_rows_and_dimensions(void) {
    Fe8MemoryReader memory = { NULL, read8 };
    Fe8Snapshot snapshot;
    setup_state();
    put16(fe8u_profile()->map_size, 65);
    assert(!fe8_extract_snapshot(&memory, fe8u_profile(), &snapshot));
    setup_state();
    put32(fe8u_profile()->map_terrain, UINT32_C(0x0203FFFC));
    assert(!fe8_extract_snapshot(&memory, fe8u_profile(), &snapshot));
}

int main(void) {
    test_identity();
    test_snapshot();
    test_rejects_bad_rows_and_dimensions();
    puts("test_fe8_profile: ok");
    return 0;
}
