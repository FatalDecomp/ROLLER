#include "editor_reference_mesh.h"

#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* M_PI is not in standard C; the reference transform needs degrees only. */
#define ED_REFERENCE_DEGREES_TO_RADIANS 0.017453292519943295

typedef struct
{
    float fValue[3];
} tEdObjPosition;

typedef struct
{
    float fValue[2];
} tEdObjUV;

typedef struct
{
    float fValue[3];
} tEdObjNormal;

static void ed_reference_error(char *szError,
                               size_t uiErrorCapacity,
                               const char *szMessage)
{
    if (!szError || uiErrorCapacity == 0)
        return;
    snprintf(szError, uiErrorCapacity, "%s",
             szMessage ? szMessage : "");
}

static int ed_reference_size_multiply(size_t uiFirst,
                                      size_t uiSecond,
                                      size_t *puiResult)
{
    if (uiFirst != 0 && uiSecond > SIZE_MAX / uiFirst)
        return 0;
    *puiResult = uiFirst * uiSecond;
    return 1;
}

static void ed_reference_mesh_generate_normals(
    tEdReferenceVertex *pVertices,
    const uint32_t *puiIndices,
    uint32_t uiIndexCount)
{
    for (uint32_t i = 0; i < uiIndexCount; i += 3u) {
        tEdReferenceVertex *pFirst = &pVertices[puiIndices[i]];
        tEdReferenceVertex *pSecond = &pVertices[puiIndices[i + 1u]];
        tEdReferenceVertex *pThird = &pVertices[puiIndices[i + 2u]];
        float afFirstEdge[3];
        float afSecondEdge[3];
        float afNormal[3];
        float fLength;

        for (size_t iAxis = 0; iAxis < 3u; iAxis++) {
            afFirstEdge[iAxis] =
                pSecond->fPosition[iAxis] - pFirst->fPosition[iAxis];
            afSecondEdge[iAxis] =
                pThird->fPosition[iAxis] - pFirst->fPosition[iAxis];
        }
        afNormal[0] = afFirstEdge[1] * afSecondEdge[2]
                    - afFirstEdge[2] * afSecondEdge[1];
        afNormal[1] = afFirstEdge[2] * afSecondEdge[0]
                    - afFirstEdge[0] * afSecondEdge[2];
        afNormal[2] = afFirstEdge[0] * afSecondEdge[1]
                    - afFirstEdge[1] * afSecondEdge[0];
        fLength = sqrtf(afNormal[0] * afNormal[0]
                      + afNormal[1] * afNormal[1]
                      + afNormal[2] * afNormal[2]);
        if (fLength > 0.000001f) {
            for (size_t iAxis = 0; iAxis < 3u; iAxis++)
                afNormal[iAxis] /= fLength;
        }
        for (size_t iAxis = 0; iAxis < 3u; iAxis++) {
            pFirst->fNormal[iAxis] += afNormal[iAxis];
            pSecond->fNormal[iAxis] += afNormal[iAxis];
            pThird->fNormal[iAxis] += afNormal[iAxis];
        }
    }

    for (uint32_t i = 0; i < uiIndexCount; i++) {
        tEdReferenceVertex *pVertex = &pVertices[puiIndices[i]];
        float fLength = sqrtf(
            pVertex->fNormal[0] * pVertex->fNormal[0]
          + pVertex->fNormal[1] * pVertex->fNormal[1]
          + pVertex->fNormal[2] * pVertex->fNormal[2]);

        if (fLength > 0.000001f) {
            pVertex->fNormal[0] /= fLength;
            pVertex->fNormal[1] /= fLength;
            pVertex->fNormal[2] /= fLength;
        }
    }
}

void ed_reference_mesh_state_init(tEdReferenceMeshState *pState)
{
    if (pState)
        memset(pState, 0, sizeof(*pState));
}

