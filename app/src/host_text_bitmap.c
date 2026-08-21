#include "host_text.h"
#include "font8x8_basic.h"

#include <stddef.h>
#include <stdint.h>

typedef Fe8HostTextCanvas Fe8BitmapTextContext;

typedef struct Fe8GlyphMetrics {
    int left;
    int width;
    int advance;
} Fe8GlyphMetrics;

static Fe8GlyphMetrics glyph_metrics(unsigned char ch, int scale) {
    Fe8GlyphMetrics metrics;
    int left = 8;
    int right = -1;
    int row;
    int column;

    if (ch >= 128) ch = '?';
    for (row = 0; row < 8; ++row) {
        unsigned char bits = font8x8_basic[ch][row];
        for (column = 0; column < 8; ++column) {
            if (bits & (unsigned char)(1u << column)) {
                if (column < left) left = column;
                if (column > right) right = column;
            }
        }
    }
    if (right < left) {
        metrics.left = 0;
        metrics.width = 0;
        metrics.advance = 4 * scale;
    } else {
        metrics.left = left;
        metrics.width = (right - left + 1) * scale;
        metrics.advance = metrics.width + scale;
    }
    return metrics;
}

static uint32_t blend_abgr(uint32_t destination, uint32_t source) {
    unsigned source_alpha = source >> 24;
    unsigned inverse_alpha;
    unsigned red;
    unsigned green;
    unsigned blue;
    unsigned destination_alpha;
    unsigned output_alpha;

    if (source_alpha == 255) return source;
    if (!source_alpha) return destination;
    inverse_alpha = 255 - source_alpha;
    red = ((source & 0xFFu) * source_alpha +
        (destination & 0xFFu) * inverse_alpha + 127) / 255;
    green = (((source >> 8) & 0xFFu) * source_alpha +
        ((destination >> 8) & 0xFFu) * inverse_alpha + 127) / 255;
    blue = (((source >> 16) & 0xFFu) * source_alpha +
        ((destination >> 16) & 0xFFu) * inverse_alpha + 127) / 255;
    destination_alpha = destination >> 24;
    output_alpha = source_alpha +
        (destination_alpha * inverse_alpha + 127) / 255;
    return (output_alpha << 24) | (blue << 16) | (green << 8) | red;
}

static void put_pixel(Fe8BitmapTextContext *context, int x, int y,
    int clip_left, int clip_top, int clip_right, int clip_bottom,
    uint32_t color) {
    uint32_t *pixel;
    if (x < clip_left || x >= clip_right || y < clip_top || y >= clip_bottom ||
        x < 0 || x >= context->width || y < 0 || y >= context->height)
        return;
    pixel = &context->pixels[y * context->stride + x];
    *pixel = blend_abgr(*pixel, color);
}

static void draw_glyph(Fe8BitmapTextContext *context, unsigned char ch,
    int x, int y, int scale, uint32_t color, Fe8HostTextWeight weight,
    int clip_left, int clip_top, int clip_right, int clip_bottom) {
    Fe8GlyphMetrics metrics = glyph_metrics(ch, scale);
    int row;
    int column;
    int pixel_y;
    int pixel_x;
    int embolden = weight == FE8_HOST_TEXT_REGULAR ? 0 : 1;

    if (!metrics.width) return;
    for (row = 0; row < 8; ++row) {
        unsigned char bits = font8x8_basic[ch < 128 ? ch : '?'][row];
        for (column = metrics.left;
             column < metrics.left + metrics.width / scale; ++column) {
            if (!(bits & (unsigned char)(1u << column))) continue;
            for (pixel_y = 0; pixel_y < scale; ++pixel_y) {
                for (pixel_x = 0; pixel_x < scale; ++pixel_x) {
                    int destination_x = x + (column - metrics.left) * scale + pixel_x;
                    int destination_y = y + row * scale + pixel_y;
                    put_pixel(context, destination_x, destination_y,
                        clip_left, clip_top, clip_right, clip_bottom, color);
                    if (embolden) {
                        put_pixel(context, destination_x + 1, destination_y,
                            clip_left, clip_top, clip_right, clip_bottom, color);
                    }
                }
            }
        }
    }
}

static const unsigned char *next_character(const unsigned char *text,
    unsigned char *character) {
    unsigned char ch = *text++;
    if (ch < 0x80) {
        *character = ch;
        return text;
    }
    while ((*text & 0xC0u) == 0x80u) ++text;
    *character = '?';
    return text;
}

