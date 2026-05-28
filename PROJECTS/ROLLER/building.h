#ifndef _ROLLER_BUILDING_H
#define _ROLLER_BUILDING_H
//-------------------------------------------------------------------------------------------------
#include "types.h"
#include "3d.h"
#include "polyf.h"
//-------------------------------------------------------------------------------------------------

typedef struct
{
  int iBuildingIdx;
  float fDepth;
} tVisibleBuilding;

//-------------------------------------------------------------------------------------------------

typedef struct
{
  float fZDepth;
  int iPolygonLink;
  int iPolygonIndex;
} tBuildingZOrderEntry;

//-------------------------------------------------------------------------------------------------

typedef struct
{
  int iX;
  int iY;
  int iClipped;
} tBuildingCoord;

//-------------------------------------------------------------------------------------------------

#define MAX_VISIBLE_BUILDINGS 256

//-------------------------------------------------------------------------------------------------

extern uint8 BuildingSub[24];
extern tBuildingZOrderEntry BuildingZOrder[32];
extern int BuildingSect[MAX_TRACK_CHUNKS];
extern float BuildingAngles[768];
extern int BuildingBase[MAX_VISIBLE_BUILDINGS][4];
extern tVec3 BuildingBox[MAX_VISIBLE_BUILDINGS][8];
extern float BuildingBaseX[MAX_VISIBLE_BUILDINGS];
extern float BuildingBaseY[MAX_VISIBLE_BUILDINGS];
extern float BuildingBaseZ[MAX_VISIBLE_BUILDINGS];
extern float BuildingX[MAX_VISIBLE_BUILDINGS];
extern float BuildingY[MAX_VISIBLE_BUILDINGS];
extern float BuildingZ[MAX_VISIBLE_BUILDINGS];
extern tVisibleBuilding VisibleBuildings[MAX_VISIBLE_BUILDINGS];
extern int16 advert_list[MAX_VISIBLE_BUILDINGS];
extern int NumBuildings;
extern int NumVisibleBuildings;

//-------------------------------------------------------------------------------------------------

void InitBuildings();
void CalcVisibleBuildings();
void DrawBuilding(int iBuildingIdx, uint8 *pScrPtr);
void init_animate_ads();
int bldZcmp(const void *pBuilding1, const void *pBuilding2);

//-------------------------------------------------------------------------------------------------
#endif