void ed_reference_mesh_state_dispose(tEdReferenceMeshState *pState)
{
    if (!pState)
        return;
    free(pState->pVertices);
    free(pState->puiIndices);
    free(pState->pbyTextureRGBA);
    memset(pState, 0, sizeof(*pState));
}

eEdReferenceMeshResult ed_reference_mesh_replace(
    tEdReferenceMeshState *pState,
    const tEdReferenceMesh *pMesh,
    char *szError,
    size_t uiErrorCapacity)
{
    tEdReferenceMeshState Staged;
    size_t uiVertexBytes;
    size_t uiIndexBytes;
    size_t uiTextureBytes = 0;
    uint32_t uiEffectiveIndexCount;

    if (!pState || !pMesh) {
        ed_reference_error(szError, uiErrorCapacity,
                           "reference mesh argument is NULL");
        return ED_REFERENCE_MESH_INVALID_ARGUMENT;
    }
    if (pMesh->uiStructSize != sizeof(*pMesh)
            || pMesh->uiVersion != ROLLER_ED_REFERENCE_MESH_VERSION) {
        ed_reference_error(szError, uiErrorCapacity,
                           "reference mesh size/version is unsupported");
        return ED_REFERENCE_MESH_INVALID_VERSION;
    }
    if (!pMesh->pVertices || pMesh->uiVertexCount == 0u) {
        ed_reference_mesh_state_dispose(pState);
        ed_reference_error(szError, uiErrorCapacity, "");
        return ED_REFERENCE_MESH_OK;
    }
    if (pMesh->puiIndices) {
        if (pMesh->uiIndexCount == 0u
                || pMesh->uiIndexCount % 3u != 0u) {
            ed_reference_error(szError, uiErrorCapacity,
                               "reference index list is not triangles");
            return ED_REFERENCE_MESH_INVALID_TOPOLOGY;
        }
        uiEffectiveIndexCount = pMesh->uiIndexCount;
    } else {
        if (pMesh->uiIndexCount != 0u
                || pMesh->uiVertexCount % 3u != 0u) {
            ed_reference_error(szError, uiErrorCapacity,
                               "reference vertex list is not triangles");
            return ED_REFERENCE_MESH_INVALID_TOPOLOGY;
        }
        uiEffectiveIndexCount = pMesh->uiVertexCount;
    }
    if (!ed_reference_size_multiply(
            pMesh->uiVertexCount, sizeof(*pMesh->pVertices),
            &uiVertexBytes)
            || !ed_reference_size_multiply(
                uiEffectiveIndexCount, sizeof(uint32_t),
                &uiIndexBytes)) {
        ed_reference_error(szError, uiErrorCapacity,
                           "reference mesh size overflow");
        return ED_REFERENCE_MESH_INVALID_ARGUMENT;
    }
    if (pMesh->pbyTextureRGBA) {
        size_t uiMinimumPitch;
        size_t uiSourceBytes;

        if (pMesh->uiTextureWidth == 0u
                || pMesh->uiTextureHeight == 0u
                || !ed_reference_size_multiply(
                    pMesh->uiTextureWidth, 4u, &uiMinimumPitch)
                || pMesh->uiTextureRowPitch < uiMinimumPitch
                || !ed_reference_size_multiply(
                    pMesh->uiTextureRowPitch,
                    pMesh->uiTextureHeight, &uiSourceBytes)
                || !ed_reference_size_multiply(
                    uiMinimumPitch, pMesh->uiTextureHeight,
                    &uiTextureBytes)
                || uiSourceBytes < uiTextureBytes) {
            ed_reference_error(szError, uiErrorCapacity,
                               "reference texture layout is invalid");
            return ED_REFERENCE_MESH_INVALID_TEXTURE;
        }
    } else if (pMesh->uiTextureWidth != 0u
            || pMesh->uiTextureHeight != 0u
            || pMesh->uiTextureRowPitch != 0u) {
        ed_reference_error(szError, uiErrorCapacity,
                           "reference texture metadata has no pixels");
        return ED_REFERENCE_MESH_INVALID_TEXTURE;
    }

    ed_reference_mesh_state_init(&Staged);
    Staged.pVertices = malloc(uiVertexBytes);
    Staged.puiIndices = malloc(uiIndexBytes);
    if (!Staged.pVertices || !Staged.puiIndices) {
        ed_reference_mesh_state_dispose(&Staged);
        ed_reference_error(szError, uiErrorCapacity,
                           "reference mesh allocation failed");
        return ED_REFERENCE_MESH_OUT_OF_MEMORY;
    }
    memcpy(Staged.pVertices, pMesh->pVertices, uiVertexBytes);
    Staged.uiVertexCount = pMesh->uiVertexCount;
    Staged.uiIndexCount = uiEffectiveIndexCount;
    if (pMesh->puiIndices) {
        memcpy(Staged.puiIndices, pMesh->puiIndices, uiIndexBytes);
    } else {
        for (uint32_t i = 0; i < uiEffectiveIndexCount; i++)
            Staged.puiIndices[i] = i;
    }
    for (uint32_t i = 0; i < uiEffectiveIndexCount; i++) {
        if (Staged.puiIndices[i] >= Staged.uiVertexCount) {
            ed_reference_mesh_state_dispose(&Staged);
            ed_reference_error(szError, uiErrorCapacity,
                               "reference index is outside the vertex array");
            return ED_REFERENCE_MESH_INVALID_INDEX;
        }
    }
    if (pMesh->pbyTextureRGBA) {
        size_t uiTightPitch = (size_t)pMesh->uiTextureWidth * 4u;

        Staged.pbyTextureRGBA = malloc(uiTextureBytes);
        if (!Staged.pbyTextureRGBA) {
            ed_reference_mesh_state_dispose(&Staged);
            ed_reference_error(szError, uiErrorCapacity,
                               "reference texture allocation failed");
            return ED_REFERENCE_MESH_OUT_OF_MEMORY;
        }
        for (uint32_t iRow = 0; iRow < pMesh->uiTextureHeight; iRow++) {
            memcpy(Staged.pbyTextureRGBA + (size_t)iRow * uiTightPitch,
                   pMesh->pbyTextureRGBA
                       + (size_t)iRow * pMesh->uiTextureRowPitch,
                   uiTightPitch);
        }
        Staged.uiTextureWidth = pMesh->uiTextureWidth;
        Staged.uiTextureHeight = pMesh->uiTextureHeight;
        Staged.uiTextureRowPitch = (uint32_t)uiTightPitch;
    }
    memcpy(Staged.fPosition, pMesh->fPosition, sizeof(Staged.fPosition));
    memcpy(Staged.fRotation, pMesh->fRotation, sizeof(Staged.fRotation));
    memcpy(Staged.fScale, pMesh->fScale, sizeof(Staged.fScale));
    Staged.uiFlags = pMesh->uiFlags;
    if (!(Staged.uiFlags & ROLLER_ED_REFERENCE_HAS_NORMALS)) {
        for (uint32_t i = 0; i < Staged.uiVertexCount; i++)
            memset(Staged.pVertices[i].fNormal, 0,
                   sizeof(Staged.pVertices[i].fNormal));
        ed_reference_mesh_generate_normals(
            Staged.pVertices, Staged.puiIndices, Staged.uiIndexCount);
        Staged.uiFlags |= ROLLER_ED_REFERENCE_HAS_NORMALS;
    }

    ed_reference_mesh_state_dispose(pState);
    *pState = Staged;
    ed_reference_error(szError, uiErrorCapacity, "");
    return ED_REFERENCE_MESH_OK;
}

