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
#include "car.h"
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
static uint8_t *s_pFirstDesign;

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
static int overlay_visible_from_chunk(tOverlayContext *pContext,
                                      uint32_t uiChunkId,
                                      const tEdOverlayState *pBaseState,
                                      const tEdOverlayState *pMarkedState,
                                      size_t *puiDifference)
{
    static const float afPitch[] = { -25.0f, -10.0f, 0.0f, 10.0f, 25.0f };
    static const float afDistance[] = { 4.0f, 8.0f };
    tEdOverlayState Base = *pBaseState;
    tEdOverlayState Marked = *pMarkedState;
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
                                "clearing the overlay on chunk %u left %zu "
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

/* E7-S3. Tower placement is core-authoritative, so aim directly at the world
 * position E7-S2 reports rather than reconstructing it from the chunk. The
 * base view deliberately has SHOW_SURFACES off and empty class masks: any
 * changed pixel therefore proves the tower-class visibility exception made
 * the marker independently visible. */
static int tower_marker_visible_from_position(
    tOverlayContext *pContext,
    const tEdTowerInfo *pTower,
    const tEdOverlayState *pBaseState,
    const tEdOverlayState *pMarkedState,
    size_t *puiDifference)
{
    static const float afYaw[] = { 0.0f, 90.0f, 180.0f, 270.0f };
    static const float afPitch[] = { -35.0f, -20.0f, 0.0f, 20.0f };
    static const float afDistance[] = { 1000.0f, 4000.0f };
    const float fRadiansPerDegree = 3.14159265358979f / 180.0f;

    *puiDifference = 0u;
    for (size_t iDistance = 0;
         iDistance < sizeof(afDistance) / sizeof(afDistance[0]);
         iDistance++) {
        for (size_t iYaw = 0; iYaw < sizeof(afYaw) / sizeof(afYaw[0]);
             iYaw++) {
            float fRadians = afYaw[iYaw] * fRadiansPerDegree;

            for (size_t iPitch = 0;
                 iPitch < sizeof(afPitch) / sizeof(afPitch[0]); iPitch++) {
                tEdCameraState Camera = {
                    .uiStructSize = sizeof(Camera),
                    .uiVersion = ROLLER_ED_CAMERA_STATE_VERSION,
                    .fPosition = {
                        pTower->fWorldPosition[0]
                            - cosf(fRadians) * afDistance[iDistance],
                        pTower->fWorldPosition[1]
                            - sinf(fRadians) * afDistance[iDistance],
                        pTower->fWorldPosition[2]
                            + afDistance[iDistance] * 0.25f
                    },
                    .fYawDegrees = afYaw[iYaw],
                    .fPitchDegrees = afPitch[iPitch]
                };

                if (RollerEd_SetCamera(&Camera) != ROLLER_ED_RESULT_OK) {
                    acceptance_error(pContext, "RollerEd_SetCamera failed");
                    return 0;
                }
                if (!render_with_overlay(pContext, pBaseState, s_pMarkerBase)
                        || !render_with_overlay(
                            pContext, pMarkedState, s_pFrame))
                    return 0;
                *puiDifference = differing_pixels(s_pFrame, s_pMarkerBase);
                if (*puiDifference == 0u)
                    continue;
                if (!render_with_overlay(pContext, pBaseState, s_pFrame))
                    return 0;
                if (memcmp(s_pFrame, s_pMarkerBase, FRAME_BYTES) != 0) {
                    acceptance_fail(
                        pContext,
                        "clearing the tower marker on chunk %u left %zu "
                        "pixels drawn",
                        pTower->uiChunkId,
                        differing_pixels(s_pFrame, s_pMarkerBase));
                    return 0;
                }
                return -1;
            }
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
    s_pFirstDesign = (uint8_t *)malloc(FRAME_BYTES);
    if (!s_pFrame || !s_pAllVisible || !s_pNothingVisible || !s_pMarkerBase
            || !s_pFirstDesign) {
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
     * frame, and switching it off again must return to it exactly. Four AI
     * lines cover more ground than the one centre line.
     */
    {
        static const struct
        {
            uint32_t uiFlag;
            const char *szName;
        } aHelpers[] = {
            { ROLLER_ED_OVERLAY_SHOW_AI_LINES, "AI lines" },
            { ROLLER_ED_OVERLAY_SHOW_CENTER_LINE, "centre line" }
        };
        size_t auiHelperDifference[2] = { 0, 0 };

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
        /* Four AI lines cover more ground than the single centre line. */
        if (auiHelperDifference[0] <= auiHelperDifference[1]) {
            acceptance_fail(pContext,
                            "the four AI lines covered %zu pixels against the "
                            "centre line's %zu",
                            auiHelperDifference[0], auiHelperDifference[1]);
            goto shutdown;
        }
        printf("helpers: AI lines %zu, centre line %zu pixels\n",
               auiHelperDifference[0], auiHelperDifference[1]);
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
            {
                tEdOverlayState MarkerBase = make_overlay(
                    ROLLER_ED_OVERLAY_SHOW_SURFACES,
                    ROLLER_ED_OVERLAY_ALL_SURFACE_CLASSES, 0u);
                tEdOverlayState Marked = make_overlay(
                    ROLLER_ED_OVERLAY_SHOW_SURFACES | aMarkers[i].uiFlag,
                    ROLLER_ED_OVERLAY_ALL_SURFACE_CLASSES, 0u);

                if (!overlay_visible_from_chunk(pContext,
                                                aMarkers[i].uiChunkId,
                                                &MarkerBase, &Marked,
                                                &auiMarkerDifference[i]))
                    goto shutdown;
            }
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

    /*
     * E7-S3. Every loaded tower is aimed at by the E7-S2-reported world
     * position, including modes the game would suppress with iEnabled <= -1.
     * The base has no surfaces and no class bits, proving the new flag alone
     * owns visibility. The last marker is then selected by its anchor chunk;
     * its outline must change the marker without any track surface present.
     */
    {
        uint32_t uiTowerCount = 0u;
        uint32_t uiSelectedChunk = ROLLER_ED_INVALID_CHUNK_ID;
        size_t uiSelectedMarkerPixels = 0u;
        tEdOverlayState TowerBase = make_overlay(0u, 0u, 0u);
        tEdOverlayState TowerMarkers = make_overlay(
            ROLLER_ED_OVERLAY_SHOW_TOWER_MARKERS, 0u, 0u);

        if (RollerEd_QueryTowerCount(&uiTowerCount) != ROLLER_ED_RESULT_OK) {
            acceptance_error(pContext, "RollerEd_QueryTowerCount failed");
            goto shutdown;
        }
        if (uiTowerCount == 0u) {
            acceptance_fail(pContext,
                            "the E7-S3 retail fixture contains no towers");
            goto shutdown;
        }
        for (uint32_t uiTower = 0u; uiTower < uiTowerCount; uiTower++) {
            tEdTowerInfo Info = {
                .uiStructSize = sizeof(Info),
                .uiVersion = ROLLER_ED_TOWER_INFO_VERSION
            };
            size_t uiDifference = 0u;

            if (RollerEd_QueryTower(uiTower, &Info) != ROLLER_ED_RESULT_OK) {
                acceptance_error(pContext, "RollerEd_QueryTower failed");
                goto shutdown;
            }
            if (!tower_marker_visible_from_position(
                    pContext, &Info, &TowerBase, &TowerMarkers,
                    &uiDifference))
                goto shutdown;
            if (uiDifference == 0u) {
                acceptance_fail(
                    pContext,
                    "tower %u on chunk %u drew nothing at its queried "
                    "position",
                    uiTower, Info.uiChunkId);
                goto shutdown;
            }
            uiSelectedChunk = Info.uiChunkId;
            uiSelectedMarkerPixels = uiDifference;
        }

        if (!render_with_overlay(pContext, &TowerMarkers, s_pMarkerBase))
            goto shutdown;
        TowerMarkers.uiFlags |= ROLLER_ED_OVERLAY_HIGHLIGHT_SELECTION;
        TowerMarkers.uiFirstSelectedChunk = uiSelectedChunk;
        TowerMarkers.uiLastSelectedChunk = uiSelectedChunk;
        if (!render_with_overlay(pContext, &TowerMarkers, s_pFrame))
            goto shutdown;
        if (memcmp(s_pFrame, s_pMarkerBase, FRAME_BYTES) == 0) {
            acceptance_fail(
                pContext,
                "selecting tower chunk %u did not highlight its marker",
                uiSelectedChunk);
            goto shutdown;
        }
        TowerMarkers.uiFlags &=
            ~(uint32_t)ROLLER_ED_OVERLAY_HIGHLIGHT_SELECTION;
        if (!render_with_overlay(pContext, &TowerMarkers, s_pFrame))
            goto shutdown;
        if (memcmp(s_pFrame, s_pMarkerBase, FRAME_BYTES) != 0) {
            acceptance_fail(
                pContext,
                "clearing the tower selection left %zu pixels highlighted",
                differing_pixels(s_pFrame, s_pMarkerBase));
            goto shutdown;
        }
        printf("tower markers: %u queried positions rendered; chunk %u "
               "covered %zu pixels and highlighted\n",
               uiTowerCount, uiSelectedChunk, uiSelectedMarkerPixels);
    }

    /*
     * E3A-S6. The test car stands on the selection's first chunk, so it is
     * aimed at the same way a marker is. Two things beyond "it draws": every
     * design has to be selectable, and the car must not move -- rendering the
     * same overlay twice has to give the same frame, which is the cheapest
     * direct evidence that no simulation is running behind it.
     */
    {
        static const uint32_t auiTestChunk[] = { 0u, 40u };
        uint32_t uiCarChunk = auiTestChunk[0];
        size_t uiCarDifference = 0;
        size_t uiFlippedDifference = 0;
        uint32_t uiDrawnDesigns = 0u;
        uint32_t uiDesignsUnlikeTheFirst = 0u;

        if ((uint32_t)TRAK_LEN > auiTestChunk[1])
            uiCarChunk = auiTestChunk[1];

        for (uint32_t uiDesign = 0u;
             uiDesign < ROLLER_ED_TEST_CAR_DESIGN_COUNT; uiDesign++) {
            tEdOverlayState CarBase = make_overlay(
                ROLLER_ED_OVERLAY_SHOW_SURFACES,
                ROLLER_ED_OVERLAY_ALL_SURFACE_CLASSES, 0u);
            tEdOverlayState WithCar = make_overlay(
                ROLLER_ED_OVERLAY_SHOW_SURFACES
                    | ROLLER_ED_OVERLAY_SHOW_TEST_CAR,
                ROLLER_ED_OVERLAY_ALL_SURFACE_CLASSES, 0u);
            size_t uiDifference = 0;

            /* The car's chunk is the selection's first endpoint, whether or
             * not the highlight itself is switched on. */
            CarBase.uiFirstSelectedChunk = uiCarChunk;
            CarBase.uiLastSelectedChunk = uiCarChunk;
            WithCar.uiFirstSelectedChunk = uiCarChunk;
            WithCar.uiLastSelectedChunk = uiCarChunk;
            WithCar.uiTestCarDesign = uiDesign;
            WithCar.uiTestCarAiLine = uiDesign % ROLLER_ED_TEST_CAR_AI_LINE_COUNT;

            if (!overlay_visible_from_chunk(pContext, uiCarChunk, &CarBase,
                                            &WithCar, &uiDifference))
                goto shutdown;
            if (uiDifference == 0u) {
                acceptance_fail(pContext,
                                "test car design %u drew nothing from any "
                                "camera aimed at chunk %u",
                                uiDesign, uiCarChunk);
                goto shutdown;
            }
            uiDrawnDesigns++;
            /*
             * overlay_visible_from_chunk leaves the *base* frame in s_pFrame,
             * because the last thing it does is check that clearing the flag
             * restores it exactly. Re-render the car at the camera it settled
             * on to get the frame this design actually produced.
             */
            if (!render_with_overlay(pContext, &WithCar, s_pFrame))
                goto shutdown;
            if (uiDesign == 0u) {
                uiCarDifference = uiDifference;
                memcpy(s_pFirstDesign, s_pFrame, FRAME_BYTES);
            } else if (memcmp(s_pFrame, s_pFirstDesign, FRAME_BYTES) != 0) {
                uiDesignsUnlikeTheFirst++;
            }
        }

        /*
         * The camera is wherever the last search left it. Render the same
         * state twice: a car that is being simulated would have moved, and a
         * car that is not cannot.
         */
        Overlay = make_overlay(
            ROLLER_ED_OVERLAY_SHOW_SURFACES | ROLLER_ED_OVERLAY_SHOW_TEST_CAR,
            ROLLER_ED_OVERLAY_ALL_SURFACE_CLASSES, 0u);
        Overlay.uiFirstSelectedChunk = uiCarChunk;
        Overlay.uiLastSelectedChunk = uiCarChunk;
        if (!render_with_overlay(pContext, &Overlay, s_pMarkerBase))
            goto shutdown;
        if (!render_with_overlay(pContext, &Overlay, s_pFrame))
            goto shutdown;
        if (memcmp(s_pFrame, s_pMarkerBase, FRAME_BYTES) != 0) {
            acceptance_fail(pContext,
                            "the test car moved between two identical frames "
                            "(%zu pixels differ) -- something is stepping it",
                            differing_pixels(s_pFrame, s_pMarkerBase));
            goto shutdown;
        }
        if (numcars != 0) {
            acceptance_fail(pContext,
                            "drawing the test car raised numcars to %d; E1-S6 "
                            "requires it to stay zero",
                            numcars);
            goto shutdown;
        }

        /*
         * The advanced-cars skin is the editor's Y model variant: the same
         * plan drawn from the `y*.bm` bank with the mirror palette remap. It
         * must change the picture without changing the design.
         */
        Overlay.uiFlags |= ROLLER_ED_OVERLAY_TEST_CAR_ADVANCED;
        if (!render_with_overlay(pContext, &Overlay, s_pFrame))
            goto shutdown;
        if (memcmp(s_pFrame, s_pMarkerBase, FRAME_BYTES) == 0) {
            acceptance_fail(pContext,
                            "the advanced-cars skin did not change the test "
                            "car -- the Y variant is drawing the X texture");
            goto shutdown;
        }
        Overlay.uiFlags &= ~(uint32_t)ROLLER_ED_OVERLAY_TEST_CAR_ADVANCED;
        if (!render_with_overlay(pContext, &Overlay, s_pFrame))
            goto shutdown;
        if (memcmp(s_pFrame, s_pMarkerBase, FRAME_BYTES) != 0) {
            acceptance_fail(pContext,
                            "clearing the advanced-cars skin left %zu pixels "
                            "changed",
                            differing_pixels(s_pFrame, s_pMarkerBase));
            goto shutdown;
        }

        /* Million Plus turns the car around, so it must change the frame
         * without changing whether the car is there at all. */
        Overlay.uiFlags |= ROLLER_ED_OVERLAY_TEST_CAR_MILLION_PLUS;
        if (!render_with_overlay(pContext, &Overlay, s_pFrame))
            goto shutdown;
        uiFlippedDifference = differing_pixels(s_pFrame, s_pMarkerBase);
        if (uiFlippedDifference == 0u) {
            acceptance_fail(pContext,
                            "Million Plus did not change the test car");
            goto shutdown;
        }

        /*
         * Picking a different car has to draw a different car. The GPU caches
         * one mesh per car slot, and a race never changes a slot's design, so
         * that cache was correct only by circumstance until the editor began
         * reusing one slot for every design the user picks.
         */
        if (uiDesignsUnlikeTheFirst != ROLLER_ED_TEST_CAR_DESIGN_COUNT - 1u) {
            /*
             * Every design has its own plan, and the camera is aimed at the
             * car, so all of them must differ. "Most of them" is the exact
             * shape of the cached-mesh bug: the body stays whichever design
             * was picked first while the per-design shadow, hitbox, and
             * colour remap still vary, so a weaker assertion passes.
             */
            acceptance_fail(pContext,
                            "only %u of %u test car designs rendered unlike "
                            "design 0 -- the car mesh is cached across a "
                            "design change",
                            uiDesignsUnlikeTheFirst,
                            (unsigned)ROLLER_ED_TEST_CAR_DESIGN_COUNT - 1u);
            goto shutdown;
        }

        printf("test car: %u/%u designs drew on chunk %u, %u unlike design 0 "
               "(design 0 covered %zu pixels; Million Plus moved %zu), "
               "numcars=%d\n",
               uiDrawnDesigns, (unsigned)ROLLER_ED_TEST_CAR_DESIGN_COUNT,
               uiCarChunk, uiDesignsUnlikeTheFirst, uiCarDifference,
               uiFlippedDifference, numcars);
    }

    /*
     * E3A-S7. A reference mesh the host supplies, placed on the track by its
     * own transform. Like the markers it is aimed at rather than hoped for:
     * the mesh is built around a known chunk's centre so a camera looking at
     * that chunk is looking at the mesh.
     */
    {
        static tEdReferenceVertex aVertices[3];
        tEdReferenceMesh Mesh;
        tEdOverlayState MeshBase = make_overlay(
            ROLLER_ED_OVERLAY_SHOW_SURFACES,
            ROLLER_ED_OVERLAY_ALL_SURFACE_CLASSES, 0u);
        tEdOverlayState WithMesh = make_overlay(
            ROLLER_ED_OVERLAY_SHOW_SURFACES
                | ROLLER_ED_OVERLAY_SHOW_REFERENCE_MESH,
            ROLLER_ED_OVERLAY_ALL_SURFACE_CLASSES, 0u);
        uint32_t uiMeshChunk = (uint32_t)TRAK_LEN > 40u ? 40u : 0u;
        float afCentre[3];
        float fRoadWidth;
        size_t uiSolidMesh = 0;
        size_t uiWireMesh = 0;

        if (!ed_helper_center_point(uiMeshChunk, afCentre)) {
            acceptance_fail(pContext, "chunk %u has no centre", uiMeshChunk);
            goto shutdown;
        }
        fRoadWidth = ed_helper_road_width(uiMeshChunk);

        /* A triangle a road-width across, lying flat above the chunk. Its
         * local coordinates are unit-scale so the mesh's own fScale is doing
         * real work rather than being 1. */
        memset(aVertices, 0, sizeof(aVertices));
        aVertices[0].fPosition[0] = -0.5f;
        aVertices[1].fPosition[0] = 0.5f;
        aVertices[2].fPosition[1] = 1.0f;
        for (size_t i = 0; i < 3u; ++i)
            aVertices[i].fNormal[2] = 1.0f;

        memset(&Mesh, 0, sizeof(Mesh));
        Mesh.uiStructSize = sizeof(Mesh);
        Mesh.uiVersion = ROLLER_ED_REFERENCE_MESH_VERSION;
        Mesh.pVertices = aVertices;
        Mesh.uiVertexCount = 3u;
        Mesh.fPosition[0] = afCentre[0];
        Mesh.fPosition[1] = afCentre[1];
        Mesh.fPosition[2] = afCentre[2] + fRoadWidth * 0.5f;
        Mesh.fScale[0] = fRoadWidth;
        Mesh.fScale[1] = fRoadWidth;
        Mesh.fScale[2] = fRoadWidth;
        Mesh.uiFlags = ROLLER_ED_REFERENCE_HAS_NORMALS;

        if (RollerEd_SetReferenceMesh(&Mesh) != ROLLER_ED_RESULT_OK) {
            acceptance_error(pContext, "RollerEd_SetReferenceMesh failed");
            goto shutdown;
        }
        if (!overlay_visible_from_chunk(pContext, uiMeshChunk, &MeshBase,
                                        &WithMesh, &uiSolidMesh))
            goto shutdown;
        if (uiSolidMesh == 0u) {
            acceptance_fail(pContext,
                            "the reference mesh drew nothing from any camera "
                            "aimed at chunk %u", uiMeshChunk);
            goto shutdown;
        }

        /* Wireframe is the same mesh as edges, so it must cover strictly less
         * than the filled triangle -- the same property the surface wireframe
         * and the selection outline are held to. */
        Mesh.uiFlags |= ROLLER_ED_REFERENCE_WIREFRAME;
        if (RollerEd_SetReferenceMesh(&Mesh) != ROLLER_ED_RESULT_OK) {
            acceptance_error(pContext, "RollerEd_SetReferenceMesh failed");
            goto shutdown;
        }
        if (!render_with_overlay(pContext, &MeshBase, s_pMarkerBase))
            goto shutdown;
        if (!render_with_overlay(pContext, &WithMesh, s_pFrame))
            goto shutdown;
        uiWireMesh = differing_pixels(s_pFrame, s_pMarkerBase);
        if (uiWireMesh == 0u || uiWireMesh >= uiSolidMesh) {
            acceptance_fail(pContext,
                            "the reference wireframe covered %zu pixels "
                            "against the solid mesh's %zu",
                            uiWireMesh, uiSolidMesh);
            goto shutdown;
        }

        /* A zero-vertex mesh clears it (AD-13), and the flag then draws
         * nothing at all. */
        Mesh.pVertices = NULL;
        Mesh.uiVertexCount = 0u;
        if (RollerEd_SetReferenceMesh(&Mesh) != ROLLER_ED_RESULT_OK) {
            acceptance_error(pContext, "clearing the reference mesh failed");
            goto shutdown;
        }
        if (!render_with_overlay(pContext, &WithMesh, s_pFrame))
            goto shutdown;
        if (memcmp(s_pFrame, s_pMarkerBase, FRAME_BYTES) != 0) {
            acceptance_fail(pContext,
                            "a cleared reference mesh still drew %zu pixels",
                            differing_pixels(s_pFrame, s_pMarkerBase));
            goto shutdown;
        }
        printf("reference mesh: solid %zu, wireframe %zu pixels on chunk %u\n",
               uiSolidMesh, uiWireMesh, uiMeshChunk);
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
    free(s_pFirstDesign);
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
