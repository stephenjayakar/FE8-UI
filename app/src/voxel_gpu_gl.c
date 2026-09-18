/* OpenGL 3.2 voxel rasterization. libmGBA still owns all emulation. No GPU
 * readback occurs during interactive rendering; captures explicitly opt in. */
#include "voxel_gpu.h"
#include <SDL.h>
#include <SDL_opengl.h>
#include <SDL_opengl_glext.h>
#include <stddef.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GL_FUNCTIONS(X) \
 X(const GLubyte *,GetString,(GLenum)) \
 X(void,GetIntegerv,(GLenum,GLint *)) \
 X(void,GetBooleanv,(GLenum,GLboolean *)) \
 X(void,GetFloatv,(GLenum,GLfloat *)) \
 X(GLboolean,IsEnabled,(GLenum)) \
 X(GLenum,GetError,(void)) \
 X(void,Enable,(GLenum)) X(void,Disable,(GLenum)) \
 X(void,Viewport,(GLint,GLint,GLsizei,GLsizei)) \
 X(void,ColorMask,(GLboolean,GLboolean,GLboolean,GLboolean)) \
 X(void,ClearColor,(GLfloat,GLfloat,GLfloat,GLfloat)) X(void,Clear,(GLbitfield)) \
 X(void,ClearDepth,(GLdouble)) X(void,DepthFunc,(GLenum)) X(void,DepthMask,(GLboolean)) \
 X(void,BlendFuncSeparate,(GLenum,GLenum,GLenum,GLenum)) X(void,BlendEquationSeparate,(GLenum,GLenum)) \
 X(GLuint,CreateShader,(GLenum)) X(void,ShaderSource,(GLuint,GLsizei,const GLchar *const *,const GLint *)) \
 X(void,CompileShader,(GLuint)) X(void,GetShaderiv,(GLuint,GLenum,GLint *)) \
 X(void,GetShaderInfoLog,(GLuint,GLsizei,GLsizei *,GLchar *)) X(void,DeleteShader,(GLuint)) \
 X(GLuint,CreateProgram,(void)) X(void,AttachShader,(GLuint,GLuint)) X(void,LinkProgram,(GLuint)) \
 X(void,GetProgramiv,(GLuint,GLenum,GLint *)) X(void,GetProgramInfoLog,(GLuint,GLsizei,GLsizei *,GLchar *)) \
 X(void,DeleteProgram,(GLuint)) X(void,UseProgram,(GLuint)) \
 X(GLint,GetUniformLocation,(GLuint,const GLchar *)) X(GLint,GetAttribLocation,(GLuint,const GLchar *)) \
 X(void,Uniform1i,(GLint,GLint)) X(void,UniformMatrix4fv,(GLint,GLsizei,GLboolean,const GLfloat *)) \
 X(void,GenBuffers,(GLsizei,GLuint *)) X(void,DeleteBuffers,(GLsizei,const GLuint *)) \
 X(void,BindBuffer,(GLenum,GLuint)) X(void,BufferData,(GLenum,GLsizeiptr,const void *,GLenum)) \
 X(void,GenVertexArrays,(GLsizei,GLuint *)) X(void,DeleteVertexArrays,(GLsizei,const GLuint *)) \
 X(void,BindVertexArray,(GLuint)) X(void,EnableVertexAttribArray,(GLuint)) \
 X(void,VertexAttribPointer,(GLuint,GLint,GLenum,GLboolean,GLsizei,const void *)) \
 X(void,DrawArrays,(GLenum,GLint,GLsizei)) \
 X(void,ActiveTexture,(GLenum)) X(void,GenTextures,(GLsizei,GLuint *)) \
 X(void,DeleteTextures,(GLsizei,const GLuint *)) X(void,BindTexture,(GLenum,GLuint)) \
 X(void,TexParameteri,(GLenum,GLenum,GLint)) X(void,PixelStorei,(GLenum,GLint)) \
 X(void,TexImage2D,(GLenum,GLint,GLint,GLsizei,GLsizei,GLint,GLenum,GLenum,const void *)) \
 X(void,TexSubImage2D,(GLenum,GLint,GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,const void *)) \
 X(void,GenFramebuffers,(GLsizei,GLuint *)) X(void,DeleteFramebuffers,(GLsizei,const GLuint *)) \
 X(void,BindFramebuffer,(GLenum,GLuint)) X(void,FramebufferTexture2D,(GLenum,GLenum,GLenum,GLuint,GLint)) \
 X(GLenum,CheckFramebufferStatus,(GLenum)) \
 X(void,GenRenderbuffers,(GLsizei,GLuint *)) X(void,DeleteRenderbuffers,(GLsizei,const GLuint *)) \
 X(void,BindRenderbuffer,(GLenum,GLuint)) X(void,RenderbufferStorage,(GLenum,GLenum,GLsizei,GLsizei)) \
 X(void,FramebufferRenderbuffer,(GLenum,GLenum,GLenum,GLuint)) \
 X(void,BlitFramebuffer,(GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLbitfield,GLenum)) \
 X(void,ReadPixels,(GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,void *))

