#include "control.h"
#include "view.h"
#include "loadtrak.h"
#include "sound.h"
#include "function.h"
#include "transfrm.h"
#include "moving.h"
#include "network.h"
#include "func2.h"
#include "replay.h"
#include "colision.h"
#include "frontend.h"
#include "roller.h"
#include <math.h>
#include <float.h>
#include <string.h>
#include <assert.h>
//-------------------------------------------------------------------------------------------------

float levels[7] = { 100.0f, 97.0f, 94.0f, 90.0f, 85.0f, 80.0f, -1.0f }; //000A4290
float mineff[6] = { 1.0f, 0.95999998f, 0.92000002f, 0.88f, 0.83999997f, 0.80000001f }; //000A42AC
int flipst[6] = { 0, 20, 40, 60, 80, 100 }; //000A42C4
int level = 3;                    //000A42DC
tCarStrategy CarStrategy[16] =    //000A42E8
{
  { { 20, 40, 40, 0, 2 },   2.5f, 1.0f, 3.0f, 1.0f, 5000.0f },
  { { 50, 30, 10, 10, 0 },  4.0f, 1.0f, 2.8f, 1.0f, 5000.0f },
  { { 70, 20, 5, 5, 0 },    2.0f, 1.0f, 1.6f, 1.6f, 5000.0f },
  { { 90, 5, 0, 5, 0 },     1.4f, 1.0f, 4.0f, 3.0f, 5000.0f },
  { { 10, 50, 35, 5, 2 },   1.4f, 1.0f, 3.0999999f, 1.0f, 9000.0f },
  { { 30, 45, 5, 20, 0 },   2.0f, 1.0f, 2.0f, 1.0f, 9000.0f },
  { { 10, 20, 70, 0, 4 },   2.0f, 2.0f, 4.3000002f, 1.0f, 8000.0f },
  { { 30, 30, 5, 35, 2 },   2.5f, 1.2f, 2.8f, 1.0f, 8000.0f },
  { { 25, 25, 25, 25, 40 }, 3.0f, 2.0f, 3.0f, 2.0f, 5000.0f },
  { { 20, 70, 5, 5, 0 },    1.4f, 1.0f, 2.0f, 1.0f, 5000.0f },
  { { 70, 5, 5, 20, 0 },    2.0f, 1.0f, 4.0f, 1.4f, 5000.0f },
  { { 15, 75, 5, 5, 0 },    1.4f, 1.0f, 3.4000001f, 1.0f, 5000.0f },
  { { 70, 20, 10, 0, 0 },   3.0f, 1.0f, 4.0f, 1.0f, 11000.0f },
  { { 85, 5, 5, 5, 0 },     3.0f, 1.0f, 4.0f, 1.4f, 11000.0f },
  { { 20, 60, 5, 15, 0 },   4.0f, 1.2f, 4.0f, 1.4f, 9000.0f },
  { { 20, 40, 0, 40, 1 },   3.0f, 2.0f, 3.4000001f, 1.0f, 9000.0f }
};
int nearcall[4][4];               //00149750
int carorder[16];                 //00149790
int stops[10];                    //001497D0
float trial_times[96];            //001497F8
float eng_chg_revs[168];          //00149978
int FirstTick;                    //00149C18
int JBYmax;                       //00149C24
int JBYmin;                       //00149C30
int JBXmin;                       //00149C34
int JBXmax;                       //00149C3C
int JAYmax;                       //00149C40
int JAXmax;                       //00149C44
int numstops;                     //00149C48
int JAYmin;                       //00149C4C
int JAXmin;                       //00149C50
int race_started;                 //00149C54
int updates;                      //00149C58
float RecordLaps[25];             //00149C5C
int RecordCars[25];               //00149CC0
int RecordKills[25];              //00149D24
int Destroyed;                    //00149D8C
int nearcarcheck;                 //00149D90
int ahead_sect;                   //00149D94
int ahead_time;                   //00149D98
int Victim;                       //00149D9C
int Fatality;                     //00149DB0
int Fatality_Count;               //00149DB4
int cheat_control;                //00149DB8
int rightang;                     //00149DBC
int leftang;                      //00149DC0
int fudge_wait;                   //00149DC4
char RecordNames[25][9];          //00149DC8

//-------------------------------------------------------------------------------------------------
//00029640
void humancar(int iCarIdx)
{
  int iTrakLen; // ebp
  int iInput; // eax
  int iSteeringInput; // edi
  uint16 unFlags; // ax
  int byCarDesignIdx; // esi
  int iFinisherCount; // edx
  int iCarIndex1; // ecx
  int iCarIndex2; // eax
  uint8 byCheatAmmo_1; // bl
  int iCurrentChunk; // eax
  int iColorIndex; // edx
  int iTrackColor1; // ecx
  int iTrackColor2; // esi
  int iTrackColor3; // ecx
  uint8 byNewCheatAmmo; // ch
  uint8 byCheatAmmo; // bh
  int iSoundEffect1; // eax
  int iTargetCarIdx; // eax
  int iTargetCar1; // edx
  int iTargetCar2; // edx
  int iPlayerIdx; // eax
  uint8 byCheatAmmo_2; // bh
  int iSoundEffect2; // eax
  int iJumpTargetIdx; // eax
  int iJumpTarget; // ebx
  int iJumpTargetCar; // eax
  int iTargetCarDesign; // ecx
  double dNewZ; // st7
  uint8 byCheatAmmo_3; // cl
  int iSoundEffect3; // eax
  int iTeleportTargetIdx; // eax
  int iTeleportTarget; // esi
  int nReferenceChunk; // ecx
  int nPitch; // edx
  int nCurrChunk; // edi
  tVec3 *posAy; // edi
  double dLocalZ; // st7
  float fChunkValue; // eax
  int iPlayerIdx2; // eax
  tCar *pCar; // eax
  int iElevation; // [esp+0h] [ebp-40h] BYREF
  int iAzimuth; // [esp+4h] [ebp-3Ch] BYREF
  int iBank; // [esp+8h] [ebp-38h] BYREF
  float fLocalZ; // [esp+Ch] [ebp-34h]
  int iAzi2; // [esp+10h] [ebp-30h] BYREF
  float fLocalX; // [esp+14h] [ebp-2Ch]
  float fLocalY; // [esp+18h] [ebp-28h]
  int iTeleportTargetIndex; // [esp+1Ch] [ebp-24h]
  int iCarIdx_1; // [esp+20h] [ebp-20h]
  int iControlFlags; // [esp+24h] [ebp-1Ch]

  iTrakLen = TRAK_LEN;                          // Initialize track length and player index
  iCarIdx_1 = iCarIdx;
  iInput = (int16)copy_multiple[readptr][iCarIdx].data.unInput;// Extract steering input and control flags from player input data
  iSteeringInput = iInput / 256;
  //iSteeringInput = (unInput - (__CFSHL__(unInput >> 31, 8) + (unInput >> 31 << 8))) >> 8;
  unFlags = copy_multiple[readptr][iCarIdx_1].data.unFlags;
  byCarDesignIdx = Car[iCarIdx_1].byCarDesignIdx;
  iControlFlags = unFlags;
  //LOWORD(iControlFlags) = unFlags;
  if (byCarDesignIdx < 8)                     // Enable cheat mode for special car designs (8-12), disable for normal cars
    cheat_control = 0;
  else
    cheat_control = unFlags & 0x20;
  if (finished_car[iCarIdx_1] || racers - 1 == finishers && Car[iCarIdx_1].byLap < NoOfLaps && competitors > 1)// Force brake mode if car finished or race is over for this player
    iControlFlags = 2;
    //LOWORD(iControlFlags) = 2;
  //if (racers - 1 == finishers && Car[iCarIdx_1].byLap < NoOfLaps && (LODWORD(Car[iCarIdx_1].fFinalSpeed) & 0x7FFFFFFF) == 0 && competitors > 1)// Handle race finish condition: play finish sounds and mark player as finished
  if (racers - 1 == finishers && Car[iCarIdx_1].byLap < NoOfLaps && fabs(Car[iCarIdx_1].fFinalSpeed) == 0 && competitors > 1)// Handle race finish condition: play finish sounds and mark player as finished
  {
    if (player1_car == iCarIdx_1 || player2_car == iCarIdx_1) {
      if ((char)Car[iCarIdx_1].byLives > 0)
        speechsample(SOUND_SAMPLE_RUBBISH, 0x8000, 18, iCarIdx_1);// SOUND_SAMPLE_RUBBISH
      speechsample(SOUND_SAMPLE_RACEOVER, 0x8000, 18, iCarIdx_1);  // SOUND_SAMPLE_RACEOVER
    }
    iTrakLen = TRAK_LEN;
    iFinisherCount = human_finishers;
    finished_car[iCarIdx_1] = -1;
    human_finishers = iFinisherCount + 1;
    ++finishers;
  }
  if (Car[iCarIdx_1].fHealth <= 0.0)          // Disable controls if car health is zero or below
    iControlFlags = 0;
    //LOWORD(unControlFlags) = 0;
  iCarIndex1 = iCarIdx_1;
  iCarIndex2 = iCarIdx_1;
  Car[iCarIdx_1].iSteeringInput = iSteeringInput;// Store steering input and handle cheat power activation
  if (!cheat_control)
    goto PROCESS_VEHICLE_CONTROLS;
  switch (byCarDesignIdx) {
    case 8:                                     // SUICYCO (explode opponent)
      if (Car[iCarIndex1].byCheatCooldown)    // Car type 8: finds nearest car and applies massive damage
        goto PROCESS_VEHICLE_CONTROLS;
      byCheatAmmo = Car[iCarIndex1].byCheatAmmo;
      TRAK_LEN = iTrakLen;
      if (!byCheatAmmo) {
        if (!cheatsampleok(iCarIndex1))
          goto SET_DAMAGE_COOLDOWN;
        iSoundEffect1 = SOUND_SAMPLE_BLOP;
        goto PLAY_DAMAGE_SOUND;
      }
      if (Car[iCarIndex1].nCurrChunk == -1) {
        if (!cheatsampleok(iCarIndex1))
          goto SET_DAMAGE_COOLDOWN;
        iSoundEffect1 = SOUND_SAMPLE_BRP;
        goto PLAY_DAMAGE_SOUND;
      }
      iTargetCarIdx = findcardistance(iCarIndex1, 8000.0);// Find target car within range (8000 units)
      iTargetCar1 = iTargetCarIdx;
      if (iTargetCarIdx >= 0 && Car[iTargetCarIdx].nCurrChunk != -1 && Car[iTargetCarIdx].byCarDesignIdx != 13) {
        Car[iTargetCarIdx].byAttacker = iCarIdx_1;// Apply damage: 200 damage, stop target car, consume ammo
        Car[iTargetCarIdx].byDamageSourceTimer = -40;
        dodamage(&Car[iTargetCarIdx], 200.0);
        Car[iTargetCar1].fFinalSpeed = 0.0;
        Car[iTargetCar1].byGearAyMax = 0;
        Car[iTargetCar1].fBaseSpeed = 0.0;
        Car[iTargetCar1].fSpeedOverflow = 0.0;
        Car[iTargetCar1].fRPMRatio = 0.0;
        iTargetCar2 = iTargetCar1;
        Car[iTargetCar2].fPower = 0.0;
        iPlayerIdx = iCarIdx_1;
        Car[iTargetCar2].nDeathTimer = 18;
        --Car[iPlayerIdx].byCheatAmmo;
        goto SET_DAMAGE_COOLDOWN;
      }
      if (cheatsampleok(iCarIdx_1)) {
        iSoundEffect1 = SOUND_SAMPLE_BRP;
      PLAY_DAMAGE_SOUND:
        sfxsample(iSoundEffect1, 0x8000);
      }
    SET_DAMAGE_COOLDOWN:
      iTrakLen = TRAK_LEN;
      Car[iCarIdx_1].byCheatCooldown = 36;      // Set cheat power cooldown (36 frames) after use
      goto PROCESS_VEHICLE_CONTROLS;
    case 9:                                     // MAYTE (top speed)
      iControlFlags = iControlFlags | 1;
      //LOBYTE(unControlFlags) = unControlFlags | 1;
      goto PROCESS_VEHICLE_CONTROLS;
    case 10:                                    // 2X4B523P (flip opponent)
      if (Car[iCarIndex2].byCheatCooldown)
        goto PROCESS_VEHICLE_CONTROLS;
      byCheatAmmo_2 = Car[iCarIndex2].byCheatAmmo;
      TRAK_LEN = iTrakLen;
      if (!byCheatAmmo_2) {
        if (!cheatsampleok(iCarIndex1))
          goto SET_JUMP_COOLDOWN;
        iSoundEffect2 = SOUND_SAMPLE_BLOP;                     // SOUND_SAMPLE_BLOP
        goto PLAY_JUMP_SOUND;
      }
      if (Car[iCarIndex2].nCurrChunk == -1) {
        if (!cheatsampleok(iCarIndex1))
          goto SET_JUMP_COOLDOWN;
      } else {
        iJumpTargetIdx = findcardistance(iCarIndex1, 16000.0);// Find target car within jump range (16000 units)
        iJumpTarget = iJumpTargetIdx;
        if (iJumpTargetIdx >= 0) {
          iJumpTargetCar = iJumpTargetIdx;
          if (Car[iJumpTargetCar].nCurrChunk != -1 && Car[iJumpTargetCar].byCarDesignIdx != 13) {
            iTargetCarDesign = Car[iJumpTargetCar].byCarDesignIdx;// Apply jump effect: launch target car into air with roll and Z-offset
            Car[iJumpTargetCar].nRoll = 0x2000;
            dNewZ = CarBox.hitboxAy[iTargetCarDesign][4].fZ + Car[iJumpTargetCar].pos.fZ;
            Car[iJumpTargetCar].iStunned = -1;
            Car[iJumpTargetCar].iSteeringInput = 0;
            Car[iJumpTargetCar].iJumpMomentum = 2048;
            Car[iJumpTargetCar].pos.fZ = (float)dNewZ;
            if (cheatsampleok(iCarIdx_1))
              sfxsample(SOUND_SAMPLE_BOING, 0x8000);            // SOUND_SAMPLE_BOING
            if (cheatsampleok(iJumpTarget))
              sfxsample(SOUND_SAMPLE_BOING, 0x8000);            // SOUND_SAMPLE_BOING
            --Car[iCarIdx_1].byCheatAmmo;
          SET_JUMP_COOLDOWN:
            iTrakLen = TRAK_LEN;
            Car[iCarIdx_1].byCheatCooldown = 36;
            goto PROCESS_VEHICLE_CONTROLS;
          }
        }
        if (!cheatsampleok(iCarIdx_1))
          goto SET_JUMP_COOLDOWN;
      }
      iSoundEffect2 = SOUND_SAMPLE_BRP;                       // SOUND_SAMPLE_BRP
    PLAY_JUMP_SOUND:
      sfxsample(iSoundEffect2, 0x8000);
      goto SET_JUMP_COOLDOWN;
    case 11:                                    // TINKLE (swap places with opponent)
      if (Car[iCarIndex2].byCheatCooldown)
        goto PROCESS_VEHICLE_CONTROLS;
      byCheatAmmo_3 = Car[iCarIndex2].byCheatAmmo;
      TRAK_LEN = iTrakLen;
      if (!byCheatAmmo_3) {
        if (!cheatsampleok(iCarIdx_1))
          goto SET_TELEPORT_COOLDOWN;
        iSoundEffect3 = SOUND_SAMPLE_BLOP;
        goto PLAY_TELEPORT_SOUND;
      }
      if (Car[iCarIndex2].nCurrChunk == -1) {
        if (!cheatsampleok(iCarIdx_1))
          goto SET_TELEPORT_COOLDOWN;
        goto PLAY_TELEPORT_FAIL_SOUND;
      }
      iTeleportTargetIdx = findcardistance(iCarIdx_1, 16000.0);// Find target car for teleport swap (16000 units)
      iTeleportTargetIndex = iTeleportTargetIdx;
      if (iTeleportTargetIdx < 0 || (iTeleportTarget = iTeleportTargetIdx, Car[iTeleportTargetIdx].nCurrChunk == -1) || Car[iTeleportTargetIdx].byCarDesignIdx == 13) {
        if (!cheatsampleok(iCarIdx_1))
          goto SET_TELEPORT_COOLDOWN;
      PLAY_TELEPORT_FAIL_SOUND:
        iSoundEffect3 = SOUND_SAMPLE_BRP;//SOUND_SAMPLE_BRP
      PLAY_TELEPORT_SOUND:
        sfxsample(iSoundEffect3, 0x8000);
        goto SET_TELEPORT_COOLDOWN;
      }
      Car[iTeleportTargetIdx].fHorizontalSpeed = 0;
      nReferenceChunk = Car[iTeleportTargetIdx].nReferenceChunk;
      Car[iTeleportTargetIdx].iSteeringInput = 0;
      nPitch = Car[iTeleportTargetIdx].nPitch;
      Car[iTeleportTargetIdx].direction.fZ = 128.0f;
      nCurrChunk = Car[iTeleportTargetIdx].nCurrChunk;
      getworldangles(Car[iTeleportTargetIdx].nActualYaw, nPitch, Car[iTeleportTargetIdx].nRoll, nReferenceChunk, &iAzimuth, &iElevation, &iBank);// Swap car positions and orientations using world angle transformations
      getworldangles(Car[iTeleportTarget].nYaw, Car[iTeleportTarget].nPitch, Car[iTeleportTarget].nRoll, Car[iTeleportTarget].nReferenceChunk, &iAzi2, &iElevation, &iBank);
      Car[iTeleportTarget].pos.fZ = Car[iTeleportTarget].pos.fZ + 64.0f;
      Car[iTeleportTarget].nYaw = iAzi2;
      Car[iTeleportTarget].nActualYaw = iAzi2;
      Car[iTeleportTarget].nPitch = iElevation;
      posAy = localdata[nCurrChunk].pointAy;
      Car[iTeleportTarget].nRoll = iBank;
      fLocalX = posAy->fY * Car[iTeleportTarget].pos.fY + posAy->fX * Car[iTeleportTarget].pos.fX + posAy->fZ * Car[iTeleportTarget].pos.fZ - posAy[3].fX;
      fLocalY = posAy[1].fY * Car[iTeleportTarget].pos.fY + posAy[1].fX * Car[iTeleportTarget].pos.fX + posAy[1].fZ * Car[iTeleportTarget].pos.fZ - posAy[3].fY;
      dLocalZ = posAy[2].fY * Car[iTeleportTarget].pos.fY + posAy[2].fX * Car[iTeleportTarget].pos.fX + posAy[2].fZ * Car[iTeleportTarget].pos.fZ - posAy[3].fZ;
      Car[iTeleportTarget].pos.fX = fLocalX;
      Car[iTeleportTarget].iRollMomentum = 0;
      Car[iTeleportTarget].direction.fX = 0.0;
      Car[iTeleportTarget].direction.fY = 0.0;
      Car[iTeleportTarget].iControlType = 0;
      Car[iTeleportTarget].fFinalSpeed = 0.0;
      Car[iTeleportTarget].fBaseSpeed = 0.0;
      Car[iTeleportTarget].fSpeedOverflow = 0.0;
      fLocalZ = (float)dLocalZ;
      Car[iTeleportTarget].fRPMRatio = 0.0;
      fChunkValue = fLocalY;
      Car[iTeleportTarget].fPower = 0.0;
      Car[iTeleportTarget].pos.fY = fChunkValue;
      Car[iTeleportTarget].nDeathTimer = 18;
      Car[iTeleportTarget].pos.fZ = fLocalZ;
      //LOWORD(fChunkValue) = Car[iTeleportTarget].nCurrChunk;
      int16 nTemp = Car[iTeleportTarget].nCurrChunk;
      Car[iTeleportTarget].nReferenceChunk = nTemp; //LOWORD(fChunkValue);
      Car[iTeleportTarget].iLastValidChunk = nTemp;// SLOWORD(fChunkValue);
      Car[iTeleportTarget].byAttacker = iCarIdx_1;
      Car[iTeleportTarget].byGearAyMax = 0;
      Car[iTeleportTarget].byDamageSourceTimer = -40;
      iPlayerIdx2 = iCarIdx_1;
      Car[iTeleportTarget].nCurrChunk = -1;
      if (cheatsampleok(iPlayerIdx2))
        sfxsample(SOUND_SAMPLE_UP, 0x8000);                  // SOUND_SAMPLE_UP
      if (cheatsampleok(iTeleportTargetIndex))
        sfxsample(SOUND_SAMPLE_UP, 0x8000);                  // SOUND_SAMPLE_UP
      --Car[iCarIdx_1].byCheatAmmo;
    SET_TELEPORT_COOLDOWN:
      iTrakLen = TRAK_LEN;
      Car[iCarIdx_1].byCheatCooldown = 36;
    PROCESS_VEHICLE_CONTROLS:
      TRAK_LEN = iTrakLen;
      if (race_started)                       // Process main vehicle controls: gear changes, material selection, throttle/brake
      {                                         // Handle gear shift controls if race has started
        if ((iControlFlags & 4) != 0)
          GoUpGear(&Car[iCarIdx_1]);
        if ((iControlFlags & 8) != 0)
          GoDownGear(&Car[iCarIdx_1], iControlFlags & 1);
        if (player_type != 2)                 // Handle material selection controls (paint job changes)
        {
          if ((iControlFlags & 0x40) != 0)
            changemateto(iCarIdx_1, 0);
          if ((iControlFlags & 0x80u) != 0)
            changemateto(iCarIdx_1, 1);
          if ((iControlFlags & 0x100) != 0)
            changemateto(iCarIdx_1, 2);
          if ((iControlFlags & 0x200) != 0)
            changemateto(iCarIdx_1, 3);
        }
      }
      pCar = &Car[iCarIdx_1];                   // Apply vehicle physics based on throttle/brake input
      if ((iControlFlags & 2) != 0)          // Check control flags: bit 2=brake, bit 1=accelerate, else=freewheel
      {
        Decelerate(pCar);
      } else if ((iControlFlags & 1) != 0) {
        Accelerate(pCar);
      } else {
        FreeWheel(pCar);
      }
      return;
    case 12:                                    // LOVEBUN (formula car)
      if (!Car[iCarIndex2].byCheatCooldown) {
        byCheatAmmo_1 = Car[iCarIndex2].byCheatAmmo;
        TRAK_LEN = iTrakLen;
        if (byCheatAmmo_1) {
          iCurrentChunk = Car[iCarIndex2].nCurrChunk;
          if (iCurrentChunk != -1) {
            iColorIndex = -12;                  // Modify track surface in 16-chunk range around car position
            do {
              if ((TrakColour[iCurrentChunk][1] & (SURFACE_FLAG_NO_SPAWN | SURFACE_FLAG_WALL_22)) == 0) {
                iTrackColor1 = TrakColour[iCurrentChunk][1] & SURFACE_MASK_FLAGS ^ SURFACE_FLAG_APPLY_TEXTURE;
                TrakColour[iCurrentChunk][1] = (159 - (iColorIndex & 7)) | iTrackColor1;
                localdata[iCurrentChunk].iCenterGrip = 12;
              }
              if ((TrakColour[iCurrentChunk][0] & (SURFACE_FLAG_NO_SPAWN | SURFACE_FLAG_WALL_22)) == 0) {
                iTrackColor2 = TrakColour[iCurrentChunk][0] & SURFACE_MASK_FLAGS ^ SURFACE_FLAG_APPLY_TEXTURE;
                TrakColour[iCurrentChunk][0] = iTrackColor2 | (159 - (iColorIndex & 7));
                localdata[iCurrentChunk].iLeftShoulderGrip = 12;
              }
              if ((TrakColour[iCurrentChunk][2] & (SURFACE_FLAG_NO_SPAWN | SURFACE_FLAG_WALL_22)) == 0) {
                iTrackColor3 = TrakColour[iCurrentChunk][1] & SURFACE_MASK_FLAGS ^ SURFACE_FLAG_APPLY_TEXTURE;
                TrakColour[iCurrentChunk][2] = (159 - (iColorIndex & 7)) | iTrackColor3;
                localdata[iCurrentChunk].iRightShoulderGrip = 12;
              }
              if (++iCurrentChunk >= iTrakLen)
                iCurrentChunk = 0;
              ++iColorIndex;
            } while (iColorIndex < 4);
            byNewCheatAmmo = Car[iCarIdx_1].byCheatAmmo - 1;
            TRAK_LEN = iTrakLen;
            Car[iCarIdx_1].byCheatAmmo = byNewCheatAmmo;
          }
        } else if (cheatsampleok(iCarIndex1)) {
          sfxsample(SOUND_SAMPLE_BLOP, 0x8000);                // SOUND_SAMPLE_BLOP
        }
        iTrakLen = TRAK_LEN;
        Car[iCarIdx_1].byCheatCooldown = 36;
      }
      goto PROCESS_VEHICLE_CONTROLS;
    default:
      goto PROCESS_VEHICLE_CONTROLS;            // Cheat power switch: handle different special car abilities
  }
}

//-------------------------------------------------------------------------------------------------
//0002A060
void GoUpGear(tCar *pCar)
{
  int iCarDesignIdx; // esi
  tCarEngine *pEngineData; // edi
  int byGearAyMax; // ecx
  int iTargetGear; // ecx
  float *pSpds; // edi
  float fNewPowerForward; // [esp+0h] [ebp-2Ch]
  float fNewPowerReverse; // [esp+0h] [ebp-2Ch]
  float fMinShiftSpeed; // [esp+4h] [ebp-28h]
  float fAdjustedSpeed; // [esp+8h] [ebp-24h]
  float fHealthFactor; // [esp+Ch] [ebp-20h]
  float fRPMRatio; // [esp+10h] [ebp-1Ch]

  iCarDesignIdx = pCar->byCarDesignIdx;         // Get car design index and engine data
  pEngineData = &CarEngines.engines[iCarDesignIdx];
  byGearAyMax = (char)pCar->byGearAyMax;
  if (byGearAyMax < pEngineData->iNumGears - 1)// Check if car can upshift (not already in top gear)
  {
    fHealthFactor = (pCar->fHealth + 34.0f) * 0.01f;// Calculate health factor (0-1) based on car damage, capped at 1.0
    iTargetGear = byGearAyMax + 1;
    if (fHealthFactor > 1.0)
      fHealthFactor = 1.0;
    fMinShiftSpeed = -1.0;                      // Initialize minimum shift speed to -1 (no restriction)
    fAdjustedSpeed = (float)(fabs(pCar->fFinalSpeed) / fHealthFactor);// Calculate current speed adjusted by health factor
    if (human_control[pCar->iDriverIdx] != 2 && iTargetGear > 0)// For AI drivers, set minimum speed for upshift (50% of current gear max speed)
      fMinShiftSpeed = pEngineData->pSpds[iTargetGear - 1] * 0.5f;
    if (fAdjustedSpeed >= (double)fMinShiftSpeed)// Check if car is fast enough to upshift
    {
      pSpds = pEngineData->pSpds;
      if (iTargetGear < 0)                    // Handle reverse gear upshift (gear < 0)
      {
        fRPMRatio = fAdjustedSpeed / *pSpds;
        if (fRPMRatio > 1.0)
          fRPMRatio = 1.0;
      } else {
        fRPMRatio = fAdjustedSpeed / pSpds[iTargetGear];// Calculate RPM ratio for target gear
        if (fRPMRatio > 1.0)
          fRPMRatio = 1.0;
      }
      pCar->fRPMRatio = fRPMRatio;
      if (player1_car == pCar->iDriverIdx && !DriveView[0])// Play gear shift sound for human players (not in cockpit view)
        sfxsample(SOUND_SAMPLE_GRSHIFT, 12000);                    // SOUND_SAMPLE_GRSHIFT
      if (player2_car == pCar->iDriverIdx && !DriveView[1])
        sfxsample(SOUND_SAMPLE_GRSHIFT, 12000);                    // SOUND_SAMPLE_GRSHIFT
      if (iTargetGear < 0)                    // Handle upshift to reverse gear (set gear 0)
      {
        fNewPowerReverse = (float)calc_pow(iCarDesignIdx, 0, fRPMRatio);
        pCar->fBaseSpeed = 0.0;
        pCar->byGearAyMax = iTargetGear;
        pCar->fPower = fNewPowerReverse;
      } else {
        fNewPowerForward = (float)calc_pow(iCarDesignIdx, iTargetGear, fRPMRatio);// Handle normal forward gear upshift
        pCar->byGearAyMax = iTargetGear;
        pCar->fPower = fNewPowerForward;
        pCar->fBaseSpeed = pSpds[iTargetGear] * fRPMRatio * fHealthFactor;
      }
      pCar->fSpeedOverflow = pCar->fFinalSpeed - pCar->fBaseSpeed;// Update speed overflow (difference between current and base speed)
    }
  }
}

//-------------------------------------------------------------------------------------------------
//0002A200
void GoDownGear(tCar *pCar, int iUseAutoLogic)
{
  int iCarDesignIdx; // ecx
  tCarEngine *pEngineData; // edi
  int iGearAyMax; // eax
  int iTargetGear; // eax
  int iGearForCalc; // edx
  int iCurrentGear; // eax
  tCar *pCarCopy; // ebx
  double dBaseSpeed; // st7
  float *pSpds; // ecx
  double dMaxSpeed; // st7
  float fRPMRatio; // [esp+0h] [ebp-28h]
  float fDownshiftThreshold; // [esp+4h] [ebp-24h]
  float fHealthFactor; // [esp+8h] [ebp-20h]
  float fCalculatedPower; // [esp+Ch] [ebp-1Ch]
  int iTargetGearIdx; // [esp+10h] [ebp-18h]

  fHealthFactor = (pCar->fHealth + 34.0f) * 0.01f;// Calculate health factor (0-1) based on car damage
  iCarDesignIdx = pCar->byCarDesignIdx;        // Get car design index and engine data
  pEngineData = &CarEngines.engines[iCarDesignIdx];
  if (fHealthFactor > 1.0)
    fHealthFactor = 1.0;
  iGearAyMax = (char)pCar->byGearAyMax;
  if (iGearAyMax > -2)                       // Check if car can downshift (not already in lowest gear -2)
  {
    iTargetGear = iGearAyMax - 1;              // Calculate target gear (current gear - 1)
    iTargetGearIdx = iTargetGear;
    if (iTargetGear < 0)                      // Handle reverse gear downshift (target < 0)
    {
      iCurrentGear = 1;
      pCarCopy = pCar;
      iGearForCalc = 0;
    } else {
      iGearForCalc = iTargetGear;               // Handle forward gear downshift
      iCurrentGear = iTargetGear + 1;
      pCarCopy = pCar;
    }
    fCalculatedPower = (float)change_gear(iCurrentGear, iGearForCalc, pCarCopy, iCarDesignIdx);// Calculate power output for target gear using change_gear function
    fDownshiftThreshold = 1000.0;               // Set downshift threshold: high value for human, gear change value * 1.2 for AI
    if (human_control[pCar->iDriverIdx] != 2 && iUseAutoLogic)
      fDownshiftThreshold = pEngineData->pChgs[2 * iTargetGearIdx] * 1.2f;
    if (fCalculatedPower < (double)fDownshiftThreshold)// Check if calculated power is below downshift threshold
    {
      pCar->fPower = fCalculatedPower;
      pCar->byGearAyMax = iTargetGearIdx;
      if (iTargetGearIdx == -1)               // Handle downshift to reverse gear (-1): zero base speed
      {
        dBaseSpeed = pCar->fBaseSpeed;
        pCar->fBaseSpeed = 0.0;
      } else {
        pSpds = pEngineData->pSpds;
        if (iTargetGearIdx >= 0)              // Handle forward gear downshift: calculate RPM ratio and speeds
        {
          fRPMRatio = (float)calc_revs(pEngineData->pRevs, iTargetGearIdx, fCalculatedPower);// Forward gear: use target gear curve data and positive max speed
          pCar->fRPMRatio = fRPMRatio;
          dMaxSpeed = pSpds[iTargetGearIdx];
        } else {
          fRPMRatio = (float)calc_revs(pEngineData->pRevs, 0, fCalculatedPower);// Reverse gear: use gear 0 curve data, negative max speed
          pCar->fRPMRatio = fRPMRatio;
          dMaxSpeed = -*pSpds;
        }
        pCar->fBaseSpeed = (float)dMaxSpeed * fRPMRatio * fHealthFactor;// Calculate new base speed: max speed * RPM ratio * health factor
        dBaseSpeed = pCar->fFinalSpeed - pCar->fBaseSpeed;// Update speed overflow (difference between final and base speed)
      }
      pCar->fSpeedOverflow = (float)dBaseSpeed;
    }
  }
}

//-------------------------------------------------------------------------------------------------
//0002A350
void control()
{
  int iChecksum; // ebx
  int iCarCounter; // edi
  int iCompetitorIdx; // esi
  int iCarIdx; // ecx
  double dHealth; // st7
  //int iCarArraySize; // ebx
  //int iByteOffset; // eax
  //int iStructOffset; // ecx
  //int iDataValue; // edx
  int iFatalityCounter; // edx
  int iCar; // ecx
  int iCar_1; // eax
  int iCarArrayIdx; // ebx
  int iCarStructIdx; // edx
  int iCarLoopIdx; // ecx
  uint8 byStatusFlags; // ah
  int16 nTimerValue; // ax
  int iUpdateCarIdx; // ecx
  int iCarUpdateIdx; // edx
  uint8 byUnk43; // bh
  int i; // ecx
  unsigned int uiSLightOffset; // edx
  float fLightSpeed; // esi
  double dLightPosX; // st7
  double dLightPosY; // st7
  double dLightPosZ; // st7
  double dLightSpeedZ; // st7
  int iCrossLineIdx; // ebx
  tCar *pCarPtr; // edx
  int iMotionCarIdx; // ecx
  tCar *pCurrentCar; // ebx
  tCar *pMotionCar; // edx
  double dDamageFactor; // st7
  double dRollMotionCalc; // st7
  double dPitchMotionCalc; // st7
  double dYawMotionCalc; // st7
  int iNumCarsTemp; // edi
  int j; // ecx
  char bySampleValue; // ah
  int iNetworkArraySize; // ecx
  int iCarOffset; // edx
  int iMasterCarIdx; // ebx
  int iMasterByteIdx; // edx
  int iSlaveByteIdx; // ebx
  int iSlaveCarIdx; // edx
  int iRacingByteIdx; // ebx
  int iFinalArraySize; // ecx
  int iFinalCarIdx; // edx
  int iFinalByteIdx; // ebx
  int8 byUnk68; // ah
  uint8 byNewValue; // al
  int aiCarStates[17]; // [esp+0h] [ebp-9Ch]
  double dLightDistanceX; // [esp+44h] [ebp-58h]
  double dLightDistanceY; // [esp+4Ch] [ebp-50h]
  double dLightDistanceZ; // [esp+54h] [ebp-48h]
  float fLightDeltaZ; // [esp+5Ch] [ebp-40h]
  float fX; // [esp+60h] [ebp-3Ch]
  float fLightDeltaX; // [esp+64h] [ebp-38h]
  float fDamageMultiplier; // [esp+68h] [ebp-34h]
  float fHealthFactor; // [esp+6Ch] [ebp-30h]
  float fHealthValue; // [esp+70h] [ebp-2Ch]
  int iRandomValue; // [esp+74h] [ebp-28h]
  float fZ; // [esp+78h] [ebp-24h]
  float fY; // [esp+7Ch] [ebp-20h]
  float fLightDeltaY; // [esp+80h] [ebp-1Ch]

  updates = 0;                                  // Initialize update counter and handle replay mode
  if (replaytype == 2)
    readptr = -10000;
  if (readptr == writeptr)
    analysespeechsamples();
  while (readptr != writeptr)                 // Main game update loop - process each frame while data available
  {
    --view1_cnt;                                // Update frame counters and warp animation angle
    --view0_cnt;
    --Quit_Count;
    warp_angle = (warp_angle + 512) & 0x3FFF;
    if (fudge_wait == readptr && wConsoleNode == master)
      racing = master ^ wConsoleNode;
    ++game_frame;
    --countdown;
    if (network_on && replaytype != 2 && write_check >= 0)// Network synchronization: calculate checksums for car state validation
    {
      iChecksum = 0;
      iCarCounter = 0;
      if (numcars > 0) {
        iCompetitorIdx = 0;
        iCarIdx = 0;
        do {
          if (!non_competitors[iCompetitorIdx]) {
            dHealth = Car[iCarIdx].fHealth;
            //_CHP();
            //LODWORD(fHealthValue) = (int)dHealth;
            //iChecksum += abs16(Car[iCarIdx].nCurrChunk)
            //  + LODWORD(Car[iCarIdx].pos.fZ)
            //  + LODWORD(Car[iCarIdx].pos.fY)
            //  + LODWORD(Car[iCarIdx].pos.fX)
            //  + abs8(Car[iCarIdx].byLives)
            //  + abs32((int)dHealth);
            // Calculate network synchronization checksum from car state data
            iChecksum += abs(Car[iCarIdx].nCurrChunk)
              + *(int *)&Car[iCarIdx].pos.fX    // Float bit pattern as int
              + *(int *)&Car[iCarIdx].pos.fY
              + *(int *)&Car[iCarIdx].pos.fZ
              + abs(Car[iCarIdx].byLives)
              + abs((int)dHealth);
          }
          ++iCompetitorIdx;
          ++iCarCounter;
          ++iCarIdx;
        } while (iCarCounter < numcars);
      }
      player_checks[write_check][player1_car] = iChecksum & 0x3F;
      write_check = ((int16)write_check + 1) & 0x1FF;
    }

    if (numcars > 0)
    {
      for (int i = 0; i < numcars; i++)
      {
        // Initialize wheel spin factor to 1.0
        Car[i].fWheelSpinFactor = 1.0;

        // Store byUnk68 value in car states array
        aiCarStates[i] = Car[i].byWheelAnimationFrame;
      }
    }
    //if (numcars > 0)                          // Initialize car state array and copy car data for processing
    //{
    //  iCarArraySize = 4 * numcars;
    //  iByteOffset = 0;
    //  iStructOffset = 0;
    //  do {
    //    iStructOffset += sizeof(tCar);
    //    iByteOffset += 4;
    //    iDataValue = *((char *)&CarBox.hitboxAy[15][5].fY + iStructOffset + 2);// references adjacent data Car
    //    *(float *)((char *)&CarBox.hitboxAy[15][7].fX + iStructOffset) = 1.0;// references adjacent data Car
    //    aiCarStates[iByteOffset / 4u] = iDataValue;
    //  } while (iByteOffset < iCarArraySize);
    //}

    if (!paused)
      ++updates;
    iFatalityCounter = Fatality_Count;          // Handle fatality timer and camera zoom effects
    if (Fatality_Count > 0) {
      --Fatality_Count;
      if (iFatalityCounter == 1)
        Fatality = -1;
    }
    dozoomstuff(0);
    if (player_type == 2)
      dozoomstuff(1);
    if (readptr >= 0) {                                           // Process each car: handle race start, player controls, and network events
      for (iCar = 0; iCar < numcars; ++iCar) {                                         // Race start condition: clear high bit from gear and enable racing
        if (game_frame == 145 && (Car[iCar].byGearAyMax & 0x80u) != 0) {
          Car[iCar].byGearAyMax = 0;
          race_started = -1;
          Car[iCar].fPower = 0.0;
        }
        if (human_control[iCar]) {                                       // Network sync request: reset car to neutral state
          if ((copy_multiple[readptr][iCar].uiFullData & 0x10000000) != 0) {
            iCar_1 = iCar;
            Car[iCar_1].byGearAyMax = -1;
            Car[iCar_1].fPower = 0.0;
            Car[iCar_1].fRPMRatio = 0.0;
            Car[iCar_1].fBaseSpeed = 0.0;
            Car[iCar_1].fSpeedOverflow = 0.0;
            Car[iCar_1].fFinalSpeed = 0.0;
            if (fudge_wait < 0)
              fudge_wait = ((int16)readptr + 1) & 0x1FF;
            stopallsamples();
          }
          if ((copy_multiple[readptr][iCar].uiFullData & 0x4000000) != 0)// Network player quit: cleanup and handle disconnection
          {
            memset(player_checks, -1, sizeof(player_checks));
            read_check = 0;
            write_check = 0;
            --players;
            if (finished_car[iCar])
              --human_finishers;
            if (player1_car == iCar) {
              net_quit = -1;
            } else {
              start_zoom(&language_buffer[3776], 0);
              subzoom(driver_names[iCar]);
            }
            human_control[iCar] = 0;
            initnearcars();
          }
        }
      }
    }
    if (replaytype == 2)                      // Handle replay data and update stunt system
      DoReplayData();
    updatestunts();
    if (replaytype != 2) {
      memset(newrepsample, 0, sizeof(newrepsample));// Process car controls and AI for non-replay mode
      iCarArrayIdx = 0;
      if (numcars > 0) {
        iCarStructIdx = 0;
        iCarLoopIdx = 0;
        do {
          byStatusFlags = Car[iCarStructIdx].byStatusFlags;// Check car status and handle death/damage states
          Car[iCarStructIdx].byAccelerating = 0;
          if ((byStatusFlags & 4) != 0 || !Car[iCarStructIdx].byLives) {
            //if ((LODWORD(Car[iCarStructIdx].fFinalSpeed) & 0x7FFFFFFF) == 0 || Car[iCarStructIdx].nUnk21 < 126) {
            if (fabs(Car[iCarStructIdx].fFinalSpeed) == 0 || Car[iCarStructIdx].nDeathTimer < 126) {
              nTimerValue = Car[iCarStructIdx].nExplosionSoundTimer - 1;
              --Car[iCarStructIdx].nDeathTimer;
              Car[iCarStructIdx].nExplosionSoundTimer = nTimerValue;
            }
            if (Car[iCarStructIdx].nDeathTimer < 0)
              Car[iCarStructIdx].byStatusFlags |= 2u;
          }
          if ((Car[iCarStructIdx].byLives & 0x80u) == 0) {                                     // Process car controls: human player or AI based on control type
            if (human_control[iCarLoopIdx]) {
              if (Car[iCarStructIdx].iControlType != 2 && !Car[iCarStructIdx].iStunned)
                humancar(iCarArrayIdx);
            } else if (Car[iCarStructIdx].iControlType == 3 && !Car[iCarStructIdx].iStunned) {
              autocar2(&Car[iCarStructIdx]);
            }
          }
          ++iCarStructIdx;
          ++iCarArrayIdx;
          ++iCarLoopIdx;
        } while (iCarArrayIdx < numcars);
      }
      iUpdateCarIdx = 0;                        // Update car physics and damage over time effects
      if (numcars > 0) {
        iCarUpdateIdx = 0;
        do {
          if ((Car[iCarUpdateIdx].byLives & 0x80u) == 0) {
            if (fudge_wait < 0)
              updatecar2(&Car[iCarUpdateIdx]);
            byUnk43 = Car[iCarUpdateIdx].byDamageSourceTimer;
            if (byUnk43 && Car[iCarUpdateIdx].fHealth > 0.0)
              Car[iCarUpdateIdx].byDamageSourceTimer = byUnk43 - 1;
          }
          iCarArrayIdx = numcars;
          ++iUpdateCarIdx;
          ++iCarUpdateIdx;
        } while (iUpdateCarIdx < numcars);
      }
      if (countdown > -72 && replaytype != 2) // Update dynamic lighting effects for local players
      {
        for (i = 0; i < local_players; ++i) {
          uiSLightOffset = 0x90 * i;
          do {
            fLightDeltaX = SLight[0][uiSLightOffset / 0x30].targetPos.fX - SLight[0][uiSLightOffset / 0x30].currentPos.fX;// Animate lights toward target positions with speed limiting
            fLightSpeed = fLightDeltaX;
            SLight[0][uiSLightOffset / 0x30].uiRotation = (SLight[0][uiSLightOffset / 0x30].uiRotation + 128) & 0x3FFF;
            //if ((LODWORD(fLightSpeed) & 0x7FFFFFFF) != 0) {
            if (fabs(fLightSpeed) > FLT_EPSILON) {
              fX = SLight[0][uiSLightOffset / 0x30].speed.fX;
              dLightDistanceX = fabs(fLightDeltaX);
              if (fX > dLightDistanceX)
                fX = (float)dLightDistanceX;
              if (fLightDeltaX <= 0.0)
                dLightPosX = SLight[0][uiSLightOffset / 0x30].currentPos.fX - fX;
              else
                dLightPosX = SLight[0][uiSLightOffset / 0x30].currentPos.fX + fX;
              SLight[0][uiSLightOffset / 0x30].currentPos.fX = (float)dLightPosX;
            }
            fLightDeltaY = SLight[0][uiSLightOffset / 0x30].targetPos.fY - SLight[0][uiSLightOffset / 0x30].currentPos.fY;
            //if ((LODWORD(fLightDeltaY) & 0x7FFFFFFF) != 0) {
            if (fabs(fLightDeltaY) > FLT_EPSILON) {
              fY = SLight[0][uiSLightOffset / 0x30].speed.fY;
              dLightDistanceY = fabs(fLightDeltaY);
              if (fY > dLightDistanceY)
                fY = (float)dLightDistanceY;
              if (fLightDeltaY <= 0.0)
                dLightPosY = SLight[0][uiSLightOffset / 0x30].currentPos.fY - fY;
              else
                dLightPosY = SLight[0][uiSLightOffset / 0x30].currentPos.fY + fY;
              SLight[0][uiSLightOffset / 0x30].currentPos.fY = (float)dLightPosY;
            }
            fLightDeltaZ = SLight[0][uiSLightOffset / 0x30].targetPos.fZ - SLight[0][uiSLightOffset / 0x30].currentPos.fZ;
            //if ((LODWORD(fLightDeltaZ) & 0x7FFFFFFF) != 0) {
            if (fabs(fLightDeltaZ) > FLT_EPSILON) {
              fZ = SLight[0][uiSLightOffset / 0x30].speed.fZ;
              dLightDistanceZ = fabs(fLightDeltaZ);
              if (fZ > dLightDistanceZ)
                fZ = (float)dLightDistanceZ;
              if (fLightDeltaZ <= 0.0)
                dLightPosZ = SLight[0][uiSLightOffset / 0x30].currentPos.fZ - fZ;
              else
                dLightPosZ = SLight[0][uiSLightOffset / 0x30].currentPos.fZ + fZ;
              SLight[0][uiSLightOffset / 0x30].currentPos.fZ = (float)dLightPosZ;
              if (countdown < 126) {
                dLightSpeedZ = SLight[0][uiSLightOffset / 0x30].speed.fZ + -2.5;
                SLight[0][uiSLightOffset / 0x30].speed.fZ = (float)dLightSpeedZ;
                if (dLightSpeedZ < 20.0)
                  SLight[0][uiSLightOffset / 0x30].speed.fZ = 20.0;
              }
            }
            uiSLightOffset += 48;
          } while (uiSLightOffset != 0x90 * i + 144);
        }
      }
      if (fudge_wait < 0)                     // Test collisions and check lap line crossings
        testcollisions();
      iCrossLineIdx = 0;
      if (numcars > 0) {
        pCarPtr = Car;
        do {
          if (fudge_wait < 0)
            check_crossed_line(pCarPtr);
          ++iCrossLineIdx;
          ++pCarPtr;
        } while (iCrossLineIdx < numcars);
      }
    }
    if (replaytype == 1 && fudge_wait < 0)
      DoReplayData();
    if (replaytype != 2 || newreplayframe)    // Calculate car motion effects based on health and speed
    {
      iMotionCarIdx = 0;
      if (numcars > 0) {
        pCurrentCar = Car;
        do {
          fHealthFactor = (pCurrentCar->fHealth + 34.0f) * 0.01f;// Apply random motion effects: more damage = more erratic movement
          pMotionCar = pCurrentCar;
          if (fHealthFactor > 1.0)
            fHealthFactor = 1.0;
          dDamageFactor = 8.0 - 7.0 * fHealthFactor;
          fDamageMultiplier = (float)dDamageFactor;
          fHealthValue = (float)dDamageFactor * pCurrentCar->fFinalSpeed;
          iRandomValue = ROLLERrand() - 0x4000;
          dRollMotionCalc = (double)iRandomValue * fHealthValue / (double)CarEngines.engines[pCurrentCar->byCarDesignIdx].iStabilityFactor;
          //_CHP();
          pCurrentCar->iRollMotion = (int)dRollMotionCalc;
          fHealthValue = fDamageMultiplier * pCurrentCar->fFinalSpeed;
          iRandomValue = ROLLERrand() - 0x4000;
          dPitchMotionCalc = (double)iRandomValue * fHealthValue / (double)CarEngines.engines[pCurrentCar->byCarDesignIdx].iStabilityFactor;
          //_CHP();

          pCurrentCar->iPitchMotion = (int)dPitchMotionCalc;
          fHealthValue = fDamageMultiplier * pCurrentCar->fFinalSpeed;
          iRandomValue = ROLLERrand() - 0x4000;
          dYawMotionCalc = (double)iRandomValue * fHealthValue / (double)CarEngines.engines[pCurrentCar->byCarDesignIdx].iStabilityFactor;
          //*(float *)&iRandomValue = fDamageMultiplier * pCurrentCar->fFinalSpeed;
          //LODWORD(fHealthValue) = rand() - 0x4000;
          //dYawMotionCalc = (double)SLODWORD(fHealthValue) * *(float *)&iRandomValue / (double)CarEngines.engines[pCurrentCar->byCarDesignIdx].iStabilityFactor;
          //_CHP();
          ++pCurrentCar;
          ++iMotionCarIdx;
          iNumCarsTemp = numcars;
          pMotionCar->iYawMotion = (int)dYawMotionCalc;
        } while (iMotionCarIdx < iNumCarsTemp);
      }
    }
    if (replaytype == 2 && replayspeed == 256)// Handle replay sound effects synchronization
    {
      for (j = 0; j < numcars; ++j) {
        bySampleValue = newrepsample[j];
        if (bySampleValue) {
          if (repsample[j] < 0 && bySampleValue > 0)
            sfxpend(bySampleValue - 1, j, (uint8)repvolume[j] << 8);
          if (repsample[j] > 0 && newrepsample[j] < 0)
            sfxpend(-newrepsample[j] - 1, j, (uint8)repvolume[j] << 8);
          repsample[j] = newrepsample[j];
        }
      }
    }
    analysespeechsamples();                     // Finalize frame: analyze speech, order cars, handle audio
    ordercars();
    if (replaytype != 2) {
      if (paused) {
        stopallsamples();
        delaywrite = delayread + 6;
      } else if (player_type == 2) {
        enginesounds2(ViewType[0], ViewType[1]);
      } else {
        enginesounds(ViewType[0]);
      }
    }
    if (!intro && human_finishers >= players && (disable_messages || network_on))// Check race completion conditions and network state
    {
      iNetworkArraySize = 4 * numcars;
      iCarOffset = network_on;
      if (network_on) {
        if (wConsoleNode == master) {
          send_finished = -1;                   // Network master: check if any human players still racing
          if (numcars > 0) {
            iMasterCarIdx = 0;
            iMasterByteIdx = 0;
            do {
              if ((Car[iMasterCarIdx].byLives & 0x80u) == 0 && Car[iMasterCarIdx].fFinalSpeed > 0.0 && human_control[iMasterByteIdx / 4u])
                send_finished = 0;
              iMasterByteIdx += 4;
              ++iMasterCarIdx;
            } while (iMasterByteIdx < iNetworkArraySize);
          }
        } else {
          iSlaveByteIdx = 0;                    // Network slave: determine local racing state
          racing = 0;
          if (numcars > 0) {
            iSlaveCarIdx = 0;
            do {
              if ((Car[iSlaveCarIdx].byLives & 0x80u) == 0 && Car[iSlaveCarIdx].fFinalSpeed > 0.0 && human_control[iSlaveByteIdx / 4u])
                racing = -1;
              iSlaveByteIdx += 4;
              ++iSlaveCarIdx;
            } while (iSlaveByteIdx < iNetworkArraySize);
          }
        }
      } else if (lastsample < -72)              // Single player: check if any human players still racing
      {
        racing = network_on;
        if (numcars > 0) {
          iRacingByteIdx = 0;
          do {
            if (*((char *)&Car[0].byLives + iCarOffset) >= 0 && *(float *)((char *)&Car[0].fFinalSpeed + iCarOffset) > 0.0 && human_control[iRacingByteIdx / 4u])
              racing = -1;
            iRacingByteIdx += 4;
            iCarOffset += sizeof(tCar);
          } while (iRacingByteIdx < iNetworkArraySize);
        }
      }
    }
    if (++nearcarcheck == 4)                  // Update near car checking counter and advance frame pointer
      nearcarcheck = 0;
    if (replaytype == 2)
      readptr = writeptr;
    else
      readptr = ((int16)readptr + 1) & 0x1FF;
  }
  if (replaytype != 2 && numcars > 0)         // Final processing: handle special car state changes for active cars
  {
    iFinalArraySize = 4 * numcars;
    iFinalCarIdx = 0;
    iFinalByteIdx = 0;
    while (1) {
      if (Car[iFinalCarIdx].fFinalSpeed <= 36.0 || Car[iFinalCarIdx].fFinalSpeed >= 100.0)
        goto LABEL_185;                         // Toggle special car state (byUnk68) based on speed and current state
      byUnk68 = Car[iFinalCarIdx].byWheelAnimationFrame;
      if (byUnk68 > 1)
        break;
      if (byUnk68 == aiCarStates[iFinalByteIdx / 4u + 1]) {
        byNewValue = Car[iFinalCarIdx].byWheelAnimationFrame ^ 1;
        goto LABEL_184;
      }
    LABEL_185:
      iFinalByteIdx += 4;
      ++iFinalCarIdx;
      if (iFinalByteIdx >= iFinalArraySize)
        return;
    }
    byNewValue = 0;
  LABEL_184:
    Car[iFinalCarIdx].byWheelAnimationFrame = byNewValue;
    goto LABEL_185;
  }
}

//-------------------------------------------------------------------------------------------------
//0002AEC0
double calc_revs(tRevCurve *pRevs, int iGear, float fChg)
{
  tRevCurve *pCurrentGearCurve; // eax
  double dDelta0;
  double dDelta1; // [esp+18h] [ebp-30h]
  double dDelta2; // st7
  double dDelta3; // st3
  double dLowInterp1;
  double dLowInterp2;
  double dHighInterp1;
  double dHighInterp2;
  double dHighInterp3;
  float fPower0;
  float fPower1; // [esp+40h] [ebp-8h]
  float fPower2; // [esp+34h] [ebp-14h]
  float fPower3; // [esp+38h] [ebp-10h]
  float fRPM0;
  float fRPM1; // [esp+2Ch] [ebp-1Ch]
  float fRPM2;
  float fRPM3;
  float fCalculatedResult; // [esp+3Ch] [ebp-Ch]

  pCurrentGearCurve = &pRevs[iGear];// Get pointer to power/RPM curve for specified gear
  fPower0 = pCurrentGearCurve->points[0].fPower;// Load power and RPM values
  fPower1 = pCurrentGearCurve->points[1].fPower;
  fPower2 = pCurrentGearCurve->points[2].fPower;
  fPower3 = pCurrentGearCurve->points[3].fPower;
  fRPM0 = pCurrentGearCurve->points[0].fRPM;
  fRPM1 = pCurrentGearCurve->points[1].fRPM;
  fRPM2 = pCurrentGearCurve->points[2].fRPM;
  fRPM3 = pCurrentGearCurve->points[3].fRPM;
  dDelta0 = fChg - fPower0;// Calculate delta values for quadratic interpolation
  dDelta1 = fChg - fPower1;
  dDelta2 = fChg - fPower2;
  dDelta3 = fChg - fPower3;
  if (fChg < 0.0 || fChg >= (double)fPower1) {
    if (fChg >= (double)fPower3) {
      fCalculatedResult = 1.0;// High RPM range: return maximum value
    } else {
      dHighInterp1 = (fPower1 - fPower2) * (fPower1 - fPower3);// Mid RPM range: quadratic interpolation between points 1-3
      dHighInterp2 = (fPower2 - fPower1) * (fPower2 - fPower3);
      dHighInterp3 = (fPower3 - fPower1) * (fPower3 - fPower2);
      fCalculatedResult = (float)(dDelta2 * dDelta3 * fRPM1 / dHighInterp1
        + dDelta1 * dDelta3 * fRPM2 / dHighInterp2
        + dDelta1 * dDelta2 * fRPM3 / dHighInterp3);// Complete quadratic interpolation using all three points
    }
  } else {
    dLowInterp1 = (fPower0 - fPower1) * fPower0;// Low RPM range: quadratic interpolation between points 0-1
    dLowInterp2 = (fPower1 - fPower0) * fPower1;
    fCalculatedResult = (float)(dDelta1 * fChg * fRPM0 / dLowInterp1
      + dDelta0 * fChg * fRPM1 / dLowInterp2);// Calculate interpolated value using quadratic formula for low range
  }

  // clamp result to between 0.0 and 1.0
  if (fCalculatedResult < 0.0)                // Clamp result to valid range [0.0, 1.0]
    fCalculatedResult = 0.0;
  if (fCalculatedResult > 1.0)
    return 1.0;
  return fCalculatedResult;
}

//-------------------------------------------------------------------------------------------------
//0002B030
double calc_pow(int iCarDesignIdx, int iCurrentGear, float fRPMRatio)
{
  tRevCurve *pfRevData; // eax
  double dLowInterp1; // st5
  double dLowInterp2; // st4
  double dLowCoeffA; // st6
  double dLowCoeffB; // st7
  double dHighInterp1; // st5
  double dHighInterp2; // st3
  double dHighInterp3; // [esp+20h] [ebp-2Ch]
  double dHighCoeffA; // [esp+0h] [ebp-4Ch]
  double dHighCoeffB; // rtt
  float fPower0; // [esp+30h] [ebp-1Ch]
  float fPower1; // [esp+44h] [ebp-8h]
  float fPower2; // [esp+3Ch] [ebp-10h]
  float fPower3; // [esp+40h] [ebp-Ch]
  float fRPM0; // [esp+24h] [ebp-28h]
  float fRPM1; // [esp+34h] [ebp-18h]
  float fRPM2; // [esp+28h] [ebp-24h]
  float fRPM3; // [esp+2Ch] [ebp-20h]
  float fCalculatedPower; // [esp+38h] [ebp-14h]

  pfRevData = CarEngines.engines[iCarDesignIdx].pRevs;// Get pointer to power/RPM curve data for this car and gear
  fPower0 = pfRevData[iCurrentGear].points[0].fPower;// Load 4 power values and 4 RPM points for power curve interpolation
  fPower1 = pfRevData[iCurrentGear].points[1].fPower;
  fPower2 = pfRevData[iCurrentGear].points[2].fPower;
  fPower3 = pfRevData[iCurrentGear].points[3].fPower;
  fRPM0 = pfRevData[iCurrentGear].points[0].fRPM;
  fRPM1 = pfRevData[iCurrentGear].points[1].fRPM;
  fRPM2 = pfRevData[iCurrentGear].points[2].fRPM;
  fRPM3 = pfRevData[iCurrentGear].points[3].fRPM;
  if (fRPMRatio < 0.0 || fRPMRatio >= (double)fRPM1) {
    if (fRPMRatio >= (double)fRPM3) {
      fCalculatedPower = fPower3;// High RPM range: use maximum power value
    } else {
      dHighInterp1 = (fPower1 - fPower2) * (fPower1 - fPower3);// Mid RPM range: quadratic interpolation between points 1-3
      dHighInterp2 = (fPower2 - fPower1) * (fPower2 - fPower3);
      dHighInterp3 = (fPower3 - fPower1) * (fPower3 - fPower2);
      dHighCoeffA = fRPM1 / dHighInterp1 + fRPM2 / dHighInterp2 + fRPM3 / dHighInterp3;
      dHighCoeffB = -(fPower2 + fPower3) * fRPM1 / dHighInterp1
        - (fPower1 + fPower3) * fRPM2 / dHighInterp2
        - (fPower1 + fPower2) * fRPM3 / dHighInterp3;
      fCalculatedPower = (float)((sqrt(
        dHighCoeffB * dHighCoeffB
        - (fPower2 * fPower3 * fRPM1 / dHighInterp1
           + fPower1 * fPower3 * fRPM2 / dHighInterp2
           + fPower1 * fPower2 * fRPM3 / dHighInterp3
           - fRPMRatio)
        * dHighCoeffA * 4.0) - dHighCoeffB)
        / (dHighCoeffA * 2.0));// Solve complex quadratic equation for mid-range power
    }
  } else {
    dLowInterp1 = (fPower0 - fPower1) * fPower0;// Low RPM range: quadratic interpolation between points 0-1
    dLowInterp2 = (fPower1 - fPower0) * fPower1;
    dLowCoeffA = fRPM0 / dLowInterp1 + fRPM1 / dLowInterp2;
    dLowCoeffB = -fPower1 * fRPM0 / dLowInterp1 - fPower0 * fRPM1 / dLowInterp2;
    fCalculatedPower = (float)((sqrt(dLowCoeffB * dLowCoeffB -
                                     -fRPMRatio * dLowCoeffA * 4.0) - dLowCoeffB) / (dLowCoeffA * 2.0));// Solve quadratic equation using quadratic formula
  }
  if (fCalculatedPower < 0.0)                 // Clamp power output to valid range [0, 1024]
    fCalculatedPower = 0.0;
  if (fCalculatedPower > 1024.0)
    return 1024.0;
  return fCalculatedPower;
}

//-------------------------------------------------------------------------------------------------
//0002B260
void Accelerate(tCar *pCar)
{                                               // Set car complexity factor: 2.0 for advanced cars, 1.0 for simple cars
  int iCarDesignIdx; // ebp
  tCarEngine *pEngineData; // edi
  int nCurrChunk; // eax
  int iNextChunk; // eax
  tData *pTrackData; // edx
  tTrackInfo *pTrackInfo; // ebx
  int iDriverIdx; // ebx
  double dDownshiftPower; // st7
  double dUpshiftPower; // st7
  double dCalculatedRPMRatio; // st7
  int iPitchDelta; // ebp
  int iMaxPitchOffset_1; // eax
  int iPitchDynamicOffset; // eax
  int iMaxPitchOffset; // edx
  int iOldPitchOffset; // eax
  double dPitchDecay; // st6
  int iPitchDeltaNormal; // ecx
  int iMaxPitchNormal; // ebp
  double dMaxRPMChange; // [esp+8h] [ebp-78h]
  float fUpshiftSpeed; // [esp+28h] [ebp-58h]
  float fNegHealthFactor; // [esp+2Ch] [ebp-54h]
  float fTrackHalfWidth; // [esp+30h] [ebp-50h]
  float fDownshiftThreshold; // [esp+34h] [ebp-4Ch]
  float fAdjustedGravity; // [esp+38h] [ebp-48h]
  float fPitchDecayAmount; // [esp+3Ch] [ebp-44h]
  int iGearChanged; // [esp+40h] [ebp-40h]
  float *pSpds; // [esp+44h] [ebp-3Ch]
  float fCarComplexityFactor; // [esp+48h] [ebp-38h]
  float fGravityFactor; // [esp+4Ch] [ebp-34h]
  float fNewRPMRatio; // [esp+50h] [ebp-30h]
  float fCalculatedBaseSpeed; // [esp+50h] [ebp-30h]
  float fRPMRatioChange; // [esp+54h] [ebp-2Ch]
  float fHealthFactor; // [esp+58h] [ebp-28h]
  float fPower; // [esp+5Ch] [ebp-24h]
  int iGearAyMax; // [esp+60h] [ebp-20h]
  int iChunkIdx; // [esp+64h] [ebp-1Ch]

  // TEX_OFF_ADVANCED_CARS
  if ((textures_off & TEX_OFF_ADVANCED_CARS) != 0)
    fCarComplexityFactor = 2.0;
  else
    fCarComplexityFactor = 1.0;
  pCar->byAccelerating = -1;                    // Initialize acceleration state and gear change flag
  iGearChanged = 0;
  if (pCar->byThrottlePressed && !pCar->byEngineStartTimer)// Check if car can accelerate (throttle pressed and not crashed)
  {
    fHealthFactor = (pCar->fHealth + 34.0f) * 0.01f;// Calculate health factor (0-1) based on car damage, capped at 1.0
    iCarDesignIdx = pCar->byCarDesignIdx;
    pCar->iEngineState = 10;
    pEngineData = &CarEngines.engines[iCarDesignIdx];
    if (fHealthFactor > 1.0)
      fHealthFactor = 1.0;
    fPower = pCar->fPower;
    iGearAyMax = (char)pCar->byGearAyMax;
    if (pCar->fRPMRatio >= 1.0)               // Check for redline condition (RPM ratio >= 1.0)
    {
      pCar->fWheelSpinFactor = 1.0;             // Redline: limit acceleration and apply engine damage for over-revving
      if (pEngineData->iNumGears - 1 > iGearAyMax && iGearAyMax >= 0 || iGearAyMax == -2 && pCar->fFinalSpeed > 0.0)
        dodamage(pCar, 0.02f);
    } else {
      nCurrChunk = pCar->nCurrChunk;
      if (nCurrChunk == -1)                   // Off-track: apply high acceleration bonus and reduced wheel spin
      {
        pCar->fWheelSpinFactor = 20.0f;
        fPower = fPower + 32.0f;
      } else {
        iNextChunk = nCurrChunk + 1;            // On-track: calculate track physics effects (banking, gravity)
        if (iNextChunk == TRAK_LEN)
          iNextChunk ^= TRAK_LEN;
        iChunkIdx = pCar->nCurrChunk;
        pTrackData = &localdata[iChunkIdx];
        fTrackHalfWidth = ((pCar->pos.fX + pTrackData->fTrackHalfLength) * localdata[iNextChunk].fTrackHalfWidth
                         - (pCar->pos.fX - pTrackData->fTrackHalfLength) * localdata[iChunkIdx].fTrackHalfWidth)
          / (pTrackData->fTrackHalfLength
           * 2.0f);                // Calculate interpolated track half-width at car position
        pTrackInfo = &TrackInfo[iChunkIdx];
        if (pCar->pos.fY <= (double)fTrackHalfWidth)// Select banking angle based on car position (left/right/center of track)
        {
          if (-fTrackHalfWidth <= pCar->pos.fY)
            fAdjustedGravity = pTrackData->gravity.fY;
          else
            fAdjustedGravity = pTrackData->gravity.fY * tcos[pTrackInfo->iRightBankAngle & 0x3FFF] - pTrackData->gravity.fZ * tsin[pTrackInfo->iRightBankAngle & 0x3FFF];
        } else {
          fAdjustedGravity = pTrackData->gravity.fY * tcos[pTrackInfo->iLeftBankAngle & 0x3FFF] + pTrackData->gravity.fZ * tsin[pTrackInfo->iLeftBankAngle & 0x3FFF];
        }
        fGravityFactor = (pTrackData->gravity.fX * tcos[pCar->nYaw] + fAdjustedGravity * tsin[pCar->nYaw]) * 2.0f;// Calculate gravity factor based on track inclination and car orientation
        fNegHealthFactor = -fHealthFactor;
        if (fGravityFactor < (double)fNegHealthFactor)
          fGravityFactor = -fHealthFactor;
        iDriverIdx = pCar->iDriverIdx;
        if (player1_car == iDriverIdx || player2_car == iDriverIdx)// Apply different wheel spin factors for human vs AI players
          pCar->fWheelSpinFactor = (fGravityFactor + 1.0f) * 6.0f;
        else
          pCar->fWheelSpinFactor = 1.0;
        if (cheat_control && iCarDesignIdx == 9)// Special acceleration bonuses: cheat mode, neutral gear, or race position
        {
          fPower = fPower + 32.0f;
        } else if (iGearAyMax == -1) {
          fPower = fCarComplexityFactor * 2.0f + fPower;
        } else if (pCar->fTotalRaceTime >= 20.0f || human_control[pCar->iDriverIdx] || g_bAINoCheatStart) {  // Last check added by ROLLER
          fPower = (fHealthFactor + fCarComplexityFactor + fGravityFactor) * 0.5f + fPower;
        } else {
          fPower = (fHealthFactor + fCarComplexityFactor + fGravityFactor) / (float)(pCar->byRacePosition / 15 + 1) + fPower;
        }
      }
    }
    if (fPower >= 1024.0f)                     // Clamp power output to maximum value (1023)
      fPower = 1023.0f;
    pSpds = pEngineData->pSpds;
    if (iGearAyMax <= 0)
      fDownshiftThreshold = 0.0f;
    else
      fDownshiftThreshold = pSpds[iGearAyMax - 1] * 0.5f;
    if (fabs(pCar->fFinalSpeed / fHealthFactor) < fDownshiftThreshold && human_control[pCar->iDriverIdx] != 2 && iGearAyMax > 0)// Auto downshift: check if speed is too low for current gear
    {
      dDownshiftPower = change_gear(iGearAyMax, iGearAyMax - 1, pCar, iCarDesignIdx);
      --iGearAyMax;
      iGearChanged = -1;
      fPower = (float)dDownshiftPower;
      pCar->byGearAyMax = iGearAyMax;
    }
    if (pEngineData->iNumGears - 1 > iGearAyMax
      && iGearAyMax >= 0
      && human_control[pCar->iDriverIdx] != 2
      && fPower > (double)pEngineData->pChgs[2 * iGearAyMax] * 1.2
      && pCar->fRPMRatio >= (double)eng_chg_revs[12 * iCarDesignIdx + 2 * iGearAyMax])// Auto upshift: check power, RPM, and speed thresholds
    {
      fUpshiftSpeed = pCar->fRPMRatio * pSpds[iGearAyMax] * fHealthFactor * 0.5f;
      if (pCar->fFinalSpeed >= (double)fUpshiftSpeed) {
        dUpshiftPower = change_gear(iGearAyMax, iGearAyMax + 1, pCar, iCarDesignIdx);
        pCar->byGearAyMax = ++iGearAyMax;
        fPower = (float)dUpshiftPower;
        iGearChanged = -1;
      }
    }
    pCar->fPower = fPower;                      // Calculate new RPM ratio using calc_revs function
    if (iGearAyMax < 0)
      dCalculatedRPMRatio = calc_revs(pEngineData->pRevs, 0, fPower);
    else
      dCalculatedRPMRatio = calc_revs(pEngineData->pRevs, iGearAyMax, fPower);
    fNewRPMRatio = (float)dCalculatedRPMRatio;
    fRPMRatioChange = fNewRPMRatio - pCar->fRPMRatio;
    if (pCar->nCurrChunk != -1 && iCarDesignIdx != 9 && iGearAyMax != -1)// Limit RPM ratio changes to realistic values when on track
    {
      if (fRPMRatioChange <= 0.0f) {
        if (fRPMRatioChange < -0.03f)
          fRPMRatioChange = -0.029999999f;
      } else if (fGravityFactor <= 0.0) {
        if (fRPMRatioChange > fCarComplexityFactor * 0.03f)
          fRPMRatioChange = 0.029999999f;
      } else {
        dMaxRPMChange = fCarComplexityFactor * 0.03f + fGravityFactor * 0.02f;
        if (fRPMRatioChange > dMaxRPMChange)
          fRPMRatioChange = (float)dMaxRPMChange;
      }
    }
    pCar->fRPMRatio = pCar->fRPMRatio + fRPMRatioChange;
    if (iGearAyMax == -1)                     // Calculate base speed for different gear types
    {
      pCar->fBaseSpeed = 0.0;
    } else if (iGearAyMax < 0) {
      pCar->fBaseSpeed = -(*pSpds * fNewRPMRatio * fHealthFactor);
    } else {
      fCalculatedBaseSpeed = pSpds[iGearAyMax] * fNewRPMRatio * fHealthFactor;
      pCar->fBaseSpeed = fCalculatedBaseSpeed;
    }
    if (iGearAyMax < 0 && !race_started)      // Reset speed overflow in reverse before race starts
      pCar->fSpeedOverflow = 0.0;
    if (iGearChanged)                         // Update speed overflow when gear changes occur
      pCar->fSpeedOverflow = pCar->fFinalSpeed - pCar->fBaseSpeed;
    if ((char)pCar->byGearAyMax != -1)        // Update car pitch dynamics based on acceleration
    {
      if (iCarDesignIdx == 9) {                                         // Special pitch handling for cheat mode (car design 9)
        if (cheat_control && pCar->fRPMRatio < 1.0) {
          iPitchDelta = 32 * pEngineData->iPitchAccelRate + pCar->iPitchDynamicOffset;
          pCar->iPitchDynamicOffset = iPitchDelta;
          iMaxPitchOffset_1 = pEngineData->iMaxPitchOffset;
          if (12 * iMaxPitchOffset_1 < iPitchDelta)
            pCar->iPitchDynamicOffset = 12 * iMaxPitchOffset_1;
        } else {
          iPitchDynamicOffset = pCar->iPitchDynamicOffset;
          iMaxPitchOffset = pEngineData->iMaxPitchOffset;
          if (iPitchDynamicOffset > iMaxPitchOffset) {
            fPitchDecayAmount = (float)(iPitchDynamicOffset - iMaxPitchOffset);
            if ((double)(48 * pEngineData->iPitchAccelRate) < fPitchDecayAmount)// Handle extreme pitch: reset camera offsets and apply damage
            {
              pCar->iCameraOscillationPhase = 0;
              pCar->iRollDampingMomentum = 0;
              pCar->iRollCameraOffset = 0;
              iOldPitchOffset = pCar->iPitchDynamicOffset;
              pCar->iPitchDynamicOffset = 0;
              pCar->iPitchCameraOffset = iOldPitchOffset;
              fPitchDecayAmount = 0.0;
              pCar->iPitchBackup = iOldPitchOffset;
            }
            dPitchDecay = (double)pCar->iPitchDynamicOffset - fPitchDecayAmount;
            //_CHP();
            pCar->iPitchDynamicOffset = (int)dPitchDecay;
          } else {
            pCar->iPitchDynamicOffset += pEngineData->iPitchAccelRate;
          }
        }
      } else {
        iPitchDeltaNormal = pEngineData->iPitchAccelRate + pCar->iPitchDynamicOffset;// Normal cars: increase pitch with acceleration, clamp to max
        pCar->iPitchDynamicOffset = iPitchDeltaNormal;
        iMaxPitchNormal = pEngineData->iMaxPitchOffset;
        if (iPitchDeltaNormal > iMaxPitchNormal)
          pCar->iPitchDynamicOffset = iMaxPitchNormal;
      }
    }
  }
}

//-------------------------------------------------------------------------------------------------
//0002B990
void Decelerate(tCar *pCar)
{
  int iCarDesignIdx; // edi
  tCarEngine *pEngineData; // esi
  int byGearAyMax; // ecx
  int iCurrChunk; // eax
  double dInitialDecelerationCalc; // st7
  int iNextChunk; // eax
  tData *pTrackData; // edx
  tTrackInfo *pTrackInfo; // ebp
  double dGravityFactor; // st7
  double dCalculatedPower; // st7
  double dRPMRatioChange; // st7
  int iPitchDelta; // ebp
  int iMinPitchOffset; // edx
  int iPitchDeltaNeg; // esi
  int iPitchDeltaPos; // edx
  float fTrackHalfWidth; // [esp+18h] [ebp-44h]
  float fAdjustedGravity; // [esp+1Ch] [ebp-40h]
  int iNextChunkIdx; // [esp+20h] [ebp-3Ch]
  float fRPMRatioChange; // [esp+24h] [ebp-38h]
  float fDecelerationRate; // [esp+28h] [ebp-34h]
  float fCurrentPower; // [esp+2Ch] [ebp-30h]
  float *pSpds; // [esp+30h] [ebp-2Ch]
  float fGravityMultiplier; // [esp+34h] [ebp-28h]
  float fHealthFactor; // [esp+38h] [ebp-24h]
  float fRPMRatio; // [esp+3Ch] [ebp-20h]
  float fBaseSpeed; // [esp+40h] [ebp-1Ch]
  float fNewBaseSpeed; // [esp+40h] [ebp-1Ch]
  float fNewSpeedOverflow; // [esp+40h] [ebp-1Ch]

  pCar->byAccelerating = 0;                            // Initialize car status flags for deceleration
  pCar->iEngineState = 6;
  fHealthFactor = (pCar->fHealth + 34.0f) * 0.01f;// Calculate health factor (0-1) based on car damage, capped at 1.0
  iCarDesignIdx = pCar->byCarDesignIdx;
  pEngineData = &CarEngines.engines[iCarDesignIdx];
  fBaseSpeed = pCar->fBaseSpeed;
  if (fHealthFactor > 1.0)
    fHealthFactor = 1.0;
  byGearAyMax = (char)pCar->byGearAyMax;
  pSpds = pEngineData->pSpds;
  if (byGearAyMax < 0)                        // Handle reverse gear: calculate base speed using RPM ratio
    fBaseSpeed = pCar->fRPMRatio * *pEngineData->pSpds * fHealthFactor;
  iCurrChunk = pCar->nCurrChunk;
  fDecelerationRate = pSpds[pEngineData->iNumGears - 1] / (pEngineData->fDragCoefficient * 36.0f);// Calculate base deceleration rate from top gear max speed
  if (iCurrChunk == -1)                       // Off-track: apply 3x deceleration penalty
  {
    dInitialDecelerationCalc = fBaseSpeed - fDecelerationRate * 3.0f;
  } else if (human_control[pCar->iDriverIdx])   // Human players: apply track banking and gravity effects
  {
    iNextChunk = iCurrChunk + 1;
    iNextChunkIdx = iNextChunk;
    if (iNextChunk == TRAK_LEN)
      iNextChunkIdx = TRAK_LEN ^ iNextChunk;
    pTrackData = &localdata[pCar->nCurrChunk];
    fTrackHalfWidth = ((pCar->pos.fX + pTrackData->fTrackHalfLength) * localdata[iNextChunkIdx].fTrackHalfWidth
                     - (pCar->pos.fX - pTrackData->fTrackHalfLength) * localdata[pCar->nCurrChunk].fTrackHalfWidth)
      / (pTrackData->fTrackHalfLength
       * 2.0f);                    // Calculate interpolated track half-width at car position
    pTrackInfo = &TrackInfo[pCar->nCurrChunk];
    if (pCar->pos.fY <= (double)fTrackHalfWidth)// Select banking angle based on car position (left/right/center of track)
    {
      if (-fTrackHalfWidth <= pCar->pos.fY)
        fAdjustedGravity = pTrackData->gravity.fY;
      else
        fAdjustedGravity = pTrackData->gravity.fY * tcos[pTrackInfo->iRightBankAngle & 0x3FFF] - pTrackData->gravity.fZ * tsin[pTrackInfo->iRightBankAngle & 0x3FFF];
    } else {
      fAdjustedGravity = pTrackData->gravity.fY * tcos[pTrackInfo->iLeftBankAngle & 0x3FFF] + pTrackData->gravity.fZ * tsin[pTrackInfo->iLeftBankAngle & 0x3FFF];
    }
    dGravityFactor = (pTrackData->gravity.fX * tcos[pCar->nYaw] + fAdjustedGravity * tsin[pCar->nYaw]) * -0.3333333333333333 + 1.0;// Calculate gravity factor based on track inclination and car orientation
    fGravityMultiplier = (float)dGravityFactor;
    if (dGravityFactor < 0.5)
      fGravityMultiplier = 0.5f;
    dInitialDecelerationCalc = fBaseSpeed - fDecelerationRate * fGravityMultiplier;
  } else {
    dInitialDecelerationCalc = fBaseSpeed - fDecelerationRate;// AI players: apply standard deceleration rate
  }
  fNewBaseSpeed = (float)dInitialDecelerationCalc;
  if (fNewBaseSpeed < 0.0)
    fNewBaseSpeed = 0.0;
  if (byGearAyMax >= 0)                       // Calculate RPM ratio and power for current gear
  {
    fRPMRatio = fNewBaseSpeed / (pSpds[byGearAyMax] * fHealthFactor);
    if (fRPMRatio > 1.0)
      fRPMRatio = 1.0f;
    dCalculatedPower = calc_pow(iCarDesignIdx, byGearAyMax, fRPMRatio);
  } else {
    fRPMRatio = fNewBaseSpeed / (*pSpds * fHealthFactor);
    if (fRPMRatio > 1.0)
      fRPMRatio = 1.0f;
    dCalculatedPower = calc_pow(iCarDesignIdx, 0, fRPMRatio);
  }
  fCurrentPower = (float)dCalculatedPower;
  if (byGearAyMax > 0 && human_control[pCar->iDriverIdx] != 2 && (double)pEngineData->pChgs[2 * byGearAyMax - 1] > fCurrentPower)// Auto downshift: check if power is below gear change threshold
  {
    fRPMRatio = fNewBaseSpeed / (pSpds[--byGearAyMax] * fHealthFactor);
    if (fRPMRatio > 1.0)
      fRPMRatio = 1.0f;
    fCurrentPower = (float)calc_pow(iCarDesignIdx, byGearAyMax, fRPMRatio);
  }
  dRPMRatioChange = fRPMRatio - pCar->fRPMRatio;
  pCar->byGearAyMax = byGearAyMax;
  pCar->fPower = fCurrentPower;
  fRPMRatioChange = (float)dRPMRatioChange;
  if (pCar->nCurrChunk != -1 && byGearAyMax != -1) {                                             // Limit RPM ratio changes to realistic values when on track
    if (fRPMRatioChange <= 0.03) {
      if (fRPMRatioChange < -0.1)
        fRPMRatioChange = -0.1f;
    } else {
      fRPMRatioChange = 0.029999999f;
    }
  }
  pCar->fRPMRatio = pCar->fRPMRatio + fRPMRatioChange;
  if (byGearAyMax == -1)                      // Handle special cases: neutral gear (0) and reverse gear (negate speed)
  {
    fNewBaseSpeed = 0.0;
  } else if (byGearAyMax == -2) {
    fNewBaseSpeed = -fNewBaseSpeed;
    //HIBYTE(fNewBaseSpeed) ^= 0x80u;
  }
  pCar->fBaseSpeed = fNewBaseSpeed;
  fNewSpeedOverflow = pCar->fSpeedOverflow - fDecelerationRate;// Update speed overflow, reducing it by deceleration rate
  if (fNewSpeedOverflow < 0.0)
    fNewSpeedOverflow = 0.0;
  pCar->fSpeedOverflow = fNewSpeedOverflow;
  if (byGearAyMax < 0)
    pCar->fSpeedOverflow = 0.0;
  if (pCar->fFinalSpeed <= 0.0)               // Update car pitch dynamics based on movement direction
  {
    if (pCar->iPitchDynamicOffset >= 0) {
      iPitchDeltaPos = pCar->iPitchDynamicOffset - pEngineData->iNoseUpRecoveryRate;
      pCar->iPitchDynamicOffset = iPitchDeltaPos;
      if (iPitchDeltaPos >= 0)
        return;
    } else {
      iPitchDeltaNeg = pEngineData->iNoseDownRecoveryRate + pCar->iPitchDynamicOffset;
      pCar->iPitchDynamicOffset = iPitchDeltaNeg;
      if (iPitchDeltaNeg <= 0)
        return;
    }
    pCar->iPitchDynamicOffset = 0;
  } else {
    iPitchDelta = pCar->iPitchDynamicOffset - pEngineData->iPitchDecayRate;
    pCar->iPitchDynamicOffset = iPitchDelta;
    iMinPitchOffset = pEngineData->iMinPitchOffset;
    if (iPitchDelta < iMinPitchOffset)
      pCar->iPitchDynamicOffset = iMinPitchOffset;
  }
}

//-------------------------------------------------------------------------------------------------
//0002BDD0
void FreeWheel(tCar *pCar)
{
  int iCarDesignIdx; // esi
  int iGearAyMax; // ecx
  tCarEngine *pEngineData; // edi
  int iCurrChunk; // eax
  double dInitialDecelerationCalc; // st7
  int iNextChunk; // eax
  tData *pTrackData; // edx
  tTrackInfo *pTrackInfo; // ebp
  double dGravityFactor; // st7
  double dCalculatedPower; // st7
  double dRPMRatioChange; // st7
  int iPitchDynamicOffset; // esi
  int iPitchDeltaNeg; // edx
  int iNoseUpRecoveryRate; // eax
  float fAdjustedDecelerationRate; // [esp+18h] [ebp-48h]
  float fDecelerationRate; // [esp+1Ch] [ebp-44h]
  float fTrackHalfWidth; // [esp+20h] [ebp-40h]
  float fAdjustedGravity; // [esp+24h] [ebp-3Ch]
  int iNextChunkIdx; // [esp+28h] [ebp-38h]
  float fCurrentPower; // [esp+2Ch] [ebp-34h]
  float fRPMRatioChange; // [esp+30h] [ebp-30h]
  float *pSpds; // [esp+34h] [ebp-2Ch]
  float fGravityMultiplier; // [esp+38h] [ebp-28h]
  float fHealthFactor; // [esp+3Ch] [ebp-24h]
  float fRPMRatio; // [esp+40h] [ebp-20h]
  float fBaseSpeed; // [esp+44h] [ebp-1Ch]
  float fNewBaseSpeed; // [esp+44h] [ebp-1Ch]

  pCar->byAccelerating = 0;                     // Initialize freewheeling state and engine status
  pCar->iEngineState = 8;
  fHealthFactor = (pCar->fHealth + 34.0f) * 0.01f;// Calculate health factor (0-1) based on car damage, capped at 1.0
  iCarDesignIdx = pCar->byCarDesignIdx;
  iGearAyMax = (char)pCar->byGearAyMax;
  fBaseSpeed = pCar->fBaseSpeed;
  pEngineData = &CarEngines.engines[iCarDesignIdx];
  if (fHealthFactor > 1.0)
    fHealthFactor = 1.0;
  pSpds = pEngineData->pSpds;
  if (iGearAyMax < 0)                         // Handle reverse gear: calculate base speed using RPM ratio
    fBaseSpeed = pCar->fRPMRatio * *pEngineData->pSpds * fHealthFactor;
  iCurrChunk = pCar->nCurrChunk;
  fDecelerationRate = pSpds[pEngineData->iNumGears - 1] / (pEngineData->fDragCoefficient * 256.0f);// Calculate base deceleration rate from top gear max speed and drag coefficient
  if (iCurrChunk == -1)                       // Off-track: apply 16x deceleration penalty
  {
    dInitialDecelerationCalc = fBaseSpeed - fDecelerationRate * 16.0;
  } else {
    iNextChunk = iCurrChunk + 1;                // On-track: calculate track physics effects (banking, gravity)
    iNextChunkIdx = iNextChunk;
    if (iNextChunk == TRAK_LEN)
      iNextChunkIdx = TRAK_LEN ^ iNextChunk;
    pTrackData = &localdata[pCar->nCurrChunk];
    fTrackHalfWidth = ((pCar->pos.fX + pTrackData->fTrackHalfLength) * localdata[iNextChunkIdx].fTrackHalfWidth
                     - (pCar->pos.fX - pTrackData->fTrackHalfLength) * localdata[pCar->nCurrChunk].fTrackHalfWidth)
      / (pTrackData->fTrackHalfLength
       * 2.0f);                    // Calculate interpolated track half-width at car position
    pTrackInfo = &TrackInfo[pCar->nCurrChunk];
    if (pCar->pos.fY <= (double)fTrackHalfWidth)// Select banking angle based on car position (left/right/center of track)
    {
      if (-fTrackHalfWidth <= pCar->pos.fY)
        fAdjustedGravity = pTrackData->gravity.fY;// fY = adjusted gravity Y component based on track banking
      else
        fAdjustedGravity = pTrackData->gravity.fY * tcos[pTrackInfo->iRightBankAngle & 0x3FFF] - pTrackData->gravity.fZ * tsin[pTrackInfo->iRightBankAngle & 0x3FFF];
    } else {
      fAdjustedGravity = pTrackData->gravity.fY * tcos[pTrackInfo->iLeftBankAngle & 0x3FFF] + pTrackData->gravity.fZ * tsin[pTrackInfo->iLeftBankAngle & 0x3FFF];
    }
    dGravityFactor = (pTrackData->gravity.fX * tcos[pCar->nYaw] + fAdjustedGravity * tsin[pCar->nYaw]) * -0.3333333333333333f + 1.0f;// Calculate gravity factor based on track inclination and car orientation
    fGravityMultiplier = (float)dGravityFactor;
    if (dGravityFactor < 0.5)
      fGravityMultiplier = 0.5f;
    fAdjustedDecelerationRate = fDecelerationRate * fGravityMultiplier;
    if (iGearAyMax == -1)                     // Apply different deceleration rates: 6x for neutral gear, 1x for forward gears
      dInitialDecelerationCalc = fBaseSpeed - fAdjustedDecelerationRate * 6.0;
    else
      dInitialDecelerationCalc = fBaseSpeed - fAdjustedDecelerationRate;
  }
  fNewBaseSpeed = (float)dInitialDecelerationCalc;
  if (fNewBaseSpeed < 0.0)
    fNewBaseSpeed = 0.0;
  if (iGearAyMax >= 0)                        // Calculate RPM ratio and power for current gear
  {
    fRPMRatio = fNewBaseSpeed / (pSpds[iGearAyMax] * fHealthFactor);
    if (fRPMRatio > 1.0)
      fRPMRatio = 1.0f;
    dCalculatedPower = (float)calc_pow(iCarDesignIdx, iGearAyMax, fRPMRatio);
  } else {
    fRPMRatio = fNewBaseSpeed / (*pSpds * fHealthFactor);
    if (fRPMRatio > 1.0)
      fRPMRatio = 1.0f;
    dCalculatedPower = (float)calc_pow(iCarDesignIdx, 0, fRPMRatio);
  }
  fCurrentPower = (float)dCalculatedPower;
  if (iGearAyMax > 0 && human_control[pCar->iDriverIdx] != 2 && (double)pEngineData->pChgs[2 * iGearAyMax - 1] > fCurrentPower)// Auto downshift: check if power is below gear change threshold
  {
    fRPMRatio = fNewBaseSpeed / (pSpds[--iGearAyMax] * fHealthFactor);
    if (fRPMRatio > 1.0)
      fRPMRatio = 1.0f;
    fCurrentPower = (float)calc_pow(iCarDesignIdx, iGearAyMax, fRPMRatio);
  }
  dRPMRatioChange = fRPMRatio - pCar->fRPMRatio;
  pCar->byGearAyMax = iGearAyMax;
  pCar->fPower = fCurrentPower;
  fRPMRatioChange = (float)dRPMRatioChange;
  if (pCar->nCurrChunk != -1 && iGearAyMax != -1)// Limit RPM ratio changes to realistic values when on track
  {
    if (fRPMRatioChange <= 0.03) {
      if (fRPMRatioChange < -0.1)
        fRPMRatioChange = -0.1f;
    } else {
      fRPMRatioChange = 0.029999999f;
    }
  }
  pCar->fRPMRatio = pCar->fRPMRatio + fRPMRatioChange;
  if (iGearAyMax == -1)                       // Handle special cases: neutral gear (0) and reverse gear (negate speed)
  {
    fNewBaseSpeed = 0.0;
  } else if (iGearAyMax == -2) {
    fNewBaseSpeed = -fNewBaseSpeed;
    //HIBYTE(fNewBaseSpeed) ^= 0x80u;
  }
  iPitchDynamicOffset = pCar->iPitchDynamicOffset;// Update car pitch dynamics: gradual recovery to neutral position
  pCar->fBaseSpeed = fNewBaseSpeed;
  if (iPitchDynamicOffset >= 0) {
    iNoseUpRecoveryRate = pEngineData->iNoseUpRecoveryRate;
    pCar->iPitchDynamicOffset = iPitchDynamicOffset - iNoseUpRecoveryRate;
    if (iPitchDynamicOffset - iNoseUpRecoveryRate >= 0)
      return;
  } else {
    iPitchDeltaNeg = iPitchDynamicOffset + pEngineData->iNoseDownRecoveryRate;
    pCar->iPitchDynamicOffset = iPitchDeltaNeg;
    if (iPitchDeltaNeg <= 0)
      return;
  }
  pCar->iPitchDynamicOffset = 0;
}

//-------------------------------------------------------------------------------------------------
//0002C1A0
void SetEngine(tCar *pCar, float fThrottle)
{
  int byCarDesignIdx; // edi
  float *pSpds; // edx
  int iNumGears; // ecx
  double dThrottleRatio; // st7
  double dCalculatedPower; // st7
  int iCurrentGear; // ebx
  int iGearTableIdx; // ecx
  float *pfCurrentGearSpd; // esi
  double dFinalSpeed; // st7
  float fReverseRatio; // [esp+0h] [ebp-44h]
  float *pfSpeedsArray; // [esp+4h] [ebp-40h]
  float fReverseSpeed; // [esp+8h] [ebp-3Ch]
  float fHealthFactor; // [esp+Ch] [ebp-38h]
  int iMaxGearIdx; // [esp+10h] [ebp-34h]
  tCarEngine *pEngineData; // [esp+14h] [ebp-30h]
  float fGearShiftFlag; // [esp+18h] [ebp-2Ch]
  float fCurrentPower; // [esp+1Ch] [ebp-28h]
  float fAdjustedThrottle; // [esp+20h] [ebp-24h]
  float fSpeedRatio; // [esp+24h] [ebp-20h]
  float fLoopExitFlag; // [esp+28h] [ebp-1Ch]

  fHealthFactor = (pCar->fHealth + 34.0f) * 0.01f;// Calculate health factor (0-1) based on car health, capped at 1.0
  if (fHealthFactor > 1.0)
    fHealthFactor = 1.0;
  byCarDesignIdx = pCar->byCarDesignIdx;
  pCar->fSpeedOverflow = 0.0;
  pEngineData = &CarEngines.engines[byCarDesignIdx];// Get engine data for this car design
  pSpds = pEngineData->pSpds;
  iNumGears = pEngineData->iNumGears;
  pfSpeedsArray = pSpds;
  if (fThrottle >= 0.5)                       // Forward throttle - calculate gear and power
  {
    iCurrentGear = 0;
    fLoopExitFlag = 0.0;
    fAdjustedThrottle = fThrottle / fHealthFactor;// Adjust throttle by health factor
    do {
      if (fAdjustedThrottle > (double)*pSpds) {
        ++iCurrentGear;
        ++pSpds;
        if (iCurrentGear != iNumGears)
          continue;
      }
      fLoopExitFlag = -1.0;
    } while (fabs(fLoopExitFlag) == 0);// Find appropriate gear for current throttle
    //} while ((LODWORD(fLoopExitFlag) & 0x7FFFFFFF) == 0);// Find appropriate gear for current throttle
    if (iCurrentGear < iNumGears) {
      fSpeedRatio = fAdjustedThrottle / *pSpds;
    } else {
      iCurrentGear = iNumGears - 1;
      fAdjustedThrottle = pfSpeedsArray[iNumGears - 1];
      fSpeedRatio = 1.0;
    }
    fGearShiftFlag = 0.0;
    iMaxGearIdx = iNumGears - 1;
    iGearTableIdx = 2 * iCurrentGear;
    pfCurrentGearSpd = &pfSpeedsArray[iCurrentGear];
    fCurrentPower = (float)calc_pow(byCarDesignIdx, iCurrentGear, fSpeedRatio);// Calculate power for current gear/throttle
    while (iCurrentGear < iMaxGearIdx)        // Check if car can upshift to higher gears
    {
      //if ((LODWORD(fGearShiftFlag) & 0x7FFFFFFF) != 0)
      if (fabs(fGearShiftFlag) > FLT_EPSILON)
        break;
      if ((double)pEngineData->pChgs[iGearTableIdx] * 1.2 >= fCurrentPower) {
        fGearShiftFlag = -1.0;
      } else {
        fSpeedRatio = fAdjustedThrottle / pfCurrentGearSpd[1];
        iGearTableIdx += 2;
        ++iCurrentGear;
        ++pfCurrentGearSpd;
        fCurrentPower = (float)calc_pow(byCarDesignIdx, iCurrentGear, fSpeedRatio);
      }
    }
    pCar->fPower = fCurrentPower;
    pCar->fRPMRatio = fSpeedRatio;
    dFinalSpeed = fSpeedRatio * pfSpeedsArray[iCurrentGear] * fHealthFactor;
    pCar->fBaseSpeed = (float)dFinalSpeed;
    pCar->byGearAyMax = iCurrentGear;
    pCar->fSpeedOverflow = fThrottle - (float)dFinalSpeed;
  } else if (fabs(fThrottle) >= 0.5)            // Reverse throttle - calculate reverse power and speed
  {
    pCar->byGearAyMax = -2;
    fReverseSpeed = *pSpds * fHealthFactor;
    fThrottle = -fThrottle;
    //HIBYTE(fThrottle) ^= 0x80u;
    if (fThrottle <= (double)fReverseSpeed) {
      dThrottleRatio = fThrottle / fReverseSpeed;
      pCar->fSpeedOverflow = 0.0;
      fReverseSpeed = fThrottle;
      fReverseRatio = (float)dThrottleRatio;
    } else {
      fReverseRatio = 1.0;
      pCar->fSpeedOverflow = fReverseSpeed - fThrottle;
    }
    pCar->fBaseSpeed = -fReverseSpeed;
    dCalculatedPower = calc_pow(byCarDesignIdx, 0, fReverseRatio);
    pCar->fRPMRatio = fReverseRatio;
    pCar->fPower = (float)dCalculatedPower;
  } else {
    pCar->byGearAyMax = 0;                      // Neutral/idle - zero all engine output values
    pCar->fPower = 0.0;
    pCar->fRPMRatio = 0.0;
    pCar->fBaseSpeed = 0.0;
    pCar->fSpeedOverflow = 0.0;
    pCar->fFinalSpeed = 0.0;
  }
  pCar->fFinalSpeed = pCar->fBaseSpeed + pCar->fSpeedOverflow;// Final speed = base speed + speed overflow
}

//-------------------------------------------------------------------------------------------------
//0002C420
double change_gear(int iCurrentGear, int iNextGear, tCar *pCar, int iCarDesignIdx)
{
  float fCalculatedPower; // [esp+4h] [ebp-18h]
  float fAdjustedSpeed; // [esp+8h] [ebp-14h]
  float fHealthFactor; // [esp+Ch] [ebp-10h]
  float fSpeedRatio; // [esp+10h] [ebp-Ch]

  fHealthFactor = (pCar->fHealth + 34.0f) * 0.01f;// Calculate health factor (0-1) based on car damage, capped at 1.0
  if (fHealthFactor > 1.0)
    fHealthFactor = 1.0;
  fAdjustedSpeed = (float)fabs(pCar->fFinalSpeed) / fHealthFactor;// Get current speed adjusted by health factor (damaged cars perform worse)
  if (iNextGear < 0)                          // Clamp negative gear to 0 for power curve lookup (reverse uses gear 0 data)
    iNextGear = 0;
  fSpeedRatio = fAdjustedSpeed / CarEngines.engines[iCarDesignIdx].pSpds[iNextGear];// Calculate speed ratio: current speed vs max speed for target gear
  if (fSpeedRatio > 1.0)                      // Clamp speed ratio to 1.0 (can't exceed gear's max RPM)
    fSpeedRatio = 1.0;
  fCalculatedPower = (float)calc_pow(iCarDesignIdx, iNextGear, fSpeedRatio);// Calculate power output for target gear at current speed ratio
  if (player1_car == pCar->iDriverIdx && !DriveView[0])// Play gear shift sound for human players (not in cockpit view)
    sfxsample(SOUND_SAMPLE_GRSHIFT, 12000);                        // SOUND_SAMPLE_GRSHIFT
  if (player2_car == pCar->iDriverIdx && !DriveView[0])
    sfxsample(SOUND_SAMPLE_GRSHIFT, 12000);                        // SOUND_SAMPLE_GRSHIFT
  return fCalculatedPower;                      // Return calculated power for the gear change
}

//-------------------------------------------------------------------------------------------------
//0002C5A0
void updatecar2(tCar *pCar)
{
  int iLeaderCarIdx; // edi
  int iLeaderProgress; // ecx
  int iCarLoopIdx; // edx
  int iCarArrayIdx; // eax
  int iLastValidChunk; // ebx
  int iLeaderChunk; // eax
  int iLeaderProgressTotal; // ecx
  int iPlayerProgress; // eax
  int iPlayer1Car; // ecx
  int iCurrentDriverIdx; // ebx
  int iPlayer2Car; // ecx
  int iSoundSampleId; // eax
  int iPlayerCarCheck; // ecx
  int iDriverIdx; // eax
  int iThisDriverIdx; // ebp
  int iPitLaneFlag; // edx
  int iCurrentChunk; // edx
  int iPrevChunk; // edx
  uint8 byEngineStartTimer; // cl
  uint8 bySfxCooldown; // al
  uint8 byCollisionTimer; // dl
  uint8 byCheatCooldown; // bl
  int iRandomValue; // eax
  double dWheelSpinAccumulator; // st7
  int iRandomValue2; // eax
  int iOscillationValue; // ebp
  int16 ReverseWarnCooldown; // dx
  int iCurrentYaw3; // ecx
  int iSpeechDriverIdx; // ebp
  int iYaw3Value; // ebx
  int iAudioDriverIdx; // edi
  int iAudioSample; // eax
  int16 nChangeMateCooldown; // di
  double dOscillationFactor; // st7
  int iControlType; // eax
  double dPitchBackupCalc; // st7
  double dTempPitchCalc; // st6
  double dCosineValue; // st7
  double dRollCalc; // st6
  int iControlTypeCheck; // edx
  double dBaseSpeed; // st7
  int iChunkCheck; // eax
  double dNewSpeed; // st7
  double dAbsoluteSpeed; // st7
  int iSteeringInput; // edx
  int iRollDynamicTemp; // ebx
  tCarEngine *pEngineTemp; // eax
  int iEngineCalc; // eax
  int iRollTemp; // ebp
  tCarEngine *pEngineTemp2; // eax
  int iEngineCalc2; // eax
  int iRollDynamicOffset; // ebp
  int iEngineParam; // eax
  int iOffsetCalc; // edx
  int iSteeringCalc; // ecx
  tCarEngine *pEngineTemp3; // eax
  int iEngineCalc3; // eax
  int iSteeringCalc2; // ebp
  tCarEngine *pEngineTemp4; // edx
  int iUnk14; // ebx
  int iRollDynamicCheck; // ebp
  int iRollCenteringRate; // eax
  int iOffsetCalc2; // edx
  int iViewType; // eax
  int iViewDriverIdx; // edi
  unsigned int iControlTypeLocal; // eax
  float *pLocalData; // ecx
  int iChunkCurrent; // ebx
  int iChunkNext; // edx
  double dPositionCalc; // st7
  double dTrackPosCalc; // st5
  double dInterpolationFactor; // st6
  int iSurfaceType; // eax
  //__int16 v75; // fps
  double fBankingCalc1; // st6
  double fBankingCalc2; // st7
  double dLeftBankAngle; // st7
  double fRightBankCalc; // st7
  //__int16 v83; // fps
  double fRightBankCalc2; // st6
  double dRightBankAngle; // st7
  bool bZeroFlag; // zf
  int iTrackColorFlag; // edx
  int iPlayer1CarIdx; // eax
  int iCurrentDriverIdx2; // ebp
  int iSpeechDriverIdx2; // ecx
  int iCurrentChunk2; // edx
  int iLoopCounter; // edx
  int iNumCarsLocal; // ecx
  int iArrayIndex; // eax
  double dHealthIncrease; // st7
  int iPlayer1Check; // eax
  int iDriverForHealth; // ecx
  int iCenterGrip; // edx
  double dSurfaceValue; // st7
  uint8 byStatusFlags; // bl
  int iGripIndex; // edx
  int iChunkIdx; // ebx
  double fY; // st7
  tVec3 *pPointAy; // edx
  tTrackInfo *pTrackInfo; // edi
  int iCurrentYaw; // eax
  float fRPMRatio; // ebp
  double dSpeedOverflowCalc1; // st7
  double dSpeedOverflowCalc2; // st7
  double dSpeedOverflowCalc3; // st7
  double dSpeedOverflowCalc4; // st7
  double dSteeringSpeedCalc; // st7
  double iUnk20; // st7
  int iYawForCarBase; // ebx
  double dCurrentSpeed; // st7
  double dMovementSpeed; // st7
  int iYaw3ForMovement; // eax
  tData *pChunkData; // eax
  double dPositionY; // st7
  int iChunkForColor; // eax
  int iTrackColorValue; // ebx
  unsigned int uiJumpFlag; // ecx
  int iCollisionFlag; // edx
  double dLeftWallCheck; // st7
  double dLeftWallCheck2; // st7
  int iLaneTypeCheck; // edx
  //__int64 llTrackColorData; // rax
  int iYawDifferenceCalc; // eax
  double dSpeedAbs; // st7
  double dGripCalculation; // st7
  int iHumanControlIdx; // eax
  //int iInput; // eax
  //__int64 llInputCheck; // rax
  int iCurrentYawForInput; // eax
  int iYaw3ForInput; // ebp
  int16 nYawAdjusted; // ax
  int16 nTargetChunk; // cx
  double dDirectionZ; // st7
  float fHorizontalSpeedLocal; // ebp
  int iPitchAngle; // eax
  //__int16 v144; // fps
  double dAtanResult; // st7
  int16 nRollAdjusted; // ax
  int iYaw3Current; // ecx
  int iYawDelta; // ebx
  int16 nTargetChunkLocal; // dx
  int iYaw3ForType2; // eax
  double dCoordY; // st7
  double dCoordX; // st5
  double dCoordZ; // rtt
  int iChunkPlus2; // ebx
  int iChunkPlus3; // ecx
  double dGroundHeightForward; // st7
  int iChunkMinus1; // ecx
  int iChunkMinus2; // ebx
  double dGroundHeightBackward; // st7
  double dBoundaryLimit; // st7
  double dTransformedY; // st7
  double dTransformedX; // st5
  double dTransformedZ; // st6
  float fCurrentSpeedFloat; // edx
  double dFallDamage; // st7
  int iGroundChunk; // eax
  int iPitch; // ebx
  int iRoll; // eax
  float fDamageAmount; // [esp+0h] [ebp-190h]
  tData coordinateSystemData; // [esp+4h] [ebp-18Ch] BYREF
  int iRightBankAngle; // [esp+8Ch] [ebp-104h]
  int iNearestChunk; // [esp+90h] [ebp-100h] BYREF
  float fLeftShoulderTrackWidth; // [esp+94h] [ebp-FCh]
  float fCurrentTrackHalfWidth; // [esp+98h] [ebp-F8h]
  float fCameraOscillationRange; // [esp+9Ch] [ebp-F4h]
  float fTempFloat1; // [esp+A0h] [ebp-F0h]
  float fDirectionZ; // [esp+A4h] [ebp-ECh]
  float fBankingForceX; // [esp+A8h] [ebp-E8h]
  float fCameraOscillationBase; // [esp+ACh] [ebp-E4h]
  float fHorizontalSpeedCopy; // [esp+B0h] [ebp-E0h]
  float fNegativeYawLimit; // [esp+B4h] [ebp-DCh]
  float fNegativeYawLimitHuman; // [esp+B8h] [ebp-D8h]
  float fLeftTrackBoundary; // [esp+BCh] [ebp-D4h]
  int iLeaderValidationFlag; // [esp+C0h] [ebp-D0h]
  float fRightCarPosY; // [esp+C4h] [ebp-CCh]
  float fRightTrackBoundary; // [esp+C8h] [ebp-C8h]
  float fSpeedLimited; // [esp+CCh] [ebp-C4h]
  float fTrackPointY; // [esp+D0h] [ebp-C0h]
  float fTrackBankingY; // [esp+D4h] [ebp-BCh]
  float fCarPosZ; // [esp+D8h] [ebp-B8h]
  float fGroundBoundaryY; // [esp+DCh] [ebp-B4h]
  float fCarBaseYProjected; // [esp+E0h] [ebp-B0h]
  float fTrackBankingX; // [esp+E4h] [ebp-ACh]
  float fCarBaseXProjected; // [esp+E8h] [ebp-A8h]
  int iYaw3; // [esp+ECh] [ebp-A4h]
  float fGroundAverageZ1; // [esp+F0h] [ebp-A0h]
  float fCoordTransformX; // [esp+F4h] [ebp-9Ch]
  float fCoordTransformZ; // [esp+F8h] [ebp-98h]
  float fBankingForceY; // [esp+FCh] [ebp-94h]
  float fSurfaceGripBase; // [esp+100h] [ebp-90h]
  float fLeftShoulderBoundary; // [esp+104h] [ebp-8Ch]
  float fZ; // [esp+108h] [ebp-88h]
  int byCarTypeIndex; // [esp+10Ch] [ebp-84h]
  float fCurrentSpeedAbs; // [esp+110h] [ebp-80h]
  float fSpeedChangeRate; // [esp+114h] [ebp-7Ch]
  float fTempGrip; // [esp+118h] [ebp-78h]
  float fSurfaceGripValue; // [esp+11Ch] [ebp-74h]
  float fCurrentGripMultiplier; // [esp+120h] [ebp-70h]
  float fFinalSpeedThisFrame; // [esp+124h] [ebp-6Ch]
  float fRightShoulderBoundary; // [esp+128h] [ebp-68h]
  float fHealthAdjustment; // [esp+12Ch] [ebp-64h]
  float fMaxGripLimit_1; // [esp+130h] [ebp-60h]
  float fCoordTransformY; // [esp+134h] [ebp-5Ch]
  float fSpeedScaled; // [esp+138h] [ebp-58h]
  int iJumpMomentum; // [esp+13Ch] [ebp-54h]
  int iSteeringInputProcessed; // [esp+140h] [ebp-50h]
  float fCarPosZBackup; // [esp+144h] [ebp-4Ch]
  int iHumanYawDiff; // [esp+148h] [ebp-48h]
  int nCurrentChunk; // [esp+14Ch] [ebp-44h]
  int iYawDifference; // [esp+150h] [ebp-40h]
  float fMaxGripLimit; // [esp+154h] [ebp-3Ch]
  tCarEngine *pCarEngine; // [esp+158h] [ebp-38h]
  int iYawCurrent; // [esp+15Ch] [ebp-34h]
  float fAbsoluteSpeed; // [esp+160h] [ebp-30h]
  float fSpeedOverflow; // [esp+164h] [ebp-2Ch]
  float fCarHalfWidthProjected; // [esp+168h] [ebp-28h]
  float fTrackCenterLine; // [esp+16Ch] [ebp-24h]
  float fAnimationSpeed; // [esp+170h] [ebp-20h]
  float fHorizontalSpeed; // [esp+174h] [ebp-1Ch]

  byCarTypeIndex = pCar->byCarDesignIdx;
  iYaw3 = pCar->nActualYaw;
  iJumpMomentum = pCar->iJumpMomentum;
  iYawCurrent = pCar->nYaw;
  nCurrentChunk = pCar->nCurrChunk;
  pCarEngine = &CarEngines.engines[byCarTypeIndex];// Get car engine data based on car design index
  if (pCar->iControlType != 3)
    goto LABEL_45;                              // AI Control Type - Find leader car for lap/unlap detection
  iLeaderCarIdx = 0;
  iLeaderProgress = 0;
  iCarLoopIdx = 0;
  iLeaderValidationFlag = -1;
  if (numcars > 0)                            // Loop through all cars to find race leader
  {
    iCarArrayIdx = 0;
    while (1) {
      iLastValidChunk = Car[iCarArrayIdx].iLastValidChunk;
      if (iLastValidChunk < 3 || TRAK_LEN - 4 < iLastValidChunk)
        break;
      if (Car[iCarArrayIdx].iLastValidChunk + TRAK_LEN * (char)Car[iCarArrayIdx].byLapNumber > iLeaderProgress)// Calculate total progress: chunk + lap * track_length
      {
        iLeaderCarIdx = iCarLoopIdx;
        iLeaderProgress = Car[iCarArrayIdx].iLastValidChunk + TRAK_LEN * (char)Car[iCarArrayIdx].byLapNumber;
      }
      ++iCarLoopIdx;
      ++iCarArrayIdx;
      if (iCarLoopIdx >= numcars)
        goto LABEL_10;
    }
    iLeaderValidationFlag = 0;
  }
LABEL_10:
  if (iLeaderCarIdx != pCar->iDriverIdx && Car[iLeaderCarIdx].iControlType == 3 && (char)Car[iLeaderCarIdx].byLives > 0 && iLeaderValidationFlag) {
    iLeaderChunk = Car[iLeaderCarIdx].nCurrChunk == -1 ? Car[iLeaderCarIdx].iLastValidChunk : Car[iLeaderCarIdx].nCurrChunk;
    iLeaderProgressTotal = TRAK_LEN * (char)Car[iLeaderCarIdx].byLapNumber + iLeaderChunk;
    if (lastsample < -18) {
      iPlayerProgress = TRAK_LEN + pCar->nCurrChunk + TRAK_LEN * (char)pCar->byLapNumber;
      if (pCar->byLappedStatus) {
        if (iLeaderProgressTotal >= iPlayerProgress - 1)
          goto LABEL_30;
        iPlayerCarCheck = player1_car;
        iDriverIdx = pCar->iDriverIdx;
        pCar->byLappedStatus = 0;
        if (iPlayerCarCheck == iDriverIdx)
          speechsample(SOUND_SAMPLE_UNLAPPED, 0x8000, 18, iPlayerCarCheck);// Play "unlapped" sound sample (52)
        iPlayer2Car = player2_car;
        if (player2_car != pCar->iDriverIdx)
          goto LABEL_30;
        iSoundSampleId = SOUND_SAMPLE_UNLAPPED;                    // SOUND_SAMPLE_UNLAPPED
      } else {
        if (iLeaderProgressTotal <= iPlayerProgress + 1)
          goto LABEL_30;
        iPlayer1Car = player1_car;
        iCurrentDriverIdx = pCar->iDriverIdx;
        pCar->byLappedStatus = -1;
        if (iPlayer1Car == iCurrentDriverIdx)
          speechsample(SOUND_SAMPLE_LAPPED, 0x8000, 18, iPlayer1Car);// Play "lapped" sound sample (51)
        iPlayer2Car = player2_car;
        if (player2_car != pCar->iDriverIdx)
          goto LABEL_30;
        iSoundSampleId = SOUND_SAMPLE_LAPPED;                    // SOUND_SAMPLE_LAPPED
      }
      speechsample(iSoundSampleId, 0x8000, 18, iPlayer2Car);
    }
  }
LABEL_30:
  iThisDriverIdx = pCar->iDriverIdx;
  if (player1_car == iThisDriverIdx || player2_car == iThisDriverIdx) {
    iPitLaneFlag = 0;
    if ((char)pCar->byLapNumber == NoOfLaps && TRAK_LEN - pCar->nCurrChunk < 200)
      iPitLaneFlag = -1;
    if (death_race)
      iPitLaneFlag = -1;
    if (pCar->fHealth < 50.0 && !iPitLaneFlag) {
      iCurrentChunk = pCar->nCurrChunk;
      if (iCurrentChunk != -1) {
        iPrevChunk = iCurrentChunk - 1;
        if (iPrevChunk < 0)
          iPrevChunk = TRAK_LEN - 1;
        if ((TrakColour[pCar->nCurrChunk][1] & SURFACE_FLAG_PIT_ZONE) != 0 && (TrakColour[iPrevChunk][1] & SURFACE_FLAG_PIT_ZONE) == 0)
          speechonly(SOUND_SAMPLE_PITIN, 0x8000, 18, pCar->iDriverIdx);// Play "pit in" sound sample (46) when entering pit lane
      }
    }
  }
LABEL_45:
  byEngineStartTimer = pCar->byEngineStartTimer;// Update various car timers - collision, cheat cooldown, etc.
  if (byEngineStartTimer)
    pCar->byEngineStartTimer = byEngineStartTimer - 1;
  bySfxCooldown = pCar->bySfxCooldown;
  if (bySfxCooldown)
    pCar->bySfxCooldown = bySfxCooldown - 1;
  byCollisionTimer = pCar->byCollisionTimer;
  if (byCollisionTimer)
    pCar->byCollisionTimer = byCollisionTimer - 1;
  byCheatCooldown = pCar->byCheatCooldown;
  if (byCheatCooldown)
    pCar->byCheatCooldown = byCheatCooldown - 1;
  //if (pCar->byThrottlePressed && !pCar->byEngineStartTimer && (LODWORD(pCar->fRPMRatio) & 0x7FFFFFFF) == 0 && !pCar->byAccelerating)
  if (pCar->byThrottlePressed && !pCar->byEngineStartTimer && fabs(pCar->fRPMRatio) == 0 && !pCar->byAccelerating)
    pCar->byThrottlePressed = 0;
  if (!pCar->byThrottlePressed && !pCar->byEngineStartTimer && pCar->byAccelerating && pCar->iControlType == 3)// False start detection and engine start logic for AI cars
  {
    fHorizontalSpeed = pCar->fHealth * 9.0f * 0.0099999998f;
    iRandomValue = rand();
    int iTemp = GetHighOrderRand(20, iRandomValue);// (20 * iRandomValue) >> 15;
    //LODWORD(fCameraOscillationRange) = (20 * iRandomValue) >> 15;
    if (fHorizontalSpeed + 10.0 <= (double)iTemp && false_starts) {
      if (player1_car == pCar->iDriverIdx)
        sfxsample(29, 0x8000);                  // SOUND_SAMPLE_FALSE - play false start sound for player 1
      if (player2_car == pCar->iDriverIdx)
        sfxsample(29, 0x8000);                  // SOUND_SAMPLE_FALSE
      pCar->byEngineStartTimer = 72;
    } else {
      if (player1_car == pCar->iDriverIdx)
        sfxsample(28, 0x8000);                  // SOUND_SAMPLE_ESTART - play engine start sound for player 1
      if (player2_car == pCar->iDriverIdx)
        sfxsample(28, 0x8000);                  // SOUND_SAMPLE_ESTART
      pCar->byThrottlePressed = -1;
      pCar->byEngineStartTimer = 36;
    }
  }
  if (pCar->fRPMRatio < 1.0) {
    pCar->fWheelSpinAccumulation = 0.0;
  } else {
    dWheelSpinAccumulator = pCar->fWheelSpinFactor + pCar->fWheelSpinAccumulation;
    pCar->fWheelSpinAccumulation = (float)dWheelSpinAccumulator;
    if (dWheelSpinAccumulator < -1000.0)
      pCar->fWheelSpinAccumulation = -1000.0;
  }
  iRandomValue2 = rand();
  //iOscillationValue = ((iRandomValue2) >> 13) + 512 + pCar->iEngineVibrateOffset;
  iOscillationValue = GetHighOrderRand(4, iRandomValue2) + 512 + pCar->iEngineVibrateOffset;
  pCar->iEngineVibrateOffset = iOscillationValue;
  if (iOscillationValue >= 0x4000)
    pCar->iEngineVibrateOffset = iOscillationValue - 0x4000;
  ReverseWarnCooldown = pCar->nReverseWarnCooldown;
  if (ReverseWarnCooldown > 0)
    pCar->nReverseWarnCooldown = ReverseWarnCooldown - 1;
  if (pCar->fFinalSpeed >= 200.0 && nCurrentChunk >= 0) {
    iCurrentYaw3 = pCar->nActualYaw;
    if (iCurrentYaw3 > 4096 && iCurrentYaw3 < 12288 && !pCar->nReverseWarnCooldown) {
      iSpeechDriverIdx = pCar->iDriverIdx;
      if (player1_car == iSpeechDriverIdx || player2_car == iSpeechDriverIdx)
        speechsample(26, 0x8000, 18, pCar->iDriverIdx);// SOUND_SAMPLE_REVERSE - warn player about driving in reverse at high speed
      pCar->nReverseWarnCooldown = 360;
    }
  }
  iYaw3Value = pCar->nActualYaw;
  if (iYaw3Value <= 4096 || iYaw3Value >= 12288 && pCar->fFinalSpeed > 0.0)
    pCar->nReverseWarnCooldown = 180;
  iAudioDriverIdx = pCar->iDriverIdx;
  if ((player1_car == iAudioDriverIdx || player2_car == iAudioDriverIdx) && (nCurrentChunk >= 0 && pCar->nActualYaw < 4096 || pCar->nActualYaw > 12288)) {
    //nCurrentChunk check and iAudioSample init added by ROLLER
    iAudioSample = 0;
    if (nCurrentChunk >= 0 && (double)samplespeed[nCurrentChunk] <= pCar->fFinalSpeed) {
      iAudioSample = samplemax[nCurrentChunk];
    } else if (pCar->fFinalSpeed <= 80.0) {
      iAudioSample = 0;
    } else if (nCurrentChunk >= 0){
      iAudioSample = samplemin[nCurrentChunk];
    }
    if (iAudioSample) {
      if (pCar->nLastCommentaryChunk != nCurrentChunk) {
        speechonly(iAudioSample - 1, 0x8000, 18, pCar->iDriverIdx);
        pCar->nLastCommentaryChunk = nCurrentChunk;
      }
    } else {
      pCar->nLastCommentaryChunk = -1;
    }
  }
  nChangeMateCooldown = pCar->nChangeMateCooldown;
  if (nChangeMateCooldown)
    pCar->nChangeMateCooldown = nChangeMateCooldown - 1;
  fHealthAdjustment = (pCar->fHealth + 34.0f) * 0.01f;
  if (fHealthAdjustment > 1.0)
    fHealthAdjustment = 1.0;
  fCameraOscillationRange = CarEngines.engines[byCarTypeIndex].fOscillationMax - CarEngines.engines[byCarTypeIndex].fOscillationMin;// Calculate camera pitch and roll offsets based on car engine oscillation
  //fHorizontalSpeed = COERCE_FLOAT(abs(pCar->iPitchBackup));
  //dOscillationFactor = (double)SLODWORD(fHorizontalSpeed) * fCameraOscillationRange * 0.00024414062 + CarEngines.engines[byCarTypeIndex].fOscillationMin;
  dOscillationFactor = (double)abs(pCar->iPitchBackup) * fCameraOscillationRange * 0.00024414062 + CarEngines.engines[byCarTypeIndex].fOscillationMin;
  fTempFloat1 = tcos[((uint16)CarEngines.engines[byCarTypeIndex].iOscillationFreq * (uint16)pCar->iCameraOscillationPhase) & 0x3FFF];
  iControlType = pCar->iControlType;
  fCameraOscillationBase = (float)dOscillationFactor;
  if (iControlType == 2)
    dPitchBackupCalc = (double)pCar->iPitchBackup * 0.9;
  else
    dPitchBackupCalc = (double)pCar->iPitchBackup * fCameraOscillationBase;
  //_CHP();
  pCar->iPitchBackup = (int)dPitchBackupCalc;
  dTempPitchCalc = dPitchBackupCalc;
  dCosineValue = fTempFloat1;
  //_CHP();
  pCar->iPitchCameraOffset = (int)(dTempPitchCalc * fTempFloat1);
  dRollCalc = (double)pCar->iRollDampingMomentum * 0.9;
  //_CHP();
  pCar->iRollDampingMomentum = (int)dRollCalc;
  //_CHP();
  pCar->iRollCameraOffset = (int)(dCosineValue * dRollCalc);
  iControlTypeCheck = pCar->iControlType;
  if (iControlTypeCheck == 3 || iControlTypeCheck == 2)
    ++pCar->iCameraOscillationPhase;
  dBaseSpeed = pCar->fBaseSpeed;
  fSpeedOverflow = pCar->fSpeedOverflow;
  iChunkCheck = pCar->nCurrChunk;
  fSpeedChangeRate = (float)dBaseSpeed + fSpeedOverflow - pCar->fFinalSpeed;
  if (iChunkCheck != -1 && pCar->byCarDesignIdx != 9)// Speed change rate limiting - prevent unrealistic acceleration/deceleration
  {
    if (fSpeedChangeRate <= 2.0) {
      if (fSpeedChangeRate < -6.0)
        fSpeedChangeRate = -6.0;
    } else {
      fSpeedChangeRate = 2.0;
    }
  }
  dNewSpeed = pCar->fFinalSpeed + fSpeedChangeRate;
  fAbsoluteSpeed = (float)dNewSpeed;
  if (fabs(dNewSpeed) < 0.1) {
    fAbsoluteSpeed = 0.0;
    fSpeedOverflow = 0.0;
  }
  dAbsoluteSpeed = fAbsoluteSpeed;
  pCar->fFinalSpeed = fAbsoluteSpeed;
  fAbsoluteSpeed = (float)fabs(dAbsoluteSpeed);
  updatesmokeandflames(pCar);
  if (fAbsoluteSpeed > 0.0) {
    if (fAbsoluteSpeed >= 300.0) {
      pCar->byWheelAnimationFrame = 4;
    } else {
      fAnimationSpeed = pCar->fLastAnimationSpeed - fAbsoluteSpeed;
      if (fAnimationSpeed < 0.0)
        ++pCar->byWheelAnimationFrame;
      if (fAbsoluteSpeed >= 100.0) {
        while (fAnimationSpeed < 0.0)
          fAnimationSpeed = fAnimationSpeed + 300.0f;
        if ((char)pCar->byWheelAnimationFrame > 3)
          pCar->byWheelAnimationFrame = 2;
      } else {
        while (fAnimationSpeed < 0.0)
          fAnimationSpeed = fAnimationSpeed + 50.0f;
        if ((char)pCar->byWheelAnimationFrame > 1)
          pCar->byWheelAnimationFrame = 0;
      }
      pCar->fLastAnimationSpeed = fAnimationSpeed;
    }
  }
  if (nCurrentChunk != -1)
    pCar->nReferenceChunk = nCurrentChunk;
  if (nCurrentChunk != -1)
    pCar->iLastValidChunk = nCurrentChunk;
  iSteeringInput = pCar->iSteeringInput;
  if ((double)pCarEngine->iSteeringSpeedLimit > fAbsoluteSpeed)
    iSteeringInput = 0;
  // CHEAT_MODE_TINY_CARS
  if ((cheat_mode & CHEAT_MODE_TINY_CARS) != 0)            // CHEAT_MODE_TINY_CARS - modified steering response
  {
    if (iSteeringInput < 0) {
      iRollDynamicTemp = pCar->iRollDynamicOffset - 8 * pCarEngine->iRollResponseRate;
      pEngineTemp = pCarEngine;
      pCar->iRollDynamicOffset = iRollDynamicTemp;
      iEngineCalc = -8 * pEngineTemp->iMaxRollOffset;
      if (iEngineCalc > iRollDynamicTemp)
        pCar->iRollDynamicOffset = iEngineCalc;
      goto LABEL_171;
    }
    if (iSteeringInput > 0) {
      iRollTemp = 8 * pCarEngine->iRollResponseRate + pCar->iRollDynamicOffset;
      pEngineTemp2 = pCarEngine;
      pCar->iRollDynamicOffset = iRollTemp;
      iEngineCalc2 = 8 * pEngineTemp2->iMaxRollOffset;
      if (iEngineCalc2 < iRollTemp)
        pCar->iRollDynamicOffset = iEngineCalc2;
      goto LABEL_171;
    }
    iRollDynamicOffset = pCar->iRollDynamicOffset;
    if (iRollDynamicOffset <= 0) {
      iOffsetCalc = 4 * pCarEngine->iRollCenteringRate + iRollDynamicOffset;
      pCar->iRollDynamicOffset = iOffsetCalc;
      if (iOffsetCalc <= 0)
        goto LABEL_171;
    } else {
      iEngineParam = 4 * pCarEngine->iRollCenteringRate;
      pCar->iRollDynamicOffset = iRollDynamicOffset - iEngineParam;
      if (iRollDynamicOffset - iEngineParam >= 0)
        goto LABEL_171;
    }
  LABEL_170:
    pCar->iRollDynamicOffset = 0;
    goto LABEL_171;
  }
  if (iSteeringInput <= 0) {
    if (iSteeringInput >= 0) {
      iRollDynamicCheck = pCar->iRollDynamicOffset;
      if (iRollDynamicCheck <= 0) {
        iOffsetCalc2 = pCarEngine->iRollCenteringRate + iRollDynamicCheck;
        pCar->iRollDynamicOffset = iOffsetCalc2;
        if (iOffsetCalc2 <= 0)
          goto LABEL_171;
      } else {
        iRollCenteringRate = pCarEngine->iRollCenteringRate;
        pCar->iRollDynamicOffset = iRollDynamicCheck - iRollCenteringRate;
        if (iRollDynamicCheck - iRollCenteringRate >= 0)
          goto LABEL_171;
      }
      goto LABEL_170;
    }
    iSteeringCalc2 = pCarEngine->iRollResponseRate + pCar->iRollDynamicOffset;
    pEngineTemp4 = pCarEngine;
    pCar->iRollDynamicOffset = iSteeringCalc2;
    iUnk14 = pEngineTemp4->iMaxRollOffset;
    if (iSteeringCalc2 > iUnk14)
      pCar->iRollDynamicOffset = iUnk14;
  } else {
    iSteeringCalc = pCar->iRollDynamicOffset - pCarEngine->iRollResponseRate;
    pEngineTemp3 = pCarEngine;
    pCar->iRollDynamicOffset = iSteeringCalc;
    iEngineCalc3 = -pEngineTemp3->iMaxRollOffset;
    if (iEngineCalc3 > iSteeringCalc)
      pCar->iRollDynamicOffset = iEngineCalc3;
  }
LABEL_171:
  pCar->posLastFrame.fX = pCar->pos.fX;
  pCar->posLastFrame.fY = pCar->pos.fY;
  iViewType = ViewType[0];
  iViewDriverIdx = pCar->iDriverIdx;
  pCar->posLastFrame.fZ = pCar->pos.fZ;
  if (iViewType == iViewDriverIdx)
    doviewtend(pCar, 1, 0);
  if (ViewType[1] == pCar->iDriverIdx)
    doviewtend(pCar, 1, 1);
  iControlTypeLocal = pCar->iControlType;       // Branch on car control type: 0=Free Movement, 1=?, 2=Ground, 3=AI/Player
  if (pCar->nCurrChunk >= 0 && pCar->nCurrChunk < TRAK_LEN)//check added by ROLLER
    pLocalData = (float *)&localdata[pCar->nCurrChunk];
  else
    pLocalData = NULL;

  if (iControlTypeLocal < 2) {
    if (!iControlTypeLocal) {
      pCar->direction.fZ = pCar->direction.fZ + -3.0f;// Free movement physics - direct position and direction updates
      pCar->pos.fX = pCar->direction.fX + pCar->pos.fX;
      pCar->pos.fY = pCar->direction.fY + pCar->pos.fY;
      dDirectionZ = pCar->direction.fZ + pCar->pos.fZ;
      fHorizontalSpeed = pCar->fHorizontalSpeed;
      fCarPosZ = pCar->direction.fZ;
      fHorizontalSpeedCopy = fHorizontalSpeed;
      fHorizontalSpeedLocal = fHorizontalSpeed;
      fDirectionZ = fCarPosZ;
      pCar->pos.fZ = (float)dDirectionZ;
      //if ((LODWORD(fHorizontalSpeedLocal) & 0x7FFFFFFF) != 0 || (LODWORD(fCarPosZ) & 0x7FFFFFFF) != 0) {
      if (fabs(fHorizontalSpeedLocal) > FLT_EPSILON || fabs(fCarPosZ) > FLT_EPSILON) {
        dAtanResult = atan2(fDirectionZ, fHorizontalSpeedCopy) * 16384.0 / 6.28318530718;
        //_CHP();
        //LODWORD(fHorizontalSpeed) = (int)dAtanResult;
        iPitchAngle = (int)dAtanResult & 0x3FFF;
      } else {
        iPitchAngle = 0;
        //LOWORD(iPitchAngle) = 0;
      }
      pCar->nPitch = iPitchAngle;
      nRollAdjusted = (int16)(pCar->iRollMomentum) + pCar->nRoll;
      nRollAdjusted &= 0x3FFFu;
      //HIBYTE(nRollAdjusted) &= 0x3Fu;
      iYaw3Current = pCar->nActualYaw;
      pCar->nRoll = nRollAdjusted;
      iYawDelta = ((int16)iYaw3Current - pCar->nYaw) & 0x3FFF;
      iYawCurrent = pCar->nYaw;
      if ((unsigned int)iYawDelta > 0x2000)
        iYawDelta -= 0x4000;
      if ((int)abs(iYawDelta) > 400) {
        if (iYawDelta <= 0)
          iYawCurrent -= 400;
        else
          iYawCurrent += 400;
        iYawCurrent &= 0x3FFFu;
      } else {
        iYawCurrent = iYaw3Current;
      }
      pCar->nYaw = iYawCurrent;
      landontrack(pCar);
      //if ((LODWORD(pCar->fHealth) & 0x7FFFFFFF) == 0 && pCar->nDeathTimer < -90)
      if (fabs(pCar->fHealth) == 0 && pCar->nDeathTimer < -90)
        goto LABEL_501;
    }
    goto LABEL_502;
  }
  if (iControlTypeLocal > 2 && pCar->nCurrChunk >= 0 && pCar->nCurrChunk < TRAK_LEN) {//curr chunk check added by ROLLER
    if (iControlTypeLocal != 3)
      goto LABEL_502;
    if ((TrakColour[pCar->nCurrChunk][1] & SURFACE_FLAG_PIT_ZONE) == 0)
      pCar->byPitLaneActiveFlag = 0;
    iChunkCurrent = pCar->nCurrChunk;
    iChunkNext = iChunkCurrent + 1;             // Track surface calculations - determine track width and banking angles
    if (iChunkCurrent + 1 == TRAK_LEN)
      iChunkNext ^= TRAK_LEN;
    dPositionCalc = pCar->pos.fX + pLocalData[12];
    fCurrentTrackHalfWidth = localdata[iChunkCurrent].fTrackHalfWidth;
    fLeftShoulderTrackWidth = localdata[iChunkNext].fTrackHalfWidth;
    dTrackPosCalc = pCar->pos.fX - pLocalData[12];
    dInterpolationFactor = 1.0 / (pLocalData[12] * 2.0);
    fTrackCenterLine = (float)((dPositionCalc * fLeftShoulderTrackWidth - dTrackPosCalc * fCurrentTrackHalfWidth) * dInterpolationFactor);
    fCurrentTrackHalfWidth = TrackInfo[iChunkCurrent].fLShoulderWidth;
    fLeftShoulderTrackWidth = TrackInfo[iChunkNext].fLShoulderWidth;
    fLeftShoulderBoundary = (float)((dPositionCalc * fLeftShoulderTrackWidth - dTrackPosCalc * fCurrentTrackHalfWidth) * dInterpolationFactor);
    fLeftShoulderTrackWidth = TrackInfo[iChunkNext].fRShoulderWidth;
    fCurrentTrackHalfWidth = TrackInfo[iChunkCurrent].fRShoulderWidth;
    leftang = 0;
    rightang = 0;
    iSurfaceType = TrackInfo[iChunkCurrent].iLeftSurfaceType - 1;
    fRightShoulderBoundary = (float)((dPositionCalc * fLeftShoulderTrackWidth - dTrackPosCalc * fCurrentTrackHalfWidth) * dInterpolationFactor);
    switch (iSurfaceType) {
      case 0:
      case 2:
      case 3:
      case 6:
      case 7:
        fBankingCalc1 = TrackInfo[iChunkNext].fLShoulderWidth
          + localdata[iChunkNext].fTrackHalfWidth
          - (TrackInfo[iChunkCurrent].fLShoulderWidth
           + localdata[iChunkCurrent].fTrackHalfWidth);
        fBankingCalc2 = pLocalData[12] * 2.0;
        goto LABEL_188;
      case 4:
      case 5:
      case 8:
        fBankingCalc2 = pLocalData[12] * 2.0;
        fCurrentTrackHalfWidth = localdata[iChunkCurrent].fTrackHalfWidth;
        fLeftShoulderTrackWidth = localdata[iChunkNext].fTrackHalfWidth;
        fBankingCalc1 = fLeftShoulderTrackWidth - fCurrentTrackHalfWidth;
      LABEL_188:
        dLeftBankAngle = atan2(fBankingCalc1, fBankingCalc2) * 16384.0 / 6.28318530718;
        //_CHP();
        //LODWORD(fCameraOscillationRange) = (int)dLeftBankAngle;
        leftang = (int)dLeftBankAngle & 0x3FFF;
        break;
      default:
        break;
    }
    switch (TrackInfo[iChunkCurrent].iRightSurfaceType) {
      case 1:
      case 3:
      case 4:
      case 7:
      case 8:
        fRightBankCalc = pLocalData[12] * 2.0;
        fRightBankCalc2 = TrackInfo[iChunkCurrent].fRShoulderWidth
          + localdata[iChunkCurrent].fTrackHalfWidth
          - (TrackInfo[iChunkNext].fRShoulderWidth
           + localdata[iChunkNext].fTrackHalfWidth);
        goto LABEL_192;
      case 5:
      case 6:
      case 9:
        fRightBankCalc = pLocalData[12] * 2.0;
        fCurrentTrackHalfWidth = localdata[iChunkCurrent].fTrackHalfWidth;
        fLeftShoulderTrackWidth = localdata[iChunkNext].fTrackHalfWidth;
        fRightBankCalc2 = fCurrentTrackHalfWidth - fLeftShoulderTrackWidth;
      LABEL_192:
        dRightBankAngle = atan2(fRightBankCalc2, fRightBankCalc) * 16384.0 / 6.28318530718;
        //_CHP();
        iRightBankAngle = (int)dRightBankAngle;
        rightang = (int)dRightBankAngle & 0x3FFF;
        break;
      default:
        break;
    }
    if (pCar->pos.fY <= (double)fTrackCenterLine) {
      if (-fTrackCenterLine <= pCar->pos.fY)
        pCar->iLaneType = 1;
      else
        pCar->iLaneType = 2;
    } else {
      pCar->iLaneType = 0;
    }
    if (finished_car[pCar->iDriverIdx] || death_race) {
      iTrackColorFlag = 0;
      bZeroFlag = 1;
    } else {
      iTrackColorFlag = TrakColour[pCar->nCurrChunk][pCar->iLaneType] & SURFACE_FLAG_PIT;
      bZeroFlag = iTrackColorFlag == 0;
    }
    if (bZeroFlag)
      Car[0].byStatusFlags &= ~8u;
    //if ((LODWORD(fAbsoluteSpeed) & 0x7FFFFFFF) != 0)
    if (fabs(fAbsoluteSpeed) > FLT_EPSILON)
      pCar->byRepairSpeechPlayed = 0;
    //if ((LODWORD(fAbsoluteSpeed) & 0x7FFFFFFF) != 0 || !iTrackColorFlag || (pCar->byStatusFlags & 4) != 0) {
    if (fabs(fAbsoluteSpeed) > FLT_EPSILON || !iTrackColorFlag || (pCar->byStatusFlags & 4) != 0) {
      pCar->byDebugDamage = 0;
    } else {
      iPlayer1CarIdx = player1_car;
      iCurrentDriverIdx2 = pCar->iDriverIdx;
      pCar->byCheatAmmo = 8;
      if ((iPlayer1CarIdx == iCurrentDriverIdx2 || player2_car == iCurrentDriverIdx2) && !pCar->byRepairSpeechPlayed && pCar->fHealth < 100.0) {
        iSpeechDriverIdx2 = pCar->iDriverIdx;
        pCar->byRepairSpeechPlayed = -1;
        speechonly(SOUND_SAMPLE_MENDING, 0x8000, 18, iSpeechDriverIdx2);// SOUND_SAMPLE_MENDING - play repair sound when health is low
      }
      pCar->byDebugDamage = -1;
      iCurrentChunk2 = pCar->nCurrChunk;
      pCar->byStatusFlags |= 8u;
      if ((TrakColour[iCurrentChunk2][1] & SURFACE_FLAG_PIT_BOX) != 0)// Handle special pit lane targeting for AI cars on repair strips
      {
        iLoopCounter = 0;
        if (numcars > 0) {
          iNumCarsLocal = numcars;
          iArrayIndex = 0;
          do {
            if (iLoopCounter != pCar->iDriverIdx && pCar->nCurrChunk == Car[iArrayIndex].iAITargetCar)
              Car[iArrayIndex].iAITargetCar = -1;
            ++iLoopCounter;
            ++iArrayIndex;
          } while (iLoopCounter < iNumCarsLocal);
        }
        pCar->iAITargetCar = pCar->nCurrChunk;
      }
      dHealthIncrease = pCar->fHealth + 0.5;
      pCar->fHealth = (float)dHealthIncrease;
      if (dHealthIncrease >= 100.0) {
        iPlayer1Check = player1_car;
        iDriverForHealth = pCar->iDriverIdx;
        pCar->fHealth = 100.0;
        if ((iPlayer1Check == iDriverForHealth || player2_car == iDriverForHealth) && pCar->byRepairSpeechPlayed) {
          speechonly(SOUND_SAMPLE_FIXED, 0x8000, 18, pCar->iDriverIdx);// SOUND_SAMPLE_FIXED - play repair complete sound when health reaches 100%
          pCar->byRepairSpeechPlayed = 0;
        }
        pCar->byDebugDamage = 0;
        pCar->byStatusFlags = 0;
      }
    }
    iCenterGrip = localdata[pCar->nCurrChunk].iCenterGrip;// Calculate grip values based on car position (center track vs shoulders)
    if (pCar->pos.fY > (double)fTrackCenterLine)
      iCenterGrip = localdata[pCar->nCurrChunk].iLeftShoulderGrip;
    if (-fTrackCenterLine > pCar->pos.fY)
      iCenterGrip = localdata[pCar->nCurrChunk].iRightShoulderGrip;
    // CHEAT_MODE_ICY_ROAD
    if ((cheat_mode & CHEAT_MODE_ICY_ROAD) != 0)            // CHEAT_MODE grip hack - force maximum grip value of 10
      iCenterGrip = 10;
    dSurfaceValue = (double)surface[iCenterGrip].iGripModifier;
    fSurfaceGripValue = surface[iCenterGrip].fBaseGrip;
    byStatusFlags = pCar->byStatusFlags;
    fCurrentGripMultiplier = (float)((dSurfaceValue + CarEngines.engines[byCarTypeIndex].fGripBonus) / (2.5 - fHealthAdjustment * 1.5));
    if ((byStatusFlags & 4) != 0)
      fMaxGripLimit_1 = 10.0;
    else
      fMaxGripLimit_1 = CarEngines.engines[byCarTypeIndex].fMaxGripLimit;
    if (fCurrentGripMultiplier > (double)fMaxGripLimit_1)
      fCurrentGripMultiplier = fMaxGripLimit_1;
    if (fCurrentGripMultiplier < 0.0)
      fCurrentGripMultiplier = 0.0;
    iGripIndex = iCenterGrip;
    fSurfaceGripBase = surface[iGripIndex].fGripMultiplier * fHealthAdjustment;
    fTempGrip = (float)(((double)surface[iGripIndex].iSecondaryGrip + CarEngines.engines[byCarTypeIndex].fGripBonus) / (2.5 - fHealthAdjustment * 1.5));
    fMaxGripLimit_1 = fMaxGripLimit_1 * 0.5f;
    if (fTempGrip > (double)fMaxGripLimit_1)
      fTempGrip = fMaxGripLimit_1;
    if (fTempGrip < 0.0)
      fTempGrip = 0.0;
    iChunkIdx = pCar->nCurrChunk;
    fY = pCar->pos.fY;
    pPointAy = localdata[iChunkIdx].pointAy;
    pTrackInfo = &TrackInfo[iChunkIdx];
    fTrackPointY = pPointAy[6].fY;
    if (fY <= fTrackCenterLine) {
      if (-fTrackCenterLine <= pCar->pos.fY)
        fZ = pPointAy[6].fZ;
      else
        fZ = pPointAy[6].fZ * tcos[pTrackInfo->iRightBankAngle & 0x3FFF] - pPointAy[7].fX * tsin[pTrackInfo->iRightBankAngle & 0x3FFF];
    } else {
      fZ = pPointAy[6].fZ * tcos[pTrackInfo->iLeftBankAngle & 0x3FFF] + pPointAy[7].fX * tsin[pTrackInfo->iLeftBankAngle & 0x3FFF];
    }
    iCurrentYaw = pCar->nYaw;
    fBankingForceY = fTrackPointY * tcos[iCurrentYaw] + fZ * tsin[iCurrentYaw];
    fBankingForceX = -fTrackPointY * tsin[iCurrentYaw] + fZ * tcos[iCurrentYaw];
    fRPMRatio = pCar->fRPMRatio;                // Physics calculations - banking effects on car movement
    fSpeedScaled = fAbsoluteSpeed * 0.015f;
    if (fRPMRatio != 1.0 || (char)pCar->byGearAyMax == -1) {
      //if ((LODWORD(fBankingForceY) & 0x7FFFFFFF) == 0) {
      if (fabs(fBankingForceY) == 0) {
        if (fSpeedOverflow >= 0.0) {
          if (fSpeedOverflow > 0.0) {
            dSpeedOverflowCalc4 = fSpeedOverflow + -1.0;
            fSpeedOverflow = (float)dSpeedOverflowCalc4;
            if (dSpeedOverflowCalc4 <= 0.1)
              fSpeedOverflow = 0.0;
          }
        } else {
          dSpeedOverflowCalc3 = fSpeedOverflow + 1.0;
          fSpeedOverflow = (float)dSpeedOverflowCalc3;
          if (dSpeedOverflowCalc3 >= -0.1)
            fSpeedOverflow = 0.0;
        }
      }
    } else if (fSpeedOverflow >= 0.0) {
      if (fSpeedOverflow > 0.0) {
        dSpeedOverflowCalc2 = fSpeedOverflow + -1.0;
        fSpeedOverflow = (float)dSpeedOverflowCalc2;
        if (dSpeedOverflowCalc2 <= 0.1)
          fSpeedOverflow = 0.0;
      }
    } else {
      dSpeedOverflowCalc1 = fSpeedOverflow + 1.0;
      fSpeedOverflow = (float)dSpeedOverflowCalc1;
      if (dSpeedOverflowCalc1 >= -0.1)
        fSpeedOverflow = 0.0;
    }
    pCar->fSpeedOverflow = fSpeedOverflow;
    iSteeringInputProcessed = pCar->iSteeringInput;// Process steering input and apply speed-based steering modifiers
    if (!iSteeringInputProcessed)
      goto LABEL_272;
    if (pCar->fFinalSpeed < 0.0) {
      //LODWORD(fCameraOscillationRange) = -iSteeringInputProcessed;
      dSteeringSpeedCalc = (4500.0 - fAbsoluteSpeed * 23.0) * (double)-iSteeringInputProcessed * 0.00039999999;
    } else {
      if (fAbsoluteSpeed >= 360.0) {
      LABEL_272:
        iUnk20 = (double)pCarEngine->iSteeringSpeedLimit;
        iSteeringInputProcessed += pCar->iBankingSteerOffset;
        if (iUnk20 > fAbsoluteSpeed)
          iSteeringInputProcessed = 0;
        iYawCurrent = ((uint16)pCar->iJumpMomentum + (int16)iSteeringInputProcessed + (int16)iYawCurrent) & 0x3FFF;
        pCar->nYaw = iYawCurrent;
        if ((int)abs(iJumpMomentum) > 50) {
          if (iJumpMomentum <= 0)
            iJumpMomentum += 50;
          else
            iJumpMomentum -= 50;
        } else {
          iJumpMomentum = 0;
        }
        iYawForCarBase = iYawCurrent;
        pCar->iJumpMomentum = iJumpMomentum;
        if (iYawForCarBase < 4096 || iYawForCarBase >= 12288)
          fCarBaseYProjected = CarBaseY;
        else
          fCarBaseYProjected = -CarBaseY;
        if (iYawCurrent >= 0x2000)
          fTrackBankingY = CarBaseX;
        else
          fTrackBankingY = -CarBaseX;
        fCarHalfWidthProjected = fCarBaseYProjected * tcos[iYawCurrent] - fTrackBankingY * tsin[iYawCurrent];
        if ((((int16)iYawCurrent + 4096) & 0x3FFFu) < 0x1000 || (((int16)iYawCurrent + 4096) & 0x3FFFu) >= 0x3000)
          fTrackBankingX = CarBaseY;
        else
          fTrackBankingX = -CarBaseY;
        if ((((int16)iYawCurrent + 4096) & 0x3FFFu) >= 0x2000)
          fCarBaseXProjected = CarBaseX;
        else
          fCarBaseXProjected = -CarBaseX;
        pCar->fCarWidthBankingProjection = fTrackBankingX * tcos[((int16)iYawCurrent + 4096) & 0x3FFF] - fCarBaseXProjected * tsin[((int16)iYawCurrent + 4096) & 0x3FFF];
        dCurrentSpeed = fAbsoluteSpeed;
        pCar->fCarHalfWidth = fCarHalfWidthProjected;
        fSpeedLimited = fAbsoluteSpeed;
        if (dCurrentSpeed > 100.0)
          fSpeedLimited = 100.0;
        //_CHP();
        pCar->iBankingSteerOffset = (int)(fSpeedLimited * fBankingForceX * 0.16666667);
        if (pCar->iBankingSteerOffset > 16)
          pCar->iBankingSteerOffset = 16;
        if (pCar->iBankingSteerOffset < -16)
          pCar->iBankingSteerOffset = -16;
        if (race_started && !pCar->byDebugDamage)// Update car position and handle track collision detection
        {
          if ((fBankingForceY > 0.0 || fAbsoluteSpeed > 2.0) && pCar->fHealth > 0.0)
            pCar->fSpeedOverflow = fBankingForceY * 0.125f + pCar->fSpeedOverflow;
        } else {
          pCar->fSpeedOverflow = 0.0;
        }
        fFinalSpeedThisFrame = pCar->fFinalSpeed;
        if (fFinalSpeedThisFrame < 1.0 && (pCar->byGearAyMax & 0x80u) == 0 && pCar->fPower > 0.0)
          fFinalSpeedThisFrame = 1.0;
        dMovementSpeed = fFinalSpeedThisFrame;
        iYaw3ForMovement = iYaw3;
        pCar->pos.fX = fFinalSpeedThisFrame * tcos[iYaw3] + pCar->pos.fX;
        pCar->pos.fY = (float)dMovementSpeed * tsin[iYaw3ForMovement] + pCar->pos.fY;
        if (dMovementSpeed < 200.0)
          pCar->pos.fZ = fFinalSpeedThisFrame * tsin[pCar->nPitch] + pCar->pos.fZ;
        pChunkData = &localdata[pCar->nCurrChunk];
        pCar->pos.fZ = pChunkData->gravity.fZ + pCar->pos.fZ;
        if (fabs(pCar->pos.fX) > pChunkData->fTrackHalfLength)
          scansection(pCar);
        dPositionY = pCar->pos.fY;
        iChunkForColor = pCar->nCurrChunk;
        nCurrentChunk = iChunkForColor;
        if (dPositionY <= fTrackCenterLine) {
          if (-fTrackCenterLine <= pCar->pos.fY)
            iTrackColorValue = TrakColour[iChunkForColor][1];
          else
            iTrackColorValue = TrakColour[iChunkForColor][2];
        } else {
          iTrackColorValue = TrakColour[iChunkForColor][0];
        }
        uiJumpFlag = abs(iTrackColorValue) & SURFACE_FLAG_NON_MAGNETIC;
        if (pCar->fFinalSpeed < 0.0)
          uiJumpFlag = 0;
        if ((iTrackColorValue & SURFACE_FLAG_PREVENT_JUMP) != 0 && pCar->fFinalSpeed > 240.0)
          uiJumpFlag = 0;
        iCollisionFlag = 0;
        if ((TrakColour[pCar->nCurrChunk][1] & SURFACE_FLAG_WALL_22) != 0 && fTrackCenterLine >= fabs(pCar->pos.fY) - fCarHalfWidthProjected) {
          if (pCar->pos.fY < 0.0) {
            pCar->pos.fY = -fTrackCenterLine - fCarHalfWidthProjected;
            hitleft(pCar, 12, 1);
          } else {
            pCar->pos.fY = fTrackCenterLine + fCarHalfWidthProjected;
            hitright(pCar, 12, 0);
          }
          iCollisionFlag = -1;
        }
        if ((TrakColour[pCar->nCurrChunk][0] & SURFACE_FLAG_WALL_22) != 0 && pCar->pos.fY + fCarHalfWidthProjected >= fTrackCenterLine) {
          pCar->pos.fY = fTrackCenterLine - fCarHalfWidthProjected;
          hitleft(pCar, 12, 0);
          iCollisionFlag = -1;
        }
        if ((TrakColour[pCar->nCurrChunk][2] & SURFACE_FLAG_WALL_22) != 0 && -fTrackCenterLine >= pCar->pos.fY - fCarHalfWidthProjected) {
          pCar->pos.fY = fCarHalfWidthProjected - fTrackCenterLine;
          hitright(pCar, 12, 1);
          iCollisionFlag = -1;
        }
        if (!iCollisionFlag) {
          fCarPosZBackup = pCar->pos.fZ;
          if (pCar->iStunned) {
            iCollisionFlag = pCar->byCarDesignIdx;
            //LOBYTE(iCollisionFlag) = pCar->byCarDesignIdx;
            fCarPosZBackup = fCarPosZBackup - CarBox.hitboxAy[iCollisionFlag][4].fZ;
          }
          if (pCar->pos.fY > 0.0) {
            switch (pTrackInfo->iLeftSurfaceType) {
              case 0:
              case 2:
                if (fTrackCenterLine + fLeftShoulderBoundary >= pCar->pos.fY - fCarHalfWidthProjected * 0.5) {
                  if (!uiJumpFlag)
                    goto LABEL_344;
                  fCameraOscillationRange = fCarPosZBackup - fSpeedScaled;
                  if (getgroundz(pCar->pos.fX, pCar->pos.fY, pCar->nCurrChunk) >= fCameraOscillationRange)
                    goto LABEL_344;
                }
                goto LABEL_403;
              case 1:
              case 3:
              case 4:
              case 7:
              case 8:
                dLeftWallCheck = pCar->pos.fY + fCarHalfWidthProjected;
                fLeftTrackBoundary = fTrackCenterLine + fLeftShoulderBoundary;
                if (dLeftWallCheck > fLeftTrackBoundary) {
                  pCar->pos.fY = fLeftTrackBoundary - fCarHalfWidthProjected;
                  hitleft(pCar, 12, 0);
                  goto LABEL_404;
                }
                if (uiJumpFlag) {
                  fHorizontalSpeed = fCarPosZBackup - fSpeedScaled;
                  if (getgroundz(pCar->pos.fX, pCar->pos.fY, pCar->nCurrChunk) < fHorizontalSpeed)
                    goto LABEL_403;
                }
                goto LABEL_344;
              case 5:
                dLeftWallCheck2 = pCar->pos.fY + fCarHalfWidthProjected;
                fRightTrackBoundary = fTrackCenterLine + fLeftShoulderBoundary;
                if (dLeftWallCheck2 > fRightTrackBoundary) {
                  pCar->pos.fY = fRightTrackBoundary - fCarHalfWidthProjected;
                  hitleft(pCar, 12, 0);
                }
                if (pCar->pos.fY + fCarHalfWidthProjected > fTrackCenterLine && pCar->iLaneType) {
                  pCar->pos.fY = fTrackCenterLine - fCarHalfWidthProjected;
                  hitleft(pCar, 12, 0);
                }
                if (pCar->pos.fY - fCarHalfWidthProjected < fTrackCenterLine && !pCar->iLaneType) {
                  pCar->pos.fY = fTrackCenterLine + fCarHalfWidthProjected;
                  hitright(pCar, 12, 0);
                }
                if (uiJumpFlag) {
                  fHorizontalSpeed = fCarPosZBackup - fSpeedScaled;
                  if (getgroundz(pCar->pos.fX, pCar->pos.fY, pCar->nCurrChunk) < fHorizontalSpeed)
                    goto LABEL_403;
                }
                goto LABEL_344;
              case 6:
              case 9:
                if (fTrackCenterLine + fLeftShoulderBoundary < pCar->pos.fY - fCarHalfWidthProjected * 0.5)
                  goto LABEL_403;
                if (fCarHalfWidthProjected > fabs(pCar->pos.fY - fTrackCenterLine) && pCar->pos.fY + fCarHalfWidthProjected > fTrackCenterLine && pCar->iLaneType) {
                  pCar->pos.fY = fTrackCenterLine - fCarHalfWidthProjected;
                  hitleft(pCar, 12, 0);
                }
                if (pCar->pos.fY - fCarHalfWidthProjected < fTrackCenterLine && !pCar->iLaneType) {
                  pCar->pos.fY = fTrackCenterLine + fCarHalfWidthProjected;
                  hitright(pCar, 12, 0);
                }
                if (uiJumpFlag) {
                  fCameraOscillationRange = fCarPosZBackup - fSpeedScaled;
                  if (getgroundz(pCar->pos.fX, pCar->pos.fY, pCar->nCurrChunk) < fCameraOscillationRange)
                    goto LABEL_403;
                }
                goto LABEL_344;
              default:
                goto LABEL_404;                 // Handle left track surface types - walls, ramps, barriers, etc.
            }
          }
          switch (pTrackInfo->iRightSurfaceType) {
            case 0:
            case 2:
              if (fTrackCenterLine + fRightShoulderBoundary >= -pCar->pos.fY - fCarHalfWidthProjected * 0.5) {
                if (!uiJumpFlag)
                  goto LABEL_344;
                fCameraOscillationRange = fCarPosZBackup - fSpeedScaled;
                if (getgroundz(pCar->pos.fX, pCar->pos.fY, pCar->nCurrChunk) >= fCameraOscillationRange)
                  goto LABEL_344;
              }
              goto LABEL_403;
            case 1:
            case 3:
            case 4:
            case 7:
            case 8:
              if (fTrackCenterLine + fRightShoulderBoundary >= fCarHalfWidthProjected - pCar->pos.fY) {
                if (uiJumpFlag && (fHorizontalSpeed = fCarPosZBackup - fSpeedScaled, getgroundz(pCar->pos.fX, pCar->pos.fY, pCar->nCurrChunk) < fHorizontalSpeed))
                  LABEL_403:
                converttoair(pCar);
                else
                  LABEL_344:
                putflat(pCar);
              } else {
                pCar->pos.fY = -fTrackCenterLine - fRightShoulderBoundary + fCarHalfWidthProjected;
                hitright(pCar, 12, 1);
              }
              break;
            case 5:
              if (fTrackCenterLine + fRightShoulderBoundary < fCarHalfWidthProjected - pCar->pos.fY) {
                pCar->pos.fY = -fTrackCenterLine - fRightShoulderBoundary + fCarHalfWidthProjected;
                hitright(pCar, 12, 1);
              }
              if (fCarHalfWidthProjected - pCar->pos.fY > fTrackCenterLine && pCar->iLaneType != 2) {
                pCar->pos.fY = fCarHalfWidthProjected - fTrackCenterLine;
                hitright(pCar, 12, 1);
              }
              if (-pCar->pos.fY - fCarHalfWidthProjected < fTrackCenterLine && pCar->iLaneType == 2) {
                pCar->pos.fY = -fTrackCenterLine - fCarHalfWidthProjected;
                hitleft(pCar, 12, 1);
              }
              if (uiJumpFlag) {
                fHorizontalSpeed = fCarPosZBackup - fSpeedScaled;
                if (getgroundz(pCar->pos.fX, pCar->pos.fY, pCar->nCurrChunk) < fHorizontalSpeed)
                  goto LABEL_403;
              }
              goto LABEL_344;
            case 6:
            case 9:
              fRightCarPosY = -pCar->pos.fY;
              if (fTrackCenterLine + fRightShoulderBoundary < fRightCarPosY - fCarHalfWidthProjected * 0.5)
                goto LABEL_403;
              if (fCarHalfWidthProjected > fabs(fRightCarPosY - fTrackCenterLine) && fCarHalfWidthProjected - pCar->pos.fY > fTrackCenterLine && pCar->iLaneType != 2) {
                pCar->pos.fY = fCarHalfWidthProjected - fTrackCenterLine;
                hitright(pCar, 12, 1);
              }
              if (-pCar->pos.fY - fCarHalfWidthProjected < fTrackCenterLine && pCar->iLaneType == 2) {
                pCar->pos.fY = -fTrackCenterLine - fCarHalfWidthProjected;
                hitleft(pCar, 12, 1);
              }
              if (uiJumpFlag) {
                fHorizontalSpeed = fCarPosZBackup - fSpeedScaled;
                if (getgroundz(pCar->pos.fX, pCar->pos.fY, pCar->nCurrChunk) < fHorizontalSpeed)
                  goto LABEL_403;
              }
              goto LABEL_344;
            default:
              break;                            // Handle right track surface types - walls, ramps, barriers, etc.
          }
        }
      LABEL_404:
        if (pCar->iControlType == 3 && localdata[pCar->nCurrChunk].fTrackHalfLength * 2.0 > CarBaseX) {
          if (pCar->pos.fY <= (double)fTrackCenterLine) {
            if (-fTrackCenterLine <= pCar->pos.fY)
              iLaneTypeCheck = 1;
            else
              iLaneTypeCheck = 2;
          } else {
            iLaneTypeCheck = 0;
          }
          //llTrackColorData = TrakColour[pCar->nCurrChunk][iLaneTypeCheck];
          //if ((((HIDWORD(llTrackColorData) ^ (unsigned int)llTrackColorData) - HIDWORD(llTrackColorData)) & 0x20000) != 0)
          if ((TrakColour[pCar->nCurrChunk][iLaneTypeCheck] & SURFACE_FLAG_SKIP_RENDER) != 0)
            converttoair(pCar);
          else
            analysefalloff(pCar);
        }
        if (pCar->iControlType == 3) {
          iYawDifferenceCalc = pCar->nYaw - pCar->nActualYaw;
          iYawDifference = iYawDifferenceCalc + (iYawDifferenceCalc < 0 ? 0x4000 : 0);
          if (iYawDifference > 0x2000)
            iYawDifference -= 0x4000;
          dSpeedAbs = fabs(pCar->fFinalSpeed);
          fCurrentSpeedAbs = (float)dSpeedAbs;
          if (dSpeedAbs <= fSurfaceGripValue) {
            fMaxGripLimit = fHealthAdjustment * 200.0f;
            dGripCalculation = (fCurrentGripMultiplier - fMaxGripLimit) / fSurfaceGripValue * fCurrentSpeedAbs + fMaxGripLimit;
          } else {
            dGripCalculation = (fCurrentSpeedAbs - fSurfaceGripBase) / (fSurfaceGripValue - fSurfaceGripBase) * fCurrentGripMultiplier
              + (fCurrentSpeedAbs - fSurfaceGripValue) / (fSurfaceGripBase - fSurfaceGripValue) * fTempGrip;
          }
          fMaxGripLimit = (float)dGripCalculation;
          fMaxGripLimit = (tsin[abs(iYawDifference)] * 4.0f + 1.0f) * fMaxGripLimit;
          if ((double)iYawDifference > fMaxGripLimit) {
            //_CHP();
            iYawDifference = (int)fMaxGripLimit;
          }
          fNegativeYawLimit = -fMaxGripLimit;
          if ((double)iYawDifference < fNegativeYawLimit) {
            //_CHP();
            iYawDifference = (int)fNegativeYawLimit;
          }
          if (!pCar->iStunned)
            pCar->nActualYaw = ((int16)iYawDifference + (uint16)pCar->nActualYaw) & 0x3FFF;
          iHumanControlIdx = pCar->iDriverIdx;
          if (human_control[iHumanControlIdx]) {
            if (!iJumpMomentum) {
              //if (!pCar->iSteeringInput
              //  || (iInput = (int16)copy_multiple[((int16)readptr - 1) & 0x1FF][iHumanControlIdx].data.unInput,
              //      llInputCheck = (iInput) >> 8,
              //      (int)((HIDWORD(llInputCheck) ^ llInputCheck) - HIDWORD(llInputCheck) - abs32(pCar->iSteeringInput)) < 0)) {
              //TODO check this
              if (!pCar->iSteeringInput || abs((copy_multiple[((int16)readptr - 1) & 0x1FF][iHumanControlIdx].data.unInput) >> 8) < abs(pCar->iSteeringInput)) {
                iCurrentYawForInput = pCar->nYaw;
                iYaw3ForInput = pCar->nActualYaw;
                if (iCurrentYawForInput != iYaw3ForInput) {
                  iHumanYawDiff = iCurrentYawForInput - iYaw3ForInput + (iCurrentYawForInput - iYaw3ForInput < 0 ? 0x4000 : 0);
                  if (iHumanYawDiff > 0x2000)
                    iHumanYawDiff -= 0x4000;
                  if ((double)iHumanYawDiff > fMaxGripLimit) {
                    //_CHP();
                    iHumanYawDiff = (int)fMaxGripLimit;
                  }
                  fNegativeYawLimitHuman = -fMaxGripLimit;
                  if ((double)iHumanYawDiff < fNegativeYawLimitHuman) {
                    //_CHP();
                    iHumanYawDiff = (int)fNegativeYawLimitHuman;
                  }
                  nYawAdjusted = pCar->nYaw - iHumanYawDiff;
                  nYawAdjusted &= 0x3FFF;
                  //HIBYTE(nYawAdjusted) &= 0x3Fu;
                  pCar->nYaw = nYawAdjusted;
                }
              }
            }
          }
        }
        if (pCar->iStunned && pCar->iControlType == 3 || (pCar->byStatusFlags & 4) != 0)// Apply speed reduction when car is stunned or has damage status
        {
          pCar->fFinalSpeed = pCar->fFinalSpeed + -2.0f;
          if (pCar->fFinalSpeed < 0.0)
            pCar->fFinalSpeed = 0.0;
          //if (pCar->nTargetChunk == -1 && (LODWORD(pCar->fFinalSpeed) & 0x7FFFFFFF) == 0)
          if (pCar->nTargetChunk == -1 && fabs(pCar->fFinalSpeed) == 0)
            pCar->nTargetChunk = 72;
          SetEngine(pCar, pCar->fFinalSpeed);
          //if (((LODWORD(pCar->fFinalSpeed) & 0x7FFFFFFF) == 0 || (LODWORD(pCar->fHealth) & 0x7FFFFFFF) == 0 && pCar->nDeathTimer < -54)
          if ((fabs(pCar->fFinalSpeed) == 0 || fabs(pCar->fHealth) == 0 && pCar->nDeathTimer < -54)
            && !pCar->iPitchCameraOffset
            && !pCar->iRollCameraOffset
            && !pCar->nTargetChunk) {
            checkplacement(pCar);
          }
        }
        nTargetChunk = pCar->nTargetChunk;
        if (nTargetChunk > 0)
          pCar->nTargetChunk = nTargetChunk - 1;
        goto LABEL_502;
      }
      //LODWORD(fCameraOscillationRange) = 6 * iSteeringInputProcessed;
      dSteeringSpeedCalc = (double)(6 * iSteeringInputProcessed) * (360.0 - fAbsoluteSpeed) * 0.00027777778 + (double)iSteeringInputProcessed;
    }
    //_CHP();
    iSteeringInputProcessed = (int)dSteeringSpeedCalc;
    goto LABEL_272;
  }
  //if (pCar->nTargetChunk == -1 && (LODWORD(fAbsoluteSpeed) & 0x7FFFFFFF) == 0)
  if (pCar->nTargetChunk == -1 && fabs(fAbsoluteSpeed) == 0)
    pCar->nTargetChunk = 144;
  nTargetChunkLocal = pCar->nTargetChunk;
  if (nTargetChunkLocal > 0)
    pCar->nTargetChunk = nTargetChunkLocal - 1;
  iYaw3ForType2 = iYaw3;
  pCar->pos.fX = pCar->fFinalSpeed * tcos[iYaw3] + pCar->pos.fX;
  pCar->pos.fY = pCar->fFinalSpeed * tsin[iYaw3ForType2] + pCar->pos.fY;
  calculateseparatedcoordinatesystem(pCar->iLastValidChunk, &coordinateSystemData);// Transform car coordinates to track coordinate system
  dCoordY = pCar->pos.fY + coordinateSystemData.pointAy[3].fY;
  dCoordX = pCar->pos.fX + coordinateSystemData.pointAy[3].fX;
  dCoordZ = pCar->pos.fZ + coordinateSystemData.pointAy[3].fZ;
  fCoordTransformX = (float)(coordinateSystemData.pointAy[1].fX * dCoordY + coordinateSystemData.pointAy[0].fX * dCoordX + coordinateSystemData.pointAy[2].fX * dCoordZ);
  fCoordTransformY = (float)(coordinateSystemData.pointAy[1].fY * dCoordY + coordinateSystemData.pointAy[0].fY * dCoordX + coordinateSystemData.pointAy[2].fY * dCoordZ);
  iChunkPlus2 = pCar->iLastValidChunk + 2;
  iChunkPlus3 = pCar->iLastValidChunk + 3;
  fCoordTransformZ = (float)(dCoordY * coordinateSystemData.pointAy[1].fZ + dCoordX * coordinateSystemData.pointAy[0].fZ + dCoordZ * coordinateSystemData.pointAy[2].fZ);
  if (iChunkPlus3 >= TRAK_LEN)
    iChunkPlus3 -= TRAK_LEN;
  if (iChunkPlus2 >= TRAK_LEN)
    iChunkPlus2 -= TRAK_LEN;
  dGroundHeightForward = (GroundPt[iChunkPlus2].pointAy[2].fZ + GroundPt[iChunkPlus2].pointAy[3].fZ + GroundPt[iChunkPlus3].pointAy[2].fZ + GroundPt[iChunkPlus3].pointAy[3].fZ)
    * 0.25;                  // Check ground height ahead to prevent impossible jumps
  dGroundHeightForward = floor(dGroundHeightForward);//_CHP();
  if (dGroundHeightForward > GroundLevel[iChunkPlus2] + 20.0)
    pCar->fFinalSpeed = 0.0;
  iChunkMinus1 = pCar->iLastValidChunk - 1;
  iChunkMinus2 = pCar->iLastValidChunk - 2;
  if (iChunkMinus1 < 0)
    iChunkMinus1 += TRAK_LEN;
  if (iChunkMinus2 < 0)
    iChunkMinus2 += TRAK_LEN;
  dGroundHeightBackward = (GroundPt[iChunkMinus2].pointAy[2].fZ
                         + GroundPt[iChunkMinus2].pointAy[3].fZ
                         + GroundPt[iChunkMinus1].pointAy[2].fZ
                         + GroundPt[iChunkMinus1].pointAy[3].fZ)
    * 0.25;
  dGroundHeightBackward = floor(dGroundHeightBackward);//_CHP();
  if (dGroundHeightBackward > GroundLevel[iChunkMinus2] + 20.0)
    pCar->fFinalSpeed = 0.0;
  if (fCoordTransformY <= 0.0) {
    fGroundAverageZ1 = (GroundPt[pCar->iLastValidChunk].pointAy[3].fY + coordinateSystemData.pointAy[3].fY) * coordinateSystemData.pointAy[1].fY
      + (GroundPt[pCar->iLastValidChunk].pointAy[3].fX + coordinateSystemData.pointAy[3].fX) * coordinateSystemData.pointAy[0].fY
      + (GroundPt[pCar->iLastValidChunk].pointAy[3].fZ + coordinateSystemData.pointAy[3].fZ) * coordinateSystemData.pointAy[2].fY;// Calculate track boundary limits for airborne cars
    if (fCoordTransformY - CarBaseX >= fGroundAverageZ1)
      goto LABEL_493;
    dBoundaryLimit = fGroundAverageZ1 + CarBaseX;
  } else {
    fGroundBoundaryY = (GroundPt[pCar->iLastValidChunk].pointAy[2].fY + coordinateSystemData.pointAy[3].fY) * coordinateSystemData.pointAy[1].fY
      + (GroundPt[pCar->iLastValidChunk].pointAy[2].fX + coordinateSystemData.pointAy[3].fX) * coordinateSystemData.pointAy[0].fY
      + (GroundPt[pCar->iLastValidChunk].pointAy[2].fZ + coordinateSystemData.pointAy[3].fZ) * coordinateSystemData.pointAy[2].fY;
    if (fCoordTransformY + CarBaseX <= fGroundBoundaryY)
      goto LABEL_493;
    dBoundaryLimit = fGroundBoundaryY - CarBaseX;
  }
  pCar->fFinalSpeed = 0.0;
  fCoordTransformY = (float)dBoundaryLimit;
LABEL_493:
  dTransformedY = fCoordTransformY;
  dTransformedX = fCoordTransformX;
  dTransformedZ = fCoordTransformZ;
  pCar->pos.fX = (float)(coordinateSystemData.pointAy[0].fY * fCoordTransformY
    + coordinateSystemData.pointAy[0].fX * fCoordTransformX
    + coordinateSystemData.pointAy[0].fZ * fCoordTransformZ
    - coordinateSystemData.pointAy[3].fX);
  pCar->pos.fY = (float)(coordinateSystemData.pointAy[1].fX * dTransformedX
    + coordinateSystemData.pointAy[1].fY * dTransformedY
    + coordinateSystemData.pointAy[1].fZ * dTransformedZ
    - coordinateSystemData.pointAy[3].fY);
  pCar->pos.fZ = (float)(dTransformedY * coordinateSystemData.pointAy[2].fY
    + dTransformedX * coordinateSystemData.pointAy[2].fX
    + dTransformedZ * coordinateSystemData.pointAy[2].fZ
    - coordinateSystemData.pointAy[3].fZ);
  findnearsection(pCar, &iNearestChunk);
  pCar->fFinalSpeed = pCar->fFinalSpeed + -6.0f;
  if (pCar->fFinalSpeed < 0.0)
    pCar->fFinalSpeed = 0.0;
  fCurrentSpeedFloat = fAbsoluteSpeed;
  SetEngine(pCar, pCar->fFinalSpeed);
  //if (((LODWORD(fCurrentSpeedFloat) & 0x7FFFFFFF) == 0 || (LODWORD(pCar->fHealth) & 0x7FFFFFFF) == 0 && pCar->nDeathTimer < -54)
  if ((fabs(fCurrentSpeedFloat) == 0 || fabs(pCar->fHealth) == 0 && pCar->nDeathTimer < -54)
    && !pCar->iPitchCameraOffset
    && !pCar->iRollCameraOffset
    && !pCar->nTargetChunk) {
  LABEL_501:
    checkplacement(pCar);
  }
LABEL_502:
  if (!pCar->iControlType && pCar->pos.fZ < (double)GroundLevel[pCar->iLastValidChunk] && (char)pCar->byLives > 0) {                                             // Handle fall damage and car landing after being airborne
    if (death_race)
      dFallDamage = fabs(pCar->direction.fZ) * 0.05 * 4.0;
    else
      dFallDamage = fabs(pCar->direction.fZ) * 0.05;
    fDamageAmount = (float)dFallDamage;
    dodamage(pCar, fDamageAmount);
    iGroundChunk = pCar->iLastValidChunk;
    pCar->iControlType = 2;
    pCar->pos.fZ = GroundLevel[iGroundChunk];
    sfxpend(2, pCar->iDriverIdx, 0x8000);
    iPitch = pCar->nPitch;
    iRoll = pCar->nRoll;
    pCar->nActualYaw = pCar->nYaw;
    if (iRoll < 4096 || iRoll > 12288) {
      pCar->nRoll = 0;
      pCar->iStunned = 0;
    } else {
      pCar->iStunned = -1;
      pCar->iSteeringInput = 0;
      pCar->nRoll = 0x2000;
      iRoll = iRoll - 0x2000 + (iRoll - 0x2000 < 0 ? 0x4000 : 0);
      pCar->pos.fZ = CarBox.hitboxAy[pCar->byCarDesignIdx][4].fZ + pCar->pos.fZ;
    }
    if (iPitch > 0x2000)
      iPitch -= 0x4000;
    pCar->iCameraOscillationPhase = 0;
    pCar->iPitchBackup = iPitch;
    pCar->iPitchCameraOffset = iPitch;
    if (iRoll > 0x2000)
      iRoll -= 0x4000;
    pCar->nPitch = 0;
    pCar->iRollDampingMomentum = iRoll;
    pCar->iRollCameraOffset = iRoll;
  }
  if (!finished_car[pCar->iDriverIdx] && pCar->fRunningLapTime < 1000.0)// Update lap timing - running lap time and total race time
  {
    if (race_started) {
      pCar->fRunningLapTime = pCar->fRunningLapTime + 0.02777777777777778f;
      pCar->fTotalRaceTime = pCar->fTotalRaceTime + 0.02777777777777778f;
      if (pCar->fRunningLapTime > 4.0 || pCar->byLap <= 1)
        pCar->fPreviousLapTime = pCar->fRunningLapTime;
    }
  }
}

//-------------------------------------------------------------------------------------------------
//0002F040
void check_crossed_line(tCar *pCar)
{
  int iDriverIdx; // eax
  int16 nCurrChunk; // dx
  int iRacers; // edx
  int iRacePosition; // eax
  int iCurrentChunk; // edx
  int iChunk2; // eax
  int byLap; // edx
  int iChampMode; // esi
  int iDriverIdx2; // eax
  int iTargetDriverIdx; // edx
  int iRacePos; // eax
  int iTargetCar; // ebx
  tCarEngine *pEngine; // eax
  int8 byMaxGear; // dl
  double dPowerCalc; // st7
  int iSoundSample1; // ecx
  int iHumanControl1; // edx
  int iDriverIdx3; // esi
  uint8 byRacePosition; // dl
  int iDriverIdx4; // ebx
  int iCarDesignIdx; // edx
  char *szDriverName; // esi
  char *szRecordName; // edi
  char cChar1; // al
  char cChar2; // al
  int iFastestLapFlag; // esi
  int iCarCounter; // edx
  int iCarOrderIdx; // eax
  int iDriverIdx5; // edx
  int iDriverIdx6; // ebx
  uint8 byRacePos2; // bl
  int iSoundSample2; // eax
  int iDriverIdx7; // ecx
  double dRunningLapTime; // st7
  int iFinishersCount1; // eax
  int iDriverIdx8; // eax
  int iHumanControl2; // ebx
  int iDriverIdx9; // esi
  uint8 byRacePos3; // al
  int iSoundSample3; // eax
  int iFinishersCount2; // eax
  int iDriverIdx10; // eax
  int v44; // edx
  int iDriverIdx11; // ecx
  uint8 byRacePos4; // bh
  int iSoundSample4; // eax
  int iSoundSample5; // [esp+0h] [ebp-20h]
  int iSoundSample6; // [esp+4h] [ebp-1Ch]

  if (champ_mode) {
    iDriverIdx = pCar->iDriverIdx;
    if (champ_go[iDriverIdx])                 // Check if this driver has championship go flag set
    {
      nCurrChunk = pCar->nCurrChunk;
      if (nCurrChunk > 16 && nCurrChunk < 64) // Check if car is in early race chunk range (after start line)
      {
        iRacers = racers;
        champ_go[iDriverIdx] = 0;               // Clear championship go flag for this driver
        iRacePosition = pCar->byRacePosition;
        if (iRacePosition < iRacers - 1) {
          champ_zoom = 0;
          ViewType[0] = carorder[iRacePosition + 1];
          --champ_car;
        }
      }
    }
  }
  iCurrentChunk = pCar->nCurrChunk;             // Check if car has valid current chunk position
  if (iCurrentChunk != -1) {
    iChunk2 = pCar->nReferenceChunk;
    if (iChunk2 != -1) {                                           // Detect forward lap line crossing (crossed finish line going forward)
      if (iChunk2 - iCurrentChunk > TRAK_LEN / 2 && !finished_car[pCar->iDriverIdx]) {                                         // Check if lap count matches expected lap number
        if (pCar->byLap == pCar->byLapNumber) {
          byLap = pCar->byLap;
          if (byLap <= NoOfLaps) {                                     // Record lap time for trial mode (game_type == 2)
            if (game_type == 2)
              trial_times[6 * pCar->iDriverIdx + byLap] = pCar->fRunningLapTime;
            iChampMode = champ_mode;
            ++pCar->byLap;                      // Increment lap counter
            if (iChampMode) {                                   // Handle championship mode camera switching
              if (champ_car) {
                iTargetDriverIdx = pCar->iDriverIdx;
                if (ViewType[0] == iTargetDriverIdx) {
                  iRacePos = 0;// iTargetDriverIdx ^ViewType[0];
                  iRacePos = pCar->byRacePosition;
                  //LOBYTE(iRacePos) = pCar->byRacePosition;
                  if (iRacePos < racers - 1) {
                    iTargetCar = carorder[iRacePos + 1];
                    champ_go[iTargetCar] = -1;
                    //iTargetCar *= sizeof(tCar);
                    pEngine = &CarEngines.engines[Car[iTargetCar].byCarDesignIdx];
                    Car[iTargetCar].fRPMRatio = 1.0;
                    byMaxGear = (uint8)(pEngine->iNumGears) - 1;
                    Car[iTargetCar].byGearAyMax = byMaxGear;
                    Car[iTargetCar].fFinalSpeed = pEngine->pSpds[byMaxGear];
                    Car[iTargetCar].fBaseSpeed = Car[iTargetCar].fFinalSpeed;
                    dPowerCalc = calc_pow(
                                   Car[iTargetCar].byCarDesignIdx,
                                   Car[iTargetCar].byGearAyMax,
                                   Car[iTargetCar].fRPMRatio);
                    Car[iTargetCar].fSpeedOverflow = 0.0;
                    Car[iTargetCar].byEngineStartTimer = 36;
                    Car[iTargetCar].byThrottlePressed = -1;
                    Car[iTargetCar].fPower = (float)dPowerCalc;
                  }
                }
              } else {
                iDriverIdx2 = pCar->iDriverIdx;
                champ_mode = 2;
                champ_go[iDriverIdx2] = 0;
              }
            }
            iSoundSample1 = -1;
            iSoundSample5 = -1;
            iSoundSample6 = -1;
            if (pCar->byLap > NoOfLaps && NoOfLaps)// Check if race is finished (exceeded total lap count)
            {
              finished_car[pCar->iDriverIdx] = -1;// Mark car as finished and increment finisher count
              iHumanControl1 = human_control[pCar->iDriverIdx];
              ++finishers;
              if (iHumanControl1)
                ++human_finishers;
              iDriverIdx3 = pCar->iDriverIdx;
              if (player1_car == iDriverIdx3 || player2_car == iDriverIdx3)// Handle finish sounds for player cars based on position
              {
                if ((char)pCar->byLives > 0) {
                  byRacePosition = pCar->byRacePosition;
                  if (byRacePosition) {
                    if (byRacePosition >= 3u) {
                      if (byRacePosition >= 0xDu)
                        iSoundSample6 = SOUND_SAMPLE_RUBBISH;     // SOUND_SAMPLE_RUBBISH
                      else
                        iSoundSample6 = SOUND_SAMPLE_POOR;     // SOUND_SAMPLE_POOR
                    } else {
                      iSoundSample6 = SOUND_SAMPLE_HARDER;       // SOUND_SAMPLE_HARDER
                    }
                  } else {
                    iSoundSample6 = SOUND_SAMPLE_WINNER;         // SOUND_SAMPLE_WINNER
                  }
                  iSoundSample5 = SOUND_SAMPLE_RACEOVER;           // SOUND_SAMPLE_RACEOVER
                }
              } else if (player_type != 2) {
                if (pCar->byRacePosition)
                  small_zoom(&language_buffer[64 * pCar->byRacePosition + 384]);
                else
                  start_zoom(&language_buffer[2560], 0);
                subzoom(driver_names[pCar->iDriverIdx]);
              }
            }
            if (pCar->fRunningLapTime < (double)pCar->fBestLapTime && pCar->byLap > 1)// Check if this lap is a new personal best
            {
              pCar->fBestLapTime = pCar->fRunningLapTime;
              if (pCar->fBestLapTime >= (double)RecordLaps[game_track])// Check if lap time beats track record
              {
                iFastestLapFlag = -1;
                if (racers > 0)               // Check if this is fastest lap among all drivers
                {
                  iCarCounter = 0;
                  do {
                    iCarOrderIdx = carorder[iCarCounter];
                    if (iCarOrderIdx != pCar->iDriverIdx && Car[iCarOrderIdx].fBestLapTime <= (double)pCar->fBestLapTime)
                      iFastestLapFlag = 0;
                    ++iCarCounter;
                  } while (iCarCounter < racers);
                }
                iDriverIdx5 = pCar->iDriverIdx;
                if (player1_car == iDriverIdx5 || player2_car == iDriverIdx5) {
                  if (pCar->byLap > 2) {
                    if (iFastestLapFlag)
                      iSoundSample1 = 40;
                    else
                      iSoundSample1 = 39;
                  }
                } else if (iFastestLapFlag && pCar->byLap > 2) {
                  sprintf(buffer, "%s %s", &language_buffer[2880], driver_names[iDriverIdx5]);
                  small_zoom(buffer);
                  make_time(buffer, pCar->fBestLapTime);
                  subzoom(buffer);
                }
              } else {
                iDriverIdx4 = pCar->iDriverIdx;
                if (player1_car == iDriverIdx4 || player2_car == iDriverIdx4) {
                  if (pCar->fBestLapTime >= RecordLaps[game_track] + -0.5)
                    iSoundSample1 = 37;
                  else
                    iSoundSample1 = 38;
                } else if (pCar->byLap > 2) {
                  sprintf(buffer, "%s %s", &language_buffer[2752], driver_names[iDriverIdx4]);
                  small_zoom(buffer);
                  make_time(buffer, pCar->fBestLapTime);
                  subzoom(buffer);
                }
                if ((textures_off & TEX_OFF_ADVANCED_CARS) != 0)
                  iCarDesignIdx = pCar->byCarDesignIdx + 16;
                else
                  iCarDesignIdx = pCar->byCarDesignIdx;
                RecordCars[game_track] = iCarDesignIdx;
                RecordLaps[game_track] = pCar->fBestLapTime;
                szDriverName = driver_names[pCar->iDriverIdx];
                szRecordName = RecordNames[game_track];
                do {
                  cChar1 = *szDriverName;
                  *szRecordName = *szDriverName;
                  if (!cChar1)
                    break;
                  cChar2 = szDriverName[1];
                  szDriverName += 2;
                  szRecordName[1] = cChar2;
                  szRecordName += 2;
                } while (cChar2);
              }
            }
            if (iSoundSample1 != -1)
              speechsample(iSoundSample1, 0x8000, 18, pCar->iDriverIdx);
            if (iSoundSample6 != -1)
              speechsample(iSoundSample6, 0x8000, 18, pCar->iDriverIdx);
            if (iSoundSample5 != -1)
              speechsample(iSoundSample5, 0x8000, 18, pCar->iDriverIdx);
            if (pCar->byLap != 1) {
              iDriverIdx6 = pCar->iDriverIdx;
              if ((player1_car == iDriverIdx6 || player2_car == iDriverIdx6) && finishers - Destroyed <= 0 && !finished_car[pCar->iDriverIdx] && competitors > 1) {                                 // Announce remaining laps to player cars
                switch (NoOfLaps - pCar->byLap) {
                  case 0:
                    byRacePos2 = pCar->byRacePosition;
                    if (byRacePos2) {
                      if (byRacePos2 >= 5u) {
                        if (!SamplePtr[16])
                          goto LABEL_102;
                        iSoundSample2 = SOUND_SAMPLE_FINAL1;     // SOUND_SAMPLE_FINAL1
                      } else {
                        if (!SamplePtr[17])
                          goto LABEL_102;
                        iSoundSample2 = SOUND_SAMPLE_FINAL2;     // SOUND_SAMPLE_FINAL2
                      }
                      break;
                    }
                    if (!SamplePtr[18])
                      goto LABEL_102;
                    iSoundSample2 = SOUND_SAMPLE_FINAL3;         // SOUND_SAMPLE_FINAL3
                    iDriverIdx7 = pCar->iDriverIdx;
                    goto LABEL_104;
                  case 1:
                    if (!SamplePtr[53])
                      goto LABEL_102;
                    iSoundSample2 = SOUND_SAMPLE_2LAPS;         // SOUND_SAMPLE_2LAPS
                    break;
                  case 2:
                    if (!SamplePtr[54])
                      goto LABEL_102;
                    iSoundSample2 = SOUND_SAMPLE_3LAPS;         // SOUND_SAMPLE_3LAPS
                    break;
                  case 3:
                    if (!SamplePtr[55])
                      goto LABEL_102;
                    iSoundSample2 = SOUND_SAMPLE_4LAPS;         // SOUND_SAMPLE_4LAPS
                    break;
                  case 4:
                    if (!SamplePtr[56])
                      goto LABEL_102;
                    iSoundSample2 = SOUND_SAMPLE_5LAPS;         // SOUND_SAMPLE_5LAPS
                    break;
                  default:
                  LABEL_102:
                    iSoundSample2 = SOUND_SAMPLE_KEEPGO;         // SOUND_SAMPLE_KEEPGO
                    break;
                }
                iDriverIdx7 = pCar->iDriverIdx;
              LABEL_104:
                speechsample(iSoundSample2, 0x8000, 18, iDriverIdx7);
              }
            }
            dRunningLapTime = pCar->fRunningLapTime;
            pCar->fRunningLapTime = 0.0;
            pCar->fPreviousLapTime = (float)dRunningLapTime;
          }
        }
        iFinishersCount1 = finishers - Destroyed;
        ++pCar->byLapNumber;
        if (iFinishersCount1 > 0) {
          iDriverIdx8 = pCar->iDriverIdx;
          if (!finished_car[iDriverIdx8] && competitors > 1) {
            finished_car[iDriverIdx8] = -1;
            iHumanControl2 = human_control[pCar->iDriverIdx];
            ++finishers;
            if (iHumanControl2)
              ++human_finishers;
            iDriverIdx9 = pCar->iDriverIdx;
            if ((player1_car == iDriverIdx9 || player2_car == iDriverIdx9) && (char)pCar->byLives > 0) {
              byRacePos3 = pCar->byRacePosition;
              if (byRacePos3) {
                if (byRacePos3 >= 3u) {
                  if (byRacePos3 >= 0xDu)
                    iSoundSample3 = SOUND_SAMPLE_RUBBISH;         // SOUND_SAMPLE_RUBBISH
                  else
                    iSoundSample3 = SOUND_SAMPLE_POOR;         // SOUND_SAMPLE_POOR
                } else {
                  iSoundSample3 = SOUND_SAMPLE_HARDER;           // SOUND_SAMPLE_HARDER
                }
              } else {
                iSoundSample3 = SOUND_SAMPLE_WINNER;             // SOUND_SAMPLE_WINNER
              }
              speechsample(iSoundSample3, 0x8000, 18, pCar->iDriverIdx);
              speechsample(SOUND_SAMPLE_RACEOVER, 0x8000, 18, pCar->iDriverIdx);// SOUND_SAMPLE_RACEOVER
            }
          }
        }
      }
      if (pCar->nReferenceChunk - pCar->nCurrChunk < -TRAK_LEN / 2)// Detect backward lap line crossing (went backwards across finish line)
      {
        iFinishersCount2 = finishers;
        --pCar->byLapNumber;
        if (iFinishersCount2 - Destroyed > 0) {
          iDriverIdx10 = pCar->iDriverIdx;
          if (!finished_car[iDriverIdx10] && competitors > 1) {
            finished_car[iDriverIdx10] = -1;
            v44 = human_control[pCar->iDriverIdx];
            ++finishers;
            if (v44)
              ++human_finishers;
            iDriverIdx11 = pCar->iDriverIdx;
            if ((player1_car == iDriverIdx11 || player2_car == iDriverIdx11) && (char)pCar->byLives > 0) {
              byRacePos4 = pCar->byRacePosition;
              if (byRacePos4) {
                if (byRacePos4 >= 3u) {
                  if (byRacePos4 >= 0xDu)
                    iSoundSample4 = SOUND_SAMPLE_RUBBISH;         // SOUND_SAMPLE_RUBBISH
                  else
                    iSoundSample4 = SOUND_SAMPLE_POOR;         // SOUND_SAMPLE_POOR
                } else {
                  iSoundSample4 = SOUND_SAMPLE_HARDER;           // SOUND_SAMPLE_HARDER
                }
              } else {
                iSoundSample4 = SOUND_SAMPLE_WINNER;             // SOUND_SAMPLE_WINNER
              }
              speechsample(iSoundSample4, 0x8000, 18, pCar->iDriverIdx);
              speechsample(SOUND_SAMPLE_RACEOVER, 0x8000, 18, pCar->iDriverIdx);// SOUND_SAMPLE_RACEOVER
            }
          }
        }
      }
      if ((pCar->byLapNumber & 0x80u) != 0)   // Prevent lap number underflow (reset to 0 if went negative)
        pCar->byLapNumber = 0;
    }
  }
}

//-------------------------------------------------------------------------------------------------
//0002F920
void checkplacement(tCar *pCar)
{
  int iRandValue; // eax
  int iNewChunk; // edi
  int iPlacementResult; // ebp
  int16 nCurrChunk; // ax
  int iRandValue2; // eax
  unsigned int uiLaneType; // ebx
  int iCarArrayIdx; // edx
  int iChunkDiff; // ecx
  int iNormalizedChunkDiff; // eax
  double dLanePosition; // st7
  int iSelectedView; // eax
  int iMaskedDriverIdx; // eax
  int iMaskedViewType; // edx
  int iCurrentChunk; // eax
  tData *pDataAy; // eax
  double dPosZ; // st7
  int iCurrentViewType; // eax
  int iRespawnRand; // eax
  int iCarLoopIdx; // edx
  int iCarArrayIdx2; // eax
  //int i; // eax
  int iDriverIdx; // edi
  //int iSprayOffset1; // eax
  int iDriverIdx2; // edx
  //int iSprayOffset2; // eax
  int8 byPreviousLap; // ch
  int iPlayer1Car; // eax
  int iCurrentDriverIdx; // edx
  uint8 byLives; // al
  int iSpeechSample; // eax
  int iViewType; // eax
  int iPlayerNum; // edx
  int iCalcViewType; // eax
  uint8 byUnk64; // dl
  int16 iBackupChunk; // [esp+0h] [ebp-34h]
  float fX; // [esp+4h] [ebp-30h]
  float fY; // [esp+8h] [ebp-2Ch]
  int iTempChunk; // [esp+10h] [ebp-24h]
  int iRespawnChunk; // [esp+14h] [ebp-20h]
  int iCarCounter; // [esp+18h] [ebp-1Ch]

  iRandValue = rand();                          // Generate random chunk position near current chunk
  iNewChunk = GetHighOrderRand(10, iRandValue) + pCar->nReferenceChunk;
  if (iNewChunk >= TRAK_LEN)
    iNewChunk = TRAK_LEN - 1;
  if (iNewChunk == -1)
    iNewChunk = 0;
  //if ((LODWORD(pCar->fHealth) & 0x7FFFFFFF) == 0 && !numstops)// Reset health to 100 if car has zero health and there's no pit boxes
  if (fabs(pCar->fHealth) == 0 && !numstops)// Reset health to 100 if car has zero health and there's no pit boxes
    pCar->fHealth = 100.0;
  //if ((LODWORD(pCar->fHealth) & 0x7FFFFFFF) != 0)// Handle placement for cars with health (alive cars)
  if (fabs(pCar->fHealth) > FLT_EPSILON)// Handle placement for cars with health (alive cars)
  {                                             // Find a valid track chunk
    while ((TrakColour[iNewChunk][0] & (SURFACE_FLAG_NO_SPAWN | SURFACE_FLAG_PIT | SURFACE_FLAG_WALL_22 | SURFACE_FLAG_SKIP_RENDER)) != 0
           && (TrakColour[iNewChunk][1] & (SURFACE_FLAG_NO_SPAWN | SURFACE_FLAG_PIT | SURFACE_FLAG_WALL_22 | SURFACE_FLAG_SKIP_RENDER)) != 0
           && (TrakColour[iNewChunk][2] & (SURFACE_FLAG_NO_SPAWN | SURFACE_FLAG_PIT | SURFACE_FLAG_WALL_22 | SURFACE_FLAG_SKIP_RENDER)) != 0) {
      if (++iNewChunk == TRAK_LEN)
        iNewChunk ^= TRAK_LEN;
    }
    iPlacementResult = -1;
    fX = pCar->pos.fX;
    pCar->pos.fX = 0.0;
    fY = pCar->pos.fY;
    nCurrChunk = pCar->nCurrChunk;
    pCar->pos.fY = 0.0;
    iBackupChunk = nCurrChunk;
    pCar->nCurrChunk = iNewChunk;
    do {
      iRandValue2 = rand();                     // Generate random lane (0-2)
      uiLaneType = GetHighOrderRand(3, iRandValue2);
    } while ((TrakColour[iNewChunk][uiLaneType] & (SURFACE_FLAG_NO_SPAWN | SURFACE_FLAG_PIT | SURFACE_FLAG_WALL_22 | SURFACE_FLAG_SKIP_RENDER)) != 0);
    iCarCounter = 0;
    if (numcars > 0)                          // Check for collision with other cars in same lane
    {
      iCarArrayIdx = 0;
      do {
        if (iCarCounter != pCar->iDriverIdx && Car[iCarArrayIdx].iControlType == 3 && uiLaneType == Car[iCarArrayIdx].iLaneType) {
          iChunkDiff = iNewChunk - Car[iCarArrayIdx].nCurrChunk;
          iNormalizedChunkDiff = iChunkDiff;
          if (iChunkDiff < 0)
            iNormalizedChunkDiff = TRAK_LEN + iChunkDiff;
          if (iNormalizedChunkDiff > TRAK_LEN / 2)
            iNormalizedChunkDiff -= TRAK_LEN;
          if (iNormalizedChunkDiff <= 8 && iNormalizedChunkDiff >= -2)
            iPlacementResult = 0;
        }
        ++iCarCounter;
        ++iCarArrayIdx;
      } while (iCarCounter < numcars);
    }
    if (!iPlacementResult)
      goto LABEL_39;
    if (uiLaneType)                           // Set Y position based on lane type (0=left shoulder, 1=center, 2=right shoulder)
    {
      if (uiLaneType <= 1) {
        pCar->pos.fY = 0.0;
      LABEL_38:
        pCar->iLaneType = uiLaneType;
      LABEL_39:
        if (!iPlacementResult) {
          pCar->pos.fX = fX;
          pCar->pos.fY = fY;
          pCar->nCurrChunk = iBackupChunk;
        }
        goto LABEL_113;
      }
      if (uiLaneType != 2)
        goto LABEL_38;
      dLanePosition = -localdata[iNewChunk].fTrackHalfWidth - TrackInfo[iNewChunk].fRShoulderWidth * 0.5f;
    } else {
      dLanePosition = TrackInfo[iNewChunk].fLShoulderWidth * 0.5f + localdata[iNewChunk].fTrackHalfWidth;
    }
    pCar->pos.fY = (float)dLanePosition;
    goto LABEL_38;
  }
  iPlacementResult = 0;
  if ((network_on || (char)pCar->byLives > 0 || lastsample >= -144 || pCar->nDeathTimer >= -64) && (!network_on || (char)pCar->byLives > 0 || pCar->nDeathTimer >= 0)) {                                             // Handle respawn for dead cars (health=0, has lives left)
    if (pCar->nDeathTimer < 0 && (char)pCar->byLives > 0) {
      iRespawnRand = rand();                    // Randomly select a respawn stop position
      iPlacementResult = -1;
      iCarLoopIdx = 0;
      iRespawnChunk = stops[GetHighOrderRand(numstops, iRespawnRand)];
      if (numcars > 0) {
        iCarArrayIdx2 = 0;
        do {
          if (iCarLoopIdx != pCar->iDriverIdx && (char)Car[iCarArrayIdx2].byLives > 0 && Car[iCarArrayIdx2].iControlType == 3) {
            iTempChunk = Car[iCarArrayIdx2].nCurrChunk;
            if ((TrakColour[iTempChunk][Car[iCarArrayIdx2].iLaneType] & SURFACE_FLAG_PIT) != 0) {
              if (iTempChunk == iRespawnChunk)
                iPlacementResult = 0;
              if (Car[iCarArrayIdx2].nCurrChunk - 1 == iRespawnChunk)
                iPlacementResult = 0;
              if (Car[iCarArrayIdx2].nCurrChunk + 1 == iRespawnChunk)
                iPlacementResult = 0;
            }
          }
          ++iCarLoopIdx;
          ++iCarArrayIdx2;
        } while (iCarLoopIdx < numcars);
      }
      if (iPlacementResult) {
        //TODO may need to look at this further
        for (int j = 0; j < 32; j++)
          CarSpray[pCar->iDriverIdx][j].fSize = 0.0f;
        //for (i = 0; i != 352; car_texs_loaded[352 * pCar->iDriverIdx + 12 + i] = 0)
        //  i += 11;

        if (player_type == 2) {
          iDriverIdx = pCar->iDriverIdx;
          if (player1_car == iDriverIdx) {
            // Clear iLifeTime for all CarSpray objects for driver 16
            for (int j = 0; j < 32; j++) {
                CarSpray[16][j].iLifeTime = 0;
            }
            //iSprayOffset1 = iDriverIdx ^ player1_car;
            //do {
            //  iSprayOffset1 += 352;
            //  *(int *)((char *)&CarSpray[15][24].iLifeTime + iSprayOffset1) = 0;
            //  *(int *)((char *)&CarSpray[15][25].iLifeTime + iSprayOffset1) = 0;
            //  *(int *)((char *)&CarSpray[15][26].iLifeTime + iSprayOffset1) = 0;
            //  *(int *)((char *)&CarSpray[15][27].iLifeTime + iSprayOffset1) = 0;
            //  *(int *)((char *)&CarSpray[15][28].iLifeTime + iSprayOffset1) = 0;
            //  *(int *)((char *)&CarSpray[15][29].iLifeTime + iSprayOffset1) = 0;
            //  *(int *)((char *)&CarSpray[15][30].iLifeTime + iSprayOffset1) = 0;
            //  *(int *)((char *)&CarSpray[15][31].iLifeTime + iSprayOffset1) = 0;
            //} while (iSprayOffset1 != 1408);
          }
          iDriverIdx2 = pCar->iDriverIdx;
          if (player2_car == iDriverIdx2) {
            // Clear iLifeTime for all CarSpray objects for driver 17
            for (int j = 0; j < 32; j++) {
                CarSpray[17][j].iLifeTime = 0;
            }
            //iSprayOffset2 = iDriverIdx2 ^ player2_car;
            //do {
            //  iSprayOffset2 += 352;
            //  *(int *)((char *)&CarSpray[16][24].iLifeTime + iSprayOffset2) = 0;
            //  *(int *)((char *)&CarSpray[16][25].iLifeTime + iSprayOffset2) = 0;
            //  *(int *)((char *)&CarSpray[16][26].iLifeTime + iSprayOffset2) = 0;
            //  *(int *)((char *)&CarSpray[16][27].iLifeTime + iSprayOffset2) = 0;
            //  *(int *)((char *)&CarSpray[16][28].iLifeTime + iSprayOffset2) = 0;
            //  *(int *)((char *)&CarSpray[16][29].iLifeTime + iSprayOffset2) = 0;
            //  *(int *)((char *)&CarSpray[16][30].iLifeTime + iSprayOffset2) = 0;
            //  *(int *)((char *)&CarSpray[16][31].iLifeTime + iSprayOffset2) = 0;
            //} while (iSprayOffset2 != 1408);
          }
        }
        if (player1_car == pCar->iDriverIdx && SelectedView[0] != DeathView[0]) {
          SelectedView[0] = DeathView[0];
          DeathView[0] = -1;
          select_view(0);
          initcarview(ViewType[0], 0);
        }
        if (player2_car == pCar->iDriverIdx && SelectedView[1] != DeathView[1]) {
          SelectedView[1] = DeathView[1];
          DeathView[1] = -1;
          select_view(1);
          initcarview(ViewType[1], 1);
        }
        iNewChunk = iRespawnChunk;
        pCar->pos.fY = localdata[iRespawnChunk].fAILine1;
        if (pCar->nCurrChunk < stops[numstops - 1] - 10) {
        //if (pCar->nCurrChunk < carorder[numstops + 15] - 10) {
          byPreviousLap = pCar->byLapNumber - 1;
          pCar->byLapNumber = byPreviousLap;
          if (byPreviousLap < 0)
            pCar->byLapNumber = 0;
        }
        pCar->fHealth = 100.0;
        iPlayer1Car = player1_car;
        iCurrentDriverIdx = pCar->iDriverIdx;
        pCar->byCheatAmmo = 8;
        if (iPlayer1Car == iCurrentDriverIdx || player2_car == iCurrentDriverIdx) {
          byLives = pCar->byLives;
          if (byLives) {
            if (byLives <= 1u) {
              iSpeechSample = SOUND_SAMPLE_1LEFT;               // SOUND_SAMPLE_1LEFT
            } else {
              if (byLives != 2)
                goto LABEL_113;
              iSpeechSample = SOUND_SAMPLE_2LEFT;               // SOUND_SAMPLE_2LEFT
            }
          } else {
            iSpeechSample = SOUND_SAMPLE_0LEFT;                 // SOUND_SAMPLE_0LEFT
          }
          speechsample(iSpeechSample, 0x8000, 18, pCar->iDriverIdx);
        }
      }
    }
  } else {
    iSelectedView = SelectedView[0];            // Handle permanently dead cars (no lives left)
    pCar->byLives = -1;
    if (iSelectedView == 7) {
      iMaskedDriverIdx = pCar->iDriverIdx & 0xFE;
      iMaskedViewType = ViewType[0] & 0xFE;
      if ((uint8)iMaskedViewType == iMaskedDriverIdx) {
        SelectedView[0] = iMaskedDriverIdx ^ iMaskedViewType;
        select_view(0);
        initcarview(ViewType[0], 0);
      }
    }
    if (human_control[pCar->iDriverIdx])
      ++dead_humans;
    if (dead_humans < players) {
      iCurrentChunk = pCar->nCurrChunk;
      pCar->iControlType = 0;
      if (iCurrentChunk == -1) {
        pCar->pos.fX = 0.0f;
        pCar->pos.fY = 0.0f;
        pCar->pos.fZ = 1000.0f;
      } else {
        pDataAy = &localdata[iCurrentChunk];
        pCar->pos.fX = 0.0f * pDataAy->pointAy[0].fY + 0.0f * pDataAy->pointAy[0].fX + 0.0f * pDataAy->pointAy[0].fZ - pDataAy->pointAy[3].fX;
        pCar->pos.fY = 0.0f * pDataAy->pointAy[1].fX + 0.0f * pDataAy->pointAy[1].fY + 0.0f * pDataAy->pointAy[1].fZ - pDataAy->pointAy[3].fY;
        dPosZ = 0.0 * pDataAy->pointAy[2].fX + 0.0 * pDataAy->pointAy[2].fY + 0.0 * pDataAy->pointAy[2].fZ - pDataAy->pointAy[3].fZ + 1000.0;
        pCar->nReferenceChunk = pCar->nCurrChunk;
        pCar->pos.fZ = (float)dPosZ;
      }
      iCurrentViewType = ViewType[0];
      pCar->nCurrChunk = -1;
      pCar->fFinalSpeed = 0.0;
      pCar->fBaseSpeed = 0.0;
      pCar->fSpeedOverflow = 0.0;
      pCar->byGearAyMax = 0;
      pCar->fRPMRatio = 0.0;
      pCar->fPower = 0.0;
      if ((Car[iCurrentViewType].byLives & 0x80u) != 0) {
        for (ViewType[0] = 0; !human_control[ViewType[0]] || (Car[ViewType[0]].byLives & 0x80u) != 0; ++ViewType[0])
          ;
        initcarview(ViewType[0], 0);
        stopallsamples();
      }
      if ((Car[ViewType[1]].byLives & 0x80u) != 0) {
        for (ViewType[1] = 0; !human_control[ViewType[1]] || (Car[ViewType[1]].byLives & 0x80u) != 0; ++ViewType[1])
          ;
        initcarview(ViewType[1], 0);
        stopallsamples();
      }
    }
  }
LABEL_113:
  if (iPlacementResult && (char)pCar->byLives > 0)// Initialize car state after successful placement
  {
    pCar->pos.fX = 0.0;
    pCar->pos.fZ = 0.0;
    pCar->nPitch = 0;
    pCar->nYaw = 0;
    pCar->nActualYaw = 0;
    pCar->nRoll = 0;
    pCar->byDamageSourceTimer = 0;
    pCar->iControlType = 3;
    pCar->iJumpMomentum = 0;
    pCar->iSelectedStrategy = 0;
    pCar->nChangeMateCooldown = 1080;
    pCar->iBobMode = 0;
    pCar->iStunned = 0;
    pCar->iPitchBackup = 0;
    pCar->iCameraOscillationPhase = 0;
    pCar->iPitchCameraOffset = 0;
    pCar->iRollDampingMomentum = 0;
    pCar->iRollCameraOffset = 0;
    pCar->iPitchDynamicOffset = 0;
    pCar->iRollDynamicOffset = 0;
    pCar->iAIUpdateTimer = 0;
    pCar->iAITargetCar = -1;
    pCar->fFinalSpeed = 0.0;
    pCar->fPower = 0.0;
    pCar->fRPMRatio = 0.0;
    pCar->fSpeedOverflow = 0.0;
    pCar->fBaseSpeed = 0.0;
    pCar->byGearAyMax = 0;
    pCar->nReverseWarnCooldown = 0;
    pCar->nTargetChunk = -1;
    pCar->byThrottlePressed = 0;
    pCar->byAccelerating = 0;
    pCar->byEngineStartTimer = 0;
    pCar->nCurrChunk = iNewChunk;
    pCar->iLastValidChunk = iNewChunk;
    pCar->nReferenceChunk = iNewChunk;
    pCar->iPitchDynamicOffset = 0;
    pCar->iRollDynamicOffset = 0;
    putflat(pCar);                              // Reset car to flat ground position and update camera views
    pCar->iDamageState = 1 - pCar->iDamageState;
    if (Play_View == 1) {
      if ((ViewType[0] & 1) != 0)
        iCalcViewType = ViewType[0] - 1;
      else
        iCalcViewType = ViewType[0] + 1;
      if (iCalcViewType != pCar->iDriverIdx)
        goto LABEL_126;
      iPlayerNum = 0;
      iViewType = -iCalcViewType - 1;
    } else {
      if (ViewType[0] == pCar->iDriverIdx)
        initcarview(ViewType[0], 0);
      iViewType = ViewType[1];
      if (ViewType[1] != pCar->iDriverIdx)
        goto LABEL_126;
      iPlayerNum = 1;
    }
    initcarview(iViewType, iPlayerNum);
  LABEL_126:
    pCar->nLastCommentaryChunk = -1;
    pCar->byStatusFlags = 0;
    pCar->bySfxCooldown = 0;
    byUnk64 = pCar->byDamageToggle;
    pCar->byCollisionTimer = 0;
    pCar->byDamageToggle = 1 - byUnk64;
  }
}

//-------------------------------------------------------------------------------------------------
//00030200
void testteaminit(tCar *pCar)
{
  int iViewType; // eax

  if ((ViewType[0] & 1) != 0)
    iViewType = ViewType[0] - 1;
  else
    iViewType = ViewType[0] + 1;
  if (iViewType == pCar->iDriverIdx)
    initcarview(-iViewType - 1, 0);
}

//-------------------------------------------------------------------------------------------------
//00030230
void doteaminit()
{
  int iViewType; // eax

  if ((ViewType[0] & 1) != 0)
    iViewType = ViewType[0] - 1;
  else
    iViewType = ViewType[0] + 1;
  initcarview(-iViewType - 1, 0);
}

//-------------------------------------------------------------------------------------------------
//00030260
void hitleft(tCar *pCar, int iSampleIdx, int iIsRightSide)
{                                               // Calculate maximum yaw change based on speed (capped at 500.0)
  double dSpeedScale; // st7
  int iYaw3; // eax
  int iCollisionAngle; // eax
  double dDamageAmount; // st7
  double dVelX; // st7
  double dVelY; // st6
  int iAngleIdx; // eax
  int iNewYaw; // eax
  //int16 nFpuTemp1; // fps
  double dAngleCalc; // st7
  double dFinalSpeed; // st7
  int iReverseYaw; // eax
  //int16 nFpuTemp2; // fps
  double dReverseAngle; // st7
  int iYawDiff; // edx
  int iConstrainedYaw; // eax
  int16 nNewYawValue; // ax
  float fDamageFloat; // [esp+0h] [ebp-44h]
  float fFinalSpeed; // [esp+0h] [ebp-44h]
  float fNegVelX; // [esp+4h] [ebp-40h]
  float fNegVelY; // [esp+18h] [ebp-2Ch]
  int iMaxYawChange; // [esp+1Ch] [ebp-28h]
  float fVelX; // [esp+20h] [ebp-24h]
  float fTangentialVel; // [esp+24h] [ebp-20h]
  float fVelY; // [esp+28h] [ebp-1Ch]
  float fFinalVelX; // [esp+2Ch] [ebp-18h]
  float fFinalVelY; // [esp+30h] [ebp-14h]
  float fPerpVel; // [esp+34h] [ebp-10h]
  float fNegPerpVel; // [esp+34h] [ebp-10h]

  if (pCar->fFinalSpeed >= 500.0) {
    iMaxYawChange = 512;
  } else {
    dSpeedScale = pCar->fFinalSpeed * 512.0 * 0.0020000001;
    //_CHP();
    iMaxYawChange = (int)dSpeedScale;
  }
  iYaw3 = pCar->nActualYaw;
  fVelX = tcos[iYaw3] * pCar->fFinalSpeed;      // Calculate velocity components from car's current speed and direction
  fVelY = tsin[iYaw3] * pCar->fFinalSpeed;
  if (iIsRightSide)                           // Select collision angle based on left/right side (a3 parameter)
    iCollisionAngle = rightang;
  else
    iCollisionAngle = leftang;
  fTangentialVel = fVelX * tcos[iCollisionAngle] + fVelY * tsin[iCollisionAngle];// Transform velocity vector to collision surface coordinate system
  fPerpVel = -fVelX * tsin[iCollisionAngle] + fVelY * tcos[iCollisionAngle];
  if (!pCar->byCollisionTimer)                // Check if car is not already in collision state (byUnk63 flag)
  {                                             // If velocity into wall is positive, play collision sound and calculate damage
    if (fPerpVel > 0.0) {
      //_CHP();
      sfxpend(iSampleIdx, pCar->iDriverIdx, (int)(fVelY * 32768.0 * 0.0049999999));
      if (death_race)
        dDamageAmount = fVelY * 0.006 * 4.0;
      else
        dDamageAmount = fVelY * 0.006;
      fDamageFloat = (float)dDamageAmount;
      dodamage(pCar, fDamageFloat);
    }
    fPerpVel = fPerpVel * -0.6f;                 // Apply collision physics - reverse perpendicular velocity with damping
    fTangentialVel = fTangentialVel * 0.8f;
  }
  //_CHP();
  fNegPerpVel = (float)-abs((int)fPerpVel);
  dVelX = fTangentialVel;                                  // Transform velocity back to world coordinates after collision response
  dVelY = fTangentialVel;
  if (iIsRightSide)
    iAngleIdx = rightang;
  else
    iAngleIdx = leftang;
  fFinalVelX = (float)dVelY * tcos[iAngleIdx] - fNegPerpVel * tsin[iAngleIdx];
  fFinalVelY = (float)dVelX * tsin[iAngleIdx] + fNegPerpVel * tcos[iAngleIdx];
  if (pCar->fHealth <= 0.0 && fFinalVelY == -40.0)// Special case - if car is dead and velocity is exactly -40, stop completely
  {
    pCar->fFinalSpeed = 0.0;
  } else {                                             // Handle reverse motion - calculate new direction and speed
    if (pCar->fFinalSpeed < 0.0) {
      fNegVelX = -fFinalVelX;
      if (fabs(fNegVelX) > FLT_EPSILON || fabs(-fFinalVelY)) {
      //if ((LODWORD(fNegVelX) & 0x7FFFFFFF) != 0 || fabs(-fFinalVelY)) {
        fNegVelY = -fFinalVelY;
        dReverseAngle = atan2(fNegVelY, fNegVelX) * 16384.0 / 6.28318530718;
        //dReverseAngle = IF_DATAN2(nFpuTemp2, fNegVelY, fNegVelX) * 16384.0 / 6.28318530718;
        //_CHP();
        iReverseYaw = (int)dReverseAngle & 0x3FFF;
      } else {
        iReverseYaw = 0;
      }
      pCar->nActualYaw = iReverseYaw;
      dFinalSpeed = -sqrt(fFinalVelX * fFinalVelX + fFinalVelY * fFinalVelY);
    } else {                                           // Handle forward motion - calculate new direction and speed
      if (fabs(dVelY * tcos[iAngleIdx] - fNegPerpVel * tsin[iAngleIdx]) || fabs(dVelX * tsin[iAngleIdx] + fNegPerpVel * tcos[iAngleIdx])) {
        dAngleCalc = atan2(fFinalVelY, fFinalVelX) * 16384.0 / 6.28318530718;
        //dAngleCalc = IF_DATAN2(nFpuTemp1, fFinalVelY, fFinalVelX) * 16384.0 / 6.28318530718;
        //_CHP();
        iNewYaw = (int)dAngleCalc & 0x3FFF;
      } else {
        iNewYaw = 0;
      }
      pCar->nActualYaw = iNewYaw;
      dFinalSpeed = sqrt(fFinalVelX * fFinalVelX + fFinalVelY * fFinalVelY);
    }
    pCar->fFinalSpeed = (float)dFinalSpeed;
  }
  iYawDiff = pCar->nActualYaw - pCar->nYaw;          // Apply yaw constraints - limit how fast car can turn based on speed
  iConstrainedYaw = iYawDiff + (iYawDiff < 0 ? 0x4000 : 0);
  if (iConstrainedYaw > 0x2000)
    iConstrainedYaw -= 0x4000;
  if (iConstrainedYaw > iMaxYawChange)
    iConstrainedYaw = iMaxYawChange;
  if (iConstrainedYaw < -iMaxYawChange)
    iConstrainedYaw = -iMaxYawChange;
  fFinalSpeed = pCar->fFinalSpeed;
  nNewYawValue = pCar->nYaw + iConstrainedYaw;
  nNewYawValue &= 0x3FFF;
  //HIBYTE(nNewYawValue) &= 0x3Fu;
  pCar->nYaw = nNewYawValue;
  SetEngine(pCar, fFinalSpeed);                 // Apply final changes - update engine, reset steering, flatten car, set collision timer
  pCar->iSteeringInput = 0;
  putflat(pCar);
  pCar->byCollisionTimer = 18;
}

//-------------------------------------------------------------------------------------------------
//00030590
void hitright(tCar *pCar, int iSampleIdx, int iIsRightSide)
{                                               // Calculate maximum yaw change based on speed (capped at 500.0)
  double dSpeedScale; // st7
  int iYaw3; // eax
  int iCollisionAngle; // eax
  double dAbsVelY; // st7
  double dDamageAmount; // st7
  double dVelX; // st7
  double dVelY; // st6
  int iAngleIdx; // eax
  int iNewYaw; // eax
  //int16 nFpuTemp1; // fps
  double dAngleCalc; // st7
  double dFinalSpeed; // st7
  int iReverseYaw; // eax
  //int16 nFpuTemp2; // fps
  double dReverseAngle; // st7
  int iYawDiff; // edx
  int iConstrainedYaw; // eax
  int16 nNewYawValue; // ax
  float fDamageFloat; // [esp+0h] [ebp-4Ch]
  float fNegVelX; // [esp+4h] [ebp-48h]
  float fNegVelY; // [esp+18h] [ebp-34h]
  float fAbsVelYFloat; // [esp+1Ch] [ebp-30h]
  int iMaxYawChange; // [esp+20h] [ebp-2Ch]
  float fVelX; // [esp+24h] [ebp-28h]
  float fVelY; // [esp+28h] [ebp-24h]
  float fTangentialVel; // [esp+2Ch] [ebp-20h]
  float fFinalVelX; // [esp+30h] [ebp-1Ch]
  float fPerpVel; // [esp+34h] [ebp-18h]
  float fAbsPerpVel; // [esp+34h] [ebp-18h]
  float fFinalVelY; // [esp+38h] [ebp-14h]

  if (pCar->fFinalSpeed >= 500.0) {
    iMaxYawChange = 512;
  } else {
    dSpeedScale = pCar->fFinalSpeed * 512.0 * 0.0020000001;
    //_CHP();
    iMaxYawChange = (int)dSpeedScale;
  }
  iYaw3 = pCar->nActualYaw;
  fVelX = tcos[iYaw3] * pCar->fFinalSpeed;      // Calculate velocity components from car's current speed and direction
  fVelY = tsin[iYaw3] * pCar->fFinalSpeed;
  if (iIsRightSide)                           // Select collision angle based on left/right side (iIsRightSide parameter)
    iCollisionAngle = rightang;
  else
    iCollisionAngle = leftang;
  fTangentialVel = fVelX * tcos[iCollisionAngle] + fVelY * tsin[iCollisionAngle];// Transform velocity vector to collision surface coordinate system
  fPerpVel = -fVelX * tsin[iCollisionAngle] + fVelY * tcos[iCollisionAngle];
  if (!pCar->byCollisionTimer)                // Check if car is not already in collision state (byCollisionTimer)
  {                                             // If velocity into wall is negative, play collision sound and calculate damage
    if (fVelY < 0.0) {
      dAbsVelY = -fVelY;
      fAbsVelYFloat = (float)dAbsVelY;
      //_CHP();
      sfxpend(iSampleIdx, pCar->iDriverIdx, (int)(dAbsVelY * 32768.0 * 0.0049999999));
      if (death_race)
        dDamageAmount = fAbsVelYFloat * 0.006 * 4.0;
      else
        dDamageAmount = fAbsVelYFloat * 0.006;
      fDamageFloat = (float)dDamageAmount;
      dodamage(pCar, fDamageFloat);
    }
    fPerpVel = fPerpVel * -0.6f;                 // Apply collision physics - reverse perpendicular velocity with damping
    fTangentialVel = fTangentialVel * 0.8f;
  }
  //_CHP();
  fAbsPerpVel = (float)(int)abs((int)fPerpVel);
  dVelX = fTangentialVel;                       // Transform velocity back to world coordinates after collision response
  dVelY = fTangentialVel;
  if (iIsRightSide)
    iAngleIdx = rightang;
  else
    iAngleIdx = leftang;
  fFinalVelX = (float)dVelY * tcos[iAngleIdx] - fAbsPerpVel * tsin[iAngleIdx];
  fFinalVelY = (float)dVelX * tsin[iAngleIdx] + fAbsPerpVel * tcos[iAngleIdx];
  if (pCar->fHealth <= 0.0 && fFinalVelY == 40.0)// Special case - if car is dead and velocity is exactly 40, stop completely
  {
    pCar->fFinalSpeed = 0.0;
  } else {                                             // Handle reverse motion - calculate new direction and speed
    if (pCar->fFinalSpeed < 0.0) {
      fNegVelX = -fFinalVelX;
      if (fabs(fNegVelX) > FLT_EPSILON || fabs(-fFinalVelY)) {
      //if ((LODWORD(fNegVelX) & 0x7FFFFFFF) != 0 || fabs(-fFinalVelY)) {
        fNegVelY = -fFinalVelY;
        dReverseAngle = atan2(fNegVelY, fNegVelX) * 16384.0 / 6.28318530718;
        //_CHP();
        iReverseYaw = (int)dReverseAngle & 0x3FFF;
      } else {
        iReverseYaw = 0;
      }
      pCar->nActualYaw = iReverseYaw;
      dFinalSpeed = -sqrt(fFinalVelX * fFinalVelX + fFinalVelY * fFinalVelY);
    } else {                                           // Handle forward motion - calculate new direction and speed
      if (fabs(dVelY * tcos[iAngleIdx] - fAbsPerpVel * tsin[iAngleIdx]) || fabs(dVelX * tsin[iAngleIdx] + fAbsPerpVel * tcos[iAngleIdx])) {
        dAngleCalc = atan2(fFinalVelY, fFinalVelX) * 16384.0 / 6.28318530718;
        //_CHP();
        iNewYaw = (int)dAngleCalc & 0x3FFF;
      } else {
        iNewYaw = 0;
      }
      pCar->nActualYaw = iNewYaw;
      dFinalSpeed = sqrt(fFinalVelX * fFinalVelX + fFinalVelY * fFinalVelY);
    }
    pCar->fFinalSpeed = (float)dFinalSpeed;
  }
  if (!pCar->byCollisionTimer) {
    iYawDiff = pCar->nActualYaw - pCar->nYaw;        // Apply yaw constraints only if not in collision state
    iConstrainedYaw = iYawDiff + (iYawDiff < 0 ? 0x4000 : 0);
    if (iConstrainedYaw > 0x2000)
      iConstrainedYaw -= 0x4000;
    if (iConstrainedYaw > iMaxYawChange)
      iConstrainedYaw = iMaxYawChange;
    if (iConstrainedYaw < -iMaxYawChange)
      iConstrainedYaw = -iMaxYawChange;
    nNewYawValue = pCar->nYaw + iConstrainedYaw;
    nNewYawValue &= 0x3FFFu;
    //HIBYTE(nNewYawValue) &= 0x3Fu;
    pCar->nYaw = nNewYawValue;
  }
  SetEngine(pCar, pCar->fFinalSpeed);           // Apply final changes - update engine, reset steering, flatten car, set collision timer
  pCar->iSteeringInput = 0;
  putflat(pCar);
  pCar->byCollisionTimer = 18;
}

//-------------------------------------------------------------------------------------------------
//000308D0
void scansection(tCar *pCar)
{
  int iTrackLen; // edi
  int iChunkIdx; // ebx
  tData *pData; // edx
  int iDirection; // ecx
  double dTempY; // st7
  double dTempX; // st5
  double dTempZ; // rt0
  double dTempY2; // st7
  double dTempX2; // st5
  double dTempZ2; // rt1
  int iTempYaw; // eax
  float fTransformedX; // [esp+4h] [ebp-30h]
  float fTransformedY; // [esp+8h] [ebp-2Ch]
  float fTransformedZ; // [esp+Ch] [ebp-28h]
  float fZ; // [esp+10h] [ebp-24h]
  float fY; // [esp+14h] [ebp-20h]
  float fX; // [esp+18h] [ebp-1Ch]

  iTrackLen = TRAK_LEN;
  fX = pCar->pos.fX;
  fY = pCar->pos.fY;
  iChunkIdx = pCar->nCurrChunk;
  fZ = pCar->pos.fZ;
  pData = &localdata[iChunkIdx];
  fTransformedX = pData->pointAy[0].fY * fY + pData->pointAy[0].fX * pCar->pos.fX + pData->pointAy[0].fZ * fZ - pData->pointAy[3].fX;// Transform car position relative to current track chunk coordinate system
  fTransformedY = pData->pointAy[1].fY * fY + pData->pointAy[1].fX * pCar->pos.fX + pData->pointAy[1].fZ * fZ - pData->pointAy[3].fY;
  fTransformedZ = pData->pointAy[2].fY * fY + pData->pointAy[2].fX * pCar->pos.fX + pData->pointAy[2].fZ * fZ - pData->pointAy[3].fZ;
  iDirection = 0;
  if (pCar->pos.fX < (double)pData->fTrackHalfLength)// If car is behind track half-length, scan backwards through chunks
  {
    do {
      if (--iChunkIdx == -1)
        iChunkIdx = TRAK_LEN - 1;
      pData = &localdata[iChunkIdx];
      dTempY = fTransformedY + pData->pointAy[3].fY;
      dTempX = fTransformedX + pData->pointAy[3].fX;
      dTempZ = fTransformedZ + pData->pointAy[3].fZ;
      fX = (float)(pData->pointAy[1].fX * dTempY + pData->pointAy[0].fX * dTempX + pData->pointAy[2].fX * dTempZ);// Apply inverse transformation to get position in new chunk's coordinate system
      fY = (float)(pData->pointAy[0].fY * dTempX + pData->pointAy[1].fY * dTempY + pData->pointAy[2].fY * dTempZ);
      fZ = (float)(dTempY * pData->pointAy[1].fZ + dTempX * pData->pointAy[0].fZ + dTempZ * pData->pointAy[2].fZ);
      iDirection -= pData->iYaw;
    } while (fX < (double)pData->fTrackHalfLength);
  }
  while (fX > (double)pData->fTrackHalfLength)// If car is ahead of track half-length, scan forwards through chunks
  {
    ++iChunkIdx;
    iDirection += pData->iYaw;
    if (iChunkIdx == TRAK_LEN)
      iChunkIdx ^= TRAK_LEN;
    pData = &localdata[iChunkIdx];
    dTempY2 = fTransformedY + pData->pointAy[3].fY;
    dTempX2 = fTransformedX + pData->pointAy[3].fX;
    dTempZ2 = fTransformedZ + pData->pointAy[3].fZ;
    fX = (float)(pData->pointAy[1].fX * dTempY2 + pData->pointAy[0].fX * dTempX2 + pData->pointAy[2].fX * dTempZ2);
    fY = (float)(pData->pointAy[0].fY * dTempX2 + pData->pointAy[1].fY * dTempY2 + pData->pointAy[2].fY * dTempZ2);
    fZ = (float)(dTempY2 * pData->pointAy[1].fZ + dTempX2 * pData->pointAy[0].fZ + dTempZ2 * pData->pointAy[2].fZ);
  }
  pCar->pos.fX = fX;                            // Store final transformed position and updated chunk/yaw back to car
  pCar->pos.fY = fY;
  pCar->pos.fZ = fZ;
  pCar->nYaw -= iDirection;
  pCar->nYaw &= 0x3FFF;
  //HIBYTE(pCar->nYaw) &= 0x3Fu;                  // Mask yaw values to keep them within valid ranges (14-bit values)
  iTempYaw = pCar->nActualYaw - iDirection;
  pCar->nCurrChunk = iChunkIdx;
  pCar->nActualYaw = iTempYaw;
  pCar->nActualYaw = iTempYaw & 0x3FFF;
  TRAK_LEN = iTrackLen;
}

//-------------------------------------------------------------------------------------------------
//00030AC0
double getgroundz(float fXPos, float fYOffset, int iChunkIdx)
{
  tData *pData; // edx
  double dAngleIdx; // st7
  tTrackInfo *pTrackInfo; // ebx
  double dAbsYOffset; // st7
  float fTempYOffset; // [esp+4h] [ebp-14h]
  float fGroundZ; // [esp+8h] [ebp-10h]

  pData = &localdata[iChunkIdx];                // Get track data for the specified chunk
  dAngleIdx = (double)pData->iBankDelta * fXPos / (pData->fTrackHalfLength * 2.0);// Calculate banking angle index based on X position in chunk
  //_CHP();
  pTrackInfo = &TrackInfo[iChunkIdx];           // Get track shoulder information for this chunk
  fGroundZ = -fYOffset * ptan[(int)dAngleIdx & 0x3FFF];// Calculate base ground height using banking angle
  if (fYOffset >= (double)pData->fTrackHalfWidth)// Check if position is on left shoulder (positive Y beyond track)
    fGroundZ = (fYOffset - pData->fTrackHalfWidth) * pTrackInfo->fLShoulderHeight / pTrackInfo->fLShoulderWidth + fGroundZ;// Add left shoulder height based on distance beyond track edge
  dAbsYOffset = -fYOffset;
  if (dAbsYOffset >= pData->fTrackHalfWidth)  // Check if position is on right shoulder (negative Y beyond track)
  {
    fTempYOffset = (float)dAbsYOffset;
    return (float)((fTempYOffset - pData->fTrackHalfWidth) * pTrackInfo->fRShoulderHeight / pTrackInfo->fRShoulderWidth + fGroundZ);// Add right shoulder height and return total ground Z
  }
  return fGroundZ;                              // Return banking height for positions on main track
}

//-------------------------------------------------------------------------------------------------
//00030B80
double getroadz(float fX, float fY, int iChunkIdx)
{
  double dBankAngleIndex; // st7

  dBankAngleIndex = (double)localdata[iChunkIdx].iBankDelta * fX / (localdata[iChunkIdx].fTrackHalfLength * 2.0);// Calculate banking angle index based on X position and track banking
  //_CHP();
  return -fY * ptan[(int)dBankAngleIndex & 0x3FFF];// Apply banking effect: negative Y times tangent of banking angle
}

//-------------------------------------------------------------------------------------------------
//00030BD0
void putflat(tCar *pCar)
{
  int nCurrChunk; // esi
  tTrackInfo *pTrackInfo; // edi
  tData *pCurrData; // ecx
  int iPrevChunkIdx; // eax
  int iNextChunkIdx2; // edx
  tData *pPrevData; // ebp
  int iPitch; // eax
  int iLaneType; // edx
  //int64 llSurfaceType1; // rax
  //int64 llSurfaceType2; // rax
  //int64 llSurfaceType3; // rax
  double dPitchFloat; // st7
  int iBankDelta; // esi
  double dInvHalfLength; // st7
  double dBankTerm1; // st6
  double dBankTerm2; // st5
  double dBankAngle; // st7
  //int16 nBankAngleInt; // fps
  double dPitchCorrected; // st7
  double dBankFloat; // st7
  double dLeftBankAdj; // st7
  double dShoulderHeight; // st7
  double dRightBankAdj; // st7
  double dAbsYPos; // st7
  double dPitchForCalc; // st7
  double dPitchResult; // st5
  //int iYawIndex; // eax
  double dRollResult; // st7
  int iPitchFinal_1; // eax
  int iStunned; // edi
  int nYaw; // [esp+14h] [ebp-40h]
  float fAbsYPos; // [esp+1Ch] [ebp-38h]
  float fNegYPos; // [esp+20h] [ebp-34h]
  int iRollFinal; // [esp+24h] [ebp-30h]
  tData *pNextData; // [esp+28h] [ebp-2Ch]
  int iNextChunkIdx; // [esp+2Ch] [ebp-28h]
  int iBankInt; // [esp+30h] [ebp-24h]
  int iPitchInt; // [esp+34h] [ebp-20h]
  int iPitchFinal; // [esp+34h] [ebp-20h]

  nCurrChunk = pCar->nCurrChunk;                // Get current track chunk and related data
  if (nCurrChunk < 0 || nCurrChunk >= TRAK_LEN) //added by ROLLER
    nCurrChunk = 0;

  pTrackInfo = &TrackInfo[nCurrChunk];
  pCurrData = &localdata[nCurrChunk];
  nYaw = pCar->nYaw;
  if (pCar->pos.fX >= 0.0)                    // Determine previous and next chunk indices based on car direction
  {
    iPrevChunkIdx = nCurrChunk;
    iNextChunkIdx = nCurrChunk + 1;
    if (nCurrChunk + 1 == TRAK_LEN)
      iNextChunkIdx = TRAK_LEN ^ (nCurrChunk + 1);
    iNextChunkIdx2 = iNextChunkIdx;
  } else {
    iPrevChunkIdx = nCurrChunk - 1;
    if (nCurrChunk - 1 < 0)
      iPrevChunkIdx = TRAK_LEN - 1;
    iNextChunkIdx2 = iPrevChunkIdx;
  }
  pNextData = &localdata[iNextChunkIdx2];
  pPrevData = &localdata[iPrevChunkIdx];
  if (pCar->pos.fY <= (double)pCurrData->fTrackHalfWidth)// Determine which lane car is in and get appropriate pitch angle
  {
    if (-pCurrData->fTrackHalfWidth <= pCar->pos.fY) {
      iPitch = pPrevData->iPitch;               // Center lane (on track)
      iLaneType = 1;
    } else {
      iPitch = pPrevData->iOuterLanePitchAngle; // Outer lane (left side of track)
      iLaneType = 2;
    }
  } else {
    iPitch = pPrevData->iInnerLanePitchAngle;   // Inner lane (right side of track)
    iLaneType = 0;
  }
  iPitchInt = iPitch;
  if (pCar->pos.fX < 0.0)                     // Check surface type and apply pitch corrections based on car position
  {

    if ((TrakColour[nCurrChunk][iLaneType] & SURFACE_FLAG_SKIP_RENDER) != 0)
    //llSurfaceType3 = TrakColour[nCurrChunk][iLaneType];
    //if ((((HIDWORD(llSurfaceType3) ^ (unsigned int)llSurfaceType3) - HIDWORD(llSurfaceType3)) & 0x20000) != 0)// SURFACE_FLAG_SKIP_RENDER
      iPitchInt = 0;
  } else {                                             // Steep downhill: zero pitch if car is fast and AI state allows
    if (iPitch < -512 && pCar->fFinalSpeed > 50.0 && (TrakColour[pCar->nCurrChunk][pCar->iLaneType] & SURFACE_FLAG_NON_MAGNETIC) != 0)
      iPitchInt = 0;

    if ((TrakColour[nCurrChunk][iLaneType] & SURFACE_FLAG_SKIP_RENDER) != 0)
    //llSurfaceType1 = TrakColour[nCurrChunk][iLaneType];// Check current chunk surface type and zero pitch if needed
    //if ((((HIDWORD(llSurfaceType1) ^ (unsigned int)llSurfaceType1) - HIDWORD(llSurfaceType1)) & 0x20000) != 0)// SURFACE_FLAG_SKIP_RENDER
      iPitchInt = 0;

    if (pCar->fFinalSpeed > 50.0) {

      if ((TrakColour[iNextChunkIdx][pCar->iLaneType] & SURFACE_FLAG_SKIP_RENDER) != 0)
      //llSurfaceType2 = TrakColour[iNextChunkIdx][pCar->iLaneType];// Check next chunk surface type for fast cars
      //if ((((HIDWORD(llSurfaceType2) ^ (unsigned int)llSurfaceType2) - HIDWORD(llSurfaceType2)) & 0x20000) != 0)// SURFACE_FLAG_SKIP_RENDER
        iPitchInt = 0;
    }
  }
  dPitchFloat = (double)iPitchInt * pCar->pos.fX / (pCurrData->fTrackHalfLength * 2.0);// Calculate pitch angle based on car position along track
  //_CHP();
  iPitchFinal = (int)dPitchFloat;
  if ((TrakColour[nCurrChunk][1] & SURFACE_FLAG_BOUNCE_30) != 0)// SURFACE_FLAG_BOUNCE_30
  {
    if (pCar->pos.fX < 0.0) {
      dInvHalfLength = 1.0 / pCurrData->fTrackHalfLength;
      dBankTerm1 = (pCar->pos.fX + pCurrData->fTrackHalfLength) * (double)pCurrData->iBankDelta * dInvHalfLength;
      dBankTerm2 = (double)((pNextData->iBankDelta + pCurrData->iBankDelta) / -2);
    } else {
      iBankDelta = pCurrData->iBankDelta;
      dInvHalfLength = 1.0 / pCurrData->fTrackHalfLength;
      dBankTerm1 = (pCar->pos.fX - pCurrData->fTrackHalfLength) * (double)-iBankDelta * dInvHalfLength;
      dBankTerm2 = (double)((iBankDelta + pNextData->iBankDelta) / 2);
    }
    dBankAngle = dInvHalfLength * (dBankTerm2 * pCar->pos.fX) + dBankTerm1;// Apply special banking interpolation and atan2 correction
    //_CHP();
    dPitchCorrected = (double)iPitchFinal
      - atan2(pCar->pos.fY * tsin[(int)dBankAngle & 0x3FFF], pCurrData->fTrackHalfLength * 2.0) * 16384.0 / 6.28318530718;
    //_CHP();
    iPitchFinal = (int)dPitchCorrected;
  }
  dBankFloat = (double)pCurrData->iBankDelta * pCar->pos.fX / (pCurrData->fTrackHalfLength * 2.0);// Calculate basic banking angle based on car X position
  //_CHP();
  iBankInt = (int)dBankFloat;
  pCar->pos.fZ = -(pCar->pos.fY * ptan[(int)dBankFloat & 0x3FFF]);// Calculate car Z position using banking angle
  if (pCar->pos.fY < 0.0)                     // Apply track banking adjustments based on car Y position
  {
    fNegYPos = -pCar->pos.fY;                   // Right side banking adjustments
    if (pCurrData->fTrackHalfWidth + pCar->fCarHalfWidth > fNegYPos) {
      if (pCurrData->fTrackHalfWidth - pCar->fCarHalfWidth <= fNegYPos) {
        dRightBankAdj = (fNegYPos - pCurrData->fTrackHalfWidth + pCar->fCarHalfWidth) * (double)pTrackInfo->iRightBankAngle / (pCar->fCarHalfWidth * 2.0) + (double)iBankInt;
        //_CHP();
        iBankInt = (int)dRightBankAdj;
      }
    } else {
      iBankInt += pTrackInfo->iRightBankAngle;
    }
    dAbsYPos = -pCar->pos.fY;                   // Add shoulder height if car is off track (right side)
    if (dAbsYPos >= pCurrData->fTrackHalfWidth) {
      fAbsYPos = (float)dAbsYPos;
      dShoulderHeight = (fAbsYPos - pCurrData->fTrackHalfWidth) * pTrackInfo->fRShoulderHeight / pTrackInfo->fRShoulderWidth;
      goto LABEL_43;
    }
  } else {                                             // Left side banking adjustments
    if (pCurrData->fTrackHalfWidth + pCar->fCarHalfWidth > pCar->pos.fY) {
      if (pCurrData->fTrackHalfWidth - pCar->fCarHalfWidth <= pCar->pos.fY) {
        dLeftBankAdj = (double)iBankInt - (pCar->pos.fY - pCurrData->fTrackHalfWidth + pCar->fCarHalfWidth) * (double)pTrackInfo->iLeftBankAngle / (pCar->fCarHalfWidth * 2.0);
        //_CHP();
        iBankInt = (int)dLeftBankAdj;
      }
    } else {
      iBankInt -= pTrackInfo->iLeftBankAngle;
    }
    if (pCar->pos.fY >= (double)pCurrData->fTrackHalfWidth)// Add shoulder height if car is off track (left side)
    {
      dShoulderHeight = (pCar->pos.fY - pCurrData->fTrackHalfWidth) * pTrackInfo->fLShoulderHeight / pTrackInfo->fLShoulderWidth;
    LABEL_43:
      pCar->pos.fZ = (float)dShoulderHeight + pCar->pos.fZ;
    }
  }
  dPitchForCalc = (double)iPitchFinal;          // Transform pitch and bank angles using car yaw rotation
  dPitchResult = dPitchForCalc * tcos[nYaw] - (double)iBankInt * tsin[nYaw];
  //_CHP();
  dRollResult = dPitchForCalc * tsin[nYaw] + (double)iBankInt * tcos[nYaw];
  //_CHP();
  

  //iPitchFinal_1 = (int)dPitchResult;
  ////iPitchFinal_1 = ((int16)(int)dPitchResult >> 8) & 0x3FFF;// Mask pitch angle to 14-bit range
  //SET_BYTE1(iPitchFinal_1, ((uint16)(int)dPitchResult >> 8) & 0x3F);// Mask pitch result to 14-bit range (0x3FFF)
  //BYTE1(iPitchFinal_1) = ((unsigned __int16)(int)dPitchResult >> 8) & 0x3F;// Mask pitch result to 14-bit range (0x3FFF)

  iPitchFinal_1 = ((int)dPitchResult) & 0x3FFF;
  iStunned = pCar->iStunned;
  pCar->nPitch = iPitchFinal_1;
  iRollFinal = (int)dRollResult;
  if (iStunned)                               // Apply stunned car effects - add roll offset and height adjustment
  {
    iRollFinal = (int16)(iRollFinal) + 0x2000;
    pCar->pos.fZ = CarBox.hitboxAy[pCar->byCarDesignIdx][4].fZ + pCar->pos.fZ;
  }
  pCar->nRoll = (int16)iRollFinal & 0x3FFF;            // Set final roll angle with 14-bit mask
}

//-------------------------------------------------------------------------------------------------
//00031060
void findnearcars(tCar *pCar, int *piLeftCarIdx, float *pfLeftTime, int *piRightCarIdx, float *pfRightTime, float *pfTargetX, float *pfTargetY)
{
  int iDriverIdx; // eax
  float fRightTargetTime; // [esp+0h] [ebp-18h]
  float fLeftTargetTime; // [esp+4h] [ebp-14h]

  iDriverIdx = pCar->iDriverIdx;                // Check if this car should perform full collision detection this frame
  if (iDriverIdx == nearcall[nearcarcheck][0] || iDriverIdx == nearcall[nearcarcheck][1] || iDriverIdx == nearcall[nearcarcheck][2] || iDriverIdx == nearcall[nearcarcheck][3])// Check against scheduled cars for this frame (load balancing)
  {
    findnearcarsforce(pCar, piLeftCarIdx, pfLeftTime, piRightCarIdx, pfRightTime, pfTargetX, pfTargetY);// Perform full collision detection and cache results
    pCar->iLeftTargetIdx = *piLeftCarIdx;       // Cache collision detection results in car structure
    pCar->fLeftTargetTime = *pfLeftTime;
    pCar->iRightTargetIdx = *piRightCarIdx;
    pCar->fRightTargetTime = *pfRightTime;
    pCar->fTargetX = *pfTargetX;
    pCar->fTargetY = *pfTargetY;
  } else {
    *piLeftCarIdx = pCar->iLeftTargetIdx;       // Use cached collision data from previous frame
    fLeftTargetTime = pCar->fLeftTargetTime;
    *piRightCarIdx = pCar->iRightTargetIdx;
    fRightTargetTime = pCar->fRightTargetTime;
    *pfTargetX = pCar->fTargetX;
    *pfTargetY = pCar->fTargetY;
    if (*piLeftCarIdx != -1)                  // Update cached collision times by subtracting frame time
    {
      if (fLeftTargetTime < 0.0) {
        fLeftTargetTime = fLeftTargetTime + -0.02777777777777778f;// Subtract frame time (1/36 second is about 0.0278s for 36 FPS)
      } else {
        fLeftTargetTime = fLeftTargetTime + -0.02777777777777778f;
        if (fLeftTargetTime < 0.0)            // Clamp positive times to zero (collision imminent)
          fLeftTargetTime = 0.0;
      }
    }
    if (*piRightCarIdx != -1) {
      if (fRightTargetTime < 0.0) {
        fRightTargetTime = fRightTargetTime + -0.02777777777777778f;
      } else {
        fRightTargetTime = fRightTargetTime + -0.02777777777777778f;
        if (fRightTargetTime < 0.0)
          fRightTargetTime = 0.0;
      }
    }
    *pfLeftTime = fLeftTargetTime;
    *pfRightTime = fRightTargetTime;
    pCar->fLeftTargetTime = fLeftTargetTime;
    pCar->fRightTargetTime = fRightTargetTime;
    if (*piLeftCarIdx >= 0 && Car[*piLeftCarIdx].nCurrChunk == -1)  // Validate left target car is still on track
    {
      *piLeftCarIdx = -1;                       // Clear invalid left target (car removed from track)
      *pfLeftTime = -10000.0;
      pCar->iLeftTargetIdx = -1;
      pCar->fLeftTargetTime = -10000.0;
    }
    if (*piRightCarIdx >= 0 && Car[*piRightCarIdx].nCurrChunk == -1) // Validate right target car is still on track
    {
      *piRightCarIdx = -1;                      // Clear invalid right target (car removed from track)
      *pfRightTime = -10000.0;
      pCar->iRightTargetIdx = -1;
      pCar->fRightTargetTime = -10000.0;
    }
  }
}

//-------------------------------------------------------------------------------------------------
//00031250
void findnearcarsforce(tCar *pCar, int *piLeftCarIdx, float *pfLeftTime, int *piRightCarIdx, float *pfRightTime, float *pfTargetX, float *pfTargetY)
{
  int iTrakLen; // esi
  double dAbsSpeed; // st7
  int nCurrChunk; // ebx
  tData *pTrackData; // ecx
  double dTrackPos; // st7
  double dNewTrackPos; // st7
  int iNextChunk; // ebx
  double dYPos; // st6
  double dTempX; // rt0
  double dTempY; // rt1
  double dTempZ; // st5
  tData *pCurrTrackData; // ecx
  int iCarIdx; // edi
  int iDriverIdx; // eax
  int iChunkDiff; // ebx
  double dOtherCarSpeed; // st7
  int iTrakLenTemp; // esi
  int iChunkIdx; // ebx
  int iCounter; // eax
  double dDistanceCalc; // st7
  int iOtherCarChunk; // ebx
  double dTempDistance; // st7
  signed int i; // ecx
  int iSideFlag; // ebx
  double dSpeedDiff; // st7
  float *pOutParam; // ebx
  float fOutValue; // eax
  float fSpeedForCalc; // [esp+1Ch] [ebp-54h]
  float fStrategyValue; // [esp+20h] [ebp-50h]
  float fTrackPos; // [esp+24h] [ebp-4Ch]
  float fOwnSpeedForward; // [esp+28h] [ebp-48h]
  int iLeftCarIdx; // [esp+2Ch] [ebp-44h]
  int iRightCarIdx; // [esp+30h] [ebp-40h]
  float fCarWidth; // [esp+34h] [ebp-3Ch]
  float fTrackLength; // [esp+38h] [ebp-38h]
  float fSpeedDiffCalc; // [esp+3Ch] [ebp-34h]
  float fOtherCarSpeed; // [esp+40h] [ebp-30h]
  float fForwardPos; // [esp+44h] [ebp-2Ch]
  float fReversePos; // [esp+48h] [ebp-28h]
  float fLeftTime; // [esp+4Ch] [ebp-24h]
  float fRightTime; // [esp+50h] [ebp-20h]
  int iCarLoopIdx; // [esp+54h] [ebp-1Ch]
  float fTimeToCollision; // [esp+58h] [ebp-18h]
  int iNormalizedChunkDiff; // [esp+5Ch] [ebp-14h]
  float fDistFromChunk; // [esp+60h] [ebp-10h]
  float fDistance; // [esp+60h] [ebp-10h]
  float fNegDistance; // [esp+60h] [ebp-10h]
  float fAdjustedDistance; // [esp+60h] [ebp-10h]

  iTrakLen = TRAK_LEN;
  fStrategyValue = CarStrategy[pCar->iDriverIdx].fAvoidDistance;// Get AI strategy value for car avoidance distance
  dAbsSpeed = fabs(pCar->fFinalSpeed);          // Calculate absolute speed for distance scaling
  if (dAbsSpeed < 450.0) {
    fSpeedForCalc = (float)dAbsSpeed;
    fStrategyValue = (fStrategyValue + -1000.0f) * fSpeedForCalc * 0.0022222223f + 1000.0f;
  }
  fLeftTime = -10000.0;
  fRightTime = -10000.0;
  nCurrChunk = pCar->nCurrChunk;
  iLeftCarIdx = -1;
  iRightCarIdx = -1;
  if (pCar->iBobMode)                         // Calculate look-ahead position based on revenge mode
  {
    fReversePos = pCar->pos.fX - localdata[nCurrChunk].fTrackHalfLength - fStrategyValue;
    while (-2.0 * localdata[nCurrChunk].fTrackHalfLength > fReversePos) {
      dNewTrackPos = 2.0 * localdata[nCurrChunk--].fTrackHalfLength + fReversePos;
      fReversePos = (float)dNewTrackPos;
      if (nCurrChunk < 0)
        nCurrChunk = TRAK_LEN - 1;
    }
    pTrackData = &localdata[nCurrChunk];
    dTrackPos = fReversePos + pTrackData->fTrackHalfLength;
  } else {
    fForwardPos = fStrategyValue + pCar->pos.fX + localdata[nCurrChunk].fTrackHalfLength;
    while (1) {
      fTrackLength = 2.0f * localdata[nCurrChunk].fTrackHalfLength;
      if (fForwardPos <= (double)fTrackLength)
        break;
      ++nCurrChunk;
      fForwardPos = fForwardPos - fTrackLength;
      if (nCurrChunk == TRAK_LEN)
        nCurrChunk ^= TRAK_LEN;
    }
    pTrackData = &localdata[nCurrChunk];
    dTrackPos = fForwardPos - pTrackData->fTrackHalfLength;
  }
  fTrackPos = (float)dTrackPos;
  iNextChunk = nCurrChunk + 1;
  if (iNextChunk == TRAK_LEN)
    iNextChunk ^= TRAK_LEN;
  dYPos = ((pTrackData->fTrackHalfLength - fTrackPos) * *(&pTrackData->fAILine1 + pCar->iAICurrentLine)
         + (fTrackPos + pTrackData->fTrackHalfLength) * *(&localdata[iNextChunk].fAILine1 + pCar->iAICurrentLine))
    / (pTrackData->fTrackHalfLength
     * 2.0);
  dTempX = pTrackData->pointAy[1].fX * fTrackPos + pTrackData->pointAy[1].fY * dYPos - pTrackData->pointAy[3].fY;
  dTempY = pTrackData->pointAy[0].fY * dYPos + pTrackData->pointAy[0].fX * fTrackPos - pTrackData->pointAy[3].fX;
  dTempZ = fTrackPos * pTrackData->pointAy[2].fX + dYPos * pTrackData->pointAy[2].fY - pTrackData->pointAy[3].fZ;
  pCurrTrackData = &localdata[pCar->nCurrChunk];
  *pfTargetX = (float)((dTempX + pCurrTrackData->pointAy[3].fY) * pCurrTrackData->pointAy[1].fX
    + (dTempY + pCurrTrackData->pointAy[3].fX) * pCurrTrackData->pointAy[0].fX
    + (dTempZ + pCurrTrackData->pointAy[3].fZ) * pCurrTrackData->pointAy[2].fX);
  *pfTargetY = (float)((dTempY + pCurrTrackData->pointAy[3].fX) * pCurrTrackData->pointAy[0].fY
    + (dTempX + pCurrTrackData->pointAy[3].fY) * pCurrTrackData->pointAy[1].fY
    + (dTempZ + pCurrTrackData->pointAy[3].fZ) * pCurrTrackData->pointAy[2].fY);
  iCarLoopIdx = 0;
  fOwnSpeedForward = pCar->fFinalSpeed * tcos[pCar->nActualYaw & 0x3FFF];
  if (numcars > 0)                            // Main loop: check all cars for collision threats
  {
    iCarIdx = 0;
    do {
      iDriverIdx = pCar->iDriverIdx;
      TRAK_LEN = iTrakLen;
      if (iDriverIdx == iCarLoopIdx || (char)Car[iCarIdx].byLives <= 0)
        goto LABEL_67;
      iChunkDiff = Car[iCarIdx].nCurrChunk - pCar->nCurrChunk;
      iNormalizedChunkDiff = iChunkDiff;
      if (iChunkDiff < 0)
        iNormalizedChunkDiff = iChunkDiff + iTrakLen;
      if (iTrakLen / 2 < iNormalizedChunkDiff)
        iNormalizedChunkDiff -= iTrakLen;
      dOtherCarSpeed = Car[iCarIdx].fFinalSpeed * tcos[Car[iCarIdx].nActualYaw & 0x3FFF];
      TRAK_LEN = iTrakLen;
      fOtherCarSpeed = (float)dOtherCarSpeed;
      if (Car[iCarIdx].iControlType != 3
        || fabs((double)iNormalizedChunkDiff / (fOwnSpeedForward - fOtherCarSpeed)) >= 0.2f
        || !linevalid(pCar->nCurrChunk, pCar->pos.fY, Car[iCarIdx].pos.fY)) {
        goto LABEL_67;                          // Check if car is AI-controlled and within collision range
      }
      iTrakLenTemp = TRAK_LEN;
      if (iNormalizedChunkDiff <= 0) {
        if (iNormalizedChunkDiff >= 0) {
          fDistance = Car[iCarIdx].pos.fX - pCar->pos.fX;
        } else {
          iOtherCarChunk = Car[iCarIdx].nCurrChunk;
          fNegDistance = -(localdata[iOtherCarChunk].fTrackHalfLength - Car[iCarIdx].pos.fX);
          dTempDistance = fNegDistance - (localdata[pCar->nCurrChunk].fTrackHalfLength + pCar->pos.fX);
          for (i = 1; ; ++i) {
            ++iOtherCarChunk;
            fDistance = (float)dTempDistance;
            if (i >= (int)abs(iNormalizedChunkDiff))
              break;
            if (iOtherCarChunk >= TRAK_LEN)
              iOtherCarChunk -= TRAK_LEN;
            dTempDistance = fDistance - 2.0 * localdata[iOtherCarChunk].fTrackHalfLength;
          }
          iTrakLenTemp = TRAK_LEN;
          if (fDistance > 0.0)
            fDistance = 0.0;
        }
      } else {
        fDistFromChunk = localdata[pCar->nCurrChunk].fTrackHalfLength - pCar->pos.fX;
        iChunkIdx = pCar->nCurrChunk + 1;
        iCounter = 1;
        for (fDistance = localdata[Car[iCarIdx].nCurrChunk].fTrackHalfLength + Car[iCarIdx].pos.fX + fDistFromChunk; iCounter < iNormalizedChunkDiff; fDistance = (float)dDistanceCalc) {
          if (iChunkIdx >= TRAK_LEN)
            iChunkIdx -= TRAK_LEN;
          dDistanceCalc = 2.0 * localdata[iChunkIdx].fTrackHalfLength + fDistance;
          ++iCounter;
          ++iChunkIdx;
        }
        if (fDistance < 0.0)
          fDistance = 0.0;
      }
      fCarWidth = 2.0f * CarBaseX;
      if (fDistance < 0.0) {
        fAdjustedDistance = fDistance + fCarWidth;
        iSideFlag = 0;
        if (fAdjustedDistance > 0.0)
          fAdjustedDistance = 0.0;
      } else {
        fAdjustedDistance = fDistance - fCarWidth;
        iSideFlag = -1;
        if (fAdjustedDistance < 0.0)
          fAdjustedDistance = 0.0;
      }
      dSpeedDiff = fOwnSpeedForward - fOtherCarSpeed;
      if (fabs(dSpeedDiff) >= 0.0001) {
        fSpeedDiffCalc = (float)dSpeedDiff;
        fTimeToCollision = fAdjustedDistance / (36.0f * fSpeedDiffCalc);// Calculate time to collision based on relative speed
      } else {
        fTimeToCollision = -10000.0;
      }
      TRAK_LEN = iTrakLenTemp;
      if (fabs(fTimeToCollision) > 8.0)
        goto LABEL_67;
      if (iSideFlag) {
        if (fTimeToCollision < 0.0) {
          if (fTimeToCollision > (double)fRightTime && fRightTime < 0.0) {
            iRightCarIdx = iCarLoopIdx;
            fRightTime = fTimeToCollision;
          }
        } else if (fTimeToCollision < (double)fRightTime || fRightTime < 0.0) {
          iRightCarIdx = iCarLoopIdx;
          fRightTime = fTimeToCollision;
        }
        goto LABEL_67;
      }
      if (fTimeToCollision < 0.0) {
        if (fTimeToCollision <= (double)fLeftTime || fLeftTime >= 0.0)
          goto LABEL_67;
      } else if (fTimeToCollision >= (double)fLeftTime && fLeftTime >= 0.0) {
        goto LABEL_67;
      }
      iLeftCarIdx = iCarLoopIdx;
      fLeftTime = fTimeToCollision;
    LABEL_67:
      iTrakLen = TRAK_LEN;
      ++iCarIdx;
      ++iCarLoopIdx;
    } while (iCarLoopIdx < numcars);
  }
  if (pCar->iBobMode)                         // Return results: left/right car indices and collision times
  {
    *piLeftCarIdx = iRightCarIdx;
    *piRightCarIdx = iLeftCarIdx;
    *pfLeftTime = fRightTime;
    pOutParam = pfRightTime;
    fOutValue = fLeftTime;
  } else {
    *piLeftCarIdx = iLeftCarIdx;
    *piRightCarIdx = iRightCarIdx;
    *pfLeftTime = fLeftTime;
    pOutParam = pfRightTime;
    fOutValue = fRightTime;
  }
  *pOutParam = fOutValue;
  TRAK_LEN = iTrakLen;
}

//-------------------------------------------------------------------------------------------------
//00031870
double interpolatesteer(float fSteeringInput, float fSaturationThreshold, float fDeadzoneThreshold, float fMaxOutput, float fMinOutput)
{
  double dAbsInput; // st7
  float fAbsInput; // [esp+Ch] [ebp+4h]

  dAbsInput = fabs(fSteeringInput);             // Get absolute value of steering input
  fAbsInput = (float)dAbsInput;
  if (dAbsInput <= fSaturationThreshold)      // Check if input exceeds saturation threshold
  {                                             // Check if input is in deadzone
    if (fAbsInput < (double)fDeadzoneThreshold)
      return fMinOutput;                        // Return minimum output value (deadzone)
    return (float)(((fAbsInput - fDeadzoneThreshold) * fMaxOutput - (fAbsInput - fSaturationThreshold) * fMinOutput) / (fSaturationThreshold - fDeadzoneThreshold));// Linear interpolation between deadzone and saturation
  } else {
    return fMaxOutput;                          // Return maximum output value (saturation)
  }
}

//-------------------------------------------------------------------------------------------------
//000318F0
double avoid(
        int iCurrentCarIdx,
        int iTargetCarIdx,
        float fSteeringInput,
        float fMaxOutput,
        float fSaturationThreshold,
        float fDeadzoneThreshold,
        int *piOvertakeFlag)
{
  int iDriverOffset; // eax
  double dTargetY; // st7
  float fMinRange; // [esp+0h] [ebp-30h] BYREF
  float fMaxRange; // [esp+4h] [ebp-2Ch] BYREF
  float fCenterRange; // [esp+8h] [ebp-28h]
  float fTargetY; // [esp+Ch] [ebp-24h]
  float fLowerBound; // [esp+10h] [ebp-20h]
  float fUpperBound; // [esp+14h] [ebp-1Ch]

  *piOvertakeFlag = 0;                          // Initialize collision flag to no collision
  if (iTargetCarIdx != -1 && fSteeringInput < fabs(fSaturationThreshold))// Check if target car exists and steering input is within saturation threshold
  {
    fUpperBound = CarBaseY + Car[iTargetCarIdx].fCarHalfWidth + 200.0f + Car[iTargetCarIdx].pos.fY;// Calculate upper avoidance boundary (car position + half width + safety margin)
    fLowerBound = -CarBaseY - Car[iTargetCarIdx].fCarHalfWidth + -200.0f + Car[iTargetCarIdx].pos.fY;// Calculate lower avoidance boundary (car position - half width - safety margin)
    if (fMaxOutput < (double)fUpperBound && fMaxOutput >(double)fLowerBound)// Check if our car Y position is within avoidance zone
    {
      driverange(&Car[iTargetCarIdx], &fMinRange, &fMaxRange);// Get driving range boundaries for target car
      iDriverOffset = iCurrentCarIdx + Car[iTargetCarIdx].iDriverIdx;// Calculate combined driver index for avoidance direction
      fCenterRange = (fMinRange + fMaxRange) * 0.5f;// Calculate center of driving range
      if ((iDriverOffset & 1) != 0)           // Choose avoidance direction based on driver index parity
        dTargetY = Car[iTargetCarIdx].pos.fY + -100.0;// Odd driver index: avoid by going down (-100 offset)
      else
        dTargetY = Car[iTargetCarIdx].pos.fY + 100.0;// Even driver index: avoid by going up (+100 offset)
      fTargetY = (float)dTargetY;
      if (fTargetY <= (double)fCenterRange) {                                         // Adjust upper boundary to minimum range if needed
        if (fUpperBound > (double)fMinRange) {
          fUpperBound = fMinRange;
          if (fSteeringInput > 0.0
            && Car[iCurrentCarIdx].fFinalSpeed > 80.0
            && Car[iCurrentCarIdx].fFinalSpeed > Car[iTargetCarIdx].fFinalSpeed * tcos[Car[iTargetCarIdx].nActualYaw] + 120.0)// Check for aggressive overtaking conditions (speed advantage)
          {
            *piOvertakeFlag = -1;
          }
        }
        if (fMaxOutput <= (double)fUpperBound)
          return (float)interpolatesteer(fSteeringInput, fSaturationThreshold, fDeadzoneThreshold, fMaxOutput, fUpperBound);// Return interpolated steering to avoid upper boundary
      } else {                                         // Adjust lower boundary to maximum range if needed
        if (fLowerBound < (double)fMaxRange) {
          fLowerBound = fMaxRange;
          if (fSteeringInput > 0.0
            && Car[iCurrentCarIdx].fFinalSpeed > 80.0
            && Car[iCurrentCarIdx].fFinalSpeed > Car[iTargetCarIdx].fFinalSpeed * tcos[Car[iTargetCarIdx].nActualYaw] + 120.0)// Check for aggressive overtaking conditions (speed advantage)
          {
            *piOvertakeFlag = -1;
          }
        }
        if (fMaxOutput >= (double)fLowerBound)
          return (float)interpolatesteer(fSteeringInput, fSaturationThreshold, fDeadzoneThreshold, fMaxOutput, fLowerBound);// Return interpolated steering to avoid lower boundary
      }
    }
  }
  return fMaxOutput;                            // No avoidance needed, return original steering value
}

//-------------------------------------------------------------------------------------------------
//00031B40
double block(int iCarIdx, float fSteeringInput, float fMaxOutput, float fSaturationThreshold, float fDeadzoneThreshold)
{
  if (iCarIdx == -1)
    return fMaxOutput;
  else
    return (float)interpolatesteer(fSteeringInput, fSaturationThreshold, fDeadzoneThreshold, fMaxOutput, Car[iCarIdx].pos.fY);
}

//-------------------------------------------------------------------------------------------------
//00031BA0
void autocar2(tCar *pCar)
{
  tCarStrategy *pStrategy; // ebp
  int iCountdown; // ebx
  int iCurrChunk; // edx
  double dAIMaxSpeed; // st7
  int iHumanBestPosition; // edi
  int iCarIdx; // eax
  int iLoopIdx; // edx
  int iCarArrayIdx; // ebx
  int byRacePosition; // ecx
  int iPositionDifference; // eax
  double dSpeedAdjustment; // st7
  double dSpeedReduction; // st7
  int iCurrentPosition; // eax
  double dReducedSpeed; // st7
  double dSpeedDifference; // st7
  int iSelectedStrategy; // edi
  double dTargetReducedSpeed; // st7
  double dSpeedDelta; // st7
  uint8 byCurrentRacePos; // bl
  int iCurrentChunk; // ebx
  int iRandomPitStop; // eax
  int iPitStopTaken; // ebx
  int iCarLoopIdx; // eax
  int iChunkForPitStop; // eax
  int iPrevChunk; // eax
  int iPlayerType; // eax
  double dAvoidanceY; // st7
  int iAITargetCar; // eax
  float fInterpolatedY; // eax
  int iTargetAngle; // eax
  double dAvoidanceResult; // st7
  double dYPositionDiff; // st7
  double dTargetCarSpeed; // st7
  double dSpeedDiffCalc; // st7
  //__int16 nFPUStatus; // fps
  double dCalculatedAngle; // st7
  int iSteeringSensitivity; // eax
  int iRandomValue; // eax
  double fRPMRatio; // [esp+0h] [ebp-ACh]
  float fRightTime; // [esp+8h] [ebp-A4h] BYREF
  float fLeftTime; // [esp+Ch] [ebp-A0h] BYREF
  int iLeftCarIdx; // [esp+10h] [ebp-9Ch] BYREF
  float fTargetY; // [esp+14h] [ebp-98h] BYREF
  int iRightCarIdx; // [esp+18h] [ebp-94h] BYREF
  float fTargetX; // [esp+1Ch] [ebp-90h] BYREF
  int iActionFlag; // [esp+20h] [ebp-8Ch] BYREF
  float fDeltaX; // [esp+24h] [ebp-88h]
  float fDeltaXCopy; // [esp+28h] [ebp-84h]
  float fDragCoefficient; // [esp+2Ch] [ebp-80h]
  float fStrategyParam1; // [esp+30h] [ebp-7Ch]
  float fDeltaYCopy; // [esp+34h] [ebp-78h]
  float fSteerSensitivity; // [esp+38h] [ebp-74h]
  float fStrategyA1; // [esp+3Ch] [ebp-70h]
  float fSteerSensitivity_1; // [esp+40h] [ebp-6Ch]
  float fStrategyA0_3; // [esp+44h] [ebp-68h]
  float fTrackDistance; // [esp+48h] [ebp-64h]
  float fStrategyParam2; // [esp+4Ch] [ebp-60h]
  float fCalculatedY; // [esp+50h] [ebp-5Ch]
  float fLeftInterpolated; // [esp+54h] [ebp-58h]
  float fDeltaY; // [esp+58h] [ebp-54h]
  float fRightInterpolated; // [esp+5Ch] [ebp-50h]
  float fDefensiveY; // [esp+60h] [ebp-4Ch]
  float fForwardSpeed; // [esp+64h] [ebp-48h]
  int iAccelerationControl; // [esp+68h] [ebp-44h]
  int iPitZoneFlag; // [esp+6Ch] [ebp-40h]
  int iLastLapFlag; // [esp+70h] [ebp-3Ch]
  float fMaxEngineSpeed; // [esp+74h] [ebp-38h]
  tCarEngine *pEngine; // [esp+78h] [ebp-34h]
  int iDistanceToTarget; // [esp+7Ch] [ebp-30h]
  int iAITargetSpeed; // [esp+80h] [ebp-2Ch]
  float fTargetSteerY; // [esp+84h] [ebp-28h]
  int iPitStopTarget; // [esp+88h] [ebp-24h]
  int iAIThrottleControl; // [esp+8Ch] [ebp-20h]
  int iHumanCarIdx; // [esp+90h] [ebp-1Ch]

  iActionFlag = 0;
  pEngine = &CarEngines.engines[pCar->byCarDesignIdx];// Initialize car engine and strategy pointers
  pStrategy = &CarStrategy[pCar->iDriverIdx];
  fDragCoefficient = pEngine->fDragCoefficient;
  iCountdown = pCar->iAIUpdateTimer - 1;
  fMaxEngineSpeed = pEngine->pSpds[pEngine->iNumGears - 1];
  pCar->iAIUpdateTimer = iCountdown;
  if (iCountdown >= 0)
    return;
  changestrategy(pCar);
  changeline(pCar);
  iCurrChunk = pCar->nCurrChunk;
  dAIMaxSpeed = localdata[iCurrChunk].fAIMaxSpeed;// Calculate AI maximum speed based on track chunk
  //_CHP();
  //LOBYTE(iCurrChunk) = HIBYTE(TrakColour[iCurrChunk][1]);
  iAITargetSpeed = (int)dAIMaxSpeed;
  if ((TrakColour[iCurrChunk][1] & SURFACE_FLAG_AI_MAX_SPEED) == 0) {
  //if ((iCurrChunk & 0x10) == 0) {
    if (!winner_mode) {
      fMaxEngineSpeed = fMaxEngineSpeed * levels[level] * 0.01f;// Apply difficulty level scaling to AI maximum speed
      if ((double)iAITargetSpeed > fMaxEngineSpeed) {
        //_CHP();
        iAITargetSpeed = (int)fMaxEngineSpeed;
      }
    }
    //LODWORD(fDeltaX) = 2 * pCar->byRacePosition;
    iHumanBestPosition = 1000;
    if (pCar->fTotalRaceTime <= 80.0 - (double)(2 * pCar->byRacePosition))
    //if (pCar->fTotalRaceTime <= 80.0 - (double)SLODWORD(fDeltaX))
      goto LABEL_18;
    iHumanCarIdx = -1;
    iCarIdx = 0;
    if (numcars > 0)                          // Find human player with best race position for AI targeting
    {
      iLoopIdx = 0;
      iCarArrayIdx = 0;
      do {
        if (human_control[iLoopIdx]) {
          byRacePosition = Car[iCarArrayIdx].byRacePosition;
          if (byRacePosition < iHumanBestPosition) {
            iHumanCarIdx = iCarIdx;
            iHumanBestPosition = byRacePosition;
          }
        }
        ++iLoopIdx;
        ++iCarIdx;
        ++iCarArrayIdx;
      } while (iCarIdx < numcars);
    }
    iPositionDifference = (char)pCar->byLapNumber * TRAK_LEN + pCar->nCurrChunk - (TRAK_LEN * (char)Car[iHumanCarIdx].byLapNumber + Car[iHumanCarIdx].nCurrChunk);// Calculate position difference between AI car and leading human player
    if (iPositionDifference < 80) {
      if (iPositionDifference <= 20) {                                         // Special logic for car design 13 (boss car) - reduce speed when near human player
      LABEL_18:
        if (pCar->byCarDesignIdx == 13) {
          iCurrentPosition = pCar->byRacePosition;
          if (iCurrentPosition < iHumanBestPosition && iCurrentPosition + 4 >= iHumanBestPosition) {
            if (Car[iHumanCarIdx].fFinalSpeed <= 200.0) {
              dReducedSpeed = Car[iHumanCarIdx].fFinalSpeed + -20.0;
              //_CHP();
              iAITargetSpeed = (int)dReducedSpeed;
            } else {
              iAITargetSpeed = 100;
            }
          }
        }
        goto LABEL_24;
      }
      //LODWORD(fDeltaX) = iPositionDifference - 20;
      dSpeedReduction = (double)(iPositionDifference - 20) * mineff[level];
      //LODWORD(fDeltaX) = iPositionDifference - 80;
      dSpeedAdjustment = ((double)(iPositionDifference - 80) - dSpeedReduction) * -0.016666668 * (double)iAITargetSpeed;
    } else {
      dSpeedAdjustment = (double)iAITargetSpeed * mineff[level];
    }
    //_CHP();
    iAITargetSpeed = (int)dSpeedAdjustment;
    goto LABEL_18;
  }
LABEL_24:
  if (pCar->iBobMode) {
    if (tcos[pCar->nYaw] > 0.0)
      LABEL_28:
    iAITargetSpeed = 25;
  } else if (tcos[pCar->nYaw] < 0.0) {
    goto LABEL_28;
  }
  dSpeedDifference = (double)iAITargetSpeed - pCar->fFinalSpeed;
  //_CHP();
  iAIThrottleControl = (int)dSpeedDifference;
  iAccelerationControl = (int)dSpeedDifference;
  findnearcars(pCar, &iLeftCarIdx, &fLeftTime, &iRightCarIdx, &fRightTime, &fTargetX, &fTargetY);// Find nearby cars for collision avoidance and targeting
  iSelectedStrategy = pCar->iSelectedStrategy;
  if (pCar->fHealth >= 30.0)                  // AI strategy selection based on car health and nearby cars
  {
    if (iRightCarIdx == -1 || fRightTime < 0.0) {
      if (iLeftCarIdx != -1 && fLeftTime >= 0.0) {
        if (Car[iLeftCarIdx].fHealth < 25.0 && !finished_car[iLeftCarIdx])
          iSelectedStrategy = Car[iLeftCarIdx].fHealth > 0.0 && ((uint8)iRightCarIdx & (pCar->iDriverIdx == 65534) & 0xFE) == 0 && pCar->fHealth >= 40.0;
        if (human_control[iLeftCarIdx] && ((cheat_mode & 2) != 0 || pCar->byCarDesignIdx == 13) && Car[iLeftCarIdx].fHealth > 0.0 && !finished_car[iLeftCarIdx]) {
          iSelectedStrategy = 1;
          if (pCar->byCarDesignIdx == 13 && fLeftTime < 3.0) {
            dTargetReducedSpeed = Car[iLeftCarIdx].fFinalSpeed + -20.0;
            //_CHP();
            iAITargetSpeed = (int)dTargetReducedSpeed;
            dSpeedDelta = dTargetReducedSpeed - pCar->fFinalSpeed;
            //_CHP();
            iAccelerationControl = (int)dSpeedDelta;
          }
        }
      }
    } else {
      if (Car[iRightCarIdx].fHealth < 25.0 && !finished_car[iRightCarIdx]) {
        if (Car[iRightCarIdx].fHealth <= 0.0 || ((uint8)iRightCarIdx & (pCar->iDriverIdx == 65534) & 0xFE) != 0 || pCar->fHealth < 40.0)
          iSelectedStrategy = 0;
        else
          iSelectedStrategy = 3;
      }
      if (human_control[iRightCarIdx] && ((cheat_mode & 2) != 0 || pCar->byCarDesignIdx == 13) && Car[iRightCarIdx].fHealth > 0.0 && !finished_car[iRightCarIdx]) {
        iSelectedStrategy = 3;
        iAccelerationControl = 1;
      }
    }
  } else {
    pCar->iSelectedStrategy = 0;
  }
  if (iRightCarIdx != -1 && iSelectedStrategy == 3 && Car[iRightCarIdx].byDebugDamage)
    iSelectedStrategy = 0;
  if (iLeftCarIdx != -1 && Car[iLeftCarIdx].byDebugDamage && iSelectedStrategy <= 3)
    iSelectedStrategy = 0;
  if (iLeftCarIdx != -1 && pCar->byRacePosition < 4u && pCar->byRacePosition - Car[iLeftCarIdx].byRacePosition > 2 && !human_control[iLeftCarIdx])
    iSelectedStrategy = 0;
  if (iRightCarIdx != -1) {
    byCurrentRacePos = pCar->byRacePosition;
    //left car check added by ROLLER, todo look at asm and see if correct
    if (byCurrentRacePos < 4u && iLeftCarIdx != -1 && byCurrentRacePos - Car[iLeftCarIdx].byRacePosition > 2 && !human_control[iLeftCarIdx])
      iSelectedStrategy = 0;
  }
  if ((TrakColour[pCar->nCurrChunk][1] & SURFACE_FLAG_AI_FAST_STRAT) == 0)// Check if car is in not in go fast zone
    iAIThrottleControl = iAccelerationControl;
  else
    iSelectedStrategy = 4;
  if (winner_mode)
    iSelectedStrategy = 4;
  iCurrentChunk = pCar->nCurrChunk;
  iPitZoneFlag = TrakColour[iCurrentChunk][1] & SURFACE_FLAG_PIT_ZONE;
  iLastLapFlag = 0;
  if ((char)pCar->byLapNumber == NoOfLaps && TRAK_LEN - iCurrentChunk < 200)
    iLastLapFlag = -1;
  if (death_race) {
    iLastLapFlag = -1;
    iPitZoneFlag = 0;
  }
  if (!iPitZoneFlag || pCar->fHealth >= 60.0 || iLastLapFlag) {
    pCar->iAITargetCar = -1;
  } else if (pCar->iAITargetCar == -1) {
    iRandomPitStop = rand();                    // Select random pit stop target for AI car
    iPitStopTaken = 0;
    iPitStopTarget = stops[GetHighOrderRand(numstops, iRandomPitStop)];
    if (numcars > 0) {
      iCarLoopIdx = 0;
      do {
        if (Car[iCarLoopIdx].iAITargetCar == iPitStopTarget)
          iPitStopTaken = -1;
        ++iCarLoopIdx;
      } while (iCarLoopIdx < numcars);
    }
    if (!iPitStopTaken)
      pCar->iAITargetCar = iPitStopTarget;
  }
  if (iPitZoneFlag && !pCar->iAICurrentLine && !iLastLapFlag) {
    iSelectedStrategy = pCar->fHealth >= 60.0 ? 0 : 5;
    if (pCar->fHealth < 90.0)                 // Force pit stop strategy (5) if health is low and in pit zone
      iSelectedStrategy = 5;
  }
  if (pCar->byDebugDamage)
    iSelectedStrategy = 5;
  if (iSelectedStrategy == 5) {
    iChunkForPitStop = pCar->nCurrChunk;
    if (iChunkForPitStop != -1) {
      if (iPitZoneFlag) {
        iPrevChunk = iChunkForPitStop - 1;
        if (iPrevChunk < 0)
          iPrevChunk = TRAK_LEN - 1;
        if (!pCar->byPitLaneActiveFlag && (TrakColour[iPrevChunk][1] & SURFACE_FLAG_PIT_ZONE) == 0) {
          pCar->byPitLaneActiveFlag = -1;
          iPlayerType = player_type;
          pCar->nChangeMateCooldown = 1080;
          if (iPlayerType != 2
            && (cheat_mode & 0x4000) == 0
            && player1_car != pCar->iDriverIdx
            && pCar->byCarDesignIdx == Car[player1_car].byCarDesignIdx
            && pCar->byCarDesignIdx <= 7u
            && readsample == writesample
            && lastsample < -18) {
            speechsample(pCar->byCarDesignIdx + 71, 20000, 18, player1_car + 17920);
            speechsample(SOUND_SAMPLE_TPITS, 20000, 0, player1_car);// SOUND_SAMPLE_TPITS
          }
        }
      }
    }
  }
  switch (iSelectedStrategy) {
    case 0:
      if (iRightCarIdx != -1 && fRightTime >= 0.0)
        goto LABEL_175;
      if (iLeftCarIdx != pCar->iTrackedCarIdx)
        goto LABEL_154;
      goto LABEL_178;
    case 1:
      if (iRightCarIdx != -1 && fRightTime >= 0.0)
        goto LABEL_175;
      if (iLeftCarIdx == pCar->iTrackedCarIdx && fLeftTime < 0.0)
        goto LABEL_178;
      if (iLeftCarIdx >= 0 && pCar->byCarDesignIdx == Car[iLeftCarIdx].byCarDesignIdx)
        goto LABEL_154;
      pCar->iTrackedCarIdx = -1;
      fStrategyParam1 = pStrategy->fSteerSensitivity;
      fStrategyParam2 = pStrategy->fSteerDamping;
      if (iLeftCarIdx == -1)
        fCalculatedY = fTargetY;
      else
        fCalculatedY = (float)interpolatesteer(fLeftTime, fStrategyParam1, fStrategyParam2, fTargetY, Car[iLeftCarIdx].pos.fY);
      fInterpolatedY = fCalculatedY;
      goto LABEL_156;
    case 2:
      if (iRightCarIdx == -1 || fRightTime < 0.0) {
        if (iLeftCarIdx == pCar->iTrackedCarIdx && fLeftTime < 0.0) {
        LABEL_178:
          fTargetSteerY = (float)avoid(pCar->iDriverIdx, iLeftCarIdx, fLeftTime, fTargetY, pStrategy->fAvoidSensitivity, pStrategy->fAvoidReaction, &iActionFlag);
        } else {
          //index check added by ROLLER
          if (iLeftCarIdx >= 0 && pCar->byCarDesignIdx == Car[iLeftCarIdx].byCarDesignIdx) {
          LABEL_154:
            pCar->iTrackedCarIdx = -1;
            goto LABEL_155;
          }
          pCar->iTrackedCarIdx = -1;
          fStrategyA0_3 = pStrategy->fSteerSensitivity;
          fStrategyA1 = pStrategy->fSteerDamping;
          if (iLeftCarIdx == -1)
            fDefensiveY = fTargetY;
          else
            fDefensiveY = (float)interpolatesteer(fLeftTime, fStrategyA0_3, fStrategyA1, fTargetY, Car[iLeftCarIdx].pos.fY);

          //index check added by ROLLER
          if (iLeftCarIdx == -1)
            dYPositionDiff = 0;
          else
            dYPositionDiff = pCar->pos.fY - Car[iLeftCarIdx].pos.fY;
            
          fTargetSteerY = fDefensiveY;
          if (CarBaseY * 2.0 + -200.0 > fabs(dYPositionDiff) && fLeftTime > 0.0) {
            dTargetCarSpeed = Car[iLeftCarIdx].fFinalSpeed * 0.2;
            //_CHP();
            iAIThrottleControl = (int)dTargetCarSpeed;
            if ((int)dTargetCarSpeed < 80)
              iAIThrottleControl = 80;
            if (iAIThrottleControl > iAITargetSpeed)
              iAIThrottleControl = iAITargetSpeed;
            dSpeedDiffCalc = (double)iAIThrottleControl - pCar->fFinalSpeed;
            //_CHP();
            iAIThrottleControl = (int)dSpeedDiffCalc;
          }
        }
      } else {
      LABEL_175:
        dAvoidanceResult = avoid(pCar->iDriverIdx, iRightCarIdx, fRightTime, fTargetY, pStrategy->fAvoidSensitivity, pStrategy->fAvoidReaction, &iActionFlag);
        pCar->iTrackedCarIdx = iRightCarIdx;
        fTargetSteerY = (float)dAvoidanceResult;
      }
      break;
    case 3:
      pCar->iTrackedCarIdx = -1;
      if (iRightCarIdx == -1 || fRightTime < 0.0 || fRightTime >(double)fLeftTime && fLeftTime >= 0.0 || pCar->byCarDesignIdx == Car[iRightCarIdx].byCarDesignIdx) {
        if (iLeftCarIdx == -1 || fLeftTime < 0.0 || fLeftTime >(double)fRightTime && fRightTime >= 0.0 || pCar->byCarDesignIdx == Car[iLeftCarIdx].byCarDesignIdx) {
        LABEL_155:
          fInterpolatedY = fTargetY;
        } else {
          fSteerSensitivity = pStrategy->fSteerSensitivity;
          fLeftInterpolated = (float)interpolatesteer(fLeftTime, fSteerSensitivity, pStrategy->fSteerDamping, fTargetY, Car[iLeftCarIdx].pos.fY);
          fInterpolatedY = fLeftInterpolated;
        }
      LABEL_156:
        fTargetSteerY = fInterpolatedY;
      } else {
        fSteerSensitivity_1 = pStrategy->fSteerSensitivity;
        fRightInterpolated = (float)interpolatesteer(fRightTime, fSteerSensitivity_1, pStrategy->fSteerDamping, fTargetY, Car[iRightCarIdx].pos.fY);
        fTargetSteerY = fRightInterpolated;
        iAIThrottleControl = 1;
      }
      break;
    case 4:
      fTargetSteerY = fTargetY;
      pCar->iTrackedCarIdx = -1;
      break;
    case 5:
      if (iRightCarIdx == -1 || fRightTime < 0.0) {
        if (iLeftCarIdx == pCar->iTrackedCarIdx) {
          fTargetSteerY = (float)avoid(pCar->iDriverIdx, iLeftCarIdx, fLeftTime, fTargetY, pStrategy->fAvoidSensitivity, pStrategy->fAvoidReaction, &iActionFlag);
        } else {
          pCar->iTrackedCarIdx = -1;
          fTargetSteerY = fTargetY;
        }
      } else {
        dAvoidanceY = avoid(pCar->iDriverIdx, iRightCarIdx, fRightTime, fTargetY, pStrategy->fAvoidSensitivity, pStrategy->fAvoidReaction, &iActionFlag);
        pCar->iTrackedCarIdx = iRightCarIdx;
        fTargetSteerY = (float)dAvoidanceY;
      }
      iAITargetCar = pCar->iAITargetCar;
      if (iAITargetCar != -1) {
        iDistanceToTarget = iAITargetCar - pCar->nCurrChunk;
        if (iDistanceToTarget < 0)
          iDistanceToTarget += TRAK_LEN;
        if (TRAK_LEN / 2 < iDistanceToTarget)
          iDistanceToTarget -= TRAK_LEN;
        fForwardSpeed = pCar->fFinalSpeed * tcos[pCar->nActualYaw & 0x3FFF];
        fTrackDistance = localdata[pCar->nCurrChunk].fTrackHalfLength * 2.0f * (float)iDistanceToTarget;
        if (iDistanceToTarget < 0 || fabs(fTrackDistance / fForwardSpeed) < fDragCoefficient * 36.0 * fForwardSpeed / fMaxEngineSpeed)
          iActionFlag = -1;
      }
      //if ((LODWORD(pCar->fFinalSpeed) & 0x7FFFFFFF) == 0)
      if (fabs(pCar->fFinalSpeed) == 0)
        pCar->byDebugDamage = -1;
      if ((TrakColour[pCar->nCurrChunk][pCar->iLaneType] & SURFACE_FLAG_PIT) == 0 && pCar->fFinalSpeed < 40.0)
        iActionFlag = 0;
      break;
    default:
      break;                                    // AI strategy selection: 0=Normal, 1=Aggressive, 2=Defensive, 3=Attack, 4=Fast, 5=Pits
  }
  if (pCar->byDebugDamage)
    iActionFlag = -1;
  if (iActionFlag)
    iAIThrottleControl = -1;
  fDeltaX = fTargetX - pCar->pos.fX;
  fDeltaY = fTargetSteerY - pCar->pos.fY;
  fDeltaXCopy = fDeltaX;
  fDeltaYCopy = fDeltaY;
  //if ((LODWORD(fDeltaX) & 0x7FFFFFFF) != 0 || (LODWORD(fDeltaY) & 0x7FFFFFFF) != 0) {
  if (fabs(fDeltaX) > FLT_EPSILON || fabs(fDeltaY) > FLT_EPSILON) {
    dCalculatedAngle = atan2(fDeltaYCopy, fDeltaXCopy) * 16384.0 / 6.28318530718;// Calculate steering angle to target position
    //_CHP();
    //LODWORD(fDeltaX) = (int)dCalculatedAngle;
    iTargetAngle = (int)dCalculatedAngle & 0x3FFF;
  } else {
    iTargetAngle = 0;
    //LOWORD(iTargetAngle) = 0;
  }
  iSteeringSensitivity = ((int16)iTargetAngle - pCar->nYaw) & 0x3FFF;// Apply steering input with engine sensitivity limits
  if (iSteeringSensitivity > 0x2000)
    iSteeringSensitivity -= 0x4000;
  if (iSteeringSensitivity < -pEngine->iSteeringSensitivity)
    iSteeringSensitivity = -pEngine->iSteeringSensitivity;
  if (iSteeringSensitivity > pEngine->iSteeringSensitivity)
    iSteeringSensitivity = pEngine->iSteeringSensitivity;
  pCar->iSteeringInput = iSteeringSensitivity;
  if ((char)pCar->byGearAyMax == -1)          // AI gear shifting and RPM control based on engine state
  {
    fRPMRatio = pCar->fRPMRatio;
    if (fRPMRatio >= 0.1) {
      if (fRPMRatio < 0.9) {
        iRandomValue = rand();
        if (GetHighOrderRand(72, iRandomValue)) {
          iAIThrottleControl = pCar->byAIThrottleControl;
        } else {
          iAIThrottleControl = -pCar->byAIThrottleControl;
          pCar->byAIThrottleControl = iAIThrottleControl;
        }
      } else {
        pCar->byAIThrottleControl = -1;
        iAIThrottleControl = -1;
      }
    } else {
      pCar->byAIThrottleControl = 1;
      iAIThrottleControl = 1;
    }
  }
  //if (iAIThrottleControl > 0 && (LODWORD(pCar->fHealth) & 0x7FFFFFFF) == 0)
  if (iAIThrottleControl > 0 && fabs(pCar->fHealth) == 0)
    iAIThrottleControl = -1;
  if (champ_mode && !champ_go[pCar->iDriverIdx])
    iAIThrottleControl = -1;
  if (iAIThrottleControl <= 0)                // Apply acceleration, deceleration, or freewheeling based on AI decision
  {
    if (iAIThrottleControl >= 0)
      FreeWheel(pCar);
    else
      Decelerate(pCar);
  } else {
    Accelerate(pCar);
  }
}

//-------------------------------------------------------------------------------------------------
//00032BF0
void changestrategy(tCar *pCar)
{
  tCarStrategy *pStrategyData; // ecx
  int iOldStrategy; // edi
  int iRandStrategy; // eax
  int iRandProbability; // eax
  int iSelectedStrategy; // ebx
  int iProbabilityRemaining; // edx
  int *piStrategyIterator; // eax
  int iCurrentProbability; // ebp
  int iRandFlip; // eax
  int iRandRevenge; // eax
  unsigned int uiRacePosition; // ebx
  int iRevengeProbability; // ebx
  int iRandRevengeCheck; // eax
  uint8 byCarDesignIdx; // dl
  int iRandRevengeAlt; // eax
  uint8 byCarDesignCopy; // bl

  pStrategyData = &CarStrategy[pCar->iDriverIdx];// Get strategy data for this driver (40 bytes per driver)
  if (pCar->nChangeMateCooldown)              // Skip strategy change if cooldown timer active
    return;
  iOldStrategy = pCar->iSelectedStrategy;
  if (pCar->fTotalRaceTime > 40.0)            // Only allow strategy changes after 40 seconds of race time
  {
    iRandStrategy = rand();
    if (GetHighOrderRand(800, iRandStrategy) == 100)// 1 in 800 chance
    {
      iRandProbability = rand();
      iSelectedStrategy = 0;
      iProbabilityRemaining = GetHighOrderRand(100, iRandProbability);// Select strategy based on probability table (0-99 random)
      for (piStrategyIterator = (int *)pStrategyData; iProbabilityRemaining >= *piStrategyIterator; ++iSelectedStrategy) {
        iCurrentProbability = *piStrategyIterator++;
        iProbabilityRemaining -= iCurrentProbability;
      }
      if (pCar->byRacePosition < 4u && iSelectedStrategy == 2)// Front runners (positions 0-3) cannot use strategy 2, downgrade to 1
        iSelectedStrategy = 1;
      pCar->iSelectedStrategy = iSelectedStrategy;
      if (iSelectedStrategy) {
        iRandFlip = rand();
        if (GetHighOrderRand(100, iRandFlip) < flipst[level])// Chance to flip aggressive strategy back to normal based on level
          pCar->iSelectedStrategy = 0;
      }
      // CHEAT_MODE_DEATH_MODE
      if ((cheat_mode & CHEAT_MODE_DEATH_MODE) != 0)              // Death mode forces normal strategy (no aggression)
        pCar->iSelectedStrategy = 0;
    }
    iRandRevenge = rand();
    if (GetHighOrderRand(720, iRandRevenge) != 250)
      goto LABEL_39;                            // 1 in 720 chance for revenge mode consideration
    // CHEAT_MODE_DEATH_MODE
    if ((cheat_mode & CHEAT_MODE_DEATH_MODE) != 0 || level < 4) {
      uiRacePosition = pCar->byRacePosition;
      if (uiRacePosition >= 7) {
        // CHEAT_MODE_DEATH_MODE
        if ((cheat_mode & CHEAT_MODE_DEATH_MODE) != 0)
          iRevengeProbability = 50 * uiRacePosition - 250;// Death mode: simple position-based revenge (50 * pos - 250)
        else
          iRevengeProbability = (int)(pStrategyData->strategyAy[4] * (uiRacePosition - level)) / 2 + uiRacePosition - level;// Normal mode: complex revenge based on strategy data and position
      } else {
        iRevengeProbability = -1;
      }
      iRandRevengeCheck = rand();
      if (GetHighOrderRand(1000, iRandRevengeCheck) < iRevengeProbability) {                                         // Trigger revenge speech if same car design as player (not in clones mode)
        // CHEAT_MODE_CLONES
        if (player_type != 2 && (cheat_mode & CHEAT_MODE_CLONES) == 0 && !pCar->iBobMode && pCar->byCarDesignIdx == Car[player1_car].byCarDesignIdx) {
          byCarDesignIdx = pCar->byCarDesignIdx;
          if (byCarDesignIdx <= 7u) {
            speechsample(byCarDesignIdx + 71, 20000, 18, player1_car + 17664);
            speechsample(SOUND_SAMPLE_REVERST, 20000, 0, player1_car);// SOUND_SAMPLE_REVERST
          }
        }
        pCar->iBobMode = -1;                // Activate revenge mode (-1)
        goto LABEL_39;
      }
    } else {
      iRandRevengeAlt = rand();
      if (GetHighOrderRand(1000, iRandRevengeAlt) < pStrategyData->strategyAy[4])// Advanced levels (4+): use strategy data for revenge probability
      {
        // CHEAT_MODE_CLONES
        if (player_type != 2 && (cheat_mode & CHEAT_MODE_CLONES) == 0 && !pCar->iBobMode && pCar->byCarDesignIdx == Car[player1_car].byCarDesignIdx && pCar->byCarDesignIdx <= 7u) {
          speechsample(pCar->byCarDesignIdx + 71, 20000, 18, player1_car + 17664);
          speechsample(SOUND_SAMPLE_REVERST, 20000, 0, player1_car);// SOUND_SAMPLE_REVERST
        }
        pCar->iBobMode = -1;
        goto LABEL_39;
      }
    }
    pCar->iBobMode = 0;
  LABEL_39:
    if (winner_mode)                          // Winner mode disables revenge/aggression
      pCar->iBobMode = 0;
  }
  if (iOldStrategy != pCar->iSelectedStrategy)          // If strategy changed, play speech and set cooldown timer
  {
    // CHEAT_MODE_CLONES
    if (player_type != 2 && (cheat_mode & 0x4000) == 0 && pCar->byCarDesignIdx == Car[player1_car].byCarDesignIdx) {
      byCarDesignCopy = pCar->byCarDesignIdx;
      if (byCarDesignCopy <= 7u) {
        speechsample(byCarDesignCopy + 71, 20000, 18, ((pCar->iSelectedStrategy + 61) << 8) + player1_car);
        speechsample(pCar->iSelectedStrategy + 61, 20000, 0, player1_car);
      }
    }
    pCar->nChangeMateCooldown = 1080;           // Set 1080 frame cooldown (18 seconds at 60 FPS)
  }
}

//-------------------------------------------------------------------------------------------------
//00032FB0
int getangle(float fX, float fY)
{
  double dAngle; // st7

  // If floats are both zero, return 0
  // Masking off most significant bit so -0 == 0
  // Did this used to be necessary? It doesn't
  // seem to make a difference now.
  //if ((LODWORD(fX) & 0x7FFFFFFF) == 0 && (LODWORD(fY) & 0x7FFFFFFF) == 0)
  if (fX == 0 && fY == 0)
    return 0;

  // IF_DATAN2 is atan2 that returns a double
  dAngle = atan2(fY, fX) * 16384.0 / 6.28318530718;

  // return value is used as an index into tsin/tcos/ptan lookup tables each with 16384 elements 
  // masking off all but the 14 least significant bits ensures this value maxes out at 16383
  return (int)dAngle & 0x3FFF;
}

//-------------------------------------------------------------------------------------------------
//00033000
void landontrack(tCar *pCar)
{
  tTrackInfo *pTrackInfo; // ebp
  int iNextChunkIdx; // eax
  tData *pCurrentData; // esi
  tTrackInfo *pNextTrackInfo; // ecx
  tData *pNextData; // ebx
  double dTransformedY; // st7
  double dTransformedX; // st5
  double dTransformedZ; // rt0
  bool bRoofCollision; // eax
  double dWallNormalY; // st7
  int iRollMomentumBackup; // ecx
  double dDamageAmount; // st7
  double dSoundVolume; // st7
  double dLocalY; // st7
  double dLocalX; // st5
  double dLocalZ; // st6
  double dTempX; // st6
  double dTempZ; // st7
  double dPositionZ; // st7
  float fDirectionX; // edx
  int iChunkIdx; // eax
  //__int16 nFpuStack1; // fps
  double dAngle1; // st7
  bool bRoofCheck; // eax
  double dWallPosition; // st7
  double dDamageLeft; // st7
  int iRollBackup; // edx
  double dVolumeCalc; // st7
  double dVelocityY; // st7
  double dVelocityX; // st5
  double dVelocityZ; // st6
  double dLocalPosX; // st6
  double dLocalPosZ; // st7
  double dFinalPosZ; // st7
  float fDirX; // ecx
  //__int16 nFpuStack2; // fps
  double dAngle2; // st7
  long double dHorizontalSpeed; // st7
  double dNegTrackWidth; // st7
  int iGroundType; // edx
  //int64 llTrackColor; // rax
  int iChunkIndex; // ecx
  float fTransformX; // edx
  //__int16 nFpuStack3; // fps
  double dAngle3; // st7
  int iBankAdjusted; // edx
  int iElevationAdjusted; // eax
  double dHeightDiff; // st7
  int iBankValue; // eax
  int iStunned; // eax
  double dImpactVolume; // st7
  int iSfxType; // eax
  double dStunnedVolume; // st7
  int iGroundColorType; // edx
  double dSeparatedY; // st7
  double dSeparatedX; // st5
  double dSeparatedZ; // rt2
  tGroundPt *pGroundPts; // edx
  double dGroundY1; // st7
  double dGroundX1; // st5
  double dGroundZ1; // st4
  double dGroundY0; // st7
  double dGroundX0; // st5
  double dGroundZ0; // st4
  double dSlopeDiff; // st7
  double dUndergroundDamage; // st7
  int iRollBackup2; // edx
  double dNegSpeed; // st7
  double dBankSoundVolume; // st7
  double dInterpolatedY; // st7
  double dInterpolatedX; // st5
  double dInterpolatedZ; // st6
  double dDirectionY2; // st7
  double dDirectionX2; // st5
  double dDirectionZ2; // st6
  double dFinalDirectionZ; // st7
  float fDirXFinal; // ecx
  int dYawCalc; // eax
  //__int16 nFpuStack4; // fps
  double dAngle4; // st7
  double dGroundY2; // st7
  double dGroundX2; // st5
  double dGroundZ2; // st4
  double dSlopeDiff2; // st7
  double dUndergroundDamage2; // st7
  double dNegSpeed2; // st7
  double dBankSoundVolume2; // st7
  double dInterpolatedY2; // st7
  double dInterpolatedX2; // st5
  double dInterpolatedZ2; // st6
  double dDirectionY3; // st7
  double dDirectionX3; // st5
  double dDirectionZ3; // st6
  double dFinalDirectionZ2; // st7
  float fDirXFinal2; // ecx
  //__int16 nFpuStack5; // fps
  double dAngle5; // st7
  double dSeparatedY2; // st7
  double dSeparatedX2; // st5
  double dSeparatedZ2; // rt0
  tGroundPt *pGroundPts2; // edx
  double dGroundY4; // st7
  double dGroundX4; // st5
  double dGroundZ4; // st4
  double dGroundY5; // st7
  double dGroundX5; // st5
  double dGroundZ5; // st4
  double dSlopeDiff5; // st7
  double dUndergroundDamage5; // st7
  double dBankSoundVolume5; // st7
  double dGroundY3; // st7
  double dGroundX3; // st5
  double dGroundZ3; // st6
  double dDirectionY4; // st7
  double dDirectionX4; // st5
  double dDirectionZ4; // st6
  double dFinalDirectionZ4; // st7
  float fDirXFinal5; // ebx
  //__int16 nFpuStack6; // fps
  double dAngle6; // st7
  double dGroundY6; // st7
  double dGroundX6; // st5
  double dGroundZ6; // st4
  double dSlopeDiff6; // st7
  double dUndergroundDamage6; // st7
  double dBankSoundVolume6; // st7
  double dInterpolatedY3; // st7
  double dInterpolatedX3; // st5
  double dInterpolatedZ3; // st6
  double dDirectionY5; // st7
  double dDirectionX5; // st5
  double dDirectionZ5; // st6
  double dFinalDirectionZ5; // st7
  float fDirXFinal6; // edx
  //__int16 nFpuStack7; // fps
  double dAngle7; // st7
  double dFinalHorizSpeed; // st7
  float fTempDamage; // [esp+0h] [ebp-1ECh]
  float fTempDamage2; // [esp+0h] [ebp-1ECh]
  float fTempDamage3; // [esp+0h] [ebp-1ECh]
  float fThrottle; // [esp+0h] [ebp-1ECh]
  float fUndergroundDamage; // [esp+0h] [ebp-1ECh]
  float fUndergroundDamage2; // [esp+0h] [ebp-1ECh]
  float fUndergroundDamage5; // [esp+0h] [ebp-1ECh]
  float fUndergroundDamage6; // [esp+0h] [ebp-1ECh]
  tData data; // [esp+4h] [ebp-1E8h] BYREF
  int iBank; // [esp+84h] [ebp-168h] BYREF
  int iAzimuth; // [esp+88h] [ebp-164h] BYREF
  int piNearestChunk; // [esp+8Ch] [ebp-160h] BYREF
  int iElevation; // [esp+90h] [ebp-15Ch] BYREF
  float fAngleCalcTemp1; // [esp+94h] [ebp-158h]
  float fDirectionTemp1; // [esp+98h] [ebp-154h]
  float fX; // [esp+9Ch] [ebp-150h]
  float fDirectionTemp2; // [esp+A0h] [ebp-14Ch]
  float fDirectionTemp3; // [esp+A4h] [ebp-148h]
  float fAngleCalcTemp2; // [esp+A8h] [ebp-144h]
  float fAngleCalcTemp3; // [esp+ACh] [ebp-140h]
  float fAngleCalcTemp4; // [esp+B0h] [ebp-13Ch]
  float fGroundYCalc1; // [esp+B4h] [ebp-138h]
  float fAngleCalcTemp5; // [esp+B8h] [ebp-134h]
  float fAngleCalcTemp6; // [esp+BCh] [ebp-130h]
  float fY; // [esp+C0h] [ebp-12Ch]
  float fAngleCalcTemp7; // [esp+C4h] [ebp-128h]
  float fGroundYCalc2; // [esp+C8h] [ebp-124h]
  float fGroundYCalc3; // [esp+CCh] [ebp-120h]
  float fGroundYCalc4; // [esp+D0h] [ebp-11Ch]
  float fTrackHalfWidthNext; // [esp+D4h] [ebp-118h]
  float fHeightDiffCalc; // [esp+D8h] [ebp-114h]
  float fSlopeDiffCalc1; // [esp+DCh] [ebp-110h]
  float fDirectionYTemp1; // [esp+E0h] [ebp-10Ch]
  float fTrackWidthNeg; // [esp+E4h] [ebp-108h]
  float fWallDamageCalc; // [esp+E8h] [ebp-104h]
  float fSlopeDiffCalc2; // [esp+ECh] [ebp-100h]
  float fTrackHalfWidth; // [esp+F0h] [ebp-FCh]
  float fSlopeDiffCalc3; // [esp+F4h] [ebp-F8h]
  float fNegSpeedCalc1; // [esp+F8h] [ebp-F4h]
  float fNegSpeedCalc2; // [esp+FCh] [ebp-F0h]
  float fDirectionYTemp2; // [esp+100h] [ebp-ECh]
  float fHeightDiffGround; // [esp+104h] [ebp-E8h]
  float fPrevZPosition; // [esp+108h] [ebp-E4h]
  float fHeightDiffUnder; // [esp+10Ch] [ebp-E0h]
  float fDirectionYTemp3; // [esp+110h] [ebp-DCh]
  float fGroundHeight; // [esp+114h] [ebp-D8h]
  float fDirectionYTemp4; // [esp+118h] [ebp-D4h]
  float fDirectionYTemp5; // [esp+11Ch] [ebp-D0h]
  float fSlopeDiffCalc4; // [esp+120h] [ebp-CCh]
  float fInterpolatedBoundary; // [esp+124h] [ebp-C8h]
  float fDirectionYTemp6; // [esp+128h] [ebp-C4h]
  float fCarPosZ; // [esp+12Ch] [ebp-C0h]
  float fCarPosX; // [esp+130h] [ebp-BCh]
  float fWallBoundary; // [esp+134h] [ebp-B8h]
  float fGroundYInterp1; // [esp+138h] [ebp-B4h]
  float fGroundZInterp1; // [esp+13Ch] [ebp-B0h]
  float fDirectionZTemp1; // [esp+140h] [ebp-ACh]
  float fCarPosY; // [esp+144h] [ebp-A8h]
  float fDirectionZTemp2; // [esp+148h] [ebp-A4h]
  float fGroundZInterp2; // [esp+14Ch] [ebp-A0h]
  float fGroundZInterp3; // [esp+150h] [ebp-9Ch]
  float fInterpolatedHeight1; // [esp+154h] [ebp-98h]
  float fInterpolatedHeight2; // [esp+158h] [ebp-94h]
  float fDirectionXTemp1; // [esp+15Ch] [ebp-90h]
  float fDirectionXTemp2; // [esp+160h] [ebp-8Ch]
  float fInterpolatedHeight3; // [esp+164h] [ebp-88h]
  float fInterpolatedHeight4; // [esp+168h] [ebp-84h]
  float fVolumeCalc; // [esp+16Ch] [ebp-80h]
  float fGroundYInterp2; // [esp+170h] [ebp-7Ch]
  float fDirectionZTemp3; // [esp+174h] [ebp-78h]
  float fDirectionZTemp4; // [esp+178h] [ebp-74h]
  float fGroundZInterp4; // [esp+17Ch] [ebp-70h]
  float fDirectionXTemp3; // [esp+180h] [ebp-6Ch]
  float fDirectionXTemp4; // [esp+184h] [ebp-68h]
  float fRoofHeightCheck; // [esp+188h] [ebp-64h]
  float fRoofHeight; // [esp+18Ch] [ebp-60h]
  float fGroundYInterp3; // [esp+190h] [ebp-5Ch]
  float fGroundZInterp5; // [esp+194h] [ebp-58h]
  float fDirectionZLocal; // [esp+198h] [ebp-54h]
  float fDirectionXLocal; // [esp+19Ch] [ebp-50h]
  float fDirectionYLocal1; // [esp+1A0h] [ebp-4Ch]
  float fDirectionYLocal2; // [esp+1A4h] [ebp-48h]
  float fDirectionYLocal3; // [esp+1A8h] [ebp-44h]
  float fDirectionYLocal4; // [esp+1ACh] [ebp-40h]
  int iChunk; // [esp+1B0h] [ebp-3Ch]
  float fLocalCoordX; // [esp+1B4h] [ebp-38h]
  float fDirectionYLocal; // [esp+1B8h] [ebp-34h]
  float fLocalCoordY; // [esp+1BCh] [ebp-30h]
  float fLocalCoordZ; // [esp+1C0h] [ebp-2Ch]
  float fTempCalc; // [esp+1C4h] [ebp-28h]
  float fZ; // [esp+1C8h] [ebp-24h]
  float fDirectionYFinal; // [esp+1CCh] [ebp-20h]
  float fDirectionYFinal2; // [esp+1D0h] [ebp-1Ch]

  fX = pCar->posLastFrame.fX;                           // Store car's current and previous positions for collision calculations
  fY = pCar->posLastFrame.fY;
  fZ = pCar->posLastFrame.fZ;
  fCarPosX = pCar->pos.fX;
  fCarPosY = pCar->pos.fY;
  fCarPosZ = pCar->pos.fZ;
  findnearsection(pCar, &piNearestChunk);       // Find nearest track section for collision detection
  if (piNearestChunk == -1)
    iChunk = pCar->iLastValidChunk;
  else
    iChunk = piNearestChunk;
  pTrackInfo = &TrackInfo[iChunk];
  iNextChunkIdx = iChunk + 1;
  pCurrentData = &localdata[iChunk];
  if (iChunk + 1 >= TRAK_LEN)
    iNextChunkIdx = 0;
  pNextTrackInfo = &TrackInfo[iNextChunkIdx];
  pNextData = &localdata[iNextChunkIdx];
  if (GroundColour[iChunk][2] >= 0)           // Handle separated coordinate system for underground sections
    calculateseparatedcoordinatesystem(iChunk, &data);
  dTransformedY = fCarPosY + pCurrentData->pointAy[3].fY;
  dTransformedX = fCarPosX + pCurrentData->pointAy[3].fX;
  dTransformedZ = fCarPosZ + pCurrentData->pointAy[3].fZ;// Transform car position and direction to track local coordinates
  fLocalCoordX = (float)(pCurrentData->pointAy[1].fX * dTransformedY + pCurrentData->pointAy[0].fX * dTransformedX + pCurrentData->pointAy[2].fX * dTransformedZ);
  fLocalCoordY = (float)(pCurrentData->pointAy[0].fY * dTransformedX + pCurrentData->pointAy[1].fY * dTransformedY + pCurrentData->pointAy[2].fY * dTransformedZ);
  fLocalCoordZ = (float)(dTransformedY * pCurrentData->pointAy[1].fZ + dTransformedX * pCurrentData->pointAy[0].fZ + dTransformedZ * pCurrentData->pointAy[2].fZ);
  fPrevZPosition = (fX + pCurrentData->pointAy[3].fX) * pCurrentData->pointAy[0].fZ
    + (fY + pCurrentData->pointAy[3].fY) * pCurrentData->pointAy[1].fZ
    + (fZ + pCurrentData->pointAy[3].fZ) * pCurrentData->pointAy[2].fZ;
  fDirectionXLocal = pCurrentData->pointAy[1].fX * pCar->direction.fY + pCurrentData->pointAy[0].fX * pCar->direction.fX + pCurrentData->pointAy[2].fX * pCar->direction.fZ;
  fDirectionYLocal = pCurrentData->pointAy[0].fY * pCar->direction.fX + pCurrentData->pointAy[1].fY * pCar->direction.fY + pCurrentData->pointAy[2].fY * pCar->direction.fZ;
  fDirectionZLocal = pCurrentData->pointAy[0].fZ * pCar->direction.fX + pCurrentData->pointAy[1].fZ * pCar->direction.fY + pCurrentData->pointAy[2].fZ * pCar->direction.fZ;
  if (fLocalCoordY < 0.0) {
    if (TrakColour[iChunk][4]) {
      fRoofHeight = pTrackInfo->fRoofHeight;    // Handle collision with left wall/barrier (positive Y direction)
      if (fRoofHeight < 0.0)
        bRoofCheck = fLocalCoordZ >= (double)fRoofHeight && fLocalCoordZ <= 0.0;
      else
        bRoofCheck = fLocalCoordZ <= (double)fRoofHeight && fLocalCoordZ >= 0.0;
      if (bRoofCheck && fRoofHeight > 0.0) {
        dWallPosition = TrakColour[iChunk][4] < 0 ? -pCurrentData->fTrackHalfWidth : -pCurrentData->fTrackHalfWidth - pTrackInfo->fRShoulderWidth;
        fWallBoundary = (float)dWallPosition;
        if (fLocalCoordY - CarBaseY <= fWallBoundary) {
          if (fDirectionYLocal < 0.0) {
            if (death_race)
              dDamageLeft = -fDirectionYLocal * 0.006 * 4.0;// Apply wall collision damage (4x in death race mode)
            else
              dDamageLeft = -fDirectionYLocal * 0.006;
            fTempDamage2 = (float)dDamageLeft;
            dodamage(pCar, fTempDamage2);
            iRollBackup = -pCar->iRollMomentum;
            fDirectionYLocal = -fDirectionYLocal;
            //HIBYTE(fDirectionYLocal) ^= 0x80u;
            pCar->iRollMomentum = iRollBackup;
          }
          if (fDirectionYLocal < 40.0)
            fDirectionYLocal = 40.0;
          if (fDirectionYLocal > 40.0 && !pCar->bySfxCooldown) {
            dVolumeCalc = fDirectionYLocal * 32768.0 * 0.025;
            //_CHP();
            //LODWORD(fTempCalc) = (int)dVolumeCalc;
            sfxpend(SOUND_SAMPLE_WALL1, pCar->iDriverIdx, (int)dVolumeCalc);// SOUND_SAMPLE_WALL1
            pCar->bySfxCooldown = 9;
          }
          dVelocityY = fDirectionYLocal;
          dVelocityX = fDirectionXLocal;
          dVelocityZ = fDirectionZLocal;
          pCar->direction.fX = (float)(pCurrentData->pointAy[0].fY * fDirectionYLocal + pCurrentData->pointAy[0].fX * fDirectionXLocal + pCurrentData->pointAy[0].fZ * fDirectionZLocal);
          pCar->direction.fY = (float)(pCurrentData->pointAy[1].fX * dVelocityX + pCurrentData->pointAy[1].fY * dVelocityY + pCurrentData->pointAy[1].fZ * dVelocityZ);
          pCar->direction.fZ = (float)(dVelocityY * pCurrentData->pointAy[2].fY + dVelocityX * pCurrentData->pointAy[2].fX + dVelocityZ * pCurrentData->pointAy[2].fZ);
          fLocalCoordY = fWallBoundary + CarBaseY;
          dLocalPosX = fLocalCoordX;
          dLocalPosZ = fLocalCoordZ;
          pCar->pos.fX = pCurrentData->pointAy[0].fY * fLocalCoordY
            + pCurrentData->pointAy[0].fX * fLocalCoordX
            + pCurrentData->pointAy[0].fZ * fLocalCoordZ
            - pCurrentData->pointAy[3].fX;
          pCar->pos.fY = (float)(pCurrentData->pointAy[1].fX * dLocalPosX
            + pCurrentData->pointAy[1].fY * fLocalCoordY
            + pCurrentData->pointAy[1].fZ * dLocalPosZ
            - pCurrentData->pointAy[3].fY);
          dFinalPosZ = dLocalPosZ * pCurrentData->pointAy[2].fZ
            + dLocalPosX * pCurrentData->pointAy[2].fX
            + pCurrentData->pointAy[2].fY * fLocalCoordY
            - pCurrentData->pointAy[3].fZ;
          fTempCalc = pCar->direction.fX;
          fDirectionYTemp2 = pCar->direction.fY;
          fDirectionTemp1 = fTempCalc;
          fDirX = fTempCalc;
          fDirectionTemp3 = fDirectionYTemp2;
          pCar->pos.fZ = (float)dFinalPosZ;
          //if ((LODWORD(fDirX) & 0x7FFFFFFF) != 0 || (LODWORD(fDirectionYTemp2) & 0x7FFFFFFF) != 0) {
          if (fabs(fDirX) > FLT_EPSILON || fabs(fDirectionYTemp2) > FLT_EPSILON) {
            dAngle2 = atan2(fDirectionTemp3, fDirectionTemp1) * 16384.0 / 6.28318530718;
            //_CHP();
            //LODWORD(fTempCalc) = (int)dAngle2;
            iChunkIdx = (int)dAngle2 & 0x3FFF;
          } else {
            iChunkIdx = 0;
          }
          goto LABEL_69;
        }
      }
    }
  } else if (TrakColour[iChunk][3])             // Handle collision with right wall/barrier (negative Y direction)
  {
    fRoofHeightCheck = pTrackInfo->fRoofHeight;
    if (fRoofHeightCheck < 0.0)
      bRoofCollision = fLocalCoordZ >= (double)fRoofHeightCheck && fLocalCoordZ <= 0.0;
    else
      bRoofCollision = fLocalCoordZ <= (double)fRoofHeightCheck && fLocalCoordZ >= 0.0;
    if (bRoofCollision && fRoofHeightCheck > 0.0) {
      if (TrakColour[iChunk][3] < 0) {
        fTrackHalfWidth = pCurrentData->fTrackHalfWidth;
        fTrackHalfWidthNext = pNextData->fTrackHalfWidth;
      } else {
        fTrackHalfWidth = pCurrentData->fTrackHalfWidth + pTrackInfo->fLShoulderWidth;
        fTrackHalfWidthNext = pNextData->fTrackHalfWidth + pNextTrackInfo->fLShoulderWidth;
      }
      fInterpolatedBoundary = ((fLocalCoordX + pCurrentData->fTrackHalfLength) * fTrackHalfWidthNext - (fLocalCoordX - pCurrentData->fTrackHalfLength) * fTrackHalfWidth)
        / (pCurrentData->fTrackHalfLength
         * 2.0f);
      if (fLocalCoordY + CarBaseY >= fInterpolatedBoundary) {
        if (fDirectionYLocal > 0.0) {
          if (death_race)
            dWallNormalY = fDirectionYLocal * 0.005 * 4.0;
          else
            dWallNormalY = fDirectionYLocal * 0.005;
          fTempDamage = (float)dWallNormalY;
          dodamage(pCar, fTempDamage);
          iRollMomentumBackup = -pCar->iRollMomentum;
          fDirectionYLocal = -fDirectionYLocal;
          //HIBYTE(fDirectionYLocal) ^= 0x80u;
          pCar->iRollMomentum = iRollMomentumBackup;
        }
        if (fDirectionYLocal > -40.0)
          fDirectionYLocal = -40.0;
        dDamageAmount = -fDirectionYLocal;
        fWallDamageCalc = (float)dDamageAmount;
        if (dDamageAmount > 40.0 && !pCar->bySfxCooldown) {
          dSoundVolume = fWallDamageCalc * 32768.0 * 0.025;
          //_CHP();
          //LODWORD(fTempCalc) = (int)dSoundVolume;
          sfxpend(SOUND_SAMPLE_WALL1, pCar->iDriverIdx, (int)dSoundVolume);// SOUND_SAMPLE_WALL1
          pCar->bySfxCooldown = 9;
        }
        dLocalY = fDirectionYLocal;
        dLocalX = fDirectionXLocal;
        dLocalZ = fDirectionZLocal;
        pCar->direction.fX = (float)(pCurrentData->pointAy[0].fY * fDirectionYLocal + pCurrentData->pointAy[0].fX * fDirectionXLocal + pCurrentData->pointAy[0].fZ * fDirectionZLocal);
        pCar->direction.fY = (float)(pCurrentData->pointAy[1].fX * dLocalX + pCurrentData->pointAy[1].fY * dLocalY + pCurrentData->pointAy[1].fZ * dLocalZ);
        pCar->direction.fZ = (float)(dLocalY * pCurrentData->pointAy[2].fY + dLocalX * pCurrentData->pointAy[2].fX + dLocalZ * pCurrentData->pointAy[2].fZ);
        fLocalCoordY = fInterpolatedBoundary - CarBaseY;
        dTempX = fLocalCoordX;
        dTempZ = fLocalCoordZ;
        pCar->pos.fX = pCurrentData->pointAy[0].fY * fLocalCoordY
          + pCurrentData->pointAy[0].fX * fLocalCoordX
          + pCurrentData->pointAy[0].fZ * fLocalCoordZ
          - pCurrentData->pointAy[3].fX;
        pCar->pos.fY = (float)(pCurrentData->pointAy[1].fX * dTempX + pCurrentData->pointAy[1].fY * fLocalCoordY + pCurrentData->pointAy[1].fZ * dTempZ - pCurrentData->pointAy[3].fY);
        dPositionZ = dTempZ * pCurrentData->pointAy[2].fZ + dTempX * pCurrentData->pointAy[2].fX + pCurrentData->pointAy[2].fY * fLocalCoordY - pCurrentData->pointAy[3].fZ;
        fTempCalc = pCar->direction.fX;
        fDirectionYTemp1 = pCar->direction.fY;
        fAngleCalcTemp5 = fTempCalc;
        fDirectionX = fTempCalc;
        fAngleCalcTemp6 = fDirectionYTemp1;
        pCar->pos.fZ = (float)dPositionZ;
        //if ((LODWORD(fDirectionX) & 0x7FFFFFFF) != 0 || (LODWORD(fDirectionYTemp1) & 0x7FFFFFFF) != 0) {
        if (fabs(fDirectionX) > FLT_EPSILON || fabs(fDirectionYTemp1) > FLT_EPSILON) {
          dAngle1 = atan2(fAngleCalcTemp6, fAngleCalcTemp5) * 16384.0 / 6.28318530718;
          //_CHP();
          //LODWORD(fTempCalc) = (int)dAngle1;
          iChunkIdx = (int)dAngle1 & 0x3FFF;
        } else {
          iChunkIdx = 0;
        }
      LABEL_69:
        dHorizontalSpeed = pCar->direction.fX * pCar->direction.fX + pCar->direction.fY * pCar->direction.fY;
        pCar->nActualYaw = iChunkIdx;
        pCar->fHorizontalSpeed = (float)sqrt(dHorizontalSpeed);
      }
    }
  }
  if (pCurrentData->fTrackHalfWidth + pTrackInfo->fLShoulderWidth >= fLocalCoordY) {
    dNegTrackWidth = -pCurrentData->fTrackHalfWidth;// Check if car is within track boundaries for ground collision
    fTrackWidthNeg = (float)dNegTrackWidth;
    if (dNegTrackWidth - pTrackInfo->fRShoulderWidth <= fLocalCoordY) {
      if (fLocalCoordY <= (double)pCurrentData->fTrackHalfWidth)
        iGroundType = fLocalCoordY >= (double)fTrackWidthNeg ? 1 : 2;
      else
        iGroundType = 0;
      //llTrackColor = TrakColour[iChunk][iGroundType];
      //if ((((HIDWORD(llTrackColor) ^ (unsigned int)llTrackColor) - HIDWORD(llTrackColor)) & 0x20000) == 0
      if ((TrakColour[iChunk][iGroundType] & SURFACE_FLAG_SKIP_RENDER) == 0
        || pCurrentData->fTrackHalfLength * 2.0 <= CarBaseX
        || pCurrentData->fTrackHalfWidth * 2.0 <= CarBaseX) {
        iChunkIndex = iChunk;
        fGroundHeight = (float)getgroundz(fLocalCoordX, fLocalCoordY, iChunk);// Handle normal ground collision - calculate ground height and landing physics
        if (fLocalCoordZ <= (double)fGroundHeight && fDirectionZLocal * 2.0 + fGroundHeight + -250.0 <= fLocalCoordZ) {
          pCar->pos.fX = fLocalCoordX;
          pCar->pos.fY = fLocalCoordY;
          getlocalangles(pCar->nYaw, pCar->nPitch, pCar->nRoll, iChunkIndex, &iAzimuth, &iElevation, &iBank);
          pCar->nYaw = iAzimuth;
          pCar->nActualYaw = iAzimuth;
          pCar->nPitch = iElevation;
          pCar->nRoll = iBank;
          fTransformX = fDirectionXLocal;
          pCar->nCurrChunk = iChunk;
          //if ((LODWORD(fTransformX) & 0x7FFFFFFF) != 0 || (LODWORD(fDirectionYLocal) & 0x7FFFFFFF) != 0) {
          if (fabs(fTransformX) > FLT_EPSILON || fabs(fDirectionYLocal) > FLT_EPSILON) {
            dAngle3 = atan2(fDirectionYLocal, fDirectionXLocal) * 16384.0 / 6.28318530718;
            //_CHP();
            //LODWORD(fTempCalc) = (int)dAngle3;
            pCar->nActualYaw = (int)dAngle3 & 0x3FFF;
          }
          if (iBank < 4096 || iBank > 12288) {
            pCar->iStunned = 0;
          } else {
            pCar->iStunned = -1;
            pCar->iSteeringInput = 0;
            iBankAdjusted = iBank + 0x2000;
            iBank = iBankAdjusted;
            if (iBankAdjusted >= 0x4000)
              iBank = iBankAdjusted - 0x4000;
          }
          putflat(pCar);
          if (iElevation > 0x2000)
            iElevation -= 0x4000;
          iElevationAdjusted = iElevation;
          pCar->iCameraOscillationPhase = 0;
          pCar->iPitchBackup = iElevationAdjusted;
          pCar->iPitchCameraOffset = iElevation;
          if (iBank > 0x2000)
            iBank -= 0x4000;
          dHeightDiff = fPrevZPosition - fLocalCoordZ;
          fHeightDiffCalc = (float)dHeightDiff;
          iBankValue = iBank;
          pCar->iControlType = 3;
          pCar->iRollDampingMomentum = iBankValue;
          pCar->iRollCameraOffset = iBankValue;
          iStunned = pCar->iStunned;
          fVolumeCalc = (float)(dHeightDiff * 32768.0 * 0.025);
          if (iStunned) {
            dStunnedVolume = fVolumeCalc;
            //_CHP();
            //LODWORD(fTempCalc) = (int)dStunnedVolume;
            sfxpend(SOUND_SAMPLE_ROOF, pCar->iDriverIdx, (int)dStunnedVolume);// SOUND_SAMPLE_ROOF
          } else if (!pCar->bySfxCooldown) {
            dImpactVolume = fVolumeCalc;
            if (fHeightDiffCalc <= 40.0) {
              //_CHP();
              //LODWORD(fTempCalc) = (int)dImpactVolume;
              iSfxType = SOUND_SAMPLE_LIGHTLAN;                    // SOUND_SAMPLE_LIGHTLAN
            } else {
              //_CHP();                           // Play appropriate landing sound effect based on impact severity
              //LODWORD(fTempCalc) = (int)dImpactVolume;
              iSfxType = SOUND_SAMPLE_LANDSKID;                     // SOUND_SAMPLE_LANDSKID
            }
            sfxpend(iSfxType, pCar->iDriverIdx, (int)dImpactVolume);
            pCar->bySfxCooldown = 9;
          }
          fTempDamage3 = (float)fabs((fPrevZPosition - fLocalCoordZ) * tsin[iElevation & 0x3FFF] * 0.05);// Calculate landing damage based on impact angle and velocity
          dodamage(pCar, fTempDamage3);
          fThrottle = (float)sqrt(fDirectionXLocal * fDirectionXLocal + fDirectionYLocal * fDirectionYLocal);
          SetEngine(pCar, fThrottle);
        }
      }
    }
  }
  if (pCar->nCurrChunk == -1) {
    iGroundColorType = GroundColour[iChunk][2]; // Handle underground section collision detection when car is below track level
    if (iGroundColorType != -1) {
      if (iGroundColorType >= 0) {
        dSeparatedY = fCarPosY + data.pointAy[3].fY;
        dSeparatedX = fCarPosX + data.pointAy[3].fX;
        dSeparatedZ = fCarPosZ + data.pointAy[3].fZ;
        fLocalCoordX = (float)(data.pointAy[1].fX * dSeparatedY + data.pointAy[0].fX * dSeparatedX + data.pointAy[2].fX * dSeparatedZ);
        fLocalCoordY = (float)(data.pointAy[0].fY * dSeparatedX + data.pointAy[1].fY * dSeparatedY + data.pointAy[2].fY * dSeparatedZ);
        pCurrentData = &data;
        fLocalCoordZ = (float)(dSeparatedY * data.pointAy[1].fZ + dSeparatedX * data.pointAy[0].fZ + dSeparatedZ * data.pointAy[2].fZ);
      }
      if ((GroundColour[iChunk][2] < 0 || fLocalCoordY < 0.0)
        && (GroundColour[iChunk][2] >= 0 || pCurrentData->fTrackHalfWidth + pTrackInfo->fLShoulderWidth - CarBaseY > fLocalCoordY)) {
        if (GroundColour[iChunk][2] >= 0) {
          dSeparatedY2 = fCarPosY + data.pointAy[3].fY;
          dSeparatedX2 = fCarPosX + data.pointAy[3].fX;
          dSeparatedZ2 = fCarPosZ + data.pointAy[3].fZ;
          fLocalCoordX = (float)(data.pointAy[1].fX * dSeparatedY2 + data.pointAy[0].fX * dSeparatedX2 + data.pointAy[2].fX * dSeparatedZ2);
          fLocalCoordY = (float)(data.pointAy[0].fY * dSeparatedX2 + data.pointAy[1].fY * dSeparatedY2 + data.pointAy[2].fY * dSeparatedZ2);
          pCurrentData = &data;
          fLocalCoordZ = (float)(dSeparatedY2 * data.pointAy[1].fZ + dSeparatedX2 * data.pointAy[0].fZ + dSeparatedZ2 * data.pointAy[2].fZ);
        }
        if (GroundColour[iChunk][2] >= 0 && fLocalCoordY < 0.0
          || GroundColour[iChunk][2] < 0 && -pCurrentData->fTrackHalfWidth - pTrackInfo->fRShoulderWidth + CarBaseY >= fLocalCoordY) {
          pGroundPts2 = &GroundPt[iChunk];
          dGroundY4 = pGroundPts2->pointAy[4].fY + pCurrentData->pointAy[3].fY;
          dGroundX4 = pGroundPts2->pointAy[4].fX + pCurrentData->pointAy[3].fX;
          dGroundZ4 = pGroundPts2->pointAy[4].fZ + pCurrentData->pointAy[3].fZ;
          fGroundYInterp1 = (float)(pCurrentData->pointAy[1].fY * dGroundY4 + pCurrentData->pointAy[0].fY * dGroundX4 + pCurrentData->pointAy[2].fY * dGroundZ4);
          fGroundZInterp5 = (float)(dGroundY4 * pCurrentData->pointAy[1].fZ + dGroundX4 * pCurrentData->pointAy[0].fZ + dGroundZ4 * pCurrentData->pointAy[2].fZ);
          fHeightDiffUnder = fLocalCoordZ - fGroundZInterp5;
          if (fLocalCoordZ <= (double)fGroundZInterp5) {
            dGroundY6 = pGroundPts2->pointAy[3].fY + pCurrentData->pointAy[3].fY;
            dGroundX6 = pGroundPts2->pointAy[3].fX + pCurrentData->pointAy[3].fX;
            dGroundZ6 = pGroundPts2->pointAy[3].fZ + pCurrentData->pointAy[3].fZ;
            fGroundYCalc4 = (float)(pCurrentData->pointAy[1].fY * dGroundY6 + pCurrentData->pointAy[0].fY * dGroundX6 + pCurrentData->pointAy[2].fY * dGroundZ6);
            fGroundZInterp1 = (float)(dGroundY6 * pCurrentData->pointAy[1].fZ + dGroundX6 * pCurrentData->pointAy[0].fZ + dGroundZ6 * pCurrentData->pointAy[2].fZ);
            dSlopeDiff6 = fGroundZInterp5 - fGroundZInterp1;
            fSlopeDiffCalc4 = (float)dSlopeDiff6;
            if (fabs(dSlopeDiff6) <= 10.0)
              fInterpolatedHeight1 = -1000000.0;
            else
              fInterpolatedHeight1 = fHeightDiffUnder * fGroundYCalc4 / (fGroundZInterp1 - fGroundZInterp5)
              + (fLocalCoordZ - fGroundZInterp1) * fGroundYInterp1 / fSlopeDiffCalc4;
            if (fLocalCoordY - CarBaseY < fInterpolatedHeight1) {
              fDirectionXTemp4 = pCurrentData->pointAy[1].fX * pCar->direction.fY
                + pCurrentData->pointAy[0].fX * pCar->direction.fX
                + pCurrentData->pointAy[2].fX * pCar->direction.fZ;
              fDirectionYLocal4 = pCurrentData->pointAy[0].fY * pCar->direction.fX
                + pCurrentData->pointAy[1].fY * pCar->direction.fY
                + pCurrentData->pointAy[2].fY * pCar->direction.fZ;
              fDirectionZTemp2 = pCurrentData->pointAy[0].fZ * pCar->direction.fX
                + pCurrentData->pointAy[1].fZ * pCar->direction.fY
                + pCurrentData->pointAy[2].fZ * pCar->direction.fZ;
              if (fDirectionYLocal4 < 0.0) {
                fDirectionYLocal4 = -fDirectionYLocal4;
                //HIBYTE(fDirectionYLocal4) ^= 0x80u;
                if (death_race)
                  dUndergroundDamage6 = fDirectionYLocal4 * 0.005 * 4.0;
                else
                  dUndergroundDamage6 = fDirectionYLocal4 * 0.005;
                fUndergroundDamage6 = (float)dUndergroundDamage6;
                dodamage(pCar, fUndergroundDamage6);
                pCar->iRollMomentum = -pCar->iRollMomentum;
              }
              if (fDirectionYLocal4 < 40.0)
                fDirectionYLocal4 = 40.0;
              if (fDirectionYLocal4 > 40.0 && !pCar->bySfxCooldown) {
                dBankSoundVolume6 = fDirectionYLocal4 * 32768.0 * 0.025;
                //_CHP();
                //LODWORD(fTempCalc) = (int)dBankSoundVolume6;
                sfxpend(SOUND_SAMPLE_BANK, pCar->iDriverIdx, (int)dBankSoundVolume6);// SOUND_SAMPLE_BANK
                pCar->bySfxCooldown = 9;
              }
              dInterpolatedY3 = fInterpolatedHeight1 + CarBaseY;
              dInterpolatedX3 = fLocalCoordX;
              dInterpolatedZ3 = fLocalCoordZ;
              pCar->pos.fX = (float)(pCurrentData->pointAy[0].fY * dInterpolatedY3
                + pCurrentData->pointAy[0].fX * fLocalCoordX
                + pCurrentData->pointAy[0].fZ * fLocalCoordZ
                - pCurrentData->pointAy[3].fX);
              pCar->pos.fY = (float)(pCurrentData->pointAy[1].fX * dInterpolatedX3
                + pCurrentData->pointAy[1].fY * dInterpolatedY3
                + pCurrentData->pointAy[1].fZ * dInterpolatedZ3
                - pCurrentData->pointAy[3].fY);
              pCar->pos.fZ = (float)(dInterpolatedY3 * pCurrentData->pointAy[2].fY
                + dInterpolatedX3 * pCurrentData->pointAy[2].fX
                + dInterpolatedZ3 * pCurrentData->pointAy[2].fZ
                - pCurrentData->pointAy[3].fZ);
              dDirectionY5 = fDirectionYLocal4;
              dDirectionX5 = fDirectionXTemp4;
              dDirectionZ5 = fDirectionZTemp2;
              pCar->direction.fX = pCurrentData->pointAy[0].fY * fDirectionYLocal4
                + pCurrentData->pointAy[0].fX * fDirectionXTemp4
                + pCurrentData->pointAy[0].fZ * fDirectionZTemp2;
              pCar->direction.fY = (float)(pCurrentData->pointAy[1].fX * dDirectionX5 + pCurrentData->pointAy[1].fY * dDirectionY5 + pCurrentData->pointAy[1].fZ * dDirectionZ5);
              dFinalDirectionZ5 = dDirectionY5 * pCurrentData->pointAy[2].fY + dDirectionX5 * pCurrentData->pointAy[2].fX + dDirectionZ5 * pCurrentData->pointAy[2].fZ;
              fTempCalc = pCar->direction.fX;
              fDirectionYTemp4 = pCar->direction.fY;
              fAngleCalcTemp1 = fTempCalc;
              fDirXFinal6 = fTempCalc;
              fDirectionYFinal = fDirectionYTemp4;
              pCar->direction.fZ = (float)dFinalDirectionZ5;
              //if ((LODWORD(fDirXFinal6) & 0x7FFFFFFF) != 0 || (LODWORD(fDirectionYTemp4) & 0x7FFFFFFF) != 0) {
              if (fabs(fDirXFinal6) > FLT_EPSILON || fabs(fDirectionYTemp4) > FLT_EPSILON) {
                dAngle7 = atan2(fDirectionYFinal, fAngleCalcTemp1) * 16384.0 / 6.28318530718;
                //_CHP();
                //LODWORD(fTempCalc) = (int)dAngle7;
                dYawCalc = (int)dAngle7 & 0x3FFF;
              } else {
                dYawCalc = 0;
              }
              goto LABEL_192;
            }
          } else if (fLocalCoordY < (double)fGroundYInterp1) {
            dGroundY5 = pGroundPts2->pointAy[5].fY + pCurrentData->pointAy[3].fY;
            dGroundX5 = pGroundPts2->pointAy[5].fX + pCurrentData->pointAy[3].fX;
            dGroundZ5 = pGroundPts2->pointAy[5].fZ + pCurrentData->pointAy[3].fZ;
            fGroundYCalc1 = (float)(pCurrentData->pointAy[1].fY * dGroundY5 + pCurrentData->pointAy[0].fY * dGroundX5 + pCurrentData->pointAy[2].fY * dGroundZ5);
            fGroundZInterp3 = (float)(dGroundY5 * pCurrentData->pointAy[1].fZ + dGroundX5 * pCurrentData->pointAy[0].fZ + dGroundZ5 * pCurrentData->pointAy[2].fZ);
            dSlopeDiff5 = fGroundZInterp5 - fGroundZInterp3;
            fSlopeDiffCalc3 = (float)dSlopeDiff5;
            fInterpolatedHeight2 = fabs(dSlopeDiff5) <= 10.0
              ? -1000000.0f
              : fHeightDiffUnder * fGroundYCalc1 / (fGroundZInterp3 - fGroundZInterp5)
              + (fLocalCoordZ - fGroundZInterp3) * fGroundYInterp1 / fSlopeDiffCalc3;
            if (fLocalCoordY - CarBaseY < fInterpolatedHeight2) {
              fDirectionXTemp3 = pCurrentData->pointAy[1].fX * pCar->direction.fY
                + pCurrentData->pointAy[0].fX * pCar->direction.fX
                + pCurrentData->pointAy[2].fX * pCar->direction.fZ;
              fDirectionYLocal3 = pCurrentData->pointAy[0].fY * pCar->direction.fX
                + pCurrentData->pointAy[1].fY * pCar->direction.fY
                + pCurrentData->pointAy[2].fY * pCar->direction.fZ;
              fDirectionZTemp4 = pCurrentData->pointAy[0].fZ * pCar->direction.fX
                + pCurrentData->pointAy[1].fZ * pCar->direction.fY
                + pCurrentData->pointAy[2].fZ * pCar->direction.fZ;
              if (fDirectionYLocal3 < 0.0) {
                fDirectionYLocal3 = -fDirectionYLocal3;
                //HIBYTE(fDirectionYLocal3) ^= 0x80u;
                if (death_race)
                  dUndergroundDamage5 = fDirectionYLocal3 * 0.005 * 4.0;
                else
                  dUndergroundDamage5 = fDirectionYLocal3 * 0.005;
                fUndergroundDamage5 = (float)dUndergroundDamage5;
                dodamage(pCar, fUndergroundDamage5);
                pCar->iRollMomentum = -pCar->iRollMomentum;
              }
              if (fDirectionYLocal3 < 40.0)
                fDirectionYLocal3 = 40.0;
              if (fDirectionYLocal3 > 40.0 && !pCar->bySfxCooldown) {
                dBankSoundVolume5 = fDirectionYLocal3 * 32768.0 * 0.025;
                //_CHP();
                //LODWORD(fTempCalc) = (int)dBankSoundVolume5;
                sfxpend(SOUND_SAMPLE_BANK, pCar->iDriverIdx, (int)dBankSoundVolume5);// SOUND_SAMPLE_BANK
                pCar->bySfxCooldown = 9;
              }
              dGroundY3 = fInterpolatedHeight2 + CarBaseY;
              dGroundX3 = fLocalCoordX;
              dGroundZ3 = fLocalCoordZ;
              pCar->pos.fX = (float)(pCurrentData->pointAy[0].fY * dGroundY3
                + pCurrentData->pointAy[0].fX * fLocalCoordX
                + pCurrentData->pointAy[0].fZ * fLocalCoordZ
                - pCurrentData->pointAy[3].fX);
              pCar->pos.fY = (float)(pCurrentData->pointAy[1].fX * dGroundX3
                + pCurrentData->pointAy[1].fY * dGroundY3
                + pCurrentData->pointAy[1].fZ * dGroundZ3
                - pCurrentData->pointAy[3].fY);
              pCar->pos.fZ = (float)(dGroundY3 * pCurrentData->pointAy[2].fY
                + dGroundX3 * pCurrentData->pointAy[2].fX
                + dGroundZ3 * pCurrentData->pointAy[2].fZ
                - pCurrentData->pointAy[3].fZ);
              dDirectionY4 = fDirectionYLocal3;
              dDirectionX4 = fDirectionXTemp3;
              dDirectionZ4 = fDirectionZTemp4;
              pCar->direction.fX = pCurrentData->pointAy[0].fY * fDirectionYLocal3
                + pCurrentData->pointAy[0].fX * fDirectionXTemp3
                + pCurrentData->pointAy[0].fZ * fDirectionZTemp4;
              pCar->direction.fY = (float)(pCurrentData->pointAy[1].fX * dDirectionX4 + pCurrentData->pointAy[1].fY * dDirectionY4 + pCurrentData->pointAy[1].fZ * dDirectionZ4);
              dFinalDirectionZ4 = dDirectionY4 * pCurrentData->pointAy[2].fY + dDirectionX4 * pCurrentData->pointAy[2].fX + dDirectionZ4 * pCurrentData->pointAy[2].fZ;
              fTempCalc = pCar->direction.fX;
              fDirectionYTemp6 = pCar->direction.fY;
              fAngleCalcTemp3 = fTempCalc;
              fDirXFinal5 = fTempCalc;
              fDirectionTemp2 = fDirectionYTemp6;
              pCar->direction.fZ = (float)dFinalDirectionZ4;
              //if ((LODWORD(fDirXFinal5) & 0x7FFFFFFF) != 0 || (LODWORD(fDirectionYTemp6) & 0x7FFFFFFF) != 0) {
              if (fabs(fDirXFinal5) > FLT_EPSILON || fabs(fDirectionYTemp6) > FLT_EPSILON) {
                dAngle6 = atan2(fDirectionTemp2, fAngleCalcTemp3) * 16384.0 / 6.28318530718;
                //_CHP();
                //LODWORD(fTempCalc) = (int)dAngle6;
                dYawCalc = (int)dAngle6 & 0x3FFF;
              } else {
                dYawCalc = 0;
              }
              goto LABEL_192;
            }
          }
        }
      } else {
        pGroundPts = &GroundPt[iChunk];
        dGroundY1 = pGroundPts->pointAy[1].fY + pCurrentData->pointAy[3].fY;
        dGroundX1 = pGroundPts->pointAy[1].fX + pCurrentData->pointAy[3].fX;
        dGroundZ1 = pGroundPts->pointAy[1].fZ + pCurrentData->pointAy[3].fZ;
        fGroundYInterp2 = (float)(pCurrentData->pointAy[1].fY * dGroundY1 + pCurrentData->pointAy[0].fY * dGroundX1 + pCurrentData->pointAy[2].fY * dGroundZ1);
        fGroundYInterp3 = (float)(dGroundY1 * pCurrentData->pointAy[1].fZ + dGroundX1 * pCurrentData->pointAy[0].fZ + dGroundZ1 * pCurrentData->pointAy[2].fZ);
        fHeightDiffGround = fLocalCoordZ - fGroundYInterp3;
        if (fLocalCoordZ <= (double)fGroundYInterp3) {
          dGroundY2 = pGroundPts->pointAy[2].fY + pCurrentData->pointAy[3].fY;
          dGroundX2 = pGroundPts->pointAy[2].fX + pCurrentData->pointAy[3].fX;
          dGroundZ2 = pGroundPts->pointAy[2].fZ + pCurrentData->pointAy[3].fZ;
          fGroundYCalc3 = (float)(pCurrentData->pointAy[1].fY * dGroundY2 + pCurrentData->pointAy[0].fY * dGroundX2 + pCurrentData->pointAy[2].fY * dGroundZ2);
          fGroundZInterp2 = (float)(dGroundY2 * pCurrentData->pointAy[1].fZ + dGroundX2 * pCurrentData->pointAy[0].fZ + dGroundZ2 * pCurrentData->pointAy[2].fZ);
          dSlopeDiff2 = fGroundYInterp3 - fGroundZInterp2;
          fSlopeDiffCalc1 = (float)dSlopeDiff2;
          if (fabs(dSlopeDiff2) <= 10.0)
            fInterpolatedHeight4 = 1000000.0f;
          else
            fInterpolatedHeight4 = fHeightDiffGround * fGroundYCalc3 / (fGroundZInterp2 - fGroundYInterp3)
            + (fLocalCoordZ - fGroundZInterp2) * fGroundYInterp2 / fSlopeDiffCalc1;
          if (fLocalCoordY + CarBaseY > fInterpolatedHeight4) {
            fDirectionXTemp2 = pCurrentData->pointAy[1].fX * pCar->direction.fY
              + pCurrentData->pointAy[0].fX * pCar->direction.fX
              + pCurrentData->pointAy[2].fX * pCar->direction.fZ;
            fDirectionYLocal1 = pCurrentData->pointAy[0].fY * pCar->direction.fX
              + pCurrentData->pointAy[1].fY * pCar->direction.fY
              + pCurrentData->pointAy[2].fY * pCar->direction.fZ;
            fDirectionZTemp3 = pCurrentData->pointAy[0].fZ * pCar->direction.fX
              + pCurrentData->pointAy[1].fZ * pCar->direction.fY
              + pCurrentData->pointAy[2].fZ * pCar->direction.fZ;
            if (fDirectionYLocal1 > 0.0) {
              if (death_race)
                dUndergroundDamage2 = fDirectionYLocal1 * 0.005 * 4.0;
              else
                dUndergroundDamage2 = fDirectionYLocal1 * 0.005;
              fUndergroundDamage2 = (float)dUndergroundDamage2;
              dodamage(pCar, fUndergroundDamage2);
              fDirectionYLocal1 = -fDirectionYLocal1;
              //HIBYTE(fDirectionYLocal1) ^= 0x80u;
              pCar->iRollMomentum = -pCar->iRollMomentum;
            }
            if (fDirectionYLocal1 > -40.0)
              fDirectionYLocal1 = -40.0;
            dNegSpeed2 = -fDirectionYLocal1;
            fNegSpeedCalc1 = (float)dNegSpeed2;
            if (dNegSpeed2 > 40.0 && !pCar->bySfxCooldown) {
              dBankSoundVolume2 = fNegSpeedCalc1 * 32768.0 * 0.025;
              //_CHP();
              //LODWORD(fTempCalc) = (int)dBankSoundVolume2;
              sfxpend(SOUND_SAMPLE_BANK, pCar->iDriverIdx, (int)dBankSoundVolume2);// SOUND_SAMPLE_BANK
              pCar->bySfxCooldown = 9;
            }
            dInterpolatedY2 = fInterpolatedHeight4 - CarBaseY;
            dInterpolatedX2 = fLocalCoordX;
            dInterpolatedZ2 = fLocalCoordZ;
            pCar->pos.fX = (float)(pCurrentData->pointAy[0].fY * dInterpolatedY2
              + pCurrentData->pointAy[0].fX * fLocalCoordX
              + pCurrentData->pointAy[0].fZ * fLocalCoordZ
              - pCurrentData->pointAy[3].fX);
            pCar->pos.fY = (float)(pCurrentData->pointAy[1].fX * dInterpolatedX2
              + pCurrentData->pointAy[1].fY * dInterpolatedY2
              + pCurrentData->pointAy[1].fZ * dInterpolatedZ2
              - pCurrentData->pointAy[3].fY);
            pCar->pos.fZ = (float)(dInterpolatedY2 * pCurrentData->pointAy[2].fY
              + dInterpolatedX2 * pCurrentData->pointAy[2].fX
              + dInterpolatedZ2 * pCurrentData->pointAy[2].fZ
              - pCurrentData->pointAy[3].fZ);
            dDirectionY3 = fDirectionYLocal1;
            dDirectionX3 = fDirectionXTemp2;
            dDirectionZ3 = fDirectionZTemp3;
            pCar->direction.fX = pCurrentData->pointAy[0].fY * fDirectionYLocal1
              + pCurrentData->pointAy[0].fX * fDirectionXTemp2
              + pCurrentData->pointAy[0].fZ * fDirectionZTemp3;
            pCar->direction.fY = (float)(pCurrentData->pointAy[1].fX * dDirectionX3 + pCurrentData->pointAy[1].fY * dDirectionY3 + pCurrentData->pointAy[1].fZ * dDirectionZ3);
            dFinalDirectionZ2 = dDirectionY3 * pCurrentData->pointAy[2].fY + dDirectionX3 * pCurrentData->pointAy[2].fX + dDirectionZ3 * pCurrentData->pointAy[2].fZ;
            fTempCalc = pCar->direction.fX;
            fDirectionYTemp5 = pCar->direction.fY;
            fAngleCalcTemp2 = fTempCalc;
            fDirXFinal2 = fTempCalc;
            fAngleCalcTemp7 = fDirectionYTemp5;
            pCar->direction.fZ = (float)dFinalDirectionZ2;
            //if ((LODWORD(fDirXFinal2) & 0x7FFFFFFF) != 0 || (LODWORD(fDirectionYTemp5) & 0x7FFFFFFF) != 0) {
            if (fabs(fDirXFinal2) > FLT_EPSILON || fabs(fDirectionYTemp5) > FLT_EPSILON) {
              dAngle5 = atan2(fAngleCalcTemp7, fAngleCalcTemp2) * 16384.0 / 6.28318530718;
              //_CHP();
              //LODWORD(fTempCalc) = (int)dAngle5;
              dYawCalc = (int)dAngle5 & 0x3FFF;
            } else {
              dYawCalc = 0;
            }
            goto LABEL_192;
          }
        } else if (fLocalCoordY > (double)fGroundYInterp2) {
          dGroundY0 = pGroundPts->pointAy[0].fY + pCurrentData->pointAy[3].fY;
          dGroundX0 = pGroundPts->pointAy[0].fX + pCurrentData->pointAy[3].fX;
          dGroundZ0 = pGroundPts->pointAy[0].fZ + pCurrentData->pointAy[3].fZ;
          fGroundYCalc2 = (float)(pCurrentData->pointAy[1].fY * dGroundY0 + pCurrentData->pointAy[0].fY * dGroundX0 + pCurrentData->pointAy[2].fY * dGroundZ0);
          fGroundZInterp4 = (float)(dGroundY0 * pCurrentData->pointAy[1].fZ + dGroundX0 * pCurrentData->pointAy[0].fZ + dGroundZ0 * pCurrentData->pointAy[2].fZ);
          dSlopeDiff = fGroundYInterp3 - fGroundZInterp4;
          fSlopeDiffCalc2 = (float)dSlopeDiff;
          fInterpolatedHeight3 = fabs(dSlopeDiff) <= 10.0
            ? 1000000.0f
            : fHeightDiffGround * fGroundYCalc2 / (fGroundZInterp4 - fGroundYInterp3) + (fLocalCoordZ - fGroundZInterp4) * fGroundYInterp2 / fSlopeDiffCalc2;
          if (fLocalCoordY + CarBaseY > fInterpolatedHeight3) {
            fDirectionXTemp1 = pCurrentData->pointAy[1].fX * pCar->direction.fY
              + pCurrentData->pointAy[0].fX * pCar->direction.fX
              + pCurrentData->pointAy[2].fX * pCar->direction.fZ;
            fDirectionYLocal2 = pCurrentData->pointAy[0].fY * pCar->direction.fX
              + pCurrentData->pointAy[1].fY * pCar->direction.fY
              + pCurrentData->pointAy[2].fY * pCar->direction.fZ;
            fDirectionZTemp1 = pCurrentData->pointAy[0].fZ * pCar->direction.fX
              + pCurrentData->pointAy[1].fZ * pCar->direction.fY
              + pCurrentData->pointAy[2].fZ * pCar->direction.fZ;
            if (fDirectionYLocal2 > 0.0) {
              if (death_race)
                dUndergroundDamage = fDirectionYLocal2 * 0.005 * 4.0;
              else
                dUndergroundDamage = fDirectionYLocal2 * 0.005;
              fUndergroundDamage = (float)dUndergroundDamage;
              dodamage(pCar, fUndergroundDamage);
              iRollBackup2 = -pCar->iRollMomentum;
              //HIBYTE(fDirectionYLocal2) ^= 0x80u;
              fDirectionYLocal2 = -fDirectionYLocal2;
              pCar->iRollMomentum = iRollBackup2;
            }
            if (fDirectionYLocal2 > -40.0)
              fDirectionYLocal2 = -40.0;
            dNegSpeed = -fDirectionYLocal2;
            fNegSpeedCalc2 = (float)dNegSpeed;
            if (dNegSpeed > 40.0 && !pCar->bySfxCooldown) {
              dBankSoundVolume = fNegSpeedCalc2 * 32768.0 * 0.025;
              //_CHP();
              //LODWORD(fTempCalc) = (int)dBankSoundVolume;
              sfxpend(SOUND_SAMPLE_BANK, pCar->iDriverIdx, (int)dBankSoundVolume);// SOUND_SAMPLE_BANK
              pCar->bySfxCooldown = 9;
            }
            dInterpolatedY = fInterpolatedHeight3 - CarBaseY;
            dInterpolatedX = fLocalCoordX;
            dInterpolatedZ = fLocalCoordZ;
            pCar->pos.fX = (float)(pCurrentData->pointAy[0].fY * dInterpolatedY
              + pCurrentData->pointAy[0].fX * fLocalCoordX
              + pCurrentData->pointAy[0].fZ * fLocalCoordZ
              - pCurrentData->pointAy[3].fX);
            pCar->pos.fY = (float)(pCurrentData->pointAy[1].fX * dInterpolatedX
              + pCurrentData->pointAy[1].fY * dInterpolatedY
              + pCurrentData->pointAy[1].fZ * dInterpolatedZ
              - pCurrentData->pointAy[3].fY);
            pCar->pos.fZ = (float)(dInterpolatedY * pCurrentData->pointAy[2].fY
              + dInterpolatedX * pCurrentData->pointAy[2].fX
              + dInterpolatedZ * pCurrentData->pointAy[2].fZ
              - pCurrentData->pointAy[3].fZ);
            dDirectionY2 = fDirectionYLocal2;
            dDirectionX2 = fDirectionXTemp1;
            dDirectionZ2 = fDirectionZTemp1;
            pCar->direction.fX = pCurrentData->pointAy[0].fY * fDirectionYLocal2
              + pCurrentData->pointAy[0].fX * fDirectionXTemp1
              + pCurrentData->pointAy[0].fZ * fDirectionZTemp1;
            pCar->direction.fY = (float)(pCurrentData->pointAy[1].fX * dDirectionX2 + pCurrentData->pointAy[1].fY * dDirectionY2 + pCurrentData->pointAy[1].fZ * dDirectionZ2);
            dFinalDirectionZ = dDirectionY2 * pCurrentData->pointAy[2].fY + dDirectionX2 * pCurrentData->pointAy[2].fX + dDirectionZ2 * pCurrentData->pointAy[2].fZ;
            fTempCalc = pCar->direction.fX;
            fDirectionYTemp3 = pCar->direction.fY;
            fAngleCalcTemp4 = fTempCalc;
            fDirXFinal = fTempCalc;
            fDirectionYFinal2 = fDirectionYTemp3;
            pCar->direction.fZ = (float)dFinalDirectionZ;
            //if ((LODWORD(fDirXFinal) & 0x7FFFFFFF) != 0 || (LODWORD(fDirectionYTemp3) & 0x7FFFFFFF) != 0) {
            if (fabs(fDirXFinal) > FLT_EPSILON || fabs(fDirectionYTemp3) > FLT_EPSILON) {
              dAngle4 = atan2(fDirectionYFinal2, fAngleCalcTemp4) * 16384.0 / 6.28318530718;
              //_CHP();
              //LODWORD(fTempCalc) = (int)dAngle4;
              dYawCalc = (int)dAngle4 & 0x3FFF;
            } else {
              dYawCalc = 0;
            }
          LABEL_192:
            dFinalHorizSpeed = pCar->direction.fX * pCar->direction.fX + pCar->direction.fY * pCar->direction.fY;
            pCar->nActualYaw = dYawCalc;
            pCar->fHorizontalSpeed = (float)sqrt(dFinalHorizSpeed);
          }
        }
      }
    }
  }
}

//-------------------------------------------------------------------------------------------------
//00034CF0
void converttoair(tCar *pCar)
{
  int iCurrChunk; // eax
  double dRollSpeedCalc; // st7
  int iChunkBackup; // edi
  tData *pData; // edi
  double dFinalSpeed; // st7
  double dHorizontalSpeed; // st7
  int16 nTempChunk; // ax
  int iWorldPitch; // [esp+0h] [ebp-30h] BYREF
  int iWorldYaw1; // [esp+4h] [ebp-2Ch] BYREF
  float fWorldZ; // [esp+8h] [ebp-28h]
  float fWorldX; // [esp+Ch] [ebp-24h]
  int iWorldRoll; // [esp+10h] [ebp-20h] BYREF
  int iWorldYaw2; // [esp+14h] [ebp-1Ch] BYREF
  float fWorldY; // [esp+18h] [ebp-18h]

  pCar->iControlType = 0;                       // Set car to airborne control mode (0)
  iCurrChunk = pCar->nCurrChunk;
  if (iCurrChunk == -1)                       // Use backup chunk if current chunk is invalid (-1)
    iCurrChunk = pCar->nReferenceChunk;
  pCar->iLastValidChunk = iCurrChunk;
  if (iCurrChunk < 0)
    iCurrChunk += TRAK_LEN;
  dRollSpeedCalc = (double)localdata[iCurrChunk].iRoll * pCar->fFinalSpeed * 0.0013888889 + (double)pCar->iRollMomentum;// Calculate banking/roll effect based on speed (0.0013888889 = 1/720)
  //_CHP();
  pCar->iRollMomentum = (int)dRollSpeedCalc;
  iChunkBackup = pCar->nCurrChunk;
  getworldangles(pCar->nActualYaw, pCar->nPitch, pCar->nRoll, pCar->nReferenceChunk, &iWorldYaw1, &iWorldPitch, &iWorldRoll);// Convert car yaw3/pitch/roll to world angles using current chunk
  getworldangles(pCar->nYaw, pCar->nPitch, pCar->nRoll, pCar->nReferenceChunk, &iWorldYaw2, &iWorldPitch, &iWorldRoll);// Convert car yaw/pitch/roll to world angles
  pCar->nYaw = iWorldYaw2;
  pCar->nActualYaw = iWorldYaw2;
  pCar->nPitch = iWorldPitch;
  pData = &localdata[iChunkBackup];
  pCar->nRoll = iWorldRoll;
  fWorldX = pData->pointAy[0].fY * pCar->pos.fY + pData->pointAy[0].fX * pCar->pos.fX + pData->pointAy[0].fZ * pCar->pos.fZ - pData->pointAy[3].fX;// Transform car position from chunk-relative to world coordinates
  fWorldY = pData->pointAy[1].fY * pCar->pos.fY + pData->pointAy[1].fX * pCar->pos.fX + pData->pointAy[1].fZ * pCar->pos.fZ - pData->pointAy[3].fY;
  fWorldZ = pData->pointAy[2].fY * pCar->pos.fY + pData->pointAy[2].fX * pCar->pos.fX + pData->pointAy[2].fZ * pCar->pos.fZ - pData->pointAy[3].fZ;
  dFinalSpeed = pCar->fFinalSpeed;
  pCar->pos.fX = fWorldX;
  pCar->pos.fY = fWorldY;
  pCar->pos.fZ = fWorldZ;
  pCar->direction.fX = (float)(dFinalSpeed * tcos[iWorldYaw1] * tcos[iWorldPitch]);// Calculate 3D velocity vector from speed and world angles
  pCar->direction.fY = pCar->fFinalSpeed * tsin[iWorldYaw1] * tcos[iWorldPitch];
  pCar->direction.fZ = pCar->fFinalSpeed * tsin[iWorldPitch];
  dHorizontalSpeed = pCar->fFinalSpeed * tcos[iWorldPitch];
  pCar->fSpeedOverflow = 0.0;                   // Reset speed overflow and prepare for airborne physics
  nTempChunk = pCar->nCurrChunk;
  pCar->nCurrChunk = -1;                        // Set car as airborne (-1) and backup original chunk position
  pCar->nReferenceChunk = nTempChunk;
  pCar->fHorizontalSpeed = (float)dHorizontalSpeed;
}

//-------------------------------------------------------------------------------------------------
//00034EA0
void ordercars()
{
  int iTotalCars; // edi
  unsigned int uiSortIndex; // edx
  int iSortLimit; // esi
  tCar *pCurrentCar; // ebx
  tCar *pNextCar; // eax
  int8 byLapNumber; // cl
  int8 byCurrentProgress; // ch
  int iTempCarIndex; // eax
  int16 nCurrChunk; // cx
  int iSwapCarIndex; // ebx
  int iCarIndex; // eax
  int iSortCounter; // [esp+0h] [ebp-18h]
  int byPosition; // [esp+0h] [ebp-18h]

  iTotalCars = numcars;                         // Bubble sort implementation to order cars by race position
  uiSortIndex = 0;
  iSortLimit = 4 * (numcars - 1);               // Set up loop limit for bubble sort (numcars-1) iterations
  iSortCounter = 0;
  if (iSortLimit > 0) {
    do {
      pCurrentCar = &Car[carorder[uiSortIndex / 4]];// Get current car from sorted order array
      if (!finished_car[pCurrentCar->iDriverIdx])// Skip comparison if current car has finished the race
      {
        pNextCar = &Car[carorder[uiSortIndex / 4 + 1]];// Get next car in order for comparison
        if (pCurrentCar->nCurrChunk != -1 && pNextCar->nCurrChunk != -1 && !finished_car[pNextCar->iDriverIdx])// Only compare active cars that are on track and not finished
        {
          byLapNumber = pNextCar->byLapNumber;
          byCurrentProgress = pCurrentCar->byLapNumber;
          if (byLapNumber <= byCurrentProgress)// Compare race progress (byUnk24 - likely lap/sector progress)
          {                                     // If same progress, compare by track position (chunk and X coordinate)
            if (byLapNumber == byCurrentProgress) {
              nCurrChunk = pNextCar->nCurrChunk;
              if (nCurrChunk > pCurrentCar->nCurrChunk || nCurrChunk == pCurrentCar->nCurrChunk && pNextCar->pos.fX > (double)pCurrentCar->pos.fX) {
                iSwapCarIndex = carorder[uiSortIndex / 4];// Swap car positions: next car is ahead by position
                carorder[uiSortIndex / 4] = carorder[uiSortIndex / 4 + 1];
                carorder[uiSortIndex / 4 + 1] = iSwapCarIndex;
              }
            }
          } else {
            iTempCarIndex = carorder[uiSortIndex / 4];// Swap car positions: next car is ahead in progress
            carorder[uiSortIndex / 4] = carorder[uiSortIndex / 4 + 1];
            carorder[uiSortIndex / 4 + 1] = iTempCarIndex;
          }
        }
      }
      uiSortIndex += 4;
      ++iSortCounter;
    } while ((int)uiSortIndex < iSortLimit);
  }
  byPosition = 0;                               // Assign race positions based on sorted order (0=1st, 1=2nd, etc.)
  if (iTotalCars > 0) {
    iCarIndex = 0;
    do
      Car[carorder[iCarIndex++]].byRacePosition = byPosition++;
    while (iTotalCars > byPosition);
  }
  numcars = iTotalCars;
}

//-------------------------------------------------------------------------------------------------
//00034FC0
void changeline(tCar *pCar)
{
  int iCurrChunk; // eax
  tData *pData; // ecx
  int iRandValue; // eax
  int iRandForPos; // eax
  int iTargetLine; // ebp
  int iChangeSuccess; // edx
  int iPrevTargetLine; // eax
  int iRandNewLine; // eax

  iCurrChunk = pCar->nCurrChunk;
  pData = &localdata[iCurrChunk];
  if (pCar->iAITargetLine == -1)              // Check if car needs to select a new AI driving line
  {                                             // Check if center line marker is disabled (bit 24)
    if ((TrakColour[iCurrChunk][1] & SURFACE_FLAG_PIT_ZONE) == 0) { // Cars in 6th place or lower have reduced aggression
      if (pCar->byRacePosition >= 6u) {
        iRandForPos = rand();
        if (GetHighOrderRand(720, iRandForPos) != 100)
          goto LABEL_13;                        // 1 in 720 chance for line change when in poor position
        goto LABEL_7;
      }
    LABEL_12:
      pCar->iAITargetLine = 3;                  // Set target line to 3 (conservative choice for front runners)
      goto LABEL_13;
    }
    if (pCar->iAITargetCar != -1 && pCar->fHealth < 60.0)// If targeting another car and low health, stay defensive
    {
      pCar->iAITargetLine = 0;
      goto LABEL_13;
    }
    if (!pCar->iAICurrentLine)                // Check if car is not currently on an AI line
    {
      if (pCar->byRacePosition >= 6u) {
      LABEL_7:
        iRandValue = rand();                    // Select random AI line (0-3 modulo operation)
        pCar->iAITargetLine = GetHighOrderRand(4, iRandValue);// (iRandValue) >> 13;
        goto LABEL_13;
      }
      goto LABEL_12;
    }
  }
LABEL_13:
  iTargetLine = pCar->iAITargetLine;
  iChangeSuccess = 0;
  if (iTargetLine != -1 && linevalid(pCar->nCurrChunk, pCar->pos.fY, *(&pData->fAILine1 + iTargetLine)))// Check if target line is valid at current position
  {
    iPrevTargetLine = pCar->iAITargetLine;
    pCar->iAITargetLine = -1;                   // Successfully switch to target line, clear target
    iChangeSuccess = -1;
    pCar->iAICurrentLine = iPrevTargetLine;
  }
  if (!iChangeSuccess && !linevalid(pCar->nCurrChunk, pCar->pos.fY, *(&pData->fAILine1 + pCar->iAICurrentLine)))// If current line invalid and no change made, pick new random line
  {
    iRandNewLine = rand();
    pCar->iAITargetLine = GetHighOrderRand(4, iRandNewLine);// (iRandNewLine) >> 13;
  }
}

//-------------------------------------------------------------------------------------------------
//00035130
void driverange(tCar *pCar, float *pfLeftLimit, float *pfRightLimit)
{
  int iCurrChunk; // eax
  tTrackInfo *pTrackInfo; // esi
  int iLeftSurfaceType; // edx
  tData *pTrackData; // ebx
  int iRightSurfaceType; // eax
  unsigned int iLaneType; // eax
  double dTrackHalfWidth; // st7
  double dLeftBoundary; // st7
  unsigned int uiLeftLaneMarker; // [esp+0h] [ebp-28h]
  unsigned int uiRightLaneMarker; // [esp+4h] [ebp-24h]
  unsigned int uiCenterLaneMarker; // [esp+8h] [ebp-20h]
  int iRightSurfaceFlag; // [esp+Ch] [ebp-1Ch]
  int iLeftSurfaceFlag; // [esp+10h] [ebp-18h]

  iCurrChunk = pCar->nCurrChunk;
  pTrackInfo = &TrackInfo[iCurrChunk];
  iLeftSurfaceType = pTrackInfo->iLeftSurfaceType;
  pTrackData = &localdata[iCurrChunk];
  if (iLeftSurfaceType == 5 || iLeftSurfaceType == 6 || iLeftSurfaceType == 9)// Check if left surface is wall/barrier (types 5,6,9)
    iLeftSurfaceFlag = -1;
  else
    iLeftSurfaceFlag = 0;
  iRightSurfaceType = pTrackInfo->iRightSurfaceType;
  if (iRightSurfaceType == 5 || iRightSurfaceType == 6 || iRightSurfaceType == 9)// Check if right surface is wall/barrier (types 5,6,9)
    iRightSurfaceFlag = -1;
  else
    iRightSurfaceFlag = 0;
  uiLeftLaneMarker = abs(TrakColour[pCar->nCurrChunk][0]) & SURFACE_FLAG_SKIP_RENDER;
  uiCenterLaneMarker = abs(TrakColour[pCar->nCurrChunk][1]) & SURFACE_FLAG_SKIP_RENDER;
  uiRightLaneMarker = abs(TrakColour[pCar->nCurrChunk][2]) & SURFACE_FLAG_SKIP_RENDER;
  iLaneType = pCar->iLaneType;
  if (!iLaneType)                             // Lane type 0: Full track width driving (normal racing)
  {
    *pfLeftLimit = pTrackData->fTrackHalfWidth + pTrackInfo->fLShoulderWidth - pCar->fCarHalfWidth;// Set left boundary: track edge + left shoulder - car width
    if (iLeftSurfaceFlag || uiCenterLaneMarker)// If left wall/barrier or center lane marker, limit right boundary
    {
      *pfRightLimit = pTrackData->fTrackHalfWidth + pCar->fCarHalfWidth;
      return;
    }
    if (iRightSurfaceFlag || uiRightLaneMarker) {
      *pfRightLimit = pCar->fCarHalfWidth - pTrackData->fTrackHalfWidth;
      return;
    }
    goto LABEL_21;                              // If right wall/barrier or right lane marker, limit left boundary
  }
  if (iLaneType <= 1)                         // Lane type 1: Left lane driving only
  {                                             // Left lane: adjust left boundary based on wall/lane marker
    if (iLeftSurfaceFlag || uiLeftLaneMarker)
      dTrackHalfWidth = pTrackData->fTrackHalfWidth;
    else
      dTrackHalfWidth = pTrackData->fTrackHalfWidth + pTrackInfo->fLShoulderWidth;
    *pfLeftLimit = (float)(dTrackHalfWidth - pCar->fCarHalfWidth);
    if (iRightSurfaceFlag || uiRightLaneMarker) {
      *pfRightLimit = pCar->fCarHalfWidth - pTrackData->fTrackHalfWidth;
      return;
    }
  LABEL_21:
    *pfRightLimit = -pTrackData->fTrackHalfWidth - pTrackInfo->fRShoulderWidth + pCar->fCarHalfWidth;// Default right boundary: track edge + right shoulder - car width
    return;
  }
  if (iLaneType == 2)                         // Lane type 2: Right lane driving only
  {
    *pfRightLimit = -pTrackData->fTrackHalfWidth - pTrackInfo->fRShoulderWidth + pCar->fCarHalfWidth;// Right lane: set right boundary to track edge + shoulder
    if (iRightSurfaceFlag || uiCenterLaneMarker)// If right wall/barrier or center marker, use track center as left boundary
    {
      dLeftBoundary = -pTrackData->fTrackHalfWidth;
    } else {
      if (iLeftSurfaceFlag || uiLeftLaneMarker) {
        *pfLeftLimit = pTrackData->fTrackHalfWidth - pCar->fCarHalfWidth;
        return;
      }
      dLeftBoundary = pTrackData->fTrackHalfWidth + pTrackInfo->fLShoulderWidth;
    }
    *pfLeftLimit = (float)(dLeftBoundary - pCar->fCarHalfWidth);
  }
}

//-------------------------------------------------------------------------------------------------
//00035330
void updatesmokeandflames(tCar *pCar)
{
  int iCinematicMode; // edx
  int iRandSeed; // esi

  if (player_type != 2 && ViewType[0] == pCar->iDriverIdx && (!SelectedView[0] || SelectedView[0] == 2 || SelectedView[0] == 7))
    iCinematicMode = -1;
  else
    iCinematicMode = 0;
  iRandSeed = rand();
  dospray(pCar, iCinematicMode, CarSpray[pCar->iDriverIdx]);
  if (player_type == 2) {
    if (ViewType[0] == pCar->iDriverIdx)
      dospray(pCar, -1, CarSpray[16]);
    if (ViewType[1] == pCar->iDriverIdx)
      dospray(pCar, -1, CarSpray[17]);
  }
  srand(iRandSeed);
}

//-------------------------------------------------------------------------------------------------
//000353F0
void dospray(tCar *pCar, int iCinematicMode, tCarSpray *pCarSpray)
{
  int iCarDesignIdx; // edx
  int iSprayIdx; // ebp
  uint8 byType; // al
  int iRandomValue1; // eax
  char byLifeTime; // ah
  int iRandomValue2; // eax
  int iRandomValue3; // eax
  int iRandomValue4; // eax
  int iRandomVelY1; // eax
  int iRandomSize1; // eax
  int iRandomSize2; // eax
  int iRandomVelY2; // eax
  int iRandomSize3; // eax
  int iRandomVel1; // eax
  int iRandomVel2; // eax
  int iRandomLifetime1; // eax
  int iRandomColor1; // eax
  int iRandomColor2; // eax
  int iColorValue; // ebx
  int iTimer; // ebx
  int iRandomSize4; // eax
  int iRandomSize5; // eax
  int iRandomVelY3; // eax
  int iRandomVelX1; // edx
  int iRandomVelY4; // eax
  int iRandomVelY5; // eax
  int iRandomSize6; // eax
  int iRandomVelX2; // eax
  int iRandomVelY6; // eax
  int iRandomUnk5_1; // eax
  int iRandomVelY7; // eax
  int iRandomUnk5_2; // eax
  int iRandomVelX3; // eax
  int iRandomVelY8; // eax
  int iRandomUnk5_3; // eax
  int iCalculatedUnk5; // eax
  int iLifetimeDecrement; // eax
  unsigned int uiColor; // eax
  int iColorCycle; // eax
  int iColor; // edx
  int iRandomSizeGrow1; // eax
  int iSizeGrowCalc1; // eax
  int iRandomSizeShrink1; // eax
  int iSizeShrinkCalc1; // eax
  int v50; // eax
  int iRandomLifetime2; // eax
  int iRandomPlacement1; // eax
  int iPlacementIdx; // ebx
  int iRandomPosX1; // eax
  int iRandomPosY1; // eax
  int iRandomPosY2; // eax
  int iRandomPlacement2; // eax
  tVec3 *pCoordinates1; // eax
  int iPlacementIdx2; // ebx
  int iRandomPlacement3; // eax
  int iRandomPosX2; // eax
  int iRandomPosY3; // eax
  int iRandomPlacement4; // eax
  int iRandomPlacement5; // eax
  tVec3 *pCoordinates2; // eax
  int iRandomVelY9; // eax
  int iVelYCalc1; // eax
  int iRandomVelX4; // eax
  double dLifetimeCalc1; // st7
  int iRandomVelX5; // eax
  int iRandomVelY10; // eax
  int iVelYCalc2; // eax
  int iVelYBase; // edx
  int iRandomVelY11; // eax
  int iRandomUnk5_4; // eax
  int iRandomSize7; // eax
  double dLifetimeCalc2; // st7
  double dLifetimeCalc3; // st7
  float fHealthMultiplier1; // [esp+Ch] [ebp-30h]
  float fHealthMultiplier2; // [esp+10h] [ebp-2Ch]
  float fHealthMultiplier3; // [esp+14h] [ebp-28h]
  float fHealthMultiplier4; // [esp+18h] [ebp-24h]
  int iStoredCarDesignIdx; // [esp+1Ch] [ebp-20h]
  int *pPlaces; // [esp+20h] [ebp-1Ch]
  float fHealthFactor; // [esp+24h] [ebp-18h]
  int iTempVelocity; // [esp+28h] [ebp-14h]
  int iTempPosX; // [esp+28h] [ebp-14h]
  int iTempPosX2; // [esp+28h] [ebp-14h]
  int iTempRandomValue; // [esp+28h] [ebp-14h]
  int iTempVelX; // [esp+28h] [ebp-14h]
  int iTempVelY; // [esp+28h] [ebp-14h]

  fHealthFactor = (pCar->fHealth + 34.0f) * 0.01f;// Calculate health factor (0.0-1.0) based on car damage
  iCarDesignIdx = pCar->byCarDesignIdx;
  pPlaces = CarDesigns[iCarDesignIdx].pPlaces;  // Get car design data for spray particle placement points
  if (fHealthFactor > 1.0)
    fHealthFactor = 1.0;
  iSprayIdx = 0;
  iStoredCarDesignIdx = iCarDesignIdx;
  do {
    byType = pCarSpray->iType;
    if (byType) {                                           // Spray type 1: Active damage/smoke particles
      if (byType <= 1u) {
        iRandomColor1 = rand();                 // Generate random color values for active smoke particles
        iRandomColor2 = GetHighOrderRand(4, iRandomColor1);   // same as (4 * iRandomColor1) / 32768
        //iRandomColor2 = (4 * iRandomColor1 - (__CFSHL__((4 * iRandomColor1) >> 31, 15) + ((4 * iRandomColor1) >> 31 << 15))) >> 15;
        iColorValue = iRandomColor2 + 4;
        uint8 byColorComponent = ((iRandomColor2 + 4) >> 8) | 5;
        iColorValue = (iColorValue & 0xFFFF00FF) | (byColorComponent << 8);
        //BYTE1(iColorValue) = ((uint16)(iRandomColor2 + 4) >> 8) | 5;
        pCarSpray->iColor = iColorValue;
        iTimer = pCarSpray->iTimer;
        if (iCinematicMode)                   // Different particle behavior for menu/replay mode vs gameplay
        {
          iRandomSize4 = rand();
          iRandomSize5 = (GetHighOrderRand(60, iRandomSize4) + 20) * scr_size;
          //iRandomSize5 = (((60 * iRandomSize4 - (__CFSHL__((60 * iRandomSize4) >> 31, 15) + ((60 * iRandomSize4) >> 31 << 15))) >> 15) + 20) * scr_size;
          pCarSpray->fSize = (float)(iRandomSize5 >> 6);
          //pCarSpray->fSize = (float)((iRandomSize5 - (__CFSHL__(iRandomSize5 >> 31, 6) + (iRandomSize5 >> 31 << 6))) >> 6);
          iRandomVelY3 = rand();
          if (iTimer == 1)
            iRandomVelX1 = (GetHighOrderRand(50, iRandomVelY3) + 40) * scr_size;
            //iRandomVelX1 = (((50 * iRandomVelY3 - (__CFSHL__((50 * iRandomVelY3) >> 31, 15) + ((50 * iRandomVelY3) >> 31 << 15))) >> 15) + 40) * scr_size;
          else
            iRandomVelX1 = scr_size * (-40 - GetHighOrderRand(50, iRandomVelY3));
            //iRandomVelX1 = scr_size * (-40 - ((50 * iRandomVelY3 - (__CFSHL__((50 * iRandomVelY3) >> 31, 15) + ((50 * iRandomVelY3) >> 31 << 15))) >> 15));
          pCarSpray->velocity.fX = (float)(iRandomVelX1 >> 6);
          //pCarSpray->velocity.fX = (float)((iRandomVelX1 - (__CFSHL__(iRandomVelX1 >> 31, 6) + (iRandomVelX1 >> 31 << 6))) >> 6);
          iRandomVelY4 = rand();
          iRandomVelY5 = (GetHighOrderRand(100, iRandomVelY4) + 40) * scr_size;
          //iRandomVelY5 = (((100 * iRandomVelY4 - (__CFSHL__((100 * iRandomVelY4) >> 31, 15) + ((100 * iRandomVelY4) >> 31 << 15))) >> 15) + 40) * scr_size;
          pCarSpray->velocity.fY = (float)(iRandomVelY5 >> 6);
          //pCarSpray->velocity.fY = (float)((iRandomVelY5 - (__CFSHL__(iRandomVelY5 >> 31, 6) + (iRandomVelY5 >> 31 << 6))) >> 6);
          rand();
        } else {
          iRandomSize6 = rand();                // Generate larger particle sizes and velocities for normal gameplay mode
          pCarSpray->fSize = (float)(GetHighOrderRand(100, iRandomSize6) + 100);
          //pCarSpray->fSize = (float)(((100 * iRandomSize6 - (__CFSHL__((100 * iRandomSize6) >> 31, 15) + ((100 * iRandomSize6) >> 31 << 15))) >> 15) + 100);
          if (iTimer) {
            iRandomVelX3 = rand();
            if (iTimer == 1) {
              pCarSpray->velocity.fX = (float)(-100 - GetHighOrderRand(300, iRandomVelX3));
              //pCarSpray->velocity.fX = (float)(-100 - ((300 * iRandomVelX3 - (__CFSHL__((300 * iRandomVelX3) >> 31, 15) + ((300 * iRandomVelX3) >> 31 << 15))) >> 15));
              iRandomVelY7 = rand();
              pCarSpray->velocity.fY = (float)(-100 - GetHighOrderRand(200, iRandomVelY7));
              //pCarSpray->velocity.fY = (float)(-100 - ((200 * iRandomVelY7 - (__CFSHL__((200 * iRandomVelY7) >> 31, 15) + ((200 * iRandomVelY7) >> 31 << 15))) >> 15));
              iRandomUnk5_2 = rand();
              iCalculatedUnk5 = GetHighOrderRand(200, iRandomUnk5_2) + 200;
              //iCalculatedUnk5 = ((200 * iRandomUnk5_2 - (__CFSHL__((200 * iRandomUnk5_2) >> 31, 15) + ((200 * iRandomUnk5_2) >> 31 << 15))) >> 15) + 200;
            } else {
              pCarSpray->velocity.fX = (float)(-105 - GetHighOrderRand(150, iRandomVelX3));
              //pCarSpray->velocity.fX = (float)(-105 - ((150 * iRandomVelX3 - (__CFSHL__((150 * iRandomVelX3) >> 31, 15) + ((150 * iRandomVelX3) >> 31 << 15))) >> 15));
              iRandomVelY8 = rand();
              pCarSpray->velocity.fY = (float)GetHighOrderRand(400, iRandomVelY8);
              //pCarSpray->velocity.fY = (float)(((400 * iRandomVelY8 - (__CFSHL__((400 * iRandomVelY8) >> 31, 15) + ((400 * iRandomVelY8) >> 31 << 15))) >> 15) - 200);
              iRandomUnk5_3 = rand();
              iCalculatedUnk5 = GetHighOrderRand(30, iRandomUnk5_3) - 20;
              //iCalculatedUnk5 = ((30 * iRandomUnk5_3 - (__CFSHL__((30 * iRandomUnk5_3) >> 31, 15) + ((30 * iRandomUnk5_3) >> 31 << 15))) >> 15) - 20;
            }
          } else {
            iRandomVelX2 = rand();
            pCarSpray->velocity.fX = (float)(-100 - GetHighOrderRand(300, iRandomVelX2));
            //pCarSpray->velocity.fX = (float)(-100 - ((300 * iRandomVelX2 - (__CFSHL__((300 * iRandomVelX2) >> 31, 15) + ((300 * iRandomVelX2) >> 31 << 15))) >> 15));
            iRandomVelY6 = rand();
            pCarSpray->velocity.fY = (float)(GetHighOrderRand(200, iRandomVelY6) + 100);
            //pCarSpray->velocity.fY = (float)(((200 * iRandomVelY6 - (__CFSHL__((200 * iRandomVelY6) >> 31, 15) + ((200 * iRandomVelY6) >> 31 << 15))) >> 15) + 100);
            iRandomUnk5_1 = rand();
            iCalculatedUnk5 = GetHighOrderRand(200, iRandomUnk5_1) + 200;
            //iCalculatedUnk5 = ((200 * iRandomUnk5_1 - (__CFSHL__((200 * iRandomUnk5_1) >> 31, 15) + ((200 * iRandomUnk5_1) >> 31 << 15))) >> 15) + 200;
          }
          pCarSpray->velocity.fZ = (float)iCalculatedUnk5;
        }
        iLifetimeDecrement = pCarSpray->iLifeTime - 1;
        pCarSpray->iLifeTime = iLifetimeDecrement;
        if (iLifetimeDecrement < 0) {
          pCarSpray->iType = pCarSpray->iType & 0xFFFFFF00;  // Clear lowest byte only
          //LOBYTE(pCarSpray->iType) = 0;
          pCarSpray->iTimer = -1;
        }
      } else if (byType == 2)                   // Spray type 2: Death/explosion particles
      {
        if (pCar->fHealth <= 0.0) {                                       // Car is dead - generate explosion/death particles
          if (pCarSpray->iLifeTime <= 0) {
            if (pCar->nExplosionSoundTimer < 0) {
              if (replaytype != 2)
                sfxpend(SOUND_SAMPLE_EXPLO, pCar->iDriverIdx, 0x8000);// SOUND_SAMPLE_EXPLO
              iRandomValue3 = rand();
              pCar->nExplosionSoundTimer = GetHighOrderRand(4, iRandomValue3) + 4;
              //pCar->nExplosionSoundTimer = ((4 * iRandomValue3 - (__CFSHL__((4 * iRandomValue3) >> 31, 15) + ((4 * iRandomValue3) >> 31 << 15))) >> 15) + 4;
            }
            pCarSpray->position.fX = 0.0;
            pCarSpray->position.fY = 0.0;
            pCarSpray->position.fZ = 0.0;
            iRandomValue4 = rand();
            if ((iSprayIdx & 7) != 0) {
              pCarSpray->velocity.fX = (float)(50 - GetHighOrderRand(100, iRandomValue4));
              //pCarSpray->velocity.fX = (float)(50 - ((100 * iRandomValue4 - (__CFSHL__((100 * iRandomValue4) >> 31, 15) + ((100 * iRandomValue4) >> 31 << 15))) >> 15));
              iRandomVelY2 = rand();
              pCarSpray->velocity.fY = (float)(30 - GetHighOrderRand(60, iRandomVelY2));
              iRandomSize3 = rand();
              iRandomSize2 = GetHighOrderRand(64, iRandomSize3) + 128;
            } else {
              pCarSpray->velocity.fX = (float)(20 - GetHighOrderRand(40, iRandomValue4));
              iRandomVelY1 = rand();
              pCarSpray->velocity.fY = (float)(10 - GetHighOrderRand(20, iRandomVelY1));
              iRandomSize1 = rand();
              iRandomSize2 = GetHighOrderRand(320, iRandomSize1) + 320;
            }
            pCarSpray->fSize = (float)iRandomSize2;
            if (pCar->iStunned) {
              iRandomVel1 = rand();
              iTempVelocity = -5 - GetHighOrderRand(25, iRandomVel1);
            } else {
              iRandomVel2 = rand();
              iTempVelocity = GetHighOrderRand(25, iRandomVel2) + 5;
            }
            pCarSpray->velocity.fZ = (float)iTempVelocity;
            pCarSpray->iColor = 9485;
            iRandomLifetime1 = rand();
            pCarSpray->iLifeTime = GetHighOrderRand(64, iRandomLifetime1) + 32;
            //pCarSpray->iLifeTime = (((iRandomLifetime1 << 6)) >> 15) + 32;
          } else {
            if (pCarSpray->position.fX * pCarSpray->position.fX + pCarSpray->position.fY + pCarSpray->position.fY < 1125000.0
              && pCarSpray->position.fZ < 450.0
              && pCarSpray->position.fZ > -100.0) {
              iRandomValue1 = rand();
              pCarSpray->fSize = (float)(GetHighOrderRand(12, iRandomValue1) + 4)
                + pCarSpray->fSize;
              pCarSpray->position.fX = pCarSpray->velocity.fX + pCarSpray->position.fX;
              pCarSpray->position.fY = pCarSpray->velocity.fY + pCarSpray->position.fY;
              if (pCarSpray->position.fZ < 450.0 && pCarSpray->position.fZ > -100.0)
                pCarSpray->position.fZ = pCarSpray->velocity.fZ + pCarSpray->position.fZ;
            }
            --pCarSpray->iLifeTime;
            byLifeTime = pCarSpray->iLifeTime;
            pCarSpray->iTimer = 0;
            if ((byLifeTime & 7) == 0) {
              iRandomValue2 = (uint8)(pCarSpray->iColor) + 1;
              ++pCarSpray->iColor;
              if (iRandomValue2 > 20)
                pCarSpray->iColor = 9490;
            }
          }
        } else {
          pCarSpray->iLifeTime = -1;
          pCarSpray->iType = pCarSpray->iType & 0xFFFFFF00;  // Clear lowest byte only
          //LOBYTE(pCarSpray->iType) = 0;
          pCarSpray->iColor = 0x2516;
        }
      }
      goto NEXT_PARTICLE;
    }
    if (pCarSpray->iLifeTime <= 0)            // Spray type 0: Initialize new particles based on car health
    {
      if (pCar->fHealth > 0.0) {
        v50 = pCarSpray->iTimer;
        if (v50 <= 0) {
          if (fHealthFactor >= 1.0) {
            pCarSpray->iColor = 1302;
          } else {                                     // Generate damage particles based on health factor - more damage = more particles
            if ((double)ROLLERrand() * fHealthFactor < 8192.0 && fHealthFactor < 0.66) {
              pCarSpray->iType = (pCarSpray->iType & 0xFFFFFF00) | 1;
              //LOBYTE(pCarSpray->iType) = 1;
              iRandomLifetime2 = rand();
              pCarSpray->iLifeTime = GetHighOrderRand(16, iRandomLifetime2) + 8;
              if (pPlaces == (int *)-1) {
                pCarSpray->position.fX = 0.0;
                pCarSpray->position.fY = 0.0;
                pCarSpray->position.fZ = 0.0;
                pCarSpray->iTimer = -1;
              } else {
                if (iCinematicMode) {
                  iRandomPlacement1 = rand();
                  iPlacementIdx = GetHighOrderRand(2, iRandomPlacement1);
                  if (iPlacementIdx == 2)
                    iPlacementIdx = 1;
                  iRandomPosX1 = rand();
                  if (iPlacementIdx == 1)
                    iTempPosX = winw / 2 + GetHighOrderRand(scr_size, iRandomPosX1);
                  else
                    iTempPosX = winw / 2 - GetHighOrderRand(scr_size, iRandomPosX1);
                  pCarSpray->position.fX = (float)iTempPosX;
                  iRandomPosY1 = ROLLERrand(); //TODO look at this
                  pCarSpray->position.fY = (float)(((iRandomPosY1 * scr_size) >> 16)
                                                 + winh);
                } else {
                  iRandomPosY2 = rand();
                  pCarSpray->position.fY = (float)(GetHighOrderRand(32, iRandomPosY2) + winh);
                  iRandomPlacement2 = rand();
                  iPlacementIdx = GetHighOrderRand(4, iRandomPlacement2);
                  if (iPlacementIdx == 4)
                    iPlacementIdx = 3;
                  pCoordinates1 = &CarDesigns[iStoredCarDesignIdx].pCoords[pPlaces[iPlacementIdx]];// Select random attachment point from car design for particle placement
                  // CHEAT_MODE_TINY_CARS
                  if ((cheat_mode & CHEAT_MODE_TINY_CARS) == 0)// Handle CHEAT_MODE_TINY_CARS - scale particle positions accordingly
                  {
                    pCarSpray->position.fX = pCoordinates1->fX;
                    pCarSpray->position.fY = pCoordinates1->fY;
                    pCarSpray->position.fZ = pCoordinates1->fZ;
                  } else {
                    pCarSpray->position.fX = pCoordinates1->fX * 0.25f;
                    pCarSpray->position.fY = pCoordinates1->fY * 0.25f;
                    pCarSpray->position.fZ = pCoordinates1->fZ * 0.5f;
                  }
                  rand();
                }
                pCarSpray->iTimer = iPlacementIdx;
              }
            }
            if ((uint8)(pCarSpray->iType) != 1) {
              //TODO look at this
              iPlacementIdx2 = 0;
              //iPlacementIdx2 = (int)pPlaces;
              if (pPlaces == (int *)-1) {
                pCarSpray->position.fX = 0.0;
                pCarSpray->position.fY = 0.0;
                pCarSpray->position.fZ = 0.0;
              } else if (iCinematicMode) {
                iRandomPlacement3 = rand();
                iPlacementIdx2 = GetHighOrderRand(2, iRandomPlacement3);
                if (iPlacementIdx2 == 2)
                  iPlacementIdx2 = 1;
                iRandomPosX2 = rand();
                if (iPlacementIdx2 == 1)
                  iTempPosX2 = winw / 2 + GetHighOrderRand(scr_size, iRandomPosX2);
                else
                  iTempPosX2 = winw / 2 - GetHighOrderRand(scr_size, iRandomPosX2);
                pCarSpray->position.fX = (float)iTempPosX2;
                iRandomPosY3 = ROLLERrand(); //TODO: look at this
                pCarSpray->position.fY = (float)(((iRandomPosY3 * scr_size) >> 16)
                                               + winh);
              } else {                                 // Health-based placement logic: high health uses fewer placements
                if (fHealthFactor >= 0.75) {
                  iRandomPlacement5 = rand();
                  iPlacementIdx2 = GetHighOrderRand(2, iRandomPlacement5);
                  if (iPlacementIdx2 == 2)
                    iPlacementIdx2 = 1;
                } else {
                  iRandomPlacement4 = rand();
                  iPlacementIdx2 = GetHighOrderRand(4, iRandomPlacement4);
                  if (iPlacementIdx2 == 4)
                    iPlacementIdx2 = 3;
                }
                pCoordinates2 = &CarDesigns[iStoredCarDesignIdx].pCoords[pPlaces[iPlacementIdx2]];
                // CHEAT_MODE_TINY_CARS
                if ((cheat_mode & 0x8000) == 0) {
                  pCarSpray->position.fX = pCoordinates2->fX;
                  pCarSpray->position.fY = pCoordinates2->fY;
                  pCarSpray->position.fZ = pCoordinates2->fZ;
                } else {
                  pCarSpray->position.fX = pCoordinates2->fX * 0.25f;
                  pCarSpray->position.fY = pCoordinates2->fY * 0.25f;
                  pCarSpray->position.fZ = pCoordinates2->fZ * 0.5f;
                }
                rand();
                rand();
              }
              iTempRandomValue = rand();
              pCarSpray->iColor = 1302;
              pCarSpray->iLifeTime = GetHighOrderRand(24, iTempRandomValue) + 24;
              if (ROLLERrand() < 0x4000)
                pCarSpray->iColor |= 0x1000u;
                //BYTE1(pCarSpray->iColor) |= 0x10u;
              if (ROLLERrand() < 0x4000)
                pCarSpray->iColor |= 40000u;
                //BYTE2(pCarSpray->iColor) |= 4u;
              if (iCinematicMode) {
                iRandomVelY9 = rand();          // Calculate menu/replay mode velocities with screen scaling
                iVelYCalc1 = (-2 - GetHighOrderRand(6, iRandomVelY9)) * scr_size;
                pCarSpray->velocity.fY = (float)((iVelYCalc1) >> 6);
                iRandomVelX4 = rand();
                if (iPlacementIdx2 == 1)
                  iTempVelX = GetHighOrderRand(3, iRandomVelX4) + 3;
                else
                  iTempVelX = -3 - GetHighOrderRand(3, iRandomVelX4);
                pCarSpray->velocity.fX = (float)iTempVelX;
                pCarSpray->fSize = (float)(((double)ROLLERrand() * ((1.0 - fHealthFactor) * 32.0 + 32.0) * 0.000030517578125 + 32.0) * (double)scr_size * 0.015625);
                fHealthMultiplier4 = fHealthFactor * 512.0f;
                dLifetimeCalc1 = (double)ROLLERrand() * fHealthMultiplier4 * 0.000030517578 + 64.0;
                //_CHP();
                pCarSpray->iTimer = (int)dLifetimeCalc1;
                rand();
              } else {
                iRandomVelX5 = rand();          // Calculate gameplay mode velocities based on placement position
                pCarSpray->velocity.fX = (float)(-15 - GetHighOrderRand(20, iRandomVelX5));
                if (iPlacementIdx2) {
                  iRandomVelY10 = rand();
                  if (iPlacementIdx2 == 1) {
                    iVelYCalc2 = GetHighOrderRand(15, iRandomVelY10);
                    iVelYBase = -5;
                  } else {
                    iVelYCalc2 = GetHighOrderRand(40, iRandomVelY10);
                    iVelYBase = 20;
                  }
                  iTempVelY = iVelYBase - iVelYCalc2;
                } else {
                  iRandomVelY11 = rand();
                  iTempVelY = GetHighOrderRand(15, iRandomVelY11) + 5;
                }
                pCarSpray->velocity.fY = (float)iTempVelY;
                iRandomUnk5_4 = rand();
                pCarSpray->velocity.fZ = (float)GetHighOrderRand(10, iRandomUnk5_4);
                iRandomSize7 = rand();
                pCarSpray->fSize = (float)(GetHighOrderRand(10, iRandomSize7) + 10);
                if (fHealthFactor <= 0.6) {
                  if (pCar->fHealth <= 0.0) {
                    fHealthMultiplier2 = fHealthFactor * 64.0f;
                    dLifetimeCalc3 = (double)ROLLERrand() * fHealthMultiplier2 * 0.000030517578 + 18.0;
                  } else {
                    fHealthMultiplier3 = fHealthFactor * 256.0f;
                    dLifetimeCalc3 = (double)ROLLERrand() * fHealthMultiplier3 * 0.000030517578 + 24.0;
                  }
                  //_CHP();
                  pCarSpray->iTimer = (int)dLifetimeCalc3;
                } else {
                  fHealthMultiplier1 = fHealthFactor * 1024.0f;
                  dLifetimeCalc2 = (double)ROLLERrand() * fHealthMultiplier1 * 0.000030517578 + 36.0;
                  //_CHP();
                  pCarSpray->iTimer = (int)dLifetimeCalc2;
                }
              }
            }
          }
        } else {
          pCarSpray->iColor = 1302;
          pCarSpray->iTimer = v50 - 1;
        }
      } else if (pCar->nDeathTimer <= 108) {
        pCarSpray->iLifeTime = -1;
        pCarSpray->iTimer = -1;
        pCarSpray->iType = (pCarSpray->iType & 0xFFFFFF00) | 2;
        //LOBYTE(pCarSpray->iType) = 2;
        pCar->nExplosionSoundTimer = -1;
      }
      goto NEXT_PARTICLE;
    }
    if ((pCarSpray->iLifeTime & 3) == 0)      // Animate particle colors by cycling through color palette every 4 frames
    {
      uiColor = (uint8)pCarSpray->iColor;
      if (uiColor < 0x15) {
        if (!(uint8)pCarSpray->iColor) {
          iColorCycle = 21;
          goto SET_COLOR_CYCLE;
        }
      } else if (uiColor > 0x15) {
        if (uiColor <= 0x16) {
          iColorCycle = 23;
        } else {
          if (uiColor != 23)
            goto DEFAULT_COLOR_CYCLE;
          iColorCycle = 0;
        }
      SET_COLOR_CYCLE:
        iColor = pCarSpray->iColor;
        iColor = iColor & 0xFFFFFF00;  // Clear lowest byte only
        //LOBYTE(iColor) = 0;
        pCarSpray->iColor = iColorCycle | iColor;
        if (ROLLERrand() < 0x4000)
          pCarSpray->iColor ^= 0x1000u;
          //BYTE1(pCarSpray->iColor) ^= 0x10u;
        if (ROLLERrand() < 0x4000)
          pCarSpray->iColor ^= 40000u;
          //BYTE2(pCarSpray->iColor) ^= 4u;
        goto CONTINUE_COLOR_ANIMATION;
      }
    DEFAULT_COLOR_CYCLE:
      iColorCycle = 22;
      goto SET_COLOR_CYCLE;
    }
  CONTINUE_COLOR_ANIMATION:
    if ((uint8)pCarSpray->iColor && (uint8)pCarSpray->iColor < 0x15u || (uint8)pCarSpray->iColor > 0x17u)
      pCarSpray->iColor = 1302;
    pCarSpray->position.fX = pCarSpray->velocity.fX + pCarSpray->position.fX;// Update particle physics: position += velocity, apply gravity/forces
    pCarSpray->position.fY = pCarSpray->velocity.fY + pCarSpray->position.fY;
    pCarSpray->position.fZ = pCarSpray->velocity.fZ + pCarSpray->position.fZ;
    if (iCinematicMode) {
      rand();
    } else {                                           // Fade out particles: reduce size as they near end of lifetime
      if (pCarSpray->iLifeTime <= 8) {
        iRandomSizeShrink1 = rand();
        iSizeShrinkCalc1 = (GetHighOrderRand(24, iRandomSizeShrink1) + 10) * scr_size;
        pCarSpray->fSize = pCarSpray->fSize - (float)((iSizeShrinkCalc1) >> 6);
        --pCarSpray->iLifeTime;
        goto NEXT_PARTICLE;
      }
      iRandomSizeGrow1 = rand();
      iSizeGrowCalc1 = (GetHighOrderRand(8, iRandomSizeGrow1) + 8) * scr_size;
      pCarSpray->fSize = (float)((iSizeGrowCalc1) >> 6) + pCarSpray->fSize;
    }
    --pCarSpray->iLifeTime;
  NEXT_PARTICLE:
    ++iSprayIdx;
    ++pCarSpray;
  } while (iSprayIdx < 32);                     // Main loop: process all 32 spray particles for this car
}

//-------------------------------------------------------------------------------------------------
//00036280
void calculateseparatedcoordinatesystem(int iChunk, tData *pChunkData)
{
  int iNextChunk; // ecx
  int iCurrentChunk; // eax
  tData *pLocalData; // ebx
  double dInverseTrackLength; // st5
  double dCenterDeltaY; // st6
  float fTangentX; // [esp+0h] [ebp-28h]
  float fTangentY; // [esp+4h] [ebp-24h]

  iNextChunk = iChunk + 1;                      // Calculate next chunk index
  if (iChunk + 1 >= TRAK_LEN)                 // Wrap around if we've reached the end of the track
    iNextChunk = 0;
  iCurrentChunk = iChunk;
  pLocalData = &localdata[iChunk];              // Get pointer to local data for current chunk
  dInverseTrackLength = 1.0 / (pLocalData->fTrackHalfLength * 2.0);// Calculate inverse track length for normalization
  fTangentX = ((TrakPt[iNextChunk].pointAy[2].fX + TrakPt[iNextChunk].pointAy[3].fX) * 0.5f - (TrakPt[iCurrentChunk].pointAy[2].fX + TrakPt[iCurrentChunk].pointAy[3].fX) * 0.5f)
    * (float)dInverseTrackLength;              // Calculate X component of track tangent vector (normalized)
  dCenterDeltaY = (TrakPt[iNextChunk].pointAy[2].fY + TrakPt[iNextChunk].pointAy[3].fY) * 0.5
    - (TrakPt[iCurrentChunk].pointAy[2].fY + TrakPt[iCurrentChunk].pointAy[3].fY) * 0.5;// Calculate Y difference between track centers
  fTangentY = (float)(dInverseTrackLength * dCenterDeltaY);
  pChunkData->pointAy[0].fY = (float)(-(dInverseTrackLength * dCenterDeltaY));// Set up local coordinate system - pointAy[0] is right vector
  pChunkData->pointAy[0].fZ = 0.0;
  pChunkData->pointAy[1].fZ = 0.0;
  pChunkData->pointAy[2].fX = 0.0;              // Set up local coordinate system - pointAy[2] is up vector (0,0,1)
  pChunkData->pointAy[2].fY = 0.0;
  pChunkData->pointAy[2].fZ = 1.0;
  pChunkData->pointAy[0].fX = fTangentX;        // Set up local coordinate system - pointAy[1] is forward vector
  pChunkData->pointAy[1].fX = fTangentY;
  pChunkData->pointAy[1].fY = fTangentX;
  pChunkData->pointAy[3] = pLocalData->pointAy[3];// Copy track position and dimensions from local data
  pChunkData->fTrackHalfLength = pLocalData->fTrackHalfLength;
  pChunkData->fTrackHalfWidth = pLocalData->fTrackHalfWidth;
}

//-------------------------------------------------------------------------------------------------
//00036390
void findnearsection(tCar *pCar, int *piNearestChunk)
{
  int iTrakLen; // ecx
  int iNearestChunk; // edi
  int iForwardChunkIdx; // edx
  tData *pForwardData; // esi
  int iForwardGroundIdx; // ebp
  double dForwardCarX; // st7
  double dForwardCarY; // st6
  double dForwardAbsY; // st5
  double dForwardCarZ; // st4
  double dForwardAbsZ; // rt0
  double dForwardAbsX; // st4
  int iBackwardChunkIdx; // edx
  tData *pBackwardData; // esi
  int iBackwardGroundIdx; // ebp
  double dBackwardCarX; // st7
  double dBackwardCarY; // st6
  double dBackwardAbsY; // st5
  double dBackwardCarZ; // st4
  double dBackwardAbsZ; // rt2
  double dBackwardAbsX; // st4
  int iForwardExtraStart; // ebp
  int iForwardExtraIdx; // edx
  tData *pForwardExtraData; // esi
  int iForwardExtraGroundIdx; // ebp
  double dForwardExtraCarX; // st7
  double dForwardExtraCarY; // st6
  double dForwardExtraAbsY; // st5
  double dForwardExtraCarZ; // st4
  double dForwardExtraAbsZ; // rt0
  double dForwardExtraAbsX; // st4
  int iBackwardExtraStart; // ebp
  tData *pBackwardExtraData; // esi
  int iBackwardExtraIdx; // edx
  int iBackwardExtraGroundIdx; // ebp
  double dBackwardExtraCarX; // st7
  double dBackwardExtraCarY; // st6
  double dBackwardExtraAbsY; // st5
  double dBackwardExtraCarZ; // st4
  double dBackwardExtraAbsZ; // rt2
  double dBackwardExtraAbsX; // st4
  tTrakView *pTrakView; // [esp+10h] [ebp-6Ch]
  int iLastValidChunk; // [esp+14h] [ebp-68h]
  int iBackwardMainEnd; // [esp+18h] [ebp-64h]
  int iBackwardExtraEnd; // [esp+1Ch] [ebp-60h]
  int iForwardMainEnd; // [esp+20h] [ebp-5Ch]
  float fBackwardDotProduct; // [esp+24h] [ebp-58h]
  float fForwardExtraDotProduct; // [esp+28h] [ebp-54h]
  float fBackwardExtraDotProduct; // [esp+2Ch] [ebp-50h]
  float fForwardDotProduct; // [esp+30h] [ebp-4Ch]
  float fBackwardExtraPosition; // [esp+34h] [ebp-48h]
  float fForwardExtraPosition; // [esp+38h] [ebp-44h]
  float fBackwardPosition; // [esp+3Ch] [ebp-40h]
  float fForwardPosition; // [esp+40h] [ebp-3Ch]
  float fForwardExtraDistance; // [esp+44h] [ebp-38h]
  float fForwardDistance; // [esp+48h] [ebp-34h]
  float fBackwardExtraDistance; // [esp+4Ch] [ebp-30h]
  float fBackwardDistance; // [esp+50h] [ebp-2Ch]
  float fMinCurrentDistance; // [esp+54h] [ebp-28h]
  float fMinValidDistance; // [esp+58h] [ebp-24h]
  float fTempCarX; // [esp+5Ch] [ebp-20h]
  float fTempCarX2; // [esp+5Ch] [ebp-20h]
  int iForwardExtraEnd; // [esp+60h] [ebp-1Ch]
  float fTempCarX3; // [esp+64h] [ebp-18h]
  float fTempCarX4; // [esp+64h] [ebp-18h]

  iTrakLen = TRAK_LEN;                          // Store track length and initialize minimum distance trackers
  fMinValidDistance = 1.0e10;
  fMinCurrentDistance = 1.0e10;
  iLastValidChunk = pCar->iLastValidChunk;      // Get car's last known valid track chunk and setup search parameters
  iNearestChunk = -1;
  pTrakView = &TrakView[iLastValidChunk];
  iForwardMainEnd = pTrakView->byForwardMainChunks + iLastValidChunk;
  iForwardChunkIdx = iLastValidChunk;
  pForwardData = &localdata[iLastValidChunk];
  if (iLastValidChunk < iForwardMainEnd)      // Search forward main chunks for nearest track section
  {
    iForwardGroundIdx = iLastValidChunk;
    do {
      dForwardCarX = pCar->pos.fX + pForwardData->pointAy[3].fX;// Calculate car position relative to current track chunk
      fTempCarX3 = (float)dForwardCarX;
      dForwardCarY = pCar->pos.fY + pForwardData->pointAy[3].fY;
      dForwardAbsY = fabs(dForwardCarY);
      dForwardCarZ = pCar->pos.fZ + pForwardData->pointAy[3].fZ;
      fForwardDotProduct = pForwardData->pointAy[0].fZ * pCar->direction.fX
        + pForwardData->pointAy[1].fZ * pCar->direction.fY
        + pForwardData->pointAy[2].fZ * pCar->direction.fZ;
      dForwardAbsZ = fabs(dForwardCarZ);
      fForwardPosition = (float)(dForwardCarY * pForwardData->pointAy[1].fZ + pForwardData->pointAy[0].fZ * fTempCarX3 + dForwardCarZ * pForwardData->pointAy[2].fZ);
      dForwardAbsX = fabs(dForwardCarX);
      fForwardDistance = (float)sqrt(dForwardAbsX * dForwardAbsX + dForwardAbsY * dForwardAbsY + dForwardAbsZ * dForwardAbsZ) - pForwardData->fTrackHalfLength;// Calculate 3D distance from car to track centerline
      if (iForwardGroundIdx >= 0 && iForwardGroundIdx < TRAK_LEN && //bounds check added by ROLLER
        (fForwardPosition >= -400.0 || GroundColour[iForwardGroundIdx][2] >= 0) && fForwardDistance < (double)fMinValidDistance)// Check position validity and update closest valid chunk if closer
      {
        fMinValidDistance = fForwardDistance;
        if (iForwardChunkIdx < iTrakLen) {
          if (iForwardChunkIdx >= 0)
            pCar->iLastValidChunk = iForwardChunkIdx;
          else
            pCar->iLastValidChunk = iTrakLen + iForwardChunkIdx;
        } else {
          pCar->iLastValidChunk = iForwardChunkIdx - iTrakLen;
        }
      }
      if (iForwardGroundIdx >= 0 && iForwardGroundIdx < TRAK_LEN && //bounds check added by ROLLER
        (fForwardDotProduct * 4.0 + -500.0 <= fForwardPosition || GroundColour[iForwardGroundIdx][2] >= 0) && fForwardDistance < (double)fMinCurrentDistance)// Check forward position and update nearest current chunk if closer
      {
        fMinCurrentDistance = fForwardDistance;
        if (iForwardChunkIdx < iTrakLen) {
          if (iForwardChunkIdx >= 0)
            iNearestChunk = iForwardChunkIdx;
          else
            iNearestChunk = iForwardChunkIdx + iTrakLen;
        } else {
          iNearestChunk = iForwardChunkIdx - iTrakLen;
        }
      }
      if (iForwardChunkIdx == iTrakLen - 1)   // Handle track wrapping: loop back to start when reaching end
        pForwardData = localdata;
      else
        ++pForwardData;
      ++iForwardChunkIdx;
      ++iForwardGroundIdx;
    } while (iForwardChunkIdx < iForwardMainEnd);
  }
  iBackwardMainEnd = iLastValidChunk - pTrakView->byBackwardMainChunks;
  iBackwardChunkIdx = iLastValidChunk;
  pBackwardData = &localdata[iLastValidChunk];
  if (iLastValidChunk > iBackwardMainEnd)     // Search backward main chunks for nearest track section
  {
    iBackwardGroundIdx = iLastValidChunk;
    do {
      dBackwardCarX = pCar->pos.fX + pBackwardData->pointAy[3].fX;
      fTempCarX = (float)dBackwardCarX;
      dBackwardCarY = pCar->pos.fY + pBackwardData->pointAy[3].fY;
      dBackwardAbsY = fabs(dBackwardCarY);
      dBackwardCarZ = pCar->pos.fZ + pBackwardData->pointAy[3].fZ;
      fBackwardDotProduct = pBackwardData->pointAy[0].fZ * pCar->direction.fX
        + pBackwardData->pointAy[1].fZ * pCar->direction.fY
        + pBackwardData->pointAy[2].fZ * pCar->direction.fZ;
      dBackwardAbsZ = fabs(dBackwardCarZ);
      fBackwardPosition = (float)(dBackwardCarY * pBackwardData->pointAy[1].fZ + pBackwardData->pointAy[0].fZ * fTempCarX + dBackwardCarZ * pBackwardData->pointAy[2].fZ);
      dBackwardAbsX = fabs(dBackwardCarX);
      fBackwardDistance = (float)sqrt(dBackwardAbsX * dBackwardAbsX + dBackwardAbsY * dBackwardAbsY + dBackwardAbsZ * dBackwardAbsZ) - pBackwardData->fTrackHalfLength;
      if (iBackwardGroundIdx >= 0 && iBackwardGroundIdx < TRAK_LEN && //bounds check added by ROLLER
        (fBackwardPosition >= -400.0 || GroundColour[iBackwardGroundIdx][2] >= 0) && fBackwardDistance < (double)fMinValidDistance) {
        fMinValidDistance = fBackwardDistance;
        if (iBackwardChunkIdx < iTrakLen) {
          if (iBackwardChunkIdx >= 0)
            pCar->iLastValidChunk = iBackwardChunkIdx;
          else
            pCar->iLastValidChunk = iTrakLen + iBackwardChunkIdx;
        } else {
          pCar->iLastValidChunk = iBackwardChunkIdx - iTrakLen;
        }
      }
      if (iBackwardGroundIdx >= 0 && iBackwardGroundIdx < TRAK_LEN && //bounds check added by ROLLER
        (fBackwardDotProduct * 4.0 + -500.0 <= fBackwardPosition || GroundColour[iBackwardGroundIdx][2] >= 0) && fBackwardDistance < (double)fMinCurrentDistance) {
        fMinCurrentDistance = fBackwardDistance;
        if (iBackwardChunkIdx < iTrakLen) {
          if (iBackwardChunkIdx >= 0)
            iNearestChunk = iBackwardChunkIdx;
          else
            iNearestChunk = iBackwardChunkIdx + iTrakLen;
        } else {
          iNearestChunk = iBackwardChunkIdx - iTrakLen;
        }
      }
      if (iBackwardChunkIdx)                  // Handle track wrapping: loop to end when reaching start
        --pBackwardData;
      else
        pBackwardData = &localdata[iTrakLen - 1];
      --iBackwardChunkIdx;
      --iBackwardGroundIdx;
    } while (iBackwardChunkIdx > iBackwardMainEnd);
  }
  if ((TrakColour[iLastValidChunk][1] & SURFACE_FLAG_NO_EXTRAS) == 0)// Check if not on special track section - if so, search extra chunks
  {
    iForwardExtraStart = pTrakView->nForwardExtraStart;
    iForwardExtraEnd = pTrakView->byForwardExtraChunks + iForwardExtraStart;
    iForwardExtraIdx = iForwardExtraStart;
    if (iForwardExtraStart >= 0 && iForwardExtraStart < TRAK_LEN) //check added by ROLLER
      pForwardExtraData = &localdata[iForwardExtraStart];
    else
      pForwardExtraData = NULL;

    if (iForwardExtraStart < iForwardExtraEnd && pForwardExtraData)// Search forward extra chunks for nearest track section
    {
      iForwardExtraGroundIdx = iForwardExtraStart;
      do {
        dForwardExtraCarX = pCar->pos.fX + pForwardExtraData->pointAy[3].fX;
        fTempCarX2 = (float)dForwardExtraCarX;
        dForwardExtraCarY = pCar->pos.fY + pForwardExtraData->pointAy[3].fY;
        dForwardExtraAbsY = fabs(dForwardExtraCarY);
        dForwardExtraCarZ = pCar->pos.fZ + pForwardExtraData->pointAy[3].fZ;
        fForwardExtraDotProduct = pForwardExtraData->pointAy[0].fZ * pCar->direction.fX
          + pForwardExtraData->pointAy[1].fZ * pCar->direction.fY
          + pForwardExtraData->pointAy[2].fZ * pCar->direction.fZ;
        dForwardExtraAbsZ = fabs(dForwardExtraCarZ);
        fForwardExtraPosition = (float)(dForwardExtraCarY * pForwardExtraData->pointAy[1].fZ
          + pForwardExtraData->pointAy[0].fZ * fTempCarX2
          + dForwardExtraCarZ * pForwardExtraData->pointAy[2].fZ);
        dForwardExtraAbsX = fabs(dForwardExtraCarX);
        fForwardExtraDistance = (float)sqrt(dForwardExtraAbsX * dForwardExtraAbsX + dForwardExtraAbsY * dForwardExtraAbsY + dForwardExtraAbsZ * dForwardExtraAbsZ)
          - pForwardExtraData->fTrackHalfLength;
        if ((fForwardExtraPosition >= -400.0 || GroundColour[iForwardExtraGroundIdx][2] >= 0) && fForwardExtraDistance < (double)fMinValidDistance) {
          fMinValidDistance = fForwardExtraDistance;
          if (iForwardExtraIdx < iTrakLen) {
            if (iForwardExtraIdx >= 0)
              pCar->iLastValidChunk = iForwardExtraIdx;
            else
              pCar->iLastValidChunk = iTrakLen + iForwardExtraIdx;
          } else {
            pCar->iLastValidChunk = iForwardExtraIdx - iTrakLen;
          }
        }
        if ((fForwardExtraDotProduct * 4.0 + -500.0 <= fForwardExtraPosition || GroundColour[iForwardExtraGroundIdx][2] >= 0)
          && fForwardExtraDistance < (double)fMinCurrentDistance) {
          fMinCurrentDistance = fForwardExtraDistance;
          if (iForwardExtraIdx < iTrakLen) {
            if (iForwardExtraIdx >= 0)
              iNearestChunk = iForwardExtraIdx;
            else
              iNearestChunk = iForwardExtraIdx + iTrakLen;
          } else {
            iNearestChunk = iForwardExtraIdx - iTrakLen;
          }
        }
        if (iForwardExtraIdx == iTrakLen - 1)
          pForwardExtraData = localdata;
        else
          ++pForwardExtraData;
        ++iForwardExtraIdx;
        ++iForwardExtraGroundIdx;
      } while (iForwardExtraIdx < iForwardExtraEnd);
    }
    if (pTrakView->nBackwardExtraStart >= 0 && pTrakView->nBackwardExtraStart < TRAK_LEN) { //bounds check added by ROLLER
      iBackwardExtraStart = pTrakView->nBackwardExtraStart;
      iBackwardExtraEnd = iBackwardExtraStart - pTrakView->byBackwardExtraChunks;
      pBackwardExtraData = &localdata[iBackwardExtraStart];
      iBackwardExtraIdx = iBackwardExtraStart;
      if (iBackwardExtraStart > iBackwardExtraEnd)// Search backward extra chunks for nearest track section
      {
        iBackwardExtraGroundIdx = iBackwardExtraStart;
        do {
          dBackwardExtraCarX = pCar->pos.fX + pBackwardExtraData->pointAy[3].fX;
          fTempCarX4 = (float)dBackwardExtraCarX;
          dBackwardExtraCarY = pCar->pos.fY + pBackwardExtraData->pointAy[3].fY;
          dBackwardExtraAbsY = fabs(dBackwardExtraCarY);
          dBackwardExtraCarZ = pCar->pos.fZ + pBackwardExtraData->pointAy[3].fZ;
          fBackwardExtraDotProduct = pBackwardExtraData->pointAy[0].fZ * pCar->direction.fX
            + pBackwardExtraData->pointAy[1].fZ * pCar->direction.fY
            + pBackwardExtraData->pointAy[2].fZ * pCar->direction.fZ;
          dBackwardExtraAbsZ = fabs(dBackwardExtraCarZ);
          fBackwardExtraPosition = (float)(dBackwardExtraCarY * pBackwardExtraData->pointAy[1].fZ
            + pBackwardExtraData->pointAy[0].fZ * fTempCarX4
            + dBackwardExtraCarZ * pBackwardExtraData->pointAy[2].fZ);
          dBackwardExtraAbsX = fabs(dBackwardExtraCarX);
          fBackwardExtraDistance = (float)sqrt(dBackwardExtraAbsX * dBackwardExtraAbsX + dBackwardExtraAbsY * dBackwardExtraAbsY + dBackwardExtraAbsZ * dBackwardExtraAbsZ)
            - pBackwardExtraData->fTrackHalfLength;
          if ((fBackwardExtraPosition >= -400.0 || GroundColour[iBackwardExtraGroundIdx][2] >= 0) && fBackwardExtraDistance < (double)fMinValidDistance) {
            fMinValidDistance = fBackwardExtraDistance;
            if (iBackwardExtraIdx < iTrakLen) {
              if (iBackwardExtraIdx >= 0)
                pCar->iLastValidChunk = iBackwardExtraIdx;
              else
                pCar->iLastValidChunk = iTrakLen + iBackwardExtraIdx;
            } else {
              pCar->iLastValidChunk = iBackwardExtraIdx - iTrakLen;
            }
          }
          if ((fBackwardExtraDotProduct * 4.0 + -500.0 <= fBackwardExtraPosition || GroundColour[iBackwardExtraGroundIdx][2] >= 0)
            && fBackwardExtraDistance < (double)fMinCurrentDistance) {
            fMinCurrentDistance = fBackwardExtraDistance;
            if (iBackwardExtraIdx < iTrakLen) {
              if (iBackwardExtraIdx >= 0)
                iNearestChunk = iBackwardExtraIdx;
              else
                iNearestChunk = iBackwardExtraIdx + iTrakLen;
            } else {
              iNearestChunk = iBackwardExtraIdx - iTrakLen;
            }
          }
          if (iBackwardExtraIdx)
            --pBackwardExtraData;
          else
            pBackwardExtraData = &localdata[iTrakLen - 1];
          --iBackwardExtraIdx;
          --iBackwardExtraGroundIdx;
        } while (iBackwardExtraIdx > iBackwardExtraEnd);
      }
    }
  }
  *piNearestChunk = iNearestChunk;              // Return index of nearest track chunk found
  TRAK_LEN = iTrakLen;
}

//-------------------------------------------------------------------------------------------------
//000369C0
void dozoomstuff(int iPlayerIdx)
{
  int iPrevChampMode; // ebx
  int iPlayerIdx_1; // eax
  int iZoomCountdown; // esi
  double dZoomInScale; // st7
  int iPlayerIdx2; // edx
  double dZoomOutScale; // st7
  int iPlayerIdx3; // edx

  iPrevChampMode = champ_mode;                  // Store current championship mode state
  if (champ_mode >= 16)                       // Decrement champ counter if in championship mode
    --champ_count;
  iPlayerIdx_1 = iPlayerIdx;
  iZoomCountdown = game_count[iPlayerIdx];      // Get zoom countdown timer for this player
  if (iZoomCountdown <= 0) {
    if (iZoomCountdown >= 0) {
      if (champ_mode)
        dZoomOutScale = game_scale[iPlayerIdx] * 0.5;// Zoom out phase: apply zoom factor based on championship vs regular mode
      else
        dZoomOutScale = game_scale[iPlayerIdx] * 0.7692307692307693;
      game_scale[iPlayerIdx] = (float)dZoomOutScale;
      iPlayerIdx3 = iPlayerIdx;
      if (game_scale[iPlayerIdx3] < 64.0)     // Check if zoom has reached minimum scale limit
      {
        game_scale[iPlayerIdx3] = 64.0;         // Clamp to minimum zoom (64.0) and set appropriate countdown timer
        if (iPrevChampMode) {
          game_count[iPlayerIdx3] = 36;
          champ_mode = iPrevChampMode;
          return;
        }
        game_count[iPlayerIdx3] = 72;
      }
    } else if (iZoomCountdown > -2) {
      dZoomInScale = champ_mode ? game_scale[iPlayerIdx] * 2.0 : game_scale[iPlayerIdx] * 1.3;
      game_scale[iPlayerIdx] = (float)dZoomInScale;
      iPlayerIdx2 = iPlayerIdx;
      if (game_scale[iPlayerIdx2] > 32768.0)  // Check if zoom has reached maximum scale limit
      {
        game_scale[iPlayerIdx2] = 32768.0;      // Clamp to maximum zoom and disable further zooming
        game_count[iPlayerIdx2] = -2;
        sub_on[iPlayerIdx2] = 0;
        champ_mode = iPrevChampMode;
        return;
      }
    }
  } else {
    game_count[iPlayerIdx_1] = iZoomCountdown - 1;// Countdown is positive - decrement timer
    if (iZoomCountdown == 1 && game_scale[iPlayerIdx_1] == 64.0)// Special case: if countdown reaches 1 and scale is at minimum (64.0), start zoom in
      game_count[iPlayerIdx_1] = -1;
  }
  champ_mode = iPrevChampMode;                  // Restore championship mode state
}

//-------------------------------------------------------------------------------------------------
//00036B10
int findcardistance(int iCarIdx, float fMaxDistance)
{
  int iClosestCarIdx; // esi
  int iCarCounter; // ebx
  int iCurrentCarIdx; // edx
  double dCurrentCarPos; // st7
  double dDistance; // st7
  float fReferenceCarPos; // [esp+4h] [ebp-24h]
  float fClosestDistance; // [esp+8h] [ebp-20h]
  float fAdjustedCarPos; // [esp+Ch] [ebp-1Ch]

  iClosestCarIdx = -1;                          // Initialize closest car index to -1 (no car found)
  iCarCounter = 0;                              // Initialize car counter for iteration
  fReferenceCarPos = (float)((double)(averagesectionlen * Car[iCarIdx].nCurrChunk) + Car[iCarIdx].pos.fX);// Calculate absolute track position of reference car
  if (numcars > 0)                            // Check if there are cars to iterate through
  {
    iCurrentCarIdx = 0;
    do {                                           // Skip self, dead cars, non-AI cars, and stunned cars
      if (iCarCounter != iCarIdx && (char)Car[iCurrentCarIdx].byLives > 0 && Car[iCurrentCarIdx].iControlType == 3 && !Car[iCurrentCarIdx].iStunned) {
        dCurrentCarPos = (double)(averagesectionlen * Car[iCurrentCarIdx].nCurrChunk) + Car[iCurrentCarIdx].pos.fX;// Calculate absolute track position of current car
        fAdjustedCarPos = (float)dCurrentCarPos;
        if (dCurrentCarPos < fReferenceCarPos)
          fAdjustedCarPos = (float)((double)totaltrackdistance + fAdjustedCarPos);// Handle track wrapping - add total track distance if car is behind
        dDistance = fAdjustedCarPos - fReferenceCarPos;// Calculate distance from reference car to current car
        if (dDistance < fMaxDistance)         // If this car is closer than current closest, update closest car
        {
          iClosestCarIdx = iCarCounter;
          fClosestDistance = (float)dDistance;
          fMaxDistance = fClosestDistance;
        }
      }
      ++iCarCounter;
      ++iCurrentCarIdx;
    } while (iCarCounter < numcars);
  }
  return iClosestCarIdx;                        // Return index of closest car ahead, or -1 if none found
}

//-------------------------------------------------------------------------------------------------
