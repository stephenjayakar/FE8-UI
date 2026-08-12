#include "fe8_profile.h"

#include <string.h>

#define FE8_EWRAM_START UINT32_C(0x02000000)
#define FE8_EWRAM_END UINT32_C(0x02040000)
#define FE8_IWRAM_START UINT32_C(0x03000000)
#define FE8_IWRAM_END UINT32_C(0x03008000)
#define FE8_ROM_START UINT32_C(0x08000000)
#define FE8_ROM_END UINT32_C(0x0A000000)
#define FE8_HEADER_TITLE UINT32_C(0x080000A0)
#define FE8_HEADER_GAME_CODE UINT32_C(0x080000AC)
#define FE8_HEADER_MAKER_CODE UINT32_C(0x080000B0)
#define FE8_HEADER_VERSION UINT32_C(0x080000BC)
#define FE8_HEADER_CHECKSUM UINT32_C(0x080000BD)

#define FE8_BM_STATE UINT32_C(0x0202BCB0)
#define FE8_PLAY_STATE UINT32_C(0x0202BCF0)
#define FE8_MAP_SIZE UINT32_C(0x0202E4D4)
#define FE8_MAP_UNIT UINT32_C(0x0202E4D8)
#define FE8_MAP_TERRAIN UINT32_C(0x0202E4DC)
#define FE8_MAP_MOVEMENT UINT32_C(0x0202E4E0)
#define FE8_MAP_RANGE UINT32_C(0x0202E4E4)
#define FE8_MAP_FOG UINT32_C(0x0202E4E8)
#define FE8_MAP_HIDDEN UINT32_C(0x0202E4EC)
#define FE8_MAP_OTHER UINT32_C(0x0202E4F0)
#define FE8_MAP_BASE_TILES UINT32_C(0x0859A9D4)
#define FE8_TILESET_CONFIG UINT32_C(0x02030B8C)
#define FE8_BLUE_UNITS UINT32_C(0x0202BE4C)
#define FE8_RED_UNITS UINT32_C(0x0202CFBC)
#define FE8_GREEN_UNITS UINT32_C(0x0202DDCC)

#define FE8_UNIT_SIZE UINT32_C(0x48)
#define FE8_BLUE_UNIT_COUNT 62u
#define FE8_RED_UNIT_COUNT 50u
#define FE8_GREEN_UNIT_COUNT 20u
#define FE8_UNIT_STATE_HIDDEN UINT32_C(1u << 0)
#define FE8_UNIT_STATE_DEAD UINT32_C(1u << 2)
#define FE8_UNIT_STATE_NOT_DEPLOYED UINT32_C(1u << 3)

static const Fe8Profile s_fe8u_profile = {
    FE8_HEADER_TITLE, FE8_HEADER_GAME_CODE, FE8_HEADER_MAKER_CODE, 0, 0x9D,
    FE8_BM_STATE, FE8_PLAY_STATE, FE8_MAP_SIZE, FE8_MAP_UNIT, FE8_MAP_TERRAIN,
    FE8_MAP_MOVEMENT, FE8_MAP_RANGE, FE8_MAP_FOG, FE8_MAP_HIDDEN, FE8_MAP_OTHER,
    FE8_MAP_BASE_TILES, FE8_TILESET_CONFIG,
    FE8_BLUE_UNITS, FE8_RED_UNITS, FE8_GREEN_UNITS,
};

static bool valid_reader(const Fe8MemoryReader *memory) {
    return memory && memory->read8;
}

static bool valid_range(uint32_t address, uint32_t size, uint32_t begin, uint32_t end) {
    return address >= begin && size <= end - begin && address - begin <= end - begin - size;
}

static bool valid_ewram(uint32_t address, uint32_t size) {
    return valid_range(address, size, FE8_EWRAM_START, FE8_EWRAM_END);
}

static bool valid_pointer(uint32_t address) {
    return valid_range(address, 1, FE8_EWRAM_START, FE8_EWRAM_END) ||
        valid_range(address, 1, FE8_IWRAM_START, FE8_IWRAM_END) ||
        valid_range(address, 1, FE8_ROM_START, FE8_ROM_END);
}

static uint8_t read8(const Fe8MemoryReader *memory, uint32_t address) {
    return memory->read8(memory->context, address);
}

static uint16_t read16(const Fe8MemoryReader *memory, uint32_t address) {
    return (uint16_t)(read8(memory, address) | ((uint16_t)read8(memory, address + 1) << 8));
}

