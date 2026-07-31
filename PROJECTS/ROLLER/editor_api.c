#include "editor_api.h"

#define SDL_MAIN_HANDLED 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum
{
    ROLLER_ED_LIFECYCLE_UNBOOTSTRAPPED = 0,
    ROLLER_ED_LIFECYCLE_BOOTSTRAPPED,
    ROLLER_ED_LIFECYCLE_INITIALIZING,
    ROLLER_ED_LIFECYCLE_INITIALIZED,
    ROLLER_ED_LIFECYCLE_INIT_FAILED
} eRollerEdLifecycleState;

static eRollerEdLifecycleState s_eLifecycle =
    ROLLER_ED_LIFECYCLE_UNBOOTSTRAPPED;
static SDL_ThreadID s_ullMainThreadId;
static SDL_ThreadID s_ullWorkerThreadId;
static bool s_bOwnsVideoReference;
static char *s_szAssetRoot;
static eRollerEdRenderer s_ePreferredRenderer = ROLLER_ED_RENDERER_GPU;
static uint32_t s_uiAllowSoftwareFallback;
static uint32_t s_uiGeometryEpoch;
static uint32_t s_uiTrackGeneration;
static eRollerEdSceneState s_eSceneState = ROLLER_ED_SCENE_EMPTY;
static char s_szLastError[512];

static void roller_ed_clear_error(void)
{
    s_szLastError[0] = '\0';
}

static void roller_ed_set_error(const char *szFormat, ...)
{
    va_list Args;

    va_start(Args, szFormat);
    vsnprintf(s_szLastError, sizeof(s_szLastError), szFormat, Args);
    va_end(Args);
    s_szLastError[sizeof(s_szLastError) - 1u] = '\0';
}

static void roller_ed_advance_geometry_epoch(void)
{
    s_uiGeometryEpoch++;
    if (s_uiGeometryEpoch == 0u)
        s_uiGeometryEpoch = 1u;
}

static bool roller_ed_is_main_owner(void)
{
    SDL_ThreadID ullCurrentThread = SDL_GetCurrentThreadID();

    if (s_ullMainThreadId != 0u)
        return ullCurrentThread == s_ullMainThreadId;
    return SDL_IsMainThread();
}

static bool roller_ed_is_worker_owner(void)
{
    return s_ullWorkerThreadId != 0u
        && SDL_GetCurrentThreadID() == s_ullWorkerThreadId;
}

static eRollerEdResult roller_ed_validate_struct(
    uint32_t uiStructSize, uint32_t uiVersion, uint32_t uiRequiredSize,
    const char *szStructName)
{
    if (uiVersion != ROLLER_ED_API_VERSION) {
        roller_ed_set_error("%s version %u is unsupported", szStructName,
                            uiVersion);
        return ROLLER_ED_RESULT_INVALID_VERSION;
    }
    if (uiStructSize < uiRequiredSize) {
        roller_ed_set_error("%s size %u is smaller than v1 size %u",
                            szStructName, uiStructSize, uiRequiredSize);
        return ROLLER_ED_RESULT_INVALID_ARGUMENT;
    }
    return ROLLER_ED_RESULT_OK;
}

static eRollerEdResult roller_ed_require_worker(void)
{
    bool bOnWorker;

    roller_ed_clear_error();
    if (s_eLifecycle != ROLLER_ED_LIFECYCLE_INITIALIZED) {
        roller_ed_set_error("roller-core is not initialized");
        return ROLLER_ED_RESULT_NOT_INITIALIZED;
    }
    bOnWorker = roller_ed_is_worker_owner();
    if (!bOnWorker) {
        roller_ed_set_error("rendering facade call made off the render worker");
        SDL_assert_always(bOnWorker);
        return ROLLER_ED_RESULT_WRONG_THREAD;
    }
    return ROLLER_ED_RESULT_OK;
}

static void roller_ed_release_worker_resources(void)
{
    free(s_szAssetRoot);
    s_szAssetRoot = NULL;
    s_ePreferredRenderer = ROLLER_ED_RENDERER_GPU;
    s_uiAllowSoftwareFallback = 0u;
    s_eSceneState = ROLLER_ED_SCENE_EMPTY;
}

