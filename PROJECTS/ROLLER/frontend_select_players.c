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
#include "drawtrk3.h"
#include "cdx.h"
#include "polytex.h"
#include "colision.h"
#include "rollercomms.h"
#include "menu_render.h"
#include "snapshot.h"
#include <fcntl.h>
#include <string.h>
#ifdef IS_WINDOWS
#include <io.h>
#define open _open
#define close _close
#else
#include <inttypes.h>
#include <unistd.h>
#define O_BINARY 0 //linux does not differentiate between text and binary
#endif
//-------------------------------------------------------------------------------------------------

static unsigned int iFrontendPlayersSelectedPlayerType = 0;
static int iFrontendPlayersNetworkStatus = 0;
static int iFrontendPlayersNetworkMode = 0;
static int iFrontendPlayersNetworkSetupFlag = 0;
static int iFrontendPlayersExitFlag = 0;
static int iFrontendPlayersExitFading = 0;
static int iFrontendPlayersNetSlotPhase = 0;
static int iFrontendPlayersNetSlotCurrent = 0;
static int iFrontendPlayersBroadcastWaitAction = 0;
static int iFrontendPlayersCloseNetworkStartFrame = 0;
static int iFrontendPlayersCloseNetworkPending = 0;

enum {
  ePLAYERS_NET_SLOT_NONE = 0,
  ePLAYERS_NET_SLOT_INIT,
  ePLAYERS_NET_SLOT_SELECT,
  ePLAYERS_NET_SLOT_JOIN_WAIT,
  ePLAYERS_NET_SLOT_LEAVE_WAIT,
  ePLAYERS_NET_SLOT_SETUP_WAIT
};

enum {
  ePLAYERS_BROADCAST_WAIT_NONE = 0,
  ePLAYERS_BROADCAST_WAIT_NETWORK_SETUP,
  ePLAYERS_BROADCAST_WAIT_CLOSE_NETWORK,
  ePLAYERS_BROADCAST_WAIT_SAME_CAR
};

//-------------------------------------------------------------------------------------------------

static void frontend_players_select_run_snapshot(void);

//-------------------------------------------------------------------------------------------------

static void frontend_players_select_handle_mouse(void)
{
  int iHovered;
  int iClicked;

  frontend_mouse_take_wheel_y();

  if (iFrontendPlayersNetworkMode) {
    (void)frontend_mouse_take_hovered_id();
    (void)frontend_mouse_consume_click();
    return;
  }

  iHovered = frontend_mouse_take_hovered_id();
  if (iHovered >= 0 && iHovered <= 2) {
    iFrontendPlayersSelectedPlayerType = iHovered;
    iFrontendPlayersNetworkStatus = 0;
  }

  iClicked = frontend_mouse_consume_click();
  if (iClicked >= 0 && iClicked <= 2) {
    iFrontendPlayersSelectedPlayerType = iClicked;
    iFrontendPlayersNetworkStatus = 0;
    frontend_mouse_press_accept();
  }
}

//-------------------------------------------------------------------------------------------------

void snapshot_render_menu_select_players(void)
{
  snapshot_setup_frontend_menu_state(0);
  player_type = 2;
  players = 2;
  frontend_players_select_run_snapshot();
}

//-------------------------------------------------------------------------------------------------

static void frontend_players_select_begin_broadcast_wait(int iBroadcastMode,
                                                         int iAction)
{
  iFrontendPlayersBroadcastWaitAction = iAction;
  network_broadcast_wait_start(iBroadcastMode, 1);
}

//-------------------------------------------------------------------------------------------------

static void frontend_players_select_finish_broadcast_wait(void)
{
  int iAction = iFrontendPlayersBroadcastWaitAction;
  iFrontendPlayersBroadcastWaitAction = ePLAYERS_BROADCAST_WAIT_NONE;

  switch (iAction) {
    case ePLAYERS_BROADCAST_WAIT_NETWORK_SETUP:
      iFrontendPlayersNetworkSetupFlag = 0;
      break;
    case ePLAYERS_BROADCAST_WAIT_CLOSE_NETWORK:
      iFrontendPlayersCloseNetworkStartFrame = frames;
      iFrontendPlayersCloseNetworkPending = -1;
      break;
    default:
      break;
  }
}