static uint32_t read32(const Fe8MemoryReader *memory, uint32_t address) {
    return (uint32_t)read16(memory, address) | ((uint32_t)read16(memory, address + 2) << 16);
}

static bool read_header_bytes(const Fe8MemoryReader *memory, uint32_t address, const char *expected, uint32_t size) {
    uint32_t i;
    if (!valid_range(address, size, FE8_ROM_START, FE8_ROM_END))
        return false;
    for (i = 0; i < size; ++i)
        if (read8(memory, address + i) != (uint8_t)expected[i])
            return false;
    return true;
}

const Fe8Profile *fe8u_profile(void) {
    return &s_fe8u_profile;
}

bool fe8_detect_fe8u_family(const Fe8MemoryReader *memory) {
    static const char game_code[] = "BE8E";
    static const char maker_code[] = "01";
    if (!valid_reader(memory))
        return false;
    return read_header_bytes(memory, FE8_HEADER_GAME_CODE, game_code, sizeof(game_code) - 1) &&
        read_header_bytes(memory, FE8_HEADER_MAKER_CODE, maker_code, sizeof(maker_code) - 1);
}

bool fe8_detect_retail_fe8u(const Fe8MemoryReader *memory) {
    static const char title[] = "FIREEMBLEM2E";
    return fe8_detect_fe8u_family(memory) &&
        read_header_bytes(memory, FE8_HEADER_TITLE, title, sizeof(title) - 1) &&
        read8(memory, FE8_HEADER_VERSION) == 0 &&
        read8(memory, FE8_HEADER_CHECKSUM) == 0x9D;
}

static bool map_rows_valid(
    const Fe8MemoryReader *memory, uint32_t handle_address, uint16_t width, uint16_t height,
    uint32_t rows[FE8_MAX_MAP_HEIGHT]) {
    uint32_t table;
    uint16_t y;
    if (!valid_ewram(handle_address, 4) || width == 0 || height == 0 ||
        width > FE8_MAX_MAP_WIDTH || height > FE8_MAX_MAP_HEIGHT)
        return false;
    table = read32(memory, handle_address);
    if (!valid_ewram(table, (uint32_t)height * 4))
        return false;
    for (y = 0; y < height; ++y) {
        rows[y] = read32(memory, table + (uint32_t)y * 4);
        if (!valid_ewram(rows[y], width))
            return false;
    }
    return true;
}

static bool extract_map(
    const Fe8MemoryReader *memory, uint32_t handle_address, uint16_t width, uint16_t height,
    uint8_t *destination) {
    uint32_t rows[FE8_MAX_MAP_HEIGHT];
    uint16_t y;
    if (!map_rows_valid(memory, handle_address, width, height, rows))
        return false;
    for (y = 0; y < height; ++y) {
        uint16_t x;
        for (x = 0; x < width; ++x)
            destination[(uint32_t)y * width + x] = read8(memory, rows[y] + x);
    }
    return true;
}

static bool base_tile_rows_valid(
    const Fe8MemoryReader *memory, uint32_t pointer_address, uint16_t width,
    uint16_t height, uint32_t *table_out) {
    uint32_t table;
    uint16_t y;
    if (!valid_range(pointer_address, 4, FE8_ROM_START, FE8_ROM_END))
        return false;
    table = read32(memory, pointer_address);
    if (!valid_ewram(table, (uint32_t)height * 4))
        return false;
    for (y = 0; y < height; ++y) {
        uint32_t row = read32(memory, table + (uint32_t)y * 4);
        if (!valid_ewram(row, (uint32_t)width * 2))
            return false;
    }
    *table_out = table;
    return true;
}

static void append_units(
    const Fe8MemoryReader *memory, uint32_t base, uint32_t count, uint8_t faction,
    uint16_t width, uint16_t height, Fe8Snapshot *snapshot) {
    uint32_t i;
    if (!valid_ewram(base, count * FE8_UNIT_SIZE))
        return;
    for (i = 0; i < count && snapshot->visible_unit_count < FE8_MAX_VISIBLE_UNITS; ++i) {
        uint32_t address = base + i * FE8_UNIT_SIZE;
        uint32_t state = read32(memory, address + 0x0C);
        int8_t x = (int8_t)read8(memory, address + 0x10);
        int8_t y = (int8_t)read8(memory, address + 0x11);
        uint32_t character = read32(memory, address);
        if (!valid_pointer(character) || (state & (FE8_UNIT_STATE_HIDDEN | FE8_UNIT_STATE_DEAD |
            FE8_UNIT_STATE_NOT_DEPLOYED)) != 0 || x < 0 || y < 0 || x >= (int8_t)width || y >= (int8_t)height)
            continue;
        snapshot->visible_units[snapshot->visible_unit_count++] = (Fe8VisibleUnit){
            read8(memory, address + 0x0B), faction, x, y, state};
    }
}

