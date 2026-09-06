#include "prebattle_inventory.h"

#include <string.h>

#define FE8_EWRAM_START UINT32_C(0x02000000)
#define FE8_EWRAM_END UINT32_C(0x02040000)
#define FE8_ROM_START UINT32_C(0x08000000)
#define FE8_ROM_END UINT32_C(0x0A000000)
#define FE8_UNIT_SIZE UINT32_C(0x48)
#define FE8_UNIT_ITEM_OFFSET UINT32_C(0x1E)
#define FE8_UNIT_RANK_OFFSET UINT32_C(0x28)
#define FE8_UNIT_STATUS_OFFSET UINT32_C(0x30)
#define FE8_UNIT_STATE_DEAD UINT32_C(1u << 2)
#define FE8_BM_FLAG_PREPSCREEN UINT8_C(1u << 4)

#define FE8_ITEM_ATTRIBUTE_WEAPON UINT32_C(1u << 0)
#define FE8_ITEM_ATTRIBUTE_MAGIC UINT32_C(1u << 1)
#define FE8_ITEM_ATTRIBUTE_STAFF UINT32_C(1u << 2)
#define FE8_ITEM_ATTRIBUTE_LOCK_3 UINT32_C(1u << 10)
#define FE8_ITEM_ATTRIBUTE_LOCK_1 UINT32_C(1u << 11)
#define FE8_ITEM_ATTRIBUTE_LOCK_2 UINT32_C(1u << 12)
#define FE8_ITEM_ATTRIBUTE_LOCK_0 UINT32_C(1u << 13)
#define FE8_ITEM_ATTRIBUTE_UNUSABLE UINT32_C(1u << 16)
#define FE8_ITEM_ATTRIBUTE_LOCK_4 UINT32_C(1u << 18)
#define FE8_ITEM_ATTRIBUTE_LOCK_5 UINT32_C(1u << 19)
#define FE8_ITEM_ATTRIBUTE_LOCK_6 UINT32_C(1u << 20)
#define FE8_ITEM_ATTRIBUTE_LOCK_7 UINT32_C(1u << 21)

#define FE8_UNIT_ATTRIBUTE_LOCK_1 UINT32_C(1u << 16)
#define FE8_UNIT_ATTRIBUTE_LOCK_2 UINT32_C(1u << 17)
#define FE8_UNIT_ATTRIBUTE_LOCK_3 UINT32_C(1u << 18)
#define FE8_UNIT_ATTRIBUTE_LOCK_4 UINT32_C(1u << 28)
#define FE8_UNIT_ATTRIBUTE_LOCK_5 UINT32_C(1u << 29)
#define FE8_UNIT_ATTRIBUTE_LOCK_6 UINT32_C(1u << 30)
#define FE8_UNIT_ATTRIBUTE_LOCK_7 UINT32_C(1u << 31)

#define FE8_UNIT_STATUS_SLEEP UINT8_C(2)
#define FE8_UNIT_STATUS_SILENCED UINT8_C(3)
#define FE8_UNIT_STATUS_BERSERK UINT8_C(4)

static bool valid_reader(const Fe8MemoryReader *memory) {
    return memory && memory->read8;
}

