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
#include "png_writer.h"           /* RollerWriteRgbaPng -- "Pick Textures as PNG" debug export */
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
#define SCENE_GPU_MAX_SECONDARY_VIEWS 2 /* rearview/side mirror uses slot 0; 2-player uses slots 0/1 */
/* SCENE_GPU_NEAR / SCENE_GPU_FAR defined in scene_render_gpu.h */

/* Mirrors polyf.c's SHADE_PALETTE_LEVELS (5) -- shadow_poly()'s per-pixel
 * palette-darken remap for TRANSPARENT non-textured track quads (glass/
 * divider walls). Levels 0-3 are a straight brightness ramp (see
 * func2.c's shade_palette builder: each level subtracts ~1/5 of the
 * ORIGINAL colour per level, so level k keeps roughly (4-k)/5 brightness);
 * level 4 is a special blend-toward-teal(0xDB) shade that this
 * approximation doesn't replicate exactly -- just uses a similarly dark
 * flat value for it. */
#define SCENE_GPU_SHADE_LEVELS 5
static const float g_sceneGpuShadeFactor[SCENE_GPU_SHADE_LEVELS] = {
    0.8f, 0.6f, 0.4f, 0.2f, 0.3f
};

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

/* Cap on SDL_GenerateMipmapsForGPUTexture calls per command buffer submit
 * during texture load -- see the comment above the mipmap generation loop
 * in scene_render_gpu_load_texture for why. */
#define SCENE_GPU_MIPMAP_BATCH_SIZE 16

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
    /* Persistent copy of the full converted atlas (RGBA, tightly packed,
     * atlasWidth*atlasHeight*4 bytes) -- kept alive for on-demand PNG export
     * of whatever tile/pair is currently picked (see g_bSaveTexOnPick).
     * Owned by this slot; freed in scene_render_gpu_free_texture. */
    uint8          *atlasRgbaCopy;
    int             atlasWidth;
    int             atlasHeight;
} SceneGPUTextureSlot;

/* Forward declaration: defined further down (near scene_render_gpu_quad_world_legacy),
 * used by scene_render_gpu_end_frame's "Pick Textures as PNG" export. */
static void save_picked_texture_png(const SceneGPUTextureSlot *slot, int surfIdx, bool isPair);

/* --------------------------------------------------------------------------
 * Deferred draw commands
 * -------------------------------------------------------------------------- */
typedef enum { SCENE_GPU_DRAW_OPAQUE = 0, SCENE_GPU_DRAW_BLEND, SCENE_GPU_DRAW_BUILDING, SCENE_GPU_DRAW_BF_BUILDING, SCENE_GPU_DRAW_SIGN, SCENE_GPU_DRAW_SIGN_BK, SCENE_GPU_DRAW_TREE, SCENE_GPU_DRAW_WALL, SCENE_GPU_DRAW_BF_WALL, SCENE_GPU_DRAW_SHADOW, SCENE_GPU_DRAW_TRACK_DARKEN } SceneGPUDrawKind;

typedef struct {
    SDL_GPUTexture  *texture;
    int              vertexStart;
    int              vertexCount;
    SceneGPUDrawKind kind;
    bool             forceNearest;
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
    SDL_GPUGraphicsPipeline *buildingPipeline;   /* opaque + LESS_OR_EQUAL; for PT and isBuilding surfaces */
    SDL_GPUGraphicsPipeline *bfBuildingPipeline; /* like buildingPipeline + small depth bias; for PT|FLIP_BACKFACE fence panels coplanar with walls */
    SDL_GPUGraphicsPipeline *signPipeline;          /* LESS_OR_EQUAL + neg bias + CULL_BACK: building signs (MSAA fallback) */
    SDL_GPUGraphicsPipeline *signBkPipeline;        /* same + CULL_NONE: gen BK BF sign content (MSAA fallback) */
    SDL_GPUGraphicsPipeline *signDepthPipeline;     /* COMPARE_ALWAYS + no depth write + CULL_BACK + depth-check shader */
    SDL_GPUGraphicsPipeline *signBkDepthPipeline;   /* same + CULL_NONE */
    SDL_GPUGraphicsPipeline *treePipeline;          /* COMPARE_ALWAYS + no depth write + CULL_NONE: tree billboards always visible */
    SDL_GPUGraphicsPipeline *wallPipeline;     /* sfBl + LESS_OR_EQUAL; for TEXTURE_PAIR surfaces */
    SDL_GPUGraphicsPipeline *bfWallPipeline;  /* like wallPipeline + small depth bias; for TEXTURE_PAIR|FLIP_BACKFACE surfaces coplanar with adjacent walls */
    SDL_GPUGraphicsPipeline *shadowPipeline;   /* blend + LESS_OR_EQUAL + no depth write + large bias; for car shadow quads coplanar with road */
    SDL_GPUGraphicsPipeline *trackDarkenPipeline; /* multiply blend (DST_COLOR*ZERO) + LESS_OR_EQUAL, no depth write;
                                                    * for TRANSPARENT non-textured TRACK world quads (drawtrk3.c glass/
                                                    * divider walls) -- SW renders these via shadow_poly()'s per-pixel
                                                    * palette-darken remap (polyf.c), not a fixed-alpha blend toward
                                                    * black. This was previously misrouted through shadowTex/SHADOW
                                                    * (a genuine car-shadow-only path that real car shadows never
                                                    * actually reach -- they use carShadowDraws[]/screen quads
                                                    * instead), which under-darkened these track surfaces. */
    SDL_GPUGraphicsPipeline *trackDarkenBorderPipeline; /* optional compatibility shader: same multiply pipeline,
                                                         * but squares the source factor along quad-local edges. */
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

    /* Scratch buffers for repack_draw_order(): physically reorders
     * r->vertices into sorted (kind,texture,forceNearest) order so
     * same-texture runs become vertex-contiguous and can be merged into one
     * draw call, instead of just reducing sampler rebinds. Sized the same as
     * vertices/drawCmds since a repack can never produce more of either. */
    SceneGPUVertex  *repackVertices;     /* heap-allocated, SCENE_GPU_MAX_VERTICES */
    SceneGPUDrawCmd *repackDrawCmds;     /* heap-allocated, SCENE_GPU_MAX_DRAW_CMDS */

    /* ---- Perf-investigation counters (see g_bRenderStatsLog) ----
     * Running peaks, never reset -- highest drawCmdCount/vertexCount seen
     * since the renderer was created, to gauge real headroom against
     * SCENE_GPU_MAX_DRAW_CMDS/SCENE_GPU_MAX_VERTICES over a full lap. */
    int              peakDrawCmdCount;
    int              peakVertexCount;
    /* Actual GPU-level SDL_DrawGPUPrimitives/BindGPUFragmentSamplers calls
     * this frame (world-quad geometry only: draw_cmd_kind + the road-shadow
     * pass), reset every begin_frame. drawCmdCount above is just the queued
     * METADATA count -- sorting by texture (compare_draw_order) cut rebinds
     * but not draw-call count, since each metadata entry still got its own
     * draw call regardless. gpuDrawCallsThisFrame is the real number after
     * merging vertex-contiguous same-texture commands in draw_cmd_kind. */
    int              gpuDrawCallsThisFrame;
    int              gpuBindsThisFrame;

    /* CPU frame-time breakdown (SDL_GetTicksNS deltas, nanoseconds), reset/
     * filled once per frame. queueNs covers everything between begin_frame
     * and end_frame being called (per-quad geometry generation in the game's
     * own render loop) -- captured by stamping frameStartNs in begin_frame
     * and reading it back at the top of end_frame. The rest are phases
     * within end_frame itself. */
    Uint64           frameStartNs;
    Uint64           queueNs;
    Uint64           sortRepackNs;
    Uint64           uploadNs;         /* sum of the 5 sub-phases below */
    Uint64           vertexMapWaitNs;  /* subset of vertexUploadNs: time inside the
                                        * vertex transfer buffer's SDL_MapGPUTransferBuffer
                                        * call alone (cycle=true can block until the
                                        * GPU is done with a previous use of the
                                        * buffer) -- isolates a GPU-availability
                                        * stall from the actual memcpy/copy-pass cost. */
    Uint64           vertexUploadNs;
    Uint64           hudUploadNs;      /* indexed_to_rgba (full hudW*hudH CPU convert,
                                        * same cost every frame regardless of scene
                                        * complexity) + HUD texture upload -- prime
                                        * suspect for uploadNs staying ~flat regardless
                                        * of vertex count. */
    Uint64           hudMapWaitNs;     /* subset of hudUploadNs: HUD transfer buffer's
                                        * own SDL_MapGPUTransferBuffer call alone. */
    Uint64           hudConvertNs;     /* subset of hudUploadNs: indexed_to_rgba call
                                        * alone (CPU palette lookup, hudPixelCount
                                        * iterations) -- isolates the CPU conversion
                                        * cost from the surrounding GPU upload calls. */
    int              hudPixelCount;    /* hudSrcW*hudSrcH last frame HUD was uploaded. */
    Uint64           skyUploadNs;
    Uint64           particleUploadNs;
    Uint64           texParticleUploadNs;
    Uint64           drawNs;
    Uint64           hudPostNs;
    Uint64           submitNs;

    /* ---- Car mesh pipeline (SceneGPUMeshVertex: pos[3]+uv[2]+col[4]) ---- */
    SDL_GPUGraphicsPipeline *carPipeline;
    SDL_GPUGraphicsPipeline *carShadowPipeline; /* LESS_OR_EQUAL + no depth write + bias */

    SceneGPUCarDrawCmd carDraws[SCENE_GPU_MAX_CAR_DRAWS];
    int                carDrawCount;
    SceneGPUCarDrawCmd carShadowDraws[SCENE_GPU_MAX_CAR_DRAWS];
    int                carShadowDrawCount;

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
    SDL_GPUTexture *darkenTex;       /* fully-opaque black for the pause-menu darken quad */
    SDL_GPUTexture *shadeLevelTex[SCENE_GPU_SHADE_LEVELS]; /* solid gray, one per shade_palette
                                                             * darkening level (see trackDarkenPipeline) --
                                                             * bound as an opaque source texture for the
                                                             * multiply-blend track-darken pass; RGB =
                                                             * shadeFactor*255 approximating shade_palette's
                                                             * per-level brightness (~80/60/40/20/30%). */

    /* Offscreen colour target rendered at native resolution, blitted to the
     * swapchain with letterbox/pillarbox scaling to fill the window. */
    SDL_GPUTexture *offscreenTex;
    int             offscreenW, offscreenH;

    /* Secondary offscreen views: let another camera's queued draws (produced
     * via the normal camera/projection/draw_car/quad_world API) render to
     * their own small target instead of the main swapchain, then get reset
     * so the next queued scene starts clean. Indexed by slot so multiple
     * secondary views can coexist within one frame without one flush's
     * render pass overwriting another's texture before it's composited --
     * used by the rearview/side mirror (slot 0 only) and 2-player split
     * screen (slot 0 = player 1, slot 1 = player 2). */
    SDL_GPUTexture *secondaryColorTex[SCENE_GPU_MAX_SECONDARY_VIEWS];
    SDL_GPUTexture *secondaryDepthTex[SCENE_GPU_MAX_SECONDARY_VIEWS];
    int             secondaryTexW[SCENE_GPU_MAX_SECONDARY_VIEWS];
    int             secondaryTexH[SCENE_GPU_MAX_SECONDARY_VIEWS];

    /* MSAA render target for the secondary-view pass (only allocated when
     * msaaSampleCount > 1), resolved into secondaryColorTex/DepthTex above --
     * mirrors the main view's msaaTex->offscreenTex pattern (ensure_msaa_
     * textures). Without this, flush_secondary_view was binding the shared
     * scene pipelines (built with sample_count = msaaSampleCount) into a
     * render pass targeting single-sample secondaryColorTex/DepthTex -- a
     * pipeline/render-pass sample-count mismatch that's invalid on Vulkan/
     * D3D12 and can hang the driver instead of erroring cleanly. Tracked with
     * its own W/H (not secondaryTexW/H) so an MSAA level change alone (no
     * resize) still forces a rebuild -- scene_render_gpu_set_msaa resets
     * these to 0 for exactly that reason. */
    SDL_GPUTexture *secondaryMsaaColorTex[SCENE_GPU_MAX_SECONDARY_VIEWS];
    SDL_GPUTexture *secondaryMsaaDepthTex[SCENE_GPU_MAX_SECONDARY_VIEWS];
    int             secondaryMsaaW[SCENE_GPU_MAX_SECONDARY_VIEWS];
    int             secondaryMsaaH[SCENE_GPU_MAX_SECONDARY_VIEWS];

    /* texParticle (textured particle / smoke) counts snapshotted BEFORE a
     * secondary view's draw_road() call queues anything, via
     * scene_render_gpu_secondary_view_will_queue() -- consumed by the next
     * scene_render_gpu_flush_secondary_view() call to discard exactly what
     * that view's own scene contributed (real smoke, which isn't drawn by
     * the trimmed secondary pass) while preserving whatever an EARLIER
     * view's composite quad already added. Taking this snapshot INSIDE the
     * flush itself is too late -- draw_road() has already run by then. */
    int secPreTexParticleVertCount;
    int secPreTexParticleRangeCount;

    /* MSAA: separate multisample colour + depth targets.  The resolved result
     * lands in offscreenTex (or swapchainTex when offscreen isn't available). */
    SDL_GPUTexture     *msaaTex;
    SDL_GPUTexture     *msaaDepthTex;
    int                 msaaW, msaaH;
    SDL_GPUSampleCount  msaaSampleCount; /* SDL_GPU_SAMPLECOUNT_1 = disabled */

    int   textureFilter;   /* 0=nearest, 1=bilinear, 2=anisotropic */
    bool  trilinear;       /* true = LINEAR mipmap mode; textures always have full mip chain */
    bool  disableMipmaps;  /* true = clamp sampler to mip 0 only (debug: isolates Adreno layout bug) */
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
    bool  emulateSoftwareTrackDarkenBorder;

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
    float                    particleNdcZv[4]; /* per-vertex depth override (v0..v3) */
    bool                     useParticleNdcZv; /* true = use particleNdcZv instead of particleNdcZ */

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
    /* dst is typically SDL_MapGPUTransferBuffer-mapped memory (upload-heap,
     * often write-combined/uncached on the CPU side) -- four separate 1-byte
     * stores per pixel there is dramatically slower than one combined 4-byte
     * store, since WC write-combining buffers need a full aligned burst to
     * combine efficiently. Confirmed via CPU frame-time instrumentation: a
     * first attempt at this fix (precomputed LUT, but still 4 byte stores
     * per pixel) barely moved the ~4.8-5.4ms/frame HUD-convert cost, which
     * only dropping the (byR*255)/63 divisions should NOT have left
     * unchanged if division were the real bottleneck -- the byte-store
     * pattern is. Fixed by packing dst as uint32_t and writing one store per
     * pixel; the LUT is also now a pure uint32 pack (R,G,B,A memory order,
     * matching SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, native byte order on
     * this little-endian target), so per-pixel work is one table read + one
     * store, no division either way. */
    uint32_t lut[256];
    if (!pal) {
        for (int i = 0; i < 256; i++) lut[i] = 0xFFFF00FFu; /* magenta, opaque: R=FF G=00 B=FF A=FF (memory order R,G,B,A) */
    } else {
        for (int i = 0; i < 256; i++) {
            const tColor *c = &pal[i];
            uint32_t r = (uint32_t)((c->byR * 255) / 63);
            uint32_t g = (uint32_t)((c->byG * 255) / 63);
            uint32_t b = (uint32_t)((c->byB * 255) / 63);
            uint32_t a = (i == 0) ? 0u : 255u;
            lut[i] = r | (g << 8) | (b << 16) | (a << 24);
        }
    }
    uint32_t *dst32 = (uint32_t *)dst;
    for (int i = 0; i < count; i++) {
        dst32[i] = lut[src[i]];
    }
}

static Uint32 mip_level_count(int w, int h)
{
    Uint32 n = 1;
    Uint32 d = (Uint32)(w > h ? w : h);
    while (d > 1) { d >>= 1; n++; }
    return n;
}

/* Uploads a fully-populated RGBA8 buffer as a new 2D texture, acquiring and
 * submitting its own dedicated command buffer synchronously (safe to call
 * any time -- unlike the batched per-tile uploads in
 * scene_render_gpu_load_texture, this does NOT touch r->cmdBuf, so it's for
 * one-off/small textures created once at init or on demand: flat-color
 * cache entries, car/menu atlases, HUD fallback textures. Not for per-frame
 * streaming data, which needs a cycled transfer buffer instead -- see the
 * cycle=true comments elsewhere in this file).
 *
 * generateMipmaps: true for textures sampled at a range of distances (track
 * tiles, flat-color fills that can appear far away); false for textures
 * always shown at native/fixed size (menu UI, car body atlases), where mip
 * levels would be wasted generation work for no visual benefit.
 *
 * Consolidated 2026-07-06 from three near-identical copies (this function,
 * game_render_hardware.c's hw_upload_rgba, menu_render_gpu.c's UploadRGBA)
 * that had silently drifted apart: the menu copy had grown extra input/cp
 * validation the other two lacked (adopted here for all callers), and only
 * this copy generated mipmaps (now a parameter instead of hidden behavior). */
SDL_GPUTexture *scene_render_gpu_upload_rgba(SDL_GPUDevice *dev, const uint8 *rgba,
                                              int w, int h, bool generateMipmaps)
{
    if (!dev || !rgba || w <= 0 || h <= 0) return NULL;

    Uint32 levels = generateMipmaps ? mip_level_count(w, h) : 1;
    SDL_GPUTextureCreateInfo ti = {0};
    ti.type        = SDL_GPU_TEXTURETYPE_2D;
    ti.format      = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ti.width       = (Uint32)w;
    ti.height      = (Uint32)h;
    ti.layer_count_or_depth = 1;
    ti.num_levels  = levels;
    ti.usage       = SDL_GPU_TEXTUREUSAGE_SAMPLER
                    | (generateMipmaps ? SDL_GPU_TEXTUREUSAGE_COLOR_TARGET : 0);
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
    if (!cp) { SDL_CancelGPUCommandBuffer(cmd); SDL_ReleaseGPUTransferBuffer(dev, tb); SDL_ReleaseGPUTexture(dev, tex); return NULL; }
    SDL_GPUTextureTransferInfo src = {.transfer_buffer = tb};
    SDL_GPUTextureRegion dst = {.texture = tex, .w = (Uint32)w, .h = (Uint32)h, .d = 1};
    SDL_UploadToGPUTexture(cp, &src, &dst, false);
    SDL_EndGPUCopyPass(cp);
    if (generateMipmaps && levels > 1)
        SDL_GenerateMipmapsForGPUTexture(cmd, tex);
    if (!SDL_SubmitGPUCommandBuffer(cmd)) {
        SDL_ReleaseGPUTransferBuffer(dev, tb);
        SDL_ReleaseGPUTexture(dev, tex);
        return NULL;
    }
    SDL_ReleaseGPUTransferBuffer(dev, tb);
    return tex;
}

/* Uploads a data buffer as a new GPU buffer (vertex/index/etc.), acquiring
 * and submitting its own dedicated command buffer synchronously -- see
 * scene_render_gpu_upload_rgba for why (one-off buffers, not per-frame
 * streaming data). Consolidated 2026-07-06 from three near-identical copies
 * (this function, game_render_hardware.c's hw_upload_gpu_buffer,
 * menu_render_gpu.c's UploadGPUBuffer). */
