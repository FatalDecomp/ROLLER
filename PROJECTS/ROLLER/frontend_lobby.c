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
}

//-------------------------------------------------------------------------------------------------

static void lobby_draw_frame(void)
{
  int iPlayerDisplayLoop;
  int iPlayerIndex;
  int iCarSpriteYOffset;
  int iCarType;
  int iCarTypeForSprite;
  int iTextYPos;
  int iY;
  char *szCurrentPlayerName;
  MenuRenderer *mr = GetMenuRenderer();

  menu_render_begin_frame(mr);
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

  if (iLobbyActive) {
    show_received_mesage();
    menu_render_end_frame(mr);
    if (!front_fade) {
      front_fade = -1;
      menu_render_begin_fade(mr, 1, 32);
      broadcast_mode = -668;
      while (broadcast_mode)
        UpdateSDL();
      broadcast_mode = -668;
      while (broadcast_mode)
        UpdateSDL();
      frames = 0;
    }
  }
}

//-------------------------------------------------------------------------------------------------

static void lobby_handle_input(void)
{
  unsigned int uiKeyPressed;

  while (fatkbhit()) {
    UpdateSDL();
    uiKeyPressed = fatgetch();
    if (uiKeyPressed < 0xD) {
      if (!uiKeyPressed)
        fatgetch();
    } else if (uiKeyPressed <= 0xD) {
      if (players_waiting == network_on && !time_to_start) {
        iLobbyActive = 0;
        broadcast_mode = -671;
        while (broadcast_mode)
          UpdateSDL();
        broadcast_mode = -671;
        while (broadcast_mode)
          UpdateSDL();
        broadcast_mode = -671;
        while (broadcast_mode)
          UpdateSDL();
        time_to_start = -1;
      }
    } else if (uiKeyPressed == 27 && !time_to_start && !restart_net) {
      StartPressed = 0;
      time_to_start = 0;
      broadcast_mode = -670;
      while (broadcast_mode)
        UpdateSDL();
      iLobbyActive = 0;
      --players_waiting;
      no_clear = -1;
    }
  }
}

//-------------------------------------------------------------------------------------------------

void frontend_lobby_update(void)
{
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
      broadcast_mode = -670;
      while (broadcast_mode)
        ;
      iLobbyActive = 0;
      --players_waiting;
    }
  } else if (switch_same < 0) {
    for (int i = 0; i < players; i++)
      Players_Cars[i] = -1;
    cheat_mode &= ~CHEAT_MODE_CLONES;
    switch_same = 0;
    StartPressed = 0;
    time_to_start = 0;
    broadcast_mode = -670;
    while (broadcast_mode)
      ;
    iLobbyActive = 0;
    --players_waiting;
  }

  check_cars();
  lobby_draw_frame();
  lobby_handle_input();

  if (!iLobbyActive) {
    if (time_to_start) {
      frontend_main_menu_prepare_race_start();
      eFrontendNextState = quit_game ? eFRONTEND_STATE_QUIT : eFRONTEND_STATE_LOADING;
    } else {
      eFrontendNextState = eFRONTEND_STATE_MAIN_MENU;
    }
  }
}

//-------------------------------------------------------------------------------------------------

void frontend_lobby_exit(void)
{
  // Post-lobby sync: brief timing gap then network handshake before race starts.
  // Only runs when time_to_start is set; skipped on escape or disconnect.
  int iTimer = ticks + 18;
  while (iTimer > ticks)
    ;

  if (time_to_start) {
    broadcast_mode = -314;
    while (broadcast_mode) {
      CheckNewNodes();
      BroadcastNews();
      UpdateSDL();
    }
    if (wConsoleNode == master) {
      int iLastRecordWaitLog = -1000;
      int iLastStartResend = -1000;
      while (received_records < network_on) {
        if (frames - iLastRecordWaitLog >= 36) {
          iLastRecordWaitLog = frames;
          SDL_Log("[NET-START] master waiting for records received=%d expected=%d",
                  received_records, network_on);
        }
        if (frames - iLastStartResend >= 36) {
          iLastStartResend = frames;
          TransmitInit();
        }
        CheckNewNodes();
        UpdateSDL();
      }
      broadcast_mode = -2718;
      while (broadcast_mode) {
        CheckNewNodes();
        BroadcastNews();
        UpdateSDL();
      }
    } else {
      int iLastRecordResend = -1000;
      while (!received_seed) {
        if (frames - iLastRecordResend >= 36) {
          iLastRecordResend = frames;
          SDL_Log("[NET-START] slave waiting for seed; resending record to master=%d", master);
          send_record_to_master(TrackLoad);
        }
        CheckNewNodes();
        UpdateSDL();
      }
    }
  }

  check_cars();

  {
    MenuRenderer *mr = GetMenuRenderer();
    menu_render_begin_fade(mr, 0, 32);
    menu_render_fade_wait(mr, fade_redraw_bg, mr);
    palette_brightness = 0;
    for (int i = 0; i < 256; i++) {
      pal_addr[i].byR = 0;
      pal_addr[i].byB = 0;
      pal_addr[i].byG = 0;
    }
  }

  front_fade = 0;
  fre((void **)front_vga);
  fre((void **)&front_vga[1]);
  fre((void **)&front_vga[2]);
  fre((void **)&front_vga[3]);
  fre((void **)&front_vga[15]);
  scr_size = iLobbySavedScrSize;
}
