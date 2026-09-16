/* Exercise the actual platform presenter, including switching back to native
 * rendering. Only synthetic colors are used; this test needs no ROM. */
#include <SDL.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "native_hud.h"
static uint32_t *captured;
static int capture_width, capture_height, captures;
#ifdef __APPLE__
static void capture_swap(SDL_Window *window);
#define SDL_GL_SwapWindow capture_swap
#include "../app/src/host_video_gl.c"
#undef SDL_GL_SwapWindow
static void capture_swap(SDL_Window *window) {
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glReadPixels(0,0,capture_width,capture_height,GL_RGBA,GL_UNSIGNED_BYTE,captured);
    assert(glGetError()==GL_NO_ERROR);
    for(int y=0;y<capture_height/2;++y)for(int x=0;x<capture_width;++x){
        size_t a=(size_t)y*capture_width+x,b=(size_t)(capture_height-y-1)*capture_width+x;
        uint32_t t=captured[a];captured[a]=captured[b];captured[b]=t;
    }
    ++captures;SDL_GL_SwapWindow(window);
}
#else
static void capture_present(SDL_Renderer *renderer);
#define SDL_RenderPresent capture_present
#include "../app/src/host_video_sdl.c"
#undef SDL_RenderPresent
static void capture_present(SDL_Renderer *renderer) {
    int logical_w,logical_h;
    SDL_RenderGetLogicalSize(renderer,&logical_w,&logical_h);
    assert(SDL_RenderSetLogicalSize(renderer,0,0)==0);
    assert(SDL_RenderSetViewport(renderer,NULL)==0);
    assert(SDL_RenderSetScale(renderer,1,1)==0);
    assert(SDL_RenderReadPixels(renderer,NULL,SDL_PIXELFORMAT_RGBA32,captured,capture_width*4)==0);
    assert(SDL_RenderSetLogicalSize(renderer,logical_w,logical_h)==0);
    ++captures;SDL_RenderPresent(renderer);
}
#endif
static void near_color(uint32_t a,uint32_t b) {
    for(unsigned shift=0;shift<24;shift+=8)
        if(abs((int)((a>>shift)&255)-(int)((b>>shift)&255))>2){
            fprintf(stderr,"color mismatch: got %08x expected %08x capture %d size %dx%d\n",a,b,captures,capture_width,capture_height);abort();
        }
}
int main(int argc,char **argv) {
#ifndef __APPLE__
    if(!getenv("DISPLAY")&&!getenv("WAYLAND_DISPLAY"))return 77;
#endif
    assert(SDL_Init(SDL_INIT_VIDEO)==0);
    Fe8HostVideo video;
    assert(fe8_host_video_init(&video,"FE8 scene compositor regression",240,160,0));
    const uint32_t colors[]={0xFF0000FF,0xFF00FF00,0xFFFF0000,0xFF00FFFF};
    uint32_t small[16*12];
    for(int y=0;y<12;++y)for(int x=0;x<16;++x)small[y*16+x]=colors[(y>=6)*2+(x>=8)];
    Fe8VideoOverlay scene={small,16,12};
    for(int pass=0;pass<3;++pass){
        if(pass==1){SDL_RestoreWindow(video.window);SDL_SetWindowSize(video.window,640,420);}
        SDL_PumpEvents();fe8_host_video_refresh_layout(&video);
        capture_width=video.scaling.drawable_width;capture_height=video.scaling.drawable_height;
        size_t n=(size_t)capture_width*capture_height;
        captured=realloc(captured,n*4);assert(captured);
        uint32_t *hud=calloc(n,4);assert(hud);
        uint32_t fg=0x8040B0D0;
        int hx=capture_width/3,hy=capture_height/3;
        for(int y=hy;y<hy+16;++y)for(int x=hx;x<hx+16;++x)hud[(size_t)y*capture_width+x]=fg;
        Fe8VideoOverlay overlay={hud,capture_width,capture_height};
        assert(fe8_host_video_present_scene(&video,&scene,&overlay));
        for(int row=0;row<2;++row)for(int col=0;col<2;++col){
            int x=(col?3:1)*capture_width/4,y=(row?3:1)*capture_height/4;
            near_color(captured[(size_t)y*capture_width+x],colors[row*2+col]);
        }
        near_color(captured[(size_t)(hy+4)*capture_width+hx+4],fe8_native_hud_over(fg,colors[0]));
        assert(!fe8_host_video_present_scene(&video,NULL,&overlay));
        if(pass==2){
            uint32_t tiny[]={0xFF8A5723};Fe8VideoOverlay resized={tiny,1,1};
            assert(fe8_host_video_present_scene(&video,&resized,NULL));
            near_color(captured[0],tiny[0]);near_color(captured[n-1],tiny[0]);
        }
        uint32_t *native=malloc((size_t)video.canvas_width*video.canvas_height*4);assert(native);
        for(int i=0;i<video.canvas_width*video.canvas_height;++i)native[i]=0xFFBD8241;
        assert(fe8_host_video_present(&video,native,NULL));
        near_color(captured[(size_t)(capture_height/2)*capture_width+capture_width/2],native[0]);
        assert(fe8_host_video_present_scene(&video,&scene,&overlay));
        if(argc>1&&pass==0){
            FILE *f=fopen(argv[1],"wb");assert(f);fprintf(f,"P6\n%d %d\n255\n",capture_width,capture_height);
            for(size_t i=0;i<n;++i){unsigned char rgb[]={captured[i]&255,(captured[i]>>8)&255,(captured[i]>>16)&255};fwrite(rgb,1,3,f);}fclose(f);
        }
        free(native);free(hud);
    }
    assert(captures==10);
    free(captured);fe8_host_video_deinit(&video);SDL_Quit();
    puts("PASS real presenter: GPU scaling, upright orientation, straight-alpha HUD, resize, native/scene switching");
    return 0;
}
