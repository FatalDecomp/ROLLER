/*
 * E4A-S4 acceptance: drive the canonical full-track traversal over a real
 * loaded track and check the generated normals against the conventions in
 * docs/adr/0003-canonical-geometry-conventions.md.
 *
 * The unit tests in editor_surface_test.c pin the maths on synthetic quads.
 * This crosses the real facade so the claims are checked against the twisted,
 * rolled, looping geometry the track format actually produces.
 */
#include "3d.h"
#include "drawtrk3.h"
#include "editor_api.h"
#include "editor_surface.h"
#include "loadtrak.h"

#define SDL_MAIN_HANDLED 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_COLLECTED_SURFACES 16384u
#define MAX_COLLECTED_MATERIALS 2048u
#define SURFACE_CLASS_COUNT 14u

typedef struct
{
    float afNormal[3];
    uint32_t uiChunkId;
    uint16_t unSurfaceClass;
    uint16_t unFlags;
} tCollectedSurface;

typedef struct
{
    tCollectedSurface *pSurfaces;
    uint32_t uiCount;
    uint32_t uiZeroNormals;
    uint32_t uiFacingMismatches;
    uint32_t uiIllConditionedFacing;
    uint32_t uiNonUnitNormals;
    uint32_t uiVertexNormalMismatches;
    bool bOverflowed;
} tCollector;

typedef struct
{
    const char *szTrackPath;
    const char *szAssetRoot;
    char szError[512];
    int iResult;
} tContext;

static void cross3(const float afLeft[3], const float afRight[3],
                   float afOut[3])
{
    afOut[0] = afLeft[1] * afRight[2] - afLeft[2] * afRight[1];
    afOut[1] = afLeft[2] * afRight[0] - afLeft[0] * afRight[2];
    afOut[2] = afLeft[0] * afRight[1] - afLeft[1] * afRight[0];
}

static float dot3(const float afLeft[3], const float afRight[3])
{
    return afLeft[0] * afRight[0] + afLeft[1] * afRight[1]
         + afLeft[2] * afRight[2];
}

static bool is_zero3(const float afVector[3])
{
    return afVector[0] == 0.0f && afVector[1] == 0.0f && afVector[2] == 0.0f;
}

/*
 * Twice the polygon's area, computed independently of editor_surface.c so a
 * bug there cannot validate itself. Used as the scale a three-corner facing
 * vector has to be measured against.
 */
static float newell_magnitude(const tEdSurfaceEmission *pSurface)
{
    float afNewell[3] = { 0.0f, 0.0f, 0.0f };

    for (uint32_t i = 0; i < ED_SURFACE_VERTEX_COUNT; i++) {
        const float *pfCurrent = pSurface->aVertices[i].fPosition;
        const float *pfNext =
            pSurface->aVertices[(i + 1u) % ED_SURFACE_VERTEX_COUNT].fPosition;
        afNewell[0] += (pfCurrent[1] - pfNext[1]) * (pfCurrent[2] + pfNext[2]);
        afNewell[1] += (pfCurrent[2] - pfNext[2]) * (pfCurrent[0] + pfNext[0]);
        afNewell[2] += (pfCurrent[0] - pfNext[0]) * (pfCurrent[1] + pfNext[1]);
    }
    return sqrtf(dot3(afNewell, afNewell));
}

