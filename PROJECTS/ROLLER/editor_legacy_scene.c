#include "editor_legacy_scene.h"

#include "3d.h"
#include "building.h"
#include "car.h"
#include "drawtrk3.h"
#include "editor_camera.h"
#include "editor_overlay.h"
#include "editor_reference_mesh.h"
#include "editor_surface.h"
#include "editor_test_car.h"
#include "func2.h"
#include "game_render.h"
#include "game_render_hw.h"
#include "graphics.h"
#include "horizon.h"
#include "loadtrak.h"
#include "moving.h"
#include "roller.h"
#include "roller_core_error.h"
#include "scene_render_gpu.h"
#include "tower.h"
#include "types.h"

#define SDL_MAIN_HANDLED 1
#include <SDL3/SDL.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EDITOR_GPU_SHADER_FORMATS \
    (SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL \
     | SDL_GPU_SHADERFORMAT_DXIL)

static SDL_GPUDevice *s_pEditorGPUDevice;
static int s_bEditorOwnsGPUDevice;
static int s_bLegacyInitialized;
static int s_bEditorCloudsAttempted;
static int s_bEditorCloudsReady;
static char s_szEditorCloudAsset[ROLLER_MAX_PATH];
static eRollerEdRenderer s_eActiveRenderer;

static void editor_scene_set_legacy_display(
    eRollerEdSoftwareDisplay eDisplay)
{
    int bSvga = eDisplay == ROLLER_ED_SOFTWARE_DISPLAY_SVGA;

    game_svga = bSvga ? -1 : 0;
    SVGA_ON = game_svga;
    current_mode = game_svga;
    game_size = bSvga ? 128 : 64;
    scr_size = game_size;
    XMAX = bSvga ? 640 : 320;
    YMAX = bSvga ? 400 : 200;
    winx = 0;
    winy = 0;
    winw = XMAX;
    winh = YMAX;
}

/*
 * draw_road already renders the real horizon and calls displayclouds(), but
 * the game normally prepares its generic texture bank and cloud transforms
 * later in race startup. The editor deliberately skips that gameplay setup,
 * so prepare just those two atmosphere dependencies here. This runs once per
 * facade lifecycle: serialized edit reloads therefore keep a stable sky
 * instead of consuming more random numbers and moving every cloud.
 *
 * Clouds are optional scenery. A minimal asset tree that was sufficient for
 * the editor before this feature must still load a track; when GENTEX.DRH is
 * absent or rejected, leave the texture-off bit set and preserve the scene.
 */
static void editor_scene_prepare_clouds(void)
{
    char szResolved[ROLLER_MAX_PATH];
    int bLoadFailed;

    if (!loadtrack_resolve_editor_asset(gencartex_name, szResolved)) {
        s_bEditorCloudsAttempted = -1;
        s_bEditorCloudsReady = 0;
        s_szEditorCloudAsset[0] = '\0';
        textures_off |= TEX_OFF_CLOUDS;
        return;
    }

    if (s_bEditorCloudsAttempted) {
        if (strcmp(s_szEditorCloudAsset, szResolved) == 0) {
            if (s_bEditorCloudsReady)
                textures_off &= ~TEX_OFF_CLOUDS;
            else
                textures_off |= TEX_OFF_CLOUDS;
            return;
        }
    }
    s_bEditorCloudsAttempted = -1;
    s_bEditorCloudsReady = 0;
    snprintf(s_szEditorCloudAsset, sizeof(s_szEditorCloudAsset),
             "%s", szResolved);

#if defined(ROLLER_EDITOR_CORE)
    roller_core_error_clear();
#endif
    LoadGenericCarTexturesFromFile(szResolved);
    bLoadFailed = !cargen_vga
            || num_textures[18] < 13
            || game_render_get_texture_handle(g_pGameRenderer, 18)
                == TEXTURE_HANDLE_INVALID;
#if defined(ROLLER_EDITOR_CORE)
    bLoadFailed = bLoadFailed || roller_core_error_pending();
#endif
    if (bLoadFailed) {
        textures_off |= TEX_OFF_CLOUDS;
#if defined(ROLLER_EDITOR_CORE)
        roller_core_error_clear();
#endif
        return;
    }

    initclouds();
    textures_off &= ~TEX_OFF_CLOUDS;
    s_bEditorCloudsReady = -1;
}

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
        EDITOR_GPU_SHADER_FORMATS,
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
        char szGPUError[256];

        snprintf(szGPUError, sizeof(szGPUError), "%s", SDL_GetError());
        if (g_pGameRenderer) {
            game_render_destroy(g_pGameRenderer);
            g_pGameRenderer = NULL;
        }
        SDL_DestroyGPUDevice(s_pEditorGPUDevice);
        s_pEditorGPUDevice = NULL;
        s_bEditorOwnsGPUDevice = 0;
        editor_scene_set_error(szError, uiErrorCapacity,
                               "windowless GPU renderer creation failed: %s",
                               szGPUError);
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

