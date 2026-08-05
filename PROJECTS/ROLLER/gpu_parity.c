#include "gpu_parity.h"

#if defined(__EMSCRIPTEN__)

int ROLLERGpuParityRun(const char *szBackend)
{
    (void)szBackend;
    return 1;
}

#else

#include "scene_render_gpu.h"
#include "game_render.h"
#include "polytex.h"
#include "3d.h"
#include "drawtrk3.h"
#include "editor_reference_mesh.h"
#include "editor_surface.h"
#include "graphics.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GPU_PARITY_MAX_CHANNEL_DELTA 1u
#define GPU_PARITY_MAX_DIFFERING_PIXEL_SHARE 0.001
#define GPU_PARITY_MIN_COVERAGE_SHARE 0.02

typedef enum
{
    GPU_PARITY_FIXTURE_OPAQUE_TRACK = 0,
    GPU_PARITY_FIXTURE_DARKENING,
    GPU_PARITY_FIXTURE_BUILDINGS_SIGNS,
    GPU_PARITY_FIXTURE_TEXTURED_WALLS,
    GPU_PARITY_FIXTURE_COUNT
} eGpuParityFixture;

typedef struct
{
    Uint32 uiWidth;
    Uint32 uiHeight;
} tGpuParitySize;

typedef struct
{
    Uint32 uiMaxChannelDelta;
    Uint64 ullDifferingPixels;
    Uint64 ullCoveredPixels;
} tGpuParityMetrics;

typedef struct
{
    float afPosition[3];
    float afUv[2];
    float afColor[4];
} tGpuReferenceVertex;

typedef struct
{
    tEdSurfaceEmission Surface;
    bool bCalled;
} tGpuEmissionCapture;

typedef struct
{
    SceneRendererGPU *pRenderer;
    SceneTextureHandle iTexture;
    const tEdSurfaceSelection *pSelection;
    Uint32 uiSelectedChunkMask;
    Uint32 uiSelectedCount;
    bool bCalled;
} tGpuEmissionRenderContext;

static const char *g_aszGpuParityFixtures[GPU_PARITY_FIXTURE_COUNT] = {
    "opaque-track",
    "transparent-darkening",
    "buildings-signs",
    "textured-walls"
};

static const tGpuParitySize g_aGpuParitySizes[] = {
    { 320, 200 },
    { 853, 480 }
};

static void gpu_parity_set_palette_color(int iIndex, uint8 byR, uint8 byG, uint8 byB)
{
    palette[iIndex].byR = byR;
    palette[iIndex].byG = byG;
    palette[iIndex].byB = byB;
}

static void gpu_parity_init_palette(void)
{
    memset(palette, 0, sizeof(palette));
    gpu_parity_set_palette_color(1, 63, 12, 8);
    gpu_parity_set_palette_color(2, 8, 48, 63);
    gpu_parity_set_palette_color(3, 58, 50, 6);
    gpu_parity_set_palette_color(4, 12, 63, 18);
    gpu_parity_set_palette_color(5, 56, 16, 55);
    gpu_parity_set_palette_color(6, 38, 38, 38);
}

static void gpu_parity_make_atlas(uint8 *pbyAtlas, int iWidth, int iHeight)
{
    for (int iY = 0; iY < iHeight; iY++) {
        for (int iX = 0; iX < iWidth; iX++) {
            int iTile = iX / 64;
            int iChecker = ((iX / 8) ^ (iY / 8)) & 1;
            if (iTile == 0)
                pbyAtlas[iY * iWidth + iX] = (uint8)(iChecker ? 1 : 2);
            else
                pbyAtlas[iY * iWidth + iX] = (uint8)(iChecker ? 3 : 4);
        }
    }
}

static void gpu_parity_make_quad(SceneRenderVertex aVerts[4],
                                 float fLeft, float fTop,
                                 float fRight, float fBottom,
                                 float fDepth, Uint32 uiWidth, Uint32 uiHeight)
{
    float fYScale = (float)uiHeight / (float)uiWidth;
    aVerts[0] = (SceneRenderVertex){ fLeft * fDepth,  fTop * fDepth * fYScale, fDepth, 0, 0 };
    aVerts[1] = (SceneRenderVertex){ fRight * fDepth, fTop * fDepth * fYScale, fDepth, 0, 0 };
    aVerts[2] = (SceneRenderVertex){ fRight * fDepth, fBottom * fDepth * fYScale, fDepth, 0, 0 };
    aVerts[3] = (SceneRenderVertex){ fLeft * fDepth,  fBottom * fDepth * fYScale, fDepth, 0, 0 };
}

static void gpu_parity_set_uvs(bool bPair)
{
    fixed16_16 iMaxU = bPair ? 0x7FF000 : 0x3FF000;
    startsx[0] = iMaxU;
    startsx[1] = 0;
    startsx[2] = 0;
    startsx[3] = iMaxU;
    startsy[0] = 0;
    startsy[1] = 0;
    startsy[2] = 0x3FF000;
    startsy[3] = 0x3FF000;
}

static void gpu_f_s5_capture_emission(const tEdSurfaceEmission *pSurface,
                                      void *pUserData)
{
    tGpuEmissionCapture *pCapture = pUserData;
    pCapture->Surface = *pSurface;
    pCapture->bCalled = true;
}

static void gpu_f_s5_render_emission(const tEdSurfaceEmission *pSurface,
                                     void *pUserData)
{
    tGpuEmissionRenderContext *pContext = pUserData;
    SceneRenderVertex aVertices[ED_SURFACE_VERTEX_COUNT];
    SceneRenderLegacyQuadOptions Options = {
        .subdivideType = SCENE_RENDER_SUBDIVIDE_TYPE_AUTO,
        .subThreshold = pSurface->fSubdivideThreshold
    };
    bool bSelected;
    uint32_t uiRenderFlags;
    SceneTextureHandle iTexture;

    if (pSurface->uiVertexCount != ED_SURFACE_VERTEX_COUNT)
        return;
    bSelected = ed_surface_selection_matches(
        pContext->pSelection, pSurface);
    uiRenderFlags = ed_surface_selection_render_flags(
        pContext->pSelection, pSurface);
    iTexture = bSelected
        ? SCENE_TEXTURE_HANDLE_INVALID
        : pContext->iTexture;
    if (bSelected) {
        pContext->uiSelectedCount++;
        if (pSurface->uiChunkId < 32u)
            pContext->uiSelectedChunkMask |= 1u << pSurface->uiChunkId;
    }
    for (uint32_t i = 0; i < ED_SURFACE_VERTEX_COUNT; i++) {
        aVertices[i].x = pSurface->aVertices[i].fPosition[0];
        aVertices[i].y = pSurface->aVertices[i].fPosition[1];
        aVertices[i].z = pSurface->aVertices[i].fPosition[2];
        aVertices[i].u = 0.0f;
        aVertices[i].v = 0.0f;
        startsx[i] = pSurface->aVertices[i].iRenderU16_16;
        startsy[i] = pSurface->aVertices[i].iRenderV16_16;
    }
    scene_render_gpu_quad_world_legacy(
        pContext->pRenderer, aVertices, iTexture,
        (int)uiRenderFlags, Options);
    pContext->bCalled = true;
}

