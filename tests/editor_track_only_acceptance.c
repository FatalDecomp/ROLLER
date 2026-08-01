#include "3d.h"
#include "car.h"
#include "drawtrk3.h"
#include "editor_api.h"
#include "editor_camera.h"
#include "game_render.h"
#include "loadtrak.h"
#include "moving.h"
#include "render_queue_3d.h"
#include "scene_render_gpu.h"

#define SDL_MAIN_HANDLED 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <math.h>
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
} tTrackOnlyContext;

static void acceptance_error(tTrackOnlyContext *pContext, const char *szMessage)
{
    snprintf(pContext->szError, sizeof(pContext->szError), "%s: %s",
             szMessage, RollerEd_GetLastError());
    pContext->iResult = 1;
}

static int queue_has_kind(const RenderQueue3D *pQueue, RenderCommand3DKind eKind)
{
    int iCount = render_queue_3d_count(pQueue);

    for (int i = 0; i < iCount; ++i) {
        const RenderCommand3D *pCommand = render_queue_3d_command_at(pQueue, i);
        if (pCommand && pCommand->kind == eKind)
            return -1;
    }
    return 0;
}

static int pixels_have_scene_content(const uint8_t *pPixels, size_t uiSize)
{
    uint32_t uiFirst;

    if (uiSize < sizeof(uiFirst))
        return 0;
    memcpy(&uiFirst, pPixels, sizeof(uiFirst));
    for (size_t i = sizeof(uiFirst); i + sizeof(uint32_t) <= uiSize;
         i += sizeof(uint32_t)) {
        uint32_t uiPixel;
        memcpy(&uiPixel, pPixels + i, sizeof(uiPixel));
        if (uiPixel != uiFirst)
            return -1;
    }
    return 0;
}

static int verify_projection_resolution_independence(
    tTrackOnlyContext *pContext, uint8_t *pPixels, size_t uiBufferSize,
    uint32_t uiRowPitch)
{
    static const uint32_t auiWidth[] = { 320u, 640u };
    static const uint32_t auiHeight[] = { 200u, 400u };
    SceneRendererGPU *pGPU = game_render_get_gpu(g_pGameRenderer);
    float fReferenceX = 0.0f;
    float fReferenceY = 0.0f;

    if (!pGPU) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "GPU renderer unavailable during projection check");
        pContext->iResult = 1;
        return 0;
    }
    for (size_t i = 0; i < sizeof(auiWidth) / sizeof(auiWidth[0]); ++i) {
        float fNdcX;
        float fNdcY;

        memset(pPixels, 0, uiBufferSize);
        if (RollerEd_RenderFrame(
                pPixels, (uint32_t)uiBufferSize, uiRowPitch,
                auiWidth[i], auiHeight[i], ROLLER_ED_PIXEL_RGBA8)
                != ROLLER_ED_RESULT_OK) {
            acceptance_error(
                pContext, "RollerEd_RenderFrame failed during projection check");
            return 0;
        }
        if (!scene_render_gpu_project_to_ndc(
                pGPU, 100.0, 50.0, 1000.0, &fNdcX, &fNdcY)) {
            snprintf(pContext->szError, sizeof(pContext->szError),
                     "camera-space projection failed during resolution check");
            pContext->iResult = 1;
            return 0;
        }
        if (i == 0) {
            fReferenceX = fNdcX;
            fReferenceY = fNdcY;
        } else if (fabsf(fNdcX - fReferenceX) > 0.00001f
                || fabsf(fNdcY - fReferenceY) > 0.00001f) {
            snprintf(pContext->szError, sizeof(pContext->szError),
                     "editor projection changed with proportional viewport: "
                     "(%.6f, %.6f) vs (%.6f, %.6f)",
                     fReferenceX, fReferenceY, fNdcX, fNdcY);
            pContext->iResult = 1;
            return 0;
        }
    }
    return -1;
}

static int nearest_chunk_to(float fX, float fY, float fZ)
{
    double dBestDistanceSquared = HUGE_VAL;
    int iBestChunk = -1;

    for (int iChunk = 0; iChunk < TRAK_LEN; ++iChunk) {
        const tVec3 *pCenter = &localdata[iChunk].pointAy[3];
        double dDeltaX = -(double)pCenter->fX - (double)fX;
        double dDeltaY = -(double)pCenter->fY - (double)fY;
        double dDeltaZ = -(double)pCenter->fZ - (double)fZ;
        double dDistanceSquared = dDeltaX * dDeltaX
                                + dDeltaY * dDeltaY
                                + dDeltaZ * dDeltaZ;
        if (dDistanceSquared < dBestDistanceSquared) {
            dBestDistanceSquared = dDistanceSquared;
            iBestChunk = iChunk;
        }
    }
    return iBestChunk;
}