eRollerEdResult ROLLER_ED_CALL RollerEd_Bootstrap(
    const tRollerEdBootstrapInfo *pInfo)
{
    eRollerEdResult eResult;
    bool bOnMainThread;

    roller_ed_clear_error();
    bOnMainThread = roller_ed_is_main_owner();
    if (!bOnMainThread) {
        roller_ed_set_error("RollerEd_Bootstrap must run on the main thread");
        SDL_assert_always(bOnMainThread);
        return ROLLER_ED_RESULT_WRONG_THREAD;
    }
    if (!pInfo) {
        roller_ed_set_error("RollerEd_Bootstrap requires pInfo");
        return ROLLER_ED_RESULT_INVALID_ARGUMENT;
    }
    eResult = roller_ed_validate_struct(
        pInfo->uiStructSize, pInfo->uiVersion, sizeof(*pInfo),
        "tRollerEdBootstrapInfo");
    if (eResult != ROLLER_ED_RESULT_OK)
        return eResult;
    if (pInfo->uiFlags != 0u) {
        roller_ed_set_error("tRollerEdBootstrapInfo.uiFlags must be zero");
        return ROLLER_ED_RESULT_INVALID_ARGUMENT;
    }

    if (s_eLifecycle != ROLLER_ED_LIFECYCLE_UNBOOTSTRAPPED)
        return ROLLER_ED_RESULT_OK;

    SDL_SetMainReady();
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        roller_ed_set_error("SDL video initialization failed: %s",
                            SDL_GetError());
        return ROLLER_ED_RESULT_INTERNAL_ERROR;
    }

    s_bOwnsVideoReference = true;
    s_ullMainThreadId = SDL_GetCurrentThreadID();
    s_eLifecycle = ROLLER_ED_LIFECYCLE_BOOTSTRAPPED;
    return ROLLER_ED_RESULT_OK;
}

eRollerEdResult ROLLER_ED_CALL RollerEd_Teardown(void)
{
    bool bOnMainThread;

    roller_ed_clear_error();
    bOnMainThread = roller_ed_is_main_owner();
    if (!bOnMainThread) {
        roller_ed_set_error("RollerEd_Teardown must run on the bootstrap thread");
        SDL_assert_always(bOnMainThread);
        return ROLLER_ED_RESULT_WRONG_THREAD;
    }
    if (s_eLifecycle == ROLLER_ED_LIFECYCLE_UNBOOTSTRAPPED)
        return ROLLER_ED_RESULT_OK;
    if (s_eLifecycle != ROLLER_ED_LIFECYCLE_BOOTSTRAPPED) {
        roller_ed_set_error("RollerEd_Teardown requires worker shutdown first");
        return ROLLER_ED_RESULT_INVALID_STATE;
    }

    if (s_bOwnsVideoReference) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        s_bOwnsVideoReference = false;
    }
    s_ullWorkerThreadId = 0u;
    s_eLifecycle = ROLLER_ED_LIFECYCLE_UNBOOTSTRAPPED;
    return ROLLER_ED_RESULT_OK;
}

