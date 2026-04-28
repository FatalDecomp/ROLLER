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

// Replace accented characters with non-accented equivalents in the font table - add by ROLLER
void font_ascii_replace_accent(char *font)
{
  font[0xc7] = font['C']; // Ç
  font[0xe7] = font['c']; // ç

  font[0xc0] = font['A']; // À
  font[0xc1] = font['A']; // Á
  font[0xc2] = font['A']; // Â
  font[0xc3] = font['A']; // Ã
  font[0xe0] = font['a']; // à
  font[0xe1] = font['a']; // á
  font[0xe2] = font['a']; // â
  font[0xe3] = font['a']; // ã

  font[0xd3] = font['O']; // Ó
  font[0xd5] = font['O']; // Õ
  font[0xf3] = font['o']; // ó
  font[0xf5] = font['o']; // õ

  font[0xcd] = font['I']; // Í
  font[0xed] = font['i']; // í

  font[0xd9] = font['U']; // Ù
  font[0xda] = font['U']; // Ú
  font[0xfa] = font['u']; // ú
  font[0xf9] = font['u']; // ù

  font[0xc9] = font['E']; // É
  font[0xc8] = font['E']; // È
  font[0xc9] = font['E']; // Ê
  font[0xe9] = font['e']; // é
  font[0xe8] = font['e']; // è
  font[0xea] = font['e']; // ê

  font[0xd1] = font['N']; // Ñ
  font[0xf1] = font['n']; // ñ
}

//-------------------------------------------------------------------------------------------------
//0003F5B0
void title_screens()
{
  winx = 0;
  winy = 0;
  winw = XMAX;
  mirror = 0;
  SVGA_ON = 0;
  winh = YMAX;
  init_screen();
  SVGA_ON = -1;
  init_screen();
  winx = 0;
  winw = XMAX;
  winy = 0;
  winh = YMAX;
  mirror = 0;

  // added by ROLLER, check to see if title.bm and title.pal exist
  // these files were not shipped in the USA localization
  bool bHasTitle = ROLLERfexists("title.bm") && ROLLERfexists("title.pal");

  setpal(bHasTitle ? "title.pal" : "whipped.pal");
  front_vga[0] = (tBlockHeader*)load_picture(bHasTitle ? "title.bm" : "whipped.bm");

  if (front_vga[0] && scrbuf) //check added by ROLLER
    display_picture(scrbuf, front_vga[0]);

  copypic(scrbuf, screen);
  loadfatalsample();
  fade_palette(32);
  if ((cheat_mode & (CHEAT_MODE_KILLER_OPPONENTS | CHEAT_MODE_DEATH_MODE)) != 0)
    dospeechsample(SOUND_SAMPLE_FATAL, 0x8000);
  disable_keyboard();
  if ((cheat_mode & (CHEAT_MODE_KILLER_OPPONENTS | CHEAT_MODE_DEATH_MODE)) != 0)
    waitsampledone(SOUND_SAMPLE_FATAL);
  fre((void**)&front_vga[0]);
  freefatalsample();

  // Add by ROLLER, check language to change font table to support Brazilian / Saspanish.
  if (strcmp(languagename, "Brazilian") == 0 || strcmp(languagename, "Saspanish") == 0) {
    SDL_Log("Language: update font1_ascii and font2_ascii to support '%s'", languagename);
    memcpy(font1_ascii, font1_ascii_br, 256);
    memcpy(font2_ascii, font2_ascii_br, 256);
    memcpy(font3_ascii, font3_ascii_br, 256);
    memcpy(font4_ascii, font4_ascii_br, 256);
    font_ascii_replace_accent((char *)font6_ascii);
    font_ascii_replace_accent((char *)ascii_conv3);
  }
}

//-------------------------------------------------------------------------------------------------
//0003F6B0
void copy_screens()
{
  SVGA_ON = -1;
  init_screen();
  winx = 0;
  winw = XMAX;
  winy = 0;
  winh = YMAX;
  mirror = 0;

  setpal("gremlin.pal");
  front_vga[0] = (tBlockHeader *)load_picture("gremlin.bm");

  display_picture(scrbuf, front_vga[0]);

  fade_palette(32);
  copypic(scrbuf, screen);
  disable_keyboard();
  ticks = 0;
#ifndef _DEBUG
  while (ticks < 180) {
    UpdateSDL();
    UpdateSDLWindow();
  }
#endif
  fre((void**)&front_vga[0]);
  fade_palette(0);
}

// Fade callback: redraws menu background so fade overlay is visible over content
void fade_redraw_bg(void *ctx)
{
  menu_render_background((MenuRenderer *)ctx, 0);
}

