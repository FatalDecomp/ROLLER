#include "editor_helpers.h"
#include "editor_surface.h"

#include "3d.h"
#include "loadtrak.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* The legacy arrays the helpers read, supplied here instead of by the loader
 * so the derivation is testable without a track file or a renderer. */
tGroundPt TrakPt[MAX_TRACK_CHUNKS];
tData localdata[MAX_TRACK_CHUNKS];
int TRAK_LEN;
float TrackFloorHeight;

static int check(int bCondition, int iLine)
{
    if (!bCondition)
        fprintf(stderr, "editor helper check failed at line %d\n", iLine);
    return bCondition ? 0 : iLine;
}

#define CHECK(condition) \
    do { \
        int iResult = check((condition), __LINE__); \
        if (iResult != 0) \
            return iResult; \
    } while (0)

static int near(float fActual, float fExpected, float fTolerance)
{
    return fabsf(fActual - fExpected) <= fTolerance;
}

static void set_point(int iChunk, int iPoint, float fX, float fY, float fZ)
{
    TrakPt[iChunk].pointAy[iPoint].fX = fX;
    TrakPt[iChunk].pointAy[iPoint].fY = fY;
    TrakPt[iChunk].pointAy[iPoint].fZ = fZ;
}

/*
 * Two chunks of straight, flat track running along +X. The road spans
 * y = -100 (right) to y = +100 (left) at z = 500, and each shoulder rises
 * 40 units over the 100 units out to the outer edge.
 */
static void build_straight_track(void)
{
    memset(TrakPt, 0, sizeof(TrakPt));
    memset(localdata, 0, sizeof(localdata));
    TRAK_LEN = 2;
    TrackFloorHeight = 20.0f;

    for (int iChunk = 0; iChunk < 2; iChunk++) {
        float fX = (float)iChunk * 1000.0f;

        set_point(iChunk, ED_HELPER_POINT_LEFT_OUTER, fX, 200.0f, 540.0f);
        set_point(iChunk, ED_HELPER_POINT_LEFT_LANE, fX, 100.0f, 500.0f);
        set_point(iChunk, ED_HELPER_POINT_RIGHT_LANE, fX, -100.0f, 500.0f);
        set_point(iChunk, ED_HELPER_POINT_RIGHT_OUTER, fX, -200.0f, 540.0f);
    }
}

