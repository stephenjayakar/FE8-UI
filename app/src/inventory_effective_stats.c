/* Evaluate the actual unit stat screen rules in an isolated mGBA core. The
   whitelist and cache layout below refer to the exact locally tested ROMs;
   FE8U-family headers alone are not permission to run these custom entrypoints. */
#include "inventory_effective_stats.h"

#include <mgba/flags.h>
#include <mgba/core/core.h>
#include <mgba/internal/arm/arm.h>
#include <mgba-util/vfs.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum { NATIVE_ARCHANEA = 1, NATIVE_ECHOES = 2, MAX_STEPS = 100000 };
static const uint8_t supported_sha1[][20] = {
    {0x22,0x0d,0x1d,0x6b,0x5f,0x56,0xc9,0xe2,0x5e,0xb6,
     0x66,0xa7,0xf3,0xdd,0x7c,0x48,0x6a,0x75,0x94,0x17},
    {0x57,0xb8,0xad,0xbd,0xea,0xe1,0xee,0xef,0x61,0xd5,
     0x4c,0x67,0x15,0xa5,0x51,0x82,0xd5,0x0d,0x8a,0x1b},
};
/* Order matches Fe8UnitStat. Con and Mov reuse former function interiors;
   this array must never be applied to an unknown hack or retail executable. */
static const uint32_t getters[FE8_STAT_COUNT] = {
    0x080191B0, 0x080191D0, 0x08019210, 0x08019298,
    0x08019250, 0x08019270, 0x08019284, 0x08019224, 0x08019190,
};

struct Fe8StatEvaluator {
    struct mCore *source;
    struct mCore *shadow;
    void *state;
    size_t state_size, rom_size;
    int profile;
};

static int supported(struct mCore *source) {
    uint8_t digest[20];
    if (!source || !source->platform || source->platform(source) != mPLATFORM_GBA ||
            !source->checksum) return 0;
    source->checksum(source, digest, mCHECKSUM_SHA1);
    for (unsigned n = 0; n < sizeof(supported_sha1) / sizeof(supported_sha1[0]); ++n)
        if (!memcmp(digest, supported_sha1[n], sizeof(digest))) return (int)n + 1;
    return 0;
}

Fe8StatEvaluator *fe8_stat_evaluator_create(struct mCore *source) {
    int profile = supported(source);
    if (!profile) return NULL;
    size_t mapped_size = 0, rom_size = source->romSize(source);
    const void *rom = mCoreGetMemoryBlock(source, 0x08000000, &mapped_size);
    if (!rom || !rom_size || rom_size > mapped_size) return NULL;
    Fe8StatEvaluator *e = calloc(1, sizeof(*e));
    if (!e) return NULL;
    e->source = source; e->profile = profile; e->rom_size = rom_size;
    e->shadow = mCoreCreate(mPLATFORM_GBA);
    if (!e->shadow) { free(e); return NULL; }
    if (!e->shadow->init(e->shadow)) { free(e->shadow); free(e); return NULL; }
    mCoreInitConfig(e->shadow, "fe8-read-only-stats");
    /* Never share a writable mapping or attach a save file/peripheral. mGBA
       owns this copied, memory-only VFile after loadROM succeeds. */
    struct VFile *vf = VFileMemChunk(rom, rom_size);
    if (!vf) { fe8_stat_evaluator_destroy(e); return NULL; }
    if (!e->shadow->loadROM(e->shadow, vf)) {
        vf->close(vf); fe8_stat_evaluator_destroy(e); return NULL;
    }
    /* Verify the copied mapping too: a file checksum alone does not detect a
       cheat/patch applied to the live ROM mapping after it was loaded. */
    if (supported(e->shadow) != profile) { fe8_stat_evaluator_destroy(e); return NULL; }
    e->shadow->reset(e->shadow);
    e->state_size = source->stateSize(source);
    if (e->state_size != e->shadow->stateSize(e->shadow)) {
        fe8_stat_evaluator_destroy(e); return NULL;
    }
    /* malloc supplies alignment required by mGBA's raw state struct. No PNG
       extended-state chunks and no serialization round trip on the live CPU. */
    e->state = calloc(1, e->state_size);
    if (!e->state) { fe8_stat_evaluator_destroy(e); return NULL; }
    return e;
}

void fe8_stat_evaluator_destroy(Fe8StatEvaluator *e) {
    if (!e) return;
    if (e->shadow) {
        mCoreConfigDeinit(&e->shadow->config);
        e->shadow->deinit(e->shadow);
    }
    free(e->state);
    free(e);
}

