#include "touch_ui.h"
#include "3d.h"
#include "control.h"
#include "debug_overlay.h"
#include "frontend.h"
#include "func2.h"
#include "graphics.h"
#include "menu_render.h"
#include "roller.h"
#include "snapshot.h"

#define TOUCH_UI_MOUSE_DEBUG (-1001)
#define TOUCH_UI_MOUSE_ESC   (-1002)
#define TOUCH_UI_MOUSE_CHEAT (-1003)

#define TOUCH_UI_BUTTON_W 54
#define TOUCH_UI_BUTTON_H 24
#define TOUCH_UI_MARGIN   6
#define TOUCH_UI_GAP      6
#define TOUCH_UI_COLOR    143
#define TOUCH_UI_TEXT_Y_OFFSET 8
#define TOUCH_UI_BACKTICK_SIZE 2

static tTouchButton s_touchButtons[3];
static int s_iTouchButtonCount = 0;
static int s_iTouchCheatHeld = 0;

//-------------------------------------------------------------------------------------------------

static void touch_ui_add_button(int iId, int iX, int iY, int iWidth,
                                int iHeight, bool bVisible)
{
  if (s_iTouchButtonCount >= (int)(sizeof(s_touchButtons) /
                                   sizeof(s_touchButtons[0])))
    return;

  s_touchButtons[s_iTouchButtonCount].iId = iId;
  s_touchButtons[s_iTouchButtonCount].iX = iX;
  s_touchButtons[s_iTouchButtonCount].iY = iY;
  s_touchButtons[s_iTouchButtonCount].iWidth = iWidth;
  s_touchButtons[s_iTouchButtonCount].iHeight = iHeight;
  s_touchButtons[s_iTouchButtonCount].bVisible = bVisible;
  ++s_iTouchButtonCount;
}

//-------------------------------------------------------------------------------------------------

static const char *touch_ui_button_label(int iId)
{
  switch (iId) {
    case TOUCH_UI_MOUSE_DEBUG:
      return "`";
    case TOUCH_UI_MOUSE_ESC:
      return "ESC";
    case TOUCH_UI_MOUSE_CHEAT:
      return "CHEAT";
    default:
      return "";
  }
}

//-------------------------------------------------------------------------------------------------

static void touch_ui_build_buttons(int iVirtualWidth, int iVirtualHeight)
{
  s_iTouchButtonCount = 0;
#if !defined(IS_ANDROID)
  return;
#endif
  if (g_bSnapshotMode)
    return;

  int iTop = TOUCH_UI_MARGIN;
  int iRightX = iVirtualWidth - TOUCH_UI_MARGIN - TOUCH_UI_BUTTON_W;
  int iRaceButtonY = TOUCH_UI_MARGIN + TOUCH_UI_BUTTON_H + TOUCH_UI_GAP;
  int iInRace = racing || race_started;
  int iCheatVisible = iInRace && (cheat_mode & CHEAT_MODE_CHEAT_CAR) != 0;

  if (iVirtualWidth <= 0)
    iVirtualWidth = 640;
  if (iVirtualHeight <= 0)
    iVirtualHeight = 400;
  if (iRightX < TOUCH_UI_MARGIN)
    iRightX = TOUCH_UI_MARGIN;
  if (iRaceButtonY + TOUCH_UI_BUTTON_H > iVirtualHeight)
    iRaceButtonY = TOUCH_UI_MARGIN;

  touch_ui_add_button(TOUCH_UI_MOUSE_DEBUG, TOUCH_UI_MARGIN, iTop,
                      TOUCH_UI_BUTTON_W, TOUCH_UI_BUTTON_H, true);
  touch_ui_add_button(TOUCH_UI_MOUSE_ESC, TOUCH_UI_MARGIN, iRaceButtonY,
                      TOUCH_UI_BUTTON_W, TOUCH_UI_BUTTON_H,
                      iInRace ? true : false);
  touch_ui_add_button(TOUCH_UI_MOUSE_CHEAT, iRightX, iTop,
                      TOUCH_UI_BUTTON_W, TOUCH_UI_BUTTON_H,
                      iCheatVisible ? true : false);
}

//-------------------------------------------------------------------------------------------------

void touch_ui_register_buttons(int iVirtualWidth, int iVirtualHeight)
{
  touch_ui_build_buttons(iVirtualWidth, iVirtualHeight);

  for (int iButton = 0; iButton < s_iTouchButtonCount; ++iButton) {
    tTouchButton *pButton = &s_touchButtons[iButton];

    if (!pButton->bVisible)
      continue;

    frontend_mouse_register_rect(pButton->iId, pButton->iX, pButton->iY,
                                 pButton->iWidth, pButton->iHeight);
  }
}

//-------------------------------------------------------------------------------------------------

void touch_ui_handle_buttons(void)
{
  int iClicked = frontend_mouse_peek_clicked_id();
  int iHovered = frontend_mouse_peek_hovered_id();

  s_iTouchCheatHeld =
      iHovered == TOUCH_UI_MOUSE_CHEAT && frontend_mouse_left_down();

  if (iClicked != TOUCH_UI_MOUSE_DEBUG &&
      iClicked != TOUCH_UI_MOUSE_ESC &&
      iClicked != TOUCH_UI_MOUSE_CHEAT)
    return;

  if (!frontend_mouse_consume_click_anywhere())
    return;

  if (iClicked == TOUCH_UI_MOUSE_DEBUG) {
    debug_overlay_toggle(ROLLERGetDebugOverlay());
  } else if (iClicked == TOUCH_UI_MOUSE_ESC) {
    pause_request = 1;
  }
}

