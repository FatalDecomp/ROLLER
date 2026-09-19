#ifndef ROLLER_NET_SIM_SEAM_H
#define ROLLER_NET_SIM_SEAM_H
#include "net_protocol.h"
#include "car.h"
#include "sound.h"
#include "moving.h"

typedef struct {
  uint32 uiTick;
  tCopyData aInputs[MAX_CARS];
} tNetInputSlot;

typedef struct {
  int iGameFrame;
  int iCountdown;
  int iRaceStarted;
  int iRacing;
  int iWarpAngle;
  int iView0Cnt;
  int iView1Cnt;
  int iQuitCount;
  int iNearcarcheck;
  int iUpdates;
  int iReadptr;
  int iWriteptr;
  int iStartRace;
  int iFatality;
  int iFatalityCount;
  int iDestroyed;
  int iVictim;
  int iCheatControl;
  int iFudgeWait;
  int iFinishers;
  int iHumanFinishers;
  int iLastsample;
  int iGameOvers;
  int iDisableMessages;
  int aiCarOrder[16];
  int aiFinished[16];
  int aiNearCall[4][4];
  float afRecordLaps[25];
  int aiRecordCars[25];
  int aiRecordKills[25];
  char aszRecordNames[25][9];
  tCarSpray aSpray[18][32];
  tSLight aLights[2][3];
  uint32 uiRandomState;
  uint64 ullRandomDraws;
} tNetSimTickContext;

typedef struct {
  tVec3 position;
  int16 nYaw, nPitch, nRoll, nActualYaw;
} tNetWorldPose;

extern int net_sim_replaying;
extern int net_sim_authority;
extern uint8 net_puppet_car[MAX_CARS];
extern void (*net_sim_puppet_hook)(void);

void NetSimCaptureContext(tNetSimTickContext *pContext);
void NetSimRestoreContext(const tNetSimTickContext *pContext);
int NetSimRestoreInputRing(const tNetInputSlot *pSlots, int iFirstTick, int iCount, int iReadPtr);
int NetSimWriteTickInputs(const tCopyData *pInputs, int iNumCars);
void NetSimSaveCar(int iCar, tCar *pSaved);
void NetSimRestoreCar(int iCar, const tCar *pSaved);
void NetSimSaveRamps(tNetRampState *pStates);
int NetSimRestoreRamps(const tNetRampState *pStates);
int NetSimSetRampState(int iRamp, const tNetRampState *pState);
int NetSimAdvanceRampStateCopy(int iRamp, tNetRampState *pState, int iTicks);
void NetSimSetPuppet(int iCar, int iPuppet);
int NetSimIsPuppet(const tCar *pCar);
void NetSimCanonicaliseInput(tCarInputData *pInput);
int NetSimLegacyToWorld(const tCar *pCar, tNetWorldPose *pPose);
int NetSimWorldToLegacy(const tNetWorldPose *pPose, tCar *pCar);
void NetSimRehomeChunk(tCar *pCar);
#endif