static int character_advance(unsigned char ch, int scale) {
    if (ch == '\t') return glyph_metrics(' ', scale).advance * 4;
    return glyph_metrics(ch, scale).advance;
}

static int word_width(const unsigned char *text, int scale) {
    int width = 0;
    unsigned char ch;
    while (*text && *text != ' ' && *text != '\t' && *text != '\r' &&
           *text != '\n') {
        text = next_character(text, &ch);
        width += character_advance(ch, scale);
    }
    return width;
}

int fe8_host_text_begin(Fe8HostTextCanvas *canvas, uint32_t *pixels,
    int stride, int width, int height) {
    if (!canvas || !pixels || stride < width || width <= 0 || height <= 0)
        return 0;
    canvas->context = canvas;
    canvas->pixels = pixels;
    canvas->stride = stride;
    canvas->width = width;
    canvas->height = height;
    return 1;
}

void fe8_host_text_draw(Fe8HostTextCanvas *canvas, int x, int y,
    int width, int height, const char *text, float size, uint32_t color,
    Fe8HostTextWeight weight, int wrap) {
    Fe8BitmapTextContext *context;
    const unsigned char *cursor;
    int scale;
    int line_height;
    int origin_x;
    int cursor_x;
    int cursor_y;
    int clip_left;
    int clip_top;
    int clip_right;
    int clip_bottom;
    int at_word_start;

    if (!canvas || !canvas->context || !text || !*text ||
        width <= 0 || height <= 0 || size <= 0.0f)
        return;
    context = (Fe8BitmapTextContext *)canvas;
    scale = (int)(size / 8.0f);
    if (scale < 1) scale = 1;
    line_height = 9 * scale;
    origin_x = x;
    cursor_x = x;
    cursor_y = y;
    clip_left = x < 0 ? 0 : x;
    clip_top = y < 0 ? 0 : y;
    clip_right = x + width;
    clip_bottom = y + height;
    if (clip_right > context->width) clip_right = context->width;
    if (clip_bottom > context->height) clip_bottom = context->height;
    if (clip_left >= clip_right || clip_top >= clip_bottom) return;

    at_word_start = 1;
    cursor = (const unsigned char *)text;
    while (*cursor && cursor_y + 8 * scale <= clip_bottom) {
        unsigned char ch;
        int advance;

        if (wrap && at_word_start && cursor_x > origin_x &&
            *cursor != ' ' && *cursor != '\t' && *cursor != '\r' &&
            *cursor != '\n') {
            int remaining_word = word_width(cursor, scale);
            if (remaining_word <= width && cursor_x + remaining_word > clip_right) {
                cursor_x = origin_x;
                cursor_y += line_height;
                if (cursor_y + 8 * scale > clip_bottom) break;
            }
        }

        cursor = next_character(cursor, &ch);
        if (ch == '\r') continue;
        if (ch == '\n') {
            cursor_x = origin_x;
            cursor_y += line_height;
            at_word_start = 1;
            continue;
        }
        if (ch == '\t') {
            advance = character_advance(ch, scale);
            if (wrap && cursor_x > origin_x && cursor_x + advance > clip_right) {
                cursor_x = origin_x;
                cursor_y += line_height;
            } else {
                cursor_x += advance;
            }
            at_word_start = 1;
            continue;
        }
        if (wrap && ch == ' ' && cursor_x == origin_x) {
            at_word_start = 1;
            continue;
        }
        advance = character_advance(ch, scale);
        if (wrap && cursor_x > origin_x && cursor_x + advance > clip_right) {
            cursor_x = origin_x;
            cursor_y += line_height;
            if (ch == ' ') continue;
        }
        if (cursor_y + 8 * scale > clip_bottom) break;
        if (cursor_x < clip_right) {
            draw_glyph(context, ch, cursor_x, cursor_y, scale, color, weight,
                clip_left, clip_top, clip_right, clip_bottom);
        }
        cursor_x += advance;
        at_word_start = ch == ' ';
        if (!wrap && cursor_x >= clip_right) break;
    }
}

void fe8_host_text_end(Fe8HostTextCanvas *canvas) {
    if (!canvas || !canvas->context) return;
    canvas->context = NULL;
    canvas->pixels = NULL;
    canvas->stride = 0;
    canvas->width = 0;
    canvas->height = 0;
}
