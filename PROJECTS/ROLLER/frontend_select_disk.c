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
//000411D0
void select_disk()
{
  int iSelectedSlot; // esi
  unsigned int uiMenuMode; // edi
  unsigned int uiCupIndex; // eax
  uint8 bySlotColor; // al
  int iSlotNumber; // ecx
  unsigned int uiSaveCupIndex; // eax
  uint8 byCupColor1; // al
  uint8 byCupColor2; // al
  uint8 byCupColor3; // al
  uint8 byTrackColor; // al
  uint8 byDifficultyColor; // al
  uint8 byLevelColor; // al
  uint8 byDamageColor; // al
  uint8 byPlayerTypeColor; // al
  uint8 byEmptySlotColor; // al
  uint32 uiUpdatedCheatFlags; // eax
  int iCheatSetLoop; // ebx
  uint8 byInputKey; // al
  uint8 byExtendedKey; // al
  int iSlotYPosition; // [esp+0h] [ebp-24h]
  int iChampResult; // [esp+4h] [ebp-20h]
  int iExitFlag; // [esp+8h] [ebp-1Ch]
  int iSaveTrackNumber; // [esp+Ch] [ebp-18h]
  int iY; // [esp+10h] [ebp-14h]
  int iSaveArrayIndex; // [esp+14h] [ebp-10h]
  int iStatusMessage; // [esp+18h] [ebp-Ch]
  int iSlotLoop; // [esp+1Ch] [ebp-8h]
  int iMenuCursor; // [esp+20h] [ebp-4h]

  fade_palette(0);                              // Initialize screen fade and menu state variables
  uiMenuMode = 0;
  front_fade = 0;
  // Restore palette for GPU rendering
  {
    extern tColor palette[];
    memcpy(pal_addr, palette, 256 * sizeof(tColor));
    palette_brightness = 32;
  }
  iExitFlag = 0;
  iMenuCursor = 2;
  iStatusMessage = 0;
  check_saves();                                // Check save file status and scan for existing championship saves
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
    if (!uiMenuMode)
      iSelectedSlot = 0;
    {                                           // RENDER FRAME (GPU)
    MenuRenderer *mr = GetMenuRenderer();
    menu_render_begin_frame(mr);
    if (!front_fade) {
      front_fade = -1;
      menu_render_begin_fade(mr, 1, 32);
    }
    menu_render_background(mr, 0);
    menu_render_sprite(mr, 6, 0, 36, 2, 0, pal_addr);
    menu_render_sprite(mr, 1, 0, head_x, head_y, 0, pal_addr);
    if (iMenuCursor >= 2)                     // Draw selection cursor
    {
      menu_render_sprite(mr, 6, 4, 62, 336, -1, pal_addr);
    } else {
      menu_render_sprite(mr, 6, 2, 62, 336, -1, pal_addr);
      menu_render_text(mr, 2, "~", font2_ascii, font2_offsets, sel_posns[iMenuCursor].x, sel_posns[iMenuCursor].y, 0x8Fu, 0, pal_addr);
    }
    menu_render_text(mr, 2, &language_buffer[576], font2_ascii, font2_offsets, sel_posns[0].x + 132, sel_posns[0].y + 7, 0x8Fu, 2u, pal_addr); // Display main menu options: "Load Game" and "Save Game"
    menu_render_text(mr, 2, &language_buffer[640], font2_ascii, font2_offsets, sel_posns[1].x + 132, sel_posns[1].y + 7, 0x8Fu, 2u, pal_addr);
    menu_render_text(mr, 15, &language_buffer[704], font1_ascii, font1_offsets, 400, 270, 0xABu, 1u, pal_addr);// CURRENT GAME INFO: Display current championship settings and progress
    menu_render_text(mr, 15, &language_buffer[768], font1_ascii, font1_offsets, 400, 290, 0x8Fu, 2u, pal_addr);
    uiCupIndex = (TrackLoad - 1) / 8;
    //uiCupIndex = (TrackLoad - 1 - (__CFSHL__((TrackLoad - 1) >> 31, 3) + 8 * ((TrackLoad - 1) >> 31))) >> 3;// Show current cup name based on track group
    if (uiCupIndex) {
      if (uiCupIndex <= 1) {
        menu_render_text(mr, 15, &language_buffer[896], font1_ascii, font1_offsets, 405, 290, 0x8Fu, 0, pal_addr);
        goto LABEL_20;
      }
      if (uiCupIndex == 2) {
        menu_render_text(mr, 15, &language_buffer[4928], font1_ascii, font1_offsets, 405, 290, 0x8Fu, 0, pal_addr);
        goto LABEL_20;
      }
    }
    menu_render_text(mr, 15, &language_buffer[832], font1_ascii, font1_offsets, 405, 290, 0x8Fu, 0, pal_addr);
  LABEL_20:
    menu_render_text(mr, 15, &language_buffer[960], font1_ascii, font1_offsets, 400, 308, 0x8Fu, 2u, pal_addr);
    menu_render_text(mr, 15, CompanyNames[Race], font1_ascii, font1_offsets, 405, 308, 0x8Fu, 0, pal_addr);
    menu_render_text(mr, 15, &language_buffer[1024], font1_ascii, font1_offsets, 400, 326, 0x8Fu, 2u, pal_addr);
    if ((unsigned int)competitors < 8) {
      if (competitors == 2)
        menu_render_text(mr, 15, &language_buffer[1088], font1_ascii, font1_offsets, 405, 326, 0x8Fu, 0, pal_addr);
    } else if ((unsigned int)competitors <= 8) {
      menu_render_text(mr, 15, &language_buffer[1152], font1_ascii, font1_offsets, 405, 326, 0x8Fu, 0, pal_addr);
    } else if (competitors == 16) {
      menu_render_text(mr, 15, &language_buffer[1216], font1_ascii, font1_offsets, 405, 326, 0x8Fu, 0, pal_addr);
    }
    menu_render_text(mr, 15, &language_buffer[1280], font1_ascii, font1_offsets, 400, 344, 0x8Fu, 2u, pal_addr);
    menu_render_text(mr, 15, &language_buffer[64 * level + 1472], font1_ascii, font1_offsets, 405, 344, 0x8Fu, 0, pal_addr);
    menu_render_text(mr, 15, &language_buffer[1344], font1_ascii, font1_offsets, 400, 362, 0x8Fu, 2u, pal_addr);
    menu_render_text(mr, 15, &language_buffer[64 * damage_level + 1856], font1_ascii, font1_offsets, 405, 362, 0x8Fu, 0, pal_addr);
    menu_render_text(mr, 15, &language_buffer[1408], font1_ascii, font1_offsets, 400, 380, 0x8Fu, 2u, pal_addr);
    if (player_type == 1 && net_type) {
      if ((unsigned int)net_type >= (unsigned int)player_type && (unsigned int)net_type <= 2)
        menu_render_text(mr, 15, &language_buffer[2304], font1_ascii, font1_offsets, 405, 380, 0x8Fu, 0, pal_addr);
    } else {
      menu_render_text(mr, 15, &language_buffer[64 * player_type + 2112], font1_ascii, font1_offsets, 405, 380, 0x8Fu, 0, pal_addr);
    }
    iSlotLoop = 0;                              // SAVE SLOT DISPLAY: Show all 4 championship save slots with their details
    iSlotYPosition = 56;
    iSaveArrayIndex = 0;
    iY = 74;
    do {
      sprintf(buffer, "%s %i:", &language_buffer[2432], iSlotLoop + 1);// Display slot number with highlighting for currently selected slot
      if (iSelectedSlot == iSlotLoop + 1)
        bySlotColor = 0xAB;
      else
        bySlotColor = 0x8F;
      menu_render_text(mr, 15, buffer, font1_ascii, font1_offsets, 300, iSlotYPosition, bySlotColor, 2u, pal_addr);
      iSlotNumber = iSlotLoop + 1;
      if (save_status[iSaveArrayIndex].iSlotUsed)// Show save slot contents: cup, track, difficulty, damage, player type
      {
        uiSaveCupIndex = (save_status[iSaveArrayIndex].iPackedTrack - 1) / 8;
        iSaveTrackNumber = ((save_status[iSaveArrayIndex].iPackedTrack - 1) % 8) + 1;
        //uiSaveCupIndex = (save_status[iSaveArrayIndex].iPackedTrack
        //                - 1
        //                - (__CFSHL__((save_status[iSaveArrayIndex].iPackedTrack - 1) >> 31, 3)
        //                   + 8 * ((save_status[iSaveArrayIndex].iPackedTrack - 1) >> 31))) >> 3;
        //iSaveTrackNumber = ((LOBYTE(save_status[iSaveArrayIndex].iPackedTrack) - 1) & 7) + 1;
        if (uiSaveCupIndex) {
          if (uiSaveCupIndex <= 1) {
            if (iSelectedSlot == iSlotNumber)
              byCupColor2 = 0xAB;
            else
              byCupColor2 = 0x8F;
            menu_render_text(mr, 15, &language_buffer[896], font1_ascii, font1_offsets, 305, iSlotYPosition, byCupColor2, 0, pal_addr);
          } else if (uiSaveCupIndex == 2) {
            if (iSelectedSlot == iSlotNumber)
              byCupColor3 = 0xAB;
            else
              byCupColor3 = 0x8F;
            menu_render_text(mr, 15, &language_buffer[4928], font1_ascii, font1_offsets, 305, iSlotYPosition, byCupColor3, 0, pal_addr);
          }
        } else {
          if (iSelectedSlot == iSlotNumber)
            byCupColor1 = 0xAB;
          else
            byCupColor1 = 0x8F;
          menu_render_text(mr, 15, &language_buffer[832], font1_ascii, font1_offsets, 305, iSlotYPosition, byCupColor1, 0, pal_addr);
        }
        sprintf(buffer, "%s %i", &language_buffer[256], iSaveTrackNumber);
        if (iSelectedSlot == iSlotLoop + 1)
          byTrackColor = 0xAB;
        else
          byTrackColor = 0x8F;
        menu_render_text(mr, 15, "-", font1_ascii, font1_offsets, 470, iSlotYPosition, byTrackColor, 0, pal_addr);
        if (iSelectedSlot == iSlotLoop + 1)
          byDifficultyColor = 0xAB;
        else
          byDifficultyColor = 0x8F;
        menu_render_text(mr, 15, buffer, font1_ascii, font1_offsets, 480, iSlotYPosition, byDifficultyColor, 0, pal_addr);
        if (iSelectedSlot == iSlotLoop + 1)
          byLevelColor = 0xAB;
        else
          byLevelColor = 0x8F;
        menu_render_text(mr, 15, &language_buffer[64 * save_status[iSaveArrayIndex].iDifficulty + 1472], font1_ascii, font1_offsets, 460, iY, byLevelColor, 2u, pal_addr);
        if (iSelectedSlot == iSlotLoop + 1)
          byDamageColor = 0xAB;
        else
          byDamageColor = 0x8F;
        menu_render_text(mr, 15, "-", font1_ascii, font1_offsets, 470, iY, byDamageColor, 0, pal_addr);
        if (iSelectedSlot == iSlotLoop + 1)
          byPlayerTypeColor = 0xAB;
        else
          byPlayerTypeColor = 0x8F;
        menu_render_text(mr, 15, &language_buffer[64 * save_status[iSaveArrayIndex].iPlayerType + 2112], font1_ascii, font1_offsets, 480, iY, byPlayerTypeColor, 0, pal_addr);
      } else {                                         // Display "Empty" for unused save slots
        if (iSelectedSlot == iSlotNumber)
          byEmptySlotColor = 0xAB;
        else
          byEmptySlotColor = 0x8F;
        menu_render_text(mr, 15, &language_buffer[2496], font1_ascii, font1_offsets, 305, iSlotYPosition, byEmptySlotColor, 0, pal_addr);
      }
      iSlotYPosition += 40;
      ++iSaveArrayIndex;
      iY += 40;
      ++iSlotLoop;
    } while (iSlotLoop < 4);
    switch (iStatusMessage) {
      case 0:
        if (network_on)                       // Case 0: Network save restriction message
          menu_render_text(mr, 15, &language_buffer[4864], font1_ascii, font1_offsets, 400, 230, 0xE7u, 1u, pal_addr);
        break;
      case 1:
        if (iChampResult)                     // Case 1: Load operation messages (success/confirmation)
          menu_render_text(mr, 15, &language_buffer[2624], font1_ascii, font1_offsets, 400, 230, 0xE7u, 1u, pal_addr);
        else
          menu_render_text(mr, 15, &language_buffer[2560], font1_ascii, font1_offsets, 400, 230, 0xE7u, 1u, pal_addr);
        break;
      case 2:
        menu_render_text(mr, 15, &language_buffer[2688], font1_ascii, font1_offsets, 400, 230, 0xE7u, 1u, pal_addr); // Case 2: Save operation success message
        break;
      case 4:
        menu_render_text(mr, 15, &language_buffer[2752], font1_ascii, font1_offsets, 400, 230, 0xE7u, 1u, pal_addr); // Case 4: Empty slot selected (no save to load)
        break;
      default:
        break;                                  // STATUS MESSAGES: Display operation results and warnings
    }
    show_received_mesage();
    menu_render_end_frame(mr);
    }
    if (switch_same > 0)                      // CHEAT MODE HANDLING: Process switch_same command for player synchronization
    {
      iCheatSetLoop = 0;
      if (players > 0) {
        for (int i = 0; i < players; i++)
        {
            Players_Cars[i] = switch_same - 666;
        }
        //iPlayersCarsOffset = 0;
        //do {
        //  iPlayersCarsOffset += 4;
        //  ++iCheatSetLoop;
        //  *(int *)((char *)&infinite_laps + iPlayersCarsOffset) = switch_same - 666;// offset into Players_Cars
        //} while (iCheatSetLoop < players);
      }
      uiUpdatedCheatFlags = cheat_mode;
      //BYTE1(uiUpdatedCheatFlags) = BYTE1(cheat_mode) | 0x40;// |= CHEAT_MODE_CLONES;
      uiUpdatedCheatFlags |= CHEAT_MODE_CLONES;
    } else {
      if (switch_same >= 0)
        goto LABEL_95;
      switch_same = 0;

      for (int i = 0; i < players; i++)
      {
          Players_Cars[i] = -1;
      }
      //iCheatResetLoop = 0;
      //if (players > 0) {
      //  iCheatResetOffset = 0;
      //  do {
      //    iCheatResetOffset += 4;
      //    ++iCheatResetLoop;
      //    *(int *)((char *)&infinite_laps + iCheatResetOffset) = -1;// offset into Players_Cars
      //  } while (iCheatResetLoop < players);
      //}
      uiUpdatedCheatFlags = cheat_mode;
      //BYTE1(uiUpdatedCheatFlags) = BYTE1(cheat_mode) & 0xBF;// &= ~CHEAT_MODE_CLONES
      uiUpdatedCheatFlags &= ~CHEAT_MODE_CLONES;
    }
    cheat_mode = uiUpdatedCheatFlags;
  LABEL_95:
    while (fatkbhit())                        // KEYBOARD INPUT PROCESSING: Handle navigation and save/load operations
    {
      byInputKey = fatgetch();
      if (byInputKey < 0xDu) {                                         // Arrow keys: Navigate between menu options and save slots
        if (!byInputKey) {
          byExtendedKey = fatgetch();
          if (byExtendedKey >= 0x48u) {
            if (byExtendedKey <= 0x48u) {                                   // Up arrow: Move up in save slot selection or main menu
              if (uiMenuMode) {
                if (iSelectedSlot > 1) {
                  iStatusMessage = 0;
                  --iSelectedSlot;
                }
              } else if (iMenuCursor > 0) {
                iStatusMessage = 0;
                --iMenuCursor;
              }
            } else if (byExtendedKey == 80) {                                   // Down arrow: Move down in save slot selection or main menu
              if (uiMenuMode) {
                if (iSelectedSlot < 4) {
                  iStatusMessage = 0;
                  ++iSelectedSlot;
                }
              } else if (iMenuCursor < 2) {
                iStatusMessage = 0;
                ++iMenuCursor;
              }
            }
          }
        }
      } else if (byInputKey <= 0xDu) {                                         // Enter key: Execute save/load operation based on current menu and selection
        if (uiMenuMode) {
          if (uiMenuMode <= 1) {
            save_champ(iSelectedSlot);          // SAVE operation: Save current championship to selected slot
            uiMenuMode = 0;
            check_saves();
            iStatusMessage = 2;
          } else {
            iStatusMessage = 1;
            if (save_status[iSelectedSlot - 1].iSlotUsed) {
              uiMenuMode = 0;                   // LOAD operation: Load championship from selected slot
              iChampResult = load_champ(iSelectedSlot);
            } else {
              iStatusMessage = 4;
            }
          }
        } else if (iMenuCursor) {
          if ((unsigned int)iMenuCursor <= 1) {
            uiMenuMode = 2;                     // \"Save Game\" selected: Enter save slot selection mode
            iSelectedSlot = 1;
          } else if (iMenuCursor == 2) {
            goto LABEL_128;
          }
        } else {
          uiMenuMode = 1;                       // \"Load Game\" selected: Enter save slot selection mode
          iSelectedSlot = 1;
        }
      } else if (byInputKey == 27) {                                         // Escape key: Cancel operation or exit to main menu
        if (uiMenuMode) {
          uiMenuMode = 0;
          iStatusMessage = 0;
        } else {
        LABEL_128:
          iExitFlag = -1;
        }
      }
      UpdateSDL();
    }
    UpdateSDL();
  } while (!iExitFlag);                         // MAIN MENU LOOP - Handle UI rendering and input processing
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
