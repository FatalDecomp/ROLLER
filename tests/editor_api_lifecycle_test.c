#include "editor_api.h"
#include "editor_legacy_scene.h"
#include "editor_track_loader.h"

#define SDL_MAIN_HANDLED 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct
{
    SDL_Semaphore *pReady;
    SDL_Semaphore *pContinue;
    const char *szValidTrack;
    const char *szMalformedTrack;
    int iFailureLine;
} tLifecycleTestContext;

static int s_iThreadAssertionCount;
static int s_iLegacyInstallCount;
static int s_iLegacyRenderCount;
static int s_iLegacySetCameraCount;
static eRollerEdRenderer s_eLastPreferredRenderer;
static uint32_t s_uiLastAllowSoftwareFallback;
static tEdCameraState s_LastLegacyCamera;

eRollerEdResult roller_ed_legacy_scene_install(
    const char *szTrackPath,
    const tEdTrackStage *pStage,
    const char *szDocumentAssetRoot,
    const char *szFallbackAssetRoot,
    eRollerEdRenderer ePreferredRenderer,
    uint32_t uiAllowSoftwareFallback,
    char *szError,
    size_t uiErrorCapacity)
{
    if (!szTrackPath || !pStage || !pStage->pbyData
            || !szDocumentAssetRoot || !szFallbackAssetRoot) {
        snprintf(szError, uiErrorCapacity, "invalid legacy install seam input");
        return ROLLER_ED_RESULT_INVALID_ARGUMENT;
    }
    s_eLastPreferredRenderer = ePreferredRenderer;
    s_uiLastAllowSoftwareFallback = uiAllowSoftwareFallback;
    s_iLegacyInstallCount++;
    if (uiErrorCapacity)
        szError[0] = '\0';
    return ROLLER_ED_RESULT_OK;
}

eRollerEdResult roller_ed_legacy_scene_render(
    uint8_t *pbyPixels,
    uint32_t uiBufferSize,
    uint32_t uiRowPitch,
    uint32_t uiWidth,
    uint32_t uiHeight,
    char *szError,
    size_t uiErrorCapacity)
{
    (void)uiBufferSize;
    for (uint32_t iRow = 0; iRow < uiHeight; ++iRow)
        memset(pbyPixels + iRow * uiRowPitch, 0x7c, uiWidth * 4u);
    s_iLegacyRenderCount++;
    if (uiErrorCapacity)
        szError[0] = '\0';
    return ROLLER_ED_RESULT_OK;
}

eRollerEdResult roller_ed_legacy_scene_set_camera(
    const tEdCameraState *pCamera,
    char *szError,
    size_t uiErrorCapacity)
{
    if (!pCamera) {
        snprintf(szError, uiErrorCapacity, "camera is required");
        return ROLLER_ED_RESULT_INVALID_ARGUMENT;
    }
    s_LastLegacyCamera = *pCamera;
    s_iLegacySetCameraCount++;
    if (uiErrorCapacity)
        szError[0] = '\0';
    return ROLLER_ED_RESULT_OK;
}

void roller_ed_legacy_scene_unload(void)
{
}

void roller_ed_legacy_scene_shutdown(void)
{
}

static SDL_AssertState SDLCALL count_thread_assertion(
    const SDL_AssertData *pData, void *pUserData)
{
    (void)pData;
    (void)pUserData;
    s_iThreadAssertionCount++;
    return SDL_ASSERTION_IGNORE;
}

static int check_condition(bool bCondition, int iLine)
{
    if (!bCondition)
        fprintf(stderr, "editor API lifecycle check failed at line %d\n", iLine);
    return bCondition ? 0 : iLine;
}

#define CHECK_MAIN(condition) \
    do { \
        int iCheck = check_condition((condition), __LINE__); \
        if (iCheck != 0) \
            return iCheck; \
    } while (0)

#define CHECK_WORKER(condition) \
    do { \
        int iCheck = check_condition((condition), __LINE__); \
        if (iCheck != 0) { \
            pContext->iFailureLine = iCheck; \
            goto publish_failure; \
        } \
    } while (0)

static int SDLCALL bootstrap_from_wrong_thread(void *pUserData)
{
    const tRollerEdBootstrapInfo *pInfo =
        (const tRollerEdBootstrapInfo *)pUserData;
    return RollerEd_Bootstrap(pInfo) == ROLLER_ED_RESULT_WRONG_THREAD ? 0 : 1;
}