//-------------------------------------------------------------------------------------------------
//0003F7B0
void select_screen()
{
  int iMenuSelection; // esi
  int iContinue; // ebp
  int iQuitConfirmed; // edi
  int iPlayer1Car; // eax
  int iNoClear; // eax
  int iPlayer; // edx
  uint8 *pBuf; // edx
  int iPlayer1Car2; // eax
  eCarType carType; // eax
  eCarType carType2; // ebx
  int iCarTexLoaded; // ecx
  int iLoadCarTextures; // edx
  void **ppCartexVgaItr2; // edx
  eCarType cartype3; // eax
  eCarType cartype4; // ebx
  int iCarTexLoaded2; // edx
  int iCurLaps; // eax
  double dRecordLap; // st7
  int iMinutes; // ecx
  int iSeconds; // ebx
  int iBlockIdx2; // ebx
  int iPlayerIdx; // edx
  int iPlayerIdx2; // eax
  uint8 **ppCartexVgaItr_2; // edx
  void **ppCartexVgaToFree_1; // eax
  eCarType carType_2; // eax
  eCarType carTypeBackup; // ebx
  int iCarTexLoaded_1; // edx
  int iCarIdx; // ecx
  int iPlayerIdx_1; // eax
  int iPlayerOffset; // edx
  uint8 **ppCartexVgaItr_1; // edx
  void **ppCartexVgaToFree; // eax
  eCarType iCar; // eax
  eCarType carTypeToLoad; // ebx
  int iCartexLoaded; // ecx
  int iLoadCarTextures_1; // edx
  uint8 byKey; // al
  uint8 byKey2; // al
  void **ppCartexVgaToFree_2; // edx
  eCarType carType_1; // eax
  eCarType carTypeSelected; // ebx
  int iCartexLoaded2; // edx
  int16 nNewYaw; // ax
  int iControl; // edx
  int j; // eax
  int iNonCompetitorIdx; // edx
  int iRacersIdx; // esi
  int iNonCompetitorIdx2; // edx
  int m; // esi
  int iTargetPos; // edx
  int iHumanIdx; // ebx
  int iGridIdx; // eax
  int iNonHumanIdx; // eax
  int iSwapGrid1; // ecx
  int iSwapGrid2; // edx
  int iInitScreen; // [esp+0h] [ebp-54h]
  int16 nFrames; // [esp+4h] [ebp-50h]
  int iRotation; // [esp+Ch] [ebp-48h]
  int iBlockIdx; // [esp+10h] [ebp-44h]
  int iLoadCarTex2; // [esp+2Ch] [ebp-28h]
  int iLoadCarTexFlag; // [esp+30h] [ebp-24h]
  int iLoadCarTextures2; // [esp+34h] [ebp-20h]

  // Initialize game state
  time_to_start = 0;
  StartPressed = 0;
  load_language_file(szSelectEng, 0);
  load_language_file(szConfigEng, 1);
  iInitScreen = -1;
  restart_net = 0;
  if (!time_to_start) {
    while (1) {
      cup_won = (textures_off & TEX_OFF_PREMIER_CUP_AVAILABLE) != 0;
      if ((textures_off & TEX_OFF_BONUS_CUP_AVAILABLE) != 0) {
        cup_won |= 2;
        //LOBYTE(cup_won) = cup_won | 2;
      }
      loadfatalsample();
      iContinue = 0;
      iRotation = 0;
      player1_car = 0;
      player2_car = 1;
      if (!network_on) {
        players = 1;
        if (iInitScreen)
          tick_on = 0;
      }
      front_fade = 0;
      frontend_on = -1;
      p_tex_size = gfx_size;

      // Load graphical assets
      front_vga[0] = (tBlockHeader*)load_picture("frontend.bm");
      front_vga[1] = (tBlockHeader*)load_picture("selhead.bm");
      front_vga[2] = (tBlockHeader*)load_picture("font2.bm");
      front_vga[3] = (tBlockHeader*)load_picture("carnames.bm");
      front_vga[4] = (tBlockHeader*)load_picture("opticon2.bm");
      front_vga[5] = (tBlockHeader*)load_picture("selicons.bm");
      front_vga[6] = (tBlockHeader*)load_picture("selexit.bm");
      front_vga[15] = (tBlockHeader *)load_picture("font1.bm");

      fade_palette(0);
      iQuitConfirmed = 0;
      SVGA_ON = -1;
      if (iInitScreen)
        init_screen();
      winx = 0;
      winw = XMAX;
      winy = 0;
      winh = YMAX;
      mirror = 0;
      setpal("frontend.pal");

      // Convert assets to GPU textures (after setpal so palette is correct)
      {
        extern tColor palette[];
        MenuRenderer *mr = GetMenuRenderer();
        if (mr) {
          for (int i = 0; i <= 15; i++) {
            if (front_vga[i])
              menu_render_load_blocks(mr, i, front_vga[i], palette);
          }
        }
      }
      if (network_on) {
        Players_Cars[0] = my_car;
        name_copy(player_names[player1_car], my_name);
        iPlayer1Car = player1_car;
        manual_control[player1_car] = my_control;
        player_invul[iPlayer1Car] = my_invul;
        player_type = 1;
        if ((!game_type || game_type == 2) && last_replay != 2) {
          iNoClear = no_clear;
          if (!no_clear && network_on > 0) {
            iPlayer = 0;
            do {
              Players_Cars[iPlayer++] = -1;
              ++iNoClear;
            } while (iNoClear < network_on);
          }
        }
      }
      if (game_type >= 3)
        game_type = last_type;
      replaytype = 0;
      if (network_on && iInitScreen) {
        remove_messages(-1);
        reset_network(0);
      }
      tick_on = -1;
      FindShades();
      check_cars();
      Car[0].nYaw = 0;
      Car[0].nRoll = 0;
      Car[0].pos.fX = 0.0;
      Car[0].pos.fY = 0.0;
      Car[0].pos.fZ = 0.0;
      intro = 0;
      Car[0].nPitch = 0;
      pBuf = (uint8 *)trybuffer(300000u);
      front_vga[7] = (tBlockHeader *)pBuf;
      iMenuSelection = 8;
      if (no_mem)
        goto LABEL_24;
      if (pBuf)
        gfx_size = no_mem;
      else
        LABEL_24:
      gfx_size = 1;
      fre((void **)&front_vga[7]);
      set_starts(0);
      car_texs_loaded[0] = 0;
      for (int i = 1; i < 16; ++i) {
        car_texs_loaded[i] = -1;
      }
      //for (i = 2; i != 16; SmokePt[i + 1023] = -1)// reference to car_texs_loaded
      //{
      //  i += 7;
      //  SmokePt[i + 1017] = -1;
      //  SmokePt[i + 1018] = -1;
      //  SmokePt[i + 1019] = -1;
      //  SmokePt[i + 1020] = -1;
      //  SmokePt[i + 1021] = -1;
      //  SmokePt[i + 1022] = -1;
      //}
      iPlayer1Car2 = Players_Cars[player1_car];
      iBlockIdx = iPlayer1Car2;
      LoadCarTextures = 0;
      if (game_type == 1) {
        loadtrack(TrackLoad, -1);
        fre((void **)&front_vga[3]);
        front_vga[3] = (tBlockHeader *)load_picture("trkname.bm");
        front_vga[13] = (tBlockHeader *)load_picture("bonustrk.bm");
        front_vga[14] = (tBlockHeader *)load_picture("cupicons.bm");
      } else if (iPlayer1Car2 >= 0) {
        carType = CarDesigns[iPlayer1Car2].carType;
        carType2 = carType;
        iCarTexLoaded = car_texs_loaded[carType];
        iLoadCarTextures = 1;
        if (iCarTexLoaded == -1) {
          LoadCarTexture(carType, 1u);
          car_texmap[iBlockIdx] = 1;
          car_texs_loaded[carType2] = 1;
          iLoadCarTextures = 2;
        } else {
          car_texmap[iBlockIdx] = iCarTexLoaded;
        }
        LoadCarTextures = iLoadCarTextures;
      }
      NoOfTextures = 255;
      if (SVGA_ON)
        scr_size = 128;
      else
        scr_size = 64;
      if (!dontrestart)
        startmusic(optionssong);
      dontrestart = 0;
      holdmusic = -1;
      ticks = 0;
      frames = 0;
      if (network_on && iInitScreen) {
        broadcast_mode = -667;
        while (broadcast_mode)
          UpdateSDL();
        name_copy(player_names[player1_car], my_name);
      }
    LABEL_45:
      if (!iContinue)
        break;
      my_car = Players_Cars[player1_car];
      name_copy(my_name, player_names[player1_car]);
      iControl = manual_control[player1_car];
      my_invul = player_invul[player1_car];
      my_control = iControl;
      last_replay = replaytype;
      if (quit_game && network_on) {
        broadcast_mode = -666;
        while (broadcast_mode)
          UpdateSDL();
        tick_on = 0;
        ticks = 0;
        while (ticks < 3)
          ;
        close_network();
      }
      releasesamples();
      if (game_type != 4 && game_type != 3)
        holdmusic = 0;
      {
        MenuRenderer *mr = GetMenuRenderer();
        menu_render_begin_fade(mr, 0, 32);
        fade_music_start(0);
        menu_render_fade_wait(mr, fade_redraw_bg, mr);
        fade_music_finish(0);
        palette_brightness = 0;
        // Zero pal_addr so software screens see a black palette
        // (GPU fade-out doesn't touch pal_addr, but fade_palette(0) expects it zeroed)
        for (int i = 0; i < 256; i++) {
          pal_addr[i].byR = 0;
          pal_addr[i].byB = 0;
          pal_addr[i].byG = 0;
        }
      }
      if (game_type != 4 && game_type != 3)
        stopmusic();
      front_fade = 0;
      Players_Cars[player1_car] = iBlockIdx;

      for (int i = 0; i < 16; ++i) {
        fre((void**)&front_vga[i]);
      }
      //ppFrontVgaItr = front_vga;
      //do {
      //  pFrontVgaToFre = (void **)ppFrontVgaItr++;
      //  fre(pFrontVgaToFre);
      //} while (ppFrontVgaItr != &front_vga[16]);

      if (iBlockIdx >= CAR_DESIGN_AUTO) {
        for (int i = 0; i < 16; ++i) {
          fre((void**)&cartex_vga[i]);
        }
        //ppCartexVgaItr = cartex_vga;
        //do {
        //  pCartexVgaToFre = (void **)ppCartexVgaItr++;
        //  fre(pCartexVgaToFre);
        //} while (ppCartexVgaItr != &cartex_vga[16]);
        remove_mapsels();
      }
      gfx_size = p_tex_size;
      no_clear = 0;
      if (!quit_game && !intro) {
        check_cars();
        if (network_on) {
          if (iMenuSelection == 8 && !intro)
            NetworkWait();
        }
      }
      if (iMenuSelection < 8 || !network_on || intro)
        time_to_start = 45;
      if (time_to_start)
        goto LABEL_232;
    }
    iInitScreen = 0;
    if (switch_types) {
      game_type = switch_types - 1;
      switch_types = 0;
      if (!game_type && competitors == 1)
        competitors = 16;
      if (iBlockIdx >= CAR_DESIGN_AUTO) {
        car_texs_loaded[CarDesigns[iBlockIdx].carType] = -1;
        ppCartexVgaItr2 = (void **)cartex_vga;
        do
          fre(ppCartexVgaItr2++);
        while (ppCartexVgaItr2 != (void **)&cartex_vga[16]);
        remove_mapsels();
      }
      if (game_type == 1) {
        loadtrack(TrackLoad, -1);
        fre((void **)&front_vga[3]);
        fre((void **)&front_vga[13]);
        fre((void **)&front_vga[14]);
        front_vga[3] = (tBlockHeader *)load_picture("trkname.bm");
        front_vga[13] = (tBlockHeader *)load_picture("bonustrk.bm");
        front_vga[14] = (tBlockHeader *)load_picture("cupicons.bm");
        Race = ((uint8)TrackLoad - 1) & 7;
      } else {
        fre((void **)&front_vga[3]);
        fre((void **)&front_vga[13]);
        fre((void **)&front_vga[14]);
        front_vga[3] = (tBlockHeader *)load_picture("carnames.bm");
        if (iBlockIdx >= CAR_DESIGN_AUTO) {
          cartype3 = CarDesigns[iBlockIdx].carType;
          cartype4 = cartype3;
          iLoadCarTextures2 = 1;
          iCarTexLoaded2 = car_texs_loaded[cartype3];
          if (iCarTexLoaded2 == -1) {
            LoadCarTexture(cartype3, 1u);
            car_texs_loaded[cartype4] = 1;
            car_texmap[iBlockIdx] = 1;
            iLoadCarTextures2 = 2;
          } else {
            car_texmap[iBlockIdx] = iCarTexLoaded2;
          }
          LoadCarTextures = iLoadCarTextures2;
        }
        network_champ_on = 0;
      }
    }
    nFrames = frames;
    frames = 0;
    if (ticks > 1080 && !iQuitConfirmed && !network_on) {
      intro = -1;
      iContinue = -1;
      replaytype = 2;
    }
    check_cars();

    MenuRenderer *mr = GetMenuRenderer();
    if (!front_fade) {
      front_fade = -1;
      menu_render_begin_fade(mr, 1, 32);
      menu_render_fade_wait(mr, fade_redraw_bg, mr);
      palette_brightness = 32;
      frames = 0;
      if (network_on) {
        while (broadcast_mode)
          UpdateSDL();
        broadcast_mode = -1;
        while (broadcast_mode)
          UpdateSDL();
      }
    }
    menu_render_begin_frame(mr);
    menu_render_background(mr, 0);
    menu_render_sprite(mr, 1, 0, head_x, head_y, 0, pal_addr);
    menu_render_sprite(mr, 6, 0, 36, 2, 0, pal_addr);
    if (iMenuSelection >= 8) {
      menu_render_sprite(mr, 6, 3, 52, 334, 0, pal_addr);
    } else {
      menu_render_sprite(mr, 6, 1, 52, 334, 0, pal_addr);
      menu_render_text(mr, 2, "~", font2_ascii, font2_offsets, sel_posns[iMenuSelection].x, sel_posns[iMenuSelection].y, 0x8Fu, 0, pal_addr);
    }
    if (game_type == 1) {
      menu_render_text(mr, 2, language_buffer, font2_ascii, font2_offsets, sel_posns[1].x + 132, sel_posns[1].y + 7, 0x8Fu, 2u, pal_addr);
      if (Race)
        menu_render_text(mr, 2, &language_buffer[128], font2_ascii, font2_offsets, sel_posns[3].x + 132, sel_posns[3].y + 7, 0x8Fu, 2u, pal_addr);
      else
        menu_render_text(mr, 2, &language_buffer[64], font2_ascii, font2_offsets, sel_posns[3].x + 132, sel_posns[3].y + 7, 0x8Fu, 2u, pal_addr);
    } else {
      menu_render_text(mr, 2, &language_buffer[256], font2_ascii, font2_offsets, sel_posns[1].x + 132, sel_posns[1].y + 7, 0x8Fu, 2u, pal_addr);
      menu_render_text(mr, 2, &language_buffer[320], font2_ascii, font2_offsets, sel_posns[3].x + 132, sel_posns[3].y + 7, 0x8Fu, 2u, pal_addr);
    }
    menu_render_text(mr, 2, &language_buffer[192], font2_ascii, font2_offsets, sel_posns[0].x + 132, sel_posns[0].y + 7, 0x8Fu, 2u, pal_addr);
    menu_render_text(mr, 2, config_buffer, font2_ascii, font2_offsets, sel_posns[2].x + 132, sel_posns[2].y + 7, 0x8Fu, 2u, pal_addr);
    menu_render_text(mr, 2, &language_buffer[384], font2_ascii, font2_offsets, sel_posns[4].x + 132, sel_posns[4].y + 7, 0x8Fu, 2u, pal_addr);
    menu_render_text(mr, 2, &language_buffer[448], font2_ascii, font2_offsets, sel_posns[5].x + 132, sel_posns[5].y + 7, 0x8Fu, 2u, pal_addr);
    menu_render_text(mr, 2, &language_buffer[512], font2_ascii, font2_offsets, sel_posns[6].x + 132, sel_posns[6].y + 7, 0x8Fu, 2u, pal_addr);
    menu_render_text(mr, 2, &config_buffer[640], font2_ascii, font2_offsets, sel_posns[7].x + 132, sel_posns[7].y + 7, 0x8Fu, 2u, pal_addr);
    if (game_type == 1) {
      menu_render_sprite(mr, 14, (TrackLoad - 1) / 8, 500, 300, 0, pal_addr);
      if (TrackLoad <= 0) {
        if (TrackLoad)
          menu_render_text(mr, 2, "EDITOR", font2_ascii, font2_offsets, 190, 350, 0x8Fu, 0, pal_addr);
        else
          menu_render_text(mr, 2, "TRACK ZERO", font2_ascii, font2_offsets, 190, 350, 0x8Fu, 0, pal_addr);
      } else if (TrackLoad >= 17) {
        menu_render_sprite(mr, 13, TrackLoad - 17, 190, 356, 0, pal_addr);
      } else {
        menu_render_sprite(mr, 3, TrackLoad - 1, 190, 356, 0, pal_addr);
      }
      menu_render_load_track_mesh(mr, palette);
      menu_render_draw_track_preview(mr, cur_TrackZ, 1280, iRotation, PREVIEW_X, TRACK_PREVIEW_Y, PREVIEW_W, PREVIEW_H);
      menu_render_set_layer(mr, MENU_LAYER_FOREGROUND);
      if (game_type < 2) {
        iCurLaps = cur_laps[level];
        NoOfLaps = iCurLaps;
        if (competitors == 2)
          NoOfLaps = iCurLaps / 2;
        sprintf(buffer, "%s: %i", &language_buffer[4544], NoOfLaps);
        menu_render_text(mr, 15, buffer, font1_ascii, font1_offsets, 420, 16, 0x8Fu, 1u, pal_addr);
        menu_render_text(mr, 15, &language_buffer[4608], font1_ascii, font1_offsets, 420, 34, 0x8Fu, 1u, pal_addr);
        if (RecordCars[TrackLoad] < 0) {
          sprintf(buffer, "%s", RecordNames[TrackLoad]);
        } else {
          dRecordLap = RecordLaps[TrackLoad] * 100.0;
          //_CHP();
          int iLapTime = (int)dRecordLap;
          iMinutes = iLapTime / 6000;
          iSeconds = (iLapTime / 100) % 60;
          int iHundredths = iLapTime % 100;
          sprintf(
              buffer,
              "%s - %s - %02i:%02i:%02i",
              RecordNames[TrackLoad],
              CompanyNames[RecordCars[TrackLoad] & 0xF],
              iMinutes,
              iSeconds,
              iHundredths);

          //dRecordLap = RecordLaps[TrackLoad] * 100.0;
          ////_CHP();
          //SET_LODWORD(llRecordLap, (int)dRecordLap);
          //SET_HIDWORD(llRecordLap, (int)dRecordLap >> 31);
          //iMinutes = llRecordLap / 6000;
          //SET_LODWORD(llRecordLap, (int)dRecordLap);
          //iSeconds = (int)(llRecordLap / 100) % 60;
          //SET_LODWORD(llRecordLap, (int)dRecordLap);
          //sprintf(
          //  buffer,
          //  "%s - %s - %02i:%02i:%02i",
          //  RecordNames[TrackLoad],
          //  CompanyNames[RecordCars[TrackLoad] & 0xF],
          //  iMinutes,
          //  iSeconds,
          //  (unsigned int)(llRecordLap % 100));
        }
        menu_render_text(mr, 15, buffer, font1_ascii, font1_offsets, 420, 52, 0x8Fu, 1u, pal_addr);
      }
    } else if (iBlockIdx >= CAR_DESIGN_AUTO) {
      if (iBlockIdx == CAR_DESIGN_F1WACK) {
        menu_render_load_car_mesh(mr, CAR_DESIGN_F1WACK, palette);
        menu_render_draw_car_preview(mr, 1280.0f, 6000.0f, Car[0].nYaw, PREVIEW_X, CAR_PREVIEW_Y, PREVIEW_W, PREVIEW_H);
      } else {
        menu_render_load_car_mesh(mr, iBlockIdx, palette);
        menu_render_draw_car_preview(mr, 1280.0f, 2200.0f, Car[0].nYaw, PREVIEW_X, CAR_PREVIEW_Y, PREVIEW_W, PREVIEW_H);
      }
      if (iBlockIdx < CAR_DESIGN_SUICYCO)
        menu_render_sprite(mr, 3, iBlockIdx, 190, 356, 0, pal_addr);
    }
    menu_render_set_layer(mr, MENU_LAYER_FOREGROUND);
    menu_render_sprite(mr, 5, player_type, -4, 247, 0, pal_addr);
    menu_render_sprite(mr, 5, game_type + 5, 135, 247, 0, pal_addr);
    switch (iMenuSelection) {
      case 1:
        if (game_type == 1)
          iBlockIdx2 = 8;
        else
          iBlockIdx2 = 1;
        menu_render_sprite(mr, 4, iBlockIdx2, 76, 257, -1, pal_addr);
        break;
      case 3:
        if (game_type == 1 && Race > 0)
          goto LABEL_102;
        menu_render_sprite(mr, 4, 3, 76, 257, -1, pal_addr);
        break;
      case 6:
      LABEL_102:
        menu_render_sprite(mr, 4, 9, 76, 257, -1, pal_addr);
        break;
      case 7:
        menu_render_sprite(mr, 4, 6, 76, 257, -1, pal_addr);
        break;
      case 8:
        menu_render_sprite(mr, 4, 7, 76, 257, -1, pal_addr);
        break;
      default:
        menu_render_sprite(mr, 4, iMenuSelection, 76, 257, -1, pal_addr);
        break;
    }
    if (iBlockIdx < CAR_DESIGN_AUTO)
      menu_render_text(mr, 15, &language_buffer[4160], font1_ascii, font1_offsets, 400, 200, 0xE7u, 1u, pal_addr);
    if (iQuitConfirmed)
      menu_render_text(mr, 15, &language_buffer[3456], font1_ascii, font1_offsets, 400, 250, 0xE7u, 1u, pal_addr);
    show_received_mesage();
    menu_render_end_frame(mr);
    if (switch_same > 0) {
      if (game_type != 1 && switch_same - 666 != iBlockIdx) {
        if (iBlockIdx >= CAR_DESIGN_AUTO) {
          car_texs_loaded[CarDesigns[iBlockIdx].carType] = -1;
          ppCartexVgaItr_2 = cartex_vga;
          do {
            ppCartexVgaToFree_1 = (void **)ppCartexVgaItr_2++;
            fre(ppCartexVgaToFree_1);
          } while (ppCartexVgaItr_2 != &cartex_vga[16]);
          remove_mapsels();
        }
        carType_2 = CarDesigns[switch_same - 666].carType;
        carTypeBackup = carType_2;
        iLoadCarTexFlag = 1;
        iCarTexLoaded_1 = car_texs_loaded[carType_2];
        iCarIdx = switch_same - 666;
        if (iCarTexLoaded_1 == -1) {
          LoadCarTexture(carType_2, 1u);
          car_texs_loaded[carTypeBackup] = 1;
          car_texmap[iCarIdx] = 1;
          iLoadCarTexFlag = 2;
        } else {
          car_texmap[iCarIdx] = iCarTexLoaded_1;
        }
        LoadCarTextures = iLoadCarTexFlag;
      }
      iPlayerIdx_1 = 0;
      if (players > 0) {
        iPlayerOffset = 0;
        do {
          iPlayerOffset += 4;
          *(int *)((char *)&infinite_laps + iPlayerOffset) = switch_same - 666;
          ++iPlayerIdx_1;
        } while (iPlayerIdx_1 < players);
      }

      cheat_mode |= CHEAT_MODE_CLONES;
      //iCheatMode = cheat_mode;
      //BYTE1(iCheatMode) = BYTE1(cheat_mode) | 0x40;
      //cheat_mode = iCheatMode;

      iBlockIdx = switch_same - 666;
    } else if (switch_same < 0) {
      switch_same = 0;
      iPlayerIdx = 0;
      if (players > 0) {
        iPlayerIdx2 = 0;
        do {
          Players_Cars[iPlayerIdx2++] = -1;
          ++iPlayerIdx;
        } while (iPlayerIdx < players);
      }

      cheat_mode &= ~CHEAT_MODE_CLONES;
      //iCheatMode2 = cheat_mode;
      //BYTE1(iCheatMode2) = BYTE1(cheat_mode) & 0xBF;
      //cheat_mode = iCheatMode2;
    }
    if (switch_sets) {
      if (game_type != 1 && iBlockIdx >= CAR_DESIGN_AUTO) {
        ppCartexVgaItr_1 = cartex_vga;
        car_texs_loaded[CarDesigns[iBlockIdx].carType] = -1;
        do {
          ppCartexVgaToFree = (void **)ppCartexVgaItr_1++;
          fre(ppCartexVgaToFree);
        } while (ppCartexVgaItr_1 != &cartex_vga[16]);
        remove_mapsels();
        iCar = CarDesigns[iBlockIdx].carType;
        carTypeToLoad = iCar;
        iCartexLoaded = car_texs_loaded[iCar];
        iLoadCarTextures_1 = 1;
        if (iCartexLoaded == -1) {
          LoadCarTexture(iCar, 1u);
          car_texmap[iBlockIdx] = 1;
          car_texs_loaded[carTypeToLoad] = 1;
          iLoadCarTextures_1 = 2;
        } else {
          car_texmap[iBlockIdx] = iCartexLoaded;
        }
        LoadCarTextures = iLoadCarTextures_1;
      }
      switch_sets = 0;
    }
    print_data = 0;
    while (1) {
      UpdateSDL();
      while (1) {
        UpdateSDL();
        if (!fatkbhit()) {
          nNewYaw = Car[0].nYaw + 32 * nFrames;
          //HIBYTE(nNewYaw) &= 0x3Fu;
          nNewYaw &= 0x3FFF;
          Car[0].nYaw = nNewYaw;
          iRotation = ((uint16)iRotation + 32 * nFrames) & 0x3FFF;
          goto LABEL_45;
        }
        ticks = 0;
        byKey = fatgetch();
        if (iQuitConfirmed)
          break;
        if (byKey) {
          if (byKey == 13)                    // KEY_ENTER
          {
            if (iBlockIdx >= CAR_DESIGN_AUTO) {
              ppCartexVgaToFree_2 = (void **)cartex_vga;
              car_texs_loaded[CarDesigns[iBlockIdx].carType] = -1;
              do
                fre(ppCartexVgaToFree_2++);
              while (ppCartexVgaToFree_2 != (void **)&cartex_vga[16]);
              remove_mapsels();
            }
            // GPU fade-out before leaving main menu to sub-menus
            // (main menu is GPU-rendered, scrbuf is stale, so software
            // fade_palette(0) in sub-menus would flash stale content)
            // Skip for case 7 (Exit to DOS) — menu stays visible for Y/N prompt
            if (iMenuSelection != 7) {
              MenuRenderer *mr2 = GetMenuRenderer();
              menu_render_begin_fade(mr2, 0, 32);
              menu_render_fade_wait(mr2, fade_redraw_bg, mr2);
              palette_brightness = 0;
              for (int i = 0; i < 256; i++) {
                pal_addr[i].byR = 0;
                pal_addr[i].byB = 0;
                pal_addr[i].byG = 0;
              }
              fre((void **)&front_vga[3]);
              fre((void **)&front_vga[13]);
              fre((void **)&front_vga[14]);
            }
            switch (iMenuSelection) {
              case 0:
                sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);
                select_car();
                break;
              case 1:
                sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);
                if (game_type == 1)
                  select_disk();
                else
                  select_track();
                break;
              case 2:
                sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);
                select_configure();
                break;
              case 3:
                sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);
                if (game_type == 1 && Race > 0) {
                  last_type = game_type;
                  game_type = 3;
                  iContinue = -1;
                } else {
                  select_players();
                }
                break;
              case 4:
                sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);
                select_type();
                break;
              case 5:
                iContinue = -1;
                sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);
                replaytype = 2;
                break;
              case 6:
                last_type = game_type;
                game_type = 4;
                iContinue = -1;
                break;
              case 7:
                iQuitConfirmed = -1;
                sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);
                break;
              case 8:
                if (iBlockIdx >= CAR_DESIGN_AUTO) {
                  iContinue = -1;
                  sfxsample(SOUND_SAMPLE_START, 0x8000);
                  netCD = 0;
                  int iTargetTicks = ticks + 108;
                  if (soundon && iTargetTicks > ticks) {
                    while (iTargetTicks > ticks)
                      UpdateSDL();
                  }
                  while (fatkbhit()) {
                    UpdateSDL();
                    fatgetch();
                  }
                  replaytype = replay_record;
                }
                break;
              default:
                break;
            }
            fre((void **)&front_vga[3]);
            fre((void **)&front_vga[13]);
            fre((void **)&front_vga[14]);
            iBlockIdx = Players_Cars[player1_car];
            if (game_type == 1) {
              loadtrack(TrackLoad, -1);
              front_vga[3] = (tBlockHeader *)load_picture("trkname.bm");
              front_vga[13] = (tBlockHeader *)load_picture("bonustrk.bm");
              front_vga[14] = (tBlockHeader *)load_picture("cupicons.bm");
            } else {
              front_vga[3] = (tBlockHeader *)load_picture("carnames.bm");
              if (iBlockIdx >= CAR_DESIGN_AUTO) {
                carType_1 = CarDesigns[iBlockIdx].carType;
                iLoadCarTex2 = 1;
                carTypeSelected = carType_1;
                iCartexLoaded2 = car_texs_loaded[carType_1];
                if (iCartexLoaded2 == -1) {
                  LoadCarTexture(carType_1, 1u);
                  car_texs_loaded[carTypeSelected] = 1;
                  car_texmap[iBlockIdx] = 1;
                  iLoadCarTex2 = 2;
                } else {
                  car_texmap[iBlockIdx] = iCartexLoaded2;
                }
                LoadCarTextures = iLoadCarTex2;
              }
            }
            ticks = 0;
            frames = 0;
          }
        } else {
          byKey2 = fatgetch();
          if (byKey2 >= 0x48u) {
            if (byKey2 <= 0x48u)              // KEY_DOWN
            {
              if (--iMenuSelection < 0)
                iMenuSelection = 0;
            } else if (byKey2 == 80 && ++iMenuSelection > 8)// KEY_UP
            {
              iMenuSelection = 8;
            }
          }
        }
      }
      if (byKey < 0x59u) {
        if (!byKey)
          fatgetch();
      LABEL_155:
        iQuitConfirmed = 0;
      } else {
        if (byKey > 0x59u && byKey != 0x79)
          goto LABEL_155;
        iContinue = -1;
        quit_game = -1;
      }
    }
  }