static bool gpu_f_s5_metadata_check(void)
{
    static const float afWorld[ED_SURFACE_VERTEX_COUNT][3] = {
        { -880.0f,  700.0f, 1000.0f },
        {  880.0f,  700.0f, 1000.0f },
        {  880.0f, -700.0f, 1000.0f },
        { -880.0f, -700.0f, 1000.0f }
    };
    fixed16_16 aiSavedU[ED_SURFACE_VERTEX_COUNT];
    fixed16_16 aiSavedV[ED_SURFACE_VERTEX_COUNT];
    int iSavedGfxSize = gfx_size;
    bool bPass = true;

    memcpy(aiSavedU, startsx, sizeof(aiSavedU));
    memcpy(aiSavedV, startsy, sizeof(aiSavedV));
    gfx_size = 0;

    tEdMaterial aReverseMaterials[2];
    tEdMaterialTable ReverseTable;
    tGpuEmissionCapture ReverseCapture = { 0 };
    tEdTextureAtlas Atlas = { TEXTURE_BANK_TRACK, 128u, 64u, 64u, 2u };
    tEdSurfaceInfo ReverseInfo = {
        .uiChunkId = 73u,
        .uiRenderFlags =
            SURFACE_FLAG_APPLY_TEXTURE | SURFACE_FLAG_BACK,
        .uiBackSurfaceFlags = 1u,
        .uiTextureSet = TEXTURE_BANK_TRACK,
        .fSubdivideThreshold = 1000000.0f,
        .bPairTextureEnabled = false,
        .bHighVariant = false,
        .unSurfaceClass = ROLLER_ED_SURFACE_CLASS_LEFT_WALL,
        .unContentClass = ROLLER_ED_CONTENT_AUTHORED_TRACK,
        .byTopology = ROLLER_ED_TOPOLOGY_QUAD,
        .byRenderUVLayout = ROLLER_ED_RENDER_UV_TILE
    };
    bPass = ed_material_table_init(
        &ReverseTable, aReverseMaterials, 2u, Atlas)
        && ed_emit_surface(
            afWorld, &ReverseInfo, &ReverseTable,
            gpu_f_s5_capture_emission, &ReverseCapture)
        && ReverseCapture.bCalled;

    set_starts(0);
    for (uint32_t i = 0; bPass && i < ED_SURFACE_VERTEX_COUNT; i++) {
        bPass = ReverseCapture.Surface.aVertices[i].iRenderU16_16
                    == startsx[i]
             && ReverseCapture.Surface.aVertices[i].iRenderV16_16
                    == startsy[i];
    }
    const tEdMaterial *pFrontMaterial = bPass
        ? ed_material_table_get(
            &ReverseTable, ReverseCapture.Surface.uiFrontMaterialId)
        : NULL;
    const tEdMaterial *pBackMaterial = bPass
        ? ed_material_table_get(
            &ReverseTable, ReverseCapture.Surface.uiBackMaterialId)
        : NULL;
    float afFrontAtlasUV[2] = { 0 };
    float afBackAtlasUV[2] = { 0 };
    if (pFrontMaterial && pBackMaterial) {
        ed_material_resolve_uv(
            pFrontMaterial,
            ReverseCapture.Surface.aVertices[0].fMaterialUV,
            afFrontAtlasUV);
        ed_material_resolve_uv(
            pBackMaterial,
            ReverseCapture.Surface.aVertices[0].fMaterialUV,
            afBackAtlasUV);
        bPass = pFrontMaterial->uiTileIndex == 0u
             && pBackMaterial->uiTileIndex == 1u
             && afFrontAtlasUV[0] < 0.5f
             && afBackAtlasUV[0] >= 0.5f;
    } else {
        bPass = false;
    }

    tEdMaterial aForwardMaterial[1];
    tEdMaterial aFlippedMaterial[1];
    tEdMaterialTable ForwardTable;
    tEdMaterialTable FlippedTable;
    tGpuEmissionCapture ForwardCapture = { 0 };
    tGpuEmissionCapture FlippedCapture = { 0 };
    tEdSurfaceInfo ForwardInfo = {
        .uiChunkId = 74u,
        .uiRenderFlags =
            SURFACE_FLAG_APPLY_TEXTURE | SURFACE_FLAG_TEXTURE_PAIR,
        .uiBackSurfaceFlags = ED_MATERIAL_ID_NONE,
        .uiTextureSet = TEXTURE_BANK_TRACK,
        .fSubdivideThreshold = 1000000.0f,
        .bPairTextureEnabled = true,
        .bHighVariant = false,
        .unSurfaceClass = ROLLER_ED_SURFACE_CLASS_LEFT_WALL,
        .unContentClass = ROLLER_ED_CONTENT_AUTHORED_TRACK,
        .byTopology = ROLLER_ED_TOPOLOGY_QUAD,
        .byRenderUVLayout = ROLLER_ED_RENDER_UV_PAIR_HORIZONTAL
    };
    tEdSurfaceInfo FlippedInfo = ForwardInfo;
    FlippedInfo.uiRenderFlags |= SURFACE_FLAG_FLIP_HORIZ;
    bPass = bPass
        && ed_material_table_init(
            &ForwardTable, aForwardMaterial, 1u, Atlas)
        && ed_material_table_init(
            &FlippedTable, aFlippedMaterial, 1u, Atlas)
        && ed_emit_surface(
            afWorld, &ForwardInfo, &ForwardTable,
            gpu_f_s5_capture_emission, &ForwardCapture)
        && ed_emit_surface(
            afWorld, &FlippedInfo, &FlippedTable,
            gpu_f_s5_capture_emission, &FlippedCapture);

    set_starts(1);
    for (uint32_t i = 0; bPass && i < ED_SURFACE_VERTEX_COUNT; i++) {
        bPass = ForwardCapture.Surface.aVertices[i].iRenderU16_16
                    == startsx[i]
             && ForwardCapture.Surface.aVertices[i].iRenderV16_16
                    == startsy[i];
    }
    if (bPass) {
        bPass = ForwardCapture.Surface.aVertices[0].fMaterialUV[0]
                    > ForwardCapture.Surface.aVertices[1].fMaterialUV[0]
             && FlippedCapture.Surface.aVertices[0].fMaterialUV[0]
                    < FlippedCapture.Surface.aVertices[1].fMaterialUV[0];
    }

    tEdMaterial NonDerivedMaterial = {
        .uiTileIndex = 203u,
        .fAtlasScale = { 0.125f, 0.25f },
        .fAtlasBias = { 0.375f, 0.5f }
    };
    const float afMaterialUV[2] = { 0.25f, 0.75f };
    float afAtlasUV[2] = { 0 };
    ed_material_resolve_uv(
        &NonDerivedMaterial, afMaterialUV, afAtlasUV);
    bPass = bPass
        && fabsf(afAtlasUV[0] - 0.40625f) < 0.000001f
        && fabsf(afAtlasUV[1] - 0.6875f) < 0.000001f;

    gfx_size = iSavedGfxSize;
    memcpy(startsx, aiSavedU, sizeof(aiSavedU));
    memcpy(startsy, aiSavedV, sizeof(aiSavedV));
    SDL_Log("F-S5 %s: exact-render-uvs reverse-material paired-directions atlas-transforms",
            bPass ? "PASS" : "FAIL");
    return bPass;
}

