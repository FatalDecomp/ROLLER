#include "frontend.h"
#include "graphics.h"
#include "3d.h"
#include "func2.h"
#include "sound.h"
#include "roller.h"
#include "rollersound.h"
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
#include <ctype.h>
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

static int iFrontendTrackAnimationTimer = 0;
static int iFrontendTrackSelectedTrack = 0;
static int iFrontendTrackCurrentTrack = 0;
static int iFrontendTrackSoundFlag = 0;
static int iFrontendTrackYaw = 0;
static int iFrontendTrackExitFlag = 0;
static int iFrontendTrackExitFading = 0;
static int iFrontendTrackSpeechPending = 0;
static float fFrontendTrackZoom = 0.0f;
static float fFrontendTrackAnimatedZoom = 0.0f;
static float fFrontendTrackTargetZoom = 0.0f;

#define FRONTEND_TRACK_ARROW_SLOT 12
#define FRONTEND_TRACK_COMMUNITY_FONT_SLOT 10
#define FRONTEND_TRACK_VISIBLE_COMMUNITY_ROWS 8
#define FRONTEND_TRACK_COMMUNITY_LIST_LEFT 58
#define FRONTEND_TRACK_COMMUNITY_LIST_RIGHT 168
#define FRONTEND_TRACK_SELECTED_NAME_RIGHT 470
#define MENU_COLOR_RED 0xE7u

//-------------------------------------------------------------------------------------------------

static const char *s_aszFrontendTrackNames[8] = {
    "AUTO ARIEL",
    "DESILVA",
    "PULSE",
    "GLOBAL",
    "MILLION PLUS",
    "MISSION",
    "ZIZIN",
    "REISE WAGON"
};

//-------------------------------------------------------------------------------------------------

static void frontend_track_select_run_snapshot(void);

//-------------------------------------------------------------------------------------------------

static void frontend_track_select_display_name(char *szDisplayName,
                                               size_t uiDisplayNameSize,
                                               const char *szFileName,
                                               int iCompact)
{
  size_t uiNameLen;

  if (!uiDisplayNameSize)
    return;

  strncpy(szDisplayName, szFileName, uiDisplayNameSize - 1);
  szDisplayName[uiDisplayNameSize - 1] = '\0';

  uiNameLen = strlen(szDisplayName);
  if (uiNameLen > 4 &&
      szDisplayName[uiNameLen - 4] == '.' &&
      toupper((unsigned char)szDisplayName[uiNameLen - 3]) == 'T' &&
      toupper((unsigned char)szDisplayName[uiNameLen - 2]) == 'R' &&
      toupper((unsigned char)szDisplayName[uiNameLen - 1]) == 'K') {
    szDisplayName[uiNameLen - 4] = '\0';
    uiNameLen -= 4;
  }

  if (iCompact && uiNameLen > 13 && uiDisplayNameSize > 14) {
    szDisplayName[11] = '.';
    szDisplayName[12] = '.';
    szDisplayName[13] = '.';
    szDisplayName[14] = '\0';
  }
}

//-------------------------------------------------------------------------------------------------

static int frontend_track_select_is_community(void)
{
  return TrackLoad == TRACK_LOAD_COMMUNITY;
}

//-------------------------------------------------------------------------------------------------

