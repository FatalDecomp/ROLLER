#include "scene_render_gpu.h"
#include "crt_filter.h"
#include "debug_overlay.h"
#include "polytex.h"              /* startsx, startsy */
#include "sound.h"                /* pal_addr */
#include "game_scene_shaders.h"   /* scene vertex/pixel SPIRV+MSL */
#include "menu_shaders.h"         /* menu_mesh_* — reused for car pipeline */
#include "game_hud_shaders.h"     /* HUD overlay vertex/pixel */
#include "3d.h"                   /* wide_on */
#include "roller.h"               /* ROLLERTryAcquireGPUSwapchainTexture */
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>

/* palette[256]: correctly filled by setpal(); use instead of pal_addr
 * (pal_addr is the raw 3-byte-per-entry file buffer, which gives wrong G/B
 * channels when read as tColor* because byB and byG are swapped in memory
 * relative to the raw R,G,B file order). */
extern tColor palette[256];

/* --------------------------------------------------------------------------
 * Limits
 * -------------------------------------------------------------------------- */
#define SCENE_GPU_MAX_TEXTURE_SLOTS  64
#define SCENE_GPU_MAX_VERTICES       300000
#define SCENE_GPU_MAX_DRAW_CMDS      8192
#define SCENE_GPU_MAX_CAR_DRAWS      32   /* 16 cars × up to 2 draws each (body + shadow) */
#define SCENE_GPU_NEAR               80.0f
#define SCENE_GPU_FAR                20000000.0f

/* --------------------------------------------------------------------------
 * Vertex layouts
 * -------------------------------------------------------------------------- */
typedef struct {
    float x, y, z;
    float u, v;
} SceneGPUVertex;

/* Matches MeshVertex in menu_render_gpu.c / GRHWMeshVertex in game_render_hardware.c
 * (same car pipeline shaders — layout must stay identical). */
typedef struct {
    float position[3];
    float uv[2];
    float color[4];
} SceneGPUMeshVertex;

/* 2D NDC vertex for HUD blit (positions already in NDC [-1,1]). */
typedef struct {
    float x, y;
    float u, v;
} SceneGPUHUDVertex;

/* --------------------------------------------------------------------------
 * Per-texture slot — one SDL_GPUTexture per tile rather than one atlas.
 * CLAMP_TO_EDGE sampler prevents anisotropic footprint from bleeding across
 * tile boundaries.  Wall surfaces need tiles N and N+1 side by side, so we
 * also store pair textures (2*tileSize wide) for each valid adjacent pair.
 * -------------------------------------------------------------------------- */
#define SCENE_GPU_MAX_TILES_PER_SLOT 256

typedef struct {
    SDL_GPUTexture *tileTextures[SCENE_GPU_MAX_TILES_PER_SLOT];
    /* pairTextures[n] = tile n left-half + tile n+1 right-half (for walls).
     * NULL when n is the last tile in its atlas row (no valid neighbour). */
    SDL_GPUTexture *pairTextures[SCENE_GPU_MAX_TILES_PER_SLOT];
    int             numTiles;
    int             tileSize;   /* 32 for SVGA, 64 for VGA */
    int             tilesPerRow;
    int             tex_idx;
    int             texHalfRes;
    int             in_use;
} SceneGPUTextureSlot;

/* --------------------------------------------------------------------------
 * Deferred draw commands
 * -------------------------------------------------------------------------- */
typedef enum { SCENE_GPU_DRAW_OPAQUE = 0, SCENE_GPU_DRAW_BLEND, SCENE_GPU_DRAW_BUILDING, SCENE_GPU_DRAW_SIGN, SCENE_GPU_DRAW_WALL, SCENE_GPU_DRAW_SHADOW } SceneGPUDrawKind;

typedef struct {
    SDL_GPUTexture  *texture;
    int              vertexStart;
    int              vertexCount;
    SceneGPUDrawKind kind;
    bool             forceNearest;
    float            mvp[16];
} SceneGPUDrawCmd;

typedef struct {
    SDL_GPUBuffer  *vertBuf;
    SDL_GPUBuffer  *idxBuf;
    SDL_GPUTexture *texture;
    int             firstIndex;
    int             idxCount;
    float           mvp[16];
} SceneGPUCarDrawCmd;

/* --------------------------------------------------------------------------
 * Main renderer struct
 * -------------------------------------------------------------------------- */
struct SceneRendererGPU {
    SDL_GPUDevice  *device;
    SDL_Window     *window;

    /* ---- 3D scene pipeline (SceneGPUVertex: pos[3] + uv[2]) ---- */
    SDL_GPUGraphicsPipeline *opaquePipeline;
    SDL_GPUGraphicsPipeline *blendPipeline;
    SDL_GPUGraphicsPipeline *buildingPipeline; /* opaque + LESS_OR_EQUAL + depth bias */
    SDL_GPUGraphicsPipeline *signPipeline;     /* COMPARE_ALWAYS + depth_write; drawn first so walls (COMPARE_LESS) can't overdraw */
    SDL_GPUGraphicsPipeline *wallPipeline;     /* sfBl (no alpha-discard) + no blend + COMPARE_LESS; for TEXTURE_PAIR track surfaces whose tiles use palette-0 as a solid colour */
    SDL_GPUGraphicsPipeline *shadowPipeline;   /* blend + LESS_OR_EQUAL + no depth write + large bias; for car shadow quads coplanar with road */
    SDL_GPUSampler          *sampler;
    SDL_GPUSampler          *samplerNearest; /* always-nearest, used for cloud quads */

    SDL_GPUTexture *depthTex;
    int             depthTexW, depthTexH;

    SDL_GPUBuffer        *vertexBuf;
    SDL_GPUTransferBuffer *vertexXfer;
    SceneGPUVertex       *vertices;      /* heap-allocated, SCENE_GPU_MAX_VERTICES */
    int                   vertexCount;

    SceneGPUDrawCmd *drawCmds;           /* heap-allocated, SCENE_GPU_MAX_DRAW_CMDS */
    int              drawCmdCount;

    /* ---- Car mesh pipeline (SceneGPUMeshVertex: pos[3]+uv[2]+col[4]) ---- */
    SDL_GPUGraphicsPipeline *carPipeline;

    SceneGPUCarDrawCmd carDraws[SCENE_GPU_MAX_CAR_DRAWS];
    int                carDrawCount;

    /* ---- HUD overlay pipeline (SceneGPUHUDVertex: NDC pos[2]+uv[2]) ---- */
    SDL_GPUGraphicsPipeline *hudPipeline;
    SDL_GPUBuffer           *hudVertBuf;    /* 6-vertex static quad */
    SDL_GPUTexture          *hudOverlayTex; /* re-uploaded each frame */
    SDL_GPUTransferBuffer   *hudXfer;       /* for the HUD pixel upload */
    int                      hudTexW, hudTexH;

    /* Updated from game_render.c each frame before end_frame. */
    uint8 *hudSrcBuf;
    int    hudSrcW, hudSrcH;

    bool splitScreen; /* when true, HUD pass is scissored to left half only */

    /* ---- Per-frame SDL3 state ---- */
    SDL_GPUCommandBuffer *cmdBuf;
    SDL_GPUTexture       *swapchainTex;

    /* ---- Texture bank ---- */
    SceneGPUTextureSlot texSlots[SCENE_GPU_MAX_TEXTURE_SLOTS];
    SceneTextureHandle  texIdxToHandle[32];

    /* ---- Camera/projection (set via API, used in draw calls) ---- */
    SceneRenderCamera     camera;
    SceneRenderProjection proj;

    /* ---- Viewport ---- */
    int viewportX, viewportY, viewportW, viewportH;

    /* ---- Sky clear color ---- */
    float skyR, skyG, skyB;

    /* Flat-colour texture cache (one 4×4 solid texture per palette index, lazy) */
    SDL_GPUTexture *flatColorCache[256];
    SDL_GPUTexture *shadowTex;       /* 50%-transparent black for car shadow quads */

    /* Offscreen colour target rendered at native resolution, blitted to the
     * swapchain with letterbox/pillarbox scaling to fill the window. */
    SDL_GPUTexture *offscreenTex;
    int             offscreenW, offscreenH;

    /* MSAA: separate multisample colour + depth targets.  The resolved result
     * lands in offscreenTex (or swapchainTex when offscreen isn't available). */
    SDL_GPUTexture     *msaaTex;
    SDL_GPUTexture     *msaaDepthTex;
    int                 msaaW, msaaH;
    SDL_GPUSampleCount  msaaSampleCount; /* SDL_GPU_SAMPLECOUNT_1 = disabled */

    int   textureFilter;   /* 0=nearest, 1=bilinear, 2=anisotropic */
    bool  trilinear;       /* true = LINEAR mipmap mode; textures always have full mip chain */
    int   anisotropyLevel; /* 0=2x, 1=4x, 2=8x, 3=16x */
    float lodBias;         /* mip LOD bias applied to all texture samples */
    float renderScale;     /* internal render resolution multiplier; 1.0 = native */

    float fogDensity;      /* exponential-squared fog density; 0.0 = off */
    float gamma;           /* output gamma; 1.0 = neutral */
    float fogColor[4];     /* RGBA fog colour (A unused) */
    float fogStart;        /* view-space depth at which fog begins; 0.0 = from camera */
    float saturation;      /* colour saturation; 1.0 = neutral */
    float contrast;        /* contrast; 1.0 = neutral */
    float vigStrength;     /* vignette darkening strength; 0.0 = off */
    float brightness;      /* additive brightness offset; 0.0 = neutral */
    float fovMultiplier;   /* FOV multiplier applied on top of the game camera; 1.0 = native */
    bool  wireframe;       /* render geometry as wireframe */

    /* Vsync is deferred: SDL_SetGPUSwapchainParameters must be called before
     * SDL_AcquireGPUSwapchainTexture, so changes requested mid-frame are applied
     * at the start of the next begin_frame. */
    bool pendingVsync;
    bool pendingVsyncSet;

    /* Optional debug overlay rendered after the final blit, before submit. */
    DebugOverlay *debugOverlay;

    /* Optional CRT post-process filter applied instead of the plain blit. */
    CRTFilter *crtFilter;
};

/* ==========================================================================
 * Internal helpers
 * ========================================================================== */

static void indexed_to_rgba(const uint8 *src, const tColor *pal,
                             uint8 *dst, int count)
{
    if (!pal) {
        for (int i = 0; i < count; i++) {
            dst[i*4+0] = 255; dst[i*4+1] = 0; dst[i*4+2] = 255; dst[i*4+3] = 255;
        }
        return;
    }
    for (int i = 0; i < count; i++) {
        const tColor *c = &pal[src[i]];
        dst[i * 4 + 0] = (uint8)((c->byR * 255) / 63);
        dst[i * 4 + 1] = (uint8)((c->byG * 255) / 63);
        dst[i * 4 + 2] = (uint8)((c->byB * 255) / 63);
        dst[i * 4 + 3] = (src[i] == 0) ? 0 : 255;
    }
}

static Uint32 mip_level_count(int w, int h)
{
    Uint32 n = 1;
    Uint32 d = (Uint32)(w > h ? w : h);
    while (d > 1) { d >>= 1; n++; }
    return n;
}

static SDL_GPUTexture *upload_rgba(SDL_GPUDevice *dev,
                                   const uint8 *rgba, int w, int h)
{
    Uint32 levels = mip_level_count(w, h);
    SDL_GPUTextureCreateInfo ti = {0};
    ti.type        = SDL_GPU_TEXTURETYPE_2D;
    ti.format      = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ti.width       = (Uint32)w;
    ti.height      = (Uint32)h;
    ti.layer_count_or_depth = 1;
    ti.num_levels  = levels;
    ti.usage       = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    SDL_GPUTexture *tex = SDL_CreateGPUTexture(dev, &ti);
    if (!tex) return NULL;

    Uint32 sz = (Uint32)(w * h * 4);
    SDL_GPUTransferBufferCreateInfo tbi = {0};
    tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbi.size  = sz;
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(dev, &tbi);
    if (!tb) { SDL_ReleaseGPUTexture(dev, tex); return NULL; }
    void *m = SDL_MapGPUTransferBuffer(dev, tb, false);
    if (!m) { SDL_ReleaseGPUTransferBuffer(dev, tb); SDL_ReleaseGPUTexture(dev, tex); return NULL; }
    memcpy(m, rgba, sz);
    SDL_UnmapGPUTransferBuffer(dev, tb);

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(dev);
    if (!cmd) { SDL_ReleaseGPUTransferBuffer(dev, tb); SDL_ReleaseGPUTexture(dev, tex); return NULL; }
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureTransferInfo src = {.transfer_buffer = tb};
    SDL_GPUTextureRegion dst = {.texture = tex, .w = (Uint32)w, .h = (Uint32)h, .d = 1};
    SDL_UploadToGPUTexture(cp, &src, &dst, false);
    SDL_EndGPUCopyPass(cp);
    if (levels > 1)
        SDL_GenerateMipmapsForGPUTexture(cmd, tex);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(dev, tb);
    return tex;
}

