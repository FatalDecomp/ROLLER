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