static int SDLCALL lifecycle_worker(void *pUserData)
{
    tLifecycleTestContext *pContext = (tLifecycleTestContext *)pUserData;
    tRollerEdInitInfo InitInfo = {
        .uiStructSize = sizeof(InitInfo),
        .uiVersion = ROLLER_ED_INIT_INFO_VERSION,
        .szAssetRoot = "facade-test-assets",
        .ePreferredRenderer = ROLLER_ED_RENDERER_GPU,
        .uiAllowSoftwareFallback = 1u
    };
    tRollerEdInitInfo InvalidInfo = InitInfo;
    tEdGeometrySizes Sizes = {
        .uiStructSize = sizeof(Sizes),
        .uiVersion = ROLLER_ED_GEOMETRY_SIZES_VERSION
    };
    uint32_t uiInitialEpoch;
    uint32_t uiInitialGeneration;

    InvalidInfo.uiVersion++;
    CHECK_WORKER(RollerEd_Init(&InvalidInfo)
                 == ROLLER_ED_RESULT_INVALID_VERSION);
    CHECK_WORKER(RollerEd_Init(&InitInfo) == ROLLER_ED_RESULT_OK);
    CHECK_WORKER(RollerEd_Init(&InitInfo) == ROLLER_ED_RESULT_INVALID_STATE);
    CHECK_WORKER(RollerEd_GetAvailableRenderers() == 0u);
    CHECK_WORKER(RollerEd_QueryGeometrySizes(&Sizes) == ROLLER_ED_RESULT_OK);
    CHECK_WORKER(Sizes.uiSceneState == ROLLER_ED_SCENE_EMPTY);
    CHECK_WORKER(Sizes.uiVertexCount == 0u && Sizes.uiIndexCount == 0u
                 && Sizes.uiPrimitiveCount == 0u && Sizes.uiMaterialCount == 0u);
    CHECK_WORKER(Sizes.uiVertexStride == sizeof(tEdVertex));
    CHECK_WORKER(Sizes.uiPrimitiveStride == sizeof(tEdPrimitive));
    CHECK_WORKER(Sizes.uiMaterialStride == sizeof(tEdMaterial));
    uiInitialEpoch = Sizes.uiGeometryEpoch;
    uiInitialGeneration = Sizes.uiTrackGeneration;

    SDL_SignalSemaphore(pContext->pReady);
    SDL_WaitSemaphore(pContext->pContinue);

    {
        tEdCameraState Camera = {
            .uiStructSize = sizeof(Camera),
            .uiVersion = ROLLER_ED_CAMERA_STATE_VERSION,
            .fPosition = { 125.0f, -250.0f, 375.0f },
            .fYawDegrees = 450.0f,
            .fPitchDegrees = -45.0f
        };
        tEdCameraState InvalidCamera = Camera;

        CHECK_WORKER(RollerEd_SetCamera(&Camera) == ROLLER_ED_RESULT_OK);
        CHECK_WORKER(s_iLegacySetCameraCount == 1);
        CHECK_WORKER(memcmp(&s_LastLegacyCamera, &Camera, sizeof(Camera)) == 0);
        Sizes.uiStructSize = sizeof(Sizes);
        Sizes.uiVersion = ROLLER_ED_GEOMETRY_SIZES_VERSION;
        CHECK_WORKER(RollerEd_QueryGeometrySizes(&Sizes)
                     == ROLLER_ED_RESULT_OK);
        CHECK_WORKER(Sizes.uiGeometryEpoch == uiInitialEpoch);
        CHECK_WORKER(Sizes.uiTrackGeneration == uiInitialGeneration);

        InvalidCamera.uiVersion++;
        CHECK_WORKER(RollerEd_SetCamera(&InvalidCamera)
                     == ROLLER_ED_RESULT_INVALID_VERSION);
        InvalidCamera = Camera;
        InvalidCamera.uiStructSize = sizeof(InvalidCamera) - 1u;
        CHECK_WORKER(RollerEd_SetCamera(&InvalidCamera)
                     == ROLLER_ED_RESULT_INVALID_ARGUMENT);
        InvalidCamera = Camera;
        InvalidCamera.fPosition[1] = INFINITY;
        CHECK_WORKER(RollerEd_SetCamera(&InvalidCamera)
                     == ROLLER_ED_RESULT_INVALID_ARGUMENT);
        CHECK_WORKER(strstr(RollerEd_GetLastError(), "finite") != NULL);
        InvalidCamera = Camera;
        InvalidCamera.fYawDegrees = NAN;
        CHECK_WORKER(RollerEd_SetCamera(&InvalidCamera)
                     == ROLLER_ED_RESULT_INVALID_ARGUMENT);
        CHECK_WORKER(s_iLegacySetCameraCount == 1);
    }
    {
        tEdGeometrySizes InvalidSizes;
        tEdGeometrySizes Before;

        memset(&InvalidSizes, 0xa5, sizeof(InvalidSizes));
        InvalidSizes.uiStructSize = sizeof(InvalidSizes);
        InvalidSizes.uiVersion = ROLLER_ED_GEOMETRY_SIZES_VERSION + 1u;
        Before = InvalidSizes;
        CHECK_WORKER(RollerEd_QueryGeometrySizes(&InvalidSizes)
                     == ROLLER_ED_RESULT_INVALID_VERSION);
        CHECK_WORKER(memcmp(&InvalidSizes, &Before, sizeof(Before)) == 0);
    }
    {
        uint8_t abyPixels[64];
        uint8_t abyBefore[64];

        memset(abyPixels, 0x5a, sizeof(abyPixels));
        memcpy(abyBefore, abyPixels, sizeof(abyPixels));
        CHECK_WORKER(RollerEd_RenderFrame(
                         abyPixels, sizeof(abyPixels), 16u, 4u, 4u,
                         ROLLER_ED_PIXEL_RGBA8)
                     == ROLLER_ED_RESULT_NO_SCENE);
        CHECK_WORKER(memcmp(abyPixels, abyBefore, sizeof(abyPixels)) == 0);
    }
    {
        tEdVertex Vertex;
        tEdVertex Before;

        memset(&Vertex, 0x3c, sizeof(Vertex));
        Before = Vertex;
        CHECK_WORKER(RollerEd_FillGeometry(
                         uiInitialEpoch, &Vertex, 1u, NULL, 0u,
                         NULL, 0u, NULL, 0u)
                     == ROLLER_ED_RESULT_NO_SCENE);
        CHECK_WORKER(memcmp(&Vertex, &Before, sizeof(Before)) == 0);
    }
    {
        uint32_t uiReadyEpoch;
        uint32_t uiReadyGeneration;

        CHECK_WORKER(RollerEd_LoadTrackFile(
                         pContext->szValidTrack, "facade-test-assets")
                     == ROLLER_ED_RESULT_OK);
        CHECK_WORKER(s_eLastPreferredRenderer == ROLLER_ED_RENDERER_GPU);
        CHECK_WORKER(s_uiLastAllowSoftwareFallback == 1u);
        Sizes.uiStructSize = sizeof(Sizes);
        Sizes.uiVersion = ROLLER_ED_GEOMETRY_SIZES_VERSION;
        CHECK_WORKER(RollerEd_QueryGeometrySizes(&Sizes)
                     == ROLLER_ED_RESULT_OK);
        CHECK_WORKER(Sizes.uiSceneState == ROLLER_ED_SCENE_READY);
        CHECK_WORKER(Sizes.uiTrackGeneration != uiInitialGeneration);
        CHECK_WORKER(Sizes.uiGeometryEpoch != uiInitialEpoch);
        {
            uint8_t abyPixels[64];

            memset(abyPixels, 0, sizeof(abyPixels));
            CHECK_WORKER(RollerEd_RenderFrame(
                             abyPixels, sizeof(abyPixels), 16u, 4u, 4u,
                             ROLLER_ED_PIXEL_RGBA8)
                         == ROLLER_ED_RESULT_OK);
            CHECK_WORKER(abyPixels[0] == 0x7c
                         && abyPixels[sizeof(abyPixels) - 1u] == 0x7c);
        }
        uiReadyEpoch = Sizes.uiGeometryEpoch;
        uiReadyGeneration = Sizes.uiTrackGeneration;

        CHECK_WORKER(RollerEd_LoadTrackFile(
                         pContext->szMalformedTrack, "facade-test-assets")
                     == ROLLER_ED_RESULT_LOAD_FAILED);
        CHECK_WORKER(RollerEd_GetLastError()[0] != '\0');
        CHECK_WORKER(strstr(RollerEd_GetLastError(), "line") != NULL);
        Sizes.uiStructSize = sizeof(Sizes);
        Sizes.uiVersion = ROLLER_ED_GEOMETRY_SIZES_VERSION;
        CHECK_WORKER(RollerEd_QueryGeometrySizes(&Sizes)
                     == ROLLER_ED_RESULT_OK);
        CHECK_WORKER(Sizes.uiSceneState == ROLLER_ED_SCENE_FAILED);
        CHECK_WORKER(Sizes.uiVertexCount == 0u && Sizes.uiIndexCount == 0u
                     && Sizes.uiPrimitiveCount == 0u
                     && Sizes.uiMaterialCount == 0u);
        CHECK_WORKER(Sizes.uiTrackGeneration == uiReadyGeneration);
        CHECK_WORKER(Sizes.uiGeometryEpoch != uiReadyEpoch);
        CHECK_WORKER(RollerEd_GetLastError()[0] == '\0');

        {
            tEdVertex Vertex;
            tEdVertex Before;

            memset(&Vertex, 0xc3, sizeof(Vertex));
            Before = Vertex;
            CHECK_WORKER(RollerEd_FillGeometry(
                             Sizes.uiGeometryEpoch, &Vertex, 1u,
                             NULL, 0u, NULL, 0u, NULL, 0u)
                         == ROLLER_ED_RESULT_NO_SCENE);
            CHECK_WORKER(memcmp(&Vertex, &Before, sizeof(Before)) == 0);
        }

        CHECK_WORKER(RollerEd_LoadTrackFile(
                         pContext->szValidTrack, "facade-test-assets")
                     == ROLLER_ED_RESULT_OK);
        Sizes.uiStructSize = sizeof(Sizes);
        Sizes.uiVersion = ROLLER_ED_GEOMETRY_SIZES_VERSION;
        CHECK_WORKER(RollerEd_QueryGeometrySizes(&Sizes)
                     == ROLLER_ED_RESULT_OK);
        CHECK_WORKER(Sizes.uiSceneState == ROLLER_ED_SCENE_READY);
        CHECK_WORKER(Sizes.uiTrackGeneration != uiReadyGeneration);

        {
            tEdVertex Vertex;
            tEdVertex Before;

            memset(&Vertex, 0x6d, sizeof(Vertex));
            Before = Vertex;
            CHECK_WORKER(RollerEd_FillGeometry(
                             uiReadyEpoch, &Vertex, 1u,
                             NULL, 0u, NULL, 0u, NULL, 0u)
                         == ROLLER_ED_RESULT_STALE);
            CHECK_WORKER(memcmp(&Vertex, &Before, sizeof(Before)) == 0);
        }

        uiReadyEpoch = Sizes.uiGeometryEpoch;
        uiReadyGeneration = Sizes.uiTrackGeneration;
        CHECK_WORKER(RollerEd_LoadTrackFile(
                         "e0_s7_missing_track_73f0d7c9.trk",
                         "facade-test-assets")
                     == ROLLER_ED_RESULT_IO_FAILED);
        CHECK_WORKER(RollerEd_GetLastError()[0] != '\0');
        Sizes.uiStructSize = sizeof(Sizes);
        Sizes.uiVersion = ROLLER_ED_GEOMETRY_SIZES_VERSION;
        CHECK_WORKER(RollerEd_QueryGeometrySizes(&Sizes)
                     == ROLLER_ED_RESULT_OK);
        CHECK_WORKER(Sizes.uiSceneState == ROLLER_ED_SCENE_FAILED);
        CHECK_WORKER(Sizes.uiTrackGeneration == uiReadyGeneration);
        CHECK_WORKER(Sizes.uiGeometryEpoch != uiReadyEpoch);

        CHECK_WORKER(RollerEd_LoadTrackFile(
                         pContext->szValidTrack, "facade-test-assets")
                     == ROLLER_ED_RESULT_OK);
    }

    CHECK_WORKER(RollerEd_UnloadTrack() == ROLLER_ED_RESULT_OK);
    Sizes.uiStructSize = sizeof(Sizes);
    Sizes.uiVersion = ROLLER_ED_GEOMETRY_SIZES_VERSION;
    CHECK_WORKER(RollerEd_QueryGeometrySizes(&Sizes) == ROLLER_ED_RESULT_OK);
    CHECK_WORKER(Sizes.uiGeometryEpoch != uiInitialEpoch);
    CHECK_WORKER(Sizes.uiSceneState == ROLLER_ED_SCENE_EMPTY);
    CHECK_WORKER(RollerEd_SelectRenderer(ROLLER_ED_RENDERER_GPU)
                 == ROLLER_ED_RESULT_RENDERER_UNAVAILABLE);
    CHECK_WORKER(RollerEd_Shutdown() == ROLLER_ED_RESULT_OK);
    CHECK_WORKER(RollerEd_Shutdown() == ROLLER_ED_RESULT_INVALID_STATE);
    return 0;

publish_failure:
    SDL_SignalSemaphore(pContext->pReady);
    return pContext->iFailureLine;
}

