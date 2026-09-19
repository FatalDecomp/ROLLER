#include "net_headless.h"
#include "net_sim_seam.h"
#include "loadtrak.h"
#include "roller.h"
#include "view.h"
#include "colision.h"
#include "function.h"
#include <time.h>
#include <string.h>
#include "3d.h"
#include "car.h"
#include "control.h"
#include "moving.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(iCondition) do { if (!(iCondition)) { \
  fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #iCondition); exit(1); \
} } while (0)

typedef struct {
  tCar aCars[MAX_CARS];
  tNetRampState aRamps[NET_MAX_RAMPS];
  tNetSimTickContext context;
} tTestMoment;

static void NetTestCapture(tTestMoment *pMoment)
{
  memset(pMoment, 0, sizeof(*pMoment));
  for (int iCar = 0; iCar < numcars; ++iCar)
    NetSimSaveCar(iCar, &pMoment->aCars[iCar]);
  NetSimSaveRamps(pMoment->aRamps);
  NetSimCaptureContext(&pMoment->context);
}
static void NetTestRestore(const tTestMoment *pMoment)
{
  CHECK(NetSimRestoreRamps(pMoment->aRamps));
  for (int iCar = 0; iCar < numcars; ++iCar)
    NetSimRestoreCar(iCar, &pMoment->aCars[iCar]);
  NetSimRestoreContext(&pMoment->context);
}

static void NetTestRollback(int iVariant)
{
  static tTestMoment initial, boundary, expected, actual, replayEnd;
  tNetInputSlot aScript[41] = {0};
  int iHumanCars = iVariant == 6 ? 2 : 1;
  human_control[0] = 1;
  human_control[1] = iHumanCars == 2;
  if (iVariant == 1) {
    game_frame = 136;
    countdown = 7;
    race_started = 0;
    Car[0].byGearAyMax = 255;
  }
  if (iVariant == 2) {
    converttoair(&Car[0]);
    Car[0].pos.fZ += 10000;
  }
  if (iVariant == 5) {
    Car[0].pos.fX = localdata[Car[0].nCurrChunk].fTrackHalfLength - 1;
  }
  if (iVariant == 7) {
    Car[0].byCarDesignIdx = 9;
    converttoair(&Car[0]);
    Car[0].pos.fZ += 10000;
    Car[0].fSpeedOverflow = 0;
    Car[0].byCheatCooldown = 0;
    g_bBrazilianMayte = true;
    SelectedView[0] = 1;
    memset(CarSpray, 0, sizeof(CarSpray));
  }
  readptr = writeptr = 509; /* Exercise wraparound and the previous input slot. */
  for (int iTick = 0; iTick <= 40; ++iTick) {
    aScript[iTick].uiTick = (uint32)iTick;
    for (int iCar = 0; iCar < iHumanCars; ++iCar) {
      aScript[iTick].aInputs[iCar].data.unInput = (uint16)((iTick % 4 == 0 ? 20 : 8) << 8);
      aScript[iTick].aInputs[iCar].data.unFlags = iTick % 9 < 5 ? BUTTON_FLAG_ACCEL :
                                                iTick % 9 < 7 ? BUTTON_FLAG_BRAKE : 0;
      if (iVariant == 3)
        aScript[iTick].aInputs[iCar].data.unFlags = BUTTON_FLAG_BRAKE;
      if (iVariant == 4 && (iTick == 4 || iTick == 6 || iTick == 18))
        aScript[iTick].aInputs[iCar].data.unFlags |= BUTTON_FLAG_UPGEAR;
      if (iVariant == 7)
        aScript[iTick].aInputs[iCar].data.unFlags |= BUTTON_FLAG_SPECIAL;
    }
  }
  memcpy(copy_multiple[508], aScript[0].aInputs, sizeof(copy_multiple[0]));
  NetTestCapture(&initial);
  for (int iTick = 1; iTick <= 40; ++iTick) {
    NetHeadlessStepInputs(aScript[iTick].aInputs, numcars);
    if (iTick == 5)
      NetTestCapture(&boundary);
    if (iTick == 25)
      NetTestCapture(&replayEnd);
  }
  NetTestCapture(&expected);
  CHECK(expected.context.iGameFrame == initial.context.iGameFrame + 40);
  CHECK(expected.context.ullRandomDraws > initial.context.ullRandomDraws);
  NetTestRestore(&initial);
  memcpy(copy_multiple[508], aScript[0].aInputs, sizeof(copy_multiple[0]));
  for (int iTick = 1; iTick <= 40; ++iTick) {
    NetHeadlessStepInputs(aScript[iTick].aInputs, numcars);
    if (iTick == 25) {
      NetTestRestore(&boundary);
      memset(copy_multiple, 0xA5, sizeof(copy_multiple));
      CHECK(NetSimRestoreInputRing(aScript + 5, 5, 21, readptr));
      net_sim_replaying = 1;
      for (int iReplay = 6; iReplay <= 25; ++iReplay)
        control_one_tick();
      net_sim_replaying = 0;
      NetTestCapture(&actual);
      CHECK(actual.context.uiRandomState == replayEnd.context.uiRandomState);
      CHECK(actual.context.ullRandomDraws == replayEnd.context.ullRandomDraws);
      for (int iCar = 0; iCar < numcars; ++iCar) {
        if (memcmp(&actual.aCars[iCar], &replayEnd.aCars[iCar], sizeof(tCar))) {
          fprintf(stderr, "variant %d car %d mismatch\n", iVariant, iCar);
          const unsigned char *pActual = (const unsigned char *)&actual.aCars[iCar];
          const unsigned char *pExpected = (const unsigned char *)&replayEnd.aCars[iCar];
          for (size_t uiByte = 0; uiByte < sizeof(tCar); ++uiByte)
            if (pActual[uiByte] != pExpected[uiByte])
              fprintf(stderr, " byte %zu: %u != %u\n", uiByte, pActual[uiByte], pExpected[uiByte]);
        }
        CHECK(!memcmp(&actual.aCars[iCar], &replayEnd.aCars[iCar], sizeof(tCar)));
      }
      CHECK(!memcmp(&actual, &replayEnd, sizeof(actual)));
    }
  }
  NetTestCapture(&actual);
  CHECK(!memcmp(&expected, &actual, sizeof(actual)));
  printf("rollback variant %d: exact cars, context, ramps and RNG (%llu draws)\n",
         iVariant, (unsigned long long)(actual.context.ullRandomDraws - initial.context.ullRandomDraws));
  NetTestRestore(&initial);
}

