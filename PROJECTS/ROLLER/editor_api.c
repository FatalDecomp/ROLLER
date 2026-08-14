#include "editor_api.h"
#include "editor_legacy_scene.h"
#include "editor_overlay.h"
#include "editor_track_loader.h"

#define SDL_MAIN_HANDLED 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <stdarg.h>
#include <stdbool.h>
#include <math.h>
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
static tEdTrackStage s_TrackStage;
static char s_szLastError[512];
/*
 * Authored geometry is camera-independent (AD-6b) and only the geometry epoch
 * invalidates it (AD-7d), so one extraction per epoch serves every query and
 * fill. Callers are always given copies; this never escapes.
 */
static tEdGeometryExtract s_GeometryExtract;
static uint32_t s_uiGeometryExtractEpoch;
static bool s_bGeometryExtractValid;

static void roller_ed_release_geometry_cache(void);

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
    /* Everything that advances the epoch has invalidated the extraction by
     * definition, so releasing here means no caller of the epoch helper has
     * to remember to. */
    roller_ed_release_geometry_cache();
}

static void roller_ed_release_geometry_cache(void)
{
    roller_ed_legacy_scene_release_geometry(&s_GeometryExtract);
    s_uiGeometryExtractEpoch = 0u;
    s_bGeometryExtractValid = false;
}

/* Extracts once per geometry epoch; a no-op while the cache is current. */
static eRollerEdResult roller_ed_sync_geometry_cache(void)
{
    eRollerEdResult eResult;

    if (s_bGeometryExtractValid
            && s_uiGeometryExtractEpoch == s_uiGeometryEpoch)
        return ROLLER_ED_RESULT_OK;

    roller_ed_release_geometry_cache();
    eResult = roller_ed_legacy_scene_extract_geometry(
        &s_GeometryExtract, s_szLastError, sizeof(s_szLastError));
    if (eResult != ROLLER_ED_RESULT_OK) {
        roller_ed_legacy_scene_release_geometry(&s_GeometryExtract);
        return eResult;
    }
    s_uiGeometryExtractEpoch = s_uiGeometryEpoch;
    s_bGeometryExtractValid = true;
    return ROLLER_ED_RESULT_OK;
}

static void roller_ed_advance_track_generation(void)
{
    s_uiTrackGeneration++;
    if (s_uiTrackGeneration == 0u)
        s_uiTrackGeneration = 1u;
}

static eRollerEdResult roller_ed_track_load_result(
    eEdTrackLoadResult eResult)
{
    switch (eResult) {
    case ED_TRACK_LOAD_OK:
        return ROLLER_ED_RESULT_OK;
    case ED_TRACK_LOAD_INVALID_ARGUMENT:
        return ROLLER_ED_RESULT_INVALID_ARGUMENT;
    case ED_TRACK_LOAD_IO_FAILED:
        return ROLLER_ED_RESULT_IO_FAILED;
    case ED_TRACK_LOAD_OUT_OF_MEMORY:
        return ROLLER_ED_RESULT_OUT_OF_MEMORY;
    case ED_TRACK_LOAD_TRUNCATED:
    case ED_TRACK_LOAD_INVALID_SIZE:
    case ED_TRACK_LOAD_INVALID_BACK_REFERENCE:
    case ED_TRACK_LOAD_OUTPUT_OVERFLOW:
    case ED_TRACK_LOAD_MALFORMED_TEXT:
        return ROLLER_ED_RESULT_LOAD_FAILED;
    }
    return ROLLER_ED_RESULT_INTERNAL_ERROR;
}

