/*
 * E3A-S2 and E3A-S3 acceptance. Crosses the real facade and renderer with
 * retail assets and checks that the surface, wireframe, and selection toggles
 * actually change the frame.
 *
 * Every assertion here is camera-independent, because there is no camera from
 * which all fourteen surface classes and every chunk are visible, and no way
 * to pick one that stays right as the track data changes:
 *
 *   - hiding classes one at a time, or growing the selected chunk range, can
 *     only ever make more pixels differ from the all-visible frame, never
 *     fewer, whatever is in front of what;
 *   - hiding every class must reach exactly the frame with both masters off;
 *   - wireframe and selection outlines must cover less than the solid fill
 *     they sit on, which is what distinguishes an outline from a recolour;
 *   - clearing a toggle must reproduce the original frame byte for byte,
 *     because overlay state is the only thing that changed.
 */
#include "3d.h"
#include "drawtrk3.h"
#include "editor_api.h"
#include "editor_helpers.h"
#include "loadtrak.h"

#define SDL_MAIN_HANDLED 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { WIDTH = 640, HEIGHT = 480, ROW_PITCH = WIDTH * 4,
       FRAME_BYTES = ROW_PITCH * HEIGHT };

typedef struct
{
    const char *szTrackPath;
    const char *szAssetRoot;
    char szError[512];
    int iResult;
} tOverlayContext;

static uint8_t *s_pFrame;
static uint8_t *s_pAllVisible;
static uint8_t *s_pNothingVisible;
static uint8_t *s_pMarkerBase;

static void acceptance_error(tOverlayContext *pContext, const char *szMessage)
{
    snprintf(pContext->szError, sizeof(pContext->szError), "%s: %s",
             szMessage, RollerEd_GetLastError());
    pContext->iResult = 1;
}

static void acceptance_fail(tOverlayContext *pContext, const char *szFormat, ...)
{
    va_list Args;

    va_start(Args, szFormat);
    vsnprintf(pContext->szError, sizeof(pContext->szError), szFormat, Args);
    va_end(Args);
    pContext->iResult = 1;
}

static size_t differing_pixels(const uint8_t *pLeft, const uint8_t *pRight)
{
    size_t uiDiffering = 0;

    for (size_t i = 0; i + 4u <= (size_t)FRAME_BYTES; i += 4u) {
        if (memcmp(pLeft + i, pRight + i, 4u) != 0)
            uiDiffering++;
    }
    return uiDiffering;
}

static int frame_has_content(const uint8_t *pPixels)
{
    uint32_t uiFirst;

    memcpy(&uiFirst, pPixels, sizeof(uiFirst));
    for (size_t i = sizeof(uiFirst); i + sizeof(uint32_t) <= (size_t)FRAME_BYTES;
         i += sizeof(uint32_t)) {
        uint32_t uiPixel;
        memcpy(&uiPixel, pPixels + i, sizeof(uiPixel));
        if (uiPixel != uiFirst)
            return -1;
    }
    return 0;
}

static tEdOverlayState make_overlay(uint32_t uiFlags,
                                    uint32_t uiSurfaceClassMask,
                                    uint32_t uiWireframeClassMask)
{
    tEdOverlayState State;

    memset(&State, 0, sizeof(State));
    State.uiStructSize = sizeof(State);
    State.uiVersion = ROLLER_ED_OVERLAY_STATE_VERSION;
    State.uiFlags = uiFlags;
    State.uiFirstSelectedChunk = ROLLER_ED_INVALID_CHUNK_ID;
    State.uiLastSelectedChunk = ROLLER_ED_INVALID_CHUNK_ID;
    State.uiSurfaceClassMask = uiSurfaceClassMask;
    State.uiWireframeClassMask = uiWireframeClassMask;
    return State;
}