static SDL_GPUBuffer *upload_gpu_buffer(SDL_GPUDevice *dev,
                                         SDL_GPUBufferUsageFlags usage,
                                         const void *data, Uint32 size)
{
    SDL_GPUBufferCreateInfo bi = {.usage = usage, .size = size};
    SDL_GPUBuffer *buf = SDL_CreateGPUBuffer(dev, &bi);
    if (!buf) return NULL;

    SDL_GPUTransferBufferCreateInfo tbi = {.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = size};
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(dev, &tbi);
    if (!tb) { SDL_ReleaseGPUBuffer(dev, buf); return NULL; }

    void *m = SDL_MapGPUTransferBuffer(dev, tb, false);
    if (!m) { SDL_ReleaseGPUTransferBuffer(dev, tb); SDL_ReleaseGPUBuffer(dev, buf); return NULL; }
    memcpy(m, data, size);
    SDL_UnmapGPUTransferBuffer(dev, tb);

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(dev);
    if (!cmd) { SDL_ReleaseGPUTransferBuffer(dev, tb); SDL_ReleaseGPUBuffer(dev, buf); return NULL; }
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation srcloc = {.transfer_buffer = tb};
    SDL_GPUBufferRegion dstreg = {.buffer = buf, .size = size};
    SDL_UploadToGPUBuffer(cp, &srcloc, &dstreg, false);
    SDL_EndGPUCopyPass(cp);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(dev, tb);
    return buf;
}


static void ensure_depth_texture(SceneRendererGPU *r, int w, int h)
{
    if (r->depthTex && r->depthTexW == w && r->depthTexH == h)
        return;
    if (r->depthTex)
        SDL_ReleaseGPUTexture(r->device, r->depthTex);
    SDL_GPUTextureCreateInfo ti = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
        .width = (Uint32)w, .height = (Uint32)h,
        .layer_count_or_depth = 1, .num_levels = 1,
        .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET
    };
    r->depthTex  = SDL_CreateGPUTexture(r->device, &ti);
    r->depthTexW = w;
    r->depthTexH = h;
}

static void ensure_offscreen_texture(SceneRendererGPU *r, int w, int h)
{
    if (r->offscreenTex && r->offscreenW == w && r->offscreenH == h)
        return;
    if (r->offscreenTex)
        SDL_ReleaseGPUTexture(r->device, r->offscreenTex);
    SDL_GPUTextureCreateInfo ti = {
        .type   = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GetGPUSwapchainTextureFormat(r->device, r->window),
        .width  = (Uint32)w, .height = (Uint32)h,
        .layer_count_or_depth = 1, .num_levels = 1,
        .usage  = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER
    };
    r->offscreenTex = SDL_CreateGPUTexture(r->device, &ti);
    r->offscreenW   = w;
    r->offscreenH   = h;
}

static SDL_GPUSampleCount level_to_sample_count(int level)
{
    if (level == 1) return SDL_GPU_SAMPLECOUNT_2;
    if (level == 2) return SDL_GPU_SAMPLECOUNT_4;
    if (level == 3) return SDL_GPU_SAMPLECOUNT_8;
    return SDL_GPU_SAMPLECOUNT_1;
}

static void ensure_msaa_textures(SceneRendererGPU *r, int w, int h)
{
    SDL_GPUSampleCount sc = r->msaaSampleCount;
    if (r->msaaTex && r->msaaW == w && r->msaaH == h) return;
    if (r->msaaTex)      { SDL_ReleaseGPUTexture(r->device, r->msaaTex);      r->msaaTex      = NULL; }
    if (r->msaaDepthTex) { SDL_ReleaseGPUTexture(r->device, r->msaaDepthTex); r->msaaDepthTex = NULL; }
    r->msaaW = w; r->msaaH = h;
    if (sc <= SDL_GPU_SAMPLECOUNT_1) return;
    SDL_GPUTextureCreateInfo ci = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GetGPUSwapchainTextureFormat(r->device, r->window),
        .width = (Uint32)w, .height = (Uint32)h,
        .layer_count_or_depth = 1, .num_levels = 1,
        .sample_count = sc,
        .usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET
    };
    r->msaaTex = SDL_CreateGPUTexture(r->device, &ci);
    SDL_GPUTextureCreateInfo di = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
        .width = (Uint32)w, .height = (Uint32)h,
        .layer_count_or_depth = 1, .num_levels = 1,
        .sample_count = sc,
        .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET
    };
    r->msaaDepthTex = SDL_CreateGPUTexture(r->device, &di);
}

static SDL_GPUTexture *get_flat_color_texture(SceneRendererGPU *r, int colorIdx)
{
    if (colorIdx < 0 || colorIdx >= 256) return NULL;
    if (r->flatColorCache[colorIdx]) return r->flatColorCache[colorIdx];
    uint8 rgba[4 * 4 * 4];
    const tColor *c = &palette[colorIdx];
    uint8 R = (uint8)((c->byR * 255) / 63);
    uint8 G = (uint8)((c->byG * 255) / 63);
    uint8 B = (uint8)((c->byB * 255) / 63);
    for (int i = 0; i < 16; i++) {
        rgba[i*4+0] = R; rgba[i*4+1] = G; rgba[i*4+2] = B; rgba[i*4+3] = 255;
    }
    r->flatColorCache[colorIdx] = upload_rgba(r->device, rgba, 4, 4);
    return r->flatColorCache[colorIdx];
}

/* Build column-major 4×4 VP (= MVP for world-space geometry with identity model).
 *
 * Software renderer (drawtrk3.c) equations:
 *   fV       = proj.view * (world - cam.pos)
 *   xp       = cam.fovScale * fV.x / fV.z + proj.centerX
 *   yp       = cam.fovScale * fV.y / fV.z + proj.centerY   (in 0-199 Y-up units)
 *   screen.x = (scr_size/64) * xp
 *   screen.y = (scr_size/64) * (199 - yp)                  (flips to Y-down; horizon at ss*(199-centerY))
 *
 * GPU NDC (Vulkan: y-down, z ∈ [0,1]):
 *   NDC.x =  fovX * fV.x / fV.z
 *   NDC.y = -fovY * fV.y / fV.z  (shader negates Y once)
 *   screen.y = (1 - fovY*fV.y/fV.z) * vpH/2  → horizon at vpH/2 without correction
 *
 * To match SW, add a constant clip.y shift so horizon lands at ss*(199-centerY):
 *   shift_y = (vpH/2 - ss*(199-centerY)) / (vpH/2)
 *   clip.y += shift_y * fV.z  →  each column j: mvp[j*4+1] += shift_y * mvp[j*4+3]
 */
static void build_mvp(float mvp[16],
                      const SceneRenderCamera *cam,
                      const SceneRenderProjection *proj,
                      int vpW, int vpH,
                      float fovMult)
{
    float ss   = (float)proj->screenScale / 64.0f;
    float fovX = (2.0f * cam->fovScale * fovMult * ss) / (float)vpW;
    float fovY = (2.0f * cam->fovScale * fovMult * ss) / (float)vpH;
    float zF   = SCENE_GPU_FAR  / (SCENE_GPU_FAR - SCENE_GPU_NEAR);
    float zB   = -(SCENE_GPU_FAR * SCENE_GPU_NEAR) / (SCENE_GPU_FAR - SCENE_GPU_NEAR);

    const float (*R)[3] = proj->view;   /* view[col][row] */
    float cx = cam->viewX, cy = cam->viewY, cz = cam->viewZ;
    float tx = -(R[0][0]*cx + R[1][0]*cy + R[2][0]*cz);
    float ty = -(R[0][1]*cx + R[1][1]*cy + R[2][1]*cz);
    float tz = -(R[0][2]*cx + R[1][2]*cy + R[2][2]*cz);

    /* shadercross (HLSL→SPIR-V) negates gl_Position.y once.
     * We must NOT pre-negate Y here; let the single shader-compiler
     * negation convert from D3D (y-up) to Vulkan (y-down) convention. */
    for (int j = 0; j < 3; j++) {
        mvp[j*4+0] =  fovX * R[j][0];
        mvp[j*4+1] =  fovY * R[j][1];
        mvp[j*4+2] =  zF   * R[j][2];
        mvp[j*4+3] =         R[j][2];
    }
    mvp[12] =  fovX * tx;
    mvp[13] =  fovY * ty;
    mvp[14] =  zF   * tz + zB;
    mvp[15] =  tz;

    /* Vertical center correction: SW horizon is at ss*(199-centerY) screen rows from top;
     * GPU default puts it at vpH/2.  Add shift_y*fV.z to clip.y so horizons coincide. */
    float horizon_y = ss * (199.0f - (float)proj->centerY);
    float shift_y   = ((float)vpH * 0.5f - horizon_y) / ((float)vpH * 0.5f);
    for (int j = 0; j < 4; j++)
        mvp[j*4+1] += shift_y * mvp[j*4+3];
}

void scene_render_gpu_build_vp(const SceneRendererGPU *r, float vp[16])
{
    int vpW = r->viewportW > 0 ? r->viewportW : 640;
    int vpH = r->viewportH > 0 ? r->viewportH : 400;
    build_mvp(vp, &r->camera, &r->proj, vpW, vpH, r->fovMultiplier);
}

static SDL_GPUShader *load_shader(SDL_GPUDevice *dev, SDL_GPUShaderStage stage,
                                   const unsigned char *spirv, unsigned int spirv_sz,
                                   const unsigned char *msl,   unsigned int msl_sz,
                                   int num_samplers, int num_uniforms)
{
    SDL_GPUShaderFormat fmts = SDL_GetGPUShaderFormats(dev);
    SDL_GPUShaderCreateInfo info = {
        .stage = stage,
        .num_samplers = (Uint32)num_samplers,
        .num_uniform_buffers = (Uint32)num_uniforms
    };
    if (fmts & SDL_GPU_SHADERFORMAT_SPIRV) {
        info.format = SDL_GPU_SHADERFORMAT_SPIRV;
        info.code = spirv; info.code_size = spirv_sz;
        info.entrypoint = "main";
    } else if (fmts & SDL_GPU_SHADERFORMAT_MSL) {
        info.format = SDL_GPU_SHADERFORMAT_MSL;
        info.code = msl; info.code_size = msl_sz;
        info.entrypoint = "main0";
    } else {
        return NULL;
    }
    return SDL_CreateGPUShader(dev, &info);
}

static SDL_GPUGraphicsPipeline *make_scene_pipeline(SceneRendererGPU *r,
                                                     SDL_GPUShader *vert,
                                                     SDL_GPUShader *frag,
                                                     bool blendEnable,
                                                     bool depthBias,
                                                     SDL_GPUSampleCount sc,
                                                     SDL_GPUFillMode fillMode)
{
    SDL_GPUVertexAttribute attrs[2] = {
        {.location=0, .format=SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
         .offset=(Uint32)offsetof(SceneGPUVertex, x)},
        {.location=1, .format=SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
         .offset=(Uint32)offsetof(SceneGPUVertex, u)},
    };
    SDL_GPUVertexBufferDescription binding = {
        .pitch = sizeof(SceneGPUVertex),
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX
    };
    SDL_GPUColorTargetDescription ct = {
        .format = SDL_GetGPUSwapchainTextureFormat(r->device, r->window)
    };
    if (blendEnable) {
        ct.blend_state.enable_blend          = true;
        ct.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        ct.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        ct.blend_state.color_blend_op        = SDL_GPU_BLENDOP_ADD;
        ct.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        ct.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        ct.blend_state.alpha_blend_op        = SDL_GPU_BLENDOP_ADD;
    }
    SDL_GPUGraphicsPipelineCreateInfo pi = {
        .vertex_shader  = vert,
        .fragment_shader = frag,
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .vertex_input_state = {
            .vertex_attributes     = attrs,
            .num_vertex_attributes = 2,
            .vertex_buffer_descriptions = &binding,
            .num_vertex_buffers         = 1
        },
        .target_info = {
            .color_target_descriptions = &ct,
            .num_color_targets         = 1,
            .has_depth_stencil_target  = true,
            .depth_stencil_format      = SDL_GPU_TEXTUREFORMAT_D32_FLOAT
        },
        .multisample_state = { .sample_count = sc },
        .depth_stencil_state = {
            .enable_depth_test  = true,
            .enable_depth_write = !blendEnable,
            /* LESS_OR_EQUAL when depthBias is set (blend pass and building pass)
             * so coplanar signs/decals at the same depth as the opaque wall they
             * sit on still pass the depth test. */
            .compare_op = depthBias ? SDL_GPU_COMPAREOP_LESS_OR_EQUAL
                                    : SDL_GPU_COMPAREOP_LESS
        }
    };
    pi.rasterizer_state.fill_mode = fillMode;
    if (depthBias) {
        /* Depth bias pulls these quads slightly toward the camera so signs/decals
         * that are coplanar with or fractionally behind the opaque wall surface
         * are still visible.  Values mirror the car-shadow pipeline bias. */
        pi.rasterizer_state.enable_depth_bias          = true;
        pi.rasterizer_state.depth_bias_constant_factor = -2.0f;
        pi.rasterizer_state.depth_bias_slope_factor    = -1.0f;
    }
    return SDL_CreateGPUGraphicsPipeline(r->device, &pi);
}

