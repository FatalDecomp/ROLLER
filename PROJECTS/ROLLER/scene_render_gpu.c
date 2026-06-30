#include "scene_render_gpu.h"
#include "crt_filter.h"
#include "debug_overlay.h"
#include "polytex.h"              /* startsx, startsy */
#include "sound.h"                /* pal_addr */
#include "game_scene_shaders.h"   /* scene vertex/pixel SPIRV+MSL */
#include "menu_shaders.h"         /* menu_mesh_* — reused for car pipeline */
#include "game_hud_shaders.h"     /* HUD overlay vertex/pixel */
#include "game_particle_shaders.h" /* particle vertex/pixel */
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
extern int backwards;          /* drawtrk3.c: -1 = camera looks backward along track */

/* --------------------------------------------------------------------------
 * Limits
 * -------------------------------------------------------------------------- */
#define SCENE_GPU_MAX_TEXTURE_SLOTS  64
#define SCENE_GPU_MAX_VERTICES       300000
#define SCENE_GPU_MAX_DRAW_CMDS      8192
#define SCENE_GPU_MAX_CAR_DRAWS      32   /* 16 cars × up to 2 draws each (body + shadow) */
#define SCENE_GPU_MAX_PARTICLE_VERTS (576 * 6) /* 18 cars × 32 particles × 6 verts per quad */
#define SCENE_GPU_MAX_TEX_RANGES     (SCENE_GPU_MAX_PARTICLE_VERTS / 6) /* one range per quad worst case */
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

/* 2D NDC vertex for screen-space particle quads: position + flat colour. */
typedef struct {
    float x, y, z;
    float r, g, b, a;
} SceneGPUParticleVertex;

/* 2D NDC vertex for screen-space textured particle quads: position + UV + colour tint. */
typedef struct {
    float x, y, z;
    float u, v;
    float r, g, b, a;
} SceneGPUTexParticleVertex;

/* --------------------------------------------------------------------------
 * Per-texture slot — one SDL_GPUTexture per tile rather than one atlas.
 * CLAMP_TO_EDGE sampler prevents anisotropic footprint from bleeding across
 * tile boundaries.  Wall surfaces need tiles N and N+1 side by side, so we
 * also store pair textures (2*tileSize wide) for each valid adjacent pair.
 * -------------------------------------------------------------------------- */
#define SCENE_GPU_MAX_TILES_PER_SLOT 256

typedef struct {
    SDL_GPUTexture *tileTextures[SCENE_GPU_MAX_TILES_PER_SLOT];
    /* pairTextures[n] = [tile n | tile n+1]; NULL when n is the last tile. */
    SDL_GPUTexture *pairTextures[SCENE_GPU_MAX_TILES_PER_SLOT];
    /* particleTileTextures: white-patched variant where palette index 0 maps to opaque white
     * (only populated for tex_idx 18 / cargen); used by the screen-space particle path so that
     * smoke/fire quads receive the correct vertex-colour tint via (texture × vertex_colour). */
    SDL_GPUTexture *particleTileTextures[SCENE_GPU_MAX_TILES_PER_SLOT];
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
typedef enum { SCENE_GPU_DRAW_OPAQUE = 0, SCENE_GPU_DRAW_BLEND, SCENE_GPU_DRAW_BUILDING, SCENE_GPU_DRAW_SIGN, SCENE_GPU_DRAW_SIGN_BK, SCENE_GPU_DRAW_WALL, SCENE_GPU_DRAW_BF_WALL, SCENE_GPU_DRAW_SHADOW } SceneGPUDrawKind;

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
    SDL_GPUGraphicsPipeline *signPipeline;          /* LESS_OR_EQUAL + neg bias + CULL_BACK: building signs (MSAA fallback) */
    SDL_GPUGraphicsPipeline *signBkPipeline;        /* same + CULL_NONE: gen BK BF sign content (MSAA fallback) */
    SDL_GPUGraphicsPipeline *signDepthPipeline;     /* COMPARE_ALWAYS + no depth write + CULL_BACK + depth-check shader */
    SDL_GPUGraphicsPipeline *signBkDepthPipeline;   /* same + CULL_NONE */
    SDL_GPUGraphicsPipeline *wallPipeline;     /* sfBl + LESS_OR_EQUAL; for TEXTURE_PAIR surfaces */
    SDL_GPUGraphicsPipeline *bfWallPipeline;  /* like wallPipeline + small depth bias; for TEXTURE_PAIR|FLIP_BACKFACE surfaces coplanar with adjacent walls */
    SDL_GPUGraphicsPipeline *shadowPipeline;   /* blend + LESS_OR_EQUAL + no depth write + large bias; for car shadow quads coplanar with road */
    SDL_GPUSampler          *sampler;
    SDL_GPUSampler          *samplerNearest; /* always-nearest, used for cloud quads */

    SDL_GPUTexture *depthTex;
    SDL_GPUTexture *signDepthCopyTex; /* depth snapshot taken after BUILDING, before SIGN; sampled by sign depth-check shader */
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

    /* ---- Horizon split ---- */
    int   groundColorIdx;  /* palette index for ground clear colour, -1 = disabled */
    bool  skyAnyGround;    /* true when at least one screen corner is on the ground side */
    int   skyPolyN;        /* number of sky polygon vertices (0 = no sky quad) */
    float skyPoly[5][2];   /* sky region polygon in NDC, CCW (max 5 verts from S-H clip) */
    SDL_GPUGraphicsPipeline *skyPipeline;  /* no depth test/write; draws sky quad before 3D */
    SDL_GPUBuffer           *skyVertBuf;
    SDL_GPUTransferBuffer   *skyVertXfer;

    /* Flat-colour texture cache (one 4×4 solid texture per palette index, lazy).
     * paletteCacheHash is a fingerprint of palette[] at the time the cache was last
     * flushed; begin_frame recomputes it and invalidates stale entries on mismatch. */
    SDL_GPUTexture *flatColorCache[256];
    uint32_t        paletteCacheHash;
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
    int   cullMode;        /* 0=default/none, 1=none, 2=back, 3=front */

    /* Vsync is deferred: SDL_SetGPUSwapchainParameters must be called before
     * SDL_AcquireGPUSwapchainTexture, so changes requested mid-frame are applied
     * at the start of the next begin_frame. */
    bool pendingVsync;
    bool pendingVsyncSet;

    /* Optional debug overlay rendered after the final blit, before submit. */
    DebugOverlay *debugOverlay;

    /* Optional CRT post-process filter applied instead of the plain blit. */
    CRTFilter *crtFilter;

    /* ---- Screen-space particle pass ---- */
    SDL_GPUGraphicsPipeline *particlePipeline;
    SDL_GPUBuffer           *particleVertBuf;
    SDL_GPUTransferBuffer   *particleVertXfer;
    SceneGPUParticleVertex  *particleVerts;  /* CPU-side accumulation buffer */
    int                      particleVertCount;
    float                    particleNdcZ;   /* depth for next screen_quad_flat call */

    /* ---- Screen-space textured particle pass ---- */
    SDL_GPUGraphicsPipeline   *texParticlePipeline;
    SDL_GPUBuffer             *texParticleVertBuf;
    SDL_GPUTransferBuffer     *texParticleVertXfer;
    SceneGPUTexParticleVertex *texParticleVerts;
    int                        texParticleVertCount;
    struct {
        SDL_GPUTexture *tex;
        int             start; /* first vertex index in texParticleVerts */
        int             count; /* number of vertices in this range */
    }                          texParticleRanges[SCENE_GPU_MAX_TEX_RANGES];
    int                        texParticleRangeCount;
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
    if (r->signDepthCopyTex)
        SDL_ReleaseGPUTexture(r->device, r->signDepthCopyTex);
    SDL_GPUTextureCreateInfo ti = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
        .width = (Uint32)w, .height = (Uint32)h,
        .layer_count_or_depth = 1, .num_levels = 1,
        .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET
    };
    r->depthTex  = SDL_CreateGPUTexture(r->device, &ti);
    /* Depth snapshot used by the sign depth-check shader; same format as depthTex
     * for copy compatibility.  DEPTH_STENCIL_TARGET enables use as copy destination;
     * SAMPLER lets the shader Load() from it. */
    ti.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    r->signDepthCopyTex = SDL_CreateGPUTexture(r->device, &ti);
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
                                                     float biasConst,
                                                     float biasSlope,
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
    pi.rasterizer_state.fill_mode   = fillMode;
    pi.rasterizer_state.front_face  = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pi.rasterizer_state.cull_mode   = (r->cullMode == 2) ? SDL_GPU_CULLMODE_BACK
                                    : (r->cullMode == 3) ? SDL_GPU_CULLMODE_FRONT
                                    : SDL_GPU_CULLMODE_NONE;
    if (biasConst != 0.0f || biasSlope != 0.0f) {
        pi.rasterizer_state.enable_depth_bias          = true;
        pi.rasterizer_state.depth_bias_constant_factor = biasConst;
        pi.rasterizer_state.depth_bias_slope_factor    = biasSlope;
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
        .rasterizer_state = { .fill_mode  = fillMode,
                              .front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE,
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
        /* Signs drawn last (after opaque, wall, building) so the depth buffer
         * already contains all solid geometry.  LESS_OR_EQUAL + large negative
         * bias lets signs that are coplanar with or slightly behind their wall
         * surface still pass the depth test. */
        .depth_stencil_state = {
            .enable_depth_test  = true,
            .enable_depth_write = true,
            .compare_op         = SDL_GPU_COMPAREOP_LESS_OR_EQUAL
        },
        .rasterizer_state = {
            .fill_mode                  = fillMode,
            .front_face                 = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE,
            .enable_depth_bias          = true,
            .depth_bias_constant_factor = -4096.0f,
            .depth_bias_clamp           = 0.0f,
            .depth_bias_slope_factor    = -1.0f,
        }
    };
    /* Signs must only be visible from their front face (the side with correct
     * UV).  The back face, which faces the wrong side of the boundary wall,
     * renders with H-flipped texture and must be discarded. */
    pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
    return SDL_CreateGPUGraphicsPipeline(r->device, &pi);
}

/* Same depth settings as make_sign_pipeline, but no face culling.
 * Used for gen BK BF back-texture sign content: the polygon is back-facing
 * from the camera (dot < 0 triggered texture_back substitution), so BACK
 * culling would discard it. */
static SDL_GPUGraphicsPipeline *make_sign_bk_pipeline(SceneRendererGPU *r,
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
        .depth_stencil_state = {
            .enable_depth_test  = true,
            .enable_depth_write = true,
            .compare_op         = SDL_GPU_COMPAREOP_LESS_OR_EQUAL
        },
        .rasterizer_state = {
            .fill_mode                  = fillMode,
            .cull_mode                  = SDL_GPU_CULLMODE_NONE,
            .front_face                 = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE,
            .enable_depth_bias          = true,
            .depth_bias_constant_factor = -4096.0f,
            .depth_bias_clamp           = 0.0f,
            .depth_bias_slope_factor    = -1.0f,
        }
    };
    return SDL_CreateGPUGraphicsPipeline(r->device, &pi);
}

/* COMPARE_ALWAYS + depth-write off: sign rendering with depth check done in
 * the fragment shader (game_scene_sign_pixel.hlsl) against a pre-sign depth
 * snapshot.  Non-MSAA only; MSAA falls back to the bias-based signPipeline. */
