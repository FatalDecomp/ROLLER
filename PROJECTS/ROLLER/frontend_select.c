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
//00041CA0
void select_car()
{
  eCarType currentCarType; // ebx
  int iSelectedCar; // ebp
  eCarDesignIndex iPlayer1Car; // edi
  eCarType carType; // eax
  int iLoadCarTextures; // edx
  int iCartexLoaded; // esi
  int iCarIdx; // eax
  int iCarPieOffset; // edx
  int iInvertedPieValue; // ebx
  int iCurrentCarSelectorPos; // esi
  int iPlayerNameX; // eax
  int iAnimationCounter; // eax
  int iPieChartIndex; // edx
  int iCarStatsOffset; // ebx
  int iCurrentPieValue; // ecx
  int iNextPieValue; // ecx
  int iNewZoomDistance; // ebx
  int iCarAllocationStatus; // edx
  eCarDesignIndex iCarDesignIndex; // eax
  uint8 **ppTextureArray; // edi
  void **ppCurrentTexture; // eax
  int iPlayerCarIndex; // eax
  eCarType eNewCarType; // eax
  int iTextureLoadedStatus; // ecx
  int iLoadTextureFlag; // edx
  int iPlayerLoopCounter; // edx
  unsigned int uiNavigationDirection; // ebx
  uint8 byInputKey; // al
  int16 nRotationAngle; // ax
  uint8 **ppCleanupTextureArray; // edx
  void **ppCleanupTexture; // eax
  float fCarDrawDistance; // [esp+0h] [ebp-80h]
  unsigned int uiPlayerIndex; // [esp+8h] [ebp-78h]
  int blockIdxAy[7]; // [esp+Ch] [ebp-74h] BYREF
  int iStatAnimationFrame; // [esp+28h] [ebp-58h]
  int iOriginalCarSelection; // [esp+2Ch] [ebp-54h]
  int iActivePlayer; // [esp+30h] [ebp-50h]
  int iDelayBeforeRotation; // [esp+34h] [ebp-4Ch]
  int iZoomSpeed; // [esp+38h] [ebp-48h]
  int iZoomDistance; // [esp+3Ch] [ebp-44h]
  int byMenuExitFlag; // [esp+40h] [ebp-40h]
  unsigned int uiNetworkPlayerIndex; // [esp+44h] [ebp-3Ch]
  char *szPlayerName; // [esp+48h] [ebp-38h]
  char *szCurrentCompanyName = '\0'; // [esp+4Ch] [ebp-34h]
  int iTargetPieValue; // [esp+50h] [ebp-30h]
  int iNextCarIndex; // [esp+58h] [ebp-28h]
  unsigned int uiNetworkLoop; // [esp+5Ch] [ebp-24h]
  int iPieChartY; // [esp+60h] [ebp-20h]
  int iNetworkPlayerCount; // [esp+64h] [ebp-1Ch]

  fade_palette(0);                              // Initialize screen fade and prepare UI graphics
  front_fade = 0;
  front_vga[3] = (tBlockHeader*)load_picture("carnames.bm");   // Load car company name graphics (carnames.bm) and car selection UI (selcar2.bm)
  front_vga[7] = (tBlockHeader*)load_picture("selcar2.bm");    // Load car selection UI bitmap
  // Restore palette and create GPU textures for sub-menu rendering
  {
    extern tColor palette[];
    memcpy(pal_addr, palette, 256 * sizeof(tColor));
    palette_brightness = 32;
    MenuRenderer *mr = GetMenuRenderer();
    if (mr) {
      menu_render_load_blocks(mr, 3, front_vga[3], palette);
      menu_render_load_blocks(mr, 7, front_vga[7], palette);
    }
  }
  car_request = 0;
  byMenuExitFlag = 0;                           // Initialize selection state: byMenuExitFlag=0 (stay in menu), iOriginalCarSelection=-1 (no backup)
  iOriginalCarSelection = -1;                   // byExitCode = 0 - stay in selection, -1 = exit to menu
  iPlayer1Car = Players_Cars[player1_car];      // iSavedCarIndex = -1 initially, stores original car selection
  if (game_type == 1)                         // Championship mode: restrict car changes except for first race (Race == 0)
  {
    Race = ((uint8)TrackLoad - 1) & 7;
    if ((((uint8)TrackLoad - 1) & 7) != 0)
      iOriginalCarSelection = iPlayer1Car;
  }
  if (iPlayer1Car >= CAR_DESIGN_AUTO) {
    carType = CarDesigns[iPlayer1Car].carType;
    currentCarType = carType;
    iLoadCarTextures = 1;
    iCartexLoaded = car_texs_loaded[carType];
    if (iCartexLoaded == -1) {
      LoadCarTexture(carType, 1u);              // Car texture not loaded yet - load it now and mark as loaded
      iLoadCarTextures = 2;
      car_texmap[iPlayer1Car] = 1;
      car_texs_loaded[currentCarType] = 1;
    } else {
      car_texmap[iPlayer1Car] = iCartexLoaded;
    }
    LoadCarTextures = iLoadCarTextures;
  }
  if (iPlayer1Car < CAR_DESIGN_AUTO)          // Initialize car statistics pie chart display blocks (7 stats: Speed, Acceleration, etc.)
  {
    memset(blockIdxAy, 8, 7 * sizeof(int));
    //_STOSD(blockIdxAy, 8, currentCarType * 4, 7u);
  } else {
    iCarIdx = 0;
    iCarPieOffset = 7 * iPlayer1Car;
    do {
      ++iCarIdx;
      iInvertedPieValue = 9 - car_pies[iCarPieOffset++];
      blockIdxAy[iCarIdx - 1] = iInvertedPieValue;
    } while (iCarIdx < 7);
  }
  iZoomDistance = 2000;                         // Setup 3D car zoom animation: start at 2000 units, zoom out to 40000 (-2000 speed)
  iZoomSpeed = -2000;
  if (iPlayer1Car >= CAR_DESIGN_SUICYCO)       // Position car selector cursor
    iCurrentCarSelectorPos = 8;
  else
    iCurrentCarSelectorPos = iPlayer1Car;
  if (iCurrentCarSelectorPos < 0)             // Clamp car selector position to valid range (0-8, where 8 = random)
    iCurrentCarSelectorPos = 8;
  iDelayBeforeRotation = 36;                    // 36-frame delay before starting car rotation animation
  iActivePlayer = 0;
  frames = 0;
  if (!byMenuExitFlag) {
    if (iOriginalCarSelection >= 0) //check added by ROLLER for zig build
      szCurrentCompanyName = CompanyNames[iOriginalCarSelection];
    do {                                           // Handle game type switches (race type, championship, etc.)
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
      iStatAnimationFrame = frames;
      frames = 0;
      if (SoundCard
        && front_fade
        && SampleHandleCar[84].handles[0] != -1
        && DIGISampleDone(SampleHandleCar[84].handles[0])) {//DIGISampleDone(*(int *)&DIGIHandle, SampleHandleCar[84].handles[0])) {
        frontendsample(0x8000);
        SampleHandleCar[84].handles[0] = -1;
      }
      {                                           // RENDER FRAME (GPU)
      MenuRenderer *mr = GetMenuRenderer();
      menu_render_begin_frame(mr);
      if (!front_fade) {
        front_fade = -1;
        menu_render_begin_fade(mr, 1, 32);
      }
      menu_render_background(mr, 0);
      if (player_type == 2)                   // Two-player mode
      {
        if (iActivePlayer)
          menu_render_sprite(mr, 1, 6, head_x, head_y, 0, pal_addr);
        else
          menu_render_sprite(mr, 1, 5, head_x, head_y, 0, pal_addr);
        menu_render_sprite(mr, 1, 7, 200, 56, 0, pal_addr);
      } else {
        menu_render_sprite(mr, 1, 1, head_x, head_y, 0, pal_addr);
      }
      menu_render_sprite(mr, 6, 0, 36, 2, 0, pal_addr);
      if (iPlayer1Car < CAR_DESIGN_AUTO) {
        menu_render_text(mr, 15, &language_buffer[4160], font1_ascii, font1_offsets, 400, 200, 0xE7u, 1u, pal_addr);
      } else {                                         // 3D car preview (GPU mesh rendering)
        menu_render_load_car_mesh(mr, iPlayer1Car, palette);
        if (iPlayer1Car == CAR_DESIGN_F1WACK) {
          menu_render_draw_car_preview(mr, 1280.0f, 6000.0f, Car[0].nYaw, PREVIEW_X, CAR_PREVIEW_Y, PREVIEW_W, PREVIEW_H);
        } else if (iDelayBeforeRotation) {
          menu_render_draw_car_preview(mr, 1280.0f, 2200.0f, Car[0].nYaw, PREVIEW_X, CAR_PREVIEW_Y, PREVIEW_W, PREVIEW_H);
        } else {
          fCarDrawDistance = (float)iZoomDistance;
          menu_render_draw_car_preview(mr, 1280.0f, fCarDrawDistance, Car[0].nYaw, PREVIEW_X, CAR_PREVIEW_Y, PREVIEW_W, PREVIEW_H);
        }
        if (iPlayer1Car < CAR_DESIGN_SUICYCO)
          menu_render_sprite(mr, 3, iPlayer1Car, 190, 356, 0, pal_addr);
      }
      menu_render_set_layer(mr, MENU_LAYER_FOREGROUND);
      menu_render_sprite(mr, 7, 0, 560, 20, 0, pal_addr);
      uiNetworkLoop = 0;
      iPieChartY = 19;                          // Draw 7 pie chart segments
      do {
        menu_render_sprite(mr, 7, blockIdxAy[uiNetworkLoop / 4], 568, iPieChartY, 0, pal_addr);
        uiNetworkLoop += 4;
        iPieChartY += 51;
      } while (uiNetworkLoop != 28);
      menu_render_sprite(mr, 5, player_type, -4, 247, 0, pal_addr);
      menu_render_sprite(mr, 5, game_type + 5, 135, 247, 0, pal_addr);
      menu_render_sprite(mr, 4, 0, 76, 257, -1, pal_addr);
      if (iCurrentCarSelectorPos >= 8)        // Draw car selection cursor
      {
        menu_render_sprite(mr, 6, 4, 62, 336, -1, pal_addr);
      } else {
        menu_render_sprite(mr, 6, 2, 62, 336, -1, pal_addr);
        menu_render_text(mr, 2, "~", font2_ascii, font2_offsets, sel_posns[iCurrentCarSelectorPos].x, sel_posns[iCurrentCarSelectorPos].y, 0x8Fu, 0, pal_addr);
      }
      menu_render_text(mr, 2, "AUTO ARIEL", font2_ascii, font2_offsets, sel_posns[0].x + 132, sel_posns[0].y + 7, 0x8Fu, 2u, pal_addr);
      menu_render_text(mr, 2, "DESILVA", font2_ascii, font2_offsets, sel_posns[1].x + 132, sel_posns[1].y + 7, 0x8Fu, 2u, pal_addr);
      menu_render_text(mr, 2, "PULSE", font2_ascii, font2_offsets, sel_posns[2].x + 132, sel_posns[2].y + 7, 0x8Fu, 2u, pal_addr);
      menu_render_text(mr, 2, "GLOBAL", font2_ascii, font2_offsets, sel_posns[3].x + 132, sel_posns[3].y + 7, 0x8Fu, 2u, pal_addr);
      menu_render_text(mr, 2, "MILLION PLUS", font2_ascii, font2_offsets, sel_posns[4].x + 132, sel_posns[4].y + 7, 0x8Fu, 2u, pal_addr);
      menu_render_text(mr, 2, "MISSION", font2_ascii, font2_offsets, sel_posns[5].x + 132, sel_posns[5].y + 7, 0x8Fu, 2u, pal_addr);
      menu_render_text(mr, 2, "ZIZIN", font2_ascii, font2_offsets, sel_posns[6].x + 132, sel_posns[6].y + 7, 0x8Fu, 2u, pal_addr);
      menu_render_text(mr, 2, "REISE WAGON", font2_ascii, font2_offsets, sel_posns[7].x + 132, sel_posns[7].y + 7, 0x8Fu, 2u, pal_addr);
      if (iCurrentCarSelectorPos < 8 && network_on && (cheat_mode & 0x4000) == 0)// Network mode
      {
        menu_render_text(mr, 15, &language_buffer[4672], font1_ascii, font1_offsets, 380, 380, 0x8Fu, 2u, pal_addr);
        if (allocated_cars[iCurrentCarSelectorPos]) {
          iPlayerNameX = 385;
          iNetworkPlayerCount = 0;
          if (players > 0) {
            uiNetworkPlayerIndex = 0;
            szPlayerName = player_names[0];
            do {
              if (iCurrentCarSelectorPos == Players_Cars[uiNetworkPlayerIndex / 4]) {
                if (iPlayerNameX == 385)
                  uiPlayerIndex = 0;
                else
                  uiPlayerIndex = 2;
                menu_render_text(mr, 15, szPlayerName, font1_ascii, font1_offsets, iPlayerNameX, 380, 0x8Fu, uiPlayerIndex, pal_addr);
                iPlayerNameX = 620;
              }
              uiNetworkPlayerIndex += 4;
              szPlayerName += 9;
              ++iNetworkPlayerCount;
            } while (iNetworkPlayerCount < players);
          }
        } else {
          menu_render_text(mr, 15, &language_buffer[4736], font1_ascii, font1_offsets, 385, 380, 0x83u, 0, pal_addr);
        }
      }
      if (iOriginalCarSelection >= 0)         // Display current player's selected car company name at bottom
      {
        if (iActivePlayer)
          sprintf(buffer, "%s %s", &language_buffer[2880], CompanyNames[Players_Cars[player2_car]]);
        else
          sprintf(buffer, "%s %s", &language_buffer[2816], szCurrentCompanyName);
        menu_render_scaled_text(mr, 15, buffer, font1_ascii, font1_offsets, 375, 316, 231, 1u, 170, 550, pal_addr);
      }
      show_received_mesage();
      menu_render_end_frame(mr);
      }
      iAnimationCounter = iDelayBeforeRotation; // ANIMATION UPDATE: Rotate pie chart segments during delay period
      if (iDelayBeforeRotation) {
        if (iPlayer1Car >= CAR_DESIGN_AUTO) {
          iPieChartIndex = 0;
          iCarStatsOffset = 7 * iPlayer1Car;
          do {
            iTargetPieValue = 9 - car_pies[iCarStatsOffset];
            iCurrentPieValue = blockIdxAy[iPieChartIndex];
            if (iCurrentPieValue != iTargetPieValue) {
              iNextPieValue = iCurrentPieValue + 1;
              blockIdxAy[iPieChartIndex] = iNextPieValue;
              if (iNextPieValue > 8)
                blockIdxAy[iPieChartIndex] = 1;
            }
            ++iPieChartIndex;
            ++iCarStatsOffset;
          } while (iPieChartIndex < 7);
        }
      } else {
        for (int i = 0; i < 7; i++) {
            blockIdxAy[i]++;
            if (blockIdxAy[i] > 8) {
                blockIdxAy[i] = 1;
            }
        }
        //do {
        //  int iIncrementedPieValue = *(int *)((char *)blockIdxAy + iAnimationCounter) + 1;
        //  *(int *)((char *)blockIdxAy + iAnimationCounter) = iIncrementedPieValue;
        //  if (iIncrementedPieValue > 8)
        //    *(int *)((char *)blockIdxAy + iAnimationCounter) = 1;
        //  iAnimationCounter += 4;
        //} while (iAnimationCounter != 28);
      }
      if (!iDelayBeforeRotation)              // ZOOM ANIMATION: Handle 3D car zoom in/out with car switching logic
      {
        iNewZoomDistance = iStatAnimationFrame * iZoomSpeed + iZoomDistance;
        iZoomDistance = iNewZoomDistance;
        if (iNewZoomDistance <= 40000)        // Handle car zoom in/out animation and car selection
        {
          if (iNewZoomDistance < 4000) {
            iZoomDistance = 4000;
            iDelayBeforeRotation = 72;
          }
        } else {
          iCarAllocationStatus = allocated_cars[iSelectedCar];// Zoom reached maximum - switch cars if available, otherwise restore previous car
          iZoomDistance = 40000;
          iZoomSpeed = -iZoomSpeed;
          if (iCarAllocationStatus < 2) {                                     // Free old car textures and load new car's textures
            if (iPlayer1Car >= CAR_DESIGN_AUTO) {
              iCarDesignIndex = iPlayer1Car;
              ppTextureArray = cartex_vga;
              car_texs_loaded[CarDesigns[iCarDesignIndex].carType] = -1;
              do {
                ppCurrentTexture = (void **)ppTextureArray++;
                fre(ppCurrentTexture);
              } while (ppTextureArray != &cartex_vga[16]);
              remove_mapsels();
              if (frontendspeechptr)
                fre((void **)&frontendspeechptr);
            }
            if (game_type == 1 && Race > 0) {
              iPlayer1Car = iSelectedCar;
            } else {
              if (iActivePlayer)
                iPlayerCarIndex = player2_car;
              else
                iPlayerCarIndex = player1_car;
              iPlayer1Car = iSelectedCar;
              Players_Cars[iPlayerCarIndex] = iSelectedCar;
            }
            if (iPlayer1Car >= CAR_DESIGN_AUTO) {
              eNewCarType = CarDesigns[iPlayer1Car].carType;
              iTextureLoadedStatus = car_texs_loaded[eNewCarType];

              if (iTextureLoadedStatus == -1) {
                  // Texture not loaded yet - load it now
                  LoadCarTexture(eNewCarType, 1u);
                  car_texmap[iPlayer1Car] = 1;
                  car_texs_loaded[eNewCarType] = 1;
                  iLoadTextureFlag = 2;
              } else {
                  // Texture already loaded - use existing
                  car_texmap[iPlayer1Car] = iTextureLoadedStatus;
                  iLoadTextureFlag = 1;
              }
              //eNewCarType = CarDesigns[iPlayer1Car].carType;
              //eCarTypeBackup = eNewCarType;
              //uiCarArrayOffset = 4 * iPlayer1Car;
              //iTextureLoadedStatus = car_texs_loaded[eNewCarType];
              //iLoadTextureFlag = 1;
              //if (iTextureLoadedStatus == -1) {
              //  LoadCarTexture(eNewCarType, 1u);
              //  car_texmap[uiCarArrayOffset / 4] = 1;
              //  car_texs_loaded[eCarTypeBackup] = 1;
              //  iLoadTextureFlag = 2;
              //} else {
              //  car_texmap[uiCarArrayOffset / 4] = iTextureLoadedStatus;
              //}
              LoadCarTextures = iLoadTextureFlag;
              if (!network_on)
                check_cars();
            }
          } else {
            if (iActivePlayer)
              iPlayer1Car = Players_Cars[player2_car];
            else
              iPlayer1Car = Players_Cars[player1_car];
            iSelectedCar = iPlayer1Car;
          }
          if (iPlayer1Car >= CAR_DESIGN_AUTO) {
            sfxsample(SOUND_SAMPLE_CARIN, 0x8000);
            if (iPlayer1Car < CAR_DESIGN_SUICYCO)
              loadfrontendsample(descript[iPlayer1Car]);
            if (!SamplePtr[SOUND_SAMPLE_CARIN])
              frontendsample(0x8000);
          }
          broadcast_mode = -1;
          while (broadcast_mode) {
            UpdateSDL();
          }
          frames = 0;
        }
      }
      if (car_request < 0)                    // NETWORK CAR REQUESTS: Handle incoming car selection requests from other players
      {
        iDelayBeforeRotation = 0;
        iZoomSpeed = 2000;
        iSelectedCar = -car_request - 1;
        sfxsample(SOUND_SAMPLE_CAROUT, 0x8000);
        car_request = 0;
        if ((cheat_mode & 0x4000) != 0)
          switch_same = iSelectedCar + 666;
      }
      if (switch_same > 0)                    // CHEAT MODE: Handle "switch_same" command to force all players to same car
      {
        if (switch_same - 666 != Players_Cars[player1_car]) {
          iSelectedCar = switch_same - 666;
          iZoomSpeed = 2000;
          iDelayBeforeRotation = 0;
          sfxsample(SOUND_SAMPLE_CAROUT, 0x8000);

          if (players > 0) {
              for (iPlayerLoopCounter = 0; iPlayerLoopCounter < players; iPlayerLoopCounter++) {
                  Players_Cars[iPlayerLoopCounter] = switch_same - 666;
              }
          }
          //iPlayerLoopCounter = 0;
          //if (players > 0) {
          //  iPlayerArrayOffset = 0;
          //  do {
          //    iPlayerArrayOffset += 4;
          //    ++iPlayerLoopCounter;
          //    *(int *)((char *)&infinite_laps + iPlayerArrayOffset) = switch_same - 666;// offset into Players_Cars
          //  } while (iPlayerLoopCounter < players);
          //}

          cheat_mode |= CHEAT_MODE_CLONES;
        }
      } else if (switch_same < 0) {
        switch_same = 0;
        iDelayBeforeRotation = 0;
        iZoomSpeed = 2000;
        iSelectedCar = -1;
        sfxsample(SOUND_SAMPLE_CAROUT, 0x8000);
        cheat_mode &= ~CHEAT_MODE_CLONES;
      }
      if (switch_sets) {
        iSelectedCar = iPlayer1Car;
        iDelayBeforeRotation = 0;
        iZoomSpeed = 2000;
        sfxsample(SOUND_SAMPLE_CAROUT, 0x8000);
        switch_sets = 0;
      }
      if (!front_fade)                        // Screen fade-in: Load car voice sample and fade palette to visible
      {
        if (iPlayer1Car < CAR_DESIGN_SUICYCO)
          loadfrontendsample(descript[iPlayer1Car]);
        front_fade = -1;
        fade_palette(32);
        frontendsample(0x8000);
        frames = 0;
      }
      uiNavigationDirection = 0;
      iNextCarIndex = iCurrentCarSelectorPos + 1;
      while (fatkbhit())                      // KEYBOARD INPUT PROCESSING: Handle all user input and navigation
      {
        byInputKey = fatgetch();
        if (byInputKey < 0x20u) {
          if (byInputKey < 0xDu) {
            if (!byInputKey) {
              switch ((uint8)fatgetch()) {
                case 0x48u:
                case 0x4Bu:
                  goto LABEL_152;               // Arrow keys: Handle up/down/left/right navigation between cars
                case 0x4Du:
                case 0x50u:
                  goto LABEL_153;
                default:
                  continue;
              }
            }
          } else if (byInputKey <= 0xDu)        // Enter key: Confirm car selection or trigger car switching animation
          {
            if (iCurrentCarSelectorPos != 8 && iCurrentCarSelectorPos != iPlayer1Car || iCurrentCarSelectorPos == 8) {
              remove_frontendspeech();
              sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);
            }
            if (iCurrentCarSelectorPos == iPlayer1Car
              && SoundCard
              && frontendspeechhandle != -1
              && DIGISampleDone(frontendspeechhandle)) { //DIGISampleDone(*(int *)&DIGIHandle, frontendspeechhandle)) {
              frontendspeechhandle = -1;
              frontendsample(0x8000);
            }
            if (iCurrentCarSelectorPos == 8) {
              byMenuExitFlag = -1;
            } else if (iPlayer1Car != iCurrentCarSelectorPos
                   && (allocated_cars[iCurrentCarSelectorPos] < 2 || game_type == 1 && Race > 0)) {
              if (network_on) {
                car_request = iNextCarIndex;
                broadcast_mode = -9999;
                while (broadcast_mode)
                  UpdateSDL();
              } else {
                iDelayBeforeRotation = 0;
                iZoomSpeed = 2000;
                iSelectedCar = iCurrentCarSelectorPos;
                sfxsample(SOUND_SAMPLE_CAROUT, 0x8000);
              }
            }
          } else if (byInputKey == 27)          // Escape key: Exit to main menu
          {
            byMenuExitFlag = -1;
            remove_frontendspeech();
            sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);
          }
        } else if (byInputKey <= 0x20u) {                                       // Space key: Switch between Player 1 and Player 2 in two-player mode
          if (player_type == 2) {
            if (iActivePlayer) {
              iActivePlayer = 0;
              iDelayBeforeRotation = 0;
              iZoomSpeed = 2000;
              iSelectedCar = Players_Cars[player1_car];
            } else {
              iDelayBeforeRotation = 0;
              iActivePlayer = 1;
              iZoomSpeed = 2000;
              iSelectedCar = Players_Cars[player2_car];
            }
          }
        } else if (byInputKey < 0x2Du) {
          if (byInputKey == 43)
            LABEL_153:
          uiNavigationDirection = 1;
        } else {
          if (byInputKey <= 0x2Du)
            goto LABEL_152;
          if (byInputKey >= 0x3Du) {
            if (byInputKey <= 0x3Du)
              goto LABEL_153;
            if (byInputKey == 95)
              LABEL_152:
            uiNavigationDirection = 2;
          }
        }
        UpdateSDL();

      }
      if (uiNavigationDirection)              // Apply navigation input to move car selector cursor
      {
        if (uiNavigationDirection > 1) {
          if (--iCurrentCarSelectorPos < 0)
            iCurrentCarSelectorPos = 0;
        } else if (++iCurrentCarSelectorPos > 8) {
          iCurrentCarSelectorPos = 8;
        }
      }
      nRotationAngle = Car[0].nYaw + 32 * iStatAnimationFrame;// Update 3D car rotation angle for spinning animation
      nRotationAngle &= 0x3FFFu;
      Car[0].nYaw = nRotationAngle;

      UpdateSDL();
    } while (!byMenuExitFlag);                  // MAIN SELECTION LOOP - Handle UI rendering, input, and car switching
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
  fre((void **)&front_vga[7]);
  if (frontendspeechptr)
    fre((void **)&frontendspeechptr);
  front_fade = 0;
  if (iPlayer1Car >= CAR_DESIGN_AUTO) {
    ppCleanupTextureArray = cartex_vga;
    car_texs_loaded[CarDesigns[iPlayer1Car].carType] = -1;
    do {
      ppCleanupTexture = (void **)ppCleanupTextureArray++;
      fre(ppCleanupTexture);
    } while (ppCleanupTextureArray != &cartex_vga[16]);
    remove_mapsels();
  }
  if (iOriginalCarSelection >= 0)
    Players_Cars[player1_car] = iOriginalCarSelection;
}

//-------------------------------------------------------------------------------------------------
//00042D40
