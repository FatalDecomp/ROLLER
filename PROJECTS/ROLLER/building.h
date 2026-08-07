#ifndef _ROLLER_BUILDING_H
#define _ROLLER_BUILDING_H
//-------------------------------------------------------------------------------------------------
#include "types.h"
#include "3d.h"
#include "polyf.h"
#include "carplans.h"
#include "editor_surface.h"
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

// BuildingPlans[] has this many entries; InitBuildings places nothing for a
// building whose type falls outside it.
#define BUILDING_PLAN_COUNT 17
// The widest plan (quadbld) has 32 coordinates and 20 polygons.
#define BUILDING_MAX_PLAN_COORDS 32

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

// True for the plans DrawBuilding rotates by worlddirn rather than by their
// authored yaw: the balloons (9, 15) and the tree (10).
bool building_type_is_billboard(unsigned int uiBuildingType);

typedef enum
{
  // What DrawBuilding uses: a billboard plan turns to face the viewer through
  // worlddirn, so its world positions change every frame.
  BUILDING_YAW_RENDER = 0,
  // What the canonical scenery traversal uses: every plan takes the yaw the
  // track file placed it with, so no viewer is involved anywhere. See
  // docs/adr/0005-camera-independent-scenery-traversal.md.
  BUILDING_YAW_AUTHORED = 1
} eBuildingYawSource;

// World positions for one placed building's plan. Returns the coordinate count
// written, or 0 when the index or its plan is out of range. With
// BUILDING_YAW_AUTHORED nothing here reads the viewer.
uint32 building_transform_plan_coords(int iBuildingIdx,
                                      eBuildingYawSource eYawSource,
                                      tVec3 aWorld[BUILDING_MAX_PLAN_COORDS]);

// Camera-independent texture selection and canonical identity for one plan
// polygon. bApplyRenderToggles honours textures_off; the export traversal
// passes false so a view setting cannot change exported materials.
bool building_polygon_surface_info(int iBuildingIdx,
                                   const tPolygon *pPolygon,
                                   bool bApplyRenderToggles,
                                   tEdSurfaceInfo *pInfo);
void init_animate_ads();
int bldZcmp(const void *pBuilding1, const void *pBuilding2);

//-------------------------------------------------------------------------------------------------
#endif
