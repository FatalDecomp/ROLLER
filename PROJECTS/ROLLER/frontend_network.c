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

static int NetworkGridRand(int *pSeed)
{
  uint32 uiSeed = (uint32)*pSeed;
  uiSeed = uiSeed * 1103515245u + 12345u;
  *pSeed = (int)uiSeed;
  return (int)((uiSeed >> 16) & 0x7FFFu);
}

static int NetworkGridRandRange(int iRange, int *pSeed)
{
  return (int)(((uint32)iRange * (uint32)NetworkGridRand(pSeed)) >> 15);
}

//-------------------------------------------------------------------------------------------------
//00049C70
void NetworkWait()
{
  int iPlayerLoop1; // esi
  int iPlayerLoop2; // esi
  int iPlayerDisplayLoop; // ebp
  int iPlayerIndex; // esi
  int iTextYPos; // edi
  int iCarType; // ecx
  int iCarTypeForSprite; // ebx
  unsigned int uiKeyPressed; // eax
  int iSavedScrSize; // [esp+0h] [ebp-2Ch]
  int iCarSpriteYOffset; // [esp+4h] [ebp-28h]
  int iContinueLoop; // [esp+8h] [ebp-24h]
  char *szCurrentPlayerName; // [esp+Ch] [ebp-20h]
  int iY; // [esp+10h] [ebp-1Ch]

  iSavedScrSize = scr_size;                     // Initialize network wait screen - save screen size and setup display
  front_fade = 0;
  tick_on = -1;
  frontend_on = -1;
  clear_network_game();
  netCD = 0;
  cd_error = 0;
  SVGA_ON = -1;
  network_test = 1;
  init_screen();
  front_vga[0] = (tBlockHeader *)load_picture("result.bm");// Load UI graphics: result screen, fonts, car sprites, and text tables
  front_vga[1] = (tBlockHeader *)load_picture("font2.bm");
  front_vga[2] = (tBlockHeader *)load_picture("smallcar.bm");
  front_vga[3] = (tBlockHeader *)load_picture("tabtext.bm");
  front_vga[15] = (tBlockHeader *)load_picture("font1.bm");
  iContinueLoop = -1;
  setpal("result.pal");
  // Restore palette for GPU rendering
  {
    extern tColor palette[];
    memcpy(pal_addr, palette, 256 * sizeof(tColor));
    palette_brightness = 32;
    MenuRenderer *mr = GetMenuRenderer();
    if (mr) {
      menu_render_load_blocks(mr, 0, front_vga[0], palette);
      menu_render_load_blocks(mr, 1, front_vga[1], palette);
      menu_render_load_blocks(mr, 2, front_vga[2], palette);
      menu_render_load_blocks(mr, 3, front_vga[3], palette);
      menu_render_load_blocks(mr, 15, front_vga[15], palette);
    }
  }
  if (network_on) {
    while (1) {
      UpdateSDL();
      if (!iContinueLoop)
        goto LABEL_83;
      if (switch_types)                       // Handle game type switching (championship/single race/team game)
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
      if (switch_same <= 0)
        break;                                  // Check for same-car mode activation

      // Force all players to use the same car type in same-car mode
      for (iPlayerLoop2 = 0; iPlayerLoop2 < players; iPlayerLoop2++) {
          Players_Cars[iPlayerLoop2] = switch_same - 666;  // Convert switch value to car type
      }
      //iPlayerLoop2 = 0;
      //if (players > 0) {
      //  iArrayOffset2 = 0;                      // Set same-car mode for all players (switch_same - 666 magic number)
      //  do {
      //    iArrayOffset2 += 4;
      //    ++iPlayerLoop2;
      //    *(int *)((char *)&infinite_laps + iArrayOffset2) = switch_same - 666;
      //  } while (iPlayerLoop2 < players);
      //}

      cheat_mode |= CHEAT_MODE_CLONES;

      if (Players_Cars[player1_car] < 0)      // Check if player1 car is invalid (negative) - disconnect from network
      {
        StartPressed = 0;
        time_to_start = 0;
        broadcast_mode = -670;
        while (broadcast_mode)
          ;
        iContinueLoop = 0;
      LABEL_25:
        --players_waiting;
      }
    LABEL_26:
      check_cars();
      {                                           // RENDER FRAME (GPU)
      MenuRenderer *mr = GetMenuRenderer();
      menu_render_begin_frame(mr);
      menu_render_background(mr, 0);             // Main network lobby display loop - show waiting screen
      sprintf(buffer, "%s: %i", &language_buffer[64], players);
      menu_render_text(mr, 1, buffer, font2_ascii, font2_offsets, 16, 4, 0x8Fu, 0, pal_addr);
      sprintf(buffer, "%s: %i", &language_buffer[256], TrackLoad);
      menu_render_text(mr, 1, buffer, font2_ascii, font2_offsets, 16, 24, 0x8Fu, 0, pal_addr);
      if (game_type)                          // Display game type text based on current mode
      {
        if ((unsigned int)game_type <= 1) {
          sprintf(buffer, "%s", &language_buffer[3520]);
        } else if (game_type == 2) {
          sprintf(buffer, "%s", &language_buffer[3712]);
        }
      } else {
        sprintf(buffer, "%s", &language_buffer[3648]);
      }
      menu_render_text(mr, 1, buffer, font2_ascii, font2_offsets, 200, 4, 0x8Fu, 1u, pal_addr);
      if (players_waiting == network_on)      // Show flashing "Ready to start" message when all players connected
      {
        if ((frames & 0xFu) < 8)
          menu_render_text(mr, 1, &language_buffer[4800], font2_ascii, font2_offsets, 200, 22, 0x8Fu, 1u, pal_addr);
        if (time_to_start)
          iContinueLoop = 0;
      }
      iPlayerDisplayLoop = 0;
      if (players > 0) {
        iPlayerIndex = 0;                       // Display player list with names, car selections, and connection status
        iY = 44;
        szCurrentPlayerName = player_names[0];
        iTextYPos = 49;
        do {                                       // Show flashing indicator for player1, solid for others
          if (player_started[iPlayerIndex] && (!iPlayerDisplayLoop && (frames & 0xFu) < 8 || iPlayerDisplayLoop > 0))
            menu_render_sprite(mr, 2, 0, 13, iY, 0, pal_addr);
          sprintf(buffer, "%i", iPlayerDisplayLoop + 1);
          iCarSpriteYOffset = 22 * iPlayerDisplayLoop;
          menu_render_text(mr, 1, buffer, font2_ascii, font2_offsets, 33, iTextYPos, 0x8Fu, 0, pal_addr);
          sprintf(buffer, "%s", szCurrentPlayerName);
          menu_render_text(mr, 1, buffer, font2_ascii, font2_offsets, 85, iTextYPos, 0x8Fu, 0, pal_addr);
          iCarType = Players_Cars[iPlayerIndex];
          if (iCarType >= 0)                  // Display car company name and sprite if valid car selected
          {
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
            menu_render_text(mr, 1, &language_buffer[4160], font2_ascii, font2_offsets, 218, iTextYPos, 0x8Fu, 0, pal_addr);
          }
          ++iPlayerIndex;
          iTextYPos += 22;
          ++iPlayerDisplayLoop;
          iY += 22;
          szCurrentPlayerName += 9;
        } while (iPlayerDisplayLoop < players);
      }
      if (time_to_start)
        iContinueLoop = 0;
      if (iContinueLoop) {
        show_received_mesage();
        menu_render_end_frame(mr);
        if (!front_fade) {
          front_fade = -1;                      // Initialize screen fade and network synchronization
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
      while (fatkbhit())                      // Handle keyboard input for network lobby
      {
        UpdateSDL();
        uiKeyPressed = fatgetch();
        if (uiKeyPressed < 0xD) {
          if (!uiKeyPressed)
            fatgetch();
        } else if (uiKeyPressed <= 0xD) {                                       // Enter key pressed - start race if all players ready
          if (players_waiting == network_on && !time_to_start) {
            iContinueLoop = 0;
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
        } else if (uiKeyPressed == 27 && !time_to_start && !restart_net)// Escape key pressed - leave network game
        {
          StartPressed = 0;
          time_to_start = 0;
          broadcast_mode = -670;
          while (broadcast_mode)
            UpdateSDL();
          iContinueLoop = 0;
          --players_waiting;
          no_clear = -1;
        }
      }
    }
    if (switch_same >= 0)
      goto LABEL_26;

    for (iPlayerLoop1 = 0; iPlayerLoop1 < players; iPlayerLoop1++) {
        Players_Cars[iPlayerLoop1] = -1;
    }
    //iPlayerLoop1 = 0;                           // Reset same-car mode - clear player settings
    //if (players > 0) {
    //  iArrayOffset1 = 0;
    //  do {
    //    iArrayOffset1 += 4;
    //    ++iPlayerLoop1;
    //    *(int *)((char *)&infinite_laps + iArrayOffset1) = -1;
    //  } while (iPlayerLoop1 < players);
    //}

    cheat_mode &= ~CHEAT_MODE_CLONES;

    switch_same = 0;
    StartPressed = 0;
    time_to_start = 0;
    broadcast_mode = -670;
    while (broadcast_mode)
      ;
    iContinueLoop = 0;
    goto LABEL_25;
  }
LABEL_83:
  int iTimer = ticks + 18;
  while (iTimer > ticks)                  // Wait loop for network synchronization timing
    ;
  if (time_to_start)                          // Final network synchronization before race starts
  {
    broadcast_mode = -314;
    while (broadcast_mode) {
      CheckNewNodes();
      BroadcastNews();
      UpdateSDL();
    }
    if (wConsoleNode == master)               // Master node waits for all client records, then broadcasts seed
    {
      int iLastRecordWaitLog = -1000;
      while (received_records < network_on) {
        if (frames - iLastRecordWaitLog >= 36) {
          iLastRecordWaitLog = frames;
          SDL_Log("[NET-START] master waiting for records received=%d expected=%d",
                  received_records, network_on);
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
    } else {                                           // Client nodes wait for random seed from master
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
  check_cars();                                 // Cleanup: fade screen, free graphics memory, restore screen size
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
  fre((void **)front_vga);
  fre((void **)&front_vga[1]);
  fre((void **)&front_vga[2]);
  fre((void **)&front_vga[3]);
  fre((void **)&front_vga[15]);
  scr_size = iSavedScrSize;
}

//-------------------------------------------------------------------------------------------------
//0004AB30
void restart_net_game()
{
  int iActualCompetitors; // edi
  int iGridIndex; // edx
  int iCompetitorLoop; // ecx
  int i; // eax
  int iTotalCars; // esi
  int iCarIndex; // edx
  int iGridOffset; // ecx
  int iNonCompSearch; // eax
  int j; // eax
  int iAISearch; // ebx
  int iRandRange; // eax
  int iFirstSwapPos; // ecx
  int iSecondRand; // eax
  int iSecondSwapPos; // eax
  int iTempCarId; // ebp
  int k; // esi
  int iHandicapPos; // edx
  int iHumanPos; // ebx
  int iOrderLoop; // eax
  int m; // eax
  int iAICarId; // ecx
  int iHumanCarId; // edx
  int iMaxCarOffset; // [esp+4h] [ebp-1Ch]

  SVGA_ON = -1;                                 // Initialize graphics and game state
  init_screen();
  winx = 0;
  winy = 0;
  winw = XMAX;
  mirror = 0;
  winh = YMAX;
  frontend_on = -1;
  time_to_start = 0;
  StartPressed = 0;
  tick_on = -1;
  load_language_file(szSelectEng, 0);           // Load language files for interface
  load_language_file(szConfigEng, 1);
  remove_messages(-1);
  reset_network(0);                             // Reset network and wait for broadcast mode to clear
  broadcast_mode = -667;
  while (broadcast_mode)
    UpdateSDL();
  no_clear = 0;
  if (!quit_game && !intro) {
    check_cars();                               // Check cars and wait for network if not quitting or in intro
    NetworkWait();
  }
  if (replaytype != 2 && !quit_game)
    AllocateCars();
  Race = ((uint8)TrackLoad - 1) & 7;            // Calculate current race number (0-7)
  if (game_type == 1 && !Race) {
    memset(championship_points, 0, sizeof(championship_points));// Reset championship statistics for new championship
    memset(team_points, 0, sizeof(team_points));
    memset(total_kills, 0, sizeof(total_kills));
    memset(total_fasts, 0, sizeof(total_fasts));
    memset(total_wins, 0, sizeof(total_wins));
    memset(team_kills, 0, sizeof(team_kills));
    memset(team_fasts, 0, sizeof(team_fasts));
    memset(team_wins, 0, sizeof(team_wins));
  }
  iActualCompetitors = competitors;
  if (competitors == 2)                       // Determine actual number of competitors based on game type
  {
    iActualCompetitors = players;
    if (players < 2)
      iActualCompetitors = competitors;
  }
  if (competitors == 1)
    iActualCompetitors = players;
  iGridIndex = 0;
  if (iActualCompetitors > 0) {                                             // Fill grid with competing cars (skip non-competitors)
    for (iCompetitorLoop = 0; iCompetitorLoop < iActualCompetitors; ++iCompetitorLoop) {
      for (i = iGridIndex; non_competitors[i]; ++i)
        ++iGridIndex;
      grid[iCompetitorLoop] = iGridIndex++;
    }
  }
  iTotalCars = iActualCompetitors;
  iCarIndex = 0;
  if (iActualCompetitors < numcars) {
    iGridOffset = 4 * iActualCompetitors;       // Fill remaining grid positions with AI cars
    iMaxCarOffset = 4 * numcars;
    do {
      for (iNonCompSearch = iCarIndex; !non_competitors[iNonCompSearch]; ++iNonCompSearch)
        ++iCarIndex;
      ++iTotalCars;
      grid[iGridOffset / 4u] = iCarIndex;
      iGridOffset += 4;
      ++iCarIndex;
    } while (iGridOffset < iMaxCarOffset);
  }
  if (game_type == 1 && Race > 0) {
    if (iActualCompetitors > 0) {
      for (j = 0; j < iActualCompetitors; ++j) {
          grid[j] = champorder[j];
      }
      //for (j = 0; j < iActualCompetitors; finished_car[j + 15] = teamorder[j + 7])
      //  ++j;
    }
  } else {
    racers = iActualCompetitors;
    int iNetworkGridSeed = random_seed;
    for (iAISearch = 0; iAISearch < 6 * iActualCompetitors; grid[iSecondSwapPos] = iTempCarId)// Shuffle grid positions randomly for non-championship races
    {
      iRandRange = network_on ? 0 : ROLLERrandRaw();
      //iFirstSwapPos = iRandRange % iActualCompetitors;  // Get random position within grid bounds
      iFirstSwapPos = network_on ? NetworkGridRandRange(iActualCompetitors, &iNetworkGridSeed) : GetHighOrderRand(iActualCompetitors, iRandRange);
      //iFirstSwapPos = (iActualCompetitors * iRandRange - (__CFSHL__((iActualCompetitors * iRandRange) >> 31, 15) + ((iActualCompetitors * iRandRange) >> 31 << 15))) >> 15;
      iSecondRand = network_on ? 0 : ROLLERrandRaw();
      //iSecondSwapPos = iSecondRand % iActualCompetitors;  // Get second random position within grid bounds
      iSecondSwapPos = network_on ? NetworkGridRandRange(iActualCompetitors, &iNetworkGridSeed) : GetHighOrderRand(iActualCompetitors, iSecondRand);
      //iSecondSwapPos = (iActualCompetitors * iSecondRand - (__CFSHL__((iActualCompetitors * iSecondRand) >> 31, 15) + ((iActualCompetitors * iSecondRand) >> 31 << 15))) >> 15;
      iTempCarId = grid[iFirstSwapPos];
      grid[iFirstSwapPos] = grid[iSecondSwapPos];
      ++iAISearch;
    }
    iActualCompetitors = racers;
    for (k = 0; k < players; ++k)             // Position human players based on difficulty level
    {                                           // Calculate starting position handicap based on level
      if (level && (cheat_mode & 2) == 0)
        iHandicapPos = iActualCompetitors - 2 * level * players;
      else
        iHandicapPos = iActualCompetitors - players;
      if (iHandicapPos < 0)
        iHandicapPos = 0;
      iHumanPos = 0;
      for (iOrderLoop = 0; !human_control[grid[iOrderLoop]]; ++iOrderLoop)// Find human player in grid and ensure proper positioning
        ++iHumanPos;
      if (iHumanPos < iHandicapPos) {
        for (m = iHandicapPos; ; ++m) {
          iAICarId = grid[m];
          if (!human_control[iAICarId])
            break;
          ++iHandicapPos;
        }
        iHumanCarId = grid[iHumanPos];
        grid[iHumanPos] = iAICarId;
        grid[m] = iHumanCarId;
      }
    }
  }
  StartPressed = 0;                             // Finalize restart state
  restart_net = 0;
  racers = iActualCompetitors;
}

//-------------------------------------------------------------------------------------------------