/* Shadow pipeline: blend + LESS_OR_EQUAL depth (no write) + large bias.
 * Large bias is required because car shadow quads can be at or slightly
 * below the road surface in NDC space when the car is on the ground. */
static SDL_GPUGraphicsPipeline *make_shadow_pipeline(SceneRendererGPU *r,
                                                      SDL_GPUShader *vert,
                                                      SDL_GPUShader *frag,
                                                      SDL_GPUSampleCount sc,
                                                      SDL_GPUFillMode fillMode)
{
    SDL_GPUVertexAttribute attrs[2] = {
        {.location=0, .format=SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
         .offset=(Uint32)offsetof(SceneGPUVertex, x)},
        {.location=1, .format=SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
         .offset=(Uint32)offsetof(SceneGPUVertex, u)},
    };
    SDL_GPUVertexBufferDescription binding = { .pitch = sizeof(SceneGPUVertex),
                                               .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX };
    SDL_GPUColorTargetDescription ct = {
        .format = SDL_GetGPUSwapchainTextureFormat(r->device, r->window),
        .blend_state = {
            .enable_blend          = true,
            .src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
            .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .color_blend_op        = SDL_GPU_BLENDOP_ADD,
            .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
            .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .alpha_blend_op        = SDL_GPU_BLENDOP_ADD
        }
    };
    SDL_GPUGraphicsPipelineCreateInfo pi = {
        .vertex_shader   = vert,
        .fragment_shader = frag,
        .primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .vertex_input_state = { .vertex_attributes = attrs, .num_vertex_attributes = 2,
                                .vertex_buffer_descriptions = &binding, .num_vertex_buffers = 1 },
        .target_info = { .color_target_descriptions = &ct, .num_color_targets = 1,
                         .has_depth_stencil_target = true,
                         .depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT },
        .multisample_state = { .sample_count = sc },
        /* Shadow is drawn AFTER the car mesh. Car-surface depth (written by the
         * car pass) blocks the shadow where the car body is; only road pixels
         * (D_road ≈ D_shadow) get darkened.  A small bias overcomes fp noise
         * on the coplanar road surface without punching through kerbs. */
        .depth_stencil_state = { .enable_depth_test  = true,
                                 .enable_depth_write = false,
                                 .compare_op         = SDL_GPU_COMPAREOP_LESS_OR_EQUAL },
        .rasterizer_state = { .fill_mode = fillMode,
                              .enable_depth_bias          = true,
                              .depth_bias_constant_factor = -50.0f,
                              .depth_bias_slope_factor    = -1.0f }
    };
    return SDL_CreateGPUGraphicsPipeline(r->device, &pi);
}

static SDL_GPUGraphicsPipeline *make_sign_pipeline(SceneRendererGPU *r,
                                                    SDL_GPUShader *vert,
                                                    SDL_GPUShader *frag,
                                                    SDL_GPUSampleCount sc,
                                                    SDL_GPUFillMode fillMode)
{
    SDL_GPUVertexAttribute attrs[2] = {
        {.location=0, .format=SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
         .offset=(Uint32)offsetof(SceneGPUVertex, x)},
        {.location=1, .format=SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
         .offset=(Uint32)offsetof(SceneGPUVertex, u)},
    };
    SDL_GPUVertexBufferDescription binding = {
        .pitch = sizeof(SceneGPUVertex),
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX
    };
    SDL_GPUColorTargetDescription ct = {
        .format = SDL_GetGPUSwapchainTextureFormat(r->device, r->window)
    };
    SDL_GPUGraphicsPipelineCreateInfo pi = {
        .vertex_shader   = vert,
        .fragment_shader = frag,
        .primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .vertex_input_state = {
            .vertex_attributes          = attrs,
            .num_vertex_attributes      = 2,
            .vertex_buffer_descriptions = &binding,
            .num_vertex_buffers         = 1
        },
        .target_info = {
            .color_target_descriptions = &ct,
            .num_color_targets         = 1,
            .has_depth_stencil_target  = true,
            .depth_stencil_format      = SDL_GPU_TEXTUREFORMAT_D32_FLOAT
        },
        .multisample_state  = { .sample_count = sc },
        /* enable_depth_test must be TRUE for depth writes to take effect in
         * Vulkan/D3D12: when depth testing is disabled the spec also disables
         * depth writes regardless of enable_depth_write.  COMPARE_ALWAYS gives
         * the same "always passes" semantic while keeping writes enabled.
         *
         * The negative bias pushes the sign's written depth toward the camera
         * so that the building pass (COMPARE_LESS_OR_EQUAL, bias -2r) cannot
         * overwrite it.  Signs may be geometrically 1-10+ world units behind
         * their wall (floating-point differences in vertex transforms), so a
         * large constant_factor is needed.  -4096 ≈ -0.000245 NDC at D32_FLOAT
         * resolution, covering sign-behind-wall offsets up to ~15 world units
         * at a view-Z of 2200.  Closer geometry (barriers, hills) is still
         * correctly occluding because their depth advantage exceeds the bias. */
        .depth_stencil_state = {
            .enable_depth_test  = true,
            .enable_depth_write = true,
            .compare_op         = SDL_GPU_COMPAREOP_ALWAYS
        },
        .rasterizer_state = {
            .fill_mode                  = fillMode,
            .enable_depth_bias          = true,
            .depth_bias_constant_factor = -4096.0f,
            .depth_bias_clamp           = 0.0f,
            .depth_bias_slope_factor    = -1.0f,
        }
    };
    return SDL_CreateGPUGraphicsPipeline(r->device, &pi);
}

static SDL_GPUGraphicsPipeline *make_car_pipeline(SceneRendererGPU *r,
                                                   SDL_GPUShader *vert,
                                                   SDL_GPUShader *frag,
                                                   SDL_GPUSampleCount sc,
                                                   SDL_GPUFillMode fillMode)
{
    SDL_GPUVertexAttribute attrs[3] = {
        {.location=0, .format=SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
         .offset=(Uint32)offsetof(SceneGPUMeshVertex, position)},
        {.location=1, .format=SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
         .offset=(Uint32)offsetof(SceneGPUMeshVertex, uv)},
        {.location=2, .format=SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
         .offset=(Uint32)offsetof(SceneGPUMeshVertex, color)}
    };
    SDL_GPUVertexBufferDescription binding = {
        .pitch = sizeof(SceneGPUMeshVertex),
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX
    };
    SDL_GPUColorTargetDescription ct = {
        .format = SDL_GetGPUSwapchainTextureFormat(r->device, r->window),
        .blend_state = {
            .enable_blend          = true,
            .src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
            .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .color_blend_op        = SDL_GPU_BLENDOP_ADD,
            .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
            .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .alpha_blend_op        = SDL_GPU_BLENDOP_ADD
        }
    };
    SDL_GPUGraphicsPipelineCreateInfo pi = {
        .vertex_shader  = vert,
        .fragment_shader = frag,
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .vertex_input_state = {
            .vertex_attributes     = attrs,
            .num_vertex_attributes = 3,
            .vertex_buffer_descriptions = &binding,
            .num_vertex_buffers         = 1
        },
        .rasterizer_state = {
            /* No backface culling: exterior faces win via COMPAREOP_LESS depth
             * (far/back faces fail once the nearer face has written depth).
             * This also lets the car's floor panels appear on the road surface
             * as a shadow, using the negative depth bias to sit above the road. */
            .fill_mode = fillMode,
            .cull_mode = SDL_GPU_CULLMODE_NONE,
            .enable_depth_bias = true,
            .depth_bias_constant_factor = -50.0f,
            .depth_bias_clamp = 0.0f,
            .depth_bias_slope_factor = -1.0f,
        },
        .target_info = {
            .color_target_descriptions = &ct,
            .num_color_targets         = 1,
            .has_depth_stencil_target  = true,
            .depth_stencil_format      = SDL_GPU_TEXTUREFORMAT_D32_FLOAT
        },
        .multisample_state = { .sample_count = sc },
        .depth_stencil_state = {
            .enable_depth_test  = true,
            .enable_depth_write = true,
            .compare_op         = SDL_GPU_COMPAREOP_LESS
        }
    };
    return SDL_CreateGPUGraphicsPipeline(r->device, &pi);
}

