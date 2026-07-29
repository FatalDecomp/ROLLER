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
                                                uiBufferSize, uiWidth * 4u);
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

static bool gpu_reference_depth_check(SDL_GPUDevice *pDevice,
                                      SceneRendererGPU *pRenderer,
                                      SceneTextureHandle iTrackTexture)
{
    static const float afIdentity[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };
    static const uint8 abyWhiteTexture[16] = {
        255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 255
    };
    static const tGpuReferenceVertex aVertices[8] = {
        {{-0.82f,  0.55f, 0.50f}, {0, 0}, {1, 0, 0, 1}},
        {{-0.08f,  0.55f, 0.50f}, {1, 0}, {1, 0, 0, 1}},
        {{-0.08f, -0.55f, 0.50f}, {1, 1}, {1, 0, 0, 1}},
        {{-0.82f, -0.55f, 0.50f}, {0, 1}, {1, 0, 0, 1}},
        {{ 0.08f,  0.55f, 1.00f}, {0, 0}, {0, 1, 0, 1}},
        {{ 0.82f,  0.55f, 1.00f}, {1, 0}, {0, 1, 0, 1}},
        {{ 0.82f, -0.55f, 1.00f}, {1, 1}, {0, 1, 0, 1}},
        {{ 0.08f, -0.55f, 1.00f}, {0, 1}, {0, 1, 0, 1}}
    };
    /* Each rectangle contains both windings. The active backend culls one
     * winding and renders the other, keeping this fixture independent of the
     * backend's render-target Y convention. */
    static const Uint32 aIndices[24] = {
        0, 1, 2, 0, 2, 3, 0, 2, 1, 0, 3, 2,
        4, 5, 6, 4, 6, 7, 4, 6, 5, 4, 7, 6
    };
    const Uint32 uiWidth = 320;
    const Uint32 uiHeight = 200;
    const Uint32 uiBufferSize = uiWidth * uiHeight * 4u;
    uint8 *pbyPixels = malloc(uiBufferSize);
    SDL_GPUBuffer *pVertexBuffer = NULL;
    SDL_GPUBuffer *pIndexBuffer = NULL;
    SDL_GPUTexture *pMeshTexture = NULL;
    bool bPass = false;

    if (!pbyPixels)
        goto cleanup;
    pVertexBuffer = scene_render_gpu_upload_buffer(
        pDevice, SDL_GPU_BUFFERUSAGE_VERTEX, aVertices, sizeof(aVertices));
    pIndexBuffer = scene_render_gpu_upload_buffer(
        pDevice, SDL_GPU_BUFFERUSAGE_INDEX, aIndices, sizeof(aIndices));
    pMeshTexture = scene_render_gpu_upload_rgba(
        pDevice, abyWhiteTexture, 2, 2, false);
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
                                    pMeshTexture, 0, 24, afIdentity);
    if (!scene_render_gpu_end_frame_readback(
            pRenderer, pbyPixels, uiBufferSize, uiWidth * 4u)) {
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
    SDL_Log("F-S4a %s: reference-front-pixels=%llu behind-track-pixels=%llu",
            bPass ? "PASS" : "FAIL",
            (unsigned long long)ullFrontPixels,
            (unsigned long long)ullOccludedPixels);

cleanup:
    if (pMeshTexture)
        SDL_ReleaseGPUTexture(pDevice, pMeshTexture);
    if (pIndexBuffer)
        SDL_ReleaseGPUBuffer(pDevice, pIndexBuffer);
    if (pVertexBuffer)
        SDL_ReleaseGPUBuffer(pDevice, pVertexBuffer);
    free(pbyPixels);
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

    bool bReferenceDepthPass = gpu_reference_depth_check(
        pDevice, pWindowless, iWindowlessTexture);
    if (iFailures == 0) {
        SDL_Log("F-S1 PASS: all 16 windowed/windowless comparisons passed");
    } else {
        SDL_Log("F-S1 FAIL: %d comparison(s) failed", iFailures);
    }
    if (iFailures == 0 && bReferenceDepthPass)
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