//-------------------------------------------------------------------------------------------------

static void touch_ui_render_menu_button(MenuRenderer *pRenderer,
                                        const tTouchButton *pButton)
{
  int iTextX;
  int iTextY;

  if (!pButton->bVisible)
    return;

  menu_render_box(pRenderer, pButton->iX, pButton->iY, pButton->iWidth,
                  pButton->iHeight, TOUCH_UI_COLOR, pal_addr);
  if (pButton->iId == TOUCH_UI_MOUSE_DEBUG) {
    int iMarkX = pButton->iX + pButton->iWidth / 2 - 4;
    int iMarkY = pButton->iY + 7;

    menu_render_box(pRenderer, iMarkX, iMarkY,
                    TOUCH_UI_BACKTICK_SIZE, TOUCH_UI_BACKTICK_SIZE,
                    TOUCH_UI_COLOR, pal_addr);
    menu_render_box(pRenderer, iMarkX + 2, iMarkY + 2,
                    TOUCH_UI_BACKTICK_SIZE, TOUCH_UI_BACKTICK_SIZE,
                    TOUCH_UI_COLOR, pal_addr);
    menu_render_box(pRenderer, iMarkX + 4, iMarkY + 4,
                    TOUCH_UI_BACKTICK_SIZE, TOUCH_UI_BACKTICK_SIZE,
                    TOUCH_UI_COLOR, pal_addr);
    return;
  }

  if (!front_vga[15])
    return;

  iTextX = pButton->iX + pButton->iWidth / 2;
  iTextY = pButton->iY + TOUCH_UI_TEXT_Y_OFFSET;
  menu_render_text(pRenderer, 15, touch_ui_button_label(pButton->iId),
                   font1_ascii, font1_offsets, iTextX, iTextY,
                   TOUCH_UI_COLOR, 1, pal_addr);
}

//-------------------------------------------------------------------------------------------------

void touch_ui_render_menu(MenuRenderer *pRenderer, int iVirtualWidth,
                          int iVirtualHeight)
{
  if (!pRenderer)
    return;

  touch_ui_build_buttons(iVirtualWidth, iVirtualHeight);
  for (int iButton = 0; iButton < s_iTouchButtonCount; ++iButton)
    touch_ui_render_menu_button(pRenderer, &s_touchButtons[iButton]);
}

//-------------------------------------------------------------------------------------------------

static void touch_ui_render_game_button(const tTouchButton *pButton)
{
  int iSavedScrSize;

  if (!pButton->bVisible)
    return;

  box_screen(pButton->iX, pButton->iY, pButton->iWidth, pButton->iHeight,
             TOUCH_UI_COLOR);
  if (pButton->iId == TOUCH_UI_MOUSE_DEBUG) {
    int iMarkX = pButton->iX + pButton->iWidth / 2 - 4;
    int iMarkY = pButton->iY + 7;

    box_screen(iMarkX, iMarkY, TOUCH_UI_BACKTICK_SIZE,
               TOUCH_UI_BACKTICK_SIZE, TOUCH_UI_COLOR);
    box_screen(iMarkX + 2, iMarkY + 2, TOUCH_UI_BACKTICK_SIZE,
               TOUCH_UI_BACKTICK_SIZE, TOUCH_UI_COLOR);
    box_screen(iMarkX + 4, iMarkY + 4, TOUCH_UI_BACKTICK_SIZE,
               TOUCH_UI_BACKTICK_SIZE, TOUCH_UI_COLOR);
    return;
  }

  if (!rev_vga[1])
    return;

  iSavedScrSize = scr_size;
  scr_size = 64;
  prt_centrecol(rev_vga[1], touch_ui_button_label(pButton->iId),
                pButton->iX + pButton->iWidth / 2,
                pButton->iY + TOUCH_UI_TEXT_Y_OFFSET,
                TOUCH_UI_COLOR);
  scr_size = iSavedScrSize;
}

//-------------------------------------------------------------------------------------------------

void touch_ui_render_game(int iVirtualWidth, int iVirtualHeight)
{
  touch_ui_build_buttons(iVirtualWidth, iVirtualHeight);
  for (int iButton = 0; iButton < s_iTouchButtonCount; ++iButton)
    touch_ui_render_game_button(&s_touchButtons[iButton]);
}

//-------------------------------------------------------------------------------------------------

int touch_ui_cheat_pressed(void)
{
  return s_iTouchCheatHeld;
}

//-------------------------------------------------------------------------------------------------

int touch_ui_point_in_visible_button(int iX, int iY)
{
  for (int iButton = 0; iButton < s_iTouchButtonCount; ++iButton) {
    tTouchButton *pButton = &s_touchButtons[iButton];

    if (!pButton->bVisible)
      continue;
    if (iX >= pButton->iX && iY >= pButton->iY &&
        iX < pButton->iX + pButton->iWidth &&
        iY < pButton->iY + pButton->iHeight)
      return -1;
  }

  return 0;
}

//-------------------------------------------------------------------------------------------------
