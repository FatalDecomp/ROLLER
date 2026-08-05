/*
 * E4A-S5 acceptance: query and fill the public geometry buffers across the
 * real facade with a retail track loaded.
 *
 * The facade lifecycle test covers the epoch, capacity, and no-write rules
 * against a stub. This checks the same API over the actual canonical
 * traversal: that what comes out of RollerEd_FillGeometry is the geometry the
 * emitter produced, that it survives a stale-provoking reload, and that no
 * core-owned pointer is handed back.
 */
#include "editor_api.h"

#define SDL_MAIN_HANDLED 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    tEdVertex *pVertices;
    uint32_t *puiIndices;
    tEdPrimitive *pPrimitives;
    tEdMaterial *pMaterials;
    tEdGeometrySizes Sizes;
} tGeometryBuffers;

typedef struct
{
    const char *szTrackPath;
    const char *szAssetRoot;
    char szError[512];
    int iResult;
} tContext;

static void buffers_release(tGeometryBuffers *pBuffers)
{
    free(pBuffers->pVertices);
    free(pBuffers->puiIndices);
    free(pBuffers->pPrimitives);
    free(pBuffers->pMaterials);
    memset(pBuffers, 0, sizeof(*pBuffers));
}

static bool buffers_allocate(tGeometryBuffers *pBuffers)
{
    pBuffers->pVertices = calloc(pBuffers->Sizes.uiVertexCount,
                                 sizeof(*pBuffers->pVertices));
    pBuffers->puiIndices = calloc(pBuffers->Sizes.uiIndexCount,
                                  sizeof(*pBuffers->puiIndices));
    pBuffers->pPrimitives = calloc(pBuffers->Sizes.uiPrimitiveCount,
                                   sizeof(*pBuffers->pPrimitives));
    pBuffers->pMaterials = calloc(pBuffers->Sizes.uiMaterialCount,
                                  sizeof(*pBuffers->pMaterials));
    return pBuffers->pVertices && pBuffers->puiIndices
        && pBuffers->pPrimitives && pBuffers->pMaterials;
}

static eRollerEdResult buffers_fill(tGeometryBuffers *pBuffers)
{
    return RollerEd_FillGeometry(
        pBuffers->Sizes.uiGeometryEpoch,
        pBuffers->pVertices, pBuffers->Sizes.uiVertexCount,
        pBuffers->puiIndices, pBuffers->Sizes.uiIndexCount,
        pBuffers->pPrimitives, pBuffers->Sizes.uiPrimitiveCount,
        pBuffers->pMaterials, pBuffers->Sizes.uiMaterialCount);
}

static bool query_sizes(tContext *pContext, tEdGeometrySizes *pSizes)
{
    memset(pSizes, 0, sizeof(*pSizes));
    pSizes->uiStructSize = sizeof(*pSizes);
    pSizes->uiVersion = ROLLER_ED_GEOMETRY_SIZES_VERSION;
    if (RollerEd_QueryGeometrySizes(pSizes) != ROLLER_ED_RESULT_OK) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "RollerEd_QueryGeometrySizes failed: %s",
                 RollerEd_GetLastError());
        return false;
    }
    return true;
}

#define FAIL(...) \
    do { \
        snprintf(pContext->szError, sizeof(pContext->szError), __VA_ARGS__); \
        return false; \
    } while (0)