static int chunk_forward_yaw_degrees(int iChunk, float *pfYawDegrees)
{
    for (int iStep = 1; iStep < TRAK_LEN; ++iStep) {
        int iNextChunk = iChunk + iStep;
        double dTrackX;
        double dTrackY;

        if (iNextChunk >= TRAK_LEN)
            iNextChunk -= TRAK_LEN;
        dTrackX = (double)localdata[iChunk].pointAy[3].fX
                - (double)localdata[iNextChunk].pointAy[3].fX;
        dTrackY = (double)localdata[iChunk].pointAy[3].fY
                - (double)localdata[iNextChunk].pointAy[3].fY;
        if (dTrackX * dTrackX + dTrackY * dTrackY > 0.000001) {
            *pfYawDegrees = (float)(atan2(dTrackY, dTrackX)
                                  * 57.29577951308232);
            return -1;
        }
    }
    *pfYawDegrees = 0.0f;
    return 0;
}

static int verify_visibility_at(tTrackOnlyContext *pContext,
                                float fX, float fY, float fZ,
                                int bCheckBothDirections)
{
    int iExpectedChunk = nearest_chunk_to(fX, fY, fZ);
    float fForwardYaw;
    int bHasDirection = chunk_forward_yaw_degrees(
        iExpectedChunk, &fForwardYaw);
    int iDirectionCount = bCheckBothDirections && bHasDirection ? 2 : 1;

    for (int iDirection = 0; iDirection < iDirectionCount; ++iDirection) {
        tEdCameraState Camera = {
            .uiStructSize = sizeof(Camera),
            .uiVersion = ROLLER_ED_CAMERA_STATE_VERSION,
            .fPosition = { fX, fY, fZ },
            .fYawDegrees = fForwardYaw + (iDirection ? 180.0f : 0.0f),
            .fPitchDegrees = 0.0f
        };
        int iAnchorChunk;
        int iExpectedBackwards = iDirection ? -1 : 0;

        if (RollerEd_SetCamera(&Camera) != ROLLER_ED_RESULT_OK) {
            acceptance_error(pContext, "RollerEd_SetCamera failed during visibility sweep");
            return 0;
        }
        if (!roller_ed_camera_apply()) {
            snprintf(pContext->szError, sizeof(pContext->szError),
                     "explicit camera did not apply during visibility sweep");
            pContext->iResult = 1;
            return 0;
        }
        iAnchorChunk = CalcVisibleTrackEditor(0u);
        if (iAnchorChunk != iExpectedChunk
                || TrackSize != TRAK_LEN - 1
                || first_size != TrackSize
                || gap_size != 6 * TRAK_LEN
                || (bHasDirection && backwards != iExpectedBackwards)) {
            snprintf(pContext->szError, sizeof(pContext->szError),
                     "editor visibility mismatch at camera (%.1f, %.1f, %.1f): "
                     "anchor=%d expected=%d backwards=%d expected=%d "
                     "range=%d/%d gap=%d",
                     fX, fY, fZ, iAnchorChunk, iExpectedChunk,
                     backwards, iExpectedBackwards,
                     TrackSize, first_size, gap_size);
            pContext->iResult = 1;
            return 0;
        }
        if ((backwards && start_sect != (iExpectedChunk + 1) % TRAK_LEN)
                || (!backwards && start_sect != iExpectedChunk)) {
            snprintf(pContext->szError, sizeof(pContext->szError),
                     "editor visibility start section did not follow camera direction");
            pContext->iResult = 1;
            return 0;
        }
    }
    return -1;
}

static int verify_editor_visibility_sweep(tTrackOnlyContext *pContext)
{
    for (int iChunk = 0; iChunk < TRAK_LEN; ++iChunk) {
        const tVec3 *pCenter = &localdata[iChunk].pointAy[3];
        if (!verify_visibility_at(
                pContext, -pCenter->fX, -pCenter->fY, -pCenter->fZ, -1))
            return 0;
    }

    if (!verify_visibility_at(
            pContext, 10000000.0f, -20000000.0f, 5000000.0f, -1))
        return 0;
    if (numcars != 0) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "editor visibility sweep mutated numcars");
        pContext->iResult = 1;
        return 0;
    }
    return -1;
}

