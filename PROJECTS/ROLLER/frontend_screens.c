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

void snapshot_setup_frontend_menu_state(int iGameType)
{
  load_language_file(szSelectEng, 0);
  load_language_file(szConfigEng, 1);

  time_to_start = 0;
  StartPressed = 0;
  restart_net = 0;
  network_on = 0;
  players = 1;
  player_type = 0;
  player1_car = 0;
  player2_car = 1;
  Players_Cars[player1_car] = CAR_DESIGN_AUTO;
  Players_Cars[player2_car] = CAR_DESIGN_DESILVA;
  game_type = iGameType;
  last_type = 0;
  replaytype = 0;
  Race = 0;
  TrackLoad = 1;
  front_fade = -1;
  frontend_on = -1;
  p_tex_size = gfx_size;

  SVGA_ON = -1;
  init_screen();
  winx = 0;
  winw = XMAX;
  winy = 0;
  winh = YMAX;
  mirror = 0;
  setpal("frontend.pal");

  front_vga[0] = (tBlockHeader*)load_picture("frontend.bm");
  front_vga[1] = (tBlockHeader*)load_picture("selhead.bm");
  front_vga[2] = (tBlockHeader*)load_picture("font2.bm");
  front_vga[4] = (tBlockHeader*)load_picture("opticon2.bm");
  front_vga[5] = (tBlockHeader*)load_picture("selicons.bm");
  front_vga[6] = (tBlockHeader*)load_picture("selexit.bm");
  front_vga[15] = (tBlockHeader*)load_picture("font1.bm");

  FindShades();
  check_cars();
  Car[0].nYaw = 0;
  Car[0].nRoll = 0;
  Car[0].nPitch = 0;
  Car[0].pos.fX = 0.0;
  Car[0].pos.fY = 0.0;
  Car[0].pos.fZ = 0.0;
  set_starts(0);
  for (int i = 0; i < 16; ++i)
    car_texs_loaded[i] = -1;
  car_texs_loaded[0] = 0;
  LoadCarTextures = 0;
  NoOfTextures = 255;
  scr_size = SVGA_ON ? 128 : 64;
  ticks = 0;
  frames = 0;

  MenuRenderer *mr = GetMenuRenderer();
  for (int i = 0; i <= 15; i++) {
    if (front_vga[i])
      menu_render_load_blocks(mr, i, front_vga[i], palette);
  }
}

//-------------------------------------------------------------------------------------------------

static int iFrontendMainMenuInitialized = 0;
static int iFrontendMainMenuResumeFromChild = 0;
static int iFrontendMainMenuSelection = 8;
static int iFrontendMainMenuContinue = 0;
static int iFrontendMainMenuQuitConfirmed = 0;
static int iFrontendMainMenuRotation = 0;
static int iFrontendMainMenuBlockIdx = 0;
static int iFrontendMainMenuPtexSize = 0;
static int iFrontendMainMenuStartDelayTarget = 0;

static void frontend_main_menu_load_renderer_blocks(void)
{
  MenuRenderer *mr = GetMenuRenderer();

  if (!mr)
    return;

  for (int i = 0; i <= 15; ++i) {
    if (front_vga[i])
      menu_render_load_blocks(mr, i, front_vga[i], palette);
  }
}

static void frontend_main_menu_black_palette(void)
{
  palette_brightness = 0;
  for (int i = 0; i < 256; ++i) {
    pal_addr[i].byR = 0;
    pal_addr[i].byB = 0;
    pal_addr[i].byG = 0;
  }
}

static void frontend_main_menu_fade_out(int iFadeMusic)
{
  MenuRenderer *mr = GetMenuRenderer();

  menu_render_begin_fade(mr, 0, 32);
  if (iFadeMusic)
    fade_music_start(0);
  menu_render_fade_wait(mr, fade_redraw_bg, mr);
  if (iFadeMusic)
    fade_music_finish(0);
  frontend_main_menu_black_palette();
}

static void frontend_main_menu_free_selected_car_textures(void)
{
  if (iFrontendMainMenuBlockIdx < CAR_DESIGN_AUTO)
    return;

  car_texs_loaded[CarDesigns[iFrontendMainMenuBlockIdx].carType] = -1;
  for (int i = 0; i < 16; ++i)
    fre((void **)&cartex_vga[i]);
  remove_mapsels();
}