static void gpu_parity_queue_quad(SceneRendererGPU *pRenderer,
                                  SceneTextureHandle iTexture,
                                  int iSurfaceFlags,
                                  int iSubdivideType,
                                  float fLeft, float fTop,
                                  float fRight, float fBottom,
                                  float fDepth, Uint32 uiWidth, Uint32 uiHeight)
{
    SceneRenderVertex aVerts[4];
    gpu_parity_make_quad(aVerts, fLeft, fTop, fRight, fBottom,
                         fDepth, uiWidth, uiHeight);
    gpu_parity_set_uvs((iSurfaceFlags & SURFACE_FLAG_TEXTURE_PAIR) != 0);
    SceneRenderLegacyQuadOptions options = {
        .subdivideType = iSubdivideType,
        .subThreshold = 1000000.0f
    };
    scene_render_gpu_quad_world_legacy(pRenderer, aVerts, iTexture,
                                       iSurfaceFlags, options);
}

static bool gpu_parity_render_fixture(SceneRendererGPU *pRenderer,
                                      SceneTextureHandle iTexture,
                                      eGpuParityFixture eFixture,
                                      Uint32 uiWidth, Uint32 uiHeight,
                                      uint8 *pbyPixels, Uint32 uiBufferSize)
{
    SceneRenderCamera camera = {
        .viewX = 0.0f,
        .viewY = 0.0f,
        .viewZ = 0.0f,
        .cosYaw = 1.0f,
        .sinYaw = 0.0f,
        .fovScale = (float)uiWidth * 0.5f,
        .renderChunkIdx = -1
    };
    SceneRenderProjection projection = {
        .view = {
            { 1.0f, 0.0f, 0.0f },
            { 0.0f, 1.0f, 0.0f },
            { 0.0f, 0.0f, 1.0f }
        },
        .screenScale = 64,
        .centerX = (int)uiWidth / 2,
        .centerY = 199 - (int)uiHeight / 2,
        .texHalfRes = 0
    };

    scene_render_gpu_begin_frame(pRenderer);
    scene_render_gpu_set_viewport(pRenderer, 0, 0, (int)uiWidth, (int)uiHeight);
    scene_render_gpu_set_camera(pRenderer, &camera);
    scene_render_gpu_set_projection(pRenderer, &projection);
    scene_render_gpu_set_sky_color(pRenderer, 0.07f, 0.10f, 0.16f);

    switch (eFixture) {
    case GPU_PARITY_FIXTURE_OPAQUE_TRACK:
        gpu_parity_queue_quad(pRenderer, iTexture,
            SURFACE_FLAG_APPLY_TEXTURE,
            SCENE_RENDER_SUBDIVIDE_TYPE_AUTO,
            -0.82f, 0.72f, 0.82f, -0.72f, 1000.0f, uiWidth, uiHeight);
        break;

    case GPU_PARITY_FIXTURE_DARKENING:
        gpu_parity_queue_quad(pRenderer, iTexture,
            SURFACE_FLAG_APPLY_TEXTURE | 1,
            SCENE_RENDER_SUBDIVIDE_TYPE_AUTO,
            -0.90f, 0.78f, 0.90f, -0.78f, 1100.0f, uiWidth, uiHeight);
        gpu_parity_queue_quad(pRenderer, SCENE_TEXTURE_HANDLE_INVALID,
            SURFACE_FLAG_TRANSPARENT | 2,
            SCENE_RENDER_SUBDIVIDE_TYPE_AUTO,
            -0.58f, 0.52f, 0.62f, -0.48f, 1000.0f, uiWidth, uiHeight);
        break;

    case GPU_PARITY_FIXTURE_BUILDINGS_SIGNS:
        gpu_parity_queue_quad(pRenderer, iTexture,
            SURFACE_FLAG_APPLY_TEXTURE,
            SCENE_RENDER_SUBDIVIDE_TYPE_BUILDING,
            -0.92f, 0.72f, 0.10f, -0.68f, 1100.0f, uiWidth, uiHeight);
        gpu_parity_queue_quad(pRenderer, iTexture,
            SURFACE_FLAG_APPLY_TEXTURE | SURFACE_FLAG_GPU_IS_SIGN | 1,
            SCENE_RENDER_SUBDIVIDE_TYPE_SIGN,
            -0.12f, 0.48f, 0.82f, -0.46f, 900.0f, uiWidth, uiHeight);
        break;

    case GPU_PARITY_FIXTURE_TEXTURED_WALLS:
        gpu_parity_queue_quad(pRenderer, iTexture,
            SURFACE_FLAG_APPLY_TEXTURE | SURFACE_FLAG_TEXTURE_PAIR,
            SCENE_RENDER_SUBDIVIDE_TYPE_AUTO,
            -0.88f, 0.70f, 0.88f, -0.70f, 1000.0f, uiWidth, uiHeight);
        break;

    default:
        scene_render_gpu_cancel_frame(pRenderer);
        return false;
    }

    return scene_render_gpu_end_frame_readback(pRenderer, pbyPixels,
                                                uiBufferSize, uiWidth * 4u,
                                                uiWidth, uiHeight);
}

