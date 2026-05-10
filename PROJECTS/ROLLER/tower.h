#ifndef _ROLLER_TOWER_H
#define _ROLLER_TOWER_H
//-------------------------------------------------------------------------------------------------
#include "polyf.h"
//-------------------------------------------------------------------------------------------------

typedef struct {
  int iChunkIdx;
  int iHOffset;
  int iVOffset;
  int iEnabled;
  int iTowerType;
} tTowerBase;

//-------------------------------------------------------------------------------------------------

extern int TowerSect[MAX_TRACK_CHUNKS];
extern float TowerX[32];
extern float TowerY[32];
extern float TowerZ[32];
extern tTowerBase TowerBase[32];
extern tPolyParams TowerPol;
extern int NumTowers;

//-------------------------------------------------------------------------------------------------

void InitTowers();
void DrawTower(int iTowerIdx, uint8 *pScrBuf);

//-------------------------------------------------------------------------------------------------
#endif