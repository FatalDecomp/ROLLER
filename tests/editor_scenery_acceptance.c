/*
 * E4A-S6 acceptance: drive the canonical scenery traversal over a real loaded
 * track and check that signs and buildings reach an exporter without a camera.
 *
 * This is the scenery counterpart of editor_geometry_conventions_acceptance.c.
 * The claims that matter are the ones a unit test on synthetic data cannot
 * make: that two very different cameras produce byte-identical scenery, that
 * nothing runtime-generated escapes into the canonical stream, and that the
 * public geometry API publishes the result alongside the track.
 */
#include "3d.h"
#include "building.h"
#include "drawtrk3.h"
#include "editor_api.h"
#include "editor_surface.h"
#include "loadtrak.h"
#include "scene_render.h"

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

typedef struct
{
    tEdSurfaceEmission *pSurfaces;
    uint32_t uiCount;
    bool bOverflowed;
} tCollector;

typedef struct
{
    const char *szTrackPath;
    const char *szAssetRoot;
    char szError[512];
    int iResult;
} tContext;

#define FAIL(...) \
    do { \
        snprintf(pContext->szError, sizeof(pContext->szError), __VA_ARGS__); \
        return false; \
    } while (0)

static float dot3(const float afLeft[3], const float afRight[3])
{
    return afLeft[0] * afRight[0] + afLeft[1] * afRight[1]
         + afLeft[2] * afRight[2];
}

static bool is_zero3(const float afVector[3])
{
    return afVector[0] == 0.0f && afVector[1] == 0.0f && afVector[2] == 0.0f;
}

static void collect_surface(const tEdSurfaceEmission *pSurface,
                            void *pUserData)
{
    tCollector *pCollector = pUserData;

    if (pCollector->uiCount >= MAX_COLLECTED_SURFACES) {
        pCollector->bOverflowed = true;
        return;
    }
    /* ed_emit_surface zeroes the emission before filling it, so the padding is
     * deterministic and the whole struct can be compared byte for byte. */
    pCollector->pSurfaces[pCollector->uiCount++] = *pSurface;
}

static void collector_release(tCollector *pCollector)
{
    free(pCollector->pSurfaces);
    memset(pCollector, 0, sizeof(*pCollector));
}

static bool collector_init(tContext *pContext, tCollector *pCollector)
{
    memset(pCollector, 0, sizeof(*pCollector));
    pCollector->pSurfaces = calloc(MAX_COLLECTED_SURFACES,
                                   sizeof(*pCollector->pSurfaces));
    if (!pCollector->pSurfaces)
        FAIL("surface collector allocation failed");
    return true;
}

/*
 * Runs the scenery producer from one camera, capturing both the emissions and
 * the material table they interned into.
 */
static bool emit_scenery_with_camera(tContext *pContext,
                                     const tEdCameraState *pCamera,
                                     tCollector *pCollector,
                                     tEdMaterial *pMaterials,
                                     uint32_t *puiMaterialCount)
{
    tEdMaterialTable Table;

    if (RollerEd_SetCamera(pCamera) != ROLLER_ED_RESULT_OK)
        FAIL("RollerEd_SetCamera failed: %s", RollerEd_GetLastError());
    if (!drawtrk3_init_editor_material_table(
            &Table, pMaterials, MAX_COLLECTED_MATERIALS))
        FAIL("material table initialization failed");
    if (!drawtrk3_emit_full_scenery(&Table, collect_surface, pCollector))
        FAIL("scenery traversal aborted after %u surfaces",
             pCollector->uiCount);
    if (pCollector->bOverflowed)
        FAIL("scenery collector overflowed");
    *puiMaterialCount = Table.uiCount;
    return true;
}