static tGpuParityMetrics gpu_parity_compare(const uint8 *pbyWindowed,
                                            const uint8 *pbyWindowless,
                                            Uint32 uiWidth, Uint32 uiHeight)
{
    tGpuParityMetrics metrics = { 0 };
    Uint64 ullPixels = (Uint64)uiWidth * uiHeight;

    for (Uint64 ullPixel = 0; ullPixel < ullPixels; ullPixel++) {
        bool bPixelDiffers = false;
        bool bCovered = false;
        for (int iChannel = 0; iChannel < 4; iChannel++) {
            Uint64 ullOffset = ullPixel * 4u + (Uint64)iChannel;
            int iDelta = abs((int)pbyWindowed[ullOffset]
                           - (int)pbyWindowless[ullOffset]);
            if ((Uint32)iDelta > metrics.uiMaxChannelDelta)
                metrics.uiMaxChannelDelta = (Uint32)iDelta;
            if (iDelta != 0)
                bPixelDiffers = true;
            if (iChannel < 3
                    && pbyWindowless[ullOffset] != pbyWindowless[iChannel])
                bCovered = true;
        }
        if (bPixelDiffers)
            metrics.ullDifferingPixels++;
        if (bCovered)
            metrics.ullCoveredPixels++;
    }

    return metrics;
}

static bool gpu_f_s5_render_fixture(SceneRendererGPU *pRenderer,
                                    SceneTextureHandle iTexture,
                                    bool bUseEmitter,
                                    uint8 *pbyPixels,
                                    Uint32 uiBufferSize)
{
    const Uint32 uiWidth = 320;
    const Uint32 uiHeight = 200;
    SceneRenderCamera Camera = {
        .cosYaw = 1.0f,
        .fovScale = (float)uiWidth * 0.5f,
        .renderChunkIdx = -1
    };
    SceneRenderProjection Projection = {
        .view = {
            { 1.0f, 0.0f, 0.0f },
            { 0.0f, 1.0f, 0.0f },
            { 0.0f, 0.0f, 1.0f }
        },
        .screenScale = 64,
        .centerX = (int)uiWidth / 2,
        .centerY = 199 - (int)uiHeight / 2
    };

    scene_render_gpu_begin_frame(pRenderer);
    scene_render_gpu_set_viewport(
        pRenderer, 0, 0, (int)uiWidth, (int)uiHeight);
    scene_render_gpu_set_camera(pRenderer, &Camera);
    scene_render_gpu_set_projection(pRenderer, &Projection);
    scene_render_gpu_set_sky_color(pRenderer, 0.07f, 0.10f, 0.16f);

    if (bUseEmitter) {
        SceneRenderVertex aVertices[ED_SURFACE_VERTEX_COUNT];
        float afWorld[ED_SURFACE_VERTEX_COUNT][3];
        tEdMaterial aMaterials[1];
        tEdMaterialTable MaterialTable;
        tEdTextureAtlas Atlas = { TEXTURE_BANK_TRACK, 128u, 64u, 64u, 2u };
        tEdSurfaceInfo Info = {
            .uiChunkId = 91u,
            .uiRenderFlags =
                SURFACE_FLAG_APPLY_TEXTURE | SURFACE_FLAG_TEXTURE_PAIR,
            .uiBackSurfaceFlags = ED_MATERIAL_ID_NONE,
            .uiTextureSet = TEXTURE_BANK_TRACK,
            .fSubdivideThreshold = 1000000.0f,
            .bPairTextureEnabled = true,
            .bHighVariant = false,
            .unSurfaceClass = ROLLER_ED_SURFACE_CLASS_LEFT_WALL,
            .unContentClass = ROLLER_ED_CONTENT_AUTHORED_TRACK,
            .byTopology = ROLLER_ED_TOPOLOGY_QUAD,
            .byRenderUVLayout = ROLLER_ED_RENDER_UV_PAIR_HORIZONTAL
        };
        tGpuEmissionRenderContext RenderContext = {
            .pRenderer = pRenderer,
            .iTexture = iTexture,
            .bCalled = false
        };
        gpu_parity_make_quad(
            aVertices, -0.88f, 0.70f, 0.88f, -0.70f,
            1000.0f, uiWidth, uiHeight);
        for (uint32_t i = 0; i < ED_SURFACE_VERTEX_COUNT; i++) {
            afWorld[i][0] = aVertices[i].x;
            afWorld[i][1] = aVertices[i].y;
            afWorld[i][2] = aVertices[i].z;
        }
        if (!ed_material_table_init(
                &MaterialTable, aMaterials, 1u, Atlas)
                || !ed_emit_surface(
                    afWorld, &Info, &MaterialTable,
                    gpu_f_s5_render_emission, &RenderContext)
                || !RenderContext.bCalled) {
            scene_render_gpu_cancel_frame(pRenderer);
            return false;
        }
    } else {
        gpu_parity_queue_quad(
            pRenderer, iTexture,
            SURFACE_FLAG_APPLY_TEXTURE | SURFACE_FLAG_TEXTURE_PAIR,
            SCENE_RENDER_SUBDIVIDE_TYPE_AUTO,
            -0.88f, 0.70f, 0.88f, -0.70f,
            1000.0f, uiWidth, uiHeight);
    }

    return scene_render_gpu_end_frame_readback(
        pRenderer, pbyPixels, uiBufferSize, uiWidth * 4u,
        uiWidth, uiHeight);
}

static bool gpu_f_s5_renderer_byte_check(SceneRendererGPU *pRenderer,
                                         SceneTextureHandle iTexture)
{
    const Uint32 uiWidth = 320;
    const Uint32 uiHeight = 200;
    const Uint32 uiBufferSize = uiWidth * uiHeight * 4u;
    uint8 *pbyLegacy = malloc(uiBufferSize);
    uint8 *pbyEmitted = malloc(uiBufferSize);
    bool bPass = false;

    if (!pbyLegacy || !pbyEmitted)
        goto cleanup;
    scene_render_gpu_set_msaa(pRenderer, 0);
    if (!gpu_f_s5_render_fixture(
            pRenderer, iTexture, false, pbyLegacy, uiBufferSize)
            || !gpu_f_s5_render_fixture(
                pRenderer, iTexture, true, pbyEmitted, uiBufferSize)) {
        SDL_Log("F-S5 FAIL: renderer readback failed: %s", SDL_GetError());
        goto cleanup;
    }

    tGpuParityMetrics Metrics = gpu_parity_compare(
        pbyLegacy, pbyEmitted, uiWidth, uiHeight);
    bPass = Metrics.uiMaxChannelDelta == 0
         && Metrics.ullDifferingPixels == 0
         && Metrics.ullCoveredPixels
                >= (Uint64)(uiWidth * uiHeight * GPU_PARITY_MIN_COVERAGE_SHARE);
    SDL_Log("F-S5 %s: renderer output byte-identical differing-pixels=%llu coverage=%.2f%%",
            bPass ? "PASS" : "FAIL",
            (unsigned long long)Metrics.ullDifferingPixels,
            (double)Metrics.ullCoveredPixels * 100.0
                / (double)((Uint64)uiWidth * uiHeight));

cleanup:
    free(pbyEmitted);
    free(pbyLegacy);
    return bPass;
}

