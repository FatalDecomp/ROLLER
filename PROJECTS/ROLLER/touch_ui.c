#include "touch_ui.h"
#include "3d.h"
#include "control.h"
#include "debug_overlay.h"
#include "frontend.h"
#include "func2.h"
#include "graphics.h"
#include "menu_render.h"
#include "moving.h"
#include "phone_ui.h"
#include "roller.h"
#include "rollerinput.h"
#include "replay.h"
#include "snapshot.h"

#define TOUCH_UI_MOUSE_DEBUG (-1001)
#define TOUCH_UI_MOUSE_ESC   (-1002)
#define TOUCH_UI_MOUSE_CHEAT (-1003)

#define TOUCH_UI_BUTTON_W 54
#define TOUCH_UI_BUTTON_H 24
#define TOUCH_UI_MARGIN   6
#define TOUCH_UI_GAP      6
#define TOUCH_UI_COLOR    143
#define TOUCH_UI_ACTIVE_TURN_COLOR  0xFF
#define TOUCH_UI_ACTIVE_BRAKE_COLOR 0xE7
#define TOUCH_UI_TEXT_Y_OFFSET 8
#define TOUCH_UI_BACKTICK_SIZE 2
#define TOUCH_UI_ACTIVE_BORDER 3

static tTouchButton s_touchButtons[3];
static int s_iTouchButtonCount = 0;
static int s_iTouchCheatHeld = 0;

//-------------------------------------------------------------------------------------------------

static int touch_ui_race_buttons_visible(void)
{
#if defined(IS_ANDROID) || defined(IS_WASM)
  if (!ROLLERPhoneUIActive() || g_bSnapshotMode)
    return 0;
  if (eFrontendCurrentState != eFRONTEND_STATE_RACING)
    return 0;
  if (frontend_on || game_req || intro || winner_mode || replaytype == 2)
    return 0;
  if (!racing && !race_started)
    return 0;

  return -1;
#else
  return 0;
#endif
}

//-------------------------------------------------------------------------------------------------

static int touch_ui_esc_button_visible(void)
{
#if defined(IS_ANDROID) || defined(IS_WASM)
  if (!ROLLERPhoneUIActive() || g_bSnapshotMode)
    return 0;
  if (eFrontendCurrentState != eFRONTEND_STATE_RACING)
    return 0;
  if (frontend_on || game_req || intro || winner_mode)
    return 0;
  if (replaytype == 2)
    return racing != 0;

  return touch_ui_race_buttons_visible();
#else
  return 0;
#endif
}

//-------------------------------------------------------------------------------------------------

static int touch_ui_active_controls_visible(void)
{
#if defined(IS_ANDROID) || defined(IS_WASM)
  if (!ROLLERPhoneUIActive() || !g_bShowActiveTouchControls ||
      g_ePhoneControls == PHONE_CONTROLS_DISABLED)
    return 0;

  return touch_ui_race_buttons_visible();
#else
  return 0;
#endif
}

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
#if !defined(IS_ANDROID) && !defined(IS_WASM)
  return;
#endif
  if (!ROLLERPhoneUIActive() || g_bSnapshotMode)
    return;

  int iTop = TOUCH_UI_MARGIN;
  int iRightX = iVirtualWidth - TOUCH_UI_MARGIN - TOUCH_UI_BUTTON_W;
  int iRaceButtonY = TOUCH_UI_MARGIN + TOUCH_UI_BUTTON_H + TOUCH_UI_GAP;
  int iInRace = touch_ui_race_buttons_visible();
  int iEscVisible = touch_ui_esc_button_visible();
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
                      iEscVisible ? true : false);
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
  if (!ROLLERPhoneUIActive()) {
    s_iTouchCheatHeld = 0;
    return;
  }

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
    if (replaytype == 2 && filingmenu) {
      filingmenu = 0;
      lastfile = 0;
      disciconpressed = 0;
      rotpoint = currentreplayframe;
    } else {
      pause_request = -1;
    }
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

static void touch_ui_active_regions(int iVirtualWidth, int iVirtualHeight,
                                    int *piLeftX, int *piLeftW,
                                    int *piBrakeX, int *piBrakeW,
                                    int *piRightX, int *piRightW,
                                    int *piBrakeFull)
{
  if (iVirtualWidth <= 0)
    iVirtualWidth = 640;
  if (iVirtualHeight <= 0)
    iVirtualHeight = 400;

  *piLeftX = 0;
  *piLeftW = iVirtualWidth / 4;
  *piBrakeX = iVirtualWidth / 4;
  *piBrakeW = iVirtualWidth / 2;
  *piRightX = (iVirtualWidth * 3) / 4;
  *piRightW = iVirtualWidth - *piRightX;
  *piBrakeFull = g_ePhoneControls == PHONE_CONTROLS_TILT_TURN;
  (void)iVirtualHeight;
}

//-------------------------------------------------------------------------------------------------

static void touch_ui_render_menu_box_thick(MenuRenderer *pRenderer,
                                           int iX, int iY, int iWidth,
                                           int iHeight, uint8 byColor)
{
  for (int iInset = 0; iInset < TOUCH_UI_ACTIVE_BORDER; ++iInset)
    menu_render_box(pRenderer, iX + iInset, iY + iInset,
                    iWidth - iInset * 2, iHeight - iInset * 2,
                    byColor, pal_addr);
}

//-------------------------------------------------------------------------------------------------

