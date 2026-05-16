#ifndef _ROLLER_CONTROL_H
#define _ROLLER_CONTROL_H
//-------------------------------------------------------------------------------------------------
#include "types.h"
#include "engines.h"
#include "car.h"
#include "3d.h"
//-------------------------------------------------------------------------------------------------

typedef struct
{
  int strategyAy[5];
  float fSteerSensitivity;
  float fSteerDamping;
  float fAvoidSensitivity;
  float fAvoidReaction;
  float fAvoidDistance;
} tCarStrategy;

//-------------------------------------------------------------------------------------------------

extern float levels[7];
extern float mineff[6];
extern int flipst[6];
extern int level;
extern tCarStrategy CarStrategy[16];
extern int nearcall[4][4];
extern int carorder[16];
extern int stops[10];
extern float trial_times[96];
extern float eng_chg_revs[168];
extern int FirstTick;
extern int JBYmax;
extern int JBYmin;
extern int JBXmin;
extern int JBXmax;
extern int JAYmax;
extern int JAXmax;
extern int numstops;
extern int JAYmin;
extern int JAXmin;
extern int race_started;
extern int updates;
extern float RecordLaps[25];
extern int RecordCars[25];
extern int RecordKills[25];
extern int Destroyed;
extern int nearcarcheck;
extern int ahead_sect;
extern int ahead_time;
extern int Victim;
extern int Fatality;
extern int Fatality_Count;
extern int cheat_control;
extern int fudge_wait;
extern char RecordNames[25][9];

//-------------------------------------------------------------------------------------------------

void humancar(int iCarIdx);
void GoUpGear(tCar *pCar);
void GoDownGear(tCar *pCar, int iUseAutoLogic);
void control();
void control_one_tick(void);
double calc_revs(tRevCurve *pRevs, int iGear, float fChg);
double calc_pow(int iCarDesignIdx, int iCurrentGear, float fRPMRatio);
void Accelerate(tCar *pCar);
void Decelerate(tCar *pCar);
void FreeWheel(tCar *pCar);
void SetEngine(tCar *pCar, float fThrottle);
double change_gear(int iCurrentGear, int iNextGear, tCar *pCar, int iCarDesignIdx);
void updatecar2(tCar *pCar);
void check_crossed_line(tCar *pCar);
void checkplacement(tCar *pCar);
void testteaminit(tCar *pCar);
void doteaminit();
void hitleft(tCar *pCar, int iSampleIdx, int iIsRightSide);
void hitright(tCar *pCar, int iSampleIdx, int iIsRightSide);
void scansection(tCar *pCar);
double getgroundz(float fXPos, float fYOffset, int iChunkIdx);
double getroadz(float fX, float fY, int iChunkIdx);
void putflat(tCar *pCar);
void findnearcars(tCar *pCar, int *piLeftCarIdx, float *pfLeftTime, int *piRightCarIdx, float *pfRightTime, float *pfTargetX, float *pfTargetY);
void findnearcarsforce(tCar *pCar, int *piLeftCarIdx, float *pfLeftTime, int *piRightCarIdx, float *pfRightTime, float *pfTargetX, float *pfTargetY);
double interpolatesteer(float fSteeringInput, float fSaturationThreshold, float fDeadzoneThreshold, float fMaxOutput, float fMinOutput);
double avoid(int iCurrentCarIdx, int iTargetCarIdx, float fSteeringInput, float fMaxOutput, float fSaturationThreshold, float fDeadzoneThreshold, int *piOvertakeFlag);
double block(int iCarIdx, float fSteeringInput, float fMaxOutput, float fSaturationThreshold, float fDeadzoneThreshold);
void autocar2(tCar *pCar);
void changestrategy(tCar *pCar);
int getangle(float fX, float fY);
void landontrack(tCar *pCar);
void converttoair(tCar *pCar);
void ordercars();
void changeline(tCar *pCar);
void driverange(tCar *pCar, float *pfLeftLimit, float *pfRightLimit);
void updatesmokeandflames(tCar *pCar);
void dospray(tCar *pCar, int iCinematicMode, tCarSpray *pCarSpray);
void calculateseparatedcoordinatesystem(int iChunk, tData *pChunkData);
void findnearsection(tCar *pCar, int *piNearestChunk);
void dozoomstuff(int iPlayerIdx);
int findcardistance(int iCarIdx, float fMaxDistance);

//-------------------------------------------------------------------------------------------------
#endif
