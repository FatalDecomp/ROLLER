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
     | ROLLER_ED_OVERLAY_SHOW_AUDIO_MARKERS \
     | ROLLER_ED_OVERLAY_SHOW_STUNT_MARKERS \
     | ROLLER_ED_OVERLAY_SHOW_TEST_CAR \
     | ROLLER_ED_OVERLAY_SHOW_REFERENCE_MESH \
     | ROLLER_ED_OVERLAY_TEST_CAR_MILLION_PLUS \
     | ROLLER_ED_OVERLAY_TEST_CAR_ADVANCED \
     | ROLLER_ED_OVERLAY_SHOW_TOWER_MARKERS \
     | ROLLER_ED_OVERLAY_DETACH_LAST)

/*
 * The defaults reproduce the E1-S6 track-only view exactly: every surface
 * class is drawn solid, nothing is drawn as wireframe, and no other overlay is
 * active -- so a host that never calls RollerEd_SetOverlayState keeps the
 * frame it had before this story existed.
 */
#define ROLLER_ED_OVERLAY_DEFAULT_FLAGS ROLLER_ED_OVERLAY_SHOW_SURFACES
#define ROLLER_ED_OVERLAY_DEFAULT_SURFACE_CLASS_MASK \
    ROLLER_ED_OVERLAY_ALL_SURFACE_CLASSES
#define ROLLER_ED_OVERLAY_DEFAULT_WIREFRAME_CLASS_MASK 0u
/* E3A-S6. Only read when SHOW_TEST_CAR is set, which it is not by default,
 * so these merely have to be in range. Design 0 is CAR_DESIGN_AUTO. */
#define ROLLER_ED_OVERLAY_DEFAULT_TEST_CAR_DESIGN 0u
#define ROLLER_ED_OVERLAY_DEFAULT_TEST_CAR_AI_LINE 0u

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

/*
 * A canonical track segment belongs to its starting chunk. The final one
 * joins chunk (count - 1) to chunk zero and follows DETACH_LAST; every other
 * in-range segment remains visible. Invalid ids never describe a segment.
 */
bool roller_ed_overlay_track_segment_visible(uint32_t uiChunkId,
                                             uint32_t uiChunkCount);

/*
 * E3A-S2. Both queries are the master flag AND the class's mask bit, so a host
 * can blank the whole view without losing its per-class choices. E7-S3's tower
 * marker is the one surface-class exception: its solid visibility follows only
 * SHOW_TOWER_MARKERS, allowing editor furniture to remain in a surface-free
 * wireframe view. A class value at or beyond ROLLER_ED_SURFACE_CLASS_COUNT is
 * never drawn either way: the emitter refuses those identities, so seeing one
 * here means something already went wrong upstream.
 */
bool roller_ed_overlay_surface_class_visible(uint16_t unSurfaceClass);
bool roller_ed_overlay_wireframe_class_visible(uint16_t unSurfaceClass);

/*
 * E3A-S6. False unless SHOW_TEST_CAR is set, in which case the stored design,
 * AI line, chunk, million-plus, and advanced-cars modifiers are published.
 * "Advanced" is the editor's Y model variant: the same plan drawn with the
 * second texture bank and the mirror palette remap. The chunk is the
 * selection's first endpoint -- where the legacy editor drew the car -- and
 * falls back to zero when nothing is selected. Range validation happens at
 * the facade, so what comes out here is already in range.
 */
bool roller_ed_overlay_test_car(uint32_t *puiDesign, uint32_t *puiAiLine,
                                uint32_t *puiChunk, bool *pbMillionPlus,
                                bool *pbAdvanced);

#endif
