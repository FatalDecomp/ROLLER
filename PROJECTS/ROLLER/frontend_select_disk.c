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
//-------------------------------------------------------------------------------------------------

static unsigned int uiFrontendDiskMenuMode = 0;
static int iFrontendDiskSelectedSlot = 0;
static int iFrontendDiskChampResult = 0;
static int iFrontendDiskStatusMessage = 0;
static int iFrontendDiskMenuCursor = 2;
static int iFrontendDiskExitFlag = 0;
static int iFrontendDiskExitFading = 0;
static int iFrontendDiskLoadPending = 0;

//-------------------------------------------------------------------------------------------------

static void frontend_disk_select_black_palette(void)
{
  palette_brightness = 0;
  for (int i = 0; i < 256; ++i) {
    pal_addr[i].byR = 0;
    pal_addr[i].byB = 0;
    pal_addr[i].byG = 0;
  }
}

//-------------------------------------------------------------------------------------------------

static void frontend_disk_select_request_exit(void)
{
  iFrontendDiskExitFlag = -1;
  if (eFrontendCurrentState == eFRONTEND_STATE_DISK_SELECT) {
    MenuRenderer *mr = GetMenuRenderer();
    menu_render_begin_fade(mr, 0, 32);
    iFrontendDiskExitFading = 1;
  }
}

//-------------------------------------------------------------------------------------------------

static int frontend_disk_select_update_load(void)
{
  if (!iFrontendDiskLoadPending)
    return 0;

  if (!load_champ_update())
    return -1;

  iFrontendDiskLoadPending = 0;
  return -1;
}

//-------------------------------------------------------------------------------------------------

static void frontend_disk_select_apply_type_switch(void)
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

static void frontend_disk_select_apply_same_car_switch(void)
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

static void frontend_disk_select_draw_current_game(MenuRenderer *mr)
{
  unsigned int uiCupIndex = (TrackLoad - 1) / 8;

  menu_render_text(mr, 15, &language_buffer[704], font1_ascii,
                   font1_offsets, 400, 270, 0xABu, 1u, pal_addr);
  menu_render_text(mr, 15, &language_buffer[768], font1_ascii,
                   font1_offsets, 400, 290, 0x8Fu, 2u, pal_addr);

  if (uiCupIndex == 0) {
    menu_render_text(mr, 15, &language_buffer[832], font1_ascii,
                     font1_offsets, 405, 290, 0x8Fu, 0, pal_addr);
  } else if (uiCupIndex == 1) {
    menu_render_text(mr, 15, &language_buffer[896], font1_ascii,
                     font1_offsets, 405, 290, 0x8Fu, 0, pal_addr);
  } else if (uiCupIndex == 2) {
    menu_render_text(mr, 15, &language_buffer[4928], font1_ascii,
                     font1_offsets, 405, 290, 0x8Fu, 0, pal_addr);
  }

  menu_render_text(mr, 15, &language_buffer[960], font1_ascii,
                   font1_offsets, 400, 308, 0x8Fu, 2u, pal_addr);
  menu_render_text(mr, 15, CompanyNames[Race], font1_ascii, font1_offsets,
                   405, 308, 0x8Fu, 0, pal_addr);
  menu_render_text(mr, 15, &language_buffer[1024], font1_ascii,
                   font1_offsets, 400, 326, 0x8Fu, 2u, pal_addr);
  if ((unsigned int)competitors < 8) {
    if (competitors == 2)
      menu_render_text(mr, 15, &language_buffer[1088], font1_ascii,
                       font1_offsets, 405, 326, 0x8Fu, 0, pal_addr);
  } else if ((unsigned int)competitors <= 8) {
    menu_render_text(mr, 15, &language_buffer[1152], font1_ascii,
                     font1_offsets, 405, 326, 0x8Fu, 0, pal_addr);
  } else if (competitors == 16) {
    menu_render_text(mr, 15, &language_buffer[1216], font1_ascii,
                     font1_offsets, 405, 326, 0x8Fu, 0, pal_addr);
  }
  menu_render_text(mr, 15, &language_buffer[1280], font1_ascii,
                   font1_offsets, 400, 344, 0x8Fu, 2u, pal_addr);
  menu_render_text(mr, 15, &language_buffer[64 * level + 1472],
                   font1_ascii, font1_offsets, 405, 344, 0x8Fu, 0,
                   pal_addr);
  menu_render_text(mr, 15, &language_buffer[1344], font1_ascii,
                   font1_offsets, 400, 362, 0x8Fu, 2u, pal_addr);
  menu_render_text(mr, 15, &language_buffer[64 * damage_level + 1856],
                   font1_ascii, font1_offsets, 405, 362, 0x8Fu, 0,
                   pal_addr);
  menu_render_text(mr, 15, &language_buffer[1408], font1_ascii,
                   font1_offsets, 400, 380, 0x8Fu, 2u, pal_addr);
  if (player_type == 1 && net_type) {
    if ((unsigned int)net_type >= (unsigned int)player_type &&
        (unsigned int)net_type <= 2)
      menu_render_text(mr, 15, &language_buffer[2304], font1_ascii,
                       font1_offsets, 405, 380, 0x8Fu, 0, pal_addr);
  } else {
    menu_render_text(mr, 15, &language_buffer[64 * player_type + 2112],
                     font1_ascii, font1_offsets, 405, 380, 0x8Fu, 0,
                     pal_addr);
  }
}

