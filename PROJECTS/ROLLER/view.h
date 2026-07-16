#ifndef _ROLLER_VIEW_H
#define _ROLLER_VIEW_H
//-------------------------------------------------------------------------------------------------
#include "types.h"
#include <stdbool.h>
//-------------------------------------------------------------------------------------------------

typedef struct
{
  float fChaseDistance;
  float fChaseMinDistance;
  float fChasePullNormal;
  float fChasePullCrash;
  float fChasePullDefault;
  float fChaseLookAhead;
} tViewData;

//-------------------------------------------------------------------------------------------------

typedef struct
{
  tVec3 pos;
  float fDistance;
} tCameraPos;

//-------------------------------------------------------------------------------------------------

extern tViewData viewdata[2];
extern int chaseview[2];
extern float CHASE_DIST[2];
extern float CHASE_MIN[2];
extern float PULLZ[2];
extern float LOOKZ[2];
extern int nextpoint[2];
extern tCameraPos lastpos[2][64];
extern float TowerGx;
extern float TowerGy;
extern float TowerGz;
extern int lastcamelevation;
extern int lastcamdirection;
extern int NearTow;
extern float chase_x;
extern float chase_y;
extern float chase_z;

//-------------------------------------------------------------------------------------------------

void calculateview(int iViewMode, int iCarIdx, int iChaseCamIdx);
void initcarview(int iCarIdx, int iPlayer);
void newchaseview(int iCarIdx, int iChaseCamIdx);
void noclip_camera_reset(void);
void noclip_camera_set_input_enabled(bool bEnabled);
void noclip_camera_update(void);
void noclip_camera_apply(void);
void chase_look_apply(void);

//-------------------------------------------------------------------------------------------------
#endif
