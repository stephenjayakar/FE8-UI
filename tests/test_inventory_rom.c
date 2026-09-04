/* Optional integration test. ROMs and generated captures remain local. */
#include <mgba/flags.h>
#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba/core/serialize.h>
#include <mgba-util/vfs.h>

#include "address_space.h"
#include "prebattle_inventory_ui.h"
#ifdef FE8_TEST_DESKTOP
#include "inventory_desktop.h"
#endif

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef FE8_TEST_DESKTOP
#define WIDTH 1280
#define HEIGHT 800
#else
#define WIDTH 960
#define HEIGHT 640
#endif
enum { MAX_FRAMES = 40000 };

static uint8_t read8(void *context, uint32_t address) {
    struct mCore *core = context;
    return core->busRead8(core, address);
}

#ifdef FE8_TEST_DESKTOP
static void hover_label(Fe8InventoryUi *, const Fe8InventorySnapshot *, Fe8InventoryHitKind);
static void write8(void *context, uint32_t address, uint8_t value) {
    struct mCore *core = context;
    core->busWrite8(core, address, value);
}
static void check_real_transfer(struct mCore *core, const Fe8MemoryReader *reader,
    const Fe8Profile *profile, Fe8InventorySnapshot *snapshot, Fe8InventoryUi *ui,
    const uint8_t *original, const void *ewram, size_t ram_size) {
    Fe8MemoryWriter writer = {core, write8};
    Fe8InventoryEndpoint source = {0}, destination = {0};
    uint16_t encoded = 0;
    int destination_unit = -1;
    for (int n = 0; n < snapshot->unit_count && !encoded; ++n)
        for (int j = 0; j < 5 && !encoded; ++j)
            if (snapshot->units[n].items[j] && snapshot->units[n].item_info[j].movable) {
                source = (Fe8InventoryEndpoint){FE8_INVENTORY_ENDPOINT_UNIT, snapshot->units[n].address, (unsigned)j};
                encoded = snapshot->units[n].items[j];
            }
    assert(encoded);
    for (int n = 0; n < snapshot->unit_count && destination_unit < 0; ++n)
        for (int j = 0; j < 5 && destination_unit < 0; ++j)
            if (snapshot->units[n].address != source.unit_address && !snapshot->units[n].items[j]) {
                destination = (Fe8InventoryEndpoint){FE8_INVENTORY_ENDPOINT_UNIT, snapshot->units[n].address, (unsigned)j};
                destination_unit = n;
            }
    assert(destination_unit >= 0);
    ui->selected = source; ui->has_selection = 1; ui->current_unit = destination_unit;
    fe8_inventory_ui_cycle_sort(ui, snapshot);
    fe8_inventory_ui_toggle_density(ui);
    hover_label(ui, snapshot, FE8_INVENTORY_HIT_UNIT_CLASS);
    assert(ui->has_selection && ui->selected.unit_address == source.unit_address && ui->selected.slot == source.slot);
    Fe8InventoryDesktopLayout layout;
    fe8_inventory_desktop_layout(ui, WIDTH, HEIGHT, &layout);
    int index;
    float scale = fe8_inventory_desktop_scale(ui, WIDTH, HEIGHT);
    Fe8InventoryHitKind hit = fe8_inventory_ui_hit_test(ui, snapshot, WIDTH, HEIGHT,
        (int)(30 * scale + .5f),
        (int)((layout.items_y + destination.slot * layout.side_row_height + 3) * scale + .5f), &index);
    assert(hit == FE8_INVENTORY_HIT_UNIT_ITEM && index == (int)destination.slot);
    Fe8InventoryEndpoint resolved = fe8_inventory_ui_endpoint(ui, snapshot, hit, index);
    assert(resolved.unit_address == destination.unit_address && resolved.slot == destination.slot);
    assert(memcmp(original, ewram, ram_size) == 0);
    assert(fe8_swap_inventory_endpoints(reader, &writer, profile, source, encoded, resolved, 0));
    uint32_t address = resolved.unit_address + 0x1E + resolved.slot * 2;
    assert(core->busRead16(core, address) == encoded);
    assert(fe8_swap_inventory_endpoints(reader, &writer, profile, source, 0, resolved, encoded));
    assert(memcmp(original, ewram, ram_size) == 0);
    ui->has_selection = 0;
    fe8_inventory_ui_toggle_density(ui);
    puts("  Real-ROM transfer after hover/sort/density changes and undo preserved RAM exactly");
}
#endif