static int SDLCALL track_only_worker(void *pUserData)
{
    enum { WIDTH = 640, HEIGHT = 480, ROW_PITCH = WIDTH * 4 };
    static const float afYaw[] = { 0.0f, 90.0f, 180.0f, 270.0f };
    static const float afPitch[] = { -25.0f, 0.0f, 25.0f };
    tTrackOnlyContext *pContext = (tTrackOnlyContext *)pUserData;
    tRollerEdInitInfo InitInfo = {
        .uiStructSize = sizeof(InitInfo),
        .uiVersion = ROLLER_ED_INIT_INFO_VERSION,
        .szAssetRoot = pContext->szAssetRoot,
        .ePreferredRenderer = ROLLER_ED_RENDERER_GPU,
        .uiAllowSoftwareFallback = 0u
    };
    uint8_t *pPixels = NULL;
    float fTargetX = 0.0f;
    float fTargetY = 0.0f;
    float fTargetZ = 0.0f;
    int bFoundTrackFrame = 0;

    if (RollerEd_Init(&InitInfo) != ROLLER_ED_RESULT_OK) {
        acceptance_error(pContext, "RollerEd_Init failed");
        return pContext->iResult;
    }
    if (RollerEd_LoadTrackFile(pContext->szTrackPath, pContext->szAssetRoot)
            != ROLLER_ED_RESULT_OK) {
        acceptance_error(pContext, "RollerEd_LoadTrackFile failed");
        goto shutdown;
    }
    if (!roller_ed_track_only_active() || numcars != 0) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "editor load did not commit track-only zero-car mode");
        pContext->iResult = 1;
        goto shutdown;
    }
    if (!verify_editor_visibility_sweep(pContext))
        goto shutdown;

    for (int iPoint = 0; iPoint < 6; ++iPoint) {
        fTargetX += TrakPt[0].pointAy[iPoint].fX;
        fTargetY += TrakPt[0].pointAy[iPoint].fY;
        fTargetZ += TrakPt[0].pointAy[iPoint].fZ;
    }
    fTargetX /= 6.0f;
    fTargetY /= 6.0f;
    fTargetZ /= 6.0f;

    pPixels = (uint8_t *)malloc((size_t)ROW_PITCH * HEIGHT);
    if (!pPixels) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "pixel allocation failed");
        pContext->iResult = 1;
        goto shutdown;
    }

    for (int iLight = 0; iLight < 3; ++iLight) {
        SLight[0][iLight].currentPos.fX = fTargetX;
        SLight[0][iLight].currentPos.fY = fTargetY;
        SLight[0][iLight].currentPos.fZ = fTargetZ + 500.0f;
    }
    countdown = 0;
    replaytype = 0;
    game_type = 0;
    winner_mode = 0;

    if (!verify_projection_resolution_independence(
            pContext, pPixels, (size_t)ROW_PITCH * HEIGHT, ROW_PITCH))
        goto shutdown;

    for (size_t iPitch = 0;
         iPitch < sizeof(afPitch) / sizeof(afPitch[0]) && !bFoundTrackFrame;
         ++iPitch) {
        for (size_t iYaw = 0;
             iYaw < sizeof(afYaw) / sizeof(afYaw[0]) && !bFoundTrackFrame;
             ++iYaw) {
            tEdCameraState Camera = {
                .uiStructSize = sizeof(Camera),
                .uiVersion = ROLLER_ED_CAMERA_STATE_VERSION,
                .fPosition = { fTargetX - 4000.0f, fTargetY,
                               fTargetZ + 1600.0f },
                .fYawDegrees = afYaw[iYaw],
                .fPitchDegrees = afPitch[iPitch]
            };
            RenderQueue3D *pQueue;

            if (RollerEd_SetCamera(&Camera) != ROLLER_ED_RESULT_OK) {
                acceptance_error(pContext, "RollerEd_SetCamera failed");
                goto shutdown;
            }
            memset(pPixels, 0, (size_t)ROW_PITCH * HEIGHT);
            if (RollerEd_RenderFrame(
                    pPixels, ROW_PITCH * HEIGHT, ROW_PITCH, WIDTH, HEIGHT,
                    ROLLER_ED_PIXEL_RGBA8) != ROLLER_ED_RESULT_OK) {
                acceptance_error(pContext, "RollerEd_RenderFrame failed");
                goto shutdown;
            }

            pQueue = render_queue_3d_global();
            if (queue_has_kind(pQueue, RENDER_COMMAND_3D_KIND_CAR)
                    || queue_has_kind(pQueue, RENDER_COMMAND_3D_KIND_START_LIGHT)) {
                snprintf(pContext->szError, sizeof(pContext->szError),
                         "editor frame queued a gameplay car or race-start cube");
                pContext->iResult = 1;
                goto shutdown;
            }
            bFoundTrackFrame =
                (queue_has_kind(pQueue, RENDER_COMMAND_3D_KIND_ROAD_SURFACE)
                 || queue_has_kind(pQueue, RENDER_COMMAND_3D_KIND_GROUND_SURFACE))
                && queue_has_kind(pQueue, RENDER_COMMAND_3D_KIND_BUILDING)
                && pixels_have_scene_content(
                    pPixels, (size_t)ROW_PITCH * HEIGHT);
        }
    }

    if (!bFoundTrackFrame) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "explicit camera produced no visible track/scenery content");
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
    tTrackOnlyContext Context;
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
    pWorker = SDL_CreateThread(track_only_worker, "editor-track-only", &Context);
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
        fprintf(stderr, "E1-S6b acceptance failed: %s\n", Context.szError);
        return 1;
    }
    puts("E1-S6b PASS: camera-derived visibility covered the full track and a far-off camera without cars or race-start cubes");
    return 0;
}
