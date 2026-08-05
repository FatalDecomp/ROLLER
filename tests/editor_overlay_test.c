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

static tEdOverlayState make_state(uint32_t uiFlags,
                                  uint32_t uiFirstChunk,
                                  uint32_t uiLastChunk)
{
    tEdOverlayState State;

    memset(&State, 0, sizeof(State));
    State.uiStructSize = (uint32_t)sizeof(State);
    State.uiVersion = ROLLER_ED_OVERLAY_STATE_VERSION;
    State.uiFlags = uiFlags;
    State.uiFirstSelectedChunk = uiFirstChunk;
    State.uiLastSelectedChunk = uiLastChunk;
    return State;
}

int main(void)
{
    static const uint32_t auiFlags[] = {
        ROLLER_ED_OVERLAY_SHOW_SURFACES,
        ROLLER_ED_OVERLAY_SHOW_WIREFRAME,
        ROLLER_ED_OVERLAY_HIGHLIGHT_SELECTION,
        ROLLER_ED_OVERLAY_SHOW_AI_LINES,
        ROLLER_ED_OVERLAY_SHOW_CENTER_LINE,
        ROLLER_ED_OVERLAY_SHOW_ENVIRONMENT_FLOOR,
        ROLLER_ED_OVERLAY_SHOW_AUDIO_MARKERS,
        ROLLER_ED_OVERLAY_SHOW_STUNT_MARKERS,
        ROLLER_ED_OVERLAY_SHOW_TEST_CAR,
        ROLLER_ED_OVERLAY_SHOW_REFERENCE_MESH
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
        CHECK((uiKnownFlags & (1u << 10)) == 0u);
    }

    /* Defaults reproduce the E1-S6 track-only view. */
    roller_ed_overlay_reset();
    CHECK(roller_ed_overlay_flags() == ROLLER_ED_OVERLAY_DEFAULT_FLAGS);
    CHECK(roller_ed_overlay_enabled(ROLLER_ED_OVERLAY_SHOW_SURFACES));
    CHECK(!roller_ed_overlay_enabled(ROLLER_ED_OVERLAY_SHOW_WIREFRAME));
    CHECK(!roller_ed_overlay_enabled(
              ROLLER_ED_OVERLAY_HIGHLIGHT_SELECTION));
    CHECK(!roller_ed_overlay_selection_range(&uiFirstChunk, &uiLastChunk));
    roller_ed_overlay_get(&State);
    CHECK(State.uiStructSize == sizeof(State));
    CHECK(State.uiVersion == ROLLER_ED_OVERLAY_STATE_VERSION);
    CHECK(State.uiFlags == ROLLER_ED_OVERLAY_DEFAULT_FLAGS);
    CHECK(State.uiFirstSelectedChunk == ROLLER_ED_INVALID_CHUNK_ID);
    CHECK(State.uiLastSelectedChunk == ROLLER_ED_INVALID_CHUNK_ID);

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
            ROLLER_ED_OVERLAY_SHOW_SURFACES | (1u << 10) | (1u << 31), 1u, 2u);

        roller_ed_overlay_set(&Unknown);
        CHECK(roller_ed_overlay_flags() == ROLLER_ED_OVERLAY_SHOW_SURFACES);
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

    /* NULL is ignored rather than clearing state. */
    roller_ed_overlay_set(NULL);
    CHECK(roller_ed_overlay_flags()
          == (ROLLER_ED_OVERLAY_SHOW_SURFACES
              | ROLLER_ED_OVERLAY_SHOW_TEST_CAR));
    roller_ed_overlay_get(NULL);

    /* Worker shutdown returns the defaults. */
    roller_ed_overlay_reset();
    CHECK(roller_ed_overlay_flags() == ROLLER_ED_OVERLAY_DEFAULT_FLAGS);
    CHECK(!roller_ed_overlay_selection_range(&uiFirstChunk, &uiLastChunk));

    puts("editor overlay state round-trip and selection tests passed");
    return 0;
}
