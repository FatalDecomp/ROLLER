#include "editor_test_car.h"

#include "3d.h"
#include "car.h"
#include "carplans.h"
#include "editor_helpers.h"
#include "editor_overlay.h"
#include "game_render.h"
#include "graphics.h"
#include "loadtrak.h"
#if defined(ROLLER_EDITOR_CORE)
#include "roller_core_error.h"
#endif

#include <math.h>
#include <stdio.h>
#include <string.h>

/* The car slot the editor owns. Nothing else writes Car[] while numcars is
 * zero, so slot 0 is free by construction. */
#define ED_TEST_CAR_SLOT 0

/* The texture bank the editor loads the design's car texture into.
 * LoadCarTexture's slot argument is one-based, and 3d.c registers cartex_vga[i]
 * as bank i + 1, so slot 1 is bank 1. */
#define ED_TEST_CAR_TEXTURE_SLOT 1u

/* The legacy 14-bit angle circle every ROLLER heading uses. */
#define ED_TEST_CAR_ANGLE_MASK 0x3FFF
#define ED_TEST_CAR_ANGLE_COUNT 16384.0
#define ED_TEST_CAR_HALF_TURN 0x2000
#define ED_TEST_CAR_PI 3.14159265358979323846

/* Nothing prepared yet. A real design index is always < 14. */
#define ED_TEST_CAR_NO_DESIGN 0xFFFFFFFFu

static uint32_t s_uiPreparedDesign = ED_TEST_CAR_NO_DESIGN;
static bool s_bPreparedAdvanced = false;
static bool s_bCarSizesReady = false;

void ed_test_car_reset(void)
{
    s_uiPreparedDesign = ED_TEST_CAR_NO_DESIGN;
    s_bPreparedAdvanced = false;
    /*
     * CalcCarSizes() only reads the static car plans, so what it computed
     * stays valid across a reload and does not need recomputing. The design
     * is forgotten because the texture banks do not survive a renderer.
     */
}

/*
 * LoadCarTexture opens car_texture_names[carType] by bare name, which in the
 * editor resolves against the process working directory rather than against
 * the document. E1-S4 solved the same problem for the track's own texture
 * banks by putting an absolute path into the legacy filename buffer; the same
 * trick applies here, and the table is restored afterwards so the game build
 * still sees the names it expects.
 */
static bool ed_test_car_load_texture(eCarType CarType, bool bAdvanced)
{
    char szOriginal[sizeof(car_texture_names[0])];
    char szWanted[sizeof(car_texture_names[0])];
    char szResolved[ROLLER_MAX_PATH];
    const int iSavedTexturesOff = textures_off;
    bool bLoaded;

    snprintf(szOriginal, sizeof(szOriginal), "%s", car_texture_names[CarType]);
    snprintf(szWanted, sizeof(szWanted), "%s", szOriginal);

    /*
     * The advanced set is the same file name with a leading 'y' instead of
     * 'x' -- LoadCarTexture does that substitution itself, in place, on the
     * first character of the name buffer. That cannot be left to it here,
     * because the buffer is about to hold an absolute path and the swap would
     * rewrite its drive letter. Do it on the bare name instead, resolve
     * *that*, and clear the flag across the call so the loader leaves the
     * path alone.
     *
     * Only carTypes 1..8 have a Y variant; F1WACK and DEATH do not, which is
     * the same range the loader guards and the same set the editor offers X/Y
     * pairs for.
     */
    if (bAdvanced && CarType >= CAR_AUTO && CarType <= CAR_REISE
            && (szWanted[0] == 'x' || szWanted[0] == 'X'))
        szWanted[0] = (szWanted[0] == 'X') ? 'Y' : 'y';

    if (loadtrack_resolve_editor_asset(szWanted, szResolved)) {
        snprintf(car_texture_names[CarType],
                 sizeof(car_texture_names[CarType]), "%s", szResolved);
    } else {
        snprintf(car_texture_names[CarType],
                 sizeof(car_texture_names[CarType]), "%s", szWanted);
    }
    textures_off &= ~TEX_OFF_ADVANCED_CARS;

    /*
     * LoadCarTexture allocates a fresh bank and overwrites the slot without
     * freeing what was there. In the game that happens once per texture at
     * load time, so it never mattered; the editor reloads this one slot every
     * time the user picks a different car, which would leak a bank per
     * change. Release it first, the same way the frontend does when it swaps
     * its preview car (frontend_screens.c).
     */
    fre((void **)&cartex_vga[ED_TEST_CAR_TEXTURE_SLOT - 1u]);

#if defined(ROLLER_EDITOR_CORE)
    /* E0-S7 turned the loader's fatal dialog into a recorded error, so a
     * missing car bank is recoverable here. The check is gated because
     * roller_core_error_* exists only in the editor core; the game compiles
     * this module but never reaches it, the same arrangement the helper and
     * selection passes already use. */
    roller_core_error_clear();
#endif
    LoadCarTexture((int)CarType, (uint8)ED_TEST_CAR_TEXTURE_SLOT);
    bLoaded = cartex_vga[ED_TEST_CAR_TEXTURE_SLOT - 1u] != NULL;
#if defined(ROLLER_EDITOR_CORE)
    if (roller_core_error_pending())
        bLoaded = false;
#endif

    snprintf(car_texture_names[CarType],
             sizeof(car_texture_names[CarType]), "%s", szOriginal);
    textures_off = iSavedTexturesOff;
    return bLoaded;
}

