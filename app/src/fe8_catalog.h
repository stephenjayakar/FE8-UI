#ifndef FE8_CATALOG_H
#define FE8_CATALOG_H

#include "fe8_profile.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { FE8_PORTRAIT_WIDTH = 32, FE8_PORTRAIT_HEIGHT = 32 };

typedef struct Fe8Catalog {
    uint32_t message_table;
    uint32_t huffman_root;
    uint32_t huffman_table;
    uint32_t item_table;
    uint32_t portrait_table_bias;
    uint32_t immovable_item_attributes;
    bool valid;
} Fe8Catalog;

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
    uint16_t portrait_id, uint32_t pixels[FE8_PORTRAIT_WIDTH * FE8_PORTRAIT_HEIGHT]);

#endif
