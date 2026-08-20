#ifndef FE8_PROFILE_H
#define FE8_PROFILE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef FE8_READ8_DEFINED
#define FE8_READ8_DEFINED
typedef uint8_t (*Fe8Read8)(void *context, uint32_t address);
#endif

typedef struct Fe8MemoryReader {
    void *context;
    Fe8Read8 read8;
} Fe8MemoryReader;

typedef struct Fe8InventoryLayout {
    uint32_t message_table_literal;
    uint32_t huffman_root_literal;
    uint32_t huffman_table_literal;
    uint32_t item_table;
    uint32_t portrait_table_literal;
    uint32_t get_convoy_items;
    uint16_t convoy_capacity;
    uint32_t immovable_item_attributes;
} Fe8InventoryLayout;

typedef struct Fe8Profile {
    uint32_t rom_title;
    uint32_t rom_game_code;
    uint32_t rom_maker_code;
    uint8_t rom_version;
    uint8_t rom_header_checksum;

    uint32_t bm_state;
    uint32_t play_state;
    uint32_t map_size;
    uint32_t map_unit;
    uint32_t map_terrain;
    uint32_t map_movement;
    uint32_t map_range;
    uint32_t map_fog;
    uint32_t map_hidden;
    uint32_t map_other;
    uint32_t map_base_tiles;
    uint32_t tileset_config;
    uint32_t blue_units;
    uint32_t red_units;
    uint32_t green_units;
    uint32_t active_unit;
    uint32_t sms_handle_array;
    uint32_t map_animation_state;
    uint32_t bg1_tilemap;
    uint32_t convoy_items;
    const char *profile_name;
    const char *rom_title_match;
    Fe8InventoryLayout inventory;
} Fe8Profile;

enum {
    FE8_MAX_MAP_WIDTH = 64,
    FE8_MAX_MAP_HEIGHT = 64,
    FE8_MAX_MAP_CELLS = FE8_MAX_MAP_WIDTH * FE8_MAX_MAP_HEIGHT,
    FE8_MAX_VISIBLE_UNITS = 128,
    FE8_MAX_MAP_SPRITES = 99,
};

enum Fe8SnapshotFlags {
    FE8_SNAPSHOT_TERRAIN = 1u << 0,
    FE8_SNAPSHOT_UNIT_MAP = 1u << 1,
    FE8_SNAPSHOT_MOVEMENT = 1u << 2,
    FE8_SNAPSHOT_RANGE = 1u << 3,
    FE8_SNAPSHOT_FOG = 1u << 4,
    FE8_SNAPSHOT_HIDDEN = 1u << 5,
    FE8_SNAPSHOT_OTHER = 1u << 6,
    FE8_SNAPSHOT_UNITS = 1u << 7,
    FE8_SNAPSHOT_MAP_SPRITES = 1u << 8,
    FE8_SNAPSHOT_HP_BARS = 1u << 9,
};

typedef struct Fe8VisibleUnit {
    uint8_t unit_id;
    uint8_t faction;
    int8_t x;
    int8_t y;
    uint32_t state;
    uint32_t attributes;
    uint32_t map_sprite_handle;
    uint8_t max_hp;
    uint8_t current_hp;
} Fe8VisibleUnit;

typedef struct Fe8VisibleMapSprite {
    int16_t x_display;
    int16_t y_display;
    uint16_t oam2;
    uint8_t config;
} Fe8VisibleMapSprite;

typedef struct Fe8LiveState {
    uint16_t map_width;
    uint16_t map_height;
    int16_t camera_x;
    int16_t camera_y;
    int16_t camera_max_x;
    int16_t camera_max_y;
    uint8_t cursor_x;
    uint8_t cursor_y;
    int16_t cursor_target_x;
    int16_t cursor_target_y;
    int16_t cursor_display_x;
    int16_t cursor_display_y;
    uint32_t active_unit_address;
    uint8_t game_state_bits;
    uint8_t input_lock;
    uint8_t chapter;
    uint8_t phase;
    bool combat_panel_active;
} Fe8LiveState;

typedef struct Fe8Snapshot {
    uint16_t map_width;
    uint16_t map_height;
    int16_t camera_x;
    int16_t camera_y;
    int16_t camera_max_x;
    int16_t camera_max_y;
    uint8_t cursor_x;
    uint8_t cursor_y;
    int16_t cursor_target_x;
    int16_t cursor_target_y;
    int16_t cursor_display_x;
    int16_t cursor_display_y;
    uint32_t active_unit_address;
    uint8_t game_state_bits;
    uint8_t input_lock;
    uint8_t chapter;
    uint8_t phase;
    bool combat_panel_active;
    uint32_t flags;
    uint32_t base_tile_rows;
    uint32_t fog_rows;

    /* Flattened row-major data; only map_width * map_height is populated. */
    uint8_t terrain[FE8_MAX_MAP_CELLS];
    uint8_t unit_map[FE8_MAX_MAP_CELLS];
    uint8_t movement[FE8_MAX_MAP_CELLS];
    uint8_t range[FE8_MAX_MAP_CELLS];
    uint8_t fog[FE8_MAX_MAP_CELLS];
    uint8_t hidden[FE8_MAX_MAP_CELLS];
    uint8_t other[FE8_MAX_MAP_CELLS];

    uint16_t visible_unit_count;
    Fe8VisibleUnit visible_units[FE8_MAX_VISIBLE_UNITS];
    uint16_t map_sprite_count;
    Fe8VisibleMapSprite map_sprites[FE8_MAX_MAP_SPRITES];
} Fe8Snapshot;

/* Returns the fixed profile for the unmodified FE8U retail executable. */
const Fe8Profile *fe8u_profile(void);
const Fe8Profile *fe8_profile_for_rom(const Fe8MemoryReader *memory);

/* Checks the GBA header identity without writing to emulated memory. */
bool fe8_detect_retail_fe8u(const Fe8MemoryReader *memory);

/* Header-family check for FE8U hacks; structural validation is still required. */
bool fe8_detect_fe8u_family(const Fe8MemoryReader *memory);

bool fe8_extract_live_state(
    const Fe8MemoryReader *memory,
    const Fe8Profile *profile,
    Fe8LiveState *state);

/* Extracts a coherent, bounds-checked battle-map snapshot. */
bool fe8_extract_snapshot(
    const Fe8MemoryReader *memory,
    const Fe8Profile *profile,
    Fe8Snapshot *snapshot);

#ifdef __cplusplus
}
#endif

#endif
