#include "editor_helpers.h"

#include "3d.h"
#include "editor_surface.h"
#include "loadtrak.h"
#include "moving.h"

#include <math.h>
#include <string.h>

static bool helper_chunk_valid(uint32_t uiChunkId)
{
    return TRAK_LEN > 0 && uiChunkId < (uint32_t)TRAK_LEN;
}

static void helper_copy(const tVec3 *pPoint, float afOut[3])
{
    afOut[0] = pPoint->fX;
    afOut[1] = pPoint->fY;
    afOut[2] = pPoint->fZ;
}

static double helper_length(const float afVector[3])
{
    return sqrt((double)afVector[0] * afVector[0]
              + (double)afVector[1] * afVector[1]
              + (double)afVector[2] * afVector[2]);
}

static bool helper_direction(const float afFrom[3], const float afTo[3],
                             float afDirectionOut[3], double *pdLength)
{
    double dLength;

    for (uint32_t i = 0; i < 3u; i++)
        afDirectionOut[i] = afTo[i] - afFrom[i];
    dLength = helper_length(afDirectionOut);
    if (pdLength)
        *pdLength = dLength;
    if (!(dLength > 0.0))
        return false;
    for (uint32_t i = 0; i < 3u; i++)
        afDirectionOut[i] = (float)((double)afDirectionOut[i] / dLength);
    return true;
}

static void helper_cross(const float afLeft[3], const float afRight[3],
                         float afOut[3])
{
    afOut[0] = afLeft[1] * afRight[2] - afLeft[2] * afRight[1];
    afOut[1] = afLeft[2] * afRight[0] - afLeft[0] * afRight[2];
    afOut[2] = afLeft[0] * afRight[1] - afLeft[1] * afRight[0];
}

/* The chunk after this one, wrapping at the end so the track closes. */
static uint32_t helper_next_chunk(uint32_t uiChunkId)
{
    uint32_t uiNextChunk = uiChunkId + 1u;

    return uiNextChunk >= (uint32_t)TRAK_LEN ? 0u : uiNextChunk;
}

float ed_helper_road_width(uint32_t uiChunkId)
{
    float afLeft[3];
    float afRight[3];
    float afSpan[3];

    if (!helper_chunk_valid(uiChunkId))
        return 0.0f;
    helper_copy(&TrakPt[uiChunkId].pointAy[ED_HELPER_POINT_LEFT_LANE], afLeft);
    helper_copy(&TrakPt[uiChunkId].pointAy[ED_HELPER_POINT_RIGHT_LANE],
                afRight);
    for (uint32_t i = 0; i < 3u; i++)
        afSpan[i] = afLeft[i] - afRight[i];
    return (float)helper_length(afSpan);
}

bool ed_helper_center_point(uint32_t uiChunkId, float afPointOut[3])
{
    const tVec3 *pLeft;
    const tVec3 *pRight;

    if (!afPointOut || !helper_chunk_valid(uiChunkId))
        return false;
    pLeft = &TrakPt[uiChunkId].pointAy[ED_HELPER_POINT_LEFT_LANE];
    pRight = &TrakPt[uiChunkId].pointAy[ED_HELPER_POINT_RIGHT_LANE];
    afPointOut[0] = (pLeft->fX + pRight->fX) * 0.5f;
    afPointOut[1] = (pLeft->fY + pRight->fY) * 0.5f;
    afPointOut[2] = (pLeft->fZ + pRight->fZ) * 0.5f;
    return true;
}

bool ed_helper_ai_line_point(uint32_t uiChunkId, uint32_t uiLine,
                             float afPointOut[3])
{
    float afCenter[3];
    float afLaneEdge[3];
    float afOuterEdge[3];
    float afDirection[3];
    double dLaneWidth;
    float fOffset;
    float fRemaining;

    if (!afPointOut || uiLine >= ED_HELPER_AI_LINE_COUNT
            || !helper_chunk_valid(uiChunkId))
        return false;
    if (!ed_helper_center_point(uiChunkId, afPointOut))
        return false;
    memcpy(afCenter, afPointOut, sizeof(afCenter));

    /* localdata's four AI line fields are contiguous, and the game itself
     * indexes them this way (control.c: *(&pData->fAILine1 + iLine)). */
    fOffset = *(&localdata[uiChunkId].fAILine1 + uiLine);
    if (fOffset == 0.0f)
        return true;

    /* Positive is left, matching the game's lateral sign convention. */
    if (fOffset > 0.0f) {
        helper_copy(&TrakPt[uiChunkId].pointAy[ED_HELPER_POINT_LEFT_LANE],
                    afLaneEdge);
        helper_copy(&TrakPt[uiChunkId].pointAy[ED_HELPER_POINT_LEFT_OUTER],
                    afOuterEdge);
    } else {
        helper_copy(&TrakPt[uiChunkId].pointAy[ED_HELPER_POINT_RIGHT_LANE],
                    afLaneEdge);
        helper_copy(&TrakPt[uiChunkId].pointAy[ED_HELPER_POINT_RIGHT_OUTER],
                    afOuterEdge);
        fOffset = -fOffset;
    }

    if (!helper_direction(afCenter, afLaneEdge, afDirection, &dLaneWidth))
        return true;
    if ((double)fOffset <= dLaneWidth) {
        for (uint32_t i = 0; i < 3u; i++)
            afPointOut[i] = afCenter[i] + afDirection[i] * fOffset;
        return true;
    }

    /*
     * Past the lane edge the line is on the shoulder. Continuing along the
     * shoulder chord picks up its slope from the real geometry, which is what
     * the editor used to approximate with a tangent of the shoulder's
     * height-to-width ratio.
     */
    fRemaining = (float)((double)fOffset - dLaneWidth);
    if (!helper_direction(afLaneEdge, afOuterEdge, afDirection, NULL)) {
        memcpy(afPointOut, afLaneEdge, sizeof(afCenter));
        return true;
    }
    for (uint32_t i = 0; i < 3u; i++)
        afPointOut[i] = afLaneEdge[i] + afDirection[i] * fRemaining;
    return true;
}