static SDL_GPUGraphicsPipeline *make_sign_depth_pipeline(SceneRendererGPU *r,
                                                          SDL_GPUShader *vert,
                                                          SDL_GPUShader *fragSign,
                                                          SDL_GPUCullMode cullMode,
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
        .fragment_shader = fragSign,
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
        .multisample_state  = { .sample_count = SDL_GPU_SAMPLECOUNT_1 },
        .depth_stencil_state = {
            .enable_depth_test  = true,
            .enable_depth_write = false,
            .compare_op         = SDL_GPU_COMPAREOP_ALWAYS
        },
        .rasterizer_state = {
            .fill_mode  = fillMode,
            .cull_mode  = cullMode,
            .front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE,
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
            /* CULLMODE_BACK: each polygon's front face (exterior) is rendered;
             * pBacks[] twin polygons (reversed winding) cover the back side.
             * Shadow quad normal points +Z (upward, toward camera) so it is
             * front-facing and renders correctly with this cull mode. */
            .fill_mode  = fillMode,
            .front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE,
            .cull_mode  = SDL_GPU_CULLMODE_BACK,
            .enable_depth_bias = true,
            //.depth_bias_constant_factor = -50.0f,
            //.depth_bias_clamp = 0.0f,
            //.depth_bias_slope_factor = -1.0f,
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

static SDL_GPUGraphicsPipeline *make_sky_pipeline(SceneRendererGPU *r,
                                                      SDL_GPUShader *vert,
                                                      SDL_GPUShader *frag,
                                                      SDL_GPUSampleCount sc)
{
    SDL_GPUVertexAttribute attrs[2] = {
        {.location=0, .format=SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
         .offset=(Uint32)offsetof(SceneGPUVertex, x)},
        {.location=1, .format=SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
         .offset=(Uint32)offsetof(SceneGPUVertex, u)},
    };
    SDL_GPUVertexBufferDescription binding = {
        .pitch      = sizeof(SceneGPUVertex),
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
        .multisample_state   = { .sample_count = sc },
        .depth_stencil_state = { .enable_depth_test = false, .enable_depth_write = false }
    };
    pi.rasterizer_state.fill_mode  = SDL_GPU_FILLMODE_FILL;
    pi.rasterizer_state.cull_mode  = SDL_GPU_CULLMODE_NONE;
    pi.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    return SDL_CreateGPUGraphicsPipeline(r->device, &pi);
}

static SDL_GPUGraphicsPipeline *make_particle_pipeline(SceneRendererGPU *r,
                                                        SDL_GPUShader *vert,
                                                        SDL_GPUShader *frag,
                                                        SDL_GPUSampleCount sc)
{
    SDL_GPUVertexAttribute attrs[2] = {
        {.location=0, .format=SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
         .offset=(Uint32)offsetof(SceneGPUParticleVertex, x)},
        {.location=1, .format=SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
         .offset=(Uint32)offsetof(SceneGPUParticleVertex, r)},
    };
    SDL_GPUVertexBufferDescription binding = {
        .pitch      = sizeof(SceneGPUParticleVertex),
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
        .depth_stencil_state = {
            .enable_depth_test  = true,
            .enable_depth_write = false,
            .compare_op         = SDL_GPU_COMPAREOP_LESS_OR_EQUAL
        },
        .multisample_state = { .sample_count = sc }
    };
    pi.rasterizer_state.fill_mode  = SDL_GPU_FILLMODE_FILL;
    pi.rasterizer_state.cull_mode  = SDL_GPU_CULLMODE_NONE;
    return SDL_CreateGPUGraphicsPipeline(r->device, &pi);
}

static SDL_GPUGraphicsPipeline *make_tex_particle_pipeline(SceneRendererGPU *r,
                                                           SDL_GPUShader *vert,
                                                           SDL_GPUShader *frag,
                                                           SDL_GPUSampleCount sc)
{
    SDL_GPUVertexAttribute attrs[3] = {
        {.location=0, .format=SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
         .offset=(Uint32)offsetof(SceneGPUTexParticleVertex, x)},
        {.location=1, .format=SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
         .offset=(Uint32)offsetof(SceneGPUTexParticleVertex, u)},
        {.location=2, .format=SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
         .offset=(Uint32)offsetof(SceneGPUTexParticleVertex, r)},
    };
    SDL_GPUVertexBufferDescription binding = {
        .pitch      = sizeof(SceneGPUTexParticleVertex),
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
        .vertex_shader   = vert,
        .fragment_shader = frag,
        .primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .vertex_input_state = {
            .vertex_attributes          = attrs,
            .num_vertex_attributes      = 3,
            .vertex_buffer_descriptions = &binding,
            .num_vertex_buffers         = 1
        },
        .target_info = {
            .color_target_descriptions = &ct,
            .num_color_targets         = 1,
            .has_depth_stencil_target  = true,
            .depth_stencil_format      = SDL_GPU_TEXTUREFORMAT_D32_FLOAT
        },
        .depth_stencil_state = {
            .enable_depth_test  = true,
            .enable_depth_write = false,
            .compare_op         = SDL_GPU_COMPAREOP_LESS_OR_EQUAL
        },
        .multisample_state = { .sample_count = sc }
    };
    pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
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
    SDL_GPUShader *sfSign = load_shader(device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        game_scene_sign_pixel_spirv,  game_scene_sign_pixel_spirv_size,
        game_scene_sign_pixel_msl,    game_scene_sign_pixel_msl_size,    2, 1);
    if (!sv || !sfOp || !sfBl || !sfSign) goto fail;

    r->opaquePipeline        = make_scene_pipeline(r, sv, sfOp, false, false, 0.0f,   0.0f, SDL_GPU_SAMPLECOUNT_1, SDL_GPU_FILLMODE_FILL);
    r->blendPipeline         = make_scene_pipeline(r, sv, sfBl, true,  true,  0.0f,   0.0f, SDL_GPU_SAMPLECOUNT_1, SDL_GPU_FILLMODE_FILL);
    r->buildingPipeline      = make_scene_pipeline(r, sv, sfOp, false, true,  0.0f,   0.0f, SDL_GPU_SAMPLECOUNT_1, SDL_GPU_FILLMODE_FILL);
    r->signPipeline          = make_sign_pipeline   (r, sv, sfOp,               SDL_GPU_SAMPLECOUNT_1, SDL_GPU_FILLMODE_FILL);
    r->signBkPipeline        = make_sign_bk_pipeline(r, sv, sfOp,               SDL_GPU_SAMPLECOUNT_1, SDL_GPU_FILLMODE_FILL);
    r->signDepthPipeline     = make_sign_depth_pipeline(r, sv, sfSign, SDL_GPU_CULLMODE_BACK, SDL_GPU_FILLMODE_FILL);
    r->signBkDepthPipeline   = make_sign_depth_pipeline(r, sv, sfSign, SDL_GPU_CULLMODE_NONE, SDL_GPU_FILLMODE_FILL);
    r->wallPipeline          = make_scene_pipeline  (r, sv, sfBl, false, true,  0.0f,   0.0f, SDL_GPU_SAMPLECOUNT_1, SDL_GPU_FILLMODE_FILL);
    r->bfWallPipeline        = make_scene_pipeline  (r, sv, sfBl, false, true,  -20.0f, 0.0f, SDL_GPU_SAMPLECOUNT_1, SDL_GPU_FILLMODE_FILL);
    r->shadowPipeline        = make_shadow_pipeline (r, sv, sfBl,               SDL_GPU_SAMPLECOUNT_1, SDL_GPU_FILLMODE_FILL);
    r->skyPipeline           = make_sky_pipeline    (r, sv, sfOp, SDL_GPU_SAMPLECOUNT_1);
    SDL_ReleaseGPUShader(device, sv);
    SDL_ReleaseGPUShader(device, sfOp);
    SDL_ReleaseGPUShader(device, sfBl);
    SDL_ReleaseGPUShader(device, sfSign);
    if (!r->opaquePipeline || !r->blendPipeline || !r->buildingPipeline || !r->signPipeline || !r->signBkPipeline || !r->signDepthPipeline || !r->signBkDepthPipeline || !r->wallPipeline || !r->bfWallPipeline || !r->shadowPipeline || !r->skyPipeline) goto fail;

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

    /* ---- Particle shaders ---- */
    SDL_GPUShader *pv = load_shader(device, SDL_GPU_SHADERSTAGE_VERTEX,
        game_particle_vertex_spirv, game_particle_vertex_spirv_size,
        game_particle_vertex_msl,   game_particle_vertex_msl_size, 0, 0);
    SDL_GPUShader *pf = load_shader(device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        game_particle_pixel_spirv,  game_particle_pixel_spirv_size,
        game_particle_pixel_msl,    game_particle_pixel_msl_size,  0, 0);
    if (!pv || !pf) goto fail;
    r->particlePipeline = make_particle_pipeline(r, pv, pf, SDL_GPU_SAMPLECOUNT_1);
    SDL_ReleaseGPUShader(device, pv);
    SDL_ReleaseGPUShader(device, pf);
    if (!r->particlePipeline) { SDL_Log("PARTICLE: pipeline creation failed: %s", SDL_GetError()); goto fail; }

    /* ---- Textured particle shaders ---- */
    SDL_GPUShader *tpv = load_shader(device, SDL_GPU_SHADERSTAGE_VERTEX,
        game_particle_tex_vertex_spirv, game_particle_tex_vertex_spirv_size,
        game_particle_tex_vertex_msl,   game_particle_tex_vertex_msl_size, 0, 0);
    SDL_GPUShader *tpf = load_shader(device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        game_particle_tex_pixel_spirv,  game_particle_tex_pixel_spirv_size,
        game_particle_tex_pixel_msl,    game_particle_tex_pixel_msl_size,  1, 0);
    if (!tpv || !tpf) goto fail;
    r->texParticlePipeline = make_tex_particle_pipeline(r, tpv, tpf, SDL_GPU_SAMPLECOUNT_1);
    SDL_ReleaseGPUShader(device, tpv);
    SDL_ReleaseGPUShader(device, tpf);
    if (!r->texParticlePipeline) goto fail;

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

    /* ---- Ground strip vertex buffer (6 verts, constant size) ---- */
    {
        Uint32 gvSize = 9 * sizeof(SceneGPUVertex);  /* max 3 triangles for 5-vertex polygon */
        SDL_GPUBufferCreateInfo gbi = { .usage = SDL_GPU_BUFFERUSAGE_VERTEX, .size = gvSize };
        r->skyVertBuf = SDL_CreateGPUBuffer(device, &gbi);
        SDL_GPUTransferBufferCreateInfo gtbi = { .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = gvSize };
        r->skyVertXfer = SDL_CreateGPUTransferBuffer(device, &gtbi);
        if (!r->skyVertBuf || !r->skyVertXfer) goto fail;
    }

    /* ---- Particle vertex buffer ---- */
    {
        Uint32 pvSize = (Uint32)(SCENE_GPU_MAX_PARTICLE_VERTS * (int)sizeof(SceneGPUParticleVertex));
        SDL_GPUBufferCreateInfo pbi = { .usage = SDL_GPU_BUFFERUSAGE_VERTEX, .size = pvSize };
        r->particleVertBuf  = SDL_CreateGPUBuffer(device, &pbi);
        SDL_GPUTransferBufferCreateInfo ptbi = { .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = pvSize };
        r->particleVertXfer = SDL_CreateGPUTransferBuffer(device, &ptbi);
        r->particleVerts    = malloc(pvSize);
        if (!r->particleVertBuf || !r->particleVertXfer || !r->particleVerts) goto fail;
    }

    /* ---- Textured particle vertex buffer ---- */
    {
        Uint32 tpvSize = (Uint32)(SCENE_GPU_MAX_PARTICLE_VERTS * (int)sizeof(SceneGPUTexParticleVertex));
        SDL_GPUBufferCreateInfo tpbi = { .usage = SDL_GPU_BUFFERUSAGE_VERTEX, .size = tpvSize };
        r->texParticleVertBuf  = SDL_CreateGPUBuffer(device, &tpbi);
        SDL_GPUTransferBufferCreateInfo tptbi = { .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = tpvSize };
        r->texParticleVertXfer = SDL_CreateGPUTransferBuffer(device, &tptbi);
        r->texParticleVerts    = malloc(tpvSize);
        if (!r->texParticleVertBuf || !r->texParticleVertXfer || !r->texParticleVerts) goto fail;
    }

    r->groundColorIdx = -1;

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
            if (r->texSlots[i].particleTileTextures[t])
                SDL_ReleaseGPUTexture(r->device, r->texSlots[i].particleTileTextures[t]);
        }
    }
    for (int i = 0; i < 256; i++) {
        if (r->flatColorCache[i])
            SDL_ReleaseGPUTexture(r->device, r->flatColorCache[i]);
    }
    if (r->shadowTex)     SDL_ReleaseGPUTexture(r->device, r->shadowTex);
    if (r->offscreenTex)  SDL_ReleaseGPUTexture(r->device, r->offscreenTex);
    if (r->depthTex)          SDL_ReleaseGPUTexture(r->device, r->depthTex);
    if (r->signDepthCopyTex)  SDL_ReleaseGPUTexture(r->device, r->signDepthCopyTex);
    if (r->msaaTex)           SDL_ReleaseGPUTexture(r->device, r->msaaTex);
    if (r->msaaDepthTex)  SDL_ReleaseGPUTexture(r->device, r->msaaDepthTex);
    if (r->hudOverlayTex) SDL_ReleaseGPUTexture(r->device, r->hudOverlayTex);
    if (r->hudXfer)       SDL_ReleaseGPUTransferBuffer(r->device, r->hudXfer);
    if (r->hudVertBuf)    SDL_ReleaseGPUBuffer(r->device, r->hudVertBuf);
    if (r->skyVertBuf) SDL_ReleaseGPUBuffer(r->device, r->skyVertBuf);
    if (r->skyVertXfer) SDL_ReleaseGPUTransferBuffer(r->device, r->skyVertXfer);
    if (r->vertexBuf)     SDL_ReleaseGPUBuffer(r->device, r->vertexBuf);
    if (r->vertexXfer)    SDL_ReleaseGPUTransferBuffer(r->device, r->vertexXfer);
    free(r->vertices);
    free(r->drawCmds);
    if (r->opaquePipeline)   SDL_ReleaseGPUGraphicsPipeline(r->device, r->opaquePipeline);
    if (r->blendPipeline)    SDL_ReleaseGPUGraphicsPipeline(r->device, r->blendPipeline);
    if (r->buildingPipeline) SDL_ReleaseGPUGraphicsPipeline(r->device, r->buildingPipeline);
    if (r->signPipeline)         SDL_ReleaseGPUGraphicsPipeline(r->device, r->signPipeline);
    if (r->signBkPipeline)       SDL_ReleaseGPUGraphicsPipeline(r->device, r->signBkPipeline);
    if (r->signDepthPipeline)    SDL_ReleaseGPUGraphicsPipeline(r->device, r->signDepthPipeline);
    if (r->signBkDepthPipeline)  SDL_ReleaseGPUGraphicsPipeline(r->device, r->signBkDepthPipeline);
    if (r->wallPipeline)     SDL_ReleaseGPUGraphicsPipeline(r->device, r->wallPipeline);
    if (r->bfWallPipeline)   SDL_ReleaseGPUGraphicsPipeline(r->device, r->bfWallPipeline);
    if (r->shadowPipeline)   SDL_ReleaseGPUGraphicsPipeline(r->device, r->shadowPipeline);
    if (r->carPipeline)      SDL_ReleaseGPUGraphicsPipeline(r->device, r->carPipeline);
    if (r->skyPipeline)      SDL_ReleaseGPUGraphicsPipeline(r->device, r->skyPipeline);
    if (r->hudPipeline)      SDL_ReleaseGPUGraphicsPipeline(r->device, r->hudPipeline);
    if (r->particlePipeline)    SDL_ReleaseGPUGraphicsPipeline(r->device, r->particlePipeline);
    if (r->particleVertBuf)     SDL_ReleaseGPUBuffer(r->device, r->particleVertBuf);
    if (r->particleVertXfer)    SDL_ReleaseGPUTransferBuffer(r->device, r->particleVertXfer);
    free(r->particleVerts);
    if (r->texParticlePipeline) SDL_ReleaseGPUGraphicsPipeline(r->device, r->texParticlePipeline);
    if (r->texParticleVertBuf)  SDL_ReleaseGPUBuffer(r->device, r->texParticleVertBuf);
    if (r->texParticleVertXfer) SDL_ReleaseGPUTransferBuffer(r->device, r->texParticleVertXfer);
    free(r->texParticleVerts);
    if (r->sampler)          SDL_ReleaseGPUSampler(r->device, r->sampler);
    if (r->samplerNearest) SDL_ReleaseGPUSampler(r->device, r->samplerNearest);
    free(r);
}

/* --------------------------------------------------------------------------
 * Mouse surface-pick state
 * -------------------------------------------------------------------------- */
static bool s_pt_in_tri(float qx, float qy,
                         float ax, float ay, float bx, float by,
                         float cx, float cy)
{
    float d1 = (qx-bx)*(ay-by) - (ax-bx)*(qy-by);
    float d2 = (qx-cx)*(by-cy) - (bx-cx)*(qy-cy);
    float d3 = (qx-ax)*(cy-ay) - (cx-ax)*(qy-ay);
    return !((d1<0||d2<0||d3<0) && (d1>0||d2>0||d3>0));
}
static struct { bool active; int surfIdx; int surfaceFlags; char path[8]; float vZ; } s_clickHit;
static bool s_clickWasPending;

