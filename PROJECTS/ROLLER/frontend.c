#include "frontend.h"

//-------------------------------------------------------------------------------------------------

typedef void (*tFrontendEnterFn)(void);
typedef void (*tFrontendUpdateFn)(void);
typedef void (*tFrontendDrawFn)(void);
typedef void (*tFrontendExitFn)(void);

typedef struct {
  tFrontendEnterFn pfnEnter;
  tFrontendUpdateFn pfnUpdate;
  tFrontendDrawFn pfnDraw;
  tFrontendExitFn pfnExit;
} tFrontendScreen;

//-------------------------------------------------------------------------------------------------

eFrontendState eFrontendCurrentState = eFRONTEND_STATE_NONE;
eFrontendState eFrontendNextState = eFRONTEND_STATE_NONE;

#define OVERLAY_STACK_DEPTH 4

static eFrontendState aOverlayStack[OVERLAY_STACK_DEPTH];
static int iOverlayStackTop = 0;

static const tFrontendScreen aScreens[eFRONTEND_STATE_QUIT + 1] = {
  [eFRONTEND_STATE_TITLE] = { NULL, frontend_title_update, NULL, NULL },
  [eFRONTEND_STATE_MAIN_MENU] = { frontend_menu_enter, frontend_menu_update, NULL, NULL },
  [eFRONTEND_STATE_CAR_SELECT] = { frontend_car_select_enter, frontend_car_select_update, NULL, frontend_car_select_exit },
  [eFRONTEND_STATE_TRACK_SELECT] = { frontend_track_select_enter, frontend_track_select_update, NULL, frontend_track_select_exit },
  [eFRONTEND_STATE_DISK_SELECT] = { frontend_disk_select_enter, frontend_disk_select_update, NULL, frontend_disk_select_exit },
  [eFRONTEND_STATE_PLAYERS_SELECT] = { frontend_players_select_enter, frontend_players_select_update, NULL, frontend_players_select_exit },
  [eFRONTEND_STATE_TYPE_SELECT] = { frontend_type_select_enter, frontend_type_select_update, NULL, frontend_type_select_exit },
  [eFRONTEND_STATE_LOBBY]   = { frontend_lobby_enter, frontend_lobby_update, NULL, frontend_lobby_exit },
  [eFRONTEND_STATE_LOADING] = { frontend_loading_enter, frontend_loading_update, NULL, NULL },
  [eFRONTEND_STATE_RACING] = { race_enter, race_update, race_draw, race_exit },
  [eFRONTEND_STATE_PAUSE_OVERLAY] = { frontend_pause_enter, frontend_pause_update, frontend_pause_draw, frontend_pause_exit },
  [eFRONTEND_STATE_RESULTS] = { NULL, frontend_results_update, NULL, NULL },
  [eFRONTEND_STATE_CHAMPIONSHIP_STANDINGS] = { NULL, frontend_championship_standings_update, NULL, NULL },
  [eFRONTEND_STATE_OPTIONS] = { frontend_config_enter, frontend_config_update, NULL, frontend_config_exit },
};

//-------------------------------------------------------------------------------------------------

static int frontend_state_is_valid(eFrontendState eState)
{
  return eState >= eFRONTEND_STATE_NONE &&
         eState < (eFrontendState)(sizeof(aScreens) / sizeof(aScreens[0]));
}

//-------------------------------------------------------------------------------------------------

void frontend_set_state(eFrontendState eState)
{
  if (!frontend_state_is_valid(eState))
    eState = eFRONTEND_STATE_NONE;

  if (eState == eFrontendCurrentState) {
    eFrontendNextState = eState;
    return;
  }

  if (frontend_state_is_valid(eFrontendCurrentState) &&
      aScreens[eFrontendCurrentState].pfnExit)
    aScreens[eFrontendCurrentState].pfnExit();

  eFrontendCurrentState = eState;
  eFrontendNextState = eState;

  if (aScreens[eFrontendCurrentState].pfnEnter)
    aScreens[eFrontendCurrentState].pfnEnter();
}

//-------------------------------------------------------------------------------------------------

void frontend_update(void)
{
  if (eFrontendNextState != eFrontendCurrentState)
    frontend_set_state(eFrontendNextState);

  if (!frontend_state_is_valid(eFrontendCurrentState))
    return;

  if (aScreens[eFrontendCurrentState].pfnUpdate)
    aScreens[eFrontendCurrentState].pfnUpdate();

  if (eFrontendNextState != eFrontendCurrentState) {
    frontend_set_state(eFrontendNextState);
    return;
  }

  if (aScreens[eFrontendCurrentState].pfnDraw)
    aScreens[eFrontendCurrentState].pfnDraw();
}

//-------------------------------------------------------------------------------------------------

void push_overlay(eFrontendState eOverlay)
{
  if (iOverlayStackTop >= OVERLAY_STACK_DEPTH ||
      !frontend_state_is_valid(eOverlay))
    return;

  // Preserve current state WITHOUT calling its exit callback — it stays logically active.
  aOverlayStack[iOverlayStackTop++] = eFrontendCurrentState;
  eFrontendCurrentState = eOverlay;
  eFrontendNextState = eOverlay;

  if (aScreens[eFrontendCurrentState].pfnEnter)
    aScreens[eFrontendCurrentState].pfnEnter();
}

//-------------------------------------------------------------------------------------------------

void pop_overlay(void)
{
  if (iOverlayStackTop <= 0)
    return;

  // Exit the overlay WITHOUT calling the restored state's enter callback.
  if (frontend_state_is_valid(eFrontendCurrentState) &&
      aScreens[eFrontendCurrentState].pfnExit)
    aScreens[eFrontendCurrentState].pfnExit();

  eFrontendCurrentState = aOverlayStack[--iOverlayStackTop];
  eFrontendNextState = eFrontendCurrentState;
}

//-------------------------------------------------------------------------------------------------
