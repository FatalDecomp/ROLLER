#include "view.h"
#include "3d.h"
#include "car.h"
#include "moving.h"
#include "replay.h"
#include "transfrm.h"
#include "control.h"
#include "tower.h"
#include "loadtrak.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <float.h>
//-------------------------------------------------------------------------------------------------

#define NOCLIP_MOVE_SPEED       9000.0f
#define NOCLIP_FAST_MULTIPLIER  2.0f
#define NOCLIP_SLOW_MULTIPLIER  0.5f
#define NOCLIP_MAX_DT           0.1f
#define NOCLIP_MOUSE_SENSITIVITY 8.0f
#define NOCLIP_PITCH_LIMIT      (0x1000 - 64)

extern bool g_bNoclip;
SDL_Window *ROLLERGetWindow(void);

tViewData viewdata[2] =       //000A74A8
{
  { 4500.0, 500.0, 640.0, -240.0, 200.0, 160.0 },
  { 2000.0, 500.0, 450.0, 350.0, 200.0, 100.0 }
};
int chaseview[2] = { 0, 0 };  //000A74D8
float CHASE_DIST[2] = { 4500.0, 4500.0 }; //000A74E0
float CHASE_MIN[2] = { 500.0, 500.0 };  //000A74E8
float PULLZ[2] = { 640.0, 640.0 }; //000A74F0
float LOOKZ[2] = { 160.0, 160.0 }; //000A74F8
int nextpoint[2] = { 0, 0 };  //000A7500
tCameraPos lastpos[2][64];    //001A1250
float TowerGx;                //001A1A54
float TowerGy;                //001A1A58
float TowerGz;                //001A1A5C
int lastcamelevation;         //001A1A60
int lastcamdirection;         //001A1A64
int NearTow;                  //001A1A68
float chase_x;                //001A1A6C
float chase_y;                //001A1A70
float chase_z;                //001A1A74

//-------------------------------------------------------------------------------------------------

static bool s_bNoclipInitialized = false;
static bool s_bNoclipInputEnabled = true;
static float s_fNoclipX = 0.0f;
static float s_fNoclipY = 0.0f;
static float s_fNoclipZ = 0.0f;
static int s_iNoclipYaw = 0;
static int s_iNoclipPitch = 0;
static int s_iNoclipViewDist = 200;
static float s_fNoclipSpeedMultiplier = 1.0f;
static bool s_bNoclipFastWasDown = false;
static bool s_bNoclipSlowWasDown = false;
static uint64 s_ullNoclipLastUpdateNs = 0;

/* Set by chase_look_apply() below. Deliberately does not touch SDL's
 * relative-mouse-mode grab (that grab has a confirmed quirk here where it
 * doesn't start delivering motion until the window loses and regains focus)
 * -- motion is instead read from raw event deltas accumulated in roller.c. */
static bool s_bChaseLookActive = false;

static int noclip_angle14(int iAngle)
{
  return iAngle & 0x3FFF;
}

static int noclip_signed_angle(int iAngle)
{
  iAngle &= 0x3FFF;
  if (iAngle >= 0x2000)
    iAngle -= 0x4000;
  return iAngle;
}

static int noclip_clamp_pitch(int iPitch)
{
  if (iPitch > NOCLIP_PITCH_LIMIT)
    return NOCLIP_PITCH_LIMIT;
  if (iPitch < -NOCLIP_PITCH_LIMIT)
    return -NOCLIP_PITCH_LIMIT;
  return iPitch;
}

static int noclip_key_down(const bool *pKeys, SDL_Scancode scancode)
{
  return pKeys && pKeys[scancode] ? 1 : 0;
}

static void noclip_sync_mouse_mode(void)
{
  SDL_Window *pWindow = ROLLERGetWindow();
  if (!pWindow)
    return;

  bool bWantRelative = g_bNoclip && s_bNoclipInitialized && s_bNoclipInputEnabled;
  if (SDL_GetWindowRelativeMouseMode(pWindow) != bWantRelative) {
    if (!SDL_SetWindowRelativeMouseMode(pWindow, bWantRelative))
      SDL_Log("noclip: failed to set relative mouse mode: %s", SDL_GetError());

    float fIgnoredX, fIgnoredY;
    SDL_GetRelativeMouseState(&fIgnoredX, &fIgnoredY);
  }
}

void noclip_camera_reset(void)
{
  s_bNoclipInitialized = false;
  s_fNoclipSpeedMultiplier = 1.0f;
  s_bNoclipFastWasDown = false;
  s_bNoclipSlowWasDown = false;
  s_ullNoclipLastUpdateNs = 0;
  noclip_sync_mouse_mode();
}

/* Debug free-look: hold the right mouse button and move the mouse to orbit
 * the look direction in any view except cockpit and the rearview mirror.
 * Applied as a rotate-only post-process after calculateview() has already
 * produced viewx/viewy/viewz (the final camera position for whichever view
 * branch ran) -- re-running calculatetransform with the same position and a
 * rotated direction/elevation leaves that view's own position/smoothing
 * logic completely untouched. */
#define CHASE_LOOK_MOUSE_SENSITIVITY 8.0f

static int  s_iChaseLookYaw = 0;
static int  s_iChaseLookPitch = 0;

extern bool  g_bChaseLookRightDown;
extern float g_fChaseLookAccumX;
extern float g_fChaseLookAccumY;
extern bool  g_bFreeCamera;

static void chase_look_deactivate(void)
{
  s_bChaseLookActive = false;
  s_iChaseLookYaw = 0;
  s_iChaseLookPitch = 0;
}

void chase_look_apply(void)
{
  /* Gated purely on the "Free Camera" debug checkbox -- user preference is
   * for this to work unconditionally in every view (including cockpit and
   * the mirror pass) once enabled, no per-view exclusions. */
  if (!g_bFreeCamera || !g_bChaseLookRightDown) {
    chase_look_deactivate();
    g_fChaseLookAccumX = 0.0f;
    g_fChaseLookAccumY = 0.0f;
    return;
  }

  s_bChaseLookActive = true;
  float fMouseX = g_fChaseLookAccumX;
  float fMouseY = g_fChaseLookAccumY;
  g_fChaseLookAccumX = 0.0f;
  g_fChaseLookAccumY = 0.0f;
  s_iChaseLookYaw = noclip_angle14(s_iChaseLookYaw - (int)(fMouseX * CHASE_LOOK_MOUSE_SENSITIVITY));
  s_iChaseLookPitch = noclip_clamp_pitch(s_iChaseLookPitch - (int)(fMouseY * CHASE_LOOK_MOUSE_SENSITIVITY));

  int iRenderDirection = noclip_angle14(worlddirn + s_iChaseLookYaw);
  int iRenderElevation = noclip_angle14(noclip_clamp_pitch(noclip_signed_angle(worldelev) + s_iChaseLookPitch));
  calculatetransform(-1, iRenderDirection, iRenderElevation, worldtilt, viewx, viewy, viewz, 0.0f, 0.0f, 0.0f);
  worlddirn = iRenderDirection;
  worldelev = iRenderElevation;
}

void noclip_camera_set_input_enabled(bool bEnabled)
{
  s_bNoclipInputEnabled = bEnabled;
  s_ullNoclipLastUpdateNs = 0;
  noclip_sync_mouse_mode();
}