static bool gpu_f_s4b_selected_surface_check(
    SceneRendererGPU *pRenderer,
    SceneTextureHandle iTexture)
{
    static const float afLeft[4] = {
        -0.92f, -0.46f, 0.06f, 0.52f
    };
    static const float afRight[4] = {
        -0.52f, -0.06f, 0.46f, 0.92f
    };
    /* Deliberately non-monotonic in screen order: identity, not position or
     * queue order, must choose the two outer quads. */
    static const uint32_t auiChunkId[4] = { 10u, 9u, 12u, 11u };
    const Uint32 uiWidth = 320;
    const Uint32 uiHeight = 200;
    const Uint32 uiBufferSize = uiWidth * uiHeight * 4u;
    uint8 *pbyPixels = malloc(uiBufferSize);
    tEdMaterial aMaterials[1];
    tEdMaterialTable MaterialTable;
    tEdTextureAtlas Atlas = { TEXTURE_BANK_TRACK, 128u, 64u, 64u, 2u };
    tEdSurfaceSelection Selection = {
        .uiFirstChunkId = 10u,
        .uiLastChunkId = 11u,
        .unSurfaceClass = ROLLER_ED_SURFACE_CLASS_LEFT_WALL,
        .byHighlightColour = 5u,
        .bEnabled = true
    };
    tGpuEmissionRenderContext RenderContext = {
        .pRenderer = pRenderer,
        .iTexture = iTexture,
        .pSelection = &Selection
    };
    SceneRenderCamera Camera = {
        .cosYaw = 1.0f,
        .fovScale = (float)uiWidth * 0.5f,
        .renderChunkIdx = -1
    };
    SceneRenderProjection Projection = {
        .view = {
            { 1.0f, 0.0f, 0.0f },
            { 0.0f, 1.0f, 0.0f },
            { 0.0f, 0.0f, 1.0f }
        },
        .screenScale = 64,
        .centerX = (int)uiWidth / 2,
        .centerY = 199 - (int)uiHeight / 2
    };
    Uint64 aullHighlightPixels[4] = { 0 };
    bool bPass = false;

    if (!pbyPixels
            || !ed_material_table_init(
                &MaterialTable, aMaterials, 1u, Atlas))
        goto cleanup;

    scene_render_gpu_set_msaa(pRenderer, 0);
    scene_render_gpu_begin_frame(pRenderer);
    scene_render_gpu_set_viewport(
        pRenderer, 0, 0, (int)uiWidth, (int)uiHeight);
    scene_render_gpu_set_camera(pRenderer, &Camera);
    scene_render_gpu_set_projection(pRenderer, &Projection);
    scene_render_gpu_set_sky_color(pRenderer, 0.07f, 0.10f, 0.16f);

    for (int iQuad = 0; iQuad < 4; iQuad++) {
        SceneRenderVertex aVertices[ED_SURFACE_VERTEX_COUNT];
        float afWorld[ED_SURFACE_VERTEX_COUNT][3];
        tEdSurfaceInfo Info = {
            .uiChunkId = auiChunkId[iQuad],
            .uiRenderFlags = SURFACE_FLAG_APPLY_TEXTURE,
            .uiBackSurfaceFlags = ED_MATERIAL_ID_NONE,
            .uiTextureSet = TEXTURE_BANK_TRACK,
            .fSubdivideThreshold = 1000000.0f,
            .bPairTextureEnabled = false,
            .bHighVariant = false,
            .unSurfaceClass = ROLLER_ED_SURFACE_CLASS_LEFT_WALL,
            .unContentClass = ROLLER_ED_CONTENT_AUTHORED_TRACK,
            .byTopology = ROLLER_ED_TOPOLOGY_QUAD,
            .byRenderUVLayout = ROLLER_ED_RENDER_UV_TILE
        };
        gpu_parity_make_quad(
            aVertices, afLeft[iQuad], 0.65f,
            afRight[iQuad], -0.65f,
            1000.0f, uiWidth, uiHeight);
        for (uint32_t i = 0; i < ED_SURFACE_VERTEX_COUNT; i++) {
            afWorld[i][0] = aVertices[i].x;
            afWorld[i][1] = aVertices[i].y;
            afWorld[i][2] = aVertices[i].z;
        }
        if (!ed_emit_surface(
                afWorld, &Info, &MaterialTable,
                gpu_f_s5_render_emission, &RenderContext)) {
            scene_render_gpu_cancel_frame(pRenderer);
            goto cleanup;
        }
    }

    if (!scene_render_gpu_end_frame_readback(
            pRenderer, pbyPixels, uiBufferSize, uiWidth * 4u,
            uiWidth, uiHeight))
        goto cleanup;

    for (int iQuad = 0; iQuad < 4; iQuad++) {
        int iMinX = (int)((afLeft[iQuad] + 1.0f) * 0.5f * uiWidth) + 3;
        int iMaxX = (int)((afRight[iQuad] + 1.0f) * 0.5f * uiWidth) - 3;
        for (int iY = 0; iY < (int)uiHeight; iY++) {
            for (int iX = iMinX; iX <= iMaxX; iX++) {
                const uint8 *pbyPixel =
                    &pbyPixels[((Uint32)iY * uiWidth + (Uint32)iX) * 4u];
                if (pbyPixel[0] >= 210u
                        && pbyPixel[1] >= 45u && pbyPixel[1] <= 90u
                        && pbyPixel[2] >= 205u)
                    aullHighlightPixels[iQuad]++;
            }
        }
    }

    bPass = RenderContext.uiSelectedCount == 2u
         && RenderContext.uiSelectedChunkMask
                == ((1u << 10) | (1u << 11))
         && aullHighlightPixels[0] >= 3000u
         && aullHighlightPixels[1] == 0u
         && aullHighlightPixels[2] == 0u
         && aullHighlightPixels[3] >= 3000u;
    SDL_Log("F-S4b %s: selected-mask=0x%X highlight-pixels=[%llu,%llu,%llu,%llu]",
            bPass ? "PASS" : "FAIL",
            RenderContext.uiSelectedChunkMask,
            (unsigned long long)aullHighlightPixels[0],
            (unsigned long long)aullHighlightPixels[1],
            (unsigned long long)aullHighlightPixels[2],
            (unsigned long long)aullHighlightPixels[3]);

cleanup:
    free(pbyPixels);
    return bPass;
}

