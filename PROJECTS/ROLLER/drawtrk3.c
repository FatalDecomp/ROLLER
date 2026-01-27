#include "drawtrk3.h"
#include "polytex.h"
#include "graphics.h"
#include "car.h"
#include "loadtrak.h"
#include "horizon.h"
#include "moving.h"
#include "transfrm.h"
#include "building.h"
#include "tower.h"
#include "roller.h"
#include <math.h>
#include <stdlib.h>
#include <assert.h>
//-------------------------------------------------------------------------------------------------

int showsub = 0;    //000A34A0
int cube_faces[6][4] =  //000A4158
{
  { 0, 1, 2, 3 },
  { 4, 5, 6, 7 },
  { 3, 2, 6, 7 },
  { 0, 3, 7, 4 },
  { 1, 2, 6, 5 },
  { 0, 1, 5, 4 }
};
int view_limit = 0; //000A41B8
int divtype = 0;    //000A41BC
int NextSect[MAX_TRACK_CHUNKS];  //00143BF4
tScreenPt LightXYZ[8];  //001443C4
int tex_hgt;        //00144464
int polyysize;      //00144468
int polyxsize;      //0014446C
uint8 *subptr;      //00144474
int fliptype;       //00144478
int subpolytype;    //0014447C
tPolyParams *subpoly; //00144480
int tex_wid;        //00144484
int flatpol;        //00144488
tPolyParams RoofPoly; //0014448C
tPolyParams G5Poly;   //001444B8
tPolyParams G4Poly;   //001444E4
tPolyParams G3Poly;   //00144510
tPolyParams G2Poly;   //0014453C
tPolyParams G1Poly;   //00144568
tPolyParams RWallPoly;//00144594
tPolyParams LWallPoly;//001445C0
tPolyParams RightPoly;//001445EC
tPolyParams LeftPoly; //00144618
tPolyParams RoadPoly; //00144644
int start_sect;     //00144670
int gap_size;       //00144674
int first_size;     //00144678
int TrackSize;      //0014467C
int backwards;      //00144684
int next_front;     //00144684
int mid_sec;        //00144688
int back_sec;       //0014468C
int front_sec;      //00144690
int VisibleHumans;  //00144694
int min_sub_size;   //00144698
int NamesLeft;      //0014469C
int CarsLeft;       //001446A0
int VisibleCars;    //001446A4
int num_pols;       //001446A8
int small_poly;     //001446AC
int num_bits;       //001446B0

//-------------------------------------------------------------------------------------------------
//0001D740
int CalcVisibleTrack(int iCarIdx, unsigned int uiViewMode)
{
  int iCurrChunk; // esi
  int iSearchRadius; // edi
  int iDefaultOffset; // ebp
  int iSearchIdx; // ebx
  int iChunkIdx2; // edx
  tData *pDataAy; // eax
  double dDeltaX; // st4
  double dDeltaXSquared; // st7
  double dDeltaY; // st4
  double dDeltaZ; // st6
  double dLengthSquared; // st7
  int iNextChunk; // eax
  tData *pNextChunkDataAy; // edx
  tData *pCurrChunkDataAy; // eax
  double fDeltaX; // st7
  double fDeltaY; // st6
  uint8 byExtraChunks; // al
  int iFrontWithOffset; // esi
  int result; // eax
  float fViewAlignment; // [esp+20h] [ebp-38h]
  int iHasExtraView; // [esp+28h] [ebp-30h]
  int iViewOffset; // [esp+2Ch] [ebp-2Ch]
  int iExtraViewStart; // [esp+30h] [ebp-28h]
  int iChunkIdx; // [esp+34h] [ebp-24h]
  float fMinDistSq; // [esp+38h] [ebp-20h]
  float fLengthSquared; // [esp+3Ch] [ebp-1Ch]

  // Init tex coords
  set_starts(0);

  // Init rendering params
  TrackSize = -1;                               // number of track chunks to render
  iExtraViewStart = -1;                         // start of extra view range (for tunnels?)
  iCurrChunk = Car[iCarIdx].nCurrChunk;         // current track chunk the car is on
  iHasExtraView = 0;

  // Set starting chunk idx based on whether the car is on track
  if (iCurrChunk == -1)
    iChunkIdx = Car[iCarIdx].iLastValidChunk;   // use last valid chunk if off-track
  else
    iChunkIdx = Car[iCarIdx].nCurrChunk;
  alltrackflag = 0;                             // flag for rendering entire track

  // Set view params based on view mode
  switch (uiViewMode) {
    case 2u:                                    // near view (chase cam close)
      iSearchRadius = 24;
      iDefaultOffset = -4;
      iCurrChunk = -1;                          // force search for nearest chunk
      goto LABEL_10;
    case 3u:                                    // far view (chase cam far)
      iSearchRadius = 96;
      iViewOffset = -8;
      iCurrChunk = -1;                          // force search for nearest chunk
      break;
    case 4u:                                    // in-car view
      iSearchRadius = 32;
      iViewOffset = -16;
      break;
    case 6u:                                    // helicopter/top-down view
      iSearchRadius = 96;
      iCurrChunk = -1;                          // force search for nearest chunk
      iViewOffset = -4;
      break;
    default:                                    // default view
      iDefaultOffset = -1;
      iSearchRadius = 4;
    LABEL_10:
      iViewOffset = iDefaultOffset;
      break;
  }

  // Calculate view alignment with track direction
  if (iCurrChunk >= 0) {
    // use car's yaw angle to determine view alignment
    fViewAlignment = tcos[Car[iCarIdx].nYaw];
  } else {
    // Search for nearest track chunk to camera pos
    iSearchIdx = -iSearchRadius;
    for (fMinDistSq = 9.9999998e17f; iSearchIdx <= iSearchRadius; ++iSearchIdx) {
      // Wrap chunk idx around track
      iChunkIdx2 = iSearchIdx + iChunkIdx;
      if (iSearchIdx + iChunkIdx < 0)
        iChunkIdx2 += TRAK_LEN;
      if (iChunkIdx2 >= TRAK_LEN)
        iChunkIdx2 -= TRAK_LEN;

      // Calculate distance from camera to chunk center point
      pDataAy = &localdata[iChunkIdx2];
      dDeltaX = -pDataAy->pointAy[3].fX - viewx;// point 3 is center of chunk
      dDeltaXSquared = dDeltaX * dDeltaX;
      dDeltaY = -pDataAy->pointAy[3].fY - viewy;
      dDeltaZ = -pDataAy->pointAy[3].fZ - viewz;
      dLengthSquared = dDeltaXSquared + dDeltaY * dDeltaY + dDeltaZ * dDeltaZ;

      // Track closest chunk
      if (dLengthSquared < fMinDistSq) {
        iCurrChunk = iChunkIdx2;
        fLengthSquared = (float)dLengthSquared;
        fMinDistSq = fLengthSquared;
      }
    }

    // Calculate view alignment based on track direction at nearest chunk
    iNextChunk = iCurrChunk + 1;
    if (iCurrChunk + 1 >= TRAK_LEN)
      iNextChunk -= TRAK_LEN;

    pNextChunkDataAy = &localdata[iNextChunk];
    pCurrChunkDataAy = &localdata[iCurrChunk];

    // Vector from curr to next chunk
    fDeltaX = pCurrChunkDataAy->pointAy[3].fX - pNextChunkDataAy->pointAy[3].fX;
    fDeltaY = pCurrChunkDataAy->pointAy[3].fY - pNextChunkDataAy->pointAy[3].fY;

    // Calculate dot product of track dir with world view dir (normalized)
    fViewAlignment = (float)((fDeltaX * tcos[worlddirn] + fDeltaY * tsin[worlddirn])
      / (sqrt(tcos[worlddirn] * tcos[worlddirn] + tsin[worlddirn] * tsin[worlddirn])
       + sqrt(fDeltaY * fDeltaY + fDeltaX * fDeltaX)));
  }

  // Check if view is perpendicular to track (inside corner or similar)
  // This triggers special rendering for better visibility
  if (fViewAlignment < 0.3
    && fViewAlignment >= -0.3
    && ((TrakColour[iCurrChunk][TRAK_COLOUR_LEFT_LANE] & SURFACE_FLAG_SKIP_RENDER) == 0// SURFACE_FLAG_SKIP_RENDER
        || (TrakColour[iCurrChunk][TRAK_COLOUR_CENTER] & SURFACE_FLAG_SKIP_RENDER) == 0
        || (TrakColour[iCurrChunk][TRAK_COLOUR_RIGHT_LANE] & SURFACE_FLAG_SKIP_RENDER) == 0)) {
       // Extend view range when looking perpendicular to track
    if (uiViewMode >= 3 && (uiViewMode <= 3 || uiViewMode == 6)) {
      TrackSize = 48;                           // render 48 chunks
      iViewOffset = -24;                        // center view 24 chunks back
    } else {
      TrackSize = 24;                           // render 24 chunks
      iViewOffset = -12;                        // center view 12 chunks back
    }
  }

  test_y1 = iCurrChunk;                         // debug variable
  backwards = (fViewAlignment >= 0.0) - 1;


  //added by ROLLER to force maximum draw distance
  //if (g_bForceMaxDraw) {
  //  byForwardMainChunks = 255;
  //  nForwardExtraStart = -1;
  //  byForwardExtraChunks = 0;
  //  byBackwardMainChunks = 255;
  //  nBackwardExtraStart = -1;
  //  byBackwardExtraChunks = 0;
  //}

  // Load pre-calculated view ranges from track data
  if (iCurrChunk >= 0 && TrackSize < 0) {
    if (backwards)                            // looking backward along track
    {
      if (g_bForceMaxDraw) { //added by ROLLER for max draw distance
        TrackSize = 255 - iViewOffset;
        iExtraViewStart = -1;
        byExtraChunks = 0;
      } else {
        TrackSize = TrakView[iCurrChunk].byBackwardMainChunks - iViewOffset;
        iExtraViewStart = TrakView[iCurrChunk].nBackwardExtraStart;
        byExtraChunks = TrakView[iCurrChunk].byBackwardExtraChunks;
      }
    } else                                        // looking forward along track
    {
      if (g_bForceMaxDraw) {
        TrackSize = 255 - iViewOffset;
        iExtraViewStart = -1;
        byExtraChunks = 0;
      } else {
        TrackSize = TrakView[iCurrChunk].byForwardMainChunks - iViewOffset;
        iExtraViewStart = TrakView[iCurrChunk].nForwardExtraStart;
        byExtraChunks = TrakView[iCurrChunk].byForwardExtraChunks;
      }
    }
    iHasExtraView = byExtraChunks;
  }

  // Apply view distance limits for certain game modes
  if ((view_limit || player_type == 2) && replaytype != 2 && !winner_mode && !g_bForceMaxDraw) {
    iHasExtraView = 0;
    iExtraViewStart = -1;
    if (player_type == 2) {
      if (TrackSize > 28)
        TrackSize = 28;
    } else if (TrackSize > view_limit) {
      TrackSize = view_limit;                   // user-defined limit?
    }
  }

  // Handle mirror view or special view mode
  if (mirror || uiViewMode == 1)
    backwards = backwards == 0;                 // flip view direction

  // Calculate track section ranges for rendering
  if (backwards) {
    // Calculate front section with offset
    iFrontWithOffset = iCurrChunk - iViewOffset;
    front_sec = iFrontWithOffset;
    if (iFrontWithOffset >= TRAK_LEN)
      front_sec = iFrontWithOffset - TRAK_LEN;

    // Handle extra view range
    if (iHasExtraView > 0 && iExtraViewStart >= 0) {
      // Calculate mid section
      mid_sec = front_sec - TrackSize;
      if (front_sec - TrackSize < 0)
        mid_sec = front_sec - TrackSize + TRAK_LEN;

      // Calculate back section
      back_sec = iExtraViewStart - iHasExtraView;

      // Check if extra view range overlaps with main view
      if (front_sec >= iExtraViewStart || back_sec - 1 > front_sec) {
        // No overlap, set up gap between main and extra view
        if (back_sec < 0)
          back_sec += TRAK_LEN;
        gap_size = mid_sec - back_sec;
        if (mid_sec - back_sec < 0)
          gap_size = mid_sec - back_sec + TRAK_LEN;
        next_front = iExtraViewStart;
        first_size = iHasExtraView;
      } else {
        // overlap detected, merge ranges
        front_sec = iExtraViewStart;
        back_sec = mid_sec;
        next_front = -1;
        test_y1 = -2;
        mid_sec = -1;
        gap_size = 6 * TRAK_LEN;                // large value to disable gap
      }

      // Recalculate total track size
      TrackSize = front_sec - back_sec;
      if (front_sec - back_sec < 0)
        TrackSize = front_sec - back_sec + TRAK_LEN;
      if (mid_sec < 0)
        first_size = TrackSize;
    } else {
      // No extra view range, simple calculation
      back_sec = front_sec - TrackSize;
      if (front_sec - TrackSize < 0)
        back_sec = front_sec - TrackSize + TRAK_LEN;
      mid_sec = -1;
      first_size = TrackSize;
      next_front = -1;
      gap_size = 6 * TRAK_LEN;                  // large value to disable gap
    }
  } else                                          // looking forward
  {
    // Calculate front section with offset
    front_sec = iCurrChunk + iViewOffset;
    if (iCurrChunk + iViewOffset < 0)
      front_sec = iCurrChunk + iViewOffset + TRAK_LEN;

    // Handle extra view range
    if (iHasExtraView > 0 && iExtraViewStart >= 0) {
      first_size = TrackSize;
      mid_sec = TrackSize + front_sec;
      back_sec = iHasExtraView + iExtraViewStart;

      // Check if extra view range overlaps with main view
      if (front_sec <= iExtraViewStart || back_sec + 1 < front_sec) {
        // No overlap, set up gap
        if (mid_sec >= TRAK_LEN)
          mid_sec -= TRAK_LEN;
        gap_size = iExtraViewStart - front_sec;
        if (iExtraViewStart - front_sec < 0)
          gap_size = TRAK_LEN + iExtraViewStart - front_sec;
        next_front = iExtraViewStart;
      } else {
        // Overlap detected, merge ranges
        gap_size = 6 * TRAK_LEN;                // large value to disable gap
        next_front = -1;
        front_sec = iExtraViewStart;
        test_y1 = -1;
        back_sec = mid_sec;
        mid_sec = -1;
      }

      // Recalc total track size
      TrackSize = back_sec - front_sec;
      if (back_sec - front_sec < 0)
        TrackSize = back_sec - front_sec + TRAK_LEN;

      if (mid_sec < 0)
        first_size = TrackSize;

      if (back_sec >= TRAK_LEN)
        back_sec -= TRAK_LEN;
    } else {
      // No extra view range, simple calculation
      back_sec = TrackSize + front_sec;
      if (TrackSize + front_sec >= TRAK_LEN)
        back_sec = TrackSize + front_sec - TRAK_LEN;

      mid_sec = -1;
      first_size = TrackSize;
      next_front = -1;
      gap_size = 6 * TRAK_LEN;                  // large value to disable gap
    }
  }

  // Return to starting section based on view dir
  if (backwards)
    result = back_sec;
  else
    result = front_sec;

  // Store globally for rendering
  start_sect = result;
  return result;
}

//-------------------------------------------------------------------------------------------------
//0001DE40
void DrawTrack3(uint8 *pScrPtr, int iChaseCamIdx, int iCarIdx)
{
  tTrackScreenXYZ *pScreenCoord; // ebp
  tTrackScreenXYZ *pScreenCoord_1; // edi
  int iCurrentTrackIndex; // ecx
  tVec3 *pCurrentTrackPt; // eax
  tTrackScreenXYZ *pCurrentTrackScreenXYZ; // edx
  tVec3 *pTrackPoint4; // esi
  tVec3 *pTrackPoint3; // ebx
  double dDeltaX1; // st7
  double dDeltaY1; // st6
  double dDeltaZ1; // st5
  double dCameraZ1; // st7
  double dViewDistance1; // st7
  double dInvZ1; // st6
  double dScreenX1; // st5
  double dScreenY1; // st7
  //int iScreenX1; // eax
  int iScreenY1; // eax
  double dDeltaX2; // st7
  double dDeltaY2; // st6
  double dDeltaZ2; // st5
  double dCameraZ2; // st7
  double dViewDistance2; // st7
  double dInvZ2; // st6
  double dScreenX2; // st5
  double dScreenY2; // st7
  ///int iScreenX2; // eax
  int iScreenY2; // eax
  double dDeltaX3; // st7
  double dDeltaY3; // st6
  double dDeltaZ3; // st5
  double dCameraZ3; // st7
  int iClipIncrement3; // eax
  double dViewDistance3; // st7
  double dInvZ3; // st6
  double dScreenX3; // st5
  double dScreenY3; // st7
  //int iScreenX3; // eax
  int iScreenY3; // eax
  double dDeltaX4; // st7
  double dDeltaY4; // st6
  double dDeltaZ4; // st5
  double dCameraZ4; // st7
  int iClipIncrement4; // eax
  double dViewDistance4; // st7
  double dInvZ4; // st6
  double dScreenX4; // st5
  double dScreenY4; // st7
  int iScrSize; // ebx
  //int iScreenX4; // eax
  int *pLeftWallTypePtr; // esi
  int iPrevSectionIndex; // eax
  int *pPrevLeftWallTypePtr; // ebx
  tGroundPt *pGroundPt; // eax
  double dLeftWallDeltaX; // st7
  double dLeftWallDeltaY; // st6
  double dLeftWallDeltaZ; // st5
  double dLeftWallCameraZ; // st7
  double dLeftWallViewDist; // st7
  double dLeftWallInvZ; // st6
  double dLeftWallScreenX; // st5
  double dLeftWallScreenY; // st7
  //int iLeftWallScreenX; // eax
  int iLeftWallScreenY; // eax
  double dLeftWallCopyZ; // st7
  int iLeftWallCopyY; // eax
  tGroundPt *pGroundPt2; // eax
  double dRightWallDeltaX; // st7
  double dRightWallDeltaY; // st6
  double dRightWallDeltaZ; // st5
  double dRightWallCameraZ; // st7
  double dRightWallViewDist; // st7
  double dRightWallInvZ; // st6
  double dRightWallScreenX; // st5
  double dRightWallScreenY; // st7
  int iRightWallScrSize; // ebx
  //int iRightWallScreenX; // eax
  int iRightWallScreenY; // eax
  double dRightWallDepthCopy; // st7
  int iRightWallCopy; // eax
  tTrackScreenXYZ *pGroundScreenXYZ; // eax
  tScreenPt *pScreenPoint; // esi
  tGroundPt *pCurrentGroundPt; // edx
  int iPrevGroundIndex; // eax
  int iGroundPointIndex; // ebx
  double dGroundDeltaX; // st7
  double dGroundDeltaY; // st6
  float *pGroundPointZ; // edx
  double dGroundDeltaZ; // st5
  double dGroundCameraZ; // st7
  double dGroundViewDist; // st7
  double dGroundInvZ; // st6
  double dGroundScreenX; // st5
  double dGroundScreenY; // st7
  //int iGroundScreenX; // eax
  int iGroundScreenY; // eax
  //int iGroundSectionOffset; // eax
  float fGroundProjectedZ; // ecx
  //int iGroundSectionOffset2; // eax
  int iTrackLoopCounter; // ecx
  int iCurrentSect; // esi
  //int iSectionOffset; // edx
  int iOFloorType; // edx
  bool bFloorVisible; // eax
  int iCurrentFloorType; // ebx
  float fGroundDepthMax1; // eax
  float fGroundDepthMax2; // eax
  float fGroundDepthSelected; // eax
  int iTrackIndexPlus2; // eax
  tTrackScreenXYZ *pTrackScreenPlus2; // edx
  float fTrackDepthChoice1; // eax
  float fTrackDepthChoice2; // eax
  float fTrackDepthFinal; // eax
  tTrackZOrderEntry *pGroundRenderCmd; // eax
  float fGroundRenderDepth; // edx
  int iGroundRenderIndex; // ebx
  float fRoadCenterDepthMax1; // eax
  float fRoadCenterDepthMax2; // eax
  float fRoadCenterDepthSelected; // eax
  tTrackZOrderEntry *pRoadCenterCmd; // eax
  float fRoadCenterCmdDepth; // edx
  int iRoadCenterCmdIndex; // ebx
  float fLeftRoadDepthMax1; // eax
  float fLeftRoadDepthMax2; // eax
  float fLeftRoadDepthSelected; // eax
  tTrackZOrderEntry *pLeftRoadCmd; // eax
  float fLeftRoadCmdDepth; // edx
  int iLeftRoadCmdIndex; // ebx
  float fRightRoadDepthMax1; // eax
  float fRightRoadDepthMax2; // eax
  float fRightRoadDepthSelected; // eax
  tTrackZOrderEntry *pRightRoadCmd; // eax
  float fRightRoadCmdDepth; // edx
  int iRightRoadCmdIndex; // ebx
  int iRoofTypeCheck; // eax
  float fRoof1OuterDepth; // eax
  float fRoof1InnerDepth; // eax
  float fRoof1SelectedDepth; // eax
  tTrackZOrderEntry *pRoof1RenderCmd; // eax
  float fRoof1CmdDepth; // edx
  int iRoofType; // ebx
  double dRoof2WallDepth1; // st7
  double dRoof2WallDepth2; // st7
  float fRoof2WallMinDepth; // eax
  float fRoof2SelectedDepth; // eax
  tTrackZOrderEntry *pRoof2RenderCmd; // eax
  float fRoof2CmdDepth; // edx
  float fRoof3OuterDepth; // eax
  float fRoof3InnerDepth; // eax
  float fRoof3SelectedDepth; // eax
  int iRoof3CmdIndex; // ebx
  float fLeftLowerWallDepth1; // eax
  float fLeftLowerWallDepth2; // eax
  float fLeftLowerWallSelected; // eax
  tTrackZOrderEntry *pLeftLowerWallCmd; // eax
  float fLeftLowerWallCmdDepth; // edx
  float fRightLowerWallDepth1; // eax
  float fRightLowerWallDepth2; // eax
  float fRightLowerWallSelected; // eax
  tTrackZOrderEntry *pRightLowerWallCmd; // eax
  float fRightLowerWallCmdDepth; // edx
  float fLeftWallDepthMax1; // eax
  float fLeftWallDepthMax2; // eax
  float fLeftWallDepthSelected; // eax
  tTrackZOrderEntry *pLeftWallCmd; // eax
  int iLeftWallCmdIndex; // edx
  float fRightWallDepthMax1; // eax
  float fRightWallDepthMax2; // eax
  float fRightWallDepthSelected; // eax
  tTrackZOrderEntry *pRightWallCmd; // eax
  float fRightWallCmdDepth; // edx
  float fRightWallBasicDepth1; // eax
  float fRightWallBasicDepth2; // eax
  float fRightWallBasicSelected; // eax
  tTrackZOrderEntry *pRightWallBasicCmd; // eax
  float fRightWallBasicCmdDepth; // edx
  int iRightWallBasicCmdIndex; // ebx
  float fRightWallRoofDepth1; // eax
  float fRightWallRoofDepth2; // eax
  float fRightWallRoofSelected; // eax
  tTrackZOrderEntry *pRightWallRoofCmd; // eax
  int iRightWallRoofCmdIndex; // edx
  int iCarIndex; // esi
  int iCarArrayIndex; // ebx
  tTrackZOrderEntry *pCarRenderCmd; // eax
  int iCarDrawOrderStatus; // edx
  float iCarDrawOrderIndex; // edx
  int iCarProcessingFlag; // ecx
  int iCarsRenderedCount; // esi
  int iCarLoopIndex; // ebx
  unsigned int uiCarIndexOffset; // edx
  int iCarStatusFlag; // ecx
  int iCarVisibilityCount; // esi
  int iNamesDisplayCount; // eax
  tVisibleBuilding *pVisibleBuildingsPtr; // ebx
  tTrackZOrderEntry *pBuildingRenderCmd; // edx
  int iBuildingCmdIndex; // ecx
  //int iBuildingValue; // eax
  int iBuildingNext; // esi
  int iLightIndex; // ebx
  tTrackZOrderEntry *pLightRenderCmd; // ecx
  int iLightArrayOffset; // edx
  float fLightDepth; // eax
  int iLightCmdIndex; // esi
  tTrackZOrderEntry *pRenderCommand; // eax
  int iSectionNum; // esi
  int iSectionCommand; // eax
  float iSectionTypeIndex; // eax
  float fSurfaceDepth1; // eax
  float fSurfaceDepth2; // eax
  float fSurfaceDepth3; // eax
  float fSurfaceDepth4; // eax
  float fSurfaceDepth5; // eax
  int iCenterSurfType; // eax
  float fObjectDepth1; // eax
  float fObjectDepth2; // eax
  float fObjectDepth3; // eax
  float fObjectDepth4; // eax
  float fObjectDepth5; // eax
  float fObjectDepth6; // eax
  int iLeftSurfType; // eax
  //int iObjectCommandType; // edx
  float fMiddleDepth1; // eax
  float fMiddleDepth2; // eax
  float fMiddleDepth3; // eax
  float fMiddleDepth4; // eax
  float fMiddleDepth5; // eax
  float fMiddleDepth6; // eax
  int iRightSurfType; // eax
  //int iMiddleCommandType; // edx
  float fRightDepth1; // eax
  float fRightDepth2; // eax
  float fRightDepth3; // eax
  float fRightDepth4; // eax
  float fRightDepth5; // eax
  float fRightDepth6; // eax
  int iLeftWallType; // eax
  //char byLeftWallFlag; // dl
  float fRoof1InnerDepthAlt; // eax
  float fWallDepthZ; // eax
  float fWallZDepthAlt; // eax
  float fWallInnerDepth; // eax
  float fWallDepthZ_1; // eax
  float fWallLeftDepth1; // eax
  float fWallLeftDepth2; // eax
  float fWallLeftDepth3; // eax
  float fWallLeftDepth4; // eax
  float fWallLeftDepth5; // eax
  float fWallLeftDepth6; // eax
  float fWallLeftDepth7; // eax
  int iRightWallType; // eax
  //char byRightWallFlag; // bl
  float fRightWallGeomDepth1; // eax
  float fRightWallGeomDepth2; // eax
  float fRightWallGeomDepth3; // eax
  float fRightWallGeomDepth4; // eax
  float fRightWallGeomDepth5; // eax
  float fRightWallGeomDepth6; // eax
  //char byWallTypeFlag; // bh
  float fGeometryDepth1; // eax
  float fGeometryDepth2; // eax
  float fGeometryDepth3; // eax
  float fGeometryDepth4; // eax
  float fGeometryDepth5; // eax
  float fGeometryDepth6; // eax
  int iGeometryIndex; // eax
  int iProcessingIndex; // edx
  float fComputedDepth1; // eax
  float fRenderValue1; // eax
  float fRenderValue2; // eax
  float fRenderValue3; // eax
  float fRenderValue4; // eax
  float fRenderValue5; // eax
  int iRenderCommandIndex; // ebx
  int iScreenYCoord; // eax
  float fTrackDepth1; // eax
  float fTrackDepth2; // eax
  float fTrackDepth3; // eax
  float fTrackDepth4; // eax
  float fTrackDepth5; // eax
  float fTrackDepth6; // eax
  float fTrackDepth7; // eax
  float fTrackDepth8; // eax
  float fTrackDepth9; // eax
  float fTrackDepth10; // eax
  float fTrackDepth11; // eax
  float fTrackDepth12; // eax
  float fTrackDepth13; // eax
  float fTrackDepth14; // eax
  float fTrackDepth15; // eax
  float fTrackDepth16; // eax
  float fTrackDepth17; // eax
  float fTrackDepth18; // eax
  float fTrackDepth19; // eax
  float fTrackDepth20; // eax
  float fTrackDepth21; // eax
  tTrackScreenXYZ *pTrackScreenPtr1; // eax
  tTrackScreenXYZ *pTrackScreenPtr2; // eax
  tTrackScreenXYZ *pTrackScreenPtr3; // eax
  float fScreenDepth1; // eax
  tTrackScreenXYZ *pTrackScreenPtr4; // eax
  tTrackScreenXYZ *pTrackScreenPtr5; // eax
  tTrackScreenXYZ *pTrackScreenPtr6; // eax
  tTrackScreenXYZ *pTrackScreenPtr7; // eax
  float fScreenDepth2; // eax
  tTrackScreenXYZ *pTrackScreenPtr8; // eax
  tTrackScreenXYZ *pTrackScreenPtr9; // eax
  tTrackScreenXYZ *pTrackScreenPtr10; // eax
  tTrackScreenXYZ *pTrackScreenPtr11; // eax
  float fScreenDepth3; // eax
  tTrackScreenXYZ *pTrackScreenPtr12; // eax
  tTrackScreenXYZ *pTrackScreenPtr13; // eax
  tTrackScreenXYZ *pTrackScreenPtr14; // eax
  tTrackScreenXYZ *pTrackScreenPtr15; // eax
  float fScreenDepth4; // eax
  tTrackScreenXYZ *pTrackScreenPtr16; // eax
  tTrackScreenXYZ *pTrackScreenPtr17; // eax
  tTrackScreenXYZ *pTrackScreenPtr18; // eax
  tTrackScreenXYZ *pTrackScreenPtr19; // eax
  float fScreenDepth5; // eax
  tTrackScreenXYZ *pTrackScreenPtr20; // eax
  tTrackScreenXYZ *pTrackScreenPtr21; // eax
  tTrackScreenXYZ *pTrackScreenPtr22; // eax
  tTrackScreenXYZ *pTrackScreenPtr23; // eax
  float fScreenDepth6; // eax
  tTrackScreenXYZ *pTrackScreen1; // eax
  tTrackScreenXYZ *pTrackScreen2; // eax
  tTrackScreenXYZ *pTrackScreen3; // eax
  tTrackScreenXYZ *pTrackScreen4; // eax
  float fTrackScreenDepth7; // eax
  tTrackScreenXYZ *pTrackScreen5; // eax
  tTrackScreenXYZ *pTrackScreen6; // eax
  tTrackScreenXYZ *pTrackScreen7; // eax
  tTrackScreenXYZ *pTrackScreen8; // eax
  float fTrackScreenDepth8; // eax
  tTrackScreenXYZ *pTrackScreen9; // eax
  int iScreenIndex1; // edx
  int iScreenIndex2; // esi
  int iScreenIndex3; // edx
  double dTransform1; // st7
  double dTransform2; // st7
  double dTransform3; // st6
  double dTransform4; // st7
  double dTransform5; // st6
  double dTransform6; // st5
  double dTransform7; // st7
  double dTransform8; // st7
  double dTransform9; // st6
  double dTransform10; // st5
  double dTransform11; // st7
  double dTransform12; // st6
  double dTransform13; // st7
  double dTransform14; // st5
  float iTransformInt1; // eax
  double dTransform15; // st7
  //float iTransformInt2; // eax
  double dTransform16; // st7
  double dTransform17; // st6
  double dTransform18; // st5
  double dTransform19; // st7
  double dTransform20; // st6
  double dTransform21; // st7
  double dTransform22; // st5
  float iTransformInt3; // eax
  double dTransform23; // st7
  //float iTransformInt4; // eax
  double dTransform24; // st7
  double dTransform25; // st6
  double dTransform26; // st5
  double dTransform27; // st7
  double dTransform28; // st6
  double dTransform29; // st7
  double dTransform30; // st5
  float iTransformInt5; // eax
  double dTransform31; // st7
  //float iTransformInt6; // eax
  double dTransform32; // st7
  double dTransform33; // st6
  double dTransform34; // st5
  double dProjectionDepth1; // st7
  double dProjectionDepth2; // st6
  double dProjectionDepth3; // st7
  double dProjectionDepth4; // st5
  float iProjectionIndex1; // eax
  double dProjectionDepth5; // st7
  //float iProjectionIndex2; // eax
  double dProjectionDepth6; // st7
  double dProjectionDepth7; // st6
  double dProjectionDepth8; // st5
  double dProjectionDepth9; // st7
  double dProjectionDepth10; // st6
  double dProjectionDepth11; // st7
  double dProjectionDepth12; // st5
  float iProjectionIndex3; // eax
  double dProjectionDepth13; // st7
  //float iProjectionIndex4; // eax
  double dProjectionDepth14; // st7
  double dProjectionDepth15; // st6
  double dProjectionDepth16; // st5
  double dProjectionDepth17; // st7
  double dProjectionDepth18; // st6
  double dProjectionDepth19; // st7
  double dProjectionDepth20; // st5
  float iProjectionIndex5; // eax
  double dProjectionDepth21; // st7
  //float iProjectionIndex6; // eax
  double dProjectionDepth22; // st7
  double dProjectionDepth23; // st6
  double dProjectionDepth24; // st5
  double dProjectionDepth25; // st7
  double dProjectionDepth26; // st6
  double dProjectionDepth27; // st7
  double dProjectionDepth28; // st5
  float iProjectionIndex7; // eax
  double dProjectionDepth29; // st7
  //float iProjectionIndex8; // eax
  double dProjectionDepth30; // st7
  double dProjectionDepth31; // st6
  double dProjectionDepth32; // st5
  double dProjectionDepth33; // st7
  int iRenderingIndex1; // edx
  //int iRenderingIndex2; // eax
  int iRenderingIndex3; // edx
  int iRenderingIndex4; // eax
  double dRenderingDepth1; // st6
  int iRenderingIndex5; // esi
  int iRenderingIndex6; // edx
  int iRenderingIndex7; // ebx
  int iRenderingIndex8; // ebx
  int iRenderingIndex9; // eax
  int iRenderingIndex10; // edx
  int iRenderingIndex11; // ecx
  int iRenderLoopVar; // edx
  int iRenderingLoopIndex; // esi
  int iRenderingIndexTmp; // edx
  int iRenderingCoordIndex; // eax
  int iRenderingDataIndex; // edx
  float fDepthValuesArray[9]; // [esp+4h] [ebp-4F0h]
  float fRoadCenterDepth1; // [esp+28h] [ebp-4CCh]
  float fRoadCenterDepth2; // [esp+2Ch] [ebp-4C8h]
  float fRoadCenterFinalDepth; // [esp+30h] [ebp-4C4h]
  float fRoadCenterDepthNear; // [esp+34h] [ebp-4C0h]
  float fRoadCenterDepthFar; // [esp+38h] [ebp-4BCh]
  float fRightRoadDepth2; // [esp+3Ch] [ebp-4B8h]
  float fRightRoadFinalDepth; // [esp+40h] [ebp-4B4h]
  float fRightRoadDepthNear; // [esp+44h] [ebp-4B0h]
  float fRightRoadDepthFar; // [esp+48h] [ebp-4ACh]
  float fRoof1OuterDepthTmp; // [esp+4Ch] [ebp-4A8h]
  float fRoof1InnerDepthTmp; // [esp+50h] [ebp-4A4h]
  float fRoof1DepthSelected; // [esp+54h] [ebp-4A0h]
  float fRoof1DepthOuter; // [esp+58h] [ebp-49Ch]
  float fRoof1DepthInner; // [esp+5Ch] [ebp-498h]
  float fRoof2WallMinDepthTmp; // [esp+60h] [ebp-494h]
  int iRoof2WallDepthMin; // [esp+64h] [ebp-490h]
  float fRoof2DepthSelected; // [esp+68h] [ebp-48Ch]
  int iRoof2WallDepthChoice; // [esp+6Ch] [ebp-488h]
  float fRoof2DepthWall; // [esp+70h] [ebp-484h]
  float fRoof3OuterDepthTmp; // [esp+74h] [ebp-480h]
  float fRoof3InnerDepthTmp; // [esp+78h] [ebp-47Ch]
  float fRoof3DepthSelected; // [esp+7Ch] [ebp-478h]
  float fRoof3DepthOuter; // [esp+80h] [ebp-474h]
  float fRoof3DepthInner; // [esp+84h] [ebp-470h]
  float fLeftLowerWallDepthNear; // [esp+88h] [ebp-46Ch]
  float fLeftLowerWallDepthFar; // [esp+8Ch] [ebp-468h]
  float fLeftLowerWallDepthSelected; // [esp+90h] [ebp-464h]
  float fLeftLowerWallDepthTmp1; // [esp+94h] [ebp-460h]
  float fLeftLowerWallDepthTmp2; // [esp+98h] [ebp-45Ch]
  float fRightLowerWallDepthNear; // [esp+9Ch] [ebp-458h]
  float fRightLowerWallDepthFar; // [esp+A0h] [ebp-454h]
  float fRightLowerWallDepthSelected; // [esp+A4h] [ebp-450h]
  float fRightLowerWallDepthTmp1; // [esp+A8h] [ebp-44Ch]
  float fRightLowerWallDepthTmp2; // [esp+ACh] [ebp-448h]
  float fLeftWallDepth1; // [esp+B0h] [ebp-444h]
  float fLeftWallDepth2; // [esp+B4h] [ebp-440h]
  float fLeftWallFinalDepth; // [esp+B8h] [ebp-43Ch]
  float fLeftWallDepthTmp1; // [esp+BCh] [ebp-438h]
  float fLeftWallDepthTmp2; // [esp+C0h] [ebp-434h]
  float fLeftWallDepthTmp3; // [esp+C4h] [ebp-430h]
  float fLeftWallDepthTmp4; // [esp+C8h] [ebp-42Ch]
  float fLeftWallDepthTmp5; // [esp+CCh] [ebp-428h]
  float fLeftWallDepthTmp6; // [esp+D0h] [ebp-424h]
  float fLeftWallDepthTmp7; // [esp+D4h] [ebp-420h]
  float fRightWallDepth2; // [esp+D8h] [ebp-41Ch]
  float fRightWallFinalDepth; // [esp+DCh] [ebp-418h]
  float fRightWallDepthTmp1; // [esp+E0h] [ebp-414h]
  float fRightWallDepthTmp2; // [esp+E4h] [ebp-410h]
  float fRightWallDepthTmp3; // [esp+E8h] [ebp-40Ch]
  float fRightWallDepthTmp4; // [esp+ECh] [ebp-408h]
  float fRightWallDepthTmp5; // [esp+F0h] [ebp-404h]
  float fRightWallDepthTmp6; // [esp+F4h] [ebp-400h]
  float fRightWallDepthTmp7; // [esp+F8h] [ebp-3FCh]
  unsigned int uiCarArrayOffset; // [esp+FCh] [ebp-3F8h]
  float fGeometryDepthTmp1; // [esp+100h] [ebp-3F4h]
  float fGeometryDepthTmp2; // [esp+104h] [ebp-3F0h]
  float fGeometryDepthTmp3; // [esp+108h] [ebp-3ECh]
  float fGeometryDepthTmp4; // [esp+10Ch] [ebp-3E8h]
  float fGeometryDepthTmp5; // [esp+110h] [ebp-3E4h]
  float fGeometryDepthTmp6; // [esp+114h] [ebp-3E0h]
  float fGeometryDepthTmp7; // [esp+118h] [ebp-3DCh]
  float fRenderDepthTmp1; // [esp+11Ch] [ebp-3D8h]
  float fRenderDepthTmp2; // [esp+120h] [ebp-3D4h]
  float fRenderDepthTmp3; // [esp+124h] [ebp-3D0h]
  float fRenderDepthTmp4; // [esp+128h] [ebp-3CCh]
  float fRenderDepthTmp5; // [esp+12Ch] [ebp-3C8h]
  float fRenderDepthTmp6; // [esp+130h] [ebp-3C4h]
  tVec3 *pTrackVec3Array; // [esp+134h] [ebp-3C0h]
  tVec3 *pTrackGeomFloats; // [esp+138h] [ebp-3BCh]
  float fRenderDepthTmp7; // [esp+13Ch] [ebp-3B8h]
  float fRenderDepthTmp8; // [esp+140h] [ebp-3B4h]
  float fRenderDepthTmp9; // [esp+144h] [ebp-3B0h]
  float fRenderDepthTmp10; // [esp+148h] [ebp-3ACh]
  float fRightWallDepth1; // [esp+14Ch] [ebp-3A8h]
  float fRenderDepthTmp11; // [esp+150h] [ebp-3A4h]
  float fRenderDepthTmp12; // [esp+154h] [ebp-3A0h]
  float fRenderDepthTmp13; // [esp+158h] [ebp-39Ch]
  float fRenderDepthTmp14; // [esp+15Ch] [ebp-398h]
  int iTrackSectionIndex; // [esp+160h] [ebp-394h]
  int iProjectedZ; // [esp+164h] [ebp-390h]
  int iNextSectionIndex; // [esp+168h] [ebp-38Ch]
  int iRightWallFlags; // [esp+16Ch] [ebp-388h]
  int iLeftWallFlags; // [esp+170h] [ebp-384h]
  bool bGroundVisible; // [esp+174h] [ebp-380h]
  float fScreenTempX1; // [esp+178h] [ebp-37Ch]
  float fScreenTempY1; // [esp+17Ch] [ebp-378h]
  float fScreenTempZ1; // [esp+180h] [ebp-374h]
  float fScreenTempX2; // [esp+184h] [ebp-370h]
  float fScreenTempY2; // [esp+188h] [ebp-36Ch]
  float fScreenTempZ2; // [esp+18Ch] [ebp-368h]
  float fScreenTempX3; // [esp+190h] [ebp-364h]
  float fScreenTempY3; // [esp+194h] [ebp-360h]
  float fScreenTempZ3; // [esp+198h] [ebp-35Ch]
  float fScreenTempX4; // [esp+19Ch] [ebp-358h]
  float fScreenTempY4; // [esp+1A0h] [ebp-354h]
  float fScreenTempZ4; // [esp+1A4h] [ebp-350h]
  float fScreenTempX5; // [esp+1A8h] [ebp-34Ch]
  float fScreenTempY5; // [esp+1ACh] [ebp-348h]
  float fScreenTempZ5; // [esp+1B0h] [ebp-344h]
  float fScreenTempX6; // [esp+1B4h] [ebp-340h]
  float fScreenTempY6; // [esp+1B8h] [ebp-33Ch]
  float fScreenTempZ6; // [esp+1BCh] [ebp-338h]
  float fScreenTempX7; // [esp+1C0h] [ebp-334h]
  float fScreenTempY7; // [esp+1C4h] [ebp-330h]
  float fScreenTempZ7; // [esp+1C8h] [ebp-32Ch]
  float fScreenTempX8; // [esp+1CCh] [ebp-328h]
  float fScreenTempY8; // [esp+1D0h] [ebp-324h]
  float fScreenTempZ8; // [esp+1D4h] [ebp-320h]
  float fScreenTempX9; // [esp+1D8h] [ebp-31Ch]
  float fScreenTempY9; // [esp+1DCh] [ebp-318h]
  float fScreenTempZ9; // [esp+1E0h] [ebp-314h]
  float fScreenTempX10; // [esp+1E4h] [ebp-310h]
  float fScreenTempX11; // [esp+1F4h] [ebp-300h]
  float fScreenTempY11; // [esp+1F8h] [ebp-2FCh]
  float fScreenTempZ11; // [esp+1FCh] [ebp-2F8h]
  float fScreenTempX12; // [esp+200h] [ebp-2F4h]
  float fScreenTempY12; // [esp+204h] [ebp-2F0h]
  float fScreenTempZ12; // [esp+208h] [ebp-2ECh]
  float fGroundDepth1; // [esp+20Ch] [ebp-2E8h]
  float fGroundDepthTmp1; // [esp+210h] [ebp-2E4h]
  float fGroundDepthTmp2; // [esp+214h] [ebp-2E0h]
  float fGroundDepthTmp3; // [esp+218h] [ebp-2DCh]
  float fGroundDepthTmp4; // [esp+21Ch] [ebp-2D8h]
  float fTrackDepthTmp1; // [esp+220h] [ebp-2D4h]
  float fTrackDepthTmp2; // [esp+224h] [ebp-2D0h]
  float fProjectionTmp1; // [esp+228h] [ebp-2CCh]
  float fProjectionTmp2; // [esp+22Ch] [ebp-2C8h]
  float fProjectionTmp3; // [esp+230h] [ebp-2C4h]
  float fProjectionTmp4; // [esp+234h] [ebp-2C0h]
  float fLeftRoadDepth1; // [esp+238h] [ebp-2BCh]
  float fLeftRoadDepth2; // [esp+23Ch] [ebp-2B8h]
  float fLeftRoadFinalDepth; // [esp+240h] [ebp-2B4h]
  float fLeftRoadTmp1; // [esp+244h] [ebp-2B0h]
  float fLeftRoadTmp2; // [esp+248h] [ebp-2ACh]
  float fRightRoadDepth1; // [esp+24Ch] [ebp-2A8h]
  float fRightRoadTmp1; // [esp+250h] [ebp-2A4h]
  float fRightRoadTmp2; // [esp+254h] [ebp-2A0h]
  float fSurfaceTmp1; // [esp+258h] [ebp-29Ch]
  float fSurfaceTmp2; // [esp+25Ch] [ebp-298h]
  float fSurfaceTmp3; // [esp+260h] [ebp-294h]
  float fSurfaceTmp4; // [esp+264h] [ebp-290h]
  float fSurfaceTmp5; // [esp+268h] [ebp-28Ch]
  float fSurfaceTmp6; // [esp+26Ch] [ebp-288h]
  float fSurfaceTmp7; // [esp+270h] [ebp-284h]
  float fSurfaceTmp8; // [esp+274h] [ebp-280h]
  float fSurfaceTmp9; // [esp+278h] [ebp-27Ch]
  float fSurfaceTmp10; // [esp+27Ch] [ebp-278h]
  float fSurfaceTmp11; // [esp+280h] [ebp-274h]
  float fSurfaceTmp12; // [esp+284h] [ebp-270h]
  float fSurfaceTmp13; // [esp+288h] [ebp-26Ch]
  float fSurfaceTmp14; // [esp+28Ch] [ebp-268h]
  float fSurfaceTmp15; // [esp+290h] [ebp-264h]
  float fSurfaceTmp16; // [esp+294h] [ebp-260h]
  float fSurfaceTmp17; // [esp+298h] [ebp-25Ch]
  float fSurfaceTmp18; // [esp+29Ch] [ebp-258h]
  float fSurfaceTmp19; // [esp+2A0h] [ebp-254h]
  float fSurfaceTmp20; // [esp+2A4h] [ebp-250h]
  float fSurfaceTmp21; // [esp+2A8h] [ebp-24Ch]
  float fSurfaceTmp22; // [esp+2ACh] [ebp-248h]
  float fSurfaceTmp23; // [esp+2B0h] [ebp-244h]
  float fSurfaceTmp24; // [esp+2B4h] [ebp-240h]
  float fSurfaceTmp25; // [esp+2B8h] [ebp-23Ch]
  float fSurfaceTmp26; // [esp+2BCh] [ebp-238h]
  float fSurfaceTmp27; // [esp+2C0h] [ebp-234h]
  float fSurfaceTmp28; // [esp+2C4h] [ebp-230h]
  float fSurfaceTmp29; // [esp+2C8h] [ebp-22Ch]
  float fSurfaceTmp30; // [esp+2CCh] [ebp-228h]
  float fSurfaceTmp31; // [esp+2D0h] [ebp-224h]
  float fSurfaceTmp32; // [esp+2D4h] [ebp-220h]
  float fSurfaceTmp33; // [esp+2D8h] [ebp-21Ch]
  int iIndexTmp1; // [esp+2DCh] [ebp-218h]
  //int iIndexTmp2; // [esp+2E0h] [ebp-214h]
  int iIndexTmp3; // [esp+2E4h] [ebp-210h]
  float fRenderDepth; // [esp+2E8h] [ebp-20Ch]
  float fLightZ; // [esp+2ECh] [ebp-208h]
  float fRightWallRoofDepth; // [esp+2F0h] [ebp-204h]
  float fLeftWallRoofDepth; // [esp+2F4h] [ebp-200h]
  int *pPrevGroundColour; // [esp+2F8h] [ebp-1FCh]
  float fLightTmp1; // [esp+2FCh] [ebp-1F8h]
  float fOffsetTmp1; // [esp+300h] [ebp-1F4h]
  int iOffsetTmp2; // [esp+308h] [ebp-1ECh]
  int iGroundProjectedZ; // [esp+30Ch] [ebp-1E8h]
  int iRightWallProjectedZ; // [esp+310h] [ebp-1E4h]
  int iLeftWallProjectedZ; // [esp+314h] [ebp-1E0h]
  float fCameraTransformX1; // [esp+318h] [ebp-1DCh]
  float fGroundCameraZ; // [esp+31Ch] [ebp-1D8h]
  float fRightWallCameraZ; // [esp+320h] [ebp-1D4h]
  float fLeftWallCameraZ; // [esp+324h] [ebp-1D0h]
  float fCameraTransformY1; // [esp+328h] [ebp-1CCh]
  float fGroundCameraY; // [esp+32Ch] [ebp-1C8h]
  float fRightWallCameraY; // [esp+330h] [ebp-1C4h]
  float fLeftWallCameraY; // [esp+334h] [ebp-1C0h]
  float fCameraTransformZ1; // [esp+338h] [ebp-1BCh]
  float fGroundCameraX; // [esp+33Ch] [ebp-1B8h]
  float fRightWallCameraX; // [esp+340h] [ebp-1B4h]
  float fLeftWallCameraX; // [esp+344h] [ebp-1B0h]
  int iRenderObjectIndex; // [esp+348h] [ebp-1ACh]
  float fObjectDepthA1; // [esp+34Ch] [ebp-1A8h]
  float fObjectDepthA2; // [esp+350h] [ebp-1A4h]
  float fObjectDepthA3; // [esp+354h] [ebp-1A0h]
  float fObjectDepthA4; // [esp+358h] [ebp-19Ch]
  float fObjectDepthA5; // [esp+35Ch] [ebp-198h]
  float fObjectDepthA6; // [esp+360h] [ebp-194h]
  float fObjectDepthB1; // [esp+364h] [ebp-190h]
  float fObjectDepthB2; // [esp+368h] [ebp-18Ch]
  float fObjectDepthB3; // [esp+36Ch] [ebp-188h]
  float fObjectDepthB4; // [esp+370h] [ebp-184h]
  float fObjectDepthB5; // [esp+374h] [ebp-180h]
  float fObjectDepthB6; // [esp+378h] [ebp-17Ch]
  float fObjectDepthC1; // [esp+37Ch] [ebp-178h]
  float fObjectDepthC2; // [esp+380h] [ebp-174h]
  float fObjectDepthC3; // [esp+384h] [ebp-170h]
  float fObjectDepthC4; // [esp+388h] [ebp-16Ch]
  float fObjectDepthC5; // [esp+38Ch] [ebp-168h]
  float fObjectDepthC6; // [esp+390h] [ebp-164h]
  float fObjectDepthD1; // [esp+394h] [ebp-160h]
  float fObjectDepthD2; // [esp+398h] [ebp-15Ch]
  float fObjectDepthD3; // [esp+39Ch] [ebp-158h]
  float fObjectDepthD4; // [esp+3A0h] [ebp-154h]
  float fObjectDepthD5; // [esp+3A4h] [ebp-150h]
  float fObjectDepthD6; // [esp+3A8h] [ebp-14Ch]
  float fObjectDepthE1; // [esp+3ACh] [ebp-148h]
  float fObjectDepthE2; // [esp+3B0h] [ebp-144h]
  float fObjectDepthE3; // [esp+3B4h] [ebp-140h]
  float fObjectDepthE4; // [esp+3B8h] [ebp-13Ch]
  float fObjectDepthE5; // [esp+3BCh] [ebp-138h]
  float fObjectDepthE6; // [esp+3C0h] [ebp-134h]
  float fObjectDepthF1; // [esp+3C4h] [ebp-130h]
  float fObjectDepthF2; // [esp+3C8h] [ebp-12Ch]
  float fObjectDepthF3; // [esp+3CCh] [ebp-128h]
  float fObjectDepthF4; // [esp+3D0h] [ebp-124h]
  float fObjectDepthF5; // [esp+3D4h] [ebp-120h]
  float fObjectDepthF6; // [esp+3D8h] [ebp-11Ch]
  float fObjectDepthG1; // [esp+3DCh] [ebp-118h]
  float fObjectDepthG2; // [esp+3E0h] [ebp-114h]
  float fObjectDepthG3; // [esp+3E4h] [ebp-110h]
  float fProjectionTempX1; // [esp+3E8h] [ebp-10Ch]
  float fProjectionTempY1; // [esp+3ECh] [ebp-108h]
  float fProjectionTempZ1; // [esp+3F0h] [ebp-104h]
  float fProjectionTempX2; // [esp+3F4h] [ebp-100h]
  float fProjectionTempY2; // [esp+3F8h] [ebp-FCh]
  float fProjectionTempZ2; // [esp+3FCh] [ebp-F8h]
  float fProjectionTempX3; // [esp+400h] [ebp-F4h]
  float fProjectionTempY3; // [esp+404h] [ebp-F0h]
  float fProjectionTempZ3; // [esp+408h] [ebp-ECh]
  float fProjectionTempX4; // [esp+40Ch] [ebp-E8h]
  float fProjectionTempY4; // [esp+410h] [ebp-E4h]
  float fProjectionTempZ4; // [esp+414h] [ebp-E0h]
  float fProjectionTempX5; // [esp+418h] [ebp-DCh]
  float fProjectionTempY5; // [esp+41Ch] [ebp-D8h]
  float fProjectionTempZ5; // [esp+420h] [ebp-D4h]
  float fProjectionTempX6; // [esp+424h] [ebp-D0h]
  float fProjectionTempY6; // [esp+428h] [ebp-CCh]
  float fProjectionTempZ6; // [esp+42Ch] [ebp-C8h]
  float fProjectionTempX7; // [esp+430h] [ebp-C4h]
  float fProjectionTempY7; // [esp+434h] [ebp-C0h]
  float fProjectionTempZ7; // [esp+438h] [ebp-BCh]
  float fProjectionTempX8; // [esp+43Ch] [ebp-B8h]
  float fProjectionTempY8; // [esp+440h] [ebp-B4h]
  float fProjectionTempZ8; // [esp+444h] [ebp-B0h]
  float fProjectionTempX9; // [esp+448h] [ebp-ACh]
  float fProjectionTempY9; // [esp+44Ch] [ebp-A8h]
  float fProjectionTempZ9; // [esp+450h] [ebp-A4h]
  float fProjectionTempX10; // [esp+454h] [ebp-A0h]
  float fProjectionTempY10; // [esp+458h] [ebp-9Ch]
  float fProjectionTempX11; // [esp+464h] [ebp-90h]
  float fProjectionTempY11; // [esp+468h] [ebp-8Ch]
  float fProjectionTempZ11; // [esp+46Ch] [ebp-88h]
  float fProjectionTempX12; // [esp+470h] [ebp-84h]
  float fProjectionTempY12; // [esp+474h] [ebp-80h]
  float fProjectionTempZ12; // [esp+478h] [ebp-7Ch]
  float fProjectionTempX13; // [esp+47Ch] [ebp-78h]
  float fProjectionTempY13; // [esp+480h] [ebp-74h]
  float fProjectionTempZ13; // [esp+484h] [ebp-70h]
  float fProjectionTempFinal; // [esp+488h] [ebp-6Ch]
  uint8 *pScrPtr_1; // [esp+48Ch] [ebp-68h]
  int iChaseCamIdx_1; // [esp+490h] [ebp-64h]
  int iCarIdx_1; // [esp+494h] [ebp-60h]
  float fProjectionZ; // [esp+498h] [ebp-5Ch]
  tTrackScreenXYZ *pNextGroundScreen; // [esp+49Ch] [ebp-58h]
  tTrackScreenXYZ *pCurrentGroundScreen; // [esp+4A0h] [ebp-54h]
  int *pCurrentGroundColour; // [esp+4A4h] [ebp-50h]
  float fTransformTempX1; // [esp+4A8h] [ebp-4Ch]
  float fTransformTempY1; // [esp+4ACh] [ebp-48h]
  float fTransformTempZ1; // [esp+4B0h] [ebp-44h]
  float fTransformTempX2; // [esp+4B4h] [ebp-40h]
  float fTransformTempY2; // [esp+4B8h] [ebp-3Ch]
  float fTransformTempZ2; // [esp+4BCh] [ebp-38h]
  float fTransformTempFinal; // [esp+4C0h] [ebp-34h]
  float fWorldZ; // [esp+4C8h] [ebp-2Ch]
  float fWorldY; // [esp+4CCh] [ebp-28h]
  float fWorldX; // [esp+4D0h] [ebp-24h]
  int iRenderingTemp; // [esp+4E0h] [ebp-14h]

  pScrPtr_1 = pScrPtr;                          // Store function parameters
  iChaseCamIdx_1 = iChaseCamIdx;
  iCarIdx_1 = iCarIdx;
  cars_drawn = 0;                               // Initialize global counters for rendering
  num_pols = 0;
  for (iTrackSectionIndex = 0; TrackSize + 1 >= iTrackSectionIndex; ++iTrackSectionIndex) {
    if (first_size + 1 >= iTrackSectionIndex || iTrackSectionIndex >= gap_size) {
      iCurrentTrackIndex = start_sect + iTrackSectionIndex;// Calculate current track section index with wraparound
      if (start_sect + iTrackSectionIndex >= TRAK_LEN)
        iCurrentTrackIndex -= TRAK_LEN;
      if (iCurrentTrackIndex < 0)
        iCurrentTrackIndex += TRAK_LEN;
      pCurrentTrackPt = TrakPt[iCurrentTrackIndex].pointAy;// Get track geometry data and screen projection structures
      pCurrentTrackScreenXYZ = &TrackScreenXYZ[iCurrentTrackIndex];
      pTrackGeomFloats = pCurrentTrackPt;
      pTrackPoint4 = pCurrentTrackPt + 4;
      pTrackVec3Array = pCurrentTrackPt + 2;
      pTrackPoint3 = pCurrentTrackPt + 3;
      pCurrentTrackPt += 2;
      pCurrentTrackScreenXYZ->iClipCount = 0;
      dDeltaX1 = pCurrentTrackPt->fX - viewx;   // Transform track point 1 from world to camera space
      dDeltaY1 = pCurrentTrackPt->fY - viewy;
      dDeltaZ1 = pCurrentTrackPt->fZ - viewz;
      fWorldX = (float)(dDeltaX1 * vk1 + dDeltaY1 * vk4 + dDeltaZ1 * vk7);
      fWorldY = (float)(dDeltaX1 * vk2 + dDeltaY1 * vk5 + dDeltaZ1 * vk8);
      dCameraZ1 = dDeltaX1 * vk3 + dDeltaY1 * vk6 + dDeltaZ1 * vk9;
      fWorldZ = (float)dCameraZ1;
      dCameraZ1 = round(dCameraZ1);//_CHP();
      iProjectedZ = (int)dCameraZ1;
      if (fWorldZ < 80.0)                     // Apply near clipping plane (min Z = 80.0)
      {
        fWorldZ = 80.0;
        ++pCurrentTrackScreenXYZ->iClipCount;
      }
      dViewDistance1 = (double)VIEWDIST;
      dInvZ1 = 1.0 / fWorldZ;                   // Project to screen coordinates using perspective division
      dScreenX1 = dViewDistance1 * fWorldX * dInvZ1 + (double)xbase;
      dScreenX1 = round(dScreenX1);//_CHP();
      xp = (int)dScreenX1;
      dScreenY1 = dInvZ1 * (dViewDistance1 * fWorldY) + (double)ybase;
      dScreenY1 = round(dScreenY1);//_CHP();
      yp = (int)dScreenY1;
      pCurrentTrackScreenXYZ->screenPtAy[1].screen.x = xp * scr_size >> 6;
      //pCurrentTrackScreenXYZ->screenPtAy[1].screen.x = iScreenX1 >> 6;
      iScreenY1 = scr_size * (199 - yp);
      pCurrentTrackScreenXYZ->screenPtAy[1].projected.fZ = (float)iProjectedZ;
      pCurrentTrackScreenXYZ->screenPtAy[1].screen.y = iScreenY1 >> 6;
      pCurrentTrackScreenXYZ->screenPtAy[1].projected.fX = fWorldX;
      pCurrentTrackScreenXYZ->screenPtAy[1].projected.fY = fWorldY;
      dDeltaX2 = pTrackGeomFloats->fX - viewx;
      dDeltaY2 = pTrackGeomFloats->fY - viewy;
      dDeltaZ2 = pTrackGeomFloats->fZ - viewz;
      fWorldX = (float)(dDeltaX2 * vk1 + dDeltaY2 * vk4 + dDeltaZ2 * vk7);
      fWorldY = (float)(dDeltaX2 * vk2 + dDeltaY2 * vk5 + dDeltaZ2 * vk8);
      dCameraZ2 = dDeltaX2 * vk3 + dDeltaY2 * vk6 + dDeltaZ2 * vk9;
      fWorldZ = (float)dCameraZ2;
      dCameraZ2 = round(dCameraZ2);//_CHP();
      iProjectedZ = (int)dCameraZ2;
      if (fWorldZ < 80.0) {
        fWorldZ = 80.0;
        ++pCurrentTrackScreenXYZ->iClipCount;
      }
      dViewDistance2 = (double)VIEWDIST;
      dInvZ2 = 1.0 / fWorldZ;
      dScreenX2 = dViewDistance2 * fWorldX * dInvZ2 + (double)xbase;
      dScreenX2 = round(dScreenX2);//_CHP();
      xp = (int)dScreenX2;
      dScreenY2 = dInvZ2 * (dViewDistance2 * fWorldY) + (double)ybase;
      dScreenY2 = round(dScreenY2);//_CHP();
      yp = (int)dScreenY2;
      pCurrentTrackScreenXYZ->screenPtAy[0].screen.x = xp * scr_size >> 6;
      //pCurrentTrackScreenXYZ->screenPtAy[0].screen.x = iScreenX2 >> 6;
      iScreenY2 = scr_size * (199 - yp);
      pCurrentTrackScreenXYZ->screenPtAy[0].projected.fZ = (float)iProjectedZ;
      pCurrentTrackScreenXYZ->screenPtAy[0].screen.y = iScreenY2 >> 6;
      pCurrentTrackScreenXYZ->screenPtAy[0].projected.fX = fWorldX;
      pCurrentTrackScreenXYZ->screenPtAy[0].projected.fY = fWorldY;
      dDeltaX3 = pTrackPoint3->fX - viewx;
      dDeltaY3 = pTrackPoint3->fY - viewy;
      dDeltaZ3 = pTrackPoint3->fZ - viewz;
      fWorldX = (float)(dDeltaX3 * vk1 + dDeltaY3 * vk4 + dDeltaZ3 * vk7);
      fWorldY = (float)(dDeltaX3 * vk2 + dDeltaY3 * vk5 + dDeltaZ3 * vk8);
      dCameraZ3 = dDeltaX3 * vk3 + dDeltaY3 * vk6 + dDeltaZ3 * vk9;
      fWorldZ = (float)dCameraZ3;
      dCameraZ3 = round(dCameraZ3);//_CHP();
      iProjectedZ = (int)dCameraZ3;
      if (fWorldZ < 80.0) {
        iClipIncrement3 = pCurrentTrackScreenXYZ->iClipCount + 1;
        fWorldZ = 80.0;
        pCurrentTrackScreenXYZ->iClipCount = iClipIncrement3;
      }
      dViewDistance3 = (double)VIEWDIST;
      dInvZ3 = 1.0 / fWorldZ;
      dScreenX3 = dViewDistance3 * fWorldX * dInvZ3 + (double)xbase;
      dScreenX3 = round(dScreenX3);//_CHP();
      xp = (int)dScreenX3;
      dScreenY3 = dInvZ3 * (dViewDistance3 * fWorldY) + (double)ybase;
      dScreenY3 = round(dScreenY3);//_CHP();
      yp = (int)dScreenY3;
      pCurrentTrackScreenXYZ->screenPtAy[2].screen.x = xp * scr_size >> 6;
      //pCurrentTrackScreenXYZ->screenPtAy[2].screen.x = iScreenX3 >> 6;
      iScreenY3 = scr_size * (199 - yp);
      pCurrentTrackScreenXYZ->screenPtAy[2].projected.fZ = (float)iProjectedZ;
      pCurrentTrackScreenXYZ->screenPtAy[2].screen.y = iScreenY3 >> 6;
      pCurrentTrackScreenXYZ->screenPtAy[2].projected.fX = fWorldX;
      pCurrentTrackScreenXYZ->screenPtAy[2].projected.fY = fWorldY;
      dDeltaX4 = pTrackPoint4->fX - viewx;
      dDeltaY4 = pTrackPoint4->fY - viewy;
      dDeltaZ4 = pTrackPoint4->fZ - viewz;
      fWorldX = (float)(dDeltaX4 * vk1 + dDeltaY4 * vk4 + dDeltaZ4 * vk7);
      fWorldY = (float)(dDeltaX4 * vk2 + dDeltaY4 * vk5 + dDeltaZ4 * vk8);
      dCameraZ4 = dDeltaX4 * vk3 + dDeltaY4 * vk6 + dDeltaZ4 * vk9;
      fWorldZ = (float)dCameraZ4;
      dCameraZ4 = round(dCameraZ4);//_CHP();
      iProjectedZ = (int)dCameraZ4;
      if (fWorldZ < 80.0) {
        iClipIncrement4 = pCurrentTrackScreenXYZ->iClipCount + 1;
        fWorldZ = 80.0;
        pCurrentTrackScreenXYZ->iClipCount = iClipIncrement4;
      }
      dViewDistance4 = (double)VIEWDIST;
      dInvZ4 = 1.0 / fWorldZ;
      dScreenX4 = dViewDistance4 * fWorldX * dInvZ4 + (double)xbase;
      dScreenX4 = round(dScreenX4);//_CHP();
      xp = (int)dScreenX4;
      dScreenY4 = dInvZ4 * (dViewDistance4 * fWorldY) + (double)ybase;
      iScrSize = scr_size;
      dScreenY4 = round(dScreenY4);//_CHP();
      yp = (int)dScreenY4;
      pCurrentTrackScreenXYZ->screenPtAy[3].screen.x = xp * scr_size >> 6;
      //pCurrentTrackScreenXYZ->screenPtAy[3].screen.x = iScreenX4 >> 6;
      pCurrentTrackScreenXYZ->screenPtAy[3].screen.y = (iScrSize * (199 - yp)) >> 6;
      pCurrentTrackScreenXYZ->screenPtAy[3].projected.fX = fWorldX;
      pCurrentTrackScreenXYZ->screenPtAy[3].projected.fY = fWorldY;
      pCurrentTrackScreenXYZ->screenPtAy[3].projected.fZ = (float)iProjectedZ;
      pLeftWallTypePtr = &TrakColour[iCurrentTrackIndex][TRAK_COLOUR_LEFT_WALL];
      iPrevSectionIndex = iCurrentTrackIndex ? iCurrentTrackIndex - 1 : TRAK_LEN - 1;
      pPrevLeftWallTypePtr = &TrakColour[iPrevSectionIndex][TRAK_COLOUR_LEFT_WALL];
      if (*pLeftWallTypePtr && *pPrevLeftWallTypePtr) {
        //LODWORD(fOffsetTmp1) = 72 * iCurrentTrackIndex;//unused here?
        pGroundPt = &TrakPt[iCurrentTrackIndex];
        dLeftWallDeltaX = pGroundPt->pointAy[1].fX - viewx;// Calculate left wall point (point 5) projection to screen coordinates
        dLeftWallDeltaY = pGroundPt->pointAy[1].fY - viewy;
        dLeftWallDeltaZ = pGroundPt->pointAy[1].fZ - viewz;
        fLeftWallCameraX = (float)(dLeftWallDeltaX * vk1 + dLeftWallDeltaY * vk4 + dLeftWallDeltaZ * vk7);
        fLeftWallCameraY = (float)(dLeftWallDeltaX * vk2 + dLeftWallDeltaY * vk5 + dLeftWallDeltaZ * vk8);
        dLeftWallCameraZ = dLeftWallDeltaX * vk3 + dLeftWallDeltaY * vk6 + dLeftWallDeltaZ * vk9;
        fLeftWallCameraZ = (float)dLeftWallCameraZ;
        dLeftWallCameraZ = round(dLeftWallCameraZ);//_CHP();
        iLeftWallProjectedZ = (int)dLeftWallCameraZ;
        if (fLeftWallCameraZ < 80.0)
          fLeftWallCameraZ = 80.0;
        dLeftWallViewDist = (double)VIEWDIST;
        dLeftWallInvZ = 1.0 / fLeftWallCameraZ;
        dLeftWallScreenX = dLeftWallViewDist * fLeftWallCameraX * dLeftWallInvZ + (double)xbase;
        dLeftWallScreenX = round(dLeftWallScreenX);//_CHP();
        xp = (int)dLeftWallScreenX;
        dLeftWallScreenY = dLeftWallInvZ * (dLeftWallViewDist * fLeftWallCameraY) + (double)ybase;
        dLeftWallScreenY = round(dLeftWallScreenY);//_CHP();
        yp = (int)dLeftWallScreenY;
        pCurrentTrackScreenXYZ->screenPtAy[4].screen.x = xp * scr_size >> 6;
        //pCurrentTrackScreenXYZ->screenPtAy[4].screen.x = iLeftWallScreenX >> 6;
        iLeftWallScreenY = scr_size * (199 - yp);
        pCurrentTrackScreenXYZ->screenPtAy[4].projected.fZ = (float)iLeftWallProjectedZ;
        pCurrentTrackScreenXYZ->screenPtAy[4].screen.y = iLeftWallScreenY >> 6;
        pCurrentTrackScreenXYZ->screenPtAy[4].projected.fX = fLeftWallCameraX;
        pCurrentTrackScreenXYZ->screenPtAy[4].projected.fY = fLeftWallCameraY;
      } else {                                         // Copy coordinates from existing points when walls are not present
        if (*pLeftWallTypePtr >= 0 && *pPrevLeftWallTypePtr >= 0) {
          pCurrentTrackScreenXYZ->screenPtAy[4].projected.fX = pCurrentTrackScreenXYZ->screenPtAy[0].projected.fX;
          pCurrentTrackScreenXYZ->screenPtAy[4].projected.fY = pCurrentTrackScreenXYZ->screenPtAy[0].projected.fY;
          dLeftWallCopyZ = pCurrentTrackScreenXYZ->screenPtAy[0].projected.fZ;
          pCurrentTrackScreenXYZ->screenPtAy[4].screen.x = pCurrentTrackScreenXYZ->screenPtAy[0].screen.x;
          iLeftWallCopyY = pCurrentTrackScreenXYZ->screenPtAy[0].screen.y;
        } else {
          pCurrentTrackScreenXYZ->screenPtAy[4].projected.fX = pCurrentTrackScreenXYZ->screenPtAy[1].projected.fX;
          pCurrentTrackScreenXYZ->screenPtAy[4].projected.fY = pCurrentTrackScreenXYZ->screenPtAy[1].projected.fY;
          dLeftWallCopyZ = pCurrentTrackScreenXYZ->screenPtAy[1].projected.fZ;
          pCurrentTrackScreenXYZ->screenPtAy[4].screen.x = pCurrentTrackScreenXYZ->screenPtAy[1].screen.x;
          iLeftWallCopyY = pCurrentTrackScreenXYZ->screenPtAy[1].screen.y;
        }
        pCurrentTrackScreenXYZ->screenPtAy[4].screen.y = iLeftWallCopyY;
        pCurrentTrackScreenXYZ->screenPtAy[4].projected.fZ = (float)dLeftWallCopyZ;
      }
      if (TrakColour[iCurrentTrackIndex][TRAK_COLOUR_RIGHT_WALL] && pPrevLeftWallTypePtr[1]) {
        pGroundPt2 = &TrakPt[iCurrentTrackIndex];
        dRightWallDeltaX = pGroundPt2->pointAy[5].fX - viewx;// Calculate right wall point (point 6) projection to screen coordinates
        dRightWallDeltaY = pGroundPt2->pointAy[5].fY - viewy;
        dRightWallDeltaZ = pGroundPt2->pointAy[5].fZ - viewz;
        fRightWallCameraX = (float)(dRightWallDeltaX * vk1 + dRightWallDeltaY * vk4 + dRightWallDeltaZ * vk7);
        fRightWallCameraY = (float)(dRightWallDeltaX * vk2 + dRightWallDeltaY * vk5 + dRightWallDeltaZ * vk8);
        dRightWallCameraZ = dRightWallDeltaX * vk3 + dRightWallDeltaY * vk6 + dRightWallDeltaZ * vk9;
        fRightWallCameraZ = (float)dRightWallCameraZ;
        dRightWallCameraZ = round(dRightWallCameraZ);//_CHP();
        iRightWallProjectedZ = (int)dRightWallCameraZ;
        if (fRightWallCameraZ < 80.0)
          fRightWallCameraZ = 80.0;
        dRightWallViewDist = (double)VIEWDIST;
        dRightWallInvZ = 1.0 / fRightWallCameraZ;
        dRightWallScreenX = dRightWallViewDist * fRightWallCameraX * dRightWallInvZ + (double)xbase;
        dRightWallScreenX = round(dRightWallScreenX);//_CHP();
        xp = (int)dRightWallScreenX;
        dRightWallScreenY = dRightWallInvZ * (dRightWallViewDist * fRightWallCameraY) + (double)ybase;
        iRightWallScrSize = scr_size;
        dRightWallScreenY = round(dRightWallScreenY);//_CHP();
        yp = (int)dRightWallScreenY;
        pCurrentTrackScreenXYZ->screenPtAy[5].screen.x = xp * scr_size >> 6;
        //pCurrentTrackScreenXYZ->screenPtAy[5].screen.x = iRightWallScreenX >> 6;
        iRightWallScreenY = iRightWallScrSize * (199 - yp);
        pCurrentTrackScreenXYZ->screenPtAy[5].projected.fZ = (float)iRightWallProjectedZ;
        pCurrentTrackScreenXYZ->screenPtAy[5].screen.y = iRightWallScreenY >> 6;
        pCurrentTrackScreenXYZ->screenPtAy[5].projected.fX = fRightWallCameraX;
        pCurrentTrackScreenXYZ->screenPtAy[5].projected.fY = fRightWallCameraY;
      } else {                                         // Copy right wall coordinates from existing points when walls are not present
        if (TrakColour[iCurrentTrackIndex][TRAK_COLOUR_RIGHT_WALL] >= 0 && pPrevLeftWallTypePtr[1] >= 0) {
          pCurrentTrackScreenXYZ->screenPtAy[5].projected.fX = pCurrentTrackScreenXYZ->screenPtAy[3].projected.fX;
          pCurrentTrackScreenXYZ->screenPtAy[5].projected.fY = pCurrentTrackScreenXYZ->screenPtAy[3].projected.fY;
          dRightWallDepthCopy = pCurrentTrackScreenXYZ->screenPtAy[3].projected.fZ;
          pCurrentTrackScreenXYZ->screenPtAy[5].screen.x = pCurrentTrackScreenXYZ->screenPtAy[3].screen.x;
          iRightWallCopy = pCurrentTrackScreenXYZ->screenPtAy[3].screen.y;
        } else {
          pCurrentTrackScreenXYZ->screenPtAy[5].projected.fX = pCurrentTrackScreenXYZ->screenPtAy[2].projected.fX;
          pCurrentTrackScreenXYZ->screenPtAy[5].projected.fY = pCurrentTrackScreenXYZ->screenPtAy[2].projected.fY;
          dRightWallDepthCopy = pCurrentTrackScreenXYZ->screenPtAy[2].projected.fZ;
          pCurrentTrackScreenXYZ->screenPtAy[5].screen.x = pCurrentTrackScreenXYZ->screenPtAy[2].screen.x;
          iRightWallCopy = pCurrentTrackScreenXYZ->screenPtAy[2].screen.y;
        }
        pCurrentTrackScreenXYZ->screenPtAy[5].screen.y = iRightWallCopy;
        pCurrentTrackScreenXYZ->screenPtAy[5].projected.fZ = (float)dRightWallDepthCopy;
      }
      if (Banks_On) {
        pGroundScreenXYZ = &GroundScreenXYZ[iCurrentTrackIndex];
        pScreenPoint = pGroundScreenXYZ->screenPtAy;
        pGroundScreenXYZ->iClipCount = 0;
        pCurrentGroundPt = &GroundPt[iCurrentTrackIndex];
        pCurrentGroundColour = &GroundColour[iCurrentTrackIndex][GROUND_COLOUR_LUOWALL];
        if (iCurrentTrackIndex)
          iPrevGroundIndex = iCurrentTrackIndex - 1;
        else
          iPrevGroundIndex = TRAK_LEN - 1;
        pPrevGroundColour = &GroundColour[iPrevGroundIndex][GROUND_COLOUR_LUOWALL];
        iGroundPointIndex = 0;
        //iIndexTmp2 = iCurrentTrackIndex << 7;
        do {
          if (*pCurrentGroundColour != -1 || *pPrevGroundColour != -1) {
            if (iGroundPointIndex < 2 || iGroundPointIndex > 3 || *pCurrentGroundColour != -2 || TrackScreenXYZ[iCurrentTrackIndex].iClipCount == 99) {
            //if (iGroundPointIndex < 2 || iGroundPointIndex > 3 || *pCurrentGroundColour != -2 || *(_DWORD *)(iIndexTmp2 + 0xF03D4) == 99) {
              dGroundDeltaX = pCurrentGroundPt->pointAy[0].fX - viewx;
              dGroundDeltaY = pCurrentGroundPt->pointAy[0].fY - viewy;
              pGroundPointZ = &pCurrentGroundPt->pointAy[0].fZ;
              dGroundDeltaZ = *pGroundPointZ - viewz;
              fGroundCameraX = (float)(dGroundDeltaX * vk1 + dGroundDeltaY * vk4 + dGroundDeltaZ * vk7);
              fGroundCameraY = (float)(dGroundDeltaX * vk2 + dGroundDeltaY * vk5 + dGroundDeltaZ * vk8);
              dGroundCameraZ = dGroundDeltaX * vk3 + dGroundDeltaY * vk6 + dGroundDeltaZ * vk9;
              fGroundCameraZ = (float)dGroundCameraZ;
              dGroundCameraZ = round(dGroundCameraZ);//_CHP();
              iGroundProjectedZ = (int)dGroundCameraZ;
              pCurrentGroundPt = (tGroundPt *)(pGroundPointZ + 1);
              if (fGroundCameraZ < 80.0)
                fGroundCameraZ = 80.0;
              dGroundViewDist = (double)VIEWDIST;
              dGroundInvZ = 1.0 / fGroundCameraZ;
              dGroundScreenX = dGroundViewDist * fGroundCameraX * dGroundInvZ + (double)xbase;
              dGroundScreenX = round(dGroundScreenX);//_CHP();
              xp = (int)dGroundScreenX;
              dGroundScreenY = dGroundInvZ * (dGroundViewDist * fGroundCameraY) + (double)ybase;
              dGroundScreenY = round(dGroundScreenY);//_CHP();
              yp = (int)dGroundScreenY;
              pScreenPoint->screen.x = xp * scr_size >> 6;
              //pScreenPoint->screen.x = iGroundScreenX >> 6;
              iGroundScreenY = scr_size * (199 - yp);
              pScreenPoint->projected.fZ = (float)iGroundProjectedZ;
              pScreenPoint->screen.y = iGroundScreenY >> 6;
              pScreenPoint->projected.fX = fGroundCameraX;
              pScreenPoint->projected.fY = fGroundCameraY;
              goto LABEL_58;
            }
            if (iGroundPointIndex == 2) {
              pScreenPoint->screen.x = TrackScreenXYZ[iCurrentTrackIndex].screenPtAy[0].screen.x;
              pScreenPoint->screen.y = TrackScreenXYZ[iCurrentTrackIndex].screenPtAy[0].screen.y;
              pScreenPoint->projected.fX = TrackScreenXYZ[iCurrentTrackIndex].screenPtAy[0].projected.fX;
              pScreenPoint->projected.fY = TrackScreenXYZ[iCurrentTrackIndex].screenPtAy[0].projected.fY;
              fGroundProjectedZ = TrackScreenXYZ[iCurrentTrackIndex].screenPtAy[0].projected.fZ;
              //pScreenPoint->screen.x = *(int *)((char *)&TrackScreenXYZ[0].screenPtAy[0].screen.x + iIndexTmp2);
              //pScreenPoint->screen.y = *(int *)((char *)&TrackScreenXYZ[0].screenPtAy[0].screen.y + iIndexTmp2);
              //iGroundSectionOffset = iIndexTmp2;
              //pScreenPoint->projected.fX = *(float *)((char *)&TrackScreenXYZ[0].screenPtAy[0].projected.fX + iIndexTmp2);
              //pScreenPoint->projected.fY = *(float *)((char *)&TrackScreenXYZ[0].screenPtAy[0].projected.fY + iGroundSectionOffset);
              //fGroundProjectedZ = *(float *)((char *)&TrackScreenXYZ[0].screenPtAy[0].projected.fZ + iGroundSectionOffset);
            } else {
              pScreenPoint->screen.x = TrackScreenXYZ[iCurrentTrackIndex].screenPtAy[3].screen.x;
              pScreenPoint->screen.y = TrackScreenXYZ[iCurrentTrackIndex].screenPtAy[3].screen.y;
              pScreenPoint->projected.fX = TrackScreenXYZ[iCurrentTrackIndex].screenPtAy[3].projected.fX;
              pScreenPoint->projected.fY = TrackScreenXYZ[iCurrentTrackIndex].screenPtAy[3].projected.fY;
              fGroundProjectedZ = TrackScreenXYZ[iCurrentTrackIndex].screenPtAy[3].projected.fZ;
              //pScreenPoint->screen.x = *(int *)((char *)&TrackScreenXYZ[0].screenPtAy[3].screen.x + iIndexTmp2);
              //pScreenPoint->screen.y = *(int *)((char *)&TrackScreenXYZ[0].screenPtAy[3].screen.y + iIndexTmp2);
              //iGroundSectionOffset2 = iIndexTmp2;
              //pScreenPoint->projected.fX = *(float *)((char *)&TrackScreenXYZ[0].screenPtAy[3].projected.fX + iIndexTmp2);
              //pScreenPoint->projected.fY = *(float *)((char *)&TrackScreenXYZ[0].screenPtAy[3].projected.fY + iGroundSectionOffset2);
              //fGroundProjectedZ = *(float *)((char *)&TrackScreenXYZ[0].screenPtAy[3].projected.fZ + iGroundSectionOffset2);
            }
            pScreenPoint->projected.fZ = fGroundProjectedZ;
          }
          pCurrentGroundPt = (tGroundPt *)((char *)pCurrentGroundPt + 12);
        LABEL_58:
          if (iGroundPointIndex != 2) {
            ++pCurrentGroundColour;
            ++pPrevGroundColour;
          }
          ++iGroundPointIndex;
          ++pScreenPoint;
        } while (iGroundPointIndex < 6);
      }
    }
  }
  iTrackLoopCounter = TrackSize;                // Second phase: Build render list by traversing track backwards
  num_bits = 0;
  if (TrackSize >= 0) {
    while (iTrackLoopCounter > first_size && iTrackLoopCounter < gap_size) {
    LABEL_356:
      if (--iTrackLoopCounter < 0)
        goto LABEL_357;
    }
    iCurrentSect = iTrackLoopCounter + start_sect;
    if (iTrackLoopCounter + start_sect < 0)
      iCurrentSect += TRAK_LEN;
    if (iCurrentSect >= TRAK_LEN)
      iCurrentSect -= TRAK_LEN;
    iNextSectionIndex = iCurrentSect + 1;
    if (iCurrentSect + 1 < 0)
      iNextSectionIndex += TRAK_LEN;
    if (iNextSectionIndex >= TRAK_LEN)
      iNextSectionIndex -= TRAK_LEN;
    //iSectionOffset = iNextSectionIndex;
    NextSect[iCurrentSect] = iNextSectionIndex;
    //iSectionOffset <<= 7;
    pScreenCoord_1 = &TrackScreenXYZ[iCurrentSect];
    pNextGroundScreen = &GroundScreenXYZ[iNextSectionIndex];
    pScreenCoord = &TrackScreenXYZ[iNextSectionIndex];
    //pNextGroundScreen = (tTrackScreenXYZ *)((char *)GroundScreenXYZ + iSectionOffset);
    //pScreenCoord = (tTrackScreenXYZ *)((char *)TrackScreenXYZ + iSectionOffset);
    iOFloorType = GroundColour[iCurrentSect][GROUND_COLOUR_OFLOOR];// Check if ground floor is visible and banks are enabled
    pCurrentGroundScreen = &GroundScreenXYZ[iCurrentSect];
    bFloorVisible = iOFloorType != -1 && GroundColour[iNextSectionIndex][GROUND_COLOUR_OFLOOR] != -1 && Banks_On;
    iCurrentFloorType = GroundColour[iCurrentSect][GROUND_COLOUR_OFLOOR];
    bGroundVisible = bFloorVisible;
    if (iCurrentFloorType != -2 && bFloorVisible) {
      if (GroundColour[iNextSectionIndex][GROUND_COLOUR_OFLOOR] == -2) {
        iTrackIndexPlus2 = iNextSectionIndex + 2;
        if (iNextSectionIndex + 2 >= TRAK_LEN)
          iTrackIndexPlus2 -= TRAK_LEN;
        pTrackScreenPlus2 = &TrackScreenXYZ[iTrackIndexPlus2];
        if (pTrackScreenPlus2->screenPtAy[2].projected.fZ <= (double)pTrackScreenPlus2->screenPtAy[1].projected.fZ)
          fTrackDepthChoice1 = pTrackScreenPlus2->screenPtAy[1].projected.fZ;
        else
          fTrackDepthChoice1 = pTrackScreenPlus2->screenPtAy[2].projected.fZ;
        fTrackDepthTmp1 = fTrackDepthChoice1;
        if (pScreenCoord->screenPtAy[2].projected.fZ <= (double)pScreenCoord->screenPtAy[1].projected.fZ)
          fTrackDepthChoice2 = pScreenCoord->screenPtAy[1].projected.fZ;
        else
          fTrackDepthChoice2 = pScreenCoord->screenPtAy[2].projected.fZ;
        fTrackDepthTmp2 = fTrackDepthChoice2;
        if (fTrackDepthTmp1 <= (double)fTrackDepthChoice2) {
          if (pScreenCoord->screenPtAy[2].projected.fZ <= (double)pScreenCoord->screenPtAy[1].projected.fZ)
            fTrackDepthFinal = pScreenCoord->screenPtAy[1].projected.fZ;
          else
            fTrackDepthFinal = pScreenCoord->screenPtAy[2].projected.fZ;
          fDepthValuesArray[8] = fTrackDepthFinal;
        } else {
          if (pTrackScreenPlus2->screenPtAy[2].projected.fZ <= (double)pTrackScreenPlus2->screenPtAy[1].projected.fZ)
            fTrackDepthFinal = pTrackScreenPlus2->screenPtAy[1].projected.fZ;
          else
            fTrackDepthFinal = pTrackScreenPlus2->screenPtAy[2].projected.fZ;
          fDepthValuesArray[7] = fTrackDepthFinal;
        }
        fDepthValuesArray[6] = fTrackDepthFinal;
        fProjectionZ = fTrackDepthFinal;
        pScreenCoord_1 = &TrackScreenXYZ[iCurrentSect];
      } else {
        if (pNextGroundScreen->screenPtAy[2].projected.fZ <= (double)pNextGroundScreen->screenPtAy[3].projected.fZ)
          fGroundDepthMax1 = pNextGroundScreen->screenPtAy[3].projected.fZ;
        else
          fGroundDepthMax1 = pNextGroundScreen->screenPtAy[2].projected.fZ;
        fGroundDepth1 = fGroundDepthMax1;
        if (pCurrentGroundScreen->screenPtAy[2].projected.fZ <= (double)pCurrentGroundScreen->screenPtAy[3].projected.fZ)
          fGroundDepthMax2 = pCurrentGroundScreen->screenPtAy[3].projected.fZ;
        else
          fGroundDepthMax2 = pCurrentGroundScreen->screenPtAy[2].projected.fZ;
        fGroundDepthTmp1 = fGroundDepthMax2;
        if (fGroundDepth1 <= (double)fGroundDepthMax2) {
          if (pCurrentGroundScreen->screenPtAy[2].projected.fZ <= (double)pCurrentGroundScreen->screenPtAy[3].projected.fZ)
            fGroundDepthSelected = pCurrentGroundScreen->screenPtAy[3].projected.fZ;
          else
            fGroundDepthSelected = pCurrentGroundScreen->screenPtAy[2].projected.fZ;
          fGroundDepthTmp4 = fGroundDepthSelected;
        } else {
          if (pNextGroundScreen->screenPtAy[2].projected.fZ <= (double)pNextGroundScreen->screenPtAy[3].projected.fZ)
            fGroundDepthSelected = pNextGroundScreen->screenPtAy[3].projected.fZ;
          else
            fGroundDepthSelected = pNextGroundScreen->screenPtAy[2].projected.fZ;
          fGroundDepthTmp3 = fGroundDepthSelected;
        }
        fGroundDepthTmp2 = fGroundDepthSelected;
        fProjectionZ = fGroundDepthSelected;
      }
      pGroundRenderCmd = &TrackView[num_bits];  // Add ground floor polygon to render list
      fGroundRenderDepth = fProjectionZ;
      iGroundRenderIndex = num_bits;
      pGroundRenderCmd->nRenderPriority = 2;
      pGroundRenderCmd->nChunkIdx = iCurrentSect;
      num_bits = iGroundRenderIndex + 1;
      pGroundRenderCmd->fZDepth = fGroundRenderDepth;
    }
    if (pScreenCoord_1->iClipCount != 99 && pScreenCoord->iClipCount != 99 && Road_On) {
      if (pScreenCoord_1->screenPtAy[2].projected.fZ <= (double)pScreenCoord_1->screenPtAy[1].projected.fZ)
        fRoadCenterDepthMax1 = pScreenCoord_1->screenPtAy[1].projected.fZ;
      else
        fRoadCenterDepthMax1 = pScreenCoord_1->screenPtAy[2].projected.fZ;
      fRoadCenterDepth1 = fRoadCenterDepthMax1;
      if (pScreenCoord->screenPtAy[2].projected.fZ <= (double)pScreenCoord->screenPtAy[1].projected.fZ)
        fRoadCenterDepthMax2 = pScreenCoord->screenPtAy[1].projected.fZ;
      else
        fRoadCenterDepthMax2 = pScreenCoord->screenPtAy[2].projected.fZ;
      fRoadCenterDepth2 = fRoadCenterDepthMax2;
      if (fRoadCenterDepth1 <= (double)fRoadCenterDepthMax2) {
        if (pScreenCoord->screenPtAy[2].projected.fZ <= (double)pScreenCoord->screenPtAy[1].projected.fZ)
          fRoadCenterDepthSelected = pScreenCoord->screenPtAy[1].projected.fZ;
        else
          fRoadCenterDepthSelected = pScreenCoord->screenPtAy[2].projected.fZ;
        fRoadCenterDepthFar = fRoadCenterDepthSelected;
      } else {
        if (pScreenCoord_1->screenPtAy[2].projected.fZ <= (double)pScreenCoord_1->screenPtAy[1].projected.fZ)
          fRoadCenterDepthSelected = pScreenCoord_1->screenPtAy[1].projected.fZ;
        else
          fRoadCenterDepthSelected = pScreenCoord_1->screenPtAy[2].projected.fZ;
        fRoadCenterDepthNear = fRoadCenterDepthSelected;
      }
      fRoadCenterFinalDepth = fRoadCenterDepthSelected;
      pRoadCenterCmd = &TrackView[num_bits];    // Add road center polygon to render list
      fRoadCenterCmdDepth = fRoadCenterFinalDepth;
      iRoadCenterCmdIndex = num_bits;
      pRoadCenterCmd->nRenderPriority = 5;
      pRoadCenterCmd->nChunkIdx = iCurrentSect;
      num_bits = iRoadCenterCmdIndex + 1;
      pRoadCenterCmd->fZDepth = fRoadCenterCmdDepth;
    }
    if (pScreenCoord_1->iClipCount != 99 && pScreenCoord->iClipCount != 99 && Road_On) {
      if (pScreenCoord_1->screenPtAy[0].projected.fZ <= (double)pScreenCoord_1->screenPtAy[1].projected.fZ)
        fLeftRoadDepthMax1 = pScreenCoord_1->screenPtAy[1].projected.fZ;
      else
        fLeftRoadDepthMax1 = pScreenCoord_1->screenPtAy[0].projected.fZ;
      fLeftRoadDepth1 = fLeftRoadDepthMax1;
      if (pScreenCoord->screenPtAy[0].projected.fZ <= (double)pScreenCoord->screenPtAy[1].projected.fZ)
        fLeftRoadDepthMax2 = pScreenCoord->screenPtAy[1].projected.fZ;
      else
        fLeftRoadDepthMax2 = pScreenCoord->screenPtAy[0].projected.fZ;
      fLeftRoadDepth2 = fLeftRoadDepthMax2;
      if (fLeftRoadDepth1 <= (double)fLeftRoadDepthMax2) {
        if (pScreenCoord->screenPtAy[0].projected.fZ <= (double)pScreenCoord->screenPtAy[1].projected.fZ)
          fLeftRoadDepthSelected = pScreenCoord->screenPtAy[1].projected.fZ;
        else
          fLeftRoadDepthSelected = pScreenCoord->screenPtAy[0].projected.fZ;
        fLeftRoadTmp2 = fLeftRoadDepthSelected;
      } else {
        if (pScreenCoord_1->screenPtAy[0].projected.fZ <= (double)pScreenCoord_1->screenPtAy[1].projected.fZ)
          fLeftRoadDepthSelected = pScreenCoord_1->screenPtAy[1].projected.fZ;
        else
          fLeftRoadDepthSelected = pScreenCoord_1->screenPtAy[0].projected.fZ;
        fLeftRoadTmp1 = fLeftRoadDepthSelected;
      }
      fLeftRoadFinalDepth = fLeftRoadDepthSelected;
      pLeftRoadCmd = &TrackView[num_bits];      // Add road left side polygon to render list
      fLeftRoadCmdDepth = fLeftRoadFinalDepth;
      iLeftRoadCmdIndex = num_bits;
      pLeftRoadCmd->nRenderPriority = 6;
      pLeftRoadCmd->nChunkIdx = iCurrentSect;
      num_bits = iLeftRoadCmdIndex + 1;
      pLeftRoadCmd->fZDepth = fLeftRoadCmdDepth;
    }
    if (pScreenCoord_1->iClipCount != 99 && pScreenCoord->iClipCount != 99 && Road_On) {
      if (pScreenCoord_1->screenPtAy[2].projected.fZ <= (double)pScreenCoord_1->screenPtAy[3].projected.fZ)
        fRightRoadDepthMax1 = pScreenCoord_1->screenPtAy[3].projected.fZ;
      else
        fRightRoadDepthMax1 = pScreenCoord_1->screenPtAy[2].projected.fZ;
      fRightRoadDepth1 = fRightRoadDepthMax1;
      if (pScreenCoord->screenPtAy[2].projected.fZ <= (double)pScreenCoord->screenPtAy[3].projected.fZ)
        fRightRoadDepthMax2 = pScreenCoord->screenPtAy[3].projected.fZ;
      else
        fRightRoadDepthMax2 = pScreenCoord->screenPtAy[2].projected.fZ;
      fRightRoadDepth2 = fRightRoadDepthMax2;
      if (fRightRoadDepth1 <= (double)fRightRoadDepthMax2) {
        if (pScreenCoord->screenPtAy[2].projected.fZ <= (double)pScreenCoord->screenPtAy[3].projected.fZ)
          fRightRoadDepthSelected = pScreenCoord->screenPtAy[3].projected.fZ;
        else
          fRightRoadDepthSelected = pScreenCoord->screenPtAy[2].projected.fZ;
        fRightRoadDepthFar = fRightRoadDepthSelected;
      } else {
        if (pScreenCoord_1->screenPtAy[2].projected.fZ <= (double)pScreenCoord_1->screenPtAy[3].projected.fZ)
          fRightRoadDepthSelected = pScreenCoord_1->screenPtAy[3].projected.fZ;
        else
          fRightRoadDepthSelected = pScreenCoord_1->screenPtAy[2].projected.fZ;
        fRightRoadDepthNear = fRightRoadDepthSelected;
      }
      fRightRoadFinalDepth = fRightRoadDepthSelected;
      pRightRoadCmd = &TrackView[num_bits];     // Add road right side polygon to render list
      fRightRoadCmdDepth = fRightRoadFinalDepth;
      iRightRoadCmdIndex = num_bits;
      pRightRoadCmd->nRenderPriority = 7;
      pRightRoadCmd->nChunkIdx = iCurrentSect;
      num_bits = iRightRoadCmdIndex + 1;
      pRightRoadCmd->fZDepth = fRightRoadCmdDepth;
    }
    if (pScreenCoord_1->iClipCount != 99 && pScreenCoord->iClipCount != 99) {
      if (Walls_On) {
        iRoofTypeCheck = TrakColour[iCurrentSect][TRAK_COLOUR_ROOF];
        //TODO look at this line
        if (iRoofTypeCheck != -1 && TrakColour[iCurrentSect][TRAK_COLOUR_LEFT_WALL] && TrakColour[iCurrentSect][TRAK_COLOUR_RIGHT_WALL]) {
        //if (iRoofTypeCheck != -1 && iLeftWallFlags && iRightWallFlags) {
          if (iRoofTypeCheck < 0) {
            if (pNextGroundScreen->screenPtAy[5].projected.fZ >= (double)pNextGroundScreen->screenPtAy[0].projected.fZ)
              fRoof1OuterDepth = pNextGroundScreen->screenPtAy[0].projected.fZ;
            else
              fRoof1OuterDepth = pNextGroundScreen->screenPtAy[5].projected.fZ;
            fRoof1OuterDepthTmp = fRoof1OuterDepth;
            if (pNextGroundScreen->screenPtAy[1].projected.fZ >= (double)pNextGroundScreen->screenPtAy[4].projected.fZ)
              fRoof1InnerDepth = pNextGroundScreen->screenPtAy[4].projected.fZ;
            else
              fRoof1InnerDepth = pNextGroundScreen->screenPtAy[1].projected.fZ;
            fRoof1InnerDepthTmp = fRoof1InnerDepth;
            if (fRoof1OuterDepthTmp >= (double)fRoof1InnerDepth) {
              if (pNextGroundScreen->screenPtAy[1].projected.fZ >= (double)pNextGroundScreen->screenPtAy[4].projected.fZ)
                fRoof1SelectedDepth = pNextGroundScreen->screenPtAy[4].projected.fZ;
              else
                fRoof1SelectedDepth = pNextGroundScreen->screenPtAy[1].projected.fZ;
              fRoof1DepthInner = fRoof1SelectedDepth;
            } else {
              if (pNextGroundScreen->screenPtAy[5].projected.fZ >= (double)pNextGroundScreen->screenPtAy[0].projected.fZ)
                fRoof1SelectedDepth = pNextGroundScreen->screenPtAy[0].projected.fZ;
              else
                fRoof1SelectedDepth = pNextGroundScreen->screenPtAy[5].projected.fZ;
              fRoof1DepthOuter = fRoof1SelectedDepth;
            }
            fRoof1DepthSelected = fRoof1SelectedDepth;
            pRoof1RenderCmd = &TrackView[num_bits];// Add roof/ceiling polygon to render list
            pRoof1RenderCmd->nRenderPriority = 10;
            fRoof1CmdDepth = fRoof1DepthSelected;
            pRoof1RenderCmd->nChunkIdx = iCurrentSect;
            pRoof1RenderCmd->fZDepth = fRoof1CmdDepth;
            ++num_bits;
            goto LABEL_238;
          }
          iRoofType = TrakColour[iNextSectionIndex][TRAK_COLOUR_ROOF];
          if (iRoofType <= 0) {
            if (iRoofType >= -1)
              goto LABEL_238;
            if (pCurrentGroundScreen->screenPtAy[5].projected.fZ >= (double)pCurrentGroundScreen->screenPtAy[0].projected.fZ)
              fRoof3OuterDepth = pCurrentGroundScreen->screenPtAy[0].projected.fZ;
            else
              fRoof3OuterDepth = pCurrentGroundScreen->screenPtAy[5].projected.fZ;
            fRoof3OuterDepthTmp = fRoof3OuterDepth;
            if (pCurrentGroundScreen->screenPtAy[1].projected.fZ >= (double)pCurrentGroundScreen->screenPtAy[4].projected.fZ)
              fRoof3InnerDepth = pCurrentGroundScreen->screenPtAy[4].projected.fZ;
            else
              fRoof3InnerDepth = pCurrentGroundScreen->screenPtAy[1].projected.fZ;
            fRoof3InnerDepthTmp = fRoof3InnerDepth;
            if (fRoof3OuterDepthTmp >= (double)fRoof3InnerDepth) {
              if (pCurrentGroundScreen->screenPtAy[1].projected.fZ >= (double)pCurrentGroundScreen->screenPtAy[4].projected.fZ)
                fRoof3SelectedDepth = pCurrentGroundScreen->screenPtAy[4].projected.fZ;
              else
                fRoof3SelectedDepth = pCurrentGroundScreen->screenPtAy[1].projected.fZ;
              fRoof3DepthInner = fRoof3SelectedDepth;
            } else {
              if (pCurrentGroundScreen->screenPtAy[5].projected.fZ >= (double)pCurrentGroundScreen->screenPtAy[0].projected.fZ)
                fRoof3SelectedDepth = pCurrentGroundScreen->screenPtAy[0].projected.fZ;
              else
                fRoof3SelectedDepth = pCurrentGroundScreen->screenPtAy[5].projected.fZ;
              fRoof3DepthOuter = fRoof3SelectedDepth;
            }
            fRoof3DepthSelected = fRoof3SelectedDepth;
            pRoof2RenderCmd = &TrackView[num_bits];
            fRoof2CmdDepth = fRoof3DepthSelected;
          } else {
            if (TrakColour[iCurrentSect][TRAK_COLOUR_RIGHT_WALL] >= 0)
              dRoof2WallDepth1 = pScreenCoord_1->screenPtAy[3].projected.fZ;
            else
              dRoof2WallDepth1 = pScreenCoord_1->screenPtAy[2].projected.fZ;
            dRoof2WallDepth1 = round(dRoof2WallDepth1);//_CHP();
            iRightWallFlags = (int)dRoof2WallDepth1;
            if (TrakColour[iCurrentSect][TRAK_COLOUR_LEFT_WALL] >= 0)
              dRoof2WallDepth2 = pScreenCoord_1->screenPtAy[0].projected.fZ;
            else
              dRoof2WallDepth2 = pScreenCoord_1->screenPtAy[1].projected.fZ;
            dRoof2WallDepth2 = round(dRoof2WallDepth2);//_CHP();
            iLeftWallFlags = (int)dRoof2WallDepth2;
            if (pScreenCoord_1->screenPtAy[4].projected.fZ >= (double)pScreenCoord_1->screenPtAy[5].projected.fZ)
              fRoof2WallMinDepth = pScreenCoord_1->screenPtAy[5].projected.fZ;
            else
              fRoof2WallMinDepth = pScreenCoord_1->screenPtAy[4].projected.fZ;
            fRoof2WallMinDepthTmp = fRoof2WallMinDepth;
            if (iRightWallFlags >= iLeftWallFlags)
              iRoof2WallDepthMin = iLeftWallFlags;
            else
              iRoof2WallDepthMin = iRightWallFlags;
            if ((double)iRoof2WallDepthMin >= fRoof2WallMinDepthTmp) {
              if (pScreenCoord_1->screenPtAy[4].projected.fZ >= (double)pScreenCoord_1->screenPtAy[5].projected.fZ)
                fRoof2SelectedDepth = pScreenCoord_1->screenPtAy[5].projected.fZ;
              else
                fRoof2SelectedDepth = pScreenCoord_1->screenPtAy[4].projected.fZ;
              fRoof2DepthWall = fRoof2SelectedDepth;
              fRoof2DepthSelected = fRoof2SelectedDepth;
            } else {
              if (iRightWallFlags >= iLeftWallFlags)
                iRoof2WallDepthChoice = iLeftWallFlags;
              else
                iRoof2WallDepthChoice = iRightWallFlags;
              fRoof2DepthSelected = (float)iRoof2WallDepthChoice;
            }
            pRoof2RenderCmd = &TrackView[num_bits];
            fRoof2CmdDepth = fRoof2DepthSelected;
          }
          iRoof3CmdIndex = num_bits;
          pRoof2RenderCmd->nRenderPriority = 10;
          pRoof2RenderCmd->nChunkIdx = iCurrentSect;
          num_bits = iRoof3CmdIndex + 1;
          pRoof2RenderCmd->fZDepth = fRoof2CmdDepth;
        }
      }
    }
  LABEL_238:
    if (GroundColour[iCurrentSect][GROUND_COLOUR_LLOWALL] != -1 && bGroundVisible) {
      if (pNextGroundScreen->screenPtAy[2].projected.fZ <= (double)pNextGroundScreen->screenPtAy[1].projected.fZ)
        fLeftLowerWallDepth1 = pNextGroundScreen->screenPtAy[1].projected.fZ;
      else
        fLeftLowerWallDepth1 = pNextGroundScreen->screenPtAy[2].projected.fZ;
      fLeftLowerWallDepthNear = fLeftLowerWallDepth1;
      if (pCurrentGroundScreen->screenPtAy[2].projected.fZ <= (double)pCurrentGroundScreen->screenPtAy[1].projected.fZ)
        fLeftLowerWallDepth2 = pCurrentGroundScreen->screenPtAy[1].projected.fZ;
      else
        fLeftLowerWallDepth2 = pCurrentGroundScreen->screenPtAy[2].projected.fZ;
      fLeftLowerWallDepthFar = fLeftLowerWallDepth2;
      if (fLeftLowerWallDepthNear <= (double)fLeftLowerWallDepth2) {
        if (pCurrentGroundScreen->screenPtAy[2].projected.fZ <= (double)pCurrentGroundScreen->screenPtAy[1].projected.fZ)
          fLeftLowerWallSelected = pCurrentGroundScreen->screenPtAy[1].projected.fZ;
        else
          fLeftLowerWallSelected = pCurrentGroundScreen->screenPtAy[2].projected.fZ;
        fLeftLowerWallDepthTmp2 = fLeftLowerWallSelected;
      } else {
        if (pNextGroundScreen->screenPtAy[2].projected.fZ <= (double)pNextGroundScreen->screenPtAy[1].projected.fZ)
          fLeftLowerWallSelected = pNextGroundScreen->screenPtAy[1].projected.fZ;
        else
          fLeftLowerWallSelected = pNextGroundScreen->screenPtAy[2].projected.fZ;
        fLeftLowerWallDepthTmp1 = fLeftLowerWallSelected;
      }
      fLeftLowerWallDepthSelected = fLeftLowerWallSelected;
      pLeftLowerWallCmd = &TrackView[num_bits]; // Add left lower wall polygon to render list
      pLeftLowerWallCmd->nRenderPriority = 3;
      fLeftLowerWallCmdDepth = fLeftLowerWallDepthSelected;
      pLeftLowerWallCmd->nChunkIdx = iCurrentSect;
      pLeftLowerWallCmd->fZDepth = fLeftLowerWallCmdDepth;
      ++num_bits;
    }
    if (GroundColour[iCurrentSect][GROUND_COLOUR_RLOWALL] != -1 && bGroundVisible) {
      if (pNextGroundScreen->screenPtAy[3].projected.fZ <= (double)pNextGroundScreen->screenPtAy[4].projected.fZ)
        fRightLowerWallDepth1 = pNextGroundScreen->screenPtAy[4].projected.fZ;
      else
        fRightLowerWallDepth1 = pNextGroundScreen->screenPtAy[3].projected.fZ;
      fRightLowerWallDepthNear = fRightLowerWallDepth1;
      if (pCurrentGroundScreen->screenPtAy[3].projected.fZ <= (double)pCurrentGroundScreen->screenPtAy[4].projected.fZ)
        fRightLowerWallDepth2 = pCurrentGroundScreen->screenPtAy[4].projected.fZ;
      else
        fRightLowerWallDepth2 = pCurrentGroundScreen->screenPtAy[3].projected.fZ;
      fRightLowerWallDepthFar = fRightLowerWallDepth2;
      if (fRightLowerWallDepthNear <= (double)fRightLowerWallDepth2) {
        if (pCurrentGroundScreen->screenPtAy[3].projected.fZ <= (double)pCurrentGroundScreen->screenPtAy[4].projected.fZ)
          fRightLowerWallSelected = pCurrentGroundScreen->screenPtAy[4].projected.fZ;
        else
          fRightLowerWallSelected = pCurrentGroundScreen->screenPtAy[3].projected.fZ;
        fRightLowerWallDepthTmp2 = fRightLowerWallSelected;
      } else {
        if (pNextGroundScreen->screenPtAy[3].projected.fZ <= (double)pNextGroundScreen->screenPtAy[4].projected.fZ)
          fRightLowerWallSelected = pNextGroundScreen->screenPtAy[4].projected.fZ;
        else
          fRightLowerWallSelected = pNextGroundScreen->screenPtAy[3].projected.fZ;
        fRightLowerWallDepthTmp1 = fRightLowerWallSelected;
      }
      fRightLowerWallDepthSelected = fRightLowerWallSelected;
      pRightLowerWallCmd = &TrackView[num_bits];// Add right lower wall polygon to render list
      pRightLowerWallCmd->nRenderPriority = 4;
      fRightLowerWallCmdDepth = fRightLowerWallDepthSelected;
      pRightLowerWallCmd->nChunkIdx = iCurrentSect;
      pRightLowerWallCmd->fZDepth = fRightLowerWallCmdDepth;
      ++num_bits;
    }
    if (pScreenCoord_1->iClipCount != 99 && pScreenCoord->iClipCount != 99) {
      if (Walls_On) {
        iLeftWallFlags = TrakColour[iCurrentSect][TRAK_COLOUR_LEFT_WALL];
        if (iLeftWallFlags) {
          if (TrackInfo[iCurrentSect].fRoofHeight >= 0.0 && TrackInfo[iNextSectionIndex].fRoofHeight >= 0.0) {
            if (iLeftWallFlags >= 0) {
              if (pScreenCoord->screenPtAy[0].projected.fZ <= (double)pScreenCoord_1->screenPtAy[0].projected.fZ)
                fRightWallDepthMax1 = pScreenCoord_1->screenPtAy[0].projected.fZ;
              else
                fRightWallDepthMax1 = pScreenCoord->screenPtAy[0].projected.fZ;
              fLeftWallDepthTmp3 = fRightWallDepthMax1;
              if (pScreenCoord->screenPtAy[4].projected.fZ <= (double)pScreenCoord_1->screenPtAy[4].projected.fZ)
                fRightWallDepthMax2 = pScreenCoord_1->screenPtAy[4].projected.fZ;
              else
                fRightWallDepthMax2 = pScreenCoord->screenPtAy[4].projected.fZ;
              fLeftWallDepthTmp4 = fRightWallDepthMax2;
              if (fLeftWallDepthTmp3 <= (double)fRightWallDepthMax2) {
                if (pScreenCoord->screenPtAy[4].projected.fZ <= (double)pScreenCoord_1->screenPtAy[4].projected.fZ)
                  fRightWallDepthSelected = pScreenCoord_1->screenPtAy[4].projected.fZ;
                else
                  fRightWallDepthSelected = pScreenCoord->screenPtAy[4].projected.fZ;
                fLeftWallDepthTmp7 = fRightWallDepthSelected;
              } else {
                if (pScreenCoord->screenPtAy[0].projected.fZ <= (double)pScreenCoord_1->screenPtAy[0].projected.fZ)
                  fRightWallDepthSelected = pScreenCoord_1->screenPtAy[0].projected.fZ;
                else
                  fRightWallDepthSelected = pScreenCoord->screenPtAy[0].projected.fZ;
                fLeftWallDepthTmp6 = fRightWallDepthSelected;
              }
              fLeftWallDepthTmp5 = fRightWallDepthSelected;
              fLeftWallRoofDepth = fRightWallDepthSelected;
            } else {
              fLeftWallRoofDepth = (pScreenCoord->screenPtAy[1].projected.fZ
                                  + pScreenCoord_1->screenPtAy[1].projected.fZ
                                  + pScreenCoord->screenPtAy[4].projected.fZ
                                  + pScreenCoord_1->screenPtAy[4].projected.fZ)
                * 0.25f;
            }
            pRightWallCmd = &TrackView[num_bits];
            pRightWallCmd->nRenderPriority = 8;
            fRightWallCmdDepth = fLeftWallRoofDepth;
            pRightWallCmd->nChunkIdx = iCurrentSect;
            pRightWallCmd->fZDepth = fRightWallCmdDepth;
            ++num_bits;
          } else {
            if (pScreenCoord_1->screenPtAy[0].projected.fZ <= (double)pScreenCoord_1->screenPtAy[1].projected.fZ)
              fLeftWallDepthMax1 = pScreenCoord_1->screenPtAy[1].projected.fZ;
            else
              fLeftWallDepthMax1 = pScreenCoord_1->screenPtAy[0].projected.fZ;
            fLeftWallDepth1 = fLeftWallDepthMax1;
            if (pScreenCoord->screenPtAy[0].projected.fZ <= (double)pScreenCoord->screenPtAy[1].projected.fZ)
              fLeftWallDepthMax2 = pScreenCoord->screenPtAy[1].projected.fZ;
            else
              fLeftWallDepthMax2 = pScreenCoord->screenPtAy[0].projected.fZ;
            fLeftWallDepth2 = fLeftWallDepthMax2;
            if (fLeftWallDepth1 <= (double)fLeftWallDepthMax2) {
              if (pScreenCoord->screenPtAy[0].projected.fZ <= (double)pScreenCoord->screenPtAy[1].projected.fZ)
                fLeftWallDepthSelected = pScreenCoord->screenPtAy[1].projected.fZ;
              else
                fLeftWallDepthSelected = pScreenCoord->screenPtAy[0].projected.fZ;
              fLeftWallDepthTmp2 = fLeftWallDepthSelected;
            } else {
              if (pScreenCoord_1->screenPtAy[0].projected.fZ <= (double)pScreenCoord_1->screenPtAy[1].projected.fZ)
                fLeftWallDepthSelected = pScreenCoord_1->screenPtAy[1].projected.fZ;
              else
                fLeftWallDepthSelected = pScreenCoord_1->screenPtAy[0].projected.fZ;
              fLeftWallDepthTmp1 = fLeftWallDepthSelected;
            }
            fLeftWallFinalDepth = fLeftWallDepthSelected;
            pLeftWallCmd = &TrackView[num_bits];// Add left wall polygon to render list
            pLeftWallCmd->fZDepth = fLeftWallFinalDepth;
            iLeftWallCmdIndex = num_bits;
            pLeftWallCmd->nRenderPriority = 0;
            pLeftWallCmd->nChunkIdx = iCurrentSect;
            num_bits = iLeftWallCmdIndex + 1;
          }
        }
      }
    }
    if (pScreenCoord_1->iClipCount != 99 && pScreenCoord->iClipCount != 99) {
      if (Walls_On) {
        iRightWallFlags = TrakColour[iCurrentSect][TRAK_COLOUR_RIGHT_WALL];
        if (iRightWallFlags) {
          if (TrackInfo[iCurrentSect].fRoofHeight >= 0.0 && TrackInfo[iNextSectionIndex].fRoofHeight >= 0.0) {
            if (iRightWallFlags >= 0) {
              if (pScreenCoord_1->screenPtAy[3].projected.fZ <= (double)pScreenCoord->screenPtAy[3].projected.fZ)
                fRightWallRoofDepth1 = pScreenCoord->screenPtAy[3].projected.fZ;
              else
                fRightWallRoofDepth1 = pScreenCoord_1->screenPtAy[3].projected.fZ;
              fRightWallDepthTmp3 = fRightWallRoofDepth1;
              if (pScreenCoord_1->screenPtAy[5].projected.fZ <= (double)pScreenCoord->screenPtAy[5].projected.fZ)
                fRightWallRoofDepth2 = pScreenCoord->screenPtAy[5].projected.fZ;
              else
                fRightWallRoofDepth2 = pScreenCoord_1->screenPtAy[5].projected.fZ;
              fRightWallDepthTmp4 = fRightWallRoofDepth2;
              if (fRightWallDepthTmp3 <= (double)fRightWallRoofDepth2) {
                if (pScreenCoord_1->screenPtAy[5].projected.fZ <= (double)pScreenCoord->screenPtAy[5].projected.fZ)
                  fRightWallRoofSelected = pScreenCoord->screenPtAy[5].projected.fZ;
                else
                  fRightWallRoofSelected = pScreenCoord_1->screenPtAy[5].projected.fZ;
                fRightWallDepthTmp7 = fRightWallRoofSelected;
              } else {
                if (pScreenCoord_1->screenPtAy[3].projected.fZ <= (double)pScreenCoord->screenPtAy[3].projected.fZ)
                  fRightWallRoofSelected = pScreenCoord->screenPtAy[3].projected.fZ;
                else
                  fRightWallRoofSelected = pScreenCoord_1->screenPtAy[3].projected.fZ;
                fRightWallDepthTmp6 = fRightWallRoofSelected;
              }
              fRightWallDepthTmp5 = fRightWallRoofSelected;
              fRightWallRoofDepth = fRightWallRoofSelected;
            } else {
              fRightWallRoofDepth = (pScreenCoord_1->screenPtAy[2].projected.fZ
                                   + pScreenCoord->screenPtAy[2].projected.fZ
                                   + pScreenCoord_1->screenPtAy[5].projected.fZ
                                   + pScreenCoord->screenPtAy[5].projected.fZ)
                * 0.25f;
            }
            pRightWallRoofCmd = &TrackView[num_bits];
            pRightWallRoofCmd->fZDepth = fRightWallRoofDepth;
            iRightWallRoofCmdIndex = num_bits;
            pRightWallRoofCmd->nRenderPriority = 9;
            pRightWallRoofCmd->nChunkIdx = iCurrentSect;
            num_bits = iRightWallRoofCmdIndex + 1;
          } else {
            if (pScreenCoord_1->screenPtAy[2].projected.fZ <= (double)pScreenCoord_1->screenPtAy[3].projected.fZ)
              fRightWallBasicDepth1 = pScreenCoord_1->screenPtAy[3].projected.fZ;
            else
              fRightWallBasicDepth1 = pScreenCoord_1->screenPtAy[2].projected.fZ;
            fRightWallDepth1 = fRightWallBasicDepth1;
            if (pScreenCoord->screenPtAy[2].projected.fZ <= (double)pScreenCoord->screenPtAy[3].projected.fZ)
              fRightWallBasicDepth2 = pScreenCoord->screenPtAy[3].projected.fZ;
            else
              fRightWallBasicDepth2 = pScreenCoord->screenPtAy[2].projected.fZ;
            fRightWallDepth2 = fRightWallBasicDepth2;
            if (fRightWallDepth1 <= (double)fRightWallBasicDepth2) {
              if (pScreenCoord->screenPtAy[2].projected.fZ <= (double)pScreenCoord->screenPtAy[3].projected.fZ)
                fRightWallBasicSelected = pScreenCoord->screenPtAy[3].projected.fZ;
              else
                fRightWallBasicSelected = pScreenCoord->screenPtAy[2].projected.fZ;
              fRightWallDepthTmp2 = fRightWallBasicSelected;
            } else {
              if (pScreenCoord_1->screenPtAy[2].projected.fZ <= (double)pScreenCoord_1->screenPtAy[3].projected.fZ)
                fRightWallBasicSelected = pScreenCoord_1->screenPtAy[3].projected.fZ;
              else
                fRightWallBasicSelected = pScreenCoord_1->screenPtAy[2].projected.fZ;
              fRightWallDepthTmp1 = fRightWallBasicSelected;
            }
            fRightWallFinalDepth = fRightWallBasicSelected;
            pRightWallBasicCmd = &TrackView[num_bits];// Add right wall polygon to render list
            fRightWallBasicCmdDepth = fRightWallFinalDepth;
            iRightWallBasicCmdIndex = num_bits;
            pRightWallBasicCmd->nRenderPriority = 1;
            pRightWallBasicCmd->nChunkIdx = iCurrentSect;
            num_bits = iRightWallBasicCmdIndex + 1;
            pRightWallBasicCmd->fZDepth = fRightWallBasicCmdDepth;
          }
        }
      }
    }
    goto LABEL_356;
  }
LABEL_357:
  iCarIndex = 0;                                // Third phase: Process car objects for rendering
  if (numcars > 0) {
    iCarArrayIndex = 0;
    pCarRenderCmd = &TrackView[num_bits];
    do {
      iCarDrawOrderStatus = car_draw_order[iCarArrayIndex].iChunkIdx;
      if (iCarDrawOrderStatus == -3 || iCarDrawOrderStatus >= 0) {
        iCarDrawOrderIndex = car_draw_order[iCarArrayIndex].fMinZDepth;
        pCarRenderCmd->nRenderPriority = 11;
        fOffsetTmp1 = iCarDrawOrderIndex;
        iCarProcessingFlag = cars_drawn;
        pCarRenderCmd->nChunkIdx = car_draw_order[iCarArrayIndex].iCarIdx;
        pCarRenderCmd->fZDepth = fOffsetTmp1;
        ++pCarRenderCmd;
        cars_drawn = iCarProcessingFlag + 1;
        ++num_bits;
      }
      ++iCarIndex;
      ++iCarArrayIndex;
    } while (iCarIndex < numcars);
  }
  iCarsRenderedCount = 0;                       // Fourth phase: Count visible cars and determine names display
  iCarLoopIndex = 0;
  NamesLeft = 0;
  VisibleCars = 0;
  VisibleHumans = 0;
  if (numcars > 0) {
    uiCarArrayOffset = 12 * iCarIdx_1;
    uiCarIndexOffset = 0;
    iIndexTmp3 = 12 * numcars;
    do {
      iCarStatusFlag = car_draw_order[uiCarIndexOffset / 0xC].iChunkIdx;
      if ((iCarStatusFlag == -3 || iCarStatusFlag >= 0) && iCarIdx_1 != car_draw_order[uiCarIndexOffset / 0xC].iCarIdx) {
        if (car_draw_order[uiCarIndexOffset / 0xC].fMinZDepth < (double)car_draw_order[uiCarArrayOffset / 0xC].fMinZDepth) {
          if (human_control[car_draw_order[uiCarIndexOffset / 0xC].iCarIdx])
            ++VisibleCars;
          ++NamesLeft;
        } else {
          if (human_control[car_draw_order[uiCarIndexOffset / 0xC].iCarIdx])
            ++VisibleHumans;
          ++iCarsRenderedCount;
        }
      }
      uiCarIndexOffset += 12;
      ++iCarLoopIndex;
    } while ((int)uiCarIndexOffset < iIndexTmp3);
  }
  if (NamesLeft <= 1) {
    CarsLeft = iCarsRenderedCount + 2 - NamesLeft;
  } else {
    CarsLeft = iCarsRenderedCount;
    if (iCarsRenderedCount < 6) {
      if (NamesLeft >= 4)
        iCarVisibilityCount = iCarsRenderedCount + 3;
      else
        iCarVisibilityCount = NamesLeft + iCarsRenderedCount;
      CarsLeft = iCarVisibilityCount;
    }
  }
  if (VisibleCars < 2)
    VisibleHumans = VisibleHumans + 2 - VisibleCars;
  if (!names_on) {
    NamesLeft = 1000;
    goto LABEL_393;
  }
  if ((unsigned int)names_on <= 1) {
    iNamesDisplayCount = CarsLeft;
  LABEL_392:
    NamesLeft = iNamesDisplayCount;
    goto LABEL_393;
  }
  if (names_on == 2) {
    iNamesDisplayCount = VisibleHumans;
    goto LABEL_392;
  }
LABEL_393:
  pVisibleBuildingsPtr = &VisibleBuildings[0];     // Process building objects for rendering
  if (VisibleBuildings[0].iBuildingIdx != -1) {
    pBuildingRenderCmd = &TrackView[num_bits];
    do {
      iBuildingCmdIndex = num_bits;
      //iBuildingValue = pVisibleBuildingsPtr->fDepth;
      pBuildingRenderCmd->nRenderPriority = 13;
      //fOffsetTmp1 = iBuildingValue;
      //LOWORD(iBuildingValue) = pVisibleBuildingsPtr->iBuildingIdx;
      pBuildingRenderCmd->nChunkIdx = pVisibleBuildingsPtr->iBuildingIdx;
      //pBuildingRenderCmd->nChunkIdx = LOWORD(iBuildingValue);
      pBuildingRenderCmd->fZDepth = pVisibleBuildingsPtr->fDepth;
      //pBuildingRenderCmd->fZDepth = fOffsetTmp1;
      ++pVisibleBuildingsPtr;
      ++pBuildingRenderCmd;
      iBuildingNext = pVisibleBuildingsPtr->iBuildingIdx;
      num_bits = iBuildingCmdIndex + 1;
    } while (iBuildingNext != -1);
  }
  if (countdown > -72 && replaytype != 2 && game_type != 2 && !winner_mode)// Process starting lights for rendering (if countdown active)
  {
    iLightIndex = 0;
    pLightRenderCmd = &TrackView[num_bits];
    iLightArrayOffset = 3 * iChaseCamIdx_1;
    do {
      fLightZ = (SLight[0][iLightArrayOffset].currentPos.fX - viewx) * vk3
        + (SLight[0][iLightArrayOffset].currentPos.fY - viewy) * vk6
        + (SLight[0][iLightArrayOffset].currentPos.fZ - viewz) * vk9;
      if (fLightZ > 0.0) {
        pLightRenderCmd->nRenderPriority = 14;
        ++pLightRenderCmd;
        fLightDepth = fLightZ;
        iLightCmdIndex = num_bits;
        pLightRenderCmd[-1].nChunkIdx = iLightIndex;
        pLightRenderCmd[-1].fZDepth = fLightDepth;
        num_bits = iLightCmdIndex + 1;
      }
      ++iLightIndex;
      ++iLightArrayOffset;
    } while (iLightIndex < 3);
  }
  qsort(TrackView, num_bits, 8u, Zcmp);// Fifth phase: Sort render list by Z-depth and render objects
  iRenderObjectIndex = 0;
  if (num_bits > 0) {
    iIndexTmp1 = 144 * iChaseCamIdx_1;
    while (1) {
      pRenderCommand = &TrackView[num_bits - 1 - iRenderObjectIndex];
      fRenderDepth = pRenderCommand->fZDepth;
      iSectionNum = pRenderCommand->nChunkIdx;
      pScreenCoord_1 = NULL;
      pScreenCoord = NULL;
      pCurrentGroundScreen = NULL;
      pNextGroundScreen = NULL;
      if (pRenderCommand->nRenderPriority != 11 && pRenderCommand->nRenderPriority != 14 
        && iSectionNum >= 0 && iSectionNum < MAX_TRACK_CHUNKS //index checks added by ROLLER
        && iNextSectionIndex >= 0 && iNextSectionIndex < MAX_TRACK_CHUNKS) {

        iNextSectionIndex = NextSect[iSectionNum];
        pScreenCoord_1 = &TrackScreenXYZ[iSectionNum];
        pCurrentGroundScreen = &GroundScreenXYZ[iSectionNum];
        pScreenCoord = &TrackScreenXYZ[iNextSectionIndex];
        pNextGroundScreen = &GroundScreenXYZ[iNextSectionIndex];
      }
      switch (pRenderCommand->nRenderPriority) {
        case 0:
        case 8:
          if (TrakColour[iSectionNum][TRAK_COLOUR_LEFT_WALL] < 0)// Render left wall polygon (cases 0 and 8)
          {
            iLeftWallType = TrakColour[iSectionNum][TRAK_COLOUR_LEFT_WALL];
            LWallPoly.uiNumVerts = 4;
            LWallPoly.iSurfaceType = iLeftWallType;
            if (iLeftWallType < 0)
              LWallPoly.iSurfaceType = -iLeftWallType;
            //byLeftWallFlag = BYTE1(LWallPoly.iSurfaceType) | 0x20;
            LWallPoly.iSurfaceType |= SURFACE_FLAG_FLIP_BACKFACE;
            //BYTE1(LWallPoly.iSurfaceType) |= 0x20u;
            if ((textures_off & TEX_OFF_WALL_TEXTURES) != 0) {
              if ((LWallPoly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) != 0) {
                LWallPoly.iSurfaceType = remap_tex[(uint8)(LWallPoly.iSurfaceType)] + (LWallPoly.iSurfaceType & (SURFACE_MASK_FLAGS ^ SURFACE_FLAG_APPLY_TEXTURE));
                goto LABEL_607;
              }
              if ((LWallPoly.iSurfaceType & SURFACE_FLAG_TRANSPARENT) != 0)
                LABEL_606:
              LWallPoly.iSurfaceType = SURFACE_FLAG_SKIP_RENDER;
            LABEL_607:
              LWallPoly.vertices[0] = pScreenCoord->screenPtAy[4].screen;
              LWallPoly.vertices[1] = pScreenCoord_1->screenPtAy[4].screen;
              LWallPoly.vertices[2] = pScreenCoord_1->screenPtAy[1].screen;
              LWallPoly.vertices[3] = pScreenCoord->screenPtAy[1].screen;
              if ((LWallPoly.iSurfaceType & SURFACE_FLAG_TEXTURE_PAIR) != 0 && wide_on) {
                set_starts(1u);
                if (pScreenCoord->screenPtAy[4].projected.fZ >= (double)pScreenCoord_1->screenPtAy[4].projected.fZ)
                  fRoof1InnerDepthAlt = pScreenCoord_1->screenPtAy[4].projected.fZ;
                else
                  fRoof1InnerDepthAlt = pScreenCoord->screenPtAy[4].projected.fZ;
                fObjectDepthE3 = fRoof1InnerDepthAlt;
                if (pScreenCoord_1->screenPtAy[1].projected.fZ >= (double)pScreenCoord->screenPtAy[1].projected.fZ)
                  fWallDepthZ = pScreenCoord->screenPtAy[1].projected.fZ;
                else
                  fWallDepthZ = pScreenCoord_1->screenPtAy[1].projected.fZ;
                fObjectDepthE4 = fWallDepthZ;
                if (fObjectDepthE3 >= (double)fWallDepthZ) {
                  if (pScreenCoord_1->screenPtAy[1].projected.fZ >= (double)pScreenCoord->screenPtAy[1].projected.fZ)
                    fWallZDepthAlt = pScreenCoord->screenPtAy[1].projected.fZ;
                  else
                    fWallZDepthAlt = pScreenCoord_1->screenPtAy[1].projected.fZ;
                  fObjectDepthF1 = fWallZDepthAlt;
                } else {
                  if (pScreenCoord->screenPtAy[4].projected.fZ >= (double)pScreenCoord_1->screenPtAy[4].projected.fZ)
                    fWallZDepthAlt = pScreenCoord_1->screenPtAy[4].projected.fZ;
                  else
                    fWallZDepthAlt = pScreenCoord->screenPtAy[4].projected.fZ;
                  fObjectDepthE6 = fWallZDepthAlt;
                }
                fObjectDepthE5 = fWallZDepthAlt;
                iRenderingTemp = (uint8)Subdivide[iSectionNum].subdivides[3];
                if ((double)(int16)iRenderingTemp * subscale > fWallZDepthAlt) {
                  subdivide(
                    pScrPtr_1,
                    &LWallPoly,
                    pScreenCoord->screenPtAy[4].projected.fX,
                    pScreenCoord->screenPtAy[4].projected.fY,
                    pScreenCoord->screenPtAy[4].projected.fZ,
                    pScreenCoord_1->screenPtAy[4].projected.fX,
                    pScreenCoord_1->screenPtAy[4].projected.fY,
                    pScreenCoord_1->screenPtAy[4].projected.fZ,
                    pScreenCoord_1->screenPtAy[1].projected.fX,
                    pScreenCoord_1->screenPtAy[1].projected.fY,
                    pScreenCoord_1->screenPtAy[1].projected.fZ,
                    pScreenCoord->screenPtAy[1].projected.fX,
                    pScreenCoord->screenPtAy[1].projected.fY,
                    pScreenCoord->screenPtAy[1].projected.fZ,
                    -1,
                    gfx_size);
                  goto LABEL_1271;
                }
                if ((LWallPoly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) == 0)
                  goto LABEL_628;
              LABEL_677:
                POLYTEX(texture_vga, pScrPtr_1, &LWallPoly, 0, gfx_size);
                goto LABEL_1203;
              }
              set_starts(0);
              if (pScreenCoord->screenPtAy[4].projected.fZ >= (double)pScreenCoord_1->screenPtAy[4].projected.fZ)
                fWallInnerDepth = pScreenCoord_1->screenPtAy[4].projected.fZ;
              else
                fWallInnerDepth = pScreenCoord->screenPtAy[4].projected.fZ;
              fObjectDepthF2 = fWallInnerDepth;
              if (pScreenCoord_1->screenPtAy[1].projected.fZ >= (double)pScreenCoord->screenPtAy[1].projected.fZ)
                fWallDepthZ_1 = pScreenCoord->screenPtAy[1].projected.fZ;
              else
                fWallDepthZ_1 = pScreenCoord_1->screenPtAy[1].projected.fZ;
              fObjectDepthF3 = fWallDepthZ_1;
              if (fObjectDepthF2 >= (double)fWallDepthZ_1) {
                if (pScreenCoord_1->screenPtAy[1].projected.fZ >= (double)pScreenCoord->screenPtAy[1].projected.fZ)
                  fWallLeftDepth1 = pScreenCoord->screenPtAy[1].projected.fZ;
                else
                  fWallLeftDepth1 = pScreenCoord_1->screenPtAy[1].projected.fZ;
                fObjectDepthF6 = fWallLeftDepth1;
              } else {
                if (pScreenCoord->screenPtAy[4].projected.fZ >= (double)pScreenCoord_1->screenPtAy[4].projected.fZ)
                  fWallLeftDepth1 = pScreenCoord_1->screenPtAy[4].projected.fZ;
                else
                  fWallLeftDepth1 = pScreenCoord->screenPtAy[4].projected.fZ;
                fObjectDepthF5 = fWallLeftDepth1;
              }
              fObjectDepthF4 = fWallLeftDepth1;
              iRenderingTemp = (uint8)Subdivide[iSectionNum].subdivides[3];
              if ((double)(int16)iRenderingTemp * subscale > fWallLeftDepth1) {
                subdivide(
                  pScrPtr_1,
                  &LWallPoly,
                  pScreenCoord->screenPtAy[4].projected.fX,
                  pScreenCoord->screenPtAy[4].projected.fY,
                  pScreenCoord->screenPtAy[4].projected.fZ,
                  pScreenCoord_1->screenPtAy[4].projected.fX,
                  pScreenCoord_1->screenPtAy[4].projected.fY,
                  pScreenCoord_1->screenPtAy[4].projected.fZ,
                  pScreenCoord_1->screenPtAy[1].projected.fX,
                  pScreenCoord_1->screenPtAy[1].projected.fY,
                  pScreenCoord_1->screenPtAy[1].projected.fZ,
                  pScreenCoord->screenPtAy[1].projected.fX,
                  pScreenCoord->screenPtAy[1].projected.fY,
                  pScreenCoord->screenPtAy[1].projected.fZ,
                  0,
                  gfx_size);
                goto LABEL_1271;
              }
              if ((LWallPoly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) == 0) {
              LABEL_647:
                POLYFLAT(pScrPtr_1, &LWallPoly);
                goto LABEL_1227;
              }
              goto LABEL_697;
            }
            if ((textures_off & TEX_OFF_GLASS_WALLS) == 0 || (LWallPoly.iSurfaceType & SURFACE_FLAG_TRANSPARENT) == 0)
              goto LABEL_607;
            goto LABEL_606;
          }
          LWallPoly.iSurfaceType = TrakColour[iSectionNum][TRAK_COLOUR_LEFT_WALL];
          LWallPoly.uiNumVerts = 4;
          LWallPoly.iSurfaceType |= SURFACE_FLAG_FLIP_BACKFACE;
          //BYTE1(LWallPoly.iSurfaceType) |= 0x20u;
          if ((textures_off & TEX_OFF_WALL_TEXTURES) != 0) {
            if ((LWallPoly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) != 0) {
              LWallPoly.iSurfaceType = remap_tex[(uint8)(LWallPoly.iSurfaceType)] + (LWallPoly.iSurfaceType & (SURFACE_MASK_FLAGS ^ SURFACE_FLAG_APPLY_TEXTURE));
              goto LABEL_656;
            }
            if ((LWallPoly.iSurfaceType & SURFACE_FLAG_TRANSPARENT) != 0)
              LABEL_655:
            LWallPoly.iSurfaceType = SURFACE_FLAG_SKIP_RENDER;
          LABEL_656:
            LWallPoly.vertices[0] = pScreenCoord->screenPtAy[4].screen;
            LWallPoly.vertices[1] = pScreenCoord_1->screenPtAy[4].screen;
            LWallPoly.vertices[2] = pScreenCoord_1->screenPtAy[0].screen;
            LWallPoly.vertices[3] = pScreenCoord->screenPtAy[0].screen;
            if ((LWallPoly.iSurfaceType & SURFACE_FLAG_TEXTURE_PAIR) != 0 && wide_on) {
              set_starts(1u);
              if (pScreenCoord->screenPtAy[4].projected.fZ >= (double)pScreenCoord_1->screenPtAy[4].projected.fZ)
                fWallLeftDepth2 = pScreenCoord_1->screenPtAy[4].projected.fZ;
              else
                fWallLeftDepth2 = pScreenCoord->screenPtAy[4].projected.fZ;
              fObjectDepthG1 = fWallLeftDepth2;
              if (pScreenCoord_1->screenPtAy[0].projected.fZ >= (double)pScreenCoord->screenPtAy[0].projected.fZ)
                fWallLeftDepth3 = pScreenCoord->screenPtAy[0].projected.fZ;
              else
                fWallLeftDepth3 = pScreenCoord_1->screenPtAy[0].projected.fZ;
              fObjectDepthG2 = fWallLeftDepth3;
              if (fObjectDepthG1 >= (double)fWallLeftDepth3) {
                if (pScreenCoord_1->screenPtAy[0].projected.fZ >= (double)pScreenCoord->screenPtAy[0].projected.fZ)
                  fWallLeftDepth4 = pScreenCoord->screenPtAy[0].projected.fZ;
                else
                  fWallLeftDepth4 = pScreenCoord_1->screenPtAy[0].projected.fZ;
                fProjectionTempY1 = fWallLeftDepth4;
              } else {
                if (pScreenCoord->screenPtAy[4].projected.fZ >= (double)pScreenCoord_1->screenPtAy[4].projected.fZ)
                  fWallLeftDepth4 = pScreenCoord_1->screenPtAy[4].projected.fZ;
                else
                  fWallLeftDepth4 = pScreenCoord->screenPtAy[4].projected.fZ;
                fProjectionTempX1 = fWallLeftDepth4;
              }
              fObjectDepthG3 = fWallLeftDepth4;
              iRenderingTemp = (uint8)Subdivide[iSectionNum].subdivides[3];
              if ((double)(int16)iRenderingTemp * subscale > fWallLeftDepth4) {
                subdivide(
                  pScrPtr_1,
                  &LWallPoly,
                  pScreenCoord->screenPtAy[4].projected.fX,
                  pScreenCoord->screenPtAy[4].projected.fY,
                  pScreenCoord->screenPtAy[4].projected.fZ,
                  pScreenCoord_1->screenPtAy[4].projected.fX,
                  pScreenCoord_1->screenPtAy[4].projected.fY,
                  pScreenCoord_1->screenPtAy[4].projected.fZ,
                  pScreenCoord_1->screenPtAy[0].projected.fX,
                  pScreenCoord_1->screenPtAy[0].projected.fY,
                  pScreenCoord_1->screenPtAy[0].projected.fZ,
                  pScreenCoord->screenPtAy[0].projected.fX,
                  pScreenCoord->screenPtAy[0].projected.fY,
                  pScreenCoord->screenPtAy[0].projected.fZ,
                  -1,
                  gfx_size);
                goto LABEL_1271;
              }
              if ((LWallPoly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) == 0) {
              LABEL_628:
                POLYFLAT(pScrPtr_1, &LWallPoly);
                goto LABEL_1203;
              }
              goto LABEL_677;
            }
            set_starts(0);
            if (pScreenCoord->screenPtAy[4].projected.fZ >= (double)pScreenCoord_1->screenPtAy[4].projected.fZ)
              fWallLeftDepth5 = pScreenCoord_1->screenPtAy[4].projected.fZ;
            else
              fWallLeftDepth5 = pScreenCoord->screenPtAy[4].projected.fZ;
            fProjectionTempZ1 = fWallLeftDepth5;
            if (pScreenCoord_1->screenPtAy[0].projected.fZ >= (double)pScreenCoord->screenPtAy[0].projected.fZ)
              fWallLeftDepth6 = pScreenCoord->screenPtAy[0].projected.fZ;
            else
              fWallLeftDepth6 = pScreenCoord_1->screenPtAy[0].projected.fZ;
            fScreenTempX1 = fWallLeftDepth6;
            if (fProjectionTempZ1 >= (double)fWallLeftDepth6) {
              if (pScreenCoord_1->screenPtAy[0].projected.fZ >= (double)pScreenCoord->screenPtAy[0].projected.fZ)
                fWallLeftDepth7 = pScreenCoord->screenPtAy[0].projected.fZ;
              else
                fWallLeftDepth7 = pScreenCoord_1->screenPtAy[0].projected.fZ;
              fScreenTempX2 = fWallLeftDepth7;
            } else {
              if (pScreenCoord->screenPtAy[4].projected.fZ >= (double)pScreenCoord_1->screenPtAy[4].projected.fZ)
                fWallLeftDepth7 = pScreenCoord_1->screenPtAy[4].projected.fZ;
              else
                fWallLeftDepth7 = pScreenCoord->screenPtAy[4].projected.fZ;
              fScreenTempZ1 = fWallLeftDepth7;
            }
            fScreenTempY1 = fWallLeftDepth7;
            iRenderingTemp = (uint8)Subdivide[iSectionNum].subdivides[3];
            if ((double)(int16)iRenderingTemp * subscale <= fWallLeftDepth7) {
              if ((LWallPoly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) == 0)
                goto LABEL_647;
            LABEL_697:
              POLYTEX(texture_vga, pScrPtr_1, &LWallPoly, 0, gfx_size);
              goto LABEL_1227;
            }
            subdivide(
              pScrPtr_1,
              &LWallPoly,
              pScreenCoord->screenPtAy[4].projected.fX,
              pScreenCoord->screenPtAy[4].projected.fY,
              pScreenCoord->screenPtAy[4].projected.fZ,
              pScreenCoord_1->screenPtAy[4].projected.fX,
              pScreenCoord_1->screenPtAy[4].projected.fY,
              pScreenCoord_1->screenPtAy[4].projected.fZ,
              pScreenCoord_1->screenPtAy[0].projected.fX,
              pScreenCoord_1->screenPtAy[0].projected.fY,
              pScreenCoord_1->screenPtAy[0].projected.fZ,
              pScreenCoord->screenPtAy[0].projected.fX,
              pScreenCoord->screenPtAy[0].projected.fY,
              pScreenCoord->screenPtAy[0].projected.fZ,
              0,
              gfx_size);
            goto LABEL_1271;
          }
          if ((textures_off & TEX_OFF_GLASS_WALLS) == 0 || (LWallPoly.iSurfaceType & SURFACE_FLAG_TRANSPARENT) == 0)
            goto LABEL_656;
          goto LABEL_655;
        case 1:
        case 9:
          if (TrakColour[iSectionNum][TRAK_COLOUR_RIGHT_WALL] < 0) {
            iRightWallType = TrakColour[iSectionNum][TRAK_COLOUR_RIGHT_WALL];
            RWallPoly.uiNumVerts = 4;
            RWallPoly.iSurfaceType = iRightWallType;
            if (iRightWallType < 0)
              RWallPoly.iSurfaceType = -iRightWallType;
            //byRightWallFlag = BYTE1(RWallPoly.iSurfaceType) | 0x20;
            //BYTE1(RWallPoly.iSurfaceType) |= 0x20u;
            if ((textures_off & TEX_OFF_WALL_TEXTURES) != 0) {
              if ((RWallPoly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) != 0) {
                RWallPoly.iSurfaceType = remap_tex[(uint8)(RWallPoly.iSurfaceType)] + (RWallPoly.iSurfaceType & (SURFACE_MASK_FLAGS ^ SURFACE_FLAG_APPLY_TEXTURE));
                goto LABEL_710;
              }
              if ((RWallPoly.iSurfaceType & SURFACE_FLAG_TRANSPARENT) != 0)
                LABEL_709:
              RWallPoly.iSurfaceType = SURFACE_FLAG_SKIP_RENDER;
            LABEL_710:
              RWallPoly.vertices[0] = pScreenCoord_1->screenPtAy[5].screen;
              RWallPoly.vertices[1] = pScreenCoord->screenPtAy[5].screen;
              RWallPoly.vertices[2] = pScreenCoord->screenPtAy[2].screen;
              RWallPoly.vertices[3] = pScreenCoord_1->screenPtAy[2].screen;
              if ((RWallPoly.iSurfaceType & SURFACE_FLAG_TEXTURE_PAIR) != 0 && wide_on) {
                set_starts(1u);
                if (pScreenCoord_1->screenPtAy[5].projected.fZ >= (double)pScreenCoord->screenPtAy[5].projected.fZ)
                  fRightWallGeomDepth1 = pScreenCoord->screenPtAy[5].projected.fZ;
                else
                  fRightWallGeomDepth1 = pScreenCoord_1->screenPtAy[5].projected.fZ;
                fProjectionTempX2 = fRightWallGeomDepth1;
                if (pScreenCoord->screenPtAy[2].projected.fZ >= (double)pScreenCoord_1->screenPtAy[2].projected.fZ)
                  fRightWallGeomDepth2 = pScreenCoord_1->screenPtAy[2].projected.fZ;
                else
                  fRightWallGeomDepth2 = pScreenCoord->screenPtAy[2].projected.fZ;
                fScreenTempY2 = fRightWallGeomDepth2;
                if (fProjectionTempX2 >= (double)fRightWallGeomDepth2) {
                  if (pScreenCoord->screenPtAy[2].projected.fZ >= (double)pScreenCoord_1->screenPtAy[2].projected.fZ)
                    fRightWallGeomDepth3 = pScreenCoord_1->screenPtAy[2].projected.fZ;
                  else
                    fRightWallGeomDepth3 = pScreenCoord->screenPtAy[2].projected.fZ;
                  fProjectionTempY2 = fRightWallGeomDepth3;
                } else {
                  if (pScreenCoord_1->screenPtAy[5].projected.fZ >= (double)pScreenCoord->screenPtAy[5].projected.fZ)
                    fRightWallGeomDepth3 = pScreenCoord->screenPtAy[5].projected.fZ;
                  else
                    fRightWallGeomDepth3 = pScreenCoord_1->screenPtAy[5].projected.fZ;
                  fScreenTempX3 = fRightWallGeomDepth3;
                }
                fScreenTempZ2 = fRightWallGeomDepth3;
                iRenderingTemp = (uint8)Subdivide[iSectionNum].subdivides[4];
                if ((double)(int16)iRenderingTemp * subscale > fRightWallGeomDepth3) {
                  subdivide(
                    pScrPtr_1,
                    &RWallPoly,
                    pScreenCoord_1->screenPtAy[5].projected.fX,
                    pScreenCoord_1->screenPtAy[5].projected.fY,
                    pScreenCoord_1->screenPtAy[5].projected.fZ,
                    pScreenCoord->screenPtAy[5].projected.fX,
                    pScreenCoord->screenPtAy[5].projected.fY,
                    pScreenCoord->screenPtAy[5].projected.fZ,
                    pScreenCoord->screenPtAy[2].projected.fX,
                    pScreenCoord->screenPtAy[2].projected.fY,
                    pScreenCoord->screenPtAy[2].projected.fZ,
                    pScreenCoord_1->screenPtAy[2].projected.fX,
                    pScreenCoord_1->screenPtAy[2].projected.fY,
                    pScreenCoord_1->screenPtAy[2].projected.fZ,
                    -1,
                    gfx_size);
                  goto LABEL_1271;
                }
                if ((RWallPoly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) == 0) {
                LABEL_731:
                  POLYFLAT(pScrPtr_1, &RWallPoly);
                  goto LABEL_1203;
                }
              LABEL_780:
                POLYTEX(texture_vga, pScrPtr_1, &RWallPoly, 0, gfx_size);
                goto LABEL_1203;
              }
              set_starts(0);
              if (pScreenCoord_1->screenPtAy[5].projected.fZ >= (double)pScreenCoord->screenPtAy[5].projected.fZ)
                fRightWallGeomDepth4 = pScreenCoord->screenPtAy[5].projected.fZ;
              else
                fRightWallGeomDepth4 = pScreenCoord_1->screenPtAy[5].projected.fZ;
              fProjectionTempZ2 = fRightWallGeomDepth4;
              if (pScreenCoord->screenPtAy[2].projected.fZ >= (double)pScreenCoord_1->screenPtAy[2].projected.fZ)
                fRightWallGeomDepth5 = pScreenCoord_1->screenPtAy[2].projected.fZ;
              else
                fRightWallGeomDepth5 = pScreenCoord->screenPtAy[2].projected.fZ;
              fProjectionTempX3 = fRightWallGeomDepth5;
              if (fProjectionTempZ2 >= (double)fRightWallGeomDepth5) {
                if (pScreenCoord->screenPtAy[2].projected.fZ >= (double)pScreenCoord_1->screenPtAy[2].projected.fZ)
                  fRightWallGeomDepth6 = pScreenCoord_1->screenPtAy[2].projected.fZ;
                else
                  fRightWallGeomDepth6 = pScreenCoord->screenPtAy[2].projected.fZ;
                fScreenTempY3 = fRightWallGeomDepth6;
              } else {
                if (pScreenCoord_1->screenPtAy[5].projected.fZ >= (double)pScreenCoord->screenPtAy[5].projected.fZ)
                  fRightWallGeomDepth6 = pScreenCoord->screenPtAy[5].projected.fZ;
                else
                  fRightWallGeomDepth6 = pScreenCoord_1->screenPtAy[5].projected.fZ;
                fProjectionTempZ3 = fRightWallGeomDepth6;
              }
              fProjectionTempY3 = fRightWallGeomDepth6;
              iRenderingTemp = (uint8)Subdivide[iSectionNum].subdivides[4];
              if ((double)(int16)iRenderingTemp * subscale > fRightWallGeomDepth6) {
                subdivide(
                  pScrPtr_1,
                  &RWallPoly,
                  pScreenCoord_1->screenPtAy[5].projected.fX,
                  pScreenCoord_1->screenPtAy[5].projected.fY,
                  pScreenCoord_1->screenPtAy[5].projected.fZ,
                  pScreenCoord->screenPtAy[5].projected.fX,
                  pScreenCoord->screenPtAy[5].projected.fY,
                  pScreenCoord->screenPtAy[5].projected.fZ,
                  pScreenCoord->screenPtAy[2].projected.fX,
                  pScreenCoord->screenPtAy[2].projected.fY,
                  pScreenCoord->screenPtAy[2].projected.fZ,
                  pScreenCoord_1->screenPtAy[2].projected.fX,
                  pScreenCoord_1->screenPtAy[2].projected.fY,
                  pScreenCoord_1->screenPtAy[2].projected.fZ,
                  0,
                  gfx_size);
                goto LABEL_1271;
              }
              if ((RWallPoly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) == 0) {
              LABEL_750:
                POLYFLAT(pScrPtr_1, &RWallPoly);
                goto LABEL_1227;
              }
              goto LABEL_800;
            }
            if ((textures_off & TEX_OFF_GLASS_WALLS) == 0 || (RWallPoly.iSurfaceType & SURFACE_FLAG_TRANSPARENT) == 0)
              goto LABEL_710;
            goto LABEL_709;
          }
          RWallPoly.iSurfaceType = TrakColour[iSectionNum][TRAK_COLOUR_RIGHT_WALL];
          RWallPoly.uiNumVerts = 4;
          //byWallTypeFlag = BYTE1(RWallPoly.iSurfaceType) | 0x20;
          RWallPoly.iSurfaceType |= SURFACE_FLAG_FLIP_BACKFACE;
          //BYTE1(RWallPoly.iSurfaceType) |= 0x20u;
          if ((textures_off & TEX_OFF_WALL_TEXTURES) == 0) {
            if ((textures_off & TEX_OFF_GLASS_WALLS) == 0 || (RWallPoly.iSurfaceType & SURFACE_FLAG_TRANSPARENT) == 0)
              goto LABEL_759;
          LABEL_758:
            RWallPoly.iSurfaceType = SURFACE_FLAG_SKIP_RENDER;
            goto LABEL_759;
          }
          if ((RWallPoly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) != 0) {
            RWallPoly.iSurfaceType = remap_tex[(uint8)(RWallPoly.iSurfaceType)] + (RWallPoly.iSurfaceType & (SURFACE_MASK_FLAGS ^ SURFACE_FLAG_APPLY_TEXTURE));
            goto LABEL_759;
          }
          if ((RWallPoly.iSurfaceType & SURFACE_FLAG_TRANSPARENT) != 0)
            goto LABEL_758;
        LABEL_759:
          RWallPoly.vertices[0] = pScreenCoord_1->screenPtAy[5].screen;
          RWallPoly.vertices[1] = pScreenCoord->screenPtAy[5].screen;
          RWallPoly.vertices[2] = pScreenCoord->screenPtAy[3].screen;
          RWallPoly.vertices[3] = pScreenCoord_1->screenPtAy[3].screen;
          if ((RWallPoly.iSurfaceType & SURFACE_FLAG_TEXTURE_PAIR) != 0 && wide_on) {
            set_starts(1u);
            if (pScreenCoord_1->screenPtAy[5].projected.fZ >= (double)pScreenCoord->screenPtAy[5].projected.fZ)
              fGeometryDepth1 = pScreenCoord->screenPtAy[5].projected.fZ;
            else
              fGeometryDepth1 = pScreenCoord_1->screenPtAy[5].projected.fZ;
            fProjectionTempX4 = fGeometryDepth1;
            if (pScreenCoord->screenPtAy[3].projected.fZ >= (double)pScreenCoord_1->screenPtAy[3].projected.fZ)
              fGeometryDepth2 = pScreenCoord_1->screenPtAy[3].projected.fZ;
            else
              fGeometryDepth2 = pScreenCoord->screenPtAy[3].projected.fZ;
            fScreenTempZ3 = fGeometryDepth2;
            if (fProjectionTempX4 >= (double)fGeometryDepth2) {
              if (pScreenCoord->screenPtAy[3].projected.fZ >= (double)pScreenCoord_1->screenPtAy[3].projected.fZ)
                fGeometryDepth3 = pScreenCoord_1->screenPtAy[3].projected.fZ;
              else
                fGeometryDepth3 = pScreenCoord->screenPtAy[3].projected.fZ;
              fScreenTempZ4 = fGeometryDepth3;
            } else {
              if (pScreenCoord_1->screenPtAy[5].projected.fZ >= (double)pScreenCoord->screenPtAy[5].projected.fZ)
                fGeometryDepth3 = pScreenCoord->screenPtAy[5].projected.fZ;
              else
                fGeometryDepth3 = pScreenCoord_1->screenPtAy[5].projected.fZ;
              fScreenTempY4 = fGeometryDepth3;
            }
            fScreenTempX4 = fGeometryDepth3;
            iRenderingTemp = (uint8)Subdivide[iSectionNum].subdivides[4];
            if ((double)(int16)iRenderingTemp * subscale > fGeometryDepth3) {
              subdivide(
                pScrPtr_1,
                &RWallPoly,
                pScreenCoord_1->screenPtAy[5].projected.fX,
                pScreenCoord_1->screenPtAy[5].projected.fY,
                pScreenCoord_1->screenPtAy[5].projected.fZ,
                pScreenCoord->screenPtAy[5].projected.fX,
                pScreenCoord->screenPtAy[5].projected.fY,
                pScreenCoord->screenPtAy[5].projected.fZ,
                pScreenCoord->screenPtAy[3].projected.fX,
                pScreenCoord->screenPtAy[3].projected.fY,
                pScreenCoord->screenPtAy[3].projected.fZ,
                pScreenCoord_1->screenPtAy[3].projected.fX,
                pScreenCoord_1->screenPtAy[3].projected.fY,
                pScreenCoord_1->screenPtAy[3].projected.fZ,
                -1,
                gfx_size);
              goto LABEL_1271;
            }
            if ((RWallPoly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) == 0)
              goto LABEL_731;
            goto LABEL_780;
          }
          set_starts(0);
          if (pScreenCoord_1->screenPtAy[5].projected.fZ >= (double)pScreenCoord->screenPtAy[5].projected.fZ)
            fGeometryDepth4 = pScreenCoord->screenPtAy[5].projected.fZ;
          else
            fGeometryDepth4 = pScreenCoord_1->screenPtAy[5].projected.fZ;
          fScreenTempX5 = fGeometryDepth4;
          if (pScreenCoord->screenPtAy[3].projected.fZ >= (double)pScreenCoord_1->screenPtAy[3].projected.fZ)
            fGeometryDepth5 = pScreenCoord_1->screenPtAy[3].projected.fZ;
          else
            fGeometryDepth5 = pScreenCoord->screenPtAy[3].projected.fZ;
          fScreenTempY5 = fGeometryDepth5;
          if (fScreenTempX5 >= (double)fGeometryDepth5) {
            if (pScreenCoord->screenPtAy[3].projected.fZ >= (double)pScreenCoord_1->screenPtAy[3].projected.fZ)
              fGeometryDepth6 = pScreenCoord_1->screenPtAy[3].projected.fZ;
            else
              fGeometryDepth6 = pScreenCoord->screenPtAy[3].projected.fZ;
            fScreenTempY6 = fGeometryDepth6;
          } else {
            if (pScreenCoord_1->screenPtAy[5].projected.fZ >= (double)pScreenCoord->screenPtAy[5].projected.fZ)
              fGeometryDepth6 = pScreenCoord->screenPtAy[5].projected.fZ;
            else
              fGeometryDepth6 = pScreenCoord_1->screenPtAy[5].projected.fZ;
            fScreenTempX6 = fGeometryDepth6;
          }
          fScreenTempZ5 = fGeometryDepth6;
          iRenderingTemp = (uint8)Subdivide[iSectionNum].subdivides[4];
          if ((double)(int16)iRenderingTemp * subscale <= fGeometryDepth6) {
            if ((RWallPoly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) == 0)
              goto LABEL_750;
          LABEL_800:
            POLYTEX(texture_vga, pScrPtr_1, &RWallPoly, 0, gfx_size);
            goto LABEL_1227;
          }
          subdivide(
            pScrPtr_1,
            &RWallPoly,
            pScreenCoord_1->screenPtAy[5].projected.fX,
            pScreenCoord_1->screenPtAy[5].projected.fY,
            pScreenCoord_1->screenPtAy[5].projected.fZ,
            pScreenCoord->screenPtAy[5].projected.fX,
            pScreenCoord->screenPtAy[5].projected.fY,
            pScreenCoord->screenPtAy[5].projected.fZ,
            pScreenCoord->screenPtAy[3].projected.fX,
            pScreenCoord->screenPtAy[3].projected.fY,
            pScreenCoord->screenPtAy[3].projected.fZ,
            pScreenCoord_1->screenPtAy[3].projected.fX,
            pScreenCoord_1->screenPtAy[3].projected.fY,
            pScreenCoord_1->screenPtAy[3].projected.fZ,
            0,
            gfx_size);
        LABEL_1271:
          if (++iRenderObjectIndex >= num_bits)
            return;
          break;
        case 2:
          if (!facing_ok(
            pNextGroundScreen->screenPtAy[2].projected.fX,
            pNextGroundScreen->screenPtAy[2].projected.fY,
            pNextGroundScreen->screenPtAy[2].projected.fZ,
            pCurrentGroundScreen->screenPtAy[2].projected.fX,
            pCurrentGroundScreen->screenPtAy[2].projected.fY,
            pCurrentGroundScreen->screenPtAy[2].projected.fZ,
            pCurrentGroundScreen->screenPtAy[3].projected.fX,
            pCurrentGroundScreen->screenPtAy[3].projected.fY,
            pCurrentGroundScreen->screenPtAy[3].projected.fZ,
            pNextGroundScreen->screenPtAy[3].projected.fX,
            pNextGroundScreen->screenPtAy[3].projected.fY,
            pNextGroundScreen->screenPtAy[3].projected.fZ))
            goto LABEL_1271;
          G3Poly.iSurfaceType = GroundColour[iSectionNum][GROUND_COLOUR_OFLOOR];
          G3Poly.vertices[0] = pNextGroundScreen->screenPtAy[3].screen;
          G3Poly.vertices[1] = pNextGroundScreen->screenPtAy[2].screen;
          G3Poly.vertices[2] = pCurrentGroundScreen->screenPtAy[2].screen;
          G3Poly.vertices[3] = pCurrentGroundScreen->screenPtAy[3].screen;
          G3Poly.uiNumVerts = 4;
          if ((G3Poly.iSurfaceType & SURFACE_FLAG_TEXTURE_PAIR) != 0) {
            set_starts(1u);
            if ((textures_off & TEX_OFF_GROUND_TEXTURES) != 0 && (G3Poly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) != 0)
              G3Poly.iSurfaceType = remap_tex[(uint8)(G3Poly.iSurfaceType)] + (G3Poly.iSurfaceType & (SURFACE_MASK_FLAGS ^ SURFACE_FLAG_APPLY_TEXTURE));
            if (pNextGroundScreen->screenPtAy[3].projected.fZ >= (double)pNextGroundScreen->screenPtAy[2].projected.fZ)
              fTrackDepth16 = pNextGroundScreen->screenPtAy[2].projected.fZ;
            else
              fTrackDepth16 = pNextGroundScreen->screenPtAy[3].projected.fZ;
            fProjectionTempZ11 = fTrackDepth16;
            if (pCurrentGroundScreen->screenPtAy[2].projected.fZ >= (double)pCurrentGroundScreen->screenPtAy[3].projected.fZ)
              fTrackDepth17 = pCurrentGroundScreen->screenPtAy[3].projected.fZ;
            else
              fTrackDepth17 = pCurrentGroundScreen->screenPtAy[2].projected.fZ;
            fProjectionTempX12 = fTrackDepth17;
            if (fProjectionTempZ11 >= (double)fTrackDepth17) {
              if (pCurrentGroundScreen->screenPtAy[2].projected.fZ >= (double)pCurrentGroundScreen->screenPtAy[3].projected.fZ)
                fTrackDepth18 = pCurrentGroundScreen->screenPtAy[3].projected.fZ;
              else
                fTrackDepth18 = pCurrentGroundScreen->screenPtAy[2].projected.fZ;
              fScreenTempY12 = fTrackDepth18;
            } else {
              if (pNextGroundScreen->screenPtAy[3].projected.fZ >= (double)pNextGroundScreen->screenPtAy[2].projected.fZ)
                fTrackDepth18 = pNextGroundScreen->screenPtAy[2].projected.fZ;
              else
                fTrackDepth18 = pNextGroundScreen->screenPtAy[3].projected.fZ;
              fScreenTempX12 = fTrackDepth18;
            }
            fProjectionTempY12 = fTrackDepth18;
            iRenderingTemp = (uint8)Subdivide[iSectionNum].subdivides[8];
            if ((double)(int16)iRenderingTemp * subscale > fTrackDepth18) {
              subdivide(
                pScrPtr_1,
                &G3Poly,
                pNextGroundScreen->screenPtAy[3].projected.fX,
                pNextGroundScreen->screenPtAy[3].projected.fY,
                pNextGroundScreen->screenPtAy[3].projected.fZ,
                pNextGroundScreen->screenPtAy[2].projected.fX,
                pNextGroundScreen->screenPtAy[2].projected.fY,
                pNextGroundScreen->screenPtAy[2].projected.fZ,
                pCurrentGroundScreen->screenPtAy[2].projected.fX,
                pCurrentGroundScreen->screenPtAy[2].projected.fY,
                pCurrentGroundScreen->screenPtAy[2].projected.fZ,
                pCurrentGroundScreen->screenPtAy[3].projected.fX,
                pCurrentGroundScreen->screenPtAy[3].projected.fY,
                pCurrentGroundScreen->screenPtAy[3].projected.fZ,
                -1,
                gfx_size);
              goto LABEL_1271;
            }
            if ((G3Poly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) != 0)
              POLYTEX(texture_vga, pScrPtr_1, &G3Poly, 0, gfx_size);
            else
              POLYFLAT(pScrPtr_1, &G3Poly);
            goto LABEL_1203;
          }
          if ((textures_off & TEX_OFF_GROUND_TEXTURES) != 0 && (G3Poly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) != 0)
            G3Poly.iSurfaceType = (G3Poly.iSurfaceType & (SURFACE_MASK_FLAGS ^ SURFACE_FLAG_APPLY_TEXTURE)) + remap_tex[(uint8)(G3Poly.iSurfaceType)];
          set_starts(0);
          if (pNextGroundScreen->screenPtAy[3].projected.fZ >= (double)pNextGroundScreen->screenPtAy[2].projected.fZ)
            fTrackDepth19 = pNextGroundScreen->screenPtAy[2].projected.fZ;
          else
            fTrackDepth19 = pNextGroundScreen->screenPtAy[3].projected.fZ;
          fScreenTempZ12 = fTrackDepth19;
          if (pCurrentGroundScreen->screenPtAy[2].projected.fZ >= (double)pCurrentGroundScreen->screenPtAy[3].projected.fZ)
            fTrackDepth20 = pCurrentGroundScreen->screenPtAy[3].projected.fZ;
          else
            fTrackDepth20 = pCurrentGroundScreen->screenPtAy[2].projected.fZ;
          fProjectionTempZ12 = fTrackDepth20;
          if (fScreenTempZ12 >= (double)fTrackDepth20) {
            if (pCurrentGroundScreen->screenPtAy[2].projected.fZ >= (double)pCurrentGroundScreen->screenPtAy[3].projected.fZ)
              fTrackDepth21 = pCurrentGroundScreen->screenPtAy[3].projected.fZ;
            else
              fTrackDepth21 = pCurrentGroundScreen->screenPtAy[2].projected.fZ;
            fProjectionTempZ13 = fTrackDepth21;
          } else {
            if (pNextGroundScreen->screenPtAy[3].projected.fZ >= (double)pNextGroundScreen->screenPtAy[2].projected.fZ)
              fTrackDepth21 = pNextGroundScreen->screenPtAy[2].projected.fZ;
            else
              fTrackDepth21 = pNextGroundScreen->screenPtAy[3].projected.fZ;
            fProjectionTempY13 = fTrackDepth21;
          }
          fProjectionTempX13 = fTrackDepth21;
          iRenderingTemp = (uint8)Subdivide[iSectionNum].subdivides[8];
          if ((double)(int16)iRenderingTemp * subscale > fTrackDepth21) {
            subdivide(
              pScrPtr_1,
              &G3Poly,
              pNextGroundScreen->screenPtAy[3].projected.fX,
              pNextGroundScreen->screenPtAy[3].projected.fY,
              pNextGroundScreen->screenPtAy[3].projected.fZ,
              pNextGroundScreen->screenPtAy[2].projected.fX,
              pNextGroundScreen->screenPtAy[2].projected.fY,
              pNextGroundScreen->screenPtAy[2].projected.fZ,
              pCurrentGroundScreen->screenPtAy[2].projected.fX,
              pCurrentGroundScreen->screenPtAy[2].projected.fY,
              pCurrentGroundScreen->screenPtAy[2].projected.fZ,
              pCurrentGroundScreen->screenPtAy[3].projected.fX,
              pCurrentGroundScreen->screenPtAy[3].projected.fY,
              pCurrentGroundScreen->screenPtAy[3].projected.fZ,
              0,
              gfx_size);
            goto LABEL_1271;
          }
          if ((G3Poly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) != 0)
            POLYTEX(texture_vga, pScrPtr_1, &G3Poly, 0, gfx_size);
          else
            POLYFLAT(pScrPtr_1, &G3Poly);
          goto LABEL_1227;
        case 3:
          if (!facing_ok(
            pNextGroundScreen->screenPtAy[0].projected.fX,
            pNextGroundScreen->screenPtAy[0].projected.fY,
            pNextGroundScreen->screenPtAy[0].projected.fZ,
            pCurrentGroundScreen->screenPtAy[0].projected.fX,
            pCurrentGroundScreen->screenPtAy[0].projected.fY,
            pCurrentGroundScreen->screenPtAy[0].projected.fZ,
            pCurrentGroundScreen->screenPtAy[1].projected.fX,
            pCurrentGroundScreen->screenPtAy[1].projected.fY,
            pCurrentGroundScreen->screenPtAy[1].projected.fZ,
            pNextGroundScreen->screenPtAy[1].projected.fX,
            pNextGroundScreen->screenPtAy[1].projected.fY,
            pNextGroundScreen->screenPtAy[1].projected.fZ)
            && (GroundColour[iSectionNum][GROUND_COLOUR_LUOWALL] & SURFACE_FLAG_CONCAVE) == 0) {
            goto LABEL_1068;
          }
          G1Poly.uiNumVerts = 4;
          G1Poly.iSurfaceType = GroundColour[iSectionNum][GROUND_COLOUR_LUOWALL];
          G1Poly.vertices[0] = pNextGroundScreen->screenPtAy[0].screen;
          G1Poly.vertices[1] = pCurrentGroundScreen->screenPtAy[0].screen;
          G1Poly.vertices[2] = pCurrentGroundScreen->screenPtAy[1].screen;
          G1Poly.vertices[3] = pNextGroundScreen->screenPtAy[1].screen;
          if (G1Poly.iSurfaceType == -1 || GroundColour[iSectionNum][GROUND_COLOUR_OFLOOR] == -1)
            goto LABEL_1068;
          if ((G1Poly.iSurfaceType & SURFACE_FLAG_TEXTURE_PAIR) == 0) {
            if ((textures_off & TEX_OFF_GROUND_TEXTURES) != 0 && (G1Poly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) != 0)
              G1Poly.iSurfaceType = remap_tex[(uint8)(G1Poly.iSurfaceType)] + (G1Poly.iSurfaceType & (SURFACE_MASK_FLAGS ^ SURFACE_FLAG_APPLY_TEXTURE));
            set_starts(0);
            if (pNextGroundScreen->screenPtAy[0].projected.fZ >= (double)pCurrentGroundScreen->screenPtAy[0].projected.fZ)
              pTrackScreenPtr5 = pCurrentGroundScreen;
            else
              pTrackScreenPtr5 = pNextGroundScreen;
            fRightRoadTmp1 = pTrackScreenPtr5->screenPtAy[0].projected.fZ;
            if (pCurrentGroundScreen->screenPtAy[1].projected.fZ >= (double)pNextGroundScreen->screenPtAy[1].projected.fZ)
              pTrackScreenPtr6 = pNextGroundScreen;
            else
              pTrackScreenPtr6 = pCurrentGroundScreen;
            fRightRoadTmp2 = pTrackScreenPtr6->screenPtAy[1].projected.fZ;
            if (fRightRoadTmp1 >= (double)fRightRoadTmp2) {
              if (pCurrentGroundScreen->screenPtAy[1].projected.fZ >= (double)pNextGroundScreen->screenPtAy[1].projected.fZ)
                pTrackScreenPtr8 = pNextGroundScreen;
              else
                pTrackScreenPtr8 = pCurrentGroundScreen;
              fSurfaceTmp3 = pTrackScreenPtr8->screenPtAy[1].projected.fZ;
              fScreenDepth2 = fSurfaceTmp3;
            } else {
              if (pNextGroundScreen->screenPtAy[0].projected.fZ >= (double)pCurrentGroundScreen->screenPtAy[0].projected.fZ)
                pTrackScreenPtr7 = pCurrentGroundScreen;
              else
                pTrackScreenPtr7 = pNextGroundScreen;
              fSurfaceTmp2 = pTrackScreenPtr7->screenPtAy[0].projected.fZ;
              fScreenDepth2 = fSurfaceTmp2;
            }
            fSurfaceTmp1 = fScreenDepth2;
            iRenderingTemp = (uint8)Subdivide[iSectionNum].subdivides[6];
            if ((double)(int16)iRenderingTemp * subscale > fScreenDepth2) {
              subdivide(
                pScrPtr_1,
                &G1Poly,
                pNextGroundScreen->screenPtAy[0].projected.fX,
                pNextGroundScreen->screenPtAy[0].projected.fY,
                pNextGroundScreen->screenPtAy[0].projected.fZ,
                pCurrentGroundScreen->screenPtAy[0].projected.fX,
                pCurrentGroundScreen->screenPtAy[0].projected.fY,
                pCurrentGroundScreen->screenPtAy[0].projected.fZ,
                pCurrentGroundScreen->screenPtAy[1].projected.fX,
                pCurrentGroundScreen->screenPtAy[1].projected.fY,
                pCurrentGroundScreen->screenPtAy[1].projected.fZ,
                pNextGroundScreen->screenPtAy[1].projected.fX,
                pNextGroundScreen->screenPtAy[1].projected.fY,
                pNextGroundScreen->screenPtAy[1].projected.fZ,
                0,
                gfx_size);
              goto LABEL_1068;
            }
            if ((G1Poly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) != 0)
              POLYTEX(texture_vga, pScrPtr_1, &G1Poly, 0, gfx_size);
            else
              POLYFLAT(pScrPtr_1, &G1Poly);
          } else {
            set_starts(1u);
            if ((textures_off & TEX_OFF_GROUND_TEXTURES) != 0 && (G1Poly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) != 0)
              G1Poly.iSurfaceType = (G1Poly.iSurfaceType & (SURFACE_MASK_FLAGS ^ SURFACE_FLAG_APPLY_TEXTURE)) + remap_tex[(uint8)(G1Poly.iSurfaceType)];
            if (pNextGroundScreen->screenPtAy[0].projected.fZ >= (double)pCurrentGroundScreen->screenPtAy[0].projected.fZ)
              pTrackScreenPtr1 = pCurrentGroundScreen;
            else
              pTrackScreenPtr1 = pNextGroundScreen;
            fProjectionTmp1 = pTrackScreenPtr1->screenPtAy[0].projected.fZ;
            if (pCurrentGroundScreen->screenPtAy[1].projected.fZ >= (double)pNextGroundScreen->screenPtAy[1].projected.fZ)
              pTrackScreenPtr2 = pNextGroundScreen;
            else
              pTrackScreenPtr2 = pCurrentGroundScreen;
            fProjectionTmp2 = pTrackScreenPtr2->screenPtAy[1].projected.fZ;
            if (fProjectionTmp1 >= (double)fProjectionTmp2) {
              if (pCurrentGroundScreen->screenPtAy[1].projected.fZ >= (double)pNextGroundScreen->screenPtAy[1].projected.fZ)
                pTrackScreenPtr4 = pNextGroundScreen;
              else
                pTrackScreenPtr4 = pCurrentGroundScreen;
              fProjectionTempFinal = pTrackScreenPtr4->screenPtAy[1].projected.fZ;
              fScreenDepth1 = fProjectionTempFinal;
            } else {
              if (pNextGroundScreen->screenPtAy[0].projected.fZ >= (double)pCurrentGroundScreen->screenPtAy[0].projected.fZ)
                pTrackScreenPtr3 = pCurrentGroundScreen;
              else
                pTrackScreenPtr3 = pNextGroundScreen;
              fProjectionTmp4 = pTrackScreenPtr3->screenPtAy[0].projected.fZ;
              fScreenDepth1 = fProjectionTmp4;
            }
            fProjectionTmp3 = fScreenDepth1;
            iRenderingTemp = (uint8)Subdivide[iSectionNum].subdivides[6];
            if ((double)(int16)iRenderingTemp * subscale > fScreenDepth1) {
              subdivide(
                pScrPtr_1,
                &G1Poly,
                pNextGroundScreen->screenPtAy[0].projected.fX,
                pNextGroundScreen->screenPtAy[0].projected.fY,
                pNextGroundScreen->screenPtAy[0].projected.fZ,
                pCurrentGroundScreen->screenPtAy[0].projected.fX,
                pCurrentGroundScreen->screenPtAy[0].projected.fY,
                pCurrentGroundScreen->screenPtAy[0].projected.fZ,
                pCurrentGroundScreen->screenPtAy[1].projected.fX,
                pCurrentGroundScreen->screenPtAy[1].projected.fY,
                pCurrentGroundScreen->screenPtAy[1].projected.fZ,
                pNextGroundScreen->screenPtAy[1].projected.fX,
                pNextGroundScreen->screenPtAy[1].projected.fY,
                pNextGroundScreen->screenPtAy[1].projected.fZ,
                -1,
                gfx_size);
              goto LABEL_1068;
            }
            if ((G1Poly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) != 0)
              POLYTEX(texture_vga, pScrPtr_1, &G1Poly, 0, gfx_size);
            else
              POLYFLAT(pScrPtr_1, &G1Poly);
            set_starts(0);
          }
          set_starts(0);
        LABEL_1068:
          if (!facing_ok(
            pNextGroundScreen->screenPtAy[1].projected.fX,
            pNextGroundScreen->screenPtAy[1].projected.fY,
            pNextGroundScreen->screenPtAy[1].projected.fZ,
            pCurrentGroundScreen->screenPtAy[1].projected.fX,
            pCurrentGroundScreen->screenPtAy[1].projected.fY,
            pCurrentGroundScreen->screenPtAy[1].projected.fZ,
            pCurrentGroundScreen->screenPtAy[2].projected.fX,
            pCurrentGroundScreen->screenPtAy[2].projected.fY,
            pCurrentGroundScreen->screenPtAy[2].projected.fZ,
            pNextGroundScreen->screenPtAy[2].projected.fX,
            pNextGroundScreen->screenPtAy[2].projected.fY,
            pNextGroundScreen->screenPtAy[2].projected.fZ)
            && (GroundColour[iSectionNum][GROUND_COLOUR_LLOWALL] & SURFACE_FLAG_CONCAVE) == 0) {
            goto LABEL_1271;
          }
          G2Poly.iSurfaceType = GroundColour[iSectionNum][GROUND_COLOUR_LLOWALL];
          G2Poly.vertices[0] = pNextGroundScreen->screenPtAy[1].screen;
          G2Poly.vertices[1] = pCurrentGroundScreen->screenPtAy[1].screen;
          G2Poly.vertices[2] = pCurrentGroundScreen->screenPtAy[2].screen;
          G2Poly.vertices[3].x = pNextGroundScreen->screenPtAy[2].screen.x;
          G2Poly.uiNumVerts = 4;
          G2Poly.vertices[3].y = pNextGroundScreen->screenPtAy[2].screen.y;
          if (G2Poly.iSurfaceType == -1 || GroundColour[iSectionNum][GROUND_COLOUR_OFLOOR] == -1)
            goto LABEL_1271;
          if ((G2Poly.iSurfaceType & SURFACE_FLAG_TEXTURE_PAIR) != 0) {
            if(G2Poly.iSurfaceType == 65854)
              G2Poly.iSurfaceType += 1;
            set_starts(1u);
            if ((textures_off & TEX_OFF_GROUND_TEXTURES) != 0 && (G2Poly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) != 0)
              G2Poly.iSurfaceType = remap_tex[(uint8)(G2Poly.iSurfaceType)] + (G2Poly.iSurfaceType & (SURFACE_MASK_FLAGS ^ SURFACE_FLAG_APPLY_TEXTURE));
            if (pNextGroundScreen->screenPtAy[1].projected.fZ >= (double)pCurrentGroundScreen->screenPtAy[1].projected.fZ)
              pTrackScreenPtr9 = pCurrentGroundScreen;
            else
              pTrackScreenPtr9 = pNextGroundScreen;
            fSurfaceTmp4 = pTrackScreenPtr9->screenPtAy[1].projected.fZ;
            if (pCurrentGroundScreen->screenPtAy[2].projected.fZ >= (double)pNextGroundScreen->screenPtAy[2].projected.fZ)
              pTrackScreenPtr10 = pNextGroundScreen;
            else
              pTrackScreenPtr10 = pCurrentGroundScreen;
            fSurfaceTmp5 = pTrackScreenPtr10->screenPtAy[2].projected.fZ;
            if (fSurfaceTmp4 >= (double)fSurfaceTmp5) {
              if (pCurrentGroundScreen->screenPtAy[2].projected.fZ >= (double)pNextGroundScreen->screenPtAy[2].projected.fZ)
                pTrackScreenPtr12 = pNextGroundScreen;
              else
                pTrackScreenPtr12 = pCurrentGroundScreen;
              fSurfaceTmp8 = pTrackScreenPtr12->screenPtAy[2].projected.fZ;
              fScreenDepth3 = fSurfaceTmp8;
            } else {
              if (pNextGroundScreen->screenPtAy[1].projected.fZ >= (double)pCurrentGroundScreen->screenPtAy[1].projected.fZ)
                pTrackScreenPtr11 = pCurrentGroundScreen;
              else
                pTrackScreenPtr11 = pNextGroundScreen;
              fSurfaceTmp7 = pTrackScreenPtr11->screenPtAy[1].projected.fZ;
              fScreenDepth3 = fSurfaceTmp7;
            }
            fSurfaceTmp6 = fScreenDepth3;
            iRenderingTemp = (uint8)Subdivide[iSectionNum].subdivides[7];
            if ((double)(int16)iRenderingTemp * subscale > fScreenDepth3) {
              subdivide(
                pScrPtr_1,
                &G2Poly,
                pNextGroundScreen->screenPtAy[1].projected.fX,
                pNextGroundScreen->screenPtAy[1].projected.fY,
                pNextGroundScreen->screenPtAy[1].projected.fZ,
                pCurrentGroundScreen->screenPtAy[1].projected.fX,
                pCurrentGroundScreen->screenPtAy[1].projected.fY,
                pCurrentGroundScreen->screenPtAy[1].projected.fZ,
                pCurrentGroundScreen->screenPtAy[2].projected.fX,
                pCurrentGroundScreen->screenPtAy[2].projected.fY,
                pCurrentGroundScreen->screenPtAy[2].projected.fZ,
                pNextGroundScreen->screenPtAy[2].projected.fX,
                pNextGroundScreen->screenPtAy[2].projected.fY,
                pNextGroundScreen->screenPtAy[2].projected.fZ,
                -1,
                gfx_size);
              goto LABEL_1271;
            }
            if ((G2Poly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) != 0)
              POLYTEX(texture_vga, pScrPtr_1, &G2Poly, 0, gfx_size);
            else
              POLYFLAT(pScrPtr_1, &G2Poly);
            goto LABEL_1203;
          }
          if ((textures_off & TEX_OFF_GROUND_TEXTURES) != 0 && (G2Poly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) != 0)
            G2Poly.iSurfaceType = remap_tex[(uint8)(G2Poly.iSurfaceType)] + (G2Poly.iSurfaceType & (SURFACE_MASK_FLAGS ^ SURFACE_FLAG_APPLY_TEXTURE));
          set_starts(0);
          if (pNextGroundScreen->screenPtAy[1].projected.fZ >= (double)pCurrentGroundScreen->screenPtAy[1].projected.fZ)
            pTrackScreenPtr13 = pCurrentGroundScreen;
          else
            pTrackScreenPtr13 = pNextGroundScreen;
          fSurfaceTmp9 = pTrackScreenPtr13->screenPtAy[1].projected.fZ;
          if (pCurrentGroundScreen->screenPtAy[2].projected.fZ >= (double)pNextGroundScreen->screenPtAy[2].projected.fZ)
            pTrackScreenPtr14 = pNextGroundScreen;
          else
            pTrackScreenPtr14 = pCurrentGroundScreen;
          fSurfaceTmp10 = pTrackScreenPtr14->screenPtAy[2].projected.fZ;
          if (fSurfaceTmp9 >= (double)fSurfaceTmp10) {
            if (pCurrentGroundScreen->screenPtAy[2].projected.fZ >= (double)pNextGroundScreen->screenPtAy[2].projected.fZ)
              pTrackScreenPtr16 = pNextGroundScreen;
            else
              pTrackScreenPtr16 = pCurrentGroundScreen;
            fSurfaceTmp13 = pTrackScreenPtr16->screenPtAy[2].projected.fZ;
            fScreenDepth4 = fSurfaceTmp13;
          } else {
            if (pNextGroundScreen->screenPtAy[1].projected.fZ >= (double)pCurrentGroundScreen->screenPtAy[1].projected.fZ)
              pTrackScreenPtr15 = pCurrentGroundScreen;
            else
              pTrackScreenPtr15 = pNextGroundScreen;
            fSurfaceTmp12 = pTrackScreenPtr15->screenPtAy[1].projected.fZ;
            fScreenDepth4 = fSurfaceTmp12;
          }
          fSurfaceTmp11 = fScreenDepth4;
          iRenderingTemp = (uint8)Subdivide[iSectionNum].subdivides[7];
          if ((double)(int16)iRenderingTemp * subscale > fScreenDepth4) {
            subdivide(
              pScrPtr_1,
              &G2Poly,
              pNextGroundScreen->screenPtAy[1].projected.fX,
              pNextGroundScreen->screenPtAy[1].projected.fY,
              pNextGroundScreen->screenPtAy[1].projected.fZ,
              pCurrentGroundScreen->screenPtAy[1].projected.fX,
              pCurrentGroundScreen->screenPtAy[1].projected.fY,
              pCurrentGroundScreen->screenPtAy[1].projected.fZ,
              pCurrentGroundScreen->screenPtAy[2].projected.fX,
              pCurrentGroundScreen->screenPtAy[2].projected.fY,
              pCurrentGroundScreen->screenPtAy[2].projected.fZ,
              pNextGroundScreen->screenPtAy[2].projected.fX,
              pNextGroundScreen->screenPtAy[2].projected.fY,
              pNextGroundScreen->screenPtAy[2].projected.fZ,
              0,
              gfx_size);
            goto LABEL_1271;
          }
          if ((G2Poly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) != 0)
            POLYTEX(texture_vga, pScrPtr_1, &G2Poly, 0, gfx_size);
          else
            POLYFLAT(pScrPtr_1, &G2Poly);
          goto LABEL_1227;
        case 4:
          if (!facing_ok(
            pNextGroundScreen->screenPtAy[4].projected.fX,
            pNextGroundScreen->screenPtAy[4].projected.fY,
            pNextGroundScreen->screenPtAy[4].projected.fZ,
            pCurrentGroundScreen->screenPtAy[4].projected.fX,
            pCurrentGroundScreen->screenPtAy[4].projected.fY,
            pCurrentGroundScreen->screenPtAy[4].projected.fZ,
            pCurrentGroundScreen->screenPtAy[5].projected.fX,
            pCurrentGroundScreen->screenPtAy[5].projected.fY,
            pCurrentGroundScreen->screenPtAy[5].projected.fZ,
            pNextGroundScreen->screenPtAy[5].projected.fX,
            pNextGroundScreen->screenPtAy[5].projected.fY,
            pNextGroundScreen->screenPtAy[5].projected.fZ)
            && (GroundColour[iSectionNum][GROUND_COLOUR_RUOWALL] & SURFACE_FLAG_CONCAVE) == 0) {
            goto LABEL_1174;
          }
          G5Poly.iSurfaceType = GroundColour[iSectionNum][GROUND_COLOUR_RUOWALL];
          G5Poly.vertices[0] = pNextGroundScreen->screenPtAy[4].screen;
          G5Poly.vertices[1] = pCurrentGroundScreen->screenPtAy[4].screen;
          G5Poly.vertices[2] = pCurrentGroundScreen->screenPtAy[5].screen;
          G5Poly.vertices[3].x = pNextGroundScreen->screenPtAy[5].screen.x;
          G5Poly.uiNumVerts = 4;
          G5Poly.vertices[3].y = pNextGroundScreen->screenPtAy[5].screen.y;
          if (G5Poly.iSurfaceType == -1 || GroundColour[iSectionNum][GROUND_COLOUR_OFLOOR] == -1)
            goto LABEL_1174;
          if ((G5Poly.iSurfaceType & SURFACE_FLAG_TEXTURE_PAIR) == 0) {
            if ((textures_off & TEX_OFF_GROUND_TEXTURES) != 0 && (G5Poly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) != 0)
              G5Poly.iSurfaceType = remap_tex[(uint8)(G5Poly.iSurfaceType)] + (G5Poly.iSurfaceType & (SURFACE_MASK_FLAGS ^ SURFACE_FLAG_APPLY_TEXTURE));
            set_starts(0);
            if (pNextGroundScreen->screenPtAy[4].projected.fZ >= (double)pCurrentGroundScreen->screenPtAy[4].projected.fZ)
              pTrackScreenPtr21 = pCurrentGroundScreen;
            else
              pTrackScreenPtr21 = pNextGroundScreen;
            fSurfaceTmp19 = pTrackScreenPtr21->screenPtAy[4].projected.fZ;
            if (pCurrentGroundScreen->screenPtAy[5].projected.fZ >= (double)pNextGroundScreen->screenPtAy[5].projected.fZ)
              pTrackScreenPtr22 = pNextGroundScreen;
            else
              pTrackScreenPtr22 = pCurrentGroundScreen;
            fSurfaceTmp20 = pTrackScreenPtr22->screenPtAy[5].projected.fZ;
            if (fSurfaceTmp19 >= (double)fSurfaceTmp20) {
              if (pCurrentGroundScreen->screenPtAy[5].projected.fZ >= (double)pNextGroundScreen->screenPtAy[5].projected.fZ)
                pTrackScreen1 = pNextGroundScreen;
              else
                pTrackScreen1 = pCurrentGroundScreen;
              fSurfaceTmp23 = pTrackScreen1->screenPtAy[5].projected.fZ;
              fScreenDepth6 = fSurfaceTmp23;
            } else {
              if (pNextGroundScreen->screenPtAy[4].projected.fZ >= (double)pCurrentGroundScreen->screenPtAy[4].projected.fZ)
                pTrackScreenPtr23 = pCurrentGroundScreen;
              else
                pTrackScreenPtr23 = pNextGroundScreen;
              fSurfaceTmp22 = pTrackScreenPtr23->screenPtAy[4].projected.fZ;
              fScreenDepth6 = fSurfaceTmp22;
            }
            fSurfaceTmp21 = fScreenDepth6;
            iRenderingTemp = (uint8)Subdivide[iSectionNum].subdivides[10];
            if ((double)(int16)iRenderingTemp * subscale > fScreenDepth6) {
              subdivide(
                pScrPtr_1,
                &G5Poly,
                pNextGroundScreen->screenPtAy[4].projected.fX,
                pNextGroundScreen->screenPtAy[4].projected.fY,
                pNextGroundScreen->screenPtAy[4].projected.fZ,
                pCurrentGroundScreen->screenPtAy[4].projected.fX,
                pCurrentGroundScreen->screenPtAy[4].projected.fY,
                pCurrentGroundScreen->screenPtAy[4].projected.fZ,
                pCurrentGroundScreen->screenPtAy[5].projected.fX,
                pCurrentGroundScreen->screenPtAy[5].projected.fY,
                pCurrentGroundScreen->screenPtAy[5].projected.fZ,
                pNextGroundScreen->screenPtAy[5].projected.fX,
                pNextGroundScreen->screenPtAy[5].projected.fY,
                pNextGroundScreen->screenPtAy[5].projected.fZ,
                0,
                gfx_size);
              goto LABEL_1174;
            }
            if ((G5Poly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) != 0)
              POLYTEX(texture_vga, pScrPtr_1, &G5Poly, 0, gfx_size);
            else
              POLYFLAT(pScrPtr_1, &G5Poly);
          } else {
            set_starts(1u);
            if ((textures_off & TEX_OFF_GROUND_TEXTURES) != 0 && (G5Poly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) != 0)
              G5Poly.iSurfaceType = remap_tex[(uint8)(G5Poly.iSurfaceType)] + (G5Poly.iSurfaceType & (SURFACE_MASK_FLAGS ^ SURFACE_FLAG_APPLY_TEXTURE));
            if (pNextGroundScreen->screenPtAy[4].projected.fZ >= (double)pCurrentGroundScreen->screenPtAy[4].projected.fZ)
              pTrackScreenPtr17 = pCurrentGroundScreen;
            else
              pTrackScreenPtr17 = pNextGroundScreen;
            fSurfaceTmp14 = pTrackScreenPtr17->screenPtAy[4].projected.fZ;
            if (pCurrentGroundScreen->screenPtAy[5].projected.fZ >= (double)pNextGroundScreen->screenPtAy[5].projected.fZ)
              pTrackScreenPtr18 = pNextGroundScreen;
            else
              pTrackScreenPtr18 = pCurrentGroundScreen;
            fSurfaceTmp15 = pTrackScreenPtr18->screenPtAy[5].projected.fZ;
            if (fSurfaceTmp14 >= (double)fSurfaceTmp15) {
              if (pCurrentGroundScreen->screenPtAy[5].projected.fZ >= (double)pNextGroundScreen->screenPtAy[5].projected.fZ)
                pTrackScreenPtr20 = pNextGroundScreen;
              else
                pTrackScreenPtr20 = pCurrentGroundScreen;
              fSurfaceTmp18 = pTrackScreenPtr20->screenPtAy[5].projected.fZ;
              fScreenDepth5 = fSurfaceTmp18;
            } else {
              if (pNextGroundScreen->screenPtAy[4].projected.fZ >= (double)pCurrentGroundScreen->screenPtAy[4].projected.fZ)
                pTrackScreenPtr19 = pCurrentGroundScreen;
              else
                pTrackScreenPtr19 = pNextGroundScreen;
              fSurfaceTmp17 = pTrackScreenPtr19->screenPtAy[4].projected.fZ;
              fScreenDepth5 = fSurfaceTmp17;
            }
            fSurfaceTmp16 = fScreenDepth5;
            iRenderingTemp = (uint8)Subdivide[iSectionNum].subdivides[10];
            if ((double)(int16)iRenderingTemp * subscale > fScreenDepth5) {
              subdivide(
                pScrPtr_1,
                &G5Poly,
                pNextGroundScreen->screenPtAy[4].projected.fX,
                pNextGroundScreen->screenPtAy[4].projected.fY,
                pNextGroundScreen->screenPtAy[4].projected.fZ,
                pCurrentGroundScreen->screenPtAy[4].projected.fX,
                pCurrentGroundScreen->screenPtAy[4].projected.fY,
                pCurrentGroundScreen->screenPtAy[4].projected.fZ,
                pCurrentGroundScreen->screenPtAy[5].projected.fX,
                pCurrentGroundScreen->screenPtAy[5].projected.fY,
                pCurrentGroundScreen->screenPtAy[5].projected.fZ,
                pNextGroundScreen->screenPtAy[5].projected.fX,
                pNextGroundScreen->screenPtAy[5].projected.fY,
                pNextGroundScreen->screenPtAy[5].projected.fZ,
                -1,
                gfx_size);
              goto LABEL_1174;
            }
            if ((G5Poly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) != 0)
              POLYTEX(texture_vga, pScrPtr_1, &G5Poly, 0, gfx_size);
            else
              POLYFLAT(pScrPtr_1, &G5Poly);
            set_starts(0);
          }
          set_starts(0);
        LABEL_1174:
          if (!facing_ok(
            pNextGroundScreen->screenPtAy[3].projected.fX,
            pNextGroundScreen->screenPtAy[3].projected.fY,
            pNextGroundScreen->screenPtAy[3].projected.fZ,
            pCurrentGroundScreen->screenPtAy[3].projected.fX,
            pCurrentGroundScreen->screenPtAy[3].projected.fY,
            pCurrentGroundScreen->screenPtAy[3].projected.fZ,
            pCurrentGroundScreen->screenPtAy[4].projected.fX,
            pCurrentGroundScreen->screenPtAy[4].projected.fY,
            pCurrentGroundScreen->screenPtAy[4].projected.fZ,
            pNextGroundScreen->screenPtAy[4].projected.fX,
            pNextGroundScreen->screenPtAy[4].projected.fY,
            pNextGroundScreen->screenPtAy[4].projected.fZ)
            && (GroundColour[iSectionNum][GROUND_COLOUR_RLOWALL] & SURFACE_FLAG_CONCAVE) == 0) {
            goto LABEL_1271;
          }
          G4Poly.iSurfaceType = GroundColour[iSectionNum][GROUND_COLOUR_RLOWALL];
          G4Poly.vertices[0] = pNextGroundScreen->screenPtAy[3].screen;
          G4Poly.vertices[1] = pCurrentGroundScreen->screenPtAy[3].screen;
          G4Poly.vertices[2] = pCurrentGroundScreen->screenPtAy[4].screen;
          G4Poly.vertices[3] = pNextGroundScreen->screenPtAy[4].screen;
          G4Poly.uiNumVerts = 4;
          if (G4Poly.iSurfaceType == -1 || GroundColour[iSectionNum][GROUND_COLOUR_OFLOOR] == -1)
            goto LABEL_1271;
          if ((G4Poly.iSurfaceType & SURFACE_FLAG_TEXTURE_PAIR) != 0) {
            set_starts(1u);
            if ((textures_off & TEX_OFF_GROUND_TEXTURES) != 0 && (G4Poly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) != 0)
              G4Poly.iSurfaceType = (G4Poly.iSurfaceType & (SURFACE_MASK_FLAGS ^ SURFACE_FLAG_APPLY_TEXTURE)) + remap_tex[(uint8)(G4Poly.iSurfaceType)];
            if (pNextGroundScreen->screenPtAy[3].projected.fZ >= (double)pCurrentGroundScreen->screenPtAy[3].projected.fZ)
              pTrackScreen2 = pCurrentGroundScreen;
            else
              pTrackScreen2 = pNextGroundScreen;
            fSurfaceTmp24 = pTrackScreen2->screenPtAy[3].projected.fZ;
            if (pCurrentGroundScreen->screenPtAy[4].projected.fZ >= (double)pNextGroundScreen->screenPtAy[4].projected.fZ)
              pTrackScreen3 = pNextGroundScreen;
            else
              pTrackScreen3 = pCurrentGroundScreen;
            fSurfaceTmp25 = pTrackScreen3->screenPtAy[4].projected.fZ;
            if (fSurfaceTmp24 >= (double)fSurfaceTmp25) {
              if (pCurrentGroundScreen->screenPtAy[4].projected.fZ >= (double)pNextGroundScreen->screenPtAy[4].projected.fZ)
                pTrackScreen5 = pNextGroundScreen;
              else
                pTrackScreen5 = pCurrentGroundScreen;
              fSurfaceTmp28 = pTrackScreen5->screenPtAy[4].projected.fZ;
              fTrackScreenDepth7 = fSurfaceTmp28;
            } else {
              if (pNextGroundScreen->screenPtAy[3].projected.fZ >= (double)pCurrentGroundScreen->screenPtAy[3].projected.fZ)
                pTrackScreen4 = pCurrentGroundScreen;
              else
                pTrackScreen4 = pNextGroundScreen;
              fSurfaceTmp27 = pTrackScreen4->screenPtAy[3].projected.fZ;
              fTrackScreenDepth7 = fSurfaceTmp27;
            }
            fSurfaceTmp26 = fTrackScreenDepth7;
            iRenderingTemp = (uint8)Subdivide[iSectionNum].subdivides[9];
            if ((double)(int16)iRenderingTemp * subscale > fTrackScreenDepth7) {
              subdivide(
                pScrPtr_1,
                &G4Poly,
                pNextGroundScreen->screenPtAy[3].projected.fX,
                pNextGroundScreen->screenPtAy[3].projected.fY,
                pNextGroundScreen->screenPtAy[3].projected.fZ,
                pCurrentGroundScreen->screenPtAy[3].projected.fX,
                pCurrentGroundScreen->screenPtAy[3].projected.fY,
                pCurrentGroundScreen->screenPtAy[3].projected.fZ,
                pCurrentGroundScreen->screenPtAy[4].projected.fX,
                pCurrentGroundScreen->screenPtAy[4].projected.fY,
                pCurrentGroundScreen->screenPtAy[4].projected.fZ,
                pNextGroundScreen->screenPtAy[4].projected.fX,
                pNextGroundScreen->screenPtAy[4].projected.fY,
                pNextGroundScreen->screenPtAy[4].projected.fZ,
                -1,
                gfx_size);
              goto LABEL_1271;
            }
            if ((G4Poly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) != 0)
              POLYTEX(texture_vga, pScrPtr_1, &G4Poly, 0, gfx_size);
            else
              POLYFLAT(pScrPtr_1, &G4Poly);
            goto LABEL_1203;
          }
          if ((textures_off & TEX_OFF_GROUND_TEXTURES) != 0 && (G4Poly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) != 0)
            G4Poly.iSurfaceType = remap_tex[(uint8)(G4Poly.iSurfaceType)] + (G4Poly.iSurfaceType & (SURFACE_MASK_FLAGS ^ SURFACE_FLAG_APPLY_TEXTURE));
          set_starts(0);
          if (pNextGroundScreen->screenPtAy[3].projected.fZ >= (double)pCurrentGroundScreen->screenPtAy[3].projected.fZ)
            pTrackScreen6 = pCurrentGroundScreen;
          else
            pTrackScreen6 = pNextGroundScreen;
          fSurfaceTmp29 = pTrackScreen6->screenPtAy[3].projected.fZ;
          if (pCurrentGroundScreen->screenPtAy[4].projected.fZ >= (double)pNextGroundScreen->screenPtAy[4].projected.fZ)
            pTrackScreen7 = pNextGroundScreen;
          else
            pTrackScreen7 = pCurrentGroundScreen;
          fSurfaceTmp30 = pTrackScreen7->screenPtAy[4].projected.fZ;
          if (fSurfaceTmp29 >= (double)fSurfaceTmp30) {
            if (pCurrentGroundScreen->screenPtAy[4].projected.fZ >= (double)pNextGroundScreen->screenPtAy[4].projected.fZ)
              pTrackScreen9 = pNextGroundScreen;
            else
              pTrackScreen9 = pCurrentGroundScreen;
            fSurfaceTmp33 = pTrackScreen9->screenPtAy[4].projected.fZ;
            fTrackScreenDepth8 = fSurfaceTmp33;
          } else {
            if (pNextGroundScreen->screenPtAy[3].projected.fZ >= (double)pCurrentGroundScreen->screenPtAy[3].projected.fZ)
              pTrackScreen8 = pCurrentGroundScreen;
            else
              pTrackScreen8 = pNextGroundScreen;
            fSurfaceTmp32 = pTrackScreen8->screenPtAy[3].projected.fZ;
            fTrackScreenDepth8 = fSurfaceTmp32;
          }
          fSurfaceTmp31 = fTrackScreenDepth8;
          iRenderingTemp = (uint8)Subdivide[iSectionNum].subdivides[9];
          if ((double)(int16)iRenderingTemp * subscale > fTrackScreenDepth8) {
            subdivide(
              pScrPtr_1,
              &G4Poly,
              pNextGroundScreen->screenPtAy[3].projected.fX,
              pNextGroundScreen->screenPtAy[3].projected.fY,
              pNextGroundScreen->screenPtAy[3].projected.fZ,
              pCurrentGroundScreen->screenPtAy[3].projected.fX,
              pCurrentGroundScreen->screenPtAy[3].projected.fY,
              pCurrentGroundScreen->screenPtAy[3].projected.fZ,
              pCurrentGroundScreen->screenPtAy[4].projected.fX,
              pCurrentGroundScreen->screenPtAy[4].projected.fY,
              pCurrentGroundScreen->screenPtAy[4].projected.fZ,
              pNextGroundScreen->screenPtAy[4].projected.fX,
              pNextGroundScreen->screenPtAy[4].projected.fY,
              pNextGroundScreen->screenPtAy[4].projected.fZ,
              0,
              gfx_size);
            goto LABEL_1271;
          }
          if ((G4Poly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) != 0)
            POLYTEX(texture_vga, pScrPtr_1, &G4Poly, 0, gfx_size);
          else
            POLYFLAT(pScrPtr_1, &G4Poly);
          goto LABEL_1227;
        case 5:
          if (facing_ok(
            pScreenCoord->screenPtAy[2].projected.fX,
            pScreenCoord->screenPtAy[2].projected.fY,
            pScreenCoord->screenPtAy[2].projected.fZ,
            pScreenCoord->screenPtAy[1].projected.fX,
            pScreenCoord->screenPtAy[1].projected.fY,
            pScreenCoord->screenPtAy[1].projected.fZ,
            pScreenCoord_1->screenPtAy[1].projected.fX,
            pScreenCoord_1->screenPtAy[1].projected.fY,
            pScreenCoord_1->screenPtAy[1].projected.fZ,
            pScreenCoord_1->screenPtAy[2].projected.fX,
            pScreenCoord_1->screenPtAy[2].projected.fY,
            pScreenCoord_1->screenPtAy[2].projected.fZ)
            || (TrakColour[iSectionNum][TRAK_COLOUR_CENTER] & SURFACE_FLAG_CONCAVE) != 0) {
            RoadPoly.uiNumVerts = 4;
            iSectionCommand = TrakColour[iSectionNum][TRAK_COLOUR_CENTER];
            RoadPoly.iSurfaceType = iSectionCommand;
            if ((textures_off & TEX_OFF_ROAD_TEXTURES) != 0 && (RoadPoly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) != 0)
              RoadPoly.iSurfaceType = remap_tex[(uint8)iSectionCommand] + (RoadPoly.iSurfaceType & (SURFACE_MASK_FLAGS ^ SURFACE_FLAG_APPLY_TEXTURE));
            RoadPoly.vertices[0] = pScreenCoord->screenPtAy[2].screen;
            RoadPoly.vertices[1] = pScreenCoord->screenPtAy[1].screen;
            RoadPoly.vertices[2] = pScreenCoord_1->screenPtAy[1].screen;
            RoadPoly.vertices[3] = pScreenCoord_1->screenPtAy[2].screen;
            if ((RoadPoly.iSurfaceType & SURFACE_FLAG_TEXTURE_PAIR) != 0 && wide_on) {
              set_starts(1u);
              if (pScreenCoord->screenPtAy[2].projected.fZ >= (double)pScreenCoord->screenPtAy[1].projected.fZ)
                iSectionTypeIndex = pScreenCoord->screenPtAy[1].projected.fZ;
              else
                iSectionTypeIndex = pScreenCoord->screenPtAy[2].projected.fZ;
              fObjectDepthA1 = iSectionTypeIndex;
              if (pScreenCoord_1->screenPtAy[1].projected.fZ >= (double)pScreenCoord_1->screenPtAy[2].projected.fZ)
                fSurfaceDepth1 = pScreenCoord_1->screenPtAy[2].projected.fZ;
              else
                fSurfaceDepth1 = pScreenCoord_1->screenPtAy[1].projected.fZ;
              fGeometryDepthTmp1 = fSurfaceDepth1;
              if (fObjectDepthA1 >= (double)fSurfaceDepth1) {
                if (pScreenCoord_1->screenPtAy[1].projected.fZ >= (double)pScreenCoord_1->screenPtAy[2].projected.fZ)
                  fSurfaceDepth2 = pScreenCoord_1->screenPtAy[2].projected.fZ;
                else
                  fSurfaceDepth2 = pScreenCoord_1->screenPtAy[1].projected.fZ;
                fObjectDepthA3 = fSurfaceDepth2;
              } else {
                if (pScreenCoord->screenPtAy[2].projected.fZ >= (double)pScreenCoord->screenPtAy[1].projected.fZ)
                  fSurfaceDepth2 = pScreenCoord->screenPtAy[1].projected.fZ;
                else
                  fSurfaceDepth2 = pScreenCoord->screenPtAy[2].projected.fZ;
                fGeometryDepthTmp2 = fSurfaceDepth2;
              }
              fObjectDepthA2 = fSurfaceDepth2;
              iRenderingTemp = (uint8)Subdivide[iSectionNum].subdivides[1];
              if ((double)(int16)iRenderingTemp * subscale > fSurfaceDepth2)
                goto LABEL_434;
              if ((RoadPoly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) == 0)
                goto LABEL_481;
              goto LABEL_436;
            }
            set_starts(0);
            if (pScreenCoord->screenPtAy[2].projected.fZ >= (double)pScreenCoord->screenPtAy[1].projected.fZ)
              fSurfaceDepth3 = pScreenCoord->screenPtAy[1].projected.fZ;
            else
              fSurfaceDepth3 = pScreenCoord->screenPtAy[2].projected.fZ;
            fObjectDepthA4 = fSurfaceDepth3;
            if (pScreenCoord_1->screenPtAy[1].projected.fZ >= (double)pScreenCoord_1->screenPtAy[2].projected.fZ)
              fSurfaceDepth4 = pScreenCoord_1->screenPtAy[2].projected.fZ;
            else
              fSurfaceDepth4 = pScreenCoord_1->screenPtAy[1].projected.fZ;
            fGeometryDepthTmp3 = fSurfaceDepth4;
            if (fObjectDepthA4 >= (double)fSurfaceDepth4) {
              if (pScreenCoord_1->screenPtAy[1].projected.fZ >= (double)pScreenCoord_1->screenPtAy[2].projected.fZ)
                fSurfaceDepth5 = pScreenCoord_1->screenPtAy[2].projected.fZ;
              else
                fSurfaceDepth5 = pScreenCoord_1->screenPtAy[1].projected.fZ;
              fGeometryDepthTmp5 = fSurfaceDepth5;
            } else {
              if (pScreenCoord->screenPtAy[2].projected.fZ >= (double)pScreenCoord->screenPtAy[1].projected.fZ)
                fSurfaceDepth5 = pScreenCoord->screenPtAy[1].projected.fZ;
              else
                fSurfaceDepth5 = pScreenCoord->screenPtAy[2].projected.fZ;
              fGeometryDepthTmp4 = fSurfaceDepth5;
            }
            fObjectDepthA5 = fSurfaceDepth5;
            iRenderingTemp = (uint8)Subdivide[iSectionNum].subdivides[1];
            if ((double)(int16)iRenderingTemp * subscale <= fSurfaceDepth5) {
              if ((RoadPoly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) == 0)
                goto LABEL_499;
              goto LABEL_456;
            }
          } else {
            iCenterSurfType = TrakColour[iSectionNum][TRAK_COLOUR_CENTER];
            RoadPoly.uiNumVerts = 4;
            RoadPoly.iSurfaceType = iCenterSurfType;
            if ((textures_off & TEX_OFF_ROAD_TEXTURES) != 0 && (RoadPoly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) != 0)
              RoadPoly.iSurfaceType = remap_tex[(uint8)iCenterSurfType] + (RoadPoly.iSurfaceType & (SURFACE_MASK_FLAGS ^ SURFACE_FLAG_APPLY_TEXTURE));
            RoadPoly.vertices[0] = pScreenCoord->screenPtAy[1].screen;
            RoadPoly.vertices[1] = pScreenCoord->screenPtAy[2].screen;
            RoadPoly.vertices[2] = pScreenCoord_1->screenPtAy[2].screen;
            RoadPoly.vertices[3] = pScreenCoord_1->screenPtAy[1].screen;
            if ((RoadPoly.iSurfaceType & SURFACE_FLAG_TEXTURE_PAIR) != 0 && wide_on) {
              set_starts(1u);
              if (pScreenCoord->screenPtAy[2].projected.fZ >= (double)pScreenCoord->screenPtAy[1].projected.fZ)
                fObjectDepth1 = pScreenCoord->screenPtAy[1].projected.fZ;
              else
                fObjectDepth1 = pScreenCoord->screenPtAy[2].projected.fZ;
              fGeometryDepthTmp6 = fObjectDepth1;
              if (pScreenCoord_1->screenPtAy[1].projected.fZ >= (double)pScreenCoord_1->screenPtAy[2].projected.fZ)
                fObjectDepth2 = pScreenCoord_1->screenPtAy[2].projected.fZ;
              else
                fObjectDepth2 = pScreenCoord_1->screenPtAy[1].projected.fZ;
              fObjectDepthA6 = fObjectDepth2;
              if (fGeometryDepthTmp6 >= (double)fObjectDepth2) {
                if (pScreenCoord_1->screenPtAy[1].projected.fZ >= (double)pScreenCoord_1->screenPtAy[2].projected.fZ)
                  fObjectDepth3 = pScreenCoord_1->screenPtAy[2].projected.fZ;
                else
                  fObjectDepth3 = pScreenCoord_1->screenPtAy[1].projected.fZ;
                fRenderDepthTmp1 = fObjectDepth3;
              } else {
                if (pScreenCoord->screenPtAy[2].projected.fZ >= (double)pScreenCoord->screenPtAy[1].projected.fZ)
                  fObjectDepth3 = pScreenCoord->screenPtAy[1].projected.fZ;
                else
                  fObjectDepth3 = pScreenCoord->screenPtAy[2].projected.fZ;
                fObjectDepthB1 = fObjectDepth3;
              }
              fGeometryDepthTmp7 = fObjectDepth3;
              iRenderingTemp = (uint8)Subdivide[iSectionNum].subdivides[1];
              if ((double)(int16)iRenderingTemp * subscale > fObjectDepth3) {
              LABEL_434:
                subdivide(
                  pScrPtr_1,
                  &RoadPoly,
                  pScreenCoord->screenPtAy[2].projected.fX,
                  pScreenCoord->screenPtAy[2].projected.fY,
                  pScreenCoord->screenPtAy[2].projected.fZ,
                  pScreenCoord->screenPtAy[1].projected.fX,
                  pScreenCoord->screenPtAy[1].projected.fY,
                  pScreenCoord->screenPtAy[1].projected.fZ,
                  pScreenCoord_1->screenPtAy[1].projected.fX,
                  pScreenCoord_1->screenPtAy[1].projected.fY,
                  pScreenCoord_1->screenPtAy[1].projected.fZ,
                  pScreenCoord_1->screenPtAy[2].projected.fX,
                  pScreenCoord_1->screenPtAy[2].projected.fY,
                  pScreenCoord_1->screenPtAy[2].projected.fZ,
                  -1,
                  gfx_size);
                goto LABEL_1271;
              }
              if ((RoadPoly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) == 0) {
              LABEL_481:
                POLYFLAT(pScrPtr_1, &RoadPoly);
                goto LABEL_1203;
              }
            LABEL_436:
              POLYTEX(texture_vga, pScrPtr_1, &RoadPoly, 0, gfx_size);
            LABEL_1203:
              set_starts(0);
              set_starts(0);
              goto LABEL_1271;
            }
            set_starts(0);
            if (pScreenCoord->screenPtAy[2].projected.fZ >= (double)pScreenCoord->screenPtAy[1].projected.fZ)
              fObjectDepth4 = pScreenCoord->screenPtAy[1].projected.fZ;
            else
              fObjectDepth4 = pScreenCoord->screenPtAy[2].projected.fZ;
            fObjectDepthB2 = fObjectDepth4;
            if (pScreenCoord_1->screenPtAy[1].projected.fZ >= (double)pScreenCoord_1->screenPtAy[2].projected.fZ)
              fObjectDepth5 = pScreenCoord_1->screenPtAy[2].projected.fZ;
            else
              fObjectDepth5 = pScreenCoord_1->screenPtAy[1].projected.fZ;
            fObjectDepthB3 = fObjectDepth5;
            if (fObjectDepthB2 >= (double)fObjectDepth5) {
              if (pScreenCoord_1->screenPtAy[1].projected.fZ >= (double)pScreenCoord_1->screenPtAy[2].projected.fZ)
                fObjectDepth6 = pScreenCoord_1->screenPtAy[2].projected.fZ;
              else
                fObjectDepth6 = pScreenCoord_1->screenPtAy[1].projected.fZ;
              fObjectDepthB4 = fObjectDepth6;
            } else {
              if (pScreenCoord->screenPtAy[2].projected.fZ >= (double)pScreenCoord->screenPtAy[1].projected.fZ)
                fObjectDepth6 = pScreenCoord->screenPtAy[1].projected.fZ;
              else
                fObjectDepth6 = pScreenCoord->screenPtAy[2].projected.fZ;
              fRenderDepthTmp3 = fObjectDepth6;
            }
            fRenderDepthTmp2 = fObjectDepth6;
            iRenderingTemp = (uint8)Subdivide[iSectionNum].subdivides[1];
            if ((double)(int16)iRenderingTemp * subscale <= fObjectDepth6) {
              if ((RoadPoly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) == 0) {
              LABEL_499:
                POLYFLAT(pScrPtr_1, &RoadPoly);
                goto LABEL_1227;
              }
            LABEL_456:
              POLYTEX(texture_vga, pScrPtr_1, &RoadPoly, 0, gfx_size);
              goto LABEL_1227;
            }
          }
          subdivide(
            pScrPtr_1,
            &RoadPoly,
            pScreenCoord->screenPtAy[2].projected.fX,
            pScreenCoord->screenPtAy[2].projected.fY,
            pScreenCoord->screenPtAy[2].projected.fZ,
            pScreenCoord->screenPtAy[1].projected.fX,
            pScreenCoord->screenPtAy[1].projected.fY,
            pScreenCoord->screenPtAy[1].projected.fZ,
            pScreenCoord_1->screenPtAy[1].projected.fX,
            pScreenCoord_1->screenPtAy[1].projected.fY,
            pScreenCoord_1->screenPtAy[1].projected.fZ,
            pScreenCoord_1->screenPtAy[2].projected.fX,
            pScreenCoord_1->screenPtAy[2].projected.fY,
            pScreenCoord_1->screenPtAy[2].projected.fZ,
            0,
            gfx_size);
          goto LABEL_1271;
        case 6:
          iLeftSurfType = TrakColour[iSectionNum][TRAK_COLOUR_LEFT_LANE];
          if (iLeftSurfType < 0)
            iLeftSurfType = -iLeftSurfType;
          LeftPoly.iSurfaceType = iLeftSurfType;
          LeftPoly.uiNumVerts = 4;
          if ((textures_off & TEX_OFF_ROAD_TEXTURES) != 0 && (iLeftSurfType & SURFACE_FLAG_APPLY_TEXTURE) != 0) {
            LeftPoly.iSurfaceType = remap_tex[(uint8)iLeftSurfType] + (iLeftSurfType & (SURFACE_MASK_FLAGS ^ SURFACE_FLAG_APPLY_TEXTURE));
          }
          LeftPoly.vertices[0] = pScreenCoord->screenPtAy[1].screen;
          LeftPoly.vertices[1] = pScreenCoord->screenPtAy[0].screen;
          LeftPoly.vertices[2] = pScreenCoord_1->screenPtAy[0].screen;
          LeftPoly.vertices[3] = pScreenCoord_1->screenPtAy[1].screen;
          if ((LeftPoly.iSurfaceType & SURFACE_FLAG_TEXTURE_PAIR) != 0 && wide_on) {
            set_starts(1u);
            if (pScreenCoord->screenPtAy[1].projected.fZ >= (double)pScreenCoord->screenPtAy[0].projected.fZ)
              fMiddleDepth1 = pScreenCoord->screenPtAy[0].projected.fZ;
            else
              fMiddleDepth1 = pScreenCoord->screenPtAy[1].projected.fZ;
            fRenderDepthTmp4 = fMiddleDepth1;
            if (pScreenCoord_1->screenPtAy[0].projected.fZ >= (double)pScreenCoord_1->screenPtAy[1].projected.fZ)
              fMiddleDepth2 = pScreenCoord_1->screenPtAy[1].projected.fZ;
            else
              fMiddleDepth2 = pScreenCoord_1->screenPtAy[0].projected.fZ;
            fRenderDepthTmp5 = fMiddleDepth2;
            if (fRenderDepthTmp4 >= (double)fMiddleDepth2) {
              if (pScreenCoord_1->screenPtAy[0].projected.fZ >= (double)pScreenCoord_1->screenPtAy[1].projected.fZ)
                fMiddleDepth3 = pScreenCoord_1->screenPtAy[1].projected.fZ;
              else
                fMiddleDepth3 = pScreenCoord_1->screenPtAy[0].projected.fZ;
              fObjectDepthB6 = fMiddleDepth3;
            } else {
              if (pScreenCoord->screenPtAy[1].projected.fZ >= (double)pScreenCoord->screenPtAy[0].projected.fZ)
                fMiddleDepth3 = pScreenCoord->screenPtAy[0].projected.fZ;
              else
                fMiddleDepth3 = pScreenCoord->screenPtAy[1].projected.fZ;
              fObjectDepthB5 = fMiddleDepth3;
            }
            fRenderDepthTmp6 = fMiddleDepth3;
            iRenderingTemp = (uint8)Subdivide[iSectionNum].subdivides[0];
            if ((double)(int16)iRenderingTemp * subscale > fMiddleDepth3) {
              subdivide(
                pScrPtr_1,
                &LeftPoly,
                pScreenCoord->screenPtAy[1].projected.fX,
                pScreenCoord->screenPtAy[1].projected.fY,
                pScreenCoord->screenPtAy[1].projected.fZ,
                pScreenCoord->screenPtAy[0].projected.fX,
                pScreenCoord->screenPtAy[0].projected.fY,
                pScreenCoord->screenPtAy[0].projected.fZ,
                pScreenCoord_1->screenPtAy[0].projected.fX,
                pScreenCoord_1->screenPtAy[0].projected.fY,
                pScreenCoord_1->screenPtAy[0].projected.fZ,
                pScreenCoord_1->screenPtAy[1].projected.fX,
                pScreenCoord_1->screenPtAy[1].projected.fY,
                pScreenCoord_1->screenPtAy[1].projected.fZ,
                -1,
                gfx_size);
              goto LABEL_1271;
            }
            if ((LeftPoly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) != 0)
              POLYTEX(texture_vga, pScrPtr_1, &LeftPoly, 0, gfx_size);
            else
              POLYFLAT(pScrPtr_1, &LeftPoly);
            goto LABEL_1203;
          }
          set_starts(0);
          if (pScreenCoord->screenPtAy[1].projected.fZ >= (double)pScreenCoord->screenPtAy[0].projected.fZ)
            fMiddleDepth4 = pScreenCoord->screenPtAy[0].projected.fZ;
          else
            fMiddleDepth4 = pScreenCoord->screenPtAy[1].projected.fZ;
          fObjectDepthC1 = fMiddleDepth4;
          if (pScreenCoord_1->screenPtAy[0].projected.fZ >= (double)pScreenCoord_1->screenPtAy[1].projected.fZ)
            fMiddleDepth5 = pScreenCoord_1->screenPtAy[1].projected.fZ;
          else
            fMiddleDepth5 = pScreenCoord_1->screenPtAy[0].projected.fZ;
          fObjectDepthC2 = fMiddleDepth5;
          if (fObjectDepthC1 >= (double)fMiddleDepth5) {
            if (pScreenCoord_1->screenPtAy[0].projected.fZ >= (double)pScreenCoord_1->screenPtAy[1].projected.fZ)
              fMiddleDepth6 = pScreenCoord_1->screenPtAy[1].projected.fZ;
            else
              fMiddleDepth6 = pScreenCoord_1->screenPtAy[0].projected.fZ;
            fObjectDepthC4 = fMiddleDepth6;
          } else {
            if (pScreenCoord->screenPtAy[1].projected.fZ >= (double)pScreenCoord->screenPtAy[0].projected.fZ)
              fMiddleDepth6 = pScreenCoord->screenPtAy[0].projected.fZ;
            else
              fMiddleDepth6 = pScreenCoord->screenPtAy[1].projected.fZ;
            fRenderDepthTmp10 = fMiddleDepth6;
          }
          fObjectDepthC3 = fMiddleDepth6;
          iRenderingTemp = (uint8)Subdivide[iSectionNum].subdivides[0];
          if ((double)(int16)iRenderingTemp * subscale > fMiddleDepth6) {
            subdivide(
              pScrPtr_1,
              &LeftPoly,
              pScreenCoord->screenPtAy[1].projected.fX,
              pScreenCoord->screenPtAy[1].projected.fY,
              pScreenCoord->screenPtAy[1].projected.fZ,
              pScreenCoord->screenPtAy[0].projected.fX,
              pScreenCoord->screenPtAy[0].projected.fY,
              pScreenCoord->screenPtAy[0].projected.fZ,
              pScreenCoord_1->screenPtAy[0].projected.fX,
              pScreenCoord_1->screenPtAy[0].projected.fY,
              pScreenCoord_1->screenPtAy[0].projected.fZ,
              pScreenCoord_1->screenPtAy[1].projected.fX,
              pScreenCoord_1->screenPtAy[1].projected.fY,
              pScreenCoord_1->screenPtAy[1].projected.fZ,
              0,
              gfx_size);
            goto LABEL_1271;
          }
          if ((LeftPoly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) != 0)
            POLYTEX(texture_vga, pScrPtr_1, &LeftPoly, 0, gfx_size);
          else
            POLYFLAT(pScrPtr_1, &LeftPoly);
          goto LABEL_1227;
        case 7:
          iRightSurfType = TrakColour[iSectionNum][TRAK_COLOUR_RIGHT_LANE];
          if (iRightSurfType < 0)
            iRightSurfType = -iRightSurfType;
          RightPoly.iSurfaceType = iRightSurfType;
          RightPoly.uiNumVerts = 4;
          if ((textures_off & TEX_OFF_ROAD_TEXTURES) != 0 && (iRightSurfType & SURFACE_FLAG_APPLY_TEXTURE) != 0) {
            RightPoly.iSurfaceType = remap_tex[(uint8)iRightSurfType] + (iRightSurfType & (SURFACE_MASK_FLAGS ^ SURFACE_FLAG_APPLY_TEXTURE));
          }
          RightPoly.vertices[0] = pScreenCoord->screenPtAy[3].screen;
          RightPoly.vertices[1] = pScreenCoord->screenPtAy[2].screen;
          RightPoly.vertices[2] = pScreenCoord_1->screenPtAy[2].screen;
          RightPoly.vertices[3] = pScreenCoord_1->screenPtAy[3].screen;
          if ((RightPoly.iSurfaceType & SURFACE_FLAG_TEXTURE_PAIR) != 0 && wide_on) {
            set_starts(1u);
            if (pScreenCoord->screenPtAy[3].projected.fZ >= (double)pScreenCoord->screenPtAy[2].projected.fZ)
              fRightDepth1 = pScreenCoord->screenPtAy[2].projected.fZ;
            else
              fRightDepth1 = pScreenCoord->screenPtAy[3].projected.fZ;
            fObjectDepthC5 = fRightDepth1;
            if (pScreenCoord_1->screenPtAy[2].projected.fZ >= (double)pScreenCoord_1->screenPtAy[3].projected.fZ)
              fRightDepth2 = pScreenCoord_1->screenPtAy[3].projected.fZ;
            else
              fRightDepth2 = pScreenCoord_1->screenPtAy[2].projected.fZ;
            fObjectDepthC6 = fRightDepth2;
            if (fObjectDepthC5 >= (double)fRightDepth2) {
              if (pScreenCoord_1->screenPtAy[2].projected.fZ >= (double)pScreenCoord_1->screenPtAy[3].projected.fZ)
                fRightDepth3 = pScreenCoord_1->screenPtAy[3].projected.fZ;
              else
                fRightDepth3 = pScreenCoord_1->screenPtAy[2].projected.fZ;
              fObjectDepthD3 = fRightDepth3;
            } else {
              if (pScreenCoord->screenPtAy[3].projected.fZ >= (double)pScreenCoord->screenPtAy[2].projected.fZ)
                fRightDepth3 = pScreenCoord->screenPtAy[2].projected.fZ;
              else
                fRightDepth3 = pScreenCoord->screenPtAy[3].projected.fZ;
              fObjectDepthD2 = fRightDepth3;
            }
            fObjectDepthD1 = fRightDepth3;
            iRenderingTemp = (uint8)Subdivide[iSectionNum].subdivides[2];
            if ((double)(int16)iRenderingTemp * subscale > fRightDepth3) {
              subdivide(
                pScrPtr_1,
                &RightPoly,
                pScreenCoord->screenPtAy[3].projected.fX,
                pScreenCoord->screenPtAy[3].projected.fY,
                pScreenCoord->screenPtAy[3].projected.fZ,
                pScreenCoord->screenPtAy[2].projected.fX,
                pScreenCoord->screenPtAy[2].projected.fY,
                pScreenCoord->screenPtAy[2].projected.fZ,
                pScreenCoord_1->screenPtAy[2].projected.fX,
                pScreenCoord_1->screenPtAy[2].projected.fY,
                pScreenCoord_1->screenPtAy[2].projected.fZ,
                pScreenCoord_1->screenPtAy[3].projected.fX,
                pScreenCoord_1->screenPtAy[3].projected.fY,
                pScreenCoord_1->screenPtAy[3].projected.fZ,
                -1,
                gfx_size);
              goto LABEL_1271;
            }
            if ((RightPoly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) != 0)
              POLYTEX(texture_vga, pScrPtr_1, &RightPoly, 0, gfx_size);
            else
              POLYFLAT(pScrPtr_1, &RightPoly);
            goto LABEL_1203;
          }
          set_starts(0);
          if (pScreenCoord->screenPtAy[3].projected.fZ >= (double)pScreenCoord->screenPtAy[2].projected.fZ)
            fRightDepth4 = pScreenCoord->screenPtAy[2].projected.fZ;
          else
            fRightDepth4 = pScreenCoord->screenPtAy[3].projected.fZ;
          fObjectDepthD4 = fRightDepth4;
          if (pScreenCoord_1->screenPtAy[2].projected.fZ >= (double)pScreenCoord_1->screenPtAy[3].projected.fZ)
            fRightDepth5 = pScreenCoord_1->screenPtAy[3].projected.fZ;
          else
            fRightDepth5 = pScreenCoord_1->screenPtAy[2].projected.fZ;
          fObjectDepthD5 = fRightDepth5;
          if (fObjectDepthD4 >= (double)fRightDepth5) {
            if (pScreenCoord_1->screenPtAy[2].projected.fZ >= (double)pScreenCoord_1->screenPtAy[3].projected.fZ)
              fRightDepth6 = pScreenCoord_1->screenPtAy[3].projected.fZ;
            else
              fRightDepth6 = pScreenCoord_1->screenPtAy[2].projected.fZ;
            fObjectDepthE2 = fRightDepth6;
          } else {
            if (pScreenCoord->screenPtAy[3].projected.fZ >= (double)pScreenCoord->screenPtAy[2].projected.fZ)
              fRightDepth6 = pScreenCoord->screenPtAy[2].projected.fZ;
            else
              fRightDepth6 = pScreenCoord->screenPtAy[3].projected.fZ;
            fObjectDepthE1 = fRightDepth6;
          }
          fObjectDepthD6 = fRightDepth6;
          iRenderingTemp = (uint8)Subdivide[iSectionNum].subdivides[2];
          if ((double)(int16)iRenderingTemp * subscale > fRightDepth6) {
            subdivide(
              pScrPtr_1,
              &RightPoly,
              pScreenCoord->screenPtAy[3].projected.fX,
              pScreenCoord->screenPtAy[3].projected.fY,
              pScreenCoord->screenPtAy[3].projected.fZ,
              pScreenCoord->screenPtAy[2].projected.fX,
              pScreenCoord->screenPtAy[2].projected.fY,
              pScreenCoord->screenPtAy[2].projected.fZ,
              pScreenCoord_1->screenPtAy[2].projected.fX,
              pScreenCoord_1->screenPtAy[2].projected.fY,
              pScreenCoord_1->screenPtAy[2].projected.fZ,
              pScreenCoord_1->screenPtAy[3].projected.fX,
              pScreenCoord_1->screenPtAy[3].projected.fY,
              pScreenCoord_1->screenPtAy[3].projected.fZ,
              0,
              gfx_size);
            goto LABEL_1271;
          }
          if ((RightPoly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) != 0)
            POLYTEX(texture_vga, pScrPtr_1, &RightPoly, 0, gfx_size);
          else
            POLYFLAT(pScrPtr_1, &RightPoly);
          goto LABEL_1227;
        case 0xA:
          RoofPoly.uiNumVerts = 4;
          iGeometryIndex = TrakColour[iSectionNum][TRAK_COLOUR_ROOF];
          RoofPoly.iSurfaceType = iGeometryIndex;
          if (iGeometryIndex < 0) {
            RoofPoly.vertices[2].x = GroundScreenXYZ[iNextSectionIndex].screenPtAy[5].screen.x;
            RoofPoly.vertices[2].y = GroundScreenXYZ[iNextSectionIndex].screenPtAy[5].screen.y;
            RoofPoly.vertices[3].x = GroundScreenXYZ[iNextSectionIndex].screenPtAy[0].screen.x;
            RoofPoly.vertices[3].y = GroundScreenXYZ[iNextSectionIndex].screenPtAy[0].screen.y;
            RoofPoly.vertices[0].x = GroundScreenXYZ[iNextSectionIndex].screenPtAy[1].screen.x;
            RoofPoly.vertices[0].y = GroundScreenXYZ[iNextSectionIndex].screenPtAy[1].screen.y;
            iProcessingIndex = GroundScreenXYZ[iNextSectionIndex].screenPtAy[4].screen.x;
            RoofPoly.iSurfaceType = -iGeometryIndex;
            RoofPoly.vertices[1].x = iProcessingIndex;
            RoofPoly.vertices[1].y = GroundScreenXYZ[iNextSectionIndex].screenPtAy[4].screen.y;
            if ((-iGeometryIndex & SURFACE_FLAG_TEXTURE_PAIR) != 0) {
              set_starts(1u);
              if ((textures_off & TEX_OFF_GROUND_TEXTURES) != 0 && (RoofPoly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) != 0)
                RoofPoly.iSurfaceType = remap_tex[(uint8)(RoofPoly.iSurfaceType)] + (RoofPoly.iSurfaceType & (SURFACE_MASK_FLAGS ^ SURFACE_FLAG_APPLY_TEXTURE));
              if (pNextGroundScreen->screenPtAy[1].projected.fZ >= (double)pNextGroundScreen->screenPtAy[4].projected.fZ)
                fComputedDepth1 = pNextGroundScreen->screenPtAy[4].projected.fZ;
              else
                fComputedDepth1 = pNextGroundScreen->screenPtAy[1].projected.fZ;
              fProjectionTempY4 = fComputedDepth1;
              if (pNextGroundScreen->screenPtAy[5].projected.fZ >= (double)pNextGroundScreen->screenPtAy[0].projected.fZ)
                fRenderValue1 = pNextGroundScreen->screenPtAy[0].projected.fZ;
              else
                fRenderValue1 = pNextGroundScreen->screenPtAy[5].projected.fZ;
              fProjectionTempZ4 = fRenderValue1;
              if (fProjectionTempY4 >= (double)fRenderValue1) {
                if (pNextGroundScreen->screenPtAy[5].projected.fZ >= (double)pNextGroundScreen->screenPtAy[0].projected.fZ)
                  fRenderValue2 = pNextGroundScreen->screenPtAy[0].projected.fZ;
                else
                  fRenderValue2 = pNextGroundScreen->screenPtAy[5].projected.fZ;
                fProjectionTempZ5 = fRenderValue2;
              } else {
                if (pNextGroundScreen->screenPtAy[1].projected.fZ >= (double)pNextGroundScreen->screenPtAy[4].projected.fZ)
                  fRenderValue2 = pNextGroundScreen->screenPtAy[4].projected.fZ;
                else
                  fRenderValue2 = pNextGroundScreen->screenPtAy[1].projected.fZ;
                fProjectionTempY5 = fRenderValue2;
              }
              fProjectionTempX5 = fRenderValue2;
              iRenderingTemp = (uint8)Subdivide[iSectionNum].subdivides[5];
              if ((double)(int16)iRenderingTemp * subscale > fRenderValue2) {
                subdivide(
                  pScrPtr_1,
                  &RoofPoly,
                  pNextGroundScreen->screenPtAy[1].projected.fX,
                  pNextGroundScreen->screenPtAy[1].projected.fY,
                  pNextGroundScreen->screenPtAy[1].projected.fZ,
                  pNextGroundScreen->screenPtAy[4].projected.fX,
                  pNextGroundScreen->screenPtAy[4].projected.fY,
                  pNextGroundScreen->screenPtAy[4].projected.fZ,
                  pNextGroundScreen->screenPtAy[5].projected.fX,
                  pNextGroundScreen->screenPtAy[5].projected.fY,
                  pNextGroundScreen->screenPtAy[5].projected.fZ,
                  pNextGroundScreen->screenPtAy[0].projected.fX,
                  pNextGroundScreen->screenPtAy[0].projected.fY,
                  pNextGroundScreen->screenPtAy[0].projected.fZ,
                  -1,
                  gfx_size);
                goto LABEL_1271;
              }
              if ((RoofPoly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) == 0) {
              LABEL_826:
                POLYFLAT(pScrPtr_1, &RoofPoly);
                goto LABEL_1203;
              }
            LABEL_924:
              POLYTEX(texture_vga, pScrPtr_1, &RoofPoly, 0, gfx_size);
              goto LABEL_1203;
            }
            if ((textures_off & TEX_OFF_GROUND_TEXTURES) != 0 && (RoofPoly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) != 0)
              RoofPoly.iSurfaceType = (RoofPoly.iSurfaceType & (SURFACE_MASK_FLAGS ^ SURFACE_FLAG_APPLY_TEXTURE)) + remap_tex[(uint8)(RoofPoly.iSurfaceType)];
            set_starts(0);
            if (pNextGroundScreen->screenPtAy[1].projected.fZ >= (double)pNextGroundScreen->screenPtAy[4].projected.fZ)
              fRenderValue3 = pNextGroundScreen->screenPtAy[4].projected.fZ;
            else
              fRenderValue3 = pNextGroundScreen->screenPtAy[1].projected.fZ;
            fScreenTempZ6 = fRenderValue3;
            if (pNextGroundScreen->screenPtAy[5].projected.fZ >= (double)pNextGroundScreen->screenPtAy[0].projected.fZ)
              fRenderValue4 = pNextGroundScreen->screenPtAy[0].projected.fZ;
            else
              fRenderValue4 = pNextGroundScreen->screenPtAy[5].projected.fZ;
            fProjectionTempX6 = fRenderValue4;
            if (fScreenTempZ6 >= (double)fRenderValue4) {
              if (pNextGroundScreen->screenPtAy[5].projected.fZ >= (double)pNextGroundScreen->screenPtAy[0].projected.fZ)
                fRenderValue5 = pNextGroundScreen->screenPtAy[0].projected.fZ;
              else
                fRenderValue5 = pNextGroundScreen->screenPtAy[5].projected.fZ;
              fProjectionTempX7 = fRenderValue5;
            } else {
              if (pNextGroundScreen->screenPtAy[1].projected.fZ >= (double)pNextGroundScreen->screenPtAy[4].projected.fZ)
                fRenderValue5 = pNextGroundScreen->screenPtAy[4].projected.fZ;
              else
                fRenderValue5 = pNextGroundScreen->screenPtAy[1].projected.fZ;
              fProjectionTempZ6 = fRenderValue5;
            }
            fProjectionTempY6 = fRenderValue5;
            iRenderingTemp = (uint8)Subdivide[iSectionNum].subdivides[5];
            if ((double)(int16)iRenderingTemp * subscale > fRenderValue5) {
              subdivide(
                pScrPtr_1,
                &RoofPoly,
                pNextGroundScreen->screenPtAy[1].projected.fX,
                pNextGroundScreen->screenPtAy[1].projected.fY,
                pNextGroundScreen->screenPtAy[1].projected.fZ,
                pNextGroundScreen->screenPtAy[4].projected.fX,
                pNextGroundScreen->screenPtAy[4].projected.fY,
                pNextGroundScreen->screenPtAy[4].projected.fZ,
                pNextGroundScreen->screenPtAy[5].projected.fX,
                pNextGroundScreen->screenPtAy[5].projected.fY,
                pNextGroundScreen->screenPtAy[5].projected.fZ,
                pNextGroundScreen->screenPtAy[0].projected.fX,
                pNextGroundScreen->screenPtAy[0].projected.fY,
                pNextGroundScreen->screenPtAy[0].projected.fZ,
                0,
                gfx_size);
              goto LABEL_1271;
            }
            if ((RoofPoly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) != 0)
              goto LABEL_963;
            goto LABEL_944;
          }
          if (!TrakColour[iSectionNum][TRAK_COLOUR_RIGHT_WALL] || !TrakColour[iSectionNum][TRAK_COLOUR_LEFT_WALL])
            goto LABEL_1271;
          iRenderCommandIndex = TrakColour[iNextSectionIndex][TRAK_COLOUR_ROOF];
          if (iRenderCommandIndex < -1) {
            RoofPoly.iSurfaceType = -iRenderCommandIndex;
            RoofPoly.vertices[0].x = GroundScreenXYZ[iSectionNum].screenPtAy[4].screen.x;
            RoofPoly.vertices[0].y = GroundScreenXYZ[iSectionNum].screenPtAy[4].screen.y;
            RoofPoly.vertices[1].x = GroundScreenXYZ[iSectionNum].screenPtAy[1].screen.x;
            RoofPoly.vertices[1].y = GroundScreenXYZ[iSectionNum].screenPtAy[1].screen.y;
            RoofPoly.vertices[2].x = GroundScreenXYZ[iSectionNum].screenPtAy[0].screen.x;
            RoofPoly.vertices[2].y = GroundScreenXYZ[iSectionNum].screenPtAy[0].screen.y;
            iScreenYCoord = GroundScreenXYZ[iSectionNum].screenPtAy[5].screen.y;
            RoofPoly.vertices[3].x = GroundScreenXYZ[iSectionNum].screenPtAy[5].screen.x;
            RoofPoly.vertices[3].y = iScreenYCoord;
            if ((-iRenderCommandIndex & SURFACE_FLAG_TEXTURE_PAIR) != 0) {
              set_starts(1u);
              if ((textures_off & TEX_OFF_GROUND_TEXTURES) != 0 && (RoofPoly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) != 0)
                RoofPoly.iSurfaceType = remap_tex[(uint8)(RoofPoly.iSurfaceType)] + (RoofPoly.iSurfaceType & (SURFACE_MASK_FLAGS ^ SURFACE_FLAG_APPLY_TEXTURE));
              if (pCurrentGroundScreen->screenPtAy[4].projected.fZ >= (double)pCurrentGroundScreen->screenPtAy[1].projected.fZ)
                fTrackDepth1 = pCurrentGroundScreen->screenPtAy[1].projected.fZ;
              else
                fTrackDepth1 = pCurrentGroundScreen->screenPtAy[4].projected.fZ;
              fScreenTempX7 = fTrackDepth1;
              if (pCurrentGroundScreen->screenPtAy[0].projected.fZ >= (double)pCurrentGroundScreen->screenPtAy[5].projected.fZ)
                fTrackDepth2 = pCurrentGroundScreen->screenPtAy[5].projected.fZ;
              else
                fTrackDepth2 = pCurrentGroundScreen->screenPtAy[0].projected.fZ;
              fScreenTempY7 = fTrackDepth2;
              if (fScreenTempX7 >= (double)fTrackDepth2) {
                if (pCurrentGroundScreen->screenPtAy[0].projected.fZ >= (double)pCurrentGroundScreen->screenPtAy[5].projected.fZ)
                  fTrackDepth3 = pCurrentGroundScreen->screenPtAy[5].projected.fZ;
                else
                  fTrackDepth3 = pCurrentGroundScreen->screenPtAy[0].projected.fZ;
                fScreenTempY8 = fTrackDepth3;
              } else {
                if (pCurrentGroundScreen->screenPtAy[4].projected.fZ >= (double)pCurrentGroundScreen->screenPtAy[1].projected.fZ)
                  fTrackDepth3 = pCurrentGroundScreen->screenPtAy[1].projected.fZ;
                else
                  fTrackDepth3 = pCurrentGroundScreen->screenPtAy[4].projected.fZ;
                fScreenTempX8 = fTrackDepth3;
              }
              fScreenTempZ7 = fTrackDepth3;
              iRenderingTemp = (uint8)Subdivide[iSectionNum].subdivides[5];
              if ((double)(int16)iRenderingTemp * subscale > fTrackDepth3) {
                subdivide(
                  pScrPtr_1,
                  &RoofPoly,
                  pCurrentGroundScreen->screenPtAy[4].projected.fX,
                  pCurrentGroundScreen->screenPtAy[4].projected.fY,
                  pCurrentGroundScreen->screenPtAy[4].projected.fZ,
                  pCurrentGroundScreen->screenPtAy[1].projected.fX,
                  pCurrentGroundScreen->screenPtAy[1].projected.fY,
                  pCurrentGroundScreen->screenPtAy[1].projected.fZ,
                  pCurrentGroundScreen->screenPtAy[0].projected.fX,
                  pCurrentGroundScreen->screenPtAy[0].projected.fY,
                  pCurrentGroundScreen->screenPtAy[0].projected.fZ,
                  pCurrentGroundScreen->screenPtAy[5].projected.fX,
                  pCurrentGroundScreen->screenPtAy[5].projected.fY,
                  pCurrentGroundScreen->screenPtAy[5].projected.fZ,
                  -1,
                  gfx_size);
                goto LABEL_1271;
              }
              if ((RoofPoly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) == 0)
                goto LABEL_826;
              goto LABEL_924;
            }
            if ((textures_off & TEX_OFF_GROUND_TEXTURES) != 0 && (RoofPoly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) != 0)
              RoofPoly.iSurfaceType = remap_tex[(uint8)(RoofPoly.iSurfaceType)] + (RoofPoly.iSurfaceType & (SURFACE_MASK_FLAGS ^ SURFACE_FLAG_APPLY_TEXTURE));
            set_starts(0);
            if (pCurrentGroundScreen->screenPtAy[4].projected.fZ >= (double)pCurrentGroundScreen->screenPtAy[1].projected.fZ)
              fTrackDepth4 = pCurrentGroundScreen->screenPtAy[1].projected.fZ;
            else
              fTrackDepth4 = pCurrentGroundScreen->screenPtAy[4].projected.fZ;
            fProjectionTempY7 = fTrackDepth4;
            if (pCurrentGroundScreen->screenPtAy[0].projected.fZ >= (double)pCurrentGroundScreen->screenPtAy[5].projected.fZ)
              fTrackDepth5 = pCurrentGroundScreen->screenPtAy[5].projected.fZ;
            else
              fTrackDepth5 = pCurrentGroundScreen->screenPtAy[0].projected.fZ;
            fProjectionTempZ7 = fTrackDepth5;
            if (fProjectionTempY7 >= (double)fTrackDepth5) {
              if (pCurrentGroundScreen->screenPtAy[0].projected.fZ >= (double)pCurrentGroundScreen->screenPtAy[5].projected.fZ)
                fTrackDepth6 = pCurrentGroundScreen->screenPtAy[5].projected.fZ;
              else
                fTrackDepth6 = pCurrentGroundScreen->screenPtAy[0].projected.fZ;
              fScreenTempZ8 = fTrackDepth6;
            } else {
              if (pCurrentGroundScreen->screenPtAy[4].projected.fZ >= (double)pCurrentGroundScreen->screenPtAy[1].projected.fZ)
                fTrackDepth6 = pCurrentGroundScreen->screenPtAy[1].projected.fZ;
              else
                fTrackDepth6 = pCurrentGroundScreen->screenPtAy[4].projected.fZ;
              fProjectionTempY8 = fTrackDepth6;
            }
            fProjectionTempX8 = fTrackDepth6;
            iRenderingTemp = (uint8)Subdivide[iSectionNum].subdivides[5];
            if ((double)(int16)iRenderingTemp * subscale > fTrackDepth6) {
              subdivide(
                pScrPtr_1,
                &RoofPoly,
                pCurrentGroundScreen->screenPtAy[4].projected.fX,
                pCurrentGroundScreen->screenPtAy[4].projected.fY,
                pCurrentGroundScreen->screenPtAy[4].projected.fZ,
                pCurrentGroundScreen->screenPtAy[1].projected.fX,
                pCurrentGroundScreen->screenPtAy[1].projected.fY,
                pCurrentGroundScreen->screenPtAy[1].projected.fZ,
                pCurrentGroundScreen->screenPtAy[0].projected.fX,
                pCurrentGroundScreen->screenPtAy[0].projected.fY,
                pCurrentGroundScreen->screenPtAy[0].projected.fZ,
                pCurrentGroundScreen->screenPtAy[5].projected.fX,
                pCurrentGroundScreen->screenPtAy[5].projected.fY,
                pCurrentGroundScreen->screenPtAy[5].projected.fZ,
                0,
                gfx_size);
              goto LABEL_1271;
            }
            if ((RoofPoly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) != 0) {
            LABEL_963:
              POLYTEX(texture_vga, pScrPtr_1, &RoofPoly, 0, gfx_size);
              goto LABEL_1227;
            }
          LABEL_944:
            POLYFLAT(pScrPtr_1, &RoofPoly);
          LABEL_1227:
            set_starts(0);
            goto LABEL_1271;
          }
          if ((textures_off & TEX_OFF_WALL_TEXTURES) != 0 && (RoofPoly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) != 0)
            RoofPoly.iSurfaceType = remap_tex[(uint8)(RoofPoly.iSurfaceType)] + (RoofPoly.iSurfaceType & (SURFACE_MASK_FLAGS ^ SURFACE_FLAG_APPLY_TEXTURE));
          if ((RoofPoly.iSurfaceType & SURFACE_FLAG_TEXTURE_PAIR) != 0) {
            RoofPoly.vertices[0] = pScreenCoord->screenPtAy[4].screen;
            RoofPoly.vertices[1] = pScreenCoord->screenPtAy[5].screen;
            RoofPoly.vertices[2] = pScreenCoord_1->screenPtAy[5].screen;
            RoofPoly.vertices[3] = pScreenCoord_1->screenPtAy[4].screen;
            if (wide_on) {
              set_starts(1u);
              if (pScreenCoord->screenPtAy[4].projected.fZ >= (double)pScreenCoord->screenPtAy[5].projected.fZ)
                fTrackDepth7 = pScreenCoord->screenPtAy[5].projected.fZ;
              else
                fTrackDepth7 = pScreenCoord->screenPtAy[4].projected.fZ;
              fScreenTempX9 = fTrackDepth7;
              if (pScreenCoord_1->screenPtAy[5].projected.fZ >= (double)pScreenCoord_1->screenPtAy[4].projected.fZ)
                fTrackDepth8 = pScreenCoord_1->screenPtAy[4].projected.fZ;
              else
                fTrackDepth8 = pScreenCoord_1->screenPtAy[5].projected.fZ;
              fScreenTempY9 = fTrackDepth8;
              if (fScreenTempX9 >= (double)fTrackDepth8) {
                if (pScreenCoord_1->screenPtAy[5].projected.fZ >= (double)pScreenCoord_1->screenPtAy[4].projected.fZ)
                  fTrackDepth9 = pScreenCoord_1->screenPtAy[4].projected.fZ;
                else
                  fTrackDepth9 = pScreenCoord_1->screenPtAy[5].projected.fZ;
                fProjectionTempY9 = fTrackDepth9;
              } else {
                if (pScreenCoord->screenPtAy[4].projected.fZ >= (double)pScreenCoord->screenPtAy[5].projected.fZ)
                  fTrackDepth9 = pScreenCoord->screenPtAy[5].projected.fZ;
                else
                  fTrackDepth9 = pScreenCoord->screenPtAy[4].projected.fZ;
                fProjectionTempX9 = fTrackDepth9;
              }
              fProjectionTempZ8 = fTrackDepth9;
              iRenderingTemp = (uint8)Subdivide[iSectionNum].subdivides[5];
              if ((double)(int16)iRenderingTemp * subscale > fTrackDepth9) {
                subdivide(
                  pScrPtr_1,
                  &RoofPoly,
                  pScreenCoord->screenPtAy[4].projected.fX,
                  pScreenCoord->screenPtAy[4].projected.fY,
                  pScreenCoord->screenPtAy[4].projected.fZ,
                  pScreenCoord->screenPtAy[5].projected.fX,
                  pScreenCoord->screenPtAy[5].projected.fY,
                  pScreenCoord->screenPtAy[5].projected.fZ,
                  pScreenCoord_1->screenPtAy[5].projected.fX,
                  pScreenCoord_1->screenPtAy[5].projected.fY,
                  pScreenCoord_1->screenPtAy[5].projected.fZ,
                  pScreenCoord_1->screenPtAy[4].projected.fX,
                  pScreenCoord_1->screenPtAy[4].projected.fY,
                  pScreenCoord_1->screenPtAy[4].projected.fZ,
                  -1,
                  gfx_size);
                goto LABEL_1271;
              }
              if ((RoofPoly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) == 0)
                goto LABEL_826;
              goto LABEL_924;
            }
            set_starts(0);
            if (pScreenCoord->screenPtAy[4].projected.fZ >= (double)pScreenCoord->screenPtAy[5].projected.fZ)
              fTrackDepth10 = pScreenCoord->screenPtAy[5].projected.fZ;
            else
              fTrackDepth10 = pScreenCoord->screenPtAy[4].projected.fZ;
            fProjectionTempZ9 = fTrackDepth10;
            if (pScreenCoord_1->screenPtAy[5].projected.fZ >= (double)pScreenCoord_1->screenPtAy[4].projected.fZ)
              fTrackDepth11 = pScreenCoord_1->screenPtAy[4].projected.fZ;
            else
              fTrackDepth11 = pScreenCoord_1->screenPtAy[5].projected.fZ;
            fProjectionTempX10 = fTrackDepth11;
            if (fProjectionTempZ9 >= (double)fTrackDepth11) {
              if (pScreenCoord_1->screenPtAy[5].projected.fZ >= (double)pScreenCoord_1->screenPtAy[4].projected.fZ)
                fTrackDepth12 = pScreenCoord_1->screenPtAy[4].projected.fZ;
              else
                fTrackDepth12 = pScreenCoord_1->screenPtAy[5].projected.fZ;
              fScreenTempX10 = fTrackDepth12;
            } else {
              if (pScreenCoord->screenPtAy[4].projected.fZ >= (double)pScreenCoord->screenPtAy[5].projected.fZ)
                fTrackDepth12 = pScreenCoord->screenPtAy[5].projected.fZ;
              else
                fTrackDepth12 = pScreenCoord->screenPtAy[4].projected.fZ;
              fScreenTempZ9 = fTrackDepth12;
            }
            fProjectionTempY10 = fTrackDepth12;
            iRenderingTemp = (uint8)Subdivide[iSectionNum].subdivides[5];
            if ((double)(int16)iRenderingTemp * subscale <= fTrackDepth12) {
              if ((RoofPoly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) != 0)
                goto LABEL_963;
              goto LABEL_944;
            }
            subdivide(
              pScrPtr_1,
              &RoofPoly,
              pScreenCoord->screenPtAy[4].projected.fX,
              pScreenCoord->screenPtAy[4].projected.fY,
              pScreenCoord->screenPtAy[4].projected.fZ,
              pScreenCoord->screenPtAy[5].projected.fX,
              pScreenCoord->screenPtAy[5].projected.fY,
              pScreenCoord->screenPtAy[5].projected.fZ,
              pScreenCoord_1->screenPtAy[5].projected.fX,
              pScreenCoord_1->screenPtAy[5].projected.fY,
              pScreenCoord_1->screenPtAy[5].projected.fZ,
              pScreenCoord_1->screenPtAy[4].projected.fX,
              pScreenCoord_1->screenPtAy[4].projected.fY,
              pScreenCoord_1->screenPtAy[4].projected.fZ,
              0,
              gfx_size);
          } else {
            RoofPoly.vertices[0] = pScreenCoord_1->screenPtAy[4].screen;
            RoofPoly.vertices[1] = pScreenCoord->screenPtAy[4].screen;
            RoofPoly.vertices[2] = pScreenCoord->screenPtAy[5].screen;
            RoofPoly.vertices[3] = pScreenCoord_1->screenPtAy[5].screen;
            set_starts(0);
            if (pScreenCoord_1->screenPtAy[4].projected.fZ >= (double)pScreenCoord->screenPtAy[4].projected.fZ)
              fTrackDepth13 = pScreenCoord->screenPtAy[4].projected.fZ;
            else
              fTrackDepth13 = pScreenCoord_1->screenPtAy[4].projected.fZ;
            fProjectionTempX11 = fTrackDepth13;
            if (pScreenCoord->screenPtAy[5].projected.fZ >= (double)pScreenCoord_1->screenPtAy[5].projected.fZ)
              fTrackDepth14 = pScreenCoord_1->screenPtAy[5].projected.fZ;
            else
              fTrackDepth14 = pScreenCoord->screenPtAy[5].projected.fZ;
            fProjectionTempY11 = fTrackDepth14;
            if (fProjectionTempX11 >= (double)fTrackDepth14) {
              if (pScreenCoord->screenPtAy[5].projected.fZ >= (double)pScreenCoord_1->screenPtAy[5].projected.fZ)
                fTrackDepth15 = pScreenCoord_1->screenPtAy[5].projected.fZ;
              else
                fTrackDepth15 = pScreenCoord->screenPtAy[5].projected.fZ;
              fScreenTempZ11 = fTrackDepth15;
            } else {
              if (pScreenCoord_1->screenPtAy[4].projected.fZ >= (double)pScreenCoord->screenPtAy[4].projected.fZ)
                fTrackDepth15 = pScreenCoord->screenPtAy[4].projected.fZ;
              else
                fTrackDepth15 = pScreenCoord_1->screenPtAy[4].projected.fZ;
              fScreenTempY11 = fTrackDepth15;
            }
            fScreenTempX11 = fTrackDepth15;
            iRenderingTemp = (uint8)Subdivide[iSectionNum].subdivides[5];
            if ((double)(int16)iRenderingTemp * subscale <= fTrackDepth15) {
              if ((RoofPoly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) != 0)
                goto LABEL_963;
              goto LABEL_944;
            }
            subdivide(
              pScrPtr_1,
              &RoofPoly,
              pScreenCoord_1->screenPtAy[4].projected.fX,
              pScreenCoord_1->screenPtAy[4].projected.fY,
              pScreenCoord_1->screenPtAy[4].projected.fZ,
              pScreenCoord->screenPtAy[4].projected.fX,
              pScreenCoord->screenPtAy[4].projected.fY,
              pScreenCoord->screenPtAy[4].projected.fZ,
              pScreenCoord->screenPtAy[5].projected.fX,
              pScreenCoord->screenPtAy[5].projected.fY,
              pScreenCoord->screenPtAy[5].projected.fZ,
              pScreenCoord_1->screenPtAy[5].projected.fX,
              pScreenCoord_1->screenPtAy[5].projected.fY,
              pScreenCoord_1->screenPtAy[5].projected.fZ,
              0,
              gfx_size);
          }
          goto LABEL_1271;
        case 0xB:
          if (CarsLeft < 7 && CarsLeft > -3 || winner_mode || replaytype == 2 || g_bForceMaxDraw)
            DisplayCar(iSectionNum, pScrPtr_1, fRenderDepth);
          --CarsLeft;
          if (names_on && (names_on == 1 || human_control[iSectionNum]))
            --NamesLeft;
          goto LABEL_1271;
        case 0xC:
          DrawTower(iSectionNum, pScrPtr_1);
          goto LABEL_1271;
        case 0xD:
          DrawBuilding(iSectionNum, pScrPtr_1);
          goto LABEL_1271;
        case 0xE:
          iScreenIndex1 = iIndexTmp1 + 48 * iSectionNum;
          fRenderDepthTmp14 = *(float *)((char *)&SLight[0][0].currentPos.fX + iScreenIndex1);
          fRenderDepthTmp13 = *(float *)((char *)&SLight[0][0].currentPos.fY + iScreenIndex1);
          iScreenIndex2 = *(uint32 *)((char *)&SLight[0][0].uiRotation + iScreenIndex1);
          fRenderDepthTmp12 = *(float *)((char *)&SLight[0][0].currentPos.fZ + iScreenIndex1);
          iScreenIndex3 = ((int16)iScreenIndex2 + (int16)worlddirn) & 0x3FFF;
          fTransformTempZ2 = tcos[iScreenIndex3] * tcos[0];
          fTransformTempY2 = tsin[iScreenIndex3] * tcos[0];
          dTransform1 = tcos[iScreenIndex3];
          fTransformTempX2 = tsin[0];
          dTransform2 = dTransform1 * tsin[0];
          fTransformTempZ1 = (float)dTransform2 * tsin[0] - fTransformTempY2;
          dTransform3 = tsin[iScreenIndex3] * tsin[0];
          fTransformTempY1 = (float)dTransform3 * tsin[0] + fTransformTempZ2;
          fTransformTempX1 = -tsin[0] * tcos[0];
          fRenderDepthTmp9 = -tcos[iScreenIndex3] * tsin[0] * tcos[0] - (float)dTransform3;
          fRenderDepthTmp8 = (float)dTransform2 + -tsin[iScreenIndex3] * tsin[0] * tcos[0];
          fRenderDepthTmp7 = tcos[0] * tcos[0];
          fRenderDepthTmp11 = -100.0f * fTransformTempZ2 + 100.0f * fTransformTempZ1 - 100.0f * fRenderDepthTmp9 + fRenderDepthTmp14;
          fTransformTempFinal = -100.0f * tsin[0] + 100.0f * fTransformTempX1 - 100.0f * fRenderDepthTmp7 + fRenderDepthTmp12;
          dTransform4 = fRenderDepthTmp11 - viewx;
          dTransform5 = -100.0 * fTransformTempY2 + 100.0 * fTransformTempY1 - 100.0 * fRenderDepthTmp8 + fRenderDepthTmp13 - viewy;
          dTransform6 = fTransformTempFinal - viewz;
          fCameraTransformZ1 = (float)(dTransform4 * vk1 + dTransform5 * vk4 + dTransform6 * vk7);
          fCameraTransformY1 = (float)(dTransform4 * vk2 + dTransform5 * vk5 + dTransform6 * vk8);
          dTransform7 = dTransform4 * vk3 + dTransform5 * vk6 + dTransform6 * vk9;
          fCameraTransformX1 = (float)dTransform7;
          dTransform7 = round(dTransform7);//_CHP();
          iOffsetTmp2 = (int)dTransform7;
          if (fCameraTransformX1 < 80.0)
            fCameraTransformX1 = 80.0;
          dTransform8 = (double)VIEWDIST;
          dTransform9 = 1.0 / fCameraTransformX1;
          dTransform10 = dTransform8 * fCameraTransformZ1 * dTransform9 + (double)xbase;
          dTransform10 = round(dTransform10);//_CHP();
          xp = (int)dTransform10;
          dTransform11 = dTransform9 * (dTransform8 * fCameraTransformY1) + (double)ybase;
          dTransform11 = round(dTransform11);//_CHP();
          yp = (int)dTransform11;
          LightXYZ[0].projected.fZ = (float)iOffsetTmp2;
          dTransform12 = -100.0 * fTransformTempZ2 - 100.0 * fTransformTempZ1 - 100.0 * fRenderDepthTmp9 + fRenderDepthTmp14 - viewx;
          dTransform13 = -100.0 * fTransformTempY2 - 100.0 * fTransformTempY1 - 100.0 * fRenderDepthTmp8 + fRenderDepthTmp13 - viewy;
          dTransform14 = -100.0 * fTransformTempX2 - 100.0 * fTransformTempX1 - 100.0 * fRenderDepthTmp7 + fRenderDepthTmp12 - viewz;
          LightXYZ[0].screen.x = (scr_size * xp) >> 6;
          LightXYZ[0].screen.y = (scr_size * (199 - yp)) >> 6;
          iTransformInt1 = fCameraTransformZ1;
          fCameraTransformZ1 = (float)(dTransform12 * vk1 + dTransform13 * vk4 + dTransform14 * vk7);
          LightXYZ[0].projected.fX = iTransformInt1;
          fCameraTransformY1 = (float)(dTransform12 * vk2 + dTransform13 * vk5 + dTransform14 * vk8);
          dTransform15 = dTransform12 * vk3 + dTransform13 * vk6 + dTransform14 * vk9;
          fCameraTransformX1 = (float)dTransform15;
          dTransform15 = round(dTransform15);//_CHP();
          iOffsetTmp2 = (int)dTransform15;
          LightXYZ[0].projected.fY = (float)(int)dTransform15;// iTransformInt2;
          if (fCameraTransformX1 < 80.0)
            fCameraTransformX1 = 80.0;
          dTransform16 = (double)VIEWDIST;
          dTransform17 = 1.0 / fCameraTransformX1;
          dTransform18 = dTransform16 * fCameraTransformZ1 * dTransform17 + (double)xbase;
          dTransform18 = round(dTransform18);//_CHP();
          xp = (int)dTransform18;
          dTransform19 = dTransform17 * (dTransform16 * fCameraTransformY1) + (double)ybase;
          dTransform19 = round(dTransform19);//_CHP();
          yp = (int)dTransform19;
          LightXYZ[1].projected.fZ = (float)iOffsetTmp2;
          dTransform20 = 100.0 * fTransformTempZ2 - 100.0 * fTransformTempZ1 - 100.0 * fRenderDepthTmp9 + fRenderDepthTmp14 - viewx;
          dTransform21 = 100.0 * fTransformTempY2 - 100.0 * fTransformTempY1 - 100.0 * fRenderDepthTmp8 + fRenderDepthTmp13 - viewy;
          dTransform22 = 100.0 * fTransformTempX2 - 100.0 * fTransformTempX1 - 100.0 * fRenderDepthTmp7 + fRenderDepthTmp12 - viewz;
          LightXYZ[1].screen.x = (scr_size * xp) >> 6;
          LightXYZ[1].screen.y = (scr_size * (199 - yp)) >> 6;
          iTransformInt3 = fCameraTransformZ1;
          fCameraTransformZ1 = (float)(dTransform20 * vk1 + dTransform21 * vk4 + dTransform22 * vk7);
          LightXYZ[1].projected.fX = iTransformInt3;
          fCameraTransformY1 = (float)(dTransform20 * vk2 + dTransform21 * vk5 + dTransform22 * vk8);
          dTransform23 = dTransform20 * vk3 + dTransform21 * vk6 + dTransform22 * vk9;
          fCameraTransformX1 = (float)dTransform23;
          dTransform23 = round(dTransform23);//_CHP();
          iOffsetTmp2 = (int)dTransform23;
          LightXYZ[1].projected.fY = (float)(int)dTransform23;// iTransformInt4;
          if (fCameraTransformX1 < 80.0)
            fCameraTransformX1 = 80.0;
          dTransform24 = (double)VIEWDIST;
          dTransform25 = 1.0 / fCameraTransformX1;
          dTransform26 = dTransform24 * fCameraTransformZ1 * dTransform25 + (double)xbase;
          dTransform26 = round(dTransform26);//_CHP();
          xp = (int)dTransform26;
          dTransform27 = dTransform25 * (dTransform24 * fCameraTransformY1) + (double)ybase;
          dTransform27 = round(dTransform27);//_CHP();
          yp = (int)dTransform27;
          LightXYZ[2].projected.fZ = (float)iOffsetTmp2;
          dTransform28 = 100.0 * fTransformTempZ2 + 100.0 * fTransformTempZ1 - 100.0 * fRenderDepthTmp9 + fRenderDepthTmp14 - viewx;
          dTransform29 = 100.0 * fTransformTempY2 + 100.0 * fTransformTempY1 - 100.0 * fRenderDepthTmp8 + fRenderDepthTmp13 - viewy;
          dTransform30 = 100.0 * fTransformTempX2 + 100.0 * fTransformTempX1 - 100.0 * fRenderDepthTmp7 + fRenderDepthTmp12 - viewz;
          LightXYZ[2].screen.x = (scr_size * xp) >> 6;
          LightXYZ[2].screen.y = (scr_size * (199 - yp)) >> 6;
          iTransformInt5 = fCameraTransformZ1;
          fCameraTransformZ1 = (float)(dTransform28 * vk1 + dTransform29 * vk4 + dTransform30 * vk7);
          LightXYZ[2].projected.fX = iTransformInt5;
          fCameraTransformY1 = (float)(dTransform28 * vk2 + dTransform29 * vk5 + dTransform30 * vk8);
          dTransform31 = dTransform28 * vk3 + dTransform29 * vk6 + dTransform30 * vk9;
          fCameraTransformX1 = (float)dTransform31;
          dTransform31 = round(dTransform31);//_CHP();
          iOffsetTmp2 = (int)dTransform31;
          LightXYZ[2].projected.fY = (float)(int)dTransform31;// iTransformInt6;
          if (fCameraTransformX1 < 80.0)
            fCameraTransformX1 = 80.0;
          dTransform32 = (double)VIEWDIST;
          dTransform33 = 1.0 / fCameraTransformX1;
          dTransform34 = dTransform32 * fCameraTransformZ1 * dTransform33 + (double)xbase;
          dTransform34 = round(dTransform34);//_CHP();
          xp = (int)dTransform34;
          dProjectionDepth1 = dTransform33 * (dTransform32 * fCameraTransformY1) + (double)ybase;
          dProjectionDepth1 = round(dProjectionDepth1);//_CHP();
          yp = (int)dProjectionDepth1;
          LightXYZ[3].projected.fZ = (float)iOffsetTmp2;
          dProjectionDepth2 = -100.0 * fTransformTempZ2 + 100.0 * fTransformTempZ1 + 100.0 * fRenderDepthTmp9 + fRenderDepthTmp14 - viewx;
          dProjectionDepth3 = -100.0 * fTransformTempY2 + 100.0 * fTransformTempY1 + 100.0 * fRenderDepthTmp8 + fRenderDepthTmp13 - viewy;
          dProjectionDepth4 = -100.0 * fTransformTempX2 + 100.0 * fTransformTempX1 + 100.0 * fRenderDepthTmp7 + fRenderDepthTmp12 - viewz;
          LightXYZ[3].screen.x = (scr_size * xp) >> 6;
          LightXYZ[3].screen.y = (scr_size * (199 - yp)) >> 6;
          iProjectionIndex1 = fCameraTransformZ1;
          fCameraTransformZ1 = (float)(dProjectionDepth2 * vk1 + dProjectionDepth3 * vk4 + dProjectionDepth4 * vk7);
          LightXYZ[3].projected.fX = iProjectionIndex1;
          fCameraTransformY1 = (float)(dProjectionDepth2 * vk2 + dProjectionDepth3 * vk5 + dProjectionDepth4 * vk8);
          dProjectionDepth5 = dProjectionDepth2 * vk3 + dProjectionDepth3 * vk6 + dProjectionDepth4 * vk9;
          fCameraTransformX1 = (float)dProjectionDepth5;
          dProjectionDepth5 = round(dProjectionDepth5);//_CHP();
          iOffsetTmp2 = (int)dProjectionDepth5;
          LightXYZ[3].projected.fY = (float)(int)dProjectionDepth5;// iProjectionIndex2;
          if (fCameraTransformX1 < 80.0)
            fCameraTransformX1 = 80.0;
          dProjectionDepth6 = (double)VIEWDIST;
          dProjectionDepth7 = 1.0 / fCameraTransformX1;
          dProjectionDepth8 = dProjectionDepth6 * fCameraTransformZ1 * dProjectionDepth7 + (double)xbase;
          dProjectionDepth8 = round(dProjectionDepth8);//_CHP();
          xp = (int)dProjectionDepth8;
          dProjectionDepth9 = dProjectionDepth7 * (dProjectionDepth6 * fCameraTransformY1) + (double)ybase;
          dProjectionDepth9 = round(dProjectionDepth9);//_CHP();
          yp = (int)dProjectionDepth9;
          LightXYZ[4].projected.fZ = (float)iOffsetTmp2;
          dProjectionDepth10 = -100.0 * fTransformTempZ2 - 100.0 * fTransformTempZ1 + 100.0 * fRenderDepthTmp9 + fRenderDepthTmp14 - viewx;
          dProjectionDepth11 = -100.0 * fTransformTempY2 - 100.0 * fTransformTempY1 + 100.0 * fRenderDepthTmp8 + fRenderDepthTmp13 - viewy;
          dProjectionDepth12 = -100.0 * fTransformTempX2 - 100.0 * fTransformTempX1 + 100.0 * fRenderDepthTmp7 + fRenderDepthTmp12 - viewz;
          LightXYZ[4].screen.x = (scr_size * xp) >> 6;
          LightXYZ[4].screen.y = (scr_size * (199 - yp)) >> 6;
          iProjectionIndex3 = fCameraTransformZ1;
          fCameraTransformZ1 = (float)(dProjectionDepth10 * vk1 + dProjectionDepth11 * vk4 + dProjectionDepth12 * vk7);
          LightXYZ[4].projected.fX = iProjectionIndex3;
          fCameraTransformY1 = (float)(dProjectionDepth10 * vk2 + dProjectionDepth11 * vk5 + dProjectionDepth12 * vk8);
          dProjectionDepth13 = dProjectionDepth10 * vk3 + dProjectionDepth11 * vk6 + dProjectionDepth12 * vk9;
          fCameraTransformX1 = (float)dProjectionDepth13;
          dProjectionDepth13 = round(dProjectionDepth13);//_CHP();
          iOffsetTmp2 = (int)dProjectionDepth13;
          LightXYZ[4].projected.fY = (float)(int)dProjectionDepth13;// iProjectionIndex4;
          if (fCameraTransformX1 < 80.0)
            fCameraTransformX1 = 80.0;
          dProjectionDepth14 = (double)VIEWDIST;
          dProjectionDepth15 = 1.0 / fCameraTransformX1;
          dProjectionDepth16 = dProjectionDepth14 * fCameraTransformZ1 * dProjectionDepth15 + (double)xbase;
          dProjectionDepth16 = round(dProjectionDepth16);//_CHP();
          xp = (int)dProjectionDepth16;
          dProjectionDepth17 = dProjectionDepth15 * (dProjectionDepth14 * fCameraTransformY1) + (double)ybase;
          dProjectionDepth17 = round(dProjectionDepth17);//_CHP();
          yp = (int)dProjectionDepth17;
          LightXYZ[5].projected.fZ = (float)iOffsetTmp2;
          dProjectionDepth18 = 100.0 * fTransformTempZ2 - 100.0 * fTransformTempZ1 + 100.0 * fRenderDepthTmp9 + fRenderDepthTmp14 - viewx;
          dProjectionDepth19 = 100.0 * fTransformTempY2 - 100.0 * fTransformTempY1 + 100.0 * fRenderDepthTmp8 + fRenderDepthTmp13 - viewy;
          dProjectionDepth20 = 100.0 * fTransformTempX2 - 100.0 * fTransformTempX1 + 100.0 * fRenderDepthTmp7 + fRenderDepthTmp12 - viewz;
          LightXYZ[5].screen.x = (scr_size * xp) >> 6;
          LightXYZ[5].screen.y = (scr_size * (199 - yp)) >> 6;
          iProjectionIndex5 = fCameraTransformZ1;
          fCameraTransformZ1 = (float)(dProjectionDepth18 * vk1 + dProjectionDepth19 * vk4 + dProjectionDepth20 * vk7);
          LightXYZ[5].projected.fX = iProjectionIndex5;
          fCameraTransformY1 = (float)(dProjectionDepth18 * vk2 + dProjectionDepth19 * vk5 + dProjectionDepth20 * vk8);
          dProjectionDepth21 = dProjectionDepth18 * vk3 + dProjectionDepth19 * vk6 + dProjectionDepth20 * vk9;
          fCameraTransformX1 = (float)dProjectionDepth21;
          dProjectionDepth21 = round(dProjectionDepth21);//_CHP();
          iOffsetTmp2 = (int)dProjectionDepth21;
          LightXYZ[5].projected.fY = (float)(int)dProjectionDepth21; //iProjectionIndex6;
          if (fCameraTransformX1 < 80.0)
            fCameraTransformX1 = 80.0;
          dProjectionDepth22 = (double)VIEWDIST;
          dProjectionDepth23 = 1.0 / fCameraTransformX1;
          dProjectionDepth24 = dProjectionDepth22 * fCameraTransformZ1 * dProjectionDepth23 + (double)xbase;
          dProjectionDepth24 = round(dProjectionDepth24);//_CHP();
          xp = (int)dProjectionDepth24;
          dProjectionDepth25 = dProjectionDepth23 * (dProjectionDepth22 * fCameraTransformY1) + (double)ybase;
          dProjectionDepth25 = round(dProjectionDepth25);//_CHP();
          yp = (int)dProjectionDepth25;
          LightXYZ[6].projected.fZ = (float)iOffsetTmp2;
          dProjectionDepth26 = 100.0 * fTransformTempZ2 + 100.0 * fTransformTempZ1 + 100.0 * fRenderDepthTmp9 + fRenderDepthTmp14 - viewx;
          dProjectionDepth27 = 100.0 * fTransformTempY2 + 100.0 * fTransformTempY1 + 100.0 * fRenderDepthTmp8 + fRenderDepthTmp13 - viewy;
          dProjectionDepth28 = 100.0 * fTransformTempX2 + 100.0 * fTransformTempX1 + 100.0 * fRenderDepthTmp7 + fRenderDepthTmp12 - viewz;
          LightXYZ[6].screen.x = (scr_size * xp) >> 6;
          LightXYZ[6].screen.y = (scr_size * (199 - yp)) >> 6;
          iProjectionIndex7 = fCameraTransformZ1;
          fCameraTransformZ1 = (float)(dProjectionDepth26 * vk1 + dProjectionDepth27 * vk4 + dProjectionDepth28 * vk7);
          LightXYZ[6].projected.fX = iProjectionIndex7;
          fCameraTransformY1 = (float)(dProjectionDepth26 * vk2 + dProjectionDepth27 * vk5 + dProjectionDepth28 * vk8);
          dProjectionDepth29 = dProjectionDepth26 * vk3 + dProjectionDepth27 * vk6 + dProjectionDepth28 * vk9;
          fCameraTransformX1 = (float)dProjectionDepth29;
          dProjectionDepth29 = round(dProjectionDepth29);//_CHP();
          iOffsetTmp2 = (int)dProjectionDepth29;
          LightXYZ[6].projected.fY = (float)(int)dProjectionDepth29; //iProjectionIndex8;
          if (fCameraTransformX1 < 80.0)
            fCameraTransformX1 = 80.0;
          dProjectionDepth30 = (double)VIEWDIST;
          dProjectionDepth31 = 1.0 / fCameraTransformX1;
          dProjectionDepth32 = dProjectionDepth30 * fCameraTransformZ1 * dProjectionDepth31 + (double)xbase;
          dProjectionDepth32 = round(dProjectionDepth32);//_CHP();
          xp = (int)dProjectionDepth32;
          dProjectionDepth33 = dProjectionDepth31 * (dProjectionDepth30 * fCameraTransformY1) + (double)ybase;
          iRenderingIndex1 = scr_size;
          dProjectionDepth33 = round(dProjectionDepth33);//_CHP();
          yp = (int)dProjectionDepth33;
          LightXYZ[7].screen.x = xp * scr_size >> 6;
          //LightXYZ[7].screen.x = iRenderingIndex2 >> 6;
          LightXYZ[7].screen.y = (iRenderingIndex1 * (199 - (int)dProjectionDepth33)) >> 6;
          LightXYZ[7].projected.fX = fCameraTransformZ1;
          RoadPoly.uiNumVerts = 4;
          LightXYZ[7].projected.fY = fCameraTransformY1;
          LightXYZ[7].projected.fZ = (float)iOffsetTmp2;
          if (countdown >= 0) {
            if (countdown >= 72)
              RoadPoly.iSurfaceType = 0x2101;
            else
              RoadPoly.iSurfaceType = 0x2102;
          } else {
            RoadPoly.iSurfaceType = 0x2103;
          }
          iRenderingIndex3 = 0;
          iRenderingIndex4 = 0;
          do {
            dRenderingDepth1 = (LightXYZ[cube_faces[iRenderingIndex4][0]].projected.fZ
                              + LightXYZ[cube_faces[iRenderingIndex4][1]].projected.fZ
                              + LightXYZ[cube_faces[iRenderingIndex4][2]].projected.fZ
                              + LightXYZ[cube_faces[iRenderingIndex4][3]].projected.fZ)
              * 0.25;
            ++iRenderingIndex3;
            ++iRenderingIndex4;
            fDepthValuesArray[iRenderingIndex3 - 1] = (float)dRenderingDepth1;
          } while (iRenderingIndex3 != 6);
          iNextSectionIndex = 0;
          set_starts(0);
          do {
            iRenderingIndex5 = 1;
            iRenderingIndex6 = 1;
            iRenderingIndex7 = 0;
            fLightTmp1 = fDepthValuesArray[0];
            do {
              if (fDepthValuesArray[iRenderingIndex6] > (double)fLightTmp1) {
                iRenderingIndex7 = iRenderingIndex5;
                fLightTmp1 = fDepthValuesArray[iRenderingIndex6];
              }
              ++iRenderingIndex5;
              ++iRenderingIndex6;
            } while (iRenderingIndex5 < 6);
            fDepthValuesArray[iRenderingIndex7] = -9.9999998e17f;
            iRenderingIndex8 = iRenderingIndex7;
            iRenderingIndex9 = cube_faces[iRenderingIndex8][0];
            iRenderingIndex10 = LightXYZ[iRenderingIndex9].screen.x;
            iRenderingIndex11 = cube_faces[iRenderingIndex8][1];
            RoadPoly.vertices[0].y = LightXYZ[iRenderingIndex9].screen.y;
            RoadPoly.vertices[0].x = iRenderingIndex10;
            iRenderLoopVar = LightXYZ[iRenderingIndex11].screen.x;
            iRenderingLoopIndex = cube_faces[iRenderingIndex8][2];
            RoadPoly.vertices[1].y = LightXYZ[iRenderingIndex11].screen.y;
            RoadPoly.vertices[1].x = iRenderLoopVar;
            RoadPoly.vertices[2].x = LightXYZ[iRenderingLoopIndex].screen.x;
            iRenderingIndexTmp = cube_faces[iRenderingIndex8][3];
            RoadPoly.vertices[2].y = LightXYZ[iRenderingLoopIndex].screen.y;
            iRenderingCoordIndex = iRenderingIndexTmp;
            iRenderingDataIndex = LightXYZ[iRenderingIndexTmp].screen.x;
            RoadPoly.vertices[3].y = LightXYZ[iRenderingCoordIndex].screen.y;
            RoadPoly.vertices[3].x = iRenderingDataIndex;
            if ((RoadPoly.iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) != 0)
              POLYTEX(cargen_vga, pScrPtr_1, &RoadPoly, 18, gfx_size);
            else
              POLYFLAT(pScrPtr_1, &RoadPoly);
            ++iNextSectionIndex;
          } while (iNextSectionIndex < 6);
          goto LABEL_1271;
        default:
          goto LABEL_1271;                      // Switch on render object type to call appropriate renderer
      }
    }
  }
}

//-------------------------------------------------------------------------------------------------
//00025550
void subdivide(uint8 *pDest, tPolyParams *polyParams,
               float fX0_3D, float fY0_3D, float fZ0_3D,
               float fX1_3D, float fY1_3D, float fZ1_3D,
               float fX2_3D, float fY2_3D, float fZ2_3D,
               float fX3_3D, float fY3_3D, float fZ3_3D,
               int iSubpolyType,
               int bHalfResTex)
{
  int iX0; // ebp
  int iX1; // edi
  int iX3; // esi
  int iTexHgt; // ebx
  int iY3; // [esp+0h] [ebp-28h]
  int iY0; // [esp+4h] [ebp-24h]
  int iY1; // [esp+8h] [ebp-20h]
  int iY2; // [esp+Ch] [ebp-1Ch]
  int iX2; // [esp+10h] [ebp-18h]

  if ((polyParams->iSurfaceType & SURFACE_FLAG_SKIP_RENDER) != 0)// SURFACE_FLAG_SKIP_RENDER
    return;

  // setup globals for dodivide
  subptr = pDest;
  subpoly = polyParams;

  // Extract screen coords from pol verts
  iX0 = polyParams->vertices[0].x;
  iX1 = polyParams->vertices[1].x;
  iY0 = polyParams->vertices[0].y;
  iY3 = polyParams->vertices[3].y;
  iY1 = polyParams->vertices[1].y;
  subpolytype = iSubpolyType;
  iX2 = polyParams->vertices[2].x;
  iY2 = polyParams->vertices[2].y;
  iX3 = polyParams->vertices[3].x;

  // determine tex flipping mode
  fliptype = (polyParams->iSurfaceType & SURFACE_FLAG_FLIP_HORIZ) != 0;// SURFACE_FLAG_FLIP_HORIZ
  if ((polyParams->iSurfaceType & SURFACE_FLAG_FLIP_VERT) != 0)// SURFACE_FLAG_FLIP_VERT
    fliptype += 2;                              // 0=none, 1=horiz, 2=vert, 3=both

  // set flat pol flag if SURFACE_FLAG_APPLY_TEXTURE is not set
  // This disables screen-size based subdivision for untextured pol
  flatpol = ((subpoly->iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) != 0) - 1;// SURFACE_FLAG_APPLY_TEXTURE

  // Determine tex dimensions based on pol type
  if (subpolytype >= 0) {
    // Standard pol type
    iTexHgt = 1024;
    tex_wid = 1024;
    goto LABEL_9;
  }
  if (subpolytype != -1) {
    // wide tex
    iTexHgt = 2048;
    tex_wid = 1024;
  LABEL_9:
    tex_hgt = iTexHgt;
    goto LABEL_10;
  }
  // type -1: tall tex
  tex_wid = 2048;
  tex_hgt = 1024;
LABEL_10:
  // Apply half-res tex mode if requested
  if (bHalfResTex) {
    tex_wid >>= 1;
    tex_hgt >>= 1;
  }
  dodivide(
    fX0_3D,
    fY0_3D,
    fZ0_3D,
    fX1_3D,
    fY1_3D,
    fZ1_3D,
    fX2_3D,
    fY2_3D,
    fZ2_3D,
    fX3_3D,
    fY3_3D,
    fZ3_3D,
    iX0,
    iY0,
    iX1,
    iY1,
    iX2,
    iY2,
    iX3,
    iY3,
    0,
    0,
    tex_wid,
    tex_hgt);

  // Reset tex coords to default values
  // Clean slate for next pol
  set_starts(0);

  // Debug mode: draw pol outline if showsub flag is enabled
  if (showsub) {
    // Edge 0-1 (top)
    if (fZ0_3D >= 1.0 && fZ1_3D >= 1.0)
      compout(subptr, iX0, iY0, iX1, iY1, 0xF3u);// 0xF3 is blue in PALETTE.PAL
    // Edge 1-2 (right)
    if (fZ1_3D >= 1.0 && fZ2_3D >= 1.0)
      compout(subptr, iX1, iY1, iX2, iY2, 0xF3u);
    // Edge 2-3 (bottom)
    if (fZ2_3D >= 1.0 && fZ3_3D >= 1.0)
      compout(subptr, iX2, iY2, iX3, iY3, 0xF3u);
    // Edge 3-0 (left)
    if (fZ3_3D >= 1.0 && fZ0_3D >= 1.0)
      compout(subptr, iX3, iY3, iX0, iY0, 0xF3u);
  }
}

//-------------------------------------------------------------------------------------------------
//000257E0
void dodivide(float fX0_3D, float fY0_3D, float fZ0_3D,
              float fX1_3D, float fY1_3D, float fZ1_3D,
              float fX2_3D, float fY2_3D, float fZ2_3D,
              float fX3_3D, float fY3_3D, float fZ3_3D,
              int iScreenX0, int iScreenY0, int iScreenX1, int iScreenY1, int iScreenX2, int iScreenY2, int iScreenX3, int iScreenY3,
              int iTexU, int iTexV, int iTexWid, int iTexHgt)
{
  int iCullFlag; // edx
  float fMaxZ; // eax
  float fMaxZ_temp; // eax
  float fMaxZ_final; // eax
  float fMinZ; // eax
  float fMinZ_temp; // eax
  float fMinZ_final; // eax
  int iMaxScreenX; // eax
  int iMaxScreenX_temp; // eax
  int iMinScreenX; // eax
  int iMinScreenX_temp; // eax
  int iMaxScreenY; // eax
  int iMaxScreenY_temp; // eax
  int iMinScreenY; // eax
  int iMinScreenY_temp; // eax
  tPolyParams *pPolyParams; // edx
  int iPolyType; // eax
  uint8 *pFrameBuf; // eax
  double dViewDist; // st7
  double dInvZ; // st6
  double dScreenScale; // st5
  double dProjX; // st4
  double dProjY; // st7
  double dViewDist_1; // st7
  double dInvZ_1; // st6
  double dScreenScale_1; // st5
  double dProjX_1; // st4
  double dProjY_1; // st7
  double dViewDist_2; // st7
  double dInvZ_2; // st6
  double dScreenScale_2; // st5
  double dProjX_2; // st4
  double dProjY_2; // st7
  double dViewDist_3; // st7
  double dInvZ_3; // st6
  double dScreenScale_3; // st5
  double dProjX_3; // st4
  double dProjY_3; // st7
  double dViewDist_4; // st7
  double dInvZ_4; // st6
  double dScreenScale_4; // st5
  double dProjX_4; // st4
  double dProjY_4; // st7
  double dViewDist_5; // st7
  double dInvZ_5; // st6
  double dScreenScale_5; // st5
  double dProjX_5; // st4
  double dProjY_5; // st7
  double dViewDist_6; // st7
  double dInvZ_6; // st6
  double dScreenScale_6; // st5
  double dProjX_6; // st4
  double dProjY_6; // st7
  double dViewDist_7; // st7
  double dInvZ_7; // st6
  double dScreenScale_7; // st5
  double dProjX_7; // st4
  double dProjY_7; // st7
  double dViewDist_8; // st7
  double dInvZ_8; // st6
  double dScreenScale_8; // st5
  double dProjX_8; // st4
  double dProjY_8; // st7
  int iTexU_right; // edx
  int iScreenCenterY; // [esp-28h] [ebp-180h]
  tPolyParams *pPolyParamsLocal; // [esp+Ch] [ebp-14Ch]
  float fMidZ_Edge01; // [esp+10h] [ebp-148h]
  float fMidZ_Edge23; // [esp+14h] [ebp-144h]
  float fMidZ_Edge03; // [esp+18h] [ebp-140h]
  int iRenderFlag; // [esp+1Ch] [ebp-13Ch]
  int iMaxScreenCoord; // [esp+28h] [ebp-130h]
  int iMinScreenCoord; // [esp+30h] [ebp-128h]
  float fMinZ_edge; // [esp+3Ch] [ebp-11Ch]
  float fMaxZ_quad; // [esp+54h] [ebp-104h]
  int iMaxScreenX_quad; // [esp+5Ch] [ebp-FCh]
  int iMinScreenY_quad; // [esp+64h] [ebp-F4h]
  int iScreenCenterX_calc; // [esp+78h] [ebp-E0h]
  float fMidZ_quad; // [esp+7Ch] [ebp-DCh]
  int iScreenY_mid30; // [esp+8Ch] [ebp-CCh]
  int iScreenX_mid30; // [esp+90h] [ebp-C8h]
  int iScreenY_mid12; // [esp+94h] [ebp-C4h]
  int iScreenX_mid23; // [esp+98h] [ebp-C0h]
  float fX_mid01; // [esp+9Ch] [ebp-BCh]
  float fY_mid01; // [esp+A0h] [ebp-B8h]
  int iScreenX_mid12; // [esp+A4h] [ebp-B4h]
  float fZ_mid30; // [esp+A8h] [ebp-B0h]
  float fY_mid30; // [esp+ACh] [ebp-ACh]
  float fX_mid30; // [esp+B0h] [ebp-A8h]
  float fZ_mid12; // [esp+B4h] [ebp-A4h]
  float fY_mid12; // [esp+B8h] [ebp-A0h]
  float fX_mid12; // [esp+BCh] [ebp-9Ch]
  float fZ_mid12_1; // [esp+C0h] [ebp-98h]
  float fY_mid12_1; // [esp+C4h] [ebp-94h]
  float fX_mid12_1; // [esp+C8h] [ebp-90h]
  int iScreenY_mid12_1; // [esp+CCh] [ebp-8Ch]
  int iScreenX_mid12_1; // [esp+D0h] [ebp-88h]
  int iScreenX_subdiv1; // [esp+D4h] [ebp-84h]
  int iTexV_bottom; // [esp+D8h] [ebp-80h]
  int iScreenY_mid01; // [esp+E4h] [ebp-74h]
  int iScreenY_mid23; // [esp+E8h] [ebp-70h]
  int iScreenX_mid01; // [esp+ECh] [ebp-6Ch]
  int iScreenY_mid30_1; // [esp+F0h] [ebp-68h]
  int iScreenX_mid23_1; // [esp+F4h] [ebp-64h]
  int iScreenY_mid30_2; // [esp+FCh] [ebp-5Ch]
  int iScreenX_mid12_2; // [esp+100h] [ebp-58h]
  int iScreenX_mid30_1; // [esp+104h] [ebp-54h]
  float fY_mid12_2; // [esp+108h] [ebp-50h]
  float fZ_mid12_2; // [esp+10Ch] [ebp-4Ch]
  float fZ_mid30_1; // [esp+110h] [ebp-48h]
  float fX_mid30_1; // [esp+114h] [ebp-44h]
  float fX_mid12_2; // [esp+118h] [ebp-40h]
  float fY_mid30_1; // [esp+11Ch] [ebp-3Ch]
  float fY_mid23; // [esp+120h] [ebp-38h]
  float fZ_mid23; // [esp+124h] [ebp-34h]
  float fX_mid23; // [esp+128h] [ebp-30h]
  int iScreenY_center; // [esp+12Ch] [ebp-2Ch]
  float fX_center; // [esp+130h] [ebp-28h]
  float fY_center; // [esp+134h] [ebp-24h]
  float fZ_center; // [esp+138h] [ebp-20h]
  int iScreenX_center; // [esp+13Ch] [ebp-1Ch]

  while (2) {
    iCullFlag = 0;
    divtype = 0;                                // Reset subdivision type

    // Find maximum Z value among all 4 verts
    if (fZ0_3D <= (double)fZ1_3D)
      fMaxZ = fZ1_3D;
    else
      fMaxZ = fZ0_3D;
    fMaxZ_quad = fMaxZ;

    if (fZ2_3D <= (double)fZ3_3D)
      fMaxZ_temp = fZ3_3D;
    else
      fMaxZ_temp = fZ2_3D;
    if (fMaxZ_quad <= (double)fMaxZ_temp) {
      if (fZ2_3D <= (double)fZ3_3D)
        fMaxZ_final = fZ3_3D;
      else
        fMaxZ_final = fZ2_3D;
    } else if (fZ0_3D <= (double)fZ1_3D) {
      fMaxZ_final = fZ1_3D;
    } else {
      fMaxZ_final = fZ0_3D;
    }

    // Cull if polygon is too close to camera (Z < 80)
    if (fMaxZ_final < 80.0)
      iCullFlag = -1;
    if (fZ0_3D >= (double)fZ1_3D)
      fMinZ = fZ1_3D;
    else
      fMinZ = fZ0_3D;
    fMinZ_edge = fMinZ;

    if (fZ2_3D >= (double)fZ3_3D)
      fMinZ_temp = fZ3_3D;
    else
      fMinZ_temp = fZ2_3D;

    if (fMinZ_edge >= (double)fMinZ_temp) {
      if (fZ2_3D >= (double)fZ3_3D)
        fMinZ_final = fZ3_3D;
      else
        fMinZ_final = fZ2_3D;
    } else if (fZ0_3D >= (double)fZ1_3D) {
      fMinZ_final = fZ1_3D;
    } else {
      fMinZ_final = fZ0_3D;
    }

    // Check if polygon is far enough away from camera
    if (fMinZ_final >= 1.0) {
      // Find bounding box of screen coords
      // Max X coord
      if (iScreenX1 >= iScreenX0)
        iMaxScreenCoord = iScreenX1;
      else
        iMaxScreenCoord = iScreenX0;
      iMaxScreenX = iScreenX2;
      if (iScreenX2 <= iScreenX3)
        iMaxScreenX = iScreenX3;
      if (iMaxScreenCoord <= iMaxScreenX) {
        iMaxScreenX_temp = iScreenX2;
        if (iScreenX2 <= iScreenX3)
          iMaxScreenX_temp = iScreenX3;
      } else {
        iMaxScreenX_temp = iScreenX0;
        if (iScreenX1 >= iScreenX0)
          iMaxScreenX_temp = iScreenX1;
      }

      // cull if bounding box is completely off-screen (left)
      if (iMaxScreenX_temp < 0)
        iCullFlag = -1;

      // Minimum X coord
      if (iScreenX1 <= iScreenX0)
        iMaxScreenX_quad = iScreenX1;
      else
        iMaxScreenX_quad = iScreenX0;
      iMinScreenX = iScreenX2;
      if (iScreenX2 >= iScreenX3)
        iMinScreenX = iScreenX3;
      if (iMaxScreenX_quad >= iMinScreenX) {
        iMinScreenX_temp = iScreenX2;
        if (iScreenX2 >= iScreenX3)
          iMinScreenX_temp = iScreenX3;
      } else {
        iMinScreenX_temp = iScreenX0;
        if (iScreenX1 <= iScreenX0)
          iMinScreenX_temp = iScreenX1;
      }

      // Cull if bounding box is completely off-screen (right)
      if (iMinScreenX_temp >= winw)
        iCullFlag = -1;

      // Maximum Y coord
      if (iScreenY0 <= iScreenY1)
        iMinScreenY_quad = iScreenY1;
      else
        iMinScreenY_quad = iScreenY0;
      iMaxScreenY = iScreenY2;
      if (iScreenY2 <= iScreenY3)
        iMaxScreenY = iScreenY3;
      if (iMinScreenY_quad <= iMaxScreenY) {
        iMaxScreenY_temp = iScreenY2;
        if (iScreenY2 <= iScreenY3)
          iMaxScreenY_temp = iScreenY3;
      } else if (iScreenY0 <= iScreenY1) {
        iMaxScreenY_temp = iScreenY1;
      } else {
        iMaxScreenY_temp = iScreenY0;
      }

      // Cull if bounding box is completely off-screen (top)
      if (iMaxScreenY_temp < 0)
        iCullFlag = -1;

      // Minimum Y coord
      if (iScreenY0 >= iScreenY1)
        iMinScreenCoord = iScreenY1;
      else
        iMinScreenCoord = iScreenY0;
      iMinScreenY = iScreenY2;
      if (iScreenY2 >= iScreenY3)
        iMinScreenY = iScreenY3;
      if (iMinScreenCoord >= iMinScreenY) {
        iMinScreenY_temp = iScreenY2;
        if (iScreenY2 >= iScreenY3)
          iMinScreenY_temp = iScreenY3;
      } else if (iScreenY0 >= iScreenY1) {
        iMinScreenY_temp = iScreenY1;
      } else {
        iMinScreenY_temp = iScreenY0;
      }

      // Cull if bounding box is completely off-screen (bottom)
      if (iMinScreenY_temp >= winh)
        iCullFlag = -1;

      // Render polygon
      iRenderFlag = -1;
    } else {
      // Polygon is too close, check if we should subdivide based on tex size
      if (iTexWid > min_sub_size)
        ++divtype;                              // subdivide horizontally
      if (iTexHgt > min_sub_size)
        divtype += 2;                           // subdivide vertically
      iRenderFlag = 0;
    }

    // If polygon is not culled
    if (!iCullFlag) {
      // Additional subdivision checks based on polygon screen size
      if (!divtype && !flatpol) {
        // Calculate perimeter in screen space to determine if polygon is large enough to subdivide
        polyxsize = abs(iScreenY3 - iScreenY2) + abs(iScreenY0 - iScreenY1) + abs(iScreenX3 - iScreenX2) + abs(iScreenX0 - iScreenX1);
        polyysize = abs(iScreenY1 - iScreenY2) + abs(iScreenY0 - iScreenY3) + abs(iScreenX1 - iScreenX2) + abs(iScreenX0 - iScreenX3);
        if (polyxsize > small_poly && iTexWid > min_sub_size)
          ++divtype;                            // Subdivide horizontally if pol is large enough
        if (polyysize > small_poly && iTexHgt > min_sub_size)
          divtype += 2;                         // Subdivide vertically if pol is large enough
      }

      // Calculate midpoints for potential subdivision
      fMidZ_Edge23 = fZ0_3D + fZ1_3D;
      fMidZ_Edge01 = fY0_3D + fY1_3D;
      fMidZ_Edge03 = fX0_3D + fX1_3D;
      fY_mid01 = fMidZ_Edge23 * 0.5f;            // Midpoint between verts 0 and 1
      fMidZ_quad = fMidZ_Edge01 * 0.5f;
      iScreenCenterX_calc = winw / 2;           // screen center x
      fX_mid01 = fMidZ_Edge03 * 0.5f;

      switch (divtype) {
        case 0:                                 // No subdivision - render the polygon
          if (!iRenderFlag)
            return;                             // don't render if flag is not set

          // Set up pol params for rendering
          pPolyParamsLocal = subpoly;
          pPolyParams = subpoly;
          subpoly->vertices[0].x = iScreenX0;
          pPolyParams->vertices[2].x = iScreenX2;
          pPolyParams->vertices[2].y = iScreenY2;
          pPolyParams->vertices[3].x = iScreenX3;
          pPolyParams->vertices[3].y = iScreenY3;

          // Set up texture coords in 16.16 fixed point
          startsx[0] = (iTexWid + iTexU - 1) << 12;
          startsx[1] = iTexU << 12;
          startsx[2] = iTexU << 12;
          startsx[3] = (iTexWid + iTexU - 1) << 12;
          startsy[0] = iTexV << 12;
          startsy[1] = iTexV << 12;
          pPolyParams->vertices[0].y = iScreenY0;
          pPolyParams->vertices[1].x = iScreenX1;
          startsy[2] = (iTexHgt + iTexV - 1) << 12;
          startsy[3] = (iTexHgt + iTexV - 1) << 12;

          iPolyType = subpolytype;
          pPolyParams->vertices[1].y = iScreenY1;

          // Render based on pol type
          if (iPolyType < 1) {
            if (iPolyType < -2) {
              // Check if tex should be applied
            LABEL_111:
              if ((subpoly->iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) != 0)// SURFACE_FLAG_APPLY_TEXTURE
              {
                // Render textured pol with car texture
                //TODO is this correct?
                POLYTEX(
                  cartex_vga[car_texmap[subpolytype - 3] - 1],
                  subptr,
                  subpoly,
                  car_texmap[subpolytype - 3],
                  gfx_size);
                //POLYTEX(
                //  (&horizon_vga)[*(&car_draw_order[15].iChunkIdx + subpolytype)],// offset into car_texmap
                //  subptr,
                //  subpoly,
                //  *(&car_draw_order[15].iChunkIdx + subpolytype),// offset into car_texmap
                //  gfx_size);
                goto LABEL_115;
              }
              pFrameBuf = subptr;
              pPolyParams = subpoly;
            LABEL_114:
              POLYFLAT(pFrameBuf, pPolyParams); // render flat pol
            LABEL_115:
                          // Debug: draw pol outline if showsub is enabled
              if (showsub) {
                compout(subptr, subpoly->vertices[0].x, subpoly->vertices[0].y, subpoly->vertices[1].x, subpoly->vertices[1].y, 0x9Fu);// 0x9F is light blue in PALETTE.PAL
                compout(subptr, subpoly->vertices[1].x, subpoly->vertices[1].y, subpoly->vertices[2].x, subpoly->vertices[2].y, 0x9Fu);
                compout(subptr, subpoly->vertices[2].x, subpoly->vertices[2].y, subpoly->vertices[3].x, subpoly->vertices[3].y, 0x9Fu);
                compout(subptr, subpoly->vertices[0].x, subpoly->vertices[0].y, subpoly->vertices[1].x, subpoly->vertices[1].y, 0x9Fu);
              }
              return;
            }
            if ((pPolyParams->iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) == 0)// SURFACE_FLAG_APPLY_TEXTURE
              goto LABEL_106;
          } else if (iPolyType > 1) {
            if (iPolyType <= 2) {
            LABEL_107:
              pFrameBuf = subptr;
              goto LABEL_114;
            }
            if (iPolyType != 666)
              goto LABEL_111;
            if ((pPolyParams->iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) != 0)// SURFACE_FLAG_APPLY_TEXTURE
            {
              POLYTEX(building_vga, subptr, pPolyParamsLocal, 17, gfx_size);
              goto LABEL_115;
            }
          LABEL_106:
            pPolyParams = pPolyParamsLocal;
            goto LABEL_107;
          }
          // Default texture rendering
          POLYTEX(texture_vga, subptr, pPolyParamsLocal, 0, gfx_size);
          goto LABEL_115;
        case 1:                                 // Horiz subdivision only
          // Calculate midpoint between verts 0 and 1, and 2 and 3
          if (fY_mid01 < 1.0 || isnan(fY_mid01)) {
            if ((double)iScreenCenterX_calc <= fX_mid01)
              iScreenX_mid12 = 20000;
            else
              iScreenX_mid12 = -20000;
            if ((double)(winh / 2) <= fMidZ_quad)
              iScreenX_subdiv1 = 20000;
            else
              iScreenX_subdiv1 = -20000;
          } else {
            // Project 3D coords into screen space
            dViewDist = (double)VIEWDIST;
            dInvZ = 1.0 / fY_mid01;
            dScreenScale = (double)scr_size;
            dProjX = (dViewDist * fX_mid01 * dInvZ + (double)xbase) * dScreenScale * 0.015625;
            dProjX = round(dProjX);//_CHP();
            iScreenX_mid12 = (int)dProjX;
            dProjY = dScreenScale * (199.0 - dInvZ * (dViewDist * fMidZ_quad) - (double)ybase) * 0.015625;
            dProjY = round(dProjY);//_CHP();
            iScreenX_subdiv1 = (int)dProjY;
          }

          // Calculate midpoint between verts 1 and 2
          fX_mid12 = (fX2_3D + fX3_3D) * 0.5f;
          fY_mid12 = (fY2_3D + fY3_3D) * 0.5f;
          fZ_mid12 = (fZ2_3D + fZ3_3D) * 0.5f;

          if (fZ_mid12 < 1.0 || isnan(fZ_mid12)) {
            if ((double)(winw / 2) <= fX_mid12)
              iScreenX_mid23 = 20000;
            else
              iScreenX_mid23 = -20000;
            if ((double)(winh / 2) <= fY_mid12)
              iScreenY_mid12 = 20000;
            else
              iScreenY_mid12 = -20000;
          } else {
            // Project to screen space
            dViewDist_1 = (double)VIEWDIST;
            dInvZ_1 = 1.0 / fZ_mid12;
            dScreenScale_1 = (double)scr_size;
            dProjX_1 = (dViewDist_1 * fX_mid12 * dInvZ_1 + (double)xbase) * dScreenScale_1 * 0.015625;
            dProjX_1 = round(dProjX_1);//_CHP();
            iScreenX_mid23 = (int)dProjX_1;
            dProjY_1 = dScreenScale_1 * (199.0 - dInvZ_1 * (dViewDist_1 * fY_mid12) - (double)ybase) * 0.015625;
            dProjY_1 = round(dProjY_1);//_CHP();
            iScreenY_mid12 = (int)dProjY_1;
          }

          // Half tex width for subdivision
          iTexWid >>= 1;

          // Check flip type for tex coord handling
          if ((fliptype & 1) != 0) {
            // Flip horiz - swap coords
            iScreenCenterY = iScreenX1;
            iScreenX1 = iScreenX_mid12;

            // Recursively subdivide left half
            dodivide(
              fX_mid01,
              fMidZ_quad,
              fY_mid01,
              fX1_3D,
              fY1_3D,
              fZ1_3D,
              fX2_3D,
              fY2_3D,
              fZ2_3D,
              fX_mid12,
              fY_mid12,
              fZ_mid12,
              iScreenX_mid12,
              iScreenX_subdiv1,
              iScreenCenterY,
              iScreenY1,
              iScreenX2,
              iScreenY2,
              iScreenX_mid23,
              iScreenY_mid12,
              iTexWid + iTexU,
              iTexV,
              iTexWid,
              iTexHgt);

            // Update coords for right half
            fX1_3D = fMidZ_Edge03 * 0.5f;
            fY1_3D = fMidZ_Edge01 * 0.5f;
            fZ1_3D = fMidZ_Edge23 * 0.5f;
            fX2_3D = (fX2_3D + fX3_3D) * 0.5f;
            fY2_3D = (fY2_3D + fY3_3D) * 0.5f;
            fZ2_3D = (fZ2_3D + fZ3_3D) * 0.5f;
          } else {
            // No flip - normal subdivision
            dodivide(
              fX_mid01,
              fMidZ_quad,
              fY_mid01,
              fX1_3D,
              fY1_3D,
              fZ1_3D,
              fX2_3D,
              fY2_3D,
              fZ2_3D,
              fX_mid12,
              fY_mid12,
              fZ_mid12,
              iScreenX_mid12,
              iScreenX_subdiv1,
              iScreenX1,
              iScreenY1,
              iScreenX2,
              iScreenY2,
              iScreenX_mid23,
              iScreenY_mid12,
              iTexU,
              iTexV,
              iTexWid,
              iTexHgt);

            // Update coords for right half
            fX1_3D = fMidZ_Edge03 * 0.5f;
            fY1_3D = fMidZ_Edge01 * 0.5f;
            fZ1_3D = fMidZ_Edge23 * 0.5f;
            fX2_3D = (fX2_3D + fX3_3D) * 0.5f;
            fY2_3D = (fY2_3D + fY3_3D) * 0.5f;
            iTexU += iTexWid;                   // Advance texture U coord
            fZ2_3D = (fZ2_3D + fZ3_3D) * 0.5f;
            iScreenX1 = iScreenX_mid12;
          }
          iScreenX2 = iScreenX_mid23;
          iScreenY1 = iScreenX_subdiv1;
          iScreenY2 = iScreenY_mid12;
          continue;
        case 2:                                 // Vertical flip
          // Calculate midpoint between verts 1 and 2
          fX_mid12_1 = (fX1_3D + fX2_3D) * 0.5f;
          fY_mid12_1 = (fY1_3D + fY2_3D) * 0.5f;
          fZ_mid12_1 = (fZ1_3D + fZ2_3D) * 0.5f;

          // Too close to camera
          if (fZ_mid12_1 < 1.0 || isnan(fZ_mid12_1)) {
            if ((double)iScreenCenterX_calc <= fX_mid12_1)
              iScreenX_mid12_1 = 20000;
            else
              iScreenX_mid12_1 = -20000;
            if ((double)(winh / 2) <= fY_mid12_1)
              iScreenY_mid12_1 = 20000;
            else
              iScreenY_mid12_1 = -20000;
          } else {
            dViewDist_2 = (double)VIEWDIST;
            dInvZ_2 = 1.0 / fZ_mid12_1;
            dScreenScale_2 = (double)scr_size;
            dProjX_2 = (dViewDist_2 * fX_mid12_1 * dInvZ_2 + (double)xbase) * dScreenScale_2 * 0.015625;
            dProjX_2 = round(dProjX_2);//_CHP();
            iScreenX_mid12_1 = (int)dProjX_2;
            dProjY_2 = dScreenScale_2 * (199.0 - dInvZ_2 * (dViewDist_2 * fY_mid12_1) - (double)ybase) * 0.015625;
            dProjY_2 = round(dProjY_2);//_CHP();
            iScreenY_mid12_1 = (int)dProjY_2;
          }

          // Update for bottom-right quad
          fX_mid30 = (fX3_3D + fX0_3D) * 0.5f;
          fY_mid30 = (fY3_3D + fY0_3D) * 0.5f;
          fZ_mid30 = (fZ3_3D + fZ0_3D) * 0.5f;
          if (fZ_mid30 < 1.0 || isnan(fZ_mid30)) {
            if ((double)(winw / 2) <= fX_mid30)
              iScreenX_mid30 = 20000;
            else
              iScreenX_mid30 = -20000;
            if ((double)(winh / 2) <= fY_mid30)
              iScreenY_mid30 = 20000;
            else
              iScreenY_mid30 = -20000;
          } else {
            dViewDist_3 = (double)VIEWDIST;
            dInvZ_3 = 1.0 / fZ_mid30;
            dScreenScale_3 = (double)scr_size;
            dProjX_3 = (dViewDist_3 * fX_mid30 * dInvZ_3 + (double)xbase) * dScreenScale_3 * 0.015625;
            dProjX_3 = round(dProjX_3);//_CHP();
            iScreenX_mid30 = (int)dProjX_3;
            dProjY_3 = dScreenScale_3 * (199.0 - dInvZ_3 * (dViewDist_3 * fY_mid30) - (double)ybase) * 0.015625;
            dProjY_3 = round(dProjY_3);//_CHP();
            iScreenY_mid30 = (int)dProjY_3;
          }

          // Half texture height for subdivision
          iTexHgt >>= 1;

          // Vertical flip
          if ((fliptype & 2) != 0) {
            // Recursively subdivide top half
            dodivide(
              fX0_3D,
              fY0_3D,
              fZ0_3D,
              fX1_3D,
              fY1_3D,
              fZ1_3D,
              fX_mid12_1,
              fY_mid12_1,
              fZ_mid12_1,
              fX_mid30,
              fY_mid30,
              fZ_mid30,
              iScreenX0,
              iScreenY0,
              iScreenX1,
              iScreenY1,
              iScreenX_mid12_1,
              iScreenY_mid12_1,
              iScreenX_mid30,
              iScreenY_mid30,
              iTexU,
              iTexHgt + iTexV,                  // bottom half of texture
              iTexWid,
              iTexHgt);

            // Update coords for bottom half
            fX0_3D = (fX3_3D + fX0_3D) * 0.5f;
            fY0_3D = (fY3_3D + fY0_3D) * 0.5f;
            fZ0_3D = (fZ3_3D + fZ0_3D) * 0.5f;
            fX1_3D = (fX1_3D + fX2_3D) * 0.5f;
            iScreenY1 = iScreenY_mid12_1;
            fY1_3D = (fY1_3D + fY2_3D) * 0.5f;
            iScreenX1 = iScreenX_mid12_1;
            fZ1_3D = (fZ1_3D + fZ2_3D) * 0.5f;
            iScreenY0 = iScreenY_mid30;
          } else {
            // No flip - normal subdivision
            // Recursively subdivide top half
            dodivide(
              fX0_3D,
              fY0_3D,
              fZ0_3D,
              fX1_3D,
              fY1_3D,
              fZ1_3D,
              fX_mid12_1,
              fY_mid12_1,
              fZ_mid12_1,
              fX_mid30,
              fY_mid30,
              fZ_mid30,
              iScreenX0,
              iScreenY0,
              iScreenX1,
              iScreenY1,
              iScreenX_mid12_1,
              iScreenY_mid12_1,
              iScreenX_mid30,
              iScreenY_mid30,
              iTexU,
              iTexV,                            // top half of tex
              iTexWid,
              iTexHgt);

            // Update coords for bottom half
            fX0_3D = (fX3_3D + fX0_3D) * 0.5f;
            fY0_3D = (fY3_3D + fY0_3D) * 0.5f;
            fZ0_3D = (fZ3_3D + fZ0_3D) * 0.5f;
            iTexV += iTexHgt;                   // Advance texture V coord to bottom half
            fX1_3D = (fX1_3D + fX2_3D) * 0.5f;
            iScreenY1 = iScreenY_mid12_1;
            fY1_3D = (fY1_3D + fY2_3D) * 0.5f;
            iScreenY0 = iScreenY_mid30;
            fZ1_3D = (fZ1_3D + fZ2_3D) * 0.5f;
            iScreenX1 = iScreenX_mid12_1;
          }
          iScreenX0 = iScreenX_mid30;
          continue;                             // Process bottom half in next iteration
        case 3:                                 // Both horiz and vert subdivision
          // Calculate all edge midpoints

          // midpoint of edge 0-1 (top edge)
          if (fY_mid01 < 1.0 || isnan(fY_mid01)) {
            if ((double)iScreenCenterX_calc <= fX_mid01)
              iScreenX_mid01 = 20000;
            else
              iScreenX_mid01 = -20000;
            if ((double)(winh / 2) <= fMidZ_quad)
              iScreenY_mid01 = 20000;
            else
              iScreenY_mid01 = -20000;
          } else {
            dViewDist_4 = (double)VIEWDIST;
            dInvZ_4 = 1.0 / fY_mid01;
            dScreenScale_4 = (double)scr_size;
            dProjX_4 = (dViewDist_4 * fX_mid01 * dInvZ_4 + (double)xbase) * dScreenScale_4 * 0.015625;
            dProjX_4 = round(dProjX_4);//_CHP();
            iScreenX_mid01 = (int)dProjX_4;
            dProjY_4 = dScreenScale_4 * (199.0 - dInvZ_4 * (dViewDist_4 * fMidZ_quad) - (double)ybase) * 0.015625;
            dProjY_4 = round(dProjY_4);//_CHP();
            iScreenY_mid01 = (int)dProjY_4;
          }

          // Midpoint of edge 1-2 (right edge)
          fX_mid12_2 = (fX1_3D + fX2_3D) * 0.5f;
          fY_mid12_2 = (fY1_3D + fY2_3D) * 0.5f;
          fZ_mid12_2 = (fZ1_3D + fZ2_3D) * 0.5f;
          if (fZ_mid12_2 < 1.0 || isnan(fZ_mid12_2)) {
            if ((double)(winw / 2) <= fX_mid12_2)
              iScreenX_mid12_2 = 20000;
            else
              iScreenX_mid12_2 = -20000;
            if ((double)(winh / 2) <= fY_mid12_2)
              iScreenY_mid23 = 20000;
            else
              iScreenY_mid23 = -20000;
          } else {
            dViewDist_5 = (double)VIEWDIST;
            dInvZ_5 = 1.0 / fZ_mid12_2;
            dScreenScale_5 = (double)scr_size;
            dProjX_5 = (dViewDist_5 * fX_mid12_2 * dInvZ_5 + (double)xbase) * dScreenScale_5 * 0.015625;
            dProjX_5 = round(dProjX_5);//_CHP();
            iScreenX_mid12_2 = (int)dProjX_5;
            dProjY_5 = dScreenScale_5 * (199.0 - dInvZ_5 * (dViewDist_5 * fY_mid12_2) - (double)ybase) * 0.015625;
            dProjY_5 = round(dProjY_5);//_CHP();
            iScreenY_mid23 = (int)dProjY_5;
          }

          // Midpoint fo edge 2-3 (bottom edge)
          fX_mid23 = (fX2_3D + fX3_3D) * 0.5f;
          fY_mid23 = (fY2_3D + fY3_3D) * 0.5f;
          fZ_mid23 = (fZ2_3D + fZ3_3D) * 0.5f;
          if (fZ_mid23 < 1.0 || isnan(fZ_mid23)) {
            if ((double)(winw / 2) <= fX_mid23)
              iScreenX_mid23_1 = 20000;
            else
              iScreenX_mid23_1 = -20000;
            if ((double)(winh / 2) <= fY_mid23)
              iScreenY_mid30_1 = 20000;
            else
              iScreenY_mid30_1 = -20000;
          } else {
            dViewDist_6 = (double)VIEWDIST;
            dInvZ_6 = 1.0 / fZ_mid23;
            dScreenScale_6 = (double)scr_size;
            dProjX_6 = (dViewDist_6 * fX_mid23 * dInvZ_6 + (double)xbase) * dScreenScale_6 * 0.015625;
            dProjX_6 = round(dProjX_6);//_CHP();
            iScreenX_mid23_1 = (int)dProjX_6;
            dProjY_6 = dScreenScale_6 * (199.0 - dInvZ_6 * (dViewDist_6 * fY_mid23) - (double)ybase) * 0.015625;
            dProjY_6 = round(dProjY_6);//_CHP();
            iScreenY_mid30_1 = (int)dProjY_6;
          }

          // Midpoint of edge 3-0 (left edge)
          fX_mid30_1 = (fX3_3D + fX0_3D) * 0.5f;
          fY_mid30_1 = (fY3_3D + fY0_3D) * 0.5f;
          fZ_mid30_1 = (fZ3_3D + fZ0_3D) * 0.5f;
          if (fZ_mid30_1 < 1.0 || isnan(fZ_mid30_1)) {
            if ((double)(winw / 2) <= fX_mid30_1)
              iScreenX_mid30_1 = 20000;
            else
              iScreenX_mid30_1 = -20000;
            if ((double)(winh / 2) <= fY_mid30_1)
              iScreenY_mid30_2 = 20000;
            else
              iScreenY_mid30_2 = -20000;
          } else {
            dViewDist_7 = (double)VIEWDIST;
            dInvZ_7 = 1.0 / fZ_mid30_1;
            dScreenScale_7 = (double)scr_size;
            dProjX_7 = (dViewDist_7 * fX_mid30_1 * dInvZ_7 + (double)xbase) * dScreenScale_7 * 0.015625;
            dProjX_7= round(dProjX_7);//_CHP();
            iScreenX_mid30_1 = (int)dProjX_7;
            dProjY_7 = dScreenScale_7 * (199.0 - dInvZ_7 * (dViewDist_7 * fY_mid30_1) - (double)ybase) * 0.015625;
            dProjY_7 = round(dProjY_7);//_CHP();
            iScreenY_mid30_2 = (int)dProjY_7;
          }

          // Calculate center point of quad
          fX_center = (fX_mid01 + fX_mid23) * 0.5f;
          fY_center = (fMidZ_quad + fY_mid23) * 0.5f;
          fZ_center = (fY_mid01 + fZ_mid23) * 0.5f;
          if (fZ_center < 1.0 || isnan(fZ_center)) {
            if ((double)(winw / 2) <= fX_center)
              iScreenX_center = 20000;
            else
              iScreenX_center = -20000;
            if ((double)(winh / 2) <= fY_center)
              iScreenY_center = 20000;
            else
              iScreenY_center = -20000;
          } else {
            dViewDist_8 = (double)VIEWDIST;
            dInvZ_8 = 1.0 / fZ_center;
            dScreenScale_8 = (double)scr_size;
            dProjX_8 = (dViewDist_8 * fX_center * dInvZ_8 + (double)xbase) * dScreenScale_8 * 0.015625;
            dProjX_8 = round(dProjX_8);//_CHP();
            iScreenX_center = (int)dProjX_8;
            dProjY_8 = dScreenScale_8 * (199.0 - dInvZ_8 * (dViewDist_8 * fY_center) - (double)ybase) * 0.015625;
            dProjY_8 = round(dProjY_8);//_CHP();
            iScreenY_center = (int)dProjY_8;
          }

          // Quarter tex dimensions
          iTexWid >>= 1;
          iTexHgt >>= 1;
          iTexV_bottom = iTexHgt + iTexV;
          iTexU_right = iTexWid + iTexU;

          // Handle all 4 flip combinations
          switch (fliptype) {
            case 0:                             // no flip
              // Top-left quadrant
              dodivide(
                fX_mid01,
                fMidZ_quad,
                fY_mid01,
                fX1_3D,
                fY1_3D,
                fZ1_3D,
                fX_mid12_2,
                fY_mid12_2,
                fZ_mid12_2,
                fX_center,
                fY_center,
                fZ_center,
                iScreenX_mid01,
                iScreenY_mid01,
                iScreenX1,
                iScreenY1,
                iScreenX_mid12_2,
                iScreenY_mid23,
                iScreenX_center,
                iScreenY_center,
                iTexU,
                iTexV,
                iTexWid,
                iTexHgt);

              // Top-right quadrant
              dodivide(
                fX0_3D,
                fY0_3D,
                fZ0_3D,
                fX_mid01,
                fMidZ_quad,
                fY_mid01,
                fX_center,
                fY_center,
                fZ_center,
                fX_mid30_1,
                fY_mid30_1,
                fZ_mid30_1,
                iScreenX0,
                iScreenY0,
                iScreenX_mid01,
                iScreenY_mid01,
                iScreenX_center,
                iScreenY_center,
                iScreenX_mid30_1,
                iScreenY_mid30_2,
                iTexU_right,
                iTexV,
                iTexWid,
                iTexHgt);

              iScreenY1 = iScreenY_center;

              // bottom-right quadrant
              dodivide(
                fX_center,
                fY_center,
                fZ_center,
                fX_mid12_2,
                fY_mid12_2,
                fZ_mid12_2,
                fX2_3D,
                fY2_3D,
                fZ2_3D,
                fX_mid23,
                fY_mid23,
                fZ_mid23,
                iScreenX_center,
                iScreenY_center,
                iScreenX_mid12_2,
                iScreenY_mid23,
                iScreenX2,
                iScreenY2,
                iScreenX_mid23_1,
                iScreenY_mid30_1,
                iTexU,
                iTexV_bottom,
                iTexWid,
                iTexHgt);

              // Update for bottom-left quadrant (processed in next iteration)
              fX0_3D = (fX3_3D + fX0_3D) * 0.5f;
              fY0_3D = (fY3_3D + fY0_3D) * 0.5f;
              fZ0_3D = (fZ3_3D + fZ0_3D) * 0.5f;
              fX1_3D = (fX_mid01 + fX_mid23) * 0.5f;
              fY1_3D = (fMidZ_quad + fY_mid23) * 0.5f;
              fZ1_3D = (fY_mid01 + fZ_mid23) * 0.5f;
              fX2_3D = (fX2_3D + fX3_3D) * 0.5f;
              fY2_3D = (fY2_3D + fY3_3D) * 0.5f;
              fZ2_3D = (fZ2_3D + fZ3_3D) * 0.5f;
              iScreenX0 = iScreenX_mid30_1;
              iTexU += iTexWid;
              iScreenX2 = iScreenX_mid23_1;
              iScreenX1 = iScreenX_center;
              iScreenY2 = iScreenY_mid30_1;
              iScreenY0 = iScreenY_mid30_2;
              iTexV += iTexHgt;
              continue;
            case 1:                             // horiz flip
              // top-right quadrant (flipped to top-left)
              dodivide(
                fX_mid01,
                fMidZ_quad,
                fY_mid01,
                fX1_3D,
                fY1_3D,
                fZ1_3D,
                fX_mid12_2,
                fY_mid12_2,
                fZ_mid12_2,
                fX_center,
                fY_center,
                fZ_center,
                iScreenX_mid01,
                iScreenY_mid01,
                iScreenX1,
                iScreenY1,
                iScreenX_mid12_2,
                iScreenY_mid23,
                iScreenX_center,
                iScreenY_center,
                iTexWid + iTexU,                // right side tex
                iTexV,
                iTexWid,
                iTexHgt);

              // Top-left quadrant (flipped to top-right)
              dodivide(
                fX0_3D,
                fY0_3D,
                fZ0_3D,
                fX_mid01,
                fMidZ_quad,
                fY_mid01,
                fX_center,
                fY_center,
                fZ_center,
                fX_mid30_1,
                fY_mid30_1,
                fZ_mid30_1,
                iScreenX0,
                iScreenY0,
                iScreenX_mid01,
                iScreenY_mid01,
                iScreenX_center,
                iScreenY_center,
                iScreenX_mid30_1,
                iScreenY_mid30_2,
                iTexU,                          // left side tex
                iTexV,
                iTexWid,
                iTexHgt);
              iScreenX1 = iScreenX_center;

              // bottom-right quadrant (flipped to bottom-left)
              dodivide(
                fX_center,
                fY_center,
                fZ_center,
                fX_mid12_2,
                fY_mid12_2,
                fZ_mid12_2,
                fX2_3D,
                fY2_3D,
                fZ2_3D,
                fX_mid23,
                fY_mid23,
                fZ_mid23,
                iScreenX_center,
                iScreenY_center,
                iScreenX_mid12_2,
                iScreenY_mid23,
                iScreenX2,
                iScreenY2,
                iScreenX_mid23_1,
                iScreenY_mid30_1,
                iTexU_right,
                iTexV_bottom,
                iTexWid,
                iTexHgt);

              // Update for bottom-left quadrant (flipped to bottom-right)
              fX0_3D = (fX3_3D + fX0_3D) * 0.5f;
              fY0_3D = (fY3_3D + fY0_3D) * 0.5f;
              fZ0_3D = (fZ3_3D + fZ0_3D) * 0.5f;
              fX1_3D = (fX_mid01 + fX_mid23) * 0.5f;
              fY1_3D = (fMidZ_quad + fY_mid23) * 0.5f;
              fZ1_3D = (fY_mid01 + fZ_mid23) * 0.5f;
              fX2_3D = (fX2_3D + fX3_3D) * 0.5f;
              fY2_3D = (fY2_3D + fY3_3D) * 0.5f;
              fZ2_3D = (fZ2_3D + fZ3_3D) * 0.5f;
              iScreenX0 = iScreenX_mid30_1;
              iScreenX2 = iScreenX_mid23_1;
              iScreenY1 = iScreenY_center;
              iScreenY2 = iScreenY_mid30_1;
              iScreenY0 = iScreenY_mid30_2;
              iTexV += iTexHgt;
              continue;
            case 2:                             // vertical flip
              // Top-left quadrant (flipped to bottom-left)
              dodivide(
                fX_mid01,
                fMidZ_quad,
                fY_mid01,
                fX1_3D,
                fY1_3D,
                fZ1_3D,
                fX_mid12_2,
                fY_mid12_2,
                fZ_mid12_2,
                fX_center,
                fY_center,
                fZ_center,
                iScreenX_mid01,
                iScreenY_mid01,
                iScreenX1,
                iScreenY1,
                iScreenX_mid12_2,
                iScreenY_mid23,
                iScreenX_center,
                iScreenY_center,
                iTexU,
                iTexV_bottom,                   // bottom tex
                iTexWid,
                iTexHgt);

              // Top-right quadrant (flipped to bottom-right)
              dodivide(
                fX0_3D,
                fY0_3D,
                fZ0_3D,
                fX_mid01,
                fMidZ_quad,
                fY_mid01,
                fX_center,
                fY_center,
                fZ_center,
                fX_mid30_1,
                fY_mid30_1,
                fZ_mid30_1,
                iScreenX0,
                iScreenY0,
                iScreenX_mid01,
                iScreenY_mid01,
                iScreenX_center,
                iScreenY_center,
                iScreenX_mid30_1,
                iScreenY_mid30_2,
                iTexU_right,
                iTexV_bottom,                   // bottom-right tex
                iTexWid,
                iTexHgt);

              iScreenY1 = iScreenY_center;

              // bottom-right quadrant (flipped to top-right)
              dodivide(
                fX_center,
                fY_center,
                fZ_center,
                fX_mid12_2,
                fY_mid12_2,
                fZ_mid12_2,
                fX2_3D,
                fY2_3D,
                fZ2_3D,
                fX_mid23,
                fY_mid23,
                fZ_mid23,
                iScreenX_center,
                iScreenY_center,
                iScreenX_mid12_2,
                iScreenY_mid23,
                iScreenX2,
                iScreenY2,
                iScreenX_mid23_1,
                iScreenY_mid30_1,
                iTexU,
                iTexV,                          // top tex
                iTexWid,
                iTexHgt);

              // Update for bottom-left quadrant (flipped to top-left)
              fX0_3D = (fX3_3D + fX0_3D) * 0.5f;
              fY0_3D = (fY3_3D + fY0_3D) * 0.5f;
              fZ0_3D = (fZ3_3D + fZ0_3D) * 0.5f;
              fX1_3D = (fX_mid01 + fX_mid23) * 0.5f;
              fY1_3D = (fMidZ_quad + fY_mid23) * 0.5f;
              fZ1_3D = (fY_mid01 + fZ_mid23) * 0.5f;
              fX2_3D = (fX2_3D + fX3_3D) * 0.5f;
              fY2_3D = (fY2_3D + fY3_3D) * 0.5f;
              fZ2_3D = (fZ2_3D + fZ3_3D) * 0.5f;
              iTexU += iTexWid;
              iScreenX0 = iScreenX_mid30_1;
              iScreenX1 = iScreenX_center;
              iScreenX2 = iScreenX_mid23_1;
              iScreenY0 = iScreenY_mid30_2;
              iScreenY2 = iScreenY_mid30_1;
              continue;
            case 3:                             // both horiz and vert flip
              // top-right quadrant (flipped to bottom-left)
              dodivide(
                fX_mid01,
                fMidZ_quad,
                fY_mid01,
                fX1_3D,
                fY1_3D,
                fZ1_3D,
                fX_mid12_2,
                fY_mid12_2,
                fZ_mid12_2,
                fX_center,
                fY_center,
                fZ_center,
                iScreenX_mid01,
                iScreenY_mid01,
                iScreenX1,
                iScreenY1,
                iScreenX_mid12_2,
                iScreenY_mid23,
                iScreenX_center,
                iScreenY_center,
                iTexWid + iTexU,                // right tex
                iTexV_bottom,                   // bottom tex
                iTexWid,
                iTexHgt);

              // top-left quadrant (flipped to bottom-right)
              dodivide(
                fX0_3D,
                fY0_3D,
                fZ0_3D,
                fX_mid01,
                fMidZ_quad,
                fY_mid01,
                fX_center,
                fY_center,
                fZ_center,
                fX_mid30_1,
                fY_mid30_1,
                fZ_mid30_1,
                iScreenX0,
                iScreenY0,
                iScreenX_mid01,
                iScreenY_mid01,
                iScreenX_center,
                iScreenY_center,
                iScreenX_mid30_1,
                iScreenY_mid30_2,
                iTexU,                          // left tex
                iTexV_bottom,                   // bottom tex
                iTexWid,
                iTexHgt);

              iScreenX1 = iScreenX_center;

              // bottom-right quadrant (flipped to top-left)
              dodivide(
                fX_center,
                fY_center,
                fZ_center,
                fX_mid12_2,
                fY_mid12_2,
                fZ_mid12_2,
                fX2_3D,
                fY2_3D,
                fZ2_3D,
                fX_mid23,
                fY_mid23,
                fZ_mid23,
                iScreenX_center,
                iScreenY_center,
                iScreenX_mid12_2,
                iScreenY_mid23,
                iScreenX2,
                iScreenY2,
                iScreenX_mid23_1,
                iScreenY_mid30_1,
                iTexU_right,
                iTexV,                          // top tex
                iTexWid,
                iTexHgt);

              // update for bottom-left quadrant (flipped to top-right)
              fX0_3D = (fX3_3D + fX0_3D) * 0.5f;
              fY0_3D = (fY3_3D + fY0_3D) * 0.5f;
              fZ0_3D = (fZ3_3D + fZ0_3D) * 0.5f;
              fX1_3D = (fX_mid01 + fX_mid23) * 0.5f;
              fY1_3D = (fMidZ_quad + fY_mid23) * 0.5f;
              fZ1_3D = (fY_mid01 + fZ_mid23) * 0.5f;
              fX2_3D = (fX2_3D + fX3_3D) * 0.5f;
              fY2_3D = (fY2_3D + fY3_3D) * 0.5f;
              fZ2_3D = (fZ2_3D + fZ3_3D) * 0.5f;
              iScreenX0 = iScreenX_mid30_1;
              iScreenY1 = iScreenY_center;
              iScreenX2 = iScreenX_mid23_1;
              iScreenY0 = iScreenY_mid30_2;
              iScreenY2 = iScreenY_mid30_1;
              continue;
            default:
              return;
          }
          return;
        default:
          return;
      }
    }
    break;
  }
}

//-------------------------------------------------------------------------------------------------
//00027980
int facing_ok(float fX0, float fY0, float fZ0,
              float fX1, float fY1, float fZ1,
              float fX2, float fY2, float fZ2,
              float fX3, float fY3, float fZ3)
{
  float fDeltaX20; // [esp+20h] [ebp-4h]

  // Calculate X component of edge vector from vertex 0 to vertex 2
  fDeltaX20 = fX2 - fX0;

  // Compute the signed volume of the tetrahedron formed by the origin and three vertices
  // This is equivalent to computing the triple scalar product: (V1-V0) \B7 ((V2-V0) \D7 (V3-V0))
  // Where V0=(fX0,fY0,fZ0), V1=(fX1,fY1,fZ1), V2=(fX2,fY2,fZ2), V3=(fX3,fY3,fZ3)

  // The formula expands to a 3x3 determinant:
  //  | (fX1-fX0)  (fY1-fY0)  (fZ1-fZ0) |
  //  | (fX2-fX0)  (fY2-fY0)  (fZ2-fZ0) |
  //  | (fX3-fX0)  (fY3-fY0)  (fZ3-fZ0) |

  // Positive determinant = vertices ordered counter-clockwise when viewed from origin
  // Negative determinant = vertices ordered clockwise when viewed from origin
  return (((fY2 - fY0) * (fZ1 - fZ3) - (fZ2 - fZ0) * (fY1 - fY3)) * fX1
        + ((fZ2 - fZ0) * (fX1 - fX3) - (fZ1 - fZ3) * fDeltaX20) * fY1
        + ((fY1 - fY3) * fDeltaX20 - (fX1 - fX3) * (fY2 - fY0)) * fZ1 >= 0.0)
    - 1;
}

//-------------------------------------------------------------------------------------------------
//00027A10
int Zcmp(const void *pTrackView1, const void *pTrackView2)
{
  int iRenderPriorityCmp2; // edx
  int iRenderPriorityCmp1; // ebx
  float fZCmp2; // [esp+0h] [ebp-Ch]
  float fZCmp1; // [esp+4h] [ebp-8h]

  const tTrackZOrderEntry *pTrackZ1 = (const tTrackZOrderEntry *)pTrackView1;
  const tTrackZOrderEntry *pTrackZ2 = (const tTrackZOrderEntry *)pTrackView2;

  fZCmp1 = pTrackZ1->fZDepth;
  fZCmp2 = pTrackZ2->fZDepth;
  iRenderPriorityCmp2 = pTrackZ2->nRenderPriority;
  iRenderPriorityCmp1 = pTrackZ1->nRenderPriority;
  if (fZCmp1 < (double)fZCmp2)
    return -1;
  if (fZCmp1 == fZCmp2) {
    if (iRenderPriorityCmp1 == iRenderPriorityCmp2)
      return 0;
    if (iRenderPriorityCmp1 >= iRenderPriorityCmp2)
      return -1;
  }
  return 1;
}

//-------------------------------------------------------------------------------------------------
//00027A60
void set_starts(unsigned int uiType)
{
  startsx[1] = 0;
  startsx[2] = 0;
  startsy[0] = 0;
  startsy[1] = 0;
  if (gfx_size) {
    if (uiType) {
      if (uiType <= 1) {
        startsx[0] = 0x3FF000; //64.0
        startsx[3] = 0x3FF000; //64.0
        startsy[2] = 0x1FF000; //31.875
        startsy[3] = 0x1FF000; //31.875
      } else if (uiType == 2) {
        startsx[0] = 0x1FF000; //31.875
        startsx[3] = 0x1FF000; //31.875
        startsy[2] = 0x3FF000; //64.0
        startsy[3] = 0x3FF000; //64.0
      }
    } else {
      startsx[0] = 0x1FF000; //31.875
      startsx[3] = 0x1FF000; //31.875
      startsy[2] = 0x1FF000; //31.875
      startsy[3] = 0x1FF000; //31.875
    }
  } else if (uiType) {
    if (uiType <= 1) {
      startsx[0] = 0x7FF000; //128.0
      startsx[3] = 0x7FF000; //128.0
      startsy[2] = 0x3FF000; //64.0
      startsy[3] = 0x3FF000; //64.0
    } else if (uiType == 2) {
      startsx[0] = 0x3FF000; //64.0
      startsx[3] = 0x3FF000; //64.0
      startsy[2] = 0x7FF000; //128.0
      startsy[3] = 0x7FF000; //128.0
    }
  } else {
    startsx[0] = 0x3FF000; //64.0
    startsx[3] = 0x3FF000; //64.0
    startsy[2] = 0x3FF000; //64.0
    startsy[3] = 0x3FF000; //64.0
  }
}

//-------------------------------------------------------------------------------------------------
