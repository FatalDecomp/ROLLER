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

void roller_ed_overlay_reset(void)
{
    s_uiFlags = ROLLER_ED_OVERLAY_DEFAULT_FLAGS;
    s_uiFirstSelectedChunk = ROLLER_ED_INVALID_CHUNK_ID;
    s_uiLastSelectedChunk = ROLLER_ED_INVALID_CHUNK_ID;
}

void roller_ed_overlay_set(const tEdOverlayState *pState)
{
    if (!pState)
        return;
    s_uiFlags = pState->uiFlags & (uint32_t)ROLLER_ED_OVERLAY_KNOWN_FLAGS;
    s_uiFirstSelectedChunk = pState->uiFirstSelectedChunk;
    s_uiLastSelectedChunk = pState->uiLastSelectedChunk;
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
}

uint32_t roller_ed_overlay_flags(void)
{
    return s_uiFlags;
}

bool roller_ed_overlay_enabled(uint32_t uiFlag)
{
    return uiFlag != 0u && (s_uiFlags & uiFlag) == uiFlag;
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