static tNetWorldPose g_puppetPose;
static int g_iHookCalls;
static void NetTestPuppetHook(void)
{
  CHECK(NetSimWorldToLegacy(&g_puppetPose, &Car[1]));
  ++g_iHookCalls;
}

static void NetTestRampsAndPuppets(void)
{
  tNetRampState aStates[NET_MAX_RAMPS], state;
  static tTestMoment initial;
  tNetWorldPose actual;
  tData oldGeometry;
  tCar oldPuppet;
  int iOriginalRamps = totalramps;
  CHECK(totalramps < NET_MAX_RAMPS);
  ramp[totalramps++] = initramp(100, 2, 16, 0, 1, 64, 10, 10, 1024, 63);
  CHECK(ramp[totalramps - 1]);
  NetTestCapture(&initial);
  NetSimSaveRamps(aStates);
  oldGeometry = localdata[99];
  state = aStates[totalramps - 1];
  state.nTickStartIdx = 8;
  state.nRunningTimer = 7;
  CHECK(NetSimSetRampState(totalramps - 1, &state));
  CHECK(ramp[totalramps - 1]->iRunningTimer == 7);
  CHECK(memcmp(&oldGeometry, &localdata[99], sizeof(oldGeometry)));
  for (int iTest = 0; iTest < 3; ++iTest) {
    int aiSteps[] = {1, 10, 200};
    NetTestRestore(&initial);
    state = aStates[totalramps - 1];
    CHECK(NetSimAdvanceRampStateCopy(totalramps - 1, &state, aiSteps[iTest]));
    for (int iTick = 0; iTick < aiSteps[iTest]; ++iTick)
      updateramp(ramp[totalramps - 1]);
    CHECK(state.nTickStartIdx == ramp[totalramps - 1]->iTickStartIdx);
    CHECK(state.nTimingGroup2 == ramp[totalramps - 1]->iTimingGroup2);
    CHECK(state.nRunningTimer == ramp[totalramps - 1]->iRunningTimer);
  }
  NetTestRestore(&initial);
  net_sim_authority = NET_AUTHORITY_REMOTE;
  for (int iCar = 0; iCar < numcars; ++iCar)
    NetSimSetPuppet(iCar, iCar != 0);
  human_control[0] = 1;
  Car[1].nCurrChunk = Car[1].nReferenceChunk = Car[1].iLastValidChunk = 99;
  Car[1].pos.fX = Car[1].pos.fY = Car[1].pos.fZ = 0;
  CHECK(NetSimLegacyToWorld(&Car[1], &g_puppetPose));
  net_sim_puppet_hook = NetTestPuppetHook;
  g_iHookCalls = 0;
  for (int iTick = 0; iTick < 200; ++iTick) {
    NetHeadlessStep();
    CHECK(NetSimLegacyToWorld(&Car[1], &actual));
    CHECK(fabsf(actual.position.fX - g_puppetPose.position.fX) < 0.05f);
    CHECK(fabsf(actual.position.fY - g_puppetPose.position.fY) < 0.05f);
    CHECK(fabsf(actual.position.fZ - g_puppetPose.position.fZ) < 0.05f);
  }
  CHECK(g_iHookCalls == 200);
  oldPuppet = Car[0];
  NetSimSetPuppet(0, 1);
  NetHeadlessStep();
  CHECK(!memcmp(&oldPuppet, &Car[0], sizeof(oldPuppet)));
  net_sim_puppet_hook = NULL;
  NetTestRestore(&initial);
  /* Host-owned counters and track mutations stay unchanged under prediction. */
  NetSimSetPuppet(0, 0);
  Car[0].byCarDesignIdx = 12;
  tCopyData aInputs[MAX_CARS] = {0};
  aInputs[0].data.unFlags = BUTTON_FLAG_SPECIAL | BUTTON_FLAG_ACCEL;
  static int aColours[MAX_TRACK_CHUNKS][6];
  memcpy(aColours, TrakColour, sizeof(aColours));
  int iLap = Car[0].byLap, iFinishers = human_finishers;
  float fTime = Car[0].fRunningLapTime;
  race_started = -1;
  for (int iTick = 0; iTick < 200; ++iTick) {
    Car[0].nCurrChunk = 0;
    Car[0].nReferenceChunk = TRAK_LEN - 1;
    NetHeadlessStepInputs(aInputs, numcars);
  }
  CHECK(Car[0].byLap == iLap && human_finishers == iFinishers);
  CHECK(Car[0].fRunningLapTime > fTime);
  CHECK(!memcmp(aColours, TrakColour, sizeof(aColours)));
  CHECK(Car[0].byCheatAmmo == initial.aCars[0].byCheatAmmo);
  /* Collision response moves the predicting car and cannot write a puppet. */
  NetTestRestore(&initial);
  Car[1] = Car[0];
  Car[1].iDriverIdx = 1;
  Car[1].pos.fX += CarBaseX;
  Car[1].fFinalSpeed = 0;
  oldPuppet = Car[1];
  initcollisions();
  testcoll(&Car[0], &Car[1], 0);
  CHECK(!memcmp(&oldPuppet, &Car[1], sizeof(oldPuppet)));
  /* Measure the real simulation, one human and fifteen puppets. */
  clock_t tickStart = clock();
  for (int iTick = 0; iTick < 1000; ++iTick)
    NetHeadlessStepInputs(aInputs, numcars);
  printf("one human + 15 puppets: %.6f ms/tick\n", 1000.0 * (clock() - tickStart) / CLOCKS_PER_SEC / 1000);
  NetTestRestore(&initial);
  net_sim_authority = NET_AUTHORITY_LOCAL;
  memset(net_puppet_car, 0, sizeof(net_puppet_car));
  fre((void **)&ramp[totalramps - 1]->chunkDataAy);
  fre((void **)&ramp[totalramps - 1]);
  totalramps = iOriginalRamps;
  puts("NET-E0 ramp, puppet and authority tests passed");
}

