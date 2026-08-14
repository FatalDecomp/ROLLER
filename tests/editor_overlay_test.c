#include "editor_overlay.h"

#include <stdio.h>
#include <string.h>

static int check(int bCondition, int iLine)
{
    if (!bCondition)
        fprintf(stderr, "editor overlay check failed at line %d\n", iLine);
    return bCondition ? 0 : iLine;
}

#define CHECK(condition) \
    do { \
        int iResult = check((condition), __LINE__); \
        if (iResult != 0) \
            return iResult; \
    } while (0)

static tEdOverlayState make_masked_state(uint32_t uiFlags,
                                         uint32_t uiFirstChunk,
                                         uint32_t uiLastChunk,
                                         uint32_t uiSurfaceClassMask,
                                         uint32_t uiWireframeClassMask)
{
    tEdOverlayState State;

    memset(&State, 0, sizeof(State));
    State.uiStructSize = (uint32_t)sizeof(State);
    State.uiVersion = ROLLER_ED_OVERLAY_STATE_VERSION;
    State.uiFlags = uiFlags;
    State.uiFirstSelectedChunk = uiFirstChunk;
    State.uiLastSelectedChunk = uiLastChunk;
    State.uiSurfaceClassMask = uiSurfaceClassMask;
    State.uiWireframeClassMask = uiWireframeClassMask;
    return State;
}

/* Class masks at their defaults, so the flag and selection cases below stay
 * about flags and selection. */
static tEdOverlayState make_state(uint32_t uiFlags,
                                  uint32_t uiFirstChunk,
                                  uint32_t uiLastChunk)
{
    return make_masked_state(
        uiFlags, uiFirstChunk, uiLastChunk,
        ROLLER_ED_OVERLAY_DEFAULT_SURFACE_CLASS_MASK,
        ROLLER_ED_OVERLAY_DEFAULT_WIREFRAME_CLASS_MASK);
}

