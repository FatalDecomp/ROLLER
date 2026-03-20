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
