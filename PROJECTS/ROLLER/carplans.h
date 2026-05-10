#ifndef _ROLLER_CARPLANS_H
#define _ROLLER_CARPLANS_H
//-------------------------------------------------------------------------------------------------
#include "types.h"
//-------------------------------------------------------------------------------------------------

typedef struct {
  uint32 uiCount;
  uint32 framesAy[16];
} tAnimation;

//-------------------------------------------------------------------------------------------------

typedef struct {
  uint8 verts[4];
  uint32 uiTex;
  int16 nNextPolIdx;
  uint16 padding;
} tPolygon;

//-------------------------------------------------------------------------------------------------

typedef struct {
  uint8 byNumPols;
  uint8 byNumCoords;
  uint8 byUnk3;
  uint8 byUnk4;
  tPolygon *pPols;
  tVec3 *pCoords;
  uint32 *pBacks;
  eCarType carType;
  int *pPlaces;
  tAnimation *pAnms;
} tCarDesign;

//-------------------------------------------------------------------------------------------------

typedef struct {
  uint32 uiColorFrom;
  uint32 uiColorTo;
} tCarColorRemap;

//-------------------------------------------------------------------------------------------------

extern tVec3 f1wack_coords[];
extern tPolygon f1wack_pols[];
extern unsigned int f1wack_backs[];
extern int f1wack_places[];
extern tAnimation f1wack_anms[];

//-------------------------------------------------------------------------------------------------

extern tVec3 xauto_coords[];
extern tPolygon xauto_pols[];
extern unsigned int xauto_backs[];
extern int xauto_places[];
extern tAnimation xauto_anms[];

//-------------------------------------------------------------------------------------------------

extern tVec3 xdesilva_coords[];
extern tPolygon xdesilva_pols[];
extern unsigned int xdesilva_backs[];
extern int xdesilva_places[];
extern tAnimation xdesilva_anms[];

//-------------------------------------------------------------------------------------------------

extern tVec3 xpulse_coords[];
extern tPolygon xpulse_pols[];
extern unsigned int xpulse_backs[];
extern int xpulse_places[];
extern tAnimation xpulse_anms[];

//-------------------------------------------------------------------------------------------------

extern tVec3 xglobal_coords[];
extern tPolygon xglobal_pols[];
extern unsigned int xglobal_backs[];
extern int xglobal_places[];
extern tAnimation xglobal_anms[];

//-------------------------------------------------------------------------------------------------

extern tVec3 xmillion_coords[];
extern tPolygon xmillion_pols[];
extern unsigned int xmillion_backs[];
extern int xmillion_places[];
extern tAnimation xmillion_anms[];

//-------------------------------------------------------------------------------------------------

extern tVec3 xmission_coords[];
extern tPolygon xmission_pols[];
extern unsigned int xmission_backs[];
extern int xmission_places[];
extern tAnimation xmission_anms[];

//-------------------------------------------------------------------------------------------------

extern tVec3 xzizin_coords[];
extern tPolygon xzizin_pols[];
extern unsigned int xzizin_backs[];
extern int xzizin_places[];
extern tAnimation xzizin_anms[];

//-------------------------------------------------------------------------------------------------

extern tVec3 xreise_coords[];
extern tPolygon xreise_pols[];
extern unsigned int xreise_backs[];
extern int xreise_places[];
extern tAnimation xreise_anms[];

//-------------------------------------------------------------------------------------------------

extern char CarNames[14][20];
extern char CompanyNames[14][20];
extern tCarDesign CarDesigns[];
extern char car_texture_names[11][256];
extern int Drivers_Car[16];
extern tCarColorRemap
    car_flat_remap[]; // this is used to remap the palette indices used to color
                      // the mirrors for the advanced car set

//-------------------------------------------------------------------------------------------------
#endif