static SDL_GPUGraphicsPipeline *make_hud_pipeline(SceneRendererGPU *r,
                                                   SDL_GPUShader *vert,
                                                   SDL_GPUShader *frag)
{
    SDL_GPUVertexAttribute attrs[2] = {
        {.location=0, .format=SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
         .offset=(Uint32)offsetof(SceneGPUHUDVertex, x)},
        {.location=1, .format=SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
         .offset=(Uint32)offsetof(SceneGPUHUDVertex, u)}
    };
    SDL_GPUVertexBufferDescription binding = {
        .pitch = sizeof(SceneGPUHUDVertex),
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX
    };
    SDL_GPUColorTargetDescription ct = {
        .format = SDL_GetGPUSwapchainTextureFormat(r->device, r->window),
        .blend_state = {
            .enable_blend          = true,
            .src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
            .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .color_blend_op        = SDL_GPU_BLENDOP_ADD,
            .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
            .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .alpha_blend_op        = SDL_GPU_BLENDOP_ADD
        }
    };
    SDL_GPUGraphicsPipelineCreateInfo pi = {
        .vertex_shader  = vert,
        .fragment_shader = frag,
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .vertex_input_state = {
            .vertex_attributes     = attrs,
            .num_vertex_attributes = 2,
            .vertex_buffer_descriptions = &binding,
            .num_vertex_buffers         = 1
        },
        .target_info = {
            .color_target_descriptions = &ct,
            .num_color_targets         = 1,
            .has_depth_stencil_target  = false
        }
    };
    return SDL_CreateGPUGraphicsPipeline(r->device, &pi);
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

SceneRendererGPU *scene_render_gpu_create(SDL_GPUDevice *device, SDL_Window *window)
{
    SceneRendererGPU *r = calloc(1, sizeof(*r));
    if (!r) return NULL;
    r->device = device;
    r->window = window;
    r->anisotropyLevel = 3;   /* default 16x, matches old hardcoded value */
    r->renderScale     = 1.0f;
    r->gamma           = 1.0f;
    r->fogColor[0]     = 0.70f;  /* light sky-grey fog */
    r->fogColor[1]     = 0.75f;
    r->fogColor[2]     = 0.80f;
    r->fogColor[3]     = 1.0f;
    r->saturation      = 1.0f;
    r->contrast        = 1.0f;
    r->fovMultiplier   = 1.0f;
    r->skyR = 0.25f; r->skyG = 0.45f; r->skyB = 0.75f;

    for (int i = 0; i < 32; i++)
        r->texIdxToHandle[i] = SCENE_TEXTURE_HANDLE_INVALID;

    SDL_GPUSamplerCreateInfo si = {
        .min_filter     = SDL_GPU_FILTER_NEAREST,
        .mag_filter     = SDL_GPU_FILTER_NEAREST,
        .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .max_lod        = 1000.0f,
    };
    r->sampler = SDL_CreateGPUSampler(device, &si);

    SDL_GPUSamplerCreateInfo siNearest = {
        .min_filter     = SDL_GPU_FILTER_NEAREST,
        .mag_filter     = SDL_GPU_FILTER_NEAREST,
        .mipmap_mode    = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .max_lod        = 0.0f,
    };
    r->samplerNearest = SDL_CreateGPUSampler(device, &siNearest);

    {
        uint8 shadowRgba[4 * 4 * 4];
        for (int i = 0; i < 16; i++) {
            shadowRgba[i*4+0] = 0; shadowRgba[i*4+1] = 0;
            shadowRgba[i*4+2] = 0; shadowRgba[i*4+3] = 128;
        }
        r->shadowTex = upload_rgba(device, shadowRgba, 4, 4);
    }

    /* ---- Scene shaders ---- */
    SDL_GPUShader *sv = load_shader(device, SDL_GPU_SHADERSTAGE_VERTEX,
        game_scene_vertex_spirv, game_scene_vertex_spirv_size,
        game_scene_vertex_msl,   game_scene_vertex_msl_size, 0, 1);
    SDL_GPUShader *sfOp = load_shader(device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        game_scene_pixel_spirv,  game_scene_pixel_spirv_size,
        game_scene_pixel_msl,    game_scene_pixel_msl_size,  1, 1);
    SDL_GPUShader *sfBl = load_shader(device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        game_scene_pixel_blend_spirv, game_scene_pixel_blend_spirv_size,
        game_scene_pixel_blend_msl,   game_scene_pixel_blend_msl_size,  1, 1);
    if (!sv || !sfOp || !sfBl) goto fail;

    r->opaquePipeline   = make_scene_pipeline(r, sv, sfOp, false, false, SDL_GPU_SAMPLECOUNT_1, SDL_GPU_FILLMODE_FILL);
    r->blendPipeline    = make_scene_pipeline(r, sv, sfBl, true,  true,  SDL_GPU_SAMPLECOUNT_1, SDL_GPU_FILLMODE_FILL);
    r->buildingPipeline = make_scene_pipeline(r, sv, sfOp, false, true,  SDL_GPU_SAMPLECOUNT_1, SDL_GPU_FILLMODE_FILL);
    r->signPipeline     = make_sign_pipeline (r, sv, sfOp,        SDL_GPU_SAMPLECOUNT_1, SDL_GPU_FILLMODE_FILL);
    r->wallPipeline     = make_scene_pipeline(r, sv, sfBl, false, true,  SDL_GPU_SAMPLECOUNT_1, SDL_GPU_FILLMODE_FILL);
    r->shadowPipeline   = make_shadow_pipeline(r, sv, sfBl,              SDL_GPU_SAMPLECOUNT_1, SDL_GPU_FILLMODE_FILL);
    SDL_ReleaseGPUShader(device, sv);
    SDL_ReleaseGPUShader(device, sfOp);
    SDL_ReleaseGPUShader(device, sfBl);
    if (!r->opaquePipeline || !r->blendPipeline || !r->buildingPipeline || !r->signPipeline || !r->wallPipeline || !r->shadowPipeline) goto fail;

    /* ---- Car shaders (game_car_* add fog+gamma; menu_mesh_* kept for menu) ---- */
    SDL_GPUShader *cv = load_shader(device, SDL_GPU_SHADERSTAGE_VERTEX,
        game_car_vertex_spirv, game_car_vertex_spirv_size,
        game_car_vertex_msl,   game_car_vertex_msl_size, 0, 1);
    SDL_GPUShader *cf = load_shader(device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        game_car_pixel_spirv,  game_car_pixel_spirv_size,
        game_car_pixel_msl,    game_car_pixel_msl_size,  1, 1);
    if (!cv || !cf) goto fail;

    r->carPipeline = make_car_pipeline(r, cv, cf, SDL_GPU_SAMPLECOUNT_1, SDL_GPU_FILLMODE_FILL);
    SDL_ReleaseGPUShader(device, cv);
    SDL_ReleaseGPUShader(device, cf);
    if (!r->carPipeline) goto fail;

    /* ---- HUD shaders ---- */
    SDL_GPUShader *hv = load_shader(device, SDL_GPU_SHADERSTAGE_VERTEX,
        game_hud_vertex_spirv, game_hud_vertex_spirv_size,
        game_hud_vertex_msl,   game_hud_vertex_msl_size, 0, 0);
    SDL_GPUShader *hf = load_shader(device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        game_hud_pixel_spirv,  game_hud_pixel_spirv_size,
        game_hud_pixel_msl,    game_hud_pixel_msl_size,  1, 1);
    if (!hv || !hf) goto fail;

    r->hudPipeline = make_hud_pipeline(r, hv, hf);
    SDL_ReleaseGPUShader(device, hv);
    SDL_ReleaseGPUShader(device, hf);
    if (!r->hudPipeline) goto fail;

    /* ---- Static HUD full-screen quad (6 verts) ----
     * shadercross (HLSL→SPIR-V) negates SV_Position.y to convert from D3D
     * convention (y=+1 top) to Vulkan (y=-1 top).  Supply D3D-convention Y so
     * after the implicit flip the top-left lands at Vulkan NDC (-1,-1). */
    SceneGPUHUDVertex hudVerts[6] = {
        {-1,  1, 0, 0}, { 1,  1, 1, 0}, { 1, -1, 1, 1},
        {-1,  1, 0, 0}, { 1, -1, 1, 1}, {-1, -1, 0, 1}
    };
    r->hudVertBuf = upload_gpu_buffer(device, SDL_GPU_BUFFERUSAGE_VERTEX,
                                      hudVerts, sizeof(hudVerts));
    if (!r->hudVertBuf) goto fail;

    /* ---- CPU-side vertex + draw-cmd arrays ---- */
    r->vertices  = malloc(SCENE_GPU_MAX_VERTICES * sizeof(SceneGPUVertex));
    r->drawCmds  = malloc(SCENE_GPU_MAX_DRAW_CMDS * sizeof(SceneGPUDrawCmd));
    if (!r->vertices || !r->drawCmds) goto fail;

    /* ---- Scene vertex buffer ---- */
    Uint32 vbufSize = (Uint32)(SCENE_GPU_MAX_VERTICES * sizeof(SceneGPUVertex));
    SDL_GPUBufferCreateInfo bi = {
        .usage = SDL_GPU_BUFFERUSAGE_VERTEX, .size = vbufSize
    };
    r->vertexBuf = SDL_CreateGPUBuffer(device, &bi);
    SDL_GPUTransferBufferCreateInfo tbi = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = vbufSize
    };
    r->vertexXfer = SDL_CreateGPUTransferBuffer(device, &tbi);
    if (!r->vertexBuf || !r->vertexXfer) goto fail;

    return r;
fail:
    scene_render_gpu_destroy(r);
    return NULL;
}

void scene_render_gpu_destroy(SceneRendererGPU *r)
{
    if (!r) return;
    for (int i = 1; i < SCENE_GPU_MAX_TEXTURE_SLOTS; i++) {
        if (!r->texSlots[i].in_use) continue;
        for (int t = 0; t < r->texSlots[i].numTiles; t++) {
            if (r->texSlots[i].tileTextures[t])
                SDL_ReleaseGPUTexture(r->device, r->texSlots[i].tileTextures[t]);
            if (r->texSlots[i].pairTextures[t])
                SDL_ReleaseGPUTexture(r->device, r->texSlots[i].pairTextures[t]);
        }
    }
    for (int i = 0; i < 256; i++) {
        if (r->flatColorCache[i])
            SDL_ReleaseGPUTexture(r->device, r->flatColorCache[i]);
    }
    if (r->shadowTex)     SDL_ReleaseGPUTexture(r->device, r->shadowTex);
    if (r->offscreenTex)  SDL_ReleaseGPUTexture(r->device, r->offscreenTex);
    if (r->depthTex)      SDL_ReleaseGPUTexture(r->device, r->depthTex);
    if (r->msaaTex)       SDL_ReleaseGPUTexture(r->device, r->msaaTex);
    if (r->msaaDepthTex)  SDL_ReleaseGPUTexture(r->device, r->msaaDepthTex);
    if (r->hudOverlayTex) SDL_ReleaseGPUTexture(r->device, r->hudOverlayTex);
    if (r->hudXfer)       SDL_ReleaseGPUTransferBuffer(r->device, r->hudXfer);
    if (r->hudVertBuf)    SDL_ReleaseGPUBuffer(r->device, r->hudVertBuf);
    if (r->vertexBuf)     SDL_ReleaseGPUBuffer(r->device, r->vertexBuf);
    if (r->vertexXfer)    SDL_ReleaseGPUTransferBuffer(r->device, r->vertexXfer);
    free(r->vertices);
    free(r->drawCmds);
    if (r->opaquePipeline)   SDL_ReleaseGPUGraphicsPipeline(r->device, r->opaquePipeline);
    if (r->blendPipeline)    SDL_ReleaseGPUGraphicsPipeline(r->device, r->blendPipeline);
    if (r->buildingPipeline) SDL_ReleaseGPUGraphicsPipeline(r->device, r->buildingPipeline);
    if (r->signPipeline)     SDL_ReleaseGPUGraphicsPipeline(r->device, r->signPipeline);
    if (r->wallPipeline)     SDL_ReleaseGPUGraphicsPipeline(r->device, r->wallPipeline);
    if (r->shadowPipeline)   SDL_ReleaseGPUGraphicsPipeline(r->device, r->shadowPipeline);
    if (r->carPipeline)      SDL_ReleaseGPUGraphicsPipeline(r->device, r->carPipeline);
    if (r->hudPipeline)    SDL_ReleaseGPUGraphicsPipeline(r->device, r->hudPipeline);
    if (r->sampler)        SDL_ReleaseGPUSampler(r->device, r->sampler);
    if (r->samplerNearest) SDL_ReleaseGPUSampler(r->device, r->samplerNearest);
    free(r);
}

void scene_render_gpu_begin_frame(SceneRendererGPU *r)
{
    if (!r) return;
    r->cmdBuf      = NULL;
    r->swapchainTex = NULL;
    if (r->pendingVsyncSet) {
        if (!SDL_SetGPUSwapchainParameters(r->device, r->window,
                SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                r->pendingVsync ? SDL_GPU_PRESENTMODE_VSYNC : SDL_GPU_PRESENTMODE_IMMEDIATE))
            SDL_Log("scene_render_gpu: SDL_SetGPUSwapchainParameters failed: %s", SDL_GetError());
        r->pendingVsyncSet = false;
    }
    r->vertexCount  = 0;
    r->drawCmdCount = 0;
    r->carDrawCount = 0;
    r->hudSrcBuf    = NULL;
    r->cmdBuf = SDL_AcquireGPUCommandBuffer(r->device);
    if (!r->cmdBuf) return;
    if (!ROLLERTryAcquireGPUSwapchainTexture(r->cmdBuf, r->window,
            &r->swapchainTex, NULL, NULL) || !r->swapchainTex) {
        SDL_CancelGPUCommandBuffer(r->cmdBuf);
        r->cmdBuf = NULL;
    }
}

