#ifndef ROLLER_EDITOR_REFERENCE_MESH_H
#define ROLLER_EDITOR_REFERENCE_MESH_H

#include <stddef.h>
#include <stdint.h>

#define ROLLER_ED_REFERENCE_MESH_VERSION 1u
#define ROLLER_ED_REFERENCE_HAS_NORMALS 0x00000001u
#define ROLLER_ED_REFERENCE_WIREFRAME   0x00000002u
#define ROLLER_ED_REFERENCE_TWO_SIDED   0x00000004u

typedef struct
{
    float fPosition[3];
    float fNormal[3];
    float fUV[2];
} tEdReferenceVertex;

typedef struct
{
    uint32_t uiStructSize;
    uint32_t uiVersion;

    const tEdReferenceVertex *pVertices;
    uint32_t uiVertexCount;

    const uint32_t *puiIndices;
    uint32_t uiIndexCount;

    const uint8_t *pbyTextureRGBA;
    uint32_t uiTextureWidth;
    uint32_t uiTextureHeight;
    uint32_t uiTextureRowPitch;

    float fPosition[3];
    float fRotation[3];
    float fScale[3];

    uint32_t uiFlags;
} tEdReferenceMesh;

typedef enum
{
    ED_REFERENCE_MESH_OK = 0,
    ED_REFERENCE_MESH_INVALID_ARGUMENT,
    ED_REFERENCE_MESH_INVALID_VERSION,
    ED_REFERENCE_MESH_INVALID_TOPOLOGY,
    ED_REFERENCE_MESH_INVALID_INDEX,
    ED_REFERENCE_MESH_INVALID_TEXTURE,
    ED_REFERENCE_MESH_OUT_OF_MEMORY,
    ED_REFERENCE_MESH_IO_FAILED,
    ED_REFERENCE_MESH_IMPORT_FAILED
} eEdReferenceMeshResult;

typedef struct
{
    tEdReferenceVertex *pVertices;
    uint32_t uiVertexCount;
    uint32_t *puiIndices;
    uint32_t uiIndexCount;
    uint8_t *pbyTextureRGBA;
    uint32_t uiTextureWidth;
    uint32_t uiTextureHeight;
    uint32_t uiTextureRowPitch;
    float fPosition[3];
    float fRotation[3];
    float fScale[3];
    uint32_t uiFlags;
} tEdReferenceMeshState;

typedef struct
{
    tEdReferenceVertex *pVertices;
    uint32_t uiVertexCount;
    uint32_t *puiIndices;
    uint32_t uiIndexCount;
    uint32_t uiFlags;
} tEdReferenceMeshImport;

void ed_reference_mesh_state_init(tEdReferenceMeshState *pState);
void ed_reference_mesh_state_dispose(tEdReferenceMeshState *pState);

/*
 * Copies every pointer-backed field before returning. Allocation, validation,
 * normal generation, and texture repacking happen in temporary storage; a
 * failed replacement leaves pState byte-for-byte unchanged.
 * NULL vertices or a zero vertex count clears the state.
 */
eEdReferenceMeshResult ed_reference_mesh_replace(
    tEdReferenceMeshState *pState,
    const tEdReferenceMesh *pMesh,
    char *szError,
    size_t uiErrorCapacity);

/*
 * Minimal Wavefront OBJ importer used by the post-WhipLib feasibility spike.
 * It accepts triangle-list v/vt[/vn] faces and expands face corners into
 * indexed AD-13 reference vertices. The caller owns the returned import.
 */
eEdReferenceMeshResult ed_reference_mesh_import_obj(
    const char *szPath,
    tEdReferenceMeshImport *pImport,
    char *szError,
    size_t uiErrorCapacity);
void ed_reference_mesh_import_init(tEdReferenceMeshImport *pImport);
void ed_reference_mesh_import_dispose(tEdReferenceMeshImport *pImport);

const char *ed_reference_mesh_result_name(eEdReferenceMeshResult eResult);

#endif