//-------------------------------------------------------------------------------------------------

static int frontend_players_select_update_broadcast_wait(void)
{
  if (iFrontendPlayersCloseNetworkPending) {
    if ((uint16)(frames - iFrontendPlayersCloseNetworkStartFrame) < 3)
      return -1;

    iFrontendPlayersCloseNetworkPending = 0;
    iFrontendPlayersCloseNetworkStartFrame = 0;
    close_network();
    iFrontendPlayersNetworkMode = 0;
    iFrontendPlayersSelectedPlayerType = 0;
    return -1;
  }

  if (!network_broadcast_wait_active())
    return 0;

  if (network_broadcast_wait_update())
    frontend_players_select_finish_broadcast_wait();

  return -1;
}

//-------------------------------------------------------------------------------------------------

static void frontend_players_select_black_palette(void)
{
  palette_brightness = 0;
  for (int i = 0; i < 256; i++) {
    pal_addr[i].byR = 0;
    pal_addr[i].byB = 0;
    pal_addr[i].byG = 0;
  }
}

//-------------------------------------------------------------------------------------------------

static void frontend_players_select_clamp_selection(void)
{
  if (iFrontendPlayersSelectedPlayerType != 0 &&
      iFrontendPlayersSelectedPlayerType != 1 &&
      iFrontendPlayersSelectedPlayerType != 2)
    iFrontendPlayersSelectedPlayerType = 1;
}

//-------------------------------------------------------------------------------------------------

static void frontend_players_select_add_cached_slot_nodes(int iSlot)
{
  if (iSlot < 0 || iSlot >= 4)
    return;

  int iCachedNodes = gamers_playing[iSlot];
  if (iCachedNodes <= 0 || iCachedNodes > 16)
    return;

  for (int i = 0; i < iCachedNodes; ++i)
    ROLLERCommsAddNode(&gamers_address[iSlot][i]);

  ROLLERCommsSortNodes();
}

//-------------------------------------------------------------------------------------------------

static void frontend_players_select_finish_network_setup(void)
{
  iFrontendPlayersNetSlotPhase = ePLAYERS_NET_SLOT_NONE;
  iFrontendPlayersNetworkSetupFlag = 0;
  if (network_on) {
    iFrontendPlayersNetworkMode = -1;
    iFrontendPlayersNetworkStatus = 0;
  } else {
    iFrontendPlayersNetworkMode = 0;
    iFrontendPlayersNetworkStatus = -1;
  }
}

//-------------------------------------------------------------------------------------------------

static void frontend_players_select_begin_net_slot(void)
{
  network_slot = -1;
  iFrontendPlayersNetSlotCurrent = 0;
  iFrontendPlayersNetworkStatus = 0;
  network_initialise_begin(-1);

  if (network_initialise_active()) {
    iFrontendPlayersNetSlotPhase = ePLAYERS_NET_SLOT_INIT;
    return;
  }

  if (network_on) {
    iFrontendPlayersNetSlotPhase = ePLAYERS_NET_SLOT_SELECT;
  } else {
    iFrontendPlayersNetSlotPhase = ePLAYERS_NET_SLOT_NONE;
    iFrontendPlayersNetworkStatus = -1;
  }
}

//-------------------------------------------------------------------------------------------------

static uint8 frontend_players_net_slot_color(int iSlot)
{
  if (gamers_playing[iSlot] == 16)
    return iFrontendPlayersNetSlotCurrent == iSlot ? 0xA8 : 0x7F;

  return iFrontendPlayersNetSlotCurrent == iSlot ? 0xAB : 0x83;
}

//-------------------------------------------------------------------------------------------------

