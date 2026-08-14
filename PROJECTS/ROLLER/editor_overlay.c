#include "editor_overlay.h"

/*
 * Only the fields the core actually consumes are retained.  uiStructSize and
 * uiVersion belong to the call, not to the state: the facade has already
 * validated them, and echoing a caller-sized header back would describe the
 * caller's build rather than this one.
 */
static uint32_t s_uiFlags = ROLLER_ED_OVERLAY_DEFAULT_FLAGS;
static uint32_t s_uiFirstSelectedChunk = ROLLER_ED_INVALID_CHUNK_ID;
static uint32_t s_uiLastSelectedChunk = ROLLER_ED_INVALID_CHUNK_ID;
static uint32_t s_uiSurfaceClassMask =
    ROLLER_ED_OVERLAY_DEFAULT_SURFACE_CLASS_MASK;
static uint32_t s_uiWireframeClassMask =
    ROLLER_ED_OVERLAY_DEFAULT_WIREFRAME_CLASS_MASK;
static uint32_t s_uiTestCarDesign = ROLLER_ED_OVERLAY_DEFAULT_TEST_CAR_DESIGN;
static uint32_t s_uiTestCarAiLine = ROLLER_ED_OVERLAY_DEFAULT_TEST_CAR_AI_LINE;

static bool overlay_class_selected(uint32_t uiMask, uint16_t unSurfaceClass)
{
    if (unSurfaceClass >= ROLLER_ED_SURFACE_CLASS_COUNT)
        return false;
    return (uiMask & ROLLER_ED_OVERLAY_CLASS_BIT(unSurfaceClass)) != 0u;
}

void roller_ed_overlay_reset(void)
{
    s_uiFlags = ROLLER_ED_OVERLAY_DEFAULT_FLAGS;
    s_uiFirstSelectedChunk = ROLLER_ED_INVALID_CHUNK_ID;
    s_uiLastSelectedChunk = ROLLER_ED_INVALID_CHUNK_ID;
    s_uiSurfaceClassMask = ROLLER_ED_OVERLAY_DEFAULT_SURFACE_CLASS_MASK;
    s_uiWireframeClassMask = ROLLER_ED_OVERLAY_DEFAULT_WIREFRAME_CLASS_MASK;
    s_uiTestCarDesign = ROLLER_ED_OVERLAY_DEFAULT_TEST_CAR_DESIGN;
    s_uiTestCarAiLine = ROLLER_ED_OVERLAY_DEFAULT_TEST_CAR_AI_LINE;
}

void roller_ed_overlay_set(const tEdOverlayState *pState)
{
    if (!pState)
        return;
    s_uiFlags = pState->uiFlags & (uint32_t)ROLLER_ED_OVERLAY_KNOWN_FLAGS;
    s_uiFirstSelectedChunk = pState->uiFirstSelectedChunk;
    s_uiLastSelectedChunk = pState->uiLastSelectedChunk;
    s_uiSurfaceClassMask = pState->uiSurfaceClassMask
        & (uint32_t)ROLLER_ED_OVERLAY_ALL_SURFACE_CLASSES;
    s_uiWireframeClassMask = pState->uiWireframeClassMask
        & (uint32_t)ROLLER_ED_OVERLAY_ALL_SURFACE_CLASSES;
    s_uiTestCarDesign = pState->uiTestCarDesign;
    s_uiTestCarAiLine = pState->uiTestCarAiLine;
}

void roller_ed_overlay_get(tEdOverlayState *pStateOut)
{
    if (!pStateOut)
        return;
    pStateOut->uiStructSize = (uint32_t)sizeof(*pStateOut);
    pStateOut->uiVersion = ROLLER_ED_OVERLAY_STATE_VERSION;
    pStateOut->uiFlags = s_uiFlags;
    pStateOut->uiFirstSelectedChunk = s_uiFirstSelectedChunk;
    pStateOut->uiLastSelectedChunk = s_uiLastSelectedChunk;
    pStateOut->uiSurfaceClassMask = s_uiSurfaceClassMask;
    pStateOut->uiWireframeClassMask = s_uiWireframeClassMask;
    pStateOut->uiTestCarDesign = s_uiTestCarDesign;
    pStateOut->uiTestCarAiLine = s_uiTestCarAiLine;
}

