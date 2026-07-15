#ifndef _ROLLER_WASM_TICK_CLOCK_H
#define _ROLLER_WASM_TICK_CLOCK_H
//-------------------------------------------------------------------------------------------------
#include <stdbool.h>
#include <stdint.h>
//-------------------------------------------------------------------------------------------------

typedef struct {
  uint64_t ullLastTimeNs;
  uint64_t ullRemainderNs;
  bool bInitialized;
  bool bWasSuspended;
} tWasmTickClock;

//-------------------------------------------------------------------------------------------------

static inline void wasm_tick_clock_reset(tWasmTickClock *pClock, uint64_t ullNowNs)
{
  pClock->ullLastTimeNs = ullNowNs;
  pClock->ullRemainderNs = 0;
  pClock->bInitialized = true;
  pClock->bWasSuspended = false;
}

//-------------------------------------------------------------------------------------------------

static inline void wasm_tick_clock_mark_suspended(tWasmTickClock *pClock, uint64_t ullNowNs)
{
  wasm_tick_clock_reset(pClock, ullNowNs);
  pClock->bWasSuspended = true;
}

//-------------------------------------------------------------------------------------------------

static inline uint64_t wasm_tick_clock_advance(
    tWasmTickClock *pClock,
    uint64_t ullNowNs,
    uint64_t ullIntervalNs,
    bool bSuspended)
{
  if (bSuspended) {
    wasm_tick_clock_mark_suspended(pClock, ullNowNs);
    return 0;
  }

  if (pClock->bWasSuspended || ullIntervalNs == 0 || !pClock->bInitialized ||
      ullNowNs < pClock->ullLastTimeNs) {
    wasm_tick_clock_reset(pClock, ullNowNs);
    return 0;
  }

  uint64_t ullDeltaNs = ullNowNs - pClock->ullLastTimeNs;
  uint64_t ullSteps = ullDeltaNs / ullIntervalNs;
  uint64_t ullDeltaRemainderNs = ullDeltaNs % ullIntervalNs;

  pClock->ullLastTimeNs = ullNowNs;
  if (pClock->ullRemainderNs >= ullIntervalNs - ullDeltaRemainderNs) {
    ++ullSteps;
    pClock->ullRemainderNs -= ullIntervalNs - ullDeltaRemainderNs;
  } else {
    pClock->ullRemainderNs += ullDeltaRemainderNs;
  }

  return ullSteps;
}

//-------------------------------------------------------------------------------------------------
#endif