static void frontend_players_net_slot_draw_slot(MenuRenderer *mr,
                                                int iSlot,
                                                int x,
                                                int iClipLeft,
                                                int iClipRight)
{
  uint8 byColor = frontend_players_net_slot_color(iSlot);

  sprintf(buffer, "%s%i", &language_buffer[7808], iSlot + 1);
  menu_render_scaled_text(mr, 15, buffer, font1_ascii, font1_offsets,
                          x, 92, byColor, 1u, iClipLeft, iClipRight,
                          pal_addr);

  if (gamers_playing[iSlot] == -2) {
    menu_render_scaled_text(mr, 15, &language_buffer[8000], font1_ascii,
                            font1_offsets, x, 200, byColor, 1u,
                            iClipLeft, iClipRight, pal_addr);
    return;
  }
  if (gamers_playing[iSlot] <= 0) {
    menu_render_scaled_text(mr, 15, &language_buffer[7872], font1_ascii,
                            font1_offsets, x, 200, byColor, 1u,
                            iClipLeft, iClipRight, pal_addr);
    return;
  }
  if (gamers_playing[iSlot] == 16) {
    menu_render_scaled_text(mr, 15, &language_buffer[7936], font1_ascii,
                            font1_offsets, x, 200, byColor, 1u,
                            iClipLeft, iClipRight, pal_addr);
    return;
  }

  char *szSlotNames = gamers_names[iSlot];
  int iY = 110;
  for (int i = 0; i < gamers_playing[iSlot]; ++i) {
    menu_render_scaled_text(mr, 15, szSlotNames, font1_ascii, font1_offsets,
                            x, iY, byColor, 1u, iClipLeft, iClipRight,
                            pal_addr);
    szSlotNames += 9;
    iY += 18;
  }
}

//-------------------------------------------------------------------------------------------------

static void frontend_players_net_slot_draw(void)
{
  MenuRenderer *mr = GetMenuRenderer();

  menu_render_begin_frame(mr);
  if (!front_fade) {
    front_fade = -1;
    menu_render_begin_fade(mr, 1, 32);
  }
  menu_render_background(mr, 0);
  menu_render_sprite(mr, 1, 3, head_x, head_y, 0, pal_addr);
  menu_render_sprite(mr, 6, 0, 36, 2, 0, pal_addr);
  menu_render_sprite(mr, 5, 1, -4, 247, 0, pal_addr);
  menu_render_sprite(mr, 5, game_type + 5, 135, 247, 0, pal_addr);
  menu_render_sprite(mr, 4, 4, 76, 257, -1, pal_addr);
  menu_render_sprite(mr, 6, 4, 62, 336, -1, pal_addr);
  menu_render_scaled_text(mr, 15, &language_buffer[6528], font1_ascii,
                          font1_offsets, 400, 55, 143, 1u, 200, 640,
                          pal_addr);
  menu_render_scaled_text(mr, 15, &language_buffer[6592], font1_ascii,
                          font1_offsets, 400, 73, 143, 1u, 200, 640,
                          pal_addr);

  frontend_players_net_slot_draw_slot(mr, 0, 260, 200, 320);
  frontend_players_net_slot_draw_slot(mr, 1, 370, 321, 419);
  frontend_players_net_slot_draw_slot(mr, 2, 474, 421, 519);
  frontend_players_net_slot_draw_slot(mr, 3, 580, 521, 639);

  show_received_mesage();
  menu_render_end_frame(mr);
}

//-------------------------------------------------------------------------------------------------

static int frontend_players_net_slot_move_left(int iSlot)
{
  int iPrevSlot = iSlot - 1;

  while (iPrevSlot > 0 && gamers_playing[iPrevSlot] == 16)
    --iPrevSlot;

  if (iPrevSlot >= 0 && gamers_playing[iPrevSlot] < 16)
    return iPrevSlot;

  return iSlot;
}

//-------------------------------------------------------------------------------------------------

static int frontend_players_net_slot_move_right(int iSlot)
{
  int iNextSlot = iSlot + 1;

  while (iNextSlot < 3 && gamers_playing[iNextSlot] == 16)
    ++iNextSlot;

  if (iNextSlot < 4 && gamers_playing[iNextSlot] < 16)
    return iNextSlot;

  return iSlot;
}

//-------------------------------------------------------------------------------------------------

