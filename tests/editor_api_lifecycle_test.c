#include "editor_api.h"

#define SDL_MAIN_HANDLED 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <stdio.h>
#include <string.h>

typedef struct
{
    SDL_Semaphore *pReady;
    SDL_Semaphore *pContinue;
    int iFailureLine;
} tLifecycleTestContext;

static int s_iThreadAssertionCount;

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

    SDL_SignalSemaphore(pContext->pReady);
    SDL_WaitSemaphore(pContext->pContinue);

    {
        tEdCameraState Camera = {
            .uiStructSize = sizeof(Camera),
            .uiVersion = ROLLER_ED_CAMERA_STATE_VERSION
        };
        CHECK_WORKER(RollerEd_SetCamera(&Camera)
                     == ROLLER_ED_RESULT_UNSUPPORTED);
        CHECK_WORKER(strstr(RollerEd_GetLastError(), "not implemented") != NULL);
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

int main(void)
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

    CHECK_MAIN(RollerEd_Teardown() == ROLLER_ED_RESULT_OK);
    CHECK_MAIN((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0u);
    CHECK_MAIN(RollerEd_Teardown() == ROLLER_ED_RESULT_OK);
    SDL_SetAssertionHandler(NULL, NULL);
    SDL_Quit();
    puts("editor API lifecycle and SDL ownership tests passed");
    return 0;
}
