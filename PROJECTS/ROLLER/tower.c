#include "tower.h"
#include "loadtrak.h"
#include "3d.h"
#include "view.h"
#include "transfrm.h"
#include <string.h>
#include <math.h>
//-------------------------------------------------------------------------------------------------

int TowerSect[MAX_TRACK_CHUNKS]; //001A1FA0
float TowerX[32];         //001A2770
float TowerY[32];         //001A27F0
float TowerZ[32];         //001A2870
tTowerBase TowerBase[32]; //001A28F0
tPolyParams TowerPol;     //001A2B70
int NumTowers;            //001A2B9C

//-------------------------------------------------------------------------------------------------
//00075700
void InitTowers()
{
  int iTowerIndex; // esi
  int iTowerBaseIndex; // edx
  int iTowerArrayIndex; // ebx
  int iTrackSegmentIndex; // edi
  int iNextSegmentIndex; // ecx
  tData *pCurrentTrackData; // eax
  tData *pNextTrackData; // ebp
  int iTotalTowers; // ecx
  float fNegTrackX; // [esp+4h] [ebp-34h]
  float fNegTrackY; // [esp+8h] [ebp-30h]
  float fTrackDistance; // [esp+Ch] [ebp-2Ch]
  float fTowerOffset; // [esp+10h] [ebp-28h]
  float fTrackDeltaX; // [esp+14h] [ebp-24h]
  float fTrackDeltaY; // [esp+18h] [ebp-20h]
  float fTowerHeight; // [esp+1Ch] [ebp-1Ch]

  memset(TowerSect, 255, sizeof(TowerSect));    // Initialize tower sector array to -1 (no towers)
  iTowerIndex = 0;
  if (NumTowers > 0)                          // Process each tower if any towers exist
  {
    iTowerBaseIndex = 0;
    iTowerArrayIndex = 0;
    do {
      iTrackSegmentIndex = TowerBase[iTowerBaseIndex].iChunkIdx;// Get track segment index for this tower
      iNextSegmentIndex = iTrackSegmentIndex + 1;// Calculate next track segment with wraparound
      fTowerOffset = (float)TowerBase[iTowerBaseIndex].iHOffset;// Convert tower offset to float
      if (iTrackSegmentIndex + 1 >= TRAK_LEN)
        iNextSegmentIndex -= TRAK_LEN;
      pCurrentTrackData = &localdata[iTrackSegmentIndex];// Get track data for current and next segments
      fNegTrackY = -pCurrentTrackData->pointAy[3].fY;
      pNextTrackData = &localdata[iNextSegmentIndex];
      fTowerHeight = (float)((double)(32 * TowerBase[iTowerBaseIndex].iVOffset) - (double)pCurrentTrackData->pointAy[3].fZ);// Calculate tower height from track Z and tower height parameter
      fTrackDeltaX = pCurrentTrackData->pointAy[3].fX - pNextTrackData->pointAy[3].fX;// Calculate track direction vector between segments
      fTrackDeltaY = pCurrentTrackData->pointAy[3].fY - pNextTrackData->pointAy[3].fY;
      fTrackDistance = (float)sqrt(fTrackDeltaX * fTrackDeltaX + fTrackDeltaY * fTrackDeltaY);// Calculate distance between track segments
      if ( fabs(fTrackDeltaX) > 0.0f )
      //if ((LODWORD(fTrackDeltaX) & 0x7FFFFFFF) != 0)// Normalize direction vector components
        fTrackDeltaX = fTrackDeltaX / fTrackDistance;
      if (fabs(pCurrentTrackData->pointAy[3].fY - pNextTrackData->pointAy[3].fY))
        fTrackDeltaY = fTrackDeltaY / fTrackDistance;
      fNegTrackX = -pCurrentTrackData->pointAy[3].fX;
      TowerX[iTowerArrayIndex] = fNegTrackX - fTowerOffset * fTrackDeltaY * 32.0f;// Calculate tower X position with perpendicular offset
      iTotalTowers = NumTowers;
      
      //loop offset fix
      TowerZ[iTowerArrayIndex] = fTowerHeight;
      TowerY[iTowerArrayIndex] = fTowerOffset * fTrackDeltaX * 32.0f + fNegTrackY;

      ++iTowerBaseIndex;
      ++iTowerArrayIndex;
      TowerSect[iTrackSegmentIndex] = iTowerIndex++;// Store tower index in track segment lookup table
      //TowerY[iTowerArrayIndex + 31] = fTowerHeight;// offset into TowerZ
      //TowerX[iTowerArrayIndex + 31] = fTowerOffset * fTrackDeltaX * 32.0 + fNegTrackY;// offset into TowerY
    } while (iTowerIndex < iTotalTowers);
  }
  TowerPol.uiNumVerts = 4;                      // Set tower polygon to 4 vertices (rectangular)
}

