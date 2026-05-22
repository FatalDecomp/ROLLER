#include "frontend.h"
#include "3d.h"
#include "func2.h"
#include "sound.h"
#include "replay.h"
#include "roller.h"

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
  if (!filingmenu)
    game_keys();
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
  remove_uncalibrated();
  check_joystick_usage();
  game_req = 0;
  paused = 0;
  if (!racing)
    stopallsamples();
  SDL_SetAtomicInt(&iTicksPending, 0);
}

//-------------------------------------------------------------------------------------------------
