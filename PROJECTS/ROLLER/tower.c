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
  if (iTowerIdx != NearTow && TowerBase[iTowerIdx].iEnabled > -1) {
    GameRenderVertex verts[4];
    float tx = TowerX[iTowerIdx];
    float ty = TowerY[iTowerIdx];
    float tz = TowerZ[iTowerIdx];
    /* Small horizontal quad at the tower's world position.
     * 100-unit half-size approximates the original ~6 px marker. */
    verts[0].x = tx + 100.0f;  verts[0].y = ty + 100.0f;  verts[0].z = tz;
    verts[1].x = tx - 100.0f;  verts[1].y = ty + 100.0f;  verts[1].z = tz;
    verts[2].x = tx - 100.0f;  verts[2].y = ty - 100.0f;  verts[2].z = tz;
    verts[3].x = tx + 100.0f;  verts[3].y = ty - 100.0f;  verts[3].z = tz;
    verts[0].u = verts[0].v = 0.0f;
    verts[1].u = verts[1].v = 0.0f;
    verts[2].u = verts[2].v = 0.0f;
    verts[3].u = verts[3].v = 0.0f;
    game_render_quad_world(g_pGameRenderer, verts,
        TEXTURE_HANDLE_INVALID,
        SURFACE_FLAG_FLIP_BACKFACE | 0xE7);
  }
}

//-------------------------------------------------------------------------------------------------