typedef struct Gl {
#define DECLARE(result,name,args) result (APIENTRY *name) args;
    GL_FUNCTIONS(DECLARE)
#undef DECLARE
} Gl;
struct Fe8VoxelGl {
    Gl gl;
    GLuint program, vao, buffers[4], textures[3], framebuffer, depth;
    GLint position, uv, rgba, transform, textured, sampler;
    int width, height, ground_width, ground_height;
    const Fe8VoxelGpuFrame *owner;
    uint64_t revisions[5];
    float last_transform[16];
    bool valid, allocated;
};

static GLuint shader(Gl *g, GLenum type, const char *source) {
    GLuint id=g->CreateShader(type);GLint ok=0;
    g->ShaderSource(id,1,&source,NULL);g->CompileShader(id);g->GetShaderiv(id,GL_COMPILE_STATUS,&ok);
    if(!ok){char log[2048]={0};g->GetShaderInfoLog(id,sizeof(log),NULL,log);
        SDL_SetError("Voxel OpenGL shader: %s",log);g->DeleteShader(id);return 0;}
    return id;
}
Fe8VoxelGl *fe8_voxel_gl_create(void) {
    if(!SDL_GL_GetCurrentContext()){SDL_SetError("Voxel GPU requires an OpenGL context");return NULL;}
    Fe8VoxelGl *v=calloc(1,sizeof(*v));if(!v)return NULL;
    Gl *g=&v->gl;
#define LOAD(result,name,args) do { void *p=SDL_GL_GetProcAddress("gl" #name); \
        if(!p){SDL_SetError("Voxel OpenGL missing gl%s",#name);free(v);return NULL;} \
        memcpy(&g->name,&p,sizeof(p)); } while(0);
    GL_FUNCTIONS(LOAD)
#undef LOAD
    int major=0,minor=0;
    const char *version=(const char *)g->GetString(GL_VERSION);
    if(!version || sscanf(version,"%d.%d",&major,&minor)!=2 || major<3 || (major==3&&minor<2)){
        SDL_SetError("Voxel GPU requires OpenGL 3.2 (found %s)",version?version:"none");free(v);return NULL;}
    const char *vs="#version 150\n"
        "in vec3 position; in vec2 texcoord; in vec4 rgba; uniform mat4 transform;\n"
        "out vec2 uv; out vec4 tint; void main(){ gl_Position=transform*vec4(position,1);uv=texcoord;tint=rgba;}\n";
    const char *fs="#version 150\n"
        "in vec2 uv; in vec4 tint; uniform sampler2D image; uniform int textured; out vec4 color;\n"
        "void main(){ color=tint; if(textured!=0)color*=texture(image,uv); if(color.a<=0.001)discard; }\n";
    GLuint a=shader(g,GL_VERTEX_SHADER,vs),b=shader(g,GL_FRAGMENT_SHADER,fs);
    if(!a||!b){if(a)g->DeleteShader(a);if(b)g->DeleteShader(b);free(v);return NULL;}
    v->program=g->CreateProgram();g->AttachShader(v->program,a);g->AttachShader(v->program,b);
    g->LinkProgram(v->program);g->DeleteShader(a);g->DeleteShader(b);
    GLint ok=0;g->GetProgramiv(v->program,GL_LINK_STATUS,&ok);
    if(!ok){char log[2048]={0};g->GetProgramInfoLog(v->program,sizeof(log),NULL,log);
        SDL_SetError("Voxel OpenGL link: %s",log);g->DeleteProgram(v->program);free(v);return NULL;}
    v->position=g->GetAttribLocation(v->program,"position");v->uv=g->GetAttribLocation(v->program,"texcoord");
    v->rgba=g->GetAttribLocation(v->program,"rgba");v->transform=g->GetUniformLocation(v->program,"transform");
    v->textured=g->GetUniformLocation(v->program,"textured");v->sampler=g->GetUniformLocation(v->program,"image");
    g->GenVertexArrays(1,&v->vao);g->GenBuffers(4,v->buffers);g->GenTextures(3,v->textures);
    g->GenFramebuffers(1,&v->framebuffer);g->GenRenderbuffers(1,&v->depth);
    fprintf(stderr,"Voxel GPU: OpenGL %s / %s\n",version,g->GetString(GL_RENDERER));
    return v;
}
void fe8_voxel_gl_destroy(Fe8VoxelGl *v) {
    if(!v)return;
    Gl *g=&v->gl;
    g->DeleteBuffers(4,v->buffers);g->DeleteVertexArrays(1,&v->vao);g->DeleteTextures(3,v->textures);
    g->DeleteFramebuffers(1,&v->framebuffer);g->DeleteRenderbuffers(1,&v->depth);
    g->DeleteProgram(v->program);free(v);
}
/* The presenter may share its context with SDL/mGBA. Restore every piece of
 * GL state touched here, including bindings and pixel-store strides. */
static const GLenum caps[]={GL_BLEND,GL_DEPTH_TEST,GL_CULL_FACE,GL_SCISSOR_TEST,GL_STENCIL_TEST,GL_FRAMEBUFFER_SRGB};
typedef struct Saved {
    GLint program,vao,buffer,draw_fb,read_fb,renderbuffer,active,texture,viewport[4];
    GLint unpack,unpack_row,unpack_buffer,depth_func,src_rgb,dst_rgb,src_alpha,dst_alpha,eq_rgb,eq_alpha;
    GLboolean enabled[6],depth_mask,color_mask[4];GLfloat clear[4],clear_depth;
} Saved;
static void save(Gl *g,Saved *s){
    g->GetIntegerv(GL_CURRENT_PROGRAM,&s->program);g->GetIntegerv(GL_VERTEX_ARRAY_BINDING,&s->vao);
    g->GetIntegerv(GL_ARRAY_BUFFER_BINDING,&s->buffer);g->GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&s->draw_fb);
    g->GetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&s->read_fb);g->GetIntegerv(GL_RENDERBUFFER_BINDING,&s->renderbuffer);
    g->GetIntegerv(GL_ACTIVE_TEXTURE,&s->active);g->ActiveTexture(GL_TEXTURE0);g->GetIntegerv(GL_TEXTURE_BINDING_2D,&s->texture);
    g->GetIntegerv(GL_VIEWPORT,s->viewport);g->GetIntegerv(GL_UNPACK_ALIGNMENT,&s->unpack);
    g->GetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&s->unpack_buffer);
    g->GetIntegerv(GL_UNPACK_ROW_LENGTH,&s->unpack_row);g->GetIntegerv(GL_DEPTH_FUNC,&s->depth_func);
    g->GetIntegerv(GL_BLEND_SRC_RGB,&s->src_rgb);g->GetIntegerv(GL_BLEND_DST_RGB,&s->dst_rgb);
    g->GetIntegerv(GL_BLEND_SRC_ALPHA,&s->src_alpha);g->GetIntegerv(GL_BLEND_DST_ALPHA,&s->dst_alpha);
    g->GetIntegerv(GL_BLEND_EQUATION_RGB,&s->eq_rgb);g->GetIntegerv(GL_BLEND_EQUATION_ALPHA,&s->eq_alpha);
    g->GetBooleanv(GL_DEPTH_WRITEMASK,&s->depth_mask);g->GetBooleanv(GL_COLOR_WRITEMASK,s->color_mask);
    g->GetFloatv(GL_COLOR_CLEAR_VALUE,s->clear);g->GetFloatv(GL_DEPTH_CLEAR_VALUE,&s->clear_depth);
    for(unsigned i=0;i<6;++i)s->enabled[i]=g->IsEnabled(caps[i]);
}
static void restore(Gl *g,const Saved *s){
    g->UseProgram(s->program);g->BindVertexArray(s->vao);g->BindBuffer(GL_ARRAY_BUFFER,s->buffer);
    g->BindFramebuffer(GL_DRAW_FRAMEBUFFER,s->draw_fb);g->BindFramebuffer(GL_READ_FRAMEBUFFER,s->read_fb);
    g->BindRenderbuffer(GL_RENDERBUFFER,s->renderbuffer);g->BindTexture(GL_TEXTURE_2D,s->texture);g->ActiveTexture(s->active);
    g->Viewport(s->viewport[0],s->viewport[1],s->viewport[2],s->viewport[3]);
    g->BindBuffer(GL_PIXEL_UNPACK_BUFFER,(GLuint)s->unpack_buffer);
    g->PixelStorei(GL_UNPACK_ALIGNMENT,s->unpack);g->PixelStorei(GL_UNPACK_ROW_LENGTH,s->unpack_row);
    g->DepthFunc(s->depth_func);g->DepthMask(s->depth_mask);
    g->ColorMask(s->color_mask[0],s->color_mask[1],s->color_mask[2],s->color_mask[3]);
    g->ClearColor(s->clear[0],s->clear[1],s->clear[2],s->clear[3]);g->ClearDepth(s->clear_depth);
    g->BlendFuncSeparate(s->src_rgb,s->dst_rgb,s->src_alpha,s->dst_alpha);g->BlendEquationSeparate(s->eq_rgb,s->eq_alpha);
    for(unsigned i=0;i<6;++i){if(s->enabled[i])g->Enable(caps[i]);else g->Disable(caps[i]);}
}
static void texture(Gl *g,GLuint id,int w,int h,const void *pixels,bool allocate){
    g->BindTexture(GL_TEXTURE_2D,id);
    g->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);g->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    g->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);g->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    if(allocate)g->TexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    else g->TexSubImage2D(GL_TEXTURE_2D,0,0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
}
static void draw(Fe8VoxelGl *v,GLuint buffer,size_t count){
    Gl *g=&v->gl;g->BindBuffer(GL_ARRAY_BUFFER,buffer);
    g->EnableVertexAttribArray(v->position);g->EnableVertexAttribArray(v->uv);g->EnableVertexAttribArray(v->rgba);
    g->VertexAttribPointer(v->position,3,GL_FLOAT,GL_FALSE,sizeof(Fe8GpuVertex),(void *)offsetof(Fe8GpuVertex,x));
    g->VertexAttribPointer(v->uv,2,GL_FLOAT,GL_FALSE,sizeof(Fe8GpuVertex),(void *)offsetof(Fe8GpuVertex,u));
    g->VertexAttribPointer(v->rgba,4,GL_UNSIGNED_BYTE,GL_TRUE,sizeof(Fe8GpuVertex),(void *)offsetof(Fe8GpuVertex,rgba));
    g->DrawArrays(GL_TRIANGLES,0,(GLsizei)count);
}
int fe8_voxel_gl_draw(Fe8VoxelGl *v,const Fe8VoxelGpuFrame *f,int dw,int dh){
    if(!v||!f||!f->ground||!f->atlas||f->width<1||f->height<1||f->width>1920||f->height>1080||
            dw<1||dh<1||dw>16384||dh>16384||f->map_width<1||f->map_height<1||
            f->map_width>1024||f->map_height>1024||f->atlas_width!=512||f->atlas_height!=256){SDL_SetError("Invalid voxel GPU frame dimensions or textures");return 0;}
    for(unsigned i=0;i<16;++i)if(!isfinite(f->transform[i])){SDL_SetError("Invalid voxel GPU camera transform");return 0;}
    const Fe8GpuMesh *meshes[]={&f->scenery,&f->overlays,&f->billboards};
    for(unsigned i=0;i<3;++i)if(meshes[i]->count>3*1024*1024||meshes[i]->count%3||
            (meshes[i]->count&&!meshes[i]->vertices))return 0;
    Gl *g=&v->gl;Saved old;save(g,&old);int ok=1;
    if(v->owner!=f){v->valid=false;v->owner=f;}
    uint64_t revisions[]={f->scenery.revision,f->overlays.revision,f->billboards.revision,f->ground_revision,f->atlas_revision};
    bool resized=v->width!=f->width||v->height!=f->height;
    bool dirty=!v->valid||resized||memcmp(revisions,v->revisions,sizeof(revisions))||memcmp(f->transform,v->last_transform,sizeof(f->transform));
    g->Disable(GL_SCISSOR_TEST);g->Disable(GL_STENCIL_TEST);g->Disable(GL_FRAMEBUFFER_SRGB);
    g->BindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
    g->ColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);g->PixelStorei(GL_UNPACK_ALIGNMENT,4);g->PixelStorei(GL_UNPACK_ROW_LENGTH,0);
    if(resized){
        texture(g,v->textures[2],f->width,f->height,NULL,true);
        g->BindFramebuffer(GL_FRAMEBUFFER,v->framebuffer);
        g->FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,v->textures[2],0);
        g->BindRenderbuffer(GL_RENDERBUFFER,v->depth);g->RenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,f->width,f->height);
        g->FramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,v->depth);
        if(g->CheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){SDL_SetError("Voxel OpenGL framebuffer allocation failed");ok=0;goto done;}
        v->width=f->width;v->height=f->height;
    }
    if(dirty){
        for(unsigned i=0;i<3;++i)if(!v->valid||v->revisions[i]!=revisions[i]){
            g->BindBuffer(GL_ARRAY_BUFFER,v->buffers[i]);
            g->BufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(meshes[i]->count*sizeof(Fe8GpuVertex)),meshes[i]->vertices,i?GL_STREAM_DRAW:GL_STATIC_DRAW);
        }
        if(!v->valid||v->revisions[3]!=revisions[3]){
            texture(g,v->textures[0],f->map_width,f->map_height,f->ground,
                !v->allocated||v->ground_width!=f->map_width||v->ground_height!=f->map_height);
            v->ground_width=f->map_width;v->ground_height=f->map_height;
            float w=(float)f->map_width,h=(float)f->map_height;
            Fe8GpuVertex ground[]={{0,0,0,0,0,~0u},{w,0,0,1,0,~0u},{w,0,h,1,1,~0u},
                {0,0,0,0,0,~0u},{w,0,h,1,1,~0u},{0,0,h,0,1,~0u}};
            g->BindBuffer(GL_ARRAY_BUFFER,v->buffers[3]);g->BufferData(GL_ARRAY_BUFFER,sizeof(ground),ground,GL_STATIC_DRAW);
        }
        if(!v->valid||v->revisions[4]!=revisions[4])texture(g,v->textures[1],512,256,f->atlas,!v->allocated);
        v->allocated=true;
        g->BindFramebuffer(GL_FRAMEBUFFER,v->framebuffer);g->Viewport(0,0,f->width,f->height);
        g->ClearColor(35.f/255,40.f/255,43.f/255,1);g->ClearDepth(1);g->DepthMask(GL_TRUE);
        g->Clear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);g->Enable(GL_DEPTH_TEST);g->DepthFunc(GL_LEQUAL);
        g->Disable(GL_CULL_FACE);g->Disable(GL_BLEND);g->BindVertexArray(v->vao);g->UseProgram(v->program);
        g->UniformMatrix4fv(v->transform,1,GL_FALSE,f->transform);g->Uniform1i(v->sampler,0);
        g->Uniform1i(v->textured,1);g->BindTexture(GL_TEXTURE_2D,v->textures[0]);draw(v,v->buffers[3],6);
        g->Uniform1i(v->textured,0);draw(v,v->buffers[0],f->scenery.count);
        g->Enable(GL_BLEND);g->BlendEquationSeparate(GL_FUNC_ADD,GL_FUNC_ADD);
        g->BlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
        g->DepthMask(GL_FALSE);draw(v,v->buffers[1],f->overlays.count);g->DepthMask(GL_TRUE);
        g->Disable(GL_BLEND);g->Uniform1i(v->textured,1);g->BindTexture(GL_TEXTURE_2D,v->textures[1]);draw(v,v->buffers[2],f->billboards.count);
        GLenum error=g->GetError();if(error){SDL_SetError("Voxel OpenGL draw error 0x%x",error);ok=0;goto done;}
        memcpy(v->revisions,revisions,sizeof(revisions));memcpy(v->last_transform,f->transform,sizeof(f->transform));v->valid=true;
    }
    g->BindFramebuffer(GL_READ_FRAMEBUFFER,v->framebuffer);g->BindFramebuffer(GL_DRAW_FRAMEBUFFER,old.draw_fb);
    g->BlitFramebuffer(0,0,f->width,f->height,0,0,dw,dh,GL_COLOR_BUFFER_BIT,GL_NEAREST);