static void touch_ui_render_active_menu(MenuRenderer *pRenderer,
                                        int iVirtualWidth, int iVirtualHeight)
{
#if defined(IS_ANDROID) || defined(IS_WASM)
  int iLeft = 0;
  int iRight = 0;
  int iBrake = 0;
  int iLeftX;
  int iLeftW;
  int iBrakeX;
  int iBrakeW;
  int iRightX;
  int iRightW;
  int iBrakeFull;

  if (!touch_ui_active_controls_visible())
    return;

  if (iVirtualWidth <= 0)
    iVirtualWidth = 640;
  if (iVirtualHeight <= 0)
    iVirtualHeight = 400;

  InputGetPhoneControlDebugState(&iLeft, &iRight, &iBrake);
  touch_ui_active_regions(iVirtualWidth, iVirtualHeight, &iLeftX, &iLeftW,
                          &iBrakeX, &iBrakeW, &iRightX, &iRightW,
                          &iBrakeFull);

  if (iBrake) {
    if (iBrakeFull)
      touch_ui_render_menu_box_thick(pRenderer, 0, 0, iVirtualWidth,
                                     iVirtualHeight,
                                     TOUCH_UI_ACTIVE_BRAKE_COLOR);
    else
      touch_ui_render_menu_box_thick(pRenderer, iBrakeX, 0, iBrakeW,
                                     iVirtualHeight,
                                     TOUCH_UI_ACTIVE_BRAKE_COLOR);
  }
  if (g_ePhoneControls == PHONE_CONTROLS_TOUCH_TURN && iLeft)
    touch_ui_render_menu_box_thick(pRenderer, iLeftX, 0, iLeftW,
                                   iVirtualHeight,
                                   TOUCH_UI_ACTIVE_TURN_COLOR);
  if (g_ePhoneControls == PHONE_CONTROLS_TOUCH_TURN && iRight)
    touch_ui_render_menu_box_thick(pRenderer, iRightX, 0, iRightW,
                                   iVirtualHeight,
                                   TOUCH_UI_ACTIVE_TURN_COLOR);
#else
  (void)pRenderer;
  (void)iVirtualWidth;
  (void)iVirtualHeight;
#endif
}

//-------------------------------------------------------------------------------------------------

void touch_ui_render_menu(MenuRenderer *pRenderer, int iVirtualWidth,
                          int iVirtualHeight)
{
  if (!pRenderer)
    return;

  touch_ui_build_buttons(iVirtualWidth, iVirtualHeight);
  touch_ui_render_active_menu(pRenderer, iVirtualWidth, iVirtualHeight);
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

static void touch_ui_render_game_box_thick(int iX, int iY, int iWidth,
                                           int iHeight, uint8 byColor)
{
  for (int iInset = 0; iInset < TOUCH_UI_ACTIVE_BORDER; ++iInset)
    box_screen(iX + iInset, iY + iInset,
               iWidth - iInset * 2, iHeight - iInset * 2,
               byColor);
}

//-------------------------------------------------------------------------------------------------

static void touch_ui_render_active_game(int iVirtualWidth, int iVirtualHeight)
{
#if defined(IS_ANDROID) || defined(IS_WASM)
  int iLeft = 0;
  int iRight = 0;
  int iBrake = 0;
  int iLeftX;
  int iLeftW;
  int iBrakeX;
  int iBrakeW;
  int iRightX;
  int iRightW;
  int iBrakeFull;

  if (!touch_ui_active_controls_visible())
    return;

  if (iVirtualWidth <= 0)
    iVirtualWidth = 640;
  if (iVirtualHeight <= 0)
    iVirtualHeight = 400;

  InputGetPhoneControlDebugState(&iLeft, &iRight, &iBrake);
  touch_ui_active_regions(iVirtualWidth, iVirtualHeight, &iLeftX, &iLeftW,
                          &iBrakeX, &iBrakeW, &iRightX, &iRightW,
                          &iBrakeFull);

  if (iBrake) {
    if (iBrakeFull)
      touch_ui_render_game_box_thick(0, 0, iVirtualWidth, iVirtualHeight,
                                     TOUCH_UI_ACTIVE_BRAKE_COLOR);
    else
      touch_ui_render_game_box_thick(iBrakeX, 0, iBrakeW, iVirtualHeight,
                                     TOUCH_UI_ACTIVE_BRAKE_COLOR);
  }
  if (g_ePhoneControls == PHONE_CONTROLS_TOUCH_TURN && iLeft)
    touch_ui_render_game_box_thick(iLeftX, 0, iLeftW, iVirtualHeight,
                                   TOUCH_UI_ACTIVE_TURN_COLOR);
  if (g_ePhoneControls == PHONE_CONTROLS_TOUCH_TURN && iRight)
    touch_ui_render_game_box_thick(iRightX, 0, iRightW, iVirtualHeight,
                                   TOUCH_UI_ACTIVE_TURN_COLOR);
#else
  (void)iVirtualWidth;
  (void)iVirtualHeight;
#endif
}

//-------------------------------------------------------------------------------------------------

void touch_ui_render_game(int iVirtualWidth, int iVirtualHeight)
{
  touch_ui_build_buttons(iVirtualWidth, iVirtualHeight);
  touch_ui_render_active_game(iVirtualWidth, iVirtualHeight);
  for (int iButton = 0; iButton < s_iTouchButtonCount; ++iButton)
    touch_ui_render_game_button(&s_touchButtons[iButton]);
}

//-------------------------------------------------------------------------------------------------

int touch_ui_cheat_pressed(void)
{
  return ROLLERPhoneUIActive() ? s_iTouchCheatHeld : 0;
}

//-------------------------------------------------------------------------------------------------

int touch_ui_point_in_visible_button(int iX, int iY)
{
  if (!ROLLERPhoneUIActive())
    return 0;

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
