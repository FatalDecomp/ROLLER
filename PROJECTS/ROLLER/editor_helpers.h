#ifndef ROLLER_EDITOR_HELPERS_H
#define ROLLER_EDITOR_HELPERS_H

#include "editor_api.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * E3A-S4. World-space geometry for the editor's helper overlays: the four AI
 * racing lines, the track centre line, and the environment floor.
 *
 * None of this is track content -- no exporter should ever see it (AD-6d) --
 * but it is derived here rather than in the editor because geometry authority
 * is ROLLER's (AD-6a). Everything comes from the loaded legacy arrays
 * (TrakPt, localdata) so the lines land exactly where the game's AI drives,
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
#define ED_HELPER_LINE_WIDTH_RATIO 0.02f
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

/*
 * The environment floor under one chunk: the span between the outer shoulder
 * edges of this chunk and the next, dropped to the track's floor height. That
 * height is the origin component the track header carries, which the editor
 * has always called the floor depth.
 */
bool ed_helper_environment_floor_quad(uint32_t uiChunkId,
                                      float afQuadOut[4][3]);

/* Road width at a chunk, which the ratios above are taken against. */
float ed_helper_road_width(uint32_t uiChunkId);

#endif
