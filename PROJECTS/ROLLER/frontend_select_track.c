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

static void frontend_track_select_run_snapshot(void);

void snapshot_render_menu_select_track(void)
{
  snapshot_setup_frontend_menu_state(0);
  frontend_track_select_run_snapshot();
}

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

static void frontend_track_select_begin_broadcast_wait(int iBroadcastMode)
{
  network_broadcast_wait_start(iBroadcastMode, 1);
}

static int frontend_track_select_update_broadcast_wait(void)
{
  if (!network_broadcast_wait_active())
    return 0;

  (void)network_broadcast_wait_update();
  return -1;
}

static void frontend_track_select_black_palette(void)
{
  palette_brightness = 0;
  for (int i = 0; i < 256; ++i) {
    pal_addr[i].byR = 0;
    pal_addr[i].byB = 0;
    pal_addr[i].byG = 0;
  }
}

static void frontend_track_select_request_exit(void)
{
  iFrontendTrackExitFlag = -1;
  if (eFrontendCurrentState == eFRONTEND_STATE_TRACK_SELECT) {
    MenuRenderer *mr = GetMenuRenderer();
    menu_render_begin_fade(mr, 0, 32);
    iFrontendTrackExitFading = 1;
  }
}

static void frontend_track_select_apply_type_switch(void)
{
  if (!switch_types)
    return;

  game_type = switch_types - 1;
  if (switch_types == 1 && competitors == 1)
    competitors = 16;
  switch_types = 0;
  if (game_type == 1)
    Race = ((uint8)TrackLoad - 1) & 7;
  else
    network_champ_on = 0;
}

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

  menu_render_text(mr, 2, "AUTO ARIEL", font2_ascii, font2_offsets,
                   sel_posns[0].x + 132, sel_posns[0].y + 7, 0x8Fu, 2u,
                   pal_addr);
  menu_render_text(mr, 2, "DESILVA", font2_ascii, font2_offsets,
                   sel_posns[1].x + 132, sel_posns[1].y + 7, 0x8Fu, 2u,
                   pal_addr);
  menu_render_text(mr, 2, "PULSE", font2_ascii, font2_offsets,
                   sel_posns[2].x + 132, sel_posns[2].y + 7, 0x8Fu, 2u,
                   pal_addr);
  menu_render_text(mr, 2, "GLOBAL", font2_ascii, font2_offsets,
                   sel_posns[3].x + 132, sel_posns[3].y + 7, 0x8Fu, 2u,
                   pal_addr);
  menu_render_text(mr, 2, "MILLION PLUS", font2_ascii, font2_offsets,
                   sel_posns[4].x + 132, sel_posns[4].y + 7, 0x8Fu, 2u,
                   pal_addr);
  menu_render_text(mr, 2, "MISSION", font2_ascii, font2_offsets,
                   sel_posns[5].x + 132, sel_posns[5].y + 7, 0x8Fu, 2u,
                   pal_addr);
  menu_render_text(mr, 2, "ZIZIN", font2_ascii, font2_offsets,
                   sel_posns[6].x + 132, sel_posns[6].y + 7, 0x8Fu, 2u,
                   pal_addr);
  menu_render_text(mr, 2, "REISE WAGON", font2_ascii, font2_offsets,
                   sel_posns[7].x + 132, sel_posns[7].y + 7, 0x8Fu, 2u,
                   pal_addr);

  iBlockIdx = (TrackLoad - 1) / 8;
  *piBlockIdx = iBlockIdx;

  if (TrackLoad >= 0) {
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
              RecordNames[TrackLoad], CompanyNames[RecordCars[TrackLoad] & 0xF],
              iRecordMinutes, iRecordSeconds, iRecordCentiseconds);
    }
    menu_render_text(mr, 15, buffer, font1_ascii, font1_offsets, 420, 52,
                     0x8Fu, 1u, pal_addr);
  }

  menu_render_sprite(mr, 14, iBlockIdx, 500, 300, 0, pal_addr);
  if (TrackLoad <= 0) {
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
      loadtracksample(TrackLoad);
      iFrontendTrackSpeechPending = -1;
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

static void frontend_track_select_handle_input(int iBlockIdx)
{
  int iCupOffset = 8 * iBlockIdx;
  int iTrackInCup = iFrontendTrackSelectedTrack + 1;
  unsigned int uiKeyDirection = 0;
  int iCalculatedTrack = 8 * iBlockIdx + iFrontendTrackSelectedTrack + 1;

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
        if (iFrontendTrackAnimationTimer)
          iFrontendTrackAnimationTimer = 0;
        else if (fFrontendTrackTargetZoom < 0.0f)
          fFrontendTrackTargetZoom = -fFrontendTrackTargetZoom;
      }
    } else if (byKey >= 0x1Bu) {
      if (byKey <= 0x1Bu) {
        remove_frontendspeech();
        iFrontendTrackSpeechPending = 0;
        sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);
        frontend_track_select_request_exit();
      } else if (byKey == 32 && game_type != 1 && TrackLoad > 0) {
        iFrontendTrackCurrentTrack += 8;
        sfxsample(SOUND_SAMPLE_TRACK, 0x8000);
        iFrontendTrackSpeechPending = 0;
        if (iFrontendTrackCurrentTrack > 8 &&
            iFrontendTrackCurrentTrack < 17 && (cup_won & 1) == 0)
          iFrontendTrackCurrentTrack += 8;
        if (iFrontendTrackCurrentTrack > 16 &&
            iFrontendTrackCurrentTrack < 25 && (cup_won & 2) == 0)
          iFrontendTrackCurrentTrack += 8;
        if (iFrontendTrackCurrentTrack > 24)
          iFrontendTrackCurrentTrack -= 24;
        if (iFrontendTrackAnimationTimer)
          iFrontendTrackAnimationTimer = 0;
        else if (fFrontendTrackTargetZoom < 0.0f)
          fFrontendTrackTargetZoom = -fFrontendTrackTargetZoom;
      }
    }

    if (iFrontendTrackExitFlag)
      return;
  }

  if (uiKeyDirection) {
    if (uiKeyDirection > 1) {
      if (game_type != 1 && --iFrontendTrackSelectedTrack < 0)
        iFrontendTrackSelectedTrack = 0;
    } else if (game_type != 1 && ++iFrontendTrackSelectedTrack > 8) {
      iFrontendTrackSelectedTrack = 8;
    }
  }
}

