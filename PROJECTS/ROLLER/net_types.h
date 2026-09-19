#ifndef ROLLER_NET_TYPES_H
#define ROLLER_NET_TYPES_H
#include "types.h"

typedef enum { NET_MODE_LEGACY, NET_MODE_MODERN } eNetMode;
typedef enum { NET_AUTHORITY_LOCAL, NET_AUTHORITY_REMOTE } eNetAuthority;
typedef enum { NET_PREDICT_FULL, NET_PREDICT_DELAYED } eNetPredictionMode;
extern int net_mode;

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
} tNetStats;
extern tNetStats g_netStats;
#endif
