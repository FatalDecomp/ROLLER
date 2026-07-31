#include "editor_legacy_scene.h"

#include "3d.h"
#include "car.h"
#include "game_render.h"
#include "loadtrak.h"
#include "scene_render_gpu.h"

#define SDL_MAIN_HANDLED 1
#include <SDL3/SDL.h>

#include <stdarg.h>
#include <stdio.h>

static SDL_GPUDevice *s_pEditorGPUDevice;
static int s_bEditorOwnsGPUDevice;
static int s_bLegacyInitialized;

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

static eRollerEdResult editor_scene_ensure_renderer(
    char *szError, size_t uiErrorCapacity)
{
#if defined(IS_WASM)
    editor_scene_set_error(szError, uiErrorCapacity,
                           "windowless GPU rendering is unavailable on wasm");
    return ROLLER_ED_RESULT_RENDERER_UNAVAILABLE;
#else
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
        return ROLLER_ED_RESULT_RENDERER_UNAVAILABLE;
    }

    /* Select before loading so the legacy texture loaders populate the GPU
     * atlas instead of deferring it as a software-only session. */
    game_render_set_mode(g_pGameRenderer, GAME_RENDER_GPU);
    game_render_set_force_gpu_load(g_pGameRenderer, true);
    return ROLLER_ED_RESULT_OK;
#endif
}

eRollerEdResult roller_ed_legacy_scene_install(
    const char *szTrackPath,
    const tEdTrackStage *pStage,
    const char *szDocumentAssetRoot,
    const char *szFallbackAssetRoot,
    char *szError,
    size_t uiErrorCapacity)
{
    eRollerEdResult eResult = editor_scene_ensure_renderer(
        szError, uiErrorCapacity);

    if (eResult != ROLLER_ED_RESULT_OK)
        return eResult;
    return loadtrack_from_stage_with_assets_ex(
        szTrackPath, pStage, szDocumentAssetRoot, szFallbackAssetRoot, 0,
        szError, uiErrorCapacity);
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
#if defined(IS_WASM)
    (void)pbyPixels;
    (void)uiBufferSize;
    (void)uiRowPitch;
    (void)uiWidth;
    (void)uiHeight;
    editor_scene_set_error(szError, uiErrorCapacity,
                           "windowless GPU rendering is unavailable on wasm");
    return ROLLER_ED_RESULT_RENDERER_UNAVAILABLE;
#else
    SceneRendererGPU *pGPU;

    if (!g_pGameRenderer) {
        editor_scene_set_error(szError, uiErrorCapacity,
                               "legacy renderer is not initialized");
        return ROLLER_ED_RESULT_RENDERER_UNAVAILABLE;
    }
    pGPU = game_render_get_gpu(g_pGameRenderer);
    if (!pGPU) {
        editor_scene_set_error(szError, uiErrorCapacity,
                               "windowless GPU renderer is unavailable");
        return ROLLER_ED_RESULT_RENDERER_UNAVAILABLE;
    }

    /* E1-S5 replaces this legacy car-derived default with the facade camera.
     * For E1-S4 it is sufficient to prove that the committed document is the
     * scene being submitted and downloaded. */
    game_render_set_viewport(g_pGameRenderer, 0, 0, (int)uiWidth,
                             (int)uiHeight);
    game_render_begin_frame(g_pGameRenderer);
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
}
