#ifndef FE8_CATALOG_H
#define FE8_CATALOG_H

#include "fe8_profile.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    /* FE8's standard menu portrait layout (PutFace80x72). */
    FE8_PORTRAIT_WIDTH = 80,
    FE8_PORTRAIT_HEIGHT = 72,
    FE8_PORTRAIT_PALETTE_SIZE = 16,
};

typedef struct Fe8Catalog {
    uint32_t message_table;
    uint32_t huffman_root;
    uint32_t huffman_table;
    uint32_t item_table;
    uint32_t portrait_table_bias;
    uint32_t immovable_item_attributes;
    uint32_t weapon_lock_table;
    bool valid;
} Fe8Catalog;

typedef enum Fe8ItemLockKind {
    FE8_ITEM_LOCK_NONE,
    FE8_ITEM_LOCK_CHARACTER,
    FE8_ITEM_LOCK_CLASS,
    FE8_ITEM_LOCK_UNKNOWN,
} Fe8ItemLockKind;

typedef struct Fe8ItemInfo {
    uint8_t id;
    uint8_t uses;
    uint8_t max_uses;
    uint8_t weapon_type;
    uint8_t might;
    uint8_t hit;
    uint8_t weight;
    uint8_t crit;
    uint8_t min_range;
    uint8_t max_range;
    uint8_t weapon_rank;
    uint32_t attributes;
    bool movable;
    Fe8ItemLockKind lock_kind;
    uint8_t lock_ids[32]; /* 256-bit ROM-backed whitelist, not display names. */
    char name[28];
    char description[192];
} Fe8ItemInfo;

bool fe8_catalog_init(const Fe8MemoryReader *memory, const Fe8Profile *profile,
    Fe8Catalog *catalog);
bool fe8_catalog_text(const Fe8MemoryReader *memory, const Fe8Catalog *catalog,
    uint16_t text_id, char *output, size_t output_size);
bool fe8_catalog_item(const Fe8MemoryReader *memory, const Fe8Catalog *catalog,
    uint16_t encoded_item, Fe8ItemInfo *item);
bool fe8_catalog_portrait(const Fe8MemoryReader *memory, const Fe8Catalog *catalog,
    uint16_t portrait_id,
    uint8_t pixels[FE8_PORTRAIT_WIDTH * FE8_PORTRAIT_HEIGHT],
    uint32_t palette[FE8_PORTRAIT_PALETTE_SIZE]);

#endif
