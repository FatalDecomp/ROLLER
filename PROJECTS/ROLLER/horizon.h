#ifndef _ROLLER_HORIZON_H
#define _ROLLER_HORIZON_H
//-------------------------------------------------------------------------------------------------
#include "types.h"
//-------------------------------------------------------------------------------------------------

typedef struct {
  float matrix[4][3];
  tVec3 world;
  int iSurfaceType;
  float fRadius;
} tCloudData;

//-------------------------------------------------------------------------------------------------

extern tCloudData cloud[40];
extern int test_y1;
extern char upside_down;
extern char ground_left;

//-------------------------------------------------------------------------------------------------

void DrawHorizon(uint8 *pScrBuf);
void initclouds();
void displayclouds(uint8 *pScrBuf);

//-------------------------------------------------------------------------------------------------
#endif