#ifndef FE8_VOXEL_BUILDINGS_H
#define FE8_VOXEL_BUILDINGS_H
#include "fe8_profile.h"
#include "extended_map_renderer.h"

/* FE8U terrain meanings, not guessed building identities from roof colors. */
typedef enum Fe8BuildingKind {
    FE8_BUILDING_NONE, FE8_BUILDING_HOUSE, FE8_BUILDING_VILLAGE,
    FE8_BUILDING_VILLAGE_CLOSED, FE8_BUILDING_ARMORY, FE8_BUILDING_VENDOR,
    FE8_BUILDING_ARENA, FE8_BUILDING_FORT, FE8_BUILDING_CASTLE,
    FE8_BUILDING_GATE, FE8_BUILDING_CHURCH, FE8_BUILDING_INN,
    FE8_BUILDING_ROOF, FE8_BUILDING_COUNT
} Fe8BuildingKind;
enum {
    FE8_BUILDING_TERRAIN = 1, FE8_BUILDING_ART_MATCH = 2,
    FE8_BUILDING_FOOTPRINT = 4
};
typedef struct Fe8Building {
    Fe8BuildingKind kind;
    uint16_t x, y, width, height; /* Owned rectangle in map tiles. */
    uint16_t anchor_x, anchor_y;  /* Original entrance, never moved in RAM. */
    unsigned evidence;
} Fe8Building;
typedef struct Fe8BuildingLayout {
    Fe8Building objects[FE8_MAX_MAP_CELLS];
    uint16_t owner[FE8_MAX_MAP_CELLS]; /* One-based index; zero is original ground. */
    uint16_t count, width, height;
} Fe8BuildingLayout;

Fe8BuildingKind fe8_building_kind(unsigned terrain);
const char *fe8_building_name(Fe8BuildingKind kind);
bool fe8_building_support(unsigned terrain);
/* Decoded, palette-correct live metatiles are optional. Exact nontrivial art
 * matches can disambiguate generic HOUSE/custom IDs using a labelled building
 * in this same map. Conflicting matches stay generic. No persistent/global
 * signature database, palette-color guessing, or chapter coordinates. */
bool fe8_buildings_classify(Fe8BuildingLayout *out, const Fe8Snapshot *snapshot,
    const Fe8HostPixel *metatiles, int stride);

typedef void (*Fe8BuildingBox)(void *context, float x, float y, float z,
    float width, float height, float depth, Fe8HostPixel color);
typedef struct Fe8BuildingPainter {
    void *context;
    Fe8BuildingBox box;
    Fe8HostPixel roof, wall;
} Fe8BuildingPainter;
/* Same finite, positive boxes feed the depth rasterizer and shadow atlas. */
void fe8_building_draw(const Fe8Building *building, const Fe8BuildingPainter *painter);
#endif