bool roller_ed_overlay_surface_class_visible(uint16_t unSurfaceClass)
{
    if (unSurfaceClass == ROLLER_ED_SURFACE_CLASS_TOWER) {
        return roller_ed_overlay_enabled(
            ROLLER_ED_OVERLAY_SHOW_TOWER_MARKERS);
    }
    return roller_ed_overlay_enabled(ROLLER_ED_OVERLAY_SHOW_SURFACES)
        && overlay_class_selected(s_uiSurfaceClassMask, unSurfaceClass);
}

bool roller_ed_overlay_wireframe_class_visible(uint16_t unSurfaceClass)
{
    return roller_ed_overlay_enabled(ROLLER_ED_OVERLAY_SHOW_WIREFRAME)
        && overlay_class_selected(s_uiWireframeClassMask, unSurfaceClass);
}

uint32_t roller_ed_overlay_flags(void)
{
    return s_uiFlags;
}

bool roller_ed_overlay_enabled(uint32_t uiFlag)
{
    return uiFlag != 0u && (s_uiFlags & uiFlag) == uiFlag;
}

bool roller_ed_overlay_track_segment_visible(uint32_t uiChunkId,
                                             uint32_t uiChunkCount)
{
    if (uiChunkCount == 0u || uiChunkId >= uiChunkCount)
        return false;
    return uiChunkId != uiChunkCount - 1u
        || !roller_ed_overlay_enabled(ROLLER_ED_OVERLAY_DETACH_LAST);
}

bool roller_ed_overlay_selection_range(uint32_t *puiFirstChunk,
                                       uint32_t *puiLastChunk)
{
    uint32_t uiFirstChunk = s_uiFirstSelectedChunk;
    uint32_t uiLastChunk = s_uiLastSelectedChunk;

    if (!roller_ed_overlay_enabled(ROLLER_ED_OVERLAY_HIGHLIGHT_SELECTION)
            || uiFirstChunk == ROLLER_ED_INVALID_CHUNK_ID
            || uiLastChunk == ROLLER_ED_INVALID_CHUNK_ID)
        return false;
    if (uiFirstChunk > uiLastChunk) {
        uint32_t uiTemp = uiFirstChunk;
        uiFirstChunk = uiLastChunk;
        uiLastChunk = uiTemp;
    }
    if (puiFirstChunk)
        *puiFirstChunk = uiFirstChunk;
    if (puiLastChunk)
        *puiLastChunk = uiLastChunk;
    return true;
}

bool roller_ed_overlay_test_car(uint32_t *puiDesign, uint32_t *puiAiLine,
                                uint32_t *puiChunk, bool *pbMillionPlus,
                                bool *pbAdvanced)
{
    if (!roller_ed_overlay_enabled(ROLLER_ED_OVERLAY_SHOW_TEST_CAR))
        return false;
    if (puiDesign)
        *puiDesign = s_uiTestCarDesign;
    if (puiAiLine)
        *puiAiLine = s_uiTestCarAiLine;
    /*
     * The legacy editor drew the car on m_iSelFrom, so the selection's first
     * endpoint is the car's chunk. It is read directly rather than through
     * roller_ed_overlay_selection_range(), because the car does not depend on
     * the highlight being switched on -- the editor publishes the endpoint
     * either way, and "nothing selected" simply means chunk zero, which is
     * where the legacy editor's own default left it.
     */
    if (puiChunk) {
        *puiChunk = s_uiFirstSelectedChunk == ROLLER_ED_INVALID_CHUNK_ID
            ? 0u
            : s_uiFirstSelectedChunk;
    }
    if (pbMillionPlus) {
        *pbMillionPlus = roller_ed_overlay_enabled(
            ROLLER_ED_OVERLAY_TEST_CAR_MILLION_PLUS);
    }
    if (pbAdvanced) {
        *pbAdvanced = roller_ed_overlay_enabled(
            ROLLER_ED_OVERLAY_TEST_CAR_ADVANCED);
    }
    return true;
}
