#include "frontend.h"
#include "graphics.h"
#include "3d.h"
#include "func2.h"
#include "sound.h"
#include "roller.h"
#include "car.h"
#include "moving.h"
#include "network.h"
#include "loadtrak.h"
#include "control.h"
#include "cdx.h"
#include "comms.h"
#include "rollercomms.h"
#include "menu_render.h"
#include <string.h>
#ifdef IS_WINDOWS
#include <io.h>
#define open _open
#define close _close
#else
#include <inttypes.h>
#include <unistd.h>
#define O_BINARY 0
#endif

//-------------------------------------------------------------------------------------------------

static int iLobbyActive;       // nonzero while waiting for players; 0 when lobby should exit
static int iLobbySavedScrSize;

typedef enum {
  eLOBBY_BROADCAST_NONE = 0,
  eLOBBY_BROADCAST_FADE_IN,
  eLOBBY_BROADCAST_START_RACE,
  eLOBBY_BROADCAST_LEAVE,
  eLOBBY_BROADCAST_SAME_CAR_DROP
} eLobbyBroadcastAction;

typedef enum {
  eLOBBY_POST_SYNC_NONE = 0,
  eLOBBY_POST_SYNC_DELAY,
  eLOBBY_POST_SYNC_INIT,
  eLOBBY_POST_SYNC_MASTER_RECORDS,
  eLOBBY_POST_SYNC_MASTER_SEED,
  eLOBBY_POST_SYNC_SLAVE_SEED
} eLobbyPostSyncPhase;

static eLobbyBroadcastAction eLobbyBroadcastActionCurrent;
static eLobbyPostSyncPhase eLobbyPostSyncCurrentPhase;
static int iLobbyPostSyncDelayTarget;
static int iLobbyLastRecordWaitLog;
static int iLobbyLastStartResend;
static int iLobbyLastRecordResend;
static int iLobbyRacePrepared;
static int iLobbyExitFading;
static eFrontendState eLobbyExitTarget;

static void lobby_draw_frame(void);
static void lobby_begin_exit(eFrontendState eTarget);

//-------------------------------------------------------------------------------------------------

void frontend_lobby_enter(void)
{
  iLobbySavedScrSize = scr_size;
  front_fade = 0;
  tick_on = -1;
  frontend_on = -1;
  clear_network_game();
  netCD = 0;
  cd_error = 0;
  SVGA_ON = -1;
  network_test = 1;
  init_screen();
  front_vga[0]  = (tBlockHeader *)load_picture("result.bm");
  front_vga[1]  = (tBlockHeader *)load_picture("font2.bm");
  front_vga[2]  = (tBlockHeader *)load_picture("smallcar.bm");
  front_vga[3]  = (tBlockHeader *)load_picture("tabtext.bm");
  front_vga[15] = (tBlockHeader *)load_picture("font1.bm");
  setpal("result.pal");
  {
    extern tColor palette[];
    memcpy(pal_addr, palette, 256 * sizeof(tColor));
    palette_brightness = 32;
    MenuRenderer *mr = GetMenuRenderer();
    if (mr) {
      menu_render_load_blocks(mr, 0,  front_vga[0],  palette);
      menu_render_load_blocks(mr, 1,  front_vga[1],  palette);
      menu_render_load_blocks(mr, 2,  front_vga[2],  palette);
      menu_render_load_blocks(mr, 3,  front_vga[3],  palette);
      menu_render_load_blocks(mr, 15, front_vga[15], palette);
    }
  }
  iLobbyActive = -1;
  eLobbyBroadcastActionCurrent = eLOBBY_BROADCAST_NONE;
  eLobbyPostSyncCurrentPhase = eLOBBY_POST_SYNC_NONE;
  iLobbyPostSyncDelayTarget = 0;
  iLobbyLastRecordWaitLog = -1000;
  iLobbyLastStartResend = -1000;
  iLobbyLastRecordResend = -1000;
  iLobbyRacePrepared = 0;
  iLobbyExitFading = 0;
  eLobbyExitTarget = eFRONTEND_STATE_NONE;
}

//-------------------------------------------------------------------------------------------------

static void lobby_begin_broadcast_wait(eLobbyBroadcastAction eAction,
                                       int iBroadcastMode,
                                       int iRepeatCount)
{
  eLobbyBroadcastActionCurrent = eAction;
  network_broadcast_wait_start(iBroadcastMode, iRepeatCount);
}

