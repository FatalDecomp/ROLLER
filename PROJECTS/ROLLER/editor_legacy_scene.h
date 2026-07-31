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

void roller_ed_legacy_scene_unload(void);
void roller_ed_legacy_scene_shutdown(void);

#endif