static bool check_contents(tContext *pContext,
                           const tGeometryBuffers *pBuffers)
{
    const tEdGeometrySizes *pSizes = &pBuffers->Sizes;
    uint32_t uiChunkBound = 0u;

    if (pSizes->uiVertexStride != sizeof(tEdVertex)
            || pSizes->uiPrimitiveStride != sizeof(tEdPrimitive)
            || pSizes->uiMaterialStride != sizeof(tEdMaterial))
        FAIL("published strides do not match the public struct sizes");

    /* Quads triangulated into a triangle list. */
    if (pSizes->uiVertexCount != pSizes->uiPrimitiveCount * 4u
            || pSizes->uiIndexCount != pSizes->uiPrimitiveCount * 6u)
        FAIL("counts are not a quad-per-primitive triangle list");

    for (uint32_t i = 0u; i < pSizes->uiPrimitiveCount; i++) {
        const tEdPrimitive *pPrimitive = &pBuffers->pPrimitives[i];

        if (pPrimitive->byTopology != ROLLER_ED_TOPOLOGY_TRIANGLE_LIST)
            FAIL("primitive %u is not a triangle list", i);
        if (pPrimitive->uiIndexCount != 6u
                || pPrimitive->uiFirstIndex != i * 6u
                || pPrimitive->uiFirstIndex + pPrimitive->uiIndexCount
                    > pSizes->uiIndexCount)
            FAIL("primitive %u has an out-of-range index range", i);
        if (pPrimitive->uiChunkId != ROLLER_ED_INVALID_CHUNK_ID
                && pPrimitive->uiChunkId >= 500u)
            FAIL("primitive %u has an invalid chunk id", i);
        if (pPrimitive->unContentClass > ROLLER_ED_CONTENT_RUNTIME_SCENERY)
            FAIL("primitive %u has an invalid content class", i);
        if (pPrimitive->uiFrontMaterialId >= pSizes->uiMaterialCount)
            FAIL("primitive %u references material %u of %u", i,
                 pPrimitive->uiFrontMaterialId, pSizes->uiMaterialCount);
        if (pPrimitive->uiBackMaterialId != ROLLER_ED_INVALID_MATERIAL_ID
                && pPrimitive->uiBackMaterialId >= pSizes->uiMaterialCount)
            FAIL("primitive %u references back material %u of %u", i,
                 pPrimitive->uiBackMaterialId, pSizes->uiMaterialCount);
        if (pPrimitive->uiChunkId != ROLLER_ED_INVALID_CHUNK_ID
                && pPrimitive->uiChunkId + 1u > uiChunkBound)
            uiChunkBound = pPrimitive->uiChunkId + 1u;
    }
    if (uiChunkBound == 0u)
        FAIL("no primitive carried a track chunk id");

    for (uint32_t i = 0u; i < pSizes->uiIndexCount; i++) {
        if (pBuffers->puiIndices[i] >= pSizes->uiVertexCount)
            FAIL("index %u points outside the vertex buffer", i);
    }

    for (uint32_t i = 0u; i < pSizes->uiVertexCount; i++) {
        const tEdVertex *pVertex = &pBuffers->pVertices[i];
        float fLengthSq = pVertex->fNormal[0] * pVertex->fNormal[0]
                        + pVertex->fNormal[1] * pVertex->fNormal[1]
                        + pVertex->fNormal[2] * pVertex->fNormal[2];

        /* E4A-S4 generates these; the fill must carry them through. */
        if (fabsf(sqrtf(fLengthSq) - 1.0f) > 0.001f)
            FAIL("vertex %u has a non-unit normal", i);
        /* Material-local UVs stay in range; the atlas transform lives on the
         * material, not in the vertex (AD-7b). */
        if (!(pVertex->fUV[0] >= 0.0f && pVertex->fUV[0] <= 1.0f
              && pVertex->fUV[1] >= 0.0f && pVertex->fUV[1] <= 1.0f))
            FAIL("vertex %u has a material UV outside [0,1]", i);
    }

    for (uint32_t i = 0u; i < pSizes->uiMaterialCount; i++) {
        if (pBuffers->pMaterials[i].uiKind > ROLLER_ED_MATERIAL_SCREEN_DARKEN)
            FAIL("material %u has an invalid kind", i);
    }
    return true;
}