//-------------------------------------------------------------------------------------------------

static void frontend_disk_select_draw_saves(MenuRenderer *mr)
{
  int iSlotYPosition = 56;
  int iY = 74;

  for (int iSlotLoop = 0; iSlotLoop < 4; ++iSlotLoop) {
    int iSlotNumber = iSlotLoop + 1;
    uint8 bySlotColor = iFrontendDiskSelectedSlot == iSlotNumber ? 0xAB : 0x8F;

    sprintf(buffer, "%s %i:", &language_buffer[2432], iSlotNumber);
    menu_render_text(mr, 15, buffer, font1_ascii, font1_offsets, 300,
                     iSlotYPosition, bySlotColor, 2u, pal_addr);

    if (save_status[iSlotLoop].iSlotUsed) {
      unsigned int uiSaveCupIndex =
          (save_status[iSlotLoop].iPackedTrack - 1) / 8;
      int iSaveTrackNumber =
          ((save_status[iSlotLoop].iPackedTrack - 1) % 8) + 1;
      uint8 byCupColor =
          iFrontendDiskSelectedSlot == iSlotNumber ? 0xAB : 0x8F;
      uint8 byTrackColor =
          iFrontendDiskSelectedSlot == iSlotNumber ? 0xAB : 0x8F;
      uint8 byDifficultyColor =
          iFrontendDiskSelectedSlot == iSlotNumber ? 0xAB : 0x8F;
      uint8 byLevelColor =
          iFrontendDiskSelectedSlot == iSlotNumber ? 0xAB : 0x8F;
      uint8 byDamageColor =
          iFrontendDiskSelectedSlot == iSlotNumber ? 0xAB : 0x8F;
      uint8 byPlayerTypeColor =
          iFrontendDiskSelectedSlot == iSlotNumber ? 0xAB : 0x8F;

      if (uiSaveCupIndex == 0) {
        menu_render_text(mr, 15, &language_buffer[832], font1_ascii,
                         font1_offsets, 305, iSlotYPosition, byCupColor, 0,
                         pal_addr);
      } else if (uiSaveCupIndex == 1) {
        menu_render_text(mr, 15, &language_buffer[896], font1_ascii,
                         font1_offsets, 305, iSlotYPosition, byCupColor, 0,
                         pal_addr);
      } else if (uiSaveCupIndex == 2) {
        menu_render_text(mr, 15, &language_buffer[4928], font1_ascii,
                         font1_offsets, 305, iSlotYPosition, byCupColor, 0,
                         pal_addr);
      }

      sprintf(buffer, "%s %i", &language_buffer[256], iSaveTrackNumber);
      menu_render_text(mr, 15, "-", font1_ascii, font1_offsets, 470,
                       iSlotYPosition, byTrackColor, 0, pal_addr);
      menu_render_text(mr, 15, buffer, font1_ascii, font1_offsets, 480,
                       iSlotYPosition, byDifficultyColor, 0, pal_addr);
      menu_render_text(mr, 15,
                       &language_buffer[64 * save_status[iSlotLoop].iDifficulty +
                                        1472],
                       font1_ascii, font1_offsets, 460, iY, byLevelColor, 2u,
                       pal_addr);
      menu_render_text(mr, 15, "-", font1_ascii, font1_offsets, 470, iY,
                       byDamageColor, 0, pal_addr);
      menu_render_text(mr, 15,
                       &language_buffer[64 * save_status[iSlotLoop].iPlayerType +
                                        2112],
                       font1_ascii, font1_offsets, 480, iY,
                       byPlayerTypeColor, 0, pal_addr);
    } else {
      uint8 byEmptySlotColor =
          iFrontendDiskSelectedSlot == iSlotNumber ? 0xAB : 0x8F;
      menu_render_text(mr, 15, &language_buffer[2496], font1_ascii,
                       font1_offsets, 305, iSlotYPosition, byEmptySlotColor, 0,
                       pal_addr);
    }

    iSlotYPosition += 40;
    iY += 40;
  }
}