static eRollerEdResult editor_scene_ensure_legacy_initialized(
    char *szError, size_t uiErrorCapacity)
{
    if (s_bLegacyInitialized)
        return ROLLER_ED_RESULT_OK;

    init();
    init_screen();
    if (!scrbuf) {
        editor_scene_set_error(szError, uiErrorCapacity,
                               "legacy screen-buffer initialization failed");
        return ROLLER_ED_RESULT_OUT_OF_MEMORY;
    }
    s_bLegacyInitialized = -1;
    return ROLLER_ED_RESULT_OK;
}

static eRollerEdResult editor_scene_ensure_renderer(
    eRollerEdRenderer ePreferredRenderer,
    uint32_t uiAllowSoftwareFallback,
    char *szError, size_t uiErrorCapacity)
{
    eRollerEdResult eResult;

    eResult = editor_scene_ensure_legacy_initialized(szError, uiErrorCapacity);
    if (eResult != ROLLER_ED_RESULT_OK)
        return eResult;

    if (g_pGameRenderer)
        return ROLLER_ED_RESULT_OK;
    if (ePreferredRenderer == ROLLER_ED_RENDERER_SOFTWARE)
        return editor_scene_create_software_renderer(szError, uiErrorCapacity);

    eResult = editor_scene_create_gpu_renderer(szError, uiErrorCapacity);
    if (eResult == ROLLER_ED_RESULT_OK || !uiAllowSoftwareFallback)
        return eResult;
    return editor_scene_create_software_renderer(szError, uiErrorCapacity);
}

uint32_t roller_ed_legacy_scene_get_available_renderers(void)
{
    uint32_t uiAvailable = ROLLER_ED_RENDERER_SOFTWARE;

#if !defined(IS_WASM)
    if (s_pEditorGPUDevice
            || (g_pGameRenderer && game_render_get_gpu(g_pGameRenderer))
            || SDL_GPUSupportsShaderFormats(EDITOR_GPU_SHADER_FORMATS, NULL))
        uiAvailable |= ROLLER_ED_RENDERER_GPU;
#endif
    return uiAvailable;
}