static int buffer_worker(void *pUserData)
{
    tContext *pContext = pUserData;
    tRollerEdInitInfo InitInfo = {
        .uiStructSize = sizeof(InitInfo),
        .uiVersion = ROLLER_ED_INIT_INFO_VERSION,
        .szAssetRoot = pContext->szAssetRoot,
        .ePreferredRenderer = ROLLER_ED_RENDERER_SOFTWARE,
        .uiAllowSoftwareFallback = 1u
    };
    tGeometryBuffers Buffers;
    tEdGeometrySizes Sizes;
    tEdVertex *pWitness = NULL;

    memset(&Buffers, 0, sizeof(Buffers));

    if (RollerEd_Init(&InitInfo) != ROLLER_ED_RESULT_OK) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "RollerEd_Init failed: %s", RollerEd_GetLastError());
        pContext->iResult = 1;
        return pContext->iResult;
    }

    /* Before any load the scene is readable and empty (AD-7d). */
    if (!query_sizes(pContext, &Sizes))
        goto fail;
    if (Sizes.uiSceneState != ROLLER_ED_SCENE_EMPTY
            || Sizes.uiVertexCount != 0u || Sizes.uiPrimitiveCount != 0u) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "an unloaded facade did not report an empty scene");
        goto fail;
    }

    if (RollerEd_LoadTrackFile(pContext->szTrackPath, pContext->szAssetRoot)
            != ROLLER_ED_RESULT_OK) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "RollerEd_LoadTrackFile failed: %s", RollerEd_GetLastError());
        goto fail;
    }

    if (!query_sizes(pContext, &Buffers.Sizes))
        goto fail;
    if (Buffers.Sizes.uiSceneState != ROLLER_ED_SCENE_READY
            || Buffers.Sizes.uiPrimitiveCount == 0u) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "a loaded retail track published no geometry");
        goto fail;
    }
    if (!buffers_allocate(&Buffers)) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "buffer allocation failed");
        goto fail;
    }

    /* One short buffer must refuse the whole call without writing. */
    {
        tEdVertex Sentinel;
        tEdVertex Before;

        memset(&Sentinel, 0x5a, sizeof(Sentinel));
        Before = Sentinel;
        if (RollerEd_FillGeometry(
                Buffers.Sizes.uiGeometryEpoch, &Sentinel, 1u,
                Buffers.puiIndices, Buffers.Sizes.uiIndexCount,
                Buffers.pPrimitives, Buffers.Sizes.uiPrimitiveCount,
                Buffers.pMaterials, Buffers.Sizes.uiMaterialCount)
                    != ROLLER_ED_RESULT_BUFFER_TOO_SMALL
                || memcmp(&Sentinel, &Before, sizeof(Before)) != 0) {
            snprintf(pContext->szError, sizeof(pContext->szError),
                     "an undersized vertex buffer was not refused cleanly");
            goto fail;
        }
    }

    if (buffers_fill(&Buffers) != ROLLER_ED_RESULT_OK) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "RollerEd_FillGeometry failed: %s", RollerEd_GetLastError());
        goto fail;
    }
    if (!check_contents(pContext, &Buffers))
        goto fail;

    /* Filling twice at the same epoch is idempotent. */
    pWitness = calloc(Buffers.Sizes.uiVertexCount, sizeof(*pWitness));
    if (!pWitness) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "witness allocation failed");
        goto fail;
    }
    memcpy(pWitness, Buffers.pVertices,
           (size_t)Buffers.Sizes.uiVertexCount * sizeof(*pWitness));
    if (buffers_fill(&Buffers) != ROLLER_ED_RESULT_OK
            || memcmp(pWitness, Buffers.pVertices,
                      (size_t)Buffers.Sizes.uiVertexCount * sizeof(*pWitness))
                != 0) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "refilling at the same epoch produced different geometry");
        goto fail;
    }

    /* A reload between query and fill must be refused, with the caller's
     * buffers left exactly as they were (AD-7a). */
    if (RollerEd_LoadTrackFile(pContext->szTrackPath, pContext->szAssetRoot)
            != ROLLER_ED_RESULT_OK) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "reload failed: %s", RollerEd_GetLastError());
        goto fail;
    }
    if (buffers_fill(&Buffers) != ROLLER_ED_RESULT_STALE
            || memcmp(pWitness, Buffers.pVertices,
                      (size_t)Buffers.Sizes.uiVertexCount * sizeof(*pWitness))
                != 0) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "a reload between query and fill was not refused cleanly");
        goto fail;
    }

    /* Camera movement must not invalidate authored geometry (AD-7d). */
    {
        tEdCameraState Camera = {
            .uiStructSize = sizeof(Camera),
            .uiVersion = ROLLER_ED_CAMERA_STATE_VERSION,
            .fPosition = { 12345.0f, -6789.0f, 4242.0f },
            .fYawDegrees = 123.5f,
            .fPitchDegrees = -21.25f
        };
        uint32_t uiEpochBeforeCamera;

        if (!query_sizes(pContext, &Sizes))
            goto fail;
        uiEpochBeforeCamera = Sizes.uiGeometryEpoch;
        if (RollerEd_SetCamera(&Camera) != ROLLER_ED_RESULT_OK) {
            snprintf(pContext->szError, sizeof(pContext->szError),
                     "RollerEd_SetCamera failed: %s", RollerEd_GetLastError());
            goto fail;
        }
        if (!query_sizes(pContext, &Sizes))
            goto fail;
        if (Sizes.uiGeometryEpoch != uiEpochBeforeCamera) {
            snprintf(pContext->szError, sizeof(pContext->szError),
                     "moving the camera advanced the geometry epoch");
            goto fail;
        }
    }

    /* Unloading empties the scene and refuses fills. */
    if (RollerEd_UnloadTrack() != ROLLER_ED_RESULT_OK) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "RollerEd_UnloadTrack failed: %s", RollerEd_GetLastError());
        goto fail;
    }
    if (!query_sizes(pContext, &Sizes))
        goto fail;
    if (Sizes.uiSceneState != ROLLER_ED_SCENE_EMPTY
            || Sizes.uiVertexCount != 0u || Sizes.uiPrimitiveCount != 0u
            || Sizes.uiMaterialCount != 0u) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "an unloaded scene still published geometry");
        goto fail;
    }
    if (RollerEd_FillGeometry(
            Sizes.uiGeometryEpoch, Buffers.pVertices,
            Buffers.Sizes.uiVertexCount, Buffers.puiIndices,
            Buffers.Sizes.uiIndexCount, Buffers.pPrimitives,
            Buffers.Sizes.uiPrimitiveCount, Buffers.pMaterials,
            Buffers.Sizes.uiMaterialCount) != ROLLER_ED_RESULT_NO_SCENE) {
        snprintf(pContext->szError, sizeof(pContext->szError),
                 "filling from an unloaded scene was not refused");
        goto fail;
    }

    printf("filled %u vertices, %u indices, %u primitives, and %u materials\n",
           Buffers.Sizes.uiVertexCount, Buffers.Sizes.uiIndexCount,
           Buffers.Sizes.uiPrimitiveCount, Buffers.Sizes.uiMaterialCount);
    goto done;

fail:
    pContext->iResult = 1;
done:
    free(pWitness);
    buffers_release(&Buffers);
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
    pWorker = SDL_CreateThread(buffer_worker, "editor-geometry-buffers",
                               &Context);
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
        fprintf(stderr, "E4A-S5 acceptance failed: %s\n", Context.szError);
        return 1;
    }
    puts("E4A-S5 PASS: query/fill published valid triangle-list geometry over "
         "a retail track, refused stale and undersized calls without writing, "
         "and kept the epoch stable across camera movement");
    return 0;
}