void scene_render_gpu_end_frame(SceneRendererGPU *r)
{
    if (!r || !r->cmdBuf) return;

    int nativeW = r->viewportW > 0 ? r->viewportW : 640;
    int nativeH = r->viewportH > 0 ? r->viewportH : 400;
    float rs = r->renderScale > 0.25f ? r->renderScale : 1.0f;
    int renderW = (int)(nativeW * rs + 0.5f);
    int renderH = (int)(nativeH * rs + 0.5f);
    if (renderW < 1) renderW = 1;
    if (renderH < 1) renderH = 1;
    ensure_depth_texture(r, renderW, renderH);
    ensure_offscreen_texture(r, renderW, renderH);
    bool useMSAA = r->msaaSampleCount > SDL_GPU_SAMPLECOUNT_1;
    if (useMSAA) ensure_msaa_textures(r, renderW, renderH);
    if (useMSAA && (!r->msaaTex || !r->msaaDepthTex)) useMSAA = false;

    if (r->vertexCount > 0) {
        void *mapped = SDL_MapGPUTransferBuffer(r->device, r->vertexXfer, true);
        memcpy(mapped, r->vertices, (size_t)r->vertexCount * sizeof(SceneGPUVertex));
        SDL_UnmapGPUTransferBuffer(r->device, r->vertexXfer);

        SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(r->cmdBuf);
        SDL_GPUTransferBufferLocation srcl = {.transfer_buffer = r->vertexXfer};
        SDL_GPUBufferRegion dstr = {.buffer = r->vertexBuf,
                                    .size = (Uint32)((size_t)r->vertexCount * sizeof(SceneGPUVertex))};
        SDL_UploadToGPUBuffer(cp, &srcl, &dstr, false);
        SDL_EndGPUCopyPass(cp);
    }

    bool drawHUD = false;
    if (r->hudSrcBuf && r->hudSrcW > 0 && r->hudSrcH > 0) {
        int hw = r->hudSrcW, hh = r->hudSrcH;
        Uint32 sz = (Uint32)(hw * hh * 4);

        if (!r->hudOverlayTex || r->hudTexW != hw || r->hudTexH != hh) {
            if (r->hudOverlayTex) {
                SDL_ReleaseGPUTexture(r->device, r->hudOverlayTex);
                r->hudOverlayTex = NULL;
            }
            if (r->hudXfer) {
                SDL_ReleaseGPUTransferBuffer(r->device, r->hudXfer);
                r->hudXfer = NULL;
            }
            SDL_GPUTextureCreateInfo ti = {
                .type = SDL_GPU_TEXTURETYPE_2D,
                .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                .width = (Uint32)hw, .height = (Uint32)hh,
                .layer_count_or_depth = 1, .num_levels = 1,
                .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER
            };
            r->hudOverlayTex = SDL_CreateGPUTexture(r->device, &ti);
            SDL_GPUTransferBufferCreateInfo tbi = {
                .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = sz
            };
            r->hudXfer = SDL_CreateGPUTransferBuffer(r->device, &tbi);
            r->hudTexW = hw;
            r->hudTexH = hh;
        }

        if (r->hudOverlayTex && r->hudXfer) {
            uint8 *mapped = SDL_MapGPUTransferBuffer(r->device, r->hudXfer, true);
            indexed_to_rgba(r->hudSrcBuf, palette, mapped, hw * hh);
            SDL_UnmapGPUTransferBuffer(r->device, r->hudXfer);

            SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(r->cmdBuf);
            SDL_GPUTextureTransferInfo srcl = {.transfer_buffer = r->hudXfer};
            SDL_GPUTextureRegion dstr = {
                .texture = r->hudOverlayTex,
                .w = (Uint32)hw, .h = (Uint32)hh, .d = 1
            };
            SDL_UploadToGPUTexture(cp, &srcl, &dstr, false);
            SDL_EndGPUCopyPass(cp);
            drawHUD = true;
        }
    }

    if (!r->swapchainTex) {
        SDL_SubmitGPUCommandBuffer(r->cmdBuf);
        r->cmdBuf = NULL;
        return;
    }

    /* Render to the offscreen target (falls back to swapchain if creation failed).
     * With MSAA: msaaTex is the render target, offscreenTex is the resolve target. */
    SDL_GPUTexture *resolveTarget = r->offscreenTex ? r->offscreenTex : r->swapchainTex;

    /* ====================================================================
     * Pass 1: 3D scene + car meshes → offscreen (with optional MSAA resolve)
     * ==================================================================== */
    /* Blend sky clear colour toward fog colour — sky is at infinity so fog
     * factor approaches 1 as density rises.  20000 is a representative
     * "horizon distance" that makes the sky fully fogged at max slider. */
    float skyFog = 1.0f - expf(-r->fogDensity * 100000.0f);
    if (skyFog < 0.0f) skyFog = 0.0f;
    if (skyFog > 1.0f) skyFog = 1.0f;
    SDL_FColor skyClear = {
        r->skyR + (r->fogColor[0] - r->skyR) * skyFog,
        r->skyG + (r->fogColor[1] - r->skyG) * skyFog,
        r->skyB + (r->fogColor[2] - r->skyB) * skyFog,
        1.0f
    };

    SDL_GPUColorTargetInfo colorInfo;
    if (useMSAA) {
        colorInfo = (SDL_GPUColorTargetInfo){
            .texture         = r->msaaTex,
            .load_op         = SDL_GPU_LOADOP_CLEAR,
            .store_op        = SDL_GPU_STOREOP_RESOLVE,
            .resolve_texture = resolveTarget,
            .clear_color     = skyClear
        };
    } else {
        colorInfo = (SDL_GPUColorTargetInfo){
            .texture     = resolveTarget,
            .load_op     = SDL_GPU_LOADOP_CLEAR,
            .store_op    = SDL_GPU_STOREOP_STORE,
            .clear_color = skyClear
        };
    }
    SDL_GPUDepthStencilTargetInfo depthInfo = {
        .texture     = useMSAA ? r->msaaDepthTex : r->depthTex,
        .load_op     = SDL_GPU_LOADOP_CLEAR,
        .store_op    = SDL_GPU_STOREOP_DONT_CARE,
        .clear_depth = 1.0f
    };
    SDL_GPURenderPass *rp = SDL_BeginGPURenderPass(r->cmdBuf, &colorInfo, 1, &depthInfo);

    SDL_GPUViewport vp = {
        .x = 0, .y = 0,
        .w = (float)renderW, .h = (float)renderH,
        .min_depth = 0.0f, .max_depth = 1.0f
    };
    SDL_SetGPUViewport(rp, &vp);

    SDL_GPUBufferBinding vbb = {.buffer = r->vertexBuf};
    SDL_BindGPUVertexBuffers(rp, 0, &vbb, 1);
    SDL_GPUTextureSamplerBinding tsb = {0};

    /* Push scene pixel uniforms (fog, gamma, saturation, contrast, brightness) once for all draws. */
    struct {
        float fogDensity, gamma, fogStart, saturation;
        float fogColor[4];
        float contrast, brightness;
        float _pad[2];
    } pfu = {
        r->fogDensity,
        (r->gamma > 0.0f)       ? r->gamma       : 1.0f,
        (r->fogStart > 0.0f)    ? r->fogStart     : 0.0f,
        (r->saturation > 0.0f)  ? r->saturation   : 1.0f,
        {r->fogColor[0], r->fogColor[1], r->fogColor[2], r->fogColor[3]},
        (r->contrast > 0.0f)    ? r->contrast     : 1.0f,
        r->brightness,
        {0}
    };
    SDL_PushGPUFragmentUniformData(r->cmdBuf, 0, &pfu, sizeof(pfu));

#define DRAW_CMD(kind_filter) \
    for (int i = 0; i < r->drawCmdCount; i++) { \
        SceneGPUDrawCmd *cmd = &r->drawCmds[i]; \
        if (cmd->kind != (kind_filter)) continue; \
        SDL_PushGPUVertexUniformData(r->cmdBuf, 0, cmd->mvp, sizeof(cmd->mvp)); \
        tsb.texture = cmd->texture; \
        tsb.sampler = cmd->forceNearest ? r->samplerNearest : r->sampler; \
        SDL_BindGPUFragmentSamplers(rp, 0, &tsb, 1); \
        SDL_DrawGPUPrimitives(rp, (Uint32)cmd->vertexCount, 1, (Uint32)cmd->vertexStart, 0); \
    }

    /* Sign pass first: writes D_sign with COMPARE_ALWAYS so that the opaque
     * pass (COMPARE_LESS) cannot paint coplanar wall surfaces over the sign,
     * while still allowing closer geometry (barriers, hills) to overwrite it.
     * The depth bias pushes D_sign slightly toward the camera so that the
     * building pass (COMPARE_LESS_OR_EQUAL) also fails for coplanar walls. */
    SDL_BindGPUGraphicsPipeline(rp, r->signPipeline);
    DRAW_CMD(SCENE_GPU_DRAW_SIGN)

    SDL_BindGPUGraphicsPipeline(rp, r->opaquePipeline);
    DRAW_CMD(SCENE_GPU_DRAW_OPAQUE)

    SDL_BindGPUGraphicsPipeline(rp, r->wallPipeline);
    DRAW_CMD(SCENE_GPU_DRAW_WALL)

    SDL_BindGPUGraphicsPipeline(rp, r->buildingPipeline);
    DRAW_CMD(SCENE_GPU_DRAW_BUILDING)

    SDL_BindGPUGraphicsPipeline(rp, r->blendPipeline);
    DRAW_CMD(SCENE_GPU_DRAW_BLEND)
#undef DRAW_CMD

    if (r->carDrawCount > 0 && r->carPipeline) {
        SDL_BindGPUGraphicsPipeline(rp, r->carPipeline);
        SDL_PushGPUFragmentUniformData(r->cmdBuf, 0, &pfu, sizeof(pfu));
        for (int i = 0; i < r->carDrawCount; i++) {
            SceneGPUCarDrawCmd *cmd = &r->carDraws[i];
            if (!cmd->texture) continue;
            SDL_PushGPUVertexUniformData(r->cmdBuf, 0, cmd->mvp, sizeof(cmd->mvp));
            SDL_GPUBufferBinding cvbb = {.buffer = cmd->vertBuf};
            SDL_BindGPUVertexBuffers(rp, 0, &cvbb, 1);
            SDL_GPUBufferBinding cibb = {.buffer = cmd->idxBuf};
            SDL_BindGPUIndexBuffer(rp, &cibb, SDL_GPU_INDEXELEMENTSIZE_32BIT);
            tsb.texture = cmd->texture;
            tsb.sampler = r->sampler;
            SDL_BindGPUFragmentSamplers(rp, 0, &tsb, 1);
            SDL_DrawGPUIndexedPrimitives(rp, (Uint32)cmd->idxCount, 1, (Uint32)cmd->firstIndex, 0, 0);
        }
    }

    /* Shadow pass: drawn after car mesh so the car's written depth masks the
     * shadow from appearing on the car body (fails LESS_OR_EQUAL where car is). */
    if (r->shadowPipeline && r->drawCmdCount > 0) {
        SDL_BindGPUVertexBuffers(rp, 0, &vbb, 1);  /* restore scene vertex buffer */
        SDL_BindGPUGraphicsPipeline(rp, r->shadowPipeline);
        SDL_GPUTextureSamplerBinding stsb = {.texture = NULL, .sampler = r->sampler};
        for (int i = 0; i < r->drawCmdCount; i++) {
            SceneGPUDrawCmd *cmd = &r->drawCmds[i];
            if (cmd->kind != SCENE_GPU_DRAW_SHADOW) continue;
            SDL_PushGPUVertexUniformData(r->cmdBuf, 0, cmd->mvp, sizeof(cmd->mvp));
            stsb.texture = cmd->texture;
            SDL_BindGPUFragmentSamplers(rp, 0, &stsb, 1);
            SDL_DrawGPUPrimitives(rp, (Uint32)cmd->vertexCount, 1, (Uint32)cmd->vertexStart, 0);
        }
    }

    SDL_EndGPURenderPass(rp);

    /* ====================================================================
     * Pass 2: HUD overlay → offscreen (no depth)
     * ==================================================================== */
    if (drawHUD) {
        SDL_GPUColorTargetInfo hudColor = {
            .texture  = resolveTarget,
            .load_op  = SDL_GPU_LOADOP_LOAD,
            .store_op = SDL_GPU_STOREOP_STORE
        };
        SDL_GPURenderPass *hrp = SDL_BeginGPURenderPass(r->cmdBuf, &hudColor, 1, NULL);
        SDL_SetGPUViewport(hrp, &vp);
        if (r->splitScreen) {
            SDL_Rect scissor = { .x = 0, .y = 0, .w = renderW / 2, .h = renderH };
            SDL_SetGPUScissor(hrp, &scissor);
        }
        SDL_BindGPUGraphicsPipeline(hrp, r->hudPipeline);
        struct { float vigStrength; float _pad[3]; } hpu = { r->vigStrength, {0} };
        SDL_PushGPUFragmentUniformData(r->cmdBuf, 0, &hpu, sizeof(hpu));
        SDL_GPUBufferBinding hvbb = {.buffer = r->hudVertBuf};
        SDL_BindGPUVertexBuffers(hrp, 0, &hvbb, 1);
        tsb.texture = r->hudOverlayTex;
        SDL_BindGPUFragmentSamplers(hrp, 0, &tsb, 1);
        SDL_DrawGPUPrimitives(hrp, 6, 1, 0, 0);
        SDL_EndGPURenderPass(hrp);
    }

    /* ====================================================================
     * Blit/CRT offscreen → swapchain with letterbox/pillarbox
     * ==================================================================== */
    if (r->offscreenTex) {
        int winW, winH;
        SDL_GetWindowSizeInPixels(r->window, &winW, &winH);

        float scaleX = (float)winW / (float)nativeW;
        float scaleY = (float)winH / (float)nativeH;
        float scale  = scaleX < scaleY ? scaleX : scaleY;
        int dstW = (int)((float)nativeW * scale);
        int dstH = (int)((float)nativeH * scale);
        if (dstW < 1) dstW = 1;
        if (dstH < 1) dstH = 1;
        int dstX = (winW - dstW) / 2;
        int dstY = (winH - dstH) / 2;

        if (r->crtFilter && r->swapchainTex) {
            crt_filter_apply(r->crtFilter, r->cmdBuf,
                             resolveTarget, (Uint32)renderW, (Uint32)renderH,
                             r->swapchainTex,
                             (Uint32)dstX, (Uint32)dstY,
                             (Uint32)dstW, (Uint32)dstH);
        } else {
            SDL_GPUBlitInfo blitInfo = {
                .source      = { .texture = resolveTarget,
                                 .w = (Uint32)renderW, .h = (Uint32)renderH },
                .destination = { .texture = r->swapchainTex,
                                 .x = (Uint32)dstX, .y = (Uint32)dstY,
                                 .w = (Uint32)dstW, .h = (Uint32)dstH },
                .load_op     = SDL_GPU_LOADOP_CLEAR,
                .clear_color = { 0.0f, 0.0f, 0.0f, 1.0f },
                .filter      = (rs > 1.0f) ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST
            };
            SDL_BlitGPUTexture(r->cmdBuf, &blitInfo);
        }
    }

    if (r->debugOverlay && r->swapchainTex) {
        int winW, winH;
        SDL_GetWindowSizeInPixels(r->window, &winW, &winH);
        debug_overlay_render(r->debugOverlay, r->cmdBuf, r->swapchainTex,
                             (Uint32)winW, (Uint32)winH);
    }

    SDL_SubmitGPUCommandBuffer(r->cmdBuf);
    r->cmdBuf = NULL;
}