//-------------------------------------------------------------------------------------------------

static void frontend_disk_select_draw_status(MenuRenderer *mr)
{
  switch (iFrontendDiskStatusMessage) {
    case 0:
      if (network_on)
        menu_render_text(mr, 15, &language_buffer[4864], font1_ascii,
                         font1_offsets, 400, 230, 0xE7u, 1u, pal_addr);
      break;
    case 1:
      menu_render_text(mr, 15,
                       iFrontendDiskChampResult ? &language_buffer[2624]
                                                : &language_buffer[2560],
                       font1_ascii, font1_offsets, 400, 230, 0xE7u, 1u,
                       pal_addr);
      break;
    case 2:
      menu_render_text(mr, 15, &language_buffer[2688], font1_ascii,
                       font1_offsets, 400, 230, 0xE7u, 1u, pal_addr);
      break;
    case 4:
      menu_render_text(mr, 15, &language_buffer[2752], font1_ascii,
                       font1_offsets, 400, 230, 0xE7u, 1u, pal_addr);
      break;
    default:
      break;
  }
}

//-------------------------------------------------------------------------------------------------

static void frontend_disk_select_handle_mouse(void)
{
  int iHovered;
  int iClicked;

  frontend_mouse_take_wheel_y();

  iHovered = frontend_mouse_take_hovered_id();
  if (uiFrontendDiskMenuMode) {
    if (iHovered >= 1 && iHovered <= 4) {
      iFrontendDiskSelectedSlot = iHovered;
      iFrontendDiskStatusMessage = 0;
    }
  } else if (iHovered >= 0 && iHovered <= 2) {
    iFrontendDiskMenuCursor = iHovered;
    iFrontendDiskStatusMessage = 0;
  }

  iClicked = frontend_mouse_peek_clicked_id();
  if (uiFrontendDiskMenuMode) {
    if (frontend_mouse_consume_click_anywhere() &&
        iClicked >= 1 && iClicked <= 4) {
      iFrontendDiskSelectedSlot = iClicked;
      iFrontendDiskStatusMessage = 0;
      frontend_mouse_press_accept();
    }
  } else if (frontend_mouse_consume_click_anywhere()) {
    if (iClicked >= 0 && iClicked <= 2)
      iFrontendDiskMenuCursor = iClicked;
    if (iFrontendDiskMenuCursor >= 0 && iFrontendDiskMenuCursor <= 2) {
      iFrontendDiskStatusMessage = 0;
      frontend_mouse_press_accept();
    }
  }
}

//-------------------------------------------------------------------------------------------------