//-------------------------------------------------------------------------------------------------

static int lobby_update_broadcast_wait(void)
{
  if (eLobbyBroadcastActionCurrent == eLOBBY_BROADCAST_NONE)
    return 0;

  lobby_draw_frame();

  if (!network_broadcast_wait_update())
    return -1;

  switch (eLobbyBroadcastActionCurrent) {
    case eLOBBY_BROADCAST_FADE_IN:
      frames = 0;
      break;
    case eLOBBY_BROADCAST_START_RACE:
      iLobbyActive = 0;
      time_to_start = -1;
      break;
    case eLOBBY_BROADCAST_LEAVE:
      iLobbyActive = 0;
      --players_waiting;
      no_clear = -1;
      break;
    case eLOBBY_BROADCAST_SAME_CAR_DROP:
      iLobbyActive = 0;
      --players_waiting;
      break;
    default:
      break;
  }

  eLobbyBroadcastActionCurrent = eLOBBY_BROADCAST_NONE;
  return -1;
}

//-------------------------------------------------------------------------------------------------

static void lobby_begin_post_sync(void)
{
  iLobbyPostSyncDelayTarget = ticks + 18;
  iLobbyLastRecordWaitLog = -1000;
  iLobbyLastStartResend = -1000;
  iLobbyLastRecordResend = -1000;
  eLobbyPostSyncCurrentPhase = eLOBBY_POST_SYNC_DELAY;
}

//-------------------------------------------------------------------------------------------------

static void lobby_finish_post_sync(void)
{
  check_cars();
  eLobbyPostSyncCurrentPhase = eLOBBY_POST_SYNC_NONE;
  lobby_begin_exit(quit_game ? eFRONTEND_STATE_QUIT : eFRONTEND_STATE_LOADING);
}

//-------------------------------------------------------------------------------------------------

static int lobby_update_post_sync(void)
{
  if (eLobbyPostSyncCurrentPhase == eLOBBY_POST_SYNC_NONE)
    return 0;

  switch (eLobbyPostSyncCurrentPhase) {
    case eLOBBY_POST_SYNC_DELAY:
      if (iLobbyPostSyncDelayTarget > ticks)
        return -1;
      network_broadcast_wait_start(-314, 1);
      eLobbyPostSyncCurrentPhase = eLOBBY_POST_SYNC_INIT;
      return -1;

    case eLOBBY_POST_SYNC_INIT:
      if (!network_broadcast_wait_update())
        return -1;
      if (wConsoleNode == master)
        eLobbyPostSyncCurrentPhase = eLOBBY_POST_SYNC_MASTER_RECORDS;
      else
        eLobbyPostSyncCurrentPhase = eLOBBY_POST_SYNC_SLAVE_SEED;
      return -1;

    case eLOBBY_POST_SYNC_MASTER_RECORDS:
      if (received_records >= network_on) {
        network_broadcast_wait_start(-2718, 1);
        eLobbyPostSyncCurrentPhase = eLOBBY_POST_SYNC_MASTER_SEED;
        return -1;
      }
      if (frames - iLobbyLastRecordWaitLog >= 36) {
        iLobbyLastRecordWaitLog = frames;
        SDL_Log("[NET-START] master waiting for records received=%d expected=%d",
                received_records, network_on);
      }
      if (frames - iLobbyLastStartResend >= 36) {
        iLobbyLastStartResend = frames;
        TransmitInit();
      }
      CheckNewNodes();
      ROLLERCommsPumpSendQueue();
      return -1;

    case eLOBBY_POST_SYNC_MASTER_SEED:
      if (!network_broadcast_wait_update())
        return -1;
      lobby_finish_post_sync();
      return -1;

    case eLOBBY_POST_SYNC_SLAVE_SEED:
      if (received_seed) {
        lobby_finish_post_sync();
        return -1;
      }
      if (frames - iLobbyLastRecordResend >= 36) {
        iLobbyLastRecordResend = frames;
        SDL_Log("[NET-START] slave waiting for seed; resending record to master=%d", master);
        send_record_to_master(TrackLoad);
      }
      CheckNewNodes();
      ROLLERCommsPumpSendQueue();
      return -1;

    default:
      eLobbyPostSyncCurrentPhase = eLOBBY_POST_SYNC_NONE;
      return 0;
  }
}