static bool valid_range(uint32_t address, uint32_t size, uint32_t begin, uint32_t end) {
    return address >= begin && size <= end - begin && address - begin <= end - begin - size;
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

static uint8_t clamp_stat(int value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return (uint8_t)value;
}

static bool valid_unit_address(const Fe8Profile *profile, uint32_t address) {
    return profile && address >= profile->blue_units &&
        address < profile->blue_units + FE8_INVENTORY_UNIT_CAPACITY * FE8_UNIT_SIZE &&
        (address - profile->blue_units) % FE8_UNIT_SIZE == 0 &&
        valid_range(address, FE8_UNIT_SIZE, FE8_EWRAM_START, FE8_EWRAM_END);
}

static void resolve_supply(const Fe8MemoryReader *memory, const Fe8Profile *profile,
    uint32_t *address, unsigned *capacity) {
    uint32_t accessor = profile->inventory.get_convoy_items;
    uint16_t load = read16(memory, accessor);
    uint16_t branch = read16(memory, accessor + 2);
    *address = profile->convoy_items;
    *capacity = profile->inventory.convoy_capacity;
    if (*capacity == 0 || *capacity > FE8_SUPPLY_MAX_CAPACITY)
        *capacity = FE8_SUPPLY_RETAIL_CAPACITY;
    if ((load & UINT16_C(0xF800)) == UINT16_C(0x4800) && branch == UINT16_C(0x4770)) {
        uint32_t literal = ((accessor + 4) & ~UINT32_C(3)) +
            (uint32_t)(load & 0xFF) * 4;
        uint32_t discovered = read32(memory, literal);
        if (valid_range(discovered, (uint32_t)*capacity * 2,
                FE8_EWRAM_START, FE8_EWRAM_END))
            *address = discovered;
    }
}

bool fe8_prebattle_inventory_active(
    const Fe8MemoryReader *memory, const Fe8Profile *profile) {
    return valid_reader(memory) && profile &&
        valid_range(profile->bm_state, 5, FE8_EWRAM_START, FE8_EWRAM_END) &&
        (read8(memory, profile->bm_state + 4) & FE8_BM_FLAG_PREPSCREEN) != 0;
}

bool fe8_inventory_management_available(
    const Fe8MemoryReader *memory, const Fe8Profile *profile) {
    unsigned index;
    if (!valid_reader(memory) || !profile ||
            !valid_range(profile->blue_units, FE8_INVENTORY_UNIT_CAPACITY * FE8_UNIT_SIZE,
                FE8_EWRAM_START, FE8_EWRAM_END))
        return false;
    /* Explicitly opening the host manager is itself the management context. A
       populated FE8 roster is a safer and broader gate than one native menu bit. */
    for (index = 0; index < FE8_INVENTORY_UNIT_CAPACITY; ++index) {
        uint32_t character = read32(memory, profile->blue_units + index * FE8_UNIT_SIZE);
        if (valid_range(character, 0x34, FE8_ROM_START, FE8_ROM_END))
            return true;
    }
    return false;
}

static bool missing_item_lock(uint32_t item_attributes, uint32_t unit_attributes) {
    static const struct {
        uint32_t item;
        uint32_t unit;
    } locks[] = {
        {FE8_ITEM_ATTRIBUTE_LOCK_1, FE8_UNIT_ATTRIBUTE_LOCK_1},
        {FE8_ITEM_ATTRIBUTE_LOCK_2, FE8_UNIT_ATTRIBUTE_LOCK_2},
        {FE8_ITEM_ATTRIBUTE_LOCK_4, FE8_UNIT_ATTRIBUTE_LOCK_4},
        {FE8_ITEM_ATTRIBUTE_LOCK_5, FE8_UNIT_ATTRIBUTE_LOCK_5},
        {FE8_ITEM_ATTRIBUTE_LOCK_6, FE8_UNIT_ATTRIBUTE_LOCK_6},
        {FE8_ITEM_ATTRIBUTE_LOCK_7, FE8_UNIT_ATTRIBUTE_LOCK_7},
    };
    unsigned index;
    for (index = 0; index < sizeof(locks) / sizeof(locks[0]); ++index) {
        if ((item_attributes & locks[index].item) != 0 &&
                (unit_attributes & locks[index].unit) == 0)
            return true;
    }
    return false;
}

Fe8InventoryUseState fe8_inventory_item_use_state(
    const Fe8InventoryUnit *unit, const Fe8ItemInfo *item) {
    uint32_t attributes;
    bool weapon;
    bool staff;
    if (!unit || !item || !item->id)
        return FE8_INVENTORY_USE_ITEM;
    attributes = item->attributes;
    weapon = (attributes & FE8_ITEM_ATTRIBUTE_WEAPON) != 0;
    staff = (attributes & FE8_ITEM_ATTRIBUTE_STAFF) != 0;
    if (!weapon && !staff)
        return FE8_INVENTORY_USE_ITEM;
    if (item->lock_kind == FE8_ITEM_LOCK_UNKNOWN)
        return FE8_INVENTORY_USE_UNKNOWN;
    if (item->lock_kind == FE8_ITEM_LOCK_CHARACTER || item->lock_kind == FE8_ITEM_LOCK_CLASS) {
        uint8_t id = item->lock_kind == FE8_ITEM_LOCK_CHARACTER ? unit->character_id : unit->class_id;
        if (!id || !(item->lock_ids[id / 8] & (1u << (id % 8))))
            return FE8_INVENTORY_USE_LOCKED;
    }
    if (missing_item_lock(attributes, unit->attributes))
        return FE8_INVENTORY_USE_LOCKED;
    if ((attributes & FE8_ITEM_ATTRIBUTE_LOCK_3) != 0) {
        if ((unit->attributes & FE8_UNIT_ATTRIBUTE_LOCK_3) == 0)
            return FE8_INVENTORY_USE_LOCKED;
        return FE8_INVENTORY_USE_READY;
    }
    /* Lock 0 and the story-controlled unusable bit require game-specific
       predicates. Conservatively present those items as locked. */
    if ((attributes & (FE8_ITEM_ATTRIBUTE_LOCK_0 |
            FE8_ITEM_ATTRIBUTE_UNUSABLE)) != 0)
        return FE8_INVENTORY_USE_LOCKED;
    if (staff && (unit->status == FE8_UNIT_STATUS_SLEEP ||
            unit->status == FE8_UNIT_STATUS_SILENCED ||
            unit->status == FE8_UNIT_STATUS_BERSERK))
        return FE8_INVENTORY_USE_STATUS;
    if (weapon && (attributes & FE8_ITEM_ATTRIBUTE_MAGIC) != 0 &&
            unit->status == FE8_UNIT_STATUS_SILENCED)
        return FE8_INVENTORY_USE_STATUS;
    if (item->weapon_type >= FE8_INVENTORY_WEAPON_TYPES ||
            unit->ranks[item->weapon_type] < item->weapon_rank)
        return FE8_INVENTORY_USE_RANK;
    return FE8_INVENTORY_USE_READY;
}

bool fe8_extract_prebattle_inventory(
    const Fe8MemoryReader *memory, const Fe8Profile *profile,
    const Fe8Catalog *catalog, Fe8InventorySnapshot *snapshot) {
    unsigned index;
    unsigned supply_capacity;
    uint32_t supply_address;
    if (!snapshot || !fe8_inventory_management_available(memory, profile) ||
            !valid_range(profile->blue_units,
                FE8_INVENTORY_UNIT_CAPACITY * FE8_UNIT_SIZE,
                FE8_EWRAM_START, FE8_EWRAM_END))
        return false;
    memset(snapshot, 0, sizeof(*snapshot));
    resolve_supply(memory, profile, &supply_address, &supply_capacity);
    snapshot->supply_address = supply_address;
    snapshot->supply_capacity = (uint16_t)supply_capacity;
    snapshot->first_empty_supply = (uint16_t)supply_capacity;
    snapshot->prebattle = fe8_prebattle_inventory_active(memory, profile);
    snapshot->chapter = read8(memory, profile->play_state + 0x0E);
    for (index = 0; index < FE8_INVENTORY_UNIT_CAPACITY; ++index) {
        uint32_t address = profile->blue_units + index * FE8_UNIT_SIZE;
        uint32_t character = read32(memory, address);
        uint32_t state;
        uint32_t class_data;
        unsigned slot;
        Fe8InventoryUnit *unit;
        if (!valid_range(character, 0x34, FE8_ROM_START, FE8_ROM_END))
            continue;
        state = read32(memory, address + 0x0C);
        if ((state & FE8_UNIT_STATE_DEAD) != 0)
            continue;
        unit = &snapshot->units[snapshot->unit_count++];
        unit->address = address;
        unit->character_id = read8(memory, character + 4);
        unit->class_id = valid_range(class_data, 5, FE8_ROM_START, FE8_ROM_END) ?
            read8(memory, class_data + 4) : 0;
        unit->level = read8(memory, address + 8);
        unit->exp = read8(memory, address + 9);
        unit->max_hp = read8(memory, address + 0x12);
        unit->hp = read8(memory, address + 0x13);
        unit->power = read8(memory, address + 0x14);
        unit->skill = read8(memory, address + 0x15);
        unit->speed = read8(memory, address + 0x16);
        unit->defense = read8(memory, address + 0x17);
        unit->resistance = read8(memory, address + 0x18);
        unit->luck = read8(memory, address + 0x19);
        unit->status = read8(memory, address + FE8_UNIT_STATUS_OFFSET) & 0x0F;
        unit->portrait_id = read16(memory, character + 6);
        unit->attributes = read32(memory, character + 0x28);
        for (slot = 0; slot < FE8_INVENTORY_WEAPON_TYPES; ++slot)
            unit->ranks[slot] = read8(memory, address + FE8_UNIT_RANK_OFFSET + slot);
        class_data = read32(memory, address + 4);
        if (valid_range(class_data, 0x2C, FE8_ROM_START, FE8_ROM_END)) {
            unit->constitution = clamp_stat((int8_t)read8(memory, character + 0x13) +
                (int8_t)read8(memory, class_data + 0x11) +
                (int8_t)read8(memory, address + 0x1A));
            unit->movement = clamp_stat((int8_t)read8(memory, class_data + 0x12) +
                (int8_t)read8(memory, address + 0x1D));
            unit->attributes |= read32(memory, class_data + 0x28);
        }
        if (!fe8_catalog_text(memory, catalog, read16(memory, character),
                unit->name, sizeof(unit->name)))
            strcpy(unit->name, "Unknown unit");
        if (!valid_range(class_data, 4, FE8_ROM_START, FE8_ROM_END) ||
                !fe8_catalog_text(memory, catalog, read16(memory, class_data),
                    unit->class_name, sizeof(unit->class_name)))
            strcpy(unit->class_name, "Unknown class");
        /* CharacterData and ClassData both store their help-text ID at +2.
           Resolve through the ROM's own message table (including hack text),
           never a host-side list of character or class descriptions. */
        if (!read16(memory, character + 2) ||
                !fe8_catalog_text(memory, catalog, read16(memory, character + 2),
                unit->description, sizeof(unit->description)))
            unit->description[0] = '\0';
        if (valid_range(class_data, 4, FE8_ROM_START, FE8_ROM_END) &&
                (!read16(memory, class_data + 2) ||
                 !fe8_catalog_text(memory, catalog, read16(memory, class_data + 2),
                    unit->class_description, sizeof(unit->class_description))))
            unit->class_description[0] = '\0';
        unit->portrait_valid = fe8_catalog_portrait(memory, catalog,
            unit->portrait_id, unit->portrait, unit->portrait_palette);
        for (slot = 0; slot < FE8_INVENTORY_ITEM_SLOTS; ++slot)
        {
            unit->items[slot] = read16(memory, address + FE8_UNIT_ITEM_OFFSET + slot * 2);
            fe8_catalog_item(memory, catalog, unit->items[slot], &unit->item_info[slot]);
        }
    }
    if (valid_range(supply_address, supply_capacity * 2,
            FE8_EWRAM_START, FE8_EWRAM_END)) {
        for (index = 0; index < supply_capacity; ++index) {
            snapshot->supply[index] = read16(memory, supply_address + index * 2);
            if (snapshot->supply[index]) {
                snapshot->supply_display_slots[snapshot->supply_count] = (uint16_t)index;
                ++snapshot->supply_count;
                fe8_catalog_item(memory, catalog, snapshot->supply[index],
                    &snapshot->supply_info[index]);
            } else if (snapshot->first_empty_supply == supply_capacity) {
                snapshot->first_empty_supply = (uint16_t)index;
            }
        }
        snapshot->supply_display_count = snapshot->supply_count;
        if (snapshot->first_empty_supply < supply_capacity) {
            snapshot->supply_display_slots[snapshot->supply_display_count] =
                snapshot->first_empty_supply;
            ++snapshot->supply_display_count;
        }
    }
    return snapshot->unit_count != 0;
}

static void write16(const Fe8MemoryWriter *writer, uint32_t address, uint16_t value) {
    writer->write8(writer->context, address, (uint8_t)value);
    writer->write8(writer->context, address + 1, (uint8_t)(value >> 8));
}

static bool item_can_move(const Fe8MemoryReader *memory, const Fe8Profile *profile,
    uint16_t encoded_item);

bool fe8_swap_prebattle_items(
    const Fe8MemoryReader *memory, const Fe8MemoryWriter *writer,
    const Fe8Profile *profile, uint32_t first_unit, unsigned first_slot,
    uint16_t expected_first, uint32_t second_unit, unsigned second_slot,
    uint16_t expected_second) {
    uint32_t first_address;
    uint32_t second_address;
    if (!writer || !writer->write8 || first_slot >= FE8_INVENTORY_ITEM_SLOTS ||
            second_slot >= FE8_INVENTORY_ITEM_SLOTS ||
            !valid_unit_address(profile, first_unit) ||
            !valid_unit_address(profile, second_unit) ||
            !item_can_move(memory, profile, expected_first) ||
            !item_can_move(memory, profile, expected_second) ||
            !fe8_inventory_management_available(memory, profile))
        return false;
    first_address = first_unit + FE8_UNIT_ITEM_OFFSET + first_slot * 2;
    second_address = second_unit + FE8_UNIT_ITEM_OFFSET + second_slot * 2;
    if (read16(memory, first_address) != expected_first ||
            read16(memory, second_address) != expected_second)
        return false;
    write16(writer, first_address, expected_second);
    write16(writer, second_address, expected_first);
    if (read16(memory, first_address) == expected_second &&
            read16(memory, second_address) == expected_first)
        return true;
    write16(writer, first_address, expected_first);
    write16(writer, second_address, expected_second);
    return false;
}

static bool endpoint_address(const Fe8MemoryReader *memory, const Fe8Profile *profile,
    Fe8InventoryEndpoint endpoint, uint32_t *address) {
    if (endpoint.kind == FE8_INVENTORY_ENDPOINT_UNIT) {
        if (!valid_unit_address(profile, endpoint.unit_address) ||
                endpoint.slot >= FE8_INVENTORY_ITEM_SLOTS)
            return false;
        *address = endpoint.unit_address + FE8_UNIT_ITEM_OFFSET + endpoint.slot * 2;
        return true;
    }
    if (endpoint.kind == FE8_INVENTORY_ENDPOINT_SUPPLY) {
        uint32_t supply_address;
        unsigned supply_capacity;
        resolve_supply(memory, profile, &supply_address, &supply_capacity);
        if (endpoint.unit_address == supply_address && endpoint.slot < supply_capacity &&
                valid_range(supply_address + endpoint.slot * 2, 2,
                    FE8_EWRAM_START, FE8_EWRAM_END)) {
            *address = supply_address + endpoint.slot * 2;
            return true;
        }
    }
    return false;
}

static bool item_can_move(const Fe8MemoryReader *memory, const Fe8Profile *profile,
    uint16_t encoded_item) {
    uint8_t item_id = (uint8_t)encoded_item;
    uint32_t record;
    uint32_t attributes;
    if (!item_id || profile->inventory.immovable_item_attributes == 0)
        return true;
    record = profile->inventory.item_table + (uint32_t)item_id * UINT32_C(0x24);
    if (!valid_range(record, UINT32_C(0x24), FE8_ROM_START, FE8_ROM_END) ||
            read8(memory, record + 6) != item_id)
        return false;
    attributes = read32(memory, record + 8);
    return (attributes & profile->inventory.immovable_item_attributes) == 0;
}

bool fe8_swap_inventory_endpoints(
    const Fe8MemoryReader *memory, const Fe8MemoryWriter *writer,
    const Fe8Profile *profile, Fe8InventoryEndpoint first,
    uint16_t expected_first, Fe8InventoryEndpoint second,
    uint16_t expected_second) {
    uint32_t a;
    uint32_t b;
    if (!writer || !writer->write8 || !fe8_inventory_management_available(memory, profile) ||
            !item_can_move(memory, profile, expected_first) ||
            !item_can_move(memory, profile, expected_second) ||
            !endpoint_address(memory, profile, first, &a) ||
            !endpoint_address(memory, profile, second, &b) ||
            read16(memory, a) != expected_first || read16(memory, b) != expected_second)
        return false;
    write16(writer, a, expected_second);
    write16(writer, b, expected_first);
    if (read16(memory, a) == expected_second && read16(memory, b) == expected_first)
        return true;
    write16(writer, a, expected_first);
    write16(writer, b, expected_second);
    return false;
}