void scene_render_gpu_cancel_frame(SceneRendererGPU *r)
{
    if (!r || !r->cmdBuf) return;
    SDL_CancelGPUCommandBuffer(r->cmdBuf);
    r->cmdBuf       = NULL;
    r->swapchainTex = NULL;
}

void scene_render_gpu_discard_queued(SceneRendererGPU *r)
{
    if (!r) return;
    r->vertexCount  = 0;
    r->drawCmdCount = 0;
    r->carDrawCount = 0;
}

/* --------------------------------------------------------------------------
 * State setters
 * -------------------------------------------------------------------------- */

void scene_render_gpu_set_debug_overlay(SceneRendererGPU *r, DebugOverlay *overlay)
{
    if (r) r->debugOverlay = overlay;
}

void scene_render_gpu_set_crt_filter(SceneRendererGPU *r, CRTFilter *filter)
{
    if (r) r->crtFilter = filter;
}

void scene_render_gpu_set_viewport(SceneRendererGPU *r, int x, int y, int w, int h)
{
    if (!r) return;
    r->viewportX = x; r->viewportY = y;
    r->viewportW = w; r->viewportH = h;
}

void scene_render_gpu_set_camera(SceneRendererGPU *r, const SceneRenderCamera *cam)
{
    if (r && cam) r->camera = *cam;
}

int scene_render_gpu_get_render_chunk(const SceneRendererGPU *r)
{
    return r ? r->camera.renderChunkIdx : -1;
}

void scene_render_gpu_set_projection(SceneRendererGPU *r, const SceneRenderProjection *proj)
{
    if (r && proj) r->proj = *proj;
}

void scene_render_gpu_set_sky_color(SceneRendererGPU *r, float red, float green, float blue)
{
    if (r) { r->skyR = red; r->skyG = green; r->skyB = blue; }
}

static void rebuild_sampler(SceneRendererGPU *r)
{
    static const float k_aniso[] = {2.0f, 4.0f, 8.0f, 16.0f};
    if (r->sampler) SDL_ReleaseGPUSampler(r->device, r->sampler);
    int filter = r->textureFilter;
    int al = (r->anisotropyLevel >= 0 && r->anisotropyLevel <= 3) ? r->anisotropyLevel : 3;
    SDL_GPUSamplerCreateInfo si = {
        .min_filter        = (filter > 0) ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST,
        .mag_filter        = (filter > 0) ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST,
        .address_mode_u    = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_v    = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .enable_anisotropy = (filter == 2),
        .max_anisotropy    = (filter == 2) ? k_aniso[al] : 1.0f,
        .mipmap_mode       = r->trilinear ? SDL_GPU_SAMPLERMIPMAPMODE_LINEAR
                                          : SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        .mip_lod_bias      = r->lodBias,
        .max_lod           = 1000.0f,
    };
    r->sampler = SDL_CreateGPUSampler(r->device, &si);
}

void scene_render_gpu_set_texture_filter(SceneRendererGPU *r, int filter)
{
    if (!r) return;
    r->textureFilter = filter;
    rebuild_sampler(r);
}

void scene_render_gpu_set_trilinear(SceneRendererGPU *r, bool enabled)
{
    if (!r) return;
    r->trilinear = enabled;
    rebuild_sampler(r);
}

void scene_render_gpu_set_vsync(SceneRendererGPU *r, bool enabled)
{
    if (!r) return;
    r->pendingVsync    = enabled;
    r->pendingVsyncSet = true;
}

void scene_render_gpu_set_anisotropy_level(SceneRendererGPU *r, int level)
{
    if (!r) return;
    r->anisotropyLevel = level;
    rebuild_sampler(r);
}

void scene_render_gpu_set_lod_bias(SceneRendererGPU *r, float bias)
{
    if (!r) return;
    r->lodBias = bias;
    rebuild_sampler(r);
}

void scene_render_gpu_set_render_scale(SceneRendererGPU *r, float scale)
{
    if (!r) return;
    r->renderScale = (scale > 0.25f) ? scale : 1.0f;
}

void scene_render_gpu_set_fog_density(SceneRendererGPU *r, float density)
{
    if (!r) return;
    r->fogDensity = (density > 0.0f) ? density : 0.0f;
}

void scene_render_gpu_set_fog_color(SceneRendererGPU *r, float fr, float fg, float fb)
{
    if (!r) return;
    r->fogColor[0] = fr;
    r->fogColor[1] = fg;
    r->fogColor[2] = fb;
}

void scene_render_gpu_set_gamma(SceneRendererGPU *r, float gamma)
{
    if (!r) return;
    r->gamma = (gamma > 0.0f) ? gamma : 1.0f;
}

void scene_render_gpu_set_fog_start(SceneRendererGPU *r, float start)
{
    if (!r) return;
    r->fogStart = (start > 0.0f) ? start : 0.0f;
}

void scene_render_gpu_set_saturation(SceneRendererGPU *r, float saturation)
{
    if (!r) return;
    r->saturation = (saturation >= 0.0f) ? saturation : 1.0f;
}

void scene_render_gpu_set_contrast(SceneRendererGPU *r, float contrast)
{
    if (!r) return;
    r->contrast = (contrast >= 0.0f) ? contrast : 1.0f;
}

void scene_render_gpu_set_vignette(SceneRendererGPU *r, float strength)
{
    if (!r) return;
    r->vigStrength = (strength >= 0.0f) ? strength : 0.0f;
}

void scene_render_gpu_set_brightness(SceneRendererGPU *r, float brightness)
{
    if (!r) return;
    r->brightness = brightness;
}

void scene_render_gpu_set_fov_multiplier(SceneRendererGPU *r, float mult)
{
    if (!r) return;
    r->fovMultiplier = (mult > 0.1f) ? mult : 1.0f;
}

void scene_render_gpu_set_wireframe(SceneRendererGPU *r, bool enabled)
{
    if (!r || r->wireframe == enabled) return;
    r->wireframe = enabled;

    SDL_WaitForGPUIdle(r->device);

    if (r->opaquePipeline)   { SDL_ReleaseGPUGraphicsPipeline(r->device, r->opaquePipeline);   r->opaquePipeline   = NULL; }
    if (r->blendPipeline)    { SDL_ReleaseGPUGraphicsPipeline(r->device, r->blendPipeline);    r->blendPipeline    = NULL; }
    if (r->buildingPipeline) { SDL_ReleaseGPUGraphicsPipeline(r->device, r->buildingPipeline); r->buildingPipeline = NULL; }
    if (r->signPipeline)     { SDL_ReleaseGPUGraphicsPipeline(r->device, r->signPipeline);     r->signPipeline     = NULL; }
    if (r->wallPipeline)     { SDL_ReleaseGPUGraphicsPipeline(r->device, r->wallPipeline);     r->wallPipeline     = NULL; }
    if (r->shadowPipeline)   { SDL_ReleaseGPUGraphicsPipeline(r->device, r->shadowPipeline);   r->shadowPipeline   = NULL; }
    if (r->carPipeline)      { SDL_ReleaseGPUGraphicsPipeline(r->device, r->carPipeline);      r->carPipeline      = NULL; }

    SDL_GPUFillMode fm = enabled ? SDL_GPU_FILLMODE_LINE : SDL_GPU_FILLMODE_FILL;
    SDL_GPUSampleCount sc = r->msaaSampleCount;

    SDL_GPUShader *sv = load_shader(r->device, SDL_GPU_SHADERSTAGE_VERTEX,
        game_scene_vertex_spirv, game_scene_vertex_spirv_size,
        game_scene_vertex_msl,   game_scene_vertex_msl_size, 0, 1);
    SDL_GPUShader *sfOp = load_shader(r->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        game_scene_pixel_spirv,  game_scene_pixel_spirv_size,
        game_scene_pixel_msl,    game_scene_pixel_msl_size,  1, 1);
    SDL_GPUShader *sfBl = load_shader(r->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        game_scene_pixel_blend_spirv, game_scene_pixel_blend_spirv_size,
        game_scene_pixel_blend_msl,   game_scene_pixel_blend_msl_size,  1, 1);
    if (sv && sfOp && sfBl) {
        r->opaquePipeline   = make_scene_pipeline(r, sv, sfOp, false, false, sc, fm);
        r->blendPipeline    = make_scene_pipeline(r, sv, sfBl, true,  true,  sc, fm);
        r->buildingPipeline = make_scene_pipeline(r, sv, sfOp, false, true,  sc, fm);
        r->signPipeline     = make_sign_pipeline (r, sv, sfOp,        sc,    fm);
        r->wallPipeline     = make_scene_pipeline(r, sv, sfBl, false, true,  sc, fm);
        r->shadowPipeline   = make_shadow_pipeline(r, sv, sfBl,       sc,    fm);
    }
    if (sv)   SDL_ReleaseGPUShader(r->device, sv);
    if (sfOp) SDL_ReleaseGPUShader(r->device, sfOp);
    if (sfBl) SDL_ReleaseGPUShader(r->device, sfBl);

    SDL_GPUShader *cv = load_shader(r->device, SDL_GPU_SHADERSTAGE_VERTEX,
        game_car_vertex_spirv, game_car_vertex_spirv_size,
        game_car_vertex_msl,   game_car_vertex_msl_size, 0, 1);
    SDL_GPUShader *cf = load_shader(r->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        game_car_pixel_spirv,  game_car_pixel_spirv_size,
        game_car_pixel_msl,    game_car_pixel_msl_size,  1, 1);
    if (cv && cf)
        r->carPipeline = make_car_pipeline(r, cv, cf, sc, fm);
    if (cv) SDL_ReleaseGPUShader(r->device, cv);
    if (cf) SDL_ReleaseGPUShader(r->device, cf);
}

