#include "editor_helpers.h"

#include "3d.h"
#include "editor_surface.h"
#include "loadtrak.h"

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

bool ed_helper_environment_floor_quad(uint32_t uiChunkId,
                                      float afQuadOut[4][3])
{
    uint32_t uiNextChunk;

    if (!afQuadOut || !helper_chunk_valid(uiChunkId))
        return false;
    uiNextChunk = uiChunkId + 1u;
    if (uiNextChunk >= (uint32_t)TRAK_LEN)
        uiNextChunk = 0u;

    helper_copy(&TrakPt[uiNextChunk].pointAy[ED_HELPER_POINT_LEFT_OUTER],
                afQuadOut[0]);
    helper_copy(&TrakPt[uiChunkId].pointAy[ED_HELPER_POINT_LEFT_OUTER],
                afQuadOut[1]);
    helper_copy(&TrakPt[uiChunkId].pointAy[ED_HELPER_POINT_RIGHT_OUTER],
                afQuadOut[2]);
    helper_copy(&TrakPt[uiNextChunk].pointAy[ED_HELPER_POINT_RIGHT_OUTER],
                afQuadOut[3]);
    for (uint32_t i = 0; i < 4u; i++)
        afQuadOut[i][ED_SURFACE_WORLD_UP_AXIS] = TrackFloorHeight;
    return true;
}
