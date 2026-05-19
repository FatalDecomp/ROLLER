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

static void frontend_car_select_run_snapshot(void);

void snapshot_render_menu_select_car(void)
{
  snapshot_setup_frontend_menu_state(0);
  frontend_car_select_run_snapshot();
}

static int iFrontendCarBlockIdxAy[7];
static int iFrontendCarOriginalCarSelection;
static int iFrontendCarActivePlayer;
static int iFrontendCarDelayBeforeRotation;
static int iFrontendCarZoomSpeed;
static int iFrontendCarZoomDistance;
static int iFrontendCarExitFlag;
static int iFrontendCarCurrentSelectorPos;
static int iFrontendCarSpeechPending;
static int iFrontendCarPlayer1Car;
static int iFrontendCarSelectedCar;
static char *szFrontendCarCurrentCompanyName;

static void frontend_car_select_begin_broadcast_wait(int iBroadcastMode)
{
  network_broadcast_wait_start(iBroadcastMode, 1);
}

static int frontend_car_select_update_broadcast_wait(void)
{
  if (!network_broadcast_wait_active())
    return 0;

  (void)network_broadcast_wait_update();
  return -1;
}

static void frontend_car_select_black_palette(void)
{
  palette_brightness = 0;
  for (int i = 0; i < 256; ++i) {
    pal_addr[i].byR = 0;
    pal_addr[i].byB = 0;
    pal_addr[i].byG = 0;
  }
}

static void frontend_car_select_request_exit(void)
{
  iFrontendCarExitFlag = -1;
  if (eFrontendCurrentState == eFRONTEND_STATE_CAR_SELECT)
    eFrontendNextState = eFRONTEND_STATE_MAIN_MENU;
}

void frontend_car_select_enter(void)
{
  eCarType currentCarType;
  int iLoadCarTextures;
  int iCartexLoaded;
  int iCarIdx;
  int iCarPieOffset;
  int iInvertedPieValue;

  iFrontendCarExitFlag = 0;
  iFrontendCarOriginalCarSelection = -1;
  iFrontendCarPlayer1Car = Players_Cars[player1_car];

  fade_palette(0);
  front_fade = 0;
  front_vga[3] = (tBlockHeader *)load_picture("carnames.bm");
  front_vga[7] = (tBlockHeader *)load_picture("selcar2.bm");
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

  if (game_type == 1) {
    Race = ((uint8)TrackLoad - 1) & 7;
    if ((((uint8)TrackLoad - 1) & 7) != 0)
      iFrontendCarOriginalCarSelection = iFrontendCarPlayer1Car;
  }

  if (iFrontendCarPlayer1Car >= CAR_DESIGN_AUTO) {
    currentCarType = CarDesigns[iFrontendCarPlayer1Car].carType;
    iLoadCarTextures = 1;
    iCartexLoaded = car_texs_loaded[currentCarType];
    if (iCartexLoaded == -1) {
      LoadCarTexture(currentCarType, 1u);
      iLoadCarTextures = 2;
      car_texmap[iFrontendCarPlayer1Car] = 1;
      car_texs_loaded[currentCarType] = 1;
    } else {
      car_texmap[iFrontendCarPlayer1Car] = iCartexLoaded;
    }
    LoadCarTextures = iLoadCarTextures;
  }

  if (iFrontendCarPlayer1Car < CAR_DESIGN_AUTO) {
    for (int i = 0; i < 7; ++i)
      iFrontendCarBlockIdxAy[i] = 8;
  } else {
    iCarIdx = 0;
    iCarPieOffset = 7 * iFrontendCarPlayer1Car;
    do {
      ++iCarIdx;
      iInvertedPieValue = 9 - car_pies[iCarPieOffset++];
      iFrontendCarBlockIdxAy[iCarIdx - 1] = iInvertedPieValue;
    } while (iCarIdx < 7);
  }

  iFrontendCarZoomDistance = 2000;
  iFrontendCarZoomSpeed = -2000;

  if (iFrontendCarPlayer1Car >= CAR_DESIGN_SUICYCO)
    iFrontendCarCurrentSelectorPos = 8;
  else
    iFrontendCarCurrentSelectorPos = iFrontendCarPlayer1Car;
  if (iFrontendCarCurrentSelectorPos < 0)
    iFrontendCarCurrentSelectorPos = 8;

  iFrontendCarDelayBeforeRotation = 36;
  iFrontendCarActivePlayer = 0;
  iFrontendCarSpeechPending = 0;
  iFrontendCarSelectedCar = 0;
  frames = 0;

  szFrontendCarCurrentCompanyName = NULL;
  if (iFrontendCarOriginalCarSelection >= 0)
    szFrontendCarCurrentCompanyName = CompanyNames[iFrontendCarOriginalCarSelection];
}