static void frontend_track_select_show_community_selection(void)
{
  int iMaxTop = g_iCommunityTrackCount - FRONTEND_TRACK_VISIBLE_COMMUNITY_ROWS;

  if (iMaxTop < 0)
    iMaxTop = 0;

  if (g_iCommunityTrackSel < 0) {
    g_iCommunityTrackTop = 0;
    iFrontendTrackSelectedTrack = 8;
    g_uiCommunityTrackCRC = 0;
    return;
  }

  if (g_iCommunityTrackSel < g_iCommunityTrackTop)
    g_iCommunityTrackTop = g_iCommunityTrackSel;
  if (g_iCommunityTrackSel >=
      g_iCommunityTrackTop + FRONTEND_TRACK_VISIBLE_COMMUNITY_ROWS)
    g_iCommunityTrackTop = g_iCommunityTrackSel -
                           FRONTEND_TRACK_VISIBLE_COMMUNITY_ROWS + 1;

  if (g_iCommunityTrackTop > iMaxTop)
    g_iCommunityTrackTop = iMaxTop;
  if (g_iCommunityTrackTop < 0)
    g_iCommunityTrackTop = 0;

  iFrontendTrackSelectedTrack = g_iCommunityTrackSel - g_iCommunityTrackTop;
  if (iFrontendTrackSelectedTrack < 0)
    iFrontendTrackSelectedTrack = 0;
  if (iFrontendTrackSelectedTrack >= FRONTEND_TRACK_VISIBLE_COMMUNITY_ROWS)
    iFrontendTrackSelectedTrack = FRONTEND_TRACK_VISIBLE_COMMUNITY_ROWS - 1;

  g_uiCommunityTrackCRC = community_track_crc(community_track_path());
}

//-------------------------------------------------------------------------------------------------

static void frontend_track_select_clamp_community_cursor(void)
{
  int iLastVisibleRow;

  if (g_iCommunityTrackCount <= 0) {
    g_iCommunityTrackTop = 0;
    iFrontendTrackSelectedTrack = 8;
    return;
  }

  if (iFrontendTrackSelectedTrack >= 8)
    return;

  if (iFrontendTrackSelectedTrack < 0)
    iFrontendTrackSelectedTrack = 0;

  iLastVisibleRow = g_iCommunityTrackCount - g_iCommunityTrackTop - 1;
  if (iLastVisibleRow >= FRONTEND_TRACK_VISIBLE_COMMUNITY_ROWS)
    iLastVisibleRow = FRONTEND_TRACK_VISIBLE_COMMUNITY_ROWS - 1;
  if (iFrontendTrackSelectedTrack > iLastVisibleRow)
    iFrontendTrackSelectedTrack = iLastVisibleRow;
}

//-------------------------------------------------------------------------------------------------

static void frontend_track_select_begin_track_animation(void)
{
  if (iFrontendTrackAnimationTimer)
    iFrontendTrackAnimationTimer = 0;
  else if (fFrontendTrackTargetZoom < 0.0f)
    fFrontendTrackTargetZoom = -fFrontendTrackTargetZoom;
}

//-------------------------------------------------------------------------------------------------

static void frontend_track_select_select_community_row(int iRow)
{
  const char *szPath;
  int iTrackIdx = g_iCommunityTrackTop + iRow;

  if (iTrackIdx < 0 || iTrackIdx >= g_iCommunityTrackCount)
    return;

  g_iCommunityTrackSel = iTrackIdx;
  szPath = community_track_path();
  g_uiCommunityTrackCRC = community_track_crc(szPath);
  iFrontendTrackCurrentTrack = TRACK_LOAD_COMMUNITY;
  frontend_track_select_begin_track_animation();
}

//-------------------------------------------------------------------------------------------------

void snapshot_render_menu_select_track(void)
{
  snapshot_setup_frontend_menu_state(0);
  frontend_track_select_run_snapshot();
}

//-------------------------------------------------------------------------------------------------

static void frontend_track_select_begin_broadcast_wait(int iBroadcastMode)
{
  network_broadcast_wait_start(iBroadcastMode, 1);
}

//-------------------------------------------------------------------------------------------------

static int frontend_track_select_update_broadcast_wait(void)
{
  if (!network_broadcast_wait_active())
    return 0;

  (void)network_broadcast_wait_update();
  return -1;
}

//-------------------------------------------------------------------------------------------------

static void frontend_track_select_black_palette(void)
{
  palette_brightness = 0;
  for (int i = 0; i < 256; ++i) {
    pal_addr[i].byR = 0;
    pal_addr[i].byB = 0;
    pal_addr[i].byG = 0;
  }
}

//-------------------------------------------------------------------------------------------------

