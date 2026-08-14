#include "3d.h"
#include "drawtrk3.h"
#include "loadtrak.h"
#include "tower.h"
#include "transfrm.h"
#include "view.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

GameRenderer *g_pGameRenderer = (GameRenderer *)(uintptr_t)1u;
tData localdata[MAX_TRACK_CHUNKS];
int TRAK_LEN;
int scr_size = 64;
int ybase = 100;
int xbase = 159;
float viewx;
float viewy;
float viewz;
int VIEWDIST = 100;
float vk1 = 1.0f;
int xp;
float vk2;
float vk3;
float vk4;
float vk5 = 1.0f;
float vk6;
float vk7;
float vk8;
float vk9 = 1.0f;
int yp;
int NearTow = -1;

typedef struct
{
    GameRenderVertex aVertices[ED_SURFACE_VERTEX_COUNT];
    tEdSurfaceInfo Info;
} tCapturedTowerMarker;

static tCapturedTowerMarker s_aCaptured[MAX_TOWERS];
static uint32_t s_uiCaptured;

bool drawtrk3_emit_surface_to_renderer(
    GameRenderer *pRenderer,
    const GameRenderVertex aVertices[ED_SURFACE_VERTEX_COUNT],
    const tEdSurfaceInfo *pInfo)
{
    tCapturedTowerMarker *pCaptured;

    if (pRenderer != g_pGameRenderer || !aVertices || !pInfo
            || s_uiCaptured >= MAX_TOWERS)
        return false;
    pCaptured = &s_aCaptured[s_uiCaptured++];
    memcpy(pCaptured->aVertices, aVertices, sizeof(pCaptured->aVertices));
    pCaptured->Info = *pInfo;
    return true;
}

static int check(int bCondition, int iLine)
{
    if (!bCondition)
        fprintf(stderr, "tower marker check failed at line %d\n", iLine);
    return bCondition ? 0 : iLine;
}

#define CHECK(condition) \
    do { \
        int iResult = check((condition), __LINE__); \
        if (iResult != 0) \
            return iResult; \
    } while (0)

static int nearly_equal(float fLeft, float fRight)
{
    return fabsf(fLeft - fRight) <= 0.0001f;
}

static void reset_capture(void)
{
    memset(s_aCaptured, 0, sizeof(s_aCaptured));
    s_uiCaptured = 0u;
}

int main(int argc, const char **argv, const char **envp)
{
    static const int aiModes[] = { -1, -4, -2, -5, -3 };
    const uint32_t uiFixtureTowers = 25u;

    (void)argc;
    (void)argv;
    (void)envp;

    NumTowers = (int)uiFixtureTowers;
    for (uint32_t i = 0u; i < uiFixtureTowers; i++) {
        TowerBase[i].iChunkIdx = (int)(100u + i);
        TowerBase[i].iEnabled = aiModes[i % 5u];
        TowerBase[i].iTowerType = (int)(i / 5u);
        TowerX[i] = 10.0f + (float)i * 3.0f;
        TowerY[i] = -20.0f + (float)i * 5.0f;
        TowerZ[i] = 1000.0f + (float)i * 100.0f;
    }

    /* Every mode/zoom pair bypasses DrawTower's game gates and emits at the
     * E7-S2 world position with its anchor chunk identity. */
    reset_capture();
    NearTow = 0;
    for (uint32_t i = 0u; i < uiFixtureTowers; i++)
        tower_emit_marker((int)i, 1.0f);
    CHECK(s_uiCaptured == uiFixtureTowers);
    for (uint32_t i = 0u; i < uiFixtureTowers; i++) {
        const tCapturedTowerMarker *pMarker = &s_aCaptured[i];
        float fCentreX = (pMarker->aVertices[0].x
                        + pMarker->aVertices[2].x) * 0.5f;
        float fCentreY = (pMarker->aVertices[0].y
                        + pMarker->aVertices[2].y) * 0.5f;
        float fCentreZ = (pMarker->aVertices[0].z
                        + pMarker->aVertices[2].z) * 0.5f;

        CHECK(nearly_equal(fCentreX, TowerX[i]));
        CHECK(nearly_equal(fCentreY, TowerY[i]));
        CHECK(nearly_equal(fCentreZ, TowerZ[i]));
        CHECK(pMarker->Info.uiChunkId == (uint32_t)TowerBase[i].iChunkIdx);
        CHECK(pMarker->Info.uiRenderFlags
              == (SURFACE_FLAG_FLIP_BACKFACE | 0xE7));
        CHECK(pMarker->Info.uiBackSurfaceFlags == ED_MATERIAL_ID_NONE);
        CHECK(pMarker->Info.uiTextureSet == TEXTURE_BANK_TRACK);
        CHECK(pMarker->Info.unSurfaceClass
              == ROLLER_ED_SURFACE_CLASS_TOWER);
        CHECK(pMarker->Info.unContentClass
              == ROLLER_ED_CONTENT_RUNTIME_SCENERY);
        CHECK(pMarker->Info.byTopology == ROLLER_ED_TOPOLOGY_QUAD);
    }

    /* fScale changes only the constant-pixel billboard extent. */
    reset_capture();
    tower_emit_marker(0, 1.0f);
    tower_emit_marker(0, 2.0f);
    CHECK(s_uiCaptured == 2u);
    CHECK(nearly_equal(
        s_aCaptured[1].aVertices[0].x - TowerX[0],
        2.0f * (s_aCaptured[0].aVertices[0].x - TowerX[0])));
    CHECK(nearly_equal(
        s_aCaptured[1].aVertices[0].y - TowerY[0],
        2.0f * (s_aCaptured[0].aVertices[0].y - TowerY[0])));

    /* Invalid direct calls are inert instead of indexing the fixed tables. */
    reset_capture();
    tower_emit_marker(-1, 1.0f);
    tower_emit_marker(NumTowers, 1.0f);
    tower_emit_marker(0, 0.0f);
    tower_emit_marker(0, NAN);
    CHECK(s_uiCaptured == 0u);

    /* DrawTower retains the game's NearTow and enabled gates, then delegates
     * the surviving legacy path to the same scale-1 seam. */
    TowerBase[0].iEnabled = 0;
    NearTow = -1;
    reset_capture();
    DrawTower(0, NULL);
    CHECK(s_uiCaptured == 1u);
    TowerBase[0].iEnabled = -1;
    DrawTower(0, NULL);
    CHECK(s_uiCaptured == 1u);
    TowerBase[0].iEnabled = 0;
    NearTow = 0;
    DrawTower(0, NULL);
    CHECK(s_uiCaptured == 1u);

    puts("tower marker seam tests passed");
    return 0;
}