void noclip_camera_update(void)
{
  if (!g_bNoclip) {
    if (s_bNoclipInitialized)
      noclip_camera_reset();
    return;
  }

  if (!s_bNoclipInitialized)
    return;

  noclip_sync_mouse_mode();

  SDL_Window *pWindow = ROLLERGetWindow();
  if (!pWindow || !s_bNoclipInputEnabled || !SDL_GetWindowRelativeMouseMode(pWindow)) {
    s_ullNoclipLastUpdateNs = 0;
    return;
  }

  float fMouseX, fMouseY;
  SDL_GetRelativeMouseState(&fMouseX, &fMouseY);
  s_iNoclipYaw = noclip_angle14(s_iNoclipYaw - (int)(fMouseX * NOCLIP_MOUSE_SENSITIVITY));
  s_iNoclipPitch = noclip_clamp_pitch(s_iNoclipPitch - (int)(fMouseY * NOCLIP_MOUSE_SENSITIVITY));

  const bool *pKeys = SDL_GetKeyboardState(NULL);
  bool bFastDown = noclip_key_down(pKeys, SDL_SCANCODE_Z) != 0;
  bool bSlowDown = noclip_key_down(pKeys, SDL_SCANCODE_X) != 0;
  if (bFastDown && !s_bNoclipFastWasDown)
    s_fNoclipSpeedMultiplier *= NOCLIP_FAST_MULTIPLIER;
  if (bSlowDown && !s_bNoclipSlowWasDown)
    s_fNoclipSpeedMultiplier *= NOCLIP_SLOW_MULTIPLIER;
  s_bNoclipFastWasDown = bFastDown;
  s_bNoclipSlowWasDown = bSlowDown;

  uint64 ullNowNs = SDL_GetTicksNS();
  if (!s_ullNoclipLastUpdateNs) {
    s_ullNoclipLastUpdateNs = ullNowNs;
    return;
  }

  float fDt = (float)((double)(ullNowNs - s_ullNoclipLastUpdateNs) / 1000000000.0);
  s_ullNoclipLastUpdateNs = ullNowNs;
  if (fDt <= 0.0f)
    return;
  if (fDt > NOCLIP_MAX_DT)
    fDt = NOCLIP_MAX_DT;

  int iForward = noclip_key_down(pKeys, SDL_SCANCODE_W) - noclip_key_down(pKeys, SDL_SCANCODE_S);
  int iRight = noclip_key_down(pKeys, SDL_SCANCODE_D) - noclip_key_down(pKeys, SDL_SCANCODE_A);
  int iUp = (noclip_key_down(pKeys, SDL_SCANCODE_E) || noclip_key_down(pKeys, SDL_SCANCODE_R))
          - (noclip_key_down(pKeys, SDL_SCANCODE_Q) || noclip_key_down(pKeys, SDL_SCANCODE_F));

  if (!iForward && !iRight && !iUp)
    return;

  float fYawRad = ANGLE_TO_RADIANS(s_iNoclipYaw);
  float fPitchRad = ANGLE_TO_RADIANS(s_iNoclipPitch);
  float fCosYaw = cosf(fYawRad);
  float fSinYaw = sinf(fYawRad);
  float fCosPitch = cosf(fPitchRad);
  float fSinPitch = sinf(fPitchRad);

  float fMoveX = (float)iForward * fCosYaw * fCosPitch + (float)iRight * fSinYaw;
  float fMoveY = (float)iForward * fSinYaw * fCosPitch - (float)iRight * fCosYaw;
  float fMoveZ = (float)iForward * fSinPitch + (float)iUp;
  float fMoveLen = (float)sqrt(fMoveX * fMoveX + fMoveY * fMoveY + fMoveZ * fMoveZ);
  if (fMoveLen <= FLT_EPSILON)
    return;

  float fStep = NOCLIP_MOVE_SPEED * s_fNoclipSpeedMultiplier * fDt / fMoveLen;
  s_fNoclipX += fMoveX * fStep;
  s_fNoclipY += fMoveY * fStep;
  s_fNoclipZ += fMoveZ * fStep;
}

void noclip_camera_apply(void)
{
  if (!g_bNoclip) {
    if (s_bNoclipInitialized)
      noclip_camera_reset();
    return;
  }

  if (!s_bNoclipInitialized) {
    s_fNoclipX = viewx;
    s_fNoclipY = viewy;
    s_fNoclipZ = viewz;
    s_iNoclipYaw = noclip_angle14(worlddirn);
    s_iNoclipPitch = noclip_clamp_pitch(noclip_signed_angle(worldelev));
    s_iNoclipViewDist = VIEWDIST;
    s_ullNoclipLastUpdateNs = 0;
    s_bNoclipInitialized = true;
    noclip_sync_mouse_mode();
  }

  VIEWDIST = s_iNoclipViewDist;
  vdirection = s_iNoclipYaw;
  velevation = noclip_angle14(s_iNoclipPitch);
  vtilt = 0;
  worldx = s_fNoclipX;
  worldy = s_fNoclipY;
  worldz = s_fNoclipZ;
  calculatetransform(-1, vdirection, velevation, 0,
                     s_fNoclipX, s_fNoclipY, s_fNoclipZ,
                     0.0f, 0.0f, 0.0f);
  worlddirn = vdirection;
  worldelev = velevation;
  worldtilt = vtilt;
  lastcamdirection = vdirection;
  lastcamelevation = velevation;
}