bool ed_test_car_prepare(GameRenderer *pRenderer, uint32_t uiDesign,
                         bool bAdvanced)
{
    eCarType CarType;
    int iTextureHeight;

    if (!pRenderer || uiDesign >= ROLLER_ED_TEST_CAR_DESIGN_COUNT)
        return false;
    /* The skin is part of what was prepared: switching X to Y reloads a
     * different texture bank even though the design is unchanged. */
    if (uiDesign == s_uiPreparedDesign && bAdvanced == s_bPreparedAdvanced)
        return true;

    /*
     * The hitbox and CarBaseX/Y/Diag tables the car draw reads are normally
     * built by InitCars(), which the editor never calls -- it is the same
     * function that hands numcars its designs. CalcCarSizes() is the half of
     * it that only reads the static plans, so it is safe on its own and is
     * all the draw path actually needs.
     */
    if (!s_bCarSizesReady) {
        CalcCarSizes();
        s_bCarSizesReady = true;
    }

    CarType = CarDesigns[uiDesign].carType;
    if (!ed_test_car_load_texture(CarType, bAdvanced))
        return false;

    /* car_texmap is indexed by car slot, not by design -- placecars fills it
     * per car, and the draw path looks up car_texmap[carIdx]. */
    car_texmap[ED_TEST_CAR_SLOT] = (int)ED_TEST_CAR_TEXTURE_SLOT;
    car_texs_loaded[CarType] = (int)ED_TEST_CAR_TEXTURE_SLOT;
    LoadCarTextures = (int)ED_TEST_CAR_TEXTURE_SLOT + 1;

    iTextureHeight = gfx_size
        ? ((num_textures[ED_TEST_CAR_TEXTURE_SLOT - 1u] + 7) / 8) * 32
        : ((num_textures[ED_TEST_CAR_TEXTURE_SLOT - 1u] + 3) / 4) * 64;
    game_render_load_texture(pRenderer,
                             cartex_vga[ED_TEST_CAR_TEXTURE_SLOT - 1u], 256,
                             iTextureHeight, (int)ED_TEST_CAR_TEXTURE_SLOT,
                             gfx_size);

    /*
     * Car[0] as pure render data. numcars stays zero, so no update ever
     * touches this; every field below is read by the draw path and by nothing
     * else. nCurrChunk = -1 is what makes the pose world-space and the motion
     * offsets inert (car.c's DisplayCarWithPose zeroes them for that value).
     */
    memset(&Car[ED_TEST_CAR_SLOT], 0, sizeof(Car[ED_TEST_CAR_SLOT]));
    Car[ED_TEST_CAR_SLOT].byCarDesignIdx = (uint8)uiDesign;
    Car[ED_TEST_CAR_SLOT].nCurrChunk = -1;
    Car[ED_TEST_CAR_SLOT].iLastValidChunk = 0;

    s_uiPreparedDesign = uiDesign;
    s_bPreparedAdvanced = bAdvanced;
    return true;
}

/*
 * The chunk's world heading in the legacy 14-bit circle, taken from the
 * direction to the next chunk's road centre. Heading zero looks along world
 * +X and increases toward +Y (ADR 0003), which is exactly what tsin/tcos are
 * indexed by, so the conversion is a plain scaling of atan2.
 */
static bool ed_test_car_heading(uint32_t uiChunkId, int32_t *piYaw)
{
    float afCentre[3];
    float afNextCentre[3];
    uint32_t uiNextChunk;
    double dHeading;

    if (TRAK_LEN <= 0)
        return false;
    uiNextChunk = uiChunkId + 1u;
    if (uiNextChunk >= (uint32_t)TRAK_LEN)
        uiNextChunk = 0u;
    if (!ed_helper_center_point(uiChunkId, afCentre)
            || !ed_helper_center_point(uiNextChunk, afNextCentre))
        return false;

    dHeading = atan2((double)(afNextCentre[1] - afCentre[1]),
                     (double)(afNextCentre[0] - afCentre[0]));
    if (piYaw) {
        *piYaw = (int32_t)(dHeading * ED_TEST_CAR_ANGLE_COUNT
                           / (2.0 * ED_TEST_CAR_PI));
    }
    return true;
}

