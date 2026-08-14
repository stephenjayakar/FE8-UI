#include "extended_unit_renderer.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t ewram[0x40000];
static uint8_t palette[0x400];
static uint8_t vram[0x18000];

static uint8_t read8(void *context, uint32_t address) {
    (void)context;
    if (address >= 0x02000000 && address < 0x02040000)
        return ewram[address - 0x02000000];
    if (address >= 0x05000000 && address < 0x05000400)
        return palette[address - 0x05000000];
    if (address >= 0x06000000 && address < 0x06018000)
        return vram[address - 0x06000000];
    return 0;
}

static void put16(uint8_t *bytes, size_t offset, uint16_t value) {
    bytes[offset] = (uint8_t)value;
    bytes[offset + 1] = (uint8_t)(value >> 8);
}

int main(void) {
    Fe8MemoryView memory = {NULL, read8};
    Fe8Snapshot snapshot;
    Fe8ExtendedViewport viewport = {64, 64, 0, 0};
    Fe8HostPixel output[64 * 64];
    unsigned tiles[] = {0x80, 0x81, 0xA0, 0xA1};
    size_t i;
    memset(&snapshot, 0, sizeof(snapshot));
    memset(output, 0, sizeof(output));
    snapshot.map_width = 4;
    snapshot.map_height = 4;
    snapshot.cursor_x = 0;
    snapshot.cursor_y = 0;
    snapshot.cursor_target_x = 0;
    snapshot.cursor_target_y = 0;
    snapshot.cursor_display_x = 0;
    snapshot.cursor_display_y = 0;
    snapshot.visible_unit_count = 1;
    snapshot.visible_units[0] = (Fe8VisibleUnit){
        0x81, 0x80, 1, 1, 0, 0, UINT32_C(0x02000100)};
    put16(ewram, 0x108, (uint16_t)(0x80 | (13 << 12)));
    ewram[0x10B] = 0;
    for (i = 0; i < sizeof(tiles) / sizeof(tiles[0]); ++i)
        memset(vram + 0x10000 + tiles[i] * 32, 0x11, 32);
    put16(palette, 0x200 + (13 * 16 + 1) * 2, 0x03E0);
    assert(fe8_render_extended_units(&memory, &snapshot, viewport,
        output, 64, 0) == 1);
    assert(output[24 * 64 + 24] == UINT32_C(0xFF00FF00));
    assert(output[0] != 0); /* Host cursor is visible independently of OAM. */

    /* The complete SMS list includes non-unit world effects. Render it in
     * preference to reconstructing only handles referenced by unit structs. */
    memset(output, 0, sizeof(output));
    snapshot.flags |= FE8_SNAPSHOT_MAP_SPRITES;
    snapshot.map_sprite_count = 1;
    snapshot.map_sprites[0] = (Fe8VisibleMapSprite){32, 16, 0xD080, 0};
    assert(fe8_render_extended_units(&memory, &snapshot, viewport,
        output, 64, 0) == 1);
    assert(output[16 * 64 + 32] == UINT32_C(0xFF00FF00));
    assert(output[24 * 64 + 24] == 0);
    snapshot.flags &= ~FE8_SNAPSHOT_MAP_SPRITES;

    /* Enemy bosses get FE8's blinking 8x8 OBJ marker in extended space. */
    memset(output, 0, sizeof(output));
    snapshot.visible_units[0].attributes = UINT32_C(1) << 15;
    memset(vram + 0x10000 + 0x10 * 32, 0x22, 32);
    put16(palette, 0x200 + 2 * 2, 0x001F);
    assert(fe8_render_extended_units(&memory, &snapshot, viewport,
        output, 64, 0) == 1);
    assert(output[23 * 64 + 25] == UINT32_C(0xFF0000FF));
    memset(output, 0, sizeof(output));
    assert(fe8_render_extended_units(&memory, &snapshot, viewport,
        output, 64, 20) == 1);
    assert(output[23 * 64 + 25] == UINT32_C(0xFF00FF00));

    /* The extended cursor follows FE8's animated pixel position, not the
     * logical tile that advances before the visible sprite arrives. */
    memset(output, 0, sizeof(output));
    snapshot.visible_unit_count = 0;
    snapshot.cursor_x = 3;
    snapshot.cursor_y = 2;
    snapshot.cursor_target_x = 48;
    snapshot.cursor_target_y = 32;
    snapshot.cursor_display_x = 20;
    snapshot.cursor_display_y = 12;
    assert(fe8_render_extended_units(&memory, &snapshot, viewport,
        output, 64, 0) == 0);
    assert(output[12 * 64 + 20] != 0);
    assert(output[32 * 64 + 48] == 0);
    puts("extended unit renderer tests passed");
    return 0;
}