//-------------------------------------------------------------------------------------------------

static void lobby_emit_draw(MenuRenderer *mr)
{
  int iPlayerDisplayLoop;
  int iPlayerIndex;
  int iCarSpriteYOffset;
  int iCarType;
  int iCarTypeForSprite;
  int iTextYPos;
  int iY;
  char *szCurrentPlayerName;

  menu_render_background(mr, 0);
  sprintf(buffer, "%s: %i", &language_buffer[64], players);
  menu_render_text(mr, 1, buffer, font2_ascii, font2_offsets, 16, 4, 0x8Fu, 0, pal_addr);
  sprintf(buffer, "%s: %i", &language_buffer[256], TrackLoad);
  menu_render_text(mr, 1, buffer, font2_ascii, font2_offsets, 16, 24, 0x8Fu, 0, pal_addr);

  if (game_type) {
    if ((unsigned int)game_type <= 1)
      sprintf(buffer, "%s", &language_buffer[3520]);
    else if (game_type == 2)
      sprintf(buffer, "%s", &language_buffer[3712]);
  } else {
    sprintf(buffer, "%s", &language_buffer[3648]);
  }
  menu_render_text(mr, 1, buffer, font2_ascii, font2_offsets, 200, 4, 0x8Fu, 1u, pal_addr);

  if (players_waiting == network_on) {
    if ((frames & 0xFu) < 8)
      menu_render_text(mr, 1, &language_buffer[4800], font2_ascii, font2_offsets,
                       200, 22, 0x8Fu, 1u, pal_addr);
    if (time_to_start)
      iLobbyActive = 0;
  }

  iPlayerDisplayLoop = 0;
  if (players > 0) {
    iPlayerIndex = 0;
    iY = 44;
    szCurrentPlayerName = player_names[0];
    iTextYPos = 49;
    do {
      if (player_started[iPlayerIndex] &&
          (!iPlayerDisplayLoop && (frames & 0xFu) < 8 || iPlayerDisplayLoop > 0))
        menu_render_sprite(mr, 2, 0, 13, iY, 0, pal_addr);
      sprintf(buffer, "%i", iPlayerDisplayLoop + 1);
      iCarSpriteYOffset = 22 * iPlayerDisplayLoop;
      menu_render_text(mr, 1, buffer, font2_ascii, font2_offsets, 33, iTextYPos, 0x8Fu, 0, pal_addr);
      sprintf(buffer, "%s", szCurrentPlayerName);
      menu_render_text(mr, 1, buffer, font2_ascii, font2_offsets, 85, iTextYPos, 0x8Fu, 0, pal_addr);
      iCarType = Players_Cars[iPlayerIndex];
      if (iCarType >= 0) {
        sprintf(buffer, "%s", CompanyNames[iCarType]);
        menu_render_text(mr, 1, buffer, font2_ascii, font2_offsets, 218, iTextYPos, 0x8Fu, 0, pal_addr);
        iCarTypeForSprite = Players_Cars[iPlayerIndex];
        if (iCarTypeForSprite < 8) {
          if ((textures_off & TEX_OFF_ADVANCED_CARS) != 0)
            menu_render_sprite(mr, 2, smallcars[1][iCarTypeForSprite], 165, iCarSpriteYOffset + 46, 0, pal_addr);
          else
            menu_render_sprite(mr, 2, smallcars[0][iCarTypeForSprite], 165, iCarSpriteYOffset + 46, 0, pal_addr);
        } else {
          menu_render_text(mr, 1, "CHEAT", font2_ascii, font2_offsets, 165, iTextYPos, 0x8Fu, 0, pal_addr);
        }
      } else {
        menu_render_text(mr, 1, &language_buffer[4160], font2_ascii, font2_offsets,
                         218, iTextYPos, 0x8Fu, 0, pal_addr);
      }
      ++iPlayerIndex;
      iTextYPos += 22;
      ++iPlayerDisplayLoop;
      iY += 22;
      szCurrentPlayerName += 9;
    } while (iPlayerDisplayLoop < players);
  }

  if (time_to_start)
    iLobbyActive = 0;

  show_received_mesage();
}

static void lobby_begin_exit(eFrontendState eTarget)
{
  if (iLobbyExitFading)
    return;

  iLobbyExitFading = -1;
  eLobbyExitTarget = eTarget;
  menu_render_begin_fade(GetMenuRenderer(), 0, 32);
}