void scene_render_gpu_screen_quad_flat(SceneRendererGPU *r,
                                       const float ndcX[4], const float ndcY[4],
                                       float cr, float cg, float cb, float ca)
{
    if (!r || !r->particleVerts) return;
    if (r->particleVertCount + 6 > SCENE_GPU_MAX_PARTICLE_VERTS) return;
    SceneGPUParticleVertex *v = r->particleVerts + r->particleVertCount;
    /* Two triangles (v0,v1,v2) and (v0,v2,v3) covering the quad. */
    static const int idx[6] = {0, 1, 2, 0, 2, 3};
    for (int i = 0; i < 6; i++) {
        int k = idx[i];
        v[i] = (SceneGPUParticleVertex){ndcX[k], ndcY[k], r->particleNdcZ, cr, cg, cb, ca};
    }
    r->particleVertCount += 6;
}

void scene_render_gpu_set_particle_ndcz(SceneRendererGPU *r, float ndcZ)
{
    if (r) r->particleNdcZ = ndcZ;
}

/* Returns the GPU texture for tile tile_idx within game tex_idx's slot, or NULL. */
SDL_GPUTexture *scene_render_gpu_get_tile_texture(SceneRendererGPU *r, int tex_idx, int tile_idx)
{
    if (!r || tex_idx < 0 || tex_idx >= 32) return NULL;
    SceneTextureHandle slotHandle = r->texIdxToHandle[tex_idx];
    if (slotHandle == SCENE_TEXTURE_HANDLE_INVALID) return NULL;
    const SceneGPUTextureSlot *s = &r->texSlots[slotHandle];
    if (!s->in_use || tile_idx < 0 || tile_idx >= s->numTiles) return NULL;
    return s->tileTextures[tile_idx];
}

/* Returns the particle-variant tile texture for tex_idx's slot (tex_idx 18 only).
 * Falls back to the normal tile when absent. */
SDL_GPUTexture *scene_render_gpu_get_particle_tile_texture(SceneRendererGPU *r, int tex_idx, int tile_idx)
{
    if (!r || tex_idx < 0 || tex_idx >= 32) return NULL;
    SceneTextureHandle slotHandle = r->texIdxToHandle[tex_idx];
    if (slotHandle == SCENE_TEXTURE_HANDLE_INVALID) return NULL;
    const SceneGPUTextureSlot *s = &r->texSlots[slotHandle];
    if (!s->in_use || tile_idx < 0 || tile_idx >= s->numTiles) return NULL;
    if (s->particleTileTextures[tile_idx]) return s->particleTileTextures[tile_idx];
    return s->tileTextures[tile_idx];
}

/* Accumulate a textured particle quad for this frame.
 * Returns true if accepted, false if full or texture conflicts (caller falls through to SW). */