static void frontend_main_menu_load_current_preview_assets(void)
{
  iFrontendMainMenuBlockIdx = Players_Cars[player1_car];

  if (game_type == 1) {
    loadtrack(TrackLoad, -1);
    front_vga[3] = (tBlockHeader *)load_picture("trkname.bm");
    front_vga[13] = (tBlockHeader *)load_picture("bonustrk.bm");
    front_vga[14] = (tBlockHeader *)load_picture("cupicons.bm");
  } else {
    front_vga[3] = (tBlockHeader *)load_picture("carnames.bm");
    if (iFrontendMainMenuBlockIdx >= CAR_DESIGN_AUTO) {
      eCarType carType = CarDesigns[iFrontendMainMenuBlockIdx].carType;
      int iCartexLoaded = car_texs_loaded[carType];
      int iLoadCarTextures = 1;

      if (iCartexLoaded == -1) {
        LoadCarTexture(carType, 1u);
        car_texmap[iFrontendMainMenuBlockIdx] = 1;
        car_texs_loaded[carType] = 1;
        iLoadCarTextures = 2;
      } else {
        car_texmap[iFrontendMainMenuBlockIdx] = iCartexLoaded;
      }
      LoadCarTextures = iLoadCarTextures;
    }
  }

  frontend_main_menu_load_renderer_blocks();
}

static void frontend_main_menu_free_preview_title_assets(void)
{
  fre((void **)&front_vga[3]);
  fre((void **)&front_vga[13]);
  fre((void **)&front_vga[14]);
}

static void frontend_main_menu_resume_after_child(void)
{
  frontend_main_menu_free_preview_title_assets();
  frontend_main_menu_load_current_preview_assets();
  ticks = 0;
  frames = 0;
  iFrontendMainMenuResumeFromChild = 0;
  iFrontendMainMenuInitialized = -1;
}

