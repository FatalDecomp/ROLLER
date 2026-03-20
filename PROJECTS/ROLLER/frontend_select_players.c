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
#include "comms.h"
#include "colision.h"
#include "rollercomms.h"
#include "menu_render.h"
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
// 3D preview viewport (GPU backend only; software backend ignores these)
#define PREVIEW_X         248
#define PREVIEW_W         300
#define PREVIEW_H         330
#define CAR_PREVIEW_Y      57
#define TRACK_PREVIEW_Y    5

//-------------------------------------------------------------------------------------------------
//00047000
void select_players()
{
  unsigned int uiSelectedPlayerType; // esi
  int iNetworkStatus; // edi
  uint32 uiCheatArrayOffset; // eax
  int iCheatPlayerLoop; // edx
  int iCheatPlayerIndex; // edx
  int iPlayerCarIndex; // ecx
  char byMenuColor1; // al
  char byMenuColor2; // al
  char byMenuColor3; // al
  char byMenuColor4; // al
  char byMenuColor5; // al
  uint8 byInputKey; // al
  uint8 byExtendedKey; // al
  int iNetworkMode; // [esp+8h] [ebp-20h]
  int iPlayerIndex; // [esp+Ch] [ebp-1Ch]
  int iY; // [esp+10h] [ebp-18h]
  int iComPortStatus; // [esp+14h] [ebp-14h]
  char *szText; // [esp+18h] [ebp-10h]
  int iPlayerListCount; // [esp+1Ch] [ebp-Ch]
  int iNetworkSetupFlag; // [esp+20h] [ebp-8h]
  int iExitFlag; // [esp+24h] [ebp-4h]

  iComPortStatus = 0;// gss16550(2);                 // Initialize COM port status and screen fade
  iExitFlag = 0;
  fade_palette(0);
  uiSelectedPlayerType = player_type;
  front_fade = 0;
  // Restore palette for GPU rendering
  {
    extern tColor palette[];
    memcpy(pal_addr, palette, 256 * sizeof(tColor));
    palette_brightness = 32;
  }
  if (player_type == 1 && net_type)           // Map network types to player selection modes: Serial=3, Modem=4
  {
    if ((unsigned int)net_type <= 1) {
      uiSelectedPlayerType = 3;
    } else if (net_type == 2) {
      uiSelectedPlayerType = 4;
    }
  }
  iNetworkSetupFlag = 0;
  if (uiSelectedPlayerType == 1 || uiSelectedPlayerType == 3 || uiSelectedPlayerType == 4)// Set network mode flag: -1 for single player and network modes, 0 for two-player
    iNetworkMode = -1;
  else
    iNetworkMode = 0;
  iNetworkStatus = 0;
  do {                                             // Handle game type switches (race type, championship, etc.)
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
    {                                           // RENDER FRAME (GPU)
    MenuRenderer *mr = GetMenuRenderer();
    menu_render_begin_frame(mr);
    if (!front_fade) {
      front_fade = -1;
      menu_render_begin_fade(mr, 1, 32);
    }
    menu_render_background(mr, 0);
    menu_render_sprite(mr, 1, 3, head_x, head_y, 0, pal_addr);
    menu_render_sprite(mr, 6, 0, 36, 2, 0, pal_addr);
    menu_render_sprite(mr, 5, uiSelectedPlayerType, -4, 247, 0, pal_addr);
    menu_render_sprite(mr, 5, game_type + 5, 135, 247, 0, pal_addr);
    menu_render_sprite(mr, 4, 4, 76, 257, -1, pal_addr);
    menu_render_sprite(mr, 6, 4, 62, 336, -1, pal_addr);
    if (iNetworkStatus && (uiSelectedPlayerType == 1 || uiSelectedPlayerType == 3 || uiSelectedPlayerType == 4))// Show connection status message for network modes
      menu_render_scaled_text(mr, 15, &language_buffer[4992], font1_ascii, font1_offsets, 400, 300, 231, 1u, 200, 640, pal_addr);
    if ((uiSelectedPlayerType == 3 || uiSelectedPlayerType == 4) && !iComPortStatus)// Show COM port error message if networking failed to initialize
      menu_render_scaled_text(mr, 15, &language_buffer[8064], font1_ascii, font1_offsets, 400, 300, 231, 1u, 200, 640, pal_addr);
    do {
      uiCheatArrayOffset = broadcast_mode;
      UpdateSDL();
    } while (broadcast_mode);
    if (switch_same > 0)                      // CHEAT MODE HANDLING: Process switch_same command for car synchronization
    {
      for (iCheatPlayerIndex = 0; iCheatPlayerIndex < players; ++iCheatPlayerIndex)
      {
        Players_Cars[iCheatPlayerIndex] = switch_same - 666;
      }
      //for (iCheatPlayerIndex = 0;
      //      iCheatPlayerIndex < players;
      //      *(int *)((char *)&infinite_laps + uiCheatArrayOffset) = switch_same - 666) {
      //  uiCheatArrayOffset += 4;
      //  ++iCheatPlayerIndex;
      //}
      if ((cheat_mode & 0x4000) == 0)
        broadcast_mode = -1;
      while (broadcast_mode)
        UpdateSDL();
      cheat_mode |= CHEAT_MODE_CLONES;
    } else if (switch_same < 0) {
      switch_same = broadcast_mode;
      for (iCheatPlayerLoop = 0; iCheatPlayerLoop < players; ++iCheatPlayerLoop)
      {
        Players_Cars[iCheatPlayerLoop] = -1;
      }
      //for (iCheatPlayerLoop = 0; iCheatPlayerLoop < players; *(int *)((char *)&infinite_laps + uiCheatArrayOffset) = -1) {
      //  uiCheatArrayOffset += 4;
      //  ++iCheatPlayerLoop;
      //}
      cheat_mode &= ~CHEAT_MODE_CLONES;
    }
    if (iNetworkMode)                         // NETWORK MODE UI: Show connection info and player list
    {
      if (iNetworkSetupFlag) {
        while (broadcast_mode)
          UpdateSDL();
        broadcast_mode = -667;
        while (broadcast_mode)
          UpdateSDL();
        iNetworkSetupFlag = 0;
      }
      if (net_type) {
        if ((unsigned int)net_type <= 1) {
          menu_render_scaled_text(mr, 15, &language_buffer[5056], font1_ascii, font1_offsets, 400, 60, 143, 1u, 200, 640, pal_addr);
        } else if (net_type == 2) {
          menu_render_scaled_text(mr, 15, &language_buffer[5120], font1_ascii, font1_offsets, 400, 60, 143, 1u, 200, 640, pal_addr);
        }
      } else {
        menu_render_scaled_text(mr, 15, &language_buffer[4096], font1_ascii, font1_offsets, 400, 60, 143, 1u, 200, 640, pal_addr);
      }
      iPlayerListCount = 0;
      if (network_on > 0)                     // Display connected players and their selected cars
      {
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

          UpdateSDL();
        } while (iPlayerListCount < network_on);
      }
      if (net_type) {
        if ((unsigned int)net_type <= 1) {
          menu_render_scaled_text(mr, 15, &language_buffer[5184], font1_ascii, font1_offsets, 400, 380, 231, 1u, 200, 640, pal_addr);
        } else if (net_type == 2) {
          menu_render_scaled_text(mr, 15, &language_buffer[5248], font1_ascii, font1_offsets, 400, 380, 231, 1u, 200, 640, pal_addr);
        }
      } else {
        menu_render_scaled_text(mr, 15, &language_buffer[4224], font1_ascii, font1_offsets, 400, 380, 231, 1u, 200, 640, pal_addr);
      }
      menu_render_scaled_text(mr, 15, &language_buffer[7104], font1_ascii, font1_offsets, 400, 360, 231, 1u, 200, 640, pal_addr);
    } else {
      menu_render_scaled_text(mr, 15, &language_buffer[2944], font1_ascii, font1_offsets, 400, 75, 143, 1u, 200, 640, pal_addr);// MENU MODE UI: Show player selection options with highlighting
      menu_render_scaled_text(mr, 15, &language_buffer[3008], font1_ascii, font1_offsets, 400, 93, 143, 1u, 200, 640, pal_addr);
      if (uiSelectedPlayerType)               // Highlight current selection
        byMenuColor1 = 0x8F;
      else
        byMenuColor1 = 0xAB;
      menu_render_scaled_text(mr, 15, &language_buffer[2112], font1_ascii, font1_offsets, 400, 135, byMenuColor1, 1u, 200, 640, pal_addr);
      if (uiSelectedPlayerType == 2)
        byMenuColor2 = 0xAB;
      else
        byMenuColor2 = 0x8F;
      menu_render_scaled_text(mr, 15, &language_buffer[2240], font1_ascii, font1_offsets, 400, 153, byMenuColor2, 1u, 200, 640, pal_addr);
      if (uiSelectedPlayerType == 1)
        byMenuColor3 = 0xAB;
      else
        byMenuColor3 = 0x8F;
      menu_render_scaled_text(mr, 15, &language_buffer[2176], font1_ascii, font1_offsets, 400, 171, byMenuColor3, 1u, 200, 640, pal_addr);
      if (uiSelectedPlayerType == 3)
        byMenuColor4 = 0xAB;
      else
        byMenuColor4 = 0x8F;
      menu_render_scaled_text(mr, 15, &language_buffer[2304], font1_ascii, font1_offsets, 400, 189, byMenuColor4, 1u, 200, 640, pal_addr);
      if (uiSelectedPlayerType == 4)
        byMenuColor5 = 0xAB;
      else
        byMenuColor5 = 0x8F;
      menu_render_scaled_text(mr, 15, &language_buffer[2368], font1_ascii, font1_offsets, 400, 207, byMenuColor5, 1u, 200, 640, pal_addr);
    }
    show_received_mesage();
    menu_render_end_frame(mr);
    }                                         // end RENDER FRAME (GPU)
    while (fatkbhit())                        // KEYBOARD INPUT PROCESSING: Handle navigation and selection
    {
      byInputKey = fatgetch();
      if (byInputKey < 0x4Du) {
        if (byInputKey < 0xDu) {                                       // Handle extended keys (arrow keys for navigation)
          if (!byInputKey) {
            byExtendedKey = fatgetch();
            if (byExtendedKey >= 0x48u) {
              if (byExtendedKey <= 0x48u) {                                 // Up arrow: Navigate through player selection options
                if (!iNetworkMode) {
                  switch (uiSelectedPlayerType) {
                    case 1u:
                      uiSelectedPlayerType = 2;
                      iNetworkStatus = 0;
                      break;
                    case 2u:
                      uiSelectedPlayerType = 0;
                      iNetworkStatus = 0;
                      break;
                    case 3u:
                      uiSelectedPlayerType = 1;
                      iNetworkStatus = 0;
                      break;
                    case 4u:
                      uiSelectedPlayerType = 3;
                      iNetworkStatus = 0;
                      break;
                    default:
                      continue;
                  }
                }
              } else if (byExtendedKey == 80 && !iNetworkMode)// Down arrow: Navigate through player selection options
              {
                switch (uiSelectedPlayerType) {
                  case 0u:
                    uiSelectedPlayerType = 2;
                    iNetworkStatus = 0;
                    break;
                  case 1u:
                    uiSelectedPlayerType = 3;
                    iNetworkStatus = 0;
                    break;
                  case 2u:
                    uiSelectedPlayerType = 1;
                    iNetworkStatus = 0;
                    break;
                  case 3u:
                    uiSelectedPlayerType = 4;
                    iNetworkStatus = 0;
                    break;
                  default:
                    continue;
                }
              }
            }
          }
        } else if (byInputKey <= 0xDu || byInputKey == 27)// Enter/Escape: Confirm selection or exit menu
        {
          switch (uiSelectedPlayerType) {
            case 0u:
            case 2u:
              goto LABEL_128;
            case 1u:
            case 3u:
            case 4u:
              if (uiSelectedPlayerType != 1 && !iComPortStatus)
                continue;
              if (uiSelectedPlayerType == 1)
                net_type = 0;
              if (uiSelectedPlayerType == 3)
                net_type = 1;
              if (uiSelectedPlayerType == 4)
                net_type = 2;
              ROLLERCommsSetType(net_type);
              if (iNetworkMode) {
              LABEL_128:
                iExitFlag = -1;
                continue;
              }
              if (iNetworkStatus)
                goto LABEL_159;
              if (net_type)                   // NETWORK SETUP: Initialize communication for selected network type
              {
                if ((unsigned int)net_type <= 1) {
                  if (select_comport(uiSelectedPlayerType))
                    goto LABEL_153;
                } else {
                  if (net_type != 2)
                    goto LABEL_156;
                  if (select_modemstuff(uiSelectedPlayerType)) {
                  LABEL_153:
                    network_slot = 0;
                    goto LABEL_156;
                  }
                }
                network_on = 0;
                network_slot = -1;
              } else {
                network_slot = select_netslot();// IPX network: Select network slot and handle connection
                if (network_slot >= 0) {
                  broadcast_mode = -1;
                  while (broadcast_mode)
                    UpdateSDL();
                } else if (network_slot == -2) {
                  broadcast_mode = -666;
                  while (broadcast_mode)
                    UpdateSDL();
                  close_network();
                } else {
                  iNetworkStatus = -1;
                }
              }
            LABEL_156:
              if (uiSelectedPlayerType == 3 && network_slot >= 0)
                Initialise_Network(0);
            LABEL_159:
              if (network_on) {
                iNetworkSetupFlag = -1;
                iNetworkMode = -1;
              }
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
        if (byInputKey == 113) {                                       // Q/q keys: Quit network and return to player selection
        LABEL_121:
          if (network_on) {
            broadcast_mode = -666;
            while (broadcast_mode)
              UpdateSDL();
            frames = 0;
            while (frames < 3)
              ;
            close_network();
            iNetworkMode = 0;
            uiSelectedPlayerType = 0;
          }
        }
      }
      UpdateSDL();
    }
    UpdateSDL();
  } while (!iExitFlag);                         // MAIN SELECTION LOOP - Handle UI rendering and input processing
  if (uiSelectedPlayerType < 3)               // CLEANUP: Set final player type and network settings based on selection
  {
    if (uiSelectedPlayerType != 1)
      goto LABEL_169;
    player_type = 1;
    net_type = 0;
  } else if (uiSelectedPlayerType <= 3) {
    player_type = 1;
    net_type = 1;
  } else if (uiSelectedPlayerType == 4) {
    player_type = 1;
    net_type = 2;
  } else {
  LABEL_169:
    player_type = uiSelectedPlayerType;
  }
  // GPU fade-out
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
}

//-------------------------------------------------------------------------------------------------
