#ifndef _ROLLER_FRONTEND_H
#define _ROLLER_FRONTEND_H
//-------------------------------------------------------------------------------------------------
#include "types.h"
#include "func3.h"
//-------------------------------------------------------------------------------------------------
// 3D preview viewport (GPU backend only; software backend ignores these)
#define PREVIEW_X         248
#define PREVIEW_W         300
#define PREVIEW_H         330
#define CAR_PREVIEW_Y      57
#define TRACK_PREVIEW_Y    5

typedef struct
{
  int x;
  int y;
} tPoint;

//-------------------------------------------------------------------------------------------------

typedef enum {
  eFRONTEND_STATE_NONE = 0,
  eFRONTEND_STATE_TITLE,
  eFRONTEND_STATE_MAIN_MENU,
  eFRONTEND_STATE_CAR_SELECT,
  eFRONTEND_STATE_TRACK_SELECT,
  eFRONTEND_STATE_DISK_SELECT,
  eFRONTEND_STATE_PLAYERS_SELECT,
  eFRONTEND_STATE_TYPE_SELECT,
  eFRONTEND_STATE_DIFFICULTY,
  eFRONTEND_STATE_LOBBY,
  eFRONTEND_STATE_LOADING,
  eFRONTEND_STATE_COUNTDOWN,
  eFRONTEND_STATE_RACING,
  eFRONTEND_STATE_PAUSE_OVERLAY,
  eFRONTEND_STATE_RESULTS,
  eFRONTEND_STATE_WINNER_SCREEN,
  eFRONTEND_STATE_WINNER_RACE,
  eFRONTEND_STATE_RESULT_ROUNDUP,
  eFRONTEND_STATE_RACE_RESULT,
  eFRONTEND_STATE_CHAMPIONSHIP_STANDINGS,
  eFRONTEND_STATE_TEAM_STANDINGS,
  eFRONTEND_STATE_LAP_RECORDS,
  eFRONTEND_STATE_TIME_TRIAL_RESULTS,
  eFRONTEND_STATE_CHAMPIONSHIP_OVER,
  eFRONTEND_STATE_CREDITS,
  eFRONTEND_STATE_OPTIONS,
  eFRONTEND_STATE_SHUTDOWN,
  eFRONTEND_STATE_QUIT
} eFrontendState;

extern eFrontendState eFrontendCurrentState;
extern eFrontendState eFrontendNextState;

void frontend_set_state(eFrontendState eState);
void frontend_update(void);
void push_overlay(eFrontendState eOverlay);
void pop_overlay(void);
void race_set_track(int iTrack);
void race_enter(void);
void race_update(void);
void race_draw(void);
void race_exit(void);
void frontend_pause_enter(void);
void frontend_pause_update(void);
void frontend_pause_draw(void);
void frontend_pause_exit(void);
void frontend_menu_enter(void);
void frontend_menu_update(void);
void frontend_menu_resume_from_child(void);
void frontend_loading_enter(void);
void frontend_loading_update(void);
void frontend_title_enter(void);
void frontend_title_update(void);
void frontend_title_exit(void);
void frontend_results_update(void);
void frontend_winner_screen_enter(void);
void frontend_winner_screen_update(void);
void frontend_winner_screen_exit(void);
void frontend_winner_race_enter(void);
void frontend_winner_race_update(void);
void frontend_winner_race_exit(void);
void frontend_result_roundup_enter(void);
void frontend_result_roundup_update(void);
void frontend_result_roundup_exit(void);
void frontend_race_result_enter(void);
void frontend_race_result_update(void);
void frontend_race_result_exit(void);
void frontend_championship_standings_enter(void);
void frontend_championship_standings_update(void);
void frontend_championship_standings_exit(void);
void frontend_team_standings_enter(void);
void frontend_team_standings_update(void);
void frontend_team_standings_exit(void);
void frontend_lap_records_enter(void);
void frontend_lap_records_update(void);
void frontend_lap_records_exit(void);
void frontend_time_trial_results_enter(void);
void frontend_time_trial_results_update(void);
void frontend_time_trial_results_exit(void);
void frontend_championship_over_enter(void);
void frontend_championship_over_update(void);
void frontend_championship_over_draw(void);
void frontend_championship_over_exit(void);
void frontend_credits_enter(void);
void frontend_credits_update(void);
void frontend_credits_exit(void);
void frontend_title_screen_enter(void);
int frontend_title_screen_update(void);
void frontend_title_screen_exit(void);
void frontend_car_select_enter(void);
void frontend_car_select_update(void);
void frontend_car_select_exit(void);
void frontend_track_select_enter(void);
void frontend_track_select_update(void);
void frontend_track_select_exit(void);
void frontend_disk_select_enter(void);
void frontend_disk_select_update(void);
void frontend_disk_select_exit(void);
void frontend_config_enter(void);
void frontend_config_update(void);
void frontend_config_exit(void);
void frontend_players_select_enter(void);
void frontend_players_select_update(void);
void frontend_players_select_exit(void);
void frontend_type_select_enter(void);
void frontend_type_select_update(void);
void frontend_type_select_exit(void);
void frontend_lobby_enter(void);
void frontend_lobby_update(void);
void frontend_lobby_exit(void);
void frontend_main_menu_prepare_race_start(void);
void frontend_shutdown_enter(void);
void frontend_shutdown_update(void);
int frontend_shutdown_complete(void);

