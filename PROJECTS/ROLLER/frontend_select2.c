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
//00047AE0
void select_type()
{
  int iMenuSelection; // edi
  int iCurrentOption; // esi
  char *pszTextBuffer; // edx
  char byGameModeColor1; // al
  char byGameModeColor2; // al
  char byGameModeColor3; // al
  char byDifficultyColor1; // al
  char byDifficultyColor2; // al
  char byDifficultyColor3; // al
  char byDifficultyColor4; // al
  char byDifficultyColor5; // al
  char byDifficultyColor6; // al
  char byCompetitorColor1; // al
  char byCompetitorColor2; // al
  char byCompetitorColor3; // al
  char byDamageColor1; // al
  char byDamageColor2; // al
  char byDamageColor3; // al
  char byTextureColor1; // al
  char byTextureColor2; // al
  uint32 uiUpdatedCheatFlags; // eax
  int iCheatSetLoop; // eax
  uint8 byInputKey; // al
  uint8 byExtendedKey; // al
  int iCupIncrement; // edx
  int iTrackUpperLimit; // edx
  int iTrackLowerLimit; // ebx
  int iTextYPosition; // [esp-14h] [ebp-48h]
  char byFinalTextColor; // [esp-10h] [ebp-44h]
  int iCheatModesAvailable; // [esp+0h] [ebp-34h]
  int iExitFlag; // [esp+4h] [ebp-30h]
  int iSkipColor; // [esp+8h] [ebp-2Ch]
  char byCompetitorMenuColor; // [esp+Ch] [ebp-28h]
  char byTextColor; // [esp+10h] [ebp-24h]
  char byDamageMenuColor; // [esp+14h] [ebp-20h]
  char byTextureMenuColor; // [esp+18h] [ebp-1Ch]
  char byDifficultyMenuColor; // [esp+1Ch] [ebp-18h]
  int iNetworkDisplayY; // [esp+20h] [ebp-14h]
  int iY; // [esp+24h] [ebp-10h]
  char *szText; // [esp+28h] [ebp-Ch]
  int iNetworkPlayerCount; // [esp+2Ch] [ebp-8h]
  int iBlockIdx; // [esp+30h] [ebp-4h]

  iExitFlag = 0;                                // Initialize exit flag and check game restrictions
  if (game_type == 1 && Race > 0)             // Championship mode after first race - restrict options to track selection only
    iSkipColor = -1;
  else
    iSkipColor = 0;
  if ((cheat_mode & TEX_OFF_SHADOWS) != 0 || (textures_off & TEX_OFF_CAR_SET_AVAILABLE) != 0)
    iCheatModesAvailable = -1;
  else
    iCheatModesAvailable = 0;
  iBlockIdx = (TrackLoad - 1) / 8;// Calculate cup index from current track (TrackLoad): cups are groups of 8 tracks
  front_vga[14] = (tBlockHeader *)load_picture("cupicons.bm");  // Load cup icons graphics and initialize screen
  iMenuSelection = 0;
  fade_palette(0);
  front_fade = 0;
  // Restore palette for GPU rendering
  {
    extern tColor palette[];
    memcpy(pal_addr, palette, 256 * sizeof(tColor));
    palette_brightness = 32;
    MenuRenderer *mr = GetMenuRenderer();
    if (mr) {
      menu_render_load_blocks(mr, 14, front_vga[14], palette);
    }
  }
  frames = 0;
  iCurrentOption = 5;
  do {                                             // Handle game type switches and updates
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
    menu_render_sprite(mr, 1, 4, head_x, head_y, 0, pal_addr);
    menu_render_sprite(mr, 6, 0, 36, 2, 0, pal_addr);
    menu_render_sprite(mr, 5, player_type, -4, 247, 0, pal_addr);
    menu_render_sprite(mr, 5, game_type + 5, 135, 247, 0, pal_addr);
    menu_render_sprite(mr, 4, 4, 76, 257, -1, pal_addr);
    if (cup_won && !iSkipColor)
      menu_render_sprite(mr, 1, 8, 200, 56, 0, pal_addr);
    if (iCurrentOption >= 5)                  // Draw selection cursor or \"Random\" indicator (option 5 = Random)
    {
      menu_render_sprite(mr, 6, 4, 62, 336, -1, pal_addr);
    } else {
      menu_render_sprite(mr, 6, 2, 62, 336, -1, pal_addr);
      menu_render_text(mr, 2, "~", font2_ascii, font2_offsets, sel_posns[iCurrentOption].x, sel_posns[iCurrentOption].y, 0x8Fu, 0, pal_addr);
    }
    if (iSkipColor)                           // OPTION LABELS: Display main option categories (Game Type/Difficulty/etc.)
    {
      menu_render_text(mr, 2, &language_buffer[3136], font2_ascii, font2_offsets, sel_posns[0].x + 132, sel_posns[0].y + 7, 0x8Fu, 2u, pal_addr);
    } else {
      menu_render_text(mr, 2, &language_buffer[384], font2_ascii, font2_offsets, sel_posns[0].x + 132, sel_posns[0].y + 7, 0x8Fu, 2u, pal_addr);
      menu_render_text(mr, 2, &language_buffer[3200], font2_ascii, font2_offsets, sel_posns[1].x + 132, sel_posns[1].y + 7, 0x8Fu, 2u, pal_addr);
      menu_render_text(mr, 2, &language_buffer[3264], font2_ascii, font2_offsets, sel_posns[2].x + 132, sel_posns[2].y + 7, 0x8Fu, 2u, pal_addr);
      menu_render_text(mr, 2, &language_buffer[3328], font2_ascii, font2_offsets, sel_posns[3].x + 132, sel_posns[3].y + 7, 0x8Fu, 2u, pal_addr);
      if (iCheatModesAvailable)
        menu_render_text(mr, 2, &language_buffer[4288], font2_ascii, font2_offsets, sel_posns[4].x + 132, sel_posns[4].y + 7, 0x8Fu, 2u, pal_addr);
    }
    menu_render_sprite(mr, 14, iBlockIdx, 500, 300, 0, pal_addr);// Display cup icon corresponding to current track selection
    if (iSkipColor)                           // CHAMPIONSHIP MODE UI: Show current settings and restrictions
    {
      menu_render_scaled_text(mr, 15, &language_buffer[3392], font1_ascii, font1_offsets, 400, 75, 143, 1u, 200, 640, pal_addr);
      menu_render_scaled_text(mr, 15, &language_buffer[1280], font1_ascii, font1_offsets, 400, 100, 143, 2u, 200, 640, pal_addr);
      if ((cheat_mode & 2) != 0)
        menu_render_scaled_text(mr, 15, &language_buffer[2048], font1_ascii, font1_offsets, 405, 100, 143, 0, 200, 640, pal_addr);
      else
        menu_render_scaled_text(mr, 15, &language_buffer[64 * level + 1472], font1_ascii, font1_offsets, 405, 100, 143, 0, 200, 640, pal_addr);
      menu_render_scaled_text(mr, 15, &language_buffer[1344], font1_ascii, font1_offsets, 400, 118, 143, 2u, 200, 640, pal_addr);
      if ((cheat_mode & 2) != 0)
        menu_render_scaled_text(mr, 15, &language_buffer[2048], font1_ascii, font1_offsets, 405, 118, 143, 0, 200, 640, pal_addr);
      else
        menu_render_scaled_text(mr, 15, &language_buffer[64 * damage_level + 1856], font1_ascii, font1_offsets, 405, 118, 143, 0, 200, 640, pal_addr);
      menu_render_scaled_text(mr, 15, &language_buffer[1024], font1_ascii, font1_offsets, 400, 136, 143, 2u, 200, 640, pal_addr);
      if ((unsigned int)competitors < 8) {
        if (competitors == 2)
          menu_render_scaled_text(mr, 15, &language_buffer[1088], font1_ascii, font1_offsets, 405, 136, 143, 0, 200, 640, pal_addr);
      } else if ((unsigned int)competitors <= 8) {
        menu_render_scaled_text(mr, 15, &language_buffer[1152], font1_ascii, font1_offsets, 405, 136, 143, 0, 200, 640, pal_addr);
      } else if (competitors == 16) {
        menu_render_scaled_text(mr, 15, &language_buffer[1216], font1_ascii, font1_offsets, 405, 136, 143, 0, 200, 640, pal_addr);
      }
      if (network_on)                         // NETWORK MODE: Display connected players list
      {
        menu_render_scaled_text(mr, 15, &language_buffer[4672], font1_ascii, font1_offsets, 400, 154, 143, 1u, 200, 640, pal_addr);
        iNetworkPlayerCount = 0;
        if (players > 0) {
          szText = player_names[0];
          iY = 28;
          iNetworkDisplayY = 172;
          do {
            if (iNetworkPlayerCount >= 8)
              menu_render_scaled_text(mr, 15, szText, font1_ascii, font1_offsets, 405, iY, 143, 0, 200, 640, pal_addr);
            else
              menu_render_scaled_text(mr, 15, szText, font1_ascii, font1_offsets, 400, iNetworkDisplayY, 143, 2u, 200, 640, pal_addr);
            szText += 9;
            iY += 18;
            iNetworkDisplayY += 18;
            ++iNetworkPlayerCount;

            UpdateSDL();
          } while (iNetworkPlayerCount < players);
        }
      }
    }
    switch (iCurrentOption) {
      case 0:
        if (!iSkipColor)                      // Option 0 - Game Type: Show race modes with highlighting for current selection
        {
          menu_render_scaled_text(mr, 15, &language_buffer[384], font1_ascii, font1_offsets, 400, 75, 143, 1u, 200, 640, pal_addr);
          if (iMenuSelection == 1) {
            menu_render_scaled_text(mr, 15, &language_buffer[3584], font1_ascii, font1_offsets, 400, 93, 143, 1u, 200, 640, pal_addr);
            byTextColor = -85;
          } else {
            byTextColor = -87;
          }
          if (game_type)
            byGameModeColor1 = -113;
          else
            byGameModeColor1 = byTextColor;
          menu_render_scaled_text(mr, 15, &language_buffer[3648], font1_ascii, font1_offsets, 400, 135, byGameModeColor1, 1u, 200, 640, pal_addr);
          if (game_type == 1)
            byGameModeColor2 = byTextColor;
          else
            byGameModeColor2 = -113;
          menu_render_scaled_text(mr, 15, &language_buffer[3520], font1_ascii, font1_offsets, 400, 153, byGameModeColor2, 1u, 200, 640, pal_addr);
          if (game_type == 2)
            byGameModeColor3 = byTextColor;
          else
            byGameModeColor3 = -113;
          byFinalTextColor = byGameModeColor3;
          iTextYPosition = 171;
          pszTextBuffer = &language_buffer[3712];
          goto LABEL_130;
        }
        if (iMenuSelection == 6) {
          menu_render_scaled_text(mr, 15, &language_buffer[3456], font1_ascii, font1_offsets, 400, 320, 231, 1u, 200, 640, pal_addr);
          byFinalTextColor = -25;
          iTextYPosition = 338;
          pszTextBuffer = &language_buffer[3520];
          goto LABEL_130;
        }
        break;
      case 1:
        menu_render_scaled_text(mr, 15, &language_buffer[3776], font1_ascii, font1_offsets, 400, 75, 143, 1u, 200, 640, pal_addr);// Option 1 - Difficulty: Show skill levels with cheat mode override
        if (iMenuSelection == 2) {
          menu_render_scaled_text(mr, 15, &language_buffer[3840], font1_ascii, font1_offsets, 400, 93, 143, 1u, 200, 640, pal_addr);
          byDifficultyMenuColor = -85;
        } else {
          byDifficultyMenuColor = -87;
        }
        if ((cheat_mode & 2) == 0) {
          if (level == 5)
            byDifficultyColor1 = byDifficultyMenuColor;
          else
            byDifficultyColor1 = -113;
          menu_render_scaled_text(mr, 15, &language_buffer[1792], font1_ascii, font1_offsets, 400, 135, byDifficultyColor1, 1u, 200, 640, pal_addr);
          if (level == 4)
            byDifficultyColor2 = byDifficultyMenuColor;
          else
            byDifficultyColor2 = -113;
          menu_render_scaled_text(mr, 15, &language_buffer[1728], font1_ascii, font1_offsets, 400, 153, byDifficultyColor2, 1u, 200, 640, pal_addr);
          if (level == 3)
            byDifficultyColor3 = byDifficultyMenuColor;
          else
            byDifficultyColor3 = -113;
          menu_render_scaled_text(mr, 15, &language_buffer[1664], font1_ascii, font1_offsets, 400, 171, byDifficultyColor3, 1u, 200, 640, pal_addr);
          if (level == 2)
            byDifficultyColor4 = byDifficultyMenuColor;
          else
            byDifficultyColor4 = -113;
          menu_render_scaled_text(mr, 15, &language_buffer[1600], font1_ascii, font1_offsets, 400, 189, byDifficultyColor4, 1u, 200, 640, pal_addr);
          if (level == 1)
            byDifficultyColor5 = byDifficultyMenuColor;
          else
            byDifficultyColor5 = -113;
          menu_render_scaled_text(mr, 15, &language_buffer[1536], font1_ascii, font1_offsets, 400, 207, byDifficultyColor5, 1u, 200, 640, pal_addr);
          if (level)
            byDifficultyColor6 = -113;
          else
            byDifficultyColor6 = byDifficultyMenuColor;
          byFinalTextColor = byDifficultyColor6;
          iTextYPosition = 225;
          pszTextBuffer = &language_buffer[1472];
          goto LABEL_130;
        }
        menu_render_scaled_text(mr, 15, &language_buffer[2048], font1_ascii, font1_offsets, 400, 135, byDifficultyMenuColor, 1u, 200, 640, pal_addr);
        break;
      case 2:
        menu_render_scaled_text(mr, 15, &language_buffer[3904], font1_ascii, font1_offsets, 400, 75, 143, 1u, 200, 640, pal_addr);// Option 2 - Competitors: Show number of opponent cars (2/8/16 or \"Just Me\")
        if (iMenuSelection == 3) {
          menu_render_scaled_text(mr, 15, &language_buffer[3008], font1_ascii, font1_offsets, 400, 93, 143, 1u, 200, 640, pal_addr);
          byDamageMenuColor = -85;
        } else {
          byDamageMenuColor = -87;
        }
        if (competitors != 1) {
          if (competitors == 2)
            byCompetitorColor1 = byDamageMenuColor;
          else
            byCompetitorColor1 = -113;
          menu_render_scaled_text(mr, 15, &language_buffer[1088], font1_ascii, font1_offsets, 400, 135, byCompetitorColor1, 1u, 200, 640, pal_addr);
          if (competitors == 8)
            byCompetitorColor2 = byDamageMenuColor;
          else
            byCompetitorColor2 = -113;
          menu_render_scaled_text(mr, 15, &language_buffer[1152], font1_ascii, font1_offsets, 400, 153, byCompetitorColor2, 1u, 200, 640, pal_addr);
          if (competitors == 16)
            byCompetitorColor3 = byDamageMenuColor;
          else
            byCompetitorColor3 = -113;
          byFinalTextColor = byCompetitorColor3;
          iTextYPosition = 171;
          pszTextBuffer = &language_buffer[1216];
          goto LABEL_130;
        }
        menu_render_scaled_text(mr, 15, &language_buffer[3968], font1_ascii, font1_offsets, 400, 135, byDamageMenuColor, 1u, 200, 640, pal_addr);
        break;
      case 3:
        menu_render_scaled_text(mr, 15, &language_buffer[4032], font1_ascii, font1_offsets, 400, 75, 143, 1u, 200, 640, pal_addr); // Option 3 - Damage: Show car damage levels (None/Cosmetic/Realistic)
        if (iMenuSelection == 4) {
          menu_render_scaled_text(mr, 15, &language_buffer[3840], font1_ascii, font1_offsets, 400, 93, 143, 1u, 200, 640, pal_addr);
          byTextureMenuColor = -85;
        } else {
          byTextureMenuColor = -87;
        }
        if ((cheat_mode & 2) == 0) {
          if (damage_level)
            byDamageColor1 = -113;
          else
            byDamageColor1 = byTextureMenuColor;
          menu_render_scaled_text(mr, 15, &language_buffer[1856], font1_ascii, font1_offsets, 400, 135, byDamageColor1, 1u, 200, 640, pal_addr);
          if (damage_level == 1)
            byDamageColor2 = byTextureMenuColor;
          else
            byDamageColor2 = -113;
          menu_render_scaled_text(mr, 15, &language_buffer[1920], font1_ascii, font1_offsets, 400, 153, byDamageColor2, 1u, 200, 640, pal_addr);
          if (damage_level == 2)
            byDamageColor3 = byTextureMenuColor;
          else
            byDamageColor3 = -113;
          byFinalTextColor = byDamageColor3;
          iTextYPosition = 171;
          pszTextBuffer = &language_buffer[1984];
          goto LABEL_130;
        }
        menu_render_scaled_text(mr, 15, &language_buffer[2048], font1_ascii, font1_offsets, 400, 135, byTextureMenuColor, 1u, 200, 640, pal_addr);
        break;
      case 4:
        menu_render_scaled_text(mr, 15, &language_buffer[4288], font1_ascii, font1_offsets, 400, 75, 143, 1u, 200, 640, pal_addr);// Option 4 - Textures: Show texture quality options (On/Off)
        if (iMenuSelection == 5) {
          menu_render_scaled_text(mr, 15, &language_buffer[4480], font1_ascii, font1_offsets, 400, 93, 143, 1u, 200, 640, pal_addr);
          byCompetitorMenuColor = -85;
        } else {
          byCompetitorMenuColor = -87;
        }
        if ((textures_off & TEX_OFF_ADVANCED_CARS) != 0)
          byTextureColor1 = -113;
        else
          byTextureColor1 = byCompetitorMenuColor;
        menu_render_scaled_text(mr, 15, &language_buffer[4352], font1_ascii, font1_offsets, 400, 135, byTextureColor1, 1u, 200, 640, pal_addr);
        if ((textures_off & TEX_OFF_ADVANCED_CARS) != 0)
          byTextureColor2 = byCompetitorMenuColor;
        else
          byTextureColor2 = -113;
        byFinalTextColor = byTextureColor2;
        iTextYPosition = 153;
        pszTextBuffer = &language_buffer[4416];
      LABEL_130:
        menu_render_scaled_text(mr, 15, pszTextBuffer, font1_ascii, font1_offsets, 400, iTextYPosition, byFinalTextColor, 1u, 200, 640, pal_addr);
        break;
      default:
        break;                                  // OPTION-SPECIFIC UI: Display details for currently selected option
    }
    show_received_mesage();
    menu_render_end_frame(mr);
    }
    if (switch_same > 0)                      // CHEAT MODE HANDLING: Process switch_same command for player synchronization
    {
      iCheatSetLoop = 0;
      if (players > 0) {
        // Set all players to the same car in cheat mode
        for (int i = 0; i < players; i++) {
            Players_Cars[i] = switch_same - 666;
        }
        //iCheatArrayOffset2 = 0;
        //do {
        //  iCheatArrayOffset2 += 4;
        //  ++iCheatSetLoop;
        //  *(int *)((char *)&infinite_laps + iCheatArrayOffset2) = switch_same - 666;
        //} while (iCheatSetLoop < players);
      }
      uiUpdatedCheatFlags = cheat_mode;
      uiUpdatedCheatFlags |= CHEAT_MODE_CLONES;
    } else {
      if (switch_same >= 0)
        goto LABEL_142;
      switch_same = 0;
      for (int i = 0; i < players; i++) {
        Players_Cars[i] = -1;
      }
      //iCheatResetLoop1 = 0;
      //switch_same = 0;
      //if (players > 0) {
      //  iCheatArrayOffset1 = 0;
      //  do {
      //    iCheatArrayOffset1 += 4;
      //    ++iCheatResetLoop1;
      //    *(int *)((char *)&infinite_laps + iCheatArrayOffset1) = -1;
      //  } while (iCheatResetLoop1 < players);
      //}
      uiUpdatedCheatFlags = cheat_mode;
      uiUpdatedCheatFlags &= ~CHEAT_MODE_CLONES;
    }
    cheat_mode = uiUpdatedCheatFlags;
  LABEL_142:
    ;
    while (fatkbhit())                        // KEYBOARD INPUT PROCESSING: Handle navigation and option changes
    {
      byInputKey = fatgetch();
      if (byInputKey < 0x1Bu) {
        if (byInputKey) {                                       // Enter key: Confirm selection or navigate into sub-options
          if (byInputKey == 13) {
            sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);
            if (iMenuSelection) {
              if (game_type == 1) {
                Race = 0;
                TrackLoad = 8 * iBlockIdx + 1;
              }
              iMenuSelection = 0;
              broadcast_mode = -1;
              while (broadcast_mode)
                UpdateSDL();
            } else {
              switch (iCurrentOption) {
                case 0:
                  if (iSkipColor)
                    iMenuSelection = 6;
                  else
                    iMenuSelection = 1;
                  break;
                case 1:
                  iMenuSelection = 2;
                  break;
                case 2:
                  iMenuSelection = 3;
                  break;
                case 3:
                  iMenuSelection = 4;
                  break;
                case 4:
                  iMenuSelection = 5;
                  break;
                case 5:
                  goto LABEL_248;
                default:
                  continue;
              }
            }
          }
        } else {
          byExtendedKey = fatgetch();           // Arrow keys: Navigate through options and change values
          switch (iMenuSelection) {
            case 0:
              if (byExtendedKey >= 0x48u)     // Main menu navigation: Up/Down arrows move between option categories
              {
                if (byExtendedKey <= 0x48u) {
                  if (iSkipColor) {
                    iCurrentOption = 0;
                  } else {
                    --iCurrentOption;
                    if (!iCheatModesAvailable && iCurrentOption == 4)
                      iCurrentOption = 3;
                    if (iCurrentOption < 0)
                      iCurrentOption = 0;
                  }
                } else if (byExtendedKey == 80) {
                  if (iSkipColor) {
                    iCurrentOption = 5;
                  } else {
                    if (iCheatModesAvailable) {
                      ++iCurrentOption;
                    } else if (++iCurrentOption > 3) {
                      iCurrentOption = 5;
                    }
                    if (iCurrentOption > 5)
                      iCurrentOption = 5;
                  }
                }
              }
              break;
            case 1:
              if (byExtendedKey >= 0x48u)     // Game Type option: Navigate between race modes and adjust competitors
              {
                if (byExtendedKey <= 0x48u) {
                  if (--game_type < 0)
                    game_type = 0;
                  if (competitors == 1)
                    goto LABEL_187;
                } else if (byExtendedKey == 80) {
                  if (++game_type < 2) {
                    if (competitors == 1)
                      competitors = 16;
                  } else {
                    game_type = 2;
                    competitors = 1;
                  }
                }
              }
              break;
            case 2:
              if (byExtendedKey >= 0x48u) {
                if (byExtendedKey <= 0x48u) {
                  if (levels[++level] <= 0.0)
                    --level;
                } else if (byExtendedKey == 80 && --level < 0) {
                  level = 0;
                }
              }
              break;
            case 3:
              if (byExtendedKey >= 0x48u) {
                if (byExtendedKey <= 0x48u) {
                  if (game_type < 2 && (unsigned int)competitors >= 8) {
                    if ((unsigned int)competitors <= 8) {
                      competitors = 2;
                    } else if (competitors == 16) {
                      competitors = 8;
                    }
                  }
                } else if (byExtendedKey == 80 && game_type < 2 && (unsigned int)competitors >= 2) {
                  if ((unsigned int)competitors <= 2) {
                    competitors = 8;
                  } else if (competitors == 8) {
                  LABEL_187:
                    competitors = 16;
                  }
                }
              }
              break;
            case 4:
              if (byExtendedKey >= 0x48u) {
                if (byExtendedKey <= 0x48u) {
                  if (--damage_level < 0)
                    damage_level = 0;
                } else if (byExtendedKey == 80 && ++damage_level > 2) {
                  damage_level = 2;
                }
              }
              break;
            case 5:
              if (byExtendedKey >= 0x48u) {
                if (byExtendedKey <= 0x48u) {
                  textures_off &= ~TEX_OFF_ADVANCED_CARS;
                } else if (byExtendedKey == 80) {
                  textures_off |= TEX_OFF_ADVANCED_CARS;
                }
              }
              break;
            default:
              continue;
          }
        }
      } else if (byInputKey <= 0x1Bu)           // Escape key: Exit to main menu or back to option selection
      {
        if (iMenuSelection)
          iMenuSelection = 0;
        else
          LABEL_248:
        iExitFlag = -1;
      } else if (byInputKey < 0x59u) {                                         // Space key: Cycle through cups (track groups) if not in championship
        if (byInputKey == 32 && !iSkipColor) {
          iCupIncrement = ++iBlockIdx;
          if ((cup_won & 1) == 0 && iCupIncrement == 1)
            iBlockIdx = 2;
          if ((cup_won & 2) == 0 && iBlockIdx == 2)
            iBlockIdx = 3;
          if (iBlockIdx > 2)
            iBlockIdx = 0;
          TrackLoad = 8 * iBlockIdx + (((uint8)TrackLoad - 1) & 7) + 1;
          broadcast_mode = -1;
          while (broadcast_mode)
            UpdateSDL();
        }
      } else if (byInputKey <= 0x59u || byInputKey == 121) {                                         // Y/y key: \"Yes\" - confirm championship exit and reset to race mode
        if (iSkipColor) {
          if (iMenuSelection == 6) {
            game_type = 0;
            iSkipColor = 0;
            iMenuSelection = 0;
            if (network_on) {
              if (Race <= 0) {
                broadcast_mode = -1;
                while (broadcast_mode)
                  UpdateSDL();
              } else {
                broadcast_mode = -666;
                while (broadcast_mode)
                  UpdateSDL();
                frames = 0;
                while (frames < 3)
                  ;
                close_network();
                network_champ_on = 0;
              }
            }
          }
        }
      }
      UpdateSDL();
    }
    UpdateSDL();
  } while (!iExitFlag);                         // MAIN SELECTION LOOP - Handle UI rendering and input processing
  if (!iSkipColor)                            // CLEANUP: Set final game parameters and track selection based on options
  {
    network_champ_on = 0;
    iTrackUpperLimit = 8 * iBlockIdx + 8;
    iTrackLowerLimit = 8 * iBlockIdx + 1;
    if (game_type)                            // Initialize championship mode: reset all statistics and set starting track
    {
      if ((unsigned int)game_type <= 1) {
        Race = 0;
        memset(championship_points, 0, sizeof(championship_points));
        memset(team_points, 0, sizeof(team_points));
        memset(total_kills, 0, sizeof(total_kills));
        memset(total_fasts, 0, sizeof(total_fasts));
        memset(total_wins, 0, sizeof(total_wins));
        memset(team_kills, 0, sizeof(team_kills));
        memset(team_fasts, 0, sizeof(team_fasts));
        memset(team_wins, 0, sizeof(team_wins));
        TrackLoad = 8 * iBlockIdx + 1;
      } else if (game_type == 2) {
        NoOfLaps = 5;
        competitors = 1;
        if (iTrackLowerLimit > TrackLoad || iTrackUpperLimit < TrackLoad)
          TrackLoad = 8 * iBlockIdx + (((uint8)TrackLoad - 1) & 7) + 1;
      }
    } else if (iTrackLowerLimit > TrackLoad || iTrackUpperLimit < TrackLoad) {
      TrackLoad = 8 * iBlockIdx + (((uint8)TrackLoad - 1) & 7) + 1;
    }
    broadcast_mode = -1;
    while (broadcast_mode)
      UpdateSDL();
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
  fre((void **)&front_vga[14]);
  front_fade = 0;
}