static int ed_obj_grow(void **ppData,
                       size_t uiElementSize,
                       size_t *puiCapacity,
                       size_t uiRequired)
{
    size_t uiCapacity = *puiCapacity ? *puiCapacity : 16u;
    void *pNewData;

    while (uiCapacity < uiRequired) {
        if (uiCapacity > SIZE_MAX / 2u)
            return 0;
        uiCapacity *= 2u;
    }
    if (uiCapacity > SIZE_MAX / uiElementSize)
        return 0;
    pNewData = realloc(*ppData, uiCapacity * uiElementSize);
    if (!pNewData)
        return 0;
    *ppData = pNewData;
    *puiCapacity = uiCapacity;
    return 1;
}

static int ed_obj_parse_index(const char *szToken,
                              size_t uiPositionCount,
                              size_t uiUVCount,
                              size_t uiNormalCount,
                              size_t *puiPosition,
                              size_t *puiUV,
                              size_t *puiNormal,
                              int *pbHasNormal)
{
    char *szEnd;
    long lPosition;
    long lUV = 0;
    long lNormal = 0;

    errno = 0;
    lPosition = strtol(szToken, &szEnd, 10);
    if (errno == ERANGE || szEnd == szToken || lPosition <= 0
            || (size_t)lPosition > uiPositionCount)
        return 0;
    if (*szEnd == '/') {
        const char *szUV = szEnd + 1;

        if (*szUV != '/') {
            lUV = strtol(szUV, &szEnd, 10);
            if (szEnd == szUV || lUV <= 0
                    || (size_t)lUV > uiUVCount)
                return 0;
        } else {
            szEnd = (char *)szUV;
        }
        if (*szEnd == '/') {
            const char *szNormal = szEnd + 1;
            lNormal = strtol(szNormal, &szEnd, 10);
            if (szEnd == szNormal || lNormal <= 0
                    || (size_t)lNormal > uiNormalCount)
                return 0;
            *pbHasNormal = 1;
        }
    }
    if (*szEnd != '\0')
        return 0;
    *puiPosition = (size_t)lPosition - 1u;
    *puiUV = lUV > 0 ? (size_t)lUV - 1u : SIZE_MAX;
    *puiNormal = lNormal > 0 ? (size_t)lNormal - 1u : SIZE_MAX;
    return 1;
}