eRollerEdResult roller_ed_legacy_scene_select_renderer(
    eRollerEdRenderer eKind,
    char *szError,
    size_t uiErrorCapacity)
{
    eRollerEdResult eResult;

    eResult = editor_scene_ensure_legacy_initialized(szError, uiErrorCapacity);
    if (eResult != ROLLER_ED_RESULT_OK)
        return eResult;

    if (eKind == ROLLER_ED_RENDERER_SOFTWARE) {
        if (!g_pGameRenderer)
            return editor_scene_create_software_renderer(
                szError, uiErrorCapacity);
        /* Once the lazy GPU backend exists, keep its texture state synchronized
         * while software is active so a later switch remains a frame-boundary
         * operation and never has to rebuild a partially stale backend. */
        game_render_set_force_gpu_load(
            g_pGameRenderer, game_render_get_gpu(g_pGameRenderer) != NULL);
        game_render_set_mode(g_pGameRenderer, GAME_RENDER_SOFTWARE);
        s_eActiveRenderer = ROLLER_ED_RENDERER_SOFTWARE;
        editor_scene_set_error(szError, uiErrorCapacity, "");
        return ROLLER_ED_RESULT_OK;
    }

    if ((roller_ed_legacy_scene_get_available_renderers()
            & ROLLER_ED_RENDERER_GPU) == 0u) {
        editor_scene_set_error(szError, uiErrorCapacity,
                               "windowless GPU renderer is unavailable");
        return ROLLER_ED_RESULT_RENDERER_UNAVAILABLE;
    }
    if (!g_pGameRenderer)
        return editor_scene_create_gpu_renderer(szError, uiErrorCapacity);

    if (!game_render_get_gpu(g_pGameRenderer)) {
#if defined(IS_WASM)
        editor_scene_set_error(szError, uiErrorCapacity,
                               "windowless GPU renderer is unavailable on wasm");
        return ROLLER_ED_RESULT_RENDERER_UNAVAILABLE;
#else
        SDL_GPUDevice *pCandidateDevice = SDL_CreateGPUDevice(
            EDITOR_GPU_SHADER_FORMATS, false, NULL);

        if (!pCandidateDevice) {
            editor_scene_set_error(szError, uiErrorCapacity,
                                   "windowless GPU device creation failed: %s",
                                   SDL_GetError());
            return ROLLER_ED_RESULT_RENDERER_UNAVAILABLE;
        }
        if (!game_render_attach_gpu_device(
                g_pGameRenderer, pCandidateDevice)) {
            char szGPUError[256];

            snprintf(szGPUError, sizeof(szGPUError), "%s", SDL_GetError());
            SDL_DestroyGPUDevice(pCandidateDevice);
            editor_scene_set_error(szError, uiErrorCapacity,
                                   "GPU renderer switch failed while preserving software mode: %s",
                                   szGPUError);
            return ROLLER_ED_RESULT_GPU_FAILED;
        }
        s_pEditorGPUDevice = pCandidateDevice;
        s_bEditorOwnsGPUDevice = -1;
#endif
    }

    game_render_set_force_gpu_load(g_pGameRenderer, true);
    game_render_set_mode(g_pGameRenderer, GAME_RENDER_GPU);
    s_eActiveRenderer = ROLLER_ED_RENDERER_GPU;
    editor_scene_set_error(szError, uiErrorCapacity, "");
    return ROLLER_ED_RESULT_OK;
}

eRollerEdResult roller_ed_legacy_scene_set_graphics_settings(
    const tEdGraphicsSettings *pSettings,
    char *szError,
    size_t uiErrorCapacity)
{
    eRollerEdResult eResult = roller_ed_legacy_scene_select_renderer(
        pSettings->eRenderer, szError, uiErrorCapacity);

    /* Match editor initialization: a requested GPU is preferred, but an
     * unavailable windowless backend must not make the preview unusable. */
    if (eResult != ROLLER_ED_RESULT_OK
            && pSettings->eRenderer == ROLLER_ED_RENDERER_GPU) {
        eResult = roller_ed_legacy_scene_select_renderer(
            ROLLER_ED_RENDERER_SOFTWARE, szError, uiErrorCapacity);
    }
    if (eResult != ROLLER_ED_RESULT_OK)
        return eResult;

    /* VGA/SVGA controls the software renderer's native framebuffer. Keep the
     * GPU path on the editor's SVGA logical canvas so this software-only
     * preference cannot change GPU projection or track detail. */
    editor_scene_set_legacy_display(
        pSettings->eRenderer == ROLLER_ED_RENDERER_SOFTWARE
            ? pSettings->eSoftwareDisplay
            : ROLLER_ED_SOFTWARE_DISPLAY_SVGA);

    g_fDrawDistanceFraction = pSettings->fDrawDistanceFraction;
    g_iAntiAliasing = (int)pSettings->eAntiAliasing;
    g_iAnisotropyLevel = (int)pSettings->eAnisotropy;
    g_iTextureFilter = (int)pSettings->eTextureFilter;
    g_bTrilinear = pSettings->uiTrilinear != 0u;
    g_fLodBias = pSettings->fLodBias;
    g_bEmulateSoftwareTrackBorders =
        pSettings->uiEmulateTransparentBorders != 0u;

    game_render_set_antialiasing(g_pGameRenderer, g_iAntiAliasing);
    game_render_set_anisotropy_level(g_pGameRenderer, g_iAnisotropyLevel);
    game_render_set_texture_filter(g_pGameRenderer, g_iTextureFilter);
    game_render_set_trilinear(g_pGameRenderer, g_bTrilinear);
    game_render_set_lod_bias(g_pGameRenderer, g_fLodBias);
    game_render_set_emulate_software_track_borders(
        g_pGameRenderer, g_bEmulateSoftwareTrackBorders);
    editor_scene_set_error(szError, uiErrorCapacity, "");
    return ROLLER_ED_RESULT_OK;
}