static void roller_ed_clear_scene(eRollerEdSceneState eState)
{
    ed_track_stage_dispose(&s_TrackStage);
    s_eSceneState = eState;
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

/*
 * AD-12: each top-level struct carries its own version, so a struct that gains
 * a field (tEdOverlayState did, in E3A-S2) bumps only its own and leaves every
 * other call alone. The expected version is therefore a parameter rather than
 * the API version.
 */
static eRollerEdResult roller_ed_validate_struct(
    uint32_t uiStructSize, uint32_t uiVersion, uint32_t uiExpectedVersion,
    uint32_t uiRequiredSize, const char *szStructName)
{
    if (uiVersion != uiExpectedVersion) {
        roller_ed_set_error("%s version %u is unsupported; this core expects %u",
                            szStructName, uiVersion, uiExpectedVersion);
        return ROLLER_ED_RESULT_INVALID_VERSION;
    }
    if (uiStructSize < uiRequiredSize) {
        roller_ed_set_error("%s size %u is smaller than the v%u size %u",
                            szStructName, uiStructSize, uiExpectedVersion,
                            uiRequiredSize);
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
    roller_ed_legacy_scene_shutdown();
    roller_ed_release_geometry_cache();
    free(s_szAssetRoot);
    s_szAssetRoot = NULL;
    s_ePreferredRenderer = ROLLER_ED_RENDERER_GPU;
    s_uiAllowSoftwareFallback = 0u;
    roller_ed_clear_scene(ROLLER_ED_SCENE_EMPTY);
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
        pInfo->uiStructSize, pInfo->uiVersion,
        ROLLER_ED_BOOTSTRAP_INFO_VERSION, sizeof(*pInfo),
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
        pInfo->uiStructSize, pInfo->uiVersion,
        ROLLER_ED_INIT_INFO_VERSION, sizeof(*pInfo),
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

    return roller_ed_legacy_scene_get_available_renderers();
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
    eResult = roller_ed_legacy_scene_select_renderer(
        eKind, s_szLastError, sizeof(s_szLastError));
    if (eResult == ROLLER_ED_RESULT_OK)
        s_ePreferredRenderer = eKind;
    return eResult;
}

eRollerEdResult ROLLER_ED_CALL RollerEd_SetGraphicsSettings(
    const tEdGraphicsSettings *pSettings)
{
    eRollerEdResult eResult = roller_ed_require_worker();

    if (eResult != ROLLER_ED_RESULT_OK)
        return eResult;
    if (!pSettings) {
        roller_ed_set_error("RollerEd_SetGraphicsSettings requires pSettings");
        return ROLLER_ED_RESULT_INVALID_ARGUMENT;
    }
    eResult = roller_ed_validate_struct(
        pSettings->uiStructSize, pSettings->uiVersion,
        ROLLER_ED_GRAPHICS_SETTINGS_VERSION, sizeof(*pSettings),
        "tEdGraphicsSettings");
    if (eResult != ROLLER_ED_RESULT_OK)
        return eResult;
    if ((pSettings->eRenderer != ROLLER_ED_RENDERER_SOFTWARE
            && pSettings->eRenderer != ROLLER_ED_RENDERER_GPU)
            || pSettings->eAntiAliasing > ROLLER_ED_ANTI_ALIASING_8X
            || pSettings->eAnisotropy > ROLLER_ED_ANISOTROPY_16X
            || pSettings->eTextureFilter
                > ROLLER_ED_TEXTURE_FILTER_ANISOTROPIC
            || pSettings->uiTrilinear > 1u
            || pSettings->uiEmulateTransparentBorders > 1u
            || pSettings->eSoftwareDisplay
                > ROLLER_ED_SOFTWARE_DISPLAY_SVGA
            || !isfinite(pSettings->fDrawDistanceFraction)
            || pSettings->fDrawDistanceFraction < 0.0f
            || pSettings->fDrawDistanceFraction > 1.0f
            || !isfinite(pSettings->fLodBias)
            || pSettings->fLodBias < -4.0f
            || pSettings->fLodBias > 4.0f) {
        roller_ed_set_error("invalid editor graphics settings");
        return ROLLER_ED_RESULT_INVALID_ARGUMENT;
    }
    eResult = roller_ed_legacy_scene_set_graphics_settings(
        pSettings, s_szLastError, sizeof(s_szLastError));
    if (eResult == ROLLER_ED_RESULT_OK)
        s_ePreferredRenderer = pSettings->eRenderer;
    return eResult;
}

static eRollerEdResult roller_ed_load_track_file(
    const char *szTrackPath, const char *szDocumentAssetRoot,
    const char *szFallbackAssetRoot)
{
    eRollerEdResult eResult = roller_ed_require_worker();
    eEdTrackLoadResult eLoadResult;
    tEdTrackStage Staged;

    if (eResult != ROLLER_ED_RESULT_OK)
        return eResult;
    if (!szTrackPath || !szTrackPath[0]
            || !szDocumentAssetRoot || !szDocumentAssetRoot[0]) {
        roller_ed_set_error("track path and document asset root are required");
        return ROLLER_ED_RESULT_INVALID_ARGUMENT;
    }

    /* The staged loader requires a pair of roots.  Treat an omitted fallback
     * as document-only rather than silently using the application directory
     * or the worker's current directory. */
    if (!szFallbackAssetRoot || !szFallbackAssetRoot[0])
        szFallbackAssetRoot = szDocumentAssetRoot;

    ed_track_stage_init(&Staged);
    eLoadResult = ed_track_file_stage(
        szTrackPath, &Staged, s_szLastError, sizeof(s_szLastError));
    if (eLoadResult != ED_TRACK_LOAD_OK) {
        ed_track_stage_dispose(&Staged);
        roller_ed_legacy_scene_unload();
        roller_ed_clear_scene(ROLLER_ED_SCENE_FAILED);
        roller_ed_advance_geometry_epoch();
        return roller_ed_track_load_result(eLoadResult);
    }

    eResult = roller_ed_legacy_scene_install(
        szTrackPath, &Staged, szDocumentAssetRoot, szFallbackAssetRoot,
        s_ePreferredRenderer, s_uiAllowSoftwareFallback,
        s_szLastError, sizeof(s_szLastError));
    if (eResult != ROLLER_ED_RESULT_OK) {
        ed_track_stage_dispose(&Staged);
        roller_ed_legacy_scene_unload();
        roller_ed_clear_scene(ROLLER_ED_SCENE_FAILED);
        roller_ed_advance_geometry_epoch();
        return eResult;
    }

    ed_track_stage_dispose(&s_TrackStage);
    s_TrackStage = Staged;
    s_eSceneState = ROLLER_ED_SCENE_READY;
    roller_ed_advance_geometry_epoch();
    roller_ed_advance_track_generation();
    return ROLLER_ED_RESULT_OK;
}

eRollerEdResult ROLLER_ED_CALL RollerEd_LoadTrackFile(
    const char *szTrackPath, const char *szDocumentAssetRoot)
{
    return roller_ed_load_track_file(
        szTrackPath, szDocumentAssetRoot, s_szAssetRoot);
}

eRollerEdResult ROLLER_ED_CALL RollerEd_LoadTrackFileEx(
    const char *szTrackPath, const char *szDocumentAssetRoot,
    const char *szFallbackAssetRoot)
{
    return roller_ed_load_track_file(
        szTrackPath, szDocumentAssetRoot, szFallbackAssetRoot);
}

eRollerEdResult ROLLER_ED_CALL RollerEd_UnloadTrack(void)
{
    eRollerEdResult eResult = roller_ed_require_worker();

    if (eResult != ROLLER_ED_RESULT_OK)
        return eResult;
    roller_ed_legacy_scene_unload();
    roller_ed_clear_scene(ROLLER_ED_SCENE_EMPTY);
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
        pCam->uiStructSize, pCam->uiVersion,
        ROLLER_ED_CAMERA_STATE_VERSION, sizeof(*pCam),
        "tEdCameraState");
    if (eResult != ROLLER_ED_RESULT_OK)
        return eResult;
    if (!isfinite(pCam->fPosition[0])
            || !isfinite(pCam->fPosition[1])
            || !isfinite(pCam->fPosition[2])
            || !isfinite(pCam->fYawDegrees)
            || !isfinite(pCam->fPitchDegrees)) {
        roller_ed_set_error("camera position and angles must be finite");
        return ROLLER_ED_RESULT_INVALID_ARGUMENT;
    }
    return roller_ed_legacy_scene_set_camera(
        pCam, s_szLastError, sizeof(s_szLastError));
}

eRollerEdResult ROLLER_ED_CALL RollerEd_AdvanceStunts(uint32_t uiTicks)
{
    eRollerEdResult eResult = roller_ed_require_worker();

    if (eResult != ROLLER_ED_RESULT_OK)
        return eResult;
    if (s_eSceneState != ROLLER_ED_SCENE_READY) {
        roller_ed_set_error("there is no stunt scene to advance");
        return ROLLER_ED_RESULT_NO_SCENE;
    }
    if (uiTicks > ROLLER_ED_MAX_STUNT_TICKS_PER_CALL) {
        roller_ed_set_error("stunt tick count %u exceeds the per-call limit %u",
                            uiTicks, ROLLER_ED_MAX_STUNT_TICKS_PER_CALL);
        return ROLLER_ED_RESULT_INVALID_ARGUMENT;
    }
    if (uiTicks == 0u)
        return ROLLER_ED_RESULT_OK;

    /* The live legacy arrays are what stunt animation mutates. Materialize
     * the authored extraction before the first tick so OBJ/glTF export and
     * epoch-based callers keep seeing the document geometry, not whichever
     * animation frame happened to be on screen. */
    eResult = roller_ed_sync_geometry_cache();
    if (eResult != ROLLER_ED_RESULT_OK)
        return eResult;
    return roller_ed_legacy_scene_advance_stunts(
        uiTicks, s_szLastError, sizeof(s_szLastError));
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
    return roller_ed_legacy_scene_render(
        pbyPixels, uiBufferSize, uiRowPitch, uiWidth, uiHeight,
        s_szLastError, sizeof(s_szLastError));
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
        pState->uiStructSize, pState->uiVersion,
        ROLLER_ED_OVERLAY_STATE_VERSION, sizeof(*pState),
        "tEdOverlayState");
    if (eResult != ROLLER_ED_RESULT_OK)
        return eResult;
    if ((pState->uiFlags & ~(uint32_t)ROLLER_ED_OVERLAY_KNOWN_FLAGS) != 0u) {
        roller_ed_set_error(
            "tEdOverlayState.uiFlags 0x%08x sets bits API version %u does not "
            "define", pState->uiFlags, (unsigned)ROLLER_ED_API_VERSION);
        return ROLLER_ED_RESULT_INVALID_ARGUMENT;
    }
    if (((pState->uiSurfaceClassMask | pState->uiWireframeClassMask)
            & ~(uint32_t)ROLLER_ED_OVERLAY_ALL_SURFACE_CLASSES) != 0u) {
        roller_ed_set_error(
            "tEdOverlayState class masks select surface classes beyond %u",
            (unsigned)ROLLER_ED_SURFACE_CLASS_COUNT);
        return ROLLER_ED_RESULT_INVALID_ARGUMENT;
    }
    /*
     * E3A-S6. The test-car selection is validated whether or not SHOW_TEST_CAR
     * is set, for the same reason the flag bits are: silently storing an
     * out-of-range design and only failing once the host ticks the box would
     * report the error against the wrong call. Both index fixed tables, so
     * out of range is a host bug, not a preference.
     */
    if (pState->uiTestCarDesign >= ROLLER_ED_TEST_CAR_DESIGN_COUNT) {
        roller_ed_set_error(
            "tEdOverlayState.uiTestCarDesign %u is not below %u",
            pState->uiTestCarDesign,
            (unsigned)ROLLER_ED_TEST_CAR_DESIGN_COUNT);
        return ROLLER_ED_RESULT_INVALID_ARGUMENT;
    }
    if (pState->uiTestCarAiLine >= ROLLER_ED_TEST_CAR_AI_LINE_COUNT) {
        roller_ed_set_error(
            "tEdOverlayState.uiTestCarAiLine %u is not below %u",
            pState->uiTestCarAiLine,
            (unsigned)ROLLER_ED_TEST_CAR_AI_LINE_COUNT);
        return ROLLER_ED_RESULT_INVALID_ARGUMENT;
    }
    /*
     * AD-7d: overlays are a view setting over the same authored geometry, so
     * this deliberately advances neither the geometry epoch nor the track
     * generation.  Doing either would throw away E4A-S5's per-epoch extraction
     * on every toggle.
     */
    return roller_ed_legacy_scene_set_overlay_state(
        pState, s_szLastError, sizeof(s_szLastError));
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
        pMesh->uiStructSize, pMesh->uiVersion,
        ROLLER_ED_REFERENCE_MESH_VERSION, sizeof(*pMesh),
        "tEdReferenceMesh");
    if (eResult != ROLLER_ED_RESULT_OK)
        return eResult;
    /*
     * AD-7d again: a reference mesh is scenery the host owns, not authored
     * track geometry, so this advances neither the geometry epoch nor the
     * track generation and E4A-S5's cached extraction stays valid.
     *
     * Vertex and texture data are both copied during this call; nothing the
     * caller passed is retained (AD-13). A NULL vertex pointer or a zero
     * vertex count clears the mesh, and a failed replacement leaves the
     * previous one intact.
     */
    return roller_ed_legacy_scene_set_reference_mesh(
        pMesh, s_szLastError, sizeof(s_szLastError));
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
        pSizesOut->uiStructSize, pSizesOut->uiVersion,
        ROLLER_ED_GEOMETRY_SIZES_VERSION, sizeof(*pSizesOut),
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
    /*
     * AD-7d: EMPTY and FAILED publish zero counts, and the call still returns
     * OK either way so uiSceneState is always readable. A failed extraction
     * is reported the same way, with the reason left in the error text --
     * refusing the query would hide the very field the caller needs.
     */
    if (s_eSceneState == ROLLER_ED_SCENE_READY
            && roller_ed_sync_geometry_cache() == ROLLER_ED_RESULT_OK) {
        Sizes.uiVertexCount = s_GeometryExtract.uiVertexCount;
        Sizes.uiIndexCount = s_GeometryExtract.uiIndexCount;
        Sizes.uiPrimitiveCount = s_GeometryExtract.uiPrimitiveCount;
        Sizes.uiMaterialCount = s_GeometryExtract.uiMaterialCount;
    }
    *pSizesOut = Sizes;
    return ROLLER_ED_RESULT_OK;
}

