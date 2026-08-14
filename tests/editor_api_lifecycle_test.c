#include "editor_api.h"
#include "editor_legacy_scene.h"
#include "editor_track_loader.h"

#define SDL_MAIN_HANDLED 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    SDL_Semaphore *pReady;
    SDL_Semaphore *pContinue;
    const char *szValidTrack;
    const char *szMalformedTrack;
    const char *szLargeTrack;
    int iFailureLine;
} tLifecycleTestContext;

static int s_iThreadAssertionCount;
static int s_iLegacyInstallCount;
static int s_iLegacyRenderCount;
static int s_iLegacySetCameraCount;
static int s_iLegacySetOverlayCount;
static int s_iLegacySetGraphicsCount;
static uint32_t s_uiLegacyStuntTicks;
static eRollerEdRenderer s_eLastPreferredRenderer;
static uint32_t s_uiLastAllowSoftwareFallback;
static uint32_t s_uiStubAvailableRenderers =
    ROLLER_ED_RENDERER_SOFTWARE | ROLLER_ED_RENDERER_GPU;
static eRollerEdRenderer s_eStubActiveRenderer;
static tEdCameraState s_LastLegacyCamera;
static tEdOverlayState s_LastLegacyOverlay;
static tEdGraphicsSettings s_LastLegacyGraphics;
static uint32_t s_uiStubQuadCount;
static int s_iStubExtractCount;

uint32_t roller_ed_legacy_scene_tower_count(void)
{
    return 2u;
}

