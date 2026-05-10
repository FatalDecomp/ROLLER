#ifndef _ROLLER_CAR_H
#define _ROLLER_CAR_H
//-------------------------------------------------------------------------------------------------
#include "carplans.h"
#include "frontend.h"
#include "polyf.h"
//-------------------------------------------------------------------------------------------------

#define CAR_STATUS_DEATH 0x02
#define CAR_STATUS_ACTIVE 0x04
#define CAR_STATUS_PIT 0x08

//-------------------------------------------------------------------------------------------------

typedef struct {
  tVec3 hitboxAy[16][8];
} tCarBox;

//-------------------------------------------------------------------------------------------------

typedef struct {
  tVec3 pos;
  int16 nCurrChunk;
  int16 nReferenceChunk;
  int16 nRoll;
  int16 nPitch;
  int16 nYaw;
  // padding byte
  // padding byte
  float fFinalSpeed;
  float fHealth;
  int iDriverIdx;
  float fHorizontalSpeed;
  tVec3 direction;
  tVec3 posLastFrame;
  int nActualYaw;
  int iJumpMomentum;
  int iControlType;
  int iSteeringInput;
  int iBankingSteerOffset;
  float fCarHalfWidth;
  float fCarWidthBankingProjection;
  int iTrackedCarIdx;
  int iUnused;
  int16 nDeathTimer;
  uint8 byCarDesignIdx;
  uint8 byLives;
  uint8 byLapNumber;
  // padding byte
  int16 nExplosionSoundTimer;
  float fLastAnimationSpeed;
  float fBaseSpeed;
  float fSpeedOverflow;
  float fRPMRatio;
  float fPower;
  uint8 byGearAyMax;
  char byLap;
  uint8 byRacePosition;
  uint8 byStatusFlags;
  int iPitchDynamicOffset;
  int iRollDynamicOffset;
  int iPitchBackup;
  int iCameraOscillationPhase;
  int iPitchCameraOffset;
  int iRollDampingMomentum;
  int iRollCameraOffset;
  int iAICurrentLine;
  int iRollMotion;
  int iPitchMotion;
  int iYawMotion;
  int iAIUpdateTimer;
  float fDurability;
  uint8 byDebugDamage;
  uint8 byAttacker;
  uint8 byKills;
  uint8 byDamageSourceTimer;
  int iBobMode;
  int iSelectedStrategy;
  int iAITargetLine;
  int iRollMomentum;
  int iStunned;
  int iAITargetCar;
  int iLaneType;
  int iLastValidChunk;
  float fRunningLapTime;
  float fBestLapTime;
  float fPreviousLapTime;
  float fTotalRaceTime;
  int16 nTargetChunk;
  int16 nChangeMateCooldown;
  uint8 byThrottlePressed;
  uint8 byAccelerating;
  uint8 byAIThrottleControl;
  uint8 byEngineStartTimer;
  uint8 byPitLaneActiveFlag;
  // padding byte
  // padding byte
  // padding byte
  int iLeftTargetIdx;
  float fLeftTargetTime;
  int iRightTargetIdx;
  float fRightTargetTime;
  float fTargetX;
  float fTargetY;
  uint8 byCollisionTimer;
  uint8 byDamageToggle;
  uint8 byLastDamageToggle;
  uint8 byDamageIntensity;
  uint8 byCheatAmmo;
  uint8 byCheatCooldown;
  uint8 byWheelAnimationFrame;
  // padding byte
  int iDamageState;
  int16 nLastCommentaryChunk;
  int16 nReverseWarnCooldown;
  int iEngineState;
  float fWheelSpinAccumulation;
  float fWheelSpinFactor;
  int iEngineVibrateOffset;
  uint8 byRepairSpeechPlayed;
  uint8 byLappedStatus;
  uint8 bySfxCooldown;
  // padding byte
} tCar;

//-------------------------------------------------------------------------------------------------

typedef struct {
  float speeds[6];
} tStoreEngine;

//-------------------------------------------------------------------------------------------------

typedef struct {
  tPoint screen;
  tVec3 world;
  tVec3 view;
} tCarPt;

//-------------------------------------------------------------------------------------------------

typedef struct {
  int iPolygonLink;
  int iPolygonIndex;
  float fZDepth;
} tCarZOrderEntry;

//-------------------------------------------------------------------------------------------------

typedef struct {
  tVec3 position;
  tVec3 velocity;
  float fSize;
  int iLifeTime;
  int iTimer;
  int iColor;
  int iType;
} tCarSpray;

typedef struct CarRenderPose {
  tVec3 position;
  int yaw;
  int pitch;
  int roll;
} CarRenderPose;

typedef struct CarRenderOptions {
  int anim_frame;
  const uint8 *color_remap;
} CarRenderOptions;

//-------------------------------------------------------------------------------------------------

typedef struct {
  int iChunkIdx;
  int iCarIdx;
  float fMinZDepth;
} tCarDrawOrder;

//-------------------------------------------------------------------------------------------------

typedef struct {
  tVec3 currentPos;
  tVec3 targetPos;
  tVec3 speed;
  uint32 uiRotation;
  float fUnk1;
  float fUnk2;
} tSLight;

//-------------------------------------------------------------------------------------------------

extern int numcars;
extern int team_col[16];
extern char default_names[16][9];
extern int16 ViewType[2];
extern tVec3 carworld[4];
extern tVec3 carpoint[4];
extern tVec3 carlocal[4];
extern float roadheight[4];
extern tCarPt CarPt[128];
extern tCarZOrderEntry CarZOrder[MAX_TRACK_CHUNKS];
extern tCarDrawOrder car_draw_order[16];
extern int car_texmap[16];
extern tCarBox CarBox;
extern tCar Car[16];
extern int car_persps[128];
extern tCarPt SmokePt[2][64];
extern int car_texs_loaded[16];
extern tCarSpray CarSpray[18][32];
extern char driver_names[16][9];
extern tStoreEngine StoreEngines[14];
extern tSLight SLight[2][3];
extern int finished_car[16];
extern int grid[16];
extern int cars_drawn;
extern tPolyParams CarPol;
extern float CarBaseX;
extern float CarBaseY;
extern int LoadCarTextures;
extern float CarDiag;

//-------------------------------------------------------------------------------------------------

void InitCarStructs();
void CalcCarSizes();
void InitCars();
void placecars();
void DrawCars(int iCarIdx, int iViewMode);
void DisplayCar(int iCarIndex, uint8 *pScreenBuffer, float fDistanceToCar);
void DisplayCarWithPose(int iCarIndex, uint8 *pScreenBuffer,
                        float fDistanceToCar, const CarRenderPose *pose,
                        const CarRenderOptions *options);
int carZcmp(const void *pCar1, const void *pCar2);

//-------------------------------------------------------------------------------------------------
#endif
