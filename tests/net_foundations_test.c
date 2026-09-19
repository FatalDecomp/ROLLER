#include "net_headless.h"
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
  while (game_frame <= 145)
    NetHeadlessStep();
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
  return 0;
}