static void frontend_track_select_request_exit(void)
{
  iFrontendTrackExitFlag = -1;
  if (eFrontendCurrentState == eFRONTEND_STATE_TRACK_SELECT) {
    MenuRenderer *mr = GetMenuRenderer();
    menu_render_begin_fade(mr, 0, 32);
    iFrontendTrackExitFading = 1;
  }
}

//-------------------------------------------------------------------------------------------------

static void frontend_track_select_apply_type_switch(void)
{
  if (!switch_types)
    return;

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

//-------------------------------------------------------------------------------------------------

static void frontend_track_select_apply_same_car_switch(void)
{
  if (switch_same > 0) {
    for (int i = 0; i < players; ++i)
      Players_Cars[i] = switch_same - 666;
    cheat_mode |= CHEAT_MODE_CLONES;
  } else if (switch_same < 0) {
    switch_same = 0;
    for (int i = 0; i < players; ++i)
      Players_Cars[i] = -1;
    cheat_mode &= ~CHEAT_MODE_CLONES;
  }
}

//-------------------------------------------------------------------------------------------------

static void frontend_track_select_draw(int *piBlockIdx, int *piStartedFadeIn)
{
  int iLapsForLevel;
  int iBlockIdx;
  MenuRenderer *mr = GetMenuRenderer();

  *piStartedFadeIn = 0;

  menu_render_begin_frame(mr);
  if (!front_fade) {
    front_fade = -1;
    *piStartedFadeIn = -1;
    menu_render_begin_fade(mr, 1, 32);
  }
  menu_render_background(mr, 0);
  menu_render_sprite(mr, 1, 2, head_x, head_y, 0, pal_addr);
  if (cup_won)
    menu_render_sprite(mr, 1, 8, 480, 388, 0, pal_addr);
  menu_render_sprite(mr, 6, 0, 36, 2, 0, pal_addr);
  menu_render_sprite(mr, 5, player_type, -4, 247, 0, pal_addr);
  menu_render_sprite(mr, 5, game_type + 5, 135, 247, 0, pal_addr);
  menu_render_sprite(mr, 4, 1, 76, 257, -1, pal_addr);

  if (iFrontendTrackSelectedTrack >= 8) {
    menu_render_sprite(mr, 6, 4, 62, 336, -1, pal_addr);
  } else {
    menu_render_sprite(mr, 6, 2, 62, 336, -1, pal_addr);
    menu_render_text(mr, 2, "~", font2_ascii, font2_offsets,
                     sel_posns[iFrontendTrackSelectedTrack].x,
                     sel_posns[iFrontendTrackSelectedTrack].y, 0x8Fu, 0,
                     pal_addr);
  }

  if (frontend_track_select_is_community()) {
    int iArrowX = sel_posns[0].x + 155;
    int iUpArrowBlock = g_iCommunityTrackTop > 0 ? 79 : 78;
    int iDownArrowBlock =
        g_iCommunityTrackTop + FRONTEND_TRACK_VISIBLE_COMMUNITY_ROWS <
                g_iCommunityTrackCount ? 81 : 80;

    for (int iRow = 0; iRow < FRONTEND_TRACK_VISIBLE_COMMUNITY_ROWS; ++iRow) {
      int iTrackIdx = g_iCommunityTrackTop + iRow;

      if (iTrackIdx < g_iCommunityTrackCount) {
        char szDisplayName[MAX_COMMUNITY_TRACK_FILENAME];

        frontend_track_select_display_name(
            szDisplayName, sizeof(szDisplayName),
            g_aszCommunityTracks[iTrackIdx], -1);
        menu_render_scaled_text(mr, 2, szDisplayName,
                                font2_ascii, font2_offsets,
                                FRONTEND_TRACK_COMMUNITY_LIST_RIGHT,
                                sel_posns[iRow].y + 7, 0x8Fu, 2u,
                                FRONTEND_TRACK_COMMUNITY_LIST_LEFT,
                                FRONTEND_TRACK_COMMUNITY_LIST_RIGHT,
                                pal_addr);
      }
    }
    menu_render_sprite(mr, FRONTEND_TRACK_ARROW_SLOT, iUpArrowBlock,
                       iArrowX, sel_posns[0].y, 255, pal_addr);
    menu_render_sprite(mr, FRONTEND_TRACK_ARROW_SLOT, iDownArrowBlock,
                       iArrowX, sel_posns[7].y, 255, pal_addr);
  } else {
    for (int iRow = 0; iRow < 8; ++iRow) {
      menu_render_text(mr, 2, s_aszFrontendTrackNames[iRow],
                       font2_ascii, font2_offsets,
                       sel_posns[iRow].x + 132, sel_posns[iRow].y + 7,
                       0x8Fu, 2u, pal_addr);
    }
  }

  iBlockIdx = frontend_track_select_is_community() ? 0 : (TrackLoad - 1) / 8;
  *piBlockIdx = iBlockIdx;

  if (frontend_track_select_is_community() && !community_track_available()) {
    menu_render_text(mr, 15, "NO TRACK SELECTED", font1_ascii, font1_offsets,
                     PREVIEW_X + PREVIEW_W / 2,
                     TRACK_PREVIEW_Y + PREVIEW_H / 2, MENU_COLOR_RED, 1u,
                     pal_addr);
  } else if (TrackLoad >= 0) {
    menu_render_load_track_mesh(mr, palette);
    if (iFrontendTrackAnimationTimer)
      menu_render_draw_track_preview(mr, fFrontendTrackZoom, 1280,
                                     iFrontendTrackYaw, PREVIEW_X,
                                     TRACK_PREVIEW_Y, PREVIEW_W, PREVIEW_H);
    else
      menu_render_draw_track_preview(mr, fFrontendTrackAnimatedZoom, 1280,
                                     iFrontendTrackYaw, PREVIEW_X,
                                     TRACK_PREVIEW_Y, PREVIEW_W, PREVIEW_H);
    menu_render_set_layer(mr, MENU_LAYER_FOREGROUND);

    if (game_type >= 2) {
      NoOfLaps = 5;
    } else {
      iLapsForLevel = cur_laps[level];
      NoOfLaps = iLapsForLevel;
      if (competitors == 2)
        NoOfLaps = iLapsForLevel / 2;
    }

    sprintf(buffer, "%s: %i", &language_buffer[4544], NoOfLaps);
    menu_render_text(mr, 15, buffer, font1_ascii, font1_offsets, 420, 16,
                     0x8Fu, 1u, pal_addr);
    if (!frontend_track_select_is_community()) {
      menu_render_text(mr, 15, &language_buffer[4608], font1_ascii,
                       font1_offsets, 420, 34, 0x8Fu, 1u, pal_addr);
      if (RecordCars[TrackLoad] < 0) {
        sprintf(buffer, "%s", RecordNames[TrackLoad]);
      } else {
        int iTotalCentiseconds = (int)(RecordLaps[TrackLoad] * 100.0);
        int iRecordMinutes = iTotalCentiseconds / 6000;
        int iRecordSeconds = (iTotalCentiseconds / 100) % 60;
        int iRecordCentiseconds = iTotalCentiseconds % 100;

        sprintf(buffer, "%s - %s - %02i:%02i:%02i",
                RecordNames[TrackLoad],
                CompanyNames[RecordCars[TrackLoad] & 0xF],
                iRecordMinutes, iRecordSeconds, iRecordCentiseconds);
      }
      menu_render_text(mr, 15, buffer, font1_ascii, font1_offsets, 420, 52,
                       0x8Fu, 1u, pal_addr);
    }
  }

  if (frontend_track_select_is_community()) {
    menu_render_text(mr, FRONTEND_TRACK_COMMUNITY_FONT_SLOT, "COMMUNITY",
                     font4_ascii, font4_offsets, 540, 288, 0x8Fu, 1u,
                     pal_addr);
    menu_render_text(mr, FRONTEND_TRACK_COMMUNITY_FONT_SLOT, "TRACKS",
                     font4_ascii, font4_offsets, 540, 324, 0x8Fu, 1u,
                     pal_addr);
  } else {
    menu_render_sprite(mr, 14, iBlockIdx, 500, 300, 0, pal_addr);
  }
  if (frontend_track_select_is_community()) {
    if (g_iCommunityTrackSel >= 0 &&
        g_iCommunityTrackSel < g_iCommunityTrackCount) {
      char szDisplayName[MAX_COMMUNITY_TRACK_FILENAME];

      frontend_track_select_display_name(
          szDisplayName, sizeof(szDisplayName),
          g_aszCommunityTracks[g_iCommunityTrackSel], 0);
      menu_render_scaled_text(mr, 15, szDisplayName, font1_ascii,
                              font1_offsets, 190, 356, 0x8Fu, 0, 190,
                              FRONTEND_TRACK_SELECTED_NAME_RIGHT, pal_addr);
    }
  } else if (TrackLoad <= 0) {
    if (TrackLoad)
      menu_render_text(mr, 2, "EDITOR", font2_ascii, font2_offsets, 190, 350,
                       0x8Fu, 0, pal_addr);
    else
      menu_render_text(mr, 2, "TRACK ZERO", font2_ascii, font2_offsets, 190,
                       350, 0x8Fu, 0, pal_addr);
  } else if (TrackLoad >= 17) {
    menu_render_sprite(mr, 13, TrackLoad - 17, 190, 356, 0, pal_addr);
  } else {
    menu_render_sprite(mr, 3, TrackLoad - 1, 190, 356, 0, pal_addr);
  }
  show_received_mesage();
  menu_render_end_frame(mr);
}

//-------------------------------------------------------------------------------------------------

static void frontend_track_select_update_animation(int iFrameCount,
                                                   int *piPrevTrackLoad,
                                                   int iStartedFadeIn)
{
  if (!iFrontendTrackAnimationTimer) {
    double dZoomInterpolation =
        (double)iFrameCount * fFrontendTrackTargetZoom +
        fFrontendTrackAnimatedZoom;
    fFrontendTrackAnimatedZoom = (float)dZoomInterpolation;
    if (dZoomInterpolation <= 10000000.0) {
      if (fFrontendTrackAnimatedZoom < fFrontendTrackZoom) {
        fFrontendTrackAnimatedZoom = fFrontendTrackZoom;
        iFrontendTrackAnimationTimer = 72;
        fFrontendTrackTargetZoom = -fFrontendTrackTargetZoom;
      }
    } else {
      fFrontendTrackAnimatedZoom = 10000000.0f;
      if (iFrontendTrackCurrentTrack == TrackLoad)
        *piPrevTrackLoad = -1;
      TrackLoad = iFrontendTrackCurrentTrack;
      remove_frontendspeech();
      iFrontendTrackSpeechPending = 0;
      if (TrackLoad >= 0)
        loadtrack(TrackLoad, -1);
      if (TrackLoad != TRACK_LOAD_COMMUNITY) {
        loadtracksample(TrackLoad);
        iFrontendTrackSpeechPending = -1;
      }
      fFrontendTrackZoom = cur_TrackZ;
      iFrontendTrackSoundFlag = -1;
      fFrontendTrackTargetZoom = (cur_TrackZ + -10000000.0f) * 0.05f;
      if (*piPrevTrackLoad != -1)
        frontend_track_select_begin_broadcast_wait(-1);
      *piPrevTrackLoad = -1;
      frames = 0;
    }
  }

  if (iStartedFadeIn) {
    if (TrackLoad != TRACK_LOAD_COMMUNITY)
      loadtracksample(TrackLoad);
    iFrontendTrackSpeechPending = 0;
    frontendsample(0x8000);
    frames = 0;
  }

  if (TrackLoad != *piPrevTrackLoad) {
    if (!iFrontendTrackSoundFlag) {
      sfxsample(SOUND_SAMPLE_TRACK, 0x8000);
      iFrontendTrackSpeechPending = 0;
    }
    iFrontendTrackSoundFlag = 0;
    iFrontendTrackCurrentTrack = TrackLoad;
    iFrontendTrackAnimationTimer = 0;
  }
}

//-------------------------------------------------------------------------------------------------

static void frontend_track_select_handle_input(int iBlockIdx)
{
  int iCupOffset = 8 * iBlockIdx;
  int iTrackInCup = iFrontendTrackSelectedTrack + 1;
  unsigned int uiKeyDirection = 0;
  int iCommunityMode = frontend_track_select_is_community();
  int iCalculatedTrack =
      iCommunityMode ? TRACK_LOAD_COMMUNITY :
                       8 * iBlockIdx + iFrontendTrackSelectedTrack + 1;

  while (fatkbhit()) {
    uint8 byKey = fatgetch();

    if (byKey < 0xDu) {
      if (!byKey) {
        uint8 byExtendedKey = fatgetch();

        if (byExtendedKey >= 0x48u) {
          if (byExtendedKey <= 0x48u)
            uiKeyDirection = 2;
          else if (byExtendedKey == 0x50)
            uiKeyDirection = 1;
        }
      }
    } else if (byKey <= 0xDu) {
      if (iCommunityMode) {
        if (iFrontendTrackSelectedTrack == 8) {
          remove_frontendspeech();
          iFrontendTrackSpeechPending = 0;
          sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);
          frontend_track_select_request_exit();
        } else if (game_type != 1) {
          int iTrackIdx = g_iCommunityTrackTop + iFrontendTrackSelectedTrack;

          if (iTrackIdx >= 0 && iTrackIdx < g_iCommunityTrackCount) {
            remove_frontendspeech();
            iFrontendTrackSpeechPending = 0;
            sfxsample(SOUND_SAMPLE_TRACK, 0x8000);
            frontend_track_select_select_community_row(
                iFrontendTrackSelectedTrack);
          }
        }
      } else {
        if ((iFrontendTrackSelectedTrack != 8 &&
             iFrontendTrackCurrentTrack != iCalculatedTrack) ||
            iFrontendTrackSelectedTrack == 8) {
          remove_frontendspeech();
          iFrontendTrackSpeechPending = 0;
          sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);
        }
        if (iFrontendTrackCurrentTrack == iCalculatedTrack && SoundCard &&
            frontendspeechhandle != -1 && DIGISampleDone(frontendspeechhandle)) {
          frontendspeechhandle = -1;
          frontendsample(0x8000);
        }
        if (iFrontendTrackSelectedTrack == 8) {
          frontend_track_select_request_exit();
        } else if (game_type != 1 &&
                   iFrontendTrackCurrentTrack != iCupOffset + iTrackInCup) {
          sfxsample(SOUND_SAMPLE_TRACK, 0x8000);
          iFrontendTrackSpeechPending = 0;
          iFrontendTrackCurrentTrack = iCupOffset + iTrackInCup;
          frontend_track_select_begin_track_animation();
        }
      }
    } else if (byKey >= 0x1Bu) {
      if (byKey <= 0x1Bu) {
        remove_frontendspeech();
        iFrontendTrackSpeechPending = 0;
        sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);
        frontend_track_select_request_exit();
      } else if (byKey == 32 && game_type != 1 && TrackLoad > 0) {
        sfxsample(SOUND_SAMPLE_TRACK, 0x8000);
        iFrontendTrackSpeechPending = 0;
        if (iFrontendTrackCurrentTrack == TRACK_LOAD_COMMUNITY ||
            TrackLoad == TRACK_LOAD_COMMUNITY) {
          iFrontendTrackCurrentTrack = 1;
          iFrontendTrackSelectedTrack = 0;
        } else {
          iFrontendTrackCurrentTrack += 8;
          if (iFrontendTrackCurrentTrack > 8 &&
              iFrontendTrackCurrentTrack < 17 && (cup_won & 1) == 0)
            iFrontendTrackCurrentTrack += 8;
          if (iFrontendTrackCurrentTrack > 16 &&
              iFrontendTrackCurrentTrack < 25 && (cup_won & 2) == 0)
            iFrontendTrackCurrentTrack += 8;
          if (iFrontendTrackCurrentTrack > 24) {
            iFrontendTrackCurrentTrack = TRACK_LOAD_COMMUNITY;
            scan_community_tracks();
            frontend_track_select_show_community_selection();
          } else {
            iFrontendTrackSelectedTrack =
                ((uint8)iFrontendTrackCurrentTrack - 1) & 7;
          }
        }
        frontend_track_select_begin_track_animation();
      }
    }

    if (iFrontendTrackExitFlag)
      return;
  }

  if (uiKeyDirection) {
    if (iCommunityMode) {
      if (uiKeyDirection > 1) {
        if (iFrontendTrackSelectedTrack == 8) {
          if (g_iCommunityTrackCount > 0) {
            iFrontendTrackSelectedTrack = g_iCommunityTrackCount -
                                          g_iCommunityTrackTop - 1;
            if (iFrontendTrackSelectedTrack >=
                FRONTEND_TRACK_VISIBLE_COMMUNITY_ROWS)
              iFrontendTrackSelectedTrack =
                  FRONTEND_TRACK_VISIBLE_COMMUNITY_ROWS - 1;
          }
        } else if (iFrontendTrackSelectedTrack > 0) {
          --iFrontendTrackSelectedTrack;
        } else if (g_iCommunityTrackTop > 0) {
          --g_iCommunityTrackTop;
        }
      } else {
        if (g_iCommunityTrackCount <= 0) {
          iFrontendTrackSelectedTrack = 8;
        } else if (iFrontendTrackSelectedTrack < 7 &&
                   g_iCommunityTrackTop + iFrontendTrackSelectedTrack + 1 <
                       g_iCommunityTrackCount) {
          ++iFrontendTrackSelectedTrack;
        } else if (iFrontendTrackSelectedTrack == 7 &&
                   g_iCommunityTrackTop +
                           FRONTEND_TRACK_VISIBLE_COMMUNITY_ROWS <
                       g_iCommunityTrackCount) {
          ++g_iCommunityTrackTop;
        } else {
          iFrontendTrackSelectedTrack = 8;
        }
      }
      frontend_track_select_clamp_community_cursor();
    } else if (uiKeyDirection > 1) {
      if (game_type != 1 && --iFrontendTrackSelectedTrack < 0)
        iFrontendTrackSelectedTrack = 0;
    } else if (game_type != 1 && ++iFrontendTrackSelectedTrack > 8) {
      iFrontendTrackSelectedTrack = 8;
    }
  }
}