static void map_memory(Fe8AddressSpace *space, struct mCore *core) {
    const uint32_t bases[] = {
        0x02000000, 0x03000000, 0x05000000, 0x06000000, 0x08000000,
    };
    fe8_address_space_init(space, core, read8);
    for (unsigned i = 0; i < sizeof(bases) / sizeof(bases[0]); ++i) {
        size_t size = 0;
        void *data = mCoreGetMemoryBlock(core, bases[i], &size);
        if (data && size)
            assert(fe8_address_space_add(space, bases[i], data, size));
    }
}

static void hover_label(Fe8InventoryUi *ui, const Fe8InventorySnapshot *snapshot,
    Fe8InventoryHitKind expected) {
    for (int y = 0; y < HEIGHT; ++y) {
        for (int x = 0; x < WIDTH; ++x) {
            int index = -1;
            Fe8InventoryHitKind hit = fe8_inventory_ui_hit_test(ui, snapshot,
                WIDTH, HEIGHT, x, y, &index);
            if (hit == expected && index == ui->current_unit) {
                fe8_inventory_ui_inspect(ui, snapshot, hit, index);
                return;
            }
        }
    }
    assert(!"Name/class label has no hit target");
}

static void capture(const char *prefix, const char *suffix, const uint32_t *pixels) {
    char path[1024];
    FILE *file;
    int length = snprintf(path, sizeof(path), "%s-%s.ppm", prefix, suffix);
    assert(length > 0 && (size_t)length < sizeof(path));
    file = fopen(path, "wb");
    assert(file);
    assert(fprintf(file, "P6\n%d %d\n255\n", WIDTH, HEIGHT) > 0);
    for (int i = 0; i < WIDTH * HEIGHT; ++i) {
        unsigned char rgb[] = {
            (unsigned char)pixels[i], (unsigned char)(pixels[i] >> 8),
            (unsigned char)(pixels[i] >> 16),
        };
        assert(fwrite(rgb, 1, sizeof(rgb), file) == sizeof(rgb));
    }
    assert(fclose(file) == 0);
}

int main(int argc, char **argv) {
    struct mCore *core;
    struct VFile *rom;
    struct mStandardLogger logger;
    Fe8AddressSpace space;
    Fe8MemoryReader reader;
    const Fe8Profile *profile;
    Fe8Catalog catalog;
    Fe8InventorySnapshot *snapshot;
    Fe8InventoryUi *ui;
    mColor *video;
    uint32_t *pixels;
    void *ewram;
    uint8_t *original_ram;
    size_t ewram_size = 0;
    unsigned frame;
    int archanae;

    if (argc < 3 || argc > 4 ||
            (strcmp(argv[2], "archanae") && strcmp(argv[2], "sacred-echoes"))) {
        fprintf(stderr, "Usage: %s ROM.gba archanae|sacred-echoes [capture-prefix]\n",
            argv[0]);
        return EXIT_FAILURE;
    }
    archanae = strcmp(argv[2], "archanae") == 0;
    core = mCoreFind(argv[1]);
    assert(core && core->init(core));
    mCoreInitConfig(core, "fe8-inventory-test");
    mStandardLoggerInit(&logger);
    logger.d.filter->defaultLevels = mLOG_FATAL | mLOG_ERROR;
    mLogSetDefaultLogger(&logger.d);
    rom = VFileOpen(argv[1], O_RDONLY);
    assert(rom && core->loadROM(core, rom));
    video = calloc(240 * 160, sizeof(*video));
    snapshot = calloc(1, sizeof(*snapshot));
    ui = calloc(1, sizeof(*ui));
    pixels = calloc(WIDTH * HEIGHT, sizeof(*pixels));
    assert(video && snapshot && ui && pixels);
    core->setVideoBuffer(core, video, 240);
    core->reset(core);
    map_memory(&space, core);
    reader.context = &space;
    reader.read8 = fe8_address_space_read8;
    profile = fe8_profile_for_rom(&reader);
    assert(strcmp(profile->profile_name, archanae ?
        "Fire Emblem: Archanae" : "Sacred Echoes") == 0);
    assert(fe8_catalog_init(&reader, profile, &catalog));

    /* Ordinary A/Start presses advance the unmodified ROM to its real roster.
       Never seed or patch RAM to manufacture a successful inventory test. */
    for (frame = 1; frame < MAX_FRAMES; ++frame) {
        unsigned phase = frame % 360;
        core->setKeys(core, phase < 3 ? 1 :
            (phase >= 180 && phase < 183 ? 8 : 0));
        core->runFrame(core);
        if (frame % 60 == 0 && fe8_extract_prebattle_inventory(&reader,
                profile, &catalog, snapshot) && snapshot->unit_count >= 2)
            break;
    }
    assert(frame < MAX_FRAMES);
    core->setKeys(core, 0);
    assert(strcmp(snapshot->units[0].name, archanae ? "Caeda" : "Alm") == 0);
    assert(strcmp(snapshot->units[0].class_name,
        archanae ? "Pegasus Knight" : "Fighter") == 0);
    printf("%s: real roster at frame %u, %u units, convoy capacity %u\n",
        profile->profile_name, frame, snapshot->unit_count, snapshot->supply_capacity);

    ewram = mCoreGetMemoryBlock(core, 0x02000000, &ewram_size);
    assert(ewram && ewram_size);
    original_ram = malloc(ewram_size);
    assert(original_ram);
    memcpy(original_ram, ewram, ewram_size);
    fe8_inventory_ui_init(ui);
    fe8_inventory_ui_open(ui, snapshot);
    ui->render_scale = 2;
#ifdef FE8_TEST_DESKTOP
    ui->desktop = 1;
    ui->desktop_scale = 1;
#endif
    for (int unit = 0; unit < snapshot->unit_count; ++unit) {
        ui->current_unit = unit;
        for (int is_class = 0; is_class <= 1; ++is_class) {
            const char *title = NULL;
            const char *help;
            const Fe8InventoryUnit *target = &snapshot->units[unit];
            hover_label(ui, snapshot, is_class ?
                FE8_INVENTORY_HIT_UNIT_CLASS : FE8_INVENTORY_HIT_UNIT_NAME);
            help = fe8_inventory_ui_unit_help(ui, snapshot, &title);
            assert(help && *help && title);
            assert(strcmp(title, is_class ? target->class_name : target->name) == 0);
            assert(strcmp(help, is_class ?
                target->class_description : target->description) == 0);
            fe8_inventory_ui_draw(ui, snapshot, pixels, WIDTH, WIDTH, HEIGHT);
            assert(memcmp(original_ram, ewram, ewram_size) == 0);
            if (argc == 4) {
                char suffix[32];
                if (unit == 0)
                    snprintf(suffix, sizeof(suffix), "%s", is_class ? "class" : "name");
                else
                    snprintf(suffix, sizeof(suffix), "%s-%d",
                        is_class ? "class" : "name", unit);
                capture(argv[3], suffix, pixels);
            }
            printf("  %s / %s: %s\n", target->name,
                is_class ? target->class_name : "Character", help);
        }
    }
#ifdef FE8_TEST_DESKTOP
    for (int percent = 80; percent <= 130; percent += 10) {
        fe8_inventory_ui_adjust_scale(ui, 0, WIDTH, HEIGHT);
        int direction = percent < 100 ? -1 : 1;
        for (int step = 0; step < abs(percent - 100) / 10; ++step)
            fe8_inventory_ui_adjust_scale(ui, direction, WIDTH, HEIGHT);
        assert(fe8_inventory_ui_scale_percent(ui, WIDTH, HEIGHT) == percent);
        check_real_transfer(core, &reader, profile, snapshot, ui, original_ram, ewram, ewram_size);
        ui->current_unit = 0;
        hover_label(ui, snapshot, FE8_INVENTORY_HIT_UNIT_CLASS);
        fe8_inventory_ui_draw(ui, snapshot, pixels, WIDTH, WIDTH, HEIGHT);
        if (argc == 4) {
            char suffix[32];
            snprintf(suffix, sizeof(suffix), "scale-%d", percent);
            capture(argv[3], suffix, pixels);
        }
    }
    fe8_inventory_ui_adjust_scale(ui, 0, WIDTH, HEIGHT);
#endif
    if (argc == 4) {
        char path[1024];
        int length = snprintf(path, sizeof(path), "%s.ss", argv[3]);
        struct VFile *state;
        assert(length > 0 && (size_t)length < sizeof(path));
        state = VFileOpen(path, O_RDWR | O_CREAT | O_TRUNC);
        assert(state && mCoreSaveStateNamed(core, state, SAVESTATE_ALL));
        state->close(state);
    }
    free(original_ram);
    free(pixels);
    free(ui);
    free(snapshot);
    mCoreConfigDeinit(&core->config);
    core->deinit(core);
    mLogSetDefaultLogger(NULL);
    mStandardLoggerDeinit(&logger);
    free(video);
    puts("ROM extraction, real name/class hover and read-only inspection passed");
    return EXIT_SUCCESS;
}
