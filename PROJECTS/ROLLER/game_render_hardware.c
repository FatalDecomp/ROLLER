#include "game_render_hardware.h"
#include "game_render.h"
#include "roller.h"
#include "scene_render_gpu.h"
#include "carplans.h"    /* CarDesigns[], tPolygon, tCarDesign, car_flat_remap[] */
#include "car.h"         /* car_texmap[], CarBox, CarPol, team_col, driver_names, CarBox */
#include "graphics.h"    /* num_textures[] */
#include "3d.h"          /* cartex_vga[], Car[], localdata[], CarBox, g_pGameRenderer, names_on, winw/winh, viewx/y/z, xbase/ybase, scr_size, VIEWDIST, tcos/tsin, mirror, intro */
#include "transfrm.h"    /* vk1-vk9 */
#include "drawtrk3.h"    /* NamesLeft */
#include "func2.h"       /* mini_prt_string, language_buffer, screen_pointer */
#include "frontend.h"    /* human_control, racers */
#include "moving.h"      /* replaytype */

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdbool.h>
#include <limits.h>

extern tColor palette[256];

static inline int hw_d2i(double d) {
    if (d >= (double)2147483647) return 2147483647;
    if (d <= (double)(-2147483647-1)) return (-2147483647-1);
    return (int)d;
}

#define GAME_RENDER_HW_NUM_CARS      16
#define GAME_RENDER_HW_MAX_CAR_DRAWS 32  /* 16 cars × up to 2 draws each */
#define GRHW_ANIM_FRAMES             4   /* wheel animation frames 0-3 */

/* --------------------------------------------------------------------------
 * Vertex layout matching SceneGPUMeshVertex in menu_render_gpu.c (same
 * shaders used for car pipeline in scene_render_gpu.c).
 * -------------------------------------------------------------------------- */
typedef struct {
    float position[3];
    float uv[2];
    float color[4];
} GRHWMeshVertex;

/* --------------------------------------------------------------------------
 * Per-car mesh cache entry
 * -------------------------------------------------------------------------- */
typedef struct {
    SDL_GPUBuffer  *vertexBufs[GRHW_ANIM_FRAMES]; /* one per wheel animation frame */
    SDL_GPUBuffer  *indexBuf;                      /* shared across all frames */
    SDL_GPUTexture *atlas;
    int             indexCount;
    int             bodyIndexCount;
    bool            built;
} GRHWCarMesh;

struct GameRendererHardware {
    SDL_GPUDevice *device;
    GRHWCarMesh    meshes[GAME_RENDER_HW_NUM_CARS];
};

/* Smoothed scrY for each car's name tag (EMA filter, -1 = uninitialised). */
static int s_tagScrY[GAME_RENDER_HW_NUM_CARS] = {
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
};

/* ==========================================================================
 * Internal helpers (copies of upload_rgba / upload_gpu_buffer /
 * indexed_to_rgba from scene_render_gpu.c — kept static here).
 * ========================================================================== */

static SDL_GPUTexture *hw_upload_rgba(SDL_GPUDevice *dev,
                                      const uint8 *rgba, int w, int h)
{
    SDL_GPUTextureCreateInfo ti = {0};
    ti.type        = SDL_GPU_TEXTURETYPE_2D;
    ti.format      = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ti.width       = (Uint32)w;
    ti.height      = (Uint32)h;
    ti.layer_count_or_depth = 1;
    ti.num_levels  = 1;
    ti.usage       = SDL_GPU_TEXTUREUSAGE_SAMPLER;
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
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(dev, tb);
    return tex;
}

