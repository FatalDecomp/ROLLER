#ifndef _ROLLER_PLANS_H
#define _ROLLER_PLANS_H
//-------------------------------------------------------------------------------------------------
#include "carplans.h"
//-------------------------------------------------------------------------------------------------

typedef struct {
  uint8 byNumPols;
  uint8 byNumCoords;
  uint8 byPadding1;
  uint8 byPadding2;
  tPolygon *pPols;
  tVec3 *pCoords;
} tBuildingPlan;

//-------------------------------------------------------------------------------------------------

extern tVec3 tower_coords[];
extern tPolygon tower_pols[];
extern unsigned int tower_backs[];
extern tAnimation tower_anms[];

//-------------------------------------------------------------------------------------------------

extern tVec3 tower2_coords[];
extern tPolygon tower2_pols[];
extern unsigned int tower2_backs[];

//-------------------------------------------------------------------------------------------------

extern tVec3 sign01_coords[];
extern tPolygon sign01_pols[];
extern unsigned int sign01_backs[];
extern tAnimation sign01_anms[];

//-------------------------------------------------------------------------------------------------

extern tVec3 sign02_coords[];
extern tPolygon sign02_pols[];
extern unsigned int sign02_backs[];
extern tAnimation sign02_anms[];

//-------------------------------------------------------------------------------------------------

extern tVec3 build_coords[];
extern tPolygon build_pols[];
extern unsigned int build_backs[];

//-------------------------------------------------------------------------------------------------

extern tVec3 build1_coords[];
extern tPolygon build1_pols[];
extern unsigned int build1_backs[];

//-------------------------------------------------------------------------------------------------

extern tVec3 build2_coords[];
extern tPolygon build2_pols[];
extern unsigned int build2_backs[];

//-------------------------------------------------------------------------------------------------

extern tVec3 build3_coords[];
extern tPolygon build3_pols[];
extern unsigned int build3_backs[];
extern tAnimation build3_anms[];

//-------------------------------------------------------------------------------------------------

extern tVec3 heelbar_coords[];
extern tPolygon heelbar_pols[];
extern unsigned int heelbar_backs[];

//-------------------------------------------------------------------------------------------------

extern tVec3 balloon_coords[];
extern tPolygon balloon_pols[];

//-------------------------------------------------------------------------------------------------

extern tVec3 balloon2_coords[];
extern tPolygon balloon2_pols[];
extern unsigned int balloon2_backs[];
extern tAnimation balloon2_anms[];

//-------------------------------------------------------------------------------------------------

extern tVec3 tree_coords[];
extern tPolygon tree_pols[];
extern unsigned int tree_backs[];

//-------------------------------------------------------------------------------------------------

extern tVec3 advert_coords[];
extern tPolygon advert_pols[];
extern unsigned int advert_backs[];
extern tAnimation advert_anms[];

//-------------------------------------------------------------------------------------------------

extern tVec3 advert2_coords[];
extern tPolygon advert2_pols[];
extern unsigned int advert2_backs[];
extern tAnimation advert2_anms[];

//-------------------------------------------------------------------------------------------------

extern tVec3 advert3_coords[];
extern tPolygon advert3_pols[];
extern unsigned int advert3_backs[];
extern tAnimation advert3_anms[];

//-------------------------------------------------------------------------------------------------

extern tVec3 quadbld_coords[];
extern tPolygon quadbld_pols[];
extern unsigned int quadbld_backs[];

//-------------------------------------------------------------------------------------------------

extern tVec3 bld0_coords[];
extern tPolygon bld0_pols[];
extern unsigned int bld0_backs[];

//-------------------------------------------------------------------------------------------------

extern char building_names[17][256];
extern tBuildingPlan BuildingPlans[];

//-------------------------------------------------------------------------------------------------
#endif