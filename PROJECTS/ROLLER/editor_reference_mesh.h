#ifndef ROLLER_EDITOR_REFERENCE_MESH_H
#define ROLLER_EDITOR_REFERENCE_MESH_H

#include "editor_api.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

/*
 * E3A-S7. The one reference mesh the facade owns, and its world-space
 * geometry.
 *
 * The mesh is editor furniture like the helpers: it never reaches the
 * canonical emitter, so no exporter can see it (AD-6d), and it carries no
 * chunk identity. It is drawn through the same world-quad path the track uses,
 * which is what depth-composes it against the scene -- a Qt overlay painted on
 * top is explicitly not acceptable, and this is why.
 */

/*
 * Replaces the current mesh, or clears it when pMesh has no vertices. A failed
 * replacement leaves the previous mesh intact (AD-13), because the copy is
 * staged before anything is freed.
 */
eEdReferenceMeshResult ed_reference_mesh_set_current(
    const tEdReferenceMesh *pMesh,
    char *szError,
    size_t uiErrorCapacity);
void ed_reference_mesh_reset_current(void);

/* Triangles after the mesh's own transform, ready to draw. */
uint32_t ed_reference_mesh_triangle_count(void);
bool ed_reference_mesh_world_triangle(uint32_t uiTriangle,
                                      float afTriangleOut[3][3]);
/* True when the host asked for the mesh to be drawn as edges. */
bool ed_reference_mesh_wireframe(void);

#endif
