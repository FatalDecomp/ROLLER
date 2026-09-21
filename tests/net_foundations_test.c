#include "net_headless.h"
#include "net_sim_seam.h"
#include "net_snapshot.h"
#include "replay.h"
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
#include "engines.h"
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

static int NetAngleDifference(int iA, int iB);

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
    cheat_control = -1;
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

static void NetTestDoubleRun(void)
{
  static tTestMoment initial, first, second;
  tCopyData aInputs[MAX_CARS] = {0};
  human_control[0] = 1;
  aInputs[0].data.unInput = 11 << 8;
  aInputs[0].data.unFlags = BUTTON_FLAG_ACCEL;
  memcpy(copy_multiple[(readptr - 1) & 511], aInputs, sizeof(aInputs));
  NetTestCapture(&initial);
  for (int iTick = 0; iTick < 20; ++iTick)
    NetHeadlessStepInputs(aInputs, numcars);
  NetTestCapture(&first);
  NetTestRestore(&initial);
  memcpy(copy_multiple[(readptr - 1) & 511], aInputs, sizeof(aInputs));
  for (int iTick = 0; iTick < 20; ++iTick)
    NetHeadlessStepInputs(aInputs, numcars);
  NetTestCapture(&second);
  CHECK(!memcmp(&first, &second, sizeof(first)));
  NetTestRestore(&initial);
  puts("20-tick double run: byte-exact cars, ramps, context and RNG");
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
  tStuntData twin;
  int iOriginalRamps = totalramps;
  int iRampA = totalramps;
  CHECK(totalramps < NET_MAX_RAMPS);
  ramp[totalramps++] = initramp(100, 2, 16, 0, 1, 64, 10, 10, 1024, 63);
  CHECK(ramp[iRampA]);
  NetTestCapture(&initial);
  NetSimSaveRamps(aStates);
  oldGeometry = localdata[99];
  state = aStates[iRampA];
  state.nTickStartIdx = 8;
  state.nRunningTimer = 7;
  CHECK(NetSimSetRampState(iRampA, &state));
  CHECK(ramp[iRampA]->iRunningTimer == 7);
  CHECK(memcmp(&oldGeometry, &localdata[99], sizeof(oldGeometry)));
  for (int iTest = 0; iTest < 3; ++iTest) {
    int aiSteps[] = {1, 10, 200};
    NetTestRestore(&initial);
    state = aStates[iRampA];
    CHECK(NetSimAdvanceRampStateCopy(iRampA, &state, aiSteps[iTest]));
    for (int iTick = 0; iTick < aiSteps[iTest]; ++iTick)
      updateramp(ramp[iRampA]);
    CHECK(state.nTickStartIdx == ramp[iRampA]->iTickStartIdx);
    CHECK(state.nTimingGroup2 == ramp[iRampA]->iTimingGroup2);
    CHECK(state.nRunningTimer == ramp[iRampA]->iRunningTimer);
  }
  NetTestRestore(&initial);
  twin = *ramp[iRampA];
  for (int iTick = 0; iTick < 200; ++iTick) {
    updateramp(ramp[iRampA]);
    updateramp(&twin);
    CHECK(ramp[iRampA]->iTickStartIdx == twin.iTickStartIdx);
    CHECK(ramp[iRampA]->iTimingGroup2 == twin.iTimingGroup2);
    CHECK(ramp[iRampA]->iRunningTimer == twin.iRunningTimer);
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
  state = aStates[iRampA];
  for (int iTick = 0; iTick < 200; ++iTick) {
    CHECK(NetSimAdvanceRampStateCopy(iRampA, &state, 1));
    NetHeadlessStep();
    CHECK(state.nTickStartIdx == ramp[iRampA]->iTickStartIdx);
    CHECK(state.nTimingGroup2 == ramp[iRampA]->iTimingGroup2);
    CHECK(state.nRunningTimer == ramp[iRampA]->iRunningTimer);
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
  cheat_control = -1;
  tCopyData aInputs[MAX_CARS] = {0};
  aInputs[0].data.unFlags = BUTTON_FLAG_SPECIAL | BUTTON_FLAG_ACCEL;
  static int aColours[MAX_TRACK_CHUNKS][6];
  memcpy(aColours, TrakColour, sizeof(aColours));
  int iLap = Car[0].byLap, iFinishers = human_finishers;
  int iFinishedCar = finished_car[0], iAllFinishers = finishers;
  float fTime = Car[0].fRunningLapTime;
  race_started = -1;
  for (int iCrossing = 0; iCrossing < 3; ++iCrossing) {
    Car[0].nCurrChunk = 0;
    Car[0].nReferenceChunk = TRAK_LEN - 1;
    NetHeadlessStepInputs(aInputs, numcars);
    CHECK(Car[0].byLap == iLap && finished_car[0] == iFinishedCar &&
          human_finishers == iFinishers && finishers == iAllFinishers);
  }
  for (int iTick = 3; iTick < 200; ++iTick)
    NetHeadlessStepInputs(aInputs, numcars);
  CHECK(Car[0].byLap == iLap && human_finishers == iFinishers);
  CHECK(finished_car[0] == iFinishedCar && finishers == iAllFinishers);
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
  for (int iRamp = totalramps - 1; iRamp >= iOriginalRamps; --iRamp) {
    fre((void **)&ramp[iRamp]->chunkDataAy);
    fre((void **)&ramp[iRamp]);
  }
  totalramps = iOriginalRamps;
  puts("NET-E0 ramp, puppet and authority tests passed");
}

static void NetTestFramesAndRanges(void)
{
  static tTestMoment initial;
  tNetCarFullState full;
  tNetCarState groundedA, groundedB, airborneA, airborneB, interpolated;
  tNetWorldPose world, roundTrip, targetWorld;
  tCar car;
  int iBanked = -1;
  NetTestCapture(&initial);
  for (int iChunk = 0; iChunk < TRAK_LEN; ++iChunk)
    if (localdata[iChunk].iRoll || localdata[iChunk].iBankDelta) {
      iBanked = iChunk;
      break;
    }
  CHECK(iBanked >= 0);

  car = Car[0];
  car.nCurrChunk = car.nReferenceChunk = (int16)iBanked;
  car.pos.fX = -localdata[iBanked].fTrackHalfLength * 0.4f;
  car.pos.fY = localdata[iBanked].fTrackHalfWidth * 0.25f;
  car.pos.fZ = 12.0f;
  car.nYaw = 1200; car.nPitch = 300; car.nRoll = 500; car.nActualYaw = 1250;
  CHECK(NetSimLegacyToWorld(&car, &world));
  tCar converted = car;
  memset(&converted.pos, 0, sizeof(converted.pos));
  converted.nYaw = converted.nPitch = converted.nRoll = converted.nActualYaw = 0;
  CHECK(NetSimWorldToLegacy(&world, &converted));
  CHECK(NetSimLegacyToWorld(&converted, &roundTrip));
  CHECK(fabsf(world.position.fX - roundTrip.position.fX) < 0.01f);
  CHECK(fabsf(world.position.fY - roundTrip.position.fY) < 0.01f);
  CHECK(fabsf(world.position.fZ - roundTrip.position.fZ) < 0.01f);
  /* The legacy integer angle transforms can round by two of 16384 units. */
  CHECK(NetAngleDifference(world.nYaw, roundTrip.nYaw) <= 2);
  CHECK(NetAngleDifference(world.nPitch, roundTrip.nPitch) <= 2);
  CHECK(NetAngleDifference(world.nRoll, roundTrip.nRoll) <= 2);

  car.nCurrChunk = car.nReferenceChunk = -1;
  car.pos.fX = 1234.5f; car.pos.fY = -456.25f; car.pos.fZ = 789.75f;
  CHECK(NetSimLegacyToWorld(&car, &world));
  converted = car;
  memset(&converted.pos, 0, sizeof(converted.pos));
  CHECK(NetSimWorldToLegacy(&world, &converted));
  CHECK(!memcmp(&car.pos, &converted.pos, sizeof(car.pos)));
  CHECK(car.nYaw == converted.nYaw && car.nPitch == converted.nPitch &&
        car.nRoll == converted.nRoll && car.nActualYaw == converted.nActualYaw);

  int iSource = iBanked;
  int iTarget = (iSource + 2) % TRAK_LEN;
  tCar target = Car[0];
  target.nCurrChunk = target.nReferenceChunk = (int16)iTarget;
  target.pos.fX = target.pos.fY = target.pos.fZ = 0.0f;
  target.nYaw = 777; target.nPitch = 0; target.nRoll = 0; target.nActualYaw = 800;
  CHECK(NetSimLegacyToWorld(&target, &targetWorld));
  car = target;
  car.nCurrChunk = car.nReferenceChunk = (int16)iSource;
  CHECK(NetSimWorldToLegacy(&targetWorld, &car));
  NetSimRehomeChunk(&car);
  CHECK(car.nCurrChunk == iTarget);
  CHECK(NetSimLegacyToWorld(&car, &roundTrip));
  CHECK(fabsf(targetWorld.position.fX - roundTrip.position.fX) < 0.05f);
  CHECK(fabsf(targetWorld.position.fY - roundTrip.position.fY) < 0.05f);
  CHECK(fabsf(targetWorld.position.fZ - roundTrip.position.fZ) < 0.05f);
  CHECK(NetAngleDifference(targetWorld.nYaw, roundTrip.nYaw) <= 2);

  NetTestRestore(&initial);
  human_control[0] = 1;
  Car[0].fFinalSpeed = 277.0f;
  CHECK(NetSnapshotEncodeCarFull(0, &full));
  memset(&Car[0], 0, sizeof(Car[0]));
  CHECK(NetSnapshotDecodeCarFull(0, &full));
  CHECK(Car[0].fFinalSpeed == 277.0f);
  NetTestRestore(&initial);
  Car[0].fFinalSpeed = -123.0f;
  CHECK(NetSnapshotEncodeCarFull(0, &full));
  memset(&Car[0], 0, sizeof(Car[0]));
  CHECK(NetSnapshotDecodeCarFull(0, &full));
  CHECK(Car[0].fFinalSpeed == -123.0f);
  NetTestRestore(&initial);
  converttoair(&Car[0]);
  Car[0].pos.fZ += 5000.0f;
  Car[0].direction.fZ = 42.0f;
  CHECK(NetSnapshotEncodeCarFull(0, &full));
  memset(&Car[0], 0, sizeof(Car[0]));
  CHECK(NetSnapshotDecodeCarFull(0, &full));
  CHECK(Car[0].nCurrChunk == -1 && Car[0].direction.fZ == 42.0f);
  NetTestRestore(&initial);
  Car[0].byCarDesignIdx = 9;
  Car[0].fFinalSpeed = 300.0f;
  Car[0].fSpeedOverflow = 23.5f;
  Car[0].byCheatCooldown = 36;
  Car[0].byEngineStartTimer = 12;
  CHECK(NetSnapshotEncodeCarFull(0, &full));
  memset(&Car[0], 0, sizeof(Car[0]));
  CHECK(NetSnapshotDecodeCarFull(0, &full));
  CHECK(Car[0].fFinalSpeed == 300.0f && Car[0].fSpeedOverflow == 23.5f &&
        Car[0].byCheatCooldown == 36 && Car[0].byEngineStartTimer == 12);

  NetTestRestore(&initial);
  Car[0] = car = initial.aCars[0];
  car.nCurrChunk = car.nReferenceChunk = (int16)iBanked;
  car.pos.fX = -localdata[iBanked].fTrackHalfLength * 0.5f;
  Car[0] = car; CHECK(NetSnapshotEncodeCarFull(0, &full)); groundedA = full.state;
  car.pos.fX = localdata[iBanked].fTrackHalfLength * 0.5f;
  Car[0] = car; CHECK(NetSnapshotEncodeCarFull(0, &full)); groundedB = full.state;
  CHECK(NetSnapshotInterpolate(&groundedA, &groundedB, 0.5f, &interpolated));
  CHECK(interpolated.nCurrChunk == groundedB.nCurrChunk);
  CHECK(fabsf(interpolated.fWorldPosX - (groundedA.fWorldPosX + groundedB.fWorldPosX) * 0.5f) < 0.01f);
  car.nCurrChunk = car.nReferenceChunk = -1;
  car.pos.fX = 10; car.pos.fY = 20; car.pos.fZ = 30;
  Car[0] = car; CHECK(NetSnapshotEncodeCarFull(0, &full)); airborneA = full.state;
  car.pos.fX = 110; car.pos.fY = 220; car.pos.fZ = 330;
  Car[0] = car; CHECK(NetSnapshotEncodeCarFull(0, &full)); airborneB = full.state;
  CHECK(NetSnapshotInterpolate(&airborneA, &airborneB, 0.5f, &interpolated));
  CHECK(interpolated.nCurrChunk == -1 && interpolated.fWorldPosX == 60.0f &&
        interpolated.fWorldPosY == 120.0f && interpolated.fWorldPosZ == 180.0f);
  CHECK(NetSnapshotInterpolate(&groundedB, &airborneA, 0.5f, &interpolated));
  CHECK(interpolated.nCurrChunk == -1);
  NetTestRestore(&initial);
  puts("NET-E0 frame conversion, re-home, range and interpolation scenarios passed");
}

static void NetTestSnapshots(void)
{
  static tTestMoment initial;
  static int aColours[MAX_TRACK_CHUNKS][6];
  static int aiGrip[MAX_TRACK_CHUNKS][3];
  tNetSnapshot base, current, decoded, older;
  uint8 abBytes[NET_MAX_PAYLOAD];
  tCopyData aInputs[MAX_CARS] = {0};
  int iLength, iSawRampMove = 0, iSawLovebun = 0, iSawAirborne = 0;
  NetTestCapture(&initial);
  memcpy(aColours, TrakColour, sizeof(aColours));
  for (int iChunk = 0; iChunk < MAX_TRACK_CHUNKS; ++iChunk) {
    aiGrip[iChunk][0] = localdata[iChunk].iCenterGrip;
    aiGrip[iChunk][1] = localdata[iChunk].iLeftShoulderGrip;
    aiGrip[iChunk][2] = localdata[iChunk].iRightShoulderGrip;
  }
  human_control[0] = 1;
  cheat_control = -1;
  Car[0].byCarDesignIdx = 12;
  Car[0].byCheatAmmo = 5;
  Car[0].byCheatCooldown = 0;
  aInputs[0].data.unFlags = BUTTON_FLAG_ACCEL;
  CHECK(NetSnapshotBuild(&base, 0, 42, 1, 0));
  if (!base.byNumRamps)
    base.byNumRamps = 1;
  older = base;
  for (int iTick = 1; iTick <= 100; ++iTick) {
    aInputs[0].data.unFlags = BUTTON_FLAG_ACCEL | (iTick == 15 ? BUTTON_FLAG_SPECIAL : 0);
    if (iTick == 30) {
      converttoair(&Car[0]);
      Car[0].pos.fZ += 10000.0f;
      Car[0].direction.fZ = 40.0f;
    }
    NetHeadlessStepInputs(aInputs, numcars);
    CHECK(NetSnapshotBuild(&current, (uint32)iTick, (uint32)iTick, 1, 0));
    if (!current.byNumRamps) {
      current.byNumRamps = 1;
      current.aRamps[0].nTickStartIdx = (int16)(iTick % 16);
      current.aRamps[0].nTimingGroup2 = (iTick / 16) & 1 ? -1 : 1;
      current.aRamps[0].nRunningTimer = (int16)(iTick % 10);
    }
    iSawRampMove |= memcmp(current.aRamps, base.aRamps,
                           current.byNumRamps * sizeof(current.aRamps[0])) != 0;
    iSawLovebun |= memcmp(aColours, TrakColour, sizeof(aColours)) != 0;
    iSawAirborne |= current.aCars[0].nCurrChunk == -1;
    CHECK(NetSnapshotEncode(&current, abBytes, sizeof(abBytes)) == sizeof(current));
    CHECK(NetSnapshotDecode(abBytes, sizeof(current), &decoded));
    CHECK(!memcmp(&current, &decoded, sizeof(current)));
    iLength = NetSnapshotEncodeDelta(&base, &current, abBytes, sizeof(abBytes));
    CHECK(iLength > 0);
    CHECK(NetSnapshotDecodeDelta(&base, abBytes, iLength, &decoded));
    CHECK(!memcmp(&current, &decoded, sizeof(current)));
    CHECK(!NetSnapshotDecodeDelta(&base, abBytes, iLength - 1, &decoded));
    iLength = NetSnapshotEncodeDelta(&older, &current, abBytes, sizeof(abBytes));
    CHECK(iLength > 0 && NetSnapshotDecodeDelta(&older, abBytes, iLength, &decoded));
    CHECK(!memcmp(&current, &decoded, sizeof(current)));
    base = current;
  }
  CHECK(iSawRampMove && iSawLovebun && iSawAirborne);
  for (int iField = 0; iField < 31; ++iField) {
    current = base;
    ++current.uiTick;
    switch (iField) {
      case 0: ++current.aCars[0].fWorldPosX; break;
      case 1: ++current.aCars[0].fWorldPosY; break;
      case 2: ++current.aCars[0].fWorldPosZ; break;
      case 3: ++current.aCars[0].fFinalSpeed; break;
      case 4: ++current.aCars[0].fHorizontalSpeed; break;
      case 5: ++current.aCars[0].fVelX; break;
      case 6: ++current.aCars[0].fVelY; break;
      case 7: ++current.aCars[0].fVelZ; break;
      case 8: ++current.aCars[0].nCurrChunk; break;
      case 9: ++current.aCars[0].nReferenceChunk; break;
      case 10: ++current.aCars[0].nLastValidChunk; break;
      case 11: current.aCars[0].nWorldRoll = (base.aCars[0].nWorldRoll + 1) & 16383; break;
      case 12: current.aCars[0].nWorldPitch = (base.aCars[0].nWorldPitch + 1) & 16383; break;
      case 13: current.aCars[0].nWorldYaw = (base.aCars[0].nWorldYaw + 1) & 16383; break;
      case 14: current.aCars[0].nActualYaw = (base.aCars[0].nActualYaw + 1) & 16383; break;
      case 15: ++current.aCars[0].nDeathTimer; break;
      case 16: ++current.aCars[0].nJumpMomentum; break;
      case 17: current.aCars[0].byHealth = 99; break;
      case 18: ++current.aCars[0].byLives; break;
      case 19: ++current.aCars[0].byLap; break;
      case 20: ++current.aCars[0].byRacePosition; break;
      case 21: ++current.aCars[0].byStatusFlags; break;
      case 22: ++current.aCars[0].byStunned; break;
      case 23: ++current.aCars[0].byDamageIntensity; break;
      case 24: ++current.aCars[0].byDamageState; break;
      case 25: ++current.aCars[0].byWheelAnimationFrame; break;
      case 26: ++current.aCars[0].byGearAyMax; break;
      case 27: current.aCars[0].byHumanControl = 2; break;
      case 28: current.aCars[0].byControlType = 1; break;
      case 29: ++current.aCars[0].byCheatAmmo; break;
      case 30: ++current.aCars[0].byPad; break;
    }
    current.uiRandomState ^= 0x87654321u;
    ++current.context.iGameFrame;
    iLength = NetSnapshotEncodeDelta(&base, &current, abBytes, sizeof(abBytes));
    CHECK(iLength > 0 && NetSnapshotDecodeDelta(&base, abBytes, iLength, &decoded));
    CHECK(!memcmp(&current, &decoded, sizeof(current)));
  }
  tNetCarState interpolated, a = base.aCars[0], b = base.aCars[0];
  a.nWorldYaw = 16380; b.nWorldYaw = 4;
  a.fWorldPosX = 0; b.fWorldPosX = 100;
  b.nCurrChunk = -1;
  CHECK(NetSnapshotInterpolate(&a, &b, 0.5f, &interpolated));
  CHECK(interpolated.nWorldYaw == 0 && interpolated.fWorldPosX == 50 && interpolated.nCurrChunk == -1);
  {
    tCar installed;
    tNetWorldPose pose;
    int iInstalledControl;
    CHECK(NetSnapshotApplyPuppet(1, &interpolated));
    CHECK(NetSimLegacyToWorld(&Car[1], &pose));
    CHECK(pose.position.fX == 50.0f && pose.nYaw == 0);
    installed = Car[1];
    iInstalledControl = human_control[1];
    interpolated.nCurrChunk = TRAK_LEN;
    CHECK(!NetSnapshotApplyPuppet(1, &interpolated));
    CHECK(!memcmp(&Car[1], &installed, sizeof(installed)));
    CHECK(human_control[1] == iInstalledControl);
  }
  current = base;
  current.byNumCars = 17;
  CHECK(!NetSnapshotEncode(&current, abBytes, sizeof(abBytes)));
  current = base;
  current.aCars[0].fFinalSpeed = NAN;
  CHECK(!NetSnapshotEncode(&current, abBytes, sizeof(abBytes)));
  memcpy(TrakColour, aColours, sizeof(aColours));
  for (int iChunk = 0; iChunk < MAX_TRACK_CHUNKS; ++iChunk) {
    localdata[iChunk].iCenterGrip = aiGrip[iChunk][0];
    localdata[iChunk].iLeftShoulderGrip = aiGrip[iChunk][1];
    localdata[iChunk].iRightShoulderGrip = aiGrip[iChunk][2];
  }
  NetTestRestore(&initial);
  puts("NET-E0 snapshot full/delta codecs and interpolation passed");
}

static void NetTestReplayOutput(void)
{
  static tTestMoment initial, live, replayed;
  tCopyData aInputs[MAX_CARS] = {0};
  int iSoundOn = soundon;
  human_control[0] = 1;
  aInputs[0].data.unFlags = BUTTON_FLAG_ACCEL;
  soundon = 1;
  memset(Pending, 0, sizeof(Pending));
  memset(SamplePending, 0, sizeof(SamplePending));
  memset(speechinfo, 0, sizeof(speechinfo));
  readsample = writesample = 0;
  NetTestCapture(&initial);
  replayfile = tmpfile();
  CHECK(replayfile);
  replaytype = 1;
  for (int iTick = 0; iTick < 20; ++iTick)
    NetHeadlessStepInputs(aInputs, numcars);
  NetTestCapture(&live);
  CHECK(ftell(replayfile) > 0);
  long lRecordedBytes = ftell(replayfile);
  NetTestRestore(&initial);
  memset(Pending, 0, sizeof(Pending));
  memset(SamplePending, 0, sizeof(SamplePending));
  net_sim_replaying = 1;
  for (int iTick = 0; iTick < 20; ++iTick)
    NetHeadlessStepInputs(aInputs, numcars);
  net_sim_replaying = 0;
  NetTestCapture(&replayed);
  CHECK(ftell(replayfile) == lRecordedBytes);
  CHECK(!memcmp(&live, &replayed, sizeof(live)));
  for (int iCar = 0; iCar < MAX_CARS; ++iCar)
    CHECK(Pending[iCar] == 0);
  CHECK(readsample == initial.context.iReadsample);
  CHECK(writesample == initial.context.iWritesample);
  CHECK(!memcmp(speechinfo, initial.context.aSpeech, sizeof(speechinfo)));
  fclose(replayfile);
  replayfile = NULL;
  replaytype = 0;
  soundon = iSoundOn;
  NetTestRestore(&initial);
  puts("20-tick replay: no file or sound queues, exact car/context/RNG equality");
}

typedef struct {
  tCar car;
  uint32 uiRandomState;
  uint64 ullRandomDraws;
} tWireReference;

static int NetAngleDifference(int iA, int iB)
{
  int iDifference = abs((iA - iB) & 16383);
  return iDifference > 8192 ? 16384 - iDifference : iDifference;
}

static int NetMovementWithin(const tCar *pExpected, const tCar *pActual,
                             const char *szScenario, int iTick)
{
  tNetWorldPose expectedPose, actualPose;
  int iOkay = NetSimLegacyToWorld(pExpected, &expectedPose) &&
              NetSimLegacyToWorld(pActual, &actualPose);
#define NEAR_FIELD(field, tolerance) do { \
  if (fabsf(pExpected->field - pActual->field) > (tolerance)) { \
    fprintf(stderr, "%s tick %d movement %s %.9g != %.9g\n", szScenario, iTick, \
            #field, pExpected->field, pActual->field); iOkay = 0; \
  } \
} while (0)
#define EXACT_FIELD(field) do { \
  if (pExpected->field != pActual->field) { \
    fprintf(stderr, "%s tick %d movement %s %d != %d\n", szScenario, iTick, \
            #field, (int)pExpected->field, (int)pActual->field); iOkay = 0; \
  } \
} while (0)
  if (iOkay) {
    float fDx = expectedPose.position.fX - actualPose.position.fX;
    float fDy = expectedPose.position.fY - actualPose.position.fY;
    float fDz = expectedPose.position.fZ - actualPose.position.fZ;
    float fDistance = sqrtf(fDx * fDx + fDy * fDy + fDz * fDz);
    if (fDistance > 0.5f) {
      fprintf(stderr, "%s tick %d movement world-position error %.9g > 0.5\n",
              szScenario, iTick, fDistance);
      iOkay = 0;
    }
    if (NetAngleDifference(expectedPose.nYaw, actualPose.nYaw) > 91 ||
        NetAngleDifference(expectedPose.nPitch, actualPose.nPitch) > 91 ||
        NetAngleDifference(expectedPose.nRoll, actualPose.nRoll) > 91 ||
        NetAngleDifference(expectedPose.nActualYaw, actualPose.nActualYaw) > 91) {
      fprintf(stderr, "%s tick %d movement world-angle error exceeds 2 degrees\n",
              szScenario, iTick);
      iOkay = 0;
    }
  }
  NEAR_FIELD(fFinalSpeed, 1.0f);
  NEAR_FIELD(fHorizontalSpeed, 1.0f);
  NEAR_FIELD(direction.fX, 1.0f);
  NEAR_FIELD(direction.fY, 1.0f);
  NEAR_FIELD(direction.fZ, 1.0f);
  NEAR_FIELD(fBaseSpeed, 1.0f);
  NEAR_FIELD(fSpeedOverflow, 1.0f);
  NEAR_FIELD(fRPMRatio, 1.0f);
  NEAR_FIELD(fPower, 1.0f);
  NEAR_FIELD(fHealth, 0.0f);
  EXACT_FIELD(nCurrChunk);
  EXACT_FIELD(nReferenceChunk);
  EXACT_FIELD(iLastValidChunk);
  EXACT_FIELD(iJumpMomentum);
  EXACT_FIELD(iControlType);
  EXACT_FIELD(iSteeringInput);
  EXACT_FIELD(iBankingSteerOffset);
  EXACT_FIELD(iRollMomentum);
  EXACT_FIELD(iRollMotion);
  EXACT_FIELD(iPitchMotion);
  EXACT_FIELD(iYawMotion);
  EXACT_FIELD(nTargetChunk);
  EXACT_FIELD(byGearAyMax);
  EXACT_FIELD(iEngineState);
  EXACT_FIELD(byThrottlePressed);
  EXACT_FIELD(byAccelerating);
  EXACT_FIELD(byCollisionTimer);
#undef EXACT_FIELD
#undef NEAR_FIELD
  return iOkay;
}

static int NetTestFullStateScenario(const tTestMoment *pRunning, int iScenario)
{
  static const char *aszScenario[] = {"speed-250", "mid-jump", "mid-braking", "mid-gear-change",
                                      "health-11.5"};
  static tTestMoment initial;
  tWireReference aReference[NET_MAX_REPLAY_TICKS];
  tNetCarFullState wire;
  tCopyData aaInputs[NET_MAX_REPLAY_TICKS][MAX_CARS] = {{{0}}};
  tCopyData aPrevious[MAX_CARS] = {{0}};
  int iMovementOkay = 1, iRngOkay = 1;
  NetTestRestore(pRunning);
  human_control[0] = 1;
  SetEngine(&Car[0], 250.0f);
  Car[0].fFinalSpeed = 250.0f;
  for (int iTick = 0; iTick < NET_MAX_REPLAY_TICKS; ++iTick) {
    aaInputs[iTick][0].data.unInput = (uint16)((10 + iTick % 5) << 8);
    aaInputs[iTick][0].data.unFlags = BUTTON_FLAG_ACCEL;
  }
  aPrevious[0] = aaInputs[0][0];
  if (iScenario == 1) {
    converttoair(&Car[0]);
    Car[0].pos.fZ += 10000.0f;
    Car[0].direction.fZ = 35.0f;
  } else if (iScenario == 2) {
    Car[0].byThrottlePressed = Car[0].byAccelerating = 1;
    for (int iTick = 0; iTick < NET_MAX_REPLAY_TICKS; ++iTick)
      aaInputs[iTick][0].data.unFlags = BUTTON_FLAG_BRAKE;
  } else if (iScenario == 3) {
    aaInputs[0][0].data.unFlags |= BUTTON_FLAG_UPGEAR;
    aaInputs[6][0].data.unFlags |= BUTTON_FLAG_UPGEAR;
  } else if (iScenario == 4) {
    /* Fractional health feeds the health factor in every speed update. */
    Car[0].fHealth = 11.5f;
  }
  memcpy(copy_multiple[(readptr - 1) & 511], aPrevious, sizeof(aPrevious));
  NetTestCapture(&initial);
  CHECK(NetSnapshotEncodeCarFull(0, &wire));
  for (int iTick = 0; iTick < NET_MAX_REPLAY_TICKS; ++iTick) {
    NetHeadlessStepInputs(aaInputs[iTick], numcars);
    aReference[iTick].car = Car[0];
    aReference[iTick].uiRandomState = ROLLERrandStateGet();
    aReference[iTick].ullRandomDraws = ROLLERrandDrawCountGet();
  }
  NetTestRestore(&initial);
  memcpy(copy_multiple[(readptr - 1) & 511], aPrevious, sizeof(aPrevious));
  memset(&Car[0], 0, sizeof(Car[0]));
  CHECK(NetSnapshotDecodeCarFull(0, &wire));
  for (int iTick = 0; iTick < NET_MAX_REPLAY_TICKS; ++iTick) {
    NetHeadlessStepInputs(aaInputs[iTick], numcars);
    iMovementOkay &= NetMovementWithin(&aReference[iTick].car, &Car[0],
                                       aszScenario[iScenario], iTick + 1);
    if (ROLLERrandStateGet() != aReference[iTick].uiRandomState ||
        ROLLERrandDrawCountGet() != aReference[iTick].ullRandomDraws) {
      fprintf(stderr, "%s tick %d RNG %u/%llu != %u/%llu\n", aszScenario[iScenario], iTick + 1,
              aReference[iTick].uiRandomState, (unsigned long long)aReference[iTick].ullRandomDraws,
              ROLLERrandStateGet(), (unsigned long long)ROLLERrandDrawCountGet());
      iRngOkay = 0;
    }
  }
  printf("wire coherence %-15s movement=%s RNG=%s (%d ticks)\n", aszScenario[iScenario],
         iMovementOkay ? "pass" : "FAIL", iRngOkay ? "pass" : "FAIL", NET_MAX_REPLAY_TICKS);
  return iMovementOkay && iRngOkay;
}

static int NetTestFullStateCoherence(void)
{
  static tTestMoment running;
  int iOkay = 1;
  NetTestCapture(&running);
  for (int iScenario = 0; iScenario < 5; ++iScenario)
    iOkay &= NetTestFullStateScenario(&running, iScenario);
  NetTestRestore(&running);
  return iOkay ? 0 : 1;
}

/* NET-FIX-2: health crosses the full-state wire exactly.  At health 11.5 the
   start gate compares 11.035 against the draw; a quantised 11.0 compares 10.99,
   so the draw iTemp == 11 starts the engine on one side and false-starts on the
   other. */
static void NetTestHealthExact(const tTestMoment *pRunning)
{
  static tTestMoment initial;
  tNetCarFullState wire;
  tCopyData aInputs[MAX_CARS] = {0};
  int iFalseStarts = false_starts;
  int iOriginalTimer, iRestoredTimer, iFound = 0;
  uint32 uiSeed;

  NetTestRestore(pRunning);
  false_starts = 1;
  human_control[0] = 1;
  aInputs[0].data.unFlags = BUTTON_FLAG_ACCEL;
  Car[0].fHealth = 11.5f;
  Car[0].iControlType = 3;
  Car[0].byThrottlePressed = 0;
  Car[0].byEngineStartTimer = 0;
  Car[0].byAccelerating = 1;
  memcpy(copy_multiple[(readptr - 1) & 511], aInputs, sizeof(aInputs));
  NetTestCapture(&initial);
  /* Find a seed whose start-gate draw is iTemp == 11 by probing the two
     healths directly, independent of the codec under test. */
  for (uiSeed = 1; uiSeed < 65536 && !iFound; ++uiSeed) {
    int aiTimer[2];
    for (int iProbe = 0; iProbe < 2; ++iProbe) {
      NetTestRestore(&initial);
      memcpy(copy_multiple[(readptr - 1) & 511], aInputs, sizeof(aInputs));
      Car[0].fHealth = iProbe ? 11.0f : 11.5f;
      ROLLERrandStateSet(uiSeed);
      NetHeadlessStepInputs(aInputs, numcars);
      aiTimer[iProbe] = Car[0].byEngineStartTimer;
    }
    iFound = aiTimer[0] == 36 && aiTimer[1] == 72;
  }
  CHECK(iFound);
  --uiSeed;

  NetTestRestore(&initial);
  memcpy(copy_multiple[(readptr - 1) & 511], aInputs, sizeof(aInputs));
  CHECK(NetSnapshotEncodeCarFull(0, &wire));
  ROLLERrandStateSet(uiSeed);
  NetHeadlessStepInputs(aInputs, numcars);
  iOriginalTimer = Car[0].byEngineStartTimer;

  NetTestRestore(&initial);
  memcpy(copy_multiple[(readptr - 1) & 511], aInputs, sizeof(aInputs));
  Car[0].fHealth = 100.0f;
  CHECK(NetSnapshotDecodeCarFull(0, &wire));
  ROLLERrandStateSet(uiSeed);
  NetHeadlessStepInputs(aInputs, numcars);
  iRestoredTimer = Car[0].byEngineStartTimer;
  if (iOriginalTimer != iRestoredTimer)
    fprintf(stderr, "start gate seed %u: original timer %d, restored timer %d\n",
            uiSeed, iOriginalTimer, iRestoredTimer);
  CHECK(iOriginalTimer == 36 && iRestoredTimer == iOriginalTimer);

  false_starts = iFalseStarts;
  NetTestRestore(pRunning);
  Car[0].fHealth = 11.5f;
  CHECK(NetSnapshotEncodeCarFull(0, &wire));
  Car[0].fHealth = 100.0f;
  CHECK(NetSnapshotDecodeCarFull(0, &wire));
  CHECK(Car[0].fHealth == 11.5f);
  NetTestRestore(pRunning);
  printf("full-state health: exact 11.5 round trip, start gate seed %u agrees (%d)\n",
         uiSeed, iRestoredTimer);
}

/* NET-FIX-1: each row pokes one full-state field out of range.  A rejected
   decode must leave every car, human_control and finished_car untouched. */
static void NetTestFullStateRejection(const tTestMoment *pRunning)
{
  static const char *aszRows[] = {
    "control", "control-edges",
    "local-yaw-high", "local-yaw-negative", "local-pitch", "local-roll",
    "local-actual-yaw", "attacker-numcars", "attacker-200", "gear-numgears",
    "gear-minus-3", "wheel-frame", "damage-state", "race-position", "lives",
    "lap-high", "lap-negative", "lap-number", "finish-position",
    "health-high", "health-negative", "health-nan", "curr-chunk",
    "reference-chunk", "last-valid-chunk"
  };
  static tCar aBefore[MAX_CARS];
  int iRows = (int)(sizeof(aszRows) / sizeof(aszRows[0]));
  int iLapBad = NoOfLaps > 0 ? NoOfLaps + 2 : 0x80;
  for (int iRow = 0; iRow < iRows; ++iRow) {
    tNetCarFullState full;
    int iHuman, iFinished, iDecoded;
    NetTestRestore(pRunning);
    human_control[0] = 1;
    CHECK(NetSnapshotEncodeCarFull(0, &full));
    switch (iRow) {
      case 0: break;
      case 1:
        full.extra.nLocalYaw = full.extra.nLocalPitch = 16383;
        full.extra.nLocalRoll = full.extra.nLocalActualYaw = 0;
        full.extra.byAttacker = (uint8)(numcars - 1);
        full.state.byGearAyMax = (uint8)-2;
        full.state.byWheelAnimationFrame = 15;
        full.state.byDamageState = 1;
        full.state.byRacePosition = (uint8)(numcars - 1);
        full.state.byLives = 254; /* destroyed non-competitor, pre-renormalise */
        full.extra.byFinishPosition = 255;
        full.extra.fHealth = 100.0f;
        full.state.nCurrChunk = (int16)(TRAK_LEN - 1);
        full.state.nReferenceChunk = (int16)(TRAK_LEN - 1);
        full.state.nLastValidChunk = (int16)(TRAK_LEN - 1);
        break;
      case 2: full.extra.nLocalYaw = 16384; break;
      case 3: full.extra.nLocalYaw = -1; break;
      case 4: full.extra.nLocalPitch = 16384; break;
      case 5: full.extra.nLocalRoll = -1; break;
      case 6: full.extra.nLocalActualYaw = 16384; break;
      case 7: full.extra.byAttacker = (uint8)numcars; break;
      case 8: full.extra.byAttacker = 200; break;
      case 9: full.state.byGearAyMax = (uint8)CarEngines.engines[Car[0].byCarDesignIdx].iNumGears; break;
      case 10: full.state.byGearAyMax = (uint8)-3; break;
      case 11: full.state.byWheelAnimationFrame = 16; break;
      case 12: full.state.byDamageState = 2; break;
      case 13: full.state.byRacePosition = (uint8)numcars; break;
      case 14: full.state.byLives = 4; break;
      case 15: full.state.byLap = (uint8)iLapBad; break;
      case 16: full.state.byLap = 0x80; break;
      case 17: full.extra.byLapNumber = (uint8)iLapBad; break;
      case 18: full.extra.byFinishPosition = (uint8)numcars; break;
      case 19: full.extra.fHealth = 100.5f; break;
      case 20: full.extra.fHealth = -1.0f; break;
      case 21: full.extra.fHealth = NAN; break;
      case 22: full.state.nCurrChunk = (int16)TRAK_LEN; break;
      case 23: full.state.nReferenceChunk = (int16)TRAK_LEN; break;
      case 24: full.state.nLastValidChunk = (int16)TRAK_LEN; break;
    }
    memcpy(aBefore, Car, sizeof(aBefore));
    iHuman = human_control[0];
    iFinished = finished_car[0];
    iDecoded = NetSnapshotDecodeCarFull(0, &full);
    if (iRow < 2) {
      if (!iDecoded)
        fprintf(stderr, "full-state row %s rejected a valid state\n", aszRows[iRow]);
      CHECK(iDecoded);
      continue;
    }
    if (iDecoded)
      fprintf(stderr, "full-state row %s was accepted\n", aszRows[iRow]);
    CHECK(!iDecoded);
    CHECK(!memcmp(aBefore, Car, sizeof(aBefore)));
    CHECK(human_control[0] == iHuman && finished_car[0] == iFinished);
  }
  NetTestRestore(pRunning);
  printf("full-state validation: %d out-of-range rows rejected, controls decode\n", iRows - 2);
}

static void NetTestFieldAudit(void)
{
  typedef struct { const char *szName, *szCategory; size_t uiOffset, uiSize; } tField;
  static const tField aFields[] = {
#define NET_CAR_FIELD(name, category) {#name, category, offsetof(tCar, name), sizeof(((tCar *)0)->name)},
#include "net_car_fields.inc"
#undef NET_CAR_FIELD
  };
  static tTestMoment initial, expected;
  tCopyData aInputs[MAX_CARS] = {0}, aPrevious[MAX_CARS];
  human_control[0] = 1;
  aInputs[0].data.unFlags = BUTTON_FLAG_ACCEL;
  aInputs[0].data.unInput = 12 << 8;
  SetEngine(&Car[0], 250);
  Car[0].fFinalSpeed = 250;
  Car[0].byEngineStartTimer = 12;
  memcpy(aPrevious, copy_multiple[(readptr - 1) & 511], sizeof(aPrevious));
  NetTestCapture(&initial);
  for (int iTick = 0; iTick < 20; ++iTick)
    NetHeadlessStepInputs(aInputs, numcars);
  NetTestCapture(&expected);
  puts("field,assignment,nonzero_at_boundary,car_changed_after_20_ticks,rng_changed");
  for (size_t uiField = 0; uiField < sizeof(aFields) / sizeof(aFields[0]); ++uiField) {
    const tField *pField = &aFields[uiField];
    int iNonzero = 0;
    NetTestRestore(&initial);
    memcpy(copy_multiple[(readptr - 1) & 511], aPrevious, sizeof(aPrevious));
    for (size_t uiByte = 0; uiByte < pField->uiSize; ++uiByte)
      iNonzero |= ((const uint8 *)&Car[0])[pField->uiOffset + uiByte];
    memset((uint8 *)&Car[0] + pField->uiOffset, 0, pField->uiSize);
    for (int iTick = 0; iTick < 20; ++iTick)
      NetHeadlessStepInputs(aInputs, numcars);
    printf("%s,%s,%d,%d,%d\n", pField->szName, pField->szCategory, iNonzero != 0,
           memcmp(&Car[0], &expected.aCars[0], sizeof(tCar)) != 0,
           ROLLERrandStateGet() != expected.context.uiRandomState);
  }
  NetTestRestore(&initial);
}

int main(int iArgc, const char **ppArgv, const char **ppEnv)
{
  (void)ppEnv;
  char szError[512];
  tCar aBefore[16];
  int iAdvanced = 0;
  CHECK(iArgc == 3 || iArgc == 4);
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
  if (iArgc == 4 && !strcmp(ppArgv[3], "--full-state-coherence"))
    return NetTestFullStateCoherence();
  if (iArgc == 4 && !strcmp(ppArgv[3], "--field-audit")) {
    NetTestFieldAudit();
    return 0;
  }
  static tTestMoment running;
  NetTestCapture(&running);
  if (iArgc == 4 && !strcmp(ppArgv[3], "--replay-output-only")) {
    NetTestReplayOutput();
    return 0;
  }
  NetTestDoubleRun();
  NetTestRestore(&running);
  for (int iVariant = 0; iVariant < 8; ++iVariant) {
    NetTestRestore(&running);
    NetTestRollback(iVariant);
  }
  NetTestRestore(&running);
  NetTestRampsAndPuppets();
  NetTestRestore(&running);
  NetTestFramesAndRanges();
  NetTestHealthExact(&running);
  NetTestFullStateRejection(&running);
  NetTestRestore(&running);
  NetTestSnapshots();
  NetTestReplayOutput();
  return 0;
}