bool scene_render_gpu_screen_quad_textured(SceneRendererGPU *r,
                                           const float ndcX[4], const float ndcY[4],
                                           SDL_GPUTexture *tex,
                                           float cr, float cg, float cb, float ca)
{
    if (!r || !r->texParticleVerts || !tex) return false;
    if (r->texParticleVertCount + 6 > SCENE_GPU_MAX_PARTICLE_VERTS) return false;

    /* Extend the last range if it uses the same texture; otherwise open a new one.
     * Only checking the last range (not searching) guarantees each range's vertices
     * are contiguous in the flat buffer even when tiles interleave across particles. */
    int ri;
    if (r->texParticleRangeCount > 0 &&
        r->texParticleRanges[r->texParticleRangeCount - 1].tex == tex) {
        ri = r->texParticleRangeCount - 1;
    } else {
        if (r->texParticleRangeCount >= SCENE_GPU_MAX_TEX_RANGES) return false;
        ri = r->texParticleRangeCount++;
        r->texParticleRanges[ri].tex   = tex;
        r->texParticleRanges[ri].start = r->texParticleVertCount;
        r->texParticleRanges[ri].count = 0;
    }

    static const float uvU[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    static const float uvV[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    static const int idx[6] = {0, 1, 2, 0, 2, 3};
    SceneGPUTexParticleVertex *v = r->texParticleVerts + r->texParticleVertCount;
    for (int i = 0; i < 6; i++) {
        int k = idx[i];
        v[i] = (SceneGPUTexParticleVertex){ndcX[k], ndcY[k], r->particleNdcZ,
                                           uvU[k], uvV[k],
                                           cr, cg, cb, ca};
    }
    r->texParticleVertCount              += 6;
    r->texParticleRanges[ri].count       += 6;
    return true;
}

void scene_render_gpu_begin_frame(SceneRendererGPU *r)
{
    if (!r) return;

    /* Detect palette changes (setpal() writes palette[] directly; no GPU callback).
     * If the fingerprint changed since last frame, wait for GPU idle then purge
     * all cached flat-colour textures so they're recreated with the new palette. */
    {
        uint32_t h = 0;
        for (int i = 0; i < 256; i++)
            h = h * 2654435761u ^ ((uint32_t)palette[i].byR << 16
                                 | (uint32_t)palette[i].byG <<  8
                                 | (uint32_t)palette[i].byB);
        if (h != r->paletteCacheHash) {
            r->paletteCacheHash = h;
            /* Only stall for idle if there are textures the GPU may still be
             * using; skip the wait (and swapchain disruption) when the cache
             * is empty (e.g. always on the very first frame). */
            bool hasCached = false;
            for (int i = 0; i < 256; i++)
                if (r->flatColorCache[i]) { hasCached = true; break; }
            if (hasCached) {
                SDL_WaitForGPUIdle(r->device);
                for (int i = 0; i < 256; i++) {
                    if (r->flatColorCache[i]) {
                        SDL_ReleaseGPUTexture(r->device, r->flatColorCache[i]);
                        r->flatColorCache[i] = NULL;
                    }
                }
            }
        }
    }

    r->cmdBuf      = NULL;
    r->swapchainTex = NULL;
    if (r->pendingVsyncSet) {
        bool supportsMailbox = SDL_WindowSupportsGPUPresentMode(r->device, r->window,
                                   SDL_GPU_PRESENTMODE_MAILBOX);
        SDL_GPUPresentMode mode = r->pendingVsync
            ? (supportsMailbox ? SDL_GPU_PRESENTMODE_MAILBOX : SDL_GPU_PRESENTMODE_VSYNC)
            : SDL_GPU_PRESENTMODE_IMMEDIATE;
        if (!SDL_SetGPUSwapchainParameters(r->device, r->window,
                SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode))
            SDL_Log("scene_render_gpu: SDL_SetGPUSwapchainParameters failed: %s",
                    SDL_GetError());
        r->pendingVsyncSet = false;
    }
    r->vertexCount       = 0;
    r->drawCmdCount      = 0;
    r->carDrawCount      = 0;
    r->particleVertCount    = 0;
    r->particleNdcZ         = 0.0f;
    r->texParticleVertCount  = 0;
    r->texParticleRangeCount = 0;
    r->hudSrcBuf         = NULL;
    debug_overlay_surface_labels_reset();
    s_clickHit.active = false;
    s_clickWasPending = g_pendingClickQuery;
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

    if (s_clickWasPending) {
        if (s_clickHit.active)
            SDL_Log("PICK: %s idx=%d sf=0x%X vZ=%.1f",
                    s_clickHit.path, s_clickHit.surfIdx,
                    (unsigned)s_clickHit.surfaceFlags, (double)s_clickHit.vZ);
        else
            SDL_Log("PICK: no surface hit");
        g_pendingClickQuery = false;
    }

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
        if (mapped) {
            memcpy(mapped, r->vertices, (size_t)r->vertexCount * sizeof(SceneGPUVertex));
            SDL_UnmapGPUTransferBuffer(r->device, r->vertexXfer);

            SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(r->cmdBuf);
            SDL_GPUTransferBufferLocation srcl = {.transfer_buffer = r->vertexXfer};
            SDL_GPUBufferRegion dstr = {.buffer = r->vertexBuf,
                                        .size = (Uint32)((size_t)r->vertexCount * sizeof(SceneGPUVertex))};
            SDL_UploadToGPUBuffer(cp, &srcl, &dstr, false);
            SDL_EndGPUCopyPass(cp);
        }
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
            if (mapped) {
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
    }

    /* ---- Sky polygon vertex upload ----
     * Sky polygon is the screen region on the sky side of the (possibly tilted) horizon,
     * pre-clipped to the NDC rectangle by Sutherland-Hodgman in game_render.c.
     * Fan the polygon (3-5 verts) into triangles and upload to skyVertBuf. */
    bool drawSky = (r->groundColorIdx >= 0 && r->skyPolyN >= 3
                       && r->skyPipeline && r->skyVertBuf);
    int skyVertCount = 0;
    if (drawSky) {
        int n_tris = r->skyPolyN - 2;
        SceneGPUVertex gv[9];
        int gi = 0;
        for (int t = 0; t < n_tris; t++) {
            gv[gi++] = (SceneGPUVertex){r->skyPoly[0][0],   r->skyPoly[0][1],   0.f, 0.5f, 0.5f};
            gv[gi++] = (SceneGPUVertex){r->skyPoly[t+1][0], r->skyPoly[t+1][1], 0.f, 0.5f, 0.5f};
            gv[gi++] = (SceneGPUVertex){r->skyPoly[t+2][0], r->skyPoly[t+2][1], 0.f, 0.5f, 0.5f};
        }
        SceneGPUVertex *gvMapped = SDL_MapGPUTransferBuffer(r->device, r->skyVertXfer, false);
        if (gvMapped) {
            memcpy(gvMapped, gv, (size_t)gi * sizeof(SceneGPUVertex));
            SDL_UnmapGPUTransferBuffer(r->device, r->skyVertXfer);
            SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(r->cmdBuf);
            SDL_GPUTransferBufferLocation gsrc = {.transfer_buffer = r->skyVertXfer};
            SDL_GPUBufferRegion gdst = {.buffer = r->skyVertBuf,
                                        .size = (Uint32)(gi * (int)sizeof(SceneGPUVertex))};
            SDL_UploadToGPUBuffer(cp, &gsrc, &gdst, false);
            SDL_EndGPUCopyPass(cp);
            skyVertCount = gi;
        } else {
            drawSky = false;
        }
    }

    /* ---- Particle vertex upload ---- */
    bool drawParticles = (r->particleVertCount > 0
                          && r->particlePipeline && r->particleVerts && r->particleVertBuf);
    if (drawParticles) {
        SceneGPUParticleVertex *pm = SDL_MapGPUTransferBuffer(r->device, r->particleVertXfer, false);
        if (pm) {
            memcpy(pm, r->particleVerts, (size_t)r->particleVertCount * sizeof(SceneGPUParticleVertex));
            SDL_UnmapGPUTransferBuffer(r->device, r->particleVertXfer);
            SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(r->cmdBuf);
            SDL_GPUTransferBufferLocation psrc = {.transfer_buffer = r->particleVertXfer};
            SDL_GPUBufferRegion pdst = {.buffer = r->particleVertBuf,
                                        .size = (Uint32)(r->particleVertCount * (int)sizeof(SceneGPUParticleVertex))};
            SDL_UploadToGPUBuffer(cp, &psrc, &pdst, false);
            SDL_EndGPUCopyPass(cp);
        } else {
            drawParticles = false;
        }
    }

    /* ---- Textured particle vertex upload ---- */
    bool drawTexParticles = (r->texParticleRangeCount > 0
                             && r->texParticlePipeline && r->texParticleVerts
                             && r->texParticleVertBuf);
    if (drawTexParticles) {
        SceneGPUTexParticleVertex *tpm = SDL_MapGPUTransferBuffer(r->device, r->texParticleVertXfer, false);
        if (tpm) {
            memcpy(tpm, r->texParticleVerts,
                   (size_t)r->texParticleVertCount * sizeof(SceneGPUTexParticleVertex));
            SDL_UnmapGPUTransferBuffer(r->device, r->texParticleVertXfer);
            SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(r->cmdBuf);
            SDL_GPUTransferBufferLocation tpsrc = {.transfer_buffer = r->texParticleVertXfer};
            SDL_GPUBufferRegion tpdst = {.buffer = r->texParticleVertBuf,
                                         .size = (Uint32)(r->texParticleVertCount * (int)sizeof(SceneGPUTexParticleVertex))};
            SDL_UploadToGPUBuffer(cp, &tpsrc, &tpdst, false);
            SDL_EndGPUCopyPass(cp);
        } else {
            drawTexParticles = false;
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
    /* Fog-blended sky colour (sky is at infinity, so fog factor → 1 as density rises). */
    float skyFog = 1.0f - expf(-r->fogDensity * 100000.0f);
    if (skyFog < 0.0f) skyFog = 0.0f;
    if (skyFog > 1.0f) skyFog = 1.0f;
    SDL_FColor skyFColor = {
        r->skyR + (r->fogColor[0] - r->skyR) * skyFog,
        r->skyG + (r->fogColor[1] - r->skyG) * skyFog,
        r->skyB + (r->fogColor[2] - r->skyB) * skyFog,
        1.0f
    };
    /* Clear colour selection mirrors SW DrawHorizon logic:
     *   groundOnTop=false (normal):  ground visible when skyFrac < 0.999  → clear=ground
     *   groundOnTop=true (inverted): ground visible when skyFrac > 0.001  → clear=ground
     * When no ground is visible (all sky), fall back to skyFColor. */
    SDL_FColor skyClear = skyFColor;
    if (r->groundColorIdx >= 0) {
        bool anyGround = r->skyAnyGround;
        if (anyGround) {
            const tColor *gc = &palette[r->groundColorIdx];
            skyClear.r = gc->byR / 63.0f;
            skyClear.g = gc->byG / 63.0f;
            skyClear.b = gc->byB / 63.0f;
        }
    }

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
        /* Non-MSAA: STORE so we can copy the depth snapshot for the sign shader. */
        .store_op    = (!useMSAA) ? SDL_GPU_STOREOP_STORE : SDL_GPU_STOREOP_DONT_CARE,
        .clear_depth = 1.0f
    };
    SDL_GPURenderPass *rp = SDL_BeginGPURenderPass(r->cmdBuf, &colorInfo, 1, &depthInfo);

    SDL_GPUViewport vp = {
        .x = 0, .y = 0,
        .w = (float)renderW, .h = (float)renderH,
        .min_depth = 0.0f, .max_depth = 1.0f
    };
    SDL_SetGPUViewport(rp, &vp);

    /* Sky quad: overdraw the upper portion with sky colour so 3D geometry
     * renders on top of it.  skyPipeline has depth test/write disabled.
     * Fog overdrive (huge density) collapses the fog lerp to 1 → pure skyFColor. */
    if (drawSky) {
        SDL_GPUTexture *gTex = get_flat_color_texture(r, r->groundColorIdx);
        if (gTex) {
            SDL_BindGPUGraphicsPipeline(rp, r->skyPipeline);
            float identMVP[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
            SDL_PushGPUVertexUniformData(r->cmdBuf, 0, identMVP, sizeof(identMVP));
            struct { float fogDensity, gamma, fogStart, saturation;
                     float fogColor[4]; float contrast, brightness; float _pad[2]; } skyU = {
                1e9f, 1.0f, 0.0f, 1.0f,
                {skyFColor.r, skyFColor.g, skyFColor.b, 1.0f},
                1.0f, 0.0f, {0, 0}
            };
            SDL_PushGPUFragmentUniformData(r->cmdBuf, 0, &skyU, sizeof(skyU));
            SDL_GPUBufferBinding gvbb = {.buffer = r->skyVertBuf};
            SDL_BindGPUVertexBuffers(rp, 0, &gvbb, 1);
            SDL_GPUTextureSamplerBinding gtsb = {.texture = gTex, .sampler = r->samplerNearest};
            SDL_BindGPUFragmentSamplers(rp, 0, &gtsb, 1);
            SDL_DrawGPUPrimitives(rp, skyVertCount, 1, 0, 0);
        }
    }

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

    /* Draw order: OPAQUE → WALL → BUILDING → SIGN
     * Opaque and wall establish the depth buffer for solid track geometry.
     * Building polygons follow (LESS_OR_EQUAL + small bias for coplanar decals).
     * Signs come last so the full depth buffer is owned before they test;
     * LESS_OR_EQUAL + large negative bias lets signs coplanar with their wall
     * still pass, while signs behind opaque geometry are correctly rejected. */
    if (r->opaquePipeline) {
        SDL_BindGPUGraphicsPipeline(rp, r->opaquePipeline);
        DRAW_CMD(SCENE_GPU_DRAW_OPAQUE)
    }
    if (r->wallPipeline) {
        SDL_BindGPUGraphicsPipeline(rp, r->wallPipeline);
        DRAW_CMD(SCENE_GPU_DRAW_WALL)
    }
    if (r->bfWallPipeline) {
        SDL_BindGPUGraphicsPipeline(rp, r->bfWallPipeline);
        DRAW_CMD(SCENE_GPU_DRAW_BF_WALL)
    }
    if (r->buildingPipeline) {
        SDL_BindGPUGraphicsPipeline(rp, r->buildingPipeline);
        DRAW_CMD(SCENE_GPU_DRAW_BUILDING)
    }
    /* Sign rendering: non-MSAA uses depth-copy pass split so signs behind canyon walls
     * are correctly hidden; MSAA falls back to the old bias-based pipeline. */
    if (!useMSAA && r->signDepthPipeline && r->signBkDepthPipeline && r->signDepthCopyTex) {
        /* End Pass A, snapshot depth, begin Pass B. */
        SDL_EndGPURenderPass(rp);
        rp = NULL;

        SDL_GPUCopyPass *dcp = SDL_BeginGPUCopyPass(r->cmdBuf);
        SDL_GPUTextureLocation dcSrc = { .texture = r->depthTex };
        SDL_GPUTextureLocation dcDst = { .texture = r->signDepthCopyTex };
        SDL_CopyGPUTextureToTexture(dcp, &dcSrc, &dcDst, (Uint32)renderW, (Uint32)renderH, 1, false);
        SDL_EndGPUCopyPass(dcp);

        SDL_GPUColorTargetInfo colorInfoB = colorInfo;
        colorInfoB.load_op  = SDL_GPU_LOADOP_LOAD;
        colorInfoB.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPUDepthStencilTargetInfo depthInfoB = {
            .texture     = r->depthTex,
            .load_op     = SDL_GPU_LOADOP_LOAD,
            .store_op    = SDL_GPU_STOREOP_DONT_CARE,
            .clear_depth = 1.0f
        };
        rp = SDL_BeginGPURenderPass(r->cmdBuf, &colorInfoB, 1, &depthInfoB);
        SDL_SetGPUViewport(rp, &vp);
        SDL_BindGPUVertexBuffers(rp, 0, &vbb, 1);
        SDL_PushGPUFragmentUniformData(r->cmdBuf, 0, &pfu, sizeof(pfu));

        /* Each sign draw binds slot 0 (scene tex) + slot 1 (depth copy). */
        SDL_GPUTextureSamplerBinding signTsb[2];
        signTsb[1].texture = r->signDepthCopyTex;
        signTsb[1].sampler = r->samplerNearest;

        SDL_BindGPUGraphicsPipeline(rp, r->signDepthPipeline);
        for (int i = 0; i < r->drawCmdCount; i++) {
            SceneGPUDrawCmd *cmd = &r->drawCmds[i];
            if (cmd->kind != SCENE_GPU_DRAW_SIGN) continue;
            SDL_PushGPUVertexUniformData(r->cmdBuf, 0, cmd->mvp, sizeof(cmd->mvp));
            signTsb[0].texture = cmd->texture;
            signTsb[0].sampler = cmd->forceNearest ? r->samplerNearest : r->sampler;
            SDL_BindGPUFragmentSamplers(rp, 0, signTsb, 2);
            SDL_DrawGPUPrimitives(rp, (Uint32)cmd->vertexCount, 1, (Uint32)cmd->vertexStart, 0);
        }
        SDL_BindGPUGraphicsPipeline(rp, r->signBkDepthPipeline);
        for (int i = 0; i < r->drawCmdCount; i++) {
            SceneGPUDrawCmd *cmd = &r->drawCmds[i];
            if (cmd->kind != SCENE_GPU_DRAW_SIGN_BK) continue;
            SDL_PushGPUVertexUniformData(r->cmdBuf, 0, cmd->mvp, sizeof(cmd->mvp));
            signTsb[0].texture = cmd->texture;
            signTsb[0].sampler = cmd->forceNearest ? r->samplerNearest : r->sampler;
            SDL_BindGPUFragmentSamplers(rp, 0, signTsb, 2);
            SDL_DrawGPUPrimitives(rp, (Uint32)cmd->vertexCount, 1, (Uint32)cmd->vertexStart, 0);
        }
    } else {
        if (r->signPipeline) {
            SDL_BindGPUGraphicsPipeline(rp, r->signPipeline);
            DRAW_CMD(SCENE_GPU_DRAW_SIGN)
        }
        if (r->signBkPipeline) {
            SDL_BindGPUGraphicsPipeline(rp, r->signBkPipeline);
            DRAW_CMD(SCENE_GPU_DRAW_SIGN_BK)
        }
    }
    if (r->blendPipeline) {
        SDL_BindGPUGraphicsPipeline(rp, r->blendPipeline);
        DRAW_CMD(SCENE_GPU_DRAW_BLEND)
    }
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

    /* Particles: depth-tested against scene geometry, drawn inside the same
     * render pass so they share the live depth buffer without needing STORE. */
    if (drawParticles) {
        SDL_BindGPUGraphicsPipeline(rp, r->particlePipeline);
        SDL_GPUBufferBinding pvbb = {.buffer = r->particleVertBuf};
        SDL_BindGPUVertexBuffers(rp, 0, &pvbb, 1);
        SDL_DrawGPUPrimitives(rp, (Uint32)r->particleVertCount, 1, 0, 0);
    }
    if (drawTexParticles) {
        SDL_BindGPUGraphicsPipeline(rp, r->texParticlePipeline);
        SDL_GPUBufferBinding tpvbb = {.buffer = r->texParticleVertBuf};
        SDL_BindGPUVertexBuffers(rp, 0, &tpvbb, 1);
        for (int ri = 0; ri < r->texParticleRangeCount; ri++) {
            SDL_GPUTextureSamplerBinding tptsb = {
                .texture = r->texParticleRanges[ri].tex,
                .sampler = r->sampler
            };
            SDL_BindGPUFragmentSamplers(rp, 0, &tptsb, 1);
            SDL_DrawGPUPrimitives(rp,
                (Uint32)r->texParticleRanges[ri].count,
                1,
                (Uint32)r->texParticleRanges[ri].start,
                0);
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

void scene_render_gpu_set_horizon(SceneRendererGPU *r, int colorIdx, bool anyGround,
                                  const float (*skyPoly)[2], int n_verts)
{
    if (!r) return;
    r->groundColorIdx = colorIdx;
    r->skyAnyGround   = anyGround;
    r->skyPolyN       = (n_verts > 5) ? 5 : n_verts;
    for (int i = 0; i < r->skyPolyN; i++) {
        r->skyPoly[i][0] = skyPoly[i][0];
        r->skyPoly[i][1] = skyPoly[i][1];
    }
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
    if (r->signPipeline)         { SDL_ReleaseGPUGraphicsPipeline(r->device, r->signPipeline);         r->signPipeline         = NULL; }
    if (r->signBkPipeline)       { SDL_ReleaseGPUGraphicsPipeline(r->device, r->signBkPipeline);       r->signBkPipeline       = NULL; }
    if (r->signDepthPipeline)    { SDL_ReleaseGPUGraphicsPipeline(r->device, r->signDepthPipeline);    r->signDepthPipeline    = NULL; }
    if (r->signBkDepthPipeline)  { SDL_ReleaseGPUGraphicsPipeline(r->device, r->signBkDepthPipeline);  r->signBkDepthPipeline  = NULL; }
    if (r->wallPipeline)         { SDL_ReleaseGPUGraphicsPipeline(r->device, r->wallPipeline);         r->wallPipeline         = NULL; }
    if (r->bfWallPipeline)       { SDL_ReleaseGPUGraphicsPipeline(r->device, r->bfWallPipeline);       r->bfWallPipeline       = NULL; }
    if (r->shadowPipeline)       { SDL_ReleaseGPUGraphicsPipeline(r->device, r->shadowPipeline);       r->shadowPipeline       = NULL; }
    if (r->carPipeline)          { SDL_ReleaseGPUGraphicsPipeline(r->device, r->carPipeline);          r->carPipeline          = NULL; }
    if (r->skyPipeline)          { SDL_ReleaseGPUGraphicsPipeline(r->device, r->skyPipeline);          r->skyPipeline          = NULL; }

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
    SDL_GPUShader *sfSign = load_shader(r->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        game_scene_sign_pixel_spirv,  game_scene_sign_pixel_spirv_size,
        game_scene_sign_pixel_msl,    game_scene_sign_pixel_msl_size,    2, 1);
    if (sv && sfOp && sfBl) {
        r->opaquePipeline   = make_scene_pipeline(r, sv, sfOp, false, false, 0.0f,   0.0f, sc, fm);
        r->blendPipeline    = make_scene_pipeline(r, sv, sfBl, true,  true,  0.0f,   0.0f, sc, fm);
        r->buildingPipeline = make_scene_pipeline(r, sv, sfOp, false, true,  0.0f,   0.0f, sc, fm);
        r->signPipeline     = make_sign_pipeline   (r, sv, sfOp,               sc, fm);
        r->signBkPipeline   = make_sign_bk_pipeline(r, sv, sfOp,               sc, fm);
        r->wallPipeline     = make_scene_pipeline  (r, sv, sfBl, false, true,  0.0f,   0.0f, sc, fm);
        r->bfWallPipeline   = make_scene_pipeline  (r, sv, sfBl, false, true,  -20.0f, 0.0f, sc, fm);
        r->shadowPipeline   = make_shadow_pipeline(r, sv, sfBl,       sc,    fm);
        r->skyPipeline      = make_sky_pipeline(r, sv, sfOp, sc);
    }
    if (sv && sfSign) {
        r->signDepthPipeline   = make_sign_depth_pipeline(r, sv, sfSign, SDL_GPU_CULLMODE_BACK, fm);
        r->signBkDepthPipeline = make_sign_depth_pipeline(r, sv, sfSign, SDL_GPU_CULLMODE_NONE, fm);
    }
    if (sv)     SDL_ReleaseGPUShader(r->device, sv);
    if (sfOp)   SDL_ReleaseGPUShader(r->device, sfOp);
    if (sfBl)   SDL_ReleaseGPUShader(r->device, sfBl);
    if (sfSign) SDL_ReleaseGPUShader(r->device, sfSign);

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

void scene_render_gpu_set_cull_mode(SceneRendererGPU *r, int mode)
{
    if (!r || r->cullMode == mode) return;
    r->cullMode = mode;

    SDL_WaitForGPUIdle(r->device);

    if (r->opaquePipeline)   { SDL_ReleaseGPUGraphicsPipeline(r->device, r->opaquePipeline);   r->opaquePipeline   = NULL; }
    if (r->blendPipeline)    { SDL_ReleaseGPUGraphicsPipeline(r->device, r->blendPipeline);    r->blendPipeline    = NULL; }
    if (r->buildingPipeline) { SDL_ReleaseGPUGraphicsPipeline(r->device, r->buildingPipeline); r->buildingPipeline = NULL; }
    if (r->signPipeline)        { SDL_ReleaseGPUGraphicsPipeline(r->device, r->signPipeline);        r->signPipeline        = NULL; }
    if (r->signBkPipeline)      { SDL_ReleaseGPUGraphicsPipeline(r->device, r->signBkPipeline);      r->signBkPipeline      = NULL; }
    if (r->signDepthPipeline)   { SDL_ReleaseGPUGraphicsPipeline(r->device, r->signDepthPipeline);   r->signDepthPipeline   = NULL; }
    if (r->signBkDepthPipeline) { SDL_ReleaseGPUGraphicsPipeline(r->device, r->signBkDepthPipeline); r->signBkDepthPipeline = NULL; }
    if (r->wallPipeline)        { SDL_ReleaseGPUGraphicsPipeline(r->device, r->wallPipeline);        r->wallPipeline        = NULL; }
    if (r->bfWallPipeline)      { SDL_ReleaseGPUGraphicsPipeline(r->device, r->bfWallPipeline);      r->bfWallPipeline      = NULL; }
    if (r->shadowPipeline)      { SDL_ReleaseGPUGraphicsPipeline(r->device, r->shadowPipeline);      r->shadowPipeline      = NULL; }
    if (r->skyPipeline)         { SDL_ReleaseGPUGraphicsPipeline(r->device, r->skyPipeline);         r->skyPipeline         = NULL; }

    SDL_GPUFillMode fm = r->wireframe ? SDL_GPU_FILLMODE_LINE : SDL_GPU_FILLMODE_FILL;
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
    SDL_GPUShader *sfSign = load_shader(r->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        game_scene_sign_pixel_spirv,  game_scene_sign_pixel_spirv_size,
        game_scene_sign_pixel_msl,    game_scene_sign_pixel_msl_size,    2, 1);
    if (sv && sfOp && sfBl) {
        r->opaquePipeline   = make_scene_pipeline(r, sv, sfOp, false, false, 0.0f,   0.0f, sc, fm);
        r->blendPipeline    = make_scene_pipeline(r, sv, sfBl, true,  true,  0.0f,   0.0f, sc, fm);
        r->buildingPipeline = make_scene_pipeline(r, sv, sfOp, false, true,  0.0f,   0.0f, sc, fm);
        r->signPipeline     = make_sign_pipeline   (r, sv, sfOp,               sc, fm);
        r->signBkPipeline   = make_sign_bk_pipeline(r, sv, sfOp,               sc, fm);
        r->wallPipeline     = make_scene_pipeline  (r, sv, sfBl, false, true,  0.0f,   0.0f, sc, fm);
        r->bfWallPipeline   = make_scene_pipeline  (r, sv, sfBl, false, true,  -20.0f, 0.0f, sc, fm);
        r->shadowPipeline   = make_shadow_pipeline(r, sv, sfBl,       sc,    fm);
        r->skyPipeline      = make_sky_pipeline(r, sv, sfOp, sc);
    }
    if (sv && sfSign) {
        r->signDepthPipeline   = make_sign_depth_pipeline(r, sv, sfSign, SDL_GPU_CULLMODE_BACK, fm);
        r->signBkDepthPipeline = make_sign_depth_pipeline(r, sv, sfSign, SDL_GPU_CULLMODE_NONE, fm);
    }
    if (sv)     SDL_ReleaseGPUShader(r->device, sv);
    if (sfOp)   SDL_ReleaseGPUShader(r->device, sfOp);
    if (sfBl)   SDL_ReleaseGPUShader(r->device, sfBl);
    if (sfSign) SDL_ReleaseGPUShader(r->device, sfSign);
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
    if (r->signPipeline)         { SDL_ReleaseGPUGraphicsPipeline(r->device, r->signPipeline);         r->signPipeline         = NULL; }
    if (r->signBkPipeline)       { SDL_ReleaseGPUGraphicsPipeline(r->device, r->signBkPipeline);       r->signBkPipeline       = NULL; }
    if (r->signDepthPipeline)    { SDL_ReleaseGPUGraphicsPipeline(r->device, r->signDepthPipeline);    r->signDepthPipeline    = NULL; }
    if (r->signBkDepthPipeline)  { SDL_ReleaseGPUGraphicsPipeline(r->device, r->signBkDepthPipeline);  r->signBkDepthPipeline  = NULL; }
    if (r->wallPipeline)         { SDL_ReleaseGPUGraphicsPipeline(r->device, r->wallPipeline);         r->wallPipeline         = NULL; }
    if (r->bfWallPipeline)       { SDL_ReleaseGPUGraphicsPipeline(r->device, r->bfWallPipeline);       r->bfWallPipeline       = NULL; }
    if (r->shadowPipeline)       { SDL_ReleaseGPUGraphicsPipeline(r->device, r->shadowPipeline);       r->shadowPipeline       = NULL; }
    if (r->carPipeline)          { SDL_ReleaseGPUGraphicsPipeline(r->device, r->carPipeline);          r->carPipeline          = NULL; }
    if (r->skyPipeline)          { SDL_ReleaseGPUGraphicsPipeline(r->device, r->skyPipeline);          r->skyPipeline          = NULL; }
    if (r->particlePipeline)     { SDL_ReleaseGPUGraphicsPipeline(r->device, r->particlePipeline);     r->particlePipeline     = NULL; }
    if (r->texParticlePipeline)  { SDL_ReleaseGPUGraphicsPipeline(r->device, r->texParticlePipeline);  r->texParticlePipeline  = NULL; }

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
    SDL_GPUShader *sfSign = load_shader(r->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        game_scene_sign_pixel_spirv,  game_scene_sign_pixel_spirv_size,
        game_scene_sign_pixel_msl,    game_scene_sign_pixel_msl_size,    2, 1);
    SDL_GPUFillMode fm = r->wireframe ? SDL_GPU_FILLMODE_LINE : SDL_GPU_FILLMODE_FILL;
    if (sv && sfOp && sfBl) {
        r->opaquePipeline   = make_scene_pipeline(r, sv, sfOp, false, false, 0.0f,   0.0f, sc, fm);
        r->blendPipeline    = make_scene_pipeline(r, sv, sfBl, true,  true,  0.0f,   0.0f, sc, fm);
        r->buildingPipeline = make_scene_pipeline(r, sv, sfOp, false, true,  0.0f,   0.0f, sc, fm);
        r->signPipeline     = make_sign_pipeline   (r, sv, sfOp,               sc, fm);
        r->signBkPipeline   = make_sign_bk_pipeline(r, sv, sfOp,               sc, fm);
        r->wallPipeline     = make_scene_pipeline  (r, sv, sfBl, false, true,  0.0f,   0.0f, sc, fm);
        r->bfWallPipeline   = make_scene_pipeline  (r, sv, sfBl, false, true,  -20.0f, 0.0f, sc, fm);
        r->shadowPipeline   = make_shadow_pipeline(r, sv, sfBl,       sc,    fm);
        r->skyPipeline      = make_sky_pipeline(r, sv, sfOp, sc);
    }
    if (sv && sfSign) {
        r->signDepthPipeline   = make_sign_depth_pipeline(r, sv, sfSign, SDL_GPU_CULLMODE_BACK, fm);
        r->signBkDepthPipeline = make_sign_depth_pipeline(r, sv, sfSign, SDL_GPU_CULLMODE_NONE, fm);
    }
    if (sv)     SDL_ReleaseGPUShader(r->device, sv);
    if (sfOp)   SDL_ReleaseGPUShader(r->device, sfOp);
    if (sfBl)   SDL_ReleaseGPUShader(r->device, sfBl);
    if (sfSign) SDL_ReleaseGPUShader(r->device, sfSign);

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

    SDL_GPUShader *pv = load_shader(r->device, SDL_GPU_SHADERSTAGE_VERTEX,
        game_particle_vertex_spirv, game_particle_vertex_spirv_size,
        game_particle_vertex_msl,   game_particle_vertex_msl_size, 0, 0);
    SDL_GPUShader *pf = load_shader(r->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        game_particle_pixel_spirv,  game_particle_pixel_spirv_size,
        game_particle_pixel_msl,    game_particle_pixel_msl_size,  0, 0);
    if (pv && pf)
        r->particlePipeline = make_particle_pipeline(r, pv, pf, sc);
    if (pv) SDL_ReleaseGPUShader(r->device, pv);
    if (pf) SDL_ReleaseGPUShader(r->device, pf);

    SDL_GPUShader *tpv = load_shader(r->device, SDL_GPU_SHADERSTAGE_VERTEX,
        game_particle_tex_vertex_spirv, game_particle_tex_vertex_spirv_size,
        game_particle_tex_vertex_msl,   game_particle_tex_vertex_msl_size, 0, 0);
    SDL_GPUShader *tpf = load_shader(r->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        game_particle_tex_pixel_spirv,  game_particle_tex_pixel_spirv_size,
        game_particle_tex_pixel_msl,    game_particle_tex_pixel_msl_size,  1, 0);
    if (tpv && tpf)
        r->texParticlePipeline = make_tex_particle_pipeline(r, tpv, tpf, sc);
    if (tpv) SDL_ReleaseGPUShader(r->device, tpv);
    if (tpf) SDL_ReleaseGPUShader(r->device, tpf);
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

/* Like upload_rgba but records the upload into an EXISTING copy pass without
 * submitting the command buffer. The caller owns cmd/cp and must submit after
 * all tiles are staged. Returns the transfer buffer (caller releases it after
 * the submit); sets *out_tex on success, NULL on failure. */
static SDL_GPUTransferBuffer *stage_rgba_upload(SDL_GPUDevice *dev,
                                                 SDL_GPUCopyPass *cp,
                                                 const uint8 *rgba, int w, int h,
                                                 SDL_GPUTexture **out_tex)
{
    *out_tex = NULL;
    Uint32 levels = mip_level_count(w, h);
    SDL_GPUTextureCreateInfo ti = {0};
    ti.type                 = SDL_GPU_TEXTURETYPE_2D;
    ti.format               = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ti.width                = (Uint32)w;
    ti.height               = (Uint32)h;
    ti.layer_count_or_depth = 1;
    ti.num_levels           = levels;
    ti.usage                = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
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

    SDL_GPUTextureTransferInfo src = {.transfer_buffer = tb};
    SDL_GPUTextureRegion      dst  = {.texture = tex, .w = (Uint32)w, .h = (Uint32)h, .d = 1};
    SDL_UploadToGPUTexture(cp, &src, &dst, false);

    *out_tex = tex;
    return tb;
}

SceneTextureHandle scene_render_gpu_load_texture(SceneRendererGPU *r,
                                                  const uint8 *pixelData,
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

    /* One command buffer for all tile + pair uploads for this slot.
     * Transfer buffers are collected and released after the single submit. */
    SDL_GPUCommandBuffer *uploadCmd = SDL_AcquireGPUCommandBuffer(r->device);
    if (!uploadCmd) {
        s->in_use = 0; free(tileRgba); free(atlasRgba);
        return SCENE_TEXTURE_HANDLE_INVALID;
    }
    /* Max TBs: numTiles (tiles) + 2*(numTiles-1) (pairs + flipped) + numTiles (particle tiles) <= 4*numTiles */
    SDL_GPUTransferBuffer *tbs[SCENE_GPU_MAX_TILES_PER_SLOT * 4];
    int nTbs = 0;
    bool uploadOk = true;

    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(uploadCmd);

    /* --- Per-tile textures --- */
    for (int t = 0; t < numTiles && uploadOk; t++) {
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
        SDL_GPUTexture *tex = NULL;
        SDL_GPUTransferBuffer *tb = stage_rgba_upload(r->device, cp, tileRgba, tileSize, tileSize, &tex);
        if (!tb) { uploadOk = false; break; }
        tbs[nTbs++] = tb;
        s->tileTextures[t] = tex;
    }

    free(tileRgba);

    /* --- Particle tile textures (cargen / tex_idx 18 only) ---
     * Identical to tileTextures but palette index 0 is forced to opaque white so that
     * the particle shader's (texture × vertex_colour) multiply produces the vertex colour
     * for those pixels.  SW's POLYTEX substitutes index 0 with the car's runtime colour;
     * this is the closest GPU equivalent for smoke/fire quads. */
    if (tex_idx == 18 && uploadOk) {
        uint8 *ptRgba = malloc((size_t)(tileSize * tileSize * 4));
        if (ptRgba) {
            for (int t = 0; t < numTiles && uploadOk; t++) {
                int col = t % tilesPerRow;
                int row = t / tilesPerRow;
                for (int y = 0; y < tileSize; y++) {
                    int srcRow = row * tileSize + y;
                    for (int x = 0; x < tileSize; x++) {
                        int atlasIdx = srcRow * width + col * tileSize + x;
                        int srcOff   = atlasIdx * 4;
                        int dstOff   = (y * tileSize + x) * 4;
                        ptRgba[dstOff+0] = atlasRgba[srcOff+0];
                        ptRgba[dstOff+1] = atlasRgba[srcOff+1];
                        ptRgba[dstOff+2] = atlasRgba[srcOff+2];
                        ptRgba[dstOff+3] = atlasRgba[srcOff+3];
                    }
                }
                SDL_GPUTexture *ptex = NULL;
                SDL_GPUTransferBuffer *ptb = stage_rgba_upload(r->device, cp, ptRgba, tileSize, tileSize, &ptex);
                if (!ptb) { uploadOk = false; break; }
                tbs[nTbs++] = ptb;
                s->particleTileTextures[t] = ptex;
            }
            free(ptRgba);
        }
    }

    /* --- Pair textures: tile N (left) + tile N+1 (right), used for walls ---
     * SW's polyt reads atlas UV 0..128 from tile N's base pointer.
     * For in-row tiles (col < tilesPerRow-1), UV 64..127 reads tile N+1. ✓
     * For last-column tiles (col == tilesPerRow-1), UV 64..127 wraps
     * diagonally through the atlas memory — not a clean tile boundary.
     * Mirror tile N as the right half so there is no bleed from tile N+1. */
    int pairW = 2 * tileSize;
    uint8 *pairRgba = uploadOk ? malloc((size_t)(pairW * tileSize * 4)) : NULL;
    if (pairRgba) {
        for (int n = 0; n + 1 < numTiles && uploadOk; n++) {
            int col_n  = n % tilesPerRow;
            int row_n  = n / tilesPerRow;
            bool lastCol = (col_n == tilesPerRow - 1);
            int col_r  = lastCol ? col_n : col_n + 1;
            int row_r  = row_n;

            for (int y = 0; y < tileSize; y++) {
                int srcRowL = row_n * tileSize + y;
                int srcRowR = row_r * tileSize + y;
                for (int x = 0; x < tileSize; x++) {
                    int srcOff = (srcRowL * width + col_n * tileSize + x) * 4;
                    int dstOff = (y * pairW + x) * 4;
                    pairRgba[dstOff+0] = atlasRgba[srcOff+0];
                    pairRgba[dstOff+1] = atlasRgba[srcOff+1];
                    pairRgba[dstOff+2] = atlasRgba[srcOff+2];
                    pairRgba[dstOff+3] = atlasRgba[srcOff+3];
                    srcOff = (srcRowR * width + col_r * tileSize + x) * 4;
                    dstOff = (y * pairW + tileSize + x) * 4;
                    pairRgba[dstOff+0] = atlasRgba[srcOff+0];
                    pairRgba[dstOff+1] = atlasRgba[srcOff+1];
                    pairRgba[dstOff+2] = atlasRgba[srcOff+2];
                    pairRgba[dstOff+3] = atlasRgba[srcOff+3];
                }
            }

            SDL_GPUTexture *tex = NULL;
            SDL_GPUTransferBuffer *tb = stage_rgba_upload(r->device, cp, pairRgba, pairW, tileSize, &tex);
            if (!tb) { uploadOk = false; break; }
            tbs[nTbs++] = tb;
            s->pairTextures[n] = tex;
        }
        free(pairRgba);
    }

    SDL_EndGPUCopyPass(cp);

    /* Generate mipmaps for all uploaded textures (outside copy pass, same cmd). */
    if (uploadOk && mip_level_count(tileSize, tileSize) > 1) {
        for (int t = 0; t < numTiles; t++) {
            if (s->tileTextures[t])         SDL_GenerateMipmapsForGPUTexture(uploadCmd, s->tileTextures[t]);
            if (s->particleTileTextures[t]) SDL_GenerateMipmapsForGPUTexture(uploadCmd, s->particleTileTextures[t]);
        }
        for (int n = 0; n + 1 < numTiles; n++)
            if (s->pairTextures[n]) SDL_GenerateMipmapsForGPUTexture(uploadCmd, s->pairTextures[n]);
    }

    SDL_SubmitGPUCommandBuffer(uploadCmd);

    for (int i = 0; i < nTbs; i++)
        SDL_ReleaseGPUTransferBuffer(r->device, tbs[i]);

    free(atlasRgba);

    if (!uploadOk) {
        for (int t = 0; t < numTiles; t++) {
            if (s->tileTextures[t])         { SDL_ReleaseGPUTexture(r->device, s->tileTextures[t]);         s->tileTextures[t]         = NULL; }
            if (s->pairTextures[t])         { SDL_ReleaseGPUTexture(r->device, s->pairTextures[t]);         s->pairTextures[t]         = NULL; }
            if (s->particleTileTextures[t]) { SDL_ReleaseGPUTexture(r->device, s->particleTileTextures[t]); s->particleTileTextures[t] = NULL; }
        }
        s->in_use = 0;
        return SCENE_TEXTURE_HANDLE_INVALID;
    }

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

SceneTextureHandle scene_render_gpu_get_texture_handle(const SceneRendererGPU *r, int tex_idx)
{
    if (!r || tex_idx < 0 || tex_idx >= 32) return SCENE_TEXTURE_HANDLE_INVALID;
    return r->texIdxToHandle[tex_idx];
}

/* --------------------------------------------------------------------------
 * Texture UV map
 *
 * A global key→list map for comparing quad UV/XYZ data between GPU mode and
 * split-screen mode.  Key = SceneTextureHandle (int).  Enable by setting
 * Populated whenever g_bSurfaceLog is true. Call texture_uv_map_dump() to
 * print all collected entries, texture_uv_map_reset() to clear.
 * Only surface_uv_log records into the map (it carries the most data).
 * -------------------------------------------------------------------------- */
typedef struct {
    char     type[8];
    int      texId;
    int      surfIdx;
    uint32   surfaceFlags;
    bool     flipV, efV, efH;
    bool     pair03Left, row01Bot;
    float    cu0, cv0, cv2, bcross;
    float    v0[3], v2[3];
    float    sY[4];
} TexUVEntry;

#define TEX_UV_MAX_KEYS    64
#define TEX_UV_MAX_ENTRIES 256

typedef struct {
    int        texId;
    int        count;
    TexUVEntry entries[TEX_UV_MAX_ENTRIES];
} TexUVBucket;

static TexUVBucket s_texUVMap[TEX_UV_MAX_KEYS];
static int         s_texUVMapKeys = 0;

void texture_uv_map_reset(void)
{
    SDL_memset(s_texUVMap, 0, sizeof(s_texUVMap));
    s_texUVMapKeys = 0;
}

void texture_uv_map_dump(int texId, bool splitScreen)
{
    system("cls");
    if (texId < 0)
        SDL_Log("=== texture_uv_map: %d distinct texIds (filter=all) [%s] ===",
                s_texUVMapKeys, splitScreen ? "split-screen" : "hardware");
    else
        SDL_Log("=== texture_uv_map: %d distinct texIds (filter tex=%d) [%s] ===",
                s_texUVMapKeys, texId, splitScreen ? "split-screen" : "hardware");
    for (int b = 0; b < TEX_UV_MAX_KEYS; b++) {
        const TexUVBucket *bkt = &s_texUVMap[b];
        if (bkt->count == 0) continue;
        if (texId >= 0 && bkt->texId != texId) continue;
        for (int i = 0; i < bkt->count; i++) {
            const TexUVEntry *e = &bkt->entries[i];
            SDL_Log("%s tex=%d idx=%d sf=0x%X flipV=%d efV=%d efH=%d"
                    " p03L=%d row01Bot=%d cu0=%.3f cv0=%.3f cv2=%.3f bcross=%.3f"
                    " | v0=(%.1f,%.1f,%.1f) v2=(%.1f,%.1f,%.1f)"
                    " sY=(%.4f,%.4f,%.4f,%.4f)",
                    e->type, e->texId, e->surfIdx, e->surfaceFlags,
                    (int)e->flipV, (int)e->efV, (int)e->efH,
                    (int)e->pair03Left, (int)e->row01Bot,
                    (double)e->cu0, (double)e->cv0, (double)e->cv2,
                    (double)e->bcross,
                    (double)e->v0[0], (double)e->v0[1], (double)e->v0[2],
                    (double)e->v2[0], (double)e->v2[1], (double)e->v2[2],
                    (double)e->sY[0], (double)e->sY[1],
                    (double)e->sY[2], (double)e->sY[3]);
        }
    }
    SDL_Log("=== end texture_uv_map ===");
}

static void texture_uv_log(const char *type, int surfIdx, int surfaceFlags,
                            bool flipV, bool efV, bool efH, const char *extra)
{
    if (!g_bSurfaceLog) return;
    if (g_iSurfaceLogId < -1) return;
    if (g_iSurfaceLogId >= 0 && surfIdx != g_iSurfaceLogId) return;
    static uint32 s_n = 0;
    if ((s_n++ & 0xFF) != 0) return;
    SDL_Log("%s idx=%d sf=0x%X flipV=%d efV=%d efH=%d%s",
            type, surfIdx, surfaceFlags, (int)flipV, (int)efV, (int)efH, extra);
}
static void surface_uv_log(const char *type, int texId, int surfIdx, int surfaceFlags,
                                  bool flipV, bool efV, bool efH,
                                  bool pair03Left, bool row01Bot,
                                  float cu0, float cv0, float cv2, float bcross,
                                  float v0x, float v0y, float v0z,
                                  float v2x, float v2y, float v2z,
                                  float sY0, float sY1, float sY2, float sY3)
{
    char extra[192];
    SDL_snprintf(extra, sizeof(extra),
                 " p03L=%d row01Bot=%d cu0=%.3f cv0=%.3f cv2=%.3f bcross=%.3f"
                 " | v0xyz=(%.1f,%.1f,%.1f) v2xyz=(%.1f,%.1f,%.1f)"
                 " sY01=(%.4f,%.4f) sY23=(%.4f,%.4f)",
                 (int)pair03Left, (int)row01Bot,
                 (double)cu0, (double)cv0, (double)cv2, (double)bcross,
                 (double)v0x, (double)v0y, (double)v0z,
                 (double)v2x, (double)v2y, (double)v2z,
                 (double)sY0, (double)sY1, (double)sY2, (double)sY3);
    texture_uv_log(type, surfIdx, surfaceFlags, flipV, efV, efH, extra);

    if (!g_bSurfaceLog) return;
    /* Find or create a bucket for this texId. */
    TexUVBucket *bkt = NULL;
    for (int b = 0; b < s_texUVMapKeys; b++) {
        if (s_texUVMap[b].texId == texId) { bkt = &s_texUVMap[b]; break; }
    }
    if (!bkt && s_texUVMapKeys < TEX_UV_MAX_KEYS) {
        bkt = &s_texUVMap[s_texUVMapKeys++];
        bkt->texId = texId;
        bkt->count = 0;
    }
    if (!bkt) return;
    /* Dedup: skip if a quad with the same world position is already recorded.
     * Each tile has a unique v0 position, so this catches same-tile repeats
     * across multiple frames without discarding tiles that differ in UV. */
    for (int i = 0; i < bkt->count; i++) {
        const TexUVEntry *x = &bkt->entries[i];
        if (x->v0[0] == v0x && x->v0[1] == v0y && x->v0[2] == v0z) return;
    }
    if (bkt->count < TEX_UV_MAX_ENTRIES) {
        TexUVEntry *e = &bkt->entries[bkt->count++];
        SDL_strlcpy(e->type, type, sizeof(e->type));
        e->texId        = texId;
        e->surfIdx      = surfIdx;
        e->surfaceFlags = surfaceFlags;
        e->flipV        = flipV;   e->efV = efV;   e->efH = efH;
        e->pair03Left   = pair03Left; e->row01Bot = row01Bot;
        e->cu0 = cu0; e->cv0 = cv0; e->cv2 = cv2; e->bcross = bcross;
        e->v0[0] = v0x; e->v0[1] = v0y; e->v0[2] = v0z;
        e->v2[0] = v2x; e->v2[1] = v2y; e->v2[2] = v2z;
        e->sY[0] = sY0; e->sY[1] = sY1; e->sY[2] = sY2; e->sY[3] = sY3;
    }
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

    /* building.c always passes SCENE_RENDER_SUBDIVIDE_TYPE_SIGN for both real advert
     * signs and untextured background-city buildings.  It sets SURFACE_FLAG_GPU_IS_SIGN
     * (bit 20, BOUNCE_20 in physics — never meaningful for building polygons) on real
     * signs only, so the GPU can route them to the sign depth pipeline while background
     * buildings fall through to the building pipeline with correct depth testing. */
    bool isRealSign    = (options.subdivideType == SCENE_RENDER_SUBDIVIDE_TYPE_SIGN)
                       && (surfaceFlags & SURFACE_FLAG_GPU_IS_SIGN);
    bool isBuilding    = (options.subdivideType == SCENE_RENDER_SUBDIVIDE_TYPE_BUILDING)
                       || (options.subdivideType == SCENE_RENDER_SUBDIVIDE_TYPE_SIGN && !isRealSign);
    bool isSign        = isRealSign;
    bool isCloud       = (options.subdivideType == SCENE_RENDER_SUBDIVIDE_TYPE_CLOUD);

    SceneGPUTextureSlot *slot = NULL;
    SDL_GPUTexture *gpuTex    = NULL;
    if (texture > 0 && texture < SCENE_GPU_MAX_TEXTURE_SLOTS &&
        r->texSlots[texture].in_use) {
        slot = &r->texSlots[texture];
    }

    bool isFlatColor = false;
    int  surfIdx     = surfaceFlags & SURFACE_MASK_TEXTURE_INDEX;

    /* SW applies texture_back[] substitution when SURFACE_FLAG_BACK (0x800) is set
     * and the camera is on the back side of the polygon.  The substituted type gives
     * a completely different tile (e.g. the advertisement face of a sign board).
     * GPU must replicate this so "gen BK BF" surfaces show the back tile when the
     * camera approaches from behind, instead of always showing the front tile. */
    bool isBackTexSign = false;
    if ((surfaceFlags & SURFACE_FLAG_BACK) && slot) {
        float e1x = verts[1].x - verts[0].x, e1y = verts[1].y - verts[0].y, e1z = verts[1].z - verts[0].z;
        float e2x = verts[2].x - verts[0].x, e2y = verts[2].y - verts[0].y, e2z = verts[2].z - verts[0].z;
        float nx = e1y*e2z - e1z*e2y, ny = e1z*e2x - e1x*e2z, nz = e1x*e2y - e1y*e2x;
        float dot = nx*(r->camera.viewX - verts[0].x)
                  + ny*(r->camera.viewY - verts[0].y)
                  + nz*(r->camera.viewZ - verts[0].z);
        if (dot < 0.0f) {
            int newType = texture_back[256 * slot->tex_idx + surfIdx];
            int newIdx  = newType & SURFACE_MASK_TEXTURE_INDEX;
            if (newIdx >= 0 && newIdx < slot->numTiles) {
                surfIdx = newIdx;
                isBackTexSign = true;
            }
        }
    }

    /* Wall pair textures only apply to non-building track surfaces. */
    bool isWall = !isBuilding && (surfaceFlags & SURFACE_FLAG_TEXTURE_PAIR) && wide_on;

    /* Back-face cull for non-two-sided track surfaces.
     * SW's scan-line renderer implicitly culls back-facing polygons because
     * reversed vertex winding produces empty scan-line spans.  The GPU renders
     * all triangles regardless, so we replicate the cull using a screen-space
     * cross product that matches SW polytex exactly.
     * SW backface: cross_SW = (v0-v1)×(v0-v2) > 0.  cross_GPU = -cross_SW
     * (Y-flip between Y-DOWN SW and Y-UP GPU), so GPU backface: cross_GPU < 0.
     * FLIP_BACKFACE/BACK = explicitly two-sided; bypass.
     * TEXTURE_PAIR = wall/road surfaces that can appear back-facing on hills
     * or from the pit lane but must still render; bypass.
     * Building/sign quads are already culled by building.c.
     * TRANSPARENT surfaces (glass road, pit-lane ceiling) must render from
     * both sides — SW's polyt() never culls them.
     * CONCAVE = SW's facing_ok check is bypassed for concave outer-wall sections
     * (LLOWALL/LUOWALL/RLOWALL/RUOWALL in drawtrk3.c), so these surfaces always
     * render in SW regardless of winding. Loop sections rely on this: the outer
     * walls at section boundaries are back-facing from outside the loop but must
     * render to produce the dark dividing lines between sections. */
    /* SURFACE_FLAG_TEXTURE_PAIR removed from bypass: SW culls non-BF pair
     * surfaces from behind (polytex backface check fires).  GPU must match.
     * All legitimately two-sided pair surfaces (loop road, etc.) carry
     * SURFACE_FLAG_FLIP_BACKFACE explicitly, so they stay in the bypass. */
    if (!isBuilding && !isSign && !isCloud
        && !(surfaceFlags & (SURFACE_FLAG_FLIP_BACKFACE | SURFACE_FLAG_BACK
                             | SURFACE_FLAG_TRANSPARENT
                             | SURFACE_FLAG_CONCAVE)))
    {
        const float (*M)[3] = r->proj.view;
        float sx[3], sy[3];
        for (int vi = 0; vi < 3; vi++) {
            float ddx = verts[vi].x - r->camera.viewX;
            float ddy = verts[vi].y - r->camera.viewY;
            float ddz = verts[vi].z - r->camera.viewZ;
            float vZ = ddx*M[0][2] + ddy*M[1][2] + ddz*M[2][2];
            float iz = (fabsf(vZ) > 1e-6f) ? 1.0f / vZ : 1.0f;
            sx[vi] = (ddx*M[0][0] + ddy*M[1][0] + ddz*M[2][0]) * iz;
            sy[vi] = (ddx*M[0][1] + ddy*M[1][1] + ddz*M[2][1]) * iz;
        }
        float cross = (sx[0]-sx[1])*(sy[0]-sy[2])
                    - (sy[0]-sy[1])*(sx[0]-sx[2]);
        if (cross < 0.0f) return;
    }

    /* For single-sided TEXTURE_PAIR walls (no FLIP_BACKFACE/BACK) check the
     * camera is on the front side of the wall plane before applying the pair
     * texture.  The screen-space cross product can flip sign at grazing angles
     * (near-zero cross near the pit lane), so use the world-space face normal
     * dot product which is purely positional and not affected by perspective.
     * If the camera is on the back side (dot < 0), suppress the pair texture
     * so the plain wall colour shows instead of the sign graphic. */
    bool wallFrontFacing = true;
    if (isWall && !(surfaceFlags & (SURFACE_FLAG_FLIP_BACKFACE | SURFACE_FLAG_BACK))) {
        float e1x = verts[1].x - verts[0].x;
        float e1y = verts[1].y - verts[0].y;
        float e1z = verts[1].z - verts[0].z;
        float e2x = verts[2].x - verts[0].x;
        float e2y = verts[2].y - verts[0].y;
        float e2z = verts[2].z - verts[0].z;
        float nx = e1y*e2z - e1z*e2y;
        float ny = e1z*e2x - e1x*e2z;
        float nz = e1x*e2y - e1y*e2x;
        float dot = nx*(r->camera.viewX - verts[0].x)
                  + ny*(r->camera.viewY - verts[0].y)
                  + nz*(r->camera.viewZ - verts[0].z);
        if (dot < 0.0f) wallFrontFacing = false;
    }

    if (slot) {
        /* Wall surfaces use the pair texture (tile N + tile N+1 side by side).
         * Fall back to the single tile if no pair was created (last-column tile). */
        if (isWall && wallFrontFacing && surfIdx >= 0 && surfIdx < slot->numTiles
                   && slot->pairTextures[surfIdx])
            gpuTex = slot->pairTextures[surfIdx];
        else {
            if (isWall && wallFrontFacing && surfIdx >= 0 && surfIdx < slot->numTiles
                       && !slot->pairTextures[surfIdx]) {
                /* PAIR-MISS: fires when a wall surface expects a pair texture but the
                 * atlas had no adjacent tile to pair with for this surfIdx.  Useful for
                 * spotting tiles where the pair upload was skipped (out-of-bounds, odd
                 * tile at end of row, etc.) so the wall falls back to the single tile. */
                static uint32 s_pairMissLogged = 0;
                if (!(s_pairMissLogged & (1u << (surfIdx & 31)))) {
                    s_pairMissLogged |= 1u << (surfIdx & 31);
                    SDL_Log("PAIR-MISS: sf=0x%X surfIdx=%d tileSize=%d numTiles=%d "
                            "v0=(%.0f,%.0f,%.0f)",
                            surfaceFlags, surfIdx, slot->tileSize, slot->numTiles,
                            (double)verts[0].x, (double)verts[0].y, (double)verts[0].z);
                }
            }
            if (surfIdx >= 0 && surfIdx < slot->numTiles)
                gpuTex = slot->tileTextures[surfIdx];
        }
    }

    if (!gpuTex) {
        if (surfaceFlags & SURFACE_FLAG_APPLY_TEXTURE) {
            /* PARTIAL_TRANS and CONCAVE surfaces are rendered as flat-color in SW
             * (poly()/twpoly() with palette[surfIdx]).  Fall through to the
             * flat-color path below to replicate that appearance. */
            if (surfaceFlags & SURFACE_FLAG_PARTIAL_TRANS) {
                /* fall through — use flat-color path below */
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
        kind = g_bSignsOnTop ? SCENE_GPU_DRAW_SIGN : SCENE_GPU_DRAW_BUILDING;
    else if (isBackTexSign)
        kind = SCENE_GPU_DRAW_SIGN_BK;
    else if (gpuTex == r->shadowTex)
        kind = SCENE_GPU_DRAW_SHADOW;
    else if (surfaceFlags & SURFACE_FLAG_TRANSPARENT)
        kind = SCENE_GPU_DRAW_BLEND;
    else if (surfaceFlags & SURFACE_FLAG_PARTIAL_TRANS)
        kind = SCENE_GPU_DRAW_BUILDING;
    else if (surfaceFlags & SURFACE_FLAG_FLIP_BACKFACE)
        kind = SCENE_GPU_DRAW_BF_WALL;
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

        if (usePair) {
            /* Both wall families use the same Standard winding: v0=TL, v1=TR, v2=BR, v3=BL.
             * The world-Z discriminant distinguishes them:
             *   Z-facing (Standard branch, layoutB=false): wall runs along world-X; all four
             *     vertices share the same Z → |v0.z-v3.z|=0, |v0.z-v1.z|=0 → 0<0 = false.
             *   X-facing (Layout B branch, layoutB=true): wall runs along world-Z; the left
             *     column {v0=TL,v3=BL} shares one Z and the right column {v1=TR,v2=BR} shares
             *     another → |v0.z-v3.z|=0, |v0.z-v1.z|>0 → 0<dz = true. */
            const float (*M)[3] = r->proj.view;
            float vX[4], vY[4], vZ[4];
            for (int vi = 0; vi < 4; vi++) {
                float ddx = verts[vi].x - r->camera.viewX;
                float ddy = verts[vi].y - r->camera.viewY;
                float ddz = verts[vi].z - r->camera.viewZ;
                vX[vi] = ddx*M[0][0] + ddy*M[1][0] + ddz*M[2][0];
                vY[vi] = ddx*M[0][1] + ddy*M[1][1] + ddz*M[2][1];
                vZ[vi] = ddx*M[0][2] + ddy*M[1][2] + ddz*M[2][2];
            }
            float sX[4], sY[4];
            for (int vi = 0; vi < 4; vi++) {
                float iz = fabsf(vZ[vi]) > 1e-6f ? 1.0f / vZ[vi] : 1.0f;
                sX[vi] = vX[vi] * iz;
                sY[vi] = vY[vi] * iz;
            }

            bool effectiveFlipH = flipH;

            bool layoutB = fabsf(verts[0].z - verts[3].z) < fabsf(verts[0].z - verts[1].z);
            bool flipV = !isBuilding && (surfaceFlags & SURFACE_FLAG_FLIP_VERT) != 0;

            if (layoutB) {
                /* X-facing wall: {v0=TL,v3=BL}=left col, {v1=TR,v2=BR}=right col (co-Z per column).
                 *                {v0=TL,v1=TR}=top row, {v2=BR,v3=BL}=bottom row.
                 * U (assigned per column) runs horizontally; V (assigned per row) runs vertically.
                 * col01Left compares (sX[TL]+sX[TR]) vs (sX[BR]+sX[BL]); since TL/BL and TR/BR
                 * share screen-X on X-facing walls, this sum is always equal → col01Left=false, so
                 * without correction V defaults to vMaxN at the top row (upside-down).
                 * FLIP_HORIZ fixes both: U controls which tile (n vs n+1) lands on the left column,
                 * and the V swap puts V=0 at the top row. FLIP_VERT adds an extra V inversion.
                 * Two-sided sign walls (FLIP_BACKFACE) come in pairs: one FV and one non-FV surface.
                 * Sign content is at GPU V=0 (top of tile).  The formula gives top=0 when
                 * effectiveFlipV=true.  FV BF already gives top=0 (flipV=true → effectiveFlipV=true).
                 * Non-FV BF however gives top=vMaxN (gray) because flipV=false → effectiveFlipV=false.
                 * Both surfaces are depth-sorted; from the backward direction the non-FV surface is
                 * drawn last, overwriting the FV sign pixels with gray.
                 * effectiveFlipV forces V-flip for non-CONCAVE BF pair surfaces (sign walls) so
                 * sign content is sampled from both sides.  Ceiling lights (CONCAVE|BF, e.g.
                 * t:118/120/124) must NOT get the BF override — they use flipV alone. */
                bool effectiveFlipV = flipV || ((surfaceFlags & SURFACE_FLAG_FLIP_BACKFACE) != 0
                                               && (surfaceFlags & SURFACE_FLAG_CONCAVE) == 0);
                bool col01Left = (sX[0]+sX[1]) < (sX[2]+sX[3]);
                float uT = effectiveFlipH ? 0.0f  : uMaxN;
                float uB = effectiveFlipH ? uMaxN : 0.0f;
                cu[0] = uT; cu[3] = uT;
                cu[1] = uB; cu[2] = uB;
                float vL, vR;
                if ((surfaceFlags & SURFACE_FLAG_CONCAVE) != 0) {
                    /* CONCAVE panels (ceiling lights, tunnel ends): col01Left is unstable
                     * for horizontal surfaces — bypass it like Z-face bypasses row01Bot.
                     * Assign V directly from vertex index to match SW startsy default.
                     * flipH only affects U (startsx) in SW, never startsy — no V swap. */
                    vL = flipV ? vMaxN : 0.0f;
                    vR = flipV ? 0.0f  : vMaxN;
                } else {
                    vL = (col01Left != effectiveFlipV) ? 0.0f  : vMaxN;
                    vR = (col01Left != effectiveFlipV) ? vMaxN : 0.0f;
                    if (flipH) { float tmp = vL; vL = vR; vR = tmp; }
                }
                cv[0] = vL; cv[1] = vL;
                cv[2] = vR; cv[3] = vR;
                surface_uv_log("PAIR-X", texture, surfIdx, surfaceFlags,
                               flipV, effectiveFlipV, effectiveFlipH,
                               col01Left, false,
                               cu[0], cv[0], cv[2], 0.0f,
                                     verts[0].x, verts[0].y, verts[0].z,
                                     verts[2].x, verts[2].y, verts[2].z,
                                     sY[0], sY[1], sY[2], sY[3]);
            } else {
                /* Z-facing wall: {v0=TL,v3=BL}=left col, {v1=TR,v2=BR}=right col.
                 * pair03Left determines which world-space column is the "left" tile (N).
                 * Must use world-space X, not screen-space sX: for BF surfaces (e.g. ceiling),
                 * the camera can look from either end of the polygon, flipping sX left/right
                 * while the world tile assignment must remain stable.  SW assigns startsx[] in
                 * world space; this mirrors that.  U runs horizontally, V runs vertically.
                 * FLIP_HORIZ swaps U, FLIP_VERT inverts V.
                 * Sign content sits at V=vMaxN (bottom of pair tile).
                 * FLIP_BACKFACE pair walls come in FV+non-FV pairs; both must give top=vMaxN
                 * so the sign is visible regardless of which surface wins the depth sort.
                 * effectiveFlipV forces V-flip for non-CONCAVE BF pair surfaces (sign walls) so
                 * sign content is sampled from both sides.  Ceiling lights (CONCAVE|BF, e.g.
                 * t:118/120/124) must NOT get the BF override — they use flipV alone. */
                bool effectiveFlipV = flipV || ((surfaceFlags & SURFACE_FLAG_FLIP_BACKFACE) != 0
                                               && (surfaceFlags & SURFACE_FLAG_CONCAVE) == 0);
                bool pair03Left = (verts[0].x + verts[3].x) < (verts[1].x + verts[2].x);
                float s_bcross = 0.0f;
                if ((surfaceFlags & SURFACE_FLAG_FLIP_BACKFACE) != 0) {
                    s_bcross = (sX[1]-sX[0])*(sY[3]-sY[0])
                             - (sY[1]-sY[0])*(sX[3]-sX[0]);
                }
                /* SW set_starts(1): startsx[0]=startsx[3]=max, startsx[1]=startsx[2]=0.
                 * polyt assigns U=max to the v0/v3 edge and U=0 to the v1/v2 edge
                 * regardless of which world-side they're on; screen-space tile placement
                 * then follows from polyt's winding-adaptive edge traversal.
                 * pair03Left is used only by the BF screen-check above, not for U. */
                float u03 = uMaxN;
                float u12 = 0.0f;
                /* FH: swap u03/u12 — matches SW which reverses the U scan so the
                 * tile-N/N+1 world assignment is preserved. */
                if (effectiveFlipH) {
                    float tmp = u03; u03 = u12; u12 = tmp;
                }
                cu[0] = u03; cu[1] = u12; cu[2] = u12; cu[3] = u03;
                /* Determine screen-bottom row from projected Y rather than fixed vertex index.
                 * For walls v0,v1=top(high sY) so row01IsBottom=false→isBottom=(k==2||k==3).
                 * For sloped/ceiling surfaces v0,v1 can be the lower screen row, flipping this.
                 * Guard with vZ: when NEXT section (v0,v1) is behind the camera (vZ<0) in
                 * reverse drive, sY = vY/vZ flips sign (floor verts below camera go positive).
                 * Snapping behind-camera sY to -1e9 prevents row01IsBottom and spansCameraCenter
                 * from inverting — they stay consistent with the forward-drive assignment. */
                float adjSY[4];
                for (int vi = 0; vi < 4; vi++)
                    adjSY[vi] = (vZ[vi] > 0.0f) ? sY[vi] : -1e9f;
                bool row01IsBottom = (adjSY[0]+adjSY[1]) < (adjSY[2]+adjSY[3]);
                /* Use row01IsBottom when BF non-CONCAVE (effectiveFlipV): the BF vertex
                 * swap changes which screen-row v0 occupies without changing SW's startsy[0],
                 * so screen-row detection is required to keep sign content visible.
                 * Also use it when the camera lies between the two vertex rows (sum01 and
                 * sum23 have opposite signs): at those extreme perspective angles the fixed
                 * SW vertex-index assignment maps the wrong tile region to the visible area.
                 * For all other efV=0 cases (small sY spread, same-sign rows), SW's fixed
                 * vertex-index assignment is correct and row01IsBottom is spurious. */
                bool spansCameraCenter = ((adjSY[0]+adjSY[1]) < 0.0f) != ((adjSY[2]+adjSY[3]) < 0.0f);
                bool useRowDetect = (effectiveFlipV || spansCameraCenter)
                                    && !flipV
                                    && (surfaceFlags & SURFACE_FLAG_CONCAVE) == 0;
                for (int k = 0; k < 4; k++) {
                    bool isBottom = useRowDetect
                                        ? (row01IsBottom ? (k==0||k==1) : (k==2||k==3))
                                        : (k==2||k==3);
                    cv[k] = (isBottom != effectiveFlipV) ? vMaxN : 0.0f;
                }
                surface_uv_log("PAIR-Z", texture, surfIdx, surfaceFlags,
                               flipV, effectiveFlipV, effectiveFlipH,
                               pair03Left, row01IsBottom,
                               cu[0], cv[0], cv[2], s_bcross,
                                     verts[0].x, verts[0].y, verts[0].z,
                                     verts[2].x, verts[2].y, verts[2].z,
                                     sY[0], sY[1], sY[2], sY[3]);
            }
        } else if (isCloud) {
            /* Cloud quads: the SW renderer's polyt() walks edges based on
             * winding order, which makes u=0 always land on the screen-left
             * side regardless of vertex order.  Replicate that by projecting
             * pairs (v0,v3) and (v1,v2) to screen X and assigning u=0 to
             * whichever pair is screen-left.  FLIP_HORIZ then inverts this. */
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
            /* POLYTEX applies FLIP_VERT for clouds (startsy swap).
             * Clouds randomly get SURFACE_FLAG_FLIP_VERT set (horizon.c).
             * Unlike walls, clouds must honour it to match SW behaviour. */
            bool flipV = (surfaceFlags & SURFACE_FLAG_FLIP_VERT) != 0;
            cu[0] = u03; cu[1] = u12; cu[2] = u12; cu[3] = u03;
            for (int k = 0; k < 4; k++) {
                bool isBottom = (k == 2 || k == 3);
                cv[k] = (isBottom != flipV) ? vMaxN : 0.0f;
            }
        } else if (isBuilding) {
            /* Buildings (SURFACE_FLAG_BUILDING / SUBDIVIDE_TYPE_BUILDING):
             * atlas tiles are stored right-way-up; FLIP_VERT must not be applied
             * even when set — it would invert V and flip the texture. */
            for (int k = 0; k < 4; k++) {
                int sk = k;
                if (flipH) { static const int hm[4] = {1,0,3,2}; sk = hm[sk]; }
                cu[k] = (sk == 0 || sk == 3) ? uMaxN : 0.0f;
                bool isBottom = (k == 2 || k == 3);
                cv[k] = isBottom ? vMaxN : 0.0f;
            }
            surface_uv_log("BLDG", texture, surfIdx, surfaceFlags,
                           false, false, flipH, false, false,
                           cu[0], cv[0], cv[2], 0.0f,
                           verts[0].x, verts[0].y, verts[0].z,
                           verts[2].x, verts[2].y, verts[2].z,
                           0.0f, 0.0f, 0.0f, 0.0f);
        } else if (isSign) {
            /* Roadside signs / adverts (SUBDIVIDE_TYPE_SIGN = 667):
             * same winding convention as general track surfaces; honour FLIP_VERT. */
            bool flipV = (surfaceFlags & SURFACE_FLAG_FLIP_VERT) != 0;
            for (int k = 0; k < 4; k++) {
                int sk = k;
                if (flipH) { static const int hm[4] = {1,0,3,2}; sk = hm[sk]; }
                cu[k] = (sk == 0 || sk == 3) ? uMaxN : 0.0f;
                bool isBottom = (k == 2 || k == 3);
                cv[k] = (isBottom != flipV) ? vMaxN : 0.0f;
            }
            surface_uv_log("SIGN", texture, surfIdx, surfaceFlags,
                           flipV, flipV, flipH, false, false,
                           cu[0], cv[0], cv[2], 0.0f,
                           verts[0].x, verts[0].y, verts[0].z,
                           verts[2].x, verts[2].y, verts[2].z,
                           0.0f, 0.0f, 0.0f, 0.0f);
        } else if (isWall) {
            /* Non-pair walls: isWall=true but usePair=false (pair texture not
             * loaded for this tile index).  Single-tile fallback; honour FLIP_VERT. */
            bool flipV = (surfaceFlags & SURFACE_FLAG_FLIP_VERT) != 0;
            for (int k = 0; k < 4; k++) {
                int sk = k;
                if (flipH) { static const int hm[4] = {1,0,3,2}; sk = hm[sk]; }
                cu[k] = (sk == 0 || sk == 3) ? uMaxN : 0.0f;
                bool isBottom = (k == 2 || k == 3);
                cv[k] = (isBottom != flipV) ? vMaxN : 0.0f;
            }
            surface_uv_log("WALL", texture, surfIdx, surfaceFlags,
                           flipV, flipV, flipH, false, false,
                           cu[0], cv[0], cv[2], 0.0f,
                           verts[0].x, verts[0].y, verts[0].z,
                           verts[2].x, verts[2].y, verts[2].z,
                           0.0f, 0.0f, 0.0f, 0.0f);
        } else {
            /* General track surfaces: road, ground, mountain sides, concrete
             * barrier walls (non-pair, non-TEXTURE_PAIR), fences, and everything
             * else.  FLIP_VERT honoured for V.
             *
             * For FLIP_BACKFACE (BF) gen surfaces: SW polytex assigns U based on
             * sorted screen-X of the vertices, so the texture direction is the same
             * regardless of which face the camera sees.  GPU's fixed vertex-index
             * UV causes h-flip when camera is on the back side of FH surfaces.
             * See FH+BF subcase below for the fix. */
            bool flipV = (surfaceFlags & SURFACE_FLAG_FLIP_VERT) != 0;
            float bfCross = 0.0f;
            if ((surfaceFlags & SURFACE_FLAG_FLIP_BACKFACE) && flipH) {
                /* FH+BF: SW keeps FH on the front face and cancels it on the back
                 * face.  Need to know which face the camera is on.  Compute GPU
                 * screen-space cross product (sign inverted vs SW Y-DOWN screen). */
                const float (*Mv)[3] = r->proj.view;
                float gsx[4], gsy[4], gvZ[4];
                for (int vi = 0; vi < 4; vi++) {
                    float ddx = verts[vi].x - r->camera.viewX;
                    float ddy = verts[vi].y - r->camera.viewY;
                    float ddz = verts[vi].z - r->camera.viewZ;
                    float vZ2 = ddx*Mv[0][2] + ddy*Mv[1][2] + ddz*Mv[2][2];
                    gvZ[vi] = vZ2;
                    /* SW clamps fProjectedZ at 80 before perspective division so
                     * behind-camera vertices (vZ<0) don't invert gsx/gsy and corrupt
                     * the bfCross sign or g03Left ordering. Match that here. */
                    float vZ2c = (vZ2 < 80.0f) ? 80.0f : vZ2;
                    float iz  = 1.0f / vZ2c;
                    gsx[vi]   = (ddx*Mv[0][0] + ddy*Mv[1][0] + ddz*Mv[2][0]) * iz;
                    gsy[vi]   = (ddx*Mv[0][1] + ddy*Mv[1][1] + ddz*Mv[2][1]) * iz;
                }
                bfCross = (gsx[0]-gsx[1])*(gsy[0]-gsy[2])
                        - (gsy[0]-gsy[1])*(gsx[0]-gsx[2]);
                bool bfBack = (bfCross < 0.0f);
                /* if (g_bSurfaceLog && (g_iSurfaceLogId < 0 || g_iSurfaceLogId == surfIdx)) {
                    float cx = (verts[0].x+verts[1].x+verts[2].x+verts[3].x)*0.25f - r->camera.viewX;
                    float cy = (verts[0].y+verts[1].y+verts[2].y+verts[3].y)*0.25f - r->camera.viewY;
                    float cz = (verts[0].z+verts[1].z+verts[2].z+verts[3].z)*0.25f - r->camera.viewZ;
                    float dist = sqrtf(cx*cx + cy*cy + cz*cz);
                    SDL_Log("GEN-BFHF idx=%d sf=0x%X bfCross=%.4f bfBack=%d backwards=%d match=%d vZ=%.1f/%.1f/%.1f/%.1f dist=%.1f",
                            surfIdx, surfaceFlags, (double)bfCross,
                            (int)bfBack, backwards, (int)(bfBack == (backwards != 0)),
                            (double)gvZ[0], (double)gvZ[1], (double)gvZ[2], (double)gvZ[3],
                            (double)dist);
                } */
                /* Screen-space g03Left: tracks which screen side v0/v3 land on.
                 * For surfaces seen from both sides (BF wall, loop), bfCross and
                 * gsx flip together, so (g03Left != bfBack) stays constant — correct.
                 * Exception: reverse drive (backwards!=0) front-facing floor surface
                 * (bfBack=false). Camera rotates 180° → gsx flips without a matching
                 * bfCross flip, so g03Left_screen incorrectly inverts. In that case
                 * only, use world-space X, which is stable across drive direction. */
                bool g03Left;
                if (backwards != 0 && !bfBack)
                    g03Left = (verts[0].x + verts[3].x) < (verts[1].x + verts[2].x);
                else
                    g03Left = (gsx[0]+gsx[3]) < (gsx[1]+gsx[2]);

                bool uLeftHi = (g03Left != bfBack) != flipV;
                cu[0] = cu[3] = uLeftHi ? uMaxN : 0.0f;
                cu[1] = cu[2] = uLeftHi ? 0.0f  : uMaxN;
            } else {
                /* Non-FH (or non-BF) gen surfaces: SW polytex's backface swap +
                 * flipH toggle cancel out in UV space for non-FH surfaces — the
                 * net screen-space UV is unchanged regardless of which face the
                 * camera sees.  Assign U by vertex index, honouring flipH only. */
                for (int k = 0; k < 4; k++) {
                    int sk = k;
                    if (flipH) { static const int hm[4] = {1,0,3,2}; sk = hm[sk]; }
                    cu[k] = (sk == 0 || sk == 3) ? uMaxN : 0.0f;
                }
            }
            for (int k = 0; k < 4; k++) {
                bool isBottom = (k == 2 || k == 3);
                cv[k] = (isBottom != flipV) ? vMaxN : 0.0f;
            }
            surface_uv_log("GEN", texture, surfIdx, surfaceFlags,
                           flipV, flipV, flipH, false, false,
                           cu[0], cv[0], cv[2], bfCross,
                           verts[0].x, verts[0].y, verts[0].z,
                           verts[2].x, verts[2].y, verts[2].z,
                           0.0f, 0.0f, 0.0f, 0.0f);
        }
    }

    {
        const bool *_kb = SDL_GetKeyboardState(NULL);
        bool _showLabels = g_bSurfaceDebugViz
                        || _kb[SDL_SCANCODE_LSHIFT] || _kb[SDL_SCANCODE_RSHIFT];
        if ((_showLabels && !isCloud) || s_clickWasPending) {
            bool bIsPair = !isFlatColor && isWall && slot
                         && slot->pairTextures[surfIdx];
            const char *pPath = isFlatColor ? "flat"
                              : bIsPair     ? "pair"
                              : isCloud     ? "cloud"
                              : isBuilding  ? "bldg"
                              : isSign      ? "sign"
                              : isWall      ? "wall"
                              :               "gen";
            const float (*M)[3] = r->proj.view;
            int vpW = r->viewportW > 0 ? r->viewportW : 640;
            int vpH = r->viewportH > 0 ? r->viewportH : 400;
            float ss   = (float)r->proj.screenScale / 64.0f;
            float fovX = 2.0f * r->camera.fovScale * g_fFovMultiplier * ss / (float)vpW;
            float fovY = 2.0f * r->camera.fovScale * g_fFovMultiplier * ss / (float)vpH;

            if (_showLabels && !isCloud) {
                /* Project quad centre to normalised viewport [0,1]. */
                float wx = (verts[0].x+verts[1].x+verts[2].x+verts[3].x)*0.25f;
                float wy = (verts[0].y+verts[1].y+verts[2].y+verts[3].y)*0.25f;
                float wz = (verts[0].z+verts[1].z+verts[2].z+verts[3].z)*0.25f;
                float ddx = wx-r->camera.viewX, ddy = wy-r->camera.viewY, ddz = wz-r->camera.viewZ;
                float vX = ddx*M[0][0]+ddy*M[1][0]+ddz*M[2][0];
                float vY = ddx*M[0][1]+ddy*M[1][1]+ddz*M[2][1];
                float vZ = ddx*M[0][2]+ddy*M[1][2]+ddz*M[2][2];
                if (vZ > 0.5f && vZ < 10000.0f) {
                    float iz = 1.0f / vZ;
                    float nx = (1.0f + fovX * vX * iz) * 0.5f;
                    float ny = (1.0f - fovY * vY * iz) * 0.5f;
                    if (nx >= -0.1f && nx <= 1.1f && ny >= -0.1f && ny <= 1.1f) {
                        char szLabel[128];
                        int n = snprintf(szLabel, sizeof(szLabel), "%s t:%d", pPath, surfIdx);
                        if (surfaceFlags & SURFACE_FLAG_FLIP_VERT)     n += snprintf(szLabel+n, sizeof(szLabel)-n, " FV");
                        if (surfaceFlags & SURFACE_FLAG_FLIP_HORIZ)    n += snprintf(szLabel+n, sizeof(szLabel)-n, " FH");
                        if (surfaceFlags & SURFACE_FLAG_BACK)          n += snprintf(szLabel+n, sizeof(szLabel)-n, " BK");
                        if (surfaceFlags & SURFACE_FLAG_FLIP_BACKFACE) n += snprintf(szLabel+n, sizeof(szLabel)-n, " BF");
                        if (surfaceFlags & SURFACE_FLAG_PARTIAL_TRANS) n += snprintf(szLabel+n, sizeof(szLabel)-n, " PT");
                        (void)n;
                        debug_overlay_surface_label_push(nx, ny, szLabel);
                    }
                }
            }

            if (s_clickWasPending) {
                /* Project all 4 vertices; reject if any are behind the camera. */
                float pnx[4], pny[4];
                float vZsum = 0.0f;
                bool allFront = true;
                for (int vi = 0; vi < 4; vi++) {
                    float ddxi = verts[vi].x - r->camera.viewX;
                    float ddyi = verts[vi].y - r->camera.viewY;
                    float ddzi = verts[vi].z - r->camera.viewZ;
                    float vZi  = ddxi*M[0][2] + ddyi*M[1][2] + ddzi*M[2][2];
                    if (vZi < 0.5f) { allFront = false; break; }
                    float vXi  = ddxi*M[0][0] + ddyi*M[1][0] + ddzi*M[2][0];
                    float vYi  = ddxi*M[0][1] + ddyi*M[1][1] + ddzi*M[2][1];
                    float izi  = 1.0f / vZi;
                    pnx[vi] = (1.0f + fovX * vXi * izi) * 0.5f;
                    pny[vi] = (1.0f - fovY * vYi * izi) * 0.5f;
                    vZsum  += vZi;
                }
                if (allFront) {
                    float qx = g_clickQueryNX, qy = g_clickQueryNY;
                    bool hit =
                        s_pt_in_tri(qx, qy, pnx[0], pny[0], pnx[1], pny[1], pnx[2], pny[2]) ||
                        s_pt_in_tri(qx, qy, pnx[0], pny[0], pnx[2], pny[2], pnx[3], pny[3]);
                    if (hit && (!s_clickHit.active || vZsum < s_clickHit.vZ)) {
                        s_clickHit.active      = true;
                        s_clickHit.surfIdx     = surfIdx;
                        s_clickHit.surfaceFlags = surfaceFlags;
                        SDL_snprintf(s_clickHit.path, sizeof(s_clickHit.path), "%s", pPath);
                        s_clickHit.vZ          = vZsum;
                    }
                }
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