void frontend_track_select_enter(void)
{
  iFrontendTrackAnimationTimer = 36;
  frontend_track_select_black_palette();
  iFrontendTrackSoundFlag = 0;
  front_fade = 0;
  iFrontendTrackExitFlag = 0;
  iFrontendTrackExitFading = 0;
  iFrontendTrackSpeechPending = 0;
  if (TrackLoad > 0) {
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
  memcpy(pal_addr, palette, 256 * sizeof(tColor));
  palette_brightness = 32;
  {
    MenuRenderer *mr = GetMenuRenderer();
    if (mr) {
      menu_render_load_blocks(mr, 3, front_vga[3], palette);
      menu_render_load_blocks(mr, 13, front_vga[13], palette);
      menu_render_load_blocks(mr, 14, front_vga[14], palette);
    }
  }
  frames = 0;
  fFrontendTrackZoom = cur_TrackZ;
  fFrontendTrackAnimatedZoom = cur_TrackZ;
  fFrontendTrackTargetZoom = -(cur_TrackZ + -10000000.0f) * 0.05f;
}

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

void frontend_track_select_exit(void)
{
  iFrontendTrackExitFading = 0;
  if (!SnapshotShouldStop())
    frontend_track_select_black_palette();

  front_fade = 0;
  fre((void **)&front_vga[3]);
  fre((void **)&front_vga[13]);
  fre((void **)&front_vga[14]);
  front_vga[3] = (tBlockHeader *)load_picture("carnames.bm");
  remove_frontendspeech();

  if (eFrontendCurrentState == eFRONTEND_STATE_TRACK_SELECT)
    frontend_menu_resume_from_child();
}

//-------------------------------------------------------------------------------------------------
//00049070
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