bool fe8_extract_snapshot(
    const Fe8MemoryReader *memory, const Fe8Profile *profile, Fe8Snapshot *snapshot) {
    uint16_t width;
    uint16_t height;
    uint32_t map_handles[7];
    uint8_t *map_destinations[7];
    uint32_t map_flags[7];
    uint32_t i;
    if (!valid_reader(memory) || !profile || !snapshot ||
        !valid_ewram(profile->bm_state, 0x2C) || !valid_ewram(profile->play_state, 0x14) ||
        !valid_ewram(profile->map_size, 4))
        return false;
    memset(snapshot, 0, sizeof(*snapshot));
    width = read16(memory, profile->map_size);
    height = read16(memory, profile->map_size + 2);
    if (width == 0 || height == 0 || width > FE8_MAX_MAP_WIDTH || height > FE8_MAX_MAP_HEIGHT)
        return false;
    snapshot->map_width = width;
    snapshot->map_height = height;
    snapshot->camera_x = (int16_t)read16(memory, profile->bm_state + 0x0C);
    snapshot->camera_y = (int16_t)read16(memory, profile->bm_state + 0x0E);
    snapshot->camera_max_x = (int16_t)read16(memory, profile->bm_state + 0x28);
    snapshot->camera_max_y = (int16_t)read16(memory, profile->bm_state + 0x2A);
    snapshot->cursor_x = (uint8_t)read16(memory, profile->bm_state + 0x14);
    snapshot->cursor_y = (uint8_t)read16(memory, profile->bm_state + 0x16);
    snapshot->chapter = read8(memory, profile->play_state + 0x0E);
    snapshot->phase = read8(memory, profile->play_state + 0x0F);
    if (snapshot->cursor_x >= width || snapshot->cursor_y >= height)
        return false;

    map_handles[0] = profile->map_terrain;
    map_handles[1] = profile->map_unit;
    map_handles[2] = profile->map_movement;
    map_handles[3] = profile->map_range;
    map_handles[4] = profile->map_fog;
    map_handles[5] = profile->map_hidden;
    map_handles[6] = profile->map_other;
    map_destinations[0] = snapshot->terrain;
    map_destinations[1] = snapshot->unit_map;
    map_destinations[2] = snapshot->movement;
    map_destinations[3] = snapshot->range;
    map_destinations[4] = snapshot->fog;
    map_destinations[5] = snapshot->hidden;
    map_destinations[6] = snapshot->other;
    map_flags[0] = FE8_SNAPSHOT_TERRAIN;
    map_flags[1] = FE8_SNAPSHOT_UNIT_MAP;
    map_flags[2] = FE8_SNAPSHOT_MOVEMENT;
    map_flags[3] = FE8_SNAPSHOT_RANGE;
    map_flags[4] = FE8_SNAPSHOT_FOG;
    map_flags[5] = FE8_SNAPSHOT_HIDDEN;
    map_flags[6] = FE8_SNAPSHOT_OTHER;
    if (!extract_map(memory, map_handles[0], width, height, snapshot->terrain))
        return false;
    snapshot->flags |= FE8_SNAPSHOT_TERRAIN;
    snapshot->fog_rows = read32(memory, profile->map_fog);
    if (!base_tile_rows_valid(memory, profile->map_base_tiles, width, height,
            &snapshot->base_tile_rows))
        return false;
    for (i = 1; i < 7; ++i)
        if (extract_map(memory, map_handles[i], width, height, map_destinations[i]))
            snapshot->flags |= map_flags[i];

    append_units(memory, profile->blue_units, FE8_BLUE_UNIT_COUNT, 0x00, width, height, snapshot);
    append_units(memory, profile->red_units, FE8_RED_UNIT_COUNT, 0x40, width, height, snapshot);
    append_units(memory, profile->green_units, FE8_GREEN_UNIT_COUNT, 0x80, width, height, snapshot);
    if (snapshot->visible_unit_count != 0)
        snapshot->flags |= FE8_SNAPSHOT_UNITS;
    return true;
}