//-------------------------------------------------------------------------------------------------
//000729C0
void calculateview(int iViewMode, int iCarIdx, int iChaseCamIdx)
{
  int iClosestTowerIdx; // esi
  int iCarIndex; // eax
  int iChunkIndex; // eax
  tData *pTransformMatrix; // eax
  int iTowerLoopIdx; // edx
  int iTowerArrayIdx; // ebx
  //int iTempValue; // eax
  int iTowerType; // eax
  double dWorldZOffset; // st7
  int iChunkIdxMinus2; // eax
  float *pTransformMinus2; // eax
  double dDeltaX; // st7
  double dDeltaY; // st6
  double dDeltaZ; // st4
  double dInvMagnitude; // st3
  int iCalculatedElevation; // eax
  float fCarPosZ; // edx
  int iCarChunk; // eax
  tData *pCarTransform; // eax
  float fCarPosZ2; // edx
  int iCarChunk2; // eax
  tData *pCarTransform2; // eax
  int iRollCalculated; // eax
  int iPitchCalculated; // edi
  int iPitchCalculated2; // edi
  int iCarIndex2; // edi
  int iYaw; // edx
  int iCurrChunk; // ecx
  float fX; // [esp-18h] [ebp-E4h]
  float fY; // [esp-14h] [ebp-E0h]
  float fZ; // [esp-10h] [ebp-DCh]
  double dChunkDistance; // [esp+0h] [ebp-CCh]
  double dMinDistance; // [esp+8h] [ebp-C4h]
  double dWrapDistance; // [esp+10h] [ebp-BCh]
  float fCameraOffsetX; // [esp+2Ch] [ebp-A0h]
  float fCameraOffsetY; // [esp+30h] [ebp-9Ch]
  float fCameraOffsetZ; // [esp+34h] [ebp-98h]
  float fPosX; // [esp+38h] [ebp-94h]
  float fPosZ; // [esp+3Ch] [ebp-90h]
  float fPosY; // [esp+40h] [ebp-8Ch]
  float fHorizontalDistance; // [esp+44h] [ebp-88h]
  float fVerticalDistance; // [esp+48h] [ebp-84h]
  float fTowerVerticalDistance; // [esp+4Ch] [ebp-80h]
  float fTowerHorizontalDistance; // [esp+50h] [ebp-7Ch]
  float fDeltaX; // [esp+54h] [ebp-78h]
  float fDeltaY; // [esp+58h] [ebp-74h]
  float fCarPosY; // [esp+5Ch] [ebp-70h]
  float fTowerDeltaX; // [esp+60h] [ebp-6Ch]
  float fCarPosY2; // [esp+64h] [ebp-68h]
  float fTowerDeltaY; // [esp+6Ch] [ebp-60h]
  float fCarPosX; // [esp+70h] [ebp-5Ch]
  float fCarPosZ3; // [esp+74h] [ebp-58h]
  float fCarPosY3; // [esp+78h] [ebp-54h]
  float fCarPosX2; // [esp+7Ch] [ebp-50h]
  float fCarPosX3; // [esp+80h] [ebp-4Ch]
  float fCarPosZ4; // [esp+84h] [ebp-48h]
  float fTransformedPosZ; // [esp+88h] [ebp-44h]
  float fTransformedPosX; // [esp+8Ch] [ebp-40h]
  float fTransformedPosY; // [esp+90h] [ebp-3Ch]
  int iTilt; // [esp+94h] [ebp-38h]
  float fWorldPosX; // [esp+98h] [ebp-34h]
  float fWorldPosX2; // [esp+9Ch] [ebp-30h]
  float fWorldPosZ; // [esp+A0h] [ebp-2Ch]
  int iElevation2; // [esp+A4h] [ebp-28h]
  int iLastValidChunk; // [esp+A8h] [ebp-24h]
  float fClosestTowerDistance; // [esp+ACh] [ebp-20h]
  float fMinTowerDistance; // [esp+B0h] [ebp-1Ch]
  float fCurrentTowerDistance; // [esp+B4h] [ebp-18h]
  float fChunkIdx; // [esp+B8h] [ebp-14h]
  float fChunkIndexDiff; // [esp+B8h] [ebp-14h]

  VIEWDIST = 200;                               // Set default view distance
  iClosestTowerIdx = -1;
  if (iViewMode < -1)                         // Handle different view modes - set camera offset values
  {
    if (iViewMode != -2)
      goto LABEL_8;
  LABEL_6:
    ext_x = 11.0;                               // Rear external view (mode -2) or cockpit view (mode 1) - behind car
    ext_y = 0.0;
    ext_z = 9.0;
    goto LABEL_9;
  }
  if (iViewMode <= 0) {
    ext_x = 0.0;                                // Driver view (mode 0) - inside car
    ext_y = 0.0;
    ext_z = 3.0;
    goto LABEL_9;
  }
  if (iViewMode == 1)
    goto LABEL_6;
LABEL_8:
  ext_x = -1.0;                                 // Invalid view mode - set error values
  ext_y = -1.0;
  ext_z = -1.0;
LABEL_9:
  iCarIndex = iCarIdx;
  if (iViewMode < 3)                          // Check if view mode is less than 3 (standard car views)
  {                                             // Standard car camera views (driver, cockpit, chase)
    if (iViewMode >= 0) {
      mirror = 0;                               // Normal view modes (non-mirror)
      if (iViewMode == 1)                     // Check if cockpit view (mode 1)
      {                                         // Calculate pitch for cockpit view - check if car is stunned
        if (Car[iCarIdx].iStunned)
          iPitchCalculated = Car[iCarIdx].iPitchCameraOffset + Car[iCarIdx].nPitch;
        else
          iPitchCalculated = Car[iCarIdx].iPitchDynamicOffset + Car[iCarIdx].iPitchCameraOffset + Car[iCarIdx].nPitch;
        iElevation2 = ((int16)iPitchCalculated + 0x2000) & 0x3FFF;
        iRollCalculated = Car[iCarIdx].iRollDynamicOffset + Car[iCarIdx].iRollCameraOffset + Car[iCarIdx].nRoll + 0x2000;
      } else {                                         // Calculate pitch for driver view - check if car is stunned
        if (Car[iCarIdx].iStunned)
          iPitchCalculated2 = Car[iCarIdx].iPitchCameraOffset + Car[iCarIdx].nPitch;
        else
          iPitchCalculated2 = Car[iCarIdx].iPitchDynamicOffset + Car[iCarIdx].iPitchCameraOffset + Car[iCarIdx].nPitch;
        iElevation2 = ((int16)iPitchCalculated2 + 455) & 0x3FFF;
        iRollCalculated = Car[iCarIdx].iRollDynamicOffset + Car[iCarIdx].iRollCameraOffset + Car[iCarIdx].nRoll;
      }
    } else {
      mirror = -1;                              // Mirror view mode - set mirror flag
      iElevation2 = ((uint16)(Car[iCarIdx].iPitchCameraOffset) + Car[iCarIdx].nPitch + 0x2000) & 0x3FFF;
      iRollCalculated = Car[iCarIdx].iRollCameraOffset + Car[iCarIdx].nRoll + 0x2000;
    }
    iTilt = iRollCalculated & 0x3FFF;
    NearTow = -1;
    if (iViewMode == 2)                       // Check if chase view mode
    {
      newchaseview(iCarIdx, iChaseCamIdx);      // Handle chase view - call specialized function
      return;
    }
    fCameraOffsetY = ext_y * 64.0f;              // Scale camera offsets and apply to display position
    fCameraOffsetZ = ext_z * 32.0f;
    fCameraOffsetX = ext_x * 64.0f;
    fPosX = DDX + fCameraOffsetY;
    iCarIndex2 = iCarIdx;
    fPosY = DDY + fCameraOffsetZ;
    fPosZ = DDZ - fCameraOffsetX;
    if (iViewMode == 1)                       // Check if cockpit view for special handling
    {
      fZ = Car[iCarIdx].pos.fZ;                 // Cockpit view - get car position and orientation
      fY = Car[iCarIdx].pos.fY;
      fX = Car[iCarIdx].pos.fX;
      iYaw = Car[iCarIdx].nYaw;
      VIEWDIST = 120;                           // Set closer view distance for cockpit
      calculatetransform(Car[iCarIndex2].nCurrChunk, iYaw, iElevation2, iTilt, fX, fY, fZ, fPosX, fPosY, fPosZ);// Calculate transformation matrix for cockpit view
      iCurrChunk = Car[iCarIndex2].nCurrChunk;
      if (iCurrChunk == -1)                   // Check if car is off-track (chunk = -1)
      {
        worlddirn = Car[iCarIndex2].nYaw;       // Car off-track - use raw orientation values
        worldelev = iElevation2;
        worldtilt = iTilt;
        return;
      }
    } else {
      calculatetransform(Car[iCarIdx].nCurrChunk, Car[iCarIdx].nYaw, iElevation2, iTilt, Car[iCarIdx].pos.fX, Car[iCarIdx].pos.fY, Car[iCarIdx].pos.fZ, fPosX, fPosY, fPosZ);// Driver view - calculate transformation matrix
      iCurrChunk = Car[iCarIdx].nCurrChunk;
      if (iCurrChunk == -1) {
        worlddirn = Car[iCarIdx].nYaw;
        worldelev = iElevation2;
        worldtilt = iTilt;
        return;
      }
    }
    getworldangles(Car[iCarIndex2].nYaw, iElevation2, iTilt, iCurrChunk, &worlddirn, &worldelev, &worldtilt);// Convert car angles to world coordinates
  } else if (iViewMode == 3)                    // Tower/spectator view mode (mode 3)
  {
    fCarPosX3 = Car[iCarIdx].pos.fX;            // Get car position for tower view
    fCarPosY2 = Car[iCarIdx].pos.fY;
    iChunkIndex = Car[iCarIdx].nCurrChunk;
    fCarPosZ3 = Car[iCarIdx].pos.fZ;
    iLastValidChunk = iChunkIndex;
    if (iChunkIndex == -1)                    // Check if car is off-track
    {
      fWorldPosX2 = Car[iCarIdx].pos.fX;        // Car off-track - use raw position
      fWorldPosZ = Car[iCarIdx].pos.fZ;
      fWorldPosX = Car[iCarIdx].pos.fY;
    } else {
      pTransformMatrix = &localdata[iChunkIndex];// Transform car position to world coordinates
      fWorldPosX2 = pTransformMatrix->pointAy[0].fY * fCarPosY2
        + pTransformMatrix->pointAy[0].fX * fCarPosX3
        + pTransformMatrix->pointAy[0].fZ * fCarPosZ3
        - pTransformMatrix->pointAy[3].fX;
      fWorldPosX = pTransformMatrix->pointAy[1].fX * fCarPosX3
        + pTransformMatrix->pointAy[1].fY * fCarPosY2
        + pTransformMatrix->pointAy[1].fZ * fCarPosZ3
        - pTransformMatrix->pointAy[3].fY;
      fWorldPosZ = fCarPosY2 * pTransformMatrix->pointAy[2].fY
        + fCarPosX3 * pTransformMatrix->pointAy[2].fX
        + fCarPosZ3 * pTransformMatrix->pointAy[2].fZ
        - pTransformMatrix->pointAy[3].fZ;
    }
    if (NumTowers <= 0)                       // Check if towers are available for spectator mode
    {
      dWorldZOffset = fWorldPosZ + 16384.0;     // No towers - use default elevated view
      worldx = fWorldPosX2;
      worldy = fWorldPosX;
    LABEL_39:
      worldz = (float)dWorldZOffset;
    } else {                                           // Find closest tower to car position
      if (iLastValidChunk == -1)
        iLastValidChunk = Car[iCarIdx].iLastValidChunk;
      iClosestTowerIdx = 0;
      iTowerLoopIdx = 0;                        // Initialize tower search variables
      fMinTowerDistance = 9.9999998e17f;
      if (NumTowers > 0) {
        iTowerArrayIdx = 0;                     // Loop through all towers
        do {
          fChunkIdx = (float)TowerBase[iTowerArrayIdx].iChunkIdx;// Calculate distance considering track wraparound
          fChunkIndexDiff = (float)((double)iLastValidChunk - fChunkIdx);
          fClosestTowerDistance = (float)TRAK_LEN;
          dWrapDistance = fabs(fChunkIndexDiff - fClosestTowerDistance);
          dChunkDistance = fabs(fChunkIndexDiff);

          if (dChunkDistance >= dWrapDistance) {
            dMinDistance = dWrapDistance;
          } else {
            dMinDistance = dChunkDistance;
          }
          //if (dChunkDistance >= dWrapDistance) {
          //  iTempValue = HIDWORD(dWrapDistance);
          //  LODWORD(dMinDistance) = LODWORD(dWrapDistance);
          //} else {
          //  iTempValue = HIDWORD(dChunkDistance);
          //  LODWORD(dMinDistance) = LODWORD(dChunkDistance);
          //}
          //HIDWORD(dMinDistance) = iTempValue;

          if (dMinDistance < fMinTowerDistance)// Update closest tower if this one is nearer
          {
            iClosestTowerIdx = iTowerLoopIdx;
            fCurrentTowerDistance = (float)dMinDistance;
            fMinTowerDistance = fCurrentTowerDistance;
          }
          ++iTowerLoopIdx;
          ++iTowerArrayIdx;
        } while (iTowerLoopIdx < NumTowers);
      }
      worldx = TowerX[iClosestTowerIdx];        // Use closest tower position
      worldy = TowerY[iClosestTowerIdx];
      iTowerType = TowerBase[iClosestTowerIdx].iTowerType - 1;// Set view distance based on tower type
      worldz = TowerZ[iClosestTowerIdx];
      switch (iTowerType) {
        case 0:
          VIEWDIST = 120;                       // Tower type 1 - medium view distance
          break;
        case 1:
          VIEWDIST = 75;                        // Tower type 2 - close view distance
          break;
        case 2:
          VIEWDIST = 500;                       // Tower type 3 - far view distance
          break;
        case 3:
          VIEWDIST = 750;                       // Tower type 4 - very far view distance
          break;
        default:
          break;
      }
      switch (TowerBase[iClosestTowerIdx].iEnabled) {
        case 0xFFFFFFFB:
          dWorldZOffset = (double)(TowerBase[iClosestTowerIdx].iVOffset << 7) + fWorldPosZ;// Mode -5: Elevated view with vertical offset
          worldx = fWorldPosX2;
          worldy = fWorldPosX;
          goto LABEL_39;
        case 0xFFFFFFFC:
          iChunkIdxMinus2 = TowerBase[iClosestTowerIdx].iChunkIdx - 2;// Mode -4: Look back 2 chunks
          if (iChunkIdxMinus2 < 0)
            iChunkIdxMinus2 += TRAK_LEN;
          pTransformMinus2 = (float *)&localdata[iChunkIdxMinus2];
          fWorldPosX2 = -pTransformMinus2[9];
          fWorldPosX = -pTransformMinus2[10];
          fWorldPosZ = -pTransformMinus2[11];
          break;
        case 0xFFFFFFFD:
        case 2:
          dDeltaX = worldx - fWorldPosX2;       // Mode -3/2: Directional offset from tower
          dDeltaY = worldy - fWorldPosX;
          dDeltaZ = worldz - fWorldPosZ;
          dInvMagnitude = 1.0 / sqrt(dDeltaX * dDeltaX + dDeltaY * dDeltaY + dDeltaZ * dDeltaZ);
          worldx = (float)(dDeltaX * 2560.0 * dInvMagnitude + fWorldPosX2);
          worldy = (float)(dDeltaY * 2560.0 * dInvMagnitude + fWorldPosX);
          worldz = (float)(dInvMagnitude * (dDeltaZ * 2560.0) + fWorldPosZ);
          break;
        case 0xFFFFFFFE:
        case 1:
          worldx = (worldx - fWorldPosX2) * 0.25f + fWorldPosX2;// Mode -2/1: Interpolated position
          worldy = fWorldPosX + (worldy - fWorldPosX) * 0.25f;
          dWorldZOffset = (worldz - fWorldPosZ) * 0.25f + fWorldPosZ;
          goto LABEL_39;
        case 0:
          dWorldZOffset = worldz + 3200.0;      // Mode 0: Fixed height offset
          goto LABEL_39;
        default:
          break;                                // Handle different tower camera modes
      }
    }
    TowerGx = worldx;
    TowerGy = worldy;
    TowerGz = worldz;
    fTowerDeltaX = fWorldPosX2 - worldx;        // Calculate direction and elevation to look at car
    fTowerDeltaY = fWorldPosX - worldy;
    NearTow = iClosestTowerIdx;
    fTowerVerticalDistance = fWorldPosZ - worldz;
    //if ((LODWORD(fTowerDeltaX) & 0x7FFFFFFF) != 0 || fabs(fWorldPosX - worldy))
    if (fabs(fTowerDeltaX) > FLT_EPSILON || fabs(fWorldPosX - worldy))
      vdirection = getangle(fTowerDeltaX, fTowerDeltaY);// Calculate horizontal direction angle
    else
      vdirection = 0;
    fTowerHorizontalDistance = (float)sqrt(fTowerDeltaX * fTowerDeltaX + fTowerDeltaY * fTowerDeltaY);
    //if ((LODWORD(fTowerHorizontalDistance) & 0x7FFFFFFF) != 0 || (LODWORD(fTowerVerticalDistance) & 0x7FFFFFFF) != 0)
    if (fabs(fTowerHorizontalDistance) > FLT_EPSILON || fabs(fTowerVerticalDistance) > FLT_EPSILON)
      iCalculatedElevation = getangle(fTowerHorizontalDistance, fTowerVerticalDistance);// Calculate elevation angle
    else
      iCalculatedElevation = 0;
    velevation = iCalculatedElevation;
    vtilt = 0;
    calculatetransform(-1, vdirection, iCalculatedElevation, 0, worldx, worldy, worldz, DDX, DDY, DDZ);// Set up tower view transformation
    worlddirn = vdirection;
    worldelev = velevation;
    worldtilt = vtilt;
  } else {                                             // Map view mode (mode 6)
    if (iViewMode == 6) {
      worldx = -localdata[2].pointAy[3].fX;     // Set map view position from chunk 2 data
      worldy = -localdata[2].pointAy[3].fY;
      VIEWDIST = 400;                           // Set map view distance
      fCarPosX = Car[iCarIdx].pos.fX;           // Get car position for map view
      fCarPosY3 = Car[iCarIdx].pos.fY;
      fCarPosZ = Car[iCarIdx].pos.fZ;
      fCarPosZ4 = Car[iCarIndex].pos.fZ;
      iCarChunk = Car[iCarIndex].nCurrChunk;
      worldz = 1024.0f - localdata[2].pointAy[3].fZ;
      if (iCarChunk == -1)                    // Transform car position if on track
      {
        fTransformedPosX = fCarPosX;
        fTransformedPosZ = fCarPosZ;
        fTransformedPosY = fCarPosY3;
      } else {
        pCarTransform = &localdata[iCarChunk];
        fTransformedPosX = pCarTransform->pointAy[0].fY * fCarPosY3
          + pCarTransform->pointAy[0].fX * fCarPosX
          + pCarTransform->pointAy[0].fZ * fCarPosZ4
          - pCarTransform->pointAy[3].fX;
        fTransformedPosY = pCarTransform->pointAy[1].fX * fCarPosX
          + pCarTransform->pointAy[1].fY * fCarPosY3
          + pCarTransform->pointAy[1].fZ * fCarPosZ4
          - pCarTransform->pointAy[3].fY;
        fTransformedPosZ = fCarPosY3 * pCarTransform->pointAy[2].fY
          + fCarPosX * pCarTransform->pointAy[2].fX
          + fCarPosZ4 * pCarTransform->pointAy[2].fZ
          - pCarTransform->pointAy[3].fZ;
      }
    } else {
      fCarPosX2 = Car[iCarIdx].pos.fX;          // Top view mode (other modes)
      fCarPosY = Car[iCarIdx].pos.fY;
      fCarPosZ2 = Car[iCarIdx].pos.fZ;
      iCarChunk2 = Car[iCarIndex].nCurrChunk;
      if (iCarChunk2 == -1)                   // Transform car position for top view
      {
        fTransformedPosX = fCarPosX2;
        fTransformedPosZ = fCarPosZ2;
        fTransformedPosY = fCarPosY;
      } else {
        pCarTransform2 = &localdata[iCarChunk2];
        fTransformedPosX = pCarTransform2->pointAy[0].fY * fCarPosY
          + pCarTransform2->pointAy[0].fX * fCarPosX2
          + pCarTransform2->pointAy[0].fZ * fCarPosZ2
          - pCarTransform2->pointAy[3].fX;
        fTransformedPosY = pCarTransform2->pointAy[1].fX * fCarPosX2
          + pCarTransform2->pointAy[1].fY * fCarPosY
          + pCarTransform2->pointAy[1].fZ * fCarPosZ2
          - pCarTransform2->pointAy[3].fY;
        fTransformedPosZ = fCarPosY * pCarTransform2->pointAy[2].fY
          + fCarPosX2 * pCarTransform2->pointAy[2].fX
          + fCarPosZ2 * pCarTransform2->pointAy[2].fZ
          - pCarTransform2->pointAy[3].fZ;
      }
      worldx = fTransformedPosX;                // Set world position with top view height
      worldy = fTransformedPosY;
      worldz = TopViewHeight + fTransformedPosZ;
    }
    fDeltaX = fTransformedPosX - worldx;        // Calculate viewing angles for top/map view
    fDeltaY = fTransformedPosY - worldy;
    NearTow = -1;
    fVerticalDistance = fTransformedPosZ - worldz;
    //if ((LODWORD(fDeltaX) & 0x7FFFFFFF) != 0 || fabs(fTransformedPosY - worldy))
    if (fabs(fDeltaX) > FLT_EPSILON || fabs(fTransformedPosY - worldy))
      vdirection = getangle(fDeltaX, fDeltaY);
    else
      vdirection = 0;
    fHorizontalDistance = (float)sqrt(fDeltaX * fDeltaX + fDeltaY * fDeltaY);
    //if ((LODWORD(fHorizontalDistance) & 0x7FFFFFFF) != 0 || (LODWORD(fVerticalDistance) & 0x7FFFFFFF) != 0)
    if (fabs(fHorizontalDistance) > FLT_EPSILON || fabs(fVerticalDistance) > FLT_EPSILON)
      velevation = getangle(fHorizontalDistance, fVerticalDistance);
    else
      velevation = 0;
    vtilt = 0;
    calculatetransform(-1, vdirection, velevation, 0, worldx, worldy, worldz, DDX, DDY, DDZ);// Set up final transformation and world angles
    worlddirn = vdirection;
    worldelev = velevation;
    worldtilt = vtilt;
  }
}