static bool check_scenery_identity(tContext *pContext,
                                   const tCollector *pCollector,
                                   uint32_t *puiSigns,
                                   uint32_t *puiScenery)
{
    *puiSigns = 0;
    *puiScenery = 0;

    for (uint32_t i = 0; i < pCollector->uiCount; i++) {
        const tEdSurfaceEmission *pSurface = &pCollector->pSurfaces[i];
        float afEdgeNext[3];
        float afEdgePrev[3];
        float afRendererFacing[3];

        /* Every scenery surface is one of the two scenery classes; the track
         * classes belong to the other producer. */
        if (pSurface->unSurfaceClass != ROLLER_ED_SURFACE_CLASS_SIGN
                && pSurface->unSurfaceClass
                    != ROLLER_ED_SURFACE_CLASS_BUILDING)
            FAIL("scenery surface %u has track surface class %u", i,
                 pSurface->unSurfaceClass);

        /* The acceptance criterion the exporters need: nothing the renderer
         * generates from the viewer reaches the canonical stream. */
        if (pSurface->unContentClass == ROLLER_ED_CONTENT_RUNTIME_SCENERY)
            FAIL("scenery surface %u is RUNTIME_SCENERY", i);
        if (pSurface->unContentClass == ROLLER_ED_CONTENT_AUTHORED_SIGN)
            (*puiSigns)++;
        else if (pSurface->unContentClass == ROLLER_ED_CONTENT_AUTHORED_SCENERY)
            (*puiScenery)++;
        else
            FAIL("scenery surface %u has content class %u", i,
                 pSurface->unContentClass);

        if (pSurface->byTopology != ROLLER_ED_TOPOLOGY_QUAD
                || pSurface->uiVertexCount != ED_SURFACE_VERTEX_COUNT)
            FAIL("scenery surface %u is not a quad", i);

        if (is_zero3(pSurface->fNormal))
            continue;
        if (fabsf(sqrtf(dot3(pSurface->fNormal, pSurface->fNormal)) - 1.0f)
                > 0.001f)
            FAIL("scenery surface %u has a non-unit normal", i);

        /* ADR 0005: the authored vertex order is canonical, and its
         * right-hand-rule normal is the front face -- the same rule ADR 0003
         * records for track surfaces. */
        for (uint32_t j = 0; j < 3; j++) {
            afEdgeNext[j] = pSurface->aVertices[1].fPosition[j]
                          - pSurface->aVertices[0].fPosition[j];
            afEdgePrev[j] = pSurface->aVertices[3].fPosition[j]
                          - pSurface->aVertices[0].fPosition[j];
        }
        afRendererFacing[0] = afEdgeNext[1] * afEdgePrev[2]
                            - afEdgeNext[2] * afEdgePrev[1];
        afRendererFacing[1] = afEdgeNext[2] * afEdgePrev[0]
                            - afEdgeNext[0] * afEdgePrev[2];
        afRendererFacing[2] = afEdgeNext[0] * afEdgePrev[1]
                            - afEdgeNext[1] * afEdgePrev[0];
        if (is_zero3(afRendererFacing))
            continue;
        if (dot3(pSurface->fNormal, afRendererFacing) <= 0.0f)
            FAIL("scenery surface %u disagrees with the renderer facing rule",
                 i);
    }
    /* Retail TRACK3's 66 placed objects are all advert balloons, so the sign
     * count is the assertion that bites; AUTHORED_SCENERY is reported rather
     * than required because this track has none. */
    if (*puiSigns == 0)
        FAIL("no advert panel reached the canonical stream");
    return true;
}

/*
 * The public API half: the extraction the editor and both exporters consume
 * must carry the same scenery, and must reference the building/sign texture
 * bank that E4-S4 already writes as <name>_BLD.png.
 */