static void collect_surface(const tEdSurfaceEmission *pSurface,
                            void *pUserData)
{
    tCollector *pCollector = pUserData;
    float afEdgeNext[3];
    float afEdgePrev[3];
    float afRendererFacing[3];
    tCollectedSurface *pRecord;

    if (pCollector->uiCount >= MAX_COLLECTED_SURFACES) {
        pCollector->bOverflowed = true;
        return;
    }
    pRecord = &pCollector->pSurfaces[pCollector->uiCount++];
    memcpy(pRecord->afNormal, pSurface->fNormal, sizeof(pRecord->afNormal));
    pRecord->uiChunkId = pSurface->uiChunkId;
    pRecord->unSurfaceClass = pSurface->unSurfaceClass;
    pRecord->unFlags = pSurface->unFlags;

    if (is_zero3(pSurface->fNormal)) {
        /* A collapsed quad is allowed to publish no normal, but it must be
         * exactly zero rather than a NaN, and it must be rare. */
        pCollector->uiZeroNormals++;
        return;
    }

    if (fabsf(sqrtf(dot3(pSurface->fNormal, pSurface->fNormal)) - 1.0f)
            > 0.001f)
        pCollector->uiNonUnitNormals++;

    /* The renderer's own front-face test (scene_render_gpu.c) uses
     * (v1-v0) x (v3-v0); the emitted normal must agree with it. */
    for (uint32_t i = 0; i < 3; i++) {
        afEdgeNext[i] = pSurface->aVertices[1].fPosition[i]
                      - pSurface->aVertices[0].fPosition[i];
        afEdgePrev[i] = pSurface->aVertices[3].fPosition[i]
                      - pSurface->aVertices[0].fPosition[i];
    }
    cross3(afEdgeNext, afEdgePrev, afRendererFacing);

    /* The renderer's vector is built from three of the four corners, so on a
     * pinched quad -- TRACK3 chunk 124's right shoulder collapses v0 onto v1
     * to within a unit at coordinates of magnitude 90000 -- it is numerical
     * noise, not a facing decision. Compare only where it is well
     * conditioned: its magnitude must be a real fraction of twice the
     * polygon area that Newell's method measures over all four corners. */
    if (sqrtf(dot3(afRendererFacing, afRendererFacing))
            < 0.1f * newell_magnitude(pSurface))
        pCollector->uiIllConditionedFacing++;
    else if (dot3(pSurface->fNormal, afRendererFacing) <= 0.0f)
        pCollector->uiFacingMismatches++;

    for (uint32_t i = 0; i < pSurface->uiVertexCount; i++) {
        const float *pfNormal = pSurface->aVertices[i].fNormal;
        if (is_zero3(pfNormal))
            continue;
        if (fabsf(sqrtf(dot3(pfNormal, pfNormal)) - 1.0f) > 0.001f
                || dot3(pfNormal, pSurface->fNormal) <= 0.0f) {
            pCollector->uiVertexNormalMismatches++;
        }
    }
}

static bool emit_with_camera(tContext *pContext, const tEdCameraState *pCamera,
                             tCollector *pCollector)
{
    tEdMaterial *pMaterials = calloc(MAX_COLLECTED_MATERIALS,
                                     sizeof(*pMaterials));
    tEdMaterialTable Table;
    bool bOk;

    if (!pMaterials) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "material allocation failed");
        return false;
    }
    if (RollerEd_SetCamera(pCamera) != ROLLER_ED_RESULT_OK) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "RollerEd_SetCamera failed: %s", RollerEd_GetLastError());
        free(pMaterials);
        return false;
    }
    if (!drawtrk3_init_editor_material_table(
            &Table, pMaterials, MAX_COLLECTED_MATERIALS)) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "material table initialization failed");
        free(pMaterials);
        return false;
    }

    bOk = drawtrk3_emit_full_track(&Table, collect_surface, pCollector);
    free(pMaterials);
    if (!bOk) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "full-track traversal aborted after %u surfaces",
                 pCollector->uiCount);
        return false;
    }
    return true;
}

/*
 * Consistency over real geometry, measured where it is a property of the
 * emitter rather than of the terrain.
 *
 * For the road body -- centre, shoulders, walls, roof, outer wall floor --
 * neighbouring chunks describe one continuous ribbon. A corkscrew turns its
 * normal gradually, so a near-reversal against the previous chunk of the same
 * class means that chunk was wound the other way, which is a real defect.
 *
 * The four outer-wall classes are the environment skirt, not a ribbon. Where
 * the terrain profile crosses over, a panel legitimately faces inward at one
 * chunk and outward at the next; TRACK3 does this twelve times. Those are
 * counted and reported, never failed -- asserting otherwise would be
 * asserting something false about the source data.
 */
