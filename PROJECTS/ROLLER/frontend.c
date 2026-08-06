#include "frontend.h"
#include "fsm.h"
#include "fsm_stack.h"

//-------------------------------------------------------------------------------------------------

typedef struct {
  void (*enter)(void);
  void (*update)(void);
  void (*draw)(void);
  void (*exit)(void);
} FrontendScreen;

//-------------------------------------------------------------------------------------------------

eFrontendState eFrontendCurrentState = eFRONTEND_STATE_NONE;
eFrontendState eFrontendNextState = eFRONTEND_STATE_NONE;

#define OVERLAY_STACK_DEPTH 4

static void frontend_fsm_enter(void *context);
static void frontend_fsm_update(void *context);
static void frontend_fsm_draw(void *context);
static void frontend_fsm_exit(void *context);

static const FrontendScreen frontend_screens[eFRONTEND_STATE_QUIT + 1] = {
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

#define FRONTEND_STATE_COUNT ((int)(sizeof(frontend_screens) / sizeof(frontend_screens[0])))

static int frontend_overlay_storage[OVERLAY_STACK_DEPTH];
static FsmMachine frontend_machine = {
  NULL,
  { frontend_fsm_enter, frontend_fsm_update, frontend_fsm_draw, frontend_fsm_exit },
  FRONTEND_STATE_COUNT,
  eFRONTEND_STATE_NONE,
  eFRONTEND_STATE_NONE,
  NULL
};
static FsmStack frontend_overlay_stack = {
  &frontend_machine,
  frontend_overlay_storage,
  OVERLAY_STACK_DEPTH,
  0
};

//-------------------------------------------------------------------------------------------------

static int frontend_state_is_valid(eFrontendState state)
{
  return state >= eFRONTEND_STATE_NONE &&
         state < (eFrontendState)FRONTEND_STATE_COUNT;
}

//-------------------------------------------------------------------------------------------------

static eFrontendState frontend_resolve_state(eFrontendState state)
{
  if (!frontend_state_is_valid(state))
    return eFRONTEND_STATE_NONE;

  if (state == eFRONTEND_STATE_QUIT && !frontend_shutdown_complete())
    return eFRONTEND_STATE_SHUTDOWN;

  return state;
}

//-------------------------------------------------------------------------------------------------

static void frontend_sync_legacy_globals(void)
{
  eFrontendCurrentState = (eFrontendState)fsm_current(&frontend_machine);
  eFrontendNextState = (eFrontendState)fsm_pending(&frontend_machine);
}

//-------------------------------------------------------------------------------------------------

static const FrontendScreen *frontend_current_screen(void)
{
  eFrontendState state = (eFrontendState)fsm_current(&frontend_machine);

  if (!frontend_state_is_valid(state))
    return NULL;

  return &frontend_screens[state];
}

//-------------------------------------------------------------------------------------------------

static void frontend_fsm_enter(void *context)
{
  const FrontendScreen *screen;
  (void)context;

  frontend_sync_legacy_globals();
  screen = frontend_current_screen();
  if (screen && screen->enter)
    screen->enter();
}

//-------------------------------------------------------------------------------------------------

static void frontend_fsm_update(void *context)
{
  const FrontendScreen *screen;
  eFrontendState requested_state;
  (void)context;

  frontend_sync_legacy_globals();
  screen = frontend_current_screen();
  if (screen && screen->update)
    screen->update();

  requested_state = frontend_resolve_state(eFrontendNextState);
  if (requested_state != (eFrontendState)fsm_pending(&frontend_machine))
    (void)fsm_request(&frontend_machine, requested_state);
  frontend_sync_legacy_globals();
}

//-------------------------------------------------------------------------------------------------

static void frontend_fsm_draw(void *context)
{
  const FrontendScreen *screen;
  (void)context;

  frontend_sync_legacy_globals();
  screen = frontend_current_screen();
  if (screen && screen->draw)
    screen->draw();
}

//-------------------------------------------------------------------------------------------------

static void frontend_fsm_exit(void *context)
{
  const FrontendScreen *screen;
  (void)context;

  frontend_sync_legacy_globals();
  screen = frontend_current_screen();
  if (screen && screen->exit)
    screen->exit();
}

//-------------------------------------------------------------------------------------------------

void frontend_set_state(eFrontendState state)
{
  state = frontend_resolve_state(state);

  if (state == (eFrontendState)fsm_current(&frontend_machine)) {
    (void)fsm_request(&frontend_machine, state);
    frontend_sync_legacy_globals();
    return;
  }

  while (fsm_stack_count(&frontend_overlay_stack) > 0)
    (void)fsm_stack_pop(&frontend_overlay_stack);
  (void)fsm_transition(&frontend_machine, state);
  frontend_sync_legacy_globals();
}

//-------------------------------------------------------------------------------------------------

void frontend_update(void)
{
  eFrontendState requested_state;

  requested_state = frontend_resolve_state(eFrontendNextState);
  if (requested_state != (eFrontendState)fsm_pending(&frontend_machine))
    (void)fsm_request(&frontend_machine, requested_state);

  (void)fsm_step(&frontend_machine);
  frontend_sync_legacy_globals();
}

//-------------------------------------------------------------------------------------------------

void push_overlay(eFrontendState overlay)
{
  if (!frontend_state_is_valid(overlay))
    return;

  if (fsm_stack_push(&frontend_overlay_stack, overlay) == FSM_OK)
    frontend_sync_legacy_globals();
}

//-------------------------------------------------------------------------------------------------

void pop_overlay(void)
{
  if (fsm_stack_pop(&frontend_overlay_stack) == FSM_OK)
    frontend_sync_legacy_globals();
}

//-------------------------------------------------------------------------------------------------