uint32_t roller_ed_legacy_scene_tower_count(void)
{
    if (NumTowers <= 0)
        return 0u;
    if (NumTowers > MAX_TOWERS)
        return MAX_TOWERS;
    return (uint32_t)NumTowers;
}

void roller_ed_legacy_scene_query_tower(
    uint32_t uiTowerIndex, tEdTowerInfo *pInfoOut)
{
    int iChunkIdx = TowerBase[uiTowerIndex].iChunkIdx;

    pInfoOut->uiChunkId = (uint32_t)iChunkIdx;
    pInfoOut->fWorldPosition[0] = TowerX[uiTowerIndex];
    pInfoOut->fWorldPosition[1] = TowerY[uiTowerIndex];
    pInfoOut->fWorldPosition[2] = TowerZ[uiTowerIndex];
    pInfoOut->fAnchorPosition[0] = -localdata[iChunkIdx].pointAy[3].fX;
    pInfoOut->fAnchorPosition[1] = -localdata[iChunkIdx].pointAy[3].fY;
    pInfoOut->fAnchorPosition[2] = -localdata[iChunkIdx].pointAy[3].fZ;
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
    eResult = loadtrack_from_stage_with_assets_editor_ex(
        szTrackPath, pStage, szDocumentAssetRoot, szFallbackAssetRoot, 0,
        szError, uiErrorCapacity);
    if (eResult == ROLLER_ED_RESULT_OK) {
        /* Race startup derives the five software transparency/shadow lookup
         * tables immediately after loading PALETTE.PAL. The editor skips race
         * startup, so build them here after every successful document palette
         * load; an all-zero shade_palette turns glass/loop surfaces black. */
        FindShades();
        editor_scene_prepare_clouds();
    }
    return eResult;
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

/*
 * Overlay state reaches the renderer the same way the camera does, and like
 * the camera it touches no loaded geometry: nothing here advances the geometry
 * epoch or the track generation (AD-7d).
 */
eRollerEdResult roller_ed_legacy_scene_set_overlay_state(
    const tEdOverlayState *pState,
    char *szError,
    size_t uiErrorCapacity)
{
    if (!pState) {
        editor_scene_set_error(szError, uiErrorCapacity,
                               "editor overlay state is required");
        return ROLLER_ED_RESULT_INVALID_ARGUMENT;
    }
    roller_ed_overlay_set(pState);
    editor_scene_set_error(szError, uiErrorCapacity, "");
    return ROLLER_ED_RESULT_OK;
}

/*
 * E3A-S7. The reference mesh is a view setting too: like the camera and the
 * overlay it never touches loaded geometry, so it advances neither the
 * geometry epoch nor the track generation (AD-7d) and E4A-S5's per-epoch
 * extraction survives every replacement.
 */
eRollerEdResult roller_ed_legacy_scene_set_reference_mesh(
    const tEdReferenceMesh *pMesh,
    char *szError,
    size_t uiErrorCapacity)
{
    char szMeshError[256];
    eEdReferenceMeshResult eResult;

    szMeshError[0] = '\0';
    eResult = ed_reference_mesh_set_current(pMesh, szMeshError,
                                            sizeof(szMeshError));
    if (eResult != ED_REFERENCE_MESH_OK) {
        editor_scene_set_error(szError, uiErrorCapacity,
                               "reference mesh replacement failed: %s%s%s",
                               ed_reference_mesh_result_name(eResult),
                               szMeshError[0] ? ": " : "", szMeshError);
        switch (eResult) {
            case ED_REFERENCE_MESH_OUT_OF_MEMORY:
                return ROLLER_ED_RESULT_OUT_OF_MEMORY;
            case ED_REFERENCE_MESH_INVALID_VERSION:
                return ROLLER_ED_RESULT_INVALID_VERSION;
            default:
                return ROLLER_ED_RESULT_INVALID_ARGUMENT;
        }
    }
    editor_scene_set_error(szError, uiErrorCapacity, "");
    return ROLLER_ED_RESULT_OK;
}

eRollerEdResult roller_ed_legacy_scene_advance_stunts(
    uint32_t uiTicks,
    char *szError,
    size_t uiErrorCapacity)
{
    for (uint32_t uiTick = 0; uiTick < uiTicks; ++uiTick)
        updatestunts();
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

    /* Overlay state is host input; the renderer reads it through its own
     * globals, so publish it once here rather than per surface. */
    drawtrk3_editor_apply_overlay_selection();

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
        /* After the scene, so the helpers sit on top of the surfaces they
         * describe and inherit the transform draw_road established. */
        drawtrk3_editor_draw_helpers(g_pGameRenderer);
        /* The test car goes in last: it stands on the helpers rather than
         * under them, and it is the only overlay that is a real model. */
        ed_test_car_draw(g_pGameRenderer);
        drawtrk3_editor_draw_reference_mesh(g_pGameRenderer);
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

    /* The game camera is authored in the legacy logical render space (320x200
     * or its 640x400 SVGA scale), while editor readback uses the QWidget's
     * device-pixel size. Preserve the game's focal length and horizon at any
     * physical target size instead of widening both FOV axes as the widget
     * grows. */
    game_render_set_projection_reference_height(
        g_pGameRenderer, YMAX > 0 ? YMAX : 200);
    game_render_set_viewport(g_pGameRenderer, 0, 0, (int)uiWidth,
                             (int)uiHeight);
    game_render_begin_frame(g_pGameRenderer);
    /* draw_road is the scene-only path: the gameplay panel/HUD compositor is
     * intentionally not part of editor readback. */
    draw_road(scrbuf, ViewType[0], (unsigned int)DriveView[0], 0, 0);
    /* After the scene, so the helpers sit on top of the surfaces they
     * describe and inherit the transform draw_road established. */
    drawtrk3_editor_draw_helpers(g_pGameRenderer);
    /* The test car goes in last: it stands on the helpers rather than under
     * them, and it is the only overlay that is a real model. */
    ed_test_car_draw(g_pGameRenderer);
    drawtrk3_editor_draw_reference_mesh(g_pGameRenderer);
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

/*
 * Materials are interned as the traversal runs, so the count is not known in
 * advance. Start small and grow; the ceiling only has to exceed what a track
 * can address, which is 256 tiles across three banks plus their non-textured
 * and paired variants.
 */
#define EDITOR_GEOMETRY_MIN_MATERIALS 256u
#define EDITOR_GEOMETRY_MAX_MATERIALS 16384u
#define EDITOR_GEOMETRY_INDICES_PER_QUAD 6u
/* Track chunks emit up to ~32 surfaces each; scenery adds at most
 * MAX_VISIBLE_BUILDINGS placed objects of at most 20 polygons apiece. */
#define EDITOR_GEOMETRY_MAX_SURFACES \
    (MAX_TRACK_CHUNKS * 32u + MAX_VISIBLE_BUILDINGS * 32u)

/* Both canonical producers, in the fixed order the probe and the fill must
 * agree on: track chunks first, then scenery. */
static bool editor_geometry_emit_all(tEdMaterialTable *pTable,
                                     tEdEmitSurfaceFn pfnEmit,
                                     void *pUserData)
{
    return drawtrk3_emit_full_track(pTable, pfnEmit, pUserData)
        && drawtrk3_emit_full_scenery(pTable, pfnEmit, pUserData);
}

typedef struct
{
    tEdGeometryExtract *pExtract;
    uint32_t uiPrimitiveIndex;
} tEdGeometryFillContext;

static bool editor_geometry_surface_usable(const tEdSurfaceEmission *pSurface)
{
    return pSurface
        && pSurface->uiVertexCount == ED_SURFACE_VERTEX_COUNT
        && pSurface->byTopology == ROLLER_ED_TOPOLOGY_QUAD;
}

static void editor_geometry_count_surface(const tEdSurfaceEmission *pSurface,
                                          void *pUserData)
{
    if (editor_geometry_surface_usable(pSurface))
        (*(uint32_t *)pUserData)++;
}

static void editor_geometry_fill_surface(const tEdSurfaceEmission *pSurface,
                                         void *pUserData)
{
    tEdGeometryFillContext *pContext = pUserData;
    tEdGeometryExtract *pExtract = pContext->pExtract;
    uint32_t uiPrimitive = pContext->uiPrimitiveIndex;
    uint32_t uiBaseVertex;
    uint32_t uiBaseIndex;
    tEdPrimitive *pPrimitive;

    if (!editor_geometry_surface_usable(pSurface)
            || uiPrimitive >= pExtract->uiPrimitiveCount)
        return;
    pContext->uiPrimitiveIndex++;
    uiBaseVertex = uiPrimitive * ED_SURFACE_VERTEX_COUNT;
    uiBaseIndex = uiPrimitive * EDITOR_GEOMETRY_INDICES_PER_QUAD;

    for (uint32_t i = 0; i < ED_SURFACE_VERTEX_COUNT; i++) {
        tEdVertex *pVertex = &pExtract->pVertices[uiBaseVertex + i];
        const tEdSurfaceVertex *pSource = &pSurface->aVertices[i];

        memcpy(pVertex->fPosition, pSource->fPosition,
               sizeof(pVertex->fPosition));
        memcpy(pVertex->fNormal, pSource->fNormal, sizeof(pVertex->fNormal));
        /* Material-local UV (AD-7b); the exporter resolves it through the
         * selected material's atlas transform. */
        memcpy(pVertex->fUV, pSource->fMaterialUV, sizeof(pVertex->fUV));
    }

    /* Both triangles keep the emitter's v0..v3 winding, so the front face
     * survives triangulation (docs/adr/0003-canonical-geometry-conventions). */
    pExtract->puiIndices[uiBaseIndex + 0u] = uiBaseVertex + 0u;
    pExtract->puiIndices[uiBaseIndex + 1u] = uiBaseVertex + 1u;
    pExtract->puiIndices[uiBaseIndex + 2u] = uiBaseVertex + 2u;
    pExtract->puiIndices[uiBaseIndex + 3u] = uiBaseVertex + 0u;
    pExtract->puiIndices[uiBaseIndex + 4u] = uiBaseVertex + 2u;
    pExtract->puiIndices[uiBaseIndex + 5u] = uiBaseVertex + 3u;

    pPrimitive = &pExtract->pPrimitives[uiPrimitive];
    memset(pPrimitive, 0, sizeof(*pPrimitive));
    pPrimitive->uiFirstIndex = uiBaseIndex;
    pPrimitive->uiIndexCount = EDITOR_GEOMETRY_INDICES_PER_QUAD;
    pPrimitive->uiChunkId = pSurface->uiChunkId;
    pPrimitive->uiFrontMaterialId = pSurface->uiFrontMaterialId;
    pPrimitive->uiBackMaterialId = pSurface->uiBackMaterialId;
    pPrimitive->unSurfaceClass = pSurface->unSurfaceClass;
    pPrimitive->unContentClass = pSurface->unContentClass;
    /* The internal and public flag sets are separate vocabularies. */
    if (pSurface->unFlags & ROLLER_ED_SURFACE_FLAG_ALPHA)
        pPrimitive->unFlags |= (uint16_t)ROLLER_ED_PRIMITIVE_FLAG_ALPHA_BLEND;
    if (pSurface->unFlags & ROLLER_ED_SURFACE_FLAG_TWO_SIDED)
        pPrimitive->unFlags |= (uint16_t)ROLLER_ED_PRIMITIVE_FLAG_TWO_SIDED;
    pPrimitive->byTopology = ROLLER_ED_TOPOLOGY_TRIANGLE_LIST;
}

/* Counts surfaces and materials, growing the material table until the whole
 * track interns. Returns false if the traversal fails for any other reason. */
static bool editor_geometry_probe(uint32_t *puiSurfaceCount,
                                  uint32_t *puiMaterialCount,
                                  char *szError,
                                  size_t uiErrorCapacity)
{
    uint32_t uiCapacity = EDITOR_GEOMETRY_MIN_MATERIALS;
    tEdMaterial *pMaterials = NULL;

    for (;;) {
        tEdMaterialTable Table;
        tEdMaterial *pGrown =
            realloc(pMaterials, (size_t)uiCapacity * sizeof(*pGrown));
        uint32_t uiSurfaceCount = 0u;

        if (!pGrown) {
            free(pMaterials);
            editor_scene_set_error(szError, uiErrorCapacity,
                                   "out of memory sizing track geometry");
            return false;
        }
        pMaterials = pGrown;
        if (!drawtrk3_init_editor_material_table(
                &Table, pMaterials, uiCapacity)) {
            free(pMaterials);
            editor_scene_set_error(szError, uiErrorCapacity,
                                   "editor material table is unavailable");
            return false;
        }
        if (editor_geometry_emit_all(
                &Table, editor_geometry_count_surface, &uiSurfaceCount)) {
            *puiSurfaceCount = uiSurfaceCount;
            *puiMaterialCount = Table.uiCount;
            free(pMaterials);
            return true;
        }
        /* A full table is the one failure worth retrying. */
        if (Table.uiCount < Table.uiCapacity
                || uiCapacity >= EDITOR_GEOMETRY_MAX_MATERIALS) {
            free(pMaterials);
            editor_scene_set_error(
                szError, uiErrorCapacity,
                "canonical traversal could not emit the loaded track");
            return false;
        }
        uiCapacity *= 2u;
    }
}

eRollerEdResult roller_ed_legacy_scene_extract_geometry(
    tEdGeometryExtract *pExtract,
    char *szError,
    size_t uiErrorCapacity)
{
    tEdMaterialTable Table;
    tEdGeometryFillContext Fill;
    uint32_t uiSurfaceCount = 0u;
    uint32_t uiMaterialCount = 0u;

    if (!pExtract) {
        editor_scene_set_error(szError, uiErrorCapacity,
                               "geometry extraction requires an output");
        return ROLLER_ED_RESULT_INVALID_ARGUMENT;
    }
    memset(pExtract, 0, sizeof(*pExtract));

    if (!editor_geometry_probe(&uiSurfaceCount, &uiMaterialCount,
                               szError, uiErrorCapacity))
        return ROLLER_ED_RESULT_INTERNAL_ERROR;
    if (uiSurfaceCount == 0u || uiMaterialCount == 0u) {
        /* Nothing to publish; the caller still sees a READY empty extract. */
        return ROLLER_ED_RESULT_OK;
    }
    if (uiSurfaceCount > EDITOR_GEOMETRY_MAX_SURFACES) {
        editor_scene_set_error(szError, uiErrorCapacity,
                               "loaded track emitted %u surfaces, above the "
                               "%u the geometry API supports",
                               uiSurfaceCount,
                               (uint32_t)EDITOR_GEOMETRY_MAX_SURFACES);
        return ROLLER_ED_RESULT_INTERNAL_ERROR;
    }

    pExtract->uiPrimitiveCount = uiSurfaceCount;
    pExtract->uiVertexCount = uiSurfaceCount * ED_SURFACE_VERTEX_COUNT;
    pExtract->uiIndexCount = uiSurfaceCount * EDITOR_GEOMETRY_INDICES_PER_QUAD;
    pExtract->uiMaterialCount = uiMaterialCount;
    pExtract->pVertices = calloc(pExtract->uiVertexCount,
                                 sizeof(*pExtract->pVertices));
    pExtract->puiIndices = calloc(pExtract->uiIndexCount,
                                  sizeof(*pExtract->puiIndices));
    pExtract->pPrimitives = calloc(pExtract->uiPrimitiveCount,
                                   sizeof(*pExtract->pPrimitives));
    pExtract->pMaterials = calloc(pExtract->uiMaterialCount,
                                  sizeof(*pExtract->pMaterials));
    if (!pExtract->pVertices || !pExtract->puiIndices
            || !pExtract->pPrimitives || !pExtract->pMaterials) {
        roller_ed_legacy_scene_release_geometry(pExtract);
        editor_scene_set_error(szError, uiErrorCapacity,
                               "out of memory extracting track geometry");
        return ROLLER_ED_RESULT_OUT_OF_MEMORY;
    }

    /* The output material array is the interning table, so the ids the
     * primitives carry index it directly. */
    if (!drawtrk3_init_editor_material_table(
            &Table, pExtract->pMaterials, uiMaterialCount)) {
        roller_ed_legacy_scene_release_geometry(pExtract);
        editor_scene_set_error(szError, uiErrorCapacity,
                               "editor material table is unavailable");
        return ROLLER_ED_RESULT_INTERNAL_ERROR;
    }
    Fill.pExtract = pExtract;
    Fill.uiPrimitiveIndex = 0u;
    if (!editor_geometry_emit_all(&Table, editor_geometry_fill_surface, &Fill)
            || Fill.uiPrimitiveIndex != uiSurfaceCount
            || Table.uiCount != uiMaterialCount) {
        /* The traversal is camera-independent and deterministic by
         * construction, so a second pass that disagrees with the first is a
         * defect rather than a condition to paper over. */
        roller_ed_legacy_scene_release_geometry(pExtract);
        editor_scene_set_error(szError, uiErrorCapacity,
                               "canonical traversal was not deterministic");
        return ROLLER_ED_RESULT_INTERNAL_ERROR;
    }
    return ROLLER_ED_RESULT_OK;
}

void roller_ed_legacy_scene_release_geometry(tEdGeometryExtract *pExtract)
{
    if (!pExtract)
        return;
    free(pExtract->pVertices);
    free(pExtract->puiIndices);
    free(pExtract->pPrimitives);
    free(pExtract->pMaterials);
    memset(pExtract, 0, sizeof(*pExtract));
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
    roller_ed_overlay_reset();
    /* Before the renderer goes: the prepared design is only meaningful while
     * the texture bank it registered still exists. */
    ed_test_car_reset();
    ed_reference_mesh_reset_current();
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
    s_bEditorCloudsAttempted = 0;
    s_bEditorCloudsReady = 0;
    s_szEditorCloudAsset[0] = '\0';
    s_eActiveRenderer = 0;
}
