#include "fe8_catalog.h"

#include <string.h>

#define ROM_START UINT32_C(0x08000000)
#define ROM_END UINT32_C(0x0A000000)
#define ITEM_DATA_SIZE UINT32_C(0x24)

static uint8_t r8(const Fe8MemoryReader *m, uint32_t a) { return m->read8(m->context, a); }
static uint16_t r16(const Fe8MemoryReader *m, uint32_t a) {
    return (uint16_t)(r8(m, a) | ((uint16_t)r8(m, a + 1) << 8));
}
static uint32_t r32(const Fe8MemoryReader *m, uint32_t a) {
    return (uint32_t)r16(m, a) | ((uint32_t)r16(m, a + 2) << 16);
}
static bool rom(uint32_t a, uint32_t n) {
    return a >= ROM_START && n <= ROM_END - ROM_START && a <= ROM_END - n;
}

bool fe8_catalog_init(const Fe8MemoryReader *memory, const Fe8Profile *profile,
    Fe8Catalog *catalog) {
    uint32_t root_pointer;
    if (!memory || !memory->read8 || !profile || !catalog)
        return false;
    memset(catalog, 0, sizeof(*catalog));
    root_pointer = r32(memory, profile->inventory.huffman_root_literal);
    catalog->message_table = r32(memory, profile->inventory.message_table_literal);
    catalog->huffman_root = rom(root_pointer, 4) ? r32(memory, root_pointer) : 0;
    catalog->huffman_table = r32(memory, profile->inventory.huffman_table_literal);
    catalog->item_table = profile->inventory.item_table;
    catalog->immovable_item_attributes = profile->inventory.immovable_item_attributes;
    catalog->portrait_table_bias = r32(memory, profile->inventory.portrait_table_literal);
    catalog->valid = rom(catalog->message_table, 4) &&
        rom(catalog->huffman_root, 4) && rom(catalog->huffman_table, 4) &&
        rom(catalog->item_table, ITEM_DATA_SIZE * 2) &&
        rom(catalog->portrait_table_bias, 28);
    return catalog->valid;
}

static void append_character(char *out, size_t cap, size_t *length, uint8_t c) {
    if (c == 0)
        return;
    if (c >= 0x20 && c < 0x7F) {
        if (*length + 1 < cap)
            out[(*length)++] = (char)c;
    } else if (c == 1 || c == 2 || c == 3 || c == 4 || c == 5 || c == 6 || c == 7) {
        /* Normalize FE line/page controls to a separator so adjacent help-text
         * fragments do not run together in the desktop layout. */
        if (*length && out[*length - 1] != ' ' && *length + 1 < cap)
            out[(*length)++] = ' ';
    } else if (*length && out[*length - 1] != ' ' && *length + 1 < cap) {
        out[(*length)++] = ' ';
    }
}

bool fe8_catalog_text(const Fe8MemoryReader *memory, const Fe8Catalog *catalog,
    uint16_t text_id, char *output, size_t output_size) {
    uint32_t raw;
    uint32_t input;
    size_t length = 0;
    unsigned guard;
    if (!memory || !memory->read8 || !catalog || !catalog->valid || !output || !output_size)
        return false;
    output[0] = '\0';
    raw = r32(memory, catalog->message_table + (uint32_t)text_id * 4);
    input = raw & UINT32_C(0x7FFFFFFF);
    if (!rom(input, 1))
        return false;
    if (raw & UINT32_C(0x80000000)) {
        for (guard = 0; guard < 255 && rom(input + guard, 1); ++guard) {
            uint8_t c = r8(memory, input + guard);
            if (!c)
                break;
            append_character(output, output_size, &length, c);
        }
    } else {
        uint32_t node = catalog->huffman_root;
        unsigned bit = 0;
        for (guard = 0; guard < 2048 && rom(input + bit / 8, 1); ++guard, ++bit) {
            unsigned branch = (r8(memory, input + bit / 8) >> (bit & 7)) & 1;
            uint16_t child = r16(memory, node + branch * 2);
            uint32_t value;
            node = catalog->huffman_table + (uint32_t)child * 4;
            if (!rom(node, 4))
                break;
            value = r32(memory, node);
            if (value & UINT32_C(0x80000000)) {
                uint8_t first = (uint8_t)value;
                uint8_t second = (uint8_t)(value >> 8);
                if (!first)
                    break;
                append_character(output, output_size, &length, first);
                /* A zero second byte is a one-byte Huffman symbol. Only a
                 * zero first byte terminates the message. */
                if (second)
                    append_character(output, output_size, &length, second);
                node = catalog->huffman_root;
            }
        }
    }
    while (length && output[length - 1] == ' ')
        --length;
    output[length] = '\0';
    return length != 0;
}