//-------------------------------------------------------------------------------------------------
//00075850
void DrawTower(int iTowerIdx, uint8 *pScrBuf)
{                                               
  double TowerMinusViewX; // st7
  double TowerMinusViewY; // st6
  double TowerMinusViewZ; // st5
  double dTransformed3DZ; // st7
  double dViewDistance; // st7
  double dZInverse; // st6
  double dScreenX; // st5
  double dScreenY; // st7
  int iScreenSize; // ebp
  int iPixelOffsetX; // ecx
  int iPixelX; // ecx
  int iPixelY; // ebx
  float fTransformed3DY; // [esp+20h] [ebp-20h]
  float fTransformed3DX; // [esp+24h] [ebp-1Ch]
  float fOriginalZ; // [esp+28h] [ebp-18h]
  float fClampedZ; // [esp+2Ch] [ebp-14h]

  if (iTowerIdx != NearTow && TowerBase[iTowerIdx].iEnabled > -1) {// Skip if tower is nearby or disabled
    TowerMinusViewX = TowerX[iTowerIdx] - viewx;// Calculate tower position relative to camera view
    TowerMinusViewY = TowerY[iTowerIdx] - viewy;
    TowerMinusViewZ = TowerZ[iTowerIdx] - viewz;
    fTransformed3DX = (float)TowerMinusViewX * vk1 + (float)TowerMinusViewY * vk4 + (float)TowerMinusViewZ * vk7;// Transform 3D tower position using view matrix
    fTransformed3DY = (float)TowerMinusViewX * vk2 + (float)TowerMinusViewY * vk5 + (float)TowerMinusViewZ * vk8;
    dTransformed3DZ = (float)TowerMinusViewX * vk3 + (float)TowerMinusViewY * vk6 + (float)TowerMinusViewZ * vk9;
    fClampedZ = (float)dTransformed3DZ;
    fOriginalZ = fClampedZ;
    if (dTransformed3DZ < 80.0)               // Clamp Z distance to minimum for perspective division
      fClampedZ = 80.0;
    dViewDistance = (double)VIEWDIST;           // Project 3D coordinates to 2D screen space
    dZInverse = 1.0 / fClampedZ;
    dScreenX = dViewDistance * fTransformed3DX * dZInverse + (double)xbase;
    //_CHP();
    xp = (int)dScreenX;
    dScreenY = dZInverse * (dViewDistance * fTransformed3DY) + (double)ybase;
    iScreenSize = scr_size;
    iPixelOffsetX = scr_size * (int)dScreenX;   // Calculate pixel buffer coordinates with bit shift scaling
    //_CHP();
    yp = (int)dScreenY;
    iPixelX = iPixelOffsetX >> 6;
    iPixelY = (iScreenSize * (199 - (int)dScreenY)) >> 6;// Complex visibility test for different distance ranges
    if (fOriginalZ >= 5000.0 && xp >= -50 && xp < 370 && yp >= -50 && yp < 250
      || fOriginalZ > -1000.0 && fOriginalZ < 5000.0 && (fTransformed3DX > -1000.0 && fTransformed3DX < 1000.0 || xp > -200 && xp < 520)) {
      TowerPol.vertices[0].x = iPixelX + 3;     // Create 6x6 pixel rectangle for tower visualization
      TowerPol.vertices[1].x = iPixelX - 3;
      TowerPol.vertices[2].x = iPixelX - 3;
      TowerPol.vertices[0].y = iPixelY - 3;
      TowerPol.vertices[1].y = iPixelY - 3;
      TowerPol.vertices[2].y = iPixelY + 3;
      TowerPol.vertices[3].y = iPixelY + 3;
      TowerPol.vertices[3].x = iPixelX + 3;
      TowerPol.iSurfaceType = SURFACE_FLAG_FLIP_BACKFACE | 0xE7; //0x20E7;          // Set tower polygon properties and draw to screen buffer
      TowerPol.uiNumVerts = 4;
      game_render_quad(g_pGameRenderer, &TowerPol, NULL, 0, 0, NULL);
    }
  }
}

//-------------------------------------------------------------------------------------------------