static bool native_stat(Fe8StatEvaluator *e, uint32_t address,
    Fe8UnitStat stat, int16_t *value) {
    struct mCore *c = e->shadow;
    if (!c->loadState(c, e->state)) return false;
    /* Armory shows map/unit-stat-screen values, not a stale battle preview's
       opponent/weapon. All changes here affect the private copy only. */
    c->busWrite16(c, 0x0203A4D4, 0); /* gBattleStats.config */
    if (e->profile == NATIVE_ARCHANEA) {
        /* This revision has separate personal/learned and item-skill caches.
           Force both to rebuild after every restore, especially after a host
           transfer to the unit that was last queried in the game. The equipped
           skill byte is rebuilt when the item list is regenerated. */
        c->busWrite32(c, 0x02026BB0, 0);
        c->busWrite32(c, 0x02026BC8, 0);
        c->busWrite8(c, 0x02026BC7, 0xFF);
    }
    if (!c->writeRegister(c, "cpsr", 0xFF) ||
            !c->writeRegister(c, "sp", 0x03007D00) ||
            !c->writeRegister(c, "lr", 0x08000001) ||
            !c->writeRegister(c, "pc", (int32_t)getters[stat])) return false;
    struct ARMCore *cpu = c->cpu;
    for (int reg = 0; reg <= 12; ++reg) cpu->gprs[reg] = 0;
    cpu->gprs[0] = (int32_t)address;
    /* Do not let VBlank, DMA or timer events advance the copied map while a
       getter runs. This pinned mGBA's step calls ARMRun, which dispatches only
       at nextEvent. No live timing fields or peripherals are touched. */
    cpu->halted = 0;
    for (int step = 0; step < MAX_STEPS; ++step) {
        /* ARM/Thumb trampolines set nextEvent to cycles. Reset before each
           instruction, since ARMRun checks events before (not after) stepping. */
        cpu->cycles = 0; cpu->nextEvent = INT_MAX;
        c->step(c);
        uint32_t pc = (uint32_t)cpu->gprs[ARM_PC];
        uint32_t sp = (uint32_t)cpu->gprs[ARM_SP];
        if (sp < 0x03006800 || sp > 0x03008000) return false;
        if (pc == 0x08000002 && cpu->cpsr.t) {
            int32_t result = cpu->gprs[0];
            if (sp != 0x03007D00 || result < 0 || result > 999) return false;
            *value = (int16_t)result;
            return true;
        }
        if (cpu->halted ||
                !((pc >= 0x08000000 && pc < 0x0A000000) ||
                  (pc >= 0x03000000 && pc < 0x03008000) || pc < 0x4000))
            return false;
    }
    return false;
}

bool fe8_stat_evaluator_refresh(Fe8StatEvaluator *e, struct mCore *source,
    Fe8InventorySnapshot *s) {
    if (!s) return false;
    for (unsigned n = 0; n < s->unit_count && n < FE8_INVENTORY_UNIT_CAPACITY; ++n) {
        s->units[n].effective_stats_valid = false;
        memset(s->units[n].effective_stats, 0, sizeof(s->units[n].effective_stats));
    }
    if (!e || e->source != source || s->unit_count > FE8_INVENTORY_UNIT_CAPACITY ||
            source->stateSize(source) != e->state_size ||
            !source->saveState(source, e->state)) return false;
    size_t source_size=0, shadow_size=0;
    const void *source_rom=mCoreGetMemoryBlock(source,0x08000000,&source_size);
    const void *shadow_rom=mCoreGetMemoryBlock(e->shadow,0x08000000,&shadow_size);
    if (!source_rom || !shadow_rom || source->romSize(source)!=e->rom_size ||
            source_size<e->rom_size || shadow_size<e->rom_size ||
            memcmp(source_rom,shadow_rom,e->rom_size)) return false;
    bool complete = true;
    for (unsigned n = 0; n < s->unit_count; ++n) {
        Fe8InventoryUnit *u = &s->units[n];
        if (u->address < 0x0202BE4C || u->address >= 0x0202CFBC ||
                (u->address - 0x0202BE4C) % 0x48) { complete = false; continue; }
        bool coherent = true;
        for (unsigned slot = 0; slot < FE8_INVENTORY_ITEM_SLOTS; ++slot)
            if (source->busRead16(source, u->address + 0x1E + 2 * slot) != u->items[slot])
                coherent = false;
        uint32_t character = source->busRead32(source, u->address);
        uint32_t class_data = source->busRead32(source, u->address + 4);
        if (character < 0x08000000 || character >= 0x0A000000 ||
                class_data < 0x08000000 || class_data >= 0x0A000000 ||
                source->busRead8(source, character + 4) != u->character_id ||
                source->busRead8(source, class_data + 4) != u->class_id)
            coherent = false;
        /* Do not combine a previous raw-stat snapshot with newer totals. */
        static const unsigned offsets[]={0x14,0x15,0x16,0x19,0x17,0x18};
        for (unsigned stat=0;stat<sizeof(offsets)/sizeof(offsets[0]);++stat)
            if ((int)source->busRead8(source,u->address+offsets[stat]) !=
                    fe8_inventory_stat_base(u,(Fe8UnitStat)stat)) coherent=false;
        if (source->busRead8(source,u->address+0x12)!=u->max_hp ||
                source->busRead8(source,u->address+0x13)!=u->hp) coherent=false;
        int16_t totals[FE8_STAT_COUNT];
        for (int stat = 0; coherent && stat < FE8_STAT_COUNT; ++stat)
            if (!native_stat(e, u->address, (Fe8UnitStat)stat, &totals[stat])) coherent = false;
        if (coherent) {
            memcpy(u->effective_stats, totals, sizeof(totals));
            u->effective_stats_valid = true;
        } else complete = false;
    }
    return complete;
}
