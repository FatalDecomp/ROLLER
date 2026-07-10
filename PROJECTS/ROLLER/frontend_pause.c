#include "frontend.h"
#include "3d.h"
#include "cdx.h"
#include "func2.h"
#include "sound.h"
#include "rollersound.h"
#include "replay.h"
#include "roller.h"

//-------------------------------------------------------------------------------------------------

enum {
  FRONTEND_PAUSE_MOUSE_QUIT_PROMPT = FRONTEND_PAUSE_MOUSE_QUIT_PROMPT_ID,
  FRONTEND_PAUSE_MOUSE_VOLUME_BAR_BASE = 200
};

static int frontend_pause_mouse_scale(int iValue)
{
  return (scr_size * iValue) >> 6;
}

static int frontend_pause_mouse_row_height(int iHeight)
{
  int iScaledHeight = frontend_pause_mouse_scale(iHeight);

  return iScaledHeight > 0 ? iScaledHeight : 1;
}

static void frontend_pause_register_row(int iId, int iY, int iHeight)
{
  frontend_mouse_register_rect(iId, 0,
                               frontend_pause_mouse_scale(iY),
                               winw,
                               frontend_pause_mouse_row_height(iHeight));
}

static int frontend_pause_virtual_width(void)
{
  return winw > 0 ? winw : XMAX;
}

static int frontend_pause_volume_bar_x(void)
{
  return (175 * frontend_pause_virtual_width()) / 320;
}

static int frontend_pause_volume_bar_fill_width(void)
{
  int iWidth = (100 * frontend_pause_virtual_width()) / 320;

  return iWidth > 0 ? iWidth : 1;
}

static int frontend_pause_volume_bar_hit_width(void)
{
  int iWidth = (102 * frontend_pause_virtual_width()) / 320;

  return iWidth > 1 ? iWidth : 2;
}

static int frontend_pause_volume_bar_height(void)
{
  int iHeight = (10 * scr_size) >> 6;

  return iHeight > 0 ? iHeight : 1;
}

static void frontend_pause_register_volume_bar(int iItem, int iY)
{
  frontend_mouse_register_rect(FRONTEND_PAUSE_MOUSE_VOLUME_BAR_BASE + iItem,
                               frontend_pause_volume_bar_x(),
                               frontend_pause_mouse_scale(iY),
                               frontend_pause_volume_bar_hit_width(),
                               frontend_pause_volume_bar_height());
}

static int frontend_pause_main_item_valid(int iItem)
{
  return iItem == 0 || iItem == 1 || iItem == 2 ||
         iItem == 4 || iItem == 5 || iItem == 6;
}

static int frontend_pause_current_item_valid(void)
{
  switch (pausewindow) {
    case 0:
      return frontend_pause_main_item_valid(req_edit);
    case 2:
      return !define_mode && control_select >= 0 && control_select <= 4;
    case 3:
      return graphic_mode >= 0 && graphic_mode <= 16;
    case 4:
      return sound_edit >= 0 && sound_edit <= 7;
    default:
      return 0;
  }
}

static int frontend_pause_item_valid(int iItem)
{
  switch (pausewindow) {
    case 0:
      return frontend_pause_main_item_valid(iItem);
    case 2:
      return !define_mode && iItem >= 0 && iItem <= 4;
    case 3:
      return iItem >= 0 && iItem <= 16;
    case 4:
      return iItem >= 0 && iItem <= 7;
    default:
      return 0;
  }
}

static void frontend_pause_set_current_item(int iItem)
{
  switch (pausewindow) {
    case 0:
      req_edit = iItem;
      break;
    case 2:
      control_select = iItem;
      break;
    case 3:
      graphic_mode = iItem;
      break;
    case 4:
      sound_edit = iItem;
      break;
    default:
      break;
  }
}

static int frontend_pause_item_from_mouse_id(int iMouseId)
{
  int iVolumeItem = iMouseId - FRONTEND_PAUSE_MOUSE_VOLUME_BAR_BASE;

  if (iVolumeItem >= 1 && iVolumeItem <= 4)
    return iVolumeItem;

  return iMouseId;
}

static int frontend_pause_clamp_volume(int iVolume)
{
  if (iVolume < 0)
    return 0;
  if (iVolume >= 128)
    return 127;
  return iVolume;
}

static int frontend_pause_volume_from_click_x(void)
{
  int iClickX = frontend_mouse_peek_click_x();
  int iFillWidth = frontend_pause_volume_bar_fill_width();
  int iLocalX = iClickX - (frontend_pause_volume_bar_x() + 1);

  if (iLocalX < 0)
    iLocalX = 0;
  if (iLocalX > iFillWidth)
    iLocalX = iFillWidth;

  return frontend_pause_clamp_volume(
      (iLocalX * 127 + iFillWidth / 2) / iFillWidth);
}

static void frontend_pause_set_volume_value(int iItem, int iVolume)
{
  iVolume = frontend_pause_clamp_volume(iVolume);

  switch (iItem) {
    case 1:
      EngineVolume = iVolume;
      break;
    case 2:
      SFXVolume = iVolume;
      break;
    case 3:
      SpeechVolume = iVolume;
      break;
    case 4:
      MusicVolume = iVolume;
      if (MusicCard)
        MIDISetMasterVolume(MusicVolume);
      if (MusicCD)
        SetAudioVolume(MusicVolume);
      break;
    default:
      break;
  }
}

static int frontend_pause_apply_volume_click(int iMouseId)
{
  int iItem = iMouseId - FRONTEND_PAUSE_MOUSE_VOLUME_BAR_BASE;

  if (pausewindow != 4 || iItem < 1 || iItem > 4)
    return 0;

  sound_edit = iItem;
  frontend_pause_set_volume_value(iItem, frontend_pause_volume_from_click_x());
  return -1;
}