//-------------------------------------------------------------------------------------------------
//00073570
void initcarview(int iCarIdx, int iPlayer)
{
  int iSelectedView; // ecx
  int iChaseViewIdx; // esi
  unsigned int iControlType; // ebx
  float fChasePullDefault; // esi
  int iYaw; // esi
  int iPitch; // edi
  int iRoll; // ecx
  double dChaseDist; // st7
  double dChaseDistMul; // st7
  int iCurrChunk; // eax
  tData *pData; // eax
  double dTransformZ; // st7
  //int iPlayerShift; // eax
  double dLastPosW; // st7
  float fDirX; // [esp+0h] [ebp-3Ch]
  float fDirY; // [esp+4h] [ebp-38h]
  float fDirZ; // [esp+8h] [ebp-34h]
  float fRightX; // [esp+Ch] [ebp-30h]
  float fUpZ; // [esp+10h] [ebp-2Ch]
  float fSinPitch; // [esp+14h] [ebp-28h]
  float fCameraX; // [esp+18h] [ebp-24h]
  float fCameraZ; // [esp+1Ch] [ebp-20h]
  float fCameraY; // [esp+20h] [ebp-1Ch]

  iSelectedView = SelectedView[iPlayer];        // Get the selected view type for the current player
  if (iSelectedView != 1 && iSelectedView != 3 && iCarIdx >= 0)
    return;                                     // Only process if view is cockpit (1) or chase (3) and car index is valid
  if (iCarIdx >= 0) {
    iChaseViewIdx = chaseview[iPlayer];         // Initialize chase camera parameters from viewdata for valid car
    CHASE_DIST[iPlayer] = viewdata[iChaseViewIdx].fChaseDistance;
    CHASE_MIN[iPlayer] = viewdata[iChaseViewIdx].fChaseMinDistance;
    LOOKZ[iPlayer] = viewdata[iChaseViewIdx].fChaseLookAhead;
  } else {
    CHASE_DIST[0] = viewdata[1].fChaseDistance; // Special case: use default chase view for invalid car index
    CHASE_MIN[0] = viewdata[1].fChaseMinDistance;
    iPlayer = 0;
    iCarIdx = -iCarIdx - 1;
    LOOKZ[0] = viewdata[1].fChaseLookAhead;
  }
  iControlType = Car[iCarIdx].iControlType;     // Get car control type (human player, AI, etc)
  if (!iControlType) {
    fChasePullDefault = viewdata[chaseview[iPlayer]].fChasePullDefault;// Human player: use default chase pull factor
  SET_PULL_FACTOR:
    PULLZ[iPlayer] = fChasePullDefault;
    goto CALCULATE_CAMERA_POSITION;
  }
  if (iControlType >= 2 && iControlType <= 3) {                                             // AI cars: choose pull factor based on crash state
    if (Car[iCarIdx].iStunned)
      fChasePullDefault = viewdata[chaseview[iPlayer]].fChasePullCrash;
    else
      fChasePullDefault = viewdata[chaseview[iPlayer]].fChasePullNormal;
    goto SET_PULL_FACTOR;
  }
CALCULATE_CAMERA_POSITION:
  if (Car[iCarIdx].nCurrChunk == -1)          // Get car orientation: use actual angles if not in a chunk
  {
    iYaw = Car[iCarIdx].nYaw;
    iPitch = Car[iCarIdx].nPitch;
    iRoll = Car[iCarIdx].nRoll;
  } else {
    iYaw = 0;                                   // Car is in a chunk: use neutral orientation
    iPitch = 0;
    iRoll = 0;
  }
  fSinPitch = tsin[iPitch];                     // Calculate sine of pitch angle for camera matrix
  if (replaytype == 2 && replaydirection == -1)// Reverse chase distance if in replay mode with reverse direction
    dChaseDist = -CHASE_DIST[iPlayer];
  else
    dChaseDist = CHASE_DIST[iPlayer];
  dChaseDistMul = -(dChaseDist * 2.0);
  fDirX = -tcos[iYaw] * fSinPitch * tcos[iRoll] - tsin[iYaw] * tsin[iRoll];// Calculate camera direction vector X component using rotation matrix
  fDirY = tcos[iYaw] * tcos[iPitch];            // Calculate camera direction vector Y component
  fCameraX = (float)(dChaseDistMul * fDirY + PULLZ[iPlayer] * fDirX + Car[iCarIdx].pos.fX);// Calculate final camera X position with distance and pull factors
  fDirZ = tsin[iYaw] * tcos[iPitch];
  fRightX = -tsin[iYaw] * fSinPitch * tcos[iRoll] + tcos[iYaw] * tsin[iRoll];
  fCameraY = (float)(dChaseDistMul * fDirZ + PULLZ[iPlayer] * fRightX + Car[iCarIdx].pos.fY);// Calculate final camera Y position
  fUpZ = tcos[iPitch] * tcos[iRoll];
  fCameraZ = (float)(dChaseDistMul * fSinPitch + PULLZ[iPlayer] * fUpZ + Car[iCarIdx].pos.fZ);// Calculate final camera Z position
  iCurrChunk = Car[iCarIdx].nCurrChunk;
  if (iCurrChunk == -1)                       // Branch: handle camera position based on chunk state
  {
    chase_x = fCameraX;                         // No chunk: directly set global chase position and lastpos array
    chase_y = fCameraY;
    lastpos[iPlayer][0].fDistance = 0.0;
    chase_z = fCameraZ;
    lastpos[iPlayer][0].pos.fX = fCameraX;
    lastpos[iPlayer][0].pos.fY = fCameraY;
    lastpos[iPlayer][0].pos.fZ = fCameraZ;
  } else {
    pData = &localdata[iCurrChunk];             // In chunk: transform camera position using chunk transformation matrix
    chase_x = pData->pointAy[0].fY * fCameraY + pData->pointAy[0].fX * fCameraX + pData->pointAy[0].fZ * fCameraZ - pData->pointAy[3].fX;
    chase_y = pData->pointAy[1].fX * fCameraX + pData->pointAy[1].fY * fCameraY + pData->pointAy[1].fZ * fCameraZ - pData->pointAy[3].fY;
    chase_z = pData->pointAy[2].fX * fCameraX + pData->pointAy[2].fY * fCameraY + pData->pointAy[2].fZ * fCameraZ - pData->pointAy[3].fZ;
    lastpos[iPlayer][0].pos.fX = chase_x;
    lastpos[iPlayer][0].pos.fY = pData->pointAy[1].fX * fCameraX + pData->pointAy[1].fY * fCameraY + pData->pointAy[1].fZ * fCameraZ - pData->pointAy[3].fY;
    dTransformZ = fCameraY * pData->pointAy[2].fY + fCameraX * pData->pointAy[2].fX + fCameraZ * pData->pointAy[2].fZ - pData->pointAy[3].fZ;
    lastpos[iPlayer][0].fDistance = 0.0;
    lastpos[iPlayer][0].pos.fZ = (float)dTransformZ;
  }

  // Copy current position to backup slots for interpolation
  lastpos[iPlayer][1].pos.fX = lastpos[iPlayer][0].pos.fX;
  lastpos[iPlayer][1].pos.fY = lastpos[iPlayer][0].pos.fY;  
  lastpos[iPlayer][1].pos.fZ = lastpos[iPlayer][0].pos.fZ;
  //iPlayerShift = iPlayer << 10;                 // Copy current position to backup slots for interpolation
  //*(float *)((char *)&lastpos[0][1].pos.fX + iPlayerShift) = lastpos[iPlayer][0].pos.fX;
  //*(float *)((char *)&lastpos[0][1].pos.fY + iPlayerShift) = lastpos[iPlayer][0].pos.fY;
  //*(float *)((char *)&lastpos[0][1].pos.fZ + iPlayerShift) = lastpos[iPlayer][0].pos.fZ;

  dLastPosW = lastpos[iPlayer][0].fDistance;
  nextpoint[iPlayer] = 1;                       // Set nextpoint flag to indicate new camera position is ready

  lastpos[iPlayer][1].fDistance = (float)dLastPosW;
  //*(float *)((char *)&lastpos[0][1].fDistance + iPlayerShift) = dLastPosW;
}