eRollerEdResult ROLLER_ED_CALL RollerEd_Init(const tRollerEdInitInfo *pInfo)
{
    eRollerEdResult eResult;
    SDL_ThreadID ullCurrentThread = SDL_GetCurrentThreadID();
    size_t uiAssetRootLength;
    char *szAssetRootCopy;
    bool bOnWorkerThread;

    roller_ed_clear_error();
    bOnWorkerThread = !roller_ed_is_main_owner();
    if (!bOnWorkerThread) {
        roller_ed_set_error("RollerEd_Init must run on the render worker");
        SDL_assert_always(bOnWorkerThread);
        return ROLLER_ED_RESULT_WRONG_THREAD;
    }
    if (s_eLifecycle == ROLLER_ED_LIFECYCLE_INITIALIZED) {
        bOnWorkerThread = roller_ed_is_worker_owner();
        if (!bOnWorkerThread) {
            roller_ed_set_error("RollerEd_Init called from a different worker");
            SDL_assert_always(bOnWorkerThread);
            return ROLLER_ED_RESULT_WRONG_THREAD;
        }
        roller_ed_set_error("RollerEd_Init has already succeeded");
        return ROLLER_ED_RESULT_INVALID_STATE;
    }
    if (s_eLifecycle == ROLLER_ED_LIFECYCLE_INIT_FAILED
            && s_ullWorkerThreadId != ullCurrentThread) {
        roller_ed_set_error("failed initialization is owned by another worker");
        SDL_assert_always(s_ullWorkerThreadId == ullCurrentThread);
        return ROLLER_ED_RESULT_WRONG_THREAD;
    }
    if (s_eLifecycle != ROLLER_ED_LIFECYCLE_BOOTSTRAPPED
            && s_eLifecycle != ROLLER_ED_LIFECYCLE_INIT_FAILED) {
        roller_ed_set_error("RollerEd_Init requires RollerEd_Bootstrap");
        return ROLLER_ED_RESULT_INVALID_STATE;
    }
    if (!pInfo) {
        roller_ed_set_error("RollerEd_Init requires pInfo");
        return ROLLER_ED_RESULT_INVALID_ARGUMENT;
    }
    eResult = roller_ed_validate_struct(
        pInfo->uiStructSize, pInfo->uiVersion, sizeof(*pInfo),
        "tRollerEdInitInfo");
    if (eResult != ROLLER_ED_RESULT_OK)
        return eResult;
    if (!pInfo->szAssetRoot || pInfo->szAssetRoot[0] == '\0') {
        roller_ed_set_error("tRollerEdInitInfo.szAssetRoot is required");
        return ROLLER_ED_RESULT_INVALID_ARGUMENT;
    }
    if (pInfo->ePreferredRenderer != ROLLER_ED_RENDERER_SOFTWARE
            && pInfo->ePreferredRenderer != ROLLER_ED_RENDERER_GPU) {
        roller_ed_set_error("invalid preferred renderer %u",
                            pInfo->ePreferredRenderer);
        return ROLLER_ED_RESULT_INVALID_ARGUMENT;
    }
    if (pInfo->uiAllowSoftwareFallback > 1u) {
        roller_ed_set_error("uiAllowSoftwareFallback must be zero or one");
        return ROLLER_ED_RESULT_INVALID_ARGUMENT;
    }

    roller_ed_release_worker_resources();
    s_eLifecycle = ROLLER_ED_LIFECYCLE_INITIALIZING;
    s_ullWorkerThreadId = ullCurrentThread;
    uiAssetRootLength = strlen(pInfo->szAssetRoot);
    szAssetRootCopy = (char *)malloc(uiAssetRootLength + 1u);
    if (!szAssetRootCopy) {
        s_eLifecycle = ROLLER_ED_LIFECYCLE_INIT_FAILED;
        roller_ed_set_error("could not copy the configured asset root");
        return ROLLER_ED_RESULT_OUT_OF_MEMORY;
    }
    memcpy(szAssetRootCopy, pInfo->szAssetRoot, uiAssetRootLength + 1u);

    s_szAssetRoot = szAssetRootCopy;
    s_ePreferredRenderer = pInfo->ePreferredRenderer;
    s_uiAllowSoftwareFallback = pInfo->uiAllowSoftwareFallback;
    s_eSceneState = ROLLER_ED_SCENE_EMPTY;
    roller_ed_advance_geometry_epoch();
    s_eLifecycle = ROLLER_ED_LIFECYCLE_INITIALIZED;
    return ROLLER_ED_RESULT_OK;
}

eRollerEdResult ROLLER_ED_CALL RollerEd_Shutdown(void)
{
    bool bOnWorkerThread;

    roller_ed_clear_error();
    bOnWorkerThread = !roller_ed_is_main_owner();
    if (!bOnWorkerThread) {
        roller_ed_set_error("RollerEd_Shutdown must run on the render worker");
        SDL_assert_always(bOnWorkerThread);
        return ROLLER_ED_RESULT_WRONG_THREAD;
    }
    if (s_eLifecycle != ROLLER_ED_LIFECYCLE_INITIALIZED
            && s_eLifecycle != ROLLER_ED_LIFECYCLE_INIT_FAILED) {
        roller_ed_set_error("RollerEd_Shutdown has no initialization to release");
        return ROLLER_ED_RESULT_INVALID_STATE;
    }
    bOnWorkerThread = roller_ed_is_worker_owner();
    if (!bOnWorkerThread) {
        roller_ed_set_error("RollerEd_Shutdown called from a different worker");
        SDL_assert_always(bOnWorkerThread);
        return ROLLER_ED_RESULT_WRONG_THREAD;
    }

    roller_ed_release_worker_resources();
    roller_ed_advance_geometry_epoch();
    s_ullWorkerThreadId = 0u;
    s_eLifecycle = ROLLER_ED_LIFECYCLE_BOOTSTRAPPED;
    return ROLLER_ED_RESULT_OK;
}

uint32_t ROLLER_ED_CALL RollerEd_GetAvailableRenderers(void)
{
    if (roller_ed_require_worker() != ROLLER_ED_RESULT_OK)
        return 0u;

    /* Availability probing and lazy GPU creation land in E1-S8. */
    return 0u;
}

