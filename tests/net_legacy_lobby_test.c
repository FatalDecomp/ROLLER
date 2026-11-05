/* Exercise legacy discovery, loading, camera setup and driving with real UDP.
   The Python runner supplies a separate process for each game instance. */
#include "3d.h"
#include "frontend.h"
#include "func2.h"
#include "func3.h"
#include "car.h"
#include "control.h"
#include "function.h"
#include "loadtrak.h"
#include "moving.h"
#include "view.h"
#include "transfrm.h"
#include "net_legacy.h"
#include "net_types.h"
#include "network.h"
#include "rollercomms.h"
#include "roller.h"
#include "rollerinput.h"
#include "sound.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define CHECK(c) do { if (!(c)) { \
  fprintf(stderr, "%d: %s (players=%d slot=%d master=%d cars=%d/%d ready=%d names=%s/%s)\n", \
          __LINE__, #c, network_on, network_slot, master, Players_Cars[0], \
          Players_Cars[1], players_waiting, player_names[0], player_names[1]); exit(1); \
} } while (0)

static void Pump(void)
{
  ++frames;
  if (network_initialise_active())
    network_initialise_update();
  else if (network_broadcast_wait_active())
    network_broadcast_wait_update();
  else {
    CheckNewNodes();
    BroadcastNews();
    ROLLERCommsPumpSendQueue();
  }
  SDL_Delay(5);
}

static void WaitBroadcast(int mode)
{
  uint64 deadline = SDL_GetTicks() + 3000;
  network_broadcast_wait_start(mode, 1);
  while (network_broadcast_wait_active() && SDL_GetTicks() < deadline)
    Pump();
  CHECK(!network_broadcast_wait_active());
}