//-------------------------------------------------------------------------------------------------

void frontend_track_select_enter(void)
{
  iFrontendTrackAnimationTimer = 36;
  frontend_track_select_black_palette();
  iFrontendTrackSoundFlag = 0;
  front_fade = 0;
  iFrontendTrackExitFlag = 0;
  iFrontendTrackExitFading = 0;
  iFrontendTrackSpeechPending = 0;
  if (game_type == 1 && TrackLoad == TRACK_LOAD_COMMUNITY)
    TrackLoad = 1;
  if (TrackLoad == TRACK_LOAD_COMMUNITY) {
    scan_community_tracks();
    frontend_track_select_show_community_selection();
  } else if (TrackLoad > 0) {
    iFrontendTrackSelectedTrack = ((uint8)TrackLoad - 1) & 7;
    if (game_type == 1)
      iFrontendTrackSelectedTrack = 8;
  } else {
    iFrontendTrackSelectedTrack = 0;
  }
  iFrontendTrackCurrentTrack = TrackLoad;
  fre((void **)&front_vga[3]);
  if (TrackLoad >= 0)
    loadtrack(TrackLoad, -1);
  front_vga[3] = (tBlockHeader *)load_picture("trkname.bm");
  front_vga[13] = (tBlockHeader *)load_picture("bonustrk.bm");
  iFrontendTrackYaw = 0;
  front_vga[14] = (tBlockHeader *)load_picture("cupicons.bm");
  front_vga[FRONTEND_TRACK_COMMUNITY_FONT_SLOT] =
      (tBlockHeader *)load_picture("font4.bm");
  front_vga[FRONTEND_TRACK_ARROW_SLOT] =
      (tBlockHeader *)load_picture("replaysc.bm");
  memcpy(pal_addr, palette, 256 * sizeof(tColor));
  palette_brightness = 32;
  {
    MenuRenderer *mr = GetMenuRenderer();
    if (mr) {
      menu_render_load_blocks(mr, 3, front_vga[3], palette);
      menu_render_load_blocks(mr, 13, front_vga[13], palette);
      menu_render_load_blocks(mr, 14, front_vga[14], palette);
      menu_render_load_blocks(mr, FRONTEND_TRACK_COMMUNITY_FONT_SLOT,
                              front_vga[FRONTEND_TRACK_COMMUNITY_FONT_SLOT],
                              palette);
      menu_render_load_blocks(mr, FRONTEND_TRACK_ARROW_SLOT,
                              front_vga[FRONTEND_TRACK_ARROW_SLOT], palette);
    }
  }
  frames = 0;
  fFrontendTrackZoom = cur_TrackZ;
  fFrontendTrackAnimatedZoom = cur_TrackZ;
  fFrontendTrackTargetZoom = -(cur_TrackZ + -10000000.0f) * 0.05f;
}