static void frontend_disk_select_draw(void)
{
  MenuRenderer *mr = GetMenuRenderer();

  frontend_mouse_begin_frame(640, 400);
  menu_render_begin_frame(mr);
  if (!front_fade) {
    front_fade = -1;
    menu_render_begin_fade(mr, 1, 32);
  }
  menu_render_background(mr, 0);
  menu_render_sprite(mr, 6, 0, 36, 2, 0, pal_addr);
  menu_render_sprite(mr, 1, 0, head_x, head_y, 0, pal_addr);
  if (iFrontendDiskMenuCursor >= 2) {
    menu_render_sprite(mr, 6, 4, 62, 336, -1, pal_addr);
  } else {
    menu_render_sprite(mr, 6, 2, 62, 336, -1, pal_addr);
    menu_render_text(mr, 2, "~", font2_ascii, font2_offsets,
                     sel_posns[iFrontendDiskMenuCursor].x,
                     sel_posns[iFrontendDiskMenuCursor].y, 0x8Fu, 0,
                     pal_addr);
  }
  menu_render_text(mr, 2, &language_buffer[576], font2_ascii, font2_offsets,
                   sel_posns[0].x + 132, sel_posns[0].y + 7, 0x8Fu, 2u,
                   pal_addr);
  menu_render_text(mr, 2, &language_buffer[640], font2_ascii, font2_offsets,
                   sel_posns[1].x + 132, sel_posns[1].y + 7, 0x8Fu, 2u,
                   pal_addr);

  if (uiFrontendDiskMenuMode) {
    int iSlotY = 56;

    for (int iSlot = 1; iSlot <= 4; ++iSlot) {
      frontend_mouse_register_rect(iSlot, 250, iSlotY, 320, 20);
      iSlotY += 40;
    }
  } else {
    frontend_mouse_register_text(0, front_vga[2], &language_buffer[576],
                                 font2_ascii, font2_offsets,
                                 sel_posns[0].x + 132,
                                 sel_posns[0].y + 7, 2);
    frontend_mouse_register_left_menu_row(0, sel_posns[0].y);
    frontend_mouse_register_text(1, front_vga[2], &language_buffer[640],
                                 font2_ascii, font2_offsets,
                                 sel_posns[1].x + 132,
                                 sel_posns[1].y + 7, 2);
    frontend_mouse_register_left_menu_row(1, sel_posns[1].y);
    if (front_vga[6])
      frontend_mouse_register_rect(2, 62, 336, front_vga[6][4].iWidth,
                                   front_vga[6][4].iHeight);
  }

  frontend_disk_select_draw_current_game(mr);
  frontend_disk_select_draw_saves(mr);
  frontend_disk_select_draw_status(mr);
  show_received_mesage();
  menu_render_end_frame(mr);
}

//-------------------------------------------------------------------------------------------------

