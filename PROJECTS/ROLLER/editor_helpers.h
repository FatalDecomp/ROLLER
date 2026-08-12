#ifndef ROLLER_EDITOR_HELPERS_H
#define ROLLER_EDITOR_HELPERS_H

#include "editor_api.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * E3A-S4 and E3A-S5. World-space geometry for the editor's helper overlays:
 * the four AI racing lines, the track centre line, and the audio and stunt
 * markers.
 *
 * There was an environment floor here too -- the green plane under the track,
 * inherited from the pre-modernization editor. It was removed once the
 * modern preview drew the real horizon, which fills that space properly; a
 * flat slab underneath no longer described anything.
 *
 * None of this is track content -- no exporter should ever see it (AD-6d) --
 * but it is derived here rather than in the editor because geometry authority
 * is ROLLER's (AD-6a). Everything comes from the loaded legacy arrays
 * (TrakPt, localdata, samplespeed, ramp) so the lines land exactly where the
 * game's AI drives and a marker stands on exactly the chunk the game triggers,
 * instead of being re-derived from scalar chunk rows and drifting.
 */

#define ED_HELPER_AI_LINE_COUNT 4u

/*
 * The cross-section points these helpers read. drawtrk3.c's own producers use
 * the same indices: the road spans 2..3, the shoulders run outward to 0 and 4.
 */
#define ED_HELPER_POINT_LEFT_OUTER 0u
#define ED_HELPER_POINT_LEFT_LANE 2u
#define ED_HELPER_POINT_RIGHT_LANE 3u
#define ED_HELPER_POINT_RIGHT_OUTER 4u

/* Ribbon width and height bias, both as a fraction of the chunk's road
 * width, for the same reason E3A-S2's wireframe scales with its quad: legacy
 * track units differ by orders of magnitude between tracks. */
#define ED_HELPER_LINE_WIDTH_RATIO 0.005f
#define ED_HELPER_LINE_HEIGHT_RATIO 0.02f

/* Centre of the road surface at a chunk: the midpoint of its lane edges. */
bool ed_helper_center_point(uint32_t uiChunkId, float afPointOut[3]);

/*
 * A racing line's world position at a chunk. uiLine is 0..3, matching
 * localdata[].fAILine1..4. The lateral offset is walked along the real
 * cross-section -- across the road first, then up the shoulder chord if it
 * runs past the lane edge -- so the line follows the surface it sits on,
 * including banking and shoulder slope, without re-deriving either.
 */
bool ed_helper_ai_line_point(uint32_t uiChunkId, uint32_t uiLine,
                             float afPointOut[3]);

/*
 * One segment of a helper line as a flat ribbon lying in the world XY plane
 * and facing up, which is how a line is drawn by a renderer that has only
 * quads. Returns false for a zero-length segment.
 */
bool ed_helper_segment_quad(const float afStart[3],
                            const float afEnd[3],
                            float fWidth,
                            float afQuadOut[4][3]);

/* Road width at a chunk, which the ratios above are taken against. */
float ed_helper_road_width(uint32_t uiChunkId);

/*
 * E3A-S5 markers.
 *
 * Both legacy icons -- the audio speaker and the stunt arrow -- were built as
 * triangle pairs sharing a diagonal, so each is exactly two quads and nothing
 * is lost expressing them for a renderer that has no other primitive. Their
 * silhouettes are the pre-modernization editor's, scaled to the chunk rather
 * than fixed in absolute track units.
 */
#define ED_HELPER_MARKER_QUAD_COUNT 2u

/*
 * Icon size and hover height as a fraction of the chunk's road width, for the
 * same reason the line ribbons scale: the legacy editor sized these in
 * absolute units, which only reads correctly on a track built to the retail
 * scale.
 */
#define ED_HELPER_MARKER_SIZE_RATIO 0.12f
#define ED_HELPER_MARKER_HOVER_RATIO 0.25f

typedef enum
{
    ED_HELPER_MARKER_AUDIO = 0,
    ED_HELPER_MARKER_STUNT = 1
} eEdHelperMarker;

/*
 * True when the chunk carries an audio trigger. samplespeed[] is the loader's
 * name for the editor's iAudioTriggerSpeed, and a zero trigger speed is how
 * both of them say "this chunk plays nothing".
 */
bool ed_helper_chunk_has_audio(uint32_t uiChunkId);

/*
 * The loaded stunts, and the chunk each one is anchored to. That chunk is the
 * ramp's tStuntData.iGeometryIdx -- its apex, the same chunk the legacy editor
 * keyed its stunt map on -- not the first chunk of the ramp's span.
 */
uint32_t ed_helper_stunt_count(void);
bool ed_helper_stunt_chunk(uint32_t uiStuntIndex, uint32_t *puiChunkOut);

/*
 * One quad of a marker icon, in world space, hovering over the chunk's road
 * centre in the plane that runs across the track and faces along it.
 */
bool ed_helper_marker_quad(uint32_t uiChunkId, eEdHelperMarker eMarker,
                           uint32_t uiQuad, float afQuadOut[4][3]);

#endif