//-------------------------------------------------------------------------------------------------

void frontend_track_select_update(void)
{
  int iFrameCount;
  int iPrevTrackLoad;
  int iStartedFadeIn;
  int iBlockIdx;

  if (game_type == 1)
    iFrontendTrackSelectedTrack = 8;
  iFrameCount = frames;
  frames = 0;
  iPrevTrackLoad = TrackLoad;
  if (SoundCard && front_fade && iFrontendTrackSpeechPending &&
      sfxplaying(SOUND_SAMPLE_TRACK) == 0) {
    frontendsample(0x8000);
    iFrontendTrackSpeechPending = 0;
  }

  frontend_track_select_apply_type_switch();
  frontend_track_select_draw(&iBlockIdx, &iStartedFadeIn);
  if (SnapshotShouldStop())
    return;

  if (iFrontendTrackExitFading) {
    MenuRenderer *mr = GetMenuRenderer();
    if (!menu_render_fade_active(mr)) {
      iFrontendTrackExitFading = 0;
      eFrontendNextState = eFRONTEND_STATE_MAIN_MENU;
    }
    return;
  }

  frontend_track_select_apply_same_car_switch();
  if (frontend_track_select_update_broadcast_wait())
    return;
  frontend_track_select_update_animation(iFrameCount, &iPrevTrackLoad,
                                         iStartedFadeIn);
  if (frontend_track_select_update_broadcast_wait())
    return;
  frontend_track_select_handle_input(iBlockIdx);
  iFrontendTrackYaw =
      ((uint16)iFrontendTrackYaw + 32 * (uint16)iFrameCount) & 0x3FFF;
}