static bool gpu_reference_depth_check(SDL_GPUDevice *pDevice,
                                      SceneRendererGPU *pRenderer,
                                      SceneTextureHandle iTrackTexture)
{
    static const char szReferenceObj[] =
        "tests/fixtures/f_s4a_reference.obj";
    static const float afIdentity[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };
    uint8 abyImportedTexture[8] = {
        255, 0, 0, 255,
        0, 255, 0, 255
    };
    const Uint32 uiWidth = 320;
    const Uint32 uiHeight = 200;
    const Uint32 uiBufferSize = uiWidth * uiHeight * 4u;
    uint8 *pbyPixels = malloc(uiBufferSize);
    tGpuReferenceVertex *pGpuVertices = NULL;
    SDL_GPUBuffer *pVertexBuffer = NULL;
    SDL_GPUBuffer *pIndexBuffer = NULL;
    SDL_GPUTexture *pMeshTexture = NULL;
    tEdReferenceMeshImport Import;
    tEdReferenceMeshState State;
    tEdReferenceMesh Mesh;
    char szReferenceError[256];
    bool bPass = false;

    ed_reference_mesh_import_init(&Import);
    ed_reference_mesh_state_init(&State);
    if (!pbyPixels)
        goto cleanup;
    if (ed_reference_mesh_import_obj(
            szReferenceObj, &Import,
            szReferenceError, sizeof(szReferenceError))
            != ED_REFERENCE_MESH_OK) {
        SDL_Log("F-S4a FAIL: reference OBJ import failed: %s",
                szReferenceError);
        goto cleanup;
    }
    memset(&Mesh, 0, sizeof(Mesh));
    Mesh.uiStructSize = sizeof(Mesh);
    Mesh.uiVersion = ROLLER_ED_REFERENCE_MESH_VERSION;
    Mesh.pVertices = Import.pVertices;
    Mesh.uiVertexCount = Import.uiVertexCount;
    Mesh.puiIndices = Import.puiIndices;
    Mesh.uiIndexCount = Import.uiIndexCount;
    Mesh.pbyTextureRGBA = abyImportedTexture;
    Mesh.uiTextureWidth = 2u;
    Mesh.uiTextureHeight = 1u;
    Mesh.uiTextureRowPitch = 8u;
    Mesh.fScale[0] = 1.0f;
    Mesh.fScale[1] = 1.0f;
    Mesh.fScale[2] = 1.0f;
    Mesh.uiFlags = ROLLER_ED_REFERENCE_TWO_SIDED;
    if (ed_reference_mesh_replace(
            &State, &Mesh,
            szReferenceError, sizeof(szReferenceError))
            != ED_REFERENCE_MESH_OK) {
        SDL_Log("F-S4a FAIL: AD-13 reference replacement failed: %s",
                szReferenceError);
        goto cleanup;
    }
    /*
     * The contract promises synchronous copies. Destroy the imported arrays
     * and overwrite the caller texture before GPU upload; correct rendering
     * can now only come from State's owned copies.
     */
    ed_reference_mesh_import_dispose(&Import);
    memset(abyImportedTexture, 0, sizeof(abyImportedTexture));
    pGpuVertices = malloc(
        (size_t)State.uiVertexCount * sizeof(*pGpuVertices));
    if (!pGpuVertices)
        goto cleanup;
    for (uint32_t i = 0; i < State.uiVertexCount; i++) {
        memcpy(pGpuVertices[i].afPosition,
               State.pVertices[i].fPosition,
               sizeof(pGpuVertices[i].afPosition));
        memcpy(pGpuVertices[i].afUv,
               State.pVertices[i].fUV,
               sizeof(pGpuVertices[i].afUv));
        for (size_t iChannel = 0; iChannel < 4u; iChannel++)
            pGpuVertices[i].afColor[iChannel] = 1.0f;
    }
    pVertexBuffer = scene_render_gpu_upload_buffer(
        pDevice, SDL_GPU_BUFFERUSAGE_VERTEX, pGpuVertices,
        (size_t)State.uiVertexCount * sizeof(*pGpuVertices));
    pIndexBuffer = scene_render_gpu_upload_buffer(
        pDevice, SDL_GPU_BUFFERUSAGE_INDEX, State.puiIndices,
        (size_t)State.uiIndexCount * sizeof(*State.puiIndices));
    pMeshTexture = scene_render_gpu_upload_rgba(
        pDevice, State.pbyTextureRGBA,
        (int)State.uiTextureWidth, (int)State.uiTextureHeight, false);
    if (!pVertexBuffer || !pIndexBuffer || !pMeshTexture) {
        SDL_Log("F-S4a FAIL: reference mesh resource upload failed: %s",
                SDL_GetError());
        goto cleanup;
    }

    SceneRenderCamera camera = {
        .cosYaw = 1.0f,
        .fovScale = (float)uiWidth * 0.5f,
        .renderChunkIdx = -1
    };
    SceneRenderProjection projection = {
        .view = {
            { 1.0f, 0.0f, 0.0f },
            { 0.0f, 1.0f, 0.0f },
            { 0.0f, 0.0f, 1.0f }
        },
        .screenScale = 64,
        .centerX = (int)uiWidth / 2,
        .centerY = 199 - (int)uiHeight / 2
    };

    scene_render_gpu_set_msaa(pRenderer, 0);
    scene_render_gpu_begin_frame(pRenderer);
    scene_render_gpu_set_viewport(pRenderer, 0, 0, (int)uiWidth, (int)uiHeight);
    scene_render_gpu_set_camera(pRenderer, &camera);
    scene_render_gpu_set_projection(pRenderer, &projection);
    scene_render_gpu_set_sky_color(pRenderer, 0.07f, 0.10f, 0.16f);
    gpu_parity_queue_quad(pRenderer, iTrackTexture,
        SURFACE_FLAG_APPLY_TEXTURE,
        SCENE_RENDER_SUBDIVIDE_TYPE_AUTO,
        -0.92f, 0.78f, 0.92f, -0.78f, 1000.0f, uiWidth, uiHeight);
    scene_render_gpu_queue_car_draw(pRenderer, pVertexBuffer, pIndexBuffer,
                                    pMeshTexture, 0,
                                    (int)State.uiIndexCount, afIdentity);
    if (!scene_render_gpu_end_frame_readback(
            pRenderer, pbyPixels, uiBufferSize, uiWidth * 4u,
            uiWidth, uiHeight)) {
        SDL_Log("F-S4a FAIL: composed-scene readback failed: %s", SDL_GetError());
        goto cleanup;
    }

    Uint64 ullFrontPixels = 0;
    Uint64 ullOccludedPixels = 0;
    for (Uint32 uiPixel = 0; uiPixel < uiWidth * uiHeight; uiPixel++) {
        const uint8 *pbyPixel = &pbyPixels[uiPixel * 4u];
        if (pbyPixel[0] >= 240 && pbyPixel[1] <= 8 && pbyPixel[2] <= 8)
            ullFrontPixels++;
        if (pbyPixel[1] >= 240 && pbyPixel[0] <= 8 && pbyPixel[2] <= 8)
            ullOccludedPixels++;
    }
    bPass = ullFrontPixels >= 4000 && ullOccludedPixels == 0;
    SDL_Log("F-S4a %s: imported-vertices=%u copied-texture=%ux%u reference-front-pixels=%llu behind-track-pixels=%llu",
            bPass ? "PASS" : "FAIL",
            State.uiVertexCount,
            State.uiTextureWidth,
            State.uiTextureHeight,
            (unsigned long long)ullFrontPixels,
            (unsigned long long)ullOccludedPixels);

cleanup:
    if (pMeshTexture)
        SDL_ReleaseGPUTexture(pDevice, pMeshTexture);
    if (pIndexBuffer)
        SDL_ReleaseGPUBuffer(pDevice, pIndexBuffer);
    if (pVertexBuffer)
        SDL_ReleaseGPUBuffer(pDevice, pVertexBuffer);
    free(pGpuVertices);
    ed_reference_mesh_state_dispose(&State);
    ed_reference_mesh_import_dispose(&Import);
    free(pbyPixels);
    return bPass;
}