//-------------------------------------------------------------------------------------------------

extern int false_starts;
extern float TrackZs[25];
extern int death_race;
extern int head_x;
extern int head_y;
extern char network_messages[5][14];
extern int competitors;
extern int manual_control[16];
extern int smallcars[2][16];
extern int infinite_laps;
extern int Players_Cars[16];
extern int points[16];
extern int player_type;
extern int cup_won;
extern int game_type;
extern int car_pies[112];
extern int track_laps[25][6];
extern char race_posn[16][5];
extern int Selected_Drives[9];
extern int Selected_Play[9];
extern int DuoViews[9];
extern int AllowedViews[9];
extern int ReplayViews[9];
extern int replay_record;
extern int last_replay;
extern int last_type;
extern int SelectedView[2];
extern int network_champ_on;
extern void *font_vga;
extern void *title_vga;
extern tBlockHeader *front_vga[16];
extern int font1_offsets[104];
extern char font1_ascii[256];
extern int font2_offsets[96];
extern char font2_ascii[256];
extern int font3_offsets[72];
extern char font3_ascii[256];
extern int font4_offsets[80];
extern int font5_offsets[80];
extern char font4_ascii[256];
extern tPoint sel_posns[12];
extern int no_clear;
extern char *descript[8];
extern char comp_name[16][15];
extern int network_test;
extern char cheat_names[32][9];
extern char player_names[16][9];
extern int DeathView[2];
extern int teamorder[8];
extern int champorder[16];
extern int team_wins[16];
extern int human_control[16];
extern int total_wins[16];
extern int allocated_cars[14];
extern int team_kills[16];
extern int total_kills[16];
extern int team_points[8];
extern int championship_points[16];
extern int team_fasts[16];
extern int total_fasts[16];
extern int non_competitors[16];
extern int player_started[16];
extern int player_invul[16];
extern int p_texs_off;
extern int p_tex_size;
extern int quit_game;
extern int players;
extern int front_fade;
extern int frontend_on;
extern int cd_cheat;
extern int my_control;
extern int my_car;
extern int my_number;
extern int my_invul;
extern int FastestLap;
extern int racers;
extern uint32 cheat_mode;
extern int Race;
extern int switch_types;
extern int players_waiting;
extern int switch_sets;
extern int time_to_start;
extern int I_Quit;
extern int StartPressed;
extern int waste;
extern int switch_same;
extern int car_request;
extern uint8 oldkeys[14];
extern char my_name[14];
extern char szSelectEng[];
extern char szConfigEng[];
extern char font1_ascii_br[256];
extern char font2_ascii_br[256];
extern char font3_ascii_br[256];
extern char font4_ascii_br[256];

//-------------------------------------------------------------------------------------------------

void fade_redraw_bg(void *ctx);
void title_screens();
void copy_screens();
void snapshot_setup_frontend_menu_state(int iGameType);
void snapshot_render_menu_main(void);
void select_disk();
void snapshot_render_menu_select_disk(void);
void select_car();
void snapshot_render_menu_select_car(void);
void select_configure();
void snapshot_render_menu_configure(void);
void front_displaycalibrationbar(int iY, int iX, int iValue);
void front_volumebar(int iY, int iVolumeLevel, int iFillColor);
void select_players();
void snapshot_render_menu_select_players(void);
void select_type();
void snapshot_render_menu_select_type(void);
void select_track();
void snapshot_render_menu_select_track(void);
void save_params();
void reset_params();
void loadcheatnames();
int CheckNames(char *szPlayerName, int iPlayerIdx);

//-------------------------------------------------------------------------------------------------
#endif