static bool check_published_geometry(tContext *pContext,
                                     uint32_t uiSceneryCount)
{
    tEdGeometrySizes Sizes;
    tEdVertex *pVertices = NULL;
    uint32_t *puiIndices = NULL;
    tEdPrimitive *pPrimitives = NULL;
    tEdMaterial *pMaterials = NULL;
    uint32_t uiSigns = 0;
    uint32_t uiScenery = 0;
    uint32_t uiRuntime = 0;
    uint32_t uiBuildingBankMaterials = 0;
    bool bOk = false;

    memset(&Sizes, 0, sizeof(Sizes));
    Sizes.uiStructSize = sizeof(Sizes);
    Sizes.uiVersion = ROLLER_ED_GEOMETRY_SIZES_VERSION;
    if (RollerEd_QueryGeometrySizes(&Sizes) != ROLLER_ED_RESULT_OK) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "RollerEd_QueryGeometrySizes failed: %s",
                 RollerEd_GetLastError());
        return false;
    }

    pVertices = calloc(Sizes.uiVertexCount, sizeof(*pVertices));
    puiIndices = calloc(Sizes.uiIndexCount, sizeof(*puiIndices));
    pPrimitives = calloc(Sizes.uiPrimitiveCount, sizeof(*pPrimitives));
    pMaterials = calloc(Sizes.uiMaterialCount, sizeof(*pMaterials));
    if (!pVertices || !puiIndices || !pPrimitives || !pMaterials) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "geometry buffer allocation failed");
        goto done;
    }
    if (RollerEd_FillGeometry(
            Sizes.uiGeometryEpoch, pVertices, Sizes.uiVertexCount,
            puiIndices, Sizes.uiIndexCount, pPrimitives,
            Sizes.uiPrimitiveCount, pMaterials, Sizes.uiMaterialCount)
                != ROLLER_ED_RESULT_OK) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "RollerEd_FillGeometry failed: %s", RollerEd_GetLastError());
        goto done;
    }

    for (uint32_t i = 0; i < Sizes.uiPrimitiveCount; i++) {
        switch (pPrimitives[i].unContentClass) {
        case ROLLER_ED_CONTENT_AUTHORED_SIGN:    uiSigns++; break;
        case ROLLER_ED_CONTENT_AUTHORED_SCENERY: uiScenery++; break;
        case ROLLER_ED_CONTENT_RUNTIME_SCENERY:  uiRuntime++; break;
        default: break;
        }
        if (pPrimitives[i].uiFrontMaterialId >= Sizes.uiMaterialCount) {
            snprintf(pContext->szError, sizeof(pContext->szError),
                     "primitive %u references material %u of %u", i,
                     pPrimitives[i].uiFrontMaterialId, Sizes.uiMaterialCount);
            goto done;
        }
    }
    for (uint32_t i = 0; i < Sizes.uiMaterialCount; i++) {
        if (pMaterials[i].uiTextureSet == (uint32_t)TEXTURE_BANK_BUILDING)
            uiBuildingBankMaterials++;
    }

    if (uiRuntime != 0) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "%u published primitives are RUNTIME_SCENERY", uiRuntime);
        goto done;
    }
    if (uiSigns + uiScenery != uiSceneryCount) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "the extraction published %u scenery primitives but the "
                 "traversal emitted %u", uiSigns + uiScenery, uiSceneryCount);
        goto done;
    }
    if (uiBuildingBankMaterials == 0) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "no published material referenced the building/sign bank");
        goto done;
    }
    printf("published %u primitives: %u signs, %u scenery, %u building-bank "
           "materials\n", Sizes.uiPrimitiveCount, uiSigns, uiScenery,
           uiBuildingBankMaterials);
    bOk = true;

done:
    free(pVertices);
    free(puiIndices);
    free(pPrimitives);
    free(pMaterials);
    return bOk;
}