static int lobby_update_exit_fade(void)
{
  if (!iLobbyExitFading)
    return 0;

  lobby_draw_frame();
  if (!menu_render_fade_active(GetMenuRenderer())) {
    iLobbyExitFading = 0;
    eFrontendNextState = eLobbyExitTarget;
    eLobbyExitTarget = eFRONTEND_STATE_NONE;
  }
  return -1;
}

static void lobby_draw_frame(void)
{
  MenuRenderer *mr = GetMenuRenderer();

  menu_render_begin_frame(mr);
  lobby_emit_draw(mr);
  menu_render_end_frame(mr);

  if (iLobbyActive) {
    if (!front_fade) {
      front_fade = -1;
      menu_render_begin_fade(mr, 1, 32);
      lobby_begin_broadcast_wait(eLOBBY_BROADCAST_FADE_IN, -668, 2);
    }
  }
}

//-------------------------------------------------------------------------------------------------

static void lobby_handle_input(void)
{
  unsigned int uiKeyPressed;

  while (fatkbhit()) {
    uiKeyPressed = fatgetch();
    if (uiKeyPressed < 0xD) {
      if (!uiKeyPressed)
        fatgetch();
    } else if (uiKeyPressed <= 0xD) {
      if (players_waiting == network_on && !time_to_start) {
        lobby_begin_broadcast_wait(eLOBBY_BROADCAST_START_RACE, -671, 3);
        return;
      }
    } else if (uiKeyPressed == 27 && !time_to_start && !restart_net) {
      StartPressed = 0;
      time_to_start = 0;
      lobby_begin_broadcast_wait(eLOBBY_BROADCAST_LEAVE, -670, 1);
      return;
    }
  }
}

//-------------------------------------------------------------------------------------------------

void frontend_lobby_update(void)
{
  if (lobby_update_exit_fade())
    return;

  if (lobby_update_broadcast_wait())
    return;

  if (lobby_update_post_sync())
    return;

  // Handle game type switching from master
  if (switch_types) {
    game_type = switch_types - 1;
    if (switch_types == 1 && competitors == 1)
      competitors = 16;
    switch_types = 0;
    if (game_type == 1)
      Race = ((uint8)TrackLoad - 1) & 7;
    else
      network_champ_on = 0;
  }

  // Handle same-car mode changes from master
  if (switch_same > 0) {
    for (int i = 0; i < players; i++)
      Players_Cars[i] = switch_same - 666;
    cheat_mode |= CHEAT_MODE_CLONES;
    if (Players_Cars[player1_car] < 0) {
      StartPressed = 0;
      time_to_start = 0;
      lobby_begin_broadcast_wait(eLOBBY_BROADCAST_SAME_CAR_DROP, -670, 1);
      return;
    }
  } else if (switch_same < 0) {
    for (int i = 0; i < players; i++)
      Players_Cars[i] = -1;
    cheat_mode &= ~CHEAT_MODE_CLONES;
    switch_same = 0;
    StartPressed = 0;
    time_to_start = 0;
    lobby_begin_broadcast_wait(eLOBBY_BROADCAST_SAME_CAR_DROP, -670, 1);
    return;
  }

  check_cars();
  lobby_draw_frame();
  lobby_handle_input();

  if (!iLobbyActive) {
    if (time_to_start) {
      if (!iLobbyRacePrepared) {
        frontend_main_menu_prepare_race_start();
        iLobbyRacePrepared = -1;
      }
      lobby_begin_post_sync();
    } else {
      lobby_begin_exit(eFRONTEND_STATE_MAIN_MENU);
    }
  }
}

//-------------------------------------------------------------------------------------------------

void frontend_lobby_exit(void)
{
  check_cars();

  palette_brightness = 0;
  for (int i = 0; i < 256; i++) {
    pal_addr[i].byR = 0;
    pal_addr[i].byB = 0;
    pal_addr[i].byG = 0;
  }

  iLobbyExitFading = 0;
  eLobbyExitTarget = eFRONTEND_STATE_NONE;
  front_fade = 0;
  fre((void **)front_vga);
  fre((void **)&front_vga[1]);
  fre((void **)&front_vga[2]);
  fre((void **)&front_vga[3]);
  fre((void **)&front_vga[15]);
  scr_size = iLobbySavedScrSize;
}
