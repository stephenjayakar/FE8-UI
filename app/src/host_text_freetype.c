/* Smooth desktop text on non-Apple hosts. Fonts are system resources, not ROM
   assets. This renderer is used only by the main-thread inventory painter. */
#include "host_text.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SYNTHESIS_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int fe8_bitmap_text_begin(Fe8HostTextCanvas *, uint32_t *, int, int, int);
void fe8_bitmap_text_draw(Fe8HostTextCanvas *, int, int, int, int, const char *, float,
    uint32_t, Fe8HostTextWeight, int);
void fe8_bitmap_text_end(Fe8HostTextCanvas *);

enum { CACHE_SIZE = 1024 };
typedef struct Glyph {
    unsigned code;
    int size, bold, width, height, left, top, advance;
    unsigned char *alpha;
} Glyph;
static FT_Library library;
static FT_Face face;
static int initialized;
static Glyph cache[CACHE_SIZE];

static void cleanup(void) {
    for (int i = 0; i < CACHE_SIZE; ++i) free(cache[i].alpha);
    if (face) FT_Done_Face(face);
    if (library) FT_Done_FreeType(library);
}
static int initialize(void) {
    static const char *paths[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
    };
    const char *path = getenv("FE8_UI_FONT");
    if (initialized) return face != NULL;
    initialized = 1;
    if (FT_Init_FreeType(&library)) return 0;
    atexit(cleanup);
    if (path && *path && FT_New_Face(library, path, 0, &face))
        fprintf(stderr, "Unable to load FE8_UI_FONT '%s'; trying system fonts\n", path);
    for (unsigned i = 0; !face && i < sizeof(paths) / sizeof(paths[0]); ++i)
        FT_New_Face(library, paths[i], 0, &face);
    if (!face) fprintf(stderr,
        "No scalable UI font found. Install fonts-dejavu-core or set FE8_UI_FONT. Using bitmap fallback.\n");
    return face != NULL;
}
static unsigned decode(const unsigned char **cursor) {
    const unsigned char *p = *cursor;
    unsigned c = *p++;
    int count = 0;
    unsigned minimum = 0;
    if (c < 128) { *cursor = p; return c; }
    if (c >= 0xC2 && c <= 0xDF) { c &= 31; count = 1; minimum = 128; }
    else if (c >= 0xE0 && c <= 0xEF) { c &= 15; count = 2; minimum = 2048; }
    else if (c >= 0xF0 && c <= 0xF4) { c &= 7; count = 3; minimum = 65536; }
    else { *cursor = p; return 0xFFFD; }
    for (int i = 0; i < count; ++i) {
        if (!*p || (*p & 0xC0) != 0x80) { *cursor = p; return 0xFFFD; }
        c = (c << 6) | (*p++ & 63);
    }
    *cursor = p;
    return c < minimum || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF) ? 0xFFFD : c;
}
static Glyph *glyph(unsigned code, int size, int bold) {
    unsigned hash = (code * 31u + (unsigned)size * 137u + (unsigned)bold * 503u) % CACHE_SIZE;
    Glyph *g = &cache[hash];
    if (g->size == size && g->code == code && g->bold == bold) return g;
    if (FT_Set_Pixel_Sizes(face, 0, (unsigned)size) || FT_Load_Char(face, code, FT_LOAD_DEFAULT)) return NULL;
    if (bold) FT_GlyphSlot_Embolden(face->glyph);
    if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL)) return NULL;
    FT_GlyphSlot slot = face->glyph;
    FT_Bitmap *bitmap = &slot->bitmap;
    /* Scalable fonts produce grayscale glyphs with this render mode. */
    if (bitmap->width && bitmap->rows && bitmap->pixel_mode != FT_PIXEL_MODE_GRAY) return NULL;
    size_t bytes = (size_t)bitmap->width * bitmap->rows;
    unsigned char *alpha = bytes ? malloc(bytes) : NULL;
    if (bytes && !alpha) return NULL;
    for (unsigned y = 0; y < bitmap->rows; ++y) {
        int pitch = bitmap->pitch;
        const unsigned char *row = bitmap->buffer +
            (pitch < 0 ? (bitmap->rows - 1 - y) * (unsigned)-pitch : y * (unsigned)pitch);
        if (bitmap->width) memcpy(alpha + y * bitmap->width, row, bitmap->width);
    }
    free(g->alpha);
    *g = (Glyph){code, size, bold, (int)bitmap->width, (int)bitmap->rows,
        slot->bitmap_left, slot->bitmap_top, (int)((slot->advance.x + 32) >> 6), alpha};
    return g;
}
static int advance(unsigned code, int size, int bold) {
    if (code == '\t') return advance(' ', size, bold) * 4;
    Glyph *g = glyph(code, size, bold);
    return g ? g->advance : size / 2;
}
static int measure(const unsigned char *p, int size, int bold, int word) {
    int width = 0;
    while (*p && *p != '\n' && *p != '\r' && (!word || (*p != ' ' && *p != '\t'))) {
        width += advance(decode(&p), size, bold);
        if (width > 1000000) break;
    }
    return width;
}
static uint32_t blend(uint32_t dst, uint32_t src, unsigned coverage) {
    unsigned a = ((src >> 24) * coverage + 127) / 255, inv = 255 - a;
    unsigned r = ((src & 255) * a + (dst & 255) * inv + 127) / 255;
    unsigned g = (((src >> 8) & 255) * a + ((dst >> 8) & 255) * inv + 127) / 255;
    unsigned b = (((src >> 16) & 255) * a + ((dst >> 16) & 255) * inv + 127) / 255;
    unsigned alpha = a + ((dst >> 24) * inv + 127) / 255;
    return (alpha << 24) | (b << 16) | (g << 8) | r;
}
static void paint(Fe8HostTextCanvas *c, unsigned code, int size, int bold,
    int x, int baseline, int left, int top, int right, int bottom, uint32_t color) {
    Glyph *g = glyph(code, size, bold);
    if (!g) return;
    for (int yy = 0; yy < g->height; ++yy) {
        int y = baseline - g->top + yy;
        if (y < 0 || y < top || y >= bottom || y >= c->height) continue;
        for (int xx = 0; xx < g->width; ++xx) {
            int dx = x + g->left + xx;
            if (dx < 0 || dx < left || dx >= right || dx >= c->width) continue;
            unsigned char a = g->alpha[yy * g->width + xx];
            if (a) c->pixels[y * c->stride + dx] = blend(c->pixels[y * c->stride + dx], color, a);
        }
    }
}
int fe8_host_text_begin(Fe8HostTextCanvas *c, uint32_t *pixels, int stride, int width, int height) {
    if (!c || !pixels || width <= 0 || height <= 0 || stride < width) return 0;
    if (!initialize()) return fe8_bitmap_text_begin(c, pixels, stride, width, height);
    *c = (Fe8HostTextCanvas){face, pixels, stride, width, height};
    return 1;
}
void fe8_host_text_draw(Fe8HostTextCanvas *c, int x, int y, int width, int height,
    const char *text, float size, uint32_t color, Fe8HostTextWeight weight, int wrap) {
    if (!c || !c->context || !text || !*text || width <= 0 || height <= 0 || !(size > 0)) return;
    if (!face) { fe8_bitmap_text_draw(c, x, y, width, height, text, size, color, weight, wrap); return; }
    int pixels = size > 256 ? 256 : (int)(size + 0.5f);
    if (pixels < 1) pixels = 1;
    int bold = weight != FE8_HOST_TEXT_REGULAR;
    if (FT_Set_Pixel_Sizes(face, 0, (unsigned)pixels)) return;
    int ascent = (int)((face->size->metrics.ascender + 63) >> 6);
    int line_height = (int)((face->size->metrics.height + 63) >> 6);
    if (line_height < pixels + 2) line_height = pixels + 2;
    int cx = x, cy = y;
    int dots = advance('.', pixels, bold) * 3;
    const unsigned char *p = (const unsigned char *)text;
    int start_word = 1;
    int truncated = !wrap && measure(p, pixels, bold, 0) > width;
    while (*p && cy + pixels <= y + height) {
        if (wrap && start_word && cx > x && *p != ' ' && *p != '\t' && *p != '\n') {
            int next_width = measure(p, pixels, bold, 1);
            if (next_width <= width && cx + next_width > x + width) { cx = x; cy += line_height; }
        }
        unsigned code = decode(&p);
        if (code == '\r') continue;
        if (code == '\n') { if (!wrap) break; cx = x; cy += line_height; start_word = 1; continue; }
        int a = advance(code, pixels, bold);
        if (wrap && cx > x && cx + a > x + width) { cx = x; cy += line_height; }
        if (cy + pixels > y + height) break;
        if (wrap && cx == x && (code == ' ' || code == '\t')) continue;
        if (truncated && cx + a + dots > x + width) {
            for (int i = 0; i < 3; ++i) {
                paint(c, '.', pixels, bold, cx, cy + ascent, x, y, x + width, y + height, color);
                cx += advance('.', pixels, bold);
            }
            break;
        }
        if (code != '\t') paint(c, code, pixels, bold, cx, cy + ascent, x, y, x + width, y + height, color);
        cx += a;
        start_word = code == ' ' || code == '\t';
        if (!wrap && cx >= x + width) break;
    }
}
void fe8_host_text_end(Fe8HostTextCanvas *c) {
    if (!c) return;
    if (!face) { fe8_bitmap_text_end(c); return; }
    memset(c, 0, sizeof(*c));
}
