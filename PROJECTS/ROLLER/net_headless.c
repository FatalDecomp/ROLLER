#include "net_headless.h"
#include "3d.h"
#include "car.h"
#include "control.h"
#include "frontend.h"
#include "function.h"
#include "loadtrak.h"
#include "moving.h"
#include "network.h"
#include "roller.h"
#include "sound.h"
#include <stdio.h>
#include <string.h>

int NetHeadlessInit(const char *szTrack, const char *szAssets, int iCars,
                    uint32 uiSeed, char *szError, size_t uiErrorCapacity)
{
  static int iInitialized;
  if (iInitialized || !szTrack || !szAssets || iCars < 1 || iCars > 16) {
    snprintf(szError, uiErrorCapacity, "invalid or repeated headless initialization");
    return 0;
  }
  iInitialized = 1;
  init();
  InitCarStructs();
  ROLLERsrand(uiSeed);
  numcars = racers = iCars;
  players = 1;
  local_players = 0;
  player_type = 0;
  network_on = replaytype = intro = champ_mode = paused = 0;
  player1_car = 0;
  player2_car = 1;
  NoOfLaps = 3;
  for (int iCar = 0; iCar < iCars; ++iCar) {
    grid[iCar] = iCar;
    Drivers_Car[iCar] = iCar % 8;
    human_control[iCar] = non_competitors[iCar] = 0;
    car_to_player[iCar] = player_to_car[iCar] = iCar;
  }
  if (loadtrack_from_path_with_assets_ex(szTrack, szAssets, szAssets, 0,
                                        szError, uiErrorCapacity) != ROLLER_ED_RESULT_OK)
    return 0;
  initnearcars();
  game_frame = -1;
  countdown = 144;
  start_race = racing = -1;
  race_started = updates = nearcarcheck = 0;
  fudge_wait = -1;
  readptr = writeptr = 0;
  memset(copy_multiple, 0, sizeof(copy_multiple));
  return 1;
}

void NetHeadlessStep(void)
{
  memset(copy_multiple[writeptr], 0, sizeof(copy_multiple[writeptr]));
  writeptr = (writeptr + 1) & 511;
  control_one_tick();
}
