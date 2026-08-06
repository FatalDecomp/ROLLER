#include "frontend.h"
#include "fsm.h"
#include "fsm_stack.h"

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

static void frontend_fsm_enter(void *pContext);
static void frontend_fsm_update(void *pContext);
static void frontend_fsm_draw(void *pContext);
static void frontend_fsm_exit(void *pContext);

static const tFrontendScreen aScreens[eFRONTEND_STATE_QUIT + 1] = {
  [eFRONTEND_STATE_COPYRIGHT] = {
    frontend_copy_screens_enter, frontend_copy_screens_update, NULL, frontend_copy_screens_exit },
  [eFRONTEND_STATE_TITLE] = { frontend_title_enter, frontend_title_update, NULL, frontend_title_exit },
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
  [eFRONTEND_STATE_NETWORK_ERROR] = {
    frontend_network_error_enter, frontend_network_error_update, NULL, frontend_network_error_exit },
  [eFRONTEND_STATE_NO_CD_ERROR] = {
    frontend_no_cd_enter, frontend_no_cd_update, NULL, frontend_no_cd_exit },
  [eFRONTEND_STATE_WINNER_SCREEN] = {
    frontend_winner_screen_enter, frontend_winner_screen_update, NULL, frontend_winner_screen_exit },
  [eFRONTEND_STATE_WINNER_RACE] = {
    frontend_winner_race_enter, frontend_winner_race_update, race_draw,
    frontend_winner_race_exit },
  [eFRONTEND_STATE_RESULT_ROUNDUP] = {
    frontend_result_roundup_enter, frontend_result_roundup_update, NULL, frontend_result_roundup_exit },
  [eFRONTEND_STATE_RACE_RESULT] = {
    frontend_race_result_enter, frontend_race_result_update, NULL, frontend_race_result_exit },
  [eFRONTEND_STATE_CHAMPIONSHIP_STANDINGS] = {
    frontend_championship_standings_enter, frontend_championship_standings_update, NULL,
    frontend_championship_standings_exit },
  [eFRONTEND_STATE_TEAM_STANDINGS] = {
    frontend_team_standings_enter, frontend_team_standings_update, NULL, frontend_team_standings_exit },
  [eFRONTEND_STATE_LAP_RECORDS] = {
    frontend_lap_records_enter, frontend_lap_records_update, NULL, frontend_lap_records_exit },
  [eFRONTEND_STATE_TIME_TRIAL_RESULTS] = {
    frontend_time_trial_results_enter, frontend_time_trial_results_update, NULL,
    frontend_time_trial_results_exit },
  [eFRONTEND_STATE_CHAMPIONSHIP_OVER] = {
    frontend_championship_over_enter, frontend_championship_over_update,
    frontend_championship_over_draw,
    frontend_championship_over_exit },
  [eFRONTEND_STATE_CREDITS] = { frontend_credits_enter, frontend_credits_update, NULL, frontend_credits_exit },
  [eFRONTEND_STATE_OPTIONS] = { frontend_config_enter, frontend_config_update, NULL, frontend_config_exit },
  [eFRONTEND_STATE_SHUTDOWN] = { frontend_shutdown_enter, frontend_shutdown_update, NULL, NULL },
};

#define FRONTEND_STATE_COUNT ((int)(sizeof(aScreens) / sizeof(aScreens[0])))

static int aOverlayStack[OVERLAY_STACK_DEPTH];
static FsmMachine sFrontendMachine = {
  NULL,
  { frontend_fsm_enter, frontend_fsm_update, frontend_fsm_draw, frontend_fsm_exit },
  FRONTEND_STATE_COUNT,
  eFRONTEND_STATE_NONE,
  eFRONTEND_STATE_NONE,
  NULL
};
static FsmStack sFrontendOverlayStack = {
  &sFrontendMachine,
  aOverlayStack,
  OVERLAY_STACK_DEPTH,
  0
};

//-------------------------------------------------------------------------------------------------

static int frontend_state_is_valid(eFrontendState eState)
{
  return eState >= eFRONTEND_STATE_NONE &&
         eState < (eFrontendState)FRONTEND_STATE_COUNT;
}

//-------------------------------------------------------------------------------------------------

