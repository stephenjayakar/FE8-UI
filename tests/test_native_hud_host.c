#include "native_hud_host.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

int main(void) {
    Fe8HudHost *host=calloc(1,sizeof(*host)); assert(host);
    Fe8HostVideo video={0};
    video.canvas_width=480;video.canvas_height=320;
    video.scaling.drawable_width=960;video.scaling.drawable_height=640;
    video.scaling.base_pixel_scale=2;
    host->scale_percent=150;host->enabled=true;
    host->hud.count=1;
    host->hud.panels[0]=(Fe8HudPanel){FE8_HUD_UNIT,{0,0,144,48},{0}};
    host->capacity=960*640;host->pixels=calloc(host->capacity,sizeof(*host->pixels));
    assert(host->pixels);host->overlay=(Fe8VideoOverlay){host->pixels,960,640};
    fe8_native_hud_layout(&host->hud,960,640,300,200,150);
    assert(fe8_hud_host_contains(host,&video,10,10));
    assert(!fe8_hud_host_contains(host,&video,300,200));
    /* The same drawable point hits the same panel after map zoom. */
    video.canvas_width=240;video.canvas_height=160;
    assert(fe8_hud_host_contains(host,&video,5,5));
    assert(!fe8_hud_host_contains(host,&video,150,100));
    /* Letterboxed portrait canvas retains physical panel hit testing. */
    video.canvas_width=320;video.canvas_height=320;
    assert(fe8_hud_host_contains(host,&video,0,10));
    video.canvas_width=480;
    Fe8HostSettings settings;fe8_host_settings_init(&settings);
    SDL_Event event={0};event.type=SDL_KEYDOWN;
    event.key.keysym.sym=SDLK_EQUALS;event.key.keysym.scancode=SDL_SCANCODE_EQUALS;
    assert(fe8_hud_host_shortcut(host,&settings,&event));assert(host->scale_percent==160);
    settings.bindings[FE8_HOST_A]=SDL_SCANCODE_EQUALS;
    assert(!fe8_hud_host_shortcut(host,&settings,&event));assert(host->scale_percent==160);
    fe8_host_settings_init(&settings);event.key.keysym.mod=KMOD_CTRL;
    assert(!fe8_hud_host_shortcut(host,&settings,&event));
    event.key.keysym.mod=0;event.key.keysym.sym=SDLK_0;event.key.keysym.scancode=SDL_SCANCODE_0;
    assert(fe8_hud_host_shortcut(host,&settings,&event));assert(host->scale_percent==150);
    host->overlay.pixels=NULL;assert(!fe8_hud_host_shortcut(host,&settings,&event));
    host->overlay.pixels=host->pixels;
    video.canvas_height=320;
    Fe8HostPixel *canvas=malloc(480*320*sizeof(*canvas));assert(canvas);
    for(unsigned p=0;p<480*320;++p)canvas[p]=0xFF0000FF;
    host->pixels[20*960+20]=0xFF00FF00;
    Fe8HostPixel *capture=fe8_hud_host_capture(host,&video,canvas);assert(capture);
    assert(capture[20*960+20]==0xFF00FF00);assert(capture[400*960+500]==0xFF0000FF);
    free(capture);free(canvas);
    fe8_hud_host_pointer(host,&video,479,319); /* clipped bottom-right pointer */
    fe8_hud_host_deinit(host);assert(!host->pixels&&!host->overlay.pixels);free(host);
    puts("HUD zoom-independent hit testing, rebinding, scale shortcuts, capture and cursor clipping passed");
    return 0;
}