eEdReferenceMeshResult ed_reference_mesh_import_obj(
    const char *szPath,
    tEdReferenceMeshImport *pImport,
    char *szError,
    size_t uiErrorCapacity)
{
    FILE *pFile;
    tEdObjPosition *pPositions = NULL;
    tEdObjUV *pUVs = NULL;
    tEdObjNormal *pNormals = NULL;
    size_t uiPositionCount = 0, uiPositionCapacity = 0;
    size_t uiUVCount = 0, uiUVCapacity = 0;
    size_t uiNormalCount = 0, uiNormalCapacity = 0;
    size_t uiVertexCapacity = 0, uiIndexCapacity = 0;
    char szLine[1024];
    uint32_t uiLine = 0;
    int bAnyNormal = 0;
    int bAllNormals = 1;
    eEdReferenceMeshResult eResult = ED_REFERENCE_MESH_IMPORT_FAILED;
    tEdReferenceMeshImport Imported;

    if (!szPath || !pImport) {
        ed_reference_error(szError, uiErrorCapacity,
                           "OBJ import argument is NULL");
        return ED_REFERENCE_MESH_INVALID_ARGUMENT;
    }
    memset(&Imported, 0, sizeof(Imported));
    pFile = fopen(szPath, "rb");
    if (!pFile) {
        ed_reference_error(szError, uiErrorCapacity,
                           "unable to open reference OBJ");
        return ED_REFERENCE_MESH_IO_FAILED;
    }

    while (fgets(szLine, sizeof(szLine), pFile)) {
        char *szToken;
        uiLine++;
        if (!strchr(szLine, '\n') && !feof(pFile)) {
            ed_reference_error(szError, uiErrorCapacity,
                               "OBJ line exceeds importer limit");
            goto cleanup;
        }
        szToken = strtok(szLine, " \t\r\n");
        if (!szToken || szToken[0] == '#')
            continue;
        if (strcmp(szToken, "v") == 0) {
            tEdObjPosition Position;

            for (size_t i = 0; i < 3u; i++) {
                char *szCoordinate = strtok(NULL, " \t\r\n");
                char *szEnd;
                if (!szCoordinate)
                    goto malformed_line;
                Position.fValue[i] = strtof(szCoordinate, &szEnd);
                if (szEnd == szCoordinate || *szEnd)
                    goto malformed_line;
            }
            if (!ed_obj_grow(
                    (void **)&pPositions, sizeof(*pPositions),
                    &uiPositionCapacity, uiPositionCount + 1u)) {
                eResult = ED_REFERENCE_MESH_OUT_OF_MEMORY;
                goto cleanup;
            }
            pPositions[uiPositionCount++] = Position;
        } else if (strcmp(szToken, "vt") == 0) {
            tEdObjUV UV;

            for (size_t i = 0; i < 2u; i++) {
                char *szCoordinate = strtok(NULL, " \t\r\n");
                char *szEnd;
                if (!szCoordinate)
                    goto malformed_line;
                UV.fValue[i] = strtof(szCoordinate, &szEnd);
                if (szEnd == szCoordinate || *szEnd)
                    goto malformed_line;
            }
            if (!ed_obj_grow(
                    (void **)&pUVs, sizeof(*pUVs),
                    &uiUVCapacity, uiUVCount + 1u)) {
                eResult = ED_REFERENCE_MESH_OUT_OF_MEMORY;
                goto cleanup;
            }
            pUVs[uiUVCount++] = UV;
        } else if (strcmp(szToken, "vn") == 0) {
            tEdObjNormal Normal;

            for (size_t i = 0; i < 3u; i++) {
                char *szCoordinate = strtok(NULL, " \t\r\n");
                char *szEnd;
                if (!szCoordinate)
                    goto malformed_line;
                Normal.fValue[i] = strtof(szCoordinate, &szEnd);
                if (szEnd == szCoordinate || *szEnd)
                    goto malformed_line;
            }
            if (!ed_obj_grow(
                    (void **)&pNormals, sizeof(*pNormals),
                    &uiNormalCapacity, uiNormalCount + 1u)) {
                eResult = ED_REFERENCE_MESH_OUT_OF_MEMORY;
                goto cleanup;
            }
            pNormals[uiNormalCount++] = Normal;
        } else if (strcmp(szToken, "f") == 0) {
            for (size_t iCorner = 0; iCorner < 3u; iCorner++) {
                tEdReferenceVertex Vertex;
                size_t uiPosition;
                size_t uiUV;
                size_t uiNormal;
                int bHasNormal = 0;
                char *szCorner = strtok(NULL, " \t\r\n");

                if (!szCorner
                        || !ed_obj_parse_index(
                            szCorner, uiPositionCount, uiUVCount,
                            uiNormalCount, &uiPosition, &uiUV, &uiNormal,
                            &bHasNormal))
                    goto malformed_line;
                memset(&Vertex, 0, sizeof(Vertex));
                memcpy(Vertex.fPosition, pPositions[uiPosition].fValue,
                       sizeof(Vertex.fPosition));
                if (uiUV != SIZE_MAX)
                    memcpy(Vertex.fUV, pUVs[uiUV].fValue,
                           sizeof(Vertex.fUV));
                if (uiNormal != SIZE_MAX)
                    memcpy(Vertex.fNormal, pNormals[uiNormal].fValue,
                           sizeof(Vertex.fNormal));
                bAnyNormal |= bHasNormal;
                bAllNormals &= bHasNormal;
                if (!ed_obj_grow(
                        (void **)&Imported.pVertices,
                        sizeof(*Imported.pVertices), &uiVertexCapacity,
                        (size_t)Imported.uiVertexCount + 1u)
                        || !ed_obj_grow(
                            (void **)&Imported.puiIndices,
                            sizeof(*Imported.puiIndices), &uiIndexCapacity,
                            (size_t)Imported.uiIndexCount + 1u)) {
                    eResult = ED_REFERENCE_MESH_OUT_OF_MEMORY;
                    goto cleanup;
                }
                Imported.pVertices[Imported.uiVertexCount] = Vertex;
                Imported.puiIndices[Imported.uiIndexCount] =
                    Imported.uiVertexCount;
                Imported.uiVertexCount++;
                Imported.uiIndexCount++;
            }
            if (strtok(NULL, " \t\r\n") != NULL)
                goto malformed_line;
        }
        continue;

malformed_line:
        if (szError && uiErrorCapacity > 0)
            snprintf(szError, uiErrorCapacity,
                     "malformed OBJ at line %u", uiLine);
        goto cleanup;
    }
    if (ferror(pFile)) {
        eResult = ED_REFERENCE_MESH_IO_FAILED;
        ed_reference_error(szError, uiErrorCapacity,
                           "reference OBJ read failed");
        goto cleanup;
    }
    if (Imported.uiIndexCount == 0u
            || Imported.uiIndexCount % 3u != 0u
            || (bAnyNormal && !bAllNormals)) {
        ed_reference_error(szError, uiErrorCapacity,
                           "OBJ must contain triangles with consistent normals");
        goto cleanup;
    }
    if (bAllNormals)
        Imported.uiFlags |= ROLLER_ED_REFERENCE_HAS_NORMALS;

    ed_reference_mesh_import_dispose(pImport);
    *pImport = Imported;
    memset(&Imported, 0, sizeof(Imported));
    ed_reference_error(szError, uiErrorCapacity, "");
    eResult = ED_REFERENCE_MESH_OK;

cleanup:
    fclose(pFile);
    free(pPositions);
    free(pUVs);
    free(pNormals);
    ed_reference_mesh_import_dispose(&Imported);
    return eResult;
}

