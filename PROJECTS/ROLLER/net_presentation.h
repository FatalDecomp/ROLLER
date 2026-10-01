#ifndef ROLLER_NET_PRESENTATION_H
#define ROLLER_NET_PRESENTATION_H

#include "types.h"

#define NET_PRESENTATION_VARIATIONS 256
#define NET_PRESENTATION_WINDOW_MS 5000u
#define NET_PRESENTATION_MAX_RESERVE_MS 250.0
#define NET_PRESENTATION_MAX_FRAME_MS 100u
#define NET_PRESENTATION_RECOVERY_MS 100u
#define NET_PRESENTATION_RECOVERY_MAX_WORLD 4096.0f

typedef struct {
  float afPosition[3];
  int16 nYaw, nPitch, nRoll, nActualYaw;
  uint8 byLives, byStatusFlags;
} tNetPresentationPose;

/* Per-car visual state.  The source pair is copied, never held as pointers to
   slots in the client's snapshot ring. */
typedef struct {
  tNetPresentationPose prior, latest;
  tNetPresentationPose offset;
  double dPriorTick, dLatestTick;
  uint32 uiBlendElapsedMs, uiBlendsStarted, uiDiscontinuities;
  float fLastErrorWorld, fLastErrorYawDeg;
  uint8 byStarved, byHasTrajectory, byActive;
} tNetPresentationRecovery;

void NetPresentationRecoveryReset(tNetPresentationRecovery *pRecovery);
/* On the first bracketed frame after starvation, compare the old bounded
   trajectory with the new sample at dCursor.  Returns the render-only pose. */
void NetPresentationRecoverySample(tNetPresentationRecovery *pRecovery,
    const tNetPresentationPose *pRaw, const tNetPresentationPose *pPrior,
    const tNetPresentationPose *pLatest, double dPriorTick,
    double dLatestTick, double dCursor, double dTicksPerMs,
    uint32 uiElapsedMs, int iStarved, int iDiscontinuity,
    tNetPresentationPose *pResult);

typedef struct {
  uint64 ullArrivalMs;
  double dVariationMs, dSourceExcessMs;
} tNetPresentationVariation;

/* A clock in relative, double-precision server ticks.  No platform clock,
   snapshot pointers, or simulation state is owned by this controller. */
typedef struct {
  uint32 uiStartTick, uiLatestTick;
  uint64 ullLatestArrivalMs, ullLastFrameMs;
  double dTicksPerMs, dIntervalMs;
  double dCursor, dReserveMs, dTargetReserveMs, dSpeed;
  tNetPresentationVariation aVariations[NET_PRESENTATION_VARIATIONS];
  uint32 uiVariationNext, uiVariationCount;
  uint32 uiForwardResyncs, uiLongFrames, uiReserveSaturations;
  uint32 uiRegressions;
  uint64 ullActiveFrameMs;
  uint8 byHasLatest, byHasFrame, byReady, byWasPaused;
} tNetPresentation;

void NetPresentationInit(tNetPresentation *pClock, uint32 uiStartTick,
                         uint16 unTickRateHz, uint8 bySnapshotInterval);
void NetPresentationReset(tNetPresentation *pClock);
/* Returns one only for a new highest tick.  Duplicate and older ticks do not
   move the arrival anchor or alter the adaptation window. */
int NetPresentationObserve(tNetPresentation *pClock, uint32 uiTick,
                           uint64 ullNowMs);
/* Called on an accepted pause even if no render frame runs until resume. */
void NetPresentationPause(tNetPresentation *pClock, uint64 ullNowMs);
/* The oldest retained tick must be supplied from validated snapshot history.
   Paused frames freeze P and do not create playback debt. */
void NetPresentationFrame(tNetPresentation *pClock, uint64 ullNowMs,
                          uint32 uiOldestTick, int iHasOldest, int iPaused);

#endif
