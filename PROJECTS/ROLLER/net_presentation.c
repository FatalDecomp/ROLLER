#include "net_presentation.h"

#include <math.h>
#include <string.h>

static double NetPresentationRelative(const tNetPresentation *pClock,
                                      uint32 uiTick)
{
  return (double)(int32)(uiTick - pClock->uiStartTick);
}

static double NetPresentationClamp(double dValue, double dMin, double dMax)
{
  return dValue < dMin ? dMin : dValue > dMax ? dMax : dValue;
}

void NetPresentationInit(tNetPresentation *pClock, uint32 uiStartTick,
                         uint16 unTickRateHz, uint8 bySnapshotInterval)
{
  memset(pClock, 0, sizeof(*pClock));
  pClock->uiStartTick = uiStartTick;
  pClock->dTicksPerMs = unTickRateHz / 1000.0;
  pClock->dIntervalMs = bySnapshotInterval / pClock->dTicksPerMs;
  pClock->dReserveMs = fmax(50.0, 2.0 * pClock->dIntervalMs);
  pClock->dTargetReserveMs = pClock->dReserveMs;
}

void NetPresentationReset(tNetPresentation *pClock)
{
  uint32 uiStartTick = pClock->uiStartTick;
  double dTicksPerMs = pClock->dTicksPerMs;
  double dIntervalMs = pClock->dIntervalMs;
  memset(pClock, 0, sizeof(*pClock));
  pClock->uiStartTick = uiStartTick;
  pClock->dTicksPerMs = dTicksPerMs;
  pClock->dIntervalMs = dIntervalMs;
  pClock->dReserveMs = fmax(50.0, 2.0 * dIntervalMs);
  pClock->dTargetReserveMs = pClock->dReserveMs;
}

int NetPresentationObserve(tNetPresentation *pClock, uint32 uiTick,
                           uint64 ullNowMs)
{
  if (pClock->byHasLatest && (int32)(uiTick - pClock->uiLatestTick) <= 0)
    return 0;
  if (pClock->byHasLatest && ullNowMs >= pClock->ullLatestArrivalMs) {
    uint32 uiSourceGap = uiTick - pClock->uiLatestTick;
    double dArrivalGapMs = (double)(ullNowMs - pClock->ullLatestArrivalMs);
    double dSourceGapMs = uiSourceGap / pClock->dTicksPerMs;
    tNetPresentationVariation *pEntry =
        &pClock->aVariations[pClock->uiVariationNext];
    pEntry->ullArrivalMs = ullNowMs;
    pEntry->dVariationMs = fmax(0.0, dArrivalGapMs - dSourceGapMs);
    pEntry->dSourceExcessMs =
        fmax(0.0, dSourceGapMs - 2.0 * pClock->dIntervalMs);
    pClock->uiVariationNext =
        (pClock->uiVariationNext + 1) % NET_PRESENTATION_VARIATIONS;
    if (pClock->uiVariationCount < NET_PRESENTATION_VARIATIONS)
      ++pClock->uiVariationCount;
  }
  pClock->uiLatestTick = uiTick;
  pClock->ullLatestArrivalMs = ullNowMs;
  pClock->byHasLatest = 1;
  return 1;
}

void NetPresentationPause(tNetPresentation *pClock, uint64 ullNowMs)
{
  pClock->byWasPaused = 1;
  pClock->ullLastFrameMs = ullNowMs;
  pClock->byHasFrame = 1;
  pClock->dSpeed = 0.0;
  pClock->ullActiveFrameMs = 0;
}

/* 95th percentile of positive delivery variation in the last five seconds.
   Source gaps are tracked separately: the largest excess over two configured
   intervals raises reserve after a missing-snapshot burst. */
static double NetPresentationReserveTarget(tNetPresentation *pClock,
                                           uint64 ullNowMs)
{
  double adVariation[NET_PRESENTATION_VARIATIONS];
  double dSourceExcess = 0.0, dFloor;
  int iCount = 0;
  for (uint32 uiIndex = 0; uiIndex < pClock->uiVariationCount; ++uiIndex) {
    const tNetPresentationVariation *pEntry =
        &pClock->aVariations[uiIndex];
    int iInsert;
    if (ullNowMs < pEntry->ullArrivalMs ||
        ullNowMs - pEntry->ullArrivalMs > NET_PRESENTATION_WINDOW_MS)
      continue;
    if (pEntry->dSourceExcessMs > dSourceExcess)
      dSourceExcess = pEntry->dSourceExcessMs;
    iInsert = iCount++;
    while (iInsert && adVariation[iInsert - 1] > pEntry->dVariationMs) {
      adVariation[iInsert] = adVariation[iInsert - 1];
      --iInsert;
    }
    adVariation[iInsert] = pEntry->dVariationMs;
  }
  dFloor = fmax(50.0, 2.0 * pClock->dIntervalMs);
  pClock->dTargetReserveMs = dFloor + dSourceExcess +
      (iCount ? adVariation[(iCount * 95 + 99) / 100 - 1] : 0.0);
  if (pClock->dTargetReserveMs > NET_PRESENTATION_MAX_RESERVE_MS) {
    pClock->dTargetReserveMs = NET_PRESENTATION_MAX_RESERVE_MS;
    ++pClock->uiReserveSaturations;
  }
  return pClock->dTargetReserveMs;
}