bool ed_test_car_pose(uint32_t uiChunkId, uint32_t uiAiLine, bool bMillionPlus,
                      float afPositionOut[3], int32_t *piYaw, int32_t *piPitch,
                      int32_t *piRoll)
{
    int32_t iYaw;

    if (!afPositionOut || uiAiLine >= ROLLER_ED_TEST_CAR_AI_LINE_COUNT)
        return false;
    if (TRAK_LEN <= 0 || uiChunkId >= (uint32_t)TRAK_LEN)
        return false;
    /* The same point the AI-line overlay draws, so the car sits on the line
     * the editor is showing rather than near it. */
    if (!ed_helper_ai_line_point(uiChunkId, uiAiLine, afPositionOut))
        return false;

    /*
     * Heading comes from the geometry, not from localdata[].iYaw.
     *
     * The track file's per-chunk pitch and roll are that chunk's slope and
     * banking -- genuinely the attitude of a car sitting on it -- but its yaw
     * is a *turn rate*, not a heading: transfrm.c stores the per-chunk delta
     * angle and wraps it to a signed half circle. Feeding that in pointed the
     * car along world +X on every straight, which read as a fixed rotation
     * away from the track. The direction to the next chunk is the heading.
     */
    if (!ed_test_car_heading(uiChunkId, &iYaw))
        return false;
    /*
     * "Million Plus" turns the car around. The legacy editor flipped the sign
     * of both of its model-correction rotations for it, and persisted the
     * checkbox under the key "wrong_way" (MainWindow.cpp) -- the user-visible
     * effect is a car facing back down the track. ROLLER's plans need no
     * correction rotation, so the same effect is a half turn of heading.
     */
    if (bMillionPlus)
        iYaw += ED_TEST_CAR_HALF_TURN;

    if (piYaw)
        *piYaw = iYaw & ED_TEST_CAR_ANGLE_MASK;
    if (piPitch)
        *piPitch = localdata[uiChunkId].iPitch & ED_TEST_CAR_ANGLE_MASK;
    if (piRoll)
        *piRoll = localdata[uiChunkId].iRoll & ED_TEST_CAR_ANGLE_MASK;
    return true;
}

void ed_test_car_draw(GameRenderer *pRenderer)
{
    uint32_t uiDesign = 0u;
    uint32_t uiAiLine = 0u;
    uint32_t uiChunkId = 0u;
    bool bMillionPlus = false;
    bool bAdvanced = false;
    float afPosition[3];
    int32_t iYaw = 0;
    int32_t iPitch = 0;
    int32_t iRoll = 0;
    GameRenderCarPose Pose;
    GameRenderCarOptions Options;

    if (!pRenderer)
        return;
    if (!roller_ed_overlay_test_car(&uiDesign, &uiAiLine, &uiChunkId,
                                    &bMillionPlus, &bAdvanced))
        return;
    if (!ed_test_car_pose(uiChunkId, uiAiLine, bMillionPlus, afPosition, &iYaw,
                          &iPitch, &iRoll))
        return;
    if (!ed_test_car_prepare(pRenderer, uiDesign, bAdvanced))
        return;

    /*
     * Both renderers read textures_off every frame -- the GPU keys its cached
     * car mesh on it, and both apply the mirror palette remap through it -- so
     * the flag has to hold for the draw, not just for the texture load.
     */
    if (bAdvanced)
        textures_off |= TEX_OFF_ADVANCED_CARS;
    else
        textures_off &= ~TEX_OFF_ADVANCED_CARS;

    /* The chunk under the car is what its shadow and lighting read. */
    Car[ED_TEST_CAR_SLOT].iLastValidChunk = (int)uiChunkId;

    memset(&Pose, 0, sizeof(Pose));
    Pose.position.fX = afPosition[0];
    Pose.position.fY = afPosition[1];
    Pose.position.fZ = afPosition[2];
    Pose.yaw = (int)iYaw;
    Pose.pitch = (int)iPitch;
    Pose.roll = (int)iRoll;

    /* Frame zero of the design's animation: a parked car, not a driving one. */
    memset(&Options, 0, sizeof(Options));
    Options.anim_frame = 0;
    Options.color_remap = NULL;

    game_render_draw_car(pRenderer, ED_TEST_CAR_SLOT, &Pose, &Options);
}