static int frontend_pause_apply_volume_wheel(int iWheelY)
{
  int iDelta;

  if (iWheelY == 0 || pausewindow != 4 || sound_edit < 1 || sound_edit > 4)
    return 0;

  iDelta = iWheelY * pausewindow;
  switch (sound_edit) {
    case 1:
      EngineVolume = frontend_pause_clamp_volume(EngineVolume + iDelta);
      break;
    case 2:
      SFXVolume = frontend_pause_clamp_volume(SFXVolume + iDelta);
      break;
    case 3:
      SpeechVolume = frontend_pause_clamp_volume(SpeechVolume + iDelta);
      break;
    case 4:
      MusicVolume = frontend_pause_clamp_volume(MusicVolume + iDelta);
      if (MusicCard)
        MIDISetMasterVolume(MusicVolume);
      if (MusicCD)
        SetAudioVolume(MusicVolume);
      break;
    default:
      return 0;
  }

  return -1;
}

static void frontend_pause_register_mouse_items(void)
{
  int iItem;

  frontend_mouse_begin_frame(winw > 0 ? winw : XMAX,
                             winh > 0 ? winh : YMAX);

  if (trying_to_exit) {
    frontend_pause_register_row(FRONTEND_PAUSE_MOUSE_QUIT_PROMPT, 96, 14);
    return;
  }

  switch (pausewindow) {
    case 0:
      frontend_pause_register_row(0, 32, 12);
      frontend_pause_register_row(1, 44, 12);
      frontend_pause_register_row(2, 56, 12);
      frontend_pause_register_row(4, 68, 12);
      frontend_pause_register_row(5, 80, 12);
      frontend_pause_register_row(6, 92, 12);
      break;
    case 2:
      if (define_mode)
        break;
      frontend_pause_register_row(4, 32, 12);
      frontend_pause_register_row(3, 44, 12);
      frontend_pause_register_row(2, 56, 12);
      frontend_pause_register_row(1, 68, 12);
      frontend_pause_register_row(0, 80, 12);
      break;
    case 3:
      for (iItem = 16; iItem >= 1; --iItem)
        frontend_pause_register_row(iItem, 22 + (16 - iItem) * 10, 10);
      frontend_pause_register_row(0, 182, 10);
      break;
    case 4:
      for (iItem = 1; iItem <= 7; ++iItem)
        frontend_pause_register_row(iItem, 44 + (iItem - 1) * 12, 12);
      frontend_pause_register_row(0, 128, 12);
      frontend_pause_register_volume_bar(1, 48);
      frontend_pause_register_volume_bar(2, 60);
      frontend_pause_register_volume_bar(3, 72);
      frontend_pause_register_volume_bar(4, 84);
      break;
    default:
      break;
  }
}

static void frontend_pause_confirm_quit(void)
{
  racing = 0;
  quit_game = -1;
  scr_size = req_size;
  trying_to_exit = 0;
}

static void frontend_pause_give_up_race(void)
{
  paused = 0;
  racing = 0;
  gave_up = -1;
  scr_size = req_size;
}

static void frontend_pause_handle_mouse(void)
{
  int iHovered;
  int iClicked;
  int iWheelY;

  frontend_pause_register_mouse_items();

  if (trying_to_exit) {
    frontend_mouse_take_wheel_y();
    (void)frontend_mouse_take_hovered_id();
    iClicked = frontend_mouse_peek_clicked_id();
    if (frontend_mouse_consume_click_anywhere()) {
      if (iClicked == FRONTEND_PAUSE_MOUSE_QUIT_PROMPT)
        frontend_pause_confirm_quit();
      else
        trying_to_exit = 0;
    }
    return;
  }

  iHovered = frontend_pause_item_from_mouse_id(frontend_mouse_take_hovered_id());
  if (frontend_pause_item_valid(iHovered))
    frontend_pause_set_current_item(iHovered);

  iWheelY = frontend_mouse_take_wheel_y();
  if (frontend_pause_apply_volume_wheel(iWheelY))
    return;

  iClicked = frontend_mouse_peek_clicked_id();
  if (frontend_mouse_consume_click_anywhere()) {
    if (frontend_pause_apply_volume_click(iClicked))
      return;
    iClicked = frontend_pause_item_from_mouse_id(iClicked);
    if (frontend_pause_item_valid(iClicked))
      frontend_pause_set_current_item(iClicked);
    if (frontend_pause_current_item_valid()) {
      if (pausewindow == 0 && req_edit == 1) {
        frontend_pause_give_up_race();
        return;
      }
      frontend_mouse_press_accept();
    }
  }
}

//-------------------------------------------------------------------------------------------------

void frontend_pause_enter(void)
{
  req_size = scr_size;
  if (SVGA_ON)
    scr_size = 128;
  else
    scr_size = 64;
  control_edit = -1;
  req_edit = 0;
  pausewindow = 0;
  game_req = 1;
  paused = 1;
  stopallsamples();
}

//-------------------------------------------------------------------------------------------------

void frontend_pause_update(void)
{
  if (!filingmenu) {
    frontend_pause_handle_mouse();
    game_keys();
  }
  if (pause_request || !racing) {
    pause_request = 0;
    pop_overlay();
  }
}

//-------------------------------------------------------------------------------------------------

void frontend_pause_draw(void)
{
  if (champ_mode < 16)
    updatescreen();
  else
    firework_screen();
}

//-------------------------------------------------------------------------------------------------

void frontend_pause_exit(void)
{
  clear_borders = -1;
  scr_size = req_size;
  game_req = 0;
  paused = 0;
  if (!racing)
    stopallsamples();
  SDL_SetAtomicInt(&iTicksPending, 0);
}

//-------------------------------------------------------------------------------------------------
