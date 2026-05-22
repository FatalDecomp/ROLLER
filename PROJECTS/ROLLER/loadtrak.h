#ifndef _ROLLER_LOADTRAK_H
#define _ROLLER_LOADTRAK_H
//-------------------------------------------------------------------------------------------------
#include "types.h"
#include <stdio.h>
//-------------------------------------------------------------------------------------------------

typedef struct
{
  int16 nForwardExtraStart;
  uint8 byForwardMainChunks;
  uint8 byForwardExtraChunks;
  int16 nBackwardExtraStart;
  uint8 byBackwardMainChunks;
  uint8 byBackwardExtraChunks;
} tTrakView;

//-------------------------------------------------------------------------------------------------

typedef struct 
{
  float fLShoulderWidth;
  float fLShoulderHeight;
  float fRShoulderWidth;
  float fRShoulderHeight;
  int iLeftBankAngle;
  int iRightBankAngle;
  int iLeftSurfaceType;
  int iRightSurfaceType;
  float fRoofHeight;
} tTrackInfo;

//-------------------------------------------------------------------------------------------------

typedef struct 
{
  char subdivides[11];
} tSubdivide;

//-------------------------------------------------------------------------------------------------

typedef struct
{
  float fBaseGrip;
  int iGripModifier;
  float fGripMultiplier;
  int iSecondaryGrip;
} tSurface;

//-------------------------------------------------------------------------------------------------

extern tSurface surface[14];
extern uint8 TrackSelect;
extern char *delims;
extern char *names[25];
extern tTrakView TrakView[MAX_TRACK_CHUNKS];
extern int16 samplespeed[MAX_SAMPLES];
extern int16 samplemax[MAX_SAMPLES];
extern float GroundLevel[MAX_TRACK_CHUNKS];
extern tTrackInfo TrackInfo[MAX_TRACK_CHUNKS];
extern int cur_mapsect;
extern float cur_TrackZ;
extern float cur_mapsize;
extern int TRAK_LEN;
extern int16 samplemin[MAX_SAMPLES];
extern int cur_laps[6];
extern uint8 fp_buf[512];
extern int actualtrack;
extern uint8 *start_f;
extern int TrackFlags;
extern int meof;
extern tSubdivide Subdivide[MAX_TRACK_CHUNKS];

//-------------------------------------------------------------------------------------------------

void loadtrack(int iTrackIdx, int iPreviewMode);
void read_backs(uint8 **ppTrackData);
void read_texturemap(uint8 **ppTrackData);
void read_bldmap(uint8 **ppTrackData);
void readstuntdata(uint8 **pTrackData);
void activatestunts();
void ReadAnimData(FILE *pFile, uint8 **ppFileData);
void readline(FILE *pFile, const char *szFmt, ...);
uint8 *memgets(uint8 *pDst, uint8 **ppSrc);
void readline2(uint8 **ppFileData, const char *pszFormat, ...);
void rotatepoint(double dX, double dY, double dZ, double dYaw, double dPitch, double dRoll, double *pdOutX, double *pdOutY, double *pdOutZ);
void setpoint(int iChunkIdx, int iPointIdx, double dX, double dY, double dZ);
void setgpoint(int iChunkIdx, int iPointIdx, double dX, double dY, double dZ);
void min_skip_stuff(uint8 **ppFileData);

//-------------------------------------------------------------------------------------------------
#endif