void ed_reference_mesh_import_dispose(tEdReferenceMeshImport *pImport)
{
    if (!pImport)
        return;
    free(pImport->pVertices);
    free(pImport->puiIndices);
    memset(pImport, 0, sizeof(*pImport));
}

void ed_reference_mesh_import_init(tEdReferenceMeshImport *pImport)
{
    if (pImport)
        memset(pImport, 0, sizeof(*pImport));
}

const char *ed_reference_mesh_result_name(eEdReferenceMeshResult eResult)
{
    static const char *const aszNames[] = {
        "ok",
        "invalid argument",
        "invalid version",
        "invalid topology",
        "invalid index",
        "invalid texture",
        "out of memory",
        "I/O failed",
        "import failed"
    };

    if ((unsigned int)eResult
            >= sizeof(aszNames) / sizeof(aszNames[0]))
        return "unknown";
    return aszNames[eResult];
}

/*
 * E3A-S7. The facade owns exactly one reference mesh, so it lives here beside
 * the copy layer that fills it rather than in a separate module.
 */
static tEdReferenceMeshState s_Current;
static bool s_bCurrentInitialized;

static void reference_mesh_ensure_initialized(void)
{
    if (s_bCurrentInitialized)
        return;
    ed_reference_mesh_state_init(&s_Current);
    s_bCurrentInitialized = true;
}

eEdReferenceMeshResult ed_reference_mesh_set_current(
    const tEdReferenceMesh *pMesh,
    char *szError,
    size_t uiErrorCapacity)
{
    reference_mesh_ensure_initialized();
    return ed_reference_mesh_replace(&s_Current, pMesh, szError,
                                     uiErrorCapacity);
}

void ed_reference_mesh_reset_current(void)
{
    if (!s_bCurrentInitialized)
        return;
    ed_reference_mesh_state_dispose(&s_Current);
    ed_reference_mesh_state_init(&s_Current);
}

uint32_t ed_reference_mesh_triangle_count(void)
{
    if (!s_bCurrentInitialized || !s_Current.puiIndices)
        return 0u;
    /* The copy layer synthesizes indices for a non-indexed mesh and refuses a
     * count that is not a multiple of three, so this division is exact. */
    return s_Current.uiIndexCount / 3u;
}

bool ed_reference_mesh_wireframe(void)
{
    return s_bCurrentInitialized
        && (s_Current.uiFlags & ROLLER_ED_REFERENCE_WIREFRAME) != 0u;
}

bool ed_reference_mesh_world_triangle(uint32_t uiTriangle,
                                      float afTriangleOut[3][3])
{
    float fYaw;
    float fPitch;
    float fRoll;
    float afRotation[3][3];

    if (!afTriangleOut || uiTriangle >= ed_reference_mesh_triangle_count())
        return false;

    /*
     * Scale, then rotate, then translate (AD-13). The rotation is yaw, then
     * pitch, then roll -- the order the legacy editor composed its matrices
     * in (TrackPreview's translate * roll * pitch * yaw * scale), carried over
     * to ROLLER's axes: world +Z is up, so yaw turns about Z, pitch tilts
     * about Y, and roll banks about X (ADR 0003). The editor's Y-up
     * yaw-about-Y / pitch-about-X / roll-about-Z is the same three rotations
     * against the same three body axes.
     */
    fYaw = s_Current.fRotation[0] * (float)ED_REFERENCE_DEGREES_TO_RADIANS;
    fPitch = s_Current.fRotation[1] * (float)ED_REFERENCE_DEGREES_TO_RADIANS;
    fRoll = s_Current.fRotation[2] * (float)ED_REFERENCE_DEGREES_TO_RADIANS;
    {
        const float fCosYaw = cosf(fYaw);
        const float fSinYaw = sinf(fYaw);
        const float fCosPitch = cosf(fPitch);
        const float fSinPitch = sinf(fPitch);
        const float fCosRoll = cosf(fRoll);
        const float fSinRoll = sinf(fRoll);

        /* Rx(roll) * Ry(pitch) * Rz(yaw), written out so the composition is
         * readable rather than three matrix multiplies. */
        afRotation[0][0] = fCosPitch * fCosYaw;
        afRotation[0][1] = -fCosPitch * fSinYaw;
        afRotation[0][2] = fSinPitch;
        afRotation[1][0] = fSinRoll * fSinPitch * fCosYaw + fCosRoll * fSinYaw;
        afRotation[1][1] = -fSinRoll * fSinPitch * fSinYaw + fCosRoll * fCosYaw;
        afRotation[1][2] = -fSinRoll * fCosPitch;
        afRotation[2][0] = -fCosRoll * fSinPitch * fCosYaw + fSinRoll * fSinYaw;
        afRotation[2][1] = fCosRoll * fSinPitch * fSinYaw + fSinRoll * fCosYaw;
        afRotation[2][2] = fCosRoll * fCosPitch;
    }

    for (uint32_t uiCorner = 0u; uiCorner < 3u; uiCorner++) {
        const uint32_t uiIndex =
            s_Current.puiIndices[uiTriangle * 3u + uiCorner];
        const float *pfLocal;
        float afScaled[3];

        if (uiIndex >= s_Current.uiVertexCount)
            return false;
        pfLocal = s_Current.pVertices[uiIndex].fPosition;
        for (uint32_t i = 0; i < 3u; i++)
            afScaled[i] = pfLocal[i] * s_Current.fScale[i];
        for (uint32_t i = 0; i < 3u; i++) {
            afTriangleOut[uiCorner][i] = s_Current.fPosition[i]
                + afRotation[i][0] * afScaled[0]
                + afRotation[i][1] * afScaled[1]
                + afRotation[i][2] * afScaled[2];
        }
    }
    return true;
}
