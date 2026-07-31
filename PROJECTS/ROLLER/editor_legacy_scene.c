#include "editor_legacy_scene.h"

#include "3d.h"
#include "car.h"
#include "editor_camera.h"
#include "game_render.h"
#include "loadtrak.h"
#include "scene_render_gpu.h"

#define SDL_MAIN_HANDLED 1
#include <SDL3/SDL.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static SDL_GPUDevice *s_pEditorGPUDevice;
static int s_bEditorOwnsGPUDevice;
static int s_bLegacyInitialized;
static eRollerEdRenderer s_eActiveRenderer;

static void editor_scene_set_error(char *szError, size_t uiErrorCapacity,
                                   const char *szFormat, ...)
{
    va_list Args;

    if (!szError || uiErrorCapacity == 0u)
        return;
    va_start(Args, szFormat);
    vsnprintf(szError, uiErrorCapacity, szFormat, Args);
    va_end(Args);
    szError[uiErrorCapacity - 1u] = '\0';
}

static eRollerEdResult editor_scene_create_software_renderer(
    char *szError, size_t uiErrorCapacity)
{
    g_pGameRenderer = game_render_create(NULL, NULL);
    if (!g_pGameRenderer) {
        editor_scene_set_error(szError, uiErrorCapacity,
                               "software renderer creation failed");
        return ROLLER_ED_RESULT_OUT_OF_MEMORY;
    }
    game_render_set_mode(g_pGameRenderer, GAME_RENDER_SOFTWARE);
    game_render_set_force_gpu_load(g_pGameRenderer, false);
    s_eActiveRenderer = ROLLER_ED_RENDERER_SOFTWARE;
    editor_scene_set_error(szError, uiErrorCapacity, "");
    return ROLLER_ED_RESULT_OK;
}

static eRollerEdResult editor_scene_create_gpu_renderer(
    char *szError, size_t uiErrorCapacity)
{
#if defined(IS_WASM)
    editor_scene_set_error(szError, uiErrorCapacity,
                           "windowless GPU rendering is unavailable on wasm");
    return ROLLER_ED_RESULT_RENDERER_UNAVAILABLE;
#else
    eRollerEdResult eResult = ROLLER_ED_RESULT_OK;

    s_pEditorGPUDevice = SDL_CreateGPUDevice(
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL
            | SDL_GPU_SHADERFORMAT_DXIL,
        false, NULL);
    if (!s_pEditorGPUDevice) {
        editor_scene_set_error(szError, uiErrorCapacity,
                               "windowless GPU device creation failed: %s",
                               SDL_GetError());
        return ROLLER_ED_RESULT_RENDERER_UNAVAILABLE;
    }
    s_bEditorOwnsGPUDevice = -1;

    g_pGameRenderer = game_render_create(s_pEditorGPUDevice, NULL);
    if (!g_pGameRenderer || !game_render_get_gpu(g_pGameRenderer)) {
        if (g_pGameRenderer) {
            game_render_destroy(g_pGameRenderer);
            g_pGameRenderer = NULL;
        }
        SDL_DestroyGPUDevice(s_pEditorGPUDevice);
        s_pEditorGPUDevice = NULL;
        s_bEditorOwnsGPUDevice = 0;
        editor_scene_set_error(szError, uiErrorCapacity,
                               "windowless GPU renderer creation failed: %s",
                               SDL_GetError());
        eResult = ROLLER_ED_RESULT_RENDERER_UNAVAILABLE;
    } else {
        /* Select before loading so the legacy texture loaders populate the GPU
         * atlas instead of deferring it as a software-only session. */
        game_render_set_mode(g_pGameRenderer, GAME_RENDER_GPU);
        game_render_set_force_gpu_load(g_pGameRenderer, true);
        s_eActiveRenderer = ROLLER_ED_RENDERER_GPU;
        editor_scene_set_error(szError, uiErrorCapacity, "");
    }
    return eResult;
#endif
}

static eRollerEdResult editor_scene_ensure_renderer(
    eRollerEdRenderer ePreferredRenderer,
    uint32_t uiAllowSoftwareFallback,
    char *szError, size_t uiErrorCapacity)
{
    eRollerEdResult eResult;

    if (!s_bLegacyInitialized) {
        init();
        init_screen();
        if (!scrbuf) {
            editor_scene_set_error(szError, uiErrorCapacity,
                                   "legacy screen-buffer initialization failed");
            return ROLLER_ED_RESULT_OUT_OF_MEMORY;
        }
        s_bLegacyInitialized = -1;
    }

    if (g_pGameRenderer)
        return ROLLER_ED_RESULT_OK;
    if (ePreferredRenderer == ROLLER_ED_RENDERER_SOFTWARE)
        return editor_scene_create_software_renderer(szError, uiErrorCapacity);

    eResult = editor_scene_create_gpu_renderer(szError, uiErrorCapacity);
    if (eResult == ROLLER_ED_RESULT_OK || !uiAllowSoftwareFallback)
        return eResult;
    return editor_scene_create_software_renderer(szError, uiErrorCapacity);
}

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
    eRollerEdResult eResult = editor_scene_ensure_renderer(
        ePreferredRenderer, uiAllowSoftwareFallback, szError, uiErrorCapacity);

    if (eResult != ROLLER_ED_RESULT_OK)
        return eResult;
    return loadtrack_from_stage_with_assets_editor_ex(
        szTrackPath, pStage, szDocumentAssetRoot, szFallbackAssetRoot, 0,
        szError, uiErrorCapacity);
}

