#ifndef _ROLLER_TOUCH_UI_H
#define _ROLLER_TOUCH_UI_H
//-------------------------------------------------------------------------------------------------
#include "types.h"

typedef struct MenuRenderer MenuRenderer;

void touch_ui_register_buttons(int iVirtualWidth, int iVirtualHeight);
void touch_ui_handle_buttons(void);
void touch_ui_render_menu(MenuRenderer *pRenderer, int iVirtualWidth,
                          int iVirtualHeight);
void touch_ui_render_game(int iVirtualWidth, int iVirtualHeight);
int touch_ui_cheat_pressed(void);
int touch_ui_point_in_visible_button(int iX, int iY);

//-------------------------------------------------------------------------------------------------
#endif
