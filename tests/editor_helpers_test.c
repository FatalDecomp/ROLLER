#include "editor_helpers.h"
#include "editor_surface.h"

#include "3d.h"
#include "loadtrak.h"
#include "moving.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* The legacy arrays the helpers read, supplied here instead of by the loader
 * so the derivation is testable without a track file or a renderer. */
tGroundPt TrakPt[MAX_TRACK_CHUNKS];
tData localdata[MAX_TRACK_CHUNKS];
int TRAK_LEN;
int16 samplespeed[MAX_SAMPLES];
tStuntData *ramp[50];
int totalramps;

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
    memset(samplespeed, 0, sizeof(samplespeed));
    memset(ramp, 0, sizeof(ramp));
    totalramps = 0;
    TRAK_LEN = 2;

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

    /*
     * E3A-S5. A chunk carries an audio marker when its trigger speed is
     * non-zero, which is the same test the legacy editor made.
     */
    CHECK(!ed_helper_chunk_has_audio(0u));
    samplespeed[0] = 60;
    CHECK(ed_helper_chunk_has_audio(0u));
    CHECK(!ed_helper_chunk_has_audio(1u));
    CHECK(!ed_helper_chunk_has_audio(2u));

    /* Stunt markers come from the loaded ramps, anchored on each ramp's apex
     * chunk rather than the first chunk of its span. */
    {
        static tStuntData Stunt;
        uint32_t uiChunkId = 0xFFFFFFFFu;

        CHECK(ed_helper_stunt_count() == 0u);
        CHECK(!ed_helper_stunt_chunk(0u, &uiChunkId));

        Stunt.iGeometryIdx = 1;
        Stunt.iChunkCount = 1;
        ramp[0] = &Stunt;
        totalramps = 1;
        CHECK(ed_helper_stunt_count() == 1u);
        CHECK(ed_helper_stunt_chunk(0u, &uiChunkId));
        CHECK(uiChunkId == 1u);
        CHECK(!ed_helper_stunt_chunk(1u, &uiChunkId));
        CHECK(!ed_helper_stunt_chunk(0u, NULL));

        /* A ramp naming a chunk the loaded track does not have draws nothing
         * rather than reading past the arrays. */
        Stunt.iGeometryIdx = 7;
        CHECK(!ed_helper_stunt_chunk(0u, &uiChunkId));
        Stunt.iGeometryIdx = -1;
        CHECK(!ed_helper_stunt_chunk(0u, &uiChunkId));

        /* A null ramp slot inside the count is skipped, not dereferenced. */
        ramp[0] = NULL;
        CHECK(!ed_helper_stunt_chunk(0u, &uiChunkId));

        Stunt.iGeometryIdx = 1;
        ramp[0] = &Stunt;
    }

    /*
     * A marker is two quads standing across the track, hovering over the road
     * centre. On this flat straight track the plane is the world YZ plane at
     * the chunk's own X, so every marker vertex shares that X and the icon
     * spans the road laterally.
     */
    {
        static const float afIconMinX[2] = { 0.0f, -0.5f };
        static const float afIconMaxX[2] = { 0.5f, 0.0f };
        const float fSize = 200.0f * ED_HELPER_MARKER_SIZE_RATIO;
        const float fHover = 500.0f + 200.0f * ED_HELPER_MARKER_HOVER_RATIO;

        for (uint32_t uiQuad = 0; uiQuad < ED_HELPER_MARKER_QUAD_COUNT;
             uiQuad++) {
            float fMinY = 1.0e30f;
            float fMaxY = -1.0e30f;

            CHECK(ed_helper_marker_quad(0u, ED_HELPER_MARKER_AUDIO, uiQuad,
                                        afQuad));
            for (uint32_t i = 0; i < 4u; i++) {
                /* Facing along the track: the icon plane holds no forward
                 * component, so it never leans down the road. */
                CHECK(near(afQuad[i][0], 0.0f, 0.001f));
                if (afQuad[i][1] < fMinY)
                    fMinY = afQuad[i][1];
                if (afQuad[i][1] > fMaxY)
                    fMaxY = afQuad[i][1];
            }
            /* Left is +Y, and the icon's local +x runs to the right, so the
             * legacy silhouette arrives mirrored into world Y. */
            CHECK(near(fMinY, -afIconMaxX[uiQuad] * fSize, 0.001f));
            CHECK(near(fMaxY, -afIconMinX[uiQuad] * fSize, 0.001f));
        }

        /* The speaker box is a square of the marker size' half, sitting on the
         * hover point rather than centred on it. */
        CHECK(ed_helper_marker_quad(0u, ED_HELPER_MARKER_AUDIO, 0u, afQuad));
        CHECK(near(afQuad[1][2], fHover, 0.001f));
        CHECK(near(afQuad[3][2], fHover + 0.5f * fSize, 0.001f));

        /*
         * The stunt arrow hangs below its anchor, which is how legacy drew
         * it. Chunk 1 is the last one here, so its forward wraps backwards
         * onto chunk 0 and the frame is flipped upright -- the marker still
         * hangs above the road, which is the point of the flip.
         */
        CHECK(ed_helper_marker_quad(1u, ED_HELPER_MARKER_STUNT, 0u, afQuad));
        CHECK(near(afQuad[0][2], fHover - 0.1f * fSize, 0.001f));
        CHECK(near(afQuad[1][2], fHover - 0.4f * fSize, 0.001f));
        /* Chunk 1 is the second chunk, so the whole icon stands at its X. */
        for (uint32_t i = 0; i < 4u; i++)
            CHECK(near(afQuad[i][0], 1000.0f, 0.001f));

        /* Out-of-range chunks, quads, and markers are refused. */
        CHECK(!ed_helper_marker_quad(2u, ED_HELPER_MARKER_AUDIO, 0u, afQuad));
        CHECK(!ed_helper_marker_quad(
            0u, ED_HELPER_MARKER_AUDIO, ED_HELPER_MARKER_QUAD_COUNT, afQuad));
        CHECK(!ed_helper_marker_quad(0u, (eEdHelperMarker)7, 0u, afQuad));
        CHECK(!ed_helper_marker_quad(0u, ED_HELPER_MARKER_AUDIO, 0u, NULL));
    }

    /* Nothing is derived before a track is loaded. */
    TRAK_LEN = 0;
    CHECK(!ed_helper_center_point(0u, afPoint));
    CHECK(ed_helper_road_width(0u) == 0.0f);
    CHECK(!ed_helper_chunk_has_audio(0u));
    CHECK(!ed_helper_marker_quad(0u, ED_HELPER_MARKER_AUDIO, 0u, afQuad));

    puts("editor helper line and marker geometry tests passed");
    return 0;
}
