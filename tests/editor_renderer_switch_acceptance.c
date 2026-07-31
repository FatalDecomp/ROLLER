#include "3d.h"
#include "editor_api.h"
#include "game_render.h"
#include "render_queue_3d.h"

#define SDL_MAIN_HANDLED 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    const char *szTrackPath;
    const char *szAssetRoot;
    char szError[512];
    int iResult;
} tRendererSwitchContext;

static void acceptance_error(tRendererSwitchContext *pContext,
                             const char *szMessage)
{
    snprintf(pContext->szError, sizeof(pContext->szError), "%s: %s",
             szMessage, RollerEd_GetLastError());
    pContext->iResult = 1;
}

static int pixels_have_content(const uint8_t *pPixels, size_t uiSize)
{
    uint32_t uiFirst;

    if (uiSize < sizeof(uiFirst))
        return 0;
    memcpy(&uiFirst, pPixels, sizeof(uiFirst));
    for (size_t uiOffset = sizeof(uiFirst);
         uiOffset + sizeof(uint32_t) <= uiSize;
         uiOffset += sizeof(uint32_t)) {
        uint32_t uiPixel;
        memcpy(&uiPixel, pPixels + uiOffset, sizeof(uiPixel));
        if (uiPixel != uiFirst)
            return -1;
    }
    return 0;
}

static int queue_has_track(const RenderQueue3D *pQueue)
{
    int iCount = render_queue_3d_count(pQueue);

    for (int i = 0; i < iCount; ++i) {
        const RenderCommand3D *pCommand = render_queue_3d_command_at(pQueue, i);
        if (pCommand
                && (pCommand->kind == RENDER_COMMAND_3D_KIND_ROAD_SURFACE
                    || pCommand->kind == RENDER_COMMAND_3D_KIND_GROUND_SURFACE))
            return -1;
    }
    return 0;
}

static int render_and_check(tRendererSwitchContext *pContext,
                            uint8_t *pPixels,
                            uint32_t uiBufferSize,
                            uint32_t uiRowPitch,
                            uint32_t uiWidth,
                            uint32_t uiHeight,
                            GameRenderMode eExpectedMode,
                            int bRequireContent,
                            const char *szPhase)
{
    int bHasContent;

    memset(pPixels, 0x5a, uiBufferSize);
    if (RollerEd_RenderFrame(
            pPixels, uiBufferSize, uiRowPitch, uiWidth, uiHeight,
            ROLLER_ED_PIXEL_RGBA8) != ROLLER_ED_RESULT_OK) {
        acceptance_error(pContext, szPhase);
        return 0;
    }
    bHasContent = pixels_have_content(pPixels, uiBufferSize)
        && queue_has_track(render_queue_3d_global());
    if (game_render_get_mode(g_pGameRenderer) != eExpectedMode
            || (bRequireContent && !bHasContent)) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "%s produced the wrong mode or no track content", szPhase);
        pContext->iResult = 1;
        return 0;
    }
    return bHasContent;
}