static bool gpu_e1_s2_present_smoke(SceneRendererGPU *pRenderer)
{
    scene_render_gpu_begin_frame(pRenderer);
    scene_render_gpu_set_viewport(pRenderer, 0, 0, 320, 200);
    scene_render_gpu_set_sky_color(pRenderer, 0.07f, 0.10f, 0.16f);

    bool bPass = scene_render_gpu_end_frame(pRenderer);
    SDL_Log("E1-S2 %s: windowed offscreen scene submitted through late swapchain presentation",
            bPass ? "PASS" : "FAIL");
    return bPass;
}

static int gpu_parity_run_matrix(SDL_GPUDevice *pDevice, SDL_Window *pWindow)
{
    const Uint32 uiAtlasW = 128;
    const Uint32 uiAtlasH = 64;
    uint8 *pbyAtlas = malloc((size_t)uiAtlasW * uiAtlasH);
    SceneRendererGPU *pWindowed = NULL;
    SceneRendererGPU *pWindowless = NULL;
    int iResult = 1;

    if (!pbyAtlas)
        return 1;
    gpu_parity_init_palette();
    gpu_parity_make_atlas(pbyAtlas, (int)uiAtlasW, (int)uiAtlasH);

    pWindowed = scene_render_gpu_create(pDevice, pWindow);
    if (!pWindowed) {
        SDL_Log("F-S1: windowed renderer creation failed: %s", SDL_GetError());
        goto cleanup;
    }
    pWindowless = scene_render_gpu_create_windowless(pDevice);
    if (!pWindowless) {
        SDL_Log("F-S1: windowless renderer creation failed: %s", SDL_GetError());
        goto cleanup;
    }

    SceneTextureHandle iWindowedTexture = scene_render_gpu_load_texture(
        pWindowed, pbyAtlas, (int)uiAtlasW, (int)uiAtlasH, 0, 0);
    SceneTextureHandle iWindowlessTexture = scene_render_gpu_load_texture(
        pWindowless, pbyAtlas, (int)uiAtlasW, (int)uiAtlasH, 0, 0);
    if (iWindowedTexture == SCENE_TEXTURE_HANDLE_INVALID
            || iWindowlessTexture == SCENE_TEXTURE_HANDLE_INVALID) {
        SDL_Log("F-S1: fixture texture upload failed: %s", SDL_GetError());
        goto cleanup;
    }

    /* E1-S3: explicit readback dimensions must be authoritative even when
     * each renderer carries different game-facing render-scale state. */
    scene_render_gpu_set_render_scale(pWindowed, 2.0f);
    scene_render_gpu_set_render_scale(pWindowless, 0.5f);

    SDL_GPUTextureFormat eWindowedFormat =
        SDL_GetGPUSwapchainTextureFormat(pDevice, pWindow);
    const SDL_GPUTextureFormat eWindowlessFormat =
        SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    int iMsaaLevel = 0;
    const char *szMsaaName = "off";
    if (SDL_GPUTextureSupportsSampleCount(pDevice, eWindowedFormat,
                                          SDL_GPU_SAMPLECOUNT_4)
            && SDL_GPUTextureSupportsSampleCount(pDevice, eWindowlessFormat,
                                                 SDL_GPU_SAMPLECOUNT_4)) {
        iMsaaLevel = 2;
        szMsaaName = "4x";
    } else if (SDL_GPUTextureSupportsSampleCount(pDevice, eWindowedFormat,
                                                 SDL_GPU_SAMPLECOUNT_2)
            && SDL_GPUTextureSupportsSampleCount(pDevice, eWindowlessFormat,
                                                 SDL_GPU_SAMPLECOUNT_2)) {
        iMsaaLevel = 1;
        szMsaaName = "2x";
    } else {
        SDL_Log("F-S1: no common 2x or 4x MSAA support for formats %d and %d",
                (int)eWindowedFormat, (int)eWindowlessFormat);
        goto cleanup;
    }

    SDL_Log("F-S1: metric max-channel-delta<=%u differing-pixels<=%.3f%% min-coverage>=%.1f%%",
            GPU_PARITY_MAX_CHANNEL_DELTA,
            GPU_PARITY_MAX_DIFFERING_PIXEL_SHARE * 100.0,
            GPU_PARITY_MIN_COVERAGE_SHARE * 100.0);
    SDL_Log("F-S1: windowed-target-format=%d windowless-target-format=%d enabled-msaa=%s",
            (int)eWindowedFormat, (int)eWindowlessFormat, szMsaaName);

    int iFailures = 0;
    for (int iMsaaPass = 0; iMsaaPass < 2; iMsaaPass++) {
        int iLevel = iMsaaPass == 0 ? 0 : iMsaaLevel;
        const char *szLevel = iMsaaPass == 0 ? "off" : szMsaaName;
        scene_render_gpu_set_msaa(pWindowed, iLevel);
        scene_render_gpu_set_msaa(pWindowless, iLevel);

        for (int iSize = 0;
             iSize < (int)(sizeof(g_aGpuParitySizes) / sizeof(g_aGpuParitySizes[0]));
             iSize++) {
            Uint32 uiWidth = g_aGpuParitySizes[iSize].uiWidth;
            Uint32 uiHeight = g_aGpuParitySizes[iSize].uiHeight;
            Uint32 uiBufferSize = uiWidth * uiHeight * 4u;
            uint8 *pbyWindowed = malloc(uiBufferSize);
            uint8 *pbyWindowless = malloc(uiBufferSize);
            if (!pbyWindowed || !pbyWindowless) {
                free(pbyWindowed);
                free(pbyWindowless);
                SDL_Log("F-S1: frame allocation failed");
                goto cleanup;
            }

            for (int iFixture = 0; iFixture < GPU_PARITY_FIXTURE_COUNT; iFixture++) {
                memset(pbyWindowed, 0xA5, uiBufferSize);
                memset(pbyWindowless, 0x5A, uiBufferSize);
                bool bWindowedOk = gpu_parity_render_fixture(
                    pWindowed, iWindowedTexture, (eGpuParityFixture)iFixture,
                    uiWidth, uiHeight, pbyWindowed, uiBufferSize);
                bool bWindowlessOk = gpu_parity_render_fixture(
                    pWindowless, iWindowlessTexture, (eGpuParityFixture)iFixture,
                    uiWidth, uiHeight, pbyWindowless, uiBufferSize);
                if (!bWindowedOk || !bWindowlessOk) {
                    SDL_Log("F-S1 FAIL: fixture=%s size=%ux%u msaa=%s readback windowed=%d windowless=%d error=%s",
                            g_aszGpuParityFixtures[iFixture], uiWidth, uiHeight,
                            szLevel, (int)bWindowedOk, (int)bWindowlessOk,
                            SDL_GetError());
                    iFailures++;
                    continue;
                }

                tGpuParityMetrics metrics = gpu_parity_compare(
                    pbyWindowed, pbyWindowless, uiWidth, uiHeight);
                Uint64 ullPixels = (Uint64)uiWidth * uiHeight;
                double dDifferingShare = (double)metrics.ullDifferingPixels
                                       / (double)ullPixels;
                double dCoverageShare = (double)metrics.ullCoveredPixels
                                      / (double)ullPixels;
                bool bPass = metrics.uiMaxChannelDelta <= GPU_PARITY_MAX_CHANNEL_DELTA
                          && dDifferingShare <= GPU_PARITY_MAX_DIFFERING_PIXEL_SHARE
                          && dCoverageShare >= GPU_PARITY_MIN_COVERAGE_SHARE;
                SDL_Log("F-S1 %s: fixture=%s size=%ux%u msaa=%s max-delta=%u differing=%.5f%% coverage=%.2f%%",
                        bPass ? "PASS" : "FAIL",
                        g_aszGpuParityFixtures[iFixture], uiWidth, uiHeight,
                        szLevel, metrics.uiMaxChannelDelta,
                        dDifferingShare * 100.0, dCoverageShare * 100.0);
                if (!bPass)
                    iFailures++;
            }

            free(pbyWindowed);
            free(pbyWindowless);
        }
    }

    scene_render_gpu_set_render_scale(pWindowed, 1.0f);
    scene_render_gpu_set_render_scale(pWindowless, 1.0f);
    bool bSurfaceMetadataPass = gpu_f_s5_metadata_check();
    bool bSurfaceRendererPass = gpu_f_s5_renderer_byte_check(
        pWindowless, iWindowlessTexture);
    bool bSelectedSurfacePass = gpu_f_s4b_selected_surface_check(
        pWindowless, iWindowlessTexture);
    bool bReferenceDepthPass = gpu_reference_depth_check(
        pDevice, pWindowless, iWindowlessTexture);
    bool bPresentationPass = gpu_e1_s2_present_smoke(pWindowed);
    if (iFailures == 0) {
        SDL_Log("F-S1 PASS: all 16 windowed/windowless comparisons passed");
        SDL_Log("E1-S3 PASS: caller-sized offscreen resize matrix completed at 320x200 and 853x480 independent of renderScale=2.0/0.5");
    } else {
        SDL_Log("F-S1 FAIL: %d comparison(s) failed", iFailures);
    }
    if (iFailures == 0 && bSurfaceMetadataPass
            && bSurfaceRendererPass && bSelectedSurfacePass
            && bReferenceDepthPass && bPresentationPass)
        iResult = 0;

cleanup:
    if (pWindowless)
        scene_render_gpu_destroy(pWindowless);
    if (pWindowed)
        scene_render_gpu_destroy(pWindowed);
    free(pbyAtlas);
    return iResult;
}