LABEL_232:
  tick_on = -1;
  messages = -1;
  demo_count = 0;
  demo_control = -1;
  old_mode = -1;
  demo_mode = 0;
  if (replaytype != 2 && !quit_game && game_type < 3)
    AllocateCars();
  if (iMenuSelection == 8 && time_to_start && !intro) {
    localCD = -1;
    if (replaytype != 2) {
      if (player_type && player_type != 2) {
        localCD = cdpresent();
        if (localCD)
          netCD = -1;
      } else {
        localCD = cdpresent();
        //removed by ROLLER, don't error for no CD
        //if (!localCD)
        //  cd_error = -1;
      }
    }
    Race = ((uint8)TrackLoad - 1) & 7;
    if (game_type == 1 && !Race) {
      memset(championship_points, 0, sizeof(championship_points));
      memset(team_points, 0, sizeof(team_points));
      memset(total_kills, 0, sizeof(total_kills));
      memset(total_fasts, 0, sizeof(total_fasts));
      memset(total_wins, 0, sizeof(total_wins));
      memset(team_kills, 0, sizeof(team_kills));
      memset(team_fasts, 0, sizeof(team_fasts));
      memset(team_wins, 0, sizeof(team_wins));
    }
    racers = competitors;
    if (competitors == 2) {
      racers = players;
      if (players < 2)
        racers = competitors;
    }
    if (competitors == 1)
      racers = players;
    j = racers;
    iNonCompetitorIdx = 0;
    if (racers > 0) {
      int iMaxRacers = racers;
      int iNonCompetitorIdx = 0;
      for (int i = 0; i < iMaxRacers; i++)
      {
        // Skip occupied non-competitor slots
        while (non_competitors[iNonCompetitorIdx] != 0)
        {
          iNonCompetitorIdx++;
        }
        // Store the index of the first available non-competitor slot
        grid[i] = iNonCompetitorIdx;
        // Move to next slot for next iteration
        iNonCompetitorIdx++;
      }
      //iMaxRacerOffset = 4 * racers;
      //iGridOffset = 0;
      //do {
      //  for (j = 4 * iNonCompetitorIdx; *(int *)((char *)non_competitors + j); j += 4)
      //    ++iNonCompetitorIdx;
      //  grid[iGridOffset / 4u] = iNonCompetitorIdx;
      //  iGridOffset += 4;
      //  ++iNonCompetitorIdx;
      //} while (iGridOffset < iMaxRacerOffset);
    }
    iRacersIdx = racers;
    iNonCompetitorIdx2 = 0;
    if (racers < numcars) {
      for (int i = racers; i < numcars; i++)
      {
        // Skip empty non-competitor slots (find occupied ones)
        while (non_competitors[iNonCompetitorIdx2] == 0)
        {
          iNonCompetitorIdx2++;
        }
        iRacersIdx++;
        // Store the index of the occupied non-competitor slot
        grid[i] = iNonCompetitorIdx2;
        // Move to next slot for next iteration
        iNonCompetitorIdx2++;
      }
      //iRacersOffset2 = 4 * racers;
      //iMaxRacersOffset2 = 4 * numcars;
      //do {
      //  for (j = 4 * iNonCompetitorIdx2; !*(int *)((char *)non_competitors + j); j += 4)
      //    ++iNonCompetitorIdx2;
      //  ++iRacersIdx;
      //  grid[iRacersOffset2 / 4u] = iNonCompetitorIdx2;
      //  iRacersOffset2 += 4;
      //  ++iNonCompetitorIdx2;
      //} while (iRacersOffset2 < iMaxRacersOffset2);
    }
    if (game_type == 1 && Race > 0) {
      if (racers > 0) {
        for (int i = 0; i <= racers; i++)
        {
          grid[i] = champorder[i];
        }
        //iRacersBytes = 4 * racers;
        //iOffset = 0;
        //do {
        //  iOffset += 4;
        //  // offsets into grid and champorder
        //  finished_car[iOffset / 4 + 15] = teamorder[iOffset / 4 + 7];
        //} while ((int)iOffset < iRacersBytes);
      }
    } else {
      int iShuffleIterations = 6 * racers;
      int iNetworkGridSeed = random_seed;
      for (int k = 0; k < iShuffleIterations; k++)
      {
          // Generate two random indices within the racers range
          //int iRandIdx1 = rand() % racers;
          //int iRandIdx2 = rand() % racers;
          int iRandIdx1 = network_on ? NetworkGridRandRange(racers, &iNetworkGridSeed) : GetHighOrderRand(racers, rand());
          int iRandIdx2 = network_on ? NetworkGridRandRange(racers, &iNetworkGridSeed) : GetHighOrderRand(racers, rand());

          // Swap grid elements
          int iGridTemp = grid[iRandIdx1];
          grid[iRandIdx1] = grid[iRandIdx2];
          grid[iRandIdx2] = iGridTemp;
      }
      //iRacers = racers;
      //iRacersOffset = 6 * racers;
      //for (k = 0; k < iRacersOffset; grid[j] = iGridTemp) {
      //  iRandVal1 = rand(j);
      //  iRandIdx1 = (iRacers * iRandVal1
      //             - (__CFSHL__((iRacers * iRandVal1) >> 31, 15)
      //                + ((iRacers * iRandVal1) >> 31 << 15))) >> 15;
      //  iRandVal2 = rand(iRandIdx1);
      //  j = (iRacers * iRandVal2 - (__CFSHL__((iRacers * iRandVal2) >> 31, 15) + ((iRacers * iRandVal2) >> 31 << 15))) >> 15;
      //  iGridTemp = grid[iRandIdx1];
      //  grid[iRandIdx1] = grid[j];
      //  ++k;
      //}

      for (m = 0; m < players; ++m) {
        // calculate target starting pos based on difficulty
        if (level && (cheat_mode & 2) == 0)
          iTargetPos = racers - 2 * level * players;
        else
          iTargetPos = racers - players;
        if (iTargetPos < 0)
          iTargetPos = 0;

        // find the first human-controlled player in grid
        iHumanIdx = 0;
        for (iGridIdx = 0; !human_control[grid[iGridIdx]]; ++iGridIdx)
          ++iHumanIdx;

        // if human is starting too far back, move them forward
        if (iHumanIdx < iTargetPos) {
          // Find first non-human player at or after target pos
          for (iNonHumanIdx = iTargetPos; ; ++iNonHumanIdx) {
            iSwapGrid1 = grid[iNonHumanIdx];
            if (!human_control[iSwapGrid1])
              break;
            ++iTargetPos;
          }
          iSwapGrid2 = grid[iHumanIdx];
          grid[iHumanIdx] = iSwapGrid1;
          grid[iNonHumanIdx] = iSwapGrid2;
        }
      }

    }
    if (network_on) {
      SDL_Log("[NET-GRID] seed=%d racers=%d p0car=%d p1car=%d grid0=%d grid1=%d grid2=%d grid3=%d\n",
             random_seed, racers, player_to_car[0], player_to_car[1],
             grid[0], grid[1], grid[2], grid[3]);
    }
  }
  StartPressed = 0;
  if (game_type != 4 && game_type != 3)
    stopmusic();
}

//-------------------------------------------------------------------------------------------------