static int SDLCALL renderer_switch_worker(void *pUserData)
{
    enum { WIDTH = 640, HEIGHT = 480, ROW_PITCH = WIDTH * 4 };
    static const float afYaw[] = { 0.0f, 90.0f, 180.0f, 270.0f };
    static const float afPitch[] = { -25.0f, 0.0f, 25.0f };
    tRendererSwitchContext *pContext = (tRendererSwitchContext *)pUserData;
    tRollerEdInitInfo InitInfo = {
        .uiStructSize = sizeof(InitInfo),
        .uiVersion = ROLLER_ED_INIT_INFO_VERSION,
        .szAssetRoot = pContext->szAssetRoot,
        .ePreferredRenderer = ROLLER_ED_RENDERER_SOFTWARE,
        .uiAllowSoftwareFallback = 0u
    };
    tEdGeometrySizes BeforeSizes = {
        .uiStructSize = sizeof(BeforeSizes),
        .uiVersion = ROLLER_ED_GEOMETRY_SIZES_VERSION
    };
    tEdGeometrySizes AfterSizes = BeforeSizes;
    uint8_t *pPixels = NULL;
    uint32_t uiAvailable;
    float fTargetX = 0.0f;
    float fTargetY = 0.0f;
    float fTargetZ = 0.0f;
    int bFoundSoftwareFrame = 0;

    if (RollerEd_Init(&InitInfo) != ROLLER_ED_RESULT_OK) {
        acceptance_error(pContext, "RollerEd_Init failed");
        return pContext->iResult;
    }
    uiAvailable = RollerEd_GetAvailableRenderers();
    if ((uiAvailable & ROLLER_ED_RENDERER_SOFTWARE) == 0u
            || (uiAvailable & ROLLER_ED_RENDERER_GPU) == 0u) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "hosted E1-S8 acceptance requires software and GPU (mask=0x%x)",
                 uiAvailable);
        pContext->iResult = 1;
        goto shutdown;
    }
    if (RollerEd_SelectRenderer(ROLLER_ED_RENDERER_SOFTWARE)
            != ROLLER_ED_RESULT_OK) {
        acceptance_error(pContext, "pre-load software selection failed");
        goto shutdown;
    }
    if (RollerEd_LoadTrackFile(pContext->szTrackPath, pContext->szAssetRoot)
            != ROLLER_ED_RESULT_OK) {
        acceptance_error(pContext, "RollerEd_LoadTrackFile failed");
        goto shutdown;
    }
    if (RollerEd_QueryGeometrySizes(&BeforeSizes) != ROLLER_ED_RESULT_OK) {
        acceptance_error(pContext, "initial geometry query failed");
        goto shutdown;
    }

    pPixels = (uint8_t *)malloc((size_t)ROW_PITCH * HEIGHT);
    if (!pPixels) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "renderer-switch pixel allocation failed");
        pContext->iResult = 1;
        goto shutdown;
    }

    for (int iPoint = 0; iPoint < 6; ++iPoint) {
        fTargetX += TrakPt[0].pointAy[iPoint].fX;
        fTargetY += TrakPt[0].pointAy[iPoint].fY;
        fTargetZ += TrakPt[0].pointAy[iPoint].fZ;
    }
    fTargetX /= 6.0f;
    fTargetY /= 6.0f;
    fTargetZ /= 6.0f;

    for (size_t iPitch = 0;
         iPitch < sizeof(afPitch) / sizeof(afPitch[0]) && !bFoundSoftwareFrame;
         ++iPitch) {
        for (size_t iYaw = 0;
             iYaw < sizeof(afYaw) / sizeof(afYaw[0]) && !bFoundSoftwareFrame;
             ++iYaw) {
            tEdCameraState Camera = {
                .uiStructSize = sizeof(Camera),
                .uiVersion = ROLLER_ED_CAMERA_STATE_VERSION,
                .fPosition = { fTargetX - 4000.0f, fTargetY,
                               fTargetZ + 1600.0f },
                .fYawDegrees = afYaw[iYaw],
                .fPitchDegrees = afPitch[iPitch]
            };

            if (RollerEd_SetCamera(&Camera) != ROLLER_ED_RESULT_OK) {
                acceptance_error(pContext, "RollerEd_SetCamera failed");
                goto shutdown;
            }
            bFoundSoftwareFrame = render_and_check(
                pContext, pPixels, ROW_PITCH * HEIGHT, ROW_PITCH,
                WIDTH, HEIGHT, GAME_RENDER_SOFTWARE,
                0,
                "initial software render failed");
            if (pContext->iResult)
                goto shutdown;
        }
    }
    if (!bFoundSoftwareFrame) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "software renderer produced no usable track frame");
        pContext->iResult = 1;
        goto shutdown;
    }

    if (RollerEd_SelectRenderer(ROLLER_ED_RENDERER_GPU)
            != ROLLER_ED_RESULT_OK) {
        acceptance_error(pContext, "software-to-GPU selection failed");
        goto shutdown;
    }
    if (!render_and_check(
            pContext, pPixels, ROW_PITCH * HEIGHT, ROW_PITCH,
            WIDTH, HEIGHT, GAME_RENDER_GPU, 1, "first GPU render failed"))
        goto shutdown;
    if (!game_render_get_device(g_pGameRenderer)
            || !game_render_get_gpu(g_pGameRenderer)) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "GPU selection did not attach a device/backend");
        pContext->iResult = 1;
        goto shutdown;
    }

    if (RollerEd_SelectRenderer(ROLLER_ED_RENDERER_SOFTWARE)
            != ROLLER_ED_RESULT_OK
            || !render_and_check(
                pContext, pPixels, ROW_PITCH * HEIGHT, ROW_PITCH,
                WIDTH, HEIGHT, GAME_RENDER_SOFTWARE,
                1,
                "GPU-to-software render failed"))
        goto shutdown;

    if (RollerEd_SelectRenderer(ROLLER_ED_RENDERER_GPU)
            != ROLLER_ED_RESULT_OK
            || !render_and_check(
                pContext, pPixels, ROW_PITCH * HEIGHT, ROW_PITCH,
                WIDTH, HEIGHT, GAME_RENDER_GPU,
                1,
                "second GPU render failed"))
        goto shutdown;

    if (RollerEd_SelectRenderer(4u) != ROLLER_ED_RESULT_INVALID_ARGUMENT
            || !render_and_check(
                pContext, pPixels, ROW_PITCH * HEIGHT, ROW_PITCH,
                WIDTH, HEIGHT, GAME_RENDER_GPU,
                1,
                "render after rejected selection failed")) {
        if (!pContext->iResult) {
            snprintf(pContext->szError, sizeof(pContext->szError),
                     "invalid selection disturbed the active GPU renderer");
            pContext->iResult = 1;
        }
        goto shutdown;
    }

    AfterSizes.uiStructSize = sizeof(AfterSizes);
    AfterSizes.uiVersion = ROLLER_ED_GEOMETRY_SIZES_VERSION;
    if (RollerEd_QueryGeometrySizes(&AfterSizes) != ROLLER_ED_RESULT_OK
            || AfterSizes.uiGeometryEpoch != BeforeSizes.uiGeometryEpoch
            || AfterSizes.uiTrackGeneration != BeforeSizes.uiTrackGeneration
            || AfterSizes.uiSceneState != BeforeSizes.uiSceneState) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "renderer switches changed facade scene identity");
        pContext->iResult = 1;
    }