void scene_render_gpu_set_msaa(SceneRendererGPU *r, int level)
{
    if (!r) return;
    SDL_GPUSampleCount sc = level_to_sample_count(level);
    if (sc == r->msaaSampleCount) return;

    SDL_WaitForGPUIdle(r->device);

    /* Release pipelines — they must be rebuilt with the new sample count. */
    if (r->opaquePipeline)   { SDL_ReleaseGPUGraphicsPipeline(r->device, r->opaquePipeline);   r->opaquePipeline   = NULL; }
    if (r->blendPipeline)    { SDL_ReleaseGPUGraphicsPipeline(r->device, r->blendPipeline);    r->blendPipeline    = NULL; }
    if (r->buildingPipeline) { SDL_ReleaseGPUGraphicsPipeline(r->device, r->buildingPipeline); r->buildingPipeline = NULL; }
    if (r->signPipeline)     { SDL_ReleaseGPUGraphicsPipeline(r->device, r->signPipeline);     r->signPipeline     = NULL; }
    if (r->wallPipeline)     { SDL_ReleaseGPUGraphicsPipeline(r->device, r->wallPipeline);     r->wallPipeline     = NULL; }
    if (r->shadowPipeline)   { SDL_ReleaseGPUGraphicsPipeline(r->device, r->shadowPipeline);   r->shadowPipeline   = NULL; }
    if (r->carPipeline)      { SDL_ReleaseGPUGraphicsPipeline(r->device, r->carPipeline);      r->carPipeline      = NULL; }

    /* Release MSAA textures — recreated in the next end_frame at the right size. */
    if (r->msaaTex)      { SDL_ReleaseGPUTexture(r->device, r->msaaTex);      r->msaaTex      = NULL; }
    if (r->msaaDepthTex) { SDL_ReleaseGPUTexture(r->device, r->msaaDepthTex); r->msaaDepthTex = NULL; }
    r->msaaW = 0; r->msaaH = 0;
    r->msaaSampleCount = sc;

    SDL_GPUShader *sv = load_shader(r->device, SDL_GPU_SHADERSTAGE_VERTEX,
        game_scene_vertex_spirv, game_scene_vertex_spirv_size,
        game_scene_vertex_msl,   game_scene_vertex_msl_size, 0, 1);
    SDL_GPUShader *sfOp = load_shader(r->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        game_scene_pixel_spirv,  game_scene_pixel_spirv_size,
        game_scene_pixel_msl,    game_scene_pixel_msl_size,  1, 1);
    SDL_GPUShader *sfBl = load_shader(r->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        game_scene_pixel_blend_spirv, game_scene_pixel_blend_spirv_size,
        game_scene_pixel_blend_msl,   game_scene_pixel_blend_msl_size,  1, 1);
    SDL_GPUFillMode fm = r->wireframe ? SDL_GPU_FILLMODE_LINE : SDL_GPU_FILLMODE_FILL;
    if (sv && sfOp && sfBl) {
        r->opaquePipeline   = make_scene_pipeline(r, sv, sfOp, false, false, sc, fm);
        r->blendPipeline    = make_scene_pipeline(r, sv, sfBl, true,  true,  sc, fm);
        r->buildingPipeline = make_scene_pipeline(r, sv, sfOp, false, true,  sc, fm);
        r->signPipeline     = make_sign_pipeline (r, sv, sfOp,        sc,    fm);
        r->wallPipeline     = make_scene_pipeline(r, sv, sfBl, false, true,  sc, fm);
        r->shadowPipeline   = make_shadow_pipeline(r, sv, sfBl,       sc,    fm);
    }
    if (sv)   SDL_ReleaseGPUShader(r->device, sv);
    if (sfOp) SDL_ReleaseGPUShader(r->device, sfOp);
    if (sfBl) SDL_ReleaseGPUShader(r->device, sfBl);

    SDL_GPUShader *cv = load_shader(r->device, SDL_GPU_SHADERSTAGE_VERTEX,
        game_car_vertex_spirv, game_car_vertex_spirv_size,
        game_car_vertex_msl,   game_car_vertex_msl_size, 0, 1);
    SDL_GPUShader *cf = load_shader(r->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        game_car_pixel_spirv,  game_car_pixel_spirv_size,
        game_car_pixel_msl,    game_car_pixel_msl_size,  1, 1);
    if (cv && cf)
        r->carPipeline = make_car_pipeline(r, cv, cf, sc, fm);
    if (cv) SDL_ReleaseGPUShader(r->device, cv);
    if (cf) SDL_ReleaseGPUShader(r->device, cf);
}

void scene_render_gpu_set_hud_buffer(SceneRendererGPU *r, uint8 *buf, int w, int h)
{
    if (!r) return;
    r->hudSrcBuf = buf;
    r->hudSrcW   = w;
    r->hudSrcH   = h;
}

void scene_render_gpu_set_split_screen(SceneRendererGPU *r, bool split)
{
    if (r) r->splitScreen = split;
}

/* --------------------------------------------------------------------------
 * Texture management
 * -------------------------------------------------------------------------- */

SceneTextureHandle scene_render_gpu_load_texture(SceneRendererGPU *r,
                                                  uint8 *pixelData,
                                                  int width, int height,
                                                  int tex_idx, int texHalfRes)
{
    if (!r || !pixelData || width <= 0 || height <= 0)
        return SCENE_TEXTURE_HANDLE_INVALID;

    if (tex_idx >= 0 && tex_idx < 32) {
        SceneTextureHandle old = r->texIdxToHandle[tex_idx];
        if (old != SCENE_TEXTURE_HANDLE_INVALID)
            scene_render_gpu_free_texture(r, old);
    }

    int slotIdx = -1;
    for (int i = 1; i < SCENE_GPU_MAX_TEXTURE_SLOTS; i++) {
        if (!r->texSlots[i].in_use) { slotIdx = i; break; }
    }
    if (slotIdx < 0) return SCENE_TEXTURE_HANDLE_INVALID;

    int tileSize    = texHalfRes ? 32 : 64;
    int tilesPerRow = width  / tileSize;
    int numRows     = height / tileSize;
    int numTiles    = tilesPerRow * numRows;
    if (numTiles <= 0 || numTiles > SCENE_GPU_MAX_TILES_PER_SLOT)
        return SCENE_TEXTURE_HANDLE_INVALID;

    /* Convert full atlas to RGBA once, then extract each tile. */
    uint8 *atlasRgba = malloc((size_t)(width * height * 4));
    if (!atlasRgba) return SCENE_TEXTURE_HANDLE_INVALID;
    indexed_to_rgba(pixelData, palette, atlasRgba, width * height);

    uint8 *tileRgba = malloc((size_t)(tileSize * tileSize * 4));
    if (!tileRgba) { free(atlasRgba); return SCENE_TEXTURE_HANDLE_INVALID; }

    SceneGPUTextureSlot *s = &r->texSlots[slotIdx];
    memset(s, 0, sizeof(*s));
    s->numTiles    = numTiles;
    s->tileSize    = tileSize;
    s->tilesPerRow = tilesPerRow;
    s->tex_idx     = tex_idx;
    s->texHalfRes  = texHalfRes;
    s->in_use      = 1;

    /* --- Per-tile textures --- */
    for (int t = 0; t < numTiles; t++) {
        int col = t % tilesPerRow;
        int row = t / tilesPerRow;
        for (int y = 0; y < tileSize; y++) {
            int srcRow = row * tileSize + y;
            for (int x = 0; x < tileSize; x++) {
                int srcOff = (srcRow * width + col * tileSize + x) * 4;
                int dstOff = (y * tileSize + x) * 4;
                tileRgba[dstOff+0] = atlasRgba[srcOff+0];
                tileRgba[dstOff+1] = atlasRgba[srcOff+1];
                tileRgba[dstOff+2] = atlasRgba[srcOff+2];
                tileRgba[dstOff+3] = atlasRgba[srcOff+3];
            }
        }
        s->tileTextures[t] = upload_rgba(r->device, tileRgba, tileSize, tileSize);
        if (!s->tileTextures[t]) {
            for (int k = 0; k < t; k++) {
                SDL_ReleaseGPUTexture(r->device, s->tileTextures[k]);
                s->tileTextures[k] = NULL;
            }
            s->in_use = 0;
            free(tileRgba);
            free(atlasRgba);
            return SCENE_TEXTURE_HANDLE_INVALID;
        }
    }

    free(tileRgba);

    /* --- Pair textures: tile N (left) + tile N+1 (right), used for walls ---
     * Only created when N and N+1 are adjacent in the same atlas row.
     * Last-column tiles (N%tilesPerRow == tilesPerRow-1) are skipped because
     * the software renderer wraps into the next atlas row in that case, which
     * would be wrong here; walls on last-column tiles fall back to single-tile. */
    int pairW = 2 * tileSize;
    uint8 *pairRgba = malloc((size_t)(pairW * tileSize * 4));
    if (pairRgba) {
        for (int n = 0; n + 1 < numTiles; n++) {
            int col_n = n % tilesPerRow;
            if (col_n == tilesPerRow - 1) continue; /* skip last-column tiles */

            int row_n  = n / tilesPerRow;
            int col_n1 = col_n + 1;          /* tile n+1 is always in the same row */

            for (int y = 0; y < tileSize; y++) {
                int srcRow = row_n * tileSize + y;
                for (int x = 0; x < tileSize; x++) {
                    /* left half: tile n */
                    int srcOff = (srcRow * width + col_n  * tileSize + x) * 4;
                    int dstOff = (y * pairW + x) * 4;
                    pairRgba[dstOff+0] = atlasRgba[srcOff+0];
                    pairRgba[dstOff+1] = atlasRgba[srcOff+1];
                    pairRgba[dstOff+2] = atlasRgba[srcOff+2];
                    pairRgba[dstOff+3] = atlasRgba[srcOff+3];
                    /* right half: tile n+1 */
                    srcOff = (srcRow * width + col_n1 * tileSize + x) * 4;
                    dstOff = (y * pairW + tileSize + x) * 4;
                    pairRgba[dstOff+0] = atlasRgba[srcOff+0];
                    pairRgba[dstOff+1] = atlasRgba[srcOff+1];
                    pairRgba[dstOff+2] = atlasRgba[srcOff+2];
                    pairRgba[dstOff+3] = atlasRgba[srcOff+3];
                }
            }
            s->pairTextures[n] = upload_rgba(r->device, pairRgba, pairW, tileSize);
        }
        free(pairRgba);
    }

    free(atlasRgba);

    if (tex_idx >= 0 && tex_idx < 32)
        r->texIdxToHandle[tex_idx] = (SceneTextureHandle)slotIdx;

    return (SceneTextureHandle)slotIdx;
}

void scene_render_gpu_free_texture(SceneRendererGPU *r, SceneTextureHandle handle)
{
    if (!r || handle <= 0 || handle >= SCENE_GPU_MAX_TEXTURE_SLOTS) return;
    SceneGPUTextureSlot *s = &r->texSlots[handle];
    if (!s->in_use) return;
    for (int t = 0; t < s->numTiles; t++) {
        if (s->tileTextures[t]) {
            SDL_ReleaseGPUTexture(r->device, s->tileTextures[t]);
            s->tileTextures[t] = NULL;
        }
        if (s->pairTextures[t]) {
            SDL_ReleaseGPUTexture(r->device, s->pairTextures[t]);
            s->pairTextures[t] = NULL;
        }
    }
    if (s->tex_idx >= 0 && s->tex_idx < 32 && r->texIdxToHandle[s->tex_idx] == handle)
        r->texIdxToHandle[s->tex_idx] = SCENE_TEXTURE_HANDLE_INVALID;
    memset(s, 0, sizeof(*s));
}

SceneTextureHandle scene_render_gpu_get_texture_handle(SceneRendererGPU *r, int tex_idx)
{
    if (!r || tex_idx < 0 || tex_idx >= 32) return SCENE_TEXTURE_HANDLE_INVALID;
    return r->texIdxToHandle[tex_idx];
}

/* --------------------------------------------------------------------------
 * World quad drawing
 *
 * Callers always set verts[i].u = verts[i].v = 0 (confirmed in drawtrk3.c).
 * UVs come from startsx[4] / startsy[4] globals (16.16 fixed-point).
 * -------------------------------------------------------------------------- */
