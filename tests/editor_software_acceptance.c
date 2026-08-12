#include "3d.h"
#include "editor_api.h"
#include "game_render.h"
#include "moving.h"
#include "render_queue_3d.h"
#include "sound.h"

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
    int bRequireStuntAnimation;
    char szError[512];
    int iResult;
} tSoftwareContext;

static void acceptance_error(tSoftwareContext *pContext, const char *szMessage)
{
    snprintf(pContext->szError, sizeof(pContext->szError), "%s: %s",
             szMessage, RollerEd_GetLastError());
    pContext->iResult = 1;
}

static int pixels_have_content(const uint8_t *pPixels, uint32_t uiRowPitch,
                               uint32_t uiWidth, uint32_t uiHeight)
{
    uint32_t uiFirst;

    memcpy(&uiFirst, pPixels, sizeof(uiFirst));
    for (uint32_t uiY = 0; uiY < uiHeight; ++uiY) {
        const uint8_t *pRow = pPixels + (size_t)uiY * uiRowPitch;
        for (uint32_t uiX = 0; uiX < uiWidth; ++uiX) {
            uint32_t uiPixel;
            memcpy(&uiPixel, pRow + uiX * 4u, sizeof(uiPixel));
            if (uiPixel != uiFirst)
                return -1;
        }
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

static int shade_palette_has_content(void)
{
    for (size_t i = 1; i < 5u * 256u; ++i) {
        if (shade_palette[i] != 0u)
            return -1;
    }
    return 0;
}

static int verify_stunt_animation(tSoftwareContext *pContext)
{
    tGroundPt *pBefore;
    uint32_t uiTicksToTry = 1u;

    if (totalramps <= 0) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "track has no moving stunts to animate");
        pContext->iResult = 1;
        return 0;
    }
    for (int iRamp = 0; iRamp < totalramps; ++iRamp) {
        const tStuntData *pStunt = ramp[iRamp];
        uint64_t ullCycle;

        if (!pStunt)
            continue;
        ullCycle = (uint64_t)(pStunt->iNumTicks > 0
                                 ? pStunt->iNumTicks : 0) * 2u
            + (uint64_t)(pStunt->iTimeBulging > 0
                             ? pStunt->iTimeBulging : 0)
            + (uint64_t)(pStunt->iTimeFlat > 0
                             ? pStunt->iTimeFlat : 0)
            + 2u;
        if (ullCycle > uiTicksToTry)
            uiTicksToTry = ullCycle > 4096u ? 4096u : (uint32_t)ullCycle;
    }

    pBefore = (tGroundPt *)malloc(sizeof(TrakPt));
    if (!pBefore) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "could not allocate stunt animation comparison");
        pContext->iResult = 1;
        return 0;
    }
    memcpy(pBefore, TrakPt, sizeof(TrakPt));
    for (uint32_t uiTick = 0; uiTick < uiTicksToTry; ++uiTick) {
        if (RollerEd_AdvanceStunts(1u) != ROLLER_ED_RESULT_OK) {
            free(pBefore);
            acceptance_error(pContext, "RollerEd_AdvanceStunts failed");
            return 0;
        }
        if (memcmp(pBefore, TrakPt, sizeof(TrakPt)) != 0) {
            free(pBefore);
            return -1;
        }
    }
    free(pBefore);
    snprintf(pContext->szError, sizeof(pContext->szError),
             "stunt ticks did not change legacy track geometry");
    pContext->iResult = 1;
    return 0;
}

static int pixel_matches_palette(const uint8_t *pRGBA, uint8_t byIndex)
{
    const tColor *pColour = &pal_addr[byIndex];

    return pRGBA[0] == (uint8_t)(pColour->byR * 255u / 63u)
        && pRGBA[1] == (uint8_t)(pColour->byG * 255u / 63u)
        && pRGBA[2] == (uint8_t)(pColour->byB * 255u / 63u)
        && pRGBA[3] == 255u;
}

static int verify_native_rgba(tSoftwareContext *pContext,
                              const uint8_t *pPixels,
                              uint32_t uiRowPitch,
                              uint32_t uiWidth,
                              uint32_t uiHeight,
                              uint8_t byPaddingValue)
{
    for (uint32_t uiY = 0; uiY < uiHeight; ++uiY) {
        const uint8_t *pRow = pPixels + (size_t)uiY * uiRowPitch;
        const uint8_t *pIndexedRow = scrbuf + (size_t)uiY * uiWidth;

        for (uint32_t uiX = 0; uiX < uiWidth; ++uiX) {
            if (!pixel_matches_palette(pRow + uiX * 4u, pIndexedRow[uiX])) {
                snprintf(pContext->szError, sizeof(pContext->szError),
                         "native RGBA mismatch at %u,%u", uiX, uiY);
                pContext->iResult = 1;
                return 0;
            }
        }
        for (uint32_t uiByte = uiWidth * 4u; uiByte < uiRowPitch; ++uiByte) {
            if (pRow[uiByte] != byPaddingValue) {
                snprintf(pContext->szError, sizeof(pContext->szError),
                         "native readback overwrote caller row padding");
                pContext->iResult = 1;
                return 0;
            }
        }
    }
    return -1;
}