bool ed_helper_segment_quad(const float afStart[3],
                            const float afEnd[3],
                            float fWidth,
                            float afQuadOut[4][3])
{
    float afDirection[3];
    float afSideways[3];
    double dLength;

    if (!afStart || !afEnd || !afQuadOut || !(fWidth > 0.0f))
        return false;
    if (!helper_direction(afStart, afEnd, afDirection, &dLength))
        return false;

    /*
     * A flat ribbon facing up: sideways is the horizontal perpendicular, so a
     * line stays readable from above however the track banks underneath it. A
     * segment that runs straight up has no such perpendicular and is skipped.
     */
    afSideways[0] = -afDirection[1];
    afSideways[1] = afDirection[0];
    afSideways[2] = 0.0f;
    if (!helper_direction((float[3]){ 0.0f, 0.0f, 0.0f }, afSideways,
                          afSideways, NULL))
        return false;

    /* Wound so the right-hand-rule normal points at world +Z, the front face
     * the renderer's facing test expects (E4A-S4). */
    for (uint32_t i = 0; i < 3u; i++) {
        afQuadOut[0][i] = afStart[i] - afSideways[i] * fWidth;
        afQuadOut[1][i] = afEnd[i] - afSideways[i] * fWidth;
        afQuadOut[2][i] = afEnd[i] + afSideways[i] * fWidth;
        afQuadOut[3][i] = afStart[i] + afSideways[i] * fWidth;
    }
    return true;
}

bool ed_helper_chunk_has_audio(uint32_t uiChunkId)
{
    if (!helper_chunk_valid(uiChunkId))
        return false;
    return samplespeed[uiChunkId] != 0;
}

uint32_t ed_helper_stunt_count(void)
{
    const uint32_t uiCapacity = (uint32_t)(sizeof(ramp) / sizeof(ramp[0]));

    if (totalramps <= 0)
        return 0u;
    return (uint32_t)totalramps > uiCapacity ? uiCapacity : (uint32_t)totalramps;
}

bool ed_helper_stunt_chunk(uint32_t uiStuntIndex, uint32_t *puiChunkOut)
{
    const tStuntData *pStunt;

    if (!puiChunkOut || uiStuntIndex >= ed_helper_stunt_count())
        return false;
    pStunt = ramp[uiStuntIndex];
    if (!pStunt || pStunt->iGeometryIdx < 0
            || !helper_chunk_valid((uint32_t)pStunt->iGeometryIdx))
        return false;
    *puiChunkOut = (uint32_t)pStunt->iGeometryIdx;
    return true;
}

/*
 * The two marker silhouettes, in units of the marker's own size: the legacy
 * editor's ShapeFactory vertices divided by the 1000-unit scale it drew them
 * at. x runs across the track and y up it, matching the local frame the legacy
 * icons were authored in. The origin is the icon's own anchor rather than its
 * centroid, which is where legacy hung it, so the speaker sits above its point
 * and the arrow below its own -- keeping both silhouettes as they were.
 */
static const float
s_aafAudioMarkerIcon[ED_HELPER_MARKER_QUAD_COUNT][4][2] = {
    /* The speaker box: legacy triangles (1,0,3) and (1,3,2). */
    { {  0.5f,  0.0f }, {  0.0f,  0.0f }, {  0.0f,  0.5f }, {  0.5f,  0.5f } },
    /* Its cone: legacy triangles (0,5,4) and (0,4,3). */
    { {  0.0f,  0.0f }, { -0.5f, -0.3f }, { -0.5f,  0.8f }, {  0.0f,  0.5f } }
};