eRollerEdResult ROLLER_ED_CALL RollerEd_SelectRenderer(eRollerEdRenderer eKind)
{
    eRollerEdResult eResult = roller_ed_require_worker();

    if (eResult != ROLLER_ED_RESULT_OK)
        return eResult;
    if (eKind != ROLLER_ED_RENDERER_SOFTWARE
            && eKind != ROLLER_ED_RENDERER_GPU) {
        roller_ed_set_error("invalid renderer %u", eKind);
        return ROLLER_ED_RESULT_INVALID_ARGUMENT;
    }
    roller_ed_set_error("renderer selection is not implemented yet");
    return ROLLER_ED_RESULT_RENDERER_UNAVAILABLE;
}

eRollerEdResult ROLLER_ED_CALL RollerEd_LoadTrackFile(
    const char *szTrackPath, const char *szDocumentAssetRoot)
{
    eRollerEdResult eResult = roller_ed_require_worker();

    if (eResult != ROLLER_ED_RESULT_OK)
        return eResult;
    if (!szTrackPath || !szTrackPath[0]
            || !szDocumentAssetRoot || !szDocumentAssetRoot[0]) {
        roller_ed_set_error("track path and document asset root are required");
        return ROLLER_ED_RESULT_INVALID_ARGUMENT;
    }
    roller_ed_set_error("facade track loading is not implemented yet");
    return ROLLER_ED_RESULT_UNSUPPORTED;
}

eRollerEdResult ROLLER_ED_CALL RollerEd_UnloadTrack(void)
{
    eRollerEdResult eResult = roller_ed_require_worker();

    if (eResult != ROLLER_ED_RESULT_OK)
        return eResult;
    s_eSceneState = ROLLER_ED_SCENE_EMPTY;
    roller_ed_advance_geometry_epoch();
    return ROLLER_ED_RESULT_OK;
}

eRollerEdResult ROLLER_ED_CALL RollerEd_SetCamera(const tEdCameraState *pCam)
{
    eRollerEdResult eResult = roller_ed_require_worker();

    if (eResult != ROLLER_ED_RESULT_OK)
        return eResult;
    if (!pCam) {
        roller_ed_set_error("RollerEd_SetCamera requires pCam");
        return ROLLER_ED_RESULT_INVALID_ARGUMENT;
    }
    eResult = roller_ed_validate_struct(
        pCam->uiStructSize, pCam->uiVersion, sizeof(*pCam),
        "tEdCameraState");
    if (eResult != ROLLER_ED_RESULT_OK)
        return eResult;
    roller_ed_set_error("facade camera control is not implemented yet");
    return ROLLER_ED_RESULT_UNSUPPORTED;
}

eRollerEdResult ROLLER_ED_CALL RollerEd_RenderFrame(
    uint8_t *pbyPixels, uint32_t uiBufferSize, uint32_t uiRowPitch,
    uint32_t uiWidth, uint32_t uiHeight, eRollerEdPixelFormat eFormat)
{
    eRollerEdResult eResult = roller_ed_require_worker();

    if (eResult != ROLLER_ED_RESULT_OK)
        return eResult;
    if (!pbyPixels || uiWidth == 0u || uiHeight == 0u
            || eFormat != ROLLER_ED_PIXEL_RGBA8
            || uiWidth > UINT32_MAX / 4u
            || uiRowPitch < uiWidth * 4u
            || (uiHeight > 0u && uiRowPitch > UINT32_MAX / uiHeight)
            || uiBufferSize < uiRowPitch * uiHeight) {
        roller_ed_set_error("invalid RGBA8 render buffer contract");
        return ROLLER_ED_RESULT_INVALID_ARGUMENT;
    }
    if (s_eSceneState != ROLLER_ED_SCENE_READY) {
        roller_ed_set_error("there is no renderable scene");
        return ROLLER_ED_RESULT_NO_SCENE;
    }
    roller_ed_set_error("facade rendering is not implemented yet");
    return ROLLER_ED_RESULT_UNSUPPORTED;
}