static int verify_scaled_rgba(tSoftwareContext *pContext,
                              const uint8_t *pPixels,
                              uint32_t uiRowPitch,
                              uint32_t uiWidth,
                              uint32_t uiHeight,
                              uint32_t uiNativeWidth,
                              uint32_t uiNativeHeight,
                              uint8_t byPaddingValue)
{
    uint32_t uiScaledWidth;
    uint32_t uiScaledHeight;
    uint32_t uiOffsetX;
    uint32_t uiOffsetY;

    if ((uint64_t)uiWidth * uiNativeHeight
            <= (uint64_t)uiHeight * uiNativeWidth) {
        uiScaledWidth = uiWidth;
        uiScaledHeight = (uint32_t)(
            (uint64_t)uiWidth * uiNativeHeight / uiNativeWidth);
    } else {
        uiScaledHeight = uiHeight;
        uiScaledWidth = (uint32_t)(
            (uint64_t)uiHeight * uiNativeWidth / uiNativeHeight);
    }
    uiOffsetX = (uiWidth - uiScaledWidth) / 2u;
    uiOffsetY = (uiHeight - uiScaledHeight) / 2u;

    for (uint32_t uiY = 0; uiY < uiHeight; ++uiY) {
        const uint8_t *pRow = pPixels + (size_t)uiY * uiRowPitch;
        for (uint32_t uiX = 0; uiX < uiWidth; ++uiX) {
            const uint8_t *pPixel = pRow + uiX * 4u;
            int bInside = uiX >= uiOffsetX && uiX < uiOffsetX + uiScaledWidth
                       && uiY >= uiOffsetY && uiY < uiOffsetY + uiScaledHeight;

            if (!bInside) {
                if (pPixel[0] != 0u || pPixel[1] != 0u
                        || pPixel[2] != 0u || pPixel[3] != 255u) {
                    snprintf(pContext->szError, sizeof(pContext->szError),
                             "letterbox pixel was not opaque black at %u,%u",
                             uiX, uiY);
                    pContext->iResult = 1;
                    return 0;
                }
            } else {
                uint32_t uiSourceX = (uint32_t)(
                    (uint64_t)(uiX - uiOffsetX) * uiNativeWidth
                    / uiScaledWidth);
                uint32_t uiSourceY = (uint32_t)(
                    (uint64_t)(uiY - uiOffsetY) * uiNativeHeight
                    / uiScaledHeight);
                uint8_t byIndex = scrbuf[(size_t)uiSourceY * uiNativeWidth
                                       + uiSourceX];
                if (!pixel_matches_palette(pPixel, byIndex)) {
                    snprintf(pContext->szError, sizeof(pContext->szError),
                             "nearest-neighbour RGBA mismatch at %u,%u",
                             uiX, uiY);
                    pContext->iResult = 1;
                    return 0;
                }
            }
        }
        for (uint32_t uiByte = uiWidth * 4u; uiByte < uiRowPitch; ++uiByte) {
            if (pRow[uiByte] != byPaddingValue) {
                snprintf(pContext->szError, sizeof(pContext->szError),
                         "scaled readback overwrote caller row padding");
                pContext->iResult = 1;
                return 0;
            }
        }
    }
    return -1;
}

