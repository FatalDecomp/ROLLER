#ifndef ROLLER_NET_TYPES_H
#define ROLLER_NET_TYPES_H
#include "types.h"

typedef enum { NET_MODE_LEGACY, NET_MODE_MODERN } eNetMode;
typedef enum { NET_AUTHORITY_LOCAL, NET_AUTHORITY_REMOTE } eNetAuthority;
typedef enum { NET_PREDICT_FULL, NET_PREDICT_DELAYED } eNetPredictionMode;
extern int net_mode;
/* Native modern-session role.  A listen host keeps the SDL timer as the
   authoritative tick source; a remote client uses its dilated accumulator. */
extern int net_listen_host;

typedef struct {
  float fRttMs, fJitterMs;
  int iLossPercent, iSnapshotAgeMs, iStalled;
  float fCorrectionMagnitude;
  int iCorrectionCount, iDeferredCorrections, iRampCorrections;
  int iReplayTicksTotal, iReplayDepth;
  float fReplayMsTotal, fReplayMsWorst;
  int iPredictionMode, iPredictionTransitions;
  uint32 uiTimeDegradedMs;
  int iLateInputs, iFutureInputs;
  float fTickScale;
  int iBytesInPerSec, iBytesOutPerSec;
  int iRouteType, iRelayThrottled; /* 0 unknown, 1 direct, 2 relay, 3 simulated */
  uint32 uiAdvancingArrivals, uiDroppedDeltas;
  uint32 uiLatestSnapshotTick;
  uint32 uiArrivalGapMinMs, uiArrivalGapMaxMs, uiSourceGapMaxTicks;
  uint32 uiDeliveryVariationMaxMs;
  uint64 ullDisplayModeMs[5], ullPausedDisplayMs;
  double dPresentationTick, dDesiredReserveMs, dActualReserveMs;
  float fPlaybackSpeed, fHistoryCoverageMs, fExtrapolationMs, fFrameMs;
  float fConfiguredSnapshotIntervalMs;
  uint32 uiFrameMaxMs;
  int iDisplayMode, iTimelineRegressions, iPresentationEpochs;
  int iRecoverySamples, iRemoteBlendMs;
  float fRecoveryErrorWorldMax, fRecoveryYawErrorDegMax;
  uint64 ullBufferedDisplayModeMs[5];
  double dBufferedPresentationTick, dBufferedReserveMs;
  double dBufferedActualReserveMs;
  float fBufferedPlaybackSpeed;
  uint32 uiBufferedForwardResyncs, uiBufferedLongFrames;
  uint32 uiBufferedReserveSaturations, uiBufferedTimelineRegressions;
  int iBufferedDisplayMode;
} tNetStats;
extern tNetStats g_netStats;
#endif