void scene_render_gpu_quad_world_legacy(SceneRendererGPU *r,
                                         const SceneRenderVertex verts[4],
                                         SceneTextureHandle texture,
                                         int surfaceFlags,
                                         SceneRenderLegacyQuadOptions options)
{
    if (!r || !r->cmdBuf) return;
    if (surfaceFlags & SURFACE_FLAG_SKIP_RENDER) return;
    if (r->vertexCount + 6 > SCENE_GPU_MAX_VERTICES) return;
    if (r->drawCmdCount >= SCENE_GPU_MAX_DRAW_CMDS)  return;

    /* building.c always passes SCENE_RENDER_SUBDIVIDE_TYPE_BUILDING=666.
     * Use that to identify building/sign quads reliably — texture-based
     * detection misses signs whose advert_list entry lacks SURFACE_FLAG_APPLY_TEXTURE
     * (no slot loaded) or has an out-of-range tile index. */
    bool isBuilding    = (options.subdivideType == SCENE_RENDER_SUBDIVIDE_TYPE_BUILDING);
    bool isSign        = (options.subdivideType == SCENE_RENDER_SUBDIVIDE_TYPE_SIGN);
    bool isCloud       = (options.subdivideType == SCENE_RENDER_SUBDIVIDE_TYPE_CLOUD);

    SceneGPUTextureSlot *slot = NULL;
    SDL_GPUTexture *gpuTex    = NULL;
    if (texture > 0 && texture < SCENE_GPU_MAX_TEXTURE_SLOTS &&
        r->texSlots[texture].in_use) {
        slot = &r->texSlots[texture];
    }

    bool isFlatColor = false;
    int  surfIdx     = surfaceFlags & SURFACE_MASK_TEXTURE_INDEX;
    /* Wall pair textures only apply to non-building track surfaces. */
    bool isWall = !isBuilding && (surfaceFlags & SURFACE_FLAG_TEXTURE_PAIR) && wide_on;

    if (slot) {
        /* Wall surfaces use the pair texture (tile N + tile N+1 side by side).
         * Fall back to the single tile if no pair was created (last-column tile). */
        if (isWall && surfIdx >= 0 && surfIdx < slot->numTiles
                   && slot->pairTextures[surfIdx])
            gpuTex = slot->pairTextures[surfIdx];
        else if (surfIdx >= 0 && surfIdx < slot->numTiles)
            gpuTex = slot->tileTextures[surfIdx];
    }

    if (!gpuTex) {
        if (surfaceFlags & SURFACE_FLAG_APPLY_TEXTURE) {
            /* PARTIAL_TRANS and CONCAVE surfaces are rendered as flat-color in SW
             * (poly()/twpoly() with palette[surfIdx]).  Fall through to the
             * flat-color path below to replicate that appearance. */
            if (surfaceFlags & SURFACE_FLAG_PARTIAL_TRANS) {
                /* fall through — use flat-color path below */
                { static bool s_ptFallLog = false;
                  if (!s_ptFallLog) { s_ptFallLog = true;
                    SDL_Log("PTRANS-FALL: sf=0x%X idx=%d slot=%s numTiles=%d",
                            surfaceFlags, surfIdx,
                            slot ? "yes" : "no",
                            slot ? slot->numTiles : -1); } }
            } else {
                if (isBuilding || isSign) {
                    static bool s_bldSkipLogged = false;
                    if (!s_bldSkipLogged) {
                        s_bldSkipLogged = true;
                        SDL_Log("GPU SKIP building tex: sf=0x%X surfIdx=%d numTiles=%d slot=%s",
                                surfaceFlags, surfIdx,
                                slot ? slot->numTiles : -1,
                                slot ? "yes" : "no");
                    }
                }
                return;  /* textured surface but texture not loaded — skip */
            }
        }
        if (surfaceFlags & SURFACE_FLAG_TRANSPARENT) {
            /* Car shadow polygon: route through blend pass (LESS_OR_EQUAL depth,
             * no depth write) as a 50%-transparent black quad. */
            gpuTex = r->shadowTex;
            if (!gpuTex) return;
        } else {
            int colorIdx = surfaceFlags & SURFACE_MASK_TEXTURE_INDEX;
            gpuTex = get_flat_color_texture(r, colorIdx);
            if (!gpuTex) return;
        }
        isFlatColor = true;
    }

    /* Glass walls carry SURFACE_FLAG_TRANSPARENT; include them in the blend pass
     * so their alpha=0 (palette-index-0) pixels are correctly discarded.
     * Building/sign quads go through the building pipeline (opaque but with
     * LESS_OR_EQUAL depth compare + bias) so signs sitting on wall surfaces
     * aren't hidden by the wall's already-written depth value.
     * PARTIAL_TRANS polygons (e.g. looping ceiling) must NOT use sfBl: their
     * textures can be entirely palette-index-0 (solid dark colour), and sfBl
     * would discard every pixel → invisible. Route them through the building
     * pipeline (sfOp, no alpha discard, LESS_OR_EQUAL) so the colour renders. */
    SceneGPUDrawKind kind;
    if (isSign)
        kind = SCENE_GPU_DRAW_SIGN;
    else if (gpuTex == r->shadowTex)
        kind = SCENE_GPU_DRAW_SHADOW;
    else if (surfaceFlags & SURFACE_FLAG_TRANSPARENT)
        kind = SCENE_GPU_DRAW_BLEND;
    else if (surfaceFlags & SURFACE_FLAG_PARTIAL_TRANS)
        kind = SCENE_GPU_DRAW_BUILDING;
    else if (surfaceFlags & SURFACE_FLAG_TEXTURE_PAIR)
        kind = SCENE_GPU_DRAW_WALL;
    else if (isBuilding)
        kind = SCENE_GPU_DRAW_BUILDING;
    else
        kind = SCENE_GPU_DRAW_OPAQUE;

    float cu[4], cv[4];
    if (isFlatColor) {
        for (int k = 0; k < 4; k++) { cu[k] = 0.5f; cv[k] = 0.5f; }
    } else {
        int texSize = slot ? slot->tileSize : 32;

        /* UV range matching set_starts() values (Q16.16 corner coordinates).
         * Walls use a pair texture (2*tileSize wide), so the effective width is
         * 2*texSize and uMaxN stays near 1.  Non-walls use a single tile texture.
         * CLAMP_TO_EDGE sampler prevents anisotropic bleed at tile edges.
         * Formula: (effectiveSize - 0.0625) / effectiveSize */
        bool usePair = isWall && slot && slot->pairTextures[surfIdx];
        int  effectiveW = usePair ? 2 * texSize : texSize;
        float uMaxN  = ((float)effectiveW - 0.0625f) / (float)effectiveW;
        float vMaxN  = ((float)texSize - 0.0625f) / (float)texSize;

        bool flipH = (surfaceFlags & SURFACE_FLAG_FLIP_HORIZ) != 0;
        bool flipV = (surfaceFlags & SURFACE_FLAG_FLIP_VERT)  != 0;

        if (usePair) {
            /* The software renderer's subdivide always maps LOW U to the
             * LEFT part of the screen regardless of vertex winding.  For pair
             * textures the direction matters (tile N vs N+1), so we must
             * replicate that by comparing projected screen X of v0 and v1.
             * Use a full perspective divide (vX/vZ) because both wall vertices
             * sit at nearly the same lateral offset — raw vX values are too
             * close to compare reliably.  The perspective divide amplifies the
             * near/far depth difference into a clear screen-left vs screen-right
             * signal.  FLIP_HORIZ is not used on wall surfaces and is ignored. */
            const float (*M)[3] = r->proj.view; /* M[col][row], view[col][row] */
            /* Software formula: dx = v - viewXYZ, then vX = dx·M_row0, vZ = dx·M_row2.
             * This gives vZ = clip_w (always > 0 for visible geometry). */
            float dx0 = verts[0].x - r->camera.viewX;
            float dy0 = verts[0].y - r->camera.viewY;
            float dz0 = verts[0].z - r->camera.viewZ;
            float dx1 = verts[1].x - r->camera.viewX;
            float dy1 = verts[1].y - r->camera.viewY;
            float dz1 = verts[1].z - r->camera.viewZ;
            float vX0 = dx0*M[0][0] + dy0*M[1][0] + dz0*M[2][0];
            float vX1 = dx1*M[0][0] + dy1*M[1][0] + dz1*M[2][0];
            float vZ0 = dx0*M[0][2] + dy0*M[1][2] + dz0*M[2][2];
            float vZ1 = dx1*M[0][2] + dy1*M[1][2] + dz1*M[2][2];
            /* pX = vX/vZ is the projected screen X (proportional to clip_x/clip_w).
             * vZ equals clip_w, so it is positive for all visible vertices.
             * Only guard against exact zero to avoid division by zero. */
            bool v0IsLeft;
            if (fabsf(vZ0) < 1e-6f || fabsf(vZ1) < 1e-6f)
                v0IsLeft = vX0 < vX1;
            else
                v0IsLeft = (vX0 / vZ0) < (vX1 / vZ1);
            /* v0IsLeft: v0 projects further left → assign U=0 (tile N), v1 gets uMaxN */
            cu[0] = v0IsLeft ? 0.0f : uMaxN;
            cu[1] = v0IsLeft ? uMaxN : 0.0f;
            cu[2] = v0IsLeft ? uMaxN : 0.0f;
            cu[3] = v0IsLeft ? 0.0f : uMaxN;
            /* V is always bottom-up; flipV still applies */
            for (int k = 0; k < 4; k++) {
                int sk = k;
                if (flipV) { static const int fvm[4] = {2,3,0,1}; sk = fvm[sk]; }
                cv[k] = (sk == 2 || sk == 3) ? vMaxN : 0.0f;
            }
        } else if (isCloud) {
            /* Cloud quads: the SW renderer's polyt() walks edges based on
             * winding order, which makes u=0 always land on the screen-left
             * side regardless of vertex order.  Replicate that by projecting
             * pairs (v0,v3) and (v1,v2) to screen X and assigning u=0 to
             * whichever pair is screen-left.  FLIP_HORIZ then inverts this.
             * The V assignment stays fixed by vertex index (same as set_starts). */
            const float (*M)[3] = r->proj.view;
            float vX[4], vZ[4];
            for (int vi = 0; vi < 4; vi++) {
                float ddx = verts[vi].x - r->camera.viewX;
                float ddy = verts[vi].y - r->camera.viewY;
                float ddz = verts[vi].z - r->camera.viewZ;
                vX[vi] = ddx*M[0][0] + ddy*M[1][0] + ddz*M[2][0];
                vZ[vi] = ddx*M[0][2] + ddy*M[1][2] + ddz*M[2][2];
            }
            float px03 = (fabsf(vZ[0]) > 1e-6f ? vX[0]/vZ[0] : vX[0])
                       + (fabsf(vZ[3]) > 1e-6f ? vX[3]/vZ[3] : vX[3]);
            float px12 = (fabsf(vZ[1]) > 1e-6f ? vX[1]/vZ[1] : vX[1])
                       + (fabsf(vZ[2]) > 1e-6f ? vX[2]/vZ[2] : vX[2]);
            bool pair03IsLeft = px03 < px12;
            float u03 = pair03IsLeft ? 0.0f : uMaxN;
            float u12 = pair03IsLeft ? uMaxN : 0.0f;
            if (flipH) { float tmp = u03; u03 = u12; u12 = tmp; }
            cu[0] = u03; cu[1] = u12; cu[2] = u12; cu[3] = u03;
            for (int k = 0; k < 4; k++) {
                int sk = k;
                if (flipV) { static const int fvm[4] = {2,3,0,1}; sk = fvm[sk]; }
                cv[k] = (sk == 2 || sk == 3) ? vMaxN : 0.0f;
            }
        } else {
            for (int k = 0; k < 4; k++) {
                int sk = k;
                if (flipH) { static const int hm[4] = {1,0,3,2}; sk = hm[sk]; }
                if (flipV) { static const int fvm[4] = {2,3,0,1}; sk = fvm[sk]; }
                cu[k] = (sk == 0 || sk == 3) ? uMaxN : 0.0f;
                cv[k] = (sk == 2 || sk == 3) ? vMaxN : 0.0f;
            }
        }
    }

    int base = r->vertexCount;
    static const int order[6] = {0,1,2, 0,2,3};
    for (int i = 0; i < 6; i++) {
        int k = order[i];
        r->vertices[base+i] = (SceneGPUVertex){
            verts[k].x, verts[k].y, verts[k].z,
            cu[k], cv[k]
        };
    }

    float mvp[16];
    int vpW = r->viewportW > 0 ? r->viewportW : 640;
    int vpH = r->viewportH > 0 ? r->viewportH : 400;
    build_mvp(mvp, &r->camera, &r->proj, vpW, vpH, r->fovMultiplier);

    SceneGPUDrawCmd *last = r->drawCmdCount > 0 ? &r->drawCmds[r->drawCmdCount-1] : NULL;
    bool batch = last && last->texture == gpuTex && last->kind == kind
              && last->forceNearest == isCloud
              && memcmp(last->mvp, mvp, sizeof(mvp)) == 0;
    if (batch) {
        last->vertexCount += 6;
    } else {
        SceneGPUDrawCmd *cmd = &r->drawCmds[r->drawCmdCount++];
        *cmd = (SceneGPUDrawCmd){
            .texture      = gpuTex,
            .vertexStart  = base,
            .vertexCount  = 6,
            .kind         = kind,
            .forceNearest = isCloud,
        };
        memcpy(cmd->mvp, mvp, sizeof(mvp));
    }
    r->vertexCount += 6;
}

/* --------------------------------------------------------------------------
 * Car draw command enqueue (called from game_render_hardware.c)
 * -------------------------------------------------------------------------- */
void scene_render_gpu_queue_car_draw(SceneRendererGPU *r,
                                      SDL_GPUBuffer *vertBuf,
                                      SDL_GPUBuffer *idxBuf,
                                      SDL_GPUTexture *texture,
                                      int firstIndex,
                                      int idxCount,
                                      const float mvp[16])
{
    if (!r || r->carDrawCount >= SCENE_GPU_MAX_CAR_DRAWS) return;
    SceneGPUCarDrawCmd *cmd = &r->carDraws[r->carDrawCount++];
    cmd->vertBuf    = vertBuf;
    cmd->idxBuf     = idxBuf;
    cmd->texture    = texture;
    cmd->firstIndex = firstIndex;
    cmd->idxCount   = idxCount;
    memcpy(cmd->mvp, mvp, 16 * sizeof(float));
}