static void frontend_players_net_slot_handle_input(void)
{
  while (fatkbhit()) {
    unsigned int uiKeyCode = fatgetch();
    if (uiKeyCode < 0xD) {
      if (!uiKeyCode) {
        unsigned int uiExtendedKey = fatgetch();
        if (uiExtendedKey == WHIP_SCANCODE_LEFT && iFrontendPlayersNetSlotCurrent > 0) {
          iFrontendPlayersNetSlotCurrent =
              frontend_players_net_slot_move_left(iFrontendPlayersNetSlotCurrent);
        } else if (uiExtendedKey == WHIP_SCANCODE_RIGHT &&
                   iFrontendPlayersNetSlotCurrent < 3) {
          iFrontendPlayersNetSlotCurrent =
              frontend_players_net_slot_move_right(iFrontendPlayersNetSlotCurrent);
        }
      }
    } else if (uiKeyCode <= 0xD) {
      if ((unsigned int)gamers_playing[iFrontendPlayersNetSlotCurrent] < 16) {
        frontend_players_select_add_cached_slot_nodes(iFrontendPlayersNetSlotCurrent);
        network_slot = iFrontendPlayersNetSlotCurrent + 1;
        network_broadcast_wait_start(-1, 1);
        iFrontendPlayersNetSlotPhase = ePLAYERS_NET_SLOT_JOIN_WAIT;
      }
    } else if (uiKeyCode == 27) {
      network_broadcast_wait_start(-666, 1);
      iFrontendPlayersNetSlotPhase = ePLAYERS_NET_SLOT_LEAVE_WAIT;
    }
  }
}

//-------------------------------------------------------------------------------------------------

static int frontend_players_net_slot_update(void)
{
  if (iFrontendPlayersNetSlotPhase == ePLAYERS_NET_SLOT_NONE)
    return 0;

  if (iFrontendPlayersNetSlotPhase == ePLAYERS_NET_SLOT_INIT) {
    frontend_players_net_slot_draw();
    if (!network_initialise_update())
      return -1;
    if (network_on) {
      iFrontendPlayersNetSlotPhase = ePLAYERS_NET_SLOT_SELECT;
    } else {
      iFrontendPlayersNetSlotPhase = ePLAYERS_NET_SLOT_NONE;
      iFrontendPlayersNetworkStatus = -1;
    }
    return -1;
  }

  if (iFrontendPlayersNetSlotPhase == ePLAYERS_NET_SLOT_JOIN_WAIT) {
    frontend_players_net_slot_draw();
    if (!network_broadcast_wait_update())
      return -1;
    network_broadcast_wait_start(-667, 1);
    iFrontendPlayersNetSlotPhase = ePLAYERS_NET_SLOT_SETUP_WAIT;
    return -1;
  }

  if (iFrontendPlayersNetSlotPhase == ePLAYERS_NET_SLOT_SETUP_WAIT) {
    frontend_players_net_slot_draw();
    if (!network_broadcast_wait_update())
      return -1;
    frontend_players_select_finish_network_setup();
    return -1;
  }

  if (iFrontendPlayersNetSlotPhase == ePLAYERS_NET_SLOT_LEAVE_WAIT) {
    frontend_players_net_slot_draw();
    if (!network_broadcast_wait_update())
      return -1;
    close_network();
    iFrontendPlayersNetSlotPhase = ePLAYERS_NET_SLOT_NONE;
    iFrontendPlayersNetworkMode = 0;
    iFrontendPlayersNetworkStatus = 0;
    return -1;
  }

  if (network_on) {
    CheckNewNodes();
    BroadcastNews();
    ROLLERCommsPumpSendQueue();
  }
  frontend_players_net_slot_draw();
  if (!SnapshotShouldStop())
    frontend_players_net_slot_handle_input();
  return -1;
}

//-------------------------------------------------------------------------------------------------

static void frontend_players_select_request_exit(void)
{
  iFrontendPlayersExitFlag = -1;
  if (eFrontendCurrentState == eFRONTEND_STATE_PLAYERS_SELECT) {
    MenuRenderer *mr = GetMenuRenderer();
    menu_render_begin_fade(mr, 0, 32);
    iFrontendPlayersExitFading = 1;
  }
}

//-------------------------------------------------------------------------------------------------