done:
    if(!ok)v->valid=false;
    restore(g,&old);return ok;
}
int fe8_voxel_gl_capture(Fe8VoxelGl *v,Fe8HostPixel *pixels,int w,int h){
    if(!v||!pixels||w<1||h<1||w>16384||h>16384||(size_t)w*h>8192*4320)return 0;
    Gl *g=&v->gl;GLint pack,row,buffer;g->GetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&buffer);
    g->BindBuffer(GL_PIXEL_PACK_BUFFER,0);g->GetIntegerv(GL_PACK_ALIGNMENT,&pack);g->GetIntegerv(GL_PACK_ROW_LENGTH,&row);
    g->PixelStorei(GL_PACK_ALIGNMENT,4);g->PixelStorei(GL_PACK_ROW_LENGTH,0);
    g->ReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    g->PixelStorei(GL_PACK_ALIGNMENT,pack);g->PixelStorei(GL_PACK_ROW_LENGTH,row);
    g->BindBuffer(GL_PIXEL_PACK_BUFFER,(GLuint)buffer);
    /* GL origin is bottom-left; host pixel origin is top-left. */
    for(int y=0;y<h/2;++y)for(int x=0;x<w;++x){size_t a=(size_t)y*w+x,b=(size_t)(h-1-y)*w+x;
        uint32_t p=pixels[a];pixels[a]=pixels[b];pixels[b]=p;}
    return g->GetError()==GL_NO_ERROR;
}