/* Applies an overlay state and renders one frame into pOut. */
static int render_with_overlay(tOverlayContext *pContext,
                               const tEdOverlayState *pOverlay,
                               uint8_t *pOut)
{
    if (RollerEd_SetOverlayState(pOverlay) != ROLLER_ED_RESULT_OK) {
        acceptance_error(pContext, "RollerEd_SetOverlayState failed");
        return 0;
    }
    memset(pOut, 0, FRAME_BYTES);
    if (RollerEd_RenderFrame(pOut, FRAME_BYTES, ROW_PITCH, WIDTH, HEIGHT,
                             ROLLER_ED_PIXEL_RGBA8) != ROLLER_ED_RESULT_OK) {
        acceptance_error(pContext, "RollerEd_RenderFrame failed");
        return 0;
    }
    return -1;
}

static int find_camera_showing_track(tOverlayContext *pContext)
{
    static const float afYaw[] = { 0.0f, 90.0f, 180.0f, 270.0f };
    static const float afPitch[] = { -25.0f, 0.0f, 25.0f };
    tEdOverlayState Defaults = make_overlay(
        ROLLER_ED_OVERLAY_SHOW_SURFACES,
        ROLLER_ED_OVERLAY_ALL_SURFACE_CLASSES, 0u);
    float fTargetX = 0.0f;
    float fTargetY = 0.0f;
    float fTargetZ = 0.0f;

    for (int iPoint = 0; iPoint < 6; ++iPoint) {
        fTargetX += TrakPt[0].pointAy[iPoint].fX;
        fTargetY += TrakPt[0].pointAy[iPoint].fY;
        fTargetZ += TrakPt[0].pointAy[iPoint].fZ;
    }
    fTargetX /= 6.0f;
    fTargetY /= 6.0f;
    fTargetZ /= 6.0f;

    for (size_t iPitch = 0; iPitch < sizeof(afPitch) / sizeof(afPitch[0]);
         ++iPitch) {
        for (size_t iYaw = 0; iYaw < sizeof(afYaw) / sizeof(afYaw[0]); ++iYaw) {
            tEdCameraState Camera = {
                .uiStructSize = sizeof(Camera),
                .uiVersion = ROLLER_ED_CAMERA_STATE_VERSION,
                .fPosition = { fTargetX - 4000.0f, fTargetY,
                               fTargetZ + 1600.0f },
                .fYawDegrees = afYaw[iYaw],
                .fPitchDegrees = afPitch[iPitch]
            };

            if (RollerEd_SetCamera(&Camera) != ROLLER_ED_RESULT_OK) {
                acceptance_error(pContext, "RollerEd_SetCamera failed");
                return 0;
            }
            if (!render_with_overlay(pContext, &Defaults, s_pAllVisible))
                return 0;
            if (frame_has_content(s_pAllVisible))
                return -1;
        }
    }
    acceptance_fail(pContext, "no camera produced visible track content");
    return 0;
}

/*
 * E3A-S5. Looks for a camera aimed at uiChunkId from which enabling uiFlag
 * changes the frame, and checks that clearing it again restores that camera's
 * own base frame byte for byte. The pitch sweep is there because a marker
 * hovers above its chunk and one fixed angle can leave it behind the geometry
 * in front of it; the sweep only decides *where* to look from, never whether
 * the marker had to appear -- the caller still fails if nothing ever drew.
 */
