#ifndef _ROLLER_DRAWTRK3_H
#define _ROLLER_DRAWTRK3_H
//-------------------------------------------------------------------------------------------------
#include "polyf.h"
#include "types.h"
#include "3d.h"
//-------------------------------------------------------------------------------------------------

extern int showsub;
extern int view_limit;
extern int divtype;
extern int NextSect[MAX_TRACK_CHUNKS];
extern int tex_hgt;
extern int polyysize;
extern int polyxsize;
extern uint8 *subptr;
extern int fliptype;
extern int subpolytype;
extern tPolyParams *subpoly;
extern int tex_wid;
extern int flatpol;
extern tPolyParams RoofPoly;
extern tPolyParams G5Poly;
extern tPolyParams G4Poly;
extern tPolyParams G3Poly;
extern tPolyParams G2Poly;
extern tPolyParams G1Poly;
extern tPolyParams RWallPoly;
extern tPolyParams LWallPoly;
extern tPolyParams RightPoly;
extern tPolyParams LeftPoly;
extern tPolyParams RoadPoly;
extern int start_sect;
extern int gap_size;
extern int first_size;
extern int TrackSize;
extern int backwards;
extern int next_front;
extern int mid_sec;
extern int back_sec;
extern int front_sec;
extern int VisibleHumans;
extern int min_sub_size;
extern int NamesLeft;
extern int CarsLeft;
extern int VisibleCars;
extern int num_pols;
extern int small_poly;
extern int num_bits;

//-------------------------------------------------------------------------------------------------

int CalcVisibleTrack(int iCarIdx, unsigned int uiViewMode);
void DrawTrack3(uint8 *pScrPtr, int iChaseCamIdx, int iCarIdx,
                const GameRenderCamera *camera,
                const GameRenderProjection *projection);
int facing_ok(float fX0, float fY0, float fZ0, float fX1, float fY1, float fZ1,
              float fX2, float fY2, float fZ2, float fX3, float fY3, float fZ3);
int Zcmp(const void *pTrackView1, const void *pTrackView2);
void set_starts(unsigned int uiType);

//-------------------------------------------------------------------------------------------------
#endif