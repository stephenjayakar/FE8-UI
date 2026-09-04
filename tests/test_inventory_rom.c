/* Optional integration test. ROMs and generated captures remain local. */
#include <mgba/flags.h>
#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba/core/serialize.h>
#include <mgba-util/vfs.h>

#include "address_space.h"
#include "prebattle_inventory_ui.h"

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { WIDTH = 960, HEIGHT = 640, MAX_FRAMES = 40000 };

static uint8_t read8(void *context, uint32_t address) {
    struct mCore *core = context;
    return core->busRead8(core, address);
}

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
