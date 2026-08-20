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
#define FE8_ACTIVE_UNIT UINT32_C(0x03004E50)
#define FE8_SMS_HANDLE_ARRAY UINT32_C(0x0203A018)
#define FE8_MAP_ANIMATION_STATE UINT32_C(0x0203E1F0)
#define FE8_BG1_TILEMAP UINT32_C(0x020234A8)
#define FE8_HP_BAR_HOOK UINT32_C(0x080276B4)

#define FE8_UNIT_SIZE UINT32_C(0x48)
#define FE8_BLUE_UNIT_COUNT 62u
#define FE8_RED_UNIT_COUNT 50u
#define FE8_GREEN_UNIT_COUNT 20u
#define FE8_UNIT_STATE_HIDDEN UINT32_C(1u << 0)
#define FE8_UNIT_STATE_DEAD UINT32_C(1u << 2)
#define FE8_UNIT_STATE_NOT_DEPLOYED UINT32_C(1u << 3)
#define FE8_UNIT_STATE_FOG_HIDDEN UINT32_C(1u << 9)
#define FE8_CHARACTER_ATTRIBUTES_OFFSET UINT32_C(0x28)
#define FE8_CLASS_ATTRIBUTES_OFFSET UINT32_C(0x28)
#define FE8_CONVOY_ITEMS UINT32_C(0x0203A81C)
#define SACRED_ECHOES_CONVOY_ITEMS UINT32_C(0x0203B200)

#define FE8_PROFILE_COMMON \
    FE8_HEADER_TITLE, FE8_HEADER_GAME_CODE, FE8_HEADER_MAKER_CODE, 0, 0x9D, \
    FE8_BM_STATE, FE8_PLAY_STATE, FE8_MAP_SIZE, FE8_MAP_UNIT, FE8_MAP_TERRAIN, \
    FE8_MAP_MOVEMENT, FE8_MAP_RANGE, FE8_MAP_FOG, FE8_MAP_HIDDEN, FE8_MAP_OTHER, \
    FE8_MAP_BASE_TILES, FE8_TILESET_CONFIG, \
    FE8_BLUE_UNITS, FE8_RED_UNITS, FE8_GREEN_UNITS, FE8_ACTIVE_UNIT, \
    FE8_SMS_HANDLE_ARRAY, FE8_MAP_ANIMATION_STATE, FE8_BG1_TILEMAP

#define FE8_INVENTORY_LAYOUT(capacity, immovable_attributes) { \
    UINT32_C(0x0800A2A0), UINT32_C(0x080006DC), UINT32_C(0x080006E0), \
    UINT32_C(0x08809B10), UINT32_C(0x08005524), UINT32_C(0x0803159C), capacity, \
    immovable_attributes \
}

static const Fe8Profile s_fe8u_profile = {
    FE8_PROFILE_COMMON, FE8_CONVOY_ITEMS,
    "Fire Emblem 8 (FE8U)", "FIREEMBLEM8",
    FE8_INVENTORY_LAYOUT(100, 0),
};