//-------------------------------------------------------------------------------------------------
//00049070
void select_track()
{
  int iAnimationTimer; // edi
  int iSelectedTrack; // ebp
  int iCurrentTrack; // esi
  int iLapsForLevel; // eax
  double dZoomInterpolation; // st7
  unsigned int uiKeyDirection; // ecx
  uint8 byKey; // al
  uint8 byKey_1; // al
  float fZ_1; // [esp+4h] [ebp-4Ch]
  float fTargetZoom; // [esp+8h] [ebp-48h]
  int iFrameCount; // [esp+Ch] [ebp-44h]
  int iBlockIdx; // [esp+10h] [ebp-40h]
  int iSoundFlag; // [esp+14h] [ebp-3Ch]
  int iYaw; // [esp+18h] [ebp-38h]
  int iPrevTrackLoad; // [esp+1Ch] [ebp-34h]
  float fZ; // [esp+20h] [ebp-30h]
  int iCupOffset; // [esp+24h] [ebp-2Ch]
  int iTrackInCup; // [esp+28h] [ebp-28h]
  int iExitFlag; // [esp+2Ch] [ebp-24h]
  int iCalculatedTrack; // [esp+30h] [ebp-20h]

  iAnimationTimer = 36;
  fade_palette(0);                              // Initialize track selection screen - fade out and reset state
  iSoundFlag = 0;
  front_fade = 0;
  iExitFlag = 0;
  if (TrackLoad > 0)                          // Set initial track selection based on current TrackLoad and game type
  {
    iSelectedTrack = ((uint8)TrackLoad - 1) & 7;
    if (game_type == 1)
      iSelectedTrack = 8;
  } else {
    iSelectedTrack = 0;
  }
  iCurrentTrack = TrackLoad;
  fre((void **)&front_vga[3]);                  // Load track selection UI graphics and 3D track preview
  if (TrackLoad >= 0)
    loadtrack(TrackLoad, -1);
  front_vga[3] = (tBlockHeader *)load_picture("trkname.bm");
  front_vga[13] = (tBlockHeader *)load_picture("bonustrk.bm");
  iYaw = 0;
  front_vga[14] = (tBlockHeader *)load_picture("cupicons.bm");
  // Restore palette and create GPU textures for sub-menu rendering
  {
    extern tColor palette[];
    memcpy(pal_addr, palette, 256 * sizeof(tColor));
    palette_brightness = 32;
    MenuRenderer *mr = GetMenuRenderer();
    if (mr) {
      menu_render_load_blocks(mr, 3, front_vga[3], palette);
      menu_render_load_blocks(mr, 13, front_vga[13], palette);
      menu_render_load_blocks(mr, 14, front_vga[14], palette);
    }
  }
  frames = 0;
  fZ = cur_TrackZ;
  fZ_1 = cur_TrackZ;
  fTargetZoom = -(cur_TrackZ + -10000000.0f) * 0.05f;
  do {                                             // Main track selection display loop
    if (game_type == 1)
      iSelectedTrack = 8;
    iFrameCount = frames;
    frames = 0;
    iPrevTrackLoad = TrackLoad;
    //if (SoundCard && front_fade && SampleHandleCar[86].handles[0] != -1 && sosDIGISampleDone(*(int *)&DIGIHandle, SampleHandleCar[86].handles[0]))// Handle frontend car engine sound sample playback
    if (SoundCard && front_fade && SampleHandleCar[86].handles[0] != -1 && DIGISampleDone(SampleHandleCar[86].handles[0]))// Handle frontend car engine sound sample playback
    {
      frontendsample(0x8000);
      SampleHandleCar[86].handles[0] = -1;
    }
    if (switch_types)                         // Handle game type switching (championship/single race/team game)
    {
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
    menu_render_sprite(mr, 1, 2, head_x, head_y, 0, pal_addr);
    if (cup_won)                              // Show cup trophy if championship won
      menu_render_sprite(mr, 1, 8, 480, 388, 0, pal_addr);
    menu_render_sprite(mr, 6, 0, 36, 2, 0, pal_addr);
    menu_render_sprite(mr, 5, player_type, -4, 247, 0, pal_addr);// Display player type and game type indicators
    menu_render_sprite(mr, 5, game_type + 5, 135, 247, 0, pal_addr);
    menu_render_sprite(mr, 4, 1, 76, 257, -1, pal_addr);
    if (iSelectedTrack >= 8)                  // Draw track selector: championship view or individual track selector
    {
      menu_render_sprite(mr, 6, 4, 62, 336, -1, pal_addr);
    } else {
      menu_render_sprite(mr, 6, 2, 62, 336, -1, pal_addr);
      menu_render_text(mr, 2, "~", font2_ascii, font2_offsets, sel_posns[iSelectedTrack].x, sel_posns[iSelectedTrack].y, 0x8Fu, 0, pal_addr);
    }
    menu_render_text(mr, 2, "AUTO ARIEL", font2_ascii, font2_offsets, sel_posns[0].x + 132, sel_posns[0].y + 7, 0x8Fu, 2u, pal_addr);// Display hardcoded track names (should use track name data)
    menu_render_text(mr, 2, "DESILVA", font2_ascii, font2_offsets, sel_posns[1].x + 132, sel_posns[1].y + 7, 0x8Fu, 2u, pal_addr);
    menu_render_text(mr, 2, "PULSE", font2_ascii, font2_offsets, sel_posns[2].x + 132, sel_posns[2].y + 7, 0x8Fu, 2u, pal_addr);
    menu_render_text(mr, 2, "GLOBAL", font2_ascii, font2_offsets, sel_posns[3].x + 132, sel_posns[3].y + 7, 0x8Fu, 2u, pal_addr);
    menu_render_text(mr, 2, "MILLION PLUS", font2_ascii, font2_offsets, sel_posns[4].x + 132, sel_posns[4].y + 7, 0x8Fu, 2u, pal_addr);
    menu_render_text(mr, 2, "MISSION", font2_ascii, font2_offsets, sel_posns[5].x + 132, sel_posns[5].y + 7, 0x8Fu, 2u, pal_addr);
    menu_render_text(mr, 2, "ZIZIN", font2_ascii, font2_offsets, sel_posns[6].x + 132, sel_posns[6].y + 7, 0x8Fu, 2u, pal_addr);
    menu_render_text(mr, 2, "REISE WAGON", font2_ascii, font2_offsets, sel_posns[7].x + 132, sel_posns[7].y + 7, 0x8Fu, 2u, pal_addr);
    iBlockIdx = (TrackLoad - 1) / 8;  // Calculate cup icon index (8 tracks per cup)
    //iBlockIdx = (TrackLoad - 1 - (__CFSHL__((TrackLoad - 1) >> 31, 3) + 8 * ((TrackLoad - 1) >> 31))) >> 3;// Calculate cup icon index for display
    if (TrackLoad >= 0) {                                           // Render 3D track preview
      menu_render_load_track_mesh(mr, palette);
      if (iAnimationTimer)
        menu_render_draw_track_preview(mr, fZ, 1280, iYaw, PREVIEW_X, TRACK_PREVIEW_Y, PREVIEW_W, PREVIEW_H);
      else
        menu_render_draw_track_preview(mr, fZ_1, 1280, iYaw, PREVIEW_X, TRACK_PREVIEW_Y, PREVIEW_W, PREVIEW_H);
      menu_render_set_layer(mr, MENU_LAYER_FOREGROUND);
      if (game_type >= 2)                     // Set number of laps based on game type and difficulty level
      {
        NoOfLaps = 5;
      } else {
        iLapsForLevel = cur_laps[level];
        NoOfLaps = iLapsForLevel;
        if (competitors == 2)
          NoOfLaps = iLapsForLevel / 2;
      }
      sprintf(buffer, "%s: %i", &language_buffer[4544], NoOfLaps);
      menu_render_text(mr, 15, buffer, font1_ascii, font1_offsets, 420, 16, 0x8Fu, 1u, pal_addr);
      menu_render_text(mr, 15, &language_buffer[4608], font1_ascii, font1_offsets, 420, 34, 0x8Fu, 1u, pal_addr);
      if (RecordCars[TrackLoad] < 0)          // Display track record holder and lap time
      {
        sprintf(buffer, "%s", RecordNames[TrackLoad]);
      } else {
        // Format lap time as MM:SS:CC (minutes:seconds:centiseconds)
        int iTotalCentiseconds = (int)(RecordLaps[TrackLoad] * 100.0);
        int iRecordMinutes = iTotalCentiseconds / 6000;
        int iRecordSeconds = (iTotalCentiseconds / 100) % 60;
        int iRecordCentiseconds = iTotalCentiseconds % 100;

        sprintf(buffer, "%s - %s - %02i:%02i:%02i",
            RecordNames[TrackLoad],
            CompanyNames[RecordCars[TrackLoad] & 0xF],
            iRecordMinutes,
            iRecordSeconds,
            iRecordCentiseconds);
        //dRecordTimeFloat = RecordLaps[TrackLoad] * 100.0;// Format lap time as MM:SS:CC (minutes:seconds:centiseconds)
        //_CHP();
        //LODWORD(llTimeCalc) = (int)dRecordTimeFloat;
        //HIDWORD(llTimeCalc) = (int)dRecordTimeFloat >> 31;
        //iRecordMinutes = llTimeCalc / 6000;
        //LODWORD(llTimeCalc) = (int)dRecordTimeFloat;
        //iRecordSeconds = (int)(llTimeCalc / 100) % 60;
        //LODWORD(llTimeCalc) = (int)dRecordTimeFloat;
        //sprintf(
        //  buffer,
        //  "%s - %s - %02i:%02i:%02i",
        //  RecordNames[TrackLoad],
        //  CompanyNames[RecordCars[TrackLoad] & 0xF],
        //  iRecordMinutes,
        //  iRecordSeconds,
        //  (unsigned int)(llTimeCalc % 100));
      }
      menu_render_text(mr, 15, buffer, font1_ascii, font1_offsets, 420, 52, 0x8Fu, 1u, pal_addr);
    }
    menu_render_sprite(mr, 14, iBlockIdx, 500, 300, 0, pal_addr);// Display cup icon and track name/type
    if (TrackLoad <= 0) {
      if (TrackLoad)
        menu_render_text(mr, 2, "EDITOR", font2_ascii, font2_offsets, 190, 350, 0x8Fu, 0, pal_addr);
      else
        menu_render_text(mr, 2, "TRACK ZERO", font2_ascii, font2_offsets, 190, 350, 0x8Fu, 0, pal_addr);
    } else if (TrackLoad >= 17)                 // Display track name: bonus tracks (17+), regular tracks (1-16), or special tracks
    {
      menu_render_sprite(mr, 13, TrackLoad - 17, 190, 356, 0, pal_addr);
    } else {
      menu_render_sprite(mr, 3, TrackLoad - 1, 190, 356, 0, pal_addr);
    }
    show_received_mesage();
    menu_render_end_frame(mr);
    }
    if (switch_same > 0)                      // Handle same-car mode activation/deactivation
    {

      for (int i = 0; i < players; i++) {
          Players_Cars[i] = switch_same - 666;
      }
      //iPlayerLoop2 = 0;                         // Activate same-car mode: set all players to same car type
      //if (players > 0) {
      //  iArrayOffset2 = 0;
      //  do {
      //    iArrayOffset2 += 4;
      //    ++iPlayerLoop2;
      //    *(int *)((char *)&infinite_laps + iArrayOffset2) = switch_same - 666;
      //  } while (iPlayerLoop2 < players);
      //}

      cheat_mode |= CHEAT_MODE_CLONES;
    } else if (switch_same < 0) {

      switch_same = 0;
      for (int i = 0; i < players; i++) {
          Players_Cars[i] = -1;
      }
      //iPlayerLoop1 = 0;                         // Deactivate same-car mode: reset all player car selections
      //switch_same = 0;
      //if (players > 0) {
      //  iArrayOffset1 = 0;
      //  do {
      //    iArrayOffset1 += 4;
      //    ++iPlayerLoop1;
      //    *(int *)((char *)&infinite_laps + iArrayOffset1) = -1;
      //  } while (iPlayerLoop1 < players);
      //}

      cheat_mode &= ~CHEAT_MODE_CLONES;
    }
    if (!iAnimationTimer)                     // Handle 3D camera zoom animation
    {
      dZoomInterpolation = (double)iFrameCount * fTargetZoom + fZ_1;
      fZ_1 = (float)dZoomInterpolation;
      if (dZoomInterpolation <= 10000000.0) {
        if (fZ_1 < fZ) {
          fZ_1 = fZ;
          iAnimationTimer = 72;
          fTargetZoom = -fTargetZoom;  // Reverse zoom direction
        }
      } else {
        fZ_1 = 10000000.0;                      // Camera reached max zoom - load new track and reset animation
        if (iCurrentTrack == TrackLoad)
          iPrevTrackLoad = -1;
        TrackLoad = iCurrentTrack;
        if (frontendspeechptr)
          fre((void **)&frontendspeechptr);
        if (TrackLoad >= 0)
          loadtrack(TrackLoad, -1);
        loadtracksample(TrackLoad);
        fZ = cur_TrackZ;
        iSoundFlag = -1;
        fTargetZoom = (cur_TrackZ + -10000000.0f) * 0.05f;
        if (iPrevTrackLoad != -1) {
          broadcast_mode = -1;
          while (broadcast_mode)
            UpdateSDL();
        }
        iPrevTrackLoad = -1;
        frames = 0;
      }
    }
    if (!front_fade)                          // Initialize screen fade and sound effects on first display
    {
      loadtracksample(TrackLoad);
      frontendsample(0x8000);
      frames = 0;
    }
    if (TrackLoad != iPrevTrackLoad)          // Track changed - play selection sound and reset animation
    {
      if (!iSoundFlag)
        sfxsample(SOUND_SAMPLE_TRACK, 0x8000);
      iSoundFlag = 0;
      iCurrentTrack = TrackLoad;
      iAnimationTimer = 0;
    }
    iCupOffset = 8 * iBlockIdx;                 // Calculate track selection values for input handling
    iTrackInCup = iSelectedTrack + 1;
    uiKeyDirection = 0;
    iCalculatedTrack = 8 * iBlockIdx + iSelectedTrack + 1;
    while (fatkbhit())                        // Process keyboard input for track selection
    {
      UpdateSDL();
      byKey = fatgetch();
      if (byKey < 0xDu) {
        if (!byKey) {
          byKey_1 = fatgetch();                 // Handle arrow keys: Up (0x48) and Down (0x50)
          if (byKey_1 >= 0x48u) {
            if (byKey_1 <= 0x48u) {
              uiKeyDirection = 2;
            } else if (byKey_1 == 0x50) {
              uiKeyDirection = 1;
            }
          }
        }
      } else if (byKey <= 0xDu) {                                         // Enter key pressed - confirm track selection
        if (iSelectedTrack != 8 && iCurrentTrack != iCalculatedTrack || iSelectedTrack == 8) {
          remove_frontendspeech();
          sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);
        }
        //if (iCurrentTrack == iCalculatedTrack && SoundCard && frontendspeechhandle != -1 && sosDIGISampleDone(*(int *)&DIGIHandle, frontendspeechhandle)) {
        if (iCurrentTrack == iCalculatedTrack && SoundCard && frontendspeechhandle != -1 && DIGISampleDone(frontendspeechhandle)) {
          frontendspeechhandle = -1;
          frontendsample(0x8000);
        }
        if (iSelectedTrack == 8) {
          iExitFlag = -1;
        } else if (game_type != 1 && iCurrentTrack != iCupOffset + iTrackInCup) {
          sfxsample(SOUND_SAMPLE_TRACK, 0x8000);
          iCurrentTrack = iCupOffset + iTrackInCup;
          if (iAnimationTimer) {
            iAnimationTimer = 0;
          } else if (fTargetZoom < 0.0) {
            fTargetZoom = -fTargetZoom;  // Reverse zoom direction
          }
        }
      } else if (byKey >= 0x1Bu) {
        if (byKey <= 0x1Bu) {
          remove_frontendspeech();              // Escape key pressed - exit track selection
          sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);
          iExitFlag = -1;
        } else if (byKey == 32 && game_type != 1 && TrackLoad > 0)// Space key pressed - cycle through cup categories
        {
          iCurrentTrack += 8;
          sfxsample(SOUND_SAMPLE_TRACK, 0x8000);
          if (iCurrentTrack > 8 && iCurrentTrack < 17 && (cup_won & 1) == 0)// Skip locked cup categories based on championship progress
            iCurrentTrack += 8;
          if (iCurrentTrack > 16 && iCurrentTrack < 25 && (cup_won & 2) == 0)
            iCurrentTrack += 8;
          if (iCurrentTrack > 24)
            iCurrentTrack -= 24;
          if (iAnimationTimer) {
            iAnimationTimer = 0;
          } else if (fTargetZoom < 0.0) {
            fTargetZoom = -fTargetZoom;  // Reverse zoom direction
          }
        }
      }
    }
    if (uiKeyDirection)                       // Process up/down arrow key movement in track list
    {
      if (uiKeyDirection > 1) {
        if (game_type != 1 && --iSelectedTrack < 0)
          iSelectedTrack = 0;
      } else if (game_type != 1 && ++iSelectedTrack > 8) {
        iSelectedTrack = 8;
      }
    }
    iYaw = ((uint16)iYaw + 32 * (uint16)iFrameCount) & 0x3FFF;

    UpdateSDL();
  } while (!iExitFlag);
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
  fre((void **)&front_vga[3]);
  fre((void **)&front_vga[13]);
  fre((void **)&front_vga[14]);
  front_vga[3] = (tBlockHeader *)load_picture("carnames.bm");
  if (frontendspeechptr)
    fre((void **)&frontendspeechptr);
}

//-------------------------------------------------------------------------------------------------
//00049C50