SDL_GPUBuffer *scene_render_gpu_upload_buffer(SDL_GPUDevice *dev, SDL_GPUBufferUsageFlags usage,
                                               const void *data, Uint32 size)
{
    if (!dev || !data || size == 0) return NULL;

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
    if (!cp) { SDL_CancelGPUCommandBuffer(cmd); SDL_ReleaseGPUTransferBuffer(dev, tb); SDL_ReleaseGPUBuffer(dev, buf); return NULL; }
    SDL_GPUTransferBufferLocation srcloc = {.transfer_buffer = tb};
    SDL_GPUBufferRegion dstreg = {.buffer = buf, .size = size};
    SDL_UploadToGPUBuffer(cp, &srcloc, &dstreg, false);
    SDL_EndGPUCopyPass(cp);
    if (!SDL_SubmitGPUCommandBuffer(cmd)) {
        SDL_ReleaseGPUTransferBuffer(dev, tb);
        SDL_ReleaseGPUBuffer(dev, buf);
        return NULL;
    }
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

static void ensure_secondary_textures(SceneRendererGPU *r, int slot, int w, int h)
{
    /* MSAA intermediate target, checked/rebuilt independently of the resolve
     * target below (its own w/h tracking) since an MSAA level change with no
     * resize must still trigger a rebuild -- see the struct comment. */
    SDL_GPUSampleCount sc = r->msaaSampleCount;
    if (!(r->secondaryMsaaColorTex[slot] && r->secondaryMsaaW[slot] == w && r->secondaryMsaaH[slot] == h)) {
        if (r->secondaryMsaaColorTex[slot]) { SDL_ReleaseGPUTexture(r->device, r->secondaryMsaaColorTex[slot]); r->secondaryMsaaColorTex[slot] = NULL; }
        if (r->secondaryMsaaDepthTex[slot]) { SDL_ReleaseGPUTexture(r->device, r->secondaryMsaaDepthTex[slot]); r->secondaryMsaaDepthTex[slot] = NULL; }
        r->secondaryMsaaW[slot] = w; r->secondaryMsaaH[slot] = h;
        if (sc > SDL_GPU_SAMPLECOUNT_1) {
            SDL_GPUTextureCreateInfo mci = {
                .type = SDL_GPU_TEXTURETYPE_2D,
                .format = SDL_GetGPUSwapchainTextureFormat(r->device, r->window),
                .width = (Uint32)w, .height = (Uint32)h,
                .layer_count_or_depth = 1, .num_levels = 1,
                .sample_count = sc,
                .usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET
            };
            r->secondaryMsaaColorTex[slot] = SDL_CreateGPUTexture(r->device, &mci);
            SDL_GPUTextureCreateInfo mdi = {
                .type = SDL_GPU_TEXTURETYPE_2D,
                .format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
                .width = (Uint32)w, .height = (Uint32)h,
                .layer_count_or_depth = 1, .num_levels = 1,
                .sample_count = sc,
                .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET
            };
            r->secondaryMsaaDepthTex[slot] = SDL_CreateGPUTexture(r->device, &mdi);
        }
    }

    if (r->secondaryColorTex[slot] && r->secondaryTexW[slot] == w && r->secondaryTexH[slot] == h)
        return;
    if (r->secondaryColorTex[slot]) SDL_ReleaseGPUTexture(r->device, r->secondaryColorTex[slot]);
    if (r->secondaryDepthTex[slot]) SDL_ReleaseGPUTexture(r->device, r->secondaryDepthTex[slot]);
    SDL_GPUTextureCreateInfo ci = {
        .type   = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GetGPUSwapchainTextureFormat(r->device, r->window),
        .width  = (Uint32)w, .height = (Uint32)h,
        .layer_count_or_depth = 1, .num_levels = 1,
        .usage  = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER
    };
    r->secondaryColorTex[slot] = SDL_CreateGPUTexture(r->device, &ci);
    SDL_GPUTextureCreateInfo di = {
        .type   = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
        .width  = (Uint32)w, .height = (Uint32)h,
        .layer_count_or_depth = 1, .num_levels = 1,
        .usage  = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET
    };
    r->secondaryDepthTex[slot] = SDL_CreateGPUTexture(r->device, &di);
    r->secondaryTexW[slot] = w;
    r->secondaryTexH[slot] = h;
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
    r->flatColorCache[colorIdx] = scene_render_gpu_upload_rgba(r->device, rgba, 4, 4, true);
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

/* Multiply blend (result = srcColor * dstColor): approximates shadow_poly()'s
 * per-pixel palette-darken remap for TRANSPARENT non-textured track quads
 * (see trackDarkenPipeline / shadeLevelTex comments). LESS_OR_EQUAL depth
 * test against the opaque geometry already drawn beneath it, no depth write
 * -- lets two adjacent/overlapping track-darken quads (e.g. at a chunk
 * boundary) both composite rather than the second one losing a depth tie,
 * matching SW's no-depth-buffer painter-style double-darkening at seams. */
static SDL_GPUGraphicsPipeline *make_track_darken_pipeline(SceneRendererGPU *r,
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
            .src_color_blendfactor = SDL_GPU_BLENDFACTOR_DST_COLOR,
            .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ZERO,
            .color_blend_op        = SDL_GPU_BLENDOP_ADD,
            .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO,
            .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
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
        /* Same fp-noise concern as make_shadow_pipeline's bias comment: these
         * quads are ordinary track geometry (walls/floors), and are very
         * plausibly coincident (or a hair further) than an adjacent already-
         * drawn opaque quad at the exact same screen position -- LESS_OR_EQUAL
         * alone can lose that tie to floating-point rounding and silently
         * never draw despite being correctly queued. */
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

/* LESS_OR_EQUAL + negative bias + no depth write + CULL_NONE: tree billboard
 * sprites win against walls (similar depth) but still lose to the road
 * (genuinely closer to camera → smaller depth value despite bias). */
static SDL_GPUGraphicsPipeline *make_tree_pipeline(SceneRendererGPU *r,
                                                    SDL_GPUShader *vert,
                                                    SDL_GPUShader *fragOp,
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
        .fragment_shader = fragOp,
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
            .enable_depth_write = false,
            .compare_op         = SDL_GPU_COMPAREOP_LESS_OR_EQUAL
        },
        .rasterizer_state = {
            .fill_mode                  = fillMode,
            .cull_mode                  = SDL_GPU_CULLMODE_NONE,
            .front_face                 = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE,
            .depth_bias_constant_factor = -200.0f,
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

static SDL_GPUGraphicsPipeline *make_car_shadow_pipeline(SceneRendererGPU *r,
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
        .vertex_shader   = vert,
        .fragment_shader = frag,
        .primitive_type  = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .vertex_input_state = {
            .vertex_attributes          = attrs,
            .num_vertex_attributes      = 3,
            .vertex_buffer_descriptions = &binding,
            .num_vertex_buffers         = 1
        },
        /* CULL_NONE: shadow quad may be seen at a grazing angle */
        .rasterizer_state = {
            .fill_mode  = fillMode,
            .front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE,
            .cull_mode  = SDL_GPU_CULLMODE_NONE,
            .enable_depth_bias          = true,
            .depth_bias_constant_factor = -200.0f,
            .depth_bias_slope_factor    = -5.0f,
        },
        .target_info = {
            .color_target_descriptions = &ct,
            .num_color_targets         = 1,
            .has_depth_stencil_target  = true,
            .depth_stencil_format      = SDL_GPU_TEXTUREFORMAT_D32_FLOAT
        },
        .multisample_state = { .sample_count = sc },
        /* Drawn after car body: car-written depth blocks shadow on car surface.
         * No depth write: preserve car body depth for subsequent passes. */
        .depth_stencil_state = {
            .enable_depth_test  = true,
            .enable_depth_write = false,
            .compare_op         = SDL_GPU_COMPAREOP_LESS_OR_EQUAL
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

static void rebuild_scene_pipelines(SceneRendererGPU *r, SDL_GPUSampleCount sc, SDL_GPUFillMode fm);

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
        r->shadowTex = scene_render_gpu_upload_rgba(device, shadowRgba, 4, 4, true);
    }

    {
        /* Fully opaque (unlike shadowTex's baked-in 50%) so the tint alpha
         * passed to scene_render_gpu_screen_quad_darken() is the actual
         * final alpha, not multiplied against a second baked-in value. */
        uint8 darkenRgba[4 * 4 * 4];
        for (int i = 0; i < 16; i++) {
            darkenRgba[i*4+0] = 0; darkenRgba[i*4+1] = 0;
            darkenRgba[i*4+2] = 0; darkenRgba[i*4+3] = 255;
        }
        r->darkenTex = scene_render_gpu_upload_rgba(device, darkenRgba, 4, 4, true);
    }

    /* Solid gray textures approximating each shade_palette darkening level
     * (see g_sceneGpuShadeFactor / trackDarkenPipeline comments). Sampled as
     * the multiply-blend pass's source colour for TRANSPARENT non-textured
     * track quads. */
    for (int i = 0; i < SCENE_GPU_SHADE_LEVELS; i++) {
        uint8 shadeRgba[4 * 4 * 4];
        uint8 v = (uint8)(g_sceneGpuShadeFactor[i] * 255.0f);
        for (int p = 0; p < 16; p++) {
            shadeRgba[p*4+0] = v; shadeRgba[p*4+1] = v;
            shadeRgba[p*4+2] = v; shadeRgba[p*4+3] = 255;
        }
        r->shadeLevelTex[i] = scene_render_gpu_upload_rgba(device, shadeRgba, 4, 4, true);
    }

    /* ---- Scene shaders + pipelines ---- */
    rebuild_scene_pipelines(r, SDL_GPU_SAMPLECOUNT_1, SDL_GPU_FILLMODE_FILL);
    if (!r->opaquePipeline || !r->blendPipeline || !r->buildingPipeline || !r->bfBuildingPipeline || !r->signPipeline || !r->signBkPipeline || !r->signDepthPipeline || !r->signBkDepthPipeline || !r->treePipeline || !r->wallPipeline || !r->bfWallPipeline || !r->shadowPipeline || !r->trackDarkenPipeline || !r->trackDarkenBorderPipeline || !r->skyPipeline) goto fail;

    /* ---- Car shaders (game_car_* add fog+gamma; menu_mesh_* kept for menu) ---- */
    SDL_GPUShader *cv = load_shader(device, SDL_GPU_SHADERSTAGE_VERTEX,
        game_car_vertex_spirv, game_car_vertex_spirv_size,
        game_car_vertex_msl,   game_car_vertex_msl_size, 0, 1);
    SDL_GPUShader *cf = load_shader(device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        game_car_pixel_spirv,  game_car_pixel_spirv_size,
        game_car_pixel_msl,    game_car_pixel_msl_size,  1, 1);
    if (!cv || !cf) goto fail;

    r->carPipeline = make_car_pipeline(r, cv, cf, SDL_GPU_SAMPLECOUNT_1, SDL_GPU_FILLMODE_FILL);
    r->carShadowPipeline = make_car_shadow_pipeline(r, cv, cf, SDL_GPU_SAMPLECOUNT_1, SDL_GPU_FILLMODE_FILL);
    SDL_ReleaseGPUShader(device, cv);
    SDL_ReleaseGPUShader(device, cf);
    if (!r->carPipeline || !r->carShadowPipeline) goto fail;

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
    r->hudVertBuf = scene_render_gpu_upload_buffer(device, SDL_GPU_BUFFERUSAGE_VERTEX,
                                      hudVerts, sizeof(hudVerts));
    if (!r->hudVertBuf) goto fail;

    /* ---- CPU-side vertex + draw-cmd arrays ---- */
    r->vertices  = malloc(SCENE_GPU_MAX_VERTICES * sizeof(SceneGPUVertex));
    r->drawCmds  = malloc(SCENE_GPU_MAX_DRAW_CMDS * sizeof(SceneGPUDrawCmd));
    r->repackVertices = malloc(SCENE_GPU_MAX_VERTICES * sizeof(SceneGPUVertex));
    r->repackDrawCmds = malloc(SCENE_GPU_MAX_DRAW_CMDS * sizeof(SceneGPUDrawCmd));
    if (!r->vertices || !r->drawCmds || !r->repackVertices || !r->repackDrawCmds) goto fail;

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
    if (r->darkenTex)     SDL_ReleaseGPUTexture(r->device, r->darkenTex);
    for (int i = 0; i < SCENE_GPU_SHADE_LEVELS; i++) {
        if (r->shadeLevelTex[i]) SDL_ReleaseGPUTexture(r->device, r->shadeLevelTex[i]);
    }
    if (r->offscreenTex)  SDL_ReleaseGPUTexture(r->device, r->offscreenTex);
    for (int i = 0; i < SCENE_GPU_MAX_SECONDARY_VIEWS; i++) {
        if (r->secondaryColorTex[i]) SDL_ReleaseGPUTexture(r->device, r->secondaryColorTex[i]);
        if (r->secondaryDepthTex[i]) SDL_ReleaseGPUTexture(r->device, r->secondaryDepthTex[i]);
        if (r->secondaryMsaaColorTex[i]) SDL_ReleaseGPUTexture(r->device, r->secondaryMsaaColorTex[i]);
        if (r->secondaryMsaaDepthTex[i]) SDL_ReleaseGPUTexture(r->device, r->secondaryMsaaDepthTex[i]);
    }
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
    free(r->repackVertices);
    free(r->repackDrawCmds);
    if (r->opaquePipeline)   SDL_ReleaseGPUGraphicsPipeline(r->device, r->opaquePipeline);
    if (r->blendPipeline)    SDL_ReleaseGPUGraphicsPipeline(r->device, r->blendPipeline);
    if (r->buildingPipeline)   SDL_ReleaseGPUGraphicsPipeline(r->device, r->buildingPipeline);
    if (r->bfBuildingPipeline) SDL_ReleaseGPUGraphicsPipeline(r->device, r->bfBuildingPipeline);
    if (r->signPipeline)         SDL_ReleaseGPUGraphicsPipeline(r->device, r->signPipeline);
    if (r->signBkPipeline)       SDL_ReleaseGPUGraphicsPipeline(r->device, r->signBkPipeline);
    if (r->signDepthPipeline)    SDL_ReleaseGPUGraphicsPipeline(r->device, r->signDepthPipeline);
    if (r->signBkDepthPipeline)  SDL_ReleaseGPUGraphicsPipeline(r->device, r->signBkDepthPipeline);
    if (r->treePipeline)         SDL_ReleaseGPUGraphicsPipeline(r->device, r->treePipeline);
    if (r->wallPipeline)     SDL_ReleaseGPUGraphicsPipeline(r->device, r->wallPipeline);
    if (r->bfWallPipeline)   SDL_ReleaseGPUGraphicsPipeline(r->device, r->bfWallPipeline);
    if (r->shadowPipeline)      SDL_ReleaseGPUGraphicsPipeline(r->device, r->shadowPipeline);
    if (r->trackDarkenPipeline) SDL_ReleaseGPUGraphicsPipeline(r->device, r->trackDarkenPipeline);
    if (r->trackDarkenBorderPipeline) SDL_ReleaseGPUGraphicsPipeline(r->device, r->trackDarkenBorderPipeline);
    if (r->carPipeline)         SDL_ReleaseGPUGraphicsPipeline(r->device, r->carPipeline);
    if (r->carShadowPipeline)   SDL_ReleaseGPUGraphicsPipeline(r->device, r->carShadowPipeline);
    if (r->skyPipeline)         SDL_ReleaseGPUGraphicsPipeline(r->device, r->skyPipeline);
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
static struct { bool active; int surfIdx; int surfaceFlags; char path[8]; float vZ;
                float v0x, v0y, v0z, v1x, v1y, v1z, v2x, v2y, v2z, v3x, v3y, v3z;
                bool uvValid; char uvType[16]; int uvTexId; bool flipV, efV, efH, p03L, row01Bot;
                float cu0, cv0, cv2, bcross; bool isFloorLike; bool wzRowBot, wzSpans; bool useRowDetect;
                int backwardsVal; float adjSum01, adjSum23; bool spansCC;
                float camX, camY, camZ;
                bool backTexApplied; float backDot; int backOrigIdx, backNewIdx;
                bool uWindingFlip; float uWindingArea;
                bool vSwap; float vNrmY;
                bool splitAValid; float splitArea012, splitArea023;
                float cv1, cv3;
                float onx[4], ony[4]; bool ovalid[4]; } s_clickHit;
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
    bool perZ = r->useParticleNdcZv;
    for (int i = 0; i < 6; i++) {
        int k = idx[i];
        float z = perZ ? r->particleNdcZv[k] : r->particleNdcZ;
        v[i] = (SceneGPUParticleVertex){ndcX[k], ndcY[k], z, cr, cg, cb, ca};
    }
    r->particleVertCount += 6;
    r->useParticleNdcZv = false;
}

void scene_render_gpu_set_particle_ndcz(SceneRendererGPU *r, float ndcZ)
{
    if (r) r->particleNdcZ = ndcZ;
}

void scene_render_gpu_set_particle_ndcz_pervertex(SceneRendererGPU *r, const float ndcZ[4])
{
    if (!r) return;
    r->particleNdcZv[0] = ndcZ[0];
    r->particleNdcZv[1] = ndcZ[1];
    r->particleNdcZv[2] = ndcZ[2];
    r->particleNdcZv[3] = ndcZ[3];
    r->useParticleNdcZv = true;
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
    bool perZ = r->useParticleNdcZv;
    SceneGPUTexParticleVertex *v = r->texParticleVerts + r->texParticleVertCount;
    for (int i = 0; i < 6; i++) {
        int k = idx[i];
        float z = perZ ? r->particleNdcZv[k] : r->particleNdcZ;
        v[i] = (SceneGPUTexParticleVertex){ndcX[k], ndcY[k], z,
                                           uvU[k], uvV[k],
                                           cr, cg, cb, ca};
    }
    r->texParticleVertCount              += 6;
    r->texParticleRanges[ri].count       += 6;
    r->useParticleNdcZv = false;
    return true;
}

/* Full-screen translucent black quad for the in-race pause-menu darken
 * effect. Uses the textured-particle path
 * (not scene_render_gpu_screen_quad_flat) deliberately: in 2-player mode,
 * flat-particle quads draw as a whole batch BEFORE textured-particle quads
 * in the same final render pass, regardless of queue order -- and each
 * player's composited view (game_render_flush_player_view) is itself an
 * OPAQUE textured-particle quad. A flat darken quad queued after both
 * composites would still get drawn first and be completely painted over by
 * them. Routing through the textured path instead means this quad's
 * position in texParticleRanges (and therefore its draw order) matches
 * when it was actually queued, so calling this after both players' views
 * have been composited correctly draws it on top of both halves. */
bool scene_render_gpu_screen_quad_darken(SceneRendererGPU *r, float alpha)
{
    if (!r || !r->darkenTex) return false;
    float ndcX[4] = { 1.0f, -1.0f, -1.0f, 1.0f };
    float ndcY[4] = { 1.0f,  1.0f, -1.0f, -1.0f };
    return scene_render_gpu_screen_quad_textured(r, ndcX, ndcY, r->darkenTex,
                                                  1.0f, 1.0f, 1.0f, alpha);
}

/* Call before a secondary view's own draw_road() runs (i.e. before any of
 * its scene content -- including real smoke/explosion particles, which use
 * this same texParticle path -- gets queued). Snapshots the current
 * texParticle counts so the next scene_render_gpu_flush_secondary_view()
 * call can correctly discard only what THIS view's own draw_road()
 * contributed, while still preserving an earlier secondary view's own
 * composite quad (added by its caller after ITS flush already ran). */
void scene_render_gpu_secondary_view_will_queue(SceneRendererGPU *r)
{
    if (!r) return;
    r->secPreTexParticleVertCount  = r->texParticleVertCount;
    r->secPreTexParticleRangeCount = r->texParticleRangeCount;
}

/* Render whatever has been queued (via the normal camera/projection/draw_car/
 * quad_world API, called with a secondary camera already set) into a small
 * dedicated offscreen target instead of the main swapchain, then reset the
 * shared per-frame vertex/draw-command state so the NEXT queued scene (the
 * main view, or another secondary view) starts clean.
 *
 * This is a trimmed-down copy of scene_render_gpu_end_frame's Pass 1: no
 * MSAA, no letterbox present blit, no HUD, and signs always use the simple
 * bias-based pipeline (no depth-copy split) -- acceptable simplifications
 * for a small, lower-detail secondary view. Everything else (opaque/wall/
 * building/tree/blend geometry, cars, car shadows, road shadows, sky) is
 * drawn exactly as the main pass would. */

/* Computes the fog-blended sky/ground clear colour (skyClear, the render
 * target's clear colour) and the sky fragment-uniform colour (skyFColor,
 * passed to the sky pipeline's fogColor uniform) for this frame. Identical
 * logic shared between scene_render_gpu_flush_secondary_view() and
 * scene_render_gpu_end_frame() -- kept as one function specifically so a
 * future fog fix can't be applied to one call site and missed in the other.
 * Both the sky-side and ground-side clear colours blend toward r->fogColor
 * by the same skyFog factor so distant background fades correctly with fog
 * density. */
static void compute_sky_colors(SceneRendererGPU *r, SDL_FColor *outSkyClear, SDL_FColor *outSkyFColor)
{
    float skyFog = 1.0f - expf(-r->fogDensity * 100000.0f);
    if (skyFog < 0.0f) skyFog = 0.0f;
    if (skyFog > 1.0f) skyFog = 1.0f;
    SDL_FColor skyFColor = {
        r->skyR + (r->fogColor[0] - r->skyR) * skyFog,
        r->skyG + (r->fogColor[1] - r->skyG) * skyFog,
        r->skyB + (r->fogColor[2] - r->skyB) * skyFog,
        1.0f
    };
    SDL_FColor skyClear = skyFColor;
    if (r->groundColorIdx >= 0 && r->skyAnyGround) {
        const tColor *gc = &palette[r->groundColorIdx];
        float groundR = gc->byR / 63.0f;
        float groundG = gc->byG / 63.0f;
        float groundB = gc->byB / 63.0f;
        skyClear.r = groundR + (r->fogColor[0] - groundR) * skyFog;
        skyClear.g = groundG + (r->fogColor[1] - groundG) * skyFog;
        skyClear.b = groundB + (r->fogColor[2] - groundB) * skyFog;
    }
    *outSkyClear  = skyClear;
    *outSkyFColor = skyFColor;
}

/* Fans the S-H clipped sky polygon (r->skyPoly, 3-5 verts, set by
 * game_render_draw_sky()) into triangles and uploads them to r->skyVertBuf.
 * Returns false (with *outVertCount left at 0) if there's nothing to draw or
 * the upload failed -- caller should then skip the sky draw for this pass.
 * Identical logic shared between scene_render_gpu_flush_secondary_view() and
 * scene_render_gpu_end_frame(). */
static bool upload_sky_polygon(SceneRendererGPU *r, int *outVertCount)
{
    *outVertCount = 0;
    if (!(r->groundColorIdx >= 0 && r->skyPolyN >= 3 && r->skyPipeline && r->skyVertBuf))
        return false;

    int n_tris = r->skyPolyN - 2;
    SceneGPUVertex gv[9];
    int gi = 0;
    for (int t = 0; t < n_tris; t++) {
        gv[gi++] = (SceneGPUVertex){r->skyPoly[0][0],   r->skyPoly[0][1],   0.f, 0.5f, 0.5f};
        gv[gi++] = (SceneGPUVertex){r->skyPoly[t+1][0], r->skyPoly[t+1][1], 0.f, 0.5f, 0.5f};
        gv[gi++] = (SceneGPUVertex){r->skyPoly[t+2][0], r->skyPoly[t+2][1], 0.f, 0.5f, 0.5f};
    }
    /* cycle=true: this transfer buffer is reused for every secondary view
     * flushed within the same frame (2-player: once per player, both
     * sharing one not-yet-submitted command buffer). cycle=false let a
     * later view's write clobber an earlier view's source bytes before
     * the GPU had actually consumed the earlier copy, so the earlier
     * view's sky quad rendered with the later view's polygon data.
     * Matches the scene vertex upload, which already cycles correctly. */
    SceneGPUVertex *gvMapped = SDL_MapGPUTransferBuffer(r->device, r->skyVertXfer, true);
    if (!gvMapped) return false;
    memcpy(gvMapped, gv, (size_t)gi * sizeof(SceneGPUVertex));
    SDL_UnmapGPUTransferBuffer(r->device, r->skyVertXfer);
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(r->cmdBuf);
    SDL_GPUTransferBufferLocation gsrc = {.transfer_buffer = r->skyVertXfer};
    SDL_GPUBufferRegion gdst = {.buffer = r->skyVertBuf,
                                .size = (Uint32)(gi * (int)sizeof(SceneGPUVertex))};
    SDL_UploadToGPUBuffer(cp, &gsrc, &gdst, false);
    SDL_EndGPUCopyPass(cp);
    *outVertCount = gi;
    return true;
}

/* Draws every queued command of the given kind from r->drawCmds. Identical
 * logic previously duplicated as the SEC_DRAW_CMD macro (in
 * scene_render_gpu_flush_secondary_view) and the DRAW_CMD macro (in
 * scene_render_gpu_end_frame) -- both bodies were byte-identical, just
 * renamed to avoid a redefinition conflict since both were #define'd at
 * file scope. tsb is reused/mutated by the caller between calls (its
 * .sampler/.texture fields get overwritten here), matching how both
 * original macros used it. */
/* BLEND relies on its queued order for correct back-to-front alpha
 * compositing; SHADOW is drawn through its own dedicated loop already (not
 * draw_cmd_kind), included here defensively in case that ever changes. Every
 * other kind is opaque-with-proper-depth-test, so any draw order inside the
 * kind produces the same correct result -- safe to reorder by texture. */
static bool draw_kind_is_order_sensitive(SceneGPUDrawKind kind)
{
    return kind == SCENE_GPU_DRAW_BLEND || kind == SCENE_GPU_DRAW_SHADOW;
}

/* qsort has no user-data parameter in portable C; single-threaded renderer,
 * so a scratch static pointer for the duration of one sort call is safe. */
static const SceneRendererGPU *s_drawOrderSortCtx;

static int compare_draw_order(const void *pa, const void *pb)
{
    int ia = *(const int *)pa, ib = *(const int *)pb;
    const SceneGPUDrawCmd *a = &s_drawOrderSortCtx->drawCmds[ia];
    const SceneGPUDrawCmd *b = &s_drawOrderSortCtx->drawCmds[ib];
    if (a->kind != b->kind) return (int)a->kind - (int)b->kind;
    if (!draw_kind_is_order_sensitive(a->kind)) {
        uintptr_t ta = (uintptr_t)a->texture, tb = (uintptr_t)b->texture;
        if (ta != tb) return (ta < tb) ? -1 : 1;
        /* draw_cmd_kind also rebinds when forceNearest changes (different
         * sampler), so group by it too -- otherwise same-texture commands
         * with mixed forceNearest can still alternate instead of grouping,
         * causing avoidable rebinds within a texture run. */
        if (a->forceNearest != b->forceNearest) return a->forceNearest ? 1 : -1;
    }
    return ia - ib; /* stable fallback: preserve original queue order */
}

/* Fills and sorts a draw-command index array (grouped by kind, then by
 * texture within reorderable kinds) so that draw_cmd_kind's per-kind scan
 * encounters same-texture commands adjacently far more often -- track tiles
 * vary constantly chunk-to-chunk in queue order, which is what limited the
 * existing same-texture/mvp queue-time batching in
 * scene_render_gpu_quad_world_legacy to only ever merging consecutively-
 * queued polygons. Sorts metadata only (lightweight indices into
 * r->drawCmds), never touches the underlying vertex data, so this is safe
 * regardless of how large r->drawCmds is. order must have room for at least
 * r->drawCmdCount ints. */
static void build_draw_order(SceneRendererGPU *r, int *order)
{
    for (int i = 0; i < r->drawCmdCount; i++) order[i] = i;
    s_drawOrderSortCtx = r;
    qsort(order, (size_t)r->drawCmdCount, sizeof(int), compare_draw_order);
}

/* Physically reorders r->vertices into the order given by `order` (from
 * build_draw_order), so that runs of same-(kind,texture,forceNearest)
 * commands -- already grouped in `order` for reduced sampler rebinds -- also
 * become vertex-CONTIGUOUS, and merges them into one draw command each while
 * repacking. Sorting alone only reduced rebinds (draw_cmd_kind still gave
 * every command its own SDL_DrawGPUPrimitives call, since two commands that
 * happen to share a texture are essentially never vertex-adjacent already:
 * the render queue is Z-sorted, not texture-grouped, so anything not merged
 * at queue time by scene_render_gpu_quad_world_legacy stays scattered
 * throughout the buffer). This closes that gap by actually moving the data.
 *
 * MUST run before the vertex buffer is uploaded to the GPU this frame (the
 * upload just copies r->vertices as-is) -- callers call this immediately
 * after build_draw_order, both before the upload step.
 *
 * Order-sensitive kinds (BLEND; SHADOW never reaches here) are copied
 * through unmerged, in their original relative order (build_draw_order's own
 * stable fallback for those kinds already preserves it) -- reordering their
 * vertex data doesn't apply since they're never merged.
 *
 * Overwrites r->vertices/r->drawCmds/r->vertexCount/r->drawCmdCount in
 * place via the repackVertices/repackDrawCmds scratch buffers. Resets
 * `order` to the identity afterward, since r->drawCmds is now already in
 * final sorted+merged order and needs no further indirection. */
static void repack_draw_order(SceneRendererGPU *r, int *order)
{
    if (r->drawCmdCount <= 0) return;

    int outVert = 0, outCmd = 0;
    for (int oi = 0; oi < r->drawCmdCount; oi++) {
        SceneGPUDrawCmd *cmd = &r->drawCmds[order[oi]];

        memcpy(&r->repackVertices[outVert], &r->vertices[cmd->vertexStart],
               (size_t)cmd->vertexCount * sizeof(SceneGPUVertex));

        bool mergeable = !draw_kind_is_order_sensitive(cmd->kind);
        bool sameBatch = mergeable && outCmd > 0
                       && r->repackDrawCmds[outCmd-1].kind         == cmd->kind
                       && r->repackDrawCmds[outCmd-1].texture      == cmd->texture
                       && r->repackDrawCmds[outCmd-1].forceNearest == cmd->forceNearest;
        if (sameBatch) {
            r->repackDrawCmds[outCmd-1].vertexCount += cmd->vertexCount;
        } else {
            r->repackDrawCmds[outCmd] = (SceneGPUDrawCmd){
                .texture      = cmd->texture,
                .vertexStart  = outVert,
                .vertexCount  = cmd->vertexCount,
                .kind         = cmd->kind,
                .forceNearest = cmd->forceNearest,
            };
            outCmd++;
        }
        outVert += cmd->vertexCount;
    }

    memcpy(r->vertices,  r->repackVertices,  (size_t)outVert * sizeof(SceneGPUVertex));
    memcpy(r->drawCmds,  r->repackDrawCmds,  (size_t)outCmd  * sizeof(SceneGPUDrawCmd));
    r->vertexCount  = outVert;
    r->drawCmdCount = outCmd;

    for (int i = 0; i < outCmd; i++) order[i] = i;
}

/* tsb2: optional second fragment sampler slot (e.g. the sign depth-copy
 * pass's depth-copy texture, bound at slot 1) -- pass NULL for the common
 * single-texture case. When non-NULL, tsb2's contents are assumed constant
 * across the whole call (the caller sets it once beforehand) since only
 * slot 0 (tsb) varies per draw command; this matches how the sign
 * depth-copy pass already used a fixed slot-1 binding. */
static void draw_cmd_kind(SceneRendererGPU *r, SDL_GPURenderPass *rp,
                          SDL_GPUTextureSamplerBinding *tsb,
                          SDL_GPUTextureSamplerBinding *tsb2,
                          const int *order,
                          SceneGPUDrawKind kind_filter)
{
    SDL_GPUTexture *lastTex = NULL;
    bool lastForceNearest = false;
    bool haveLast = false;

    /* Real draw-call merging: two commands sharing the CURRENT texture/
     * sampler binding whose vertex ranges are physically CONTIGUOUS in the
     * vertex buffer (this command starts exactly where the accumulated
     * batch ends) get issued as ONE SDL_DrawGPUPrimitives call instead of
     * two. Sorting by texture (compare_draw_order) only reduced sampler
     * rebinds before this -- it didn't by itself reduce draw-call count,
     * since every command still kept its own draw call regardless of order.
     * Contiguity is common here because scene_render_gpu_quad_world_legacy
     * already merges same-texture quads queued back-to-back into one
     * command; sorting can then place several of THOSE already-merged,
     * texture-grouped commands next to each other too when they happen to
     * have been queued in a run.
     * Never applied to BLEND (order-sensitive -- see
     * draw_kind_is_order_sensitive); SHADOW never reaches this function at
     * all (drawn via its own dedicated loop, also excluded there). */
    bool mergeable = (kind_filter != SCENE_GPU_DRAW_BLEND);
    int  mergedStart = -1, mergedCount = 0;

    for (int oi = 0; oi < r->drawCmdCount; oi++) {
        SceneGPUDrawCmd *cmd = &r->drawCmds[order[oi]];
        if (cmd->kind != kind_filter) continue;

        if (!haveLast || cmd->texture != lastTex || cmd->forceNearest != lastForceNearest) {
            if (mergedStart >= 0) {
                r->gpuDrawCallsThisFrame++;
                SDL_DrawGPUPrimitives(rp, (Uint32)mergedCount, 1, (Uint32)mergedStart, 0);
                mergedStart = -1; mergedCount = 0;
            }
            tsb->texture = cmd->texture;
            tsb->sampler = cmd->forceNearest ? r->samplerNearest : r->sampler;
            if (tsb2) {
                SDL_GPUTextureSamplerBinding combined[2] = { *tsb, *tsb2 };
                SDL_BindGPUFragmentSamplers(rp, 0, combined, 2);
            } else {
                SDL_BindGPUFragmentSamplers(rp, 0, tsb, 1);
            }
            r->gpuBindsThisFrame++;
            lastTex = cmd->texture;
            lastForceNearest = cmd->forceNearest;
            haveLast = true;
        }

        if (!mergeable) {
            r->gpuDrawCallsThisFrame++;
            SDL_DrawGPUPrimitives(rp, (Uint32)cmd->vertexCount, 1, (Uint32)cmd->vertexStart, 0);
            continue;
        }

        if (mergedStart >= 0 && cmd->vertexStart == mergedStart + mergedCount) {
            mergedCount += cmd->vertexCount;
        } else {
            if (mergedStart >= 0) {
                r->gpuDrawCallsThisFrame++;
                SDL_DrawGPUPrimitives(rp, (Uint32)mergedCount, 1, (Uint32)mergedStart, 0);
            }
            mergedStart = cmd->vertexStart;
            mergedCount = cmd->vertexCount;
        }
    }

    if (mergedStart >= 0) {
        r->gpuDrawCallsThisFrame++;
        SDL_DrawGPUPrimitives(rp, (Uint32)mergedCount, 1, (Uint32)mergedStart, 0);
    }
}

static SDL_GPUGraphicsPipeline *track_darken_pipeline_for_draw(SceneRendererGPU *r)
{
    if (!r) return NULL;
    if (r->emulateSoftwareTrackDarkenBorder && r->trackDarkenBorderPipeline)
        return r->trackDarkenBorderPipeline;
    return r->trackDarkenPipeline;
}

SDL_GPUTexture *scene_render_gpu_flush_secondary_view(SceneRendererGPU *r, int slot, int texW, int texH)
{
      if (!r || !r->cmdBuf) return NULL;
    if (slot < 0 || slot >= SCENE_GPU_MAX_SECONDARY_VIEWS) return NULL;
    if (texW < 1) texW = 1;
    if (texH < 1) texH = 1;

    /* texParticleVertCount/RangeCount are shared with the composite quad a
     * caller queues right after THIS call returns (game_render_flush_player_
     * view / game_render_composite_mirror_pass) -- and, when there are
     * multiple secondary views in one frame (2-player split screen), an
     * EARLIER view's caller may have already queued its own composite quad
     * into these same arrays. This flush must only discard whatever ITS OWN
     * scene (this slot's draw_road call) contributed -- e.g. real smoke/
     * explosion particles, which use this same texParticle path and aren't
     * drawn by the trimmed pass below -- not an earlier view's still-pending
     * composite. The snapshot MUST be taken before draw_road() ran (via
     * scene_render_gpu_secondary_view_will_queue(), called by the game_render
     * begin_2p_pass/begin_mirror_pass callers) -- taking it here, now, would
     * be too late: draw_road() has already queued this view's own smoke by
     * the time this function runs. */
    int savedTexParticleVertCount  = r->secPreTexParticleVertCount;
    int savedTexParticleRangeCount = r->secPreTexParticleRangeCount;

    ensure_secondary_textures(r, slot, texW, texH);
    if (!r->secondaryColorTex[slot] || !r->secondaryDepthTex[slot]) {
        r->vertexCount = 0; r->drawCmdCount = 0;
        r->carDrawCount = 0; r->carShadowDrawCount = 0;
        r->particleVertCount = 0;
        r->texParticleVertCount = savedTexParticleVertCount;
        r->texParticleRangeCount = savedTexParticleRangeCount;
        r->groundColorIdx = -1;
        return NULL;
    }

    /* Sort + physically repack BEFORE uploading -- see repack_draw_order's
     * own comment for why sorting alone (build_draw_order) doesn't reduce
     * draw-call count by itself. */
    int drawOrder[SCENE_GPU_MAX_DRAW_CMDS];
    build_draw_order(r, drawOrder);
    repack_draw_order(r, drawOrder);

    /* ---- Vertex upload (same pattern as the main scene flush) ---- */
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

    /* ---- Sky polygon vertex upload ---- */
    int skyVertCount = 0;
    bool drawSky = upload_sky_polygon(r, &skyVertCount);

    /* ---- Real smoke/spray/firework particles: draw THIS view's own delta ----
     * game_render_quad_screen queues real smoke (DisplayCarSmoke) and spray/
     * firework particles (func2.c draw_smoke) into the same texParticle arrays
     * used for the composite quad (see the comment above savedTexParticleVertCount).
     * They're NOT drawn by the trimmed SEC_DRAW_CMD pass above, and were
     * previously never drawn at all in a secondary view -- they'd survive the
     * discard/restore below and get deferred all the way to the shared main
     * end_frame pass, which targets the FULL screen, so their NDC coordinates
     * (computed against THIS view's own half-height winw/winh at queue time)
     * ended up wildly mis-scaled/positioned there. Fix: draw this view's own
     * newly-queued ranges (from the saved snapshot to the current count) here,
     * inside this view's own render pass -- same viewport (texW/texH) the NDC
     * coordinates were actually computed against, and the same depth buffer
     * the rest of this view's 3D geometry uses, so smoke correctly occludes
     * behind scene geometry like it does in single-player. Do NOT touch
     * anything before the snapshot (an earlier secondary view's still-pending
     * composite quad). */
    int secOwnTexRangeStart  = savedTexParticleRangeCount;
    bool drawSecTexParticles = (r->texParticleRangeCount > secOwnTexRangeStart
                                && r->texParticlePipeline && r->texParticleVerts
                                && r->texParticleVertBuf);
    if (drawSecTexParticles) {
        SceneGPUTexParticleVertex *tpm = SDL_MapGPUTransferBuffer(r->device, r->texParticleVertXfer, true);
        if (tpm) {
            memcpy(tpm, r->texParticleVerts,
                   (size_t)r->texParticleVertCount * sizeof(SceneGPUTexParticleVertex));
            SDL_UnmapGPUTransferBuffer(r->device, r->texParticleVertXfer);
            SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(r->cmdBuf);
            SDL_GPUTransferBufferLocation tpsrc = {.transfer_buffer = r->texParticleVertXfer};
            SDL_GPUBufferRegion tpdst = {.buffer = r->texParticleVertBuf,
                                         .size = (Uint32)(r->texParticleVertCount * (int)sizeof(SceneGPUTexParticleVertex))};
            SDL_UploadToGPUBuffer(cp, &tpsrc, &tpdst, true);
            SDL_EndGPUCopyPass(cp);
        } else {
            drawSecTexParticles = false;
        }
    }

    /* Flat particles (car name-tag triangle markers, car.c's own legacy SW
     * shadow screen-quad, and possibly other sources not yet identified --
     * game_render_quad_screen's TEXTURE_HANDLE_INVALID -> scene_render_gpu_
     * screen_quad_flat path has no per-source tag to filter on) are
     * intentionally NOT drawn in this secondary-view pass. Two attempts so
     * far both produced a stray black bar artifact:
     *   1. Drawing all flat particles unconditionally.
     *   2. Filtering to opaque-only (alpha>=0.99), assuming the bar was
     *      car.c's transparent (alpha=0.5) legacy shadow quad -- it wasn't
     *      only that; something else opaque (visible only for certain cars,
     *      e.g. present for "Snake" but not "Player 2" in one test) also
     *      produces a full-width bar, still un-identified.
     * Needs a live-debugger investigation (dump each quad's screen-space
     * source/coordinates before assuming a fix) rather than another blind
     * attempt. Discarding both for now (unlike texParticles above, flat
     * particles have no known-safe path drawn already, so there's no
     * working precedent to extend). */
    r->particleVertCount = 0;

    /* ---- Clear colour (same logic as the main pass) ---- */
    SDL_FColor skyClear, skyFColor;
    compute_sky_colors(r, &skyClear, &skyFColor);

    /* MSAA: render into secondaryMsaaColorTex/DepthTex (sample count matching
     * the shared pipelines, which are always built at r->msaaSampleCount) and
     * resolve into secondaryColorTex[slot] -- the texture everything else
     * (compositing) already expects. Without this, the shared pipelines
     * (sample_count = msaaSampleCount) would be bound into a render pass
     * targeting single-sample textures whenever MSAA is on -- an invalid
     * pipeline/render-pass sample-count combination that can hang the GPU
     * driver. See the secondaryMsaaColorTex struct comment. */
    bool useSecMSAA = r->msaaSampleCount > SDL_GPU_SAMPLECOUNT_1
                      && r->secondaryMsaaColorTex[slot] && r->secondaryMsaaDepthTex[slot];
    SDL_GPUColorTargetInfo colorInfo;
    if (useSecMSAA) {
        colorInfo = (SDL_GPUColorTargetInfo){
            .texture         = r->secondaryMsaaColorTex[slot],
            .load_op         = SDL_GPU_LOADOP_CLEAR,
            .store_op        = SDL_GPU_STOREOP_RESOLVE,
            .resolve_texture = r->secondaryColorTex[slot],
            .clear_color     = skyClear
        };
    } else {
        colorInfo = (SDL_GPUColorTargetInfo){
            .texture     = r->secondaryColorTex[slot],
            .load_op     = SDL_GPU_LOADOP_CLEAR,
            .store_op    = SDL_GPU_STOREOP_STORE,
            .clear_color = skyClear
        };
    }
    SDL_GPUDepthStencilTargetInfo depthInfo = {
        .texture     = useSecMSAA ? r->secondaryMsaaDepthTex[slot] : r->secondaryDepthTex[slot],
        .load_op     = SDL_GPU_LOADOP_CLEAR,
        .store_op    = SDL_GPU_STOREOP_DONT_CARE,
        .clear_depth = 1.0f
    };
    SDL_GPURenderPass *rp = SDL_BeginGPURenderPass(r->cmdBuf, &colorInfo, 1, &depthInfo);
    SDL_GPUViewport vp = { .x = 0, .y = 0, .w = (float)texW, .h = (float)texH,
                           .min_depth = 0.0f, .max_depth = 1.0f };
    SDL_SetGPUViewport(rp, &vp);

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
    /* drawOrder was already sorted + physically repacked before the vertex
     * upload above (see repack_draw_order) -- r->drawCmds is now itself in
     * final sorted+merged order, and drawOrder was reset to the identity to
     * match. */

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

    /* World-space quads (SceneGPUDrawCmd) share one VP (no per-object model
     * matrix) for the whole view -- push it once instead of once per quad.
     * Cars/car-shadows below push their own per-object MVP and are unaffected. */
    float viewVP[16];
    scene_render_gpu_build_vp(r, viewVP);
    SDL_PushGPUVertexUniformData(r->cmdBuf, 0, viewVP, sizeof(viewVP));

    if (r->opaquePipeline)     { SDL_BindGPUGraphicsPipeline(rp, r->opaquePipeline);     draw_cmd_kind(r, rp, &tsb, NULL, drawOrder, SCENE_GPU_DRAW_OPAQUE); }
    if (r->wallPipeline)       { SDL_BindGPUGraphicsPipeline(rp, r->wallPipeline);       draw_cmd_kind(r, rp, &tsb, NULL, drawOrder, SCENE_GPU_DRAW_WALL); }
    if (r->bfWallPipeline)     { SDL_BindGPUGraphicsPipeline(rp, r->bfWallPipeline);     draw_cmd_kind(r, rp, &tsb, NULL, drawOrder, SCENE_GPU_DRAW_BF_WALL); }
    if (r->buildingPipeline)   { SDL_BindGPUGraphicsPipeline(rp, r->buildingPipeline);   draw_cmd_kind(r, rp, &tsb, NULL, drawOrder, SCENE_GPU_DRAW_BUILDING); }
    if (r->bfBuildingPipeline) { SDL_BindGPUGraphicsPipeline(rp, r->bfBuildingPipeline); draw_cmd_kind(r, rp, &tsb, NULL, drawOrder, SCENE_GPU_DRAW_BF_BUILDING); }
    /* Signs: always the simple bias-based pipeline here (no depth-copy split) --
     * an acceptable simplification for this secondary, lower-detail view. */
    if (r->signPipeline)   { SDL_BindGPUGraphicsPipeline(rp, r->signPipeline);   draw_cmd_kind(r, rp, &tsb, NULL, drawOrder, SCENE_GPU_DRAW_SIGN); }
    if (r->signBkPipeline) { SDL_BindGPUGraphicsPipeline(rp, r->signBkPipeline); draw_cmd_kind(r, rp, &tsb, NULL, drawOrder, SCENE_GPU_DRAW_SIGN_BK); }
    if (r->treePipeline)   { SDL_BindGPUGraphicsPipeline(rp, r->treePipeline);   draw_cmd_kind(r, rp, &tsb, NULL, drawOrder, SCENE_GPU_DRAW_TREE); }
    /* Multiply-blend darken pass for TRANSPARENT non-textured track quads (glass/
     * divider walls) -- must run after opaque/wall/building/sign/tree so there's
     * real colour underneath to darken; see trackDarkenPipeline comment. */
    SDL_GPUGraphicsPipeline *trackDarkenPipeline = track_darken_pipeline_for_draw(r);
    if (trackDarkenPipeline) { SDL_BindGPUGraphicsPipeline(rp, trackDarkenPipeline); draw_cmd_kind(r, rp, &tsb, NULL, drawOrder, SCENE_GPU_DRAW_TRACK_DARKEN); }
    if (r->blendPipeline)  { SDL_BindGPUGraphicsPipeline(rp, r->blendPipeline);  draw_cmd_kind(r, rp, &tsb, NULL, drawOrder, SCENE_GPU_DRAW_BLEND); }

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

    if (r->carShadowDrawCount > 0 && r->carShadowPipeline) {
        SDL_BindGPUGraphicsPipeline(rp, r->carShadowPipeline);
        SDL_PushGPUFragmentUniformData(r->cmdBuf, 0, &pfu, sizeof(pfu));
        for (int i = 0; i < r->carShadowDrawCount; i++) {
            SceneGPUCarDrawCmd *cmd = &r->carShadowDraws[i];
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

    if (r->shadowPipeline && r->drawCmdCount > 0) {
        SDL_BindGPUVertexBuffers(rp, 0, &vbb, 1);  /* restore scene vertex buffer */
        SDL_BindGPUGraphicsPipeline(rp, r->shadowPipeline);
        /* Cars/car-shadows above pushed their own per-object MVP into the same
         * uniform slot -- re-push the shared view VP before resuming
         * SceneGPUDrawCmd (no per-object matrix) draws. */
        SDL_PushGPUVertexUniformData(r->cmdBuf, 0, viewVP, sizeof(viewVP));
        SDL_GPUTextureSamplerBinding stsb = {.texture = NULL, .sampler = r->sampler};
        for (int i = 0; i < r->drawCmdCount; i++) {
            SceneGPUDrawCmd *cmd = &r->drawCmds[i];
            if (cmd->kind != SCENE_GPU_DRAW_SHADOW) continue;
            stsb.texture = cmd->texture;
            SDL_BindGPUFragmentSamplers(rp, 0, &stsb, 1);
            r->gpuBindsThisFrame++;
            r->gpuDrawCallsThisFrame++;
            SDL_DrawGPUPrimitives(rp, (Uint32)cmd->vertexCount, 1, (Uint32)cmd->vertexStart, 0);
        }
    }

    /* Real smoke/spray/firework particles queued by THIS view's own draw_road()
     * (see drawSecTexParticles above) -- draw only the new ranges, leaving an
     * earlier secondary view's still-pending composite quad range untouched. */
    if (drawSecTexParticles) {
        SDL_BindGPUGraphicsPipeline(rp, r->texParticlePipeline);
        SDL_GPUBufferBinding tpvbb = {.buffer = r->texParticleVertBuf};
        SDL_BindGPUVertexBuffers(rp, 0, &tpvbb, 1);
        for (int ri = secOwnTexRangeStart; ri < r->texParticleRangeCount; ri++) {
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

    /* Reset shared per-frame draw state so the next queued scene (main view,
     * or another secondary view) starts clean. Scene-input fields (vertex/
     * draw-cmd/car arrays) are fully consumed by the render pass above, so
     * they always reset to 0 -- flat particles too, discarded rather than
     * drawn (see the comment above where particleVertCount is zeroed).
     * texParticle fields restore to the snapshot taken at entry -- see the
     * comment above it -- so an earlier secondary view's still-pending
     * composite quad survives this flush. */
    r->vertexCount           = 0;
    r->drawCmdCount          = 0;
    r->carDrawCount           = 0;
    r->carShadowDrawCount     = 0;
    r->particleVertCount     = 0;
    r->texParticleVertCount  = savedTexParticleVertCount;
    r->texParticleRangeCount = savedTexParticleRangeCount;
    /* Sky/horizon state is set once per draw_road() call via
     * game_render_draw_sky() and otherwise never reset -- if left alone,
     * the NEXT consumer of it (another secondary view, or -- worse -- the
     * final main pass, which draws a sky quad/clear regardless of whether
     * it has any of its own geometry queued) would draw this view's own
     * horizon geometry again, in the wrong place. Disabling it here is
     * enough: the main pass's `drawSky` check short-circuits on
     * groundColorIdx < 0. */
    r->groundColorIdx = -1;

    return r->secondaryColorTex[slot];
}

void scene_render_gpu_begin_frame(SceneRendererGPU *r)
{
    if (!r) return;

    r->frameStartNs = SDL_GetTicksNS();

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
    r->gpuDrawCallsThisFrame = 0;
    r->gpuBindsThisFrame     = 0;
    r->carDrawCount      = 0;
    r->carShadowDrawCount = 0;
    r->particleVertCount    = 0;
    r->particleNdcZ         = 0.0f;
    r->useParticleNdcZv     = false;
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

    Uint64 tQueueEnd = SDL_GetTicksNS();
    r->queueNs = tQueueEnd - r->frameStartNs;

    if (s_clickWasPending) {
        if (s_clickHit.active) {
            SDL_Log("PICK: %s idx=%d sf=0x%X vZ=%.1f v0xyz=(%.1f,%.1f,%.1f) v1xyz=(%.1f,%.1f,%.1f) v2xyz=(%.1f,%.1f,%.1f) v3xyz=(%.1f,%.1f,%.1f)",
                    s_clickHit.path, s_clickHit.surfIdx,
                    (unsigned)s_clickHit.surfaceFlags, (double)s_clickHit.vZ,
                    (double)s_clickHit.v0x, (double)s_clickHit.v0y, (double)s_clickHit.v0z,
                    (double)s_clickHit.v1x, (double)s_clickHit.v1y, (double)s_clickHit.v1z,
                    (double)s_clickHit.v2x, (double)s_clickHit.v2y, (double)s_clickHit.v2z,
                    (double)s_clickHit.v3x, (double)s_clickHit.v3y, (double)s_clickHit.v3z);
            if (s_clickHit.uvValid)
                SDL_Log("PICK-UV: %s tex=%d flipV=%d efV=%d efH=%d p03L=%d row01Bot=%d cu0=%.3f cv0=%.3f cv1=%.3f cv2=%.3f cv3=%.3f bcross=%.3f isFloorLike=%d wzRowBot=%d wzSpans=%d useRowDetect=%d backwards=%d adjSum01=%.4f adjSum23=%.4f spansCC=%d cam=(%.1f,%.1f,%.1f) backTexApplied=%d backDot=%.2f backOrigIdx=%d backNewIdx=%d uWindingFlip=%d uWindingArea=%.2f vSwap=%d vNrmY=%.2f splitAValid=%d splitArea012=%.2f splitArea023=%.2f",
                        s_clickHit.uvType, s_clickHit.uvTexId,
                        (int)s_clickHit.flipV, (int)s_clickHit.efV, (int)s_clickHit.efH,
                        (int)s_clickHit.p03L, (int)s_clickHit.row01Bot,
                        (double)s_clickHit.cu0, (double)s_clickHit.cv0, (double)s_clickHit.cv1,
                        (double)s_clickHit.cv2, (double)s_clickHit.cv3,
                        (double)s_clickHit.bcross, (int)s_clickHit.isFloorLike,
                        (int)s_clickHit.wzRowBot, (int)s_clickHit.wzSpans,
                        (int)s_clickHit.useRowDetect, s_clickHit.backwardsVal,
                        (double)s_clickHit.adjSum01, (double)s_clickHit.adjSum23,
                        (int)s_clickHit.spansCC,
                        (double)s_clickHit.camX, (double)s_clickHit.camY, (double)s_clickHit.camZ,
                        (int)s_clickHit.backTexApplied, (double)s_clickHit.backDot,
                        s_clickHit.backOrigIdx, s_clickHit.backNewIdx,
                        (int)s_clickHit.uWindingFlip, (double)s_clickHit.uWindingArea,
                        (int)s_clickHit.vSwap, (double)s_clickHit.vNrmY,
                        (int)s_clickHit.splitAValid, (double)s_clickHit.splitArea012, (double)s_clickHit.splitArea023);
            if (g_bPickTexturesPNG && s_clickHit.uvValid
                    && s_clickHit.uvTexId > 0 && s_clickHit.uvTexId < SCENE_GPU_MAX_TEXTURE_SLOTS) {
                const SceneGPUTextureSlot *pickSlot = &r->texSlots[s_clickHit.uvTexId];
                if (pickSlot->in_use) {
                    bool isPair = s_clickHit.surfIdx >= 0 && s_clickHit.surfIdx < pickSlot->numTiles
                               && pickSlot->pairTextures[s_clickHit.surfIdx] != NULL;
                    save_picked_texture_png(pickSlot, s_clickHit.surfIdx, isPair);
                }
            }
        } else
            SDL_Log("PICK: no surface hit");
        debug_overlay_pick_outline_set(s_clickHit.active, s_clickHit.onx, s_clickHit.ony, s_clickHit.ovalid);
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

    /* Sort + physically repack BEFORE uploading -- the upload below just
     * copies r->vertices as-is, so reordering it afterward would be too
     * late. See repack_draw_order's own comment for why sorting alone
     * (build_draw_order) doesn't reduce draw-call count by itself. */
    int drawOrder[SCENE_GPU_MAX_DRAW_CMDS];
    build_draw_order(r, drawOrder);
    repack_draw_order(r, drawOrder);

    Uint64 tSortRepack = SDL_GetTicksNS();
    r->sortRepackNs = tSortRepack - tQueueEnd;

    r->vertexMapWaitNs = 0;
    if (r->vertexCount > 0) {
        Uint64 tMapStart = SDL_GetTicksNS();
        void *mapped = SDL_MapGPUTransferBuffer(r->device, r->vertexXfer, true);
        r->vertexMapWaitNs = SDL_GetTicksNS() - tMapStart;
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

    Uint64 tVertexUpload = SDL_GetTicksNS();
    r->vertexUploadNs = tVertexUpload - tSortRepack;

    bool drawHUD = false;
    r->hudMapWaitNs = 0;
    r->hudConvertNs = 0;
    r->hudPixelCount = 0;
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
            Uint64 tHudMapStart = SDL_GetTicksNS();
            uint8 *mapped = SDL_MapGPUTransferBuffer(r->device, r->hudXfer, true);
            r->hudMapWaitNs = SDL_GetTicksNS() - tHudMapStart;
            if (mapped) {
                Uint64 tHudConvertStart = SDL_GetTicksNS();
                indexed_to_rgba(r->hudSrcBuf, palette, mapped, hw * hh);
                r->hudConvertNs = SDL_GetTicksNS() - tHudConvertStart;

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
        r->hudPixelCount = hw * hh;
    }

    Uint64 tHudUpload = SDL_GetTicksNS();
    r->hudUploadNs = tHudUpload - tVertexUpload;

    /* ---- Sky polygon vertex upload ---- */
    int skyVertCount = 0;
    bool drawSky = upload_sky_polygon(r, &skyVertCount);

    Uint64 tSkyUpload = SDL_GetTicksNS();
    r->skyUploadNs = tSkyUpload - tHudUpload;

    /* ---- Particle vertex upload ----
     * cycle=true: not currently written more than once per frame (flat
     * particles aren't drawn inside secondary-view render passes yet), but
     * matches every other shared per-frame transfer buffer in this file so
     * this doesn't become a latent trap the moment that changes -- see the
     * cycle=true comment on the sky vertex buffer for why an un-cycled
     * shared buffer written more than once per frame corrupts an earlier,
     * not-yet-GPU-consumed write. */
    bool drawParticles = (r->particleVertCount > 0
                          && r->particlePipeline && r->particleVerts && r->particleVertBuf);
    if (drawParticles) {
        SceneGPUParticleVertex *pm = SDL_MapGPUTransferBuffer(r->device, r->particleVertXfer, true);
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

    Uint64 tParticleUpload = SDL_GetTicksNS();
    r->particleUploadNs = tParticleUpload - tSkyUpload;

    /* ---- Textured particle vertex upload ----
     * cycle=true: in 2-player mode, scene_render_gpu_flush_secondary_view()
     * already wrote this same transfer buffer once per player earlier in this
     * frame's still-unsubmitted command buffer (to draw each view's own real
     * smoke) -- see the cycle=true comment on the sky vertex buffer above for
     * why reusing the same memory without cycling corrupts an earlier,
     * not-yet-GPU-consumed write. */
    bool drawTexParticles = (r->texParticleRangeCount > 0
                             && r->texParticlePipeline && r->texParticleVerts
                             && r->texParticleVertBuf);
    if (drawTexParticles) {
        SceneGPUTexParticleVertex *tpm = SDL_MapGPUTransferBuffer(r->device, r->texParticleVertXfer, true);
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

    Uint64 tUpload = SDL_GetTicksNS();
    r->texParticleUploadNs = tUpload - tParticleUpload;
    r->uploadNs = tUpload - tSortRepack;

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
    /* Fog-blended sky/ground clear colour (sky is at infinity, so fog factor
     * → 1 as density rises); clear colour selection mirrors SW DrawHorizon
     * logic (ground visible → clear=ground, else clear=sky). */
    SDL_FColor skyClear, skyFColor;
    compute_sky_colors(r, &skyClear, &skyFColor);

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
    /* drawOrder was already sorted + physically repacked before the vertex
     * upload above (see repack_draw_order) -- r->drawCmds is now itself in
     * final sorted+merged order, and drawOrder was reset to the identity to
     * match. */

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

    /* World-space quads (SceneGPUDrawCmd) share one VP (no per-object model
     * matrix) for the whole view -- push it once instead of once per quad.
     * Cars/car-shadows below push their own per-object MVP and are unaffected. */
    float viewVP[16];
    scene_render_gpu_build_vp(r, viewVP);
    SDL_PushGPUVertexUniformData(r->cmdBuf, 0, viewVP, sizeof(viewVP));

    /* Draw order: OPAQUE → WALL → BUILDING → SIGN
     * Opaque and wall establish the depth buffer for solid track geometry.
     * Building polygons follow (LESS_OR_EQUAL + small bias for coplanar decals).
     * Signs come last so the full depth buffer is owned before they test;
     * LESS_OR_EQUAL + large negative bias lets signs coplanar with their wall
     * still pass, while signs behind opaque geometry are correctly rejected. */
    if (r->opaquePipeline) {
        SDL_BindGPUGraphicsPipeline(rp, r->opaquePipeline);
        draw_cmd_kind(r, rp, &tsb, NULL, drawOrder, SCENE_GPU_DRAW_OPAQUE);
    }
    if (r->wallPipeline) {
        SDL_BindGPUGraphicsPipeline(rp, r->wallPipeline);
        draw_cmd_kind(r, rp, &tsb, NULL, drawOrder, SCENE_GPU_DRAW_WALL);
    }
    if (r->bfWallPipeline) {
        SDL_BindGPUGraphicsPipeline(rp, r->bfWallPipeline);
        draw_cmd_kind(r, rp, &tsb, NULL, drawOrder, SCENE_GPU_DRAW_BF_WALL);
    }
    if (r->buildingPipeline) {
        SDL_BindGPUGraphicsPipeline(rp, r->buildingPipeline);
        draw_cmd_kind(r, rp, &tsb, NULL, drawOrder, SCENE_GPU_DRAW_BUILDING);
    }
    if (r->bfBuildingPipeline) {
        SDL_BindGPUGraphicsPipeline(rp, r->bfBuildingPipeline);
        draw_cmd_kind(r, rp, &tsb, NULL, drawOrder, SCENE_GPU_DRAW_BF_BUILDING);
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
        SDL_PushGPUVertexUniformData(r->cmdBuf, 0, viewVP, sizeof(viewVP));

        /* Each sign draw binds slot 0 (scene tex, varies per command) + slot 1
         * (depth copy, constant for the whole pass). Slot 1 is set once here;
         * draw_cmd_kind takes care of slot 0 (including skipping redundant
         * rebinds) and now also benefits from the same sorted draw order as
         * every other kind, instead of the raw unsorted queue order this
         * used before. */
        SDL_GPUTextureSamplerBinding signTsbSlot0 = {0};
        SDL_GPUTextureSamplerBinding signTsbSlot1 = {
            .texture = r->signDepthCopyTex,
            .sampler = r->samplerNearest
        };

        SDL_BindGPUGraphicsPipeline(rp, r->signDepthPipeline);
        draw_cmd_kind(r, rp, &signTsbSlot0, &signTsbSlot1, drawOrder, SCENE_GPU_DRAW_SIGN);
        SDL_BindGPUGraphicsPipeline(rp, r->signBkDepthPipeline);
        draw_cmd_kind(r, rp, &signTsbSlot0, &signTsbSlot1, drawOrder, SCENE_GPU_DRAW_SIGN_BK);
    } else {
        if (r->signPipeline) {
            SDL_BindGPUGraphicsPipeline(rp, r->signPipeline);
            draw_cmd_kind(r, rp, &tsb, NULL, drawOrder, SCENE_GPU_DRAW_SIGN);
        }
        if (r->signBkPipeline) {
            SDL_BindGPUGraphicsPipeline(rp, r->signBkPipeline);
            draw_cmd_kind(r, rp, &tsb, NULL, drawOrder, SCENE_GPU_DRAW_SIGN_BK);
        }
    }
    if (r->treePipeline) {
        SDL_BindGPUGraphicsPipeline(rp, r->treePipeline);
        draw_cmd_kind(r, rp, &tsb, NULL, drawOrder, SCENE_GPU_DRAW_TREE);
    }
    /* Multiply-blend darken pass for TRANSPARENT non-textured track quads (glass/
     * divider walls) -- must run after opaque/wall/building/sign/tree so there's
     * real colour underneath to darken; see trackDarkenPipeline comment. */
    SDL_GPUGraphicsPipeline *trackDarkenPipeline = track_darken_pipeline_for_draw(r);
    if (trackDarkenPipeline) {
        SDL_BindGPUGraphicsPipeline(rp, trackDarkenPipeline);
        draw_cmd_kind(r, rp, &tsb, NULL, drawOrder, SCENE_GPU_DRAW_TRACK_DARKEN);
    }
    if (r->blendPipeline) {
        SDL_BindGPUGraphicsPipeline(rp, r->blendPipeline);
        draw_cmd_kind(r, rp, &tsb, NULL, drawOrder, SCENE_GPU_DRAW_BLEND);
    }

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

    /* Car shadow pass: drawn after car body so car-written depth blocks shadow
     * on the car surface (LESS_OR_EQUAL fails where car body wrote smaller depth). */
    if (r->carShadowDrawCount > 0 && r->carShadowPipeline) {
        SDL_BindGPUGraphicsPipeline(rp, r->carShadowPipeline);
        SDL_PushGPUFragmentUniformData(r->cmdBuf, 0, &pfu, sizeof(pfu));
        for (int i = 0; i < r->carShadowDrawCount; i++) {
            SceneGPUCarDrawCmd *cmd = &r->carShadowDraws[i];
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

    /* Road shadow pass: drawn after car mesh so the car's written depth masks the
     * shadow from appearing on the car body (fails LESS_OR_EQUAL where car is). */
    if (r->shadowPipeline && r->drawCmdCount > 0) {
        SDL_BindGPUVertexBuffers(rp, 0, &vbb, 1);  /* restore scene vertex buffer */
        SDL_BindGPUGraphicsPipeline(rp, r->shadowPipeline);
        /* Cars/car-shadows above pushed their own per-object MVP into the same
         * uniform slot -- re-push the shared view VP before resuming
         * SceneGPUDrawCmd (no per-object matrix) draws. */
        SDL_PushGPUVertexUniformData(r->cmdBuf, 0, viewVP, sizeof(viewVP));
        SDL_GPUTextureSamplerBinding stsb = {.texture = NULL, .sampler = r->sampler};
        for (int i = 0; i < r->drawCmdCount; i++) {
            SceneGPUDrawCmd *cmd = &r->drawCmds[i];
            if (cmd->kind != SCENE_GPU_DRAW_SHADOW) continue;
            stsb.texture = cmd->texture;
            SDL_BindGPUFragmentSamplers(rp, 0, &stsb, 1);
            r->gpuBindsThisFrame++;
            r->gpuDrawCallsThisFrame++;
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

    Uint64 tDraw = SDL_GetTicksNS();
    r->drawNs = tDraw - tUpload;

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
        tsb.sampler = r->sampler;
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

    Uint64 tHudPost = SDL_GetTicksNS();
    r->hudPostNs = tHudPost - tDraw;

    SDL_SubmitGPUCommandBuffer(r->cmdBuf);
    r->cmdBuf = NULL;
    r->submitNs = SDL_GetTicksNS() - tHudPost;

    /* Perf-investigation instrumentation (see g_bRenderStatsLog): draw-
     * command/vertex counts per frame, running peaks since the renderer was
     * created, and the ACTUAL GPU draw-call/bind counts after merging
     * vertex-contiguous same-texture commands in draw_cmd_kind (drawcmds is
     * just the queued metadata count, not the real GPU call count). Logged
     * roughly once a second (not every frame) to stay readable while driving. */
    if (r->drawCmdCount > r->peakDrawCmdCount) r->peakDrawCmdCount = r->drawCmdCount;
    if (r->vertexCount  > r->peakVertexCount)  r->peakVertexCount  = r->vertexCount;
    if (g_bRenderStatsLog) {
        static int s_logCounter = 0;
        if (++s_logCounter >= 60) {
            s_logCounter = 0;
            SDL_Log("RENDER STATS: drawcmds=%d verts=%d (peak dc=%d v=%d) "
                     "gpu-draws=%d gpu-binds=%d",
                     r->drawCmdCount, r->vertexCount,
                     r->peakDrawCmdCount, r->peakVertexCount,
                     r->gpuDrawCallsThisFrame, r->gpuBindsThisFrame);
            SDL_Log("RENDER CPU TIME (ms): queue=%.3f sort/repack=%.3f "
                     "upload=%.3f [vtx=%.3f(map-wait=%.3f) "
                     "hud=%.3f(px=%d map-wait=%.3f convert=%.3f) sky=%.3f "
                     "particle=%.3f texParticle=%.3f] draw=%.3f hud/post=%.3f "
                     "submit=%.3f total=%.3f",
                     r->queueNs             / 1000000.0,
                     r->sortRepackNs        / 1000000.0,
                     r->uploadNs            / 1000000.0,
                     r->vertexUploadNs      / 1000000.0,
                     r->vertexMapWaitNs     / 1000000.0,
                     r->hudUploadNs         / 1000000.0,
                     r->hudPixelCount,
                     r->hudMapWaitNs        / 1000000.0,
                     r->hudConvertNs        / 1000000.0,
                     r->skyUploadNs         / 1000000.0,
                     r->particleUploadNs    / 1000000.0,
                     r->texParticleUploadNs / 1000000.0,
                     r->drawNs              / 1000000.0,
                     r->hudPostNs           / 1000000.0,
                     r->submitNs            / 1000000.0,
                     (r->queueNs + r->sortRepackNs + r->uploadNs
                      + r->drawNs + r->hudPostNs + r->submitNs) / 1000000.0);
        }
    }
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
    r->carShadowDrawCount = 0;
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
        .max_lod           = r->disableMipmaps ? 0.0f : 1000.0f,
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

void scene_render_gpu_set_disable_mipmaps(SceneRendererGPU *r, bool disabled)
{
    if (!r) return;
    r->disableMipmaps = disabled;
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

void scene_render_gpu_set_emulate_software_track_darken_border(SceneRendererGPU *r, bool enabled)
{
    if (!r) return;
    r->emulateSoftwareTrackDarkenBorder = enabled;
}

static void rebuild_scene_pipelines(SceneRendererGPU *r, SDL_GPUSampleCount sc, SDL_GPUFillMode fm)
{
    SDL_GPUShader *sv = load_shader(r->device, SDL_GPU_SHADERSTAGE_VERTEX,
        game_scene_vertex_spirv, game_scene_vertex_spirv_size,
        game_scene_vertex_msl,   game_scene_vertex_msl_size, 0, 1);
    SDL_GPUShader *sfOp = load_shader(r->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        game_scene_pixel_spirv,  game_scene_pixel_spirv_size,
        game_scene_pixel_msl,    game_scene_pixel_msl_size,  1, 1);
    SDL_GPUShader *sfBl = load_shader(r->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        game_scene_pixel_blend_spirv, game_scene_pixel_blend_spirv_size,
        game_scene_pixel_blend_msl,   game_scene_pixel_blend_msl_size,  1, 1);
    SDL_GPUShader *sfTrack = load_shader(r->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        game_scene_track_darken_pixel_spirv, game_scene_track_darken_pixel_spirv_size,
        game_scene_track_darken_pixel_msl,   game_scene_track_darken_pixel_msl_size,  1, 1);
    SDL_GPUShader *sfSign = load_shader(r->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        game_scene_sign_pixel_spirv,  game_scene_sign_pixel_spirv_size,
        game_scene_sign_pixel_msl,    game_scene_sign_pixel_msl_size,    2, 1);
    if (sv && sfOp && sfBl) {
        r->opaquePipeline        = make_scene_pipeline(r, sv, sfOp, false, false, 0.0f,    0.0f,  sc, fm);
        r->blendPipeline         = make_scene_pipeline(r, sv, sfBl, true,  true,  0.0f,    0.0f,  sc, fm);
        r->buildingPipeline      = make_scene_pipeline(r, sv, sfOp, false, true,  0.0f,    0.0f,  sc, fm);
        r->bfBuildingPipeline    = make_scene_pipeline(r, sv, sfOp, false, true,  -200.0f, -1.0f, sc, fm);
        r->signPipeline          = make_sign_pipeline   (r, sv, sfOp,               sc, fm);
        r->signBkPipeline        = make_sign_bk_pipeline(r, sv, sfOp,               sc, fm);
        r->wallPipeline          = make_scene_pipeline  (r, sv, sfBl, false, true,  0.0f,   0.0f, sc, fm);
        r->bfWallPipeline        = make_scene_pipeline  (r, sv, sfBl, false, true,  -20.0f, 0.0f, sc, fm);
        r->shadowPipeline        = make_shadow_pipeline (r, sv, sfBl,               sc, fm);
        r->trackDarkenPipeline   = make_track_darken_pipeline(r, sv, sfOp,          sc, fm);
        r->skyPipeline           = make_sky_pipeline    (r, sv, sfOp, sc);
        r->treePipeline          = make_tree_pipeline   (r, sv, sfOp,               sc, fm);
    }
    if (sv && sfTrack) {
        r->trackDarkenBorderPipeline = make_track_darken_pipeline(r, sv, sfTrack, sc, fm);
    }
    if (sv && sfSign) {
        r->signDepthPipeline     = make_sign_depth_pipeline(r, sv, sfSign, SDL_GPU_CULLMODE_BACK, fm);
        r->signBkDepthPipeline   = make_sign_depth_pipeline(r, sv, sfSign, SDL_GPU_CULLMODE_NONE, fm);
    }
    if (sv)     SDL_ReleaseGPUShader(r->device, sv);
    if (sfOp)   SDL_ReleaseGPUShader(r->device, sfOp);
    if (sfBl)   SDL_ReleaseGPUShader(r->device, sfBl);
    if (sfTrack) SDL_ReleaseGPUShader(r->device, sfTrack);
    if (sfSign) SDL_ReleaseGPUShader(r->device, sfSign);
}

void scene_render_gpu_set_wireframe(SceneRendererGPU *r, bool enabled)
{
    if (!r || r->wireframe == enabled) return;
    r->wireframe = enabled;

    SDL_WaitForGPUIdle(r->device);

    if (r->opaquePipeline)   { SDL_ReleaseGPUGraphicsPipeline(r->device, r->opaquePipeline);   r->opaquePipeline   = NULL; }
    if (r->blendPipeline)    { SDL_ReleaseGPUGraphicsPipeline(r->device, r->blendPipeline);    r->blendPipeline    = NULL; }
    if (r->buildingPipeline)   { SDL_ReleaseGPUGraphicsPipeline(r->device, r->buildingPipeline);   r->buildingPipeline   = NULL; }
    if (r->bfBuildingPipeline) { SDL_ReleaseGPUGraphicsPipeline(r->device, r->bfBuildingPipeline); r->bfBuildingPipeline = NULL; }
    if (r->signPipeline)         { SDL_ReleaseGPUGraphicsPipeline(r->device, r->signPipeline);         r->signPipeline         = NULL; }
    if (r->signBkPipeline)       { SDL_ReleaseGPUGraphicsPipeline(r->device, r->signBkPipeline);       r->signBkPipeline       = NULL; }
    if (r->signDepthPipeline)    { SDL_ReleaseGPUGraphicsPipeline(r->device, r->signDepthPipeline);    r->signDepthPipeline    = NULL; }
    if (r->signBkDepthPipeline)  { SDL_ReleaseGPUGraphicsPipeline(r->device, r->signBkDepthPipeline);  r->signBkDepthPipeline  = NULL; }
    if (r->treePipeline)         { SDL_ReleaseGPUGraphicsPipeline(r->device, r->treePipeline);         r->treePipeline         = NULL; }
    if (r->wallPipeline)         { SDL_ReleaseGPUGraphicsPipeline(r->device, r->wallPipeline);         r->wallPipeline         = NULL; }
    if (r->bfWallPipeline)       { SDL_ReleaseGPUGraphicsPipeline(r->device, r->bfWallPipeline);       r->bfWallPipeline       = NULL; }
    if (r->shadowPipeline)       { SDL_ReleaseGPUGraphicsPipeline(r->device, r->shadowPipeline);       r->shadowPipeline       = NULL; }
    if (r->trackDarkenPipeline)  { SDL_ReleaseGPUGraphicsPipeline(r->device, r->trackDarkenPipeline);  r->trackDarkenPipeline  = NULL; }
    if (r->trackDarkenBorderPipeline) { SDL_ReleaseGPUGraphicsPipeline(r->device, r->trackDarkenBorderPipeline); r->trackDarkenBorderPipeline = NULL; }
    if (r->carPipeline)          { SDL_ReleaseGPUGraphicsPipeline(r->device, r->carPipeline);          r->carPipeline          = NULL; }
    if (r->carShadowPipeline)    { SDL_ReleaseGPUGraphicsPipeline(r->device, r->carShadowPipeline);    r->carShadowPipeline    = NULL; }
    if (r->skyPipeline)          { SDL_ReleaseGPUGraphicsPipeline(r->device, r->skyPipeline);          r->skyPipeline          = NULL; }

    SDL_GPUFillMode fm = enabled ? SDL_GPU_FILLMODE_LINE : SDL_GPU_FILLMODE_FILL;
    SDL_GPUSampleCount sc = r->msaaSampleCount;

    rebuild_scene_pipelines(r, sc, fm);

    SDL_GPUShader *cv = load_shader(r->device, SDL_GPU_SHADERSTAGE_VERTEX,
        game_car_vertex_spirv, game_car_vertex_spirv_size,
        game_car_vertex_msl,   game_car_vertex_msl_size, 0, 1);
    SDL_GPUShader *cf = load_shader(r->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        game_car_pixel_spirv,  game_car_pixel_spirv_size,
        game_car_pixel_msl,    game_car_pixel_msl_size,  1, 1);
    if (cv && cf) {
        r->carPipeline       = make_car_pipeline(r, cv, cf, sc, fm);
        r->carShadowPipeline = make_car_shadow_pipeline(r, cv, cf, sc, fm);
    }
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
    if (r->buildingPipeline)   { SDL_ReleaseGPUGraphicsPipeline(r->device, r->buildingPipeline);   r->buildingPipeline   = NULL; }
    if (r->bfBuildingPipeline) { SDL_ReleaseGPUGraphicsPipeline(r->device, r->bfBuildingPipeline); r->bfBuildingPipeline = NULL; }
    if (r->signPipeline)        { SDL_ReleaseGPUGraphicsPipeline(r->device, r->signPipeline);        r->signPipeline        = NULL; }
    if (r->signBkPipeline)      { SDL_ReleaseGPUGraphicsPipeline(r->device, r->signBkPipeline);      r->signBkPipeline      = NULL; }
    if (r->signDepthPipeline)   { SDL_ReleaseGPUGraphicsPipeline(r->device, r->signDepthPipeline);   r->signDepthPipeline   = NULL; }
    if (r->signBkDepthPipeline) { SDL_ReleaseGPUGraphicsPipeline(r->device, r->signBkDepthPipeline); r->signBkDepthPipeline = NULL; }
    if (r->wallPipeline)        { SDL_ReleaseGPUGraphicsPipeline(r->device, r->wallPipeline);        r->wallPipeline        = NULL; }
    if (r->bfWallPipeline)      { SDL_ReleaseGPUGraphicsPipeline(r->device, r->bfWallPipeline);      r->bfWallPipeline      = NULL; }
    if (r->shadowPipeline)      { SDL_ReleaseGPUGraphicsPipeline(r->device, r->shadowPipeline);      r->shadowPipeline      = NULL; }
    if (r->trackDarkenPipeline) { SDL_ReleaseGPUGraphicsPipeline(r->device, r->trackDarkenPipeline); r->trackDarkenPipeline = NULL; }
    if (r->trackDarkenBorderPipeline) { SDL_ReleaseGPUGraphicsPipeline(r->device, r->trackDarkenBorderPipeline); r->trackDarkenBorderPipeline = NULL; }
    if (r->skyPipeline)         { SDL_ReleaseGPUGraphicsPipeline(r->device, r->skyPipeline);         r->skyPipeline         = NULL; }

    SDL_GPUFillMode fm = r->wireframe ? SDL_GPU_FILLMODE_LINE : SDL_GPU_FILLMODE_FILL;
    SDL_GPUSampleCount sc = r->msaaSampleCount;

    rebuild_scene_pipelines(r, sc, fm);
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
    if (r->buildingPipeline)   { SDL_ReleaseGPUGraphicsPipeline(r->device, r->buildingPipeline);   r->buildingPipeline   = NULL; }
    if (r->bfBuildingPipeline) { SDL_ReleaseGPUGraphicsPipeline(r->device, r->bfBuildingPipeline); r->bfBuildingPipeline = NULL; }
    if (r->signPipeline)         { SDL_ReleaseGPUGraphicsPipeline(r->device, r->signPipeline);         r->signPipeline         = NULL; }
    if (r->signBkPipeline)       { SDL_ReleaseGPUGraphicsPipeline(r->device, r->signBkPipeline);       r->signBkPipeline       = NULL; }
    if (r->signDepthPipeline)    { SDL_ReleaseGPUGraphicsPipeline(r->device, r->signDepthPipeline);    r->signDepthPipeline    = NULL; }
    if (r->signBkDepthPipeline)  { SDL_ReleaseGPUGraphicsPipeline(r->device, r->signBkDepthPipeline);  r->signBkDepthPipeline  = NULL; }
    if (r->treePipeline)         { SDL_ReleaseGPUGraphicsPipeline(r->device, r->treePipeline);         r->treePipeline         = NULL; }
    if (r->wallPipeline)         { SDL_ReleaseGPUGraphicsPipeline(r->device, r->wallPipeline);         r->wallPipeline         = NULL; }
    if (r->bfWallPipeline)       { SDL_ReleaseGPUGraphicsPipeline(r->device, r->bfWallPipeline);       r->bfWallPipeline       = NULL; }
    if (r->shadowPipeline)       { SDL_ReleaseGPUGraphicsPipeline(r->device, r->shadowPipeline);       r->shadowPipeline       = NULL; }
    if (r->trackDarkenPipeline)  { SDL_ReleaseGPUGraphicsPipeline(r->device, r->trackDarkenPipeline);  r->trackDarkenPipeline  = NULL; }
    if (r->trackDarkenBorderPipeline) { SDL_ReleaseGPUGraphicsPipeline(r->device, r->trackDarkenBorderPipeline); r->trackDarkenBorderPipeline = NULL; }
    if (r->carPipeline)          { SDL_ReleaseGPUGraphicsPipeline(r->device, r->carPipeline);          r->carPipeline          = NULL; }
    if (r->carShadowPipeline)    { SDL_ReleaseGPUGraphicsPipeline(r->device, r->carShadowPipeline);    r->carShadowPipeline    = NULL; }
    if (r->skyPipeline)          { SDL_ReleaseGPUGraphicsPipeline(r->device, r->skyPipeline);          r->skyPipeline          = NULL; }
    if (r->particlePipeline)     { SDL_ReleaseGPUGraphicsPipeline(r->device, r->particlePipeline);     r->particlePipeline     = NULL; }
    if (r->texParticlePipeline)  { SDL_ReleaseGPUGraphicsPipeline(r->device, r->texParticlePipeline);  r->texParticlePipeline  = NULL; }

    /* Release MSAA textures — recreated in the next end_frame at the right size. */
    if (r->msaaTex)      { SDL_ReleaseGPUTexture(r->device, r->msaaTex);      r->msaaTex      = NULL; }
    if (r->msaaDepthTex) { SDL_ReleaseGPUTexture(r->device, r->msaaDepthTex); r->msaaDepthTex = NULL; }
    r->msaaW = 0; r->msaaH = 0;

    /* Same for the secondary-view (mirror/2P) MSAA intermediates -- recreated
     * (or left absent, if the new level is off) in the next
     * ensure_secondary_textures call at the right sample count. */
    for (int i = 0; i < SCENE_GPU_MAX_SECONDARY_VIEWS; i++) {
        if (r->secondaryMsaaColorTex[i]) { SDL_ReleaseGPUTexture(r->device, r->secondaryMsaaColorTex[i]); r->secondaryMsaaColorTex[i] = NULL; }
        if (r->secondaryMsaaDepthTex[i]) { SDL_ReleaseGPUTexture(r->device, r->secondaryMsaaDepthTex[i]); r->secondaryMsaaDepthTex[i] = NULL; }
        r->secondaryMsaaW[i] = 0; r->secondaryMsaaH[i] = 0;
    }

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
    SDL_GPUShader *sfTrack = load_shader(r->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        game_scene_track_darken_pixel_spirv, game_scene_track_darken_pixel_spirv_size,
        game_scene_track_darken_pixel_msl,   game_scene_track_darken_pixel_msl_size,  1, 1);
    SDL_GPUShader *sfSign = load_shader(r->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        game_scene_sign_pixel_spirv,  game_scene_sign_pixel_spirv_size,
        game_scene_sign_pixel_msl,    game_scene_sign_pixel_msl_size,    2, 1);
    SDL_GPUFillMode fm = r->wireframe ? SDL_GPU_FILLMODE_LINE : SDL_GPU_FILLMODE_FILL;
    if (sv && sfOp && sfBl) {
        r->opaquePipeline   = make_scene_pipeline(r, sv, sfOp, false, false, 0.0f,   0.0f, sc, fm);
        r->blendPipeline    = make_scene_pipeline(r, sv, sfBl, true,  true,  0.0f,   0.0f, sc, fm);
        r->buildingPipeline   = make_scene_pipeline(r, sv, sfOp, false, true,   0.0f,  0.0f, sc, fm);
        r->bfBuildingPipeline = make_scene_pipeline(r, sv, sfOp, false, true,  -200.0f, -1.0f, sc, fm);
        r->signPipeline     = make_sign_pipeline   (r, sv, sfOp,               sc, fm);
        r->signBkPipeline   = make_sign_bk_pipeline(r, sv, sfOp,               sc, fm);
        r->wallPipeline     = make_scene_pipeline  (r, sv, sfBl, false, true,   0.0f, 0.0f, sc, fm);
        r->bfWallPipeline   = make_scene_pipeline  (r, sv, sfBl, false, true,  -20.0f, 0.0f, sc, fm);
        r->shadowPipeline   = make_shadow_pipeline(r, sv, sfBl,       sc,    fm);
        r->trackDarkenPipeline = make_track_darken_pipeline(r, sv, sfOp, sc, fm);
        r->skyPipeline      = make_sky_pipeline(r, sv, sfOp, sc);
    }
    if (sv && sfTrack) {
        r->trackDarkenBorderPipeline = make_track_darken_pipeline(r, sv, sfTrack, sc, fm);
    }
    if (sv && sfSign) {
        r->signDepthPipeline   = make_sign_depth_pipeline(r, sv, sfSign, SDL_GPU_CULLMODE_BACK, fm);
        r->signBkDepthPipeline = make_sign_depth_pipeline(r, sv, sfSign, SDL_GPU_CULLMODE_NONE, fm);
    }
    if (sv)     SDL_ReleaseGPUShader(r->device, sv);
    if (sfOp)   SDL_ReleaseGPUShader(r->device, sfOp);
    if (sfBl)   SDL_ReleaseGPUShader(r->device, sfBl);
    if (sfTrack) SDL_ReleaseGPUShader(r->device, sfTrack);
    if (sfSign) SDL_ReleaseGPUShader(r->device, sfSign);

    SDL_GPUShader *cv = load_shader(r->device, SDL_GPU_SHADERSTAGE_VERTEX,
        game_car_vertex_spirv, game_car_vertex_spirv_size,
        game_car_vertex_msl,   game_car_vertex_msl_size, 0, 1);
    SDL_GPUShader *cf = load_shader(r->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        game_car_pixel_spirv,  game_car_pixel_spirv_size,
        game_car_pixel_msl,    game_car_pixel_msl_size,  1, 1);
    if (cv && cf) {
        r->carPipeline       = make_car_pipeline(r, cv, cf, sc, fm);
        r->carShadowPipeline = make_car_shadow_pipeline(r, cv, cf, sc, fm);
    }
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
    /* Ownership of atlasRgba transfers here (see the final `free(atlasRgba)`
     * below being removed for the success path) -- kept alive for on-demand
     * PNG export of a picked tile/pair. */
    s->atlasRgbaCopy = atlasRgba;
    s->atlasWidth    = width;
    s->atlasHeight   = height;

    /* One command buffer for all tile + pair uploads for this slot.
     * Transfer buffers are collected and released after the single submit. */
    SDL_GPUCommandBuffer *uploadCmd = SDL_AcquireGPUCommandBuffer(r->device);
    if (!uploadCmd) {
        s->in_use = 0; free(tileRgba); free(atlasRgba); s->atlasRgbaCopy = NULL;
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
     * SW's polyt reads both tiles through a SINGLE base pointer (tile N's own
     * address) with a 128-wide flat read (pTex[y*width+x], width fixed at
     * 256 regardless of tile size) and no per-tile bounds checking -- it
     * never actually looks up a second tile's address at all. For a
     * non-last-column tile this flat read still lines up perfectly with
     * tile N+1's own block (column offset col_n*tileSize+tileSize equals
     * (col_n+1)*tileSize, i.e. tile N+1's left edge, unshifted). For a
     * LAST-column tile, though, col_n*tileSize+tileSize lands exactly on
     * width (256) -- one full atlas row-stride -- so every row of the
     * "right" read actually comes from ONE ROW DOWN in tile (row_n, col=0)
     * (the first tile of tile N's own row), not from tile N+1 at all, except
     * for the tile's very last row, which spills past the row-stride
     * boundary into tile (row_n+1, col=0)'s first row. Reproduced exactly
     * below rather than approximated as a whole-tile substitution, since
     * whichever whole tile is picked (tile N mirrored, or tile N+1 whole)
     * only matches by coincidence when neighbouring tiles happen to look
     * alike -- it visibly fails when they don't (e.g. a pit-lane floor tile
     * whose row starts with an unrelated grass tile at column 0). */
    int pairW = 2 * tileSize;
    uint8 *pairRgba = uploadOk ? malloc((size_t)(pairW * tileSize * 4)) : NULL;
    if (pairRgba) {
        for (int n = 0; n + 1 < numTiles && uploadOk; n++) {
            int col_n  = n % tilesPerRow;
            int row_n  = n / tilesPerRow;
            int col_r  = (n + 1) % tilesPerRow;
            int row_r  = (n + 1) / tilesPerRow;
            bool lastCol = (col_n == tilesPerRow - 1);

            for (int y = 0; y < tileSize; y++) {
                int srcRowL = row_n * tileSize + y;
                int srcRowR, srcColR;
                if (lastCol) {
                    if (y < tileSize - 1) { srcRowR = row_n * tileSize + (y + 1); srcColR = 0; }
                    else                  { srcRowR = (row_n + 1) * tileSize;    srcColR = 0; }
                } else {
                    srcRowR = row_r * tileSize + y;
                    srcColR = col_r * tileSize;
                }
                for (int x = 0; x < tileSize; x++) {
                    int srcOff = (srcRowL * width + col_n * tileSize + x) * 4;
                    int dstOff = (y * pairW + x) * 4;
                    pairRgba[dstOff+0] = atlasRgba[srcOff+0];
                    pairRgba[dstOff+1] = atlasRgba[srcOff+1];
                    pairRgba[dstOff+2] = atlasRgba[srcOff+2];
                    pairRgba[dstOff+3] = atlasRgba[srcOff+3];
                    srcOff = (srcRowR * width + srcColR + x) * 4;
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
    if (!SDL_SubmitGPUCommandBuffer(uploadCmd))
        uploadOk = false;

    for (int i = 0; i < nTbs; i++)
        SDL_ReleaseGPUTransferBuffer(r->device, tbs[i]);

    /* Generate mipmaps in small batches, each its own command buffer submit,
     * instead of every texture in the slot crammed into one submit. A single
     * slot can have up to ~3*256 textures (tiles + particle tiles + pairs)
     * needing mipmaps -- too many separate command buffer submits overloads
     * some Android GPU drivers, but cramming hundreds of
     * SDL_GenerateMipmapsForGPUTexture calls into a SINGLE submit risks the
     * same kind of overload from the other direction, so this batches into
     * fixed-size chunks instead of picking either extreme. Safe to submit
     * these after uploadCmd above: GPU submits on one device queue execute
     * in submission order, so the base-level pixel data is already resident
     * by the time these run. */
    if (uploadOk && mip_level_count(tileSize, tileSize) > 1) {
        SDL_GPUTexture *mipTexs[SCENE_GPU_MAX_TILES_PER_SLOT * 3];
        int nMipTexs = 0;
        for (int t = 0; t < numTiles; t++) {
            if (s->tileTextures[t])         mipTexs[nMipTexs++] = s->tileTextures[t];
            if (s->particleTileTextures[t]) mipTexs[nMipTexs++] = s->particleTileTextures[t];
        }
        for (int n = 0; n + 1 < numTiles; n++)
            if (s->pairTextures[n]) mipTexs[nMipTexs++] = s->pairTextures[n];

        for (int i = 0; i < nMipTexs; i += SCENE_GPU_MIPMAP_BATCH_SIZE) {
            SDL_GPUCommandBuffer *mipCmd = SDL_AcquireGPUCommandBuffer(r->device);
            if (!mipCmd) break;
            int batchEnd = i + SCENE_GPU_MIPMAP_BATCH_SIZE;
            if (batchEnd > nMipTexs) batchEnd = nMipTexs;
            for (int j = i; j < batchEnd; j++)
                SDL_GenerateMipmapsForGPUTexture(mipCmd, mipTexs[j]);
            if (!SDL_SubmitGPUCommandBuffer(mipCmd)) {
                uploadOk = false;
                break;
            }
        }
    }

    /* Loading a bank can enqueue dozens of command buffers.  In particular,
     * the Vulkan backend associates a fence with every submit and only
     * recycles those fences after completed work is retired.  Texture banks
     * are loaded back-to-back, so without a reclamation point the pending
     * fences can exhaust the backend's host allocations before the next bank
     * is staged.  Loading is already synchronous from the game's point of
     * view, making the end of a bank a safe and infrequent place to drain the
     * queue.  This also guarantees that deferred transfer-buffer releases
     * have completed before another bank allocates its staging resources. */
    if (!SDL_WaitForGPUIdle(r->device))
        uploadOk = false;

    SDL_Log("tex_idx=%d tiles=%d uploadOk=%d err=%s",
        tex_idx, numTiles, uploadOk, uploadOk ? "ok" : SDL_GetError());

    if (!uploadOk) {
        for (int t = 0; t < numTiles; t++) {
            if (s->tileTextures[t])         { SDL_ReleaseGPUTexture(r->device, s->tileTextures[t]);         s->tileTextures[t]         = NULL; }
            if (s->pairTextures[t])         { SDL_ReleaseGPUTexture(r->device, s->pairTextures[t]);         s->pairTextures[t]         = NULL; }
            if (s->particleTileTextures[t]) { SDL_ReleaseGPUTexture(r->device, s->particleTileTextures[t]); s->particleTileTextures[t] = NULL; }
        }
        s->in_use = 0;
        free(s->atlasRgbaCopy);
        s->atlasRgbaCopy = NULL;
        return SCENE_TEXTURE_HANDLE_INVALID;
    }
    /* atlasRgba's ownership now belongs to s->atlasRgbaCopy on the success
     * path -- not freed here. */

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
    free(s->atlasRgbaCopy);
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
    /* Only throttle when logging every surface (id == -1) -- that can be
     * thousands of calls/frame. Once narrowed to one specific id, the
     * throttle actively hides the exact instance being investigated: it's a
     * fixed periodic sample, so a tile that's consistently queued in the
     * same relative order every frame can permanently miss every boundary
     * and never get logged at all. */
    if (g_iSurfaceLogId < 0) {
        static uint32 s_n = 0;
        if ((s_n++ & 0xFF) != 0) return;
    }
    SDL_Log("%s idx=%d sf=0x%X flipV=%d efV=%d efH=%d%s",
            type, surfIdx, surfaceFlags, (int)flipV, (int)efV, (int)efH, extra);
}
/* Staged copy of the most recent surface_uv_log() call within the current
 * quad, so the click-hit code (later in scene_render_gpu_quad_world_legacy,
 * after cu[]/cv[] are finalized) can attach it to the PICK log regardless of
 * whether g_bSurfaceLog is enabled. Reset per-quad by the caller so a quad
 * type that never calls surface_uv_log (e.g. cloud) doesn't leak stale data
 * from a previous quad into its PICK line. */
static struct { bool valid; char type[16]; int texId; bool flipV, efV, efH, p03L, row01Bot;
                float cu0, cv0, cv2, bcross; bool isFloorLike;
                bool wzRowBot, wzSpans; bool useRowDetect;
                int backwardsVal; float adjSum01, adjSum23; bool spansCC;
                float camX, camY, camZ;
                bool backTexApplied; float backDot; int backOrigIdx, backNewIdx;
                bool uWindingFlip; float uWindingArea;
                bool vSwap; float vNrmY;
                bool splitAValid; float splitArea012, splitArea023;
                float cv1, cv3; } s_lastUV;

static void surface_uv_log(const char *type, int texId, int surfIdx, int surfaceFlags,
                                  bool flipV, bool efV, bool efH,
                                  bool pair03Left, bool row01Bot,
                                  float cu0, float cv0, float cv2, float bcross,
                                  float v0x, float v0y, float v0z,
                                  float v2x, float v2y, float v2z,
                                  float sY0, float sY1, float sY2, float sY3)
{
    s_lastUV.valid    = true;
    SDL_strlcpy(s_lastUV.type, type, sizeof(s_lastUV.type));
    s_lastUV.texId    = texId;
    s_lastUV.flipV    = flipV;
    s_lastUV.efV      = efV;
    s_lastUV.efH      = efH;
    s_lastUV.p03L     = pair03Left;
    s_lastUV.row01Bot = row01Bot;
    s_lastUV.cu0      = cu0;
    s_lastUV.cv0      = cv0;
    s_lastUV.cv2      = cv2;
    s_lastUV.bcross   = bcross;

    char extra[192];
    SDL_snprintf(extra, sizeof(extra),
                 " tex=%d p03L=%d row01Bot=%d cu0=%.3f cv0=%.3f cv2=%.3f bcross=%.3f"
                 " | v0xyz=(%.1f,%.1f,%.1f) v2xyz=(%.1f,%.1f,%.1f)"
                 " sY01=(%.4f,%.4f) sY23=(%.4f,%.4f)",
                 texId, (int)pair03Left, (int)row01Bot,
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

/* Cheap frustum cull: true if all 4 vertices are behind the camera, or all 4
 * fall outside the same side of the horizontal/vertical view cone.
 *
 * DrawTrack3 (drawtrk3.c, shared SW/GPU code) decides which track chunks to
 * queue based on a CPU-side chunk-count window -- with "Draw distance"
 * (g_fDrawDistanceFraction, a 0.0-1.0 slider; 1.0 = old "Infinite draw
 * distance" checkbox) turned up, that window grows toward the ENTIRE track
 * (TrackSize approaching TRAK_LEN-1) instead of the normal ~24-48 local
 * chunks. Every polygon DrawTrack3 decides to queue reaches this function
 * and, until now, got the full GPU treatment (texture resolution, UV setup,
 * vertex generation, draw-command creation) regardless of whether it was actually
 * on screen. On a curving/looping track most of those extra chunks are
 * geometrically behind the camera or off to the side, not visible -- so
 * culling them here, before any of that downstream work, recovers most of
 * the lost performance from turning on unlimited draw distance (SW never
 * needed this: its scanline rasterizer implicitly discards off-screen spans
 * as part of drawing).
 *
 * Uses a generous NDC margin (actual clip range is +-1) so nothing visible,
 * or close to the screen edge, is ever culled -- under-culling is safe,
 * over-culling causes visible pop-in. Mixed quads (straddling the camera
 * plane) skip the side/FOV test entirely and are let through: they're close
 * to the camera and cheap regardless, and dividing by a near-zero/negative
 * depth for a behind-camera vertex would give a meaningless projection. */
static bool scene_render_gpu_quad_frustum_culled(const SceneRendererGPU *r,
                                                  const SceneRenderVertex verts[4])
{
    float fwd_x = r->proj.view[0][2], fwd_y = r->proj.view[1][2], fwd_z = r->proj.view[2][2];
    float cx = r->camera.viewX, cy = r->camera.viewY, cz = r->camera.viewZ;

    float depth[4];
    bool anyInFront = false;
    for (int k = 0; k < 4; k++) {
        depth[k] = fwd_x*(verts[k].x-cx) + fwd_y*(verts[k].y-cy) + fwd_z*(verts[k].z-cz);
        if (depth[k] > 0.0f) anyInFront = true;
    }
    if (!anyInFront) return true;  /* entirely behind camera */
    for (int k = 0; k < 4; k++)
        if (depth[k] <= 0.0f) return false;  /* straddles camera plane -- let through */

    float right_x = r->proj.view[0][0], right_y = r->proj.view[1][0], right_z = r->proj.view[2][0];
    float up_x    = r->proj.view[0][1], up_y    = r->proj.view[1][1], up_z    = r->proj.view[2][1];
    int   vpW = r->viewportW > 0 ? r->viewportW : 640;
    int   vpH = r->viewportH > 0 ? r->viewportH : 400;
    float ss   = (float)r->proj.screenScale / 64.0f;
    float fovX = (2.0f * r->camera.fovScale * r->fovMultiplier * ss) / (float)vpW;
    float fovY = (2.0f * r->camera.fovScale * r->fovMultiplier * ss) / (float)vpH;

    const float kNdcMargin = 3.0f;
    bool allLeft = true, allRight = true, allAbove = true, allBelow = true;
    for (int k = 0; k < 4; k++) {
        float dx = verts[k].x-cx, dy = verts[k].y-cy, dz = verts[k].z-cz;
        float viewX = right_x*dx + right_y*dy + right_z*dz;
        float viewY = up_x*dx    + up_y*dy    + up_z*dz;
        float ndcX = fovX * viewX / depth[k];
        float ndcY = fovY * viewY / depth[k];
        if (ndcX > -kNdcMargin) allLeft  = false;
        if (ndcX <  kNdcMargin) allRight = false;
        if (ndcY > -kNdcMargin) allAbove = false;
        if (ndcY <  kNdcMargin) allBelow = false;
    }
    return allLeft || allRight || allAbove || allBelow;
}

/* --------------------------------------------------------------------------
 * Per-case UV computation, one function per branch of
 * scene_render_gpu_quad_world_legacy's UV dispatch. Each function is a
 * verbatim move of that branch's original code -- same statements, same
 * order, no logic changes -- extracted purely for readability. See that
 * function's dispatcher for which case calls which.
 * -------------------------------------------------------------------------- */

static void compute_uv_pair_x(SceneRendererGPU *r, const SceneRenderVertex verts[4],
                               int surfaceFlags, SceneTextureHandle texture, int surfIdx,
                               bool isBuilding, bool flipH, float uMaxN, float vMaxN,
                               float cu[4], float cv[4])
{
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
    bool flipV = !isBuilding && (surfaceFlags & SURFACE_FLAG_FLIP_VERT) != 0;

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
    /* For non-FLIP_BACKFACE quads, U used to be 100% static (effectiveFlipH alone).
     * A screen-space signed-area/winding test (mirroring polyt()'s own winding calc)
     * fixed one reported case driving forward but was fragile turning around -- the
     * same quad's computed area collapsed to ~0 instead of flipping sign, meaning the
     * test carries no reliable signal from that angle. Same camera-orientation
     * fragility class as everywhere else in this file; fixed the same way -- compare
     * pure world-space coordinates between the two vertex columns ({v0,v3} vs
     * {v1,v2}) instead, with no camera involvement at all, matching the principle
     * behind pair03Left ("the camera can look from either end of the polygon... the
     * world tile assignment must remain stable").
     *
     * Gated to non-FLIP_BACKFACE (keeps this scoped away from the BF sign-wall logic),
     * isFloorLike (some twisted/rotated CONCAVE quads have a face normal that reads
     * "floor-like" (nrmZ-dominant) despite looking like a wall), AND CONCAVE (see the
     * gate comment right below for why plain non-CONCAVE floor quads, e.g. roads,
     * must NOT go through this at all). Quads failing any of the three keep the
     * original static effectiveFlipH-only U assignment. */
    float uex1x = verts[1].x-verts[0].x, uex1y = verts[1].y-verts[0].y, uex1z = verts[1].z-verts[0].z;
    float uex2x = verts[3].x-verts[0].x, uex2y = verts[3].y-verts[0].y, uex2z = verts[3].z-verts[0].z;
    float unrmX = uex1y*uex2z - uex1z*uex2y;
    float unrmY = uex1z*uex2x - uex1x*uex2z;
    float unrmZ = uex1x*uex2y - uex1y*uex2x;
    bool uIsFloorLike = fabsf(unrmZ) > sqrtf(unrmX*unrmX + unrmY*unrmY);
    bool uWindingFlip = false;
    float uWindingArea = 0.0f;
    if (uIsFloorLike && (surfaceFlags & SURFACE_FLAG_FLIP_BACKFACE) == 0
                     && (surfaceFlags & SURFACE_FLAG_CONCAVE) != 0) {
        /* This dynamic U test only matters for CONCAVE quads (e.g. arrow-sign
         * panels), which can be authored with either winding direction, so a
         * fixed effectiveFlipH-only assignment isn't reliable for them.
         * Plain (non-CONCAVE) floor-like quads, like ordinary road surfaces,
         * are always authored with consistent winding, so effectiveFlipH
         * alone is already correct for them -- running this test on them too
         * would "fix" quads that weren't broken, flipping some of them the
         * wrong way depending on which way the track curves at that point. */
        uWindingArea = (verts[0].x + verts[3].x) - (verts[1].x + verts[2].x);
        if (uWindingArea < 0.0f) uWindingFlip = true;
    }
    s_lastUV.uWindingArea = uWindingArea;
    s_lastUV.uWindingFlip = uWindingFlip;
    float uT = (effectiveFlipH != uWindingFlip) ? 0.0f  : uMaxN;
    float uB = (effectiveFlipH != uWindingFlip) ? uMaxN : 0.0f;
    cu[0] = uT; cu[3] = uT;
    cu[1] = uB; cu[2] = uB;
    if ((surfaceFlags & SURFACE_FLAG_CONCAVE) != 0 || uIsFloorLike) {
        /* col01Left is unstable for horizontal surfaces — bypass it like
         * Z-face bypasses row01Bot. The instability is about being floor-like
         * (near-horizontal) -- for those quads, small camera-angle changes can
         * flip which screen-X pair looks "left" without the quad's actual
         * top/bottom having changed at all, so col01Left can't be trusted
         * regardless of whether the quad happens to also be CONCAVE. Any
         * near-horizontal quad instead gets the stable fixed-index+flipV
         * assignment. flipH only affects U (startsx) in SW, never startsy. */
        if ((surfaceFlags & SURFACE_FLAG_CONCAVE) != 0) {
            /* Vertex winding (vnrmY sign) genuinely differs between two physical
             * placements of the same tile (idx=8), but swapping vL/vR based on its sign
             * was tried and did NOT fix a confirmed-wrong instance -- the winding
             * difference is real but not the cause of that bug (a separate
             * texture-selection issue, since fixed -- see wallFrontFacing in the main
             * function). Kept as a diagnostic only. */
            float vex1x = verts[1].x-verts[0].x, vex1z = verts[1].z-verts[0].z;
            float vex2x = verts[3].x-verts[0].x, vex2z = verts[3].z-verts[0].z;
            float vnrmY = vex1z*vex2x - vex1x*vex2z;
            s_lastUV.vSwap = false;
            s_lastUV.vNrmY = vnrmY;
        }

        /* V assigned by fixed vertex index (v0,v1=top; v2,v3=bottom) with
         * flipV as the only swap, matching SW's startsy[] default -- this is
         * SW's real, unconditional convention (set_starts() is a pure
         * fixed-index assignment, never adapted to world geometry or camera
         * position), so this is correct by SW-parity regardless of the
         * quad's actual world-space shape or which side the camera is on.
         *
         * A "nearer edge wins" camera-relative test (the kind compute_uv_pair_z
         * uses for its FLIP_BACKFACE branch) would be wrong here: these quads
         * don't have two coincident FV/non-FV surfaces offset by wall
         * thickness, so there's no "nearer edge" that maps to a stable
         * top/bottom the way it does there -- it would just make the V
         * assignment depend on camera position instead of the fixed,
         * position-independent convention SW actually uses. If a specific
         * tile still looks flipped despite this, check whether the raw atlas
         * tile itself is authored flipped before changing this formula --
         * that's a texture-content problem, not something a UV formula can
         * fix. */
        float vL = flipV ? vMaxN : 0.0f;
        float vR = flipV ? 0.0f  : vMaxN;
        cv[0] = vL; cv[1] = vL;
        cv[2] = vR; cv[3] = vR;

        /* A twisted CONCAVE quad's 4 vertices aren't guaranteed coplanar, so
         * whichever diagonal (0-2 or 1-3) splits it into triangles can matter:
         * the wrong diagonal can produce a self-intersecting "bowtie" split
         * for some camera angles even though the world vertices themselves
         * haven't moved. This picks whichever diagonal gives two triangles
         * with matching signed-area sign, i.e. a simple non-self-intersecting
         * split -- the standard convex-quad-diagonal test. This only changes
         * which triangles get formed, never UV values; it's deliberately not
         * a full port of SW's twpolym (which also re-derives UVs per
         * triangle) since that would duplicate and risk destabilizing the
         * UV logic above. Scoped to actual CONCAVE quads only (not just
         * uIsFloorLike) -- genuinely convex quads (plain roads) don't have a
         * twisted-triangulation problem to begin with; either diagonal tiles
         * them correctly. */
        if ((surfaceFlags & SURFACE_FLAG_CONCAVE) != 0 && uIsFloorLike) {
            const float (*SM)[3] = r->proj.view;
            float sgsx[4], sgsy[4];
            for (int vi = 0; vi < 4; vi++) {
                float ddx = verts[vi].x - r->camera.viewX;
                float ddy = verts[vi].y - r->camera.viewY;
                float ddz = verts[vi].z - r->camera.viewZ;
                float vvx = ddx*SM[0][0] + ddy*SM[1][0] + ddz*SM[2][0];
                float vvy = ddx*SM[0][1] + ddy*SM[1][1] + ddz*SM[2][1];
                float vvz = ddx*SM[0][2] + ddy*SM[1][2] + ddz*SM[2][2];
                float vvzc = (vvz < 80.0f) ? 80.0f : vvz;
                sgsx[vi] = vvx / vvzc;
                sgsy[vi] = -(vvy / vvzc);
            }
            #define GPU_CROSS2(ax,ay,bx,by,cx2,cy2) (((bx)-(ax))*((cy2)-(ay)) - ((by)-(ay))*((cx2)-(ax)))
            float sArea012 = GPU_CROSS2(sgsx[0],sgsy[0], sgsx[1],sgsy[1], sgsx[2],sgsy[2]);
            float sArea023 = GPU_CROSS2(sgsx[0],sgsy[0], sgsx[2],sgsy[2], sgsx[3],sgsy[3]);
            #undef GPU_CROSS2
            s_lastUV.splitAValid  = (sArea012 >= 0.0f) == (sArea023 >= 0.0f);
            s_lastUV.splitArea012 = sArea012;
            s_lastUV.splitArea023 = sArea023;
        }
    } else {
        float vL = (col01Left != effectiveFlipV) ? 0.0f  : vMaxN;
        float vR = (col01Left != effectiveFlipV) ? vMaxN : 0.0f;
        if (flipH) { float tmp = vL; vL = vR; vR = tmp; }
        cv[0] = vL; cv[1] = vL;
        cv[2] = vR; cv[3] = vR;
    }
    s_lastUV.cv1 = cv[1];
    s_lastUV.cv3 = cv[3];
    surface_uv_log("PAIR-X", texture, surfIdx, surfaceFlags,
                   flipV, effectiveFlipV, effectiveFlipH,
                   col01Left, false,
                   cu[0], cv[0], cv[2], 0.0f,
                         verts[0].x, verts[0].y, verts[0].z,
                         verts[2].x, verts[2].y, verts[2].z,
                         sY[0], sY[1], sY[2], sY[3]);
}

static void compute_uv_pair_z(SceneRendererGPU *r, const SceneRenderVertex verts[4],
                               int surfaceFlags, SceneTextureHandle texture, int surfIdx,
                               bool isBuilding, bool flipH, float uMaxN, float vMaxN,
                               float cu[4], float cv[4])
{
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
    /* Only sY is needed here (for the log call below); sX is unused in this branch. */
    float sY[4];
    for (int vi = 0; vi < 4; vi++) {
        float iz = fabsf(vZ[vi]) > 1e-6f ? 1.0f / vZ[vi] : 1.0f;
        sY[vi] = vY[vi] * iz;
    }

    bool effectiveFlipH = flipH;
    bool flipV = !isBuilding && (surfaceFlags & SURFACE_FLAG_FLIP_VERT) != 0;

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
    /* Diagnostic only (not yet used to change behavior): same face-normal
     * floor/wall classification already proven in GEN-BFHF (isFloorLike), to check
     * whether idx=14's row01IsBottom failures correlate with geometrically flat
     * (floor/ceiling-like) instances of this tile rather than true sloped walls. */
    {
        float dex1x = verts[1].x - verts[0].x, dex1y = verts[1].y - verts[0].y, dex1z = verts[1].z - verts[0].z;
        float dex2x = verts[3].x - verts[0].x, dex2y = verts[3].y - verts[0].y, dex2z = verts[3].z - verts[0].z;
        float dnrmX = dex1y*dex2z - dex1z*dex2y;
        float dnrmY = dex1z*dex2x - dex1x*dex2z;
        float dnrmZ = dex1x*dex2y - dex1y*dex2x;
        s_lastUV.isFloorLike = fabsf(dnrmZ) > sqrtf(dnrmX*dnrmX + dnrmY*dnrmY);
    }
    /* Diagnostic only (not yet used to change behavior): candidate world-space,
     * camera-orientation-invariant replacement for row01IsBottom/spansCameraCenter.
     * adjSY (below) is built from vY/vZ, i.e. the camera's CURRENT basis -- it rotates
     * with yaw/pitch, which is exactly why idx=14 flips from pure camera rotation with
     * no position change. World Z is a fixed axis regardless of camera orientation, so
     * wzRowBot should stay stable under rotation where row01IsBottom does not. Logged
     * alongside row01Bot to compare across every already-confirmed-working pair surface
     * before considering a switch. */
    {
        float rowZ01 = (verts[0].z + verts[1].z) * 0.5f;
        float rowZ23 = (verts[2].z + verts[3].z) * 0.5f;
        s_lastUV.wzRowBot = rowZ01 < rowZ23;
        bool camBelow01 = (r->camera.viewZ - rowZ01) < 0.0f;
        bool camBelow23 = (r->camera.viewZ - rowZ23) < 0.0f;
        s_lastUV.wzSpans = camBelow01 != camBelow23;
    }
    /* Clamp vZ to a minimum of 80 before the perspective divide here
     * (mirrors GEN-BFHF and SW's fProjectedZ clamp) -- the plain sX/sY
     * computed earlier only guards exact division-by-zero, so as a
     * vertex's vZ approaches a small value, gsx/gsy (and s_bcross's
     * sign) blow up and flip with no genuine backface change happening. */
    float s_bcross = 0.0f;
    if ((surfaceFlags & SURFACE_FLAG_FLIP_BACKFACE) != 0) {
        float gsx[4], gsy[4];
        for (int vi = 0; vi < 4; vi++) {
            float vZc = (vZ[vi] < 80.0f) ? 80.0f : vZ[vi];
            float giz = 1.0f / vZc;
            gsx[vi] = vX[vi] * giz;
            gsy[vi] = vY[vi] * giz;
        }
        s_bcross = (gsx[1]-gsx[0])*(gsy[3]-gsy[0])
                 - (gsy[1]-gsy[0])*(gsx[3]-gsx[0]);
    }
    /* SW set_starts(1): startsx[0]=startsx[3]=max, startsx[1]=startsx[2]=0.
     * polyt assigns U=max to the v0/v3 edge and U=0 to the v1/v2 edge
     * regardless of which world-side they're on; screen-space tile placement
     * then follows from polyt's winding-adaptive edge traversal.
     * pair03Left is used only by the BF screen-check above, not for U. */
    float u03 = uMaxN;
    float u12 = 0.0f;
    /* Real per-frame backface state (mirrors SW's POLYTEX cross-product
     * test and this file's own GEN-BFHF branch): start from the static
     * FLIP_HORIZ flip, then toggle it once if the polygon is currently
     * back-facing on screen. This replaces two earlier, flag-only
     * approximations (a plain FLIP_HORIZ swap, and an unconditional
     * "always swap for FLIP_BACKFACE" constant) that each worked for some
     * FLIP_BACKFACE surfaces and broke others (idx=180 needed the swap,
     * idx=158's exit wall did not), because neither was actually checking
     * the surface's real orientation -- only its flags. */
    bool bfBack = (s_bcross < 0.0f);
    if (effectiveFlipH != bfBack) {
        float tmp = u03; u03 = u12; u12 = tmp;
    }
    cu[0] = u03; cu[1] = u12; cu[2] = u12; cu[3] = u03;
    /* Screen-projected Y per vertex, used below for spansCameraCenter (both surface
     * types) and as the row01IsBottom formula itself for non-FLIP_BACKFACE pair
     * walls. Guard with vZ: when NEXT section (v0,v1) is behind the camera (vZ<0) in
     * reverse drive, sY = vY/vZ flips sign (floor verts below camera go positive).
     * Snapping behind-camera sY to -1e9 keeps spansCameraCenter consistent with the
     * forward-drive assignment. Recompute (not reuse sY[]) with the same 80-unit
     * near-camera clamp used for s_bcross's gsx/gsy above: sY[] only guards exact
     * division-by-zero (1e-6), so for a long wall segment with one vertex close to
     * the camera, vY/vZ still blows up and can flip sign well before vZ reaches
     * zero. */
    float adjSY[4];
    for (int vi = 0; vi < 4; vi++) {
        if (vZ[vi] <= 0.0f) { adjSY[vi] = -1e9f; continue; }
        float vZc = (vZ[vi] < 80.0f) ? 80.0f : vZ[vi];
        adjSY[vi] = vY[vi] / vZc;
    }
    /* row01IsBottom needs two different formulas depending on surface type:
     *
     * FLIP_BACKFACE pair walls (idx=14, tunnel lights, idx=180, idx=158): v0v1/v2v3
     * are a coincident FV/non-FV pair offset only by the wall's thickness, not a
     * true top/bottom pair -- both must give top=vMaxN so sign content stays visible
     * regardless of which surface wins the depth sort. The deciding question is
     * which edge faces the camera, i.e. which is nearer -- squared world-space
     * distance from camera to each edge's midpoint. The sign and the `backwards`
     * flip below were both fit to live test data, not derived analytically.
     *
     * Plain TEXTURE_PAIR walls without FLIP_BACKFACE (e.g. background buildings,
     * idx=96/72) only reach row-detection via spansCameraCenter, and ARE a genuine
     * top/bottom pair -- "nearer to camera" has no meaningful connection to world
     * top/bottom there (it just means "nearer to ground"), so these keep the
     * original screen-space `adjSY` formula. The `backwards` flip applied to it,
     * however, was itself the bug: every reverse-direction sample had
     * adjSum01>0/adjSum23<0 (the raw, unflipped test already gives the correct
     * answer), with `backwards=-1` forcing a flip to the wrong value -- i.e. this
     * flip was never a valid direction correction for this branch, it was copied
     * over from the FLIP_BACKFACE case without independent verification. Dropping it
     * (below) is correct in both directions. A pure world-Z comparison (no
     * `backwards` correction) was tried first and falsified -- kept the raw `adjSY`
     * formula instead, just without the flip. */
    float mx01 = (verts[0].x + verts[1].x) * 0.5f;
    float my01 = (verts[0].y + verts[1].y) * 0.5f;
    float mz01 = (verts[0].z + verts[1].z) * 0.5f;
    float mx23 = (verts[2].x + verts[3].x) * 0.5f;
    float my23 = (verts[2].y + verts[3].y) * 0.5f;
    float mz23 = (verts[2].z + verts[3].z) * 0.5f;
    float dx01 = r->camera.viewX - mx01, dy01 = r->camera.viewY - my01, dz01 = r->camera.viewZ - mz01;
    float dx23 = r->camera.viewX - mx23, dy23 = r->camera.viewY - my23, dz23 = r->camera.viewZ - mz23;
    float d01sq = dx01*dx01 + dy01*dy01 + dz01*dz01;
    float d23sq = dx23*dx23 + dy23*dy23 + dz23*dz23;
    bool row01IsBottom;
    if ((surfaceFlags & SURFACE_FLAG_FLIP_BACKFACE) != 0) {
        row01IsBottom = (d01sq > d23sq);
        if (backwards != 0) row01IsBottom = !row01IsBottom;
    } else {
        /* No `backwards` flip here -- see comment above. */
        row01IsBottom = (adjSY[0]+adjSY[1]) < (adjSY[2]+adjSY[3]);
    }
    s_lastUV.backwardsVal = backwards;
    s_lastUV.adjSum01 = adjSY[0]+adjSY[1];
    s_lastUV.adjSum23 = adjSY[2]+adjSY[3];
    s_lastUV.camX = r->camera.viewX;
    s_lastUV.camY = r->camera.viewY;
    s_lastUV.camZ = r->camera.viewZ;
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
    s_lastUV.useRowDetect = useRowDetect;
    s_lastUV.spansCC = spansCameraCenter;
    for (int k = 0; k < 4; k++) {
        bool isBottom = useRowDetect
                            ? (row01IsBottom ? (k==0||k==1) : (k==2||k==3))
                            : (k==2||k==3);
        cv[k] = (isBottom != effectiveFlipV) ? vMaxN : 0.0f;
    }
    s_lastUV.cv1 = cv[1];
    s_lastUV.cv3 = cv[3];
    surface_uv_log("PAIR-Z", texture, surfIdx, surfaceFlags,
                   flipV, effectiveFlipV, effectiveFlipH,
                   pair03Left, row01IsBottom,
                   cu[0], cv[0], cv[2], s_bcross,
                         verts[0].x, verts[0].y, verts[0].z,
                         verts[2].x, verts[2].y, verts[2].z,
                         sY[0], sY[1], sY[2], sY[3]);
}

static void compute_uv_cloud(int surfaceFlags, bool flipH, float uMaxN, float vMaxN,
                              float cu[4], float cv[4])
{
    /* Cloud quads: fixed vertex-index UV matching SW's set_starts(0):
     *   v0,v3 → U=uMaxN; v1,v2 → U=0   (swapped if flipH)
     *   v0,v1 → V per flipV; v2,v3 → complement
     * The earlier screen-space pair03IsLeft U detection caused an abrupt
     * H-mirror at ~90°/270° camera roll: as the quad rolled, the pair sum
     * threshold crossed and cu flipped (uMax,0,0,uMax)↔(0,uMax,uMax,0).
     * SW (set_starts) never adapts UV to camera roll — it is always fixed
     * by vertex index — so this detection was wrong. */
    bool flipV = (surfaceFlags & SURFACE_FLAG_FLIP_VERT) != 0;
    cu[0] = flipH ? 0.0f : uMaxN;
    cu[1] = flipH ? uMaxN : 0.0f;
    cu[2] = flipH ? uMaxN : 0.0f;
    cu[3] = flipH ? 0.0f : uMaxN;
    for (int k = 0; k < 4; k++) {
        bool isBottom = (k == 2 || k == 3);
        cv[k] = (isBottom != flipV) ? vMaxN : 0.0f;
    }
}

static void compute_uv_building(const SceneRenderVertex verts[4], int surfaceFlags,
                                 SceneTextureHandle texture, int surfIdx,
                                 bool flipH, float uMaxN, float vMaxN,
                                 float cu[4], float cv[4])
{
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
}

static void compute_uv_sign(const SceneRenderVertex verts[4], int surfaceFlags,
                             SceneTextureHandle texture, int surfIdx,
                             bool flipH, float uMaxN, float vMaxN,
                             float cu[4], float cv[4])
{
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
}

static void compute_uv_wall_fallback(const SceneRenderVertex verts[4], int surfaceFlags,
                                      SceneTextureHandle texture, int surfIdx,
                                      bool flipH, float uMaxN, float vMaxN,
                                      float cu[4], float cv[4])
{
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
}

static void compute_uv_gen(SceneRendererGPU *r, const SceneRenderVertex verts[4],
                           int surfaceFlags, SceneTextureHandle texture, int surfIdx,
                           bool flipH, float uMaxN, float vMaxN,
                           float cu[4], float cv[4])
{
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
        /* FH+BF gen surfaces (e.g. corkscrew outer guardrail panels): a
         * screen-space test mirroring SW's POLYTEX backface-swap-driven U
         * toggle would decide which face the camera is on from the current
         * projected geometry -- but on a twisted/rolled structure like this,
         * that projection isn't a reliable proxy for "which face" the way it
         * is for a flat wall, so it can flip between drive directions even
         * when the underlying quad hasn't changed sides at all (the same
         * fragility class as wallFrontFacing elsewhere in this file). Nor is
         * the CONCAVE flag a reliable stand-in for "needs the camera-relative
         * test": adjacent panels of the same physical guardrail can carry
         * different CONCAVE flags despite being geometrically the same kind
         * of surface, since track authoring doesn't guarantee the flag is
         * set consistently along a continuous twisted structure. And no
         * single world-space coordinate axis works either -- which axis
         * reads as "left/right" rotates with the structure's twist, so a
         * fixed axis is only ever valid at one point along it.
         *
         * Bypass the whole camera-relative test instead, the same way the
         * PAIR-X branch already bypasses col01Left for horizontal/CONCAVE
         * surfaces: assign U purely from vertex index + the static flipH
         * flag, identical to the plain non-FH/non-BF case below. This is
         * fully position- and orientation-independent (no vertex coordinate
         * is read at all), so it can't be "wrong from one direction" the way
         * any camera-relative or world-space-axis test can be for a twisted
         * surface. Note: a genuinely flat, single-sided sign-wall (as opposed
         * to a twisted structure) can still need the real camera-relative
         * test to show its content from both sides -- if one is ever found
         * broken under this fixed assignment, that's a real case for
         * re-introducing a camera-relative test, gated to flat walls only. */
        for (int k = 0; k < 4; k++) {
            int sk = k;
            static const int hm[4] = {1,0,3,2};
            sk = hm[sk];  /* flipH is always true to reach this branch */
            cu[k] = (sk == 0 || sk == 3) ? uMaxN : 0.0f;
        }
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

/* --------------------------------------------------------------------------
 * "Pick Textures as PNG" debug export
 *
 * Writes the atlas region for a picked surface's tile (or tile pair) out as a
 * PNG next to the exe, in a TEXTURES/ subfolder, prefixed "debug_" -- lets a
 * picked surface's actual texture content be inspected directly instead of
 * inferred from UV/behavior alone. Mirrors the exact pair-tile layout used
 * when building pairTextures[] in scene_render_gpu_load_texture (tile N+1
 * read at its natural row-major position, wrapping to the next row's first
 * column when N is the last column in its row -- matches SW's real flat-
 * memory read exactly).
 * -------------------------------------------------------------------------- */
static void save_picked_texture_png(const SceneGPUTextureSlot *slot, int surfIdx, bool isPair)
{
    if (!slot || !slot->atlasRgbaCopy) return;
    if (surfIdx < 0 || surfIdx >= slot->numTiles) return;

    int tileSize    = slot->tileSize;
    int tilesPerRow = slot->tilesPerRow;
    int atlasW      = slot->atlasWidth;
    int col_n       = surfIdx % tilesPerRow;
    int row_n       = surfIdx / tilesPerRow;
    int outW        = isPair ? 2 * tileSize : tileSize;

    uint8 *buf = malloc((size_t)outW * tileSize * 4);
    if (!buf) return;

    /* Matches SW's real flat-memory read exactly (see the comment above the
     * pairTextures[] build in scene_render_gpu_load_texture): non-last-column
     * tiles map cleanly onto tile N+1's own block, but a last-column tile's
     * "right" read is actually tile (row_n, col=0) shifted down one row,
     * with only the bottom row spilling into tile (row_n+1, col=0). */
    int col_r = (surfIdx + 1) % tilesPerRow;
    int row_r = (surfIdx + 1) / tilesPerRow;
    bool lastCol = (col_n == tilesPerRow - 1);
    for (int y = 0; y < tileSize; y++) {
        int srcRowL = row_n * tileSize + y;
        int srcRowR, srcColR;
        if (lastCol) {
            if (y < tileSize - 1) { srcRowR = row_n * tileSize + (y + 1); srcColR = 0; }
            else                  { srcRowR = (row_n + 1) * tileSize;    srcColR = 0; }
        } else {
            srcRowR = row_r * tileSize + y;
            srcColR = col_r * tileSize;
        }
        for (int x = 0; x < tileSize; x++) {
            size_t srcOff = ((size_t)srcRowL * atlasW + col_n * tileSize + x) * 4;
            size_t dstOff = ((size_t)y * outW + x) * 4;
            memcpy(&buf[dstOff], &slot->atlasRgbaCopy[srcOff], 4);
        }
        if (isPair) {
            for (int x = 0; x < tileSize; x++) {
                size_t srcOff = ((size_t)srcRowR * atlasW + srcColR + x) * 4;
                size_t dstOff = ((size_t)y * outW + tileSize + x) * 4;
                memcpy(&buf[dstOff], &slot->atlasRgbaCopy[srcOff], 4);
            }
        }
    }

    const char *base = SDL_GetBasePath();
    char dir[512];
    SDL_snprintf(dir, sizeof(dir), "%sTEXTURES", base ? base : "");
    SDL_CreateDirectory(dir);

    char path[560];
    SDL_snprintf(path, sizeof(path), "%s/debug_tile_%d%s.png", dir, surfIdx, isPair ? "_pair" : "");
    int rc = RollerWriteRgbaPng(path, buf, outW, tileSize);
    bool lastColLog = isPair && (col_n == tilesPerRow - 1);
    if (rc == 0)
        SDL_Log("PICK-PNG: wrote %s (%dx%d) col=%d row=%d tilesPerRow=%d lastCol=%d",
                path, outW, tileSize, col_n, row_n, tilesPerRow, (int)lastColLog);
    else
        SDL_Log("PICK-PNG: FAILED to write %s (rc=%d)", path, rc);

    free(buf);
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
    if (scene_render_gpu_quad_frustum_culled(r, verts)) return;
    s_lastUV.valid = false;
    s_lastUV.isFloorLike = false;
    s_lastUV.wzRowBot = false;
    s_lastUV.wzSpans = false;
    s_lastUV.useRowDetect = false;
    s_lastUV.backwardsVal = 0;
    s_lastUV.adjSum01 = 0.0f;
    s_lastUV.adjSum23 = 0.0f;
    s_lastUV.spansCC = false;
    s_lastUV.camX = 0.0f;
    s_lastUV.camY = 0.0f;
    s_lastUV.camZ = 0.0f;
    s_lastUV.backTexApplied = false;
    s_lastUV.backDot = 0.0f;
    s_lastUV.backOrigIdx = -1;
    s_lastUV.backNewIdx = -1;
    s_lastUV.uWindingFlip = false;
    s_lastUV.uWindingArea = 0.0f;
    s_lastUV.vSwap = false;
    s_lastUV.vNrmY = 0.0f;
    s_lastUV.splitAValid = true;
    s_lastUV.splitArea012 = 0.0f;
    s_lastUV.splitArea023 = 0.0f;
    s_lastUV.cv1 = 0.0f;
    s_lastUV.cv3 = 0.0f;

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
    bool isTrackDarken = false;
    int  surfIdx     = surfaceFlags & SURFACE_MASK_TEXTURE_INDEX;

    /* SW applies texture_back[] substitution when SURFACE_FLAG_BACK (0x800) is set and the
     * quad is back-facing, swapping in a completely different tile (e.g. the advertisement
     * face of a sign board).
     *
     * SW's real trigger (polytex.c:536-554) is a 2D screen-space cross-product winding test on
     * the projected vertices ((v0-v1) x (v0-v2), falling back to v3 if v1==v2 is degenerate) --
     * not a 3D world-space face-normal dot product. A fixed-vertex-order world-space normal
     * doesn't track the current screen-space winding, so it misfires on twisted/rotated
     * geometry whose face normal doesn't point "the expected way" for its winding. Ported
     * directly from the decompiled source, with the Y-flip (GPU view-space Y-up vs SW
     * screen-space Y-down) and the 80-unit near-camera clamp used throughout this file for
     * screen-space tests. */
    bool isBackTexSign = false;
    if ((surfaceFlags & SURFACE_FLAG_BACK) && slot) {
        /* Gated to isFloorLike: applying the screen-space test unconditionally regressed
         * genuinely-vertical walls. Some twisted/rotated CONCAVE quads have a face normal
         * that's "floor-like" (nrmZ-dominant) despite looking like a vertical wall, and flags
         * alone can't distinguish the two cases. Non-floor-like quads keep the original
         * world-space face-normal dot product, which has never been reported broken for
         * actual walls. */
        float ex1x = verts[1].x - verts[0].x, ex1y = verts[1].y - verts[0].y, ex1z = verts[1].z - verts[0].z;
        float ex2x = verts[3].x - verts[0].x, ex2y = verts[3].y - verts[0].y, ex2z = verts[3].z - verts[0].z;
        float bnrmX = ex1y*ex2z - ex1z*ex2y;
        float bnrmY = ex1z*ex2x - ex1x*ex2z;
        float bnrmZ = ex1x*ex2y - ex1y*ex2x;
        bool backIsFloorLike = fabsf(bnrmZ) > sqrtf(bnrmX*bnrmX + bnrmY*bnrmY);

        float cross;
        if (backIsFloorLike) {
            const float (*M)[3] = r->proj.view;
            float gsx[4], gsy[4];
            for (int vi = 0; vi < 4; vi++) {
                float ddx = verts[vi].x - r->camera.viewX;
                float ddy = verts[vi].y - r->camera.viewY;
                float ddz = verts[vi].z - r->camera.viewZ;
                float vx = ddx*M[0][0] + ddy*M[1][0] + ddz*M[2][0];
                float vy = ddx*M[0][1] + ddy*M[1][1] + ddz*M[2][1];
                float vz = ddx*M[0][2] + ddy*M[1][2] + ddz*M[2][2];
                float vzc = (vz < 80.0f) ? 80.0f : vz;
                gsx[vi] = vx / vzc;
                gsy[vi] = -(vy / vzc);
            }
            bool v1eqv2 = (gsx[1] == gsx[2] && gsy[1] == gsy[2]);
            int refIdx = v1eqv2 ? 3 : 2;
            cross = (gsx[0]-gsx[1])*(gsy[0]-gsy[refIdx]) - (gsy[0]-gsy[1])*(gsx[0]-gsx[refIdx]);
        } else {
            /* Original world-space face-normal dot product (kept for non-floor-like quads). */
            cross = bnrmX*(r->camera.viewX - verts[0].x)
                  + bnrmY*(r->camera.viewY - verts[0].y)
                  + bnrmZ*(r->camera.viewZ - verts[0].z);
        }
        s_lastUV.backDot = cross;
        s_lastUV.backOrigIdx = surfIdx;
        /* Deadzone around zero: SW computes this test on actual integer screen pixels at its
         * native low resolution, which stabilizes borderline cases our continuous-float
         * screen-space proxy doesn't reproduce -- the same quad's cross value can land on
         * opposite sides of zero at a razor-thin magnitude (~0.1) between drive directions,
         * flip-flopping the substitution even though SW shows the same texture both ways.
         * Treating small-magnitude crossings as "not clearly back-facing" avoids that; the
         * threshold is a judgment call, not derived. Only needed for the floor-like/screen-space
         * path -- the world-space path for walls never needed one. */
        if (backIsFloorLike ? (cross < -1.0f) : (cross < 0.0f)) {
            int newType = texture_back[256 * slot->tex_idx + surfIdx];
            int newIdx  = newType & SURFACE_MASK_TEXTURE_INDEX;
            if (newIdx >= 0 && newIdx < slot->numTiles) {
                surfIdx = newIdx;
                isBackTexSign = true;
            }
        }
        s_lastUV.backTexApplied = isBackTexSign;
        s_lastUV.backNewIdx = surfIdx;
    }

    /* Wall pair textures only apply to non-building track surfaces. */
    bool isWall = !isBuilding && (surfaceFlags & SURFACE_FLAG_TEXTURE_PAIR) && wide_on;

    /* No CPU backface cull for track surfaces. SW's scan-line renderer implicitly
     * culls back-facing polygons (reversed winding -> empty scanline spans); a GPU
     * screen-space cross-product equivalent was tried but was unreliable near the
     * near-plane (clamping artefacts flipped the sign on valley/slope surfaces) and
     * incorrectly hid roads seen from below. Track surfaces are thin single-face
     * quads, so rendering both sides is harmless -- the depth buffer handles
     * ordering. Buildings/signs are still culled earlier by building.c. SW itself
     * also bypasses its own facing_ok check for CONCAVE outer-wall sections
     * (LLOWALL/LUOWALL/RLOWALL/RUOWALL in drawtrk3.c), rendering them regardless of
     * winding, consistent with not culling them here either. (This is not the source
     * of the black divider lines seen in the looping road in SW -- a click-to-pick
     * diagnostic found no separate quad at those positions in either renderer;
     * concluded to be a SW rasterizer artifact.) */

    /* wallFrontFacing (front/back suppression of the pair texture) was removed.
     * It used to check which side of a face normal -- built from just 3 of the
     * quad's 4 vertices (v0,v1,v2) -- the camera was on, falling back to a single
     * plain tile when it read as "behind" the wall plane. That 3-vertex normal is
     * unreliable for any twisted/rolled track structure: gating it off for
     * isFloorLike quads fixed one corkscrew instance (idx=8), gating it off for all
     * CONCAVE quads fixed a residual instance of the same tile, but a THIRD instance
     * on a different track (idx=24, not CONCAVE, also not isFloorLike by this test)
     * proved neither exemption was enough. Removed the suppression entirely instead
     * of adding a fourth exemption -- same reasoning as the CPU backface-cull
     * removal above (thin single-face quads render fine from both sides) -- unless
     * a real single-sided sign-wall is ever found to depend on the plain-color
     * fallback, which hasn't been confirmed to exist in this codebase. */
    bool wallFrontFacing = true;

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
            /* We only reach here because the real atlas texture lookup above
             * failed (gpuTex is still NULL) -- this is NOT how SW normally
             * renders PARTIAL_TRANS+APPLY_TEXTURE surfaces (that's polyt() in
             * polytex.c, a real textured/colorkey-transparent render). This is
             * just a graceful degradation for the "texture not loaded" case:
             * fall through to the flat-color path below instead of skipping
             * the surface entirely, which is a reasonable approximation but
             * won't exactly match polyt()'s appearance. */
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
            /* NOT a car shadow -- real car shadows never reach this function
             * (they use carShadowDraws[]/carShadowPipeline, a separate mesh-
             * based system, or the legacy 2D screen-quad path via
             * game_render_quad_screen). A TRANSPARENT world quad with no
             * texture reaching here is drawtrk3.c/building.c track geometry
             * (glass walls, loop-section divider walls) that SW renders via
             * shadow_poly() (polyf.c) -- a per-pixel palette-darken remap of
             * whatever's already on screen, NOT a blend toward a fixed colour.
             * Approximate it with a multiply-blend pass instead of the old
             * shadowTex/SHADOW routing -- that fixed-alpha blend under-darkened
             * these surfaces badly enough that they were effectively
             * invisible. */
            int shadeLevel = surfIdx;
            if (shadeLevel < 0) shadeLevel = 0;
            if (shadeLevel >= SCENE_GPU_SHADE_LEVELS) shadeLevel = SCENE_GPU_SHADE_LEVELS - 1;
            gpuTex = r->shadeLevelTex[shadeLevel];
            isTrackDarken = true;
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
    bool isTree = (surfaceFlags & SURFACE_FLAG_GPU_IS_TREE) != 0
              && (options.subdivideType == SCENE_RENDER_SUBDIVIDE_TYPE_BUILDING
                  || options.subdivideType == SCENE_RENDER_SUBDIVIDE_TYPE_SIGN);
    SceneGPUDrawKind kind;
    if (isTree)
        kind = SCENE_GPU_DRAW_TREE;
    else if (isSign)
        kind = g_bSignsOnTop ? SCENE_GPU_DRAW_SIGN : SCENE_GPU_DRAW_BUILDING;
    else if (isBackTexSign)
        kind = SCENE_GPU_DRAW_SIGN_BK;
    else if (gpuTex == r->shadowTex)
        kind = SCENE_GPU_DRAW_SHADOW;
    else if (isTrackDarken)
        kind = SCENE_GPU_DRAW_TRACK_DARKEN;
    else if (surfaceFlags & SURFACE_FLAG_TRANSPARENT)
        kind = SCENE_GPU_DRAW_BLEND;
    else if ((surfaceFlags & SURFACE_FLAG_PARTIAL_TRANS) && (surfaceFlags & SURFACE_FLAG_FLIP_BACKFACE))
        kind = SCENE_GPU_DRAW_BF_BUILDING;
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
    if (isTrackDarken && r->emulateSoftwareTrackDarkenBorder) {
        cu[0] = 0.0f; cv[0] = 0.0f;
        cu[1] = 1.0f; cv[1] = 0.0f;
        cu[2] = 1.0f; cv[2] = 1.0f;
        cu[3] = 0.0f; cv[3] = 1.0f;
    } else if (isFlatColor) {
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

        /* Dispatch to the per-case UV function -- see each compute_uv_* above
         * for that case's own logic and comments. */
        if (usePair) {
            /* Both wall families use the same Standard winding: v0=TL, v1=TR, v2=BR, v3=BL.
             * The world-Z discriminant distinguishes them:
             *   Z-facing (Standard branch, layoutB=false): wall runs along world-X; all four
             *     vertices share the same Z → |v0.z-v3.z|=0, |v0.z-v1.z|=0 → 0<0 = false.
             *   X-facing (Layout B branch, layoutB=true): wall runs along world-Z; the left
             *     column {v0=TL,v3=BL} shares one Z and the right column {v1=TR,v2=BR} shares
             *     another → |v0.z-v3.z|=0, |v0.z-v1.z|>0 → 0<dz = true. */
            bool layoutB = fabsf(verts[0].z - verts[3].z) < fabsf(verts[0].z - verts[1].z);
            if (layoutB)
                compute_uv_pair_x(r, verts, surfaceFlags, texture, surfIdx,
                                   isBuilding, flipH, uMaxN, vMaxN, cu, cv);
            else
                compute_uv_pair_z(r, verts, surfaceFlags, texture, surfIdx,
                                   isBuilding, flipH, uMaxN, vMaxN, cu, cv);
        } else if (isCloud) {
            compute_uv_cloud(surfaceFlags, flipH, uMaxN, vMaxN, cu, cv);
        } else if (isBuilding) {
            compute_uv_building(verts, surfaceFlags, texture, surfIdx, flipH, uMaxN, vMaxN, cu, cv);
        } else if (isSign) {
            compute_uv_sign(verts, surfaceFlags, texture, surfIdx, flipH, uMaxN, vMaxN, cu, cv);
        } else if (isWall) {
            compute_uv_wall_fallback(verts, surfaceFlags, texture, surfIdx, flipH, uMaxN, vMaxN, cu, cv);
        } else {
            compute_uv_gen(r, verts, surfaceFlags, texture, surfIdx, flipH, uMaxN, vMaxN, cu, cv);
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
            /* Vertical center correction, matching build_mvp's shift_y exactly:
             * SW's horizon sits at ss*(199-centerY) screen rows from the top, not
             * necessarily at vpH/2. The simplified (1 - fovY*vY/vZ)*0.5 formula
             * below implicitly assumes the horizon IS at vpH/2, so without this
             * offset labels and pick outlines land consistently above the true
             * geometry. */
            float horizon_y = ss * (199.0f - (float)r->proj.centerY);
            float shiftY     = ((float)vpH * 0.5f - horizon_y) / ((float)vpH * 0.5f);

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
                    float ny = (1.0f - fovY * vY * iz) * 0.5f - shiftY * 0.5f;
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
                /* Project each vertex individually and test the quad's two
                 * triangles (0,1,2) and (0,2,3) independently, rather than
                 * rejecting the whole quad if any one of the 4 vertices dips
                 * behind the near plane. A large quad viewed at an oblique
                 * angle (common during free-look testing) can easily have one
                 * corner behind the camera while the rest -- and the point
                 * actually being clicked -- are comfortably in front; the old
                 * all-or-nothing reject silently excluded the whole quad from
                 * ever being pickable in that case, even though its debug
                 * label (which only projects the quad's center) kept showing. */
                float pnx[4], pny[4], pvZ[4];
                bool front[4];
                for (int vi = 0; vi < 4; vi++) {
                    float ddxi = verts[vi].x - r->camera.viewX;
                    float ddyi = verts[vi].y - r->camera.viewY;
                    float ddzi = verts[vi].z - r->camera.viewZ;
                    float vZi  = ddxi*M[0][2] + ddyi*M[1][2] + ddzi*M[2][2];
                    pvZ[vi]   = vZi;
                    front[vi] = (vZi >= 0.5f);
                    if (front[vi]) {
                        float vXi  = ddxi*M[0][0] + ddyi*M[1][0] + ddzi*M[2][0];
                        float vYi  = ddxi*M[0][1] + ddyi*M[1][1] + ddzi*M[2][1];
                        /* Clamp to a minimum of 80 before the divide (mirrors the
                         * same fix applied to s_bcross elsewhere in this file) --
                         * a vertex close to the camera but not technically behind
                         * it can still blow up 1/vZi, distorting just that one
                         * corner's screen position (visible as a collapsed edge in
                         * the pick outline for a wall near the camera). */
                        float vZic = (vZi < 80.0f) ? 80.0f : vZi;
                        float izi  = 1.0f / vZic;
                        pnx[vi] = (1.0f + fovX * vXi * izi) * 0.5f;
                        pny[vi] = (1.0f - fovY * vYi * izi) * 0.5f - shiftY * 0.5f;
                    }
                }
                bool tri1Ok = front[0] && front[1] && front[2];
                bool tri2Ok = front[0] && front[2] && front[3];
                if (tri1Ok || tri2Ok) {
                    float qx = g_clickQueryNX, qy = g_clickQueryNY;
                    bool hit =
                        (tri1Ok && s_pt_in_tri(qx, qy, pnx[0], pny[0], pnx[1], pny[1], pnx[2], pny[2])) ||
                        (tri2Ok && s_pt_in_tri(qx, qy, pnx[0], pny[0], pnx[2], pny[2], pnx[3], pny[3]));
                    float vZsum = 0.0f;
                    int   vZn   = 0;
                    for (int vi = 0; vi < 4; vi++) {
                        if (front[vi]) { vZsum += pvZ[vi]; vZn++; }
                    }
                    float vZavg = (vZn > 0) ? (vZsum / (float)vZn) : 0.0f;
                    if (hit && (!s_clickHit.active || vZavg < s_clickHit.vZ)) {
                        s_clickHit.active      = true;
                        s_clickHit.surfIdx     = surfIdx;
                        s_clickHit.surfaceFlags = surfaceFlags;
                        SDL_snprintf(s_clickHit.path, sizeof(s_clickHit.path), "%s", pPath);
                        s_clickHit.vZ          = vZavg;
                        s_clickHit.v0x = verts[0].x; s_clickHit.v0y = verts[0].y; s_clickHit.v0z = verts[0].z;
                        s_clickHit.v1x = verts[1].x; s_clickHit.v1y = verts[1].y; s_clickHit.v1z = verts[1].z;
                        s_clickHit.v2x = verts[2].x; s_clickHit.v2y = verts[2].y; s_clickHit.v2z = verts[2].z;
                        s_clickHit.v3x = verts[3].x; s_clickHit.v3y = verts[3].y; s_clickHit.v3z = verts[3].z;
                        for (int vi = 0; vi < 4; vi++) {
                            s_clickHit.onx[vi]    = pnx[vi];
                            s_clickHit.ony[vi]    = pny[vi];
                            s_clickHit.ovalid[vi] = front[vi];
                        }
                        s_clickHit.uvValid = s_lastUV.valid;
                        if (s_lastUV.valid) {
                            SDL_strlcpy(s_clickHit.uvType, s_lastUV.type, sizeof(s_clickHit.uvType));
                            s_clickHit.uvTexId  = s_lastUV.texId;
                            s_clickHit.flipV    = s_lastUV.flipV;
                            s_clickHit.efV      = s_lastUV.efV;
                            s_clickHit.efH      = s_lastUV.efH;
                            s_clickHit.p03L     = s_lastUV.p03L;
                            s_clickHit.row01Bot = s_lastUV.row01Bot;
                            s_clickHit.cu0      = s_lastUV.cu0;
                            s_clickHit.cv0      = s_lastUV.cv0;
                            s_clickHit.cv2      = s_lastUV.cv2;
                            s_clickHit.bcross   = s_lastUV.bcross;
                            s_clickHit.isFloorLike = s_lastUV.isFloorLike;
                            s_clickHit.wzRowBot = s_lastUV.wzRowBot;
                            s_clickHit.wzSpans  = s_lastUV.wzSpans;
                            s_clickHit.useRowDetect = s_lastUV.useRowDetect;
                            s_clickHit.backwardsVal = s_lastUV.backwardsVal;
                            s_clickHit.adjSum01 = s_lastUV.adjSum01;
                            s_clickHit.adjSum23 = s_lastUV.adjSum23;
                            s_clickHit.spansCC  = s_lastUV.spansCC;
                            s_clickHit.camX = s_lastUV.camX;
                            s_clickHit.camY = s_lastUV.camY;
                            s_clickHit.camZ = s_lastUV.camZ;
                            s_clickHit.backTexApplied = s_lastUV.backTexApplied;
                            s_clickHit.backDot = s_lastUV.backDot;
                            s_clickHit.backOrigIdx = s_lastUV.backOrigIdx;
                            s_clickHit.backNewIdx = s_lastUV.backNewIdx;
                            s_clickHit.uWindingFlip = s_lastUV.uWindingFlip;
                            s_clickHit.uWindingArea = s_lastUV.uWindingArea;
                            s_clickHit.vSwap = s_lastUV.vSwap;
                            s_clickHit.vNrmY = s_lastUV.vNrmY;
                            s_clickHit.splitAValid  = s_lastUV.splitAValid;
                            s_clickHit.splitArea012 = s_lastUV.splitArea012;
                            s_clickHit.splitArea023 = s_lastUV.splitArea023;
                            s_clickHit.cv1 = s_lastUV.cv1;
                            s_clickHit.cv3 = s_lastUV.cv3;
                        }
                    }
                }
            }
        }
    }

    if (r->vertexCount + 6 > SCENE_GPU_MAX_VERTICES) return;

    /* Near-plane fix (part 2 of 2): push each vertex forward along the camera
     * forward direction until fVz >= SCENE_GPU_NEAR.  Part 1 (backface check
     * above) stops near-camera quads from being incorrectly culled; this part
     * stops the GPU from hardware-clipping the surviving near-camera vertices.
     * The SW renderer does fWorldZ = max(fWorldZ, NEAR) per vertex, keeping
     * camera-space X and Y unchanged — pushing along the camera forward axis
     * in world space is mathematically identical.  Only applied when fVz > 0
     * to avoid pushing behind-camera geometry (which the GPU discards safely). */
    float fwd_x = r->proj.view[0][2];   /* vk3 — camera forward in world X */
    float fwd_y = r->proj.view[1][2];   /* vk6 — camera forward in world Y */
    float fwd_z = r->proj.view[2][2];   /* vk9 — camera forward in world Z */
    float cx = r->camera.viewX, cy = r->camera.viewY, cz = r->camera.viewZ;
    float px[4], py[4], pz[4];
    for (int k = 0; k < 4; k++) {
        px[k] = verts[k].x; py[k] = verts[k].y; pz[k] = verts[k].z;
        float fVz = fwd_x*(px[k]-cx) + fwd_y*(py[k]-cy) + fwd_z*(pz[k]-cz);
        if (fVz > 0.0f && fVz < SCENE_GPU_NEAR) {
            float delta = SCENE_GPU_NEAR - fVz;
            px[k] += delta * fwd_x;
            py[k] += delta * fwd_y;
            pz[k] += delta * fwd_z;
        }
    }

    int base = r->vertexCount;
    static const int orderA[6] = {0,1,2, 0,2,3};
    static const int orderB[6] = {1,2,3, 1,3,0};
    /* Diagonal decision computed earlier (isFloorLike-gated CONCAVE block above)
     * so it lands in the same PICK-UV snapshot as the rest of this quad's
     * diagnostics -- s_lastUV.splitAValid defaults to true (reset at top of
     * function) for every quad that doesn't go through that gated computation,
     * so this is a no-op for anything other than isFloorLike CONCAVE quads. */
    const int *order = s_lastUV.splitAValid ? orderA : orderB;
    for (int i = 0; i < 6; i++) {
        int k = order[i];
        r->vertices[base+i] = (SceneGPUVertex){
            px[k], py[k], pz[k],
            cu[k], cv[k]
        };
    }

    /* No per-quad MVP here: world-space quads use view+projection only (no
     * per-object model matrix, vertices are already absolute world-space),
     * which is identical for every quad in one view/frame -- it's pushed
     * once per view instead (see scene_render_gpu_end_frame/
     * scene_render_gpu_flush_secondary_view). This used to rebuild the full
     * matrix and memcmp it against the last draw command on every single
     * quad; both were redundant CPU work at typical (let alone unlimited)
     * draw distances. */
    SceneGPUDrawCmd *last = r->drawCmdCount > 0 ? &r->drawCmds[r->drawCmdCount-1] : NULL;
    bool batch = last && last->texture == gpuTex && last->kind == kind
              && last->forceNearest == isCloud;
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

void scene_render_gpu_queue_car_shadow_draw(SceneRendererGPU *r,
                                            SDL_GPUBuffer *vertBuf,
                                            SDL_GPUBuffer *idxBuf,
                                            SDL_GPUTexture *texture,
                                            int firstIndex,
                                            int idxCount,
                                            const float mvp[16])
{
    if (!r || r->carShadowDrawCount >= SCENE_GPU_MAX_CAR_DRAWS) return;
    SceneGPUCarDrawCmd *cmd = &r->carShadowDraws[r->carShadowDrawCount++];
    cmd->vertBuf    = vertBuf;
    cmd->idxBuf     = idxBuf;
    cmd->texture    = texture;
    cmd->firstIndex = firstIndex;
    cmd->idxCount   = idxCount;
    memcpy(cmd->mvp, mvp, 16 * sizeof(float));
}
