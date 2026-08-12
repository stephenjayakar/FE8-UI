#ifndef FE8_PROFILE_H
#define FE8_PROFILE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t (*Fe8Read8)(void *context, uint32_t address);

typedef struct Fe8MemoryReader {
    void *context;
    Fe8Read8 read8;
} Fe8MemoryReader;

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
    uint32_t blue_units;
    uint32_t red_units;
    uint32_t green_units;
} Fe8Profile;

enum {
    FE8_MAX_MAP_WIDTH = 64,
    FE8_MAX_MAP_HEIGHT = 64,
    FE8_MAX_MAP_CELLS = FE8_MAX_MAP_WIDTH * FE8_MAX_MAP_HEIGHT,
    FE8_MAX_VISIBLE_UNITS = 128,
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
};

typedef struct Fe8VisibleUnit {
    uint8_t unit_id;
    uint8_t faction;
    int8_t x;
    int8_t y;
    uint32_t state;
} Fe8VisibleUnit;

typedef struct Fe8Snapshot {
    uint16_t map_width;
    uint16_t map_height;
    int16_t camera_x;
    int16_t camera_y;
    int16_t camera_max_x;
    int16_t camera_max_y;
    uint8_t cursor_x;
    uint8_t cursor_y;
    uint8_t chapter;
    uint8_t phase;
    uint32_t flags;

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
} Fe8Snapshot;

/* Returns the fixed profile for the unmodified FE8U retail executable. */
const Fe8Profile *fe8u_profile(void);

/* Checks the GBA header identity without writing to emulated memory. */
bool fe8_detect_retail_fe8u(const Fe8MemoryReader *memory);

/* Extracts a coherent, bounds-checked battle-map snapshot. */
bool fe8_extract_snapshot(
    const Fe8MemoryReader *memory,
    const Fe8Profile *profile,
    Fe8Snapshot *snapshot);

#ifdef __cplusplus
}
#endif

#endif