static int marker_visible_from_its_chunk(tOverlayContext *pContext,
                                         uint32_t uiChunkId,
                                         uint32_t uiFlag,
                                         size_t *puiDifference)
{
    static const float afPitch[] = { -25.0f, -10.0f, 0.0f, 10.0f, 25.0f };
    static const float afDistance[] = { 4.0f, 8.0f };
    tEdOverlayState Base = make_overlay(ROLLER_ED_OVERLAY_SHOW_SURFACES,
                                        ROLLER_ED_OVERLAY_ALL_SURFACE_CLASSES,
                                        0u);
    tEdOverlayState Marked = make_overlay(
        ROLLER_ED_OVERLAY_SHOW_SURFACES | uiFlag,
        ROLLER_ED_OVERLAY_ALL_SURFACE_CLASSES, 0u);
    float afTarget[3];
    float afBehind[3];
    float fRoadWidth = ed_helper_road_width(uiChunkId);
    uint32_t uiPreviousChunk =
        uiChunkId == 0u ? (uint32_t)TRAK_LEN - 1u : uiChunkId - 1u;

    *puiDifference = 0;
    if (!ed_helper_center_point(uiChunkId, afTarget)
            || !ed_helper_center_point(uiPreviousChunk, afBehind)
            || !(fRoadWidth > 0.0f)) {
        acceptance_fail(pContext, "chunk %u has no derivable centre",
                        uiChunkId);
        return 0;
    }

    for (size_t iDistance = 0;
         iDistance < sizeof(afDistance) / sizeof(afDistance[0]); ++iDistance) {
        for (size_t iPitch = 0;
             iPitch < sizeof(afPitch) / sizeof(afPitch[0]); ++iPitch) {
            tEdCameraState Camera = {
                .uiStructSize = sizeof(Camera),
                .uiVersion = ROLLER_ED_CAMERA_STATE_VERSION
            };
            float fScale = fRoadWidth * afDistance[iDistance];
            float fDeltaX = afTarget[0] - afBehind[0];
            float fDeltaY = afTarget[1] - afBehind[1];
            float fLength = sqrtf(fDeltaX * fDeltaX + fDeltaY * fDeltaY);

            if (!(fLength > 0.0f))
                continue;
            /* Stand back down the track from the marked chunk and look at
             * it, which is the view the editor's user would drive into. */
            Camera.fPosition[0] = afTarget[0] - fDeltaX / fLength * fScale;
            Camera.fPosition[1] = afTarget[1] - fDeltaY / fLength * fScale;
            Camera.fPosition[2] = afTarget[2] + fRoadWidth;
            Camera.fYawDegrees =
                atan2f(fDeltaY, fDeltaX) * 180.0f / 3.14159265358979f;
            Camera.fPitchDegrees = afPitch[iPitch];
            if (RollerEd_SetCamera(&Camera) != ROLLER_ED_RESULT_OK) {
                acceptance_error(pContext, "RollerEd_SetCamera failed");
                return 0;
            }

            if (!render_with_overlay(pContext, &Base, s_pMarkerBase))
                return 0;
            if (!render_with_overlay(pContext, &Marked, s_pFrame))
                return 0;
            *puiDifference = differing_pixels(s_pFrame, s_pMarkerBase);
            if (*puiDifference == 0u)
                continue;

            /* Clearing the flag is the only change, so the frame must come
             * back exactly. */
            if (!render_with_overlay(pContext, &Base, s_pFrame))
                return 0;
            if (memcmp(s_pFrame, s_pMarkerBase, FRAME_BYTES) != 0) {
                acceptance_fail(pContext,
                                "clearing the marker on chunk %u left %zu "
                                "pixels drawn",
                                uiChunkId,
                                differing_pixels(s_pFrame, s_pMarkerBase));
                return 0;
            }
            return -1;
        }
    }
    return -1;
}