void frontend_car_select_update(void)
{
  eCarType eNewCarType;
  int iTextureLoadedStatus;
  int iLoadTextureFlag;
  int iCarAllocationStatus;
  int iCarDesignIndex;
  uint8 **ppTextureArray;
  void **ppCurrentTexture;
  int iPlayerCarIndex;
  int iStatAnimationFrame;
  int bStartedFadeIn;
  unsigned int uiNavigationDirection;
  int iNextCarIndex;
  uint8 byInputKey;
  int16 nRotationAngle;
  int iPieChartIndex;
  int iCarStatsOffset;
  int iTargetPieValue;
  int iCurrentPieValue;
  int iNextPieValue;
  int iNewZoomDistance;
  int iPlayerLoopCounter;
  unsigned int uiNetworkLoop;
  int iPieChartY;
  unsigned int uiNetworkPlayerIndex;
  char *szPlayerName;
  int iNetworkPlayerCount;
  float fCarDrawDistance;
  int iPlayerNameX;
  unsigned int uiPlayerIndex;

  // Apply game type switch
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
  bStartedFadeIn = 0;

  if (SoundCard && front_fade && iFrontendCarSpeechPending && sfxplaying(SOUND_SAMPLE_CARIN) == 0) {
    frontendsample(0x8000);
    iFrontendCarSpeechPending = 0;
  }

  // RENDER FRAME
  {
    MenuRenderer *mr = GetMenuRenderer();
    menu_render_begin_frame(mr);
    if (!front_fade) {
      front_fade = -1;
      bStartedFadeIn = -1;
      menu_render_begin_fade(mr, 1, 32);
    }
    menu_render_background(mr, 0);
    if (player_type == 2) {
      if (iFrontendCarActivePlayer)
        menu_render_sprite(mr, 1, 6, head_x, head_y, 0, pal_addr);
      else
        menu_render_sprite(mr, 1, 5, head_x, head_y, 0, pal_addr);
      menu_render_sprite(mr, 1, 7, 200, 56, 0, pal_addr);
    } else {
      menu_render_sprite(mr, 1, 1, head_x, head_y, 0, pal_addr);
    }
    menu_render_sprite(mr, 6, 0, 36, 2, 0, pal_addr);
    if (iFrontendCarPlayer1Car < CAR_DESIGN_AUTO) {
      menu_render_text(mr, 15, &language_buffer[4160], font1_ascii, font1_offsets, 400, 200, 0xE7u, 1u, pal_addr);
    } else {
      menu_render_load_car_mesh(mr, iFrontendCarPlayer1Car, palette);
      if (iFrontendCarPlayer1Car == CAR_DESIGN_F1WACK) {
        menu_render_draw_car_preview(mr, 1280.0f, 6000.0f, Car[0].nYaw, PREVIEW_X, CAR_PREVIEW_Y, PREVIEW_W, PREVIEW_H);
      } else if (iFrontendCarDelayBeforeRotation) {
        menu_render_draw_car_preview(mr, 1280.0f, 2200.0f, Car[0].nYaw, PREVIEW_X, CAR_PREVIEW_Y, PREVIEW_W, PREVIEW_H);
      } else {
        fCarDrawDistance = (float)iFrontendCarZoomDistance;
        menu_render_draw_car_preview(mr, 1280.0f, fCarDrawDistance, Car[0].nYaw, PREVIEW_X, CAR_PREVIEW_Y, PREVIEW_W, PREVIEW_H);
      }
      if (iFrontendCarPlayer1Car < CAR_DESIGN_SUICYCO)
        menu_render_sprite(mr, 3, iFrontendCarPlayer1Car, 190, 356, 0, pal_addr);
    }
    menu_render_set_layer(mr, MENU_LAYER_FOREGROUND);
    menu_render_sprite(mr, 7, 0, 560, 20, 0, pal_addr);
    uiNetworkLoop = 0;
    iPieChartY = 19;
    do {
      menu_render_sprite(mr, 7, iFrontendCarBlockIdxAy[uiNetworkLoop / 4], 568, iPieChartY, 0, pal_addr);
      uiNetworkLoop += 4;
      iPieChartY += 51;
    } while (uiNetworkLoop != 28);
    menu_render_sprite(mr, 5, player_type, -4, 247, 0, pal_addr);
    menu_render_sprite(mr, 5, game_type + 5, 135, 247, 0, pal_addr);
    menu_render_sprite(mr, 4, 0, 76, 257, -1, pal_addr);
    if (iFrontendCarCurrentSelectorPos >= 8) {
      menu_render_sprite(mr, 6, 4, 62, 336, -1, pal_addr);
    } else {
      menu_render_sprite(mr, 6, 2, 62, 336, -1, pal_addr);
      menu_render_text(mr, 2, "~", font2_ascii, font2_offsets,
                       sel_posns[iFrontendCarCurrentSelectorPos].x,
                       sel_posns[iFrontendCarCurrentSelectorPos].y, 0x8Fu, 0, pal_addr);
    }
    menu_render_text(mr, 2, "AUTO ARIEL",   font2_ascii, font2_offsets, sel_posns[0].x + 132, sel_posns[0].y + 7, 0x8Fu, 2u, pal_addr);
    menu_render_text(mr, 2, "DESILVA",      font2_ascii, font2_offsets, sel_posns[1].x + 132, sel_posns[1].y + 7, 0x8Fu, 2u, pal_addr);
    menu_render_text(mr, 2, "PULSE",        font2_ascii, font2_offsets, sel_posns[2].x + 132, sel_posns[2].y + 7, 0x8Fu, 2u, pal_addr);
    menu_render_text(mr, 2, "GLOBAL",       font2_ascii, font2_offsets, sel_posns[3].x + 132, sel_posns[3].y + 7, 0x8Fu, 2u, pal_addr);
    menu_render_text(mr, 2, "MILLION PLUS", font2_ascii, font2_offsets, sel_posns[4].x + 132, sel_posns[4].y + 7, 0x8Fu, 2u, pal_addr);
    menu_render_text(mr, 2, "MISSION",      font2_ascii, font2_offsets, sel_posns[5].x + 132, sel_posns[5].y + 7, 0x8Fu, 2u, pal_addr);
    menu_render_text(mr, 2, "ZIZIN",        font2_ascii, font2_offsets, sel_posns[6].x + 132, sel_posns[6].y + 7, 0x8Fu, 2u, pal_addr);
    menu_render_text(mr, 2, "REISE WAGON",  font2_ascii, font2_offsets, sel_posns[7].x + 132, sel_posns[7].y + 7, 0x8Fu, 2u, pal_addr);
    if (iFrontendCarCurrentSelectorPos < 8 && network_on && (cheat_mode & 0x4000) == 0) {
      menu_render_text(mr, 15, &language_buffer[4672], font1_ascii, font1_offsets, 380, 380, 0x8Fu, 2u, pal_addr);
      if (allocated_cars[iFrontendCarCurrentSelectorPos]) {
        iPlayerNameX = 385;
        iNetworkPlayerCount = 0;
        if (players > 0) {
          uiNetworkPlayerIndex = 0;
          szPlayerName = player_names[0];
          do {
            if (iFrontendCarCurrentSelectorPos == Players_Cars[uiNetworkPlayerIndex / 4]) {
              uiPlayerIndex = (iPlayerNameX == 385) ? 0u : 2u;
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
    if (iFrontendCarOriginalCarSelection >= 0) {
      if (iFrontendCarActivePlayer)
        sprintf(buffer, "%s %s", &language_buffer[2880], CompanyNames[Players_Cars[player2_car]]);
      else
        sprintf(buffer, "%s %s", &language_buffer[2816], szFrontendCarCurrentCompanyName);
      menu_render_scaled_text(mr, 15, buffer, font1_ascii, font1_offsets, 375, 316, 231, 1u, 170, 550, pal_addr);
    }
    show_received_mesage();
    menu_render_end_frame(mr);
    if (SnapshotShouldStop())
      return;
  }

  if (frontend_car_select_update_broadcast_wait())
    return;

  // ANIMATION UPDATE: pie chart
  if (iFrontendCarDelayBeforeRotation) {
    if (iFrontendCarPlayer1Car >= CAR_DESIGN_AUTO) {
      iPieChartIndex = 0;
      iCarStatsOffset = 7 * iFrontendCarPlayer1Car;
      do {
        iTargetPieValue = 9 - car_pies[iCarStatsOffset];
        iCurrentPieValue = iFrontendCarBlockIdxAy[iPieChartIndex];
        if (iCurrentPieValue != iTargetPieValue) {
          iNextPieValue = iCurrentPieValue + 1;
          iFrontendCarBlockIdxAy[iPieChartIndex] = iNextPieValue;
          if (iNextPieValue > 8)
            iFrontendCarBlockIdxAy[iPieChartIndex] = 1;
        }
        ++iPieChartIndex;
        ++iCarStatsOffset;
      } while (iPieChartIndex < 7);
    }
  } else {
    for (int i = 0; i < 7; ++i) {
      iFrontendCarBlockIdxAy[i]++;
      if (iFrontendCarBlockIdxAy[i] > 8)
        iFrontendCarBlockIdxAy[i] = 1;
    }
  }

  // ZOOM ANIMATION
  if (!iFrontendCarDelayBeforeRotation) {
    iNewZoomDistance = iStatAnimationFrame * iFrontendCarZoomSpeed + iFrontendCarZoomDistance;
    iFrontendCarZoomDistance = iNewZoomDistance;
    if (iNewZoomDistance <= 40000) {
      if (iNewZoomDistance < 4000) {
        iFrontendCarZoomDistance = 4000;
        iFrontendCarDelayBeforeRotation = 72;
      }
    } else {
      iCarAllocationStatus = allocated_cars[iFrontendCarSelectedCar];
      iFrontendCarZoomDistance = 40000;
      iFrontendCarZoomSpeed = -iFrontendCarZoomSpeed;
      if (iCarAllocationStatus < 2) {
        if (iFrontendCarPlayer1Car >= CAR_DESIGN_AUTO) {
          iCarDesignIndex = iFrontendCarPlayer1Car;
          ppTextureArray = cartex_vga;
          car_texs_loaded[CarDesigns[iCarDesignIndex].carType] = -1;
          do {
            ppCurrentTexture = (void **)ppTextureArray++;
            fre(ppCurrentTexture);
          } while (ppTextureArray != &cartex_vga[16]);
          remove_mapsels();
          remove_frontendspeech();
          iFrontendCarSpeechPending = 0;
        }
        if (game_type == 1 && Race > 0) {
          iFrontendCarPlayer1Car = iFrontendCarSelectedCar;
        } else {
          iPlayerCarIndex = iFrontendCarActivePlayer ? player2_car : player1_car;
          iFrontendCarPlayer1Car = iFrontendCarSelectedCar;
          Players_Cars[iPlayerCarIndex] = iFrontendCarSelectedCar;
        }
        if (iFrontendCarPlayer1Car >= CAR_DESIGN_AUTO) {
          eNewCarType = CarDesigns[iFrontendCarPlayer1Car].carType;
          iTextureLoadedStatus = car_texs_loaded[eNewCarType];
          if (iTextureLoadedStatus == -1) {
            LoadCarTexture(eNewCarType, 1u);
            car_texmap[iFrontendCarPlayer1Car] = 1;
            car_texs_loaded[eNewCarType] = 1;
            iLoadTextureFlag = 2;
          } else {
            car_texmap[iFrontendCarPlayer1Car] = iTextureLoadedStatus;
            iLoadTextureFlag = 1;
          }
          LoadCarTextures = iLoadTextureFlag;
          if (!network_on)
            check_cars();
        }
      } else {
        iFrontendCarPlayer1Car = iFrontendCarActivePlayer
                                   ? Players_Cars[player2_car]
                                   : Players_Cars[player1_car];
        iFrontendCarSelectedCar = iFrontendCarPlayer1Car;
      }
      if (iFrontendCarPlayer1Car >= CAR_DESIGN_AUTO) {
        sfxsample(SOUND_SAMPLE_CARIN, 0x8000);
        iFrontendCarSpeechPending = 0;
        if (iFrontendCarPlayer1Car < CAR_DESIGN_SUICYCO) {
          loadfrontendsample(descript[iFrontendCarPlayer1Car]);
          if (SamplePtr[SOUND_SAMPLE_CARIN])
            iFrontendCarSpeechPending = -1;
          else
            frontendsample(0x8000);
        }
      }
      frontend_car_select_begin_broadcast_wait(-1);
      frames = 0;
    }
  }

  if (frontend_car_select_update_broadcast_wait())
    return;

  // Network car request from another player
  if (car_request < 0) {
    iFrontendCarDelayBeforeRotation = 0;
    iFrontendCarZoomSpeed = 2000;
    iFrontendCarSelectedCar = -car_request - 1;
    sfxsample(SOUND_SAMPLE_CAROUT, 0x8000);
    iFrontendCarSpeechPending = 0;
    car_request = 0;
    if ((cheat_mode & 0x4000) != 0)
      switch_same = iFrontendCarSelectedCar + 666;
  }

  // Cheat: force all players to same car
  if (switch_same > 0) {
    if (switch_same - 666 != Players_Cars[player1_car]) {
      iFrontendCarSelectedCar = switch_same - 666;
      iFrontendCarZoomSpeed = 2000;
      iFrontendCarDelayBeforeRotation = 0;
      sfxsample(SOUND_SAMPLE_CAROUT, 0x8000);
      iFrontendCarSpeechPending = 0;
      for (iPlayerLoopCounter = 0; iPlayerLoopCounter < players; iPlayerLoopCounter++)
        Players_Cars[iPlayerLoopCounter] = switch_same - 666;
      cheat_mode |= CHEAT_MODE_CLONES;
    }
  } else if (switch_same < 0) {
    switch_same = 0;
    iFrontendCarDelayBeforeRotation = 0;
    iFrontendCarZoomSpeed = 2000;
    iFrontendCarSelectedCar = -1;
    sfxsample(SOUND_SAMPLE_CAROUT, 0x8000);
    iFrontendCarSpeechPending = 0;
    cheat_mode &= ~CHEAT_MODE_CLONES;
  }

  if (switch_sets) {
    iFrontendCarSelectedCar = iFrontendCarPlayer1Car;
    iFrontendCarDelayBeforeRotation = 0;
    iFrontendCarZoomSpeed = 2000;
    sfxsample(SOUND_SAMPLE_CAROUT, 0x8000);
    iFrontendCarSpeechPending = 0;
    switch_sets = 0;
  }

  // On the first frame of fade-in, load and start speech
  if (bStartedFadeIn) {
    if (iFrontendCarPlayer1Car >= CAR_DESIGN_AUTO && iFrontendCarPlayer1Car < CAR_DESIGN_SUICYCO)
      loadfrontendsample(descript[iFrontendCarPlayer1Car]);
    iFrontendCarSpeechPending = 0;
    frontendsample(0x8000);
    frames = 0;
  }

  // KEYBOARD INPUT
  uiNavigationDirection = 0;
  iNextCarIndex = iFrontendCarCurrentSelectorPos + 1;
  while (fatkbhit()) {
    byInputKey = fatgetch();
    if (byInputKey < 0x20u) {
      if (byInputKey < 0xDu) {
        if (!byInputKey) {
          switch ((uint8)fatgetch()) {
            case 0x48u:
            case 0x4Bu:
              uiNavigationDirection = 2;
              break;
            case 0x4Du:
            case 0x50u:
              uiNavigationDirection = 1;
              break;
            default:
              break;
          }
        }
      } else if (byInputKey <= 0xDu) {
        // Enter key
        if (iFrontendCarCurrentSelectorPos != 8 && iFrontendCarCurrentSelectorPos != iFrontendCarPlayer1Car
            || iFrontendCarCurrentSelectorPos == 8) {
          remove_frontendspeech();
          iFrontendCarSpeechPending = 0;
          sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);
        }
        if (iFrontendCarCurrentSelectorPos == iFrontendCarPlayer1Car
            && SoundCard
            && frontendspeechhandle != -1
            && DIGISampleDone(frontendspeechhandle)) {
          frontendspeechhandle = -1;
          frontendsample(0x8000);
        }
        if (iFrontendCarCurrentSelectorPos == 8) {
          frontend_car_select_request_exit();
        } else if (iFrontendCarPlayer1Car != iFrontendCarCurrentSelectorPos
                   && (allocated_cars[iFrontendCarCurrentSelectorPos] < 2
                       || (game_type == 1 && Race > 0))) {
          if (network_on) {
            car_request = iNextCarIndex;
            frontend_car_select_begin_broadcast_wait(-9999);
            return;
          } else {
            iFrontendCarDelayBeforeRotation = 0;
            iFrontendCarZoomSpeed = 2000;
            iFrontendCarSelectedCar = iFrontendCarCurrentSelectorPos;
            sfxsample(SOUND_SAMPLE_CAROUT, 0x8000);
            iFrontendCarSpeechPending = 0;
          }
        }
      } else if (byInputKey == 27) {
        // Escape: exit and restore car
        remove_frontendspeech();
        iFrontendCarSpeechPending = 0;
        sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);
        frontend_car_select_request_exit();
      }
    } else if (byInputKey <= 0x20u) {
      // Space: switch active player in two-player mode
      if (player_type == 2) {
        if (iFrontendCarActivePlayer) {
          iFrontendCarActivePlayer = 0;
          iFrontendCarDelayBeforeRotation = 0;
          iFrontendCarZoomSpeed = 2000;
          iFrontendCarSelectedCar = Players_Cars[player1_car];
          iFrontendCarSpeechPending = 0;
        } else {
          iFrontendCarActivePlayer = 1;
          iFrontendCarDelayBeforeRotation = 0;
          iFrontendCarZoomSpeed = 2000;
          iFrontendCarSelectedCar = Players_Cars[player2_car];
          iFrontendCarSpeechPending = 0;
        }
      }
    } else if (byInputKey < 0x2Du) {
      if (byInputKey == 43)  // '+'
        uiNavigationDirection = 1;
    } else {
      if (byInputKey == 0x2Du)       // '-'
        uiNavigationDirection = 2;
      else if (byInputKey >= 0x3Du) {
        if (byInputKey == 0x3Du)     // '='
          uiNavigationDirection = 1;
        else if (byInputKey == 95)   // '_'
          uiNavigationDirection = 2;
      }
    }

    if (iFrontendCarExitFlag)
      break;
  }

  // Apply navigation
  if (uiNavigationDirection > 1) {
    if (--iFrontendCarCurrentSelectorPos < 0)
      iFrontendCarCurrentSelectorPos = 0;
  } else if (uiNavigationDirection == 1) {
    if (++iFrontendCarCurrentSelectorPos > 8)
      iFrontendCarCurrentSelectorPos = 8;
  }

  // Rotate 3D car preview
  nRotationAngle = Car[0].nYaw + 32 * iStatAnimationFrame;
  nRotationAngle &= 0x3FFFu;
  Car[0].nYaw = nRotationAngle;
}

void frontend_car_select_exit(void)
{
  uint8 **ppCleanupTextureArray;
  void **ppCleanupTexture;

  if (!SnapshotShouldStop()) {
    MenuRenderer *mr = GetMenuRenderer();
    menu_render_begin_fade(mr, 0, 32);
    menu_render_fade_wait(mr, fade_redraw_bg, mr);
    frontend_car_select_black_palette();
  }
  fre((void **)&front_vga[7]);
  remove_frontendspeech();
  front_fade = 0;
  if (iFrontendCarPlayer1Car >= CAR_DESIGN_AUTO) {
    ppCleanupTextureArray = cartex_vga;
    car_texs_loaded[CarDesigns[iFrontendCarPlayer1Car].carType] = -1;
    do {
      ppCleanupTexture = (void **)ppCleanupTextureArray++;
      fre(ppCleanupTexture);
    } while (ppCleanupTextureArray != &cartex_vga[16]);
    remove_mapsels();
  }
  if (iFrontendCarOriginalCarSelection >= 0)
    Players_Cars[player1_car] = iFrontendCarOriginalCarSelection;

  if (eFrontendCurrentState == eFRONTEND_STATE_CAR_SELECT)
    frontend_menu_resume_from_child();
}

//-------------------------------------------------------------------------------------------------
//00041CA0
static void frontend_car_select_run_snapshot(void)
{
  frontend_car_select_enter();
  while (!iFrontendCarExitFlag && !SnapshotShouldStop()) {
    frontend_car_select_update();
    if (!iFrontendCarExitFlag && !SnapshotShouldStop())
      UpdateSDLWindow();
  }
  if (!SnapshotShouldStop())
    frontend_car_select_exit();
}

//-------------------------------------------------------------------------------------------------
