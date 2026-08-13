#ifndef ROLLER_EDITOR_LEGACY_SCENE_H
#define ROLLER_EDITOR_LEGACY_SCENE_H

#include "editor_api.h"
#include "editor_track_loader.h"

#include <stddef.h>
#include <stdint.h>

/* Internal adapter between the stable editor facade and the legacy global
 * load/render graph.  Kept narrow so facade lifecycle tests can replace it
 * without linking the complete renderer. */
eRollerEdResult roller_ed_legacy_scene_install(
    const char *szTrackPath,
    const tEdTrackStage *pStage,
    const char *szDocumentAssetRoot,
    const char *szFallbackAssetRoot,
    eRollerEdRenderer ePreferredRenderer,
    uint32_t uiAllowSoftwareFallback,
    char *szError,
    size_t uiErrorCapacity);

eRollerEdResult roller_ed_legacy_scene_set_camera(
    const tEdCameraState *pCamera,
    char *szError,
    size_t uiErrorCapacity);

eRollerEdResult roller_ed_legacy_scene_set_overlay_state(
    const tEdOverlayState *pState,
    char *szError,
    size_t uiErrorCapacity);

eRollerEdResult roller_ed_legacy_scene_advance_stunts(
    uint32_t uiTicks,
    char *szError,
    size_t uiErrorCapacity);

/* E3A-S7. Copies the mesh into the core's single reference-mesh slot. A
 * failed replacement leaves the previous mesh intact (AD-13). */
eRollerEdResult roller_ed_legacy_scene_set_reference_mesh(
    const tEdReferenceMesh *pMesh,
    char *szError,
    size_t uiErrorCapacity);

eRollerEdResult roller_ed_legacy_scene_render(
    uint8_t *pbyPixels,
    uint32_t uiBufferSize,
    uint32_t uiRowPitch,
    uint32_t uiWidth,
    uint32_t uiHeight,
    char *szError,
    size_t uiErrorCapacity);

uint32_t roller_ed_legacy_scene_get_available_renderers(void);
eRollerEdResult roller_ed_legacy_scene_select_renderer(
    eRollerEdRenderer eKind,
    char *szError,
    size_t uiErrorCapacity);
eRollerEdResult roller_ed_legacy_scene_set_graphics_settings(
    const tEdGraphicsSettings *pSettings,
    char *szError,
    size_t uiErrorCapacity);

/* Copies authoritative InitTowers results and their loaded chunk anchors. */
uint32_t roller_ed_legacy_scene_tower_count(void);
void roller_ed_legacy_scene_query_tower(
    uint32_t uiTowerIndex, tEdTowerInfo *pInfoOut);

/*
 * One extraction of the loaded scene's authored geometry, owned by whoever
 * called extract and released through the matching call. The facade caches
 * one of these per geometry epoch and copies out of it, so no core-owned
 * pointer ever reaches a caller.
 */
typedef struct
{
    tEdVertex *pVertices;
    uint32_t *puiIndices;
    tEdPrimitive *pPrimitives;
    tEdMaterial *pMaterials;
    uint32_t uiVertexCount;
    uint32_t uiIndexCount;
    uint32_t uiPrimitiveCount;
    uint32_t uiMaterialCount;
} tEdGeometryExtract;

eRollerEdResult roller_ed_legacy_scene_extract_geometry(
    tEdGeometryExtract *pExtract,
    char *szError,
    size_t uiErrorCapacity);

void roller_ed_legacy_scene_release_geometry(tEdGeometryExtract *pExtract);

void roller_ed_legacy_scene_unload(void);
void roller_ed_legacy_scene_shutdown(void);

#endif