eRollerEdResult roller_ed_legacy_scene_set_camera(
    const tEdCameraState *pCamera,
    char *szError,
    size_t uiErrorCapacity)
{
    if (!pCamera) {
        editor_scene_set_error(szError, uiErrorCapacity,
                               "editor camera state is required");
        return ROLLER_ED_RESULT_INVALID_ARGUMENT;
    }
    roller_ed_camera_set(pCamera);
    editor_scene_set_error(szError, uiErrorCapacity, "");
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
    if (!g_pGameRenderer) {
        editor_scene_set_error(szError, uiErrorCapacity,
                               "legacy renderer is not initialized");
        return ROLLER_ED_RESULT_RENDERER_UNAVAILABLE;
    }

    if (s_eActiveRenderer == ROLLER_ED_RENDERER_SOFTWARE) {
        uint32_t uiNativeWidth = XMAX > 0 ? (uint32_t)XMAX : 320u;
        uint32_t uiNativeHeight = YMAX > 0 ? (uint32_t)YMAX : 200u;

        if (uiNativeWidth > SCRBUF_MAX_PIXELS / uiNativeHeight) {
            editor_scene_set_error(szError, uiErrorCapacity,
                                   "invalid native software render size %ux%u",
                                   uiNativeWidth, uiNativeHeight);
            return ROLLER_ED_RESULT_INTERNAL_ERROR;
        }
        memset(scrbuf, 0, (size_t)uiNativeWidth * uiNativeHeight);
        winx = 0;
        winy = 0;
        winw = (int)uiNativeWidth;
        winh = (int)uiNativeHeight;
        game_render_set_viewport(g_pGameRenderer, 0, 0, winw, winh);
        game_render_begin_frame(g_pGameRenderer);
        draw_road(scrbuf, ViewType[0], (unsigned int)DriveView[0], 0, 0);
        if (!game_render_end_frame_software_readback(
                g_pGameRenderer, scrbuf, uiNativeWidth,
                uiNativeWidth, uiNativeHeight,
                pbyPixels, uiBufferSize, uiRowPitch, uiWidth, uiHeight)) {
            editor_scene_set_error(szError, uiErrorCapacity,
                                   "software scene readback failed");
            return ROLLER_ED_RESULT_INTERNAL_ERROR;
        }
        editor_scene_set_error(szError, uiErrorCapacity, "");
        return ROLLER_ED_RESULT_OK;
    }

#if defined(IS_WASM)
    editor_scene_set_error(szError, uiErrorCapacity,
                           "windowless GPU rendering is unavailable on wasm");
    return ROLLER_ED_RESULT_RENDERER_UNAVAILABLE;
#else
    SceneRendererGPU *pGPU = game_render_get_gpu(g_pGameRenderer);
    if (!pGPU) {
        editor_scene_set_error(szError, uiErrorCapacity,
                               "windowless GPU renderer is unavailable");
        return ROLLER_ED_RESULT_RENDERER_UNAVAILABLE;
    }

    game_render_set_viewport(g_pGameRenderer, 0, 0, (int)uiWidth,
                             (int)uiHeight);
    game_render_begin_frame(g_pGameRenderer);
    /* draw_road is the scene-only path: the gameplay panel/HUD compositor is
     * intentionally not part of editor readback. */
    draw_road(scrbuf, ViewType[0], (unsigned int)DriveView[0], 0, 0);
    if (!scene_render_gpu_end_frame_readback(
            pGPU, pbyPixels, uiBufferSize, uiRowPitch, uiWidth, uiHeight)) {
        editor_scene_set_error(szError, uiErrorCapacity,
                               "windowless scene readback failed: %s",
                               SDL_GetError());
        return ROLLER_ED_RESULT_INTERNAL_ERROR;
    }
    editor_scene_set_error(szError, uiErrorCapacity, "");
    return ROLLER_ED_RESULT_OK;
#endif
}

void roller_ed_legacy_scene_unload(void)
{
    /* The legacy loader owns process-global scene arrays.  The facade scene
     * state gates all access after unload; a subsequent install replaces the
     * arrays and texture banks in place. */
}

void roller_ed_legacy_scene_shutdown(void)
{
    roller_ed_camera_reset();
    if (g_pGameRenderer) {
        game_render_destroy(g_pGameRenderer);
        g_pGameRenderer = NULL;
    }
    if (s_bEditorOwnsGPUDevice && s_pEditorGPUDevice) {
#if !defined(IS_WASM)
        SDL_DestroyGPUDevice(s_pEditorGPUDevice);
#endif
        s_pEditorGPUDevice = NULL;
    }
    s_bEditorOwnsGPUDevice = 0;
    s_eActiveRenderer = 0;
}