static const Fe8Profile s_sacred_echoes_profile = {
    FE8_PROFILE_COMMON, SACRED_ECHOES_CONVOY_ITEMS,
    "Sacred Echoes", "FIREEMBLEM2E",
    /* Sacred Echoes spells are learned abilities represented as magic/staff
       items. Native inventory management never permits transferring them. */
    FE8_INVENTORY_LAYOUT(200, UINT32_C(0x00000006)),
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

static bool valid_data(uint32_t address, uint32_t size) {
    return valid_range(address, size, FE8_EWRAM_START, FE8_EWRAM_END) ||
        valid_range(address, size, FE8_IWRAM_START, FE8_IWRAM_END) ||
        valid_range(address, size, FE8_ROM_START, FE8_ROM_END);
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

static bool combat_panel_is_active(
    const Fe8MemoryReader *memory, const Fe8Profile *profile, uint8_t input_lock) {
    unsigned actor_count;
    unsigned actor;
    unsigned matching_tiles = 0;
    if (input_lock == 0 ||
            !valid_ewram(profile->map_animation_state, 0x64) ||
            !valid_ewram(profile->bg1_tilemap, 32 * 32 * 2))
        return false;
    actor_count = read8(memory, profile->map_animation_state + 0x5E);
    if (actor_count == 0 || actor_count > 2)
        return false;
    for (actor = 0; actor < actor_count; ++actor) {
        uint32_t actor_state = profile->map_animation_state + actor * 0x14;
        unsigned panel_x = read8(memory, actor_state + 0x10);
        unsigned panel_y = read8(memory, actor_state + 0x11);
        unsigned y;
        if (panel_x >= 30 || panel_y >= 20 ||
                !valid_pointer(read32(memory, actor_state)))
            return false;
        for (y = panel_y; y < panel_y + 4 && y < 20; ++y) {
            unsigned x;
            for (x = panel_x; x < panel_x + 10 && x < 30; ++x) {
                uint16_t entry = read16(memory,
                    profile->bg1_tilemap + (y * 32 + x) * 2);
                unsigned palette_bank = entry >> 12;
                unsigned tile = entry & 0x3FF;
                if (tile != 0 && palette_bank == actor + 1)
                    ++matching_tiles;
            }
        }
    }
    return matching_tiles >= actor_count * 4;
}

static bool map_hp_bar_patch_present(const Fe8MemoryReader *memory) {
    uint32_t target;
    /* Standard FE8 map-HP-bars installers replace this PutUnitSpriteIconsOam
     * instruction with `ldr r3, [pc]; bx r3; .word hook | 1`. */
    if (read8(memory, FE8_HP_BAR_HOOK) != 0x00 ||
            read8(memory, FE8_HP_BAR_HOOK + 1) != 0x4B ||
            read8(memory, FE8_HP_BAR_HOOK + 2) != 0x18 ||
            read8(memory, FE8_HP_BAR_HOOK + 3) != 0x47)
        return false;
    target = read32(memory, FE8_HP_BAR_HOOK + 4);
    return (target & 1) != 0 && valid_pointer(target & ~UINT32_C(1));
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

const Fe8Profile *fe8_profile_for_rom(const Fe8MemoryReader *memory) {
    if (valid_reader(memory) && read_header_bytes(memory, FE8_HEADER_TITLE,
            s_sacred_echoes_profile.rom_title_match, 12))
        return &s_sacred_echoes_profile;
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
        uint32_t character = read32(memory, address);
        uint32_t class_data = read32(memory, address + 4);
        uint32_t attributes = 0;
        int8_t x = (int8_t)read8(memory, address + 0x10);
        int8_t y = (int8_t)read8(memory, address + 0x11);
        if (!valid_pointer(character) || (state & (FE8_UNIT_STATE_HIDDEN | FE8_UNIT_STATE_DEAD |
            FE8_UNIT_STATE_NOT_DEPLOYED | FE8_UNIT_STATE_FOG_HIDDEN)) != 0 ||
            x < 0 || y < 0 || x >= (int8_t)width || y >= (int8_t)height)
            continue;
        if (valid_data(character, FE8_CHARACTER_ATTRIBUTES_OFFSET + 4))
            attributes |= read32(memory,
                character + FE8_CHARACTER_ATTRIBUTES_OFFSET);
        if (valid_data(class_data, FE8_CLASS_ATTRIBUTES_OFFSET + 4))
            attributes |= read32(memory,
                class_data + FE8_CLASS_ATTRIBUTES_OFFSET);
        snapshot->visible_units[snapshot->visible_unit_count++] = (Fe8VisibleUnit){
            read8(memory, address + 0x0B), faction, x, y, state, attributes,
            read32(memory, address + 0x3C),
            read8(memory, address + 0x12), read8(memory, address + 0x13)};
    }
}

static void extract_map_sprites(
    const Fe8MemoryReader *memory, const Fe8Profile *profile,
    uint16_t width, uint16_t height, Fe8Snapshot *snapshot) {
    enum { SMS_HANDLE_SIZE = 12, SMS_HANDLE_COUNT = FE8_MAX_MAP_SPRITES + 1 };
    uint32_t base = profile->sms_handle_array;
    uint32_t address;
    uint32_t visited[FE8_MAX_MAP_SPRITES];
    unsigned visited_count = 0;
    if (!valid_ewram(base, SMS_HANDLE_SIZE * SMS_HANDLE_COUNT))
        return;
    address = read32(memory, base);
    if (address == 0) {
        snapshot->flags |= FE8_SNAPSHOT_MAP_SPRITES;
        return;
    }
    while (address != 0 && visited_count < FE8_MAX_MAP_SPRITES) {
        int16_t x;
        int16_t y;
        unsigned i;
        if (address < base + SMS_HANDLE_SIZE ||
                address > base + SMS_HANDLE_SIZE * (SMS_HANDLE_COUNT - 1) ||
                (address - base) % SMS_HANDLE_SIZE != 0)
            return;
        for (i = 0; i < visited_count; ++i)
            if (visited[i] == address)
                return;
        visited[visited_count++] = address;
        x = (int16_t)read16(memory, address + 4);
        y = (int16_t)read16(memory, address + 6);
        if (x >= -32 && y >= -32 && x <= (int)width * 16 + 32 &&
                y <= (int)height * 16 + 32) {
            Fe8VisibleMapSprite *sprite =
                &snapshot->map_sprites[snapshot->map_sprite_count++];
            sprite->x_display = x;
            sprite->y_display = y;
            sprite->oam2 = read16(memory, address + 8);
            sprite->config = read8(memory, address + 0x0B);
        }
        address = read32(memory, address);
    }
    if (address == 0)
        snapshot->flags |= FE8_SNAPSHOT_MAP_SPRITES;
    else
        snapshot->map_sprite_count = 0;
}

bool fe8_extract_live_state(
    const Fe8MemoryReader *memory, const Fe8Profile *profile, Fe8LiveState *state) {
    uint16_t width;
    uint16_t height;
    if (!valid_reader(memory) || !profile || !state ||
            !valid_ewram(profile->bm_state, 0x2C) ||
            !valid_ewram(profile->play_state, 0x14) ||
            !valid_ewram(profile->map_size, 4))
        return false;
    memset(state, 0, sizeof(*state));
    width = read16(memory, profile->map_size);
    height = read16(memory, profile->map_size + 2);
    if (width == 0 || height == 0 || width > FE8_MAX_MAP_WIDTH || height > FE8_MAX_MAP_HEIGHT)
        return false;
    state->map_width = width;
    state->map_height = height;
    state->camera_x = (int16_t)read16(memory, profile->bm_state + 0x0C);
    state->camera_y = (int16_t)read16(memory, profile->bm_state + 0x0E);
    state->camera_max_x = (int16_t)read16(memory, profile->bm_state + 0x28);
    state->camera_max_y = (int16_t)read16(memory, profile->bm_state + 0x2A);
    state->cursor_x = (uint8_t)read16(memory, profile->bm_state + 0x14);
    state->cursor_y = (uint8_t)read16(memory, profile->bm_state + 0x16);
    state->cursor_target_x = (int16_t)read16(memory, profile->bm_state + 0x1C);
    state->cursor_target_y = (int16_t)read16(memory, profile->bm_state + 0x1E);
    state->cursor_display_x = (int16_t)read16(memory, profile->bm_state + 0x20);
    state->cursor_display_y = (int16_t)read16(memory, profile->bm_state + 0x22);
    state->active_unit_address = read32(memory, profile->active_unit);
    state->game_state_bits = read8(memory, profile->bm_state + 0x04);
    state->input_lock = read8(memory, profile->bm_state + 0x01);
    state->chapter = read8(memory, profile->play_state + 0x0E);
    state->phase = read8(memory, profile->play_state + 0x0F);
    state->combat_panel_active = combat_panel_is_active(
        memory, profile, state->input_lock);
    if (state->active_unit_address != 0 &&
            !valid_ewram(state->active_unit_address, FE8_UNIT_SIZE))
        return false;
    return state->cursor_x < width && state->cursor_y < height &&
        state->cursor_target_x >= 0 && state->cursor_target_y >= 0 &&
        state->cursor_display_x >= 0 && state->cursor_display_y >= 0 &&
        state->cursor_target_x <= (int)(width - 1) * 16 &&
        state->cursor_target_y <= (int)(height - 1) * 16 &&
        state->cursor_display_x <= (int)(width - 1) * 16 &&
        state->cursor_display_y <= (int)(height - 1) * 16;
}

bool fe8_extract_snapshot(
    const Fe8MemoryReader *memory, const Fe8Profile *profile, Fe8Snapshot *snapshot) {
    Fe8LiveState live;
    uint16_t width;
    uint16_t height;
    uint32_t map_handles[7];
    uint8_t *map_destinations[7];
    uint32_t map_flags[7];
    uint32_t i;
    if (!snapshot || !fe8_extract_live_state(memory, profile, &live))
        return false;
    memset(snapshot, 0, sizeof(*snapshot));
    if (map_hp_bar_patch_present(memory))
        snapshot->flags |= FE8_SNAPSHOT_HP_BARS;
    width = live.map_width;
    height = live.map_height;
#define COPY_LIVE(field) snapshot->field = live.field
    COPY_LIVE(map_width); COPY_LIVE(map_height);
    COPY_LIVE(camera_x); COPY_LIVE(camera_y);
    COPY_LIVE(camera_max_x); COPY_LIVE(camera_max_y);
    COPY_LIVE(cursor_x); COPY_LIVE(cursor_y);
    COPY_LIVE(cursor_target_x); COPY_LIVE(cursor_target_y);
    COPY_LIVE(cursor_display_x); COPY_LIVE(cursor_display_y);
    COPY_LIVE(active_unit_address); COPY_LIVE(game_state_bits);
    COPY_LIVE(input_lock); COPY_LIVE(chapter); COPY_LIVE(phase);
    COPY_LIVE(combat_panel_active);
#undef COPY_LIVE

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
    append_units(memory, profile->red_units, FE8_RED_UNIT_COUNT, 0x80, width, height, snapshot);
    append_units(memory, profile->green_units, FE8_GREEN_UNIT_COUNT, 0x40, width, height, snapshot);
    if (snapshot->visible_unit_count != 0)
        snapshot->flags |= FE8_SNAPSHOT_UNITS;
    extract_map_sprites(memory, profile, width, height, snapshot);
    return true;
}