eRollerEdResult ROLLER_ED_CALL RollerEd_QueryTowerCount(
    uint32_t *puiCountOut)
{
    eRollerEdResult eResult = roller_ed_require_worker();
    uint32_t uiCount;

    if (eResult != ROLLER_ED_RESULT_OK)
        return eResult;
    if (!puiCountOut) {
        roller_ed_set_error("RollerEd_QueryTowerCount requires puiCountOut");
        return ROLLER_ED_RESULT_INVALID_ARGUMENT;
    }
    if (s_eSceneState != ROLLER_ED_SCENE_READY) {
        roller_ed_set_error("there is no tower scene");
        return ROLLER_ED_RESULT_NO_SCENE;
    }

    uiCount = roller_ed_legacy_scene_tower_count();
    *puiCountOut = uiCount;
    return ROLLER_ED_RESULT_OK;
}

eRollerEdResult ROLLER_ED_CALL RollerEd_QueryTower(
    uint32_t uiTowerIndex, tEdTowerInfo *pInfoOut)
{
    eRollerEdResult eResult = roller_ed_require_worker();
    tEdTowerInfo Info;
    uint32_t uiCount;

    if (eResult != ROLLER_ED_RESULT_OK)
        return eResult;
    if (!pInfoOut) {
        roller_ed_set_error("RollerEd_QueryTower requires pInfoOut");
        return ROLLER_ED_RESULT_INVALID_ARGUMENT;
    }
    eResult = roller_ed_validate_struct(
        pInfoOut->uiStructSize, pInfoOut->uiVersion,
        ROLLER_ED_TOWER_INFO_VERSION, sizeof(*pInfoOut),
        "tEdTowerInfo");
    if (eResult != ROLLER_ED_RESULT_OK)
        return eResult;
    if (s_eSceneState != ROLLER_ED_SCENE_READY) {
        roller_ed_set_error("there is no tower scene");
        return ROLLER_ED_RESULT_NO_SCENE;
    }

    uiCount = roller_ed_legacy_scene_tower_count();
    if (uiTowerIndex >= uiCount) {
        roller_ed_set_error("tower index %u is not below tower count %u",
                            uiTowerIndex, uiCount);
        return ROLLER_ED_RESULT_INVALID_ARGUMENT;
    }

    memset(&Info, 0, sizeof(Info));
    Info.uiStructSize = sizeof(Info);
    Info.uiVersion = ROLLER_ED_TOWER_INFO_VERSION;
    roller_ed_legacy_scene_query_tower(uiTowerIndex, &Info);
    *pInfoOut = Info;
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

    if (eResult != ROLLER_ED_RESULT_OK)
        return eResult;
    if (s_eSceneState != ROLLER_ED_SCENE_READY) {
        roller_ed_set_error("there is no geometry scene");
        return ROLLER_ED_RESULT_NO_SCENE;
    }
    /*
     * The epoch is checked before the capacities and both before any write,
     * so a reload or a failed load between query and fill can never leave the
     * caller with a partly overwritten buffer sized for the previous track
     * (AD-7a). A failed load clears the scene without touching the track
     * generation, which is why this validates the epoch (AD-7d).
     */
    if (uiExpectedGeometryEpoch != s_uiGeometryEpoch) {
        roller_ed_set_error("geometry epoch %u is stale; current epoch is %u",
                            uiExpectedGeometryEpoch, s_uiGeometryEpoch);
        return ROLLER_ED_RESULT_STALE;
    }
    eResult = roller_ed_sync_geometry_cache();
    if (eResult != ROLLER_ED_RESULT_OK)
        return eResult;

    if ((s_GeometryExtract.uiVertexCount != 0u && !pVerts)
            || (s_GeometryExtract.uiIndexCount != 0u && !puiIndices)
            || (s_GeometryExtract.uiPrimitiveCount != 0u && !pPrims)
            || (s_GeometryExtract.uiMaterialCount != 0u && !pMats)) {
        roller_ed_set_error(
            "RollerEd_FillGeometry requires every buffer the scene needs");
        return ROLLER_ED_RESULT_INVALID_ARGUMENT;
    }
    if (uiVertexCapacity < s_GeometryExtract.uiVertexCount
            || uiIndexCapacity < s_GeometryExtract.uiIndexCount
            || uiPrimitiveCapacity < s_GeometryExtract.uiPrimitiveCount
            || uiMaterialCapacity < s_GeometryExtract.uiMaterialCount) {
        roller_ed_set_error(
            "geometry needs %u vertices, %u indices, %u primitives, and %u "
            "materials",
            s_GeometryExtract.uiVertexCount, s_GeometryExtract.uiIndexCount,
            s_GeometryExtract.uiPrimitiveCount,
            s_GeometryExtract.uiMaterialCount);
        return ROLLER_ED_RESULT_BUFFER_TOO_SMALL;
    }

    /* Copies only: the cache stays core-owned. */
    if (s_GeometryExtract.uiVertexCount != 0u)
        memcpy(pVerts, s_GeometryExtract.pVertices,
               (size_t)s_GeometryExtract.uiVertexCount * sizeof(*pVerts));
    if (s_GeometryExtract.uiIndexCount != 0u)
        memcpy(puiIndices, s_GeometryExtract.puiIndices,
               (size_t)s_GeometryExtract.uiIndexCount * sizeof(*puiIndices));
    if (s_GeometryExtract.uiPrimitiveCount != 0u)
        memcpy(pPrims, s_GeometryExtract.pPrimitives,
               (size_t)s_GeometryExtract.uiPrimitiveCount * sizeof(*pPrims));
    if (s_GeometryExtract.uiMaterialCount != 0u)
        memcpy(pMats, s_GeometryExtract.pMaterials,
               (size_t)s_GeometryExtract.uiMaterialCount * sizeof(*pMats));
    return ROLLER_ED_RESULT_OK;
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