static int SDLCALL overlay_worker(void *pUserData)
{
    tOverlayContext *pContext = (tOverlayContext *)pUserData;
    tRollerEdInitInfo InitInfo = {
        .uiStructSize = sizeof(InitInfo),
        .uiVersion = ROLLER_ED_INIT_INFO_VERSION,
        .szAssetRoot = pContext->szAssetRoot,
        .ePreferredRenderer = ROLLER_ED_RENDERER_GPU,
        .uiAllowSoftwareFallback = 0u
    };
    tEdGeometrySizes Sizes = {
        .uiStructSize = sizeof(Sizes),
        .uiVersion = ROLLER_ED_GEOMETRY_SIZES_VERSION
    };
    tEdOverlayState Overlay;
    uint32_t uiLoadedEpoch;
    uint32_t uiLoadedGeneration;
    size_t uiSolidDifference;
    size_t uiPreviousDifference = 0;
    size_t uiWireframeDifference;
    uint32_t uiHidden = 0u;
    int iChangedByHiding = 0;

    if (RollerEd_Init(&InitInfo) != ROLLER_ED_RESULT_OK) {
        acceptance_error(pContext, "RollerEd_Init failed");
        return pContext->iResult;
    }
    if (RollerEd_LoadTrackFile(pContext->szTrackPath, pContext->szAssetRoot)
            != ROLLER_ED_RESULT_OK) {
        acceptance_error(pContext, "RollerEd_LoadTrackFile failed");
        goto shutdown;
    }
    if (RollerEd_QueryGeometrySizes(&Sizes) != ROLLER_ED_RESULT_OK) {
        acceptance_error(pContext, "RollerEd_QueryGeometrySizes failed");
        goto shutdown;
    }
    uiLoadedEpoch = Sizes.uiGeometryEpoch;
    uiLoadedGeneration = Sizes.uiTrackGeneration;

    s_pFrame = (uint8_t *)malloc(FRAME_BYTES);
    s_pAllVisible = (uint8_t *)malloc(FRAME_BYTES);
    s_pNothingVisible = (uint8_t *)malloc(FRAME_BYTES);
    s_pMarkerBase = (uint8_t *)malloc(FRAME_BYTES);
    if (!s_pFrame || !s_pAllVisible || !s_pNothingVisible || !s_pMarkerBase) {
        acceptance_fail(pContext, "frame allocation failed");
        goto shutdown;
    }

    if (!find_camera_showing_track(pContext))
        goto shutdown;

    /* Both masters off: the track disappears and only the backdrop is left. */
    Overlay = make_overlay(0u, ROLLER_ED_OVERLAY_ALL_SURFACE_CLASSES,
                           ROLLER_ED_OVERLAY_ALL_SURFACE_CLASSES);
    if (!render_with_overlay(pContext, &Overlay, s_pNothingVisible))
        goto shutdown;
    uiSolidDifference = differing_pixels(s_pAllVisible, s_pNothingVisible);
    if (uiSolidDifference == 0u) {
        acceptance_fail(pContext,
                        "clearing both masters left the frame unchanged");
        goto shutdown;
    }

    /*
     * Hide one class at a time, cumulatively. Each step can only add to the
     * pixels that differ from the all-visible frame, and the last step must
     * land exactly on the frame with the masters off.
     */
    for (uint32_t uiClass = 0u; uiClass < ROLLER_ED_SURFACE_CLASS_COUNT;
         uiClass++) {
        size_t uiDifference;

        uiHidden |= ROLLER_ED_OVERLAY_CLASS_BIT(uiClass);
        Overlay = make_overlay(
            ROLLER_ED_OVERLAY_SHOW_SURFACES,
            (uint32_t)ROLLER_ED_OVERLAY_ALL_SURFACE_CLASSES & ~uiHidden, 0u);
        if (!render_with_overlay(pContext, &Overlay, s_pFrame))
            goto shutdown;
        uiDifference = differing_pixels(s_pAllVisible, s_pFrame);
        if (uiDifference < uiPreviousDifference) {
            acceptance_fail(pContext,
                            "hiding surface class %u restored %zu pixels that "
                            "hiding fewer classes had already removed",
                            uiClass, uiPreviousDifference - uiDifference);
            goto shutdown;
        }
        if (uiDifference > uiPreviousDifference)
            iChangedByHiding++;
        uiPreviousDifference = uiDifference;
    }
    if (memcmp(s_pFrame, s_pNothingVisible, FRAME_BYTES) != 0) {
        acceptance_fail(pContext,
                        "hiding every surface class did not match clearing the "
                        "master switch (%zu pixels differ)",
                        differing_pixels(s_pFrame, s_pNothingVisible));
        goto shutdown;
    }
    if (iChangedByHiding < 2) {
        acceptance_fail(pContext,
                        "only %d surface class(es) changed the frame; the "
                        "per-class mask is not reaching the renderer",
                        iChangedByHiding);
        goto shutdown;
    }

    /* Wireframe with no fill: visible, and thinner than the solid surfaces it
     * replaces. */
    Overlay = make_overlay(ROLLER_ED_OVERLAY_SHOW_WIREFRAME, 0u,
                           ROLLER_ED_OVERLAY_ALL_SURFACE_CLASSES);
    if (!render_with_overlay(pContext, &Overlay, s_pFrame))
        goto shutdown;
    uiWireframeDifference = differing_pixels(s_pFrame, s_pNothingVisible);
    if (uiWireframeDifference == 0u) {
        acceptance_fail(pContext, "wireframe-only rendering drew nothing");
        goto shutdown;
    }
    if (uiWireframeDifference >= uiSolidDifference) {
        acceptance_fail(pContext,
                        "wireframe covered %zu pixels, no less than the solid "
                        "surfaces' %zu -- it is not drawing outlines",
                        uiWireframeDifference, uiSolidDifference);
        goto shutdown;
    }

    /* Restoring the defaults reproduces the original frame exactly: overlay
     * state is the only thing that moved. */
    Overlay = make_overlay(ROLLER_ED_OVERLAY_SHOW_SURFACES,
                           ROLLER_ED_OVERLAY_ALL_SURFACE_CLASSES, 0u);
    if (!render_with_overlay(pContext, &Overlay, s_pFrame))
        goto shutdown;
    if (memcmp(s_pFrame, s_pAllVisible, FRAME_BYTES) != 0) {
        acceptance_fail(pContext,
                        "restoring the default overlay did not reproduce the "
                        "original frame (%zu pixels differ)",
                        differing_pixels(s_pFrame, s_pAllVisible));
        goto shutdown;
    }

    /*
     * E3A-S3. Same camera-independent shape as the class masks: growing the
     * selected chunk range can only ever outline more, never less.
     */
    {
        size_t uiSelectionDifference = 0;
        size_t uiPreviousSelection = 0;

        for (uint32_t uiLastChunk = 0u; uiLastChunk < (uint32_t)TRAK_LEN;
             uiLastChunk += 60u) {
            size_t uiDifference;

            Overlay = make_overlay(
                ROLLER_ED_OVERLAY_SHOW_SURFACES
                    | ROLLER_ED_OVERLAY_HIGHLIGHT_SELECTION,
                ROLLER_ED_OVERLAY_ALL_SURFACE_CLASSES, 0u);
            Overlay.uiFirstSelectedChunk = 0u;
            Overlay.uiLastSelectedChunk = uiLastChunk;
            if (!render_with_overlay(pContext, &Overlay, s_pFrame))
                goto shutdown;
            uiDifference = differing_pixels(s_pFrame, s_pAllVisible);
            if (uiDifference < uiPreviousSelection) {
                acceptance_fail(pContext,
                                "extending the selection to chunk %u removed "
                                "%zu outlined pixels",
                                uiLastChunk, uiPreviousSelection - uiDifference);
                goto shutdown;
            }
            uiPreviousSelection = uiDifference;
        }
        uiSelectionDifference = uiPreviousSelection;
        if (uiSelectionDifference == 0u) {
            acceptance_fail(pContext, "the selection highlight drew nothing");
            goto shutdown;
        }
        /* Outlines, not a recolour: the highlight must cover far less than
         * the surfaces it sits on. */
        if (uiSelectionDifference >= uiSolidDifference) {
            acceptance_fail(pContext,
                            "the selection covered %zu pixels against the "
                            "surfaces' %zu -- it is filling, not outlining",
                            uiSelectionDifference, uiSolidDifference);
            goto shutdown;
        }

        /* The flag governs: the same range with the highlight off, and the
         * sentinel range with it on, both return exactly to the base frame. */
        Overlay.uiFlags = ROLLER_ED_OVERLAY_SHOW_SURFACES;
        if (!render_with_overlay(pContext, &Overlay, s_pFrame))
            goto shutdown;
        if (memcmp(s_pFrame, s_pAllVisible, FRAME_BYTES) != 0) {
            acceptance_fail(pContext,
                            "clearing HIGHLIGHT_SELECTION left %zu pixels "
                            "outlined",
                            differing_pixels(s_pFrame, s_pAllVisible));
            goto shutdown;
        }
        Overlay.uiFlags = ROLLER_ED_OVERLAY_SHOW_SURFACES
            | ROLLER_ED_OVERLAY_HIGHLIGHT_SELECTION;
        Overlay.uiFirstSelectedChunk = ROLLER_ED_INVALID_CHUNK_ID;
        Overlay.uiLastSelectedChunk = ROLLER_ED_INVALID_CHUNK_ID;
        if (!render_with_overlay(pContext, &Overlay, s_pFrame))
            goto shutdown;
        if (memcmp(s_pFrame, s_pAllVisible, FRAME_BYTES) != 0) {
            acceptance_fail(pContext,
                            "an empty selection outlined %zu pixels",
                            differing_pixels(s_pFrame, s_pAllVisible));
            goto shutdown;
        }

        /* A selected chunk keeps its texture: the fill is untouched, so
         * hiding the surfaces must remove strictly more than the outline
         * added. */
        printf("selection outlined %zu pixels over the solid view\n",
               uiSelectionDifference);
    }

    /*
     * E3A-S4. Each helper is its own flag: switching one on must change the
     * frame, and switching it off again must return to it exactly. The floor
     * is a filled plane and the lines are ribbons, so the floor must cover
     * more than either line does.
     */
    {
        static const struct
        {
            uint32_t uiFlag;
            const char *szName;
        } aHelpers[] = {
            { ROLLER_ED_OVERLAY_SHOW_ENVIRONMENT_FLOOR, "environment floor" },
            { ROLLER_ED_OVERLAY_SHOW_AI_LINES, "AI lines" },
            { ROLLER_ED_OVERLAY_SHOW_CENTER_LINE, "centre line" }
        };
        size_t auiHelperDifference[3] = { 0, 0, 0 };

        for (size_t i = 0; i < sizeof(aHelpers) / sizeof(aHelpers[0]); ++i) {
            Overlay = make_overlay(
                ROLLER_ED_OVERLAY_SHOW_SURFACES | aHelpers[i].uiFlag,
                ROLLER_ED_OVERLAY_ALL_SURFACE_CLASSES, 0u);
            if (!render_with_overlay(pContext, &Overlay, s_pFrame))
                goto shutdown;
            auiHelperDifference[i] =
                differing_pixels(s_pFrame, s_pAllVisible);
            if (auiHelperDifference[i] == 0u) {
                acceptance_fail(pContext, "the %s helper drew nothing",
                                aHelpers[i].szName);
                goto shutdown;
            }

            Overlay.uiFlags = ROLLER_ED_OVERLAY_SHOW_SURFACES;
            if (!render_with_overlay(pContext, &Overlay, s_pFrame))
                goto shutdown;
            if (memcmp(s_pFrame, s_pAllVisible, FRAME_BYTES) != 0) {
                acceptance_fail(pContext,
                                "clearing the %s helper left %zu pixels drawn",
                                aHelpers[i].szName,
                                differing_pixels(s_pFrame, s_pAllVisible));
                goto shutdown;
            }
        }
        if (auiHelperDifference[0] <= auiHelperDifference[2]) {
            acceptance_fail(pContext,
                            "the environment floor covered %zu pixels against "
                            "the centre line's %zu -- it is not a filled plane",
                            auiHelperDifference[0], auiHelperDifference[2]);
            goto shutdown;
        }
        printf("helpers: floor %zu, AI lines %zu, centre line %zu pixels\n",
               auiHelperDifference[0], auiHelperDifference[1],
               auiHelperDifference[2]);
    }

    /*
     * E3A-S5. Unlike the lines and the floor, markers exist only on the
     * handful of chunks the track data marks, so the fixed camera above is
     * the wrong instrument: seven ramps on a 491-chunk track are usually
     * nowhere near it, and a "nothing drew" result would say more about the
     * camera than about the overlay. The camera is therefore aimed at a
     * marked chunk, which is what the story actually asks to be shown, and
     * the assertions stay data-driven: a flag whose data is absent must draw
     * nothing at all, a flag whose data is present must draw something from
     * a camera looking at it, and clearing it must be exact either way.
     */
    {
        size_t uiAudioChunks = 0;
        uint32_t uiFirstAudioChunk = ROLLER_ED_INVALID_CHUNK_ID;
        uint32_t uiFirstStuntChunk = ROLLER_ED_INVALID_CHUNK_ID;
        size_t uiStunts = (size_t)ed_helper_stunt_count();
        size_t auiMarkerDifference[2] = { 0, 0 };
        struct
        {
            uint32_t uiFlag;
            const char *szName;
            size_t uiPlaced;
            uint32_t uiChunkId;
        } aMarkers[2];

        for (int iChunk = 0; iChunk < TRAK_LEN; iChunk++) {
            if (!ed_helper_chunk_has_audio((uint32_t)iChunk))
                continue;
            if (uiAudioChunks == 0u)
                uiFirstAudioChunk = (uint32_t)iChunk;
            uiAudioChunks++;
        }
        for (uint32_t uiStunt = 0; uiStunt < (uint32_t)uiStunts; uiStunt++) {
            if (ed_helper_stunt_chunk(uiStunt, &uiFirstStuntChunk))
                break;
        }
        aMarkers[0].uiFlag = ROLLER_ED_OVERLAY_SHOW_AUDIO_MARKERS;
        aMarkers[0].szName = "audio";
        aMarkers[0].uiPlaced = uiAudioChunks;
        aMarkers[0].uiChunkId = uiFirstAudioChunk;
        aMarkers[1].uiFlag = ROLLER_ED_OVERLAY_SHOW_STUNT_MARKERS;
        aMarkers[1].szName = "stunt";
        aMarkers[1].uiPlaced = uiStunts;
        aMarkers[1].uiChunkId = uiFirstStuntChunk;

        for (size_t i = 0; i < 2u; ++i) {
            if (aMarkers[i].uiPlaced == 0u) {
                /*
                 * No data, so the flag must be inert. Both frames are taken
                 * at whatever camera is current rather than against
                 * s_pAllVisible, because a previous marker's search will have
                 * moved the camera and the comparison has to be of two frames
                 * that differ only in the flag.
                 */
                Overlay = make_overlay(
                    ROLLER_ED_OVERLAY_SHOW_SURFACES,
                    ROLLER_ED_OVERLAY_ALL_SURFACE_CLASSES, 0u);
                if (!render_with_overlay(pContext, &Overlay, s_pMarkerBase))
                    goto shutdown;
                Overlay.uiFlags =
                    ROLLER_ED_OVERLAY_SHOW_SURFACES | aMarkers[i].uiFlag;
                if (!render_with_overlay(pContext, &Overlay, s_pFrame))
                    goto shutdown;
                if (memcmp(s_pFrame, s_pMarkerBase, FRAME_BYTES) != 0) {
                    acceptance_fail(pContext,
                                    "the track carries no %s markers but the "
                                    "flag changed %zu pixels",
                                    aMarkers[i].szName,
                                    differing_pixels(s_pFrame, s_pMarkerBase));
                    goto shutdown;
                }
                continue;
            }
            if (aMarkers[i].uiChunkId == ROLLER_ED_INVALID_CHUNK_ID) {
                acceptance_fail(pContext,
                                "the track carries %zu %s marker(s) but none "
                                "named a loaded chunk",
                                aMarkers[i].uiPlaced, aMarkers[i].szName);
                goto shutdown;
            }
            if (!marker_visible_from_its_chunk(pContext, aMarkers[i].uiChunkId,
                                               aMarkers[i].uiFlag,
                                               &auiMarkerDifference[i]))
                goto shutdown;
            if (auiMarkerDifference[i] == 0u) {
                acceptance_fail(pContext,
                                "the %s marker on chunk %u drew nothing from "
                                "any camera aimed at that chunk",
                                aMarkers[i].szName, aMarkers[i].uiChunkId);
                goto shutdown;
            }
        }

        printf("markers: %zu audio chunks (chunk %u -> %zu pixels), %zu ramps "
               "(chunk %u -> %zu pixels)\n",
               uiAudioChunks, uiFirstAudioChunk, auiMarkerDifference[0],
               uiStunts, uiFirstStuntChunk, auiMarkerDifference[1]);
    }

    /* AD-7d on the real facade: none of that touched authored geometry. */
    Sizes.uiStructSize = sizeof(Sizes);
    Sizes.uiVersion = ROLLER_ED_GEOMETRY_SIZES_VERSION;
    if (RollerEd_QueryGeometrySizes(&Sizes) != ROLLER_ED_RESULT_OK) {
        acceptance_error(pContext, "RollerEd_QueryGeometrySizes failed");
        goto shutdown;
    }
    if (Sizes.uiGeometryEpoch != uiLoadedEpoch
            || Sizes.uiTrackGeneration != uiLoadedGeneration) {
        acceptance_fail(pContext,
                        "overlay changes moved the geometry epoch (%u -> %u) "
                        "or track generation (%u -> %u)",
                        uiLoadedEpoch, Sizes.uiGeometryEpoch,
                        uiLoadedGeneration, Sizes.uiTrackGeneration);
        goto shutdown;
    }

    printf("solid=%zu wireframe=%zu pixels over the empty view; %d classes "
           "changed the frame\n",
           uiSolidDifference, uiWireframeDifference, iChangedByHiding);

shutdown:
    free(s_pFrame);
    free(s_pAllVisible);
    free(s_pNothingVisible);
    free(s_pMarkerBase);
    if (RollerEd_Shutdown() != ROLLER_ED_RESULT_OK && !pContext->iResult)
        acceptance_error(pContext, "RollerEd_Shutdown failed");
    return pContext->iResult;
}

