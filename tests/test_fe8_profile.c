#include "fe8_profile.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define RAM_BASE UINT32_C(0x02000000)
#define RAM_SIZE UINT32_C(0x00040000)
#define IWRAM_BASE UINT32_C(0x03000000)
#define IWRAM_SIZE UINT32_C(0x00008000)
#define ROM_BASE UINT32_C(0x08000000)
#define ROM_SIZE UINT32_C(0x00028000)

static uint8_t ram[RAM_SIZE];
static uint8_t iwram[IWRAM_SIZE];
static uint8_t rom[ROM_SIZE];
static uint32_t synthetic_base_tiles;

static uint8_t read8(void *context, uint32_t address) {
    (void)context;
    if (address >= RAM_BASE && address - RAM_BASE < RAM_SIZE)
        return ram[address - RAM_BASE];
    if (address >= IWRAM_BASE && address - IWRAM_BASE < IWRAM_SIZE)
        return iwram[address - IWRAM_BASE];
    if (address >= ROM_BASE && address - ROM_BASE < ROM_SIZE)
        return rom[address - ROM_BASE];
    if (address >= UINT32_C(0x0859A9D4) && address < UINT32_C(0x0859A9D8))
        return (uint8_t)(synthetic_base_tiles >> ((address - UINT32_C(0x0859A9D4)) * 8));
    return 0;
}

static void put8(uint32_t address, uint8_t value) {
    if (address >= RAM_BASE && address - RAM_BASE < RAM_SIZE) {
        ram[address - RAM_BASE] = value;
        return;
    }
    assert(address >= IWRAM_BASE && address - IWRAM_BASE < IWRAM_SIZE);
    iwram[address - IWRAM_BASE] = value;
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
    memset(iwram, 0, sizeof(iwram));
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
    put8(profile->blue_units + 0x12, 24);
    put8(profile->blue_units + 0x13, 17);
    put32(profile->blue_units + 0x3C, UINT32_C(0x02031000));

    /* FE8's linked SMS list includes ordinary units and non-unit map effects
     * such as Pokemblem's flower traps. */
    put32(profile->sms_handle_array, profile->sms_handle_array + 12);
    put32(profile->sms_handle_array + 12, profile->sms_handle_array + 24);
    put16(profile->sms_handle_array + 12 + 4, 16);
    put16(profile->sms_handle_array + 12 + 6, 32);
    put16(profile->sms_handle_array + 12 + 8, 0xD080);
    put8(profile->sms_handle_array + 12 + 0x0B, 0);
    put32(profile->sms_handle_array + 24, 0);
    put16(profile->sms_handle_array + 24 + 4, 48);
    put16(profile->sms_handle_array + 24 + 6, 16);
    put16(profile->sms_handle_array + 24 + 8, 0xE0A0);
    put8(profile->sms_handle_array + 24 + 0x0B, 2);

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
    assert(strcmp(fe8_profile_for_rom(&memory)->profile_name, "Sacred Echoes") == 0);
    assert(fe8_profile_for_rom(&memory)->convoy_items == UINT32_C(0x0203B200));
    assert(fe8_profile_for_rom(&memory)->inventory.convoy_capacity == 200);
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
    assert(!snapshot.combat_panel_active);
    assert(snapshot.game_state_bits == 2);
    assert(snapshot.terrain[0] == 10 && snapshot.terrain[11] == 21);
    assert((snapshot.flags & (FE8_SNAPSHOT_TERRAIN | FE8_SNAPSHOT_UNIT_MAP)) ==
        (FE8_SNAPSHOT_TERRAIN | FE8_SNAPSHOT_UNIT_MAP));
    assert(snapshot.visible_unit_count == 2);
    assert(snapshot.visible_units[0].unit_id == 1);
    assert(snapshot.visible_units[0].x == 1 && snapshot.visible_units[0].y == 2);
    assert(snapshot.visible_units[0].faction == 0x00);
    assert(snapshot.visible_units[0].max_hp == 24);
    assert(snapshot.visible_units[0].current_hp == 17);
    assert(snapshot.visible_units[0].map_sprite_handle == UINT32_C(0x02031000));
    assert(snapshot.visible_units[1].unit_id == 0x81);
    assert(snapshot.visible_units[1].faction == 0x80);
    assert((snapshot.visible_units[1].attributes & (UINT32_C(1) << 15)) != 0);
    assert((snapshot.flags & FE8_SNAPSHOT_MAP_SPRITES) != 0);
    assert(snapshot.map_sprite_count == 2);
    assert(snapshot.map_sprites[0].x_display == 16);
    assert(snapshot.map_sprites[0].y_display == 32);
    assert(snapshot.map_sprites[0].oam2 == 0xD080);
    assert(snapshot.map_sprites[1].x_display == 48);
    assert(snapshot.map_sprites[1].config == 2);
}