void NetPresentationFrame(tNetPresentation *pClock, uint64 ullNowMs,
                          uint32 uiOldestTick, int iHasOldest, int iPaused)
{
  uint64 ullElapsedMs = 0;
  double dNewest, dOldest, dTarget, dErrorMs, dRequestedSpeed, dBefore;
  if (pClock->byHasFrame && ullNowMs >= pClock->ullLastFrameMs)
    ullElapsedMs = ullNowMs - pClock->ullLastFrameMs;
  pClock->ullLastFrameMs = ullNowMs;
  pClock->byHasFrame = 1;
  pClock->dSpeed = 0.0;
  if (iPaused) {
    pClock->byWasPaused = 1;
    return;
  }
  if (pClock->byWasPaused) {
    /* Host ticks are frozen while paused.  Re-anchor a still-current received
       tick so the local pause duration cannot masquerade as delivery debt. */
    pClock->ullLatestArrivalMs = ullNowMs;
    pClock->byWasPaused = 0;
    return;
  }
  pClock->ullActiveFrameMs = ullElapsedMs;
  if (!pClock->byHasLatest || !iHasOldest)
    return;
  dNewest = NetPresentationRelative(pClock, pClock->uiLatestTick);
  dOldest = NetPresentationRelative(pClock, uiOldestTick);
  dTarget = NetPresentationReserveTarget(pClock, ullNowMs);
  if (dTarget > pClock->dReserveMs)
    pClock->dReserveMs = dTarget;
  else
    pClock->dReserveMs = fmax(dTarget,
        pClock->dReserveMs - 0.01 * (double)ullElapsedMs);
  if (!pClock->byReady) {
    /* Three ordinary snapshots supply two intervals of history.  Until then
       a renderer can hold a valid received pose; P is not yet published. */
    if ((dNewest - dOldest) / pClock->dTicksPerMs + 0.001 <
        pClock->dReserveMs)
      return;
    pClock->dCursor = dNewest - pClock->dReserveMs * pClock->dTicksPerMs;
    pClock->byReady = 1;
    return;
  }
  dBefore = pClock->dCursor;
  if (dOldest > pClock->dCursor) {
    pClock->dCursor = dOldest;
    ++pClock->uiForwardResyncs;
  }
  if (ullElapsedMs > NET_PRESENTATION_MAX_FRAME_MS) {
    ullElapsedMs = NET_PRESENTATION_MAX_FRAME_MS;
    ++pClock->uiLongFrames;
  }
  dTarget = dNewest + (double)(ullNowMs - pClock->ullLatestArrivalMs) *
      pClock->dTicksPerMs - pClock->dReserveMs * pClock->dTicksPerMs;
  dErrorMs = (dTarget - pClock->dCursor) / pClock->dTicksPerMs;
  /* A 10 ms deadband absorbs packet scheduling quantization.  Speed changes
     only for sustained phase error, never by assigning P from a packet. */
  dRequestedSpeed = fabs(dErrorMs) <= 10.0 ? 1.0 :
      NetPresentationClamp(1.0 + 0.005 *
          (dErrorMs > 0.0 ? dErrorMs - 10.0 : dErrorMs + 10.0),
          0.95, 1.05);
  pClock->dSpeed = dRequestedSpeed;
  pClock->dCursor += (double)ullElapsedMs * pClock->dTicksPerMs *
      dRequestedSpeed;
  /* A delivery outage may use up the 100 ms visual extrapolation allowance.
     Holding here also lets a later packet refill the buffer. */
  if (pClock->dCursor > dNewest + 100.0 * pClock->dTicksPerMs) {
    pClock->dCursor = dNewest + 100.0 * pClock->dTicksPerMs;
    pClock->dSpeed = 0.0;
  }
  if (pClock->dCursor < dBefore - 0.000001)
    ++pClock->uiRegressions;
}