void roller_ed_legacy_scene_query_tower(
    uint32_t uiTowerIndex, tEdTowerInfo *pInfoOut)
{
    pInfoOut->uiChunkId = 40u + uiTowerIndex;
    pInfoOut->fWorldPosition[0] = (float)uiTowerIndex + 0.25f;
    pInfoOut->fWorldPosition[1] = (float)uiTowerIndex + 1.25f;
    pInfoOut->fWorldPosition[2] = (float)uiTowerIndex + 2.25f;
    pInfoOut->fAnchorPosition[0] = (float)uiTowerIndex - 10.0f;
    pInfoOut->fAnchorPosition[1] = (float)uiTowerIndex - 20.0f;
    pInfoOut->fAnchorPosition[2] = (float)uiTowerIndex - 30.0f;
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
    if (!szTrackPath || !pStage || !pStage->pbyData
            || !szDocumentAssetRoot || !szFallbackAssetRoot) {
        snprintf(szError, uiErrorCapacity, "invalid legacy install seam input");
        return ROLLER_ED_RESULT_INVALID_ARGUMENT;
    }
    s_eLastPreferredRenderer = ePreferredRenderer;
    s_uiLastAllowSoftwareFallback = uiAllowSoftwareFallback;
    s_eStubActiveRenderer = ePreferredRenderer;
    /* One synthetic quad per staged chunk, so a larger track really does
     * produce a larger extract and the stale path has something to protect. */
    s_uiStubQuadCount = pStage->uiChunkCount;
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

eRollerEdResult roller_ed_legacy_scene_set_overlay_state(
    const tEdOverlayState *pState,
    char *szError,
    size_t uiErrorCapacity)
{
    if (!pState) {
        snprintf(szError, uiErrorCapacity, "overlay state is required");
        return ROLLER_ED_RESULT_INVALID_ARGUMENT;
    }
    s_LastLegacyOverlay = *pState;
    s_iLegacySetOverlayCount++;
    if (uiErrorCapacity)
        szError[0] = '\0';
    return ROLLER_ED_RESULT_OK;
}

eRollerEdResult roller_ed_legacy_scene_advance_stunts(
    uint32_t uiTicks,
    char *szError,
    size_t uiErrorCapacity)
{
    s_uiLegacyStuntTicks += uiTicks;
    if (uiErrorCapacity)
        szError[0] = '\0';
    return ROLLER_ED_RESULT_OK;
}

/*
 * E3A-S7. Stands in for the copy layer: it records what the facade forwarded
 * and refuses a mesh whose vertex count is odd, so the lifecycle test can
 * check that a refusal is reported without linking the real allocator.
 */
static int s_iLegacySetReferenceMeshCount;
static tEdReferenceMesh s_LastLegacyReferenceMesh;

eRollerEdResult roller_ed_legacy_scene_set_reference_mesh(
    const tEdReferenceMesh *pMesh,
    char *szError,
    size_t uiErrorCapacity)
{
    if (!pMesh) {
        snprintf(szError, uiErrorCapacity, "reference mesh is required");
        return ROLLER_ED_RESULT_INVALID_ARGUMENT;
    }
    if (pMesh->uiVertexCount == 1u) {
        snprintf(szError, uiErrorCapacity, "stub refuses a one-vertex mesh");
        return ROLLER_ED_RESULT_INVALID_ARGUMENT;
    }
    s_LastLegacyReferenceMesh = *pMesh;
    s_iLegacySetReferenceMeshCount++;
    if (uiErrorCapacity)
        szError[0] = '\0';
    return ROLLER_ED_RESULT_OK;
}

uint32_t roller_ed_legacy_scene_get_available_renderers(void)
{
    return s_uiStubAvailableRenderers;
}

eRollerEdResult roller_ed_legacy_scene_select_renderer(
    eRollerEdRenderer eKind,
    char *szError,
    size_t uiErrorCapacity)
{
    if ((s_uiStubAvailableRenderers & eKind) == 0u) {
        snprintf(szError, uiErrorCapacity, "renderer unavailable in test seam");
        return ROLLER_ED_RESULT_RENDERER_UNAVAILABLE;
    }
    s_eStubActiveRenderer = eKind;
    if (uiErrorCapacity)
        szError[0] = '\0';
    return ROLLER_ED_RESULT_OK;
}

eRollerEdResult roller_ed_legacy_scene_set_graphics_settings(
    const tEdGraphicsSettings *pSettings,
    char *szError,
    size_t uiErrorCapacity)
{
    s_LastLegacyGraphics = *pSettings;
    s_eStubActiveRenderer = pSettings->eRenderer;
    ++s_iLegacySetGraphicsCount;
    if (uiErrorCapacity)
        szError[0] = '\0';
    return ROLLER_ED_RESULT_OK;
}

/*
 * Stands in for the canonical traversal: one quad per staged chunk, with
 * recognisable contents so the facade's copy-out can be checked field by
 * field without linking the renderer.
 */
eRollerEdResult roller_ed_legacy_scene_extract_geometry(
    tEdGeometryExtract *pExtract,
    char *szError,
    size_t uiErrorCapacity)
{
    uint32_t uiQuads = s_uiStubQuadCount;

    if (!pExtract) {
        snprintf(szError, uiErrorCapacity, "extract output is required");
        return ROLLER_ED_RESULT_INVALID_ARGUMENT;
    }
    memset(pExtract, 0, sizeof(*pExtract));
    s_iStubExtractCount++;
    if (uiErrorCapacity)
        szError[0] = '\0';
    if (uiQuads == 0u)
        return ROLLER_ED_RESULT_OK;

    pExtract->uiPrimitiveCount = uiQuads;
    pExtract->uiVertexCount = uiQuads * 4u;
    pExtract->uiIndexCount = uiQuads * 6u;
    pExtract->uiMaterialCount = 1u;
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
        snprintf(szError, uiErrorCapacity, "stub extraction ran out of memory");
        return ROLLER_ED_RESULT_OUT_OF_MEMORY;
    }

    for (uint32_t uiQuad = 0u; uiQuad < uiQuads; uiQuad++) {
        uint32_t uiBaseVertex = uiQuad * 4u;
        uint32_t uiBaseIndex = uiQuad * 6u;
        static const uint32_t auiCorner[6] = { 0u, 1u, 2u, 0u, 2u, 3u };

        for (uint32_t i = 0u; i < 4u; i++) {
            tEdVertex *pVertex = &pExtract->pVertices[uiBaseVertex + i];
            pVertex->fPosition[0] = (float)uiQuad;
            pVertex->fPosition[1] = (float)i;
            pVertex->fNormal[2] = 1.0f;
            pVertex->fUV[0] = (i == 1u || i == 2u) ? 0.0f : 1.0f;
        }
        for (uint32_t i = 0u; i < 6u; i++)
            pExtract->puiIndices[uiBaseIndex + i] = uiBaseVertex + auiCorner[i];
        pExtract->pPrimitives[uiQuad].uiFirstIndex = uiBaseIndex;
        pExtract->pPrimitives[uiQuad].uiIndexCount = 6u;
        pExtract->pPrimitives[uiQuad].uiChunkId = uiQuad;
        pExtract->pPrimitives[uiQuad].uiBackMaterialId =
            ROLLER_ED_INVALID_MATERIAL_ID;
        pExtract->pPrimitives[uiQuad].unContentClass =
            ROLLER_ED_CONTENT_AUTHORED_TRACK;
        pExtract->pPrimitives[uiQuad].byTopology =
            ROLLER_ED_TOPOLOGY_TRIANGLE_LIST;
    }
    pExtract->pMaterials[0].uiKind = ROLLER_ED_MATERIAL_TEXTURED_TILE;
    pExtract->pMaterials[0].fAtlasScale[0] = 0.25f;
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
    CHECK_WORKER(RollerEd_GetAvailableRenderers()
                 == (ROLLER_ED_RENDERER_SOFTWARE | ROLLER_ED_RENDERER_GPU));
    CHECK_WORKER(RollerEd_QueryGeometrySizes(&Sizes) == ROLLER_ED_RESULT_OK);
    CHECK_WORKER(Sizes.uiSceneState == ROLLER_ED_SCENE_EMPTY);
    CHECK_WORKER(Sizes.uiVertexCount == 0u && Sizes.uiIndexCount == 0u
                 && Sizes.uiPrimitiveCount == 0u && Sizes.uiMaterialCount == 0u);
    CHECK_WORKER(Sizes.uiVertexStride == sizeof(tEdVertex));
    CHECK_WORKER(Sizes.uiPrimitiveStride == sizeof(tEdPrimitive));
    CHECK_WORKER(Sizes.uiMaterialStride == sizeof(tEdMaterial));
    uiInitialEpoch = Sizes.uiGeometryEpoch;
    uiInitialGeneration = Sizes.uiTrackGeneration;
    {
        uint32_t uiTowerCount = 0x87654321u;
        tEdTowerInfo TowerInfo;
        tEdTowerInfo Before;

        memset(&TowerInfo, 0xa5, sizeof(TowerInfo));
        TowerInfo.uiStructSize = sizeof(TowerInfo);
        TowerInfo.uiVersion = ROLLER_ED_TOWER_INFO_VERSION;
        Before = TowerInfo;
        CHECK_WORKER(RollerEd_QueryTowerCount(&uiTowerCount)
                     == ROLLER_ED_RESULT_NO_SCENE);
        CHECK_WORKER(uiTowerCount == 0x87654321u);
        CHECK_WORKER(RollerEd_QueryTower(0u, &TowerInfo)
                     == ROLLER_ED_RESULT_NO_SCENE);
        CHECK_WORKER(memcmp(&TowerInfo, &Before, sizeof(Before)) == 0);
        CHECK_WORKER(RollerEd_QueryTowerCount(NULL)
                     == ROLLER_ED_RESULT_INVALID_ARGUMENT);
        CHECK_WORKER(RollerEd_QueryTower(0u, NULL)
                     == ROLLER_ED_RESULT_INVALID_ARGUMENT);

        TowerInfo.uiVersion = ROLLER_ED_TOWER_INFO_VERSION + 1u;
        Before = TowerInfo;
        CHECK_WORKER(RollerEd_QueryTower(0u, &TowerInfo)
                     == ROLLER_ED_RESULT_INVALID_VERSION);
        CHECK_WORKER(memcmp(&TowerInfo, &Before, sizeof(Before)) == 0);
        TowerInfo.uiVersion = ROLLER_ED_TOWER_INFO_VERSION;
        TowerInfo.uiStructSize = sizeof(TowerInfo) - 1u;
        Before = TowerInfo;
        CHECK_WORKER(RollerEd_QueryTower(0u, &TowerInfo)
                     == ROLLER_ED_RESULT_INVALID_ARGUMENT);
        CHECK_WORKER(memcmp(&TowerInfo, &Before, sizeof(Before)) == 0);
    }

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
        /* E3A-S1: overlay state is settable before any scene exists, exactly
         * like the camera, and a reversed selection range reaches the core
         * verbatim rather than being normalized at the boundary. */
        tEdOverlayState Overlay = {
            .uiStructSize = sizeof(Overlay),
            .uiVersion = ROLLER_ED_OVERLAY_STATE_VERSION,
            .uiFlags = ROLLER_ED_OVERLAY_SHOW_SURFACES
                | ROLLER_ED_OVERLAY_SHOW_WIREFRAME
                | ROLLER_ED_OVERLAY_HIGHLIGHT_SELECTION
                | ROLLER_ED_OVERLAY_SHOW_STUNT_MARKERS
                | ROLLER_ED_OVERLAY_SHOW_TOWER_MARKERS,
            .uiFirstSelectedChunk = 31u,
            .uiLastSelectedChunk = 12u,
            .uiSurfaceClassMask = ROLLER_ED_OVERLAY_ALL_SURFACE_CLASSES,
            .uiWireframeClassMask =
                ROLLER_ED_OVERLAY_CLASS_BIT(ROLLER_ED_SURFACE_CLASS_ROOF)
        };
        tEdOverlayState InvalidOverlay = Overlay;

        CHECK_WORKER(RollerEd_SetOverlayState(&Overlay)
                     == ROLLER_ED_RESULT_OK);
        CHECK_WORKER(s_iLegacySetOverlayCount == 1);
        CHECK_WORKER(memcmp(&s_LastLegacyOverlay, &Overlay, sizeof(Overlay))
                     == 0);
        Sizes.uiStructSize = sizeof(Sizes);
        Sizes.uiVersion = ROLLER_ED_GEOMETRY_SIZES_VERSION;
        CHECK_WORKER(RollerEd_QueryGeometrySizes(&Sizes)
                     == ROLLER_ED_RESULT_OK);
        CHECK_WORKER(Sizes.uiGeometryEpoch == uiInitialEpoch);
        CHECK_WORKER(Sizes.uiTrackGeneration == uiInitialGeneration);
        CHECK_WORKER(Sizes.uiSceneState == ROLLER_ED_SCENE_EMPTY);

        CHECK_WORKER(RollerEd_SetOverlayState(NULL)
                     == ROLLER_ED_RESULT_INVALID_ARGUMENT);
        InvalidOverlay.uiVersion++;
        CHECK_WORKER(RollerEd_SetOverlayState(&InvalidOverlay)
                     == ROLLER_ED_RESULT_INVALID_VERSION);
        /* E3A-S2 bumped only this struct's version. A host still sending the
         * v1 overlay state has no class masks, so it is refused rather than
         * silently given whatever its shorter allocation happened to hold. */
        InvalidOverlay = Overlay;
        InvalidOverlay.uiVersion = 1u;
        CHECK_WORKER(RollerEd_SetOverlayState(&InvalidOverlay)
                     == ROLLER_ED_RESULT_INVALID_VERSION);
        /* Every other struct kept version 1 through that bump. */
        CHECK_WORKER(ROLLER_ED_CAMERA_STATE_VERSION == 1u);
        CHECK_WORKER(ROLLER_ED_GEOMETRY_SIZES_VERSION == 1u);
        CHECK_WORKER(ROLLER_ED_OVERLAY_STATE_VERSION == 3u);
        /* A class mask past the last defined surface class is refused whole,
         * exactly like an undefined flag bit. */
        InvalidOverlay = Overlay;
        InvalidOverlay.uiSurfaceClassMask =
            ROLLER_ED_OVERLAY_CLASS_BIT(ROLLER_ED_SURFACE_CLASS_COUNT);
        CHECK_WORKER(RollerEd_SetOverlayState(&InvalidOverlay)
                     == ROLLER_ED_RESULT_INVALID_ARGUMENT);
        InvalidOverlay = Overlay;
        InvalidOverlay.uiWireframeClassMask = 0xffffffffu;
        CHECK_WORKER(RollerEd_SetOverlayState(&InvalidOverlay)
                     == ROLLER_ED_RESULT_INVALID_ARGUMENT);
        CHECK_WORKER(strstr(RollerEd_GetLastError(), "class") != NULL);
        InvalidOverlay = Overlay;
        InvalidOverlay.uiStructSize = sizeof(InvalidOverlay) - 1u;
        CHECK_WORKER(RollerEd_SetOverlayState(&InvalidOverlay)
                     == ROLLER_ED_RESULT_INVALID_ARGUMENT);
        /* A bit this API version does not define is refused whole rather than
         * quietly dropped, so the host never believes it enabled something. */
        InvalidOverlay = Overlay;
        InvalidOverlay.uiFlags |= 1u << 14;
        CHECK_WORKER(RollerEd_SetOverlayState(&InvalidOverlay)
                     == ROLLER_ED_RESULT_INVALID_ARGUMENT);
        CHECK_WORKER(strstr(RollerEd_GetLastError(), "uiFlags") != NULL);
        /* E3A-S6: the test-car selection indexes fixed tables, so it is
         * range-checked on the way in whether or not the car is switched on --
         * failing later would report against the wrong call. */
        InvalidOverlay = Overlay;
        InvalidOverlay.uiTestCarDesign = ROLLER_ED_TEST_CAR_DESIGN_COUNT;
        CHECK_WORKER(RollerEd_SetOverlayState(&InvalidOverlay)
                     == ROLLER_ED_RESULT_INVALID_ARGUMENT);
        CHECK_WORKER(strstr(RollerEd_GetLastError(), "uiTestCarDesign") != NULL);
        InvalidOverlay = Overlay;
        InvalidOverlay.uiTestCarAiLine = ROLLER_ED_TEST_CAR_AI_LINE_COUNT;
        CHECK_WORKER(RollerEd_SetOverlayState(&InvalidOverlay)
                     == ROLLER_ED_RESULT_INVALID_ARGUMENT);
        CHECK_WORKER(strstr(RollerEd_GetLastError(), "uiTestCarAiLine") != NULL);
        CHECK_WORKER(s_iLegacySetOverlayCount == 1);
        CHECK_WORKER(memcmp(&s_LastLegacyOverlay, &Overlay, sizeof(Overlay))
                     == 0);
    }
    {
        /*
         * E3A-S7. RollerEd_SetReferenceMesh reached the seam at last: it used
         * to validate the header and then return UNSUPPORTED.
         */
        static tEdReferenceVertex aVertices[3];
        tEdReferenceMesh Mesh;
        tEdReferenceMesh InvalidMesh;
        tEdGeometrySizes Before;
        tEdGeometrySizes After;

        memset(aVertices, 0, sizeof(aVertices));
        aVertices[1].fPosition[0] = 1.0f;
        aVertices[2].fPosition[1] = 1.0f;
        memset(&Mesh, 0, sizeof(Mesh));
        Mesh.uiStructSize = sizeof(Mesh);
        Mesh.uiVersion = ROLLER_ED_REFERENCE_MESH_VERSION;
        Mesh.pVertices = aVertices;
        Mesh.uiVertexCount = 3u;
        Mesh.fScale[0] = 1.0f;
        Mesh.fScale[1] = 1.0f;
        Mesh.fScale[2] = 1.0f;

        Before.uiStructSize = sizeof(Before);
        Before.uiVersion = ROLLER_ED_GEOMETRY_SIZES_VERSION;
        CHECK_WORKER(RollerEd_QueryGeometrySizes(&Before)
                     == ROLLER_ED_RESULT_OK);

        CHECK_WORKER(RollerEd_SetReferenceMesh(NULL)
                     == ROLLER_ED_RESULT_INVALID_ARGUMENT);
        InvalidMesh = Mesh;
        InvalidMesh.uiVersion = ROLLER_ED_REFERENCE_MESH_VERSION + 1u;
        CHECK_WORKER(RollerEd_SetReferenceMesh(&InvalidMesh)
                     == ROLLER_ED_RESULT_INVALID_VERSION);
        InvalidMesh = Mesh;
        InvalidMesh.uiStructSize = sizeof(InvalidMesh) - 1u;
        CHECK_WORKER(RollerEd_SetReferenceMesh(&InvalidMesh)
                     == ROLLER_ED_RESULT_INVALID_ARGUMENT);
        CHECK_WORKER(s_iLegacySetReferenceMeshCount == 0);

        CHECK_WORKER(RollerEd_SetReferenceMesh(&Mesh) == ROLLER_ED_RESULT_OK);
        CHECK_WORKER(s_iLegacySetReferenceMeshCount == 1);
        CHECK_WORKER(s_LastLegacyReferenceMesh.uiVertexCount == 3u);

        /* A seam refusal is reported and does not count as a replacement. */
        InvalidMesh = Mesh;
        InvalidMesh.uiVertexCount = 1u;
        CHECK_WORKER(RollerEd_SetReferenceMesh(&InvalidMesh)
                     == ROLLER_ED_RESULT_INVALID_ARGUMENT);
        CHECK_WORKER(s_iLegacySetReferenceMeshCount == 1);

        /* AD-7d: a reference mesh is the host's scenery, not authored track
         * geometry, so it moves neither counter. */
        After.uiStructSize = sizeof(After);
        After.uiVersion = ROLLER_ED_GEOMETRY_SIZES_VERSION;
        CHECK_WORKER(RollerEd_QueryGeometrySizes(&After)
                     == ROLLER_ED_RESULT_OK);
        CHECK_WORKER(After.uiGeometryEpoch == Before.uiGeometryEpoch);
        CHECK_WORKER(After.uiTrackGeneration == Before.uiTrackGeneration);
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
        CHECK_WORKER(RollerEd_AdvanceStunts(1u)
                     == ROLLER_ED_RESULT_NO_SCENE);
        CHECK_WORKER(s_uiLegacyStuntTicks == 0u);
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
            uint32_t uiTowerCount = 0u;
            tEdTowerInfo TowerInfo = {
                .uiStructSize = sizeof(TowerInfo),
                .uiVersion = ROLLER_ED_TOWER_INFO_VERSION
            };
            tEdTowerInfo Before;

            CHECK_WORKER(RollerEd_QueryTowerCount(&uiTowerCount)
                         == ROLLER_ED_RESULT_OK);
            CHECK_WORKER(uiTowerCount == 2u);
            CHECK_WORKER(RollerEd_QueryTower(1u, &TowerInfo)
                         == ROLLER_ED_RESULT_OK);
            CHECK_WORKER(TowerInfo.uiStructSize == sizeof(TowerInfo));
            CHECK_WORKER(TowerInfo.uiVersion
                         == ROLLER_ED_TOWER_INFO_VERSION);
            CHECK_WORKER(TowerInfo.uiChunkId == 41u);
            CHECK_WORKER(TowerInfo.fWorldPosition[0] == 1.25f
                         && TowerInfo.fWorldPosition[1] == 2.25f
                         && TowerInfo.fWorldPosition[2] == 3.25f);
            CHECK_WORKER(TowerInfo.fAnchorPosition[0] == -9.0f
                         && TowerInfo.fAnchorPosition[1] == -19.0f
                         && TowerInfo.fAnchorPosition[2] == -29.0f);

            memset(&TowerInfo, 0x5a, sizeof(TowerInfo));
            TowerInfo.uiStructSize = sizeof(TowerInfo);
            TowerInfo.uiVersion = ROLLER_ED_TOWER_INFO_VERSION;
            Before = TowerInfo;
            CHECK_WORKER(RollerEd_QueryTower(uiTowerCount, &TowerInfo)
                         == ROLLER_ED_RESULT_INVALID_ARGUMENT);
            CHECK_WORKER(memcmp(&TowerInfo, &Before, sizeof(Before)) == 0);
        }
        {
            const uint32_t uiEpochBeforeStunts = Sizes.uiGeometryEpoch;
            const uint32_t uiGenerationBeforeStunts =
                Sizes.uiTrackGeneration;

            CHECK_WORKER(RollerEd_AdvanceStunts(0u)
                         == ROLLER_ED_RESULT_OK);
            CHECK_WORKER(s_uiLegacyStuntTicks == 0u);
            CHECK_WORKER(RollerEd_AdvanceStunts(
                             ROLLER_ED_MAX_STUNT_TICKS_PER_CALL + 1u)
                         == ROLLER_ED_RESULT_INVALID_ARGUMENT);
            CHECK_WORKER(s_uiLegacyStuntTicks == 0u);
            CHECK_WORKER(RollerEd_AdvanceStunts(3u)
                         == ROLLER_ED_RESULT_OK);
            CHECK_WORKER(s_uiLegacyStuntTicks == 3u);
            Sizes.uiStructSize = sizeof(Sizes);
            Sizes.uiVersion = ROLLER_ED_GEOMETRY_SIZES_VERSION;
            CHECK_WORKER(RollerEd_QueryGeometrySizes(&Sizes)
                         == ROLLER_ED_RESULT_OK);
            CHECK_WORKER(Sizes.uiGeometryEpoch == uiEpochBeforeStunts);
            CHECK_WORKER(Sizes.uiTrackGeneration
                         == uiGenerationBeforeStunts);
        }
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

        /* The one-chunk fixture stages one quad. */
        CHECK_WORKER(Sizes.uiVertexCount == 4u);
        CHECK_WORKER(Sizes.uiIndexCount == 6u);
        CHECK_WORKER(Sizes.uiPrimitiveCount == 1u);
        CHECK_WORKER(Sizes.uiMaterialCount == 1u);
        CHECK_WORKER(Sizes.uiVertexStride == sizeof(tEdVertex));
        CHECK_WORKER(Sizes.uiPrimitiveStride == sizeof(tEdPrimitive));
        CHECK_WORKER(Sizes.uiMaterialStride == sizeof(tEdMaterial));
        {
            /* AD-7d with a scene loaded: toggling an overlay must not advance
             * the geometry epoch, and must not drop the per-epoch extraction
             * E4A-S5 caches -- an overlay toggle that forced re-extraction
             * would defeat the cache on every menu click. */
            int iExtractCount = s_iStubExtractCount;
            tEdOverlayState Overlay = {
                .uiStructSize = sizeof(Overlay),
                .uiVersion = ROLLER_ED_OVERLAY_STATE_VERSION,
                .uiFlags = ROLLER_ED_OVERLAY_SHOW_SURFACES
                    | ROLLER_ED_OVERLAY_SHOW_WIREFRAME,
                .uiFirstSelectedChunk = ROLLER_ED_INVALID_CHUNK_ID,
                .uiLastSelectedChunk = ROLLER_ED_INVALID_CHUNK_ID,
                .uiSurfaceClassMask =
                    ROLLER_ED_OVERLAY_CLASS_BIT(
                        ROLLER_ED_SURFACE_CLASS_CENTER),
                .uiWireframeClassMask = ROLLER_ED_OVERLAY_ALL_SURFACE_CLASSES
            };

            CHECK_WORKER(RollerEd_SetOverlayState(&Overlay)
                         == ROLLER_ED_RESULT_OK);
            CHECK_WORKER(s_iLegacySetOverlayCount == 2);
            Sizes.uiStructSize = sizeof(Sizes);
            Sizes.uiVersion = ROLLER_ED_GEOMETRY_SIZES_VERSION;
            CHECK_WORKER(RollerEd_QueryGeometrySizes(&Sizes)
                         == ROLLER_ED_RESULT_OK);
            CHECK_WORKER(Sizes.uiGeometryEpoch == uiReadyEpoch);
            CHECK_WORKER(Sizes.uiTrackGeneration == uiReadyGeneration);
            CHECK_WORKER(Sizes.uiPrimitiveCount == 1u);
            CHECK_WORKER(s_iStubExtractCount == iExtractCount);
        }
        {
            tEdVertex aVertices[4];
            uint32_t auiIndices[6];
            tEdPrimitive Primitive;
            tEdMaterial Material;
            tEdVertex aBeforeVertices[4];

            memset(aVertices, 0x11, sizeof(aVertices));
            memcpy(aBeforeVertices, aVertices, sizeof(aBeforeVertices));

            /* Every capacity is validated before any buffer is touched, so a
             * caller that under-sizes one array keeps all of them intact. */
            CHECK_WORKER(RollerEd_FillGeometry(
                             Sizes.uiGeometryEpoch, aVertices, 3u,
                             auiIndices, 6u, &Primitive, 1u, &Material, 1u)
                         == ROLLER_ED_RESULT_BUFFER_TOO_SMALL);
            CHECK_WORKER(memcmp(aVertices, aBeforeVertices,
                                sizeof(aBeforeVertices)) == 0);
            CHECK_WORKER(RollerEd_FillGeometry(
                             Sizes.uiGeometryEpoch, aVertices, 4u,
                             auiIndices, 5u, &Primitive, 1u, &Material, 1u)
                         == ROLLER_ED_RESULT_BUFFER_TOO_SMALL);
            CHECK_WORKER(memcmp(aVertices, aBeforeVertices,
                                sizeof(aBeforeVertices)) == 0);
            CHECK_WORKER(RollerEd_FillGeometry(
                             Sizes.uiGeometryEpoch, aVertices, 4u,
                             auiIndices, 6u, &Primitive, 0u, &Material, 1u)
                         == ROLLER_ED_RESULT_BUFFER_TOO_SMALL);
            CHECK_WORKER(memcmp(aVertices, aBeforeVertices,
                                sizeof(aBeforeVertices)) == 0);
            CHECK_WORKER(RollerEd_FillGeometry(
                             Sizes.uiGeometryEpoch, aVertices, 4u,
                             auiIndices, 6u, &Primitive, 1u, &Material, 0u)
                         == ROLLER_ED_RESULT_BUFFER_TOO_SMALL);
            CHECK_WORKER(memcmp(aVertices, aBeforeVertices,
                                sizeof(aBeforeVertices)) == 0);

            /* A required buffer that is missing entirely is a caller bug, not
             * a sizing problem. */
            CHECK_WORKER(RollerEd_FillGeometry(
                             Sizes.uiGeometryEpoch, NULL, 4u,
                             auiIndices, 6u, &Primitive, 1u, &Material, 1u)
                         == ROLLER_ED_RESULT_INVALID_ARGUMENT);

            memset(&Primitive, 0x22, sizeof(Primitive));
            memset(&Material, 0x22, sizeof(Material));
            CHECK_WORKER(RollerEd_FillGeometry(
                             Sizes.uiGeometryEpoch, aVertices, 4u,
                             auiIndices, 6u, &Primitive, 1u, &Material, 1u)
                         == ROLLER_ED_RESULT_OK);
            CHECK_WORKER(auiIndices[0] == 0u && auiIndices[1] == 1u
                         && auiIndices[2] == 2u && auiIndices[3] == 0u
                         && auiIndices[4] == 2u && auiIndices[5] == 3u);
            CHECK_WORKER(Primitive.uiFirstIndex == 0u
                         && Primitive.uiIndexCount == 6u);
            CHECK_WORKER(Primitive.byTopology
                         == ROLLER_ED_TOPOLOGY_TRIANGLE_LIST);
            CHECK_WORKER(Primitive.uiBackMaterialId
                         == ROLLER_ED_INVALID_MATERIAL_ID);
            CHECK_WORKER(Primitive.unContentClass
                         == ROLLER_ED_CONTENT_AUTHORED_TRACK);
            CHECK_WORKER(aVertices[0].fNormal[2] == 1.0f);
            CHECK_WORKER(Material.uiKind == ROLLER_ED_MATERIAL_TEXTURED_TILE);

            /* Extraction is cached per epoch: repeated queries and fills at
             * the same epoch must not re-extract. */
            {
                int iExtractCount = s_iStubExtractCount;

                Sizes.uiStructSize = sizeof(Sizes);
                Sizes.uiVersion = ROLLER_ED_GEOMETRY_SIZES_VERSION;
                CHECK_WORKER(RollerEd_QueryGeometrySizes(&Sizes)
                             == ROLLER_ED_RESULT_OK);
                CHECK_WORKER(RollerEd_FillGeometry(
                                 Sizes.uiGeometryEpoch, aVertices, 4u,
                                 auiIndices, 6u, &Primitive, 1u,
                                 &Material, 1u) == ROLLER_ED_RESULT_OK);
                CHECK_WORKER(s_iStubExtractCount == iExtractCount);
            }

            /* AD-7a: a caller that sized its buffers against the one-chunk
             * track and then lost the race to a larger load must be refused,
             * not allowed to overflow into buffers sized for the old track. */
            CHECK_WORKER(RollerEd_LoadTrackFile(
                             pContext->szLargeTrack, "facade-test-assets")
                         == ROLLER_ED_RESULT_OK);
            memset(aVertices, 0x33, sizeof(aVertices));
            memcpy(aBeforeVertices, aVertices, sizeof(aBeforeVertices));
            CHECK_WORKER(RollerEd_FillGeometry(
                             uiReadyEpoch, aVertices, 4u,
                             auiIndices, 6u, &Primitive, 1u, &Material, 1u)
                         == ROLLER_ED_RESULT_STALE);
            CHECK_WORKER(memcmp(aVertices, aBeforeVertices,
                                sizeof(aBeforeVertices)) == 0);

            Sizes.uiStructSize = sizeof(Sizes);
            Sizes.uiVersion = ROLLER_ED_GEOMETRY_SIZES_VERSION;
            CHECK_WORKER(RollerEd_QueryGeometrySizes(&Sizes)
                         == ROLLER_ED_RESULT_OK);
            CHECK_WORKER(Sizes.uiPrimitiveCount == 4u);
            CHECK_WORKER(Sizes.uiVertexCount == 16u);
            CHECK_WORKER(Sizes.uiIndexCount == 24u);
            /* Re-querying really does re-extract once the epoch moved. */
            CHECK_WORKER(RollerEd_FillGeometry(
                             Sizes.uiGeometryEpoch, aVertices, 4u,
                             auiIndices, 6u, &Primitive, 1u, &Material, 1u)
                         == ROLLER_ED_RESULT_BUFFER_TOO_SMALL);

            CHECK_WORKER(RollerEd_LoadTrackFile(
                             pContext->szValidTrack, "facade-test-assets")
                         == ROLLER_ED_RESULT_OK);
            Sizes.uiStructSize = sizeof(Sizes);
            Sizes.uiVersion = ROLLER_ED_GEOMETRY_SIZES_VERSION;
            CHECK_WORKER(RollerEd_QueryGeometrySizes(&Sizes)
                         == ROLLER_ED_RESULT_OK);
            uiReadyEpoch = Sizes.uiGeometryEpoch;
            uiReadyGeneration = Sizes.uiTrackGeneration;
        }

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
            uint32_t uiTowerCount = 0xfedcba98u;
            tEdTowerInfo TowerInfo;
            tEdTowerInfo TowerBefore;

            memset(&TowerInfo, 0x6b, sizeof(TowerInfo));
            TowerInfo.uiStructSize = sizeof(TowerInfo);
            TowerInfo.uiVersion = ROLLER_ED_TOWER_INFO_VERSION;
            TowerBefore = TowerInfo;
            CHECK_WORKER(RollerEd_QueryTowerCount(&uiTowerCount)
                         == ROLLER_ED_RESULT_NO_SCENE);
            CHECK_WORKER(uiTowerCount == 0xfedcba98u);
            CHECK_WORKER(RollerEd_QueryTower(0u, &TowerInfo)
                         == ROLLER_ED_RESULT_NO_SCENE);
            CHECK_WORKER(memcmp(&TowerInfo, &TowerBefore,
                                sizeof(TowerBefore)) == 0);
        }

        {
            tEdVertex Vertex;
            tEdVertex Before;
            tEdVertex aVertices[4];
            uint32_t auiIndices[6];
            tEdPrimitive Primitive;
            tEdMaterial Material;
            tEdVertex aBefore[4];

            memset(&Vertex, 0xc3, sizeof(Vertex));
            Before = Vertex;
            CHECK_WORKER(RollerEd_FillGeometry(
                             Sizes.uiGeometryEpoch, &Vertex, 1u,
                             NULL, 0u, NULL, 0u, NULL, 0u)
                         == ROLLER_ED_RESULT_NO_SCENE);
            CHECK_WORKER(memcmp(&Vertex, &Before, sizeof(Before)) == 0);

            /*
             * AD-7d's v5 hole: the failed load cleared the scene without
             * advancing the track generation, so a caller holding the
             * pre-failure generation would have passed a generation check and
             * written into a cleared scene. The geometry epoch did move, and
             * the buffers below are exactly the size the pre-failure query
             * reported -- so this refusal is the epoch and scene state doing
             * the work, not a capacity check.
             */
            CHECK_WORKER(uiReadyGeneration == Sizes.uiTrackGeneration);
            memset(aVertices, 0x44, sizeof(aVertices));
            memcpy(aBefore, aVertices, sizeof(aBefore));
            CHECK_WORKER(RollerEd_FillGeometry(
                             uiReadyEpoch, aVertices, 4u, auiIndices, 6u,
                             &Primitive, 1u, &Material, 1u)
                         == ROLLER_ED_RESULT_NO_SCENE);
            CHECK_WORKER(memcmp(aVertices, aBefore, sizeof(aBefore)) == 0);
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
    CHECK_WORKER(RollerEd_AdvanceStunts(1u)
                 == ROLLER_ED_RESULT_NO_SCENE);
    CHECK_WORKER(s_uiLegacyStuntTicks == 3u);
    uint32_t uiSwitchEpoch = Sizes.uiGeometryEpoch;
    uint32_t uiSwitchGeneration = Sizes.uiTrackGeneration;
    CHECK_WORKER(RollerEd_SelectRenderer(4u)
                 == ROLLER_ED_RESULT_INVALID_ARGUMENT);
    CHECK_WORKER(RollerEd_SelectRenderer(ROLLER_ED_RENDERER_SOFTWARE)
                 == ROLLER_ED_RESULT_OK);
    CHECK_WORKER(s_eStubActiveRenderer == ROLLER_ED_RENDERER_SOFTWARE);
    s_uiStubAvailableRenderers = ROLLER_ED_RENDERER_SOFTWARE;
    CHECK_WORKER(RollerEd_GetAvailableRenderers()
                 == ROLLER_ED_RENDERER_SOFTWARE);
    CHECK_WORKER(RollerEd_SelectRenderer(ROLLER_ED_RENDERER_GPU)
                 == ROLLER_ED_RESULT_RENDERER_UNAVAILABLE);
    CHECK_WORKER(s_eStubActiveRenderer == ROLLER_ED_RENDERER_SOFTWARE);
    s_uiStubAvailableRenderers |= ROLLER_ED_RENDERER_GPU;
    CHECK_WORKER(RollerEd_SelectRenderer(ROLLER_ED_RENDERER_GPU)
                 == ROLLER_ED_RESULT_OK);
    CHECK_WORKER(s_eStubActiveRenderer == ROLLER_ED_RENDERER_GPU);
    {
        tEdGraphicsSettings Graphics = {
            .uiStructSize = sizeof(Graphics),
            .uiVersion = ROLLER_ED_GRAPHICS_SETTINGS_VERSION,
            .eRenderer = ROLLER_ED_RENDERER_SOFTWARE,
            .eAntiAliasing = ROLLER_ED_ANTI_ALIASING_4X,
            .eAnisotropy = ROLLER_ED_ANISOTROPY_8X,
            .eTextureFilter = ROLLER_ED_TEXTURE_FILTER_BILINEAR,
            .uiTrilinear = 1u,
            .uiEmulateTransparentBorders = 0u,
            .fDrawDistanceFraction = 0.5f,
            .fLodBias = -1.25f,
            .eSoftwareDisplay = ROLLER_ED_SOFTWARE_DISPLAY_VGA
        };

        CHECK_WORKER(RollerEd_SetGraphicsSettings(NULL)
                     == ROLLER_ED_RESULT_INVALID_ARGUMENT);
        Graphics.uiTrilinear = 2u;
        CHECK_WORKER(RollerEd_SetGraphicsSettings(&Graphics)
                     == ROLLER_ED_RESULT_INVALID_ARGUMENT);
        CHECK_WORKER(s_iLegacySetGraphicsCount == 0);
        Graphics.uiTrilinear = 1u;
        Graphics.eSoftwareDisplay = ROLLER_ED_SOFTWARE_DISPLAY_SVGA + 1u;
        CHECK_WORKER(RollerEd_SetGraphicsSettings(&Graphics)
                     == ROLLER_ED_RESULT_INVALID_ARGUMENT);
        CHECK_WORKER(s_iLegacySetGraphicsCount == 0);
        Graphics.eSoftwareDisplay = ROLLER_ED_SOFTWARE_DISPLAY_VGA;
        CHECK_WORKER(RollerEd_SetGraphicsSettings(&Graphics)
                     == ROLLER_ED_RESULT_OK);
        CHECK_WORKER(s_iLegacySetGraphicsCount == 1);
        CHECK_WORKER(s_eStubActiveRenderer == ROLLER_ED_RENDERER_SOFTWARE);
        CHECK_WORKER(s_LastLegacyGraphics.eAntiAliasing
                     == ROLLER_ED_ANTI_ALIASING_4X);
        CHECK_WORKER(s_LastLegacyGraphics.eAnisotropy
                     == ROLLER_ED_ANISOTROPY_8X);
        CHECK_WORKER(s_LastLegacyGraphics.eTextureFilter
                     == ROLLER_ED_TEXTURE_FILTER_BILINEAR);
        CHECK_WORKER(s_LastLegacyGraphics.fDrawDistanceFraction == 0.5f);
        CHECK_WORKER(s_LastLegacyGraphics.fLodBias == -1.25f);
        CHECK_WORKER(s_LastLegacyGraphics.eSoftwareDisplay
                     == ROLLER_ED_SOFTWARE_DISPLAY_VGA);
    }
    Sizes.uiStructSize = sizeof(Sizes);
    Sizes.uiVersion = ROLLER_ED_GEOMETRY_SIZES_VERSION;
    CHECK_WORKER(RollerEd_QueryGeometrySizes(&Sizes) == ROLLER_ED_RESULT_OK);
    CHECK_WORKER(Sizes.uiGeometryEpoch == uiSwitchEpoch);
    CHECK_WORKER(Sizes.uiTrackGeneration == uiSwitchGeneration);
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

    CHECK_MAIN(argc == 4);
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
    Context.szLargeTrack = argv[3];
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
        uint32_t uiTowerCount = 0x12345678u;
        tEdTowerInfo TowerInfo = {
            .uiStructSize = sizeof(TowerInfo),
            .uiVersion = ROLLER_ED_TOWER_INFO_VERSION
        };
        tEdTowerInfo TowerBefore = TowerInfo;
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
                RollerEd_QueryTowerCount(&uiTowerCount)
                    == ROLLER_ED_RESULT_WRONG_THREAD
                && uiTowerCount == 0x12345678u,
                __LINE__);
        if (iMainFailure == 0)
            iMainFailure = check_condition(
                RollerEd_QueryTower(0u, &TowerInfo)
                    == ROLLER_ED_RESULT_WRONG_THREAD
                && memcmp(&TowerInfo, &TowerBefore,
                          sizeof(TowerBefore)) == 0,
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
    /* Three original successful installs plus the larger-track reload and the
     * return to the one-chunk track that E4A-S5's stale coverage needs. */
    CHECK_MAIN(s_iLegacyInstallCount == 5);
    CHECK_MAIN(s_iLegacyRenderCount == 1);
    CHECK_MAIN(s_iLegacySetCameraCount == 1);
    CHECK_MAIN(s_iLegacySetOverlayCount == 2);

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