int ROLLERGpuParityRun(const char *szBackend)
{
    SDL_GPUDevice *pDevice = NULL;
    SDL_Window *pWindow = NULL;
    int iResult = 1;

    if (!szBackend || !szBackend[0]) {
        fprintf(stderr, "F-S1: a GPU backend is required\n");
        return 1;
    }

    SDL_SetHint(SDL_HINT_GPU_DRIVER, szBackend);
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "F-S1: SDL video initialization failed: %s\n", SDL_GetError());
        return 1;
    }

    /* Vulkan does not guarantee an acquirable swapchain for a hidden native
     * window.  This is intentionally a real windowed path; CI supplies a
     * virtual display on Linux. */
    pWindow = SDL_CreateWindow("ROLLER F-S1 GPU parity", 960, 540, 0);
    if (!pWindow) {
        SDL_Log("F-S1: window creation failed: %s", SDL_GetError());
        goto cleanup;
    }

    pDevice = SDL_CreateGPUDevice(
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL
            | SDL_GPU_SHADERFORMAT_DXIL,
        false, szBackend);
    if (!pDevice) {
        SDL_Log("F-S1: %s device creation failed: %s", szBackend, SDL_GetError());
        goto cleanup;
    }
    const char *szActualBackend = SDL_GetGPUDeviceDriver(pDevice);
    if (!szActualBackend || SDL_strcasecmp(szActualBackend, szBackend) != 0) {
        SDL_Log("F-S1: requested backend %s but SDL selected %s",
                szBackend, szActualBackend ? szActualBackend : "(null)");
        goto cleanup;
    }

    if (!SDL_ClaimWindowForGPUDevice(pDevice, pWindow)) {
        SDL_Log("F-S1: window claim failed: %s", SDL_GetError());
        goto cleanup;
    }

    SDL_Log("F-S1: running on backend %s swapchain-format=%d",
            szActualBackend,
            (int)SDL_GetGPUSwapchainTextureFormat(pDevice, pWindow));
    iResult = gpu_parity_run_matrix(pDevice, pWindow);

cleanup:
    if (pDevice) {
        SDL_WaitForGPUIdle(pDevice);
        if (pWindow)
            SDL_ReleaseWindowFromGPUDevice(pDevice, pWindow);
        SDL_DestroyGPUDevice(pDevice);
    }
    if (pWindow)
        SDL_DestroyWindow(pWindow);
    SDL_Quit();
    return iResult;
}

#endif /* __EMSCRIPTEN__ */
