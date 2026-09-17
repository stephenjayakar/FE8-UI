# Distinct voxel buildings

`voxel-live` generates separate armory, vendor, arena, house, village, closed
village, fort, castle, gate, church, and inn models. Unit billboards, native hover
animations, mouse targeting, and the existing safe native-UI fallback are unchanged.

## Identification and footprint ownership

FE8U already has separate building terrain IDs. The previous generator sent
house (`05`), armory (`06`), vendor (`07`), arena (`08`), and church (`24`) to
one cottage recipe. It also treated adjoining roof blocks and their entrance as
independent objects. Both problems are corrected; this was not a need for a
remote image-recognition service.

The classifier uses those semantic entrance IDs first, including BOTH arena IDs
`08` and `30`. Village `03` and closed village `04` preserve their state; fort
`0A`, castle entrance `0B`, gate `23`, and inn `38` have their own recipes.
A throne is not interpreted as a castle. The meanings are documented in the
FE8 decomp's `include/constants/terrains.h`.

The model owns a connected, bounded roof footprint (`22`, `2C`, `2E`) associated
with its entrance. It requires roof evidence directly above that entrance, so
a single-tile armory beside a village cannot absorb the village's roof. Touching
roofs are partitioned between entrances. Only fully owned rectangles become
models: L-shaped gaps, roads, neighboring entrances and fog-hidden tiles are
not erased by a bounding rectangle. An entrance is rendered once, not as an
extra cottage in front of its roof. Unclassified roof fragments get low roof
slabs instead of an invented shop or castle.

As an additional conservative hint, the classifier computes signatures of live,
palette-correct 16x16 metatile artwork. A generic house or custom terrain ID can
inherit a special-building class from identical, nontrivial artwork attached to
a known armory/vendor/arena/church/inn elsewhere in the same map. Matches are
content-verified, require color/edge detail, and are rejected when different
known classes share the signature. Standard special-building IDs always win.

This is not general-purpose visual recognition. Custom artwork with no labelled
reference stays generic/original rather than guessing a shop type from its color.
There are no chapter-specific coordinates, bundled ROM graphics, model downloads,
API keys, or network generation. The original ROM and saves are untouched.

## Model language

- Armory: low stone workshop, barrel-vault roof with iron ribs, weapon plaque,
  forge chimney and an outdoor spear rack.
- Vendor: merchant storefront with a striped awning, stocked counter and crates.
- Arena: open octagonal amphitheatre, exposed sand floor, stepped seating,
  entrance and banners. No roof covers the combat pit.
- House/village: domestic tiled roofs; villages retain a surrounding fence and
  closed villages have a visibly barred entrance.
- Fort/castle/gate: low open garrison versus taller battlement towers, a keep and
  an iron portcullis. An isolated gate remains a gatehouse, not a whole castle.
- Church: pale masonry, slate roof, raised belfry, steeple and stained-glass accents.
- Inn: domestic building with a hanging hospitality sign.

Wall/roof materials are sampled from the current ROM palette. Every recipe emits
finite positive boxes into the existing depth-tested software rasterizer. The
same boxes stamp the shadow atlas, so building shadows follow their new volumes.
Original flat building art is removed only inside owned footprints.

## Performance and safety

Building classification and model generation run only when scenery inputs change,
not on each sprite/cursor frame. The bounded scene, GPU scaling, native-resolution
HUD, billboard cache, completed-frame reuse and animated-ground fast path remain.
No new service or runtime asset dependency is introduced. Cold generation and
continuous camera movement are still CPU work, not a claimed all-GPU renderer.

## Tests and captures

`test_voxel_buildings` covers all semantic classes, both arena IDs, adjacent and
multi-tile footprints, holes, map edges, fog, ambiguous/blank artwork, same-map
signature inference, maximum map capacity, every model's finite geometry, distinct
silhouettes, the arena's open interior, scene-cache reuse, class-change invalidation,
and immutable input. The real-ROM renderer test additionally verifies each visible
building's semantic ID and entrance ownership in the supplied Archanae and Sacred
Echoes maps. Existing cursor/hover interaction tests remain enabled.

```sh
cmake --build build
ctest --test-dir build --output-on-failure
mkdir -p /tmp/fe8-building-gallery
build/tests/test_voxel_buildings /tmp/fe8-building-gallery
```

The optional gallery and arena-detail PPMs are rendered by the same native C
renderer using deterministic fixture terrain. They are NOT campaign screenshots.
The Archanae gameplay capture uses the actual running game and its live armory.
Native macOS CI builds the application and runs the ROM-independent tests,
including AppKit settings and OpenGL compositor checks. ROM gameplay captures
are Linux captures; no M1 gameplay/performance claim is implied.