/* 3d.h declares the legacy entry point, so this has to match it. */
int main(int argc, const char **argv, const char **envp)
{
    float afPoint[3];
    float afQuad[4][3];

    (void)argc;
    (void)argv;
    (void)envp;
    build_straight_track();

    /* Road width is the lane-edge span, which the ribbon ratios scale by. */
    CHECK(near(ed_helper_road_width(0u), 200.0f, 0.001f));

    /* The centre line sits midway between the lane edges. */
    CHECK(ed_helper_center_point(0u, afPoint));
    CHECK(near(afPoint[0], 0.0f, 0.001f));
    CHECK(near(afPoint[1], 0.0f, 0.001f));
    CHECK(near(afPoint[2], 500.0f, 0.001f));

    /* A zero offset puts an AI line on the centre. */
    CHECK(ed_helper_ai_line_point(0u, 0u, afPoint));
    CHECK(near(afPoint[1], 0.0f, 0.001f));

    /* Positive is left, negative is right, both on the road surface. */
    localdata[0].fAILine1 = 50.0f;
    localdata[0].fAILine2 = -50.0f;
    CHECK(ed_helper_ai_line_point(0u, 0u, afPoint));
    CHECK(near(afPoint[1], 50.0f, 0.001f) && near(afPoint[2], 500.0f, 0.001f));
    CHECK(ed_helper_ai_line_point(0u, 1u, afPoint));
    CHECK(near(afPoint[1], -50.0f, 0.001f) && near(afPoint[2], 500.0f, 0.001f));

    /* Exactly on the lane edge. */
    localdata[0].fAILine3 = 100.0f;
    CHECK(ed_helper_ai_line_point(0u, 2u, afPoint));
    CHECK(near(afPoint[1], 100.0f, 0.001f) && near(afPoint[2], 500.0f, 0.001f));

    /*
     * Past the lane edge the line climbs the shoulder. Walking the real
     * shoulder chord picks up its slope: 50 units along a chord that rises 40
     * over a span of length sqrt(100^2 + 40^2) lands proportionally up it.
     */
    localdata[0].fAILine4 = 100.0f + 53.8516f; /* one full shoulder chord */
    CHECK(ed_helper_ai_line_point(0u, 3u, afPoint));
    CHECK(near(afPoint[1], 150.0f, 0.05f));
    CHECK(near(afPoint[2], 520.0f, 0.05f));

    /* Right shoulder mirrors it. */
    localdata[0].fAILine4 = -(100.0f + 53.8516f);
    CHECK(ed_helper_ai_line_point(0u, 3u, afPoint));
    CHECK(near(afPoint[1], -150.0f, 0.05f));
    CHECK(near(afPoint[2], 520.0f, 0.05f));

    /* Out-of-range chunks and lines are refused rather than read. */
    CHECK(!ed_helper_center_point(2u, afPoint));
    CHECK(!ed_helper_ai_line_point(0u, ED_HELPER_AI_LINE_COUNT, afPoint));
    CHECK(!ed_helper_ai_line_point(0u, 0u, NULL));

    /* A line segment becomes a flat ribbon facing up, straddling its own
     * centre line. */
    {
        static const float afStart[3] = { 0.0f, 0.0f, 500.0f };
        static const float afEnd[3] = { 1000.0f, 0.0f, 500.0f };
        float afNormal[3];

        CHECK(ed_helper_segment_quad(afStart, afEnd, 10.0f, afQuad));
        CHECK(ed_surface_compute_normals(
            (const float (*)[3])afQuad, afNormal, NULL));
        /* Front face is world up, so the renderer's facing test keeps it. */
        CHECK(near(afNormal[2], 1.0f, 0.0001f));
        for (uint32_t i = 0; i < 4u; i++)
            CHECK(near(afQuad[i][2], 500.0f, 0.001f));
        CHECK(near(afQuad[0][1], -10.0f, 0.001f));
        CHECK(near(afQuad[3][1], 10.0f, 0.001f));

        /* A zero-length segment has no direction and draws nothing. */
        CHECK(!ed_helper_segment_quad(afStart, afStart, 10.0f, afQuad));
        CHECK(!ed_helper_segment_quad(afStart, afEnd, 0.0f, afQuad));
    }

    /* The environment floor spans the outer edges of a chunk and its
     * successor, flattened to the track's floor height. */
    CHECK(ed_helper_environment_floor_quad(0u, afQuad));
    for (uint32_t i = 0; i < 4u; i++)
        CHECK(near(afQuad[i][ED_SURFACE_WORLD_UP_AXIS], 20.0f, 0.001f));
    CHECK(near(afQuad[1][1], 200.0f, 0.001f));
    CHECK(near(afQuad[2][1], -200.0f, 0.001f));
    CHECK(near(afQuad[1][0], 0.0f, 0.001f));
    CHECK(near(afQuad[0][0], 1000.0f, 0.001f));

    /* The last chunk wraps to the first, so the floor closes the loop. */
    CHECK(ed_helper_environment_floor_quad(1u, afQuad));
    CHECK(near(afQuad[0][0], 0.0f, 0.001f));
    CHECK(!ed_helper_environment_floor_quad(2u, afQuad));

    /* Nothing is derived before a track is loaded. */
    TRAK_LEN = 0;
    CHECK(!ed_helper_center_point(0u, afPoint));
    CHECK(!ed_helper_environment_floor_quad(0u, afQuad));
    CHECK(ed_helper_road_width(0u) == 0.0f);

    puts("editor helper line and floor geometry tests passed");
    return 0;
}