int main(int iArgc, char **ppArgv)
{
  char szError[512];
  tCar aBefore[16];
  int iAdvanced = 0;
  CHECK(iArgc == 3);
  if (!NetHeadlessInit(ppArgv[1], ppArgv[2], 16, 12345, szError, sizeof(szError))) {
    fprintf(stderr, "%s\n", szError);
    return 1;
  }
  for (int iTick = 0; iTick < 200 && game_frame <= 145; ++iTick)
    NetHeadlessStep();
  CHECK(game_frame > 145);
  CHECK(start_race && race_started && countdown < 0);
  for (int iCar = 0; iCar < numcars; ++iCar)
    aBefore[iCar] = Car[iCar];
  for (int iTick = 0; iTick < 100; ++iTick)
    NetHeadlessStep();
  for (int iCar = 0; iCar < numcars; ++iCar) {
    printf("car %d: chunk %d -> %d, speed %.3f\n", iCar,
           aBefore[iCar].iLastValidChunk, Car[iCar].iLastValidChunk, Car[iCar].fFinalSpeed);
    CHECK(isfinite(Car[iCar].fFinalSpeed) && Car[iCar].fFinalSpeed > 0);
    CHECK(Car[iCar].iLastValidChunk != aBefore[iCar].iLastValidChunk ||
          fabsf(Car[iCar].pos.fX - aBefore[iCar].pos.fX) > Car[iCar].fFinalSpeed);
    iAdvanced |= Car[iCar].iLastValidChunk != aBefore[iCar].iLastValidChunk;
  }
  CHECK(iAdvanced);
  puts("NET-E0-S8 headless stepping passed");
  static tTestMoment running;
  NetTestCapture(&running);
  for (int iVariant = 0; iVariant < 8; ++iVariant) {
    NetTestRestore(&running);
    NetTestRollback(iVariant);
  }
  NetTestRestore(&running);
  NetTestRampsAndPuppets();
  return 0;
}
