#ifndef _ROLLER_TOWER_H
#define _ROLLER_TOWER_H
//-------------------------------------------------------------------------------------------------
#include "polyf.h"
//-------------------------------------------------------------------------------------------------

#define MAX_TOWERS 32

typedef struct
{
  int iChunkIdx;
  int iHOffset;
  int iVOffset;
  int iEnabled;
  int iTowerType;
} tTowerBase;

//-------------------------------------------------------------------------------------------------

extern int TowerSect[MAX_TRACK_CHUNKS];
extern float TowerX[MAX_TOWERS];
extern float TowerY[MAX_TOWERS];
extern float TowerZ[MAX_TOWERS];
extern tTowerBase TowerBase[MAX_TOWERS];
extern tPolyParams TowerPol;
extern int NumTowers;

//-------------------------------------------------------------------------------------------------

void InitTowers();
void tower_emit_marker(int iTowerIdx, float fScale);
void DrawTower(int iTowerIdx, uint8 *pScrBuf);

//-------------------------------------------------------------------------------------------------
#endif