bool fe8_catalog_item(const Fe8MemoryReader *memory, const Fe8Catalog *catalog,
    uint16_t encoded_item, Fe8ItemInfo *item) {
    uint32_t record;
    uint16_t text_id;
    uint16_t description_id;
    uint8_t range;
    if (!item)
        return false;
    memset(item, 0, sizeof(*item));
    item->id = (uint8_t)encoded_item;
    item->uses = (uint8_t)(encoded_item >> 8);
    if (!item->id || !catalog || !catalog->valid)
        return false;
    record = catalog->item_table + (uint32_t)item->id * ITEM_DATA_SIZE;
    if (!rom(record, ITEM_DATA_SIZE) || r8(memory, record + 6) != item->id)
        return false;
    text_id = r16(memory, record);
    description_id = r16(memory, record + 2);
    item->weapon_type = r8(memory, record + 7);
    item->attributes = r32(memory, record + 8);
    item->movable = (item->attributes & catalog->immovable_item_attributes) == 0;
    item->max_uses = r8(memory, record + 0x14);
    item->might = r8(memory, record + 0x15);
    item->hit = r8(memory, record + 0x16);
    item->weight = r8(memory, record + 0x17);
    item->crit = r8(memory, record + 0x18);
    range = r8(memory, record + 0x19);
    item->min_range = range >> 4;
    item->max_range = range & 0x0F;
    item->weapon_rank = r8(memory, record + 0x1C);
    if (!fe8_catalog_text(memory, catalog, text_id, item->name, sizeof(item->name)))
        strcpy(item->name, "Unknown item");
    fe8_catalog_text(memory, catalog, description_id,
        item->description, sizeof(item->description));
    return true;
}

static bool lz77(const Fe8MemoryReader *memory, uint32_t source,
    uint8_t *output, size_t capacity) {
    uint32_t header;
    size_t length;
    size_t out = 0;
    if (!rom(source, 4) || r8(memory, source) != 0x10)
        return false;
    header = r32(memory, source);
    length = header >> 8;
    source += 4;
    if (length > capacity)
        return false;
    while (out < length && rom(source, 1)) {
        uint8_t flags = r8(memory, source++);
        unsigned bit;
        for (bit = 0; bit < 8 && out < length; ++bit) {
            if (flags & (0x80u >> bit)) {
                uint16_t token;
                size_t count;
                size_t distance;
                if (!rom(source, 2)) return false;
                token = (uint16_t)((uint16_t)r8(memory, source) << 8 | r8(memory, source + 1));
                source += 2;
                count = (token >> 12) + 3;
                distance = (token & 0x0FFF) + 1;
                if (distance > out) return false;
                while (count-- && out < length) {
                    output[out] = output[out - distance];
                    ++out;
                }
            } else {
                if (!rom(source, 1)) return false;
                output[out++] = r8(memory, source++);
            }
        }
    }
    return out == length;
}

bool fe8_catalog_portrait(const Fe8MemoryReader *memory, const Fe8Catalog *catalog,
    uint16_t portrait_id, uint32_t pixels[FE8_PORTRAIT_WIDTH * FE8_PORTRAIT_HEIGHT]) {
    uint32_t record;
    uint32_t graphics;
    uint32_t palette;
    uint8_t tiles[512];
    unsigned y;
    if (!pixels)
        return false;
    memset(pixels, 0, FE8_PORTRAIT_WIDTH * FE8_PORTRAIT_HEIGHT * sizeof(*pixels));
    if (!catalog || !catalog->valid || !portrait_id)
        return false;
    record = catalog->portrait_table_bias + (uint32_t)portrait_id * 28;
    graphics = r32(memory, record + 4) & UINT32_C(0x7FFFFFFF);
    palette = r32(memory, record + 8) & UINT32_C(0x7FFFFFFF);
    if (!rom(palette, 32) || !lz77(memory, graphics, tiles, sizeof(tiles)))
        return false;
    for (y = 0; y < 32; ++y) {
        unsigned x;
        for (x = 0; x < 32; ++x) {
            unsigned tile = (y / 8) * 4 + x / 8;
            unsigned in_tile = (y & 7) * 8 + (x & 7);
            uint8_t packed = tiles[tile * 32 + in_tile / 2];
            unsigned index = (in_tile & 1) ? packed >> 4 : packed & 0x0F;
            uint16_t color;
            if (!index)
                continue;
            color = r16(memory, palette + index * 2);
            pixels[y * 32 + x] = UINT32_C(0xFF000000) |
                ((uint32_t)((color >> 10) & 31) * 255 / 31) << 16 |
                ((uint32_t)((color >> 5) & 31) * 255 / 31) << 8 |
                (uint32_t)(color & 31) * 255 / 31;
        }
    }
    return true;
}