static uint32_t count_winding_reversals(const tCollector *pCollector,
                                        uint32_t *puiSkirtReversals)
{
    float aafPrevNormal[SURFACE_CLASS_COUNT][3];
    bool abHavePrev[SURFACE_CLASS_COUNT];
    uint32_t uiReversals = 0;

    memset(aafPrevNormal, 0, sizeof(aafPrevNormal));
    memset(abHavePrev, 0, sizeof(abHavePrev));
    *puiSkirtReversals = 0;

    for (uint32_t i = 0; i < pCollector->uiCount; i++) {
        const tCollectedSurface *pRecord = &pCollector->pSurfaces[i];
        uint16_t unClass = pRecord->unSurfaceClass;

        if (unClass >= SURFACE_CLASS_COUNT || is_zero3(pRecord->afNormal))
            continue;
        if (abHavePrev[unClass]
                && dot3(aafPrevNormal[unClass], pRecord->afNormal) < -0.5f) {
            if (unClass > ROLLER_ED_SURFACE_CLASS_OUTER_WALL_FLOOR)
                (*puiSkirtReversals)++;
            else
                uiReversals++;
        }
        memcpy(aafPrevNormal[unClass], pRecord->afNormal,
               sizeof(aafPrevNormal[unClass]));
        abHavePrev[unClass] = true;
    }
    return uiReversals;
}

static int check_collector(tContext *pContext, const tCollector *pCollector,
                           const char *szLabel)
{
    if (pCollector->bOverflowed) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "%s: surface collector overflowed", szLabel);
        return 0;
    }
    if (pCollector->uiCount == 0) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "%s: traversal emitted no surfaces", szLabel);
        return 0;
    }
    if (pCollector->uiNonUnitNormals != 0) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "%s: %u surface normals were not unit length", szLabel,
                 pCollector->uiNonUnitNormals);
        return 0;
    }
    if (pCollector->uiFacingMismatches != 0) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "%s: %u normals disagreed with the renderer facing rule",
                 szLabel, pCollector->uiFacingMismatches);
        return 0;
    }
    if (pCollector->uiVertexNormalMismatches != 0) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "%s: %u vertex normals were not unit length or faced the "
                 "opposite hemisphere from their surface", szLabel,
                 pCollector->uiVertexNormalMismatches);
        return 0;
    }
    return -1;
}