//-------------------------------------------------------------------------------------------------

void frontend_track_select_exit(void)
{
  iFrontendTrackExitFading = 0;
  if (!SnapshotShouldStop())
    frontend_track_select_black_palette();

  front_fade = 0;
  fre((void **)&front_vga[3]);
  fre((void **)&front_vga[13]);
  fre((void **)&front_vga[14]);
  fre((void **)&front_vga[FRONTEND_TRACK_COMMUNITY_FONT_SLOT]);
  fre((void **)&front_vga[FRONTEND_TRACK_ARROW_SLOT]);
  front_vga[3] = (tBlockHeader *)load_picture("carnames.bm");
  remove_frontendspeech();

  if (eFrontendCurrentState == eFRONTEND_STATE_TRACK_SELECT)
    frontend_menu_resume_from_child();
}

//-------------------------------------------------------------------------------------------------

static void frontend_track_select_run_snapshot(void)
{
  frontend_track_select_enter();
  while (!iFrontendTrackExitFlag && !SnapshotShouldStop()) {
    frontend_track_select_update();
    if (!iFrontendTrackExitFlag && !SnapshotShouldStop())
      UpdateSDLWindow();
  }
  if (!SnapshotShouldStop())
    frontend_track_select_exit();
}

//-------------------------------------------------------------------------------------------------
