#ifndef FE8_PREBATTLE_INVENTORY_H
#define FE8_PREBATTLE_INVENTORY_H

#include "fe8_profile.h"
#include "fe8_catalog.h"

#include <stdbool.h>
#include <stdint.h>

enum {
    FE8_INVENTORY_UNIT_CAPACITY = 62,
    FE8_INVENTORY_ITEM_SLOTS = 5,
    FE8_INVENTORY_WEAPON_TYPES = 8,
    FE8_INVENTORY_DESCRIPTION_SIZE = 256,
    FE8_SUPPLY_RETAIL_CAPACITY = 100,
    FE8_SUPPLY_MAX_CAPACITY = 200,
    FE8_MAP_SPRITE_MAX_WIDTH = 32,
    FE8_MAP_SPRITE_MAX_HEIGHT = 32,
    FE8_MAP_SPRITE_PALETTE_SIZE = 16,
};

typedef void (*Fe8Write8)(void *context, uint32_t address, uint8_t value);

typedef struct Fe8MemoryWriter {
    void *context;
    Fe8Write8 write8;
} Fe8MemoryWriter;

typedef enum Fe8UnitStat {
    FE8_STAT_POWER, FE8_STAT_SKILL, FE8_STAT_SPEED, FE8_STAT_LUCK,
    FE8_STAT_DEFENSE, FE8_STAT_RESISTANCE, FE8_STAT_CONSTITUTION,
    FE8_STAT_MOVEMENT, FE8_STAT_MAX_HP, FE8_STAT_COUNT,
} Fe8UnitStat;

typedef struct Fe8InventoryUnit {
    uint32_t address;
    uint8_t character_id;
    uint8_t class_id;
    uint8_t level;
    uint8_t exp;
    uint8_t hp;
    uint8_t max_hp;
    uint8_t power;
    uint8_t skill;
    uint8_t speed;
    uint8_t luck;
    uint8_t defense;
    uint8_t resistance;
    uint8_t constitution;
    uint8_t movement;
    /* Base fields above are never overwritten by derived values. Totals come
       from the ROM's unit-stat getters, not a sum of item description text. */
    int16_t effective_stats[FE8_STAT_COUNT];
    bool effective_stats_valid;
    uint8_t ranks[FE8_INVENTORY_WEAPON_TYPES];
    uint8_t status;
    uint32_t attributes;
    uint16_t portrait_id;
    char name[28];
    char class_name[28];
    char description[FE8_INVENTORY_DESCRIPTION_SIZE];
    char class_description[FE8_INVENTORY_DESCRIPTION_SIZE];
    uint8_t portrait[FE8_PORTRAIT_WIDTH * FE8_PORTRAIT_HEIGHT];
    uint32_t portrait_palette[FE8_PORTRAIT_PALETTE_SIZE];
    bool portrait_valid;
    /* A snapshot of the standing map sprite currently owned by FE8. Keeping
       pixels here makes the desktop inventory independent of later VRAM churn. */
    uint8_t map_sprite[FE8_MAP_SPRITE_MAX_WIDTH * FE8_MAP_SPRITE_MAX_HEIGHT];
    uint32_t map_sprite_palette[FE8_MAP_SPRITE_PALETTE_SIZE];
    uint8_t map_sprite_width;
    uint8_t map_sprite_height;
    bool map_sprite_valid;
    uint16_t items[FE8_INVENTORY_ITEM_SLOTS];
    Fe8ItemInfo item_info[FE8_INVENTORY_ITEM_SLOTS];
} Fe8InventoryUnit;

typedef struct Fe8InventorySnapshot {
    uint8_t chapter;
    uint8_t unit_count;
    uint16_t supply_count;
    uint16_t supply_display_count;
    uint16_t supply_capacity;
    uint16_t first_empty_supply;
    uint32_t supply_address;
    bool prebattle;
    Fe8InventoryUnit units[FE8_INVENTORY_UNIT_CAPACITY];
    uint16_t supply[FE8_SUPPLY_MAX_CAPACITY];
    Fe8ItemInfo supply_info[FE8_SUPPLY_MAX_CAPACITY];
    uint16_t supply_display_slots[FE8_SUPPLY_MAX_CAPACITY];
} Fe8InventorySnapshot;

typedef enum Fe8InventoryEndpointKind {
    FE8_INVENTORY_ENDPOINT_UNIT,
    FE8_INVENTORY_ENDPOINT_SUPPLY,
} Fe8InventoryEndpointKind;

typedef struct Fe8InventoryEndpoint {
    Fe8InventoryEndpointKind kind;
    uint32_t unit_address;
    unsigned slot;
} Fe8InventoryEndpoint;

typedef enum Fe8InventoryUseState {
    FE8_INVENTORY_USE_ITEM,
    FE8_INVENTORY_USE_READY,
    FE8_INVENTORY_USE_RANK,
    FE8_INVENTORY_USE_LOCKED,
    FE8_INVENTORY_USE_STATUS,
    FE8_INVENTORY_USE_UNKNOWN,
} Fe8InventoryUseState;

int fe8_inventory_stat_base(const Fe8InventoryUnit *unit, Fe8UnitStat stat);
int fe8_inventory_stat_value(const Fe8InventoryUnit *unit, Fe8UnitStat stat,
    bool base_only);

bool fe8_prebattle_inventory_active(
    const Fe8MemoryReader *memory, const Fe8Profile *profile);

bool fe8_extract_prebattle_inventory(
    const Fe8MemoryReader *memory, const Fe8Profile *profile,
    const Fe8Catalog *catalog, Fe8InventorySnapshot *snapshot);

bool fe8_inventory_management_available(
    const Fe8MemoryReader *memory, const Fe8Profile *profile);

Fe8InventoryUseState fe8_inventory_item_use_state(
    const Fe8InventoryUnit *unit, const Fe8ItemInfo *item);

bool fe8_swap_inventory_endpoints(
    const Fe8MemoryReader *memory, const Fe8MemoryWriter *writer,
    const Fe8Profile *profile, Fe8InventoryEndpoint first,
    uint16_t expected_first, Fe8InventoryEndpoint second,
    uint16_t expected_second);

bool fe8_swap_prebattle_items(
    const Fe8MemoryReader *memory, const Fe8MemoryWriter *writer,
    const Fe8Profile *profile, uint32_t first_unit, unsigned first_slot,
    uint16_t expected_first, uint32_t second_unit, unsigned second_slot,
    uint16_t expected_second);

#endif