static int SDLCALL software_worker(void *pUserData)
{
    static const float afYaw[] = { 0.0f, 90.0f, 180.0f, 270.0f };
    static const float afPitch[] = { -25.0f, 0.0f, 25.0f };
    enum { PADDING = 16, LETTERBOX_EXTRA_HEIGHT = 80 };
    tSoftwareContext *pContext = (tSoftwareContext *)pUserData;
    tRollerEdInitInfo InitInfo = {
        .uiStructSize = sizeof(InitInfo),
        .uiVersion = ROLLER_ED_INIT_INFO_VERSION,
        .szAssetRoot = pContext->szAssetRoot,
        .ePreferredRenderer = ROLLER_ED_RENDERER_SOFTWARE,
        .uiAllowSoftwareFallback = 0u
    };
    uint8_t *pNativePixels = NULL;
    uint8_t *pScaledPixels = NULL;
    uint32_t uiNativeWidth;
    uint32_t uiNativeHeight;
    uint32_t uiNativePitch;
    uint32_t uiScaledWidth;
    uint32_t uiScaledHeight;
    uint32_t uiScaledPitch;
    float fTargetX = 0.0f;
    float fTargetY = 0.0f;
    float fTargetZ = 0.0f;
    int bFoundContent = 0;

    if (RollerEd_Init(&InitInfo) != ROLLER_ED_RESULT_OK) {
        acceptance_error(pContext, "RollerEd_Init failed");
        return pContext->iResult;
    }
    if (RollerEd_LoadTrackFile(pContext->szTrackPath, pContext->szAssetRoot)
            != ROLLER_ED_RESULT_OK) {
        acceptance_error(pContext, "software RollerEd_LoadTrackFile failed");
        goto shutdown;
    }
    if (!g_pGameRenderer || game_render_get_device(g_pGameRenderer)
            || game_render_get_gpu(g_pGameRenderer)) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "software editor path created or required a GPU device");
        pContext->iResult = 1;
        goto shutdown;
    }
    if (!shade_palette_has_content()) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "software transparency shade palette was not initialized");
        pContext->iResult = 1;
        goto shutdown;
    }
    if (pContext->bRequireStuntAnimation
            && !verify_stunt_animation(pContext))
        goto shutdown;

    uiNativeWidth = XMAX > 0 ? (uint32_t)XMAX : 320u;
    uiNativeHeight = YMAX > 0 ? (uint32_t)YMAX : 200u;
    uiNativePitch = uiNativeWidth * 4u + PADDING;
    uiScaledWidth = uiNativeWidth * 2u;
    uiScaledHeight = uiNativeHeight * 2u + LETTERBOX_EXTRA_HEIGHT;
    uiScaledPitch = uiScaledWidth * 4u + PADDING;
    pNativePixels = (uint8_t *)malloc((size_t)uiNativePitch * uiNativeHeight);
    pScaledPixels = (uint8_t *)malloc((size_t)uiScaledPitch * uiScaledHeight);
    if (!pNativePixels || !pScaledPixels) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "software acceptance pixel allocation failed");
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
         iPitch < sizeof(afPitch) / sizeof(afPitch[0]) && !bFoundContent;
         ++iPitch) {
        for (size_t iYaw = 0;
             iYaw < sizeof(afYaw) / sizeof(afYaw[0]) && !bFoundContent;
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
                acceptance_error(pContext, "software RollerEd_SetCamera failed");
                goto shutdown;
            }
            memset(pNativePixels, 0xa5,
                   (size_t)uiNativePitch * uiNativeHeight);
            if (RollerEd_RenderFrame(
                    pNativePixels, uiNativePitch * uiNativeHeight,
                    uiNativePitch, uiNativeWidth, uiNativeHeight,
                    ROLLER_ED_PIXEL_RGBA8) != ROLLER_ED_RESULT_OK) {
                acceptance_error(pContext, "native software render failed");
                goto shutdown;
            }
            bFoundContent = pixels_have_content(
                pNativePixels, uiNativePitch, uiNativeWidth, uiNativeHeight)
                && queue_has_track(render_queue_3d_global());
        }
    }
    if (!bFoundContent) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "software facade produced no native track content");
        pContext->iResult = 1;
        goto shutdown;
    }
    if (!verify_native_rgba(
            pContext, pNativePixels, uiNativePitch,
            uiNativeWidth, uiNativeHeight, 0xa5u))
        goto shutdown;

    memset(pScaledPixels, 0x5a, (size_t)uiScaledPitch * uiScaledHeight);
    if (RollerEd_RenderFrame(
            pScaledPixels, uiScaledPitch * uiScaledHeight,
            uiScaledPitch, uiScaledWidth, uiScaledHeight,
            ROLLER_ED_PIXEL_RGBA8) != ROLLER_ED_RESULT_OK) {
        acceptance_error(pContext, "scaled software render failed");
        goto shutdown;
    }
    if (!verify_scaled_rgba(
            pContext, pScaledPixels, uiScaledPitch,
            uiScaledWidth, uiScaledHeight,
            uiNativeWidth, uiNativeHeight, 0x5au))
        goto shutdown;

shutdown:
    free(pScaledPixels);
    free(pNativePixels);
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
    tSoftwareContext Context;
    SDL_Thread *pWorker;
    int iWorkerResult = 1;

    if (argc != 3 && argc != 4) {
        fprintf(stderr, "usage: %s ABSOLUTE_TRACK_PATH ABSOLUTE_ASSET_ROOT "
                        "[--require-stunt-animation]\n",
                argv[0]);
        return 2;
    }
    memset(&Context, 0, sizeof(Context));
    Context.szTrackPath = argv[1];
    Context.szAssetRoot = argv[2];
    Context.bRequireStuntAnimation = argc == 4
        && strcmp(argv[3], "--require-stunt-animation") == 0;
    if (argc == 4 && !Context.bRequireStuntAnimation) {
        fprintf(stderr, "unknown option: %s\n", argv[3]);
        return 2;
    }

    SDL_SetMainReady();
    if (RollerEd_Bootstrap(&BootstrapInfo) != ROLLER_ED_RESULT_OK) {
        fprintf(stderr, "RollerEd_Bootstrap failed: %s\n", RollerEd_GetLastError());
        return 1;
    }
    pWorker = SDL_CreateThread(software_worker, "editor-software", &Context);
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
        fprintf(stderr, "E1-S7 acceptance failed: %s\n", Context.szError);
        return 1;
    }
    if (Context.bRequireStuntAnimation)
        puts("PASS: Track 7 moving stunts changed ROLLER's live editor geometry");
    else
        puts("E1-S7 PASS: GPU-free software rendering produced exact native RGBA8 and nearest-neighbour opaque-black letterboxing");
    return 0;
}