int main(int argc, char **argv)
{
    tRollerEdBootstrapInfo BootstrapInfo = {
        .uiStructSize = sizeof(BootstrapInfo),
        .uiVersion = ROLLER_ED_BOOTSTRAP_INFO_VERSION,
        .uiFlags = 0u
    };
    tLifecycleTestContext Context;
    SDL_Thread *pThread;
    int iThreadResult = 0;
    int iMainFailure = 0;

    CHECK_MAIN(argc == 3);
    SDL_SetMainReady();
    SDL_SetAssertionHandler(count_thread_assertion, NULL);
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    CHECK_MAIN(SDL_InitSubSystem(SDL_INIT_VIDEO));
    CHECK_MAIN(SDL_IsMainThread());

    pThread = SDL_CreateThread(
        bootstrap_from_wrong_thread, "facade-wrong-bootstrap", &BootstrapInfo);
    CHECK_MAIN(pThread != NULL);
    SDL_WaitThread(pThread, &iThreadResult);
    CHECK_MAIN(iThreadResult == 0);

    CHECK_MAIN(RollerEd_Bootstrap(&BootstrapInfo) == ROLLER_ED_RESULT_OK);
    CHECK_MAIN(RollerEd_Bootstrap(&BootstrapInfo) == ROLLER_ED_RESULT_OK);
    CHECK_MAIN(RollerEd_Teardown() == ROLLER_ED_RESULT_OK);
    CHECK_MAIN((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) != 0u);
    CHECK_MAIN(RollerEd_Teardown() == ROLLER_ED_RESULT_OK);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);

    CHECK_MAIN(RollerEd_Bootstrap(&BootstrapInfo) == ROLLER_ED_RESULT_OK);
    CHECK_MAIN((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) != 0u);

    Context.pReady = SDL_CreateSemaphore(0u);
    Context.pContinue = SDL_CreateSemaphore(0u);
    Context.szValidTrack = argv[1];
    Context.szMalformedTrack = argv[2];
    Context.iFailureLine = 0;
    CHECK_MAIN(Context.pReady != NULL && Context.pContinue != NULL);
    pThread = SDL_CreateThread(lifecycle_worker, "facade-render-worker", &Context);
    CHECK_MAIN(pThread != NULL);
    SDL_WaitSemaphore(Context.pReady);

    if (Context.iFailureLine == 0) {
        tEdGeometrySizes Sizes = {
            .uiStructSize = sizeof(Sizes),
            .uiVersion = ROLLER_ED_GEOMETRY_SIZES_VERSION
        };
        tRollerEdInitInfo InitInfo = {
            .uiStructSize = sizeof(InitInfo),
            .uiVersion = ROLLER_ED_INIT_INFO_VERSION,
            .szAssetRoot = "main-thread-invalid",
            .ePreferredRenderer = ROLLER_ED_RENDERER_SOFTWARE
        };

        iMainFailure = check_condition(
            RollerEd_QueryGeometrySizes(&Sizes)
                == ROLLER_ED_RESULT_WRONG_THREAD,
            __LINE__);
        if (iMainFailure == 0)
            iMainFailure = check_condition(
                RollerEd_Init(&InitInfo) == ROLLER_ED_RESULT_WRONG_THREAD,
                __LINE__);
        if (iMainFailure == 0)
            iMainFailure = check_condition(
                RollerEd_Teardown() == ROLLER_ED_RESULT_INVALID_STATE,
                __LINE__);
    }

    SDL_SignalSemaphore(Context.pContinue);
    SDL_WaitThread(pThread, &iThreadResult);
    SDL_DestroySemaphore(Context.pReady);
    SDL_DestroySemaphore(Context.pContinue);
    if (iMainFailure != 0)
        return iMainFailure;
    CHECK_MAIN(iThreadResult == 0 && Context.iFailureLine == 0);
    CHECK_MAIN(s_iThreadAssertionCount >= 3);
    CHECK_MAIN(s_iLegacyInstallCount == 3);
    CHECK_MAIN(s_iLegacyRenderCount == 1);
    CHECK_MAIN(s_iLegacySetCameraCount == 1);

    CHECK_MAIN(RollerEd_Teardown() == ROLLER_ED_RESULT_OK);
    CHECK_MAIN((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0u);
    CHECK_MAIN(RollerEd_Teardown() == ROLLER_ED_RESULT_OK);
    CHECK_MAIN(ed_track_loader_live_allocations() == 0u);
    CHECK_MAIN(ed_track_loader_live_bytes() == 0u);
    SDL_SetAssertionHandler(NULL, NULL);
    SDL_Quit();
    puts("editor API lifecycle and SDL ownership tests passed");
    return 0;
}
