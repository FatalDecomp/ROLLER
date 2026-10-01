#include "net_presentation.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { \
  fprintf(stderr, "presentation check failed at %s:%d: %s\n", \
          __FILE__, __LINE__, #x); exit(1); } } while (0)

static void NetTestStable(uint16 unRate, uint32 uiLatencyMs,
                          uint32 uiStartTick)
{
  tNetPresentation clock;
  uint32 uiNext = 0, uiReadyFrames = 0, uiAheadFrames = 0;
  uint32 uiAdjustedFrames = 0;
  double dPrevious = 0.0;
  NetPresentationInit(&clock, uiStartTick, unRate, 2);
  for (uint32 uiNow = 0; uiNow <= 35000; uiNow += 16) {
    while ((double)uiNext * 2000.0 / unRate + uiLatencyMs <= uiNow) {
      CHECK(NetPresentationObserve(&clock, uiStartTick + uiNext * 2,
                                   uiNow));
      ++uiNext;
    }
    if (clock.byHasLatest) {
      uint32 uiAgeTicks = unRate * 600u / 1000u;
      uint32 uiOldest = clock.uiLatestTick -
          (clock.uiLatestTick - uiStartTick > uiAgeTicks ?
           uiAgeTicks : clock.uiLatestTick - uiStartTick);
      NetPresentationFrame(&clock, uiNow, uiOldest, 1, 0);
    } else
      NetPresentationFrame(&clock, uiNow, 0, 0, 0);
    if (clock.byReady && uiNow >= 5000) {
      double dLatest = (double)(int32)(clock.uiLatestTick - uiStartTick);
      ++uiReadyFrames;
      if (clock.dCursor > dLatest + 0.000001)
        ++uiAheadFrames;
      if (fabs(clock.dSpeed - 1.0) > 0.001)
        ++uiAdjustedFrames;
      CHECK(clock.dCursor + 0.000001 >= dPrevious);
      CHECK(clock.dSpeed >= 0.0 && clock.dSpeed <= 1.05);
      dPrevious = clock.dCursor;
    }
  }
  CHECK(uiReadyFrames > 1500);
  CHECK(uiAheadFrames == 0);
  CHECK(uiAdjustedFrames < uiReadyFrames / 20u);
  CHECK(clock.uiForwardResyncs == 0);
  CHECK(clock.uiRegressions == 0);
  CHECK(clock.dReserveMs >= fmax(50.0, 4000.0 / unRate));
  CHECK(clock.dReserveMs <= NET_PRESENTATION_MAX_RESERVE_MS);
  printf("%u Hz, %u ms delivery: %u ready, %u ahead, %u adjusted\n",
         unRate, uiLatencyMs, uiReadyFrames, uiAheadFrames,
         uiAdjustedFrames);
}

static void NetTestTransitions(void)
{
  tNetPresentation clock;
  uint32 uiStart = UINT32_MAX - 10u;
  double dBefore, dReserve;
  NetPresentationInit(&clock, uiStart, 36, 2);
  CHECK(NetPresentationObserve(&clock, uiStart, 1000));
  NetPresentationFrame(&clock, 1000, uiStart, 1, 0);
  CHECK(!clock.byReady);
  CHECK(NetPresentationObserve(&clock, uiStart + 2u, 1056));
  NetPresentationFrame(&clock, 1056, uiStart, 1, 0);
  CHECK(!clock.byReady);
  CHECK(NetPresentationObserve(&clock, uiStart + 4u, 1111));
  NetPresentationFrame(&clock, 1111, uiStart, 1, 0);
  CHECK(NetPresentationObserve(&clock, uiStart + 6u, 1167));
  NetPresentationFrame(&clock, 1167, uiStart, 1, 0);
  CHECK(clock.byReady);
  CHECK(clock.dCursor >= 0.0 && clock.dCursor < 6.0);
  dBefore = clock.dCursor;
  CHECK(!NetPresentationObserve(&clock, uiStart + 6u, 1170));
  CHECK(!NetPresentationObserve(&clock, uiStart + 2u, 1171));
  CHECK(clock.ullLatestArrivalMs == 1167);
  NetPresentationFrame(&clock, 1183, uiStart, 1, 0);
  CHECK(clock.dCursor > dBefore);

  /* Delayed delivery, then a source gap, raise reserve without rewinding. */
  dBefore = clock.dCursor;
  CHECK(NetPresentationObserve(&clock, uiStart + 8u, 1335));
  NetPresentationFrame(&clock, 1336, uiStart, 1, 0);
  CHECK(clock.dReserveMs > 200.0);
  CHECK(clock.dCursor >= dBefore);
  CHECK(clock.uiLongFrames == 1);
  dReserve = clock.dReserveMs;
  CHECK(NetPresentationObserve(&clock, uiStart + 16u, 1557));
  NetPresentationFrame(&clock, 1557, uiStart, 1, 0);
  CHECK(clock.dReserveMs <= NET_PRESENTATION_MAX_RESERVE_MS);
  CHECK(clock.dReserveMs >= dReserve);
  CHECK(clock.uiReserveSaturations > 0);
  CHECK(clock.dCursor >= dBefore);

  /* Pausing consumes no clock time, even with a long wall-time gap. */
  dBefore = clock.dCursor;
  NetPresentationFrame(&clock, 1573, uiStart, 1, 1);
  NetPresentationFrame(&clock, 151600, uiStart, 1, 1);
  CHECK(clock.dCursor == dBefore);
  NetPresentationFrame(&clock, 151616, uiStart, 1, 0);
  CHECK(clock.dCursor == dBefore);
  NetPresentationFrame(&clock, 151632, uiStart, 1, 0);
  CHECK(clock.dCursor >= dBefore);

  /* A pause message can arrive without a render frame before resumption. */
  dBefore = clock.dCursor;
  NetPresentationPause(&clock, 151650);
  NetPresentationFrame(&clock, 251650, uiStart, 1, 0);
  CHECK(clock.dCursor == dBefore);
  CHECK(clock.ullActiveFrameMs == 0);

  /* A long outage can exhaust retained history.  The only clock jump is
     forward, is counted, and ordinary frames remain bounded afterward. */
  CHECK(NetPresentationObserve(&clock, uiStart + 52u, 252100));
  dBefore = clock.dCursor;
  NetPresentationFrame(&clock, 252100, uiStart + 42u, 1, 0);
  CHECK(clock.uiForwardResyncs == 1);
  CHECK(clock.dCursor >= 42.0 && clock.dCursor > dBefore);
  CHECK(clock.uiLongFrames >= 2);
  CHECK(clock.uiLatestTick == uiStart + 52u);
  CHECK(clock.uiRegressions == 0);
  dReserve = clock.dReserveMs;
  for (int iPacket = 0; iPacket < 200; ++iPacket) {
    uint32 uiTick = uiStart + 54u + (uint32)iPacket * 2u;
    uint64 ullNow = 252156u + (uint64)iPacket * 56u;
    CHECK(NetPresentationObserve(&clock, uiTick, ullNow));
    NetPresentationFrame(&clock, ullNow, uiTick - 20u, 1, 0);
  }
  CHECK(clock.dReserveMs < dReserve);
  CHECK(clock.dReserveMs >= 4000.0 / 36.0);
  CHECK(clock.uiRegressions == 0);
  NetPresentationReset(&clock);
  CHECK(!clock.byHasLatest && !clock.byReady);
  CHECK(clock.uiForwardResyncs == 0);
}

static void NetTestRecovery(void)
{
  tNetPresentationRecovery recovery = {0};
  tNetPresentationPose prior = {0}, latest = {0};
  tNetPresentationPose raw = {0}, result;
  prior.nYaw = prior.nPitch = prior.nRoll = prior.nActualYaw = 16300;
  latest = prior;
  latest.afPosition[0] = 100.0f;
  raw = latest;
  raw.afPosition[0] = 150.0f;
  NetPresentationRecoverySample(&recovery, &raw, &prior, &latest,
      0.0, 2.0, 3.0, 0.036, 16, 1, 0, &result);
  CHECK(result.afPosition[0] == 150.0f && recovery.byStarved);

  /* Both trajectories are evaluated at tick 3.4.  The first recovered
     draw must retain the old displayed trajectory, including wrapped angles. */
  raw.afPosition[0] = 120.0f;
  raw.nYaw = raw.nPitch = raw.nRoll = raw.nActualYaw = 100;
  NetPresentationRecoverySample(&recovery, &raw, NULL, NULL,
      0.0, 0.0, 3.4, 0.036, 16, 0, 0, &result);
  CHECK(fabsf(result.afPosition[0] - 170.0f) < 0.001f);
  CHECK(result.nYaw == 16300 && result.nPitch == 16300 &&
        result.nRoll == 16300 && result.nActualYaw == 16300);
  CHECK(recovery.byActive && recovery.uiBlendElapsedMs == 0);
  CHECK(recovery.uiBlendsStarted == 1 &&
        fabsf(recovery.fLastErrorWorld - 50.0f) < 0.001f);

  raw.afPosition[0] = 130.0f;
  NetPresentationRecoverySample(&recovery, &raw, NULL, NULL,
      0.0, 0.0, 3.8, 0.036, 16, 0, 0, &result);
  CHECK(fabsf(result.afPosition[0] - 172.0f) < 0.001f);
  CHECK(recovery.uiBlendsStarted == 1);
  raw.afPosition[0] = 180.0f;
  NetPresentationRecoverySample(&recovery, &raw, &prior, &latest,
      0.0, 2.0, 4.0, 0.036, 16, 1, 0, &result);
  CHECK(recovery.byActive && recovery.uiBlendElapsedMs == 32);
  raw.afPosition[0] = 140.0f;
  NetPresentationRecoverySample(&recovery, &raw, NULL, NULL,
      0.0, 0.0, 4.2, 0.036, 16, 0, 0, &result);
  CHECK(fabsf(result.afPosition[0] - 236.0f) < 0.001f);
  CHECK(recovery.uiBlendsStarted == 1 && recovery.uiBlendElapsedMs == 48);
  raw.afPosition[0] = 200.0f;
  NetPresentationRecoverySample(&recovery, &raw, NULL, NULL,
      0.0, 0.0, 5.0, 0.036, 52, 0, 0, &result);
  CHECK(result.afPosition[0] == raw.afPosition[0] && !recovery.byActive);

  /* A longer outage holds at 100 ms beyond the old latest tick.  A real
     reset or an oversized error is shown immediately, never swept. */
  NetPresentationRecoverySample(&recovery, &latest, &prior, &latest,
      0.0, 2.0, 10.0, 0.036, 16, 1, 0, &result);
  raw.afPosition[0] = 9000.0f;
  NetPresentationRecoverySample(&recovery, &raw, NULL, NULL,
      0.0, 0.0, 10.0, 0.036, 16, 0, 0, &result);
  CHECK(result.afPosition[0] == raw.afPosition[0]);
  CHECK(!recovery.byActive && recovery.uiDiscontinuities == 1);
  NetPresentationRecoverySample(&recovery, &latest, &prior, &latest,
      0.0, 2.0, 3.0, 0.036, 16, 1, 0, &result);
  raw = latest;
  raw.byLives = 2;
  NetPresentationRecoverySample(&recovery, &raw, NULL, NULL,
      0.0, 0.0, 3.2, 0.036, 16, 0, 0, &result);
  CHECK(!recovery.byActive && recovery.uiDiscontinuities == 2);
  NetPresentationRecoveryReset(&recovery);
  CHECK(!recovery.byStarved && !recovery.byActive &&
        recovery.uiBlendsStarted == 0);
  NetPresentationRecoverySample(&recovery, &latest, &prior, &latest,
      0.0, 2.0, 3.0, 0.036, 16, 1, 0, &result);
  raw = latest;
  raw.afPosition[0] = 3000.0f;
  NetPresentationRecoverySample(&recovery, &raw, NULL, NULL,
      0.0, 0.0, 3.2, 0.036, 16, 0, 1, &result);
  CHECK(result.afPosition[0] == raw.afPosition[0] &&
        !recovery.byActive && recovery.uiDiscontinuities == 1);
}

int main(void)
{
  const uint16 aunRates[] = {36, 50, 100};
  const uint32 auiLatencies[] = {5, 60, 100, 150};
  for (int iRate = 0; iRate < 3; ++iRate)
    for (int iLatency = 0; iLatency < 4; ++iLatency)
      NetTestStable(aunRates[iRate], auiLatencies[iLatency],
                    iRate == 2 ? UINT32_MAX - 1000u : 1000u);
  NetTestTransitions();
  NetTestRecovery();
  puts("net presentation controller passed");
  return 0;
}