static bool scenery_checks(tContext *pContext)
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
    tCollector First;
    tCollector Second;
    tEdMaterial *pFirstMaterials = NULL;
    tEdMaterial *pSecondMaterials = NULL;
    uint32_t uiFirstMaterialCount = 0;
    uint32_t uiSecondMaterialCount = 0;
    uint32_t uiSigns = 0;
    uint32_t uiScenery = 0;
    uint32_t uiTwoSidedSigns = 0;
    bool bOk = false;

    memset(&First, 0, sizeof(First));
    memset(&Second, 0, sizeof(Second));
    pFirstMaterials = calloc(MAX_COLLECTED_MATERIALS,
                             sizeof(*pFirstMaterials));
    pSecondMaterials = calloc(MAX_COLLECTED_MATERIALS,
                              sizeof(*pSecondMaterials));
    if (!pFirstMaterials || !pSecondMaterials) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "material allocation failed");
        goto done;
    }
    if (!collector_init(pContext, &First)
            || !collector_init(pContext, &Second))
        goto done;

    if (!emit_scenery_with_camera(pContext, &CameraA, &First, pFirstMaterials,
                                  &uiFirstMaterialCount))
        goto done;
    if (First.uiCount == 0) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "the scenery traversal emitted nothing for a retail track "
                 "(NumBuildings = %d)", NumBuildings);
        goto done;
    }
    if (!check_scenery_identity(pContext, &First, &uiSigns, &uiScenery))
        goto done;

    /* An advert panel's plan carries FLIP_BACKFACE, which is what
     * DrawBuilding's cull test reads, so the renderer draws it from both
     * sides. The advert texture that replaces the plan's own is not flagged
     * that way, and an emission that took its two-sidedness from the
     * substituted flags would export every balloon single-sided -- invisible
     * from behind in any importer that honours backface culling. */
    for (uint32_t i = 0; i < First.uiCount; i++) {
        const tEdSurfaceEmission *pSurface = &First.pSurfaces[i];
        if (pSurface->unContentClass == ROLLER_ED_CONTENT_AUTHORED_SIGN
                && (pSurface->unFlags & ROLLER_ED_SURFACE_FLAG_TWO_SIDED))
            uiTwoSidedSigns++;
    }
    if (uiTwoSidedSigns != uiSigns) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "only %u of %u advert panels published TWO_SIDED",
                 uiTwoSidedSigns, uiSigns);
        goto done;
    }

    /* The whole point of the story: a second camera, nothing like the first,
     * must produce the identical stream. */
    if (!emit_scenery_with_camera(pContext, &CameraB, &Second,
                                  pSecondMaterials, &uiSecondMaterialCount))
        goto done;
    if (Second.uiCount != First.uiCount
            || memcmp(First.pSurfaces, Second.pSurfaces,
                      (size_t)First.uiCount * sizeof(*First.pSurfaces)) != 0) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "two cameras produced different scenery (%u vs %u surfaces)",
                 First.uiCount, Second.uiCount);
        goto done;
    }
    if (uiSecondMaterialCount != uiFirstMaterialCount
            || memcmp(pFirstMaterials, pSecondMaterials,
                      (size_t)uiFirstMaterialCount * sizeof(*pFirstMaterials))
                != 0) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "two cameras interned different materials (%u vs %u)",
                 uiFirstMaterialCount, uiSecondMaterialCount);
        goto done;
    }

    if (!check_published_geometry(pContext, First.uiCount))
        goto done;

    {
        /* Type 10 is the only plan whose polygons are RUNTIME_SCENERY, so it
         * is the only content the traversal deliberately leaves behind. */
        int iTrees = 0;
        for (int i = 0; i < NumBuildings; i++) {
            if (BuildingBase[i][0] == 10)
                iTrees++;
        }
        printf("scenery traversal emitted %u surfaces from %d placed buildings "
               "(%d runtime-billboard trees skipped): %u sign (%u two-sided), "
               "%u scenery; "
               "%u materials, identical across two cameras\n",
               First.uiCount, NumBuildings, iTrees, uiSigns, uiTwoSidedSigns,
               uiScenery,
               uiFirstMaterialCount);
    }
    bOk = true;

done:
    free(pFirstMaterials);
    free(pSecondMaterials);
    collector_release(&First);
    collector_release(&Second);
    return bOk;
}

static int scenery_worker(void *pUserData)
{
    tContext *pContext = pUserData;
    tRollerEdInitInfo InitInfo = {
        .uiStructSize = sizeof(InitInfo),
        .uiVersion = ROLLER_ED_INIT_INFO_VERSION,
        .szAssetRoot = pContext->szAssetRoot,
        .ePreferredRenderer = ROLLER_ED_RENDERER_SOFTWARE,
        .uiAllowSoftwareFallback = 1u
    };

    if (RollerEd_Init(&InitInfo) != ROLLER_ED_RESULT_OK) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "RollerEd_Init failed: %s", RollerEd_GetLastError());
        pContext->iResult = 1;
        return pContext->iResult;
    }
    if (RollerEd_LoadTrackFile(pContext->szTrackPath, pContext->szAssetRoot)
            != ROLLER_ED_RESULT_OK) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "RollerEd_LoadTrackFile failed: %s", RollerEd_GetLastError());
        pContext->iResult = 1;
        goto shutdown;
    }
    if (!scenery_checks(pContext))
        pContext->iResult = 1;

shutdown:
    if (RollerEd_Shutdown() != ROLLER_ED_RESULT_OK && !pContext->iResult) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "RollerEd_Shutdown failed: %s", RollerEd_GetLastError());
        pContext->iResult = 1;
    }
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
    pWorker = SDL_CreateThread(scenery_worker, "editor-scenery", &Context);
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
        fprintf(stderr, "E4A-S6 acceptance failed: %s\n", Context.szError);
        return 1;
    }
    puts("E4A-S6 PASS: the canonical scenery traversal published signs and "
         "buildings identically from two cameras, with no RUNTIME_SCENERY "
         "reaching the public geometry API");
    return 0;
}