shutdown:
    free(pPixels);
    if (RollerEd_Shutdown() != ROLLER_ED_RESULT_OK && !pContext->iResult)
        acceptance_error(pContext, "RollerEd_Shutdown failed");
    return pContext->iResult;
}

int main(int argc, char **argv)
{
    tRollerEdBootstrapInfo BootstrapInfo = {
        .uiStructSize = sizeof(BootstrapInfo),
        .uiVersion = ROLLER_ED_BOOTSTRAP_INFO_VERSION,
        .uiFlags = 0u
    };
    tRendererSwitchContext Context;
    SDL_Thread *pWorker;
    int iWorkerResult = 1;

    if (argc != 3) {
        fprintf(stderr, "usage: %s ABSOLUTE_TRACK_PATH ABSOLUTE_ASSET_ROOT\n",
                argv[0]);
        return 2;
    }
    memset(&Context, 0, sizeof(Context));
    Context.szTrackPath = argv[1];
    Context.szAssetRoot = argv[2];

    SDL_SetMainReady();
    if (RollerEd_Bootstrap(&BootstrapInfo) != ROLLER_ED_RESULT_OK) {
        fprintf(stderr, "RollerEd_Bootstrap failed: %s\n", RollerEd_GetLastError());
        return 1;
    }
    pWorker = SDL_CreateThread(
        renderer_switch_worker, "editor-renderer-switch", &Context);
    if (!pWorker) {
        fprintf(stderr, "worker creation failed: %s\n", SDL_GetError());
        RollerEd_Teardown();
        return 1;
    }
    SDL_WaitThread(pWorker, &iWorkerResult);
    if (RollerEd_Teardown() != ROLLER_ED_RESULT_OK && iWorkerResult == 0) {
        fprintf(stderr, "RollerEd_Teardown failed: %s\n", RollerEd_GetLastError());
        return 1;
    }
    if (iWorkerResult != 0) {
        fprintf(stderr, "E1-S8 acceptance failed: %s\n", Context.szError);
        return 1;
    }
    puts("E1-S8 PASS: renderer availability and loaded-scene software/GPU toggles preserved RGBA8 rendering and scene identity");
    return 0;
}
