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
    /* Resolve the new one-click and drag gestures before using the same guarded
       transaction as the app. Every gesture must undo byte-for-byte. */
    int source_index = -1, source_unit = -1;
    for (int n = 0; n < ui->pool_count; ++n)
        if (ui->pool[n].endpoint.kind == source.kind &&
            ui->pool[n].endpoint.unit_address == source.unit_address &&
            ui->pool[n].endpoint.slot == source.slot) source_index = n;
    for (int n = 0; n < snapshot->unit_count; ++n)
        if (snapshot->units[n].address == source.unit_address) source_unit = n;
    assert(source_index >= 0 && source_unit >= 0);
    for (int gesture = 0; gesture < 4; ++gesture) {
        ui->has_selection = 0;
        ui->current_unit = gesture == 1 ? source_unit : destination_unit;
        hit = FE8_INVENTORY_HIT_QUICK_POOL; index = source_index;
        if (gesture < 2) {
            assert(!fe8_inventory_desktop_click(ui,snapshot,&hit,&index));
        } else {
            fe8_inventory_desktop_pointer_down(ui,snapshot,
                FE8_INVENTORY_HIT_POOL_ITEM,source_index,400,220);
            fe8_inventory_desktop_pointer_motion(ui,snapshot,WIDTH,HEIGHT,40,430);
            assert(ui->dragging);
            if (gesture == 2) { hit = FE8_INVENTORY_HIT_ROSTER; index = destination_unit; }
            else { hit = FE8_INVENTORY_HIT_UNIT_ITEM; index = 0; } /* Occupied swap. */
            assert(!fe8_inventory_desktop_pointer_up(ui,snapshot,&hit,&index));
        }
        assert(ui->selected.unit_address == source.unit_address && ui->selected.slot == source.slot);
        resolved = fe8_inventory_ui_endpoint(ui,snapshot,hit,index);
        uint16_t replaced = fe8_inventory_ui_endpoint_item(snapshot,resolved);
        if (gesture == 1) assert(resolved.kind == FE8_INVENTORY_ENDPOINT_SUPPLY && !replaced);
        if (gesture == 3) assert(replaced);
        assert(memcmp(original,ewram,ram_size) == 0); /* Gestures are read-only. */
        assert(fe8_swap_inventory_endpoints(reader,&writer,profile,source,encoded,resolved,replaced));
        assert(fe8_swap_inventory_endpoints(reader,&writer,profile,source,replaced,resolved,encoded));
        assert(memcmp(original,ewram,ram_size) == 0);
    }
    ui->has_selection = 0;
    puts("  Real-ROM click Give/Store, drag Give/swap, and undo preserved RAM exactly");
}
#endif

static int native_personal_lock(struct mCore *core, uint32_t unit_address, uint16_t item) {
    void *saved=malloc(core->stateSize(core)); assert(saved);
    assert(core->saveState(core,saved));
    assert(core->writeRegister(core,"cpsr",0xBF)); /* Thumb, system mode, IRQ masked. */
    assert(core->writeRegister(core,"sp",0x0203F800));
    assert(core->writeRegister(core,"r0",(int32_t)unit_address));
    assert(core->writeRegister(core,"r1",item));
    assert(core->writeRegister(core,"r2",255));
    assert(core->writeRegister(core,"lr",0x08000001));
    assert(core->writeRegister(core,"pc",0x08B3EA54));
    int32_t pc=0,result=0; int steps;
    for(steps=0;steps<2000;++steps) {
        core->step(core); assert(core->readRegister(core,"pc",&pc));
        if(pc==0x08000002)break; /* Thumb pipeline is one instruction ahead. */
    }
    assert(steps<2000);assert(core->readRegister(core,"r0",&result));
    assert(core->loadState(core,saved));free(saved);
    return result!=0;
}
static void check_archanae_locks(struct mCore *core, const Fe8MemoryReader *reader,
    const Fe8Catalog *catalog, const Fe8InventorySnapshot *snapshot) {
    Fe8ItemInfo borderland;
    int marth=-1;
    assert(fe8_catalog_item(reader,catalog,0x01C2,&borderland));
    assert(strcmp(borderland.name,"Borderland Swd")==0);
    assert(borderland.lock_kind==FE8_ITEM_LOCK_CHARACTER);
    for(int n=0;n<snapshot->unit_count;++n) {
        const Fe8InventoryUnit *u=&snapshot->units[n];
        if(strcmp(u->name,"Marth")==0)marth=n;
        assert(!native_personal_lock(core,u->address,0x01C2));
        assert(fe8_inventory_item_use_state(u,&borderland)==FE8_INVENTORY_USE_LOCKED);
    }
    assert(marth>=0);
    /* Athena is not in this early save. Use an isolated unit copy with her
       real ROM character record; restore every byte before returning. */
    const Fe8InventoryUnit *m=&snapshot->units[marth];
    uint32_t characters=core->busRead32(core,m->address)-(uint32_t)m->character_id*0x34;
    uint32_t athena_record=characters+0x36*0x34;
    char name[28];
    assert(core->busRead8(core,athena_record+4)==0x36);
    assert(fe8_catalog_text(reader,catalog,core->busRead16(core,athena_record),name,sizeof(name)));
    assert(strcmp(name,"Athena")==0);
    uint8_t saved_unit[0x48]; uint32_t scratch=0x0203FC00;
    for(unsigned j=0;j<sizeof(saved_unit);++j) {
        saved_unit[j]=core->busRead8(core,scratch+j);
        core->busWrite8(core,scratch+j,core->busRead8(core,m->address+j));
    }
    core->busWrite32(core,scratch,athena_record);
    assert(native_personal_lock(core,scratch,0x01C2));
    Fe8InventoryUnit athena=*m;athena.character_id=0x36;
    assert(fe8_inventory_item_use_state(&athena,&borderland)==FE8_INVENTORY_USE_READY);
    for(unsigned j=0;j<sizeof(saved_unit);++j)core->busWrite8(core,scratch+j,saved_unit[j]);
    puts("  Borderland Sword: native ROM predicate rejects Marth and accepts Athena; frontend agrees");
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
    if(archanae) {
        check_archanae_locks(core,&reader,&catalog,snapshot);
        assert(memcmp(original_ram,ewram,ewram_size)==0);
    }
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