static void frontend_disk_select_handle_input(void)
{
  while (fatkbhit()) {
    uint8 byInputKey = fatgetch();

    if (byInputKey < 0xDu) {
      if (!byInputKey) {
        uint8 byExtendedKey = fatgetch();

        if (byExtendedKey == 0x48u) {
          if (uiFrontendDiskMenuMode) {
            if (iFrontendDiskSelectedSlot > 1) {
              iFrontendDiskStatusMessage = 0;
              --iFrontendDiskSelectedSlot;
            }
          } else if (iFrontendDiskMenuCursor > 0) {
            iFrontendDiskStatusMessage = 0;
            --iFrontendDiskMenuCursor;
          }
        } else if (byExtendedKey == 80) {
          if (uiFrontendDiskMenuMode) {
            if (iFrontendDiskSelectedSlot < 4) {
              iFrontendDiskStatusMessage = 0;
              ++iFrontendDiskSelectedSlot;
            }
          } else if (iFrontendDiskMenuCursor < 2) {
            iFrontendDiskStatusMessage = 0;
            ++iFrontendDiskMenuCursor;
          }
        }
      }
    } else if (byInputKey <= 0xDu) {
      if (uiFrontendDiskMenuMode) {
        if (uiFrontendDiskMenuMode <= 1) {
          save_champ(iFrontendDiskSelectedSlot);
          uiFrontendDiskMenuMode = 0;
          check_saves();
          iFrontendDiskStatusMessage = 2;
        } else {
          iFrontendDiskStatusMessage = 1;
          if (save_status[iFrontendDiskSelectedSlot - 1].iSlotUsed) {
            uiFrontendDiskMenuMode = 0;
            iFrontendDiskChampResult = load_champ_begin(iFrontendDiskSelectedSlot);
            if (load_champ_active())
              iFrontendDiskLoadPending = -1;
            return;
          } else {
            iFrontendDiskStatusMessage = 4;
          }
        }
      } else if (iFrontendDiskMenuCursor) {
        if ((unsigned int)iFrontendDiskMenuCursor <= 1) {
          uiFrontendDiskMenuMode = 2;
          iFrontendDiskSelectedSlot = 1;
        } else if (iFrontendDiskMenuCursor == 2) {
          frontend_disk_select_request_exit();
        }
      } else {
        uiFrontendDiskMenuMode = 1;
        iFrontendDiskSelectedSlot = 1;
      }
    } else if (byInputKey == 27) {
      if (uiFrontendDiskMenuMode) {
        uiFrontendDiskMenuMode = 0;
        iFrontendDiskStatusMessage = 0;
      } else {
        frontend_disk_select_request_exit();
      }
    }

    if (iFrontendDiskExitFlag)
      return;
  }
}

//-------------------------------------------------------------------------------------------------

void frontend_disk_select_enter(void)
{
  frontend_disk_select_black_palette();
  uiFrontendDiskMenuMode = 0;
  front_fade = 0;
  {
    extern tColor palette[];
    memcpy(pal_addr, palette, 256 * sizeof(tColor));
    palette_brightness = 32;
  }
  iFrontendDiskExitFlag = 0;
  iFrontendDiskExitFading = 0;
  iFrontendDiskSelectedSlot = 0;
  iFrontendDiskMenuCursor = 2;
  iFrontendDiskStatusMessage = 0;
  iFrontendDiskChampResult = 0;
  iFrontendDiskLoadPending = 0;
  check_saves();
}

void frontend_disk_select_update(void)
{
  frontend_disk_select_apply_type_switch();
  if (!uiFrontendDiskMenuMode)
    iFrontendDiskSelectedSlot = 0;

  frontend_disk_select_draw();
  if (SnapshotShouldStop())
    return;

  if (iFrontendDiskExitFading) {
    MenuRenderer *mr = GetMenuRenderer();
    if (!menu_render_fade_active(mr)) {
      iFrontendDiskExitFading = 0;
      eFrontendNextState = eFRONTEND_STATE_MAIN_MENU;
    }
    return;
  }

  if (frontend_disk_select_update_load())
    return;

  frontend_disk_select_apply_same_car_switch();
  frontend_disk_select_handle_mouse();
  frontend_disk_select_handle_input();
}

//-------------------------------------------------------------------------------------------------

void frontend_disk_select_exit(void)
{
  iFrontendDiskExitFading = 0;
  if (!SnapshotShouldStop())
    frontend_disk_select_black_palette();
  front_fade = 0;

  if (eFrontendCurrentState == eFRONTEND_STATE_DISK_SELECT)
    frontend_menu_resume_from_child();
}

//-------------------------------------------------------------------------------------------------

static void frontend_disk_select_run_snapshot(void)
{
  frontend_disk_select_enter();
  while (!iFrontendDiskExitFlag && !SnapshotShouldStop()) {
    frontend_disk_select_update();
    if (!iFrontendDiskExitFlag && !SnapshotShouldStop())
      UpdateSDLWindow();
  }
  if (!SnapshotShouldStop())
    frontend_disk_select_exit();
}

//-------------------------------------------------------------------------------------------------

void snapshot_render_menu_select_disk(void)
{
  snapshot_setup_frontend_menu_state(1);
  frontend_disk_select_run_snapshot();
}

//-------------------------------------------------------------------------------------------------