static const float
s_aafStuntMarkerIcon[ED_HELPER_MARKER_QUAD_COUNT][4][2] = {
    /* The right blade: legacy triangles (4,3,2) and (4,2,1). */
    { {  0.0f, -0.1f }, {  0.3f, -0.4f }, {  0.4f, -0.3f }, {  0.1f,  0.0f } },
    /* The left blade: legacy triangles (6,5,0) and (5,1,0). */
    { { -0.4f, -0.3f }, { -0.3f, -0.4f }, {  0.1f,  0.0f }, {  0.0f,  0.1f } }
};

/*
 * The frame a marker icon is drawn in: across the track, up out of it, and the
 * point above the road centre it hangs from. Legacy rotated its icons by the
 * chunk's yaw/pitch/roll matrices, which put the icon plane across the track
 * and facing along it; taking both axes from the cross-section reaches the
 * same place without reconstructing any of those angles, and picks up banking
 * for free the way the AI lines do.
 */
static bool helper_marker_frame(uint32_t uiChunkId, float afRightOut[3],
                                float afUpOut[3], float afOriginOut[3])
{
    float afLeftLane[3];
    float afRightLane[3];
    float afForward[3];
    float afNextCenter[3];
    float fRoadWidth;

    if (!helper_chunk_valid(uiChunkId))
        return false;
    fRoadWidth = ed_helper_road_width(uiChunkId);
    if (!(fRoadWidth > 0.0f))
        return false;
    if (!ed_helper_center_point(uiChunkId, afOriginOut)
            || !ed_helper_center_point(helper_next_chunk(uiChunkId),
                                       afNextCenter))
        return false;
    if (!helper_direction(afOriginOut, afNextCenter, afForward, NULL))
        return false;

    helper_copy(&TrakPt[uiChunkId].pointAy[ED_HELPER_POINT_LEFT_LANE],
                afLeftLane);
    helper_copy(&TrakPt[uiChunkId].pointAy[ED_HELPER_POINT_RIGHT_LANE],
                afRightLane);
    if (!helper_direction(afLeftLane, afRightLane, afRightOut, NULL))
        return false;

    /* right x forward is up: on flat track heading +X with left at +Y that is
     * world +Z, which is up under ADR 0003. Banking tilts it with the road,
     * exactly as the legacy roll matrix did. */
    helper_cross(afRightOut, afForward, afUpOut);
    if (!helper_direction((float[3]){ 0.0f, 0.0f, 0.0f }, afUpOut, afUpOut,
                          NULL))
        return false;
    /*
     * Forward comes from the next chunk, which is the chunk the track loops
     * back to at the end -- so on anything that is not a closed loop the last
     * chunk's forward points backwards and the road normal comes out inverted.
     * A marker hanging under the road is useless, so flip the frame upright;
     * flipping right along with up keeps it right-handed, at the cost of the
     * icon reading rotated on that one chunk.
     */
    if (afUpOut[ED_SURFACE_WORLD_UP_AXIS] < 0.0f) {
        for (uint32_t i = 0; i < 3u; i++) {
            afUpOut[i] = -afUpOut[i];
            afRightOut[i] = -afRightOut[i];
        }
    }
    for (uint32_t i = 0; i < 3u; i++)
        afOriginOut[i] +=
            afUpOut[i] * fRoadWidth * ED_HELPER_MARKER_HOVER_RATIO;
    return true;
}

bool ed_helper_marker_quad(uint32_t uiChunkId, eEdHelperMarker eMarker,
                           uint32_t uiQuad, float afQuadOut[4][3])
{
    const float (*aafIcon)[4][2];
    float afRight[3];
    float afUp[3];
    float afOrigin[3];
    float fSize;

    if (!afQuadOut || uiQuad >= ED_HELPER_MARKER_QUAD_COUNT)
        return false;
    if (eMarker == ED_HELPER_MARKER_AUDIO)
        aafIcon = s_aafAudioMarkerIcon;
    else if (eMarker == ED_HELPER_MARKER_STUNT)
        aafIcon = s_aafStuntMarkerIcon;
    else
        return false;
    if (!helper_marker_frame(uiChunkId, afRight, afUp, afOrigin))
        return false;
    fSize = ed_helper_road_width(uiChunkId) * ED_HELPER_MARKER_SIZE_RATIO;

    for (uint32_t uiVertex = 0; uiVertex < 4u; uiVertex++) {
        const float fLocalX = aafIcon[uiQuad][uiVertex][0] * fSize;
        const float fLocalY = aafIcon[uiQuad][uiVertex][1] * fSize;

        for (uint32_t i = 0; i < 3u; i++)
            afQuadOut[uiVertex][i] =
                afOrigin[i] + afRight[i] * fLocalX + afUp[i] * fLocalY;
    }
    return true;
}
