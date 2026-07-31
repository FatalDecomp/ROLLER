#include "editor_camera.h"
#include "transfrm.h"

#include <stdio.h>

float viewx;
float viewy;
float viewz;
float worldx;
float worldy;
float worldz;
int vdirection;
int velevation;
int vtilt;
int worlddirn;
int worldelev;
int worldtilt;

static int s_iTransformCalls;
static int s_iTrackChunk;
static int s_iDirection;
static int s_iElevation;
static int s_iTilt;
static float s_fViewX;
static float s_fViewY;
static float s_fViewZ;
static float s_fPosX;
static float s_fPosY;
static float s_fPosZ;

void calculatetransform(
    int iTrackChunkIdx, int iDirection, int iElevation, int iTilt,
    float fViewX, float fViewY, float fViewZ,
    float fPosX, float fPosY, float fPosZ)
{
    s_iTransformCalls++;
    s_iTrackChunk = iTrackChunkIdx;
    s_iDirection = iDirection;
    s_iElevation = iElevation;
    s_iTilt = iTilt;
    s_fViewX = fViewX;
    s_fViewY = fViewY;
    s_fViewZ = fViewZ;
    s_fPosX = fPosX;
    s_fPosY = fPosY;
    s_fPosZ = fPosZ;
    viewx = fViewX;
    viewy = fViewY;
    viewz = fViewZ;
}

static int check(int bCondition, int iLine)
{
    if (!bCondition)
        fprintf(stderr, "editor camera check failed at line %d\n", iLine);
    return bCondition ? 0 : iLine;
}

#define CHECK(condition) \
    do { \
        int iResult = check((condition), __LINE__); \
        if (iResult != 0) \
            return iResult; \
    } while (0)

int main(void)
{
    static const struct
    {
        float fDegrees;
        int iExpected;
    } aAngles[] = {
        { 0.0f, 0 },
        { 22.5f, 1024 },
        { 90.0f, 4096 },
        { 180.0f, 8192 },
        { 270.0f, 12288 },
        { 360.0f, 0 },
        { 450.0f, 4096 },
        { -90.0f, 12288 },
        { -450.0f, 12288 }
    };
    tEdCameraState Camera = {
        .uiStructSize = sizeof(Camera),
        .uiVersion = ROLLER_ED_CAMERA_STATE_VERSION,
        .fPosition = { 100.0f, -200.0f, 300.0f },
        .fYawDegrees = 450.0f,
        .fPitchDegrees = -90.0f
    };

    roller_ed_camera_reset();
    CHECK(!roller_ed_camera_apply());
    CHECK(s_iTransformCalls == 0);

    for (size_t i = 0; i < sizeof(aAngles) / sizeof(aAngles[0]); ++i)
        CHECK(roller_ed_camera_degrees_to_angle(aAngles[i].fDegrees)
              == aAngles[i].iExpected);

    roller_ed_camera_set(&Camera);
    CHECK(roller_ed_camera_apply());
    CHECK(s_iTransformCalls == 1);
    CHECK(s_iTrackChunk == -1);
    CHECK(s_iDirection == 4096 && s_iElevation == 12288 && s_iTilt == 0);
    CHECK(s_fViewX == 100.0f && s_fViewY == -200.0f && s_fViewZ == 300.0f);
    CHECK(s_fPosX == 0.0f && s_fPosY == 0.0f && s_fPosZ == 0.0f);
    CHECK(viewx == 100.0f && viewy == -200.0f && viewz == 300.0f);
    CHECK(worldx == 100.0f && worldy == -200.0f && worldz == 300.0f);
    CHECK(vdirection == 4096 && worlddirn == 4096);
    CHECK(velevation == 12288 && worldelev == 12288);
    CHECK(vtilt == 0 && worldtilt == 0);

    Camera.fPosition[0] = -1234.5f;
    Camera.fPosition[1] = 6789.25f;
    Camera.fPosition[2] = -42.0f;
    Camera.fYawDegrees = 180.0f;
    Camera.fPitchDegrees = 90.0f;
    roller_ed_camera_set(&Camera);
    CHECK(roller_ed_camera_apply());
    CHECK(s_iTransformCalls == 2);
    CHECK(s_iDirection == 8192 && s_iElevation == 4096);
    CHECK(viewx == -1234.5f && viewy == 6789.25f && viewz == -42.0f);

    roller_ed_camera_reset();
    CHECK(!roller_ed_camera_apply());
    CHECK(s_iTransformCalls == 2);

    puts("editor camera movement and full-orbit tests passed");
    return 0;
}