static void test_extracts_iwram_movement_maps(void) {
    Fe8MemoryReader memory = { NULL, read8 };
    Fe8Snapshot snapshot;
    const Fe8Profile *profile = fe8u_profile();
    setup_state();

    /* Some FE8 hacks move the working movement and range maps into IWRAM
     * while leaving the map handles at their retail EWRAM addresses. */
    make_map(profile->map_movement, UINT32_C(0x03000800), 4, 3, 70);
    make_map(profile->map_range, UINT32_C(0x03001000), 4, 3, 90);
    assert(fe8_extract_snapshot(&memory, profile, &snapshot));
    assert((snapshot.flags & FE8_SNAPSHOT_MOVEMENT) != 0);
    assert((snapshot.flags & FE8_SNAPSHOT_RANGE) != 0);
    assert(snapshot.movement[0] == 70 && snapshot.movement[11] == 81);
    assert(snapshot.range[0] == 90 && snapshot.range[11] == 101);
}

static void test_detects_combat_panel(void) {
    Fe8MemoryReader memory = { NULL, read8 };
    Fe8Snapshot snapshot;
    const Fe8Profile *profile = fe8u_profile();
    unsigned actor;
    setup_state();
    put8(profile->bm_state + 0x01, 1);
    put8(profile->map_animation_state + 0x5E, 2);
    for (actor = 0; actor < 2; ++actor) {
        uint32_t actor_state = profile->map_animation_state + actor * 0x14;
        unsigned panel_x = actor == 0 ? 14 : 4;
        unsigned panel_y = 6;
        unsigned tile;
        put32(actor_state, UINT32_C(0x08000000) + actor * 0x40);
        put8(actor_state + 0x10, (uint8_t)panel_x);
        put8(actor_state + 0x11, (uint8_t)panel_y);
        for (tile = 0; tile < 4; ++tile)
            put16(profile->bg1_tilemap +
                (panel_y * 32 + panel_x + tile) * 2,
                (uint16_t)(((actor + 1) << 12) | (tile + 1)));
    }
    assert(fe8_extract_snapshot(&memory, profile, &snapshot));
    assert(snapshot.combat_panel_active);
    put8(profile->bm_state + 0x01, 0);
    assert(fe8_extract_snapshot(&memory, profile, &snapshot));
    assert(!snapshot.combat_panel_active);
}

static void test_detects_map_hp_bar_patch(void) {
    Fe8MemoryReader memory = { NULL, read8 };
    Fe8Snapshot snapshot;
    setup_state();
    put_rom32(UINT32_C(0x080276B4), UINT32_C(0x47184B00));
    put_rom32(UINT32_C(0x080276B8), UINT32_C(0x0842E16D));
    assert(fe8_extract_snapshot(&memory, fe8u_profile(), &snapshot));
    assert((snapshot.flags & FE8_SNAPSHOT_HP_BARS) != 0);
    put_rom32(UINT32_C(0x080276B4), 0);
    assert(fe8_extract_snapshot(&memory, fe8u_profile(), &snapshot));
    assert((snapshot.flags & FE8_SNAPSHOT_HP_BARS) == 0);
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
    test_extracts_iwram_movement_maps();
    test_detects_combat_panel();
    test_detects_map_hp_bar_patch();
    test_rejects_bad_rows_and_dimensions();
    puts("test_fe8_profile: ok");
    return 0;
}
