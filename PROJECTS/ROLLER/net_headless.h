#ifndef ROLLER_NET_HEADLESS_H
#define ROLLER_NET_HEADLESS_H

#include <stddef.h>
#include "types.h"
#include "sound.h"

/* One world per process. Call initialization once before stepping. */
int NetHeadlessInit(const char *szTrack, const char *szAssets, int iCars,
                    uint32 uiSeed, char *szError, size_t uiErrorCapacity);
void NetHeadlessStep(void);
void NetHeadlessStepInputs(const tCopyData *pInputs, int iNumCars);

#endif