static SDL_GPUBuffer *hw_upload_gpu_buffer(SDL_GPUDevice *dev,
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

/* ==========================================================================
 * Car texture atlas
 * ========================================================================== */
static SDL_GPUTexture *build_car_atlas(SDL_GPUDevice *dev, int carIdx,
                                        const tColor *pal,
                                        int *outNumTiles, int *outAtlasH)
{
    int texSlot = car_texmap[carIdx];
    if (texSlot <= 0) return NULL;
    uint8 *texData = cartex_vga[texSlot - 1];
    if (!texData) return NULL;
    int nTiles = num_textures[texSlot - 1];
    if (nTiles <= 0) return NULL;

    int numRows = (nTiles + 3) / 4;
    int atlasW  = 256;
    int atlasH  = numRows * 64 + 1;   /* +1 row for white pixel */

    uint8 *rgba = calloc((size_t)atlasW * atlasH * 4, 1);
    if (!rgba) return NULL;

    for (int t = 0; t < nTiles; t++) {
        int col = t % 4, row = t / 4;
        for (int y = 0; y < 64; y++) {
            for (int x = 0; x < 64; x++) {
                int srcOff = (row * 64 + y) * 256 + col * 64 + x;
                uint8 palIdx = texData[srcOff];
                int dstOff = ((row * 64 + y) * atlasW + col * 64 + x) * 4;
                const tColor *c = &pal[palIdx];
                rgba[dstOff + 0] = (uint8)(c->byR * 255 / 63);
                rgba[dstOff + 1] = (uint8)(c->byG * 255 / 63);
                rgba[dstOff + 2] = (uint8)(c->byB * 255 / 63);
                rgba[dstOff + 3] = palIdx ? 255 : 0;
            }
        }
    }
    for (int x = 0; x < atlasW; x++) {
        int off = ((atlasH - 1) * atlasW + x) * 4;
        rgba[off] = rgba[off+1] = rgba[off+2] = rgba[off+3] = 255;
    }

    SDL_GPUTexture *tex = hw_upload_rgba(dev, rgba, atlasW, atlasH);
    free(rgba);
    if (outNumTiles) *outNumTiles = nTiles;
    if (outAtlasH)   *outAtlasH  = atlasH;
    return tex;
}

/* Resolve animated polygon texture for a given animation frame.
 * Wheel-animation entries have animIdx 0-3 and advance with anim_frame.
 * Body-variant entries (animIdx >= 4) use bodyVariant = carIdx & 1, matching
 * SW's iAnimationOffset / 4 = carIdx & 1 scheme (car.c:1881). */
static uint32 resolve_car_tex(uint32 rawTex, tAnimation *pAnms, int animFrame, int bodyVariant)
{
    if ((rawTex & CAR_FLAG_ANMS_LOOKUP) && pAnms) {
        uint8 animIdx = (uint8)rawTex;
        tAnimation *anim = &pAnms[animIdx];
        int f = (animIdx < 4) ? animFrame : bodyVariant;
        if (anim->uiCount > 0 && f >= (int)anim->uiCount) f = 0;
        return anim->framesAy[f];
    }
    return rawTex;
}

/* Build vertex/index buffers for one car design. */
static bool build_car_mesh(SDL_GPUDevice *dev, int carIdx,
                            const tColor *pal, GRHWCarMesh *out)
{
    int numTiles = 0, atlasH = 0;
    out->atlas = build_car_atlas(dev, carIdx, pal, &numTiles, &atlasH);
    bool hasAtlas = (out->atlas != NULL && numTiles > 0);
    float fAtlasH = hasAtlas ? (float)atlasH : 1.0f;

    int designIdx = (carIdx >= 0 && carIdx < 16) ? (int)Car[carIdx].byCarDesignIdx : 0;
    if (designIdx < 0 || designIdx > CAR_DESIGN_DEATH) designIdx = 0;

    if (!out->atlas) {
        uint8 white[4] = {255, 255, 255, 255};
        out->atlas = hw_upload_rgba(dev, white, 1, 1);
    }
    float whiteU = hasAtlas ? 0.5f / 256.0f : 0.5f;
    float whiteV = hasAtlas ? (atlasH - 0.5f) / fAtlasH : 0.5f;

    tCarDesign     *design   = &CarDesigns[designIdx];
    int             numPols  = design->byNumPols;
    int             numCoords = design->byNumCoords;
    tVec3          *coords   = design->pCoords;
    tPolygon       *pols     = design->pPols;
    tAnimation     *pAnms    = design->pAnms;
    tCarColorRemap *remap    = &car_flat_remap[designIdx];

    if (!coords || !pols || numPols == 0) return false;

    /* *2 for front + back faces per polygon (pBacks[]) */
    GRHWMeshVertex *vertices = calloc((size_t)(numPols * 8 + 8), sizeof(GRHWMeshVertex));
    uint32         *indices  = calloc((size_t)(numPols * 12 + 12), sizeof(uint32));
    if (!vertices || !indices) {
        free(vertices); free(indices);
        if (out->atlas) { SDL_ReleaseGPUTexture(dev, out->atlas); out->atlas = NULL; }
        return false;
    }

    /* Build one vertex buffer per animation frame; share one index buffer (geometry is
     * identical across frames — only UV coordinates change for animated wheel polys). */
    for (int animFrame = 0; animFrame < GRHW_ANIM_FRAMES; animFrame++) {
    int vertCount = 0, idxCount = 0;

    for (int p = 0; p < numPols; p++) {
        uint32 tex = pols[p].uiTex;
        if (tex & SURFACE_FLAG_SKIP_RENDER) continue;
        tex = resolve_car_tex(tex, pAnms, animFrame, carIdx & 1);

        bool isTextured = hasAtlas && (tex & SURFACE_FLAG_APPLY_TEXTURE) &&
                          (uint8)tex < (uint8)numTiles;

        float u0, u1, v0, v1, cr, cg, cb, ca;

        if (isTextured) {
            uint8 tileIdx = (uint8)tex;
            int col = tileIdx % 4, row = tileIdx / 4;
            u0 = (col * 64.0f) / 256.0f;
            u1 = ((col + 1) * 64.0f) / 256.0f;
            v0 = (row * 64.0f) / fAtlasH;
            v1 = ((row + 1) * 64.0f) / fAtlasH;
            if (tex & SURFACE_FLAG_FLIP_HORIZ) { float t = u0; u0 = u1; u1 = t; }
            if (tex & SURFACE_FLAG_FLIP_VERT)  { float t = v0; v0 = v1; v1 = t; }
            cr = cg = cb = ca = 1.0f;
        } else {
            uint8 colorIdx = (uint8)tex;
            if (!(tex & SURFACE_FLAG_APPLY_TEXTURE) &&
                remap->uiColorFrom != 0xFFFFFFFF &&
                colorIdx == (uint8)remap->uiColorFrom)
                colorIdx = (uint8)remap->uiColorTo;
            const tColor *c = &pal[colorIdx];
            cr = (c->byR * 255.0f / 63.0f) / 255.0f;
            cg = (c->byG * 255.0f / 63.0f) / 255.0f;
            cb = (c->byB * 255.0f / 63.0f) / 255.0f;
            ca = 1.0f;
            u0 = u1 = whiteU;
            v0 = v1 = whiteV;
        }

        int baseVert = vertCount;

        /* Swap u0/u1 to compensate for the scene renderer's horizontal mirror.
         * SURFACE_FLAG_FLIP_HORIZ pre-swaps, so together this produces the
         * intended horizontal flip. */
        float uvs[4][2] = {
            { u1, v0 }, { u0, v0 }, { u0, v1 }, { u1, v1 }
        };

        for (int v = 0; v < 4; v++) {
            uint8 vi = pols[p].verts[v];
            if (vi >= (uint8)numCoords) vi = 0;
            GRHWMeshVertex *mv = &vertices[vertCount++];
            mv->position[0] = coords[vi].fX;
            mv->position[1] = coords[vi].fY;
            mv->position[2] = coords[vi].fZ;
            mv->uv[0]       = uvs[v][0];
            mv->uv[1]       = uvs[v][1];
            mv->color[0]    = cr;
            mv->color[1]    = cg;
            mv->color[2]    = cb;
            mv->color[3]    = ca;
        }

        indices[idxCount++] = (uint32)(baseVert + 0);
        indices[idxCount++] = (uint32)(baseVert + 1);
        indices[idxCount++] = (uint32)(baseVert + 2);
        indices[idxCount++] = (uint32)(baseVert + 0);
        indices[idxCount++] = (uint32)(baseVert + 2);
        indices[idxCount++] = (uint32)(baseVert + 3);
    }

    /* Back-face polygons (reversed winding), following SW renderer logic (car.c:1647):
       - FLIP_BACKFACE + BACK: front=uiTex, back=pBacks[p]  (explicit back texture)
       - FLIP_BACKFACE only:   both sides use the same resolved texture (double-sided)
       - BACK only or neither: front only — no reversed polygon needed (pBacks unused in SW) */
    for (int p = 0; p < numPols; p++) {
        uint32 rawFront = pols[p].uiTex;
        if (rawFront & SURFACE_FLAG_SKIP_RENDER) continue;
        if (!(rawFront & SURFACE_FLAG_FLIP_BACKFACE)) continue;

        uint32 tex;
        if (rawFront & SURFACE_FLAG_BACK) {
            if (!design->pBacks) continue;
            tex = design->pBacks[p];
        } else {
            tex = resolve_car_tex(rawFront, pAnms, animFrame, carIdx & 1);
        }
        if (tex & SURFACE_FLAG_SKIP_RENDER) continue;

        bool isTexturedBack = hasAtlas && (tex & SURFACE_FLAG_APPLY_TEXTURE) &&
                              (uint8)tex < (uint8)numTiles;

        float u0b, u1b, v0b, v1b, crb, cgb, cbb, cab;

        if (isTexturedBack) {
            uint8 tileIdx = (uint8)tex;
            int col = tileIdx % 4, row = tileIdx / 4;
            u0b = (col * 64.0f) / 256.0f;
            u1b = ((col + 1) * 64.0f) / 256.0f;
            v0b = (row * 64.0f) / fAtlasH;
            v1b = ((row + 1) * 64.0f) / fAtlasH;
            if (tex & SURFACE_FLAG_FLIP_HORIZ) { float t = u0b; u0b = u1b; u1b = t; }
            if (tex & SURFACE_FLAG_FLIP_VERT)  { float t = v0b; v0b = v1b; v1b = t; }
            crb = cgb = cbb = cab = 1.0f;
        } else {
            uint8 colorIdx = (uint8)tex;
            if (!(tex & SURFACE_FLAG_APPLY_TEXTURE) &&
                remap->uiColorFrom != 0xFFFFFFFF &&
                colorIdx == (uint8)remap->uiColorFrom)
                colorIdx = (uint8)remap->uiColorTo;
            const tColor *c = &pal[colorIdx];
            crb = (c->byR * 255.0f / 63.0f) / 255.0f;
            cgb = (c->byG * 255.0f / 63.0f) / 255.0f;
            cbb = (c->byB * 255.0f / 63.0f) / 255.0f;
            cab = 1.0f;
            u0b = u1b = whiteU;
            v0b = v1b = whiteV;
        }

        int baseVert = vertCount;
        float uvsBack[4][2] = {
            { u1b, v0b }, { u0b, v0b }, { u0b, v1b }, { u1b, v1b }
        };
        for (int v = 0; v < 4; v++) {
            uint8 vi = pols[p].verts[3 - v];  /* reversed vertex order */
            if (vi >= (uint8)numCoords) vi = 0;
            GRHWMeshVertex *mv = &vertices[vertCount++];
            mv->position[0] = coords[vi].fX;
            mv->position[1] = coords[vi].fY;
            mv->position[2] = coords[vi].fZ;
            mv->uv[0]       = uvsBack[v][0];
            mv->uv[1]       = uvsBack[v][1];
            mv->color[0]    = crb;
            mv->color[1]    = cgb;
            mv->color[2]    = cbb;
            mv->color[3]    = cab;
        }
        indices[idxCount++] = (uint32)(baseVert + 0);
        indices[idxCount++] = (uint32)(baseVert + 1);
        indices[idxCount++] = (uint32)(baseVert + 2);
        indices[idxCount++] = (uint32)(baseVert + 0);
        indices[idxCount++] = (uint32)(baseVert + 2);
        indices[idxCount++] = (uint32)(baseVert + 3);
    }

    if (animFrame == 0) out->bodyIndexCount = idxCount;

    /* Shadow quad: same X extents as the hitbox but narrower in Y to match
     * the SW renderer, which uses carpoint corners narrowed by 50 units on
     * each side (car.c:1149-1152, design index ≤ 7 only).
     * Use hitbox values directly — CarBaseX/Y are design-0-only globals and
     * may be 0 at first-draw time.  Hitbox is always valid (CalcCarSizes). */
    {
        const tVec3 *hb = CarBox.hitboxAy[designIdx];
        /* hb[0]=(front,right), hb[1]=(rear,right), hb[2]=(rear,left), hb[3]=(front,left) */
        float fYAdj = (designIdx <= 7) ? 50.0f : 0.0f;
        float fYLS  = hb[0].fY + fYAdj;   /* right edge, pull inward (+) */
        float fYHS  = hb[3].fY - fYAdj;   /* left edge, pull inward (−) */
        float fShadowZ = hb[0].fZ;
        float sx[4] = { hb[0].fX, hb[1].fX, hb[2].fX, hb[3].fX };
        float sy[4] = { fYLS,     fYLS,      fYHS,     fYHS     };
        int shadowBase = vertCount;
        for (int k = 0; k < 4; k++) {
            GRHWMeshVertex *mv = &vertices[vertCount++];
            mv->position[0] = sx[k];
            mv->position[1] = sy[k];
            mv->position[2] = fShadowZ;
            mv->uv[0] = whiteU; mv->uv[1] = whiteV;
            mv->color[0] = 0.0f; mv->color[1] = 0.0f;
            mv->color[2] = 0.0f; mv->color[3] = 0.5f;
        }
        indices[idxCount++] = (uint32)(shadowBase + 0);
        indices[idxCount++] = (uint32)(shadowBase + 1);
        indices[idxCount++] = (uint32)(shadowBase + 2);
        indices[idxCount++] = (uint32)(shadowBase + 0);
        indices[idxCount++] = (uint32)(shadowBase + 2);
        indices[idxCount++] = (uint32)(shadowBase + 3);
    }

    out->vertexBufs[animFrame] = hw_upload_gpu_buffer(dev, SDL_GPU_BUFFERUSAGE_VERTEX,
                                            vertices,
                                            (Uint32)(vertCount * (int)sizeof(GRHWMeshVertex)));
    if (animFrame == 0) {
        out->indexBuf   = hw_upload_gpu_buffer(dev, SDL_GPU_BUFFERUSAGE_INDEX,
                                            indices,
                                            (Uint32)(idxCount * (int)sizeof(uint32)));
        out->indexCount = idxCount;
    }
    } /* end animFrame loop */
    free(vertices);
    free(indices);

    if (!out->vertexBufs[0] || !out->indexBuf) {
        for (int f = 0; f < GRHW_ANIM_FRAMES; f++) {
            if (out->vertexBufs[f]) { SDL_ReleaseGPUBuffer(dev, out->vertexBufs[f]); out->vertexBufs[f] = NULL; }
        }
        if (out->indexBuf)  { SDL_ReleaseGPUBuffer(dev, out->indexBuf);  out->indexBuf  = NULL; }
        if (out->atlas)     { SDL_ReleaseGPUTexture(dev, out->atlas);    out->atlas      = NULL; }
        return false;
    }
    return true;
}

/* ==========================================================================
 * Matrix helpers
 * ========================================================================== */

/* 4×4 column-major matrix multiply: out = A * B */
static void mat4_mul(float out[16], const float A[16], const float B[16])
{
    for (int col = 0; col < 4; col++)
    for (int row = 0; row < 4; row++) {
        float sum = 0.0f;
        for (int k = 0; k < 4; k++)
            sum += A[k*4 + row] * B[col*4 + k];
        out[col*4 + row] = sum;
    }
}

/* Column-major model matrix with yaw, pitch, roll — matches the SW vertex transform
 * in car.c (worldX = carX*CY*CP + carY*(CY*SP*SR-SY*CR) + carZ*(-CY*SP*CR-SY*SR), etc.).
 * All angles in FATAL 14-bit units (16384 = 360°). */
static void car_model_matrix(float M[16], const GameRenderCarPose *pose)
{
    static const float kToRad = (float)(2.0 * 3.14159265358979) / 16384.0f;
    float CY = cosf((float)pose->yaw   * kToRad), SY = sinf((float)pose->yaw   * kToRad);
    float CP = cosf((float)pose->pitch * kToRad), SP = sinf((float)pose->pitch * kToRad);
    float CR = cosf((float)pose->roll  * kToRad), SR = sinf((float)pose->roll  * kToRad);

    M[ 0] =  CY*CP;             M[ 1] =  SY*CP;             M[ 2] =  SP;      M[ 3] = 0.0f;
    M[ 4] =  CY*SP*SR - SY*CR;  M[ 5] =  SY*SP*SR + CY*CR;  M[ 6] = -SR*CP;  M[ 7] = 0.0f;
    M[ 8] = -CY*SP*CR - SY*SR;  M[ 9] = -SY*SP*CR + CY*SR;  M[10] =  CP*CR;  M[11] = 0.0f;
    M[12] = pose->position.fX;
    M[13] = pose->position.fY;
    M[14] = pose->position.fZ;
    M[15] = 1.0f;
}

/* Column-major chunk transform from localdata[chunk].pointAy. */
static void chunk_model_matrix(float M[16], const tData *d)
{
    M[ 0]=d->pointAy[0].fX; M[ 1]=d->pointAy[1].fX; M[ 2]=d->pointAy[2].fX; M[ 3]=0.0f;
    M[ 4]=d->pointAy[0].fY; M[ 5]=d->pointAy[1].fY; M[ 6]=d->pointAy[2].fY; M[ 7]=0.0f;
    M[ 8]=d->pointAy[0].fZ; M[ 9]=d->pointAy[1].fZ; M[10]=d->pointAy[2].fZ; M[11]=0.0f;
    M[12]=-d->pointAy[3].fX;
    M[13]=-d->pointAy[3].fY;
    M[14]=-d->pointAy[3].fZ;
    M[15]=1.0f;
}

/* ==========================================================================
 * Lifecycle
 * ========================================================================== */

GameRendererHardware *game_render_hw_create(SDL_GPUDevice *device)
{
    GameRendererHardware *r = calloc(1, sizeof(*r));
    if (!r) return NULL;
    r->device = device;
    return r;
}

void game_render_hw_destroy(GameRendererHardware *r)
{
    if (!r) return;
    for (int i = 0; i < GAME_RENDER_HW_NUM_CARS; i++) {
        GRHWCarMesh *m = &r->meshes[i];
        if (!m->built) continue;
        for (int f = 0; f < GRHW_ANIM_FRAMES; f++)
            if (m->vertexBufs[f]) SDL_ReleaseGPUBuffer(r->device, m->vertexBufs[f]);
        if (m->indexBuf)  SDL_ReleaseGPUBuffer(r->device, m->indexBuf);
        if (m->atlas)     SDL_ReleaseGPUTexture(r->device, m->atlas);
    }
    free(r);
}

/* ==========================================================================
 * Public: draw one car this frame
 * ========================================================================== */

void game_render_hw_draw_car(GameRendererHardware       *r,
                              SceneRendererGPU           *scene,
                              int                         carIdx,
                              const GameRenderCarPose    *pose,
                              const GameRenderCarOptions *options)
{
    if (!r || !scene || !pose) return;
    if (carIdx < 0 || carIdx >= GAME_RENDER_HW_NUM_CARS) return;

    GRHWCarMesh *mesh = &r->meshes[carIdx];
    if (!mesh->built) {
        if (build_car_mesh(r->device, carIdx, palette, mesh))
            mesh->built = true;
        else
            return;
    }
    if (mesh->indexCount <= 0) return;

    int animFrame = (options && options->anim_frame >= 0 && options->anim_frame < GRHW_ANIM_FRAMES)
                    ? options->anim_frame : 0;
    SDL_GPUBuffer *vertBuf = mesh->vertexBufs[animFrame];

    float vp[16], M[16], mvp[16];
    scene_render_gpu_build_vp(scene, vp);

    /* Apply the same visual offsets the SW renderer adds (car.c lines 1111-1113):
     * yaw += iYawMotion; pitch += CameraOffset + Motion + DynamicOffset; same for roll. */
    GameRenderCarPose adjPose = *pose;
    adjPose.yaw   = (pose->yaw
                     + (int)(int16)Car[carIdx].iYawMotion) & 0x3FFF;
    adjPose.pitch = (pose->pitch
                     + Car[carIdx].iPitchCameraOffset
                     + Car[carIdx].iPitchMotion
                     + Car[carIdx].iPitchDynamicOffset) & 0x3FFF;
    adjPose.roll  = (pose->roll
                     + Car[carIdx].iRollCameraOffset
                     + Car[carIdx].iRollMotion
                     + Car[carIdx].iRollDynamicOffset) & 0x3FFF;
    car_model_matrix(M, &adjPose);

    int chunk = Car[carIdx].nCurrChunk;
    bool airborne = (chunk < 0 || chunk >= MAX_TRACK_CHUNKS);

    int designIdx = (int)Car[carIdx].byCarDesignIdx;
    if (designIdx < 0 || designIdx > CAR_DESIGN_DEATH) designIdx = 0;
    float fZLow = CarBox.hitboxAy[designIdx][0].fZ;
    M[14] -= fZLow;  /* lift body so bottom vertex lands at physics pos */

    if (!airborne) {
        float Mc[16], Mcar_chunk[16];
        chunk_model_matrix(Mc, &localdata[chunk]);
        mat4_mul(Mcar_chunk, Mc, M);
        mat4_mul(mvp, vp, Mcar_chunk);
        /* Body only — shadow drawn separately with flat matrix below. */
        scene_render_gpu_queue_car_draw(scene,
            vertBuf, mesh->indexBuf, mesh->atlas,
            0, mesh->bodyIndexCount, mvp);

        /* Shadow: world XY from chunk transform, groundZ from current chunk.
         * Direction from Mcar_chunk col-0 XY (world-space car forward, projected
         * flat) so the shadow tracks the car on curved/tilted track sections. */
        float wx = Mcar_chunk[12], wy = Mcar_chunk[13];
        const tData *gsd = &localdata[chunk];
        float gp3x = gsd->pointAy[3].fX, gp3y = gsd->pointAy[3].fY;
        float glx = gsd->pointAy[0].fX*(wx+gp3x) + gsd->pointAy[1].fX*(wy+gp3y);
        float gly = gsd->pointAy[0].fY*(wx+gp3x) + gsd->pointAy[1].fY*(wy+gp3y);
        float groundZ = gsd->pointAy[2].fX*glx + gsd->pointAy[2].fY*gly - gsd->pointAy[3].fZ;

        float fx = Mcar_chunk[0], fy = Mcar_chunk[1];
        float flen = sqrtf(fx*fx + fy*fy);
        if (flen > 1e-6f) { fx /= flen; fy /= flen; }
        float Mshadow[16];
        Mshadow[ 0]= fx;   Mshadow[ 1]= fy;   Mshadow[ 2]=0.0f; Mshadow[ 3]=0.0f;
        Mshadow[ 4]=-fy;   Mshadow[ 5]= fx;   Mshadow[ 6]=0.0f; Mshadow[ 7]=0.0f;
        Mshadow[ 8]=0.0f;  Mshadow[ 9]=0.0f;  Mshadow[10]=1.0f; Mshadow[11]=0.0f;
        Mshadow[12]=wx;    Mshadow[13]=wy;
        Mshadow[14]=groundZ - fZLow + 3.0f;  /* +3 clears coplanarity with road */
        Mshadow[15]=1.0f;
        float shadowMvp[16];
        mat4_mul(shadowMvp, vp, Mshadow);
        scene_render_gpu_queue_car_draw(scene,
            vertBuf, mesh->indexBuf, mesh->atlas,
            mesh->bodyIndexCount, 6, shadowMvp);

    } else {
        /* Body: no chunk transform (Car.pos is world-space when airborne) */
        mat4_mul(mvp, vp, M);
        scene_render_gpu_queue_car_draw(scene,
            vertBuf, mesh->indexBuf, mesh->atlas,
            0, mesh->bodyIndexCount, mvp);

        /* Shadow: project down to last valid chunk's road surface.
         * Direction from M col-0 XY projected flat (world-space when airborne). */
        int shadowChunk = Car[carIdx].iLastValidChunk;
        if (shadowChunk >= 0 && shadowChunk < MAX_TRACK_CHUNKS) {
            const tData *sd = &localdata[shadowChunk];
            float wx = M[12], wy = M[13];
            float p3x = sd->pointAy[3].fX, p3y = sd->pointAy[3].fY;
            float lx = sd->pointAy[0].fX*(wx+p3x) + sd->pointAy[1].fX*(wy+p3y);
            float ly = sd->pointAy[0].fY*(wx+p3x) + sd->pointAy[1].fY*(wy+p3y);
            float groundZ = sd->pointAy[2].fX*lx + sd->pointAy[2].fY*ly - sd->pointAy[3].fZ;

            float fx = M[0], fy = M[1];
            float flen = sqrtf(fx*fx + fy*fy);
            if (flen > 1e-6f) { fx /= flen; fy /= flen; }
            float Mshadow[16];
            Mshadow[ 0]= fx;   Mshadow[ 1]= fy;   Mshadow[ 2]=0.0f; Mshadow[ 3]=0.0f;
            Mshadow[ 4]=-fy;   Mshadow[ 5]= fx;   Mshadow[ 6]=0.0f; Mshadow[ 7]=0.0f;
            Mshadow[ 8]=0.0f;  Mshadow[ 9]=0.0f;  Mshadow[10]=1.0f; Mshadow[11]=0.0f;
            Mshadow[12]=wx;    Mshadow[13]=wy;
            Mshadow[14]=groundZ - fZLow + 3.0f;  /* +3 clears coplanarity with road */
            Mshadow[15]=1.0f;
            float shadowMvp[16];
            mat4_mul(shadowMvp, vp, Mshadow);
            scene_render_gpu_queue_car_draw(scene,
                vertBuf, mesh->indexBuf, mesh->atlas,
                mesh->bodyIndexCount, 6, shadowMvp);
        }
    }
}

/* ==========================================================================
 * Car name tag overlay (GPU mode)
 *
 * Mirrors the name-tag block from car.c's DisplayCarWithPose, using the same
 * globals and projection formula. Must be called AFTER game_render_hw_draw_car
 * and BEFORE NamesLeft is decremented for this car.
 * ========================================================================== */
void game_render_hw_draw_car_name_tag(int carIdx, const GameRenderCarPose *pose)
{
    if (!names_on) return;
    if (NamesLeft >= 5 || NamesLeft < -2) return;
    if (replaytype == 2) return;
    if (winner_mode) return;
    if (intro) return;

    tCar *pCar = &Car[carIdx];
    if (pCar->byStatusFlags & 2) { s_tagScrY[carIdx] = -1; return; }
    if (!(names_on == 1 || (names_on == 2 && human_control[pCar->iDriverIdx]))) return;

    int carDesignIndex = pCar->byCarDesignIdx;
    float fHitboxZ = CarBox.hitboxAy[carDesignIndex][4].fZ;
    float fZLowTag  = CarBox.hitboxAy[carDesignIndex][0].fZ;

    int iCurrChunk = pCar->nCurrChunk;
    const float *p = (iCurrChunk >= 0 && iCurrChunk < MAX_TRACK_CHUNKS)
                     ? (const float *)localdata[iCurrChunk].pointAy : NULL;

    /* scrX: project bounding-box centre-top so the label stays centred over
     * the car regardless of yaw (no wobble as the car rotates).
     * fZLowTag offset matches the M[14]-=fZLow correction in game_render_hw_draw_car
     * so the tag tracks the top of the lifted body mesh. */
    float fCX = pose->position.fX, fCY = pose->position.fY, fCZ_tag = fHitboxZ - fZLowTag + pose->position.fZ;
    float wX0, wY0, wZ0;
    if (!p) { wX0 = fCX; wY0 = fCY; wZ0 = fCZ_tag; }
    else {
        wX0 = p[0]*fCX + p[1]*fCY + p[2]*fCZ_tag - p[9];
        wY0 = p[3]*fCX + p[4]*fCY + p[5]*fCZ_tag - p[10];
        wZ0 = p[6]*fCX + p[7]*fCY + p[8]*fCZ_tag - p[11];
    }
    double dX0 = wX0-viewx, dY0 = wY0-viewy, dZ0 = wZ0-viewz;
    double fVX0 = dX0*vk1 + dY0*vk4 + dZ0*vk7;
    double fVY0 = dX0*vk2 + dY0*vk5 + dZ0*vk8;
    double fCamZ0 = dX0*vk3 + dY0*vk6 + dZ0*vk9;
    if (fCamZ0 <= 0.0) return;
    double fClZ0 = fCamZ0 < 80.0 ? 80.0 : fCamZ0;
    int scrX = hw_d2i((double)VIEWDIST * fVX0 / fClZ0 + xbase) * scr_size >> 6;

    /* scrY from same centre-top point as scrX.  The old approach scanned four
     * hitbox corners and took the highest-projecting one; as the car rotates
     * different corners win, producing visible Y jitter.  The centre point is
     * yaw-independent so scrY is smooth without needing EMA. */
    double sY0 = (double)VIEWDIST * fVY0 / fClZ0 + ybase;
    int scrY = (scr_size * (199 - hw_d2i(sY0))) >> 6;

    /* Smooth scrY with an EMA (K=4) to suppress residual 1/z noise.  Reset
     * instantly on large deltas (car first appearing, large jump). */
    {
        int prev = s_tagScrY[carIdx];
        int delta = (prev < 0) ? (winh + 1) : (scrY - prev);
        if (delta < 0) delta = -delta;
        if (prev < 0 || delta > winh / 4)
            s_tagScrY[carIdx] = scrY;
        else
            s_tagScrY[carIdx] = (3 * prev + scrY + 2) / 4;
        scrY = s_tagScrY[carIdx];
    }

    /* Build label string: "NAME (POS)". */
    char tagLabel[32];
    if (pCar->byRacePosition < racers - 1 || racers == 1)
        sprintf(tagLabel, "%s (%s)", driver_names[carIdx],
                &language_buffer[64 * pCar->byRacePosition + 384]);
    else
        sprintf(tagLabel, "%s (%s)", driver_names[carIdx], &language_buffer[1344]);

    int iNameWidth = 0;
    for (int i = 0; tagLabel[i]; i++) iNameWidth += 5;
    int iNameHalfWidth = iNameWidth / 2;

    int iDisplayX = mirror ? scrX + iNameHalfWidth : scrX - iNameHalfWidth;
    int iDisplayY = scrY - 16;

    if (iNameWidth <= 0) return;
    if (iDisplayX < 0 || iDisplayX >= winw - iNameWidth) return;
    if (iDisplayY <= 0 || iDisplayY >= winh - 16) return;

    int iPrevScrSize = scr_size;
    scr_size = 64;
    if (mirror)
        mini_prt_string_rev(rev_vga[0], tagLabel, iDisplayX, iDisplayY);
    else
        mini_prt_string(rev_vga[0], tagLabel, iDisplayX, iDisplayY);
    scr_size = iPrevScrSize;

    /* Coloured downward-pointing triangle above car. */
    CarPol.vertices[0].x = scrX + 6;  CarPol.vertices[0].y = scrY - 7;
    CarPol.vertices[1].x = scrX - 5;  CarPol.vertices[1].y = scrY - 7;
    CarPol.vertices[2].x = scrX;      CarPol.vertices[2].y = scrY - 1;
    CarPol.vertices[3].x = scrX + 6;  CarPol.vertices[3].y = scrY - 7;
    CarPol.iSurfaceType = team_col[carDesignIndex];
    CarPol.uiNumVerts = 4;
    game_render_quad_screen(g_pGameRenderer, &CarPol, TEXTURE_HANDLE_INVALID, NULL);
}

void game_render_hw_draw_fps_overlay(void)
{
    if (g_iFpsDisplay == 0 || !scrbuf) return;

    static Uint64 s_lastTick = 0;
    static float  s_avgMs    = 0.0f;

    Uint64 now  = SDL_GetPerformanceCounter();
    Uint64 freq = SDL_GetPerformanceFrequency();
    if (s_lastTick > 0 && freq > 0) {
        float ms = (float)((double)(now - s_lastTick) * 1000.0 / (double)freq);
        if (ms > 0.1f && ms < 2000.0f)
            s_avgMs = (s_avgMs > 0.0f) ? (0.9f * s_avgMs + 0.1f * ms) : ms;
    }
    s_lastTick = now;

    if (s_avgMs <= 0.0f) return;

    char buf[16];
    sprintf(buf, "%.0f FPS", 1000.0f / s_avgMs);

    int textW = 0;
    for (int i = 0; buf[i]; i++) textW += 5;
    const int textH = 7;
    const int margin = 4;

    int x, y;
    switch (g_iFpsDisplay) {
        default:
        case 1: x = margin;               y = margin;                    break;
        case 2: x = winw - textW - margin; y = margin;                    break;
        case 3: x = margin;               y = winh - textH - margin;     break;
        case 4: x = winw - textW - margin; y = winh - textH - margin;    break;
    }

    if (x < 0 || y < 0 || x + textW > winw || y + textH > winh) return;

    uint8 *prevScreenPointer = screen_pointer;
    int    prevScrSize       = scr_size;
    screen_pointer = scrbuf;
    scr_size       = 64;
    mini_prt_string(rev_vga[0], buf, x, y);
    scr_size       = prevScrSize;
    screen_pointer = prevScreenPointer;
}
