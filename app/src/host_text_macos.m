#import "host_text.h"

#import <CoreGraphics/CoreGraphics.h>
#import <CoreText/CoreText.h>

int fe8_host_text_begin(Fe8HostTextCanvas *canvas, uint32_t *pixels,
    int stride, int width, int height) {
    CGColorSpaceRef color_space;
    CGContextRef context;
    if (!canvas || !pixels || stride < width || width <= 0 || height <= 0)
        return 0;
    color_space = CGColorSpaceCreateDeviceRGB();
    context = CGBitmapContextCreate(pixels, (size_t)width, (size_t)height, 8,
        (size_t)stride * sizeof(*pixels), color_space,
        kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst);
    CGColorSpaceRelease(color_space);
    if (!context)
        return 0;
    CGContextSetShouldAntialias(context, true);
    CGContextSetShouldSmoothFonts(context, true);
    canvas->context = context;
    canvas->pixels = pixels;
    canvas->stride = stride;
    canvas->width = width;
    canvas->height = height;
    return 1;
}

static CTFontRef system_font(float size, Fe8HostTextWeight weight) {
    CTFontUIFontType type = weight == FE8_HOST_TEXT_REGULAR ?
        kCTFontUIFontSystem : kCTFontUIFontEmphasizedSystem;
    return CTFontCreateUIFontForLanguage(type, size, NULL);
}

void fe8_host_text_draw(Fe8HostTextCanvas *canvas, int x, int y,
    int width, int height, const char *text, float size, uint32_t color,
    Fe8HostTextWeight weight, int wrap) {
    CGContextRef context;
    CTFontRef font;
    CGColorSpaceRef color_space;
    CGColorRef foreground;
    CFStringRef string;
    CFAttributedStringRef attributed;
    CFMutableDictionaryRef attributes;
    CGFloat components[4];
    if (!canvas || !canvas->context || !text || !*text || width <= 0 || height <= 0)
        return;
    context = (CGContextRef)canvas->context;
    font = system_font(size, weight);
    components[0] = (CGFloat)(color & 0xFF) / 255.0;
    components[1] = (CGFloat)((color >> 8) & 0xFF) / 255.0;
    components[2] = (CGFloat)((color >> 16) & 0xFF) / 255.0;
    components[3] = (CGFloat)((color >> 24) & 0xFF) / 255.0;
    color_space = CGColorSpaceCreateDeviceRGB();
    foreground = CGColorCreate(color_space, components);
    CGColorSpaceRelease(color_space);
    attributes = CFDictionaryCreateMutable(NULL, 2,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(attributes, kCTFontAttributeName, font);
    CFDictionarySetValue(attributes, kCTForegroundColorAttributeName, foreground);
    string = CFStringCreateWithCString(NULL, text, kCFStringEncodingUTF8);
    attributed = CFAttributedStringCreate(NULL, string, attributes);
    CGContextSaveGState(context);
    CGContextSetTextMatrix(context, CGAffineTransformIdentity);
    /* Core Text does not honor CTLine's requested width by itself. Clip every
       draw to the caller's box so adjacent inventory columns cannot bleed
       into one another on the macOS renderer. */
    CGContextClipToRect(context,
        CGRectMake(x, canvas->height - y - height, width, height));
    if (wrap) {
        CTFramesetterRef framesetter = CTFramesetterCreateWithAttributedString(attributed);
        CGMutablePathRef path = CGPathCreateMutable();
        CTFrameRef frame;
        CGPathAddRect(path, NULL, CGRectMake(x, canvas->height - y - height, width, height));
        frame = CTFramesetterCreateFrame(framesetter, CFRangeMake(0, 0), path, NULL);
        CTFrameDraw(frame, context);
        CFRelease(frame);
        CGPathRelease(path);
        CFRelease(framesetter);
    } else {
        CTLineRef line = CTLineCreateWithAttributedString(attributed);
        CGFloat ascent = 0;
        CTLineGetTypographicBounds(line, &ascent, NULL, NULL);
        CGContextSetTextPosition(context, x, canvas->height - y - ascent);
        CTLineDraw(line, context);
        CFRelease(line);
    }
    CGContextRestoreGState(context);
    CFRelease(attributed);
    CFRelease(string);
    CFRelease(attributes);
    CGColorRelease(foreground);
    CFRelease(font);
}

void fe8_host_text_end(Fe8HostTextCanvas *canvas) {
    if (canvas && canvas->context) {
        CGContextFlush((CGContextRef)canvas->context);
        CGContextRelease((CGContextRef)canvas->context);
        canvas->context = NULL;
        canvas->pixels = NULL;
        canvas->stride = 0;
        canvas->width = 0;
        canvas->height = 0;
    }
}
