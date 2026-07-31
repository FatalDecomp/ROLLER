#include "editor_camera.h"

#include "3d.h"
#include "transfrm.h"

#include <math.h>

#define ROLLER_ED_LEGACY_ANGLE_COUNT 16384

static tEdCameraState s_Camera;
static bool s_bCameraSet;

int roller_ed_camera_degrees_to_angle(float fDegrees)
{
    double dWrapped = fmod((double)fDegrees, 360.0);
    long lAngle = lround(
        dWrapped * ((double)ROLLER_ED_LEGACY_ANGLE_COUNT / 360.0));

    return ((int)lAngle) & (ROLLER_ED_LEGACY_ANGLE_COUNT - 1);
}

void roller_ed_camera_reset(void)
{
    s_bCameraSet = false;
}

void roller_ed_camera_set(const tEdCameraState *pCamera)
{
    s_Camera = *pCamera;
    s_bCameraSet = true;
}

bool roller_ed_camera_apply(void)
{
    int iYaw;
    int iPitch;

    if (!s_bCameraSet)
        return false;

    iYaw = roller_ed_camera_degrees_to_angle(s_Camera.fYawDegrees);
    iPitch = roller_ed_camera_degrees_to_angle(s_Camera.fPitchDegrees);

    worldx = s_Camera.fPosition[0];
    worldy = s_Camera.fPosition[1];
    worldz = s_Camera.fPosition[2];
    vdirection = iYaw;
    velevation = iPitch;
    vtilt = 0;
    worlddirn = iYaw;
    worldelev = iPitch;
    worldtilt = 0;
    calculatetransform(
        -1, iYaw, iPitch, 0,
        s_Camera.fPosition[0], s_Camera.fPosition[1], s_Camera.fPosition[2],
        0.0f, 0.0f, 0.0f);
    return true;
}