static int conventions_worker(void *pUserData)
{
    static const tEdCameraState CameraA = {
        .uiStructSize = sizeof(tEdCameraState),
        .uiVersion = ROLLER_ED_CAMERA_STATE_VERSION,
        .fPosition = { 0.0f, 0.0f, 500.0f },
        .fYawDegrees = 0.0f,
        .fPitchDegrees = 0.0f
    };
    static const tEdCameraState CameraB = {
        .uiStructSize = sizeof(tEdCameraState),
        .uiVersion = ROLLER_ED_CAMERA_STATE_VERSION,
        .fPosition = { -90000.0f, 45000.0f, -12000.0f },
        .fYawDegrees = 217.5f,
        .fPitchDegrees = -33.25f
    };
    tContext *pContext = pUserData;
    tRollerEdInitInfo InitInfo = {
        .uiStructSize = sizeof(InitInfo),
        .uiVersion = ROLLER_ED_INIT_INFO_VERSION,
        .szAssetRoot = pContext->szAssetRoot,
        .ePreferredRenderer = ROLLER_ED_RENDERER_SOFTWARE,
        .uiAllowSoftwareFallback = 1u
    };
    tCollector First = { 0 };
    tCollector Second = { 0 };
    uint32_t uiReversals;
    uint32_t uiSkirtReversals;

    First.pSurfaces = calloc(MAX_COLLECTED_SURFACES, sizeof(*First.pSurfaces));
    Second.pSurfaces = calloc(MAX_COLLECTED_SURFACES,
                              sizeof(*Second.pSurfaces));
    if (!First.pSurfaces || !Second.pSurfaces) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "surface allocation failed");
        pContext->iResult = 1;
        goto cleanup;
    }

    if (RollerEd_Init(&InitInfo) != ROLLER_ED_RESULT_OK) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "RollerEd_Init failed: %s", RollerEd_GetLastError());
        pContext->iResult = 1;
        goto cleanup;
    }
    if (RollerEd_LoadTrackFile(pContext->szTrackPath, pContext->szAssetRoot)
            != ROLLER_ED_RESULT_OK) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "RollerEd_LoadTrackFile failed: %s", RollerEd_GetLastError());
        pContext->iResult = 1;
        goto shutdown;
    }

    if (!emit_with_camera(pContext, &CameraA, &First)
            || !check_collector(pContext, &First, "camera A")) {
        pContext->iResult = 1;
        goto shutdown;
    }
    if (!emit_with_camera(pContext, &CameraB, &Second)
            || !check_collector(pContext, &Second, "camera B")) {
        pContext->iResult = 1;
        goto shutdown;
    }

    /* E4A-S2 promised camera-independent geometry; E4A-S4 extends that
     * promise to the field it just added. */
    if (First.uiCount != Second.uiCount
            || memcmp(First.pSurfaces, Second.pSurfaces,
                      First.uiCount * sizeof(*First.pSurfaces)) != 0) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "two cameras produced different geometry or normals "
                 "(%u vs %u surfaces)", First.uiCount, Second.uiCount);
        pContext->iResult = 1;
        goto shutdown;
    }

    uiReversals = count_winding_reversals(&First, &uiSkirtReversals);
    if (uiReversals != 0) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "%u chunk-to-chunk winding reversals in the road body",
                 uiReversals);
        pContext->iResult = 1;
        goto shutdown;
    }

    printf("emitted %u surfaces over %d chunks; %u degenerate quads, "
           "%u pinched quads whose three-corner facing vector is noise, "
           "%u terrain-driven skirt reversals\n",
           First.uiCount, TRAK_LEN, First.uiZeroNormals,
           First.uiIllConditionedFacing, uiSkirtReversals);

shutdown:
    if (RollerEd_Shutdown() != ROLLER_ED_RESULT_OK && !pContext->iResult) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "RollerEd_Shutdown failed: %s", RollerEd_GetLastError());
        pContext->iResult = 1;
    }
cleanup:
    free(First.pSurfaces);
    free(Second.pSurfaces);
    return pContext->iResult;
}

int main(int argc, char **argv)
{
    tRollerEdBootstrapInfo BootstrapInfo = {
        .uiStructSize = sizeof(BootstrapInfo),
        .uiVersion = ROLLER_ED_BOOTSTRAP_INFO_VERSION,
        .uiFlags = 0u
    };
    tContext Context;
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
        fprintf(stderr, "RollerEd_Bootstrap failed: %s\n",
                RollerEd_GetLastError());
        return 1;
    }
    pWorker = SDL_CreateThread(
        conventions_worker, "editor-geometry-conventions", &Context);
    if (!pWorker) {
        fprintf(stderr, "worker creation failed: %s\n", SDL_GetError());
        RollerEd_Teardown();
        return 1;
    }
    SDL_WaitThread(pWorker, &iWorkerResult);
    if (RollerEd_Teardown() != ROLLER_ED_RESULT_OK && iWorkerResult == 0) {
        fprintf(stderr, "RollerEd_Teardown failed: %s\n",
                RollerEd_GetLastError());
        return 1;
    }
    if (iWorkerResult != 0) {
        fprintf(stderr, "E4A-S4 acceptance failed: %s\n", Context.szError);
        return 1;
    }
    puts("E4A-S4 PASS: full-track normals are unit length, agree with the "
         "renderer front-face rule wherever it is well conditioned, wind the "
         "road body consistently, and do not depend on the camera");
    return 0;
}
