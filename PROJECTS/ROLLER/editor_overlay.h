#ifndef ROLLER_EDITOR_OVERLAY_H
#define ROLLER_EDITOR_OVERLAY_H

#include "editor_api.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * Internal editor overlay state (E3A-S1).  The facade validates and copies the
 * caller's tEdOverlayState during RollerEd_SetOverlayState; the renderer reads
 * the copy kept here.  Overlay state is a view setting like the camera: it is
 * settable before a scene exists, survives loads and unloads, and changing it
 * never advances the geometry epoch (AD-7d), so E4A-S5's one-extraction-per-
 * epoch cache stays valid across every toggle.
 *
 * The overlays themselves are E3A-S2 through E3A-S7.  This module only owns
 * the state and the queries those stories read it through.
 */

/*
 * Every overlay flag API version 1 defines.  Bits outside this mask are
 * refused rather than stored, so a host built against a later header cannot
 * silently receive a subset of the behaviour it asked for.
 */
#define ROLLER_ED_OVERLAY_KNOWN_FLAGS \
    (ROLLER_ED_OVERLAY_SHOW_SURFACES \
     | ROLLER_ED_OVERLAY_SHOW_WIREFRAME \
     | ROLLER_ED_OVERLAY_HIGHLIGHT_SELECTION \
     | ROLLER_ED_OVERLAY_SHOW_AI_LINES \
     | ROLLER_ED_OVERLAY_SHOW_CENTER_LINE \
     | ROLLER_ED_OVERLAY_SHOW_ENVIRONMENT_FLOOR \
     | ROLLER_ED_OVERLAY_SHOW_AUDIO_MARKERS \
     | ROLLER_ED_OVERLAY_SHOW_STUNT_MARKERS \
     | ROLLER_ED_OVERLAY_SHOW_TEST_CAR \
     | ROLLER_ED_OVERLAY_SHOW_REFERENCE_MESH)

/*
 * The default reproduces the E1-S6 track-only view exactly: surfaces are drawn
 * and nothing else is, so a host that never calls RollerEd_SetOverlayState
 * keeps the frame it had before this story existed.
 */
#define ROLLER_ED_OVERLAY_DEFAULT_FLAGS ROLLER_ED_OVERLAY_SHOW_SURFACES

void roller_ed_overlay_reset(void);
void roller_ed_overlay_set(const tEdOverlayState *pState);
/*
 * Publishes the core's view of the stored state: the caller's flags and
 * selection endpoints verbatim -- including a reversed range -- under this
 * build's v1 size and version rather than whatever header size the caller
 * happened to send.
 */
void roller_ed_overlay_get(tEdOverlayState *pStateOut);

uint32_t roller_ed_overlay_flags(void);
bool roller_ed_overlay_enabled(uint32_t uiFlag);
/*
 * True when the highlight is on and both endpoints are real chunk ids, in
 * which case the ordered range is written out.  ROLLER_ED_INVALID_CHUNK_ID is
 * how the editor says "nothing is selected" without clearing the flag, and a
 * reversed range is ordered here so consumers never have to.
 */
bool roller_ed_overlay_selection_range(uint32_t *puiFirstChunk,
                                       uint32_t *puiLastChunk);

#endif