static void frontend_main_menu_setup(void)
{
  uint8 *pBuf;

  time_to_start = 0;
  StartPressed = 0;
  load_language_file(szSelectEng, 0);
  load_language_file(szConfigEng, 1);
  restart_net = 0;
  cup_won = (textures_off & TEX_OFF_PREMIER_CUP_AVAILABLE) != 0;
  if ((textures_off & TEX_OFF_BONUS_CUP_AVAILABLE) != 0)
    cup_won |= 2;
  loadfatalsample();
  iFrontendMainMenuContinue = 0;
  iFrontendMainMenuRotation = 0;
  player1_car = 0;
  player2_car = 1;
  if (!network_on) {
    players = 1;
    tick_on = 0;
  }
  front_fade = 0;
  frontend_on = -1;
  iFrontendMainMenuPtexSize = gfx_size;

  front_vga[0] = (tBlockHeader *)load_picture("frontend.bm");
  front_vga[1] = (tBlockHeader *)load_picture("selhead.bm");
  front_vga[2] = (tBlockHeader *)load_picture("font2.bm");
  front_vga[3] = (tBlockHeader *)load_picture("carnames.bm");
  front_vga[4] = (tBlockHeader *)load_picture("opticon2.bm");
  front_vga[5] = (tBlockHeader *)load_picture("selicons.bm");
  front_vga[6] = (tBlockHeader *)load_picture("selexit.bm");
  front_vga[15] = (tBlockHeader *)load_picture("font1.bm");

  fade_palette(0);
  iFrontendMainMenuQuitConfirmed = 0;
  SVGA_ON = -1;
  init_screen();
  winx = 0;
  winw = XMAX;
  winy = 0;
  winh = YMAX;
  mirror = 0;
  setpal("frontend.pal");
  frontend_main_menu_load_renderer_blocks();

  if (network_on) {
    Players_Cars[0] = my_car;
    name_copy(player_names[player1_car], my_name);
    manual_control[player1_car] = my_control;
    player_invul[player1_car] = my_invul;
    player_type = 1;
    if ((!game_type || game_type == 2) && last_replay != 2) {
      int iNoClear = no_clear;
      if (!no_clear && network_on > 0) {
        int iPlayer = 0;
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
  if (network_on) {
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
  iFrontendMainMenuSelection = 8;
  if (no_mem)
    gfx_size = 1;
  else if (pBuf)
    gfx_size = no_mem;
  else
    gfx_size = 1;
  fre((void **)&front_vga[7]);
  set_starts(0);
  car_texs_loaded[0] = 0;
  for (int i = 1; i < 16; ++i)
    car_texs_loaded[i] = -1;

  if (game_type == 1) {
    fre((void **)&front_vga[3]);
    frontend_main_menu_load_current_preview_assets();
  } else {
    iFrontendMainMenuBlockIdx = Players_Cars[player1_car];
    if (iFrontendMainMenuBlockIdx >= 0) {
      eCarType carType = CarDesigns[iFrontendMainMenuBlockIdx].carType;
      int iCarTexLoaded = car_texs_loaded[carType];
      int iLoadCarTextures = 1;

      if (iCarTexLoaded == -1) {
        LoadCarTexture(carType, 1u);
        car_texmap[iFrontendMainMenuBlockIdx] = 1;
        car_texs_loaded[carType] = 1;
        iLoadCarTextures = 2;
      } else {
        car_texmap[iFrontendMainMenuBlockIdx] = iCarTexLoaded;
      }
      LoadCarTextures = iLoadCarTextures;
    }
  }

  NoOfTextures = 255;
  scr_size = SVGA_ON ? 128 : 64;
  if (!dontrestart)
    startmusic(optionssong);
  dontrestart = 0;
  holdmusic = -1;
  ticks = 0;
  frames = 0;
  if (network_on) {
    broadcast_mode = -667;
    while (broadcast_mode)
      UpdateSDL();
    name_copy(player_names[player1_car], my_name);
  }

  iFrontendMainMenuStartDelayTarget = 0;
  iFrontendMainMenuInitialized = -1;
}

static void frontend_main_menu_draw(void)
{
  int iBlockIdx2;
  MenuRenderer *mr = GetMenuRenderer();

  menu_render_begin_frame(mr);
  menu_render_background(mr, 0);
  menu_render_sprite(mr, 1, 0, head_x, head_y, 0, pal_addr);
  menu_render_sprite(mr, 6, 0, 36, 2, 0, pal_addr);
  if (iFrontendMainMenuSelection >= 8) {
    menu_render_sprite(mr, 6, 3, 52, 334, 0, pal_addr);
  } else {
    menu_render_sprite(mr, 6, 1, 52, 334, 0, pal_addr);
    menu_render_text(mr, 2, "~", font2_ascii, font2_offsets,
                     sel_posns[iFrontendMainMenuSelection].x,
                     sel_posns[iFrontendMainMenuSelection].y, 0x8Fu, 0,
                     pal_addr);
  }
  if (game_type == 1) {
    menu_render_text(mr, 2, language_buffer, font2_ascii, font2_offsets,
                     sel_posns[1].x + 132, sel_posns[1].y + 7, 0x8Fu, 2u,
                     pal_addr);
    if (Race)
      menu_render_text(mr, 2, &language_buffer[128], font2_ascii,
                       font2_offsets, sel_posns[3].x + 132,
                       sel_posns[3].y + 7, 0x8Fu, 2u, pal_addr);
    else
      menu_render_text(mr, 2, &language_buffer[64], font2_ascii,
                       font2_offsets, sel_posns[3].x + 132,
                       sel_posns[3].y + 7, 0x8Fu, 2u, pal_addr);
  } else {
    menu_render_text(mr, 2, &language_buffer[256], font2_ascii,
                     font2_offsets, sel_posns[1].x + 132,
                     sel_posns[1].y + 7, 0x8Fu, 2u, pal_addr);
    menu_render_text(mr, 2, &language_buffer[320], font2_ascii,
                     font2_offsets, sel_posns[3].x + 132,
                     sel_posns[3].y + 7, 0x8Fu, 2u, pal_addr);
  }
  menu_render_text(mr, 2, &language_buffer[192], font2_ascii, font2_offsets,
                   sel_posns[0].x + 132, sel_posns[0].y + 7, 0x8Fu, 2u,
                   pal_addr);
  menu_render_text(mr, 2, config_buffer, font2_ascii, font2_offsets,
                   sel_posns[2].x + 132, sel_posns[2].y + 7, 0x8Fu, 2u,
                   pal_addr);
  menu_render_text(mr, 2, &language_buffer[384], font2_ascii, font2_offsets,
                   sel_posns[4].x + 132, sel_posns[4].y + 7, 0x8Fu, 2u,
                   pal_addr);
  menu_render_text(mr, 2, &language_buffer[448], font2_ascii, font2_offsets,
                   sel_posns[5].x + 132, sel_posns[5].y + 7, 0x8Fu, 2u,
                   pal_addr);
  menu_render_text(mr, 2, &language_buffer[512], font2_ascii, font2_offsets,
                   sel_posns[6].x + 132, sel_posns[6].y + 7, 0x8Fu, 2u,
                   pal_addr);
  menu_render_text(mr, 2, &config_buffer[640], font2_ascii, font2_offsets,
                   sel_posns[7].x + 132, sel_posns[7].y + 7, 0x8Fu, 2u,
                   pal_addr);

  if (game_type == 1) {
    menu_render_sprite(mr, 14, (TrackLoad - 1) / 8, 500, 300, 0, pal_addr);
    if (TrackLoad <= 0) {
      if (TrackLoad)
        menu_render_text(mr, 2, "EDITOR", font2_ascii, font2_offsets, 190,
                         350, 0x8Fu, 0, pal_addr);
      else
        menu_render_text(mr, 2, "TRACK ZERO", font2_ascii, font2_offsets,
                         190, 350, 0x8Fu, 0, pal_addr);
    } else if (TrackLoad >= 17) {
      menu_render_sprite(mr, 13, TrackLoad - 17, 190, 356, 0, pal_addr);
    } else {
      menu_render_sprite(mr, 3, TrackLoad - 1, 190, 356, 0, pal_addr);
    }
    menu_render_load_track_mesh(mr, palette);
    menu_render_draw_track_preview(mr, cur_TrackZ, 1280,
                                   iFrontendMainMenuRotation, PREVIEW_X,
                                   TRACK_PREVIEW_Y, PREVIEW_W, PREVIEW_H);
    menu_render_set_layer(mr, MENU_LAYER_FOREGROUND);
    if (game_type < 2) {
      int iCurLaps = cur_laps[level];
      NoOfLaps = iCurLaps;
      if (competitors == 2)
        NoOfLaps = iCurLaps / 2;
      sprintf(buffer, "%s: %i", &language_buffer[4544], NoOfLaps);
      menu_render_text(mr, 15, buffer, font1_ascii, font1_offsets, 420, 16,
                       0x8Fu, 1u, pal_addr);
      menu_render_text(mr, 15, &language_buffer[4608], font1_ascii,
                       font1_offsets, 420, 34, 0x8Fu, 1u, pal_addr);
      if (RecordCars[TrackLoad] < 0) {
        sprintf(buffer, "%s", RecordNames[TrackLoad]);
      } else {
        int iLapTime = (int)(RecordLaps[TrackLoad] * 100.0);
        int iMinutes = iLapTime / 6000;
        int iSeconds = (iLapTime / 100) % 60;
        int iHundredths = iLapTime % 100;
        sprintf(buffer, "%s - %s - %02i:%02i:%02i",
                RecordNames[TrackLoad],
                CompanyNames[RecordCars[TrackLoad] & 0xF], iMinutes,
                iSeconds, iHundredths);
      }
      menu_render_text(mr, 15, buffer, font1_ascii, font1_offsets, 420, 52,
                       0x8Fu, 1u, pal_addr);
    }
  } else if (iFrontendMainMenuBlockIdx >= CAR_DESIGN_AUTO) {
    if (iFrontendMainMenuBlockIdx == CAR_DESIGN_F1WACK) {
      menu_render_load_car_mesh(mr, CAR_DESIGN_F1WACK, palette);
      menu_render_draw_car_preview(mr, 1280.0f, 6000.0f, Car[0].nYaw,
                                   PREVIEW_X, CAR_PREVIEW_Y, PREVIEW_W,
                                   PREVIEW_H);
    } else {
      menu_render_load_car_mesh(mr, iFrontendMainMenuBlockIdx, palette);
      menu_render_draw_car_preview(mr, 1280.0f, 2200.0f, Car[0].nYaw,
                                   PREVIEW_X, CAR_PREVIEW_Y, PREVIEW_W,
                                   PREVIEW_H);
    }
    if (iFrontendMainMenuBlockIdx < CAR_DESIGN_SUICYCO)
      menu_render_sprite(mr, 3, iFrontendMainMenuBlockIdx, 190, 356, 0,
                         pal_addr);
  }

  menu_render_set_layer(mr, MENU_LAYER_FOREGROUND);
  menu_render_sprite(mr, 5, player_type, -4, 247, 0, pal_addr);
  menu_render_sprite(mr, 5, game_type + 5, 135, 247, 0, pal_addr);
  switch (iFrontendMainMenuSelection) {
    case 1:
      if (game_type == 1)
        iBlockIdx2 = 8;
      else
        iBlockIdx2 = 1;
      menu_render_sprite(mr, 4, iBlockIdx2, 76, 257, -1, pal_addr);
      break;
    case 3:
      if (game_type == 1 && Race > 0)
        goto FRONTEND_MAIN_MENU_CONTINUE_ICON;
      menu_render_sprite(mr, 4, 3, 76, 257, -1, pal_addr);
      break;
    case 6:
    FRONTEND_MAIN_MENU_CONTINUE_ICON:
      menu_render_sprite(mr, 4, 9, 76, 257, -1, pal_addr);
      break;
    case 7:
      menu_render_sprite(mr, 4, 6, 76, 257, -1, pal_addr);
      break;
    case 8:
      menu_render_sprite(mr, 4, 7, 76, 257, -1, pal_addr);
      break;
    default:
      menu_render_sprite(mr, 4, iFrontendMainMenuSelection, 76, 257, -1,
                         pal_addr);
      break;
  }
  if (iFrontendMainMenuBlockIdx < CAR_DESIGN_AUTO)
    menu_render_text(mr, 15, &language_buffer[4160], font1_ascii,
                     font1_offsets, 400, 200, 0xE7u, 1u, pal_addr);
  if (iFrontendMainMenuQuitConfirmed)
    menu_render_text(mr, 15, &language_buffer[3456], font1_ascii,
                     font1_offsets, 400, 250, 0xE7u, 1u, pal_addr);
  show_received_mesage();
  menu_render_end_frame(mr);
}

void snapshot_render_menu_main(void)
{
  snapshot_setup_frontend_menu_state(0);
  iFrontendMainMenuSelection = 8;
  iFrontendMainMenuQuitConfirmed = 0;
  iFrontendMainMenuRotation = 0;
  frontend_main_menu_load_current_preview_assets();
  frontend_main_menu_draw();
}

static void frontend_main_menu_apply_type_switch(void)
{
  if (!switch_types)
    return;

  game_type = switch_types - 1;
  switch_types = 0;
  if (!game_type && competitors == 1)
    competitors = 16;
  frontend_main_menu_free_selected_car_textures();
  frontend_main_menu_free_preview_title_assets();
  if (game_type == 1) {
    loadtrack(TrackLoad, -1);
    front_vga[3] = (tBlockHeader *)load_picture("trkname.bm");
    front_vga[13] = (tBlockHeader *)load_picture("bonustrk.bm");
    front_vga[14] = (tBlockHeader *)load_picture("cupicons.bm");
    Race = ((uint8)TrackLoad - 1) & 7;
  } else {
    front_vga[3] = (tBlockHeader *)load_picture("carnames.bm");
    if (iFrontendMainMenuBlockIdx >= CAR_DESIGN_AUTO) {
      eCarType carType = CarDesigns[iFrontendMainMenuBlockIdx].carType;
      int iCarTexLoaded = car_texs_loaded[carType];
      int iLoadCarTextures = 1;

      if (iCarTexLoaded == -1) {
        LoadCarTexture(carType, 1u);
        car_texs_loaded[carType] = 1;
        car_texmap[iFrontendMainMenuBlockIdx] = 1;
        iLoadCarTextures = 2;
      } else {
        car_texmap[iFrontendMainMenuBlockIdx] = iCarTexLoaded;
      }
      LoadCarTextures = iLoadCarTextures;
    }
    network_champ_on = 0;
  }
  frontend_main_menu_load_renderer_blocks();
}

static void frontend_main_menu_apply_same_car_switch(void)
{
  if (switch_same > 0) {
    if (game_type != 1 && switch_same - 666 != iFrontendMainMenuBlockIdx) {
      frontend_main_menu_free_selected_car_textures();
      eCarType carType = CarDesigns[switch_same - 666].carType;
      int iCarTexLoaded = car_texs_loaded[carType];
      int iCarIdx = switch_same - 666;
      int iLoadCarTextures = 1;

      if (iCarTexLoaded == -1) {
        LoadCarTexture(carType, 1u);
        car_texs_loaded[carType] = 1;
        car_texmap[iCarIdx] = 1;
        iLoadCarTextures = 2;
      } else {
        car_texmap[iCarIdx] = iCarTexLoaded;
      }
      LoadCarTextures = iLoadCarTextures;
    }
    for (int iPlayerIdx = 0; iPlayerIdx < players; ++iPlayerIdx)
      Players_Cars[iPlayerIdx] = switch_same - 666;
    cheat_mode |= CHEAT_MODE_CLONES;
    iFrontendMainMenuBlockIdx = switch_same - 666;
  } else if (switch_same < 0) {
    switch_same = 0;
    for (int iPlayerIdx = 0; iPlayerIdx < players; ++iPlayerIdx)
      Players_Cars[iPlayerIdx] = -1;
    cheat_mode &= ~CHEAT_MODE_CLONES;
  }

  if (switch_sets) {
    if (game_type != 1 && iFrontendMainMenuBlockIdx >= CAR_DESIGN_AUTO) {
      frontend_main_menu_free_selected_car_textures();
      eCarType carType = CarDesigns[iFrontendMainMenuBlockIdx].carType;
      int iCarTexLoaded = car_texs_loaded[carType];
      int iLoadCarTextures = 1;

      if (iCarTexLoaded == -1) {
        LoadCarTexture(carType, 1u);
        car_texmap[iFrontendMainMenuBlockIdx] = 1;
        car_texs_loaded[carType] = 1;
        iLoadCarTextures = 2;
      } else {
        car_texmap[iFrontendMainMenuBlockIdx] = iCarTexLoaded;
      }
      LoadCarTextures = iLoadCarTextures;
    }
    switch_sets = 0;
  }
}

static void frontend_main_menu_prepare_race_start(void)
{
  tick_on = -1;
  messages = -1;
  demo_count = 0;
  demo_control = -1;
  old_mode = -1;
  demo_mode = 0;
  if (replaytype != 2 && !quit_game && game_type < 3)
    AllocateCars();
  if (iFrontendMainMenuSelection == 8 && time_to_start && !intro) {
    localCD = -1;
    if (replaytype != 2) {
      if (player_type && player_type != 2) {
        localCD = cdpresent();
        if (localCD)
          netCD = -1;
      } else {
        localCD = cdpresent();
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

    int iNonCompetitorIdx = 0;
    for (int i = 0; i < racers; ++i) {
      while (non_competitors[iNonCompetitorIdx] != 0)
        ++iNonCompetitorIdx;
      grid[i] = iNonCompetitorIdx++;
    }

    int iNonCompetitorIdx2 = 0;
    for (int i = racers; i < numcars; ++i) {
      while (non_competitors[iNonCompetitorIdx2] == 0)
        ++iNonCompetitorIdx2;
      grid[i] = iNonCompetitorIdx2++;
    }

    if (game_type == 1 && Race > 0) {
      for (int i = 0; i < racers; ++i)
        grid[i] = champorder[i];
    } else {
      int iShuffleIterations = 6 * racers;
      int iNetworkGridSeed = random_seed;
      for (int k = 0; k < iShuffleIterations; ++k) {
        int iRandIdx1 = network_on ? NetworkGridRandRange(racers, &iNetworkGridSeed)
                                   : GetHighOrderRand(racers, ROLLERrandRaw());
        int iRandIdx2 = network_on ? NetworkGridRandRange(racers, &iNetworkGridSeed)
                                   : GetHighOrderRand(racers, ROLLERrandRaw());
        int iGridTemp = grid[iRandIdx1];
        grid[iRandIdx1] = grid[iRandIdx2];
        grid[iRandIdx2] = iGridTemp;
      }

      for (int m = 0; m < players && racers > 0; ++m) {
        int iTargetPos;
        if (level && (cheat_mode & CHEAT_MODE_DEATH_MODE) == 0)
          iTargetPos = racers - 2 * level * players;
        else
          iTargetPos = racers - players;
        if (iTargetPos < 0)
          iTargetPos = 0;

        int iHumanIdx = 0;
        int iGridIdx = 0;
        while (iGridIdx < racers && !human_control[grid[iGridIdx]]) {
          ++iGridIdx;
          ++iHumanIdx;
        }
        if (iGridIdx >= racers)
          break;

        if (iHumanIdx < iTargetPos) {
          int iNonHumanIdx = iTargetPos;
          while (iNonHumanIdx < racers && human_control[grid[iNonHumanIdx]]) {
            ++iNonHumanIdx;
            ++iTargetPos;
          }
          if (iNonHumanIdx < racers) {
            int iSwapGrid1 = grid[iNonHumanIdx];
            int iSwapGrid2 = grid[iHumanIdx];
            grid[iHumanIdx] = iSwapGrid1;
            grid[iNonHumanIdx] = iSwapGrid2;
          }
        }
      }
    }
  }
  StartPressed = 0;
  if (game_type != 4 && game_type != 3)
    stopmusic();
}

static void frontend_main_menu_prepare_to_start(void)
{
  my_car = Players_Cars[player1_car];
  name_copy(my_name, player_names[player1_car]);
  my_invul = player_invul[player1_car];
  my_control = manual_control[player1_car];
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
  frontend_main_menu_fade_out(-1);
  if (game_type != 4 && game_type != 3)
    stopmusic();
  front_fade = 0;
  Players_Cars[player1_car] = iFrontendMainMenuBlockIdx;

  for (int i = 0; i < 16; ++i)
    fre((void **)&front_vga[i]);
  frontend_main_menu_free_selected_car_textures();

  gfx_size = iFrontendMainMenuPtexSize;
  no_clear = 0;
  if (!quit_game && !intro) {
    check_cars();
    if (network_on && iFrontendMainMenuSelection == 8 && !intro)
      NetworkWait();
  }
  if (iFrontendMainMenuSelection < 8 || !network_on || intro)
    time_to_start = 45;
  if (!time_to_start && !quit_game) {
    frontend_main_menu_setup();
    return;
  }
  if (time_to_start)
    frontend_main_menu_prepare_race_start();

  iFrontendMainMenuInitialized = 0;
  iFrontendMainMenuStartDelayTarget = 0;
  eFrontendNextState = quit_game ? eFRONTEND_STATE_QUIT : eFRONTEND_STATE_LOADING;
}

static void frontend_main_menu_begin_child(eFrontendState eState)
{
  frontend_main_menu_free_selected_car_textures();
  frontend_main_menu_fade_out(0);
  frontend_main_menu_free_preview_title_assets();
  iFrontendMainMenuResumeFromChild = -1;
  eFrontendNextState = eState;
}

static void frontend_main_menu_handle_enter(void)
{
  if ((iFrontendMainMenuSelection >= 0 && iFrontendMainMenuSelection <= 5) ||
      iFrontendMainMenuSelection == 7) {
    sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);
  }

  switch (iFrontendMainMenuSelection) {
    case 0:
      frontend_main_menu_begin_child(eFRONTEND_STATE_CAR_SELECT);
      break;
    case 1:
      if (game_type == 1)
        frontend_main_menu_begin_child(eFRONTEND_STATE_DISK_SELECT);
      else
        frontend_main_menu_begin_child(eFRONTEND_STATE_TRACK_SELECT);
      break;
    case 2:
      frontend_main_menu_begin_child(eFRONTEND_STATE_OPTIONS);
      break;
    case 3:
      if (game_type == 1 && Race > 0) {
        last_type = game_type;
        game_type = 3;
        iFrontendMainMenuContinue = -1;
        frontend_main_menu_prepare_to_start();
      } else {
        frontend_main_menu_begin_child(eFRONTEND_STATE_PLAYERS_SELECT);
      }
      break;
    case 4:
      frontend_main_menu_begin_child(eFRONTEND_STATE_TYPE_SELECT);
      break;
    case 5:
      iFrontendMainMenuContinue = -1;
      replaytype = 2;
      frontend_main_menu_prepare_to_start();
      break;
    case 6:
      last_type = game_type;
      game_type = 4;
      iFrontendMainMenuContinue = -1;
      frontend_main_menu_prepare_to_start();
      break;
    case 7:
      iFrontendMainMenuQuitConfirmed = -1;
      break;
    case 8:
      if (iFrontendMainMenuBlockIdx >= CAR_DESIGN_AUTO) {
        iFrontendMainMenuContinue = -1;
        frontend_main_menu_free_selected_car_textures();
        frontend_main_menu_fade_out(0);
        frontend_main_menu_free_preview_title_assets();
        sfxsample(SOUND_SAMPLE_START, 0x8000);
        netCD = 0;
        if (soundon) {
          iFrontendMainMenuStartDelayTarget = ticks + 108;
        } else {
          replaytype = replay_record;
          frontend_main_menu_prepare_to_start();
        }
      }
      break;
    default:
      break;
  }
}

static void frontend_main_menu_handle_quit_confirmation(uint8 byKey)
{
  if (byKey < 0x59u) {
    if (!byKey)
      fatgetch();
    iFrontendMainMenuQuitConfirmed = 0;
    return;
  }
  if (byKey > 0x59u && byKey != 0x79) {
    iFrontendMainMenuQuitConfirmed = 0;
    return;
  }
  iFrontendMainMenuContinue = -1;
  quit_game = -1;
  frontend_main_menu_prepare_to_start();
}

static void frontend_main_menu_handle_input(void)
{
  while (fatkbhit()) {
    uint8 byKey;

    ticks = 0;
    byKey = fatgetch();
    if (iFrontendMainMenuQuitConfirmed) {
      frontend_main_menu_handle_quit_confirmation(byKey);
      if (eFrontendNextState != eFrontendCurrentState)
        return;
      continue;
    }
    if (byKey) {
      if (byKey == 13) {
        frontend_main_menu_handle_enter();
        if (eFrontendNextState != eFrontendCurrentState)
          return;
      }
    } else {
      uint8 byKey2 = fatgetch();
      if (byKey2 >= 0x48u) {
        if (byKey2 <= 0x48u) {
          if (--iFrontendMainMenuSelection < 0)
            iFrontendMainMenuSelection = 0;
        } else if (byKey2 == 80 && ++iFrontendMainMenuSelection > 8) {
          iFrontendMainMenuSelection = 8;
        }
      }
    }
  }
}

static void frontend_main_menu_update_rotation(int nFrames)
{
  int16 nNewYaw = Car[0].nYaw + 32 * nFrames;

  nNewYaw &= 0x3FFF;
  Car[0].nYaw = nNewYaw;
  iFrontendMainMenuRotation = ((uint16)iFrontendMainMenuRotation +
                               32 * nFrames) &
                              0x3FFF;
}

void frontend_menu_enter(void)
{
  start_race = 0;
  time_to_start = 0;
  if (restart_net) {
    iFrontendMainMenuInitialized = 0;
    return;
  }
  if (iFrontendMainMenuResumeFromChild)
    frontend_main_menu_resume_after_child();
  else
    frontend_main_menu_setup();
}

void frontend_menu_update(void)
{
  int nFrames;
  MenuRenderer *mr;

  if (restart_net) {
    restart_net_game();
    restart_net = 0;
    eFrontendNextState = quit_game ? eFRONTEND_STATE_QUIT : eFRONTEND_STATE_LOADING;
    return;
  }

  if (!iFrontendMainMenuInitialized)
    frontend_main_menu_setup();

  if (iFrontendMainMenuStartDelayTarget) {
    if (ticks >= iFrontendMainMenuStartDelayTarget) {
      while (fatkbhit())
        fatgetch();
      replaytype = replay_record;
      frontend_main_menu_prepare_to_start();
    }
    return;
  }

  frontend_main_menu_apply_type_switch();
  nFrames = frames;
  frames = 0;
  if (ticks > 1080 && !iFrontendMainMenuQuitConfirmed && !network_on) {
    intro = -1;
    iFrontendMainMenuContinue = -1;
    replaytype = 2;
    frontend_main_menu_prepare_to_start();
    return;
  }
  check_cars();

  mr = GetMenuRenderer();
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

  frontend_main_menu_draw();
  if (SnapshotShouldStop())
    return;

  frontend_main_menu_apply_same_car_switch();
  print_data = 0;

  frontend_main_menu_handle_input();
  if (eFrontendNextState != eFrontendCurrentState)
    return;
  frontend_main_menu_update_rotation(nFrames);
}

void frontend_menu_resume_from_child(void)
{
  iFrontendMainMenuResumeFromChild = -1;
}