static eFrontendState frontend_resolve_state(eFrontendState eState)
{
  if (!frontend_state_is_valid(eState))
    return eFRONTEND_STATE_NONE;

  if (eState == eFRONTEND_STATE_QUIT && !frontend_shutdown_complete())
    return eFRONTEND_STATE_SHUTDOWN;

  return eState;
}

//-------------------------------------------------------------------------------------------------

static void frontend_sync_legacy_globals(void)
{
  eFrontendCurrentState = (eFrontendState)fsm_current(&sFrontendMachine);
  eFrontendNextState = (eFrontendState)fsm_pending(&sFrontendMachine);
}

//-------------------------------------------------------------------------------------------------

static const tFrontendScreen *frontend_current_screen(void)
{
  eFrontendState eState = (eFrontendState)fsm_current(&sFrontendMachine);

  if (!frontend_state_is_valid(eState))
    return NULL;

  return &aScreens[eState];
}

//-------------------------------------------------------------------------------------------------

static void frontend_fsm_enter(void *pContext)
{
  const tFrontendScreen *pScreen;
  (void)pContext;

  frontend_sync_legacy_globals();
  pScreen = frontend_current_screen();
  if (pScreen && pScreen->pfnEnter)
    pScreen->pfnEnter();
}

//-------------------------------------------------------------------------------------------------

static void frontend_fsm_update(void *pContext)
{
  const tFrontendScreen *pScreen;
  eFrontendState eRequestedState;
  (void)pContext;

  frontend_sync_legacy_globals();
  pScreen = frontend_current_screen();
  if (pScreen && pScreen->pfnUpdate)
    pScreen->pfnUpdate();

  eRequestedState = frontend_resolve_state(eFrontendNextState);
  if (eRequestedState != (eFrontendState)fsm_pending(&sFrontendMachine))
    (void)fsm_request(&sFrontendMachine, eRequestedState);
  frontend_sync_legacy_globals();
}

//-------------------------------------------------------------------------------------------------

static void frontend_fsm_draw(void *pContext)
{
  const tFrontendScreen *pScreen;
  (void)pContext;

  frontend_sync_legacy_globals();
  pScreen = frontend_current_screen();
  if (pScreen && pScreen->pfnDraw)
    pScreen->pfnDraw();
}

//-------------------------------------------------------------------------------------------------

static void frontend_fsm_exit(void *pContext)
{
  const tFrontendScreen *pScreen;
  (void)pContext;

  frontend_sync_legacy_globals();
  pScreen = frontend_current_screen();
  if (pScreen && pScreen->pfnExit)
    pScreen->pfnExit();
}

//-------------------------------------------------------------------------------------------------

void frontend_set_state(eFrontendState eState)
{
  eState = frontend_resolve_state(eState);

  if (eState == (eFrontendState)fsm_current(&sFrontendMachine)) {
    (void)fsm_request(&sFrontendMachine, eState);
    frontend_sync_legacy_globals();
    return;
  }

  while (fsm_stack_count(&sFrontendOverlayStack) > 0)
    (void)fsm_stack_pop(&sFrontendOverlayStack);
  (void)fsm_transition(&sFrontendMachine, eState);
  frontend_sync_legacy_globals();
}

//-------------------------------------------------------------------------------------------------

void frontend_update(void)
{
  eFrontendState eRequestedState;

  eRequestedState = frontend_resolve_state(eFrontendNextState);
  if (eRequestedState != (eFrontendState)fsm_pending(&sFrontendMachine))
    (void)fsm_request(&sFrontendMachine, eRequestedState);

  (void)fsm_step(&sFrontendMachine);
  frontend_sync_legacy_globals();
}

//-------------------------------------------------------------------------------------------------

void push_overlay(eFrontendState eOverlay)
{
  if (!frontend_state_is_valid(eOverlay))
    return;

  if (fsm_stack_push(&sFrontendOverlayStack, eOverlay) == FSM_OK)
    frontend_sync_legacy_globals();
}

//-------------------------------------------------------------------------------------------------

void pop_overlay(void)
{
  if (fsm_stack_pop(&sFrontendOverlayStack) == FSM_OK)
    frontend_sync_legacy_globals();
}

//-------------------------------------------------------------------------------------------------