void frontend_players_select_enter(void)
{
  iFrontendPlayersExitFlag = 0;
  iFrontendPlayersExitFading = 0;
  iFrontendPlayersNetSlotPhase = ePLAYERS_NET_SLOT_NONE;
  iFrontendPlayersNetSlotCurrent = 0;
  iFrontendPlayersBroadcastWaitAction = ePLAYERS_BROADCAST_WAIT_NONE;
  iFrontendPlayersCloseNetworkPending = 0;
  iFrontendPlayersCloseNetworkStartFrame = 0;
  frontend_players_select_black_palette();
  iFrontendPlayersSelectedPlayerType = player_type;
  front_fade = 0;
  {
    extern tColor palette[];
    memcpy(pal_addr, palette, 256 * sizeof(tColor));
    palette_brightness = 32;
  }
  if (player_type == 1) {
    net_type = 0;
    iFrontendPlayersSelectedPlayerType = 1;
  }
  frontend_players_select_clamp_selection();
  iFrontendPlayersNetworkSetupFlag = 0;
  if (iFrontendPlayersSelectedPlayerType == 1)
    iFrontendPlayersNetworkMode = -1;
  else
    iFrontendPlayersNetworkMode = 0;
  iFrontendPlayersNetworkStatus = 0;
}

//-------------------------------------------------------------------------------------------------