eRollerEdResult ROLLER_ED_CALL RollerEd_SetOverlayState(
    const tEdOverlayState *pState)
{
    eRollerEdResult eResult = roller_ed_require_worker();

    if (eResult != ROLLER_ED_RESULT_OK)
        return eResult;
    if (!pState) {
        roller_ed_set_error("RollerEd_SetOverlayState requires pState");
        return ROLLER_ED_RESULT_INVALID_ARGUMENT;
    }
    eResult = roller_ed_validate_struct(
        pState->uiStructSize, pState->uiVersion, sizeof(*pState),
        "tEdOverlayState");
    if (eResult != ROLLER_ED_RESULT_OK)
        return eResult;
    roller_ed_set_error("facade overlay state is not implemented yet");
    return ROLLER_ED_RESULT_UNSUPPORTED;
}

eRollerEdResult ROLLER_ED_CALL RollerEd_SetReferenceMesh(
    const tEdReferenceMesh *pMesh)
{
    eRollerEdResult eResult = roller_ed_require_worker();

    if (eResult != ROLLER_ED_RESULT_OK)
        return eResult;
    if (!pMesh) {
        roller_ed_set_error("RollerEd_SetReferenceMesh requires pMesh");
        return ROLLER_ED_RESULT_INVALID_ARGUMENT;
    }
    eResult = roller_ed_validate_struct(
        pMesh->uiStructSize, pMesh->uiVersion, sizeof(*pMesh),
        "tEdReferenceMesh");
    if (eResult != ROLLER_ED_RESULT_OK)
        return eResult;
    roller_ed_set_error("facade reference meshes are not implemented yet");
    return ROLLER_ED_RESULT_UNSUPPORTED;
}

eRollerEdResult ROLLER_ED_CALL RollerEd_QueryGeometrySizes(
    tEdGeometrySizes *pSizesOut)
{
    eRollerEdResult eResult = roller_ed_require_worker();
    tEdGeometrySizes Sizes;

    if (eResult != ROLLER_ED_RESULT_OK)
        return eResult;
    if (!pSizesOut) {
        roller_ed_set_error("RollerEd_QueryGeometrySizes requires pSizesOut");
        return ROLLER_ED_RESULT_INVALID_ARGUMENT;
    }
    eResult = roller_ed_validate_struct(
        pSizesOut->uiStructSize, pSizesOut->uiVersion, sizeof(*pSizesOut),
        "tEdGeometrySizes");
    if (eResult != ROLLER_ED_RESULT_OK)
        return eResult;

    memset(&Sizes, 0, sizeof(Sizes));
    Sizes.uiStructSize = sizeof(Sizes);
    Sizes.uiVersion = ROLLER_ED_GEOMETRY_SIZES_VERSION;
    Sizes.uiGeometryEpoch = s_uiGeometryEpoch;
    Sizes.uiTrackGeneration = s_uiTrackGeneration;
    Sizes.uiSceneState = s_eSceneState;
    Sizes.uiVertexStride = sizeof(tEdVertex);
    Sizes.uiPrimitiveStride = sizeof(tEdPrimitive);
    Sizes.uiMaterialStride = sizeof(tEdMaterial);
    *pSizesOut = Sizes;
    return ROLLER_ED_RESULT_OK;
}

eRollerEdResult ROLLER_ED_CALL RollerEd_FillGeometry(
    uint32_t uiExpectedGeometryEpoch,
    tEdVertex *pVerts, uint32_t uiVertexCapacity,
    uint32_t *puiIndices, uint32_t uiIndexCapacity,
    tEdPrimitive *pPrims, uint32_t uiPrimitiveCapacity,
    tEdMaterial *pMats, uint32_t uiMaterialCapacity)
{
    eRollerEdResult eResult = roller_ed_require_worker();

    (void)uiExpectedGeometryEpoch;
    (void)pVerts;
    (void)uiVertexCapacity;
    (void)puiIndices;
    (void)uiIndexCapacity;
    (void)pPrims;
    (void)uiPrimitiveCapacity;
    (void)pMats;
    (void)uiMaterialCapacity;

    if (eResult != ROLLER_ED_RESULT_OK)
        return eResult;
    if (s_eSceneState != ROLLER_ED_SCENE_READY) {
        roller_ed_set_error("there is no geometry scene");
        return ROLLER_ED_RESULT_NO_SCENE;
    }
    roller_ed_set_error("facade geometry extraction is not implemented yet");
    return ROLLER_ED_RESULT_UNSUPPORTED;
}

const char *ROLLER_ED_CALL RollerEd_GetLastError(void)
{
    static const char szWrongThread[] =
        "RollerEd_GetLastError called off the render worker";

    if (s_eLifecycle == ROLLER_ED_LIFECYCLE_INITIALIZED
            && !roller_ed_is_worker_owner())
        return szWrongThread;
    return s_szLastError;
}