//-------------------------------------------------------------------------------------------------
//00073920
void newchaseview(int iCarIdx, int iChaseCamIdx)
{
  int iCarIndex; // eax
  int nYaw; // ebx
  int nPitch; // ecx
  int nRoll; // edx
  double dBackwardX; // st7
  double dBackwardY; // st6
  double dBackwardZ; // st5
  int iCurrChunk; // eax
  float *pTransformMatrix; // eax
  float fLookTempX; // edx
  double dLookTransformedY; // st7
  float fLookTempY; // edx
  float fPullTempX; // edx
  double dPullTransformedY; // st7
  float fPullTempY; // edx
  int iPrevIndex; // eax
  int iPrevArrayIndex; // eax
  //int iCurrArrayOffset; // edx
  double dDistanceFromPrev; // st7
  int iNextIndex; // edx
  //int iNewArrayOffset; // edx
  int iLoopCounter; // ebx
  int iSearchIndex; // edx
  double dAccumulatedDistance; // st7
  int iPrevSearchIndex; // eax
  //int iCarArrayOffset; // edi
  //int iSearchArrayIndex; // edx
  //int iPrevArrayOffset; // eax
  int iNextPosIndex; // eax
  //int iCarArrayOffset2; // edi
  //int iNextArrayOffset; // eax
  int iCalculatedDirection; // eax
  int iCalculatedElevation; // eax
  float fLookTempX2; // [esp+8h] [ebp-88h]
  float fPullTempX2; // [esp+8h] [ebp-88h]
  float fZ; // [esp+18h] [ebp-78h]
  float fY; // [esp+24h] [ebp-6Ch]
  float fX; // [esp+28h] [ebp-68h]
  float fPrevPosZ; // [esp+2Ch] [ebp-64h]
  float fPrevPosY; // [esp+30h] [ebp-60h]
  float fPrevPosX; // [esp+34h] [ebp-5Ch]
  float fCurrentDistance; // [esp+38h] [ebp-58h]
  float fVerticalDelta; // [esp+3Ch] [ebp-54h]
  float fHorizontalDistance; // [esp+40h] [ebp-50h]
  float fInterpolationFactor; // [esp+44h] [ebp-4Ch]
  float fViewZ; // [esp+48h] [ebp-48h]
  float fViewY; // [esp+4Ch] [ebp-44h]
  float fViewX; // [esp+50h] [ebp-40h]
  float fHorizontalDeltaX; // [esp+54h] [ebp-3Ch]
  float fLookAtX; // [esp+58h] [ebp-38h]
  float fHorizontalDeltaY; // [esp+5Ch] [ebp-34h]
  float fLookAtY; // [esp+60h] [ebp-30h]
  float fLookAtZ; // [esp+64h] [ebp-2Ch]
  float fCameraPosX; // [esp+68h] [ebp-28h]
  float fCameraPosY; // [esp+6Ch] [ebp-24h]
  float fCameraPosZ; // [esp+70h] [ebp-20h]
  float fAccumulatedDist; // [esp+74h] [ebp-1Ch]
  float fTempAccumulatedDist; // [esp+78h] [ebp-18h]

  iCarIndex = iCarIdx;
  nYaw = Car[iCarIndex].nYaw;                   // Get car orientation and position
  nPitch = Car[iCarIndex].nPitch;
  fX = Car[iCarIndex].pos.fX;
  fY = Car[iCarIndex].pos.fY;
  fZ = Car[iCarIndex].pos.fZ;
  nRoll = Car[iCarIndex].nRoll;
  dBackwardX = -tcos[nYaw] * tsin[nPitch] * tcos[nRoll] - tsin[nYaw] * tsin[nRoll];// Calculate backward direction vector from car orientation
                                                // Uses trigonometric transformation to get direction opposite to car's forward vector
  dBackwardY = -tsin[nYaw] * tsin[nPitch] * tcos[nRoll] + tcos[nYaw] * tsin[nRoll];
  dBackwardZ = tcos[nPitch] * tcos[nRoll];
  fCameraPosX = PULLZ[iChaseCamIdx] * (float)dBackwardX + fX;// Calculate camera position (behind car) using PULLZ distance
  fCameraPosY = PULLZ[iChaseCamIdx] * (float)dBackwardY + fY;
  fCameraPosZ = PULLZ[iChaseCamIdx] * (float)dBackwardZ + fZ;
  fLookAtX = (float)dBackwardX * LOOKZ[iChaseCamIdx] + fX;// Calculate look-at position (in front of car) using LOOKZ distance
  fLookAtY = (float)dBackwardY * LOOKZ[iChaseCamIdx] + fY;
  iCurrChunk = Car[iCarIndex].nCurrChunk;
  fLookAtZ = (float)dBackwardZ * LOOKZ[iChaseCamIdx] + fZ;
  if (iCurrChunk != -1)                       // Transform positions to world coordinates if car is on track
  {
    pTransformMatrix = (float *)&localdata[iCurrChunk];// Apply transformation matrix to convert from car-local to world coordinates
    fLookTempX = (float)dBackwardX * LOOKZ[iChaseCamIdx] + fX;
    fLookAtX = pTransformMatrix[1] * fLookAtY + *pTransformMatrix * fLookAtX + pTransformMatrix[2] * fLookAtZ - pTransformMatrix[9];
    fLookTempX2 = fLookTempX;
    dLookTransformedY = pTransformMatrix[3] * fLookTempX + pTransformMatrix[4] * fLookAtY + pTransformMatrix[5] * fLookAtZ - pTransformMatrix[10];
    fLookTempY = (float)dBackwardY * LOOKZ[iChaseCamIdx] + fY;
    fLookAtY = (float)dLookTransformedY;
    fLookAtZ = pTransformMatrix[6] * fLookTempX2 + pTransformMatrix[7] * fLookTempY + pTransformMatrix[8] * fLookAtZ - pTransformMatrix[11];
    fPullTempX = fCameraPosX;
    fCameraPosX = pTransformMatrix[1] * fCameraPosY + *pTransformMatrix * fCameraPosX + pTransformMatrix[2] * fCameraPosZ - pTransformMatrix[9];
    fPullTempX2 = fPullTempX;
    dPullTransformedY = pTransformMatrix[3] * fPullTempX + pTransformMatrix[4] * fCameraPosY + pTransformMatrix[5] * fCameraPosZ - pTransformMatrix[10];
    fPullTempY = PULLZ[iChaseCamIdx] * (float)dBackwardY + fY;
    fCameraPosY = (float)dPullTransformedY;
    fCameraPosZ = pTransformMatrix[6] * fPullTempX2 + pTransformMatrix[7] * fPullTempY + pTransformMatrix[8] * fCameraPosZ - pTransformMatrix[11];
  }
  iPrevIndex = nextpoint[iChaseCamIdx] - 1;     // Get previous position index in circular buffer
  if (iPrevIndex < 0)
    iPrevIndex = 63;
  iPrevArrayIndex = iPrevIndex;

  // Get current position data from position history buffer
  int iPositionIndex = nextpoint[iChaseCamIdx];
  fPrevPosX = lastpos[iChaseCamIdx][iPositionIndex].pos.fX;
  fPrevPosY = lastpos[iChaseCamIdx][iPositionIndex].pos.fY;
  fPrevPosZ = lastpos[iChaseCamIdx][iPositionIndex].pos.fZ;
  //iCurrArrayOffset = 16 * nextpoint[iChaseCamIdx] + (iChaseCamIdx << 10);// Get current position data from position history buffer
  //fPrevPosX = *(float *)((char *)&lastpos[0][0].pos.fX + iCurrArrayOffset);
  //fPrevPosY = *(float *)((char *)&lastpos[0][0].pos.fY + iCurrArrayOffset);
  //fPrevPosZ = *(float *)((char *)&lastpos[0][0].pos.fZ + iCurrArrayOffset);

  dDistanceFromPrev = sqrt(
                        (fCameraPosX - lastpos[iChaseCamIdx][iPrevArrayIndex].pos.fX) * (fCameraPosX - lastpos[iChaseCamIdx][iPrevArrayIndex].pos.fX)
                      + (fCameraPosY - lastpos[iChaseCamIdx][iPrevArrayIndex].pos.fY) * (fCameraPosY - lastpos[iChaseCamIdx][iPrevArrayIndex].pos.fY)
                      + (fCameraPosZ - lastpos[iChaseCamIdx][iPrevArrayIndex].pos.fZ) * (fCameraPosZ - lastpos[iChaseCamIdx][iPrevArrayIndex].pos.fZ));// Calculate distance from previous recorded position
  if (dDistanceFromPrev >= 3200.0)            // Check if car has moved far enough to record new position (3200 units)
  {
    iNextIndex = nextpoint[iChaseCamIdx] + 1;   // Car moved enough - advance to next position in circular buffer
    nextpoint[iChaseCamIdx] = iNextIndex;
    if (iNextIndex == 64)
      nextpoint[iChaseCamIdx] = 0;

    // Store new position and distance in history buffer
    int iNewPositionIndex = nextpoint[iChaseCamIdx];
    lastpos[iChaseCamIdx][iNewPositionIndex].pos.fX = fCameraPosX;
    lastpos[iChaseCamIdx][iNewPositionIndex].pos.fY = fCameraPosY;
    lastpos[iChaseCamIdx][iNewPositionIndex].pos.fZ = fCameraPosZ;
    lastpos[iChaseCamIdx][iNewPositionIndex].fDistance = (float)sqrt(
        (fPrevPosX - fCameraPosX) * (fPrevPosX - fCameraPosX)
      + (fPrevPosY - fCameraPosY) * (fPrevPosY - fCameraPosY)
      + (fPrevPosZ - fCameraPosZ) * (fPrevPosZ - fCameraPosZ));
    //iNewArrayOffset = 16 * nextpoint[iChaseCamIdx] + (iChaseCamIdx << 10);// Store new position and distance in history buffer
    //*(float *)((char *)&lastpos[0][0].pos.fX + iNewArrayOffset) = fCameraPosX;
    //*(float *)((char *)&lastpos[0][0].pos.fY + iNewArrayOffset) = fCameraPosY;
    //*(float *)((char *)&lastpos[0][0].pos.fZ + iNewArrayOffset) = fCameraPosZ;
    //*(float *)((char *)&lastpos[0][0].fDistance + iNewArrayOffset) = sqrt(
    //                                                                   (fPrevPosX - fCameraPosX) * (fPrevPosX - fCameraPosX)
    //                                                                 + (fPrevPosY - fCameraPosY) * (fPrevPosY - fCameraPosY)
    //                                                                 + (fPrevPosZ - fCameraPosZ) * (fPrevPosZ - fCameraPosZ));
  } else {
    // Car hasn't moved enough - update current position in place
    lastpos[iChaseCamIdx][iPositionIndex].pos.fX = fCameraPosX;
    lastpos[iChaseCamIdx][iPositionIndex].pos.fY = fCameraPosY;
    lastpos[iChaseCamIdx][iPositionIndex].pos.fZ = fCameraPosZ;
    fCurrentDistance = (float)dDistanceFromPrev;
    lastpos[iChaseCamIdx][iPositionIndex].fDistance = fCurrentDistance;
    //*(float *)((char *)&lastpos[0][0].pos.fX + iCurrArrayOffset) = fCameraPosX;// Car hasn't moved enough - update current position in place
    //*(float *)((char *)&lastpos[0][0].pos.fY + iCurrArrayOffset) = fCameraPosY;
    //*(float *)((char *)&lastpos[0][0].pos.fZ + iCurrArrayOffset) = fCameraPosZ;
    //fCurrentDistance = dDistanceFromPrev;
    //*(float *)((char *)&lastpos[0][0].fDistance + iCurrArrayOffset) = fCurrentDistance;
  }
  iLoopCounter = 62;                            // Search for chase camera position at specified distance behind car
                                                // Loop through position history to find point at CHASE_DIST distance
  fAccumulatedDist = 0.0;
  iSearchIndex = nextpoint[iChaseCamIdx];
  while (1) {
    dAccumulatedDistance = fAccumulatedDist + lastpos[iChaseCamIdx][iSearchIndex].fDistance;// Accumulate distance from position history
    if (dAccumulatedDistance >= CHASE_DIST[iChaseCamIdx] || iLoopCounter < 0)
      break;                                    // Check if reached target chase distance or exhausted history
    fTempAccumulatedDist = (float)dAccumulatedDistance;
    fAccumulatedDist = fTempAccumulatedDist;
    iSearchIndex = iSearchIndex - 1 + (iSearchIndex - 1 < 0 ? 0x40 : 0);
    --iLoopCounter;
  }
  if (iLoopCounter <= 0) {
    iNextPosIndex = nextpoint[iChaseCamIdx] + 1;// Not enough history - use position ahead of current
    if (nextpoint[iChaseCamIdx] == 63)
      iNextPosIndex = 0;

    fViewX = lastpos[iChaseCamIdx][iNextPosIndex].pos.fX;
    fViewY = lastpos[iChaseCamIdx][iNextPosIndex].pos.fY;
    fViewZ = lastpos[iChaseCamIdx][iNextPosIndex].pos.fZ;
    //iCarArrayOffset2 = iChaseCamIdx << 10;
    //iNextArrayOffset = 16 * iNextPosIndex;
    //fViewX = *(float *)((char *)&lastpos[0][0].pos.fX + iCarArrayOffset2 + iNextArrayOffset);
    //fViewY = *(float *)((char *)&lastpos[0][0].pos.fY + iCarArrayOffset2 + iNextArrayOffset);
    //fViewZ = *(float *)((char *)&lastpos[0][0].pos.fZ + iCarArrayOffset2 + iNextArrayOffset);
  } else {
    iPrevSearchIndex = iSearchIndex - 1;        // Interpolate camera position between two history points
                                                // Calculate exact position at target distance using linear interpolation
    fInterpolationFactor = (lastpos[iChaseCamIdx][iSearchIndex].fDistance + fAccumulatedDist - CHASE_DIST[iChaseCamIdx]) / lastpos[iChaseCamIdx][iSearchIndex].fDistance;


    // Handle circular buffer wraparound for previous index
    int iPrevIndex = (iPrevSearchIndex < 0) ? (iPrevSearchIndex + 64) : iPrevSearchIndex;
    fViewX = (lastpos[iChaseCamIdx][iSearchIndex].pos.fX - lastpos[iChaseCamIdx][iPrevIndex].pos.fX)
           * fInterpolationFactor
           + lastpos[iChaseCamIdx][iPrevIndex].pos.fX;
    fViewY = (lastpos[iChaseCamIdx][iSearchIndex].pos.fY - lastpos[iChaseCamIdx][iPrevIndex].pos.fY)
           * fInterpolationFactor
           + lastpos[iChaseCamIdx][iPrevIndex].pos.fY;
    fViewZ = fInterpolationFactor
           * (lastpos[iChaseCamIdx][iSearchIndex].pos.fZ - lastpos[iChaseCamIdx][iPrevIndex].pos.fZ)
           + lastpos[iChaseCamIdx][iPrevIndex].pos.fZ;
    //iCarArrayOffset = iChaseCamIdx << 10;
    //iSearchArrayIndex = iSearchIndex;
    //iPrevArrayOffset = 16 * (iPrevSearchIndex + (iPrevSearchIndex < 0 ? 64 : 0));
    //fViewX = (*(float *)((char *)&lastpos[0][iSearchArrayIndex].pos.fX + iCarArrayOffset) - *(float *)((char *)&lastpos[0][0].pos.fX + iCarArrayOffset + iPrevArrayOffset))
    //  * fInterpolationFactor
    //  + *(float *)((char *)&lastpos[0][0].pos.fX + iCarArrayOffset + iPrevArrayOffset);
    //fViewY = (*(float *)((char *)&lastpos[0][iSearchArrayIndex].pos.fY + iCarArrayOffset) - *(float *)((char *)&lastpos[0][0].pos.fY + iCarArrayOffset + iPrevArrayOffset))
    //  * fInterpolationFactor
    //  + *(float *)((char *)&lastpos[0][0].pos.fY + iCarArrayOffset + iPrevArrayOffset);
    //fViewZ = fInterpolationFactor
    //  * (*(float *)((char *)&lastpos[0][iSearchArrayIndex].pos.fZ + iCarArrayOffset) - *(float *)((char *)&lastpos[0][0].pos.fZ + iCarArrayOffset + iPrevArrayOffset))
    //  + *(float *)((char *)&lastpos[0][0].pos.fZ + iCarArrayOffset + iPrevArrayOffset);
  }
  fHorizontalDeltaX = fLookAtX - fViewX;        // Calculate direction from camera to look-at point
  fHorizontalDeltaY = fLookAtY - fViewY;
  //if ((LODWORD(fHorizontalDeltaX) & 0x7FFFFFFF) != 0 || fabs(fLookAtY - fViewY))// Calculate horizontal direction angle (yaw)
  if (fabs(fHorizontalDeltaX) > FLT_EPSILON || fabs(fLookAtY - fViewY))// Calculate horizontal direction angle (yaw)
    iCalculatedDirection = getangle(fHorizontalDeltaX, fHorizontalDeltaY);
  else
    iCalculatedDirection = lastcamdirection;    // Use last camera direction if no movement detected
  vdirection = iCalculatedDirection;
  fHorizontalDistance = (float)sqrt(fHorizontalDeltaX * fHorizontalDeltaX + fHorizontalDeltaY * fHorizontalDeltaY);// Calculate elevation angle (pitch) from horizontal distance and height difference
  //if ((LODWORD(fHorizontalDistance) & 0x7FFFFFFF) != 0 || fabs(fLookAtZ - fViewZ)) {
  if (fabs(fHorizontalDistance) > FLT_EPSILON || fabs(fLookAtZ - fViewZ)) {
    fVerticalDelta = fLookAtZ - fViewZ;
    iCalculatedElevation = getangle(fHorizontalDistance, fVerticalDelta);
  } else {
    iCalculatedElevation = lastcamelevation;    // Use last camera elevation if no height difference
  }
  velevation = iCalculatedElevation;
  vtilt = 0;
  calculatetransform(-1, vdirection, iCalculatedElevation, 0, fViewX, fViewY, fViewZ, DDX, DDY, DDZ);// Set up camera transformation matrix with calculated position and orientation
  lastcamdirection = vdirection;                // Save camera angles for next frame to avoid sudden changes
  lastcamelevation = velevation;
  worlddirn = vdirection;                       // Set global world orientation values
  worldelev = velevation;
  worldtilt = vtilt;
}

//-------------------------------------------------------------------------------------------------