int main(int argc, char **argv)
{
    tRollerEdBootstrapInfo BootstrapInfo = {
        .uiStructSize = sizeof(BootstrapInfo),
        .uiVersion = ROLLER_ED_BOOTSTRAP_INFO_VERSION,
        .uiFlags = 0u
    };
    tOverlayContext Context;
    SDL_Thread *pWorker;
    int iWorkerResult = 1;

    if (argc != 3) {
        fprintf(stderr, "usage: %s ABSOLUTE_TRACK_PATH ABSOLUTE_ASSET_ROOT\n",
                argv[0]);
        return 2;
    }
    memset(&Context, 0, sizeof(Context));
    Context.szTrackPath = argv[1];
    Context.szAssetRoot = argv[2];

    SDL_SetMainReady();
    if (RollerEd_Bootstrap(&BootstrapInfo) != ROLLER_ED_RESULT_OK) {
        fprintf(stderr, "RollerEd_Bootstrap failed: %s\n",
                RollerEd_GetLastError());
        return 1;
    }
    pWorker = SDL_CreateThread(overlay_worker, "editor-overlay", &Context);
    if (!pWorker) {
        fprintf(stderr, "worker creation failed: %s\n", SDL_GetError());
        RollerEd_Teardown();
        return 1;
    }
    SDL_WaitThread(pWorker, &iWorkerResult);
    if (RollerEd_Teardown() != ROLLER_ED_RESULT_OK && iWorkerResult == 0) {
        fprintf(stderr, "RollerEd_Teardown failed: %s\n",
                RollerEd_GetLastError());
        return 1;
    }
    if (iWorkerResult != 0) {
        fprintf(stderr, "E3A-S2 acceptance failed: %s\n", Context.szError);
        return 1;
    }
    puts("E3A-S2/S3 PASS: per-class surface toggles hide monotonically, "
         "hiding every class matches the master switch, wireframe and "
         "selection outlines draw thinner than solid fills, growing the "
         "selection only ever outlines more, and clearing either returns to "
         "the original frame exactly");
    return 0;
}