void frontend_players_select_update(void)
{
  int iCheatPlayerLoop;
  int iCheatPlayerIndex;
  int iPlayerCarIndex;
  char byMenuColor1;
  char byMenuColor2;
  char byMenuColor3;
  uint8 byInputKey;
  uint8 byExtendedKey;
  int iPlayerIndex;
  int iY;
  char *szText;
  int iPlayerListCount;

  if (select_messages_active()) {
    select_messages();
    return;
  }

  frontend_players_select_clamp_selection();

  if (switch_types) {
    game_type = switch_types - 1;
    if (switch_types == 1 && competitors == 1)
      competitors = 16;
    switch_types = 0;
    if (game_type == 1) {
      if (TrackLoad == TRACK_LOAD_COMMUNITY)
        TrackLoad = 1;
      Race = ((uint8)TrackLoad - 1) & 7;
    } else {
      network_champ_on = 0;
    }
  }

  if (frontend_players_net_slot_update())
    return;

  {                                           // RENDER FRAME (GPU)
  MenuRenderer *mr = GetMenuRenderer();
  frontend_mouse_begin_frame(640, 400);
  menu_render_begin_frame(mr);
  if (!front_fade) {
    front_fade = -1;
    menu_render_begin_fade(mr, 1, 32);
  }
  menu_render_background(mr, 0);
  menu_render_sprite(mr, 1, 3, head_x, head_y, 0, pal_addr);
  menu_render_sprite(mr, 6, 0, 36, 2, 0, pal_addr);
  menu_render_sprite(mr, 5, iFrontendPlayersSelectedPlayerType, -4, 247, 0, pal_addr);
  menu_render_sprite(mr, 5, game_type + 5, 135, 247, 0, pal_addr);
  menu_render_sprite(mr, 4, 4, 76, 257, -1, pal_addr);
  menu_render_sprite(mr, 6, 4, 62, 336, -1, pal_addr);
  if (iFrontendPlayersNetworkStatus &&
      iFrontendPlayersSelectedPlayerType == 1)
    menu_render_scaled_text(mr, 15, &language_buffer[4992], font1_ascii, font1_offsets, 400, 300, 231, 1u, 200, 640, pal_addr);
  if (switch_same > 0) {                      // CHEAT MODE HANDLING: Process switch_same command for car synchronization
    for (iCheatPlayerIndex = 0; iCheatPlayerIndex < players; ++iCheatPlayerIndex)
      Players_Cars[iCheatPlayerIndex] = switch_same - 666;
    if ((cheat_mode & 0x4000) == 0)
      frontend_players_select_begin_broadcast_wait(
          -1, ePLAYERS_BROADCAST_WAIT_SAME_CAR);
    cheat_mode |= CHEAT_MODE_CLONES;
  } else if (switch_same < 0) {
    switch_same = 0;
    for (iCheatPlayerLoop = 0; iCheatPlayerLoop < players; ++iCheatPlayerLoop)
      Players_Cars[iCheatPlayerLoop] = -1;
    cheat_mode &= ~CHEAT_MODE_CLONES;
  }
  if (iFrontendPlayersNetworkMode) {          // NETWORK MODE UI: Show connection info and player list
    if (iFrontendPlayersNetworkSetupFlag) {
      if (!network_broadcast_wait_active())
        frontend_players_select_begin_broadcast_wait(
            -667, ePLAYERS_BROADCAST_WAIT_NETWORK_SETUP);
    }
    menu_render_scaled_text(mr, 15, &language_buffer[4096], font1_ascii, font1_offsets, 400, 60, 143, 1u, 200, 640, pal_addr);
    iPlayerListCount = 0;
    if (network_on > 0) {                     // Display connected players and their selected cars
      iPlayerIndex = 0;
      iY = 80;
      szText = player_names[0];
      do {
        menu_render_scaled_text(mr, 15, szText, font1_ascii, font1_offsets, 336, iY, 143, 2u, 200, 640, pal_addr);
        iPlayerCarIndex = Players_Cars[iPlayerIndex];
        if (iPlayerCarIndex < 0)
          menu_render_scaled_text(mr, 15, &language_buffer[4160], font1_ascii, font1_offsets, 340, iY, 131, 0, 200, 640, pal_addr);
        else
          menu_render_scaled_text(mr, 15, CompanyNames[iPlayerCarIndex], font1_ascii, font1_offsets, 342, iY, 143, 0, 200, 640, pal_addr);
        ++iPlayerIndex;
        szText += 9;
        iY += 18;
        ++iPlayerListCount;

      } while (iPlayerListCount < network_on);
    }
    menu_render_scaled_text(mr, 15, &language_buffer[4224], font1_ascii, font1_offsets, 400, 380, 231, 1u, 200, 640, pal_addr);
    menu_render_scaled_text(mr, 15, &language_buffer[7104], font1_ascii, font1_offsets, 400, 360, 231, 1u, 200, 640, pal_addr);
  } else {
    menu_render_scaled_text(mr, 15, &language_buffer[2944], font1_ascii, font1_offsets, 400, 75, 143, 1u, 200, 640, pal_addr); // MENU MODE UI: Show player selection options with highlighting
    menu_render_scaled_text(mr, 15, &language_buffer[3008], font1_ascii, font1_offsets, 400, 93, 143, 1u, 200, 640, pal_addr);
    if (iFrontendPlayersSelectedPlayerType)   // Highlight current selection
      byMenuColor1 = 0x8F;
    else
      byMenuColor1 = 0xAB;
    menu_render_scaled_text(mr, 15, &language_buffer[2112], font1_ascii, font1_offsets, 400, 135, byMenuColor1, 1u, 200, 640, pal_addr);
    if (iFrontendPlayersSelectedPlayerType == 2)
      byMenuColor2 = 0xAB;
    else
      byMenuColor2 = 0x8F;
    menu_render_scaled_text(mr, 15, &language_buffer[2240], font1_ascii, font1_offsets, 400, 153, byMenuColor2, 1u, 200, 640, pal_addr);
    if (iFrontendPlayersSelectedPlayerType == 1)
      byMenuColor3 = 0xAB;
    else
      byMenuColor3 = 0x8F;
    menu_render_scaled_text(mr, 15, &language_buffer[2176], font1_ascii, font1_offsets, 400, 171, byMenuColor3, 1u, 200, 640, pal_addr);
    frontend_mouse_register_scaled_text(0, front_vga[15],
                                        &language_buffer[2112],
                                        font1_ascii, font1_offsets, 400, 135,
                                        1u, 200, 640);
    frontend_mouse_register_scaled_text(2, front_vga[15],
                                        &language_buffer[2240],
                                        font1_ascii, font1_offsets, 400, 153,
                                        1u, 200, 640);
    frontend_mouse_register_scaled_text(1, front_vga[15],
                                        &language_buffer[2176],
                                        font1_ascii, font1_offsets, 400, 171,
                                        1u, 200, 640);
  }
  show_received_mesage();
  menu_render_end_frame(mr);
  if (SnapshotShouldStop())
    return;
  }                                           // end RENDER FRAME (GPU)

  if (iFrontendPlayersExitFading) {
    MenuRenderer *mr = GetMenuRenderer();
    if (!menu_render_fade_active(mr)) {
      iFrontendPlayersExitFading = 0;
      eFrontendNextState = eFRONTEND_STATE_MAIN_MENU;
    }
    return;
  }

  if (frontend_players_select_update_broadcast_wait())
    return;

  frontend_players_select_handle_mouse();
  while (fatkbhit())                          // KEYBOARD INPUT PROCESSING: Handle navigation and selection
  {
    byInputKey = fatgetch();
    if (byInputKey < 0x4Du) {
      if (byInputKey < 0xDu) {                                       // Handle extended keys (arrow keys for navigation)
        if (!byInputKey) {
          byExtendedKey = fatgetch();
          if (byExtendedKey >= 0x48u) {
            if (byExtendedKey <= 0x48u) {                                 // Up arrow: Navigate through player selection options
              if (!iFrontendPlayersNetworkMode) {
                switch (iFrontendPlayersSelectedPlayerType) {
                  case 1u:
                    iFrontendPlayersSelectedPlayerType = 2;
                    iFrontendPlayersNetworkStatus = 0;
                    break;
                  case 2u:
                    iFrontendPlayersSelectedPlayerType = 0;
                    iFrontendPlayersNetworkStatus = 0;
                    break;
                  default:
                    continue;
                }
              }
            } else if (byExtendedKey == 80 && !iFrontendPlayersNetworkMode) // Down arrow: Navigate through player selection options
            {
              switch (iFrontendPlayersSelectedPlayerType) {
                case 0u:
                  iFrontendPlayersSelectedPlayerType = 2;
                  iFrontendPlayersNetworkStatus = 0;
                  break;
                case 1u:
                  iFrontendPlayersSelectedPlayerType = 1;
                  iFrontendPlayersNetworkStatus = 0;
                  break;
                case 2u:
                  iFrontendPlayersSelectedPlayerType = 1;
                  iFrontendPlayersNetworkStatus = 0;
                  break;
                default:
                  continue;
              }
            }
          }
        }
      } else if (byInputKey <= 0xDu || byInputKey == 27) // Enter/Escape: Confirm selection or exit menu
      {
        switch (iFrontendPlayersSelectedPlayerType) {
          case 0u:
          case 2u:
            goto LABEL_128;
          case 1u:
            net_type = 0;
            ROLLERCommsSetType(net_type);
            if (iFrontendPlayersNetworkMode) {
            LABEL_128:
              frontend_players_select_request_exit();
              continue;
            }
            if (iFrontendPlayersNetworkStatus)
              goto LABEL_159;
            frontend_players_select_begin_net_slot();
            return;
          LABEL_159:
            if (network_on)
              frontend_players_select_finish_network_setup();
            break;
          default:
            continue;
        }
      }
    } else if (byInputKey <= 0x4Du) {
    LABEL_119:
      if (network_on)
        select_messages();
    } else if (byInputKey < 0x6Du)            // M/m keys: Open message selection (network mode only)
    {
      if (byInputKey == 81)
        goto LABEL_121;
    } else {
      if (byInputKey <= 0x6Du)
        goto LABEL_119;
      if (byInputKey == 113) {                // Q/q keys: Quit network and return to player selection
      LABEL_121:
        if (network_on) {
          frontend_players_select_begin_broadcast_wait(
              -666, ePLAYERS_BROADCAST_WAIT_CLOSE_NETWORK);
          return;
        }
      }
    }
  }
}

//-------------------------------------------------------------------------------------------------

void frontend_players_select_exit(void)
{
  if (iFrontendPlayersSelectedPlayerType == 1) { // CLEANUP: Set final player type and network settings based on selection
    player_type = 1;
    net_type = 0;
  } else {
    player_type = (int)iFrontendPlayersSelectedPlayerType;
  }
  iFrontendPlayersExitFading = 0;
  if (!SnapshotShouldStop()) {
    frontend_players_select_black_palette();
  }
  front_fade = 0;

  if (eFrontendCurrentState == eFRONTEND_STATE_PLAYERS_SELECT)
    frontend_menu_resume_from_child();
}

//-------------------------------------------------------------------------------------------------

static void frontend_players_select_run_snapshot(void)
{
  frontend_players_select_enter();
  while (!iFrontendPlayersExitFlag && !SnapshotShouldStop()) {
    frontend_players_select_update();
    if (!iFrontendPlayersExitFlag && !SnapshotShouldStop())
      UpdateSDLWindow();
  }
  if (!SnapshotShouldStop())
    frontend_players_select_exit();
}

//-------------------------------------------------------------------------------------------------