int main(int argc, char **argv)
{
  CHECK(argc == 7);
  int isHost = !strcmp(argv[1], "HOST");
  const char *remoteName = isHost ? "CLIENT" : "HOST";
  int localDesign = atoi(argv[5]);
  int remoteDesign = atoi(argv[6]);
  uint16 port = (uint16)atoi(argv[2]);
  uint64 deadline;
  CHECK(SDL_Init(0));
  init();
  InitCarStructs();
  net_mode = NET_MODE_LEGACY;
  NetLegacyTrapReset();
  net_type = 0;
  frontend_on = -1;
  competitors = 16;
  level = 2;
  TrackLoad = 3;
  names[3] = argv[3];
  random_seed = 12345;
  ROLLERsrand((uint32)random_seed);
  player1_car = 0;
  player_type = 1;
  for (int i = 0; i < MAX_PLAYERS; ++i)
    Players_Cars[i] = -1;
  name_copy(player_names[0], argv[1]);
  name_copy(my_name, argv[1]);
  ROLLERCommsSetLocalPort(port);
  network_slot = -1;
  network_initialise_begin(-1);
  CHECK(network_on == 1);
  deadline = SDL_GetTicks() + 3000;
  while (network_initialise_active() && SDL_GetTicks() < deadline)
    Pump();
  CHECK(!network_initialise_active());

  if (!isHost) {
    deadline = SDL_GetTicks() + 6000;
    while (gamers_playing[0] != 1 && SDL_GetTicks() < deadline)
      Pump();
    CHECK(gamers_playing[0] == 1);
    CHECK(!strcmp(gamers_names[0], "HOST"));
    /* The slot picker promotes advertised addresses into known peers. */
    CHECK(ROLLERCommsAddNode(&gamers_address[0][0]) <= 1);
    ROLLERCommsSortNodes();
  }
  network_slot = 1;
  WaitBroadcast(-1);
  WaitBroadcast(-667);
  deadline = SDL_GetTicks() + 6000;
  while (network_on != 2 && SDL_GetTicks() < deadline)
    Pump();
  CHECK(network_on == 2);
  deadline = SDL_GetTicks() + 500;
  while (SDL_GetTicks() < deadline)
    Pump();
  CHECK(network_on == 2);
  CHECK(!strcmp(player_names[0], argv[1]));
  CHECK(!strcmp(player_names[1], remoteName));

  /* Car selection and entering the pre-race lobby must reach both peers. */
  Players_Cars[0] = localDesign;
  WaitBroadcast(-1);
  deadline = SDL_GetTicks() + 3000;
  while (Players_Cars[1] != remoteDesign && SDL_GetTicks() < deadline) {
    TransmitInit();
    Pump();
  }
  CHECK(Players_Cars[1] == remoteDesign);
  /* Drive the actual lobby, including its fade, allocation, record and seed
     phases. Merely testing FoundNodes misses later index overwrites. */
  int requested = 0;
  eFrontendNextState = eFRONTEND_STATE_NONE;
  claim_ticktimer(100);
  frontend_lobby_enter();
  deadline = SDL_GetTicks() + 6000;
  while (eFrontendNextState != eFRONTEND_STATE_LOADING && SDL_GetTicks() < deadline) {
    if (isHost && !requested && players_waiting == 2 && !time_to_start) {
      frontend_mouse_press_accept();
      requested = 1;
    }
    frontend_lobby_update();
    SDL_Delay(5);
  }
  CHECK(eFrontendNextState == eFRONTEND_STATE_LOADING);
  frontend_lobby_exit();
  CHECK(master == 0 && network_on == 2);
  CHECK(!strcmp(player_names[wConsoleNode], argv[1]));
  CHECK(Players_Cars[wConsoleNode] == localDesign);
  CHECK(!strcmp(player_names[1 - wConsoleNode], remoteName));
  CHECK(Players_Cars[1 - wConsoleNode] == remoteDesign);
  int iLocalRoster = wConsoleNode;
  int iLocalCar = player1_car;
  CHECK(iLocalCar == 2 * localDesign);
  CHECK(iLocalCar == player_to_car[iLocalRoster]);
  CHECK(ViewType[0] == iLocalCar);
  deadline = SDL_GetTicks() + 300;
  while (SDL_GetTicks() < deadline) {
    SDL_Delay(5);
    CHECK(player1_car == iLocalCar && ViewType[0] == iLocalCar);
  }
  CHECK(!strcmp(player_names[iLocalRoster], argv[1]));
  CHECK(Players_Cars[iLocalRoster] == localDesign);
  CHECK(!strcmp(player_names[1 - iLocalRoster], remoteName));
  CHECK(Players_Cars[1 - iLocalRoster] == remoteDesign);
  /* Use production initialization, including the track load, car placement,
     saved view selection and camera setup. No audio device is needed. */
  game_frame = -1;
  race_started = replaytype = 0;
  SelectedView[0] = isHost ? 1 : 3;
  DeathView[0] = DeathView[1] = -1;
  game_track = 3;
  game_view[0] = SelectedView[0];
  strcpy(TextExt, "ENG");
  winw = XMAX = 640;
  winh = YMAX = 400;
  w95 = 1; /* No audio device in this headless fixture. */
  play_game_init();
  CHECK(!quit_game);
  release_ticktimer();
  CHECK(player1_car == iLocalCar && ViewType[0] == iLocalCar);
  tCar *localCar = &Car[iLocalCar];
  CHECK(localCar->nCurrChunk >= 0 && localCar->nCurrChunk < TRAK_LEN);
  tData *track = &localdata[localCar->nCurrChunk];
  float camera[3] = {localCar->pos.fX - 2 * CHASE_DIST[0],
                    localCar->pos.fY, localCar->pos.fZ + PULLZ[0]};
  float actual[3] = {lastpos[0][0].pos.fX, lastpos[0][0].pos.fY,
                     lastpos[0][0].pos.fZ};
  float origin[3] = {track->pointAy[3].fX, track->pointAy[3].fY,
                     track->pointAy[3].fZ};
  for (int axis = 0; axis < 3; ++axis) {
    float expected = track->pointAy[axis].fX * camera[0] +
                     track->pointAy[axis].fY * camera[1] +
                     track->pointAy[axis].fZ * camera[2] -
                     origin[axis];
    CHECK(isfinite(actual[axis]) && fabsf(expected - actual[axis]) < 2.0f);
  }
  /* Check the rendered direction too: a correctly assigned car and seeded
     position alone cannot detect a camera facing backwards. */
  for (int frame = 0; frame < 120; ++frame) {
    calculateview(DriveView[0], ViewType[0], 0);
    float forwardDot = vk3 * track->pointAy[0].fX +
                       vk6 * track->pointAy[1].fX +
                       vk9 * track->pointAy[2].fX;
    CHECK(isfinite(forwardDot) && forwardDot > 0.8f);
  }
  /* Exercise the production clock/input/packet/simulation path beyond the
     360-frame network timeout and a full 512-entry input-buffer wrap. */
  memset(player_checks, -1, sizeof(player_checks));
  memset(player_syncs, 0, sizeof(player_syncs));
  read_check = write_check = readptr = writeptr = frame_number = 0;
  countdown = 144;
  network_limit = 360;
  network_timeout = frames;
  for (int i = 0; i < network_on; ++i) {
    net_time[i] = frames;
    player_ready[i] = 0;
  }
  active_nodes = network_error = network_sync_error = 0;
  fudge_wait = -1;
  initnearcars();
  net_loading = 1;
  frontend_on = 0;
  start_race = tick_on = racing = -1;
  screenready = -1;
  MusicCD = -1; /* Keep race_update from starting headless MIDI playback. */
  NoOfLaps = 3;
  p_eng[0] = &CarEngines.engines[Drivers_Car[iLocalCar]];
  InputResetBindings();
  userkey[2] = 30; /* Player one's accelerator, held throughout the drive. */
  keys[30] = 1;
  reset_tick_input_samples();
  claim_ticktimer(100);
  float peakSpeed = 0.0f;
  deadline = SDL_GetTicks() + 16000;
  while (game_frame < 900 && SDL_GetTicks() < deadline) {
    race_update();
    calculateview(DriveView[0], ViewType[0], 0);
    CHECK(!network_error && !network_sync_error && !net_quit);
    CHECK(player1_car == iLocalCar && ViewType[0] == iLocalCar);
    CHECK(isfinite(Car[iLocalCar].pos.fX) && isfinite(Car[iLocalCar].pos.fZ));
    if (fabsf(Car[iLocalCar].fFinalSpeed) > peakSpeed)
      peakSpeed = fabsf(Car[iLocalCar].fFinalSpeed);
    SDL_Delay(12);
  }
  release_ticktimer();
  CHECK(game_frame >= 900);
  CHECK(peakSpeed > 1.0f);
  CHECK(NetLegacyTrapViolationCount() == 0);
  printf("%s: camera on car %d, drove %d legacy ticks, peak speed %.1f, no network error\n", argv[1], iLocalCar, game_frame, peakSpeed);
  ROLLERCommsUnInitSystem();
  SDL_Quit();
  return 0;
}