int main(void)
{
    static const uint32_t auiFlags[] = {
        ROLLER_ED_OVERLAY_SHOW_SURFACES,
        ROLLER_ED_OVERLAY_SHOW_WIREFRAME,
        ROLLER_ED_OVERLAY_HIGHLIGHT_SELECTION,
        ROLLER_ED_OVERLAY_SHOW_AI_LINES,
        ROLLER_ED_OVERLAY_SHOW_CENTER_LINE,
        ROLLER_ED_OVERLAY_SHOW_AUDIO_MARKERS,
        ROLLER_ED_OVERLAY_SHOW_STUNT_MARKERS,
        ROLLER_ED_OVERLAY_SHOW_TEST_CAR,
        ROLLER_ED_OVERLAY_SHOW_REFERENCE_MESH,
        ROLLER_ED_OVERLAY_TEST_CAR_MILLION_PLUS,
        ROLLER_ED_OVERLAY_TEST_CAR_ADVANCED,
        ROLLER_ED_OVERLAY_SHOW_TOWER_MARKERS,
        ROLLER_ED_OVERLAY_DETACH_LAST
    };
    const uint32_t uiKnownFlags = (uint32_t)ROLLER_ED_OVERLAY_KNOWN_FLAGS;
    tEdOverlayState State;
    uint32_t uiFirstChunk;
    uint32_t uiLastChunk;

    /* The mask really is every flag the public header defines, so a state the
     * facade accepted can never lose a bit on the way in. */
    {
        uint32_t uiUnion = 0u;

        for (size_t i = 0; i < sizeof(auiFlags) / sizeof(auiFlags[0]); ++i)
            uiUnion |= auiFlags[i];
        CHECK(uiUnion == uiKnownFlags);
        /* Bit 14 is the first undefined one; bit 5 is reserved, having been
         * the retired environment floor, and must stay refused rather than
         * being quietly reused. */
        CHECK((uiKnownFlags & (1u << 14)) == 0u);
        CHECK((uiKnownFlags & (1u << 5)) == 0u);
    }

    /* Defaults reproduce the E1-S6 track-only view: every class solid, no
     * wireframe anywhere. */
    roller_ed_overlay_reset();
    for (uint32_t uiClass = 0u; uiClass < ROLLER_ED_SURFACE_CLASS_COUNT;
         uiClass++) {
        CHECK(roller_ed_overlay_surface_class_visible((uint16_t)uiClass)
              == (uiClass != ROLLER_ED_SURFACE_CLASS_TOWER));
        CHECK(!roller_ed_overlay_wireframe_class_visible((uint16_t)uiClass));
    }
    CHECK(!roller_ed_overlay_surface_class_visible(
              (uint16_t)ROLLER_ED_SURFACE_CLASS_COUNT));
    CHECK(roller_ed_overlay_flags() == ROLLER_ED_OVERLAY_DEFAULT_FLAGS);
    CHECK(roller_ed_overlay_enabled(ROLLER_ED_OVERLAY_SHOW_SURFACES));
    CHECK(!roller_ed_overlay_enabled(ROLLER_ED_OVERLAY_SHOW_WIREFRAME));
    CHECK(!roller_ed_overlay_enabled(
              ROLLER_ED_OVERLAY_HIGHLIGHT_SELECTION));
    CHECK(!roller_ed_overlay_selection_range(&uiFirstChunk, &uiLastChunk));
    CHECK(roller_ed_overlay_track_segment_visible(0u, 4u));
    CHECK(roller_ed_overlay_track_segment_visible(3u, 4u));
    roller_ed_overlay_get(&State);
    CHECK(State.uiStructSize == sizeof(State));
    CHECK(State.uiVersion == ROLLER_ED_OVERLAY_STATE_VERSION);
    CHECK(State.uiFlags == ROLLER_ED_OVERLAY_DEFAULT_FLAGS);
    CHECK(State.uiFirstSelectedChunk == ROLLER_ED_INVALID_CHUNK_ID);
    CHECK(State.uiLastSelectedChunk == ROLLER_ED_INVALID_CHUNK_ID);

    /* DETACH_LAST affects only the closing segment. Invalid ids do not name
     * visible segments, whether or not the flag is enabled. */
    {
        tEdOverlayState Open = make_state(
            ROLLER_ED_OVERLAY_SHOW_SURFACES
                | ROLLER_ED_OVERLAY_DETACH_LAST, 0u, 0u);

        roller_ed_overlay_set(&Open);
        CHECK(roller_ed_overlay_track_segment_visible(0u, 4u));
        CHECK(roller_ed_overlay_track_segment_visible(2u, 4u));
        CHECK(!roller_ed_overlay_track_segment_visible(3u, 4u));
        CHECK(!roller_ed_overlay_track_segment_visible(4u, 4u));
        CHECK(!roller_ed_overlay_track_segment_visible(0u, 0u));

        Open.uiFlags &= ~ROLLER_ED_OVERLAY_DETACH_LAST;
        roller_ed_overlay_set(&Open);
        CHECK(roller_ed_overlay_track_segment_visible(3u, 4u));
    }

    /* Every flag round-trips on its own and is reported on its own. */
    for (size_t i = 0; i < sizeof(auiFlags) / sizeof(auiFlags[0]); ++i) {
        tEdOverlayState Single = make_state(auiFlags[i], 3u, 9u);

        roller_ed_overlay_set(&Single);
        CHECK(roller_ed_overlay_flags() == auiFlags[i]);
        CHECK(roller_ed_overlay_enabled(auiFlags[i]));
        for (size_t j = 0; j < sizeof(auiFlags) / sizeof(auiFlags[0]); ++j)
            CHECK(roller_ed_overlay_enabled(auiFlags[j]) == (i == j));
        roller_ed_overlay_get(&State);
        CHECK(State.uiFlags == auiFlags[i]);
        CHECK(State.uiFirstSelectedChunk == 3u);
        CHECK(State.uiLastSelectedChunk == 9u);
    }

    /* A whole state round-trips, including a range the editor sent backwards
     * because the user dragged the selection upwards. */
    {
        tEdOverlayState Reversed = make_state(
            uiKnownFlags, 41u, 12u);

        roller_ed_overlay_set(&Reversed);
        roller_ed_overlay_get(&State);
        CHECK(State.uiFlags == uiKnownFlags);
        CHECK(State.uiFirstSelectedChunk == 41u);
        CHECK(State.uiLastSelectedChunk == 12u);
        uiFirstChunk = 0xffffu;
        uiLastChunk = 0xffffu;
        CHECK(roller_ed_overlay_selection_range(&uiFirstChunk, &uiLastChunk));
        CHECK(uiFirstChunk == 12u && uiLastChunk == 41u);
        CHECK(roller_ed_overlay_selection_range(NULL, NULL));
    }

    /* An ordered single-chunk range is the common case. */
    {
        tEdOverlayState Single = make_state(
            ROLLER_ED_OVERLAY_SHOW_SURFACES
                | ROLLER_ED_OVERLAY_HIGHLIGHT_SELECTION,
            7u, 7u);

        roller_ed_overlay_set(&Single);
        CHECK(roller_ed_overlay_selection_range(&uiFirstChunk, &uiLastChunk));
        CHECK(uiFirstChunk == 7u && uiLastChunk == 7u);
    }

    /* The highlight flag governs; the endpoints survive it being cleared. */
    {
        tEdOverlayState NoHighlight = make_state(
            ROLLER_ED_OVERLAY_SHOW_SURFACES, 4u, 6u);

        roller_ed_overlay_set(&NoHighlight);
        CHECK(!roller_ed_overlay_selection_range(&uiFirstChunk, &uiLastChunk));
        roller_ed_overlay_get(&State);
        CHECK(State.uiFirstSelectedChunk == 4u);
        CHECK(State.uiLastSelectedChunk == 6u);
    }

    /* ROLLER_ED_INVALID_CHUNK_ID is how the editor says nothing is selected
     * while leaving the highlight enabled. */
    {
        const uint32_t uiHighlight =
            ROLLER_ED_OVERLAY_HIGHLIGHT_SELECTION;
        tEdOverlayState Empty = make_state(
            uiHighlight, ROLLER_ED_INVALID_CHUNK_ID,
            ROLLER_ED_INVALID_CHUNK_ID);

        roller_ed_overlay_set(&Empty);
        CHECK(!roller_ed_overlay_selection_range(&uiFirstChunk, &uiLastChunk));
        Empty = make_state(uiHighlight, 5u, ROLLER_ED_INVALID_CHUNK_ID);
        roller_ed_overlay_set(&Empty);
        CHECK(!roller_ed_overlay_selection_range(&uiFirstChunk, &uiLastChunk));
        Empty = make_state(uiHighlight, ROLLER_ED_INVALID_CHUNK_ID, 5u);
        roller_ed_overlay_set(&Empty);
        CHECK(!roller_ed_overlay_selection_range(&uiFirstChunk, &uiLastChunk));
    }

    /* Defence in depth behind the facade's own rejection: a bit this build
     * does not define is never stored. */
    {
        tEdOverlayState Unknown = make_state(
            ROLLER_ED_OVERLAY_SHOW_SURFACES | (1u << 14) | (1u << 31), 1u, 2u);

        roller_ed_overlay_set(&Unknown);
        CHECK(roller_ed_overlay_flags() == ROLLER_ED_OVERLAY_SHOW_SURFACES);
    }

    /* E7-S3: tower markers are editor furniture. Their surface emission is
     * visible solely under the marker flag, even in a surface-free view with
     * every class-mask bit clear. Other classes keep the E3A-S2 master/mask
     * contract unchanged. */
    {
        tEdOverlayState TowersOnly = make_masked_state(
            ROLLER_ED_OVERLAY_SHOW_TOWER_MARKERS, 0u, 0u, 0u, 0u);

        roller_ed_overlay_set(&TowersOnly);
        CHECK(roller_ed_overlay_surface_class_visible(
                  ROLLER_ED_SURFACE_CLASS_TOWER));
        CHECK(!roller_ed_overlay_surface_class_visible(
                  ROLLER_ED_SURFACE_CLASS_CENTER));
        CHECK(!roller_ed_overlay_wireframe_class_visible(
                  ROLLER_ED_SURFACE_CLASS_TOWER));

        TowersOnly.uiFlags = ROLLER_ED_OVERLAY_SHOW_SURFACES;
        TowersOnly.uiSurfaceClassMask =
            ROLLER_ED_OVERLAY_CLASS_BIT(ROLLER_ED_SURFACE_CLASS_TOWER);
        roller_ed_overlay_set(&TowersOnly);
        CHECK(!roller_ed_overlay_surface_class_visible(
                  ROLLER_ED_SURFACE_CLASS_TOWER));
    }

    /* A partial match is not a match, and no flag at all is never enabled. */
    {
        tEdOverlayState Pair = make_state(
            ROLLER_ED_OVERLAY_SHOW_SURFACES | ROLLER_ED_OVERLAY_SHOW_TEST_CAR,
            0u, 0u);

        roller_ed_overlay_set(&Pair);
        CHECK(roller_ed_overlay_enabled(
                  ROLLER_ED_OVERLAY_SHOW_SURFACES
                  | ROLLER_ED_OVERLAY_SHOW_TEST_CAR));
        CHECK(!roller_ed_overlay_enabled(
                  ROLLER_ED_OVERLAY_SHOW_SURFACES
                  | ROLLER_ED_OVERLAY_SHOW_WIREFRAME));
        CHECK(!roller_ed_overlay_enabled(0u));
    }

    /* E3A-S2: per-class surface and wireframe selection, keyed on the
     * canonical surface class. */
    {
        const uint32_t uiSurfaceOnly =
            ROLLER_ED_OVERLAY_CLASS_BIT(ROLLER_ED_SURFACE_CLASS_CENTER)
            | ROLLER_ED_OVERLAY_CLASS_BIT(ROLLER_ED_SURFACE_CLASS_ROOF);
        const uint32_t uiWireOnly =
            ROLLER_ED_OVERLAY_CLASS_BIT(ROLLER_ED_SURFACE_CLASS_ROOF)
            | ROLLER_ED_OVERLAY_CLASS_BIT(ROLLER_ED_SURFACE_CLASS_TOWER);
        tEdOverlayState Masked = make_masked_state(
            ROLLER_ED_OVERLAY_SHOW_SURFACES | ROLLER_ED_OVERLAY_SHOW_WIREFRAME,
            0u, 0u, uiSurfaceOnly, uiWireOnly);

        roller_ed_overlay_set(&Masked);
        for (uint32_t uiClass = 0u; uiClass < ROLLER_ED_SURFACE_CLASS_COUNT;
             uiClass++) {
            uint32_t uiBit = ROLLER_ED_OVERLAY_CLASS_BIT(uiClass);

            CHECK(roller_ed_overlay_surface_class_visible((uint16_t)uiClass)
                  == ((uiSurfaceOnly & uiBit) != 0u));
            CHECK(roller_ed_overlay_wireframe_class_visible((uint16_t)uiClass)
                  == ((uiWireOnly & uiBit) != 0u));
        }
        /* The two are independent: ROOF is both, CENTER is surface only,
         * TOWER is wireframe only, and a wall is neither. */
        CHECK(roller_ed_overlay_surface_class_visible(
                  ROLLER_ED_SURFACE_CLASS_ROOF));
        CHECK(roller_ed_overlay_wireframe_class_visible(
                  ROLLER_ED_SURFACE_CLASS_ROOF));
        CHECK(!roller_ed_overlay_wireframe_class_visible(
                  ROLLER_ED_SURFACE_CLASS_CENTER));
        CHECK(roller_ed_overlay_wireframe_class_visible(
                  ROLLER_ED_SURFACE_CLASS_TOWER));
        CHECK(!roller_ed_overlay_surface_class_visible(
                  ROLLER_ED_SURFACE_CLASS_TOWER));
        CHECK(!roller_ed_overlay_surface_class_visible(
                  ROLLER_ED_SURFACE_CLASS_LEFT_WALL));
        CHECK(!roller_ed_overlay_wireframe_class_visible(
                  ROLLER_ED_SURFACE_CLASS_LEFT_WALL));
        roller_ed_overlay_get(&State);
        CHECK(State.uiSurfaceClassMask == uiSurfaceOnly);
        CHECK(State.uiWireframeClassMask == uiWireOnly);
    }

    /* The master switch blanks the view without losing the per-class choice,
     * which is what lets the editor restore the checkboxes on re-enable. */
    {
        const uint32_t uiAll = (uint32_t)ROLLER_ED_OVERLAY_ALL_SURFACE_CLASSES;
        tEdOverlayState NoMasters = make_masked_state(0u, 0u, 0u, uiAll, uiAll);

        roller_ed_overlay_set(&NoMasters);
        for (uint32_t uiClass = 0u; uiClass < ROLLER_ED_SURFACE_CLASS_COUNT;
             uiClass++) {
            CHECK(!roller_ed_overlay_surface_class_visible((uint16_t)uiClass));
            CHECK(!roller_ed_overlay_wireframe_class_visible(
                      (uint16_t)uiClass));
        }
        roller_ed_overlay_get(&State);
        CHECK(State.uiSurfaceClassMask == uiAll);
        CHECK(State.uiWireframeClassMask == uiAll);
    }

    /* Bits past the last class are dropped rather than stored, matching the
     * flag rule; the facade refuses them outright before this point. */
    {
        tEdOverlayState Beyond = make_masked_state(
            ROLLER_ED_OVERLAY_SHOW_SURFACES, 0u, 0u, 0xffffffffu, 0xffffffffu);

        roller_ed_overlay_set(&Beyond);
        roller_ed_overlay_get(&State);
        CHECK(State.uiSurfaceClassMask
              == (uint32_t)ROLLER_ED_OVERLAY_ALL_SURFACE_CLASSES);
        CHECK(State.uiWireframeClassMask
              == (uint32_t)ROLLER_ED_OVERLAY_ALL_SURFACE_CLASSES);
        CHECK(!roller_ed_overlay_surface_class_visible(
                  (uint16_t)ROLLER_ED_SURFACE_CLASS_COUNT));
        CHECK(!roller_ed_overlay_surface_class_visible(0xffffu));
    }

    /*
     * E3A-S6 test-car selection. It is only published while SHOW_TEST_CAR is
     * set, and its chunk is the selection's first endpoint whether or not the
     * highlight itself is on -- the legacy editor drew the car on m_iSelFrom
     * regardless.
     */
    {
        uint32_t uiDesign = 0xFFFFFFFFu;
        uint32_t uiAiLine = 0xFFFFFFFFu;
        uint32_t uiChunk = 0xFFFFFFFFu;
        bool bMillionPlus = true;

        State = make_state(ROLLER_ED_OVERLAY_SHOW_SURFACES, 12u, 20u);
        State.uiTestCarDesign = 5u;
        State.uiTestCarAiLine = 2u;
        roller_ed_overlay_set(&State);
        CHECK(!roller_ed_overlay_test_car(&uiDesign, &uiAiLine, &uiChunk,
                                          &bMillionPlus, NULL));
        /* Refused means untouched: nothing was written. */
        CHECK(uiDesign == 0xFFFFFFFFu && bMillionPlus);

        /* Stored across the toggle, so ticking the box does not reset it. */
        State.uiFlags |= ROLLER_ED_OVERLAY_SHOW_TEST_CAR;
        roller_ed_overlay_set(&State);
        CHECK(roller_ed_overlay_test_car(&uiDesign, &uiAiLine, &uiChunk,
                                         &bMillionPlus, NULL));
        CHECK(uiDesign == 5u);
        CHECK(uiAiLine == 2u);
        CHECK(uiChunk == 12u);
        CHECK(!bMillionPlus);

        /* The highlight flag governs the outline, not the car. */
        State.uiFlags |= ROLLER_ED_OVERLAY_HIGHLIGHT_SELECTION;
        roller_ed_overlay_set(&State);
        CHECK(roller_ed_overlay_test_car(NULL, NULL, &uiChunk, NULL, NULL));
        CHECK(uiChunk == 12u);

        /* Nothing selected puts the car on chunk zero rather than on the
         * invalid sentinel. */
        State = make_state(ROLLER_ED_OVERLAY_SHOW_SURFACES
                               | ROLLER_ED_OVERLAY_SHOW_TEST_CAR
                               | ROLLER_ED_OVERLAY_TEST_CAR_MILLION_PLUS,
                           ROLLER_ED_INVALID_CHUNK_ID,
                           ROLLER_ED_INVALID_CHUNK_ID);
        State.uiTestCarDesign = 13u;
        State.uiTestCarAiLine = 3u;
        roller_ed_overlay_set(&State);
        CHECK(roller_ed_overlay_test_car(&uiDesign, &uiAiLine, &uiChunk,
                                         &bMillionPlus, NULL));
        CHECK(uiChunk == 0u);
        CHECK(uiDesign == 13u);
        CHECK(uiAiLine == 3u);
        CHECK(bMillionPlus);

        /* Round trip publishes both fields under this build's version. */
        roller_ed_overlay_get(&State);
        CHECK(State.uiVersion == ROLLER_ED_OVERLAY_STATE_VERSION);
        CHECK(State.uiStructSize == (uint32_t)sizeof(State));
        CHECK(State.uiTestCarDesign == 13u);
        CHECK(State.uiTestCarAiLine == 3u);

        /* Every output pointer is optional. */
        CHECK(roller_ed_overlay_test_car(NULL, NULL, NULL, NULL, NULL));
    }

    /* NULL is ignored rather than clearing state. */
    State = make_state(ROLLER_ED_OVERLAY_SHOW_SURFACES, 0u, 0u);
    roller_ed_overlay_set(&State);
    roller_ed_overlay_set(NULL);
    CHECK(roller_ed_overlay_flags() == ROLLER_ED_OVERLAY_SHOW_SURFACES);
    roller_ed_overlay_get(&State);
    CHECK(State.uiSurfaceClassMask
          == (uint32_t)ROLLER_ED_OVERLAY_ALL_SURFACE_CLASSES);
    roller_ed_overlay_get(NULL);

    /* Worker shutdown returns the defaults. */
    roller_ed_overlay_reset();
    CHECK(roller_ed_overlay_flags() == ROLLER_ED_OVERLAY_DEFAULT_FLAGS);
    CHECK(!roller_ed_overlay_selection_range(&uiFirstChunk, &uiLastChunk));
    CHECK(!roller_ed_overlay_test_car(NULL, NULL, NULL, NULL, NULL));
    roller_ed_overlay_get(&State);
    CHECK(State.uiTestCarDesign == ROLLER_ED_OVERLAY_DEFAULT_TEST_CAR_DESIGN);
    CHECK(State.uiTestCarAiLine == ROLLER_ED_OVERLAY_DEFAULT_TEST_CAR_AI_LINE);
    CHECK(roller_ed_overlay_surface_class_visible(
              ROLLER_ED_SURFACE_CLASS_CENTER));
    CHECK(!roller_ed_overlay_wireframe_class_visible(
              ROLLER_ED_SURFACE_CLASS_CENTER));

    puts("editor overlay state round-trip and selection tests passed");
    return 0;
}
