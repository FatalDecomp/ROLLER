#include "3d.h"
#include "game_render_hw.h"
#include "cdx.h"
#include "control.h"
#include "drawtrk3.h"
#include "loadtrak.h"
#include "moving.h"
#include "func2.h"
#include "func3.h"
#include "replay.h"
#include "sound.h"
#include "network.h"
#include "roller.h"
#include "rollersound.h"
#include "function.h"
#include "view.h"
#include "graphics.h"
#include "colision.h"
#include "horizon.h"
#include "building.h"
#include "rollercomms.h"
#include "crashdump.h"
#include "snapshot.h"
#include "snapshot_scenes.h"
#include "rollerinput.h"
#include "touch_ui.h"
#include "menu_render.h"
#include <SDL3/SDL.h>
#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif
#if defined(IS_ANDROID)
#include <SDL3/SDL_main.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>
#ifdef IS_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <direct.h>
#define chdir _chdir
#else
#include <unistd.h>
#endif

//-------------------------------------------------------------------------------------------------

#define ROLLER_PLAYER_NAME_BYTES 9
#define ROLLER_PLAYER_NAME_MAX_CHARS (ROLLER_PLAYER_NAME_BYTES - 1)
#define RACE_MOUSE_QUIT_PROMPT 300

//-------------------------------------------------------------------------------------------------

typedef enum {
  eFRONTEND_POST_RACE_NONE = 0,
  eFRONTEND_POST_RACE_SINGLE,
  eFRONTEND_POST_RACE_CHAMPIONSHIP,
  eFRONTEND_POST_RACE_TIME_TRIAL,
  eFRONTEND_POST_RACE_RECORDS,
  eFRONTEND_POST_RACE_STANDALONE_CHAMPIONSHIP
} eFrontendPostRaceFlow;

//-------------------------------------------------------------------------------------------------

float fPrevGameScale[2] = { 32768.0f, 32768.0f };
uint64 ullGameScaleTimeNs[2] = { 0, 0 };
float fcos;
float fsin;
static int iFrontendShutdownStarted = 0;
static int iFrontendShutdownWaitingForNetwork = 0;
static int iFrontendShutdownFinishing = 0;
static int iFrontendShutdownComplete = 0;
static int iWinnerRaceSavedRacers = 0;
static int iWinnerRaceSavedPlayerType = 0;
static int iWinnerRaceSavedNetworkOn = 0;
static int iWinnerRaceActive = 0;
static int iChampionRaceSavedRacers = 0;
static int iChampionRaceSavedPlayerType = 0;
static int iChampionRaceSavedTrackLoad = 0;
static int iChampionRaceActive = 0;
static int iFrontendGameFlags = 0;
static eFrontendPostRaceFlow eFrontendPostRaceCurrentFlow = eFRONTEND_POST_RACE_NONE;
static int iFrontendPostRaceCleanup = 0;
static int iFrontendTimeTrialCarIdx = 0;
static int iFrontendTimeTrialScreenActive = 0;
static int iFrontendNetworkErrorHandled = 0;
static int iFrontendRaceTrack = 0;
static int iFrontendRaceFadeOutPending = 0;

//-------------------------------------------------------------------------------------------------
//symbols defined by ROLLER
char szIngameEng[11] = "ingame.eng";                    //000A01FC
char szConfigEng_0[11] = "config.eng";                  //000A0208
char szCheatPal[13] = "cheatpal.pal";                   //000A0244
char szPal[12] = "palette.pal";                         //000A0254
char szF10ToQuitChamp[25] = "F10 TO QUIT CHAMPIONSHIP"; //000A029C
char szF10ToQuitGame[17] = "F10 TO QUIT GAME";          //000A02B8

//-------------------------------------------------------------------------------------------------

int champ_track[16] = { 1, 2, 3, 9, 5, 1, 7, 8, 1, 1, 1, 1, 1, 1, 1, 1 }; //000A3130
int exiting = 0;            //000A3170
int dontrestart = 0;        //000A3174
int champ_mode = 0;         //000A3178
int cd_error = 0;           //000A317C
int game_count[2] = { -2, -2 }; //000A3180
int lastblip[2] = { 0, 0 }; //000A3188
float game_scale[2] = { 32768.0f, 32768.0f }; //000A3190
int define_mode = 0;        //000A3198
int calibrate_mode = 0;     //000A319C
int graphic_mode = 0;       //000A31A0
int sound_edit = 0;         //000A31A8
int showversion = 0;        //000A31AC
int game_svga = -1;         //000A31B0 ROLLER modification - SVGA mode by default. Must be -1 (linear SVGA), not 1 (legacy mode-X / VGA-looking copyscreenmodex path)
int game_size = 128;        //000A31B4 ROLLER modification - full SVGA view size by default (64 is the VGA full size). Must match the SVGA-on default above so a freshly-saved default config renders the race at SVGA scale (scr_size = game_size)
int game_view[2] = { 0, 0 }; //000A31B8
int svga_possible = -1;     //000A31C0
int autoswitch = -1;        //000A31C4
int hibuffers = 0;          //000A32E0
int mem_used = 0;           //000A32E8
int mem_used_low = 0;       //000A32EC
int gosound = 3;            //000A3334
int current_mode = 666;     //000A333C
int names_on = 1;           //000A3340
tCarEngine *p_eng[2] = { NULL, NULL }; //000A3498
int messages = 0;           //000A34A8
int SVGA_ON = 0;            //000A34AC
int TrackLoad = 1;          //000A34B0
int paused = 0;             //000A34C4
int pause_request = 0;      //000A34C8
char alltrackflag = 0xFF;   //000A34E1
int wide_on = -1;           //000A350C
int network_on = 0;         //000A3510
char Banks_On = -1;         //000A3514
char Road_On = -1;          //000A3516
char Walls_On = -1;         //000A3517
char Play_View = 0;         //000A3518
int DriveView[2] = { 0, 0 }; //000A351C
int mirror = 0;             //000A3524
float TopViewHeight = 12288.0f; //000A3528
int start_time = 0;         //000A3534
uint8 *screen = NULL; //= 0xA0000; //000A3538
uint8 *scrbuf = NULL;       //000A353C
GameRenderer *g_pGameRenderer = NULL;
uint8 *mirbuf = NULL;       //000A3540
uint8 *texture_vga = NULL;   //000A3544
uint8 *building_vga = NULL;  //000A3548
uint8 *horizon_vga = NULL;   //000A354C
uint8 *cartex_vga[16] = { NULL }; //000A3550
uint8 *cargen_vga = NULL;    //000A3590
tBlockHeader *rev_vga[16] = { NULL }; //000A3594
int firstrun = -1;          //000A35D4
int lagdone = 0;            //000A35D8
int language = 0;           //000A4768
int GroundColour[MAX_TRACK_CHUNKS][5];//000B8C50
int TrakColour[MAX_TRACK_CHUNKS][6];//000BB360
int HorizonColour[MAX_TRACK_CHUNKS];     //000BE240
tData localdata[MAX_TRACK_CHUNKS];       //000BEA10
tGroundPt GroundPt[MAX_TRACK_CHUNKS];    //000CE410
float hor_scan[800];        //000D70B0
tGroundPt TrakPt[MAX_TRACK_CHUNKS];      //000D7D30
tTrackScreenXYZ GroundScreenXYZ[MAX_TRACK_CHUNKS]; //000E09D0
tTrackScreenXYZ TrackScreenXYZ[MAX_TRACK_CHUNKS]; //000F03D0
uint8 shade_palette[4096];  //000FFDD0
tColor palette[256];        //00100DD0
float tsin[16384];          //001010F0
float ptan[16384];          //001110F0
float tcos[16384];          //00121128
char buffer[128];           //00131228
uint8 blank_line[640];      //001312A8
int p_joyk1[2];             //0013E048
int p_joyk2[2];             //0013E050
tMemBlock mem_blocks[MEM_BLOCK_COUNT];  //0013E058
int zoom_size[2];           //0013E858
char zoom_mes[2][24];       //0013E860
int sub_on[2];              //0013E890
char zoom_sub[2][24];       //0013E898
int champ_go[16];           //0013E8C8
int game_overs;             //0013E908
int averagesectionlen;      //0013E90C
int racing;                 //0013E910
int totaltrackdistance;     //0013E914
int disable_messages;       //0013E918
int curr_time;              //0013E924
volatile int ticks;         //0013E92C
int frame_rate;             //0013E930
int frame_count;            //0013E934
float k1;                   //0013E938
float k2;                   //0013E93C
float k3;                   //0013E940
//0013E944
//0013E948
//0013E94C
float tatn[1025];           //0013E95C
uint32 textures_off;        //0013F960
int tex_count;              //0013F964
int vtilt;                  //0013F968
int worldtilt;              //0013F96C
float worldx;               //0013F970
float worldy;               //0013F974
float worldz;               //0013F978
int worldelev;              //0013F97C
int velevation;             //0013F980
int vdirection;             //0013F984
int scr_size;               //0013F988
int ybase;                  //0013F98C
int xbase;                  //0013F990
int winx;                   //0013F994
int winy;                   //0013F998
float ext_y;                //0013F99C
float ext_z;                //0013F9A0
float viewx;                //0013F9A4
float viewy;                //0013F9A8
float viewz;                //0013F9AC
int worlddirn;              //0013F9B0
char keys[140];             //0013F9B4
int oldmode;                //0013FA40
int clear_borders;          //0013FA44
float DDX;                  //0013FA48
float DDY;                  //0013FA4C
float DDZ;                  //0013FA50
float ext_x;                //0013FA54
int test_f1;                //0013FA58
int test_f2;                //0013FA5C
int print_data;             //0013FA68
int demo_control;           //0013FA6C
int tick_on;                //0013FA70
int old_mode;               //0013FA74
int demo_mode;              //0013FA78
int demo_count;             //0013FA7C
int start_race;             //0013FA80
int NoOfLaps;               //0013FA84
int human_finishers;        //0013FA88
int finishers;              //0013FA8C
int countdown;              //0013FA90
int screenready;            //0013FA94
int shown_panel;            //0013FA98
int start_cd;               //0013FA9C
int game_level;             //0013FAA0
int max_mem;                //0013FAA4
int game_req;               //0013FAA8
int game_dam;               //0013FAAC
int pausewindow;            //0013FAB0
int scrmode;                //0013FAB4
int control_select;         //0013FAB8
int req_size;               //0013FABC
int intro;                  //0013FAC0
int shifting;               //0013FAC4
int fadedin;                //0013FAC8
int control_edit = -1;      //0013FACC
int req_edit;               //0013FAD0
int controlrelease;         //0013FAD4
float subscale;             //0013FAD8
int fatal_ini_loaded;       //0013FADC
int machine_speed;          //0013FAE0
int netCD;                  //0013FAE4
int localCD;                //0013FAE8
int dead_humans;            //0013FAEC
int I_Want_Out;             //0013FAF0
int champ_car;              //0013FAF4
int champ_zoom;             //0013FAF8
int replay_player;          //0013FAFC
int team_mate;              //0013FB00
int winner_done;            //0013FB04
int winner_mode;            //0013FB08
int network_mes_mode;       //0013FB0C
int cdchecked;              //0013FB10
int network_slot;           //0013FB14
int trying_to_exit;         //0013FB18
int local_players;          //0013FB1C
int draw_type;              //0013FB20
int network_buggered;       //0013FB24
int champ_count;            //0013FB28
int replay_cheat;           //0013FB2C
int w95;                    //0013FB30
int gave_up;                //0013FB34
int champ_size;             //0013FB38
int send_finished;          //0013FB40
int game_frame;             //0013FB44
int warp_angle;             //0013FB4C
int game_track;             //0013FB50
int prev_track;             //0013FB54
int view0_cnt;              //0013FB58
int view1_cnt;              //0013FB5C
int I_Would_Like_To_Quit;   //0013FB60
int Quit_Count;             //0013FB64
int winh;                   //0013FB68
int winw;                   //0013FB6C
int VIEWDIST;               //0013FB70
int YMAX;                   //0013FB74
int XMAX;                   //0013FB78
int time_shown;             //0013FB7C
int player2_car;            //0013FB7E
int player1_car;            //0013FB80

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

void set_game_scale(int iPlayerIdx, float fNew)
{
  uint64 ullScaleTimeNs;

  fPrevGameScale[iPlayerIdx] = game_scale[iPlayerIdx];
  game_scale[iPlayerIdx] = fNew;

  ullScaleTimeNs = ullLastTickTimeNs;
  if (!ullScaleTimeNs)
    ullScaleTimeNs = SDL_GetTicksNS();
  ullGameScaleTimeNs[iPlayerIdx] = ullScaleTimeNs;
}

//-------------------------------------------------------------------------------------------------

static void set_black_palette_now(void)
{
  if (!pal_addr)
    return;

  blankpal();
  g_bPaletteSet = true;
  UpdateSDLWindow();
}

//-------------------------------------------------------------------------------------------------

static void frontend_shutdown_begin(void)
{
  if (iFrontendShutdownStarted)
    return;

  iFrontendShutdownStarted = -1;
  exiting = -1;
  quit_game = -1;
  if (network_on) {
    tick_on = -1;
    frontend_on = -1;
    network_broadcast_wait_start(-666, 1);
    iFrontendShutdownWaitingForNetwork = -1;
  }
}

//-------------------------------------------------------------------------------------------------

static void frontend_shutdown_finish(void)
{
  if (iFrontendShutdownComplete)
    return;
  if (iFrontendShutdownFinishing) {
    iFrontendShutdownComplete = -1;
    return;
  }

  iFrontendShutdownFinishing = -1;
  close_network();
  if (!g_bSnapshotMode) SaveRecords();
  fre((void**)&mirbuf);
  for (int i = 0; i < 16; ++i) {
    fre((void**)&rev_vga[i]);
    fre((void**)&cartex_vga[i]);
    fre((void**)&front_vga[i]);
  }
  fre((void**)&font_vga);
  fre((void**)&title_vga);
  fre((void**)&cargen_vga);
  fre((void**)&texture_vga);
  fre((void**)&building_vga);
  fre((void**)&scrbuf);
  release_key_int();
  Uninitialise_SOS();
  releasesamples();
  if (MusicCD)
    cdxdone();
  ROLLERremove("../REPLAYS/REPLAY.TMP");
  if ((cheat_mode & CHEAT_MODE_WIDESCREEN) != 0)
    textures_off |= TEX_OFF_WIDESCREEN;
  else
    textures_off &= ~TEX_OFF_WIDESCREEN;
  if (false_starts)
    textures_off |= TEX_OFF_CAR_TEXTURES;
  else
    textures_off &= ~TEX_OFF_CAR_TEXTURES;
  if (!intro)
    save_fatal_config();

  //added by ROLLER
  ShutdownSDL();

  //clear keyboard buffer
  //while (kbhit(iSuccess))
  //  iSuccess = getch();
  chdir("..");

  iFrontendShutdownComplete = -1;
  iFrontendShutdownFinishing = 0;
}

//-------------------------------------------------------------------------------------------------

static int frontend_shutdown_pump(void)
{
  frontend_shutdown_begin();

  if (iFrontendShutdownWaitingForNetwork) {
    if (!network_broadcast_wait_update())
      return 0;
    iFrontendShutdownWaitingForNetwork = 0;
  }

  frontend_shutdown_finish();
  return -1;
}

//-------------------------------------------------------------------------------------------------

void frontend_shutdown_enter(void)
{
  frontend_shutdown_begin();
}

//-------------------------------------------------------------------------------------------------

void frontend_shutdown_request(void)
{
  frontend_shutdown_begin();
  eFrontendNextState = iFrontendShutdownComplete ? eFRONTEND_STATE_QUIT : eFRONTEND_STATE_SHUTDOWN;
}

//-------------------------------------------------------------------------------------------------

void frontend_shutdown_update(void)
{
  if (frontend_shutdown_pump())
    eFrontendNextState = eFRONTEND_STATE_QUIT;
}

//-------------------------------------------------------------------------------------------------

int frontend_shutdown_complete(void)
{
  return iFrontendShutdownComplete;
}

//-------------------------------------------------------------------------------------------------

static int find_free_mem_block(void)
{
  for (int i = 0; i < MEM_BLOCK_COUNT; ++i) {
    if (!mem_blocks[i].pBuf)
      return i;
  }

  return -1;
}

//-------------------------------------------------------------------------------------------------

static int find_mem_block(void *pData)
{
  for (int i = 0; i < MEM_BLOCK_COUNT; ++i) {
    if (mem_blocks[i].pBuf == pData)
      return i;
  }

  return -1;
}

//-------------------------------------------------------------------------------------------------

static void LogMemBlocks(const char *pszReason, uint32 uiRequestedSize)
{
  FILE *pFile;
  int iActiveCount = 0;
  uint32 uiTrackedSize = 0;

  pFile = fopen("roller-mem-blocks.log", "a");
  if (!pFile)
    return;

  for (int i = 0; i < MEM_BLOCK_COUNT; ++i) {
    if (mem_blocks[i].pBuf) {
      ++iActiveCount;
      uiTrackedSize += mem_blocks[i].uiSize;
    }
  }

  fprintf(pFile, "ROLLER memory block tracker exhaustion\n");
  fprintf(pFile, "Reason: %s\n", pszReason);
  fprintf(pFile, "Requested size: %u\n", uiRequestedSize);
  fprintf(pFile, "Active slots: %d/%d\n", iActiveCount, MEM_BLOCK_COUNT);
  fprintf(pFile, "Tracked bytes: %u\n", uiTrackedSize);
  fprintf(pFile, "mem_used: %d\n", mem_used);
  fprintf(pFile, "mem_used_low: %d\n", mem_used_low);
  fprintf(pFile, "hibuffers: %d\n", hibuffers);
  fprintf(pFile, "\n");
  fprintf(pFile, "slot,pBuf,uiSize,pAlsoBuf,iRegsDi\n");

  for (int i = 0; i < MEM_BLOCK_COUNT; ++i) {
    if (mem_blocks[i].pBuf) {
      fprintf(pFile, "%d,%p,%u,%p,%d\n",
              i,
              mem_blocks[i].pBuf,
              mem_blocks[i].uiSize,
              mem_blocks[i].pAlsoBuf,
              mem_blocks[i].iRegsDi);
    }
  }

  fprintf(pFile, "\n");
  fclose(pFile);
}

//-------------------------------------------------------------------------------------------------

static void cli_prepare_console(void)
{
#ifdef IS_WINDOWS
  static int iPrepared = 0;
  if (iPrepared)
    return;
  iPrepared = 1;

  HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
  HANDLE hStderr = GetStdHandle(STD_ERROR_HANDLE);
  int iStdoutValid = hStdout != NULL && hStdout != INVALID_HANDLE_VALUE &&
                     GetFileType(hStdout) != FILE_TYPE_UNKNOWN;
  int iStderrValid = hStderr != NULL && hStderr != INVALID_HANDLE_VALUE &&
                     GetFileType(hStderr) != FILE_TYPE_UNKNOWN;
  if (!iStdoutValid && !iStderrValid)
    AttachConsole(ATTACH_PARENT_PROCESS);
#endif
}

//-------------------------------------------------------------------------------------------------

static void cli_vfprintf(FILE *f, const char *fmt, va_list args)
{
#ifdef IS_WINDOWS
  char szMessage[4096];
  int iLen = vsnprintf(szMessage, sizeof(szMessage), fmt, args);
  if (iLen < 0)
    return;
  if (iLen >= (int)sizeof(szMessage))
    iLen = (int)sizeof(szMessage) - 1;

  cli_prepare_console();
  HANDLE hOut = GetStdHandle(f == stderr ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
  if (hOut != NULL && hOut != INVALID_HANDLE_VALUE && GetFileType(hOut) != FILE_TYPE_UNKNOWN) {
    DWORD dwWritten = 0;
    WriteFile(hOut, szMessage, (DWORD)iLen, &dwWritten, NULL);
    return;
  }
#endif
  vfprintf(f, fmt, args);
  fflush(f);
}

//-------------------------------------------------------------------------------------------------

static void cli_fprintf(FILE *f, const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  cli_vfprintf(f, fmt, args);
  va_end(args);
}

//-------------------------------------------------------------------------------------------------

static void print_usage(FILE *f, const char *argv0)
{
  cli_fprintf(f, "usage: %s [options]\n\n", argv0);
  cli_fprintf(f, "options:\n");
  cli_fprintf(f, " -h, --help             show this help message and exit\n");
  cli_fprintf(f, " --whiplash-root DIR    specify Whiplash data directory\n");
  cli_fprintf(f, " --midi-root DIR        specify midi data directory\n");
  cli_fprintf(f, " --player1name NAME     set player 1 name (letters, digits, spaces; max 8 chars)\n");
  cli_fprintf(f, " --local-ip IP          local IPv4 address to advertise for multiplayer\n");
  cli_fprintf(f, " --port N               UDP port to bind (default: %d)\n", ROLLER_DEFAULT_PORT);
  cli_fprintf(f, " --peer IP:PORT         pre-configure a peer for direct connection\n");
  cli_fprintf(f, " --net-slot N           network slot index; use -1 to join as client\n");
  cli_fprintf(f, " --no-crash-handler     disable crash dump generation for this run\n");
  cli_fprintf(f, " --snapshot REPLAY      headless replay-capture mode (writes indexed PNGs)\n");
  cli_fprintf(f, " --snapshot-scene NAME render a headless named scene snapshot\n");
  cli_fprintf(f, " --frames N[,M,...]     replay-frame indices to capture (--snapshot only)\n");
  cli_fprintf(f, " --out DIR              output directory for snapshot PNGs (--snapshot only)\n");
}

//-------------------------------------------------------------------------------------------------

static void frontend_run_game_loop(eFrontendState eInitialState);

//-------------------------------------------------------------------------------------------------

static int cli_player_name_copy(char *szDest, const char *szSrc)
{
  int iDst = 0;
  int iHasVisibleChar = 0;

  memset(szDest, 0, ROLLER_PLAYER_NAME_BYTES);
  for (const char *p = szSrc; *p && iDst < ROLLER_PLAYER_NAME_MAX_CHARS; ++p) {
    char c = *p;
    if (c >= 'a' && c <= 'z')
      c = (char)(c - 'a' + 'A');

    if (c == ' ' || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
      szDest[iDst++] = c;
      if (c != ' ')
        iHasVisibleChar = 1;
    }
  }

  return iHasVisibleChar;
}

//-------------------------------------------------------------------------------------------------

static void player_name_copy_bytes(char *szDest, const char *szSrc)
{
  memcpy(szDest, szSrc, ROLLER_PLAYER_NAME_BYTES);
}

//-------------------------------------------------------------------------------------------------

static void winner_race_setup(void)
{
  //int iArrayIdx; // edx
  int iWinnerCarIdx; // edx

  for (int i = 0; i < numcars; ++i) {
    grid[i] = i;
    non_competitors[i] = -1;
  }
  //if (numcars > 0) {
  //  iArrayIdx = 0;
  //  do {
  //    finished_car[++iArrayIdx + 15] = iCarIdx++;
  //    TrackArrow_variable_1[iArrayIdx] = -1;
  //  } while (iCarIdx < iNumCars);
  //}

  iWinnerCarIdx = carorder[0];
  winner_mode = -1;
  winner_done = 0;
  ViewType[0] = carorder[0];
  if (prev_track >= 17)
    SelectedView[0] = 1;
  else
    SelectedView[0] = 4;
  grid[0] = carorder[0];
  champ_zoom = 0;
  non_competitors[carorder[0]] = 0;
  human_control[iWinnerCarIdx] = 0;
  grid[iWinnerCarIdx] = 0;
  start_race = 0;
  countdown = -36;
  gosound = 3;
  delayread = 0;
  delaywrite = 6;
  writeptr = 0;
  readptr = 0;
  iWinnerRaceSavedRacers = racers;
  iWinnerRaceSavedPlayerType = player_type;
  iWinnerRaceSavedNetworkOn = network_on;
  // Winner race is a local one-car showcase even after a network race.
  network_on = 0;
  player_type = 0;
  replaytype = 0;
  racers = 1;
  iWinnerRaceActive = -1;
}

//-------------------------------------------------------------------------------------------------

static void winner_race_teardown(void)
{
  int iNumCars; // ecx
  //int iMaxOffset; // ebx
  //unsigned int uiOffset; // eax

  if (!iWinnerRaceActive)
    return;

  iNumCars = numcars;
  winner_mode = 0;
  racers = iWinnerRaceSavedRacers;
  player_type = iWinnerRaceSavedPlayerType;
  network_on = iWinnerRaceSavedNetworkOn;
  VIEWDIST = 270;

  for (int i = 0; i < numcars; ++i) {
    non_competitors[i] = result_competing[i];
  }
  //if (numcars > 0) {
  //  iMaxOffset = 4 * numcars;
  //  uiOffset = 0;
  //  do {
  //    uiOffset += 4;
  //    TrackArrow_variable_1[uiOffset / 4] = LODWORD(result_best[uiOffset / 4 + 15]);
  //  } while ((int)uiOffset < iMaxOffset);
  //}

  numcars = iNumCars;
  iWinnerRaceActive = 0;
}

//-------------------------------------------------------------------------------------------------

static int champion_race_setup(void)
{
  //int iMaxOffset_1; // ebx
  //unsigned int iOffset_1; // eax
  int iRacer; // eax
  int iNumRacers; // ecx
  int iGridIdx; // ebx
  int iCarIdx; // edx
  int iRacers; // esi
  int iCarIdx_1; // edx
  int iOffset_2; // ecx
  int iMaxOffset_2; // edi
  int j; // eax
  int iDriver; // edx
  int iTrack; // [esp+4h] [ebp-1Ch]

  iChampionRaceSavedTrackLoad = TrackLoad;
  champ_size = scr_size;
  if (game_type != 1 && numcars > 0) {

    for (int i = 0; i < numcars; ++i) {
      champorder[i] = result_order[i];
    }
    //iMaxOffset_1 = 4 * numcars;
    //iOffset_1 = 0;
    //do {
    //  iOffset_1 += 4;
    //  teamorder[iOffset_1 / 4 + 7] = result_lap[iOffset_1 / 4 + 15];// references adjacent data see above fixed loop
    //} while ((int)iOffset_1 < iMaxOffset_1);
  }

  iTrack = champ_track[champorder[0] / 2];
  iRacer = 0;
  if (racers > 0) {
    iNumRacers = racers;
    iGridIdx = 0;
    do {
      ++iGridIdx;
      iCarIdx = champorder[iNumRacers - 1 - iRacer++];
      grid[iGridIdx - 1] = iCarIdx;
      non_competitors[iCarIdx] = 0;
      human_control[iCarIdx] = 0;
    } while (iRacer < iNumRacers);
  }

  iRacers = racers;
  iCarIdx_1 = 0;
  if (racers < numcars) {
    iOffset_2 = 4 * racers;
    iMaxOffset_2 = 4 * numcars;
    do {
      for (j = iCarIdx_1; !result_competing[j]; ++j)
        ++iCarIdx_1;
      ++iRacers;
      grid[iOffset_2 / 4u] = iCarIdx_1;
      iOffset_2 += 4;
      human_control[j] = 0;
      non_competitors[j] = -1;
      ++iCarIdx_1;
    } while (iOffset_2 < iMaxOffset_2);
  }

  iChampionRaceSavedRacers = racers;
  winner_mode = -1;
  winner_done = 0;
  champ_mode = -1;
  champ_zoom = 0;
  SelectedView[0] = 8;

  iDriver = champorder[racers - 1];
  //iDriver = teamorder[racers + 7];

  champ_car = racers - 1;
  ViewType[0] = iDriver;
  champ_go[iDriver] = -1;
  start_race = 0;
  countdown = -36;
  gosound = 3;
  delayread = 0;
  delaywrite = 6;
  writeptr = 0;
  readptr = 0;
  iChampionRaceSavedPlayerType = player_type;
  replaytype = 0;
  player_type = 0;
  iChampionRaceActive = -1;
  return iTrack;
}

//-------------------------------------------------------------------------------------------------

static void champion_race_teardown(void)
{
  //unsigned int iOffset; // eax
  //int iMaxOffset; // ebx

  if (!iChampionRaceActive)
    return;

  VIEWDIST = 270;
  winner_mode = 0;
  champ_mode = 0;
  racers = iChampionRaceSavedRacers;
  player_type = iChampionRaceSavedPlayerType;
  TrackLoad = iChampionRaceSavedTrackLoad;

  for (int i = 0; i < numcars; ++i) {
    non_competitors[i] = result_competing[i];
  }
  //if (numcars > 0) {
  //  iOffset = 0;
  //  iMaxOffset = 4 * numcars;
  //  do {
  //    iOffset += 4;
  //    TrackArrow_variable_1[iOffset / 4] = LODWORD(result_best[iOffset / 4 + 15]);// references adjacent data see above fixed loop
  //  } while ((int)iOffset < iMaxOffset);
  //}

  scr_size = champ_size;
  iChampionRaceActive = 0;
}

//-------------------------------------------------------------------------------------------------

void champion_race_enter(void)
{
  int iTrack = champion_race_setup();
  race_set_track(iTrack);
  race_enter();
}

//-------------------------------------------------------------------------------------------------

int champion_race_update(void)
{
  race_update();
  if (eFrontendNextState == eFRONTEND_STATE_RESULTS) {
    eFrontendNextState = eFRONTEND_STATE_CHAMPIONSHIP_OVER;
    return -1;
  }
  return 0;
}

//-------------------------------------------------------------------------------------------------

void champion_race_draw(void)
{
  race_draw();
}

//-------------------------------------------------------------------------------------------------

void champion_race_exit(void)
{
  race_exit();
  champion_race_teardown();
}

//-------------------------------------------------------------------------------------------------

static int frontend_should_show_winner_screen(void)
{
  return human_control[carorder[0]] ||
         (cheat_mode & CHEAT_MODE_RACE_HISTORY) != 0;
}

//-------------------------------------------------------------------------------------------------

static int frontend_should_show_team_standings(void)
{
  return competitors > 8 && (cheat_mode & CHEAT_MODE_CLONES) == 0;
}

//-------------------------------------------------------------------------------------------------

static void frontend_finish_post_race_sequence(void)
{
  if (iFrontendPostRaceCleanup && player_type == 1)
    player_type = 0;

  if (eFrontendPostRaceCurrentFlow != eFRONTEND_POST_RACE_RECORDS &&
      eFrontendPostRaceCurrentFlow != eFRONTEND_POST_RACE_STANDALONE_CHAMPIONSHIP)
    intro = 0;

  eFrontendPostRaceCurrentFlow = eFRONTEND_POST_RACE_NONE;
  iFrontendPostRaceCleanup = 0;
  iFrontendTimeTrialCarIdx = 0;
  iFrontendTimeTrialScreenActive = 0;
  iFrontendTimeTrialScreenActive = 0;
  eFrontendNextState = quit_game ? eFRONTEND_STATE_QUIT : eFRONTEND_STATE_MAIN_MENU;
}

//-------------------------------------------------------------------------------------------------

static void frontend_after_single_result_screens(void)
{
  if ((cheat_mode & CHEAT_MODE_END_SEQUENCE) != 0) {
    eFrontendNextState = eFRONTEND_STATE_CHAMPIONSHIP_OVER;
  } else if ((cheat_mode & CHEAT_MODE_CREDITS) != 0) {
    eFrontendNextState = eFRONTEND_STATE_CREDITS;
  } else {
    frontend_finish_post_race_sequence();
  }
}

//-------------------------------------------------------------------------------------------------

static void frontend_after_championship_lap_records(void)
{
  ++Race;
  TrackLoad = prev_track + 1;
  if (Race == 8 || (cheat_mode & CHEAT_MODE_END_SEQUENCE) != 0) {
    eFrontendNextState = eFRONTEND_STATE_CHAMPIONSHIP_OVER;
  } else if ((cheat_mode & CHEAT_MODE_CREDITS) != 0) {
    eFrontendNextState = eFRONTEND_STATE_CREDITS;
  } else {
    frontend_finish_post_race_sequence();
  }
}

//-------------------------------------------------------------------------------------------------

static void frontend_run_game_loop(eFrontendState eInitialState);

//-------------------------------------------------------------------------------------------------

int main_loop_iteration(void)
{
  if (quit_game &&
      eFrontendCurrentState != eFRONTEND_STATE_SHUTDOWN &&
      eFrontendCurrentState != eFRONTEND_STATE_QUIT)
    eFrontendNextState = eFRONTEND_STATE_SHUTDOWN;

  if (eFrontendCurrentState == eFRONTEND_STATE_QUIT &&
      eFrontendNextState == eFRONTEND_STATE_QUIT)
    return 0;

  UpdateSDL();

#if defined(IS_WASM)
  /* Browser timer callbacks lose cadence under main-thread load. Derive the
   * game clock from elapsed monotonic time after pumping visibility/freeze
   * state, then let the existing frontend path drain at most four ticks. */
  wasm_tick_clock_update();
#endif

  if (quit_game &&
      eFrontendCurrentState != eFRONTEND_STATE_SHUTDOWN &&
      eFrontendCurrentState != eFRONTEND_STATE_QUIT)
    eFrontendNextState = eFRONTEND_STATE_SHUTDOWN;

  frontend_update();
  return eFrontendCurrentState != eFRONTEND_STATE_QUIT;
}

//-------------------------------------------------------------------------------------------------

#if defined(__EMSCRIPTEN__)
static void main_loop_iteration_wrapper(void)
{
  if (!main_loop_iteration())
    emscripten_cancel_main_loop();
}
#endif

//-------------------------------------------------------------------------------------------------

static void frontend_run_game_loop(eFrontendState eInitialState)
{
  VIEWDIST = 270;
  frontend_set_state(eInitialState);

#if defined(__EMSCRIPTEN__)
  // The shutdown frontend state performs all cleanup, including ShutdownSDL. With
  // simulated infinite-loop semantics, execution after this call is unreachable.
  emscripten_set_main_loop(main_loop_iteration_wrapper, 0, 1);
#else
  while (main_loop_iteration()) {
  }
#endif
}

//-------------------------------------------------------------------------------------------------

void frontend_copy_screens_enter(void)
{
  CopyScreensEnter();
}

//-------------------------------------------------------------------------------------------------

void frontend_copy_screens_update(void)
{
  if (CopyScreensUpdate())
    eFrontendNextState = eFRONTEND_STATE_TITLE;
}

//-------------------------------------------------------------------------------------------------

void frontend_copy_screens_exit(void)
{
  CopyScreensExit();
}

//-------------------------------------------------------------------------------------------------

void frontend_loading_enter(void)
{
  countdown = 144;
  gosound = 3;
  delayread = 0;
  delaywrite = 6;
  writeptr = 0;
  readptr = 0;
}

//-------------------------------------------------------------------------------------------------

void frontend_loading_update(void)
{
  if (quit_game) {
    eFrontendNextState = eFRONTEND_STATE_QUIT;
  } else if (game_type < 3) {
    eFrontendNextState = eFRONTEND_STATE_TITLE;
  } else if (game_type == 3) {
    eFrontendPostRaceCurrentFlow = eFRONTEND_POST_RACE_STANDALONE_CHAMPIONSHIP;
    iFrontendPostRaceCleanup = 0;
    eFrontendNextState = eFRONTEND_STATE_CHAMPIONSHIP_STANDINGS;
  } else {
    eFrontendNextState = eFRONTEND_STATE_RESULTS;
  }
}

//-------------------------------------------------------------------------------------------------

void frontend_title_enter(void)
{
  frontend_title_screen_enter();
}

//-------------------------------------------------------------------------------------------------

void frontend_title_update(void)
{
  if (!frontend_title_screen_update())
    return;

  iFrontendGameFlags = -1;
  if (replaytype == 2)
    iFrontendGameFlags = 0;
  if (intro)
    iFrontendGameFlags = 0;
  net_quit = 0;
  prev_track = TrackLoad;
  race_set_track(TrackLoad);
  eFrontendNextState = eFRONTEND_STATE_RACING;
}

//-------------------------------------------------------------------------------------------------

void frontend_title_exit(void)
{
  frontend_title_screen_exit();
}

//-------------------------------------------------------------------------------------------------

void frontend_results_update(void)
{
  eFrontendPostRaceCurrentFlow = eFRONTEND_POST_RACE_NONE;
  iFrontendPostRaceCleanup = 0;
  iFrontendTimeTrialCarIdx = 0;

  if (game_type >= 3) {
    eFrontendPostRaceCurrentFlow = eFRONTEND_POST_RACE_RECORDS;
    eFrontendNextState = eFRONTEND_STATE_LAP_RECORDS;
    return;
  }

  if (!network_buggered)
    iFrontendNetworkErrorHandled = 0;

  if (network_buggered && !iFrontendNetworkErrorHandled) {
    iFrontendGameFlags = 0;
    iFrontendNetworkErrorHandled = -1;
    eFrontendNextState = eFRONTEND_STATE_NETWORK_ERROR;
    return;
  }
  if (net_quit || network_buggered) {
    iFrontendGameFlags = 0;
    if (network_champ_on && (network_buggered != 666 || !restart_net)) {
      game_type = 0;
      network_champ_on = 0;
    }
    if (net_quit)
      close_network();
  }
  if (cd_error) {
    iFrontendGameFlags = 0;
    eFrontendNextState = eFRONTEND_STATE_NO_CD_ERROR;
    return;
  }
  VIEWDIST = 270;

  if (!quit_game && iFrontendGameFlags) {
    iFrontendPostRaceCleanup = -1;
    if (game_type) {
      if ((unsigned int)game_type > 1) {        // Time trial mode - special handling
        if (game_type == 2) {
          StoreResult();
          eFrontendPostRaceCurrentFlow = eFRONTEND_POST_RACE_TIME_TRIAL;
          eFrontendNextState = eFRONTEND_STATE_TIME_TRIAL_RESULTS;
          return;
        }
        frontend_finish_post_race_sequence();
        return;
      }
      eFrontendPostRaceCurrentFlow = eFRONTEND_POST_RACE_CHAMPIONSHIP;
      finish_race();                            // Championship mode - handle race completion
      StoreResult();
      eFrontendNextState = frontend_should_show_winner_screen()
        ? eFRONTEND_STATE_WINNER_SCREEN
        : eFRONTEND_STATE_RESULT_ROUNDUP;
      return;
    }
    eFrontendPostRaceCurrentFlow = eFRONTEND_POST_RACE_SINGLE;
    if (!gave_up) {
      finish_race();
      StoreResult();
      eFrontendNextState = frontend_should_show_winner_screen()
        ? eFRONTEND_STATE_WINNER_SCREEN
        : eFRONTEND_STATE_RESULT_ROUNDUP;
      return;
    }
    frontend_after_single_result_screens();
    return;
  }

  frontend_finish_post_race_sequence();
}

//-------------------------------------------------------------------------------------------------

void frontend_network_error_enter(void)
{
  NetworkFuckedEnter();
}

//-------------------------------------------------------------------------------------------------

void frontend_network_error_update(void)
{
  if (NetworkFuckedUpdate())
    eFrontendNextState = eFRONTEND_STATE_RESULTS;
}

//-------------------------------------------------------------------------------------------------

void frontend_network_error_exit(void)
{
  NetworkFuckedExit();
}

//-------------------------------------------------------------------------------------------------

void frontend_no_cd_enter(void)
{
  NoCdEnter();
}

//-------------------------------------------------------------------------------------------------

void frontend_no_cd_update(void)
{
  if (NoCdUpdate()) {
    quit_game = -1;
    eFrontendNextState = eFRONTEND_STATE_SHUTDOWN;
  }
}

//-------------------------------------------------------------------------------------------------

void frontend_no_cd_exit(void)
{
  NoCdExit();
}

//-------------------------------------------------------------------------------------------------

void frontend_winner_screen_update(void)
{
  if (!WinnerScreenUpdate())
    return;
  if (WinnerScreenResult())
    eFrontendNextState = eFRONTEND_STATE_WINNER_RACE;
  else
    eFrontendNextState = eFRONTEND_STATE_RESULT_ROUNDUP;
}

//-------------------------------------------------------------------------------------------------

void frontend_winner_screen_enter(void)
{
  WinnerScreenEnter((int)Car[carorder[0]].byCarDesignIdx, carorder[0] & 1);
}

//-------------------------------------------------------------------------------------------------

void frontend_winner_screen_exit(void)
{
  WinnerScreenExit();
}

//-------------------------------------------------------------------------------------------------

void frontend_winner_race_enter(void)
{
  winner_race_setup();
  race_set_track(prev_track);
  race_enter();
}

//-------------------------------------------------------------------------------------------------

void frontend_winner_race_update(void)
{
  race_update();
  if (eFrontendNextState == eFRONTEND_STATE_RESULTS)
    eFrontendNextState = eFRONTEND_STATE_RESULT_ROUNDUP;
}

//-------------------------------------------------------------------------------------------------

void frontend_winner_race_exit(void)
{
  race_exit();
  winner_race_teardown();
}

//-------------------------------------------------------------------------------------------------

void frontend_result_roundup_enter(void)
{
  ResultRoundUpEnter();
}

//-------------------------------------------------------------------------------------------------

void frontend_result_roundup_update(void)
{
  if (!ResultRoundUpUpdate())
    return;
  eFrontendNextState = eFRONTEND_STATE_RACE_RESULT;
}

//-------------------------------------------------------------------------------------------------

void frontend_result_roundup_exit(void)
{
  ResultRoundUpExit();
}

//-------------------------------------------------------------------------------------------------

void frontend_race_result_enter(void)
{
  RaceResultEnter();
}

//-------------------------------------------------------------------------------------------------

void frontend_race_result_update(void)
{
  if (!RaceResultUpdate())
    return;
  if (eFrontendPostRaceCurrentFlow == eFRONTEND_POST_RACE_CHAMPIONSHIP)
    eFrontendNextState = eFRONTEND_STATE_CHAMPIONSHIP_STANDINGS;
  else if (eFrontendPostRaceCurrentFlow == eFRONTEND_POST_RACE_SINGLE)
    eFrontendNextState = eFRONTEND_STATE_LAP_RECORDS;
  else
    frontend_finish_post_race_sequence();
}

//-------------------------------------------------------------------------------------------------

void frontend_race_result_exit(void)
{
  RaceResultExit();
}

//-------------------------------------------------------------------------------------------------

void frontend_championship_standings_update(void)
{
  if (!ChampionshipStandingsUpdate())
    return;
  if (frontend_should_show_team_standings()) {
    eFrontendNextState = eFRONTEND_STATE_TEAM_STANDINGS;
  } else if (eFrontendPostRaceCurrentFlow == eFRONTEND_POST_RACE_CHAMPIONSHIP) {
    eFrontendNextState = eFRONTEND_STATE_LAP_RECORDS;
  } else {
    dontrestart = -1;
    frontend_finish_post_race_sequence();
  }
}

//-------------------------------------------------------------------------------------------------

void frontend_championship_standings_enter(void)
{
  ChampionshipStandingsEnter();
}

//-------------------------------------------------------------------------------------------------

void frontend_championship_standings_exit(void)
{
  ChampionshipStandingsExit();
}

//-------------------------------------------------------------------------------------------------

void frontend_team_standings_update(void)
{
  if (!TeamStandingsUpdate())
    return;
  if (eFrontendPostRaceCurrentFlow == eFRONTEND_POST_RACE_CHAMPIONSHIP) {
    eFrontendNextState = eFRONTEND_STATE_LAP_RECORDS;
  } else {
    dontrestart = -1;
    frontend_finish_post_race_sequence();
  }
}

//-------------------------------------------------------------------------------------------------

void frontend_team_standings_enter(void)
{
  TeamStandingsEnter();
}

//-------------------------------------------------------------------------------------------------

void frontend_team_standings_exit(void)
{
  TeamStandingsExit();
}

//-------------------------------------------------------------------------------------------------

void frontend_lap_records_update(void)
{
  if (!ShowLapRecordsUpdate())
    return;
  switch (eFrontendPostRaceCurrentFlow) {
    case eFRONTEND_POST_RACE_CHAMPIONSHIP:
      frontend_after_championship_lap_records();
      break;
    case eFRONTEND_POST_RACE_SINGLE:
      frontend_after_single_result_screens();
      break;
    case eFRONTEND_POST_RACE_RECORDS:
      dontrestart = -1;
      frontend_finish_post_race_sequence();
      break;
    default:
      frontend_finish_post_race_sequence();
      break;
  }
}

//-------------------------------------------------------------------------------------------------

void frontend_lap_records_enter(void)
{
  ShowLapRecordsEnter();
}

//-------------------------------------------------------------------------------------------------

void frontend_lap_records_exit(void)
{
  ShowLapRecordsExit();
}

//-------------------------------------------------------------------------------------------------

void frontend_time_trial_results_update(void)
{
  if (iFrontendTimeTrialScreenActive) {
    if (!TimeTrialsUpdate())
      return;
    TimeTrialsExit();
    iFrontendTimeTrialScreenActive = 0;
  }

  while (iFrontendTimeTrialCarIdx < numcars) {
    int iCarIdx = iFrontendTimeTrialCarIdx++;
    if (human_control[iCarIdx] && (char)Car[iCarIdx].byLap > 1) {
      TimeTrialsEnter(iCarIdx);
      iFrontendTimeTrialScreenActive = -1;
      return;
    }
  }
  eFrontendNextState = eFRONTEND_STATE_LAP_RECORDS;
}

//-------------------------------------------------------------------------------------------------

void frontend_time_trial_results_enter(void)
{
  iFrontendTimeTrialScreenActive = 0;
}

//-------------------------------------------------------------------------------------------------

void frontend_time_trial_results_exit(void)
{
  if (iFrontendTimeTrialScreenActive) {
    TimeTrialsExit();
    iFrontendTimeTrialScreenActive = 0;
  }
}

//-------------------------------------------------------------------------------------------------

void frontend_championship_over_update(void)
{
  if (!ChampionshipOverUpdate())
    return;
  if (eFrontendPostRaceCurrentFlow == eFRONTEND_POST_RACE_SINGLE &&
      (cheat_mode & CHEAT_MODE_CREDITS) != 0)
    eFrontendNextState = eFRONTEND_STATE_CREDITS;
  else
    frontend_finish_post_race_sequence();
}

//-------------------------------------------------------------------------------------------------

void frontend_championship_over_enter(void)
{
  ChampionshipOverEnter();
}

//-------------------------------------------------------------------------------------------------

void frontend_championship_over_draw(void)
{
  ChampionshipOverDraw();
}

//-------------------------------------------------------------------------------------------------

void frontend_championship_over_exit(void)
{
  ChampionshipOverExit();
}

//-------------------------------------------------------------------------------------------------

void frontend_credits_update(void)
{
  if (!RollCreditsUpdate())
    return;
  frontend_finish_post_race_sequence();
}

//-------------------------------------------------------------------------------------------------

void frontend_credits_enter(void)
{
  RollCreditsEnter();
}

//-------------------------------------------------------------------------------------------------

void frontend_credits_exit(void)
{
  RollCreditsExit();
}

//-------------------------------------------------------------------------------------------------

void race_set_track(int iTrack)
{
  iFrontendRaceTrack = iTrack;
}

//-------------------------------------------------------------------------------------------------

void race_enter(void)
{
  int iNetTimeItr; // eax
  int iNetTimeItr_1; // ecx

  game_track = iFrontendRaceTrack;              // Initialize game state and track
  if (game_track == TRACK_LOAD_COMMUNITY)
    CommunityRecordLoadForCurrentTrack();
  lagdone = 0;
  I_Want_Out = 0;
  iFrontendNetworkErrorHandled = 0;
  iFrontendRaceFadeOutPending = 0;
  play_game_init();                             // Initialize game systems and memory tracking
  if (quit_game)
    return;
  reset_net_wait();
  max_mem = mem_used_low + mem_used;
  enable_keyboard();
  pend_view_init = ViewType[0];
  //_disable();
  network_limit = 4320;                         // Disable interrupts and setup network timing arrays
  iNetTimeItr = 0;
  do {
    iNetTimeItr_1 = (int16)iNetTimeItr++;       // Initialize network timing array with current frame count
    net_time[iNetTimeItr_1] = frames;
  } while ((int16)iNetTimeItr < 16);
  network_timeout = frames;
  network_error = 0;
  network_sync_error = 0;
  reset_tick_input_samples();
  SDL_SetAtomicInt(&iTicksPending, 0);
  frontend_on = 0;
  //_enable();
}

//-------------------------------------------------------------------------------------------------

static void race_drain_pending_key_input(void)
{
  while (fatkbhit()) {
    int iKey = fatgetch();
    if (!iKey && fatkbhit())
      (void)fatgetch();
  }
}

//-------------------------------------------------------------------------------------------------

static void race_confirm_network_quit_prompt(void)
{
  if (Quit_Count > 0)
    return;

  I_Want_Out = -1;
  stopallsamples();
}

//-------------------------------------------------------------------------------------------------

static void race_handle_mouse_shortcuts(void)
{
  int iWidth;
  int iHeight;
  int iClicked;

  if (g_bSnapshotMode)
    return;

  iWidth = winw > 0 ? winw : XMAX;
  iHeight = winh > 0 ? winh : YMAX;
  frontend_mouse_begin_frame(iWidth, iHeight);
  touch_ui_register_buttons(iWidth, iHeight);
  touch_ui_handle_buttons();

  if (intro && replaytype == 2 && !game_req) {
    if (frontend_mouse_consume_click_anywhere()) {
      race_drain_pending_key_input();
      racing = 0;
    }
    return;
  }

  if (!I_Would_Like_To_Quit)
    return;

  frontend_mouse_register_rect(RACE_MOUSE_QUIT_PROMPT, 0, iHeight / 2 - 18,
                               iWidth, 44);
  iClicked = frontend_mouse_peek_clicked_id();
  if (iClicked == RACE_MOUSE_QUIT_PROMPT &&
      frontend_mouse_consume_click_anywhere())
    race_confirm_network_quit_prompt();
}

//-------------------------------------------------------------------------------------------------

void race_update(void)
{
  int16 nNetTimeItr_2; // bx
  int iNetTimeItr_3; // eax
  int iPendingTicks; // eax
  bool bShiftKeyPressed; // eax
  int iSong; // eax
  int iReplayMouseHeldIcon; // eax

  if (iFrontendRaceFadeOutPending) {
    if (g_pGameRenderer && game_render_fade_active(g_pGameRenderer))
      return;

    iFrontendRaceFadeOutPending = 0;
    eFrontendNextState = eFRONTEND_STATE_RESULTS;
    return;
  }

  if (!racing && lastsample <= 0) {
    if (g_pGameRenderer) {
      game_render_begin_fade(g_pGameRenderer, 0, 0);
      iFrontendRaceFadeOutPending = -1;
      return;
    }

    eFrontendNextState = eFRONTEND_STATE_RESULTS;
    return;
  }

  if (dostopsamps) {                            // Stop all sound samples if requested
    stopallsamples();
    dostopsamps = 0;
  }
  if (network_on)                               // Handle network game timing and synchronization
  {                                             // Set countdown for network games after frame 250
    if (frame_number >= 250)
      countdown = -75;
    if (countdown == 144 && replaytype != 2 && fadedin)// Send ready signal when countdown reaches 144 (not in replay mode)
      send_ready();
    if (network_limit == 4320 && countdown < 140)// Switch network timeout from 4320 to 360 frames when race starts
    {
      //_disable();
      network_limit = 360;
      for (nNetTimeItr_2 = 0; ; ++nNetTimeItr_2) {
        iNetTimeItr_3 = nNetTimeItr_2;
        if (nNetTimeItr_2 >= network_on)
          break;
        net_time[iNetTimeItr_3] = frames;       // Update network timing array for all connected nodes
      }
      network_timeout = frames;
      //_enable();
    }
  }
  if (player_type == 1 && (network_error || network_sync_error || cd_error))// Exit network game if errors occur in single player mode
    racing = 0;
  if (fadedin && !lagdone && (countdown < 140 || replaytype == 2))// Initialize sound lag compensation when race starts
  {
    initsoundlag(0);
    lagdone = -1;
  }
  if (screenready && !fadedin)                 // Start race timing only once the race view is visible
  {
    game_render_begin_fade(g_pGameRenderer, 1, 0);
    fadedin = -1;
    holdmusic = 0;
    SDL_SetAtomicInt(&iTicksPending, 0);
    if (w95) {                                       // Start appropriate music track (title song for replay, game track for race)
      if (!MusicCD && !winner_mode && !loading_replay) {
        if (replaytype == 2)
          iSong = titlesong;
        else if (game_track == TRACK_LOAD_COMMUNITY && nummusictracks > 0)
          iSong = rand() % nummusictracks + 1;
        else
          iSong = game_track;
        startmusic(iSong);
      }
    }
  }
  updates = 0;
  if (g_bSnapshotMode) {
    // No SDL tick timer in snapshot mode: drive one logical tick per
    // iteration with no wall-clock pacing, and zero scrbuf so any
    // unredrawn region cannot leak from the previous frame.
    SnapshotZeroScreen();
    SnapshotAdvanceTick();
  } else if (fadedin || replaytype == 2) {
    // Cap drained ticks per frame to prevent spiral-of-death: if a slow frame
    // backs up iTicksPending, catching up burns main-thread time and starves
    // the next render, which backs up more ticks, and so on.
    int iDrained = 0;
    while (!g_bShiftFrozen && iDrained < 4 && (iPendingTicks = SDL_GetAtomicInt(&iTicksPending)) != 0) {
      // Claim one pending tick with CAS. The timer thread can enqueue between
      // this read and update, so a plain read/add pair can race, especially
      // when replay rewind changes the pending count's sign.
      int iNextPendingTicks = iPendingTicks > 0 ? iPendingTicks - 1 : iPendingTicks + 1;
      if (!SDL_CompareAndSwapAtomicInt(&iTicksPending, iPendingTicks, iNextPendingTicks))
        continue;
      game_tick_step();
      ++iDrained;
    }
  } else {
    SDL_SetAtomicInt(&iTicksPending, 0);
  }
  if (!g_bShiftFrozen && replaytype == 2 && !frontend_on && ticks != currentreplayframe)
    game_tick_step();
  if (!replayspeed && intro && !game_req)       // Exit replay if intro mode and no game requested
    racing = replayspeed;
  //removed by ROLLER, CD looping is handled in ROLLER code
  //if (track_playing && frames - start_cd > 36 * track_duration / 75)// Handle CD audio track looping based on duration
  //{
  //  StopTrack();
  //  RepeatTrack();
  //  start_cd = frames;
  //}
  if (network_on && net_quit && !intro)         // Handle network quit requests
    racing = 0;
  if (player_type == 2)                         // Handle end-of-race conditions for different player modes
  {                                             // 2-player mode: end race when both cars dead and sound finished
    if ((Car[player1_car].byLives & 0x80u) != 0
      && (Car[player2_car].byLives & 0x80u) != 0
      && lastsample < -72
      && readsample == writesample
      && player_type != replaytype) {
      racing = 0;
    }
  } else if (!network_on && (Car[player1_car].byLives & 0x80u) != 0 && lastsample < -72 && readsample == writesample && replaytype != 2)// Single player: end race when car dead and sound finished
  {
    racing = (unsigned int)writesample ^ readsample;
  }
  if (winner_mode)                              // Handle winner celebration mode and championship fireworks
  {
    if (winner_done && lastsample < -72 && readsample == writesample) {
      racing = 0;
    } else {
      racing = -1;
      if (champ_mode) {
        if (champ_mode >= 16 && champ_zoom > 7 && champ_count < 0 && !winner_done) {
          // SOUND_SAMPLE_WON
          speechsample(114, 0x8000, 18, player1_car);// Play victory sound sample (114) in championship mode
          winner_done = -1;
        }
        if (!winner_done && champ_mode == 2) {
          // SOUND_SAMPLE_WON
          speechsample(114, 0x8000, 18, player1_car);// Play victory sound and advance championship mode
          ++champ_mode;
        }
        if (champ_mode == 3 && lastsample < -72 && readsample == writesample)// Initialize fireworks sequence with fade out/in
        {
          holdmusic = -1;
          game_render_begin_fade(g_pGameRenderer, 0, 0); // writesample ^ readsample == 0 when readsample == writesample
          ++champ_mode;
          firework_screen();
          game_render_begin_fade(g_pGameRenderer, 1, 0);
          champ_count = 720;
          champ_zoom = 3;
          champ_mode = 16;
          memset(CarSpray, 0, 0x318u);
          if (SVGA_ON)
            scr_size = 128;
          else
            scr_size = 64;
          winw = XMAX;
          winx = 0;
          winy = 0;
          winh = YMAX;
        }
      } else if (Car[ViewType[0]].byLap > 1 && !winner_done)// Play victory sound for regular race completion
      {
        // SOUND_SAMPLE_WON
        speechsample(114, 0x8000, 18, player1_car);
        winner_done = -1;
      }
    }
  }
  if (pause_request && !intro)                  // Handle pause requests (excluding intro mode)
  {
    if (!pausewindow || !paused) {                                         // Network pause handling - master/slave coordination
      if (network_on && replaytype != 2) {
        if (wConsoleNode == master) {
          if (!finished_car[player1_car]) {
            paused = paused == 0;
            if (paused)
              stopallsamples();
            if (paused)
              pauser = wConsoleNode;
            transmitpausetoslaves();
          }
        } else if (!finished_car[player1_car]) {
          send_pause();
        }
      } else {                                       // Local pause: push overlay state
        push_overlay(eFRONTEND_STATE_PAUSE_OVERLAY);
      }
    }
    pause_request = 0;
  }
  if (network_on && slave_pause && wConsoleNode == master)// Handle slave pause requests in network games
  {
    paused = paused == 0;
    if (paused)
      stopallsamples();
    transmitpausetoslaves();
    slave_pause = 0;
  }
  race_handle_mouse_shortcuts();
  if (!filingmenu && racing)
    game_keys();
  if (replaytype == 2) {
    if (SVGA_ON)
      scr_size = 128;
    else
      scr_size = 64;
  }
  replay_control_panel_update_mouse_state();
  if (!racing && !winner_done && winner_mode)   // Handle race end with victory sound if no winner celebration yet
  {
    // SOUND_SAMPLE_WON
    speechsample(114, 0x8000, 18, player1_car);
    winner_done = -1;
    racing = -1;
  }
  bShiftKeyPressed = keys[WHIP_SCANCODE_LSHIFT] || keys[WHIP_SCANCODE_RSHIFT];
  shifting = bShiftKeyPressed;
  iReplayMouseHeldIcon = replay_control_panel_mouse_held_icon();
  if ((bShiftKeyPressed && keys[WHIP_SCANCODE_F7]) ||
      (keys[WHIP_SCANCODE_RETURN] && controlicon == 8) ||
      iReplayMouseHeldIcon == 8)
  {
    if (shifting && keys[WHIP_SCANCODE_F7])
      controlicon = 9;
    slowing = 0;
    if (!rewinding)
      Rrewindstart();
  } else if (rewinding) {
    slowing = -1;
  }
  if ((shifting && keys[WHIP_SCANCODE_F8]) ||
      (keys[WHIP_SCANCODE_RETURN] && controlicon == 10) ||
      iReplayMouseHeldIcon == 10)
  {
    if (shifting && keys[WHIP_SCANCODE_F8])
      controlicon = 9;
    slowing = 0;
    if (!forwarding)
      Rforwardstart();
  } else if (forwarding) {
    slowing = -1;
  }
  //NOCD error disabled for ROLLER
  //if (!intro && replaytype != 2)            // Handle CD-ROM validation for copy protection
  //{
  //  if (player_type && player_type != 2) {
  //    if (!cdchecked) {
  //      if (active_nodes == network_on)
  //        cdchecked = -1;
  //      if (wConsoleNode == master && !netCD && active_nodes == network_on) {
  //        send_nocd_error();
  //        cd_error = -1;
  //        cdchecked = -1;
  //      }
  //    }
  //  } else if (!localCD) {
  //    racing = 0;
  //    cd_error = -1;
  //  }
  //}
}

//-------------------------------------------------------------------------------------------------

void race_draw(void)
{
  if (champ_mode < 16)                          // Update screen display (normal game or fireworks)
    updatescreen();
  else
    firework_screen();
  if (print_data)
    SDL_Log("\n\n");
  print_data = 0;
}

//-------------------------------------------------------------------------------------------------

void race_exit(void)
{
  play_game_uninit();                           // Clean up and uninitialize game systems
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//00010020
void copypic(uint8 *pSrc, uint8 *pDest)
{
  //added by ROLLER
  // During gameplay, game_render_end_frame handles presentation
  if (!g_pGameRenderer)
    UpdateSDLWindow();
  return;

  int iRowIdx; // edx
  uint8 *pSrcRow; // esi
  uint8 *pDestRow; // edi
  uint8 *pSrcPixel; // ecx
  uint8 *pDestPixel; // eax
  int i; // esi
  int iPixelIdx; // edx
  int iWindowWidth; // edi
  uint8 byCurrPixel; // bl

  if (SVGA_ON) {
    if (SVGA_ON == 1) {
      // Mode X (planar VGA mode)
      copyscreenmodex(pSrc, pDest);
    }
  } else if (winw != XMAX || winx || mirror) {
    if (mirror) {
      pSrcPixel = pSrc;
      pDestPixel = &pDest[XMAX * winy - 1 + winx + winw];
      for (i = 0; i < winh; pDestPixel += XMAX + winw) {
        iPixelIdx = 0;

        // Copy each pixel in the row in reverse order
        if (winw > 0) {
          do {
            iWindowWidth = winw;
            --pDestPixel;
            byCurrPixel = *pSrcPixel++;
            ++iPixelIdx;
            pDestPixel[1] = byCurrPixel;
          } while (iPixelIdx < iWindowWidth);
        }
        ++i;
      }
    } else {
      // Standard windowed copy, copy rect to specific pos
      for (iRowIdx = 0; iRowIdx < winh; ++iRowIdx) {
        pSrcRow = &pSrc[winw * iRowIdx];
        pDestRow = &pDest[XMAX * (iRowIdx + winy) + winx];
        memcpy(pDestRow, pSrcRow, winw);
      }
    }
  } else {
    // Full screen copy
    memcpy(&pDest[winw * winy], pSrc, winh * winw);
  }
}

//-------------------------------------------------------------------------------------------------
//000101D0
void init_screen()
{
  int iVesaMode; // ebx
  int i; // esi
  int16 nY; // bx
  int vesaModes[9]; // [esp+0h] [ebp-24h] BYREF

  vesaModes[0] = 0x100;
  vesaModes[1] = 0x101;
  vesaModes[2] = -1;
  if (!svga_possible && SVGA_ON) {
    setmodex();
    SVGA_ON = 1;
    current_mode = -1;
    XMAX = 640;
    YMAX = 400;
    scr_size = 128;
  }
  if (SVGA_ON && current_mode != -1) {
    current_mode = -1;
    iVesaMode = 0;//VESAmode(vesaModes);
    scrmode = iVesaMode;
    if (iVesaMode == -1) {
      if (firstrun) {
        if (!language) {
          SDL_Log("\n\nThis program has not detected a VESA video driver which it needs to run\n");
          SDL_Log("at it's optimum level. Please contact your video board manufacturer for\n");
          SDL_Log("more information. The program will now continue using lower resolution\n");
          SDL_Log("graphics.\n");
        }
        firstrun = 0;
      }
      svga_possible = 0;
      setmodex();
      SVGA_ON = 1;
      XMAX = 640;
      YMAX = 400;
      scr_size = 128;
      current_mode = -1;
    } else {
      current_mode = -1;
      SVGA_ON = -1;
      XMAX = 640;
      YMAX = 400;
      scr_size = 128;
      if (iVesaMode == 0x101) {
        memset(blank_line, 112, sizeof(blank_line));
      }
    }
  }
  if (!SVGA_ON && current_mode) {
    current_mode = SVGA_ON;
    XMAX = 320;
    YMAX = 200;
    scr_size = 64;
    
    // BIOS interrupt for video services
    // ah = 0
    // al = 13h
    // Set video mode 13h
    // (320x200 pixels, 256 colors)
    //__asm { int     10h; -VIDEO - SET VIDEO MODE }
  }
  xbase = 159;
  ybase = 115;
  winw = XMAX;
  winx = 0;
  winh = YMAX;
  winy = 0;

  //removed by ROLLER, causes no palette after toggling SVGA/VGA mode in-game
  //if (palette_brightness > 0)
  //  resetpal();
}

//-------------------------------------------------------------------------------------------------
//00010430
void init()
{
  test_f1 = 0;
  test_f2 = 0;
  switch_sets = 0;
  switch_same = 0;
  switch_types = 0;
  my_number = -1;
  if (SVGA_ON)
    scr_size = 128;
  else
    scr_size = 64;
  tex_count = 1;

  for (int i = 0; i < 25; ++i) {
    strcpy(RecordNames[i], "-----");
    RecordLaps[i] = 128.0f;
    RecordCars[i] = -1;
    RecordKills[i] = 0;
  }

  NoOfLaps = 0;
  player1_car = 0;
  player_invul[0] = 0;
  player_invul[1] = 0;
  player2_car = 1;

  // Fill blank_line with 640 bytes of 0x70
  memset(blank_line, 0x70, 640);
  //_STOSB(blank_line, 0x70707070, 1, 0x280u);

  DDY = 160.0;
  DDZ = 270.0;
  clear_borders = 0;
  VIEWDIST = 270;
  DDX = 0.0;

  //load tsin, tcos, and ptan
  for (int i = 0; i < 16384; ++i) {
    double dAngle = i * TWO_PI * ONE_OVER_TRIG_LOOKUP_AY_COUNT;
    tsin[i] = (float)sin(dAngle);
    tcos[i] = (float)cos(dAngle);
    ptan[i] = (float)tan(dAngle);
  }

  ext_x = 0;
  ext_y = 0;
  ext_z = 0;

  //load tatn
  for (int i = 0; i < 1025; ++i) {
    double dAngle = i * TWO_PI * ONE_OVER_TATN_LOOKUP_AY_COUNT;
    tatn[i] = (float)atan(dAngle);
  }

  scrbuf = getbuffer(SCRBUF_MAX_PIXELS);
  LoadRecords();
}

//-------------------------------------------------------------------------------------------------
//00010700
void *getbuffer(uint32 uiSize)
{
  int iMemBlocksIdx; // esi
  void *pBuf; // eax
  int iRegsDi = 0; // [esp+28h] [ebp-24h] BYREF
  void *pPtr = NULL; // [esp+2Ch] [ebp-20h] BYREF

  iMemBlocksIdx = find_free_mem_block();
  if (iMemBlocksIdx < 0) {
    LogMemBlocks("getbuffer", uiSize);
    ErrorBoxExit("Out of tracked memory blocks while allocating %u bytes", uiSize);
    return NULL;
  }

  pBuf = malloc2(uiSize, &pPtr, &iRegsDi);
  if (pBuf) {
    mem_blocks[iMemBlocksIdx].pBuf = pBuf;
    mem_blocks[iMemBlocksIdx].uiSize = uiSize;
    mem_blocks[iMemBlocksIdx].pAlsoBuf = pPtr;
    mem_blocks[iMemBlocksIdx].iRegsDi = iRegsDi;
    hibuffers++;
    mem_used += uiSize;
  }
  return pBuf;
}

//-------------------------------------------------------------------------------------------------
//00010890
void *trybuffer(uint32 uiSize)
{
  int iMemBlocksIdx; // esi
  void *pBuf; // eax
  void *pPtr = NULL; // [esp+28h] [ebp-20h] BYREF
  int iRegsDi = 0; // [esp+2Ch] [ebp-1Ch] BYREF
  int iMemUsed; // ecx

  iMemBlocksIdx = find_free_mem_block();
  if (iMemBlocksIdx < 0) {
    LogMemBlocks("trybuffer", uiSize);
    return NULL;
  }

  pBuf = malloc2(uiSize, &pPtr, &iRegsDi);
  if (pBuf) {
    mem_blocks[iMemBlocksIdx].pBuf = pBuf;
    mem_blocks[iMemBlocksIdx].uiSize = uiSize;
    mem_blocks[iMemBlocksIdx].pAlsoBuf = pPtr;
    iMemUsed = mem_used;
    mem_blocks[iMemBlocksIdx].iRegsDi = iRegsDi;
    mem_used = uiSize + iMemUsed;
    ++hibuffers;
  }

  return pBuf;
}

//-------------------------------------------------------------------------------------------------
//00010890 helper
uint32 getbuffer_size(void *pData)
{
  int iMemBlocksIdx;

  if (!pData)
    return 0;

  iMemBlocksIdx = find_mem_block(pData);
  if (iMemBlocksIdx < 0)
    return 0;

  return mem_blocks[iMemBlocksIdx].uiSize;
}

//-------------------------------------------------------------------------------------------------
//000109F0
void fre(void **ppData)
{
  int iMemBlocksIdx; // edx
  uint32 uiSize; // eax
  void *pBuf; // ebx

  if (ppData && *ppData) {
    iMemBlocksIdx = find_mem_block(*ppData);
    if (iMemBlocksIdx < 0) {
      assert(0);
      SDL_Log("Failed to find allocated block\n");
      doexit();
      return;
    }

    pBuf = mem_blocks[iMemBlocksIdx].pBuf;
    uiSize = mem_blocks[iMemBlocksIdx].uiSize;

    mem_blocks[iMemBlocksIdx].pBuf = 0;
    mem_blocks[iMemBlocksIdx].uiSize = 0;
    mem_blocks[iMemBlocksIdx].pAlsoBuf = 0;
    mem_blocks[iMemBlocksIdx].iRegsDi = 0;

    mem_used -= (int)uiSize;
    free2(pBuf); //.pAlsoBuf?
    *ppData = 0;
  }
}

//-------------------------------------------------------------------------------------------------
//00010AF0
void doexit()
{
  frontend_shutdown_request();

  if (!frontend_shutdown_complete() &&
      (eFrontendCurrentState == eFRONTEND_STATE_NONE ||
       eFrontendCurrentState == eFRONTEND_STATE_QUIT))
    frontend_set_state(eFRONTEND_STATE_SHUTDOWN);

  if (eFrontendCurrentState == eFRONTEND_STATE_SHUTDOWN)
    frontend_shutdown_update();

#ifndef __EMSCRIPTEN__
  if (frontend_shutdown_complete())
    exit(0);
#endif
}

//-------------------------------------------------------------------------------------------------
//00010C80
void firework_screen()
{
  int iCarIndex; // esi
  tCarSpray *pCarSpray; // ebx
  uint8 byType; // al
  int iSprayIndex; // ecx
  double fPosX; // st7
  double fPosY; // st7
  int i; // ecx
  double fRandomOffset; // st7
  double fTempY; // st7
  int iRandValue; // eax
  int iColorOffset; // eax
  char byFinalOffset; // dl
  int iScreenY; // [esp+0h] [ebp-30h]
  int iScreenY2; // [esp+4h] [ebp-2Ch]
  int iScreenX; // [esp+8h] [ebp-28h]
  int iScreenX2; // [esp+Ch] [ebp-24h]
  char byColorFade; // [esp+10h] [ebp-20h]

  game_render_begin_frame(g_pGameRenderer);
  iCarIndex = 0;

  // Clear screen buffer with background color (112)
  memset(scrbuf, 112, winh * winw);

  do {
    pCarSpray = CarSpray[iCarIndex];
    byType = CarSpray[iCarIndex][0].iType;

    // Check if car has active spray effects
    if (byType) {                                           // Type 1: Trail spray effects (up to 5 particles)
      if (byType <= 1u) {
        iSprayIndex = 0;
        byColorFade = 0;                        // Initialize color fade counter for trail effect
        do {
          if ((uint8)pCarSpray->iType) {
            // Convert floating point positions to screen coordinates
            fPosX = pCarSpray->position.fX;
            //_CHP();
            iScreenX = (int)fPosX;
            fPosY = pCarSpray->position.fY;
            //_CHP();
            iScreenY2 = (int)fPosY;

            // Bounds check: ensure particle is within screen area
            if (iScreenX >= 0 && iScreenX < winw && iScreenY2 >= 0 && iScreenY2 < winh)
              scrbuf[winw * iScreenY2 + iScreenX] = (uint8)(pCarSpray->iColor) - byColorFade;// Draw particle with fading color (darker for older trail segments)
          }
          ++pCarSpray;
          ++iSprayIndex;
          byColorFade += 3;                     // Increase fade amount for next trail segment (3 units darker)
        } while (iSprayIndex < 5);
      } else if (byType == 2)                   // Type 2: Firework explosion effects (32 particles)
      {
        for (i = 0; i < 32; ++i) {
          // Only draw particles that are still alive (lifetime > 0)
          if (pCarSpray->iLifeTime > 0) {
            fRandomOffset = pCarSpray->position.fX;
            //_CHP();
            iScreenX2 = (int)fRandomOffset;
            fTempY = pCarSpray->position.fY;
            //_CHP();
            iScreenY = (int)fTempY;
            iRandValue = ROLLERrandRaw();                // Generate random color variation for firework sparkle effect
            iColorOffset = GetHighOrderRand(16, iRandValue);// (16 * iRandValue) >> 15;
            //iColorOffset = (16 * iRandValue) % 32768 / 15;
            //iColorOffset = (16 * iRandValue - (__CFSHL__((16 * iRandValue) >> 31, 15) + ((16 * iRandValue) >> 31 << 15))) >> 15;
            byFinalOffset = iColorOffset - 4;   // Calculate color offset (-4 to +11 range) with minimum of 0
            if (iColorOffset - 4 < 0)
              byFinalOffset = 0;
            if (iScreenX2 >= 0 && iScreenX2 < winw && iScreenY >= 0 && iScreenY < winh)// Bounds check for firework particle position
              scrbuf[iScreenX2 + winw * iScreenY] = (uint8)(pCarSpray->iColor) - byFinalOffset;// Draw firework particle with random color variation
          }
          ++pCarSpray;
        }
      }
    }
    ++iCarIndex;
  } while (iCarIndex != 18);                    // Loop through all 18 cars' spray effects

  // Copy finished frame buffer to display screen
  game_copypic(scrbuf, screen, ViewType[0]);
  init_animate_ads();
  game_render_end_frame(g_pGameRenderer);
}

//-------------------------------------------------------------------------------------------------
//00010E30
void updatescreen()
{
  int iOriginalScrSize; // esi
  int iMirrorWinWidth; // edi
  int iMirrorWinHeight; // ebp
  int iMirrorXOffset; // esi
  uint8 *pDestPtr; // ecx
  uint8 *pMirrorSrcPtr; // esi
  int iRowCounter; // ebx
  uint8 *pCurrentRowPtr; // ecx
  int iPixelCounter; // eax
  uint8 *pPixelPtr; // ecx
  uint8 byPixelData; // dl
  int iWinWidthCopy; // eax
  int iReareaViewScrSize; // esi
  int iRearViewWinWidth; // edi
  int iRearViewWinHeight; // ebp
  int iViewTypeIndex; // edx
  uint8 *pRearViewDestPtr; // ecx
  uint8 *pRearViewSrcPtr; // esi
  int iRearViewRowCounter; // ebx
  uint8 *pRearViewRowPtr; // ecx
  int iRearViewPixelCounter; // eax
  uint8 *pRearViewPixelPtr; // ecx
  uint8 byRearViewPixelData; // dl
  int iRearViewWinWidthCopy; // eax
  int iXMaxCopy; // ebx
  int iWinHeightPlusY; // edx
  int iYMaxCopy; // ecx
  uint8 *pMainScrPtr; // ecx
  int iMirrorYOffset; // [esp+0h] [ebp-24h]
  int iShowRearView; // [esp+4h] [ebp-20h]

  game_render_begin_frame(g_pGameRenderer);
  mirror = 0;                                   // Initialize global screen state variables
  shown_panel = 0;
  screenready = -1;
  if (SVGA_ON)                                // Set polygon detail level based on SVGA mode
    small_poly = 200;
  else
    small_poly = 100;
  time_shown = 0;
  if (!Play_View)
    goto LABEL_30;                              // Check if we're in a play view mode (rear view or side view)
  if ((uint8)Play_View <= 1u)       // Handle rear view mode (Play_View <= 1)
  {
    time_shown = -1;                            // Setup rear view window - reduce screen size and position window
    iReareaViewScrSize = scr_size;
    xbase = 319;
    scr_size /= 4;  // Reduce screen size to 1/4 for rear view window
    //scr_size = (scr_size - (__CFSHL__(scr_size >> 31, 2) + 4 * (scr_size >> 31))) >> 2;
    winw = (320 * scr_size) >> 5;
    iRearViewWinWidth = (320 * scr_size) >> 5;
    winx = 0;
    winy = 0;
    winh = (200 * scr_size) >> 6;
    iRearViewWinHeight = (200 * scr_size) >> 6;
    iShowRearView = 0;
    if ((ViewType[0] & 1) != 0)               // Check if we should show rear view for current car
    {
      iViewTypeIndex = ViewType[0] - 1;         // Get car index for rear view based on ViewType flags
      if ((Car[iViewTypeIndex].byLives & 0x80u) != 0)
        goto LABEL_20;                          // Check if car is alive (bit 7 of byLives indicates death)
    } else {
      iViewTypeIndex = ViewType[0] + 1;
      if ((Car[iViewTypeIndex].byLives & 0x80u) != 0)
        goto LABEL_20;
    }
    iShowRearView = -1;
    game_render_begin_mirror_pass(g_pGameRenderer, 0.25f); /* scr_size /= 4 above */
    draw_road(mirbuf, iViewTypeIndex, 2u, 0, 0);// Draw rear view road to mirror buffer
    /* GPU mode: offscreen render target is supersampled (x4) relative to the
     * small SW logical window size for a less blocky result once composited. */
    game_render_end_mirror_pass(g_pGameRenderer,
                                 iRearViewWinWidth * 4, iRearViewWinHeight * 4);
  LABEL_20:
    time_shown = 0;
    shown_panel = 0;
    winw = (320 * iReareaViewScrSize + 32) >> 6;
    xbase = 159;
    winh = (200 * iReareaViewScrSize + 32) >> 6;
    winx = (XMAX - ((320 * iReareaViewScrSize + 32) >> 6)) / 2;
    winy = (YMAX - ((200 * iReareaViewScrSize + 32) >> 6)) / 2;
    scr_size = iReareaViewScrSize;
    draw_road(scrbuf, ViewType[0], DriveView[0], 0, 0);// Draw main forward view road to screen buffer
    if (clear_borders)                        // Clear screen borders if needed
    {
      clear_borders = 0;
      clear_border(0, 0, XMAX, winy);
      clear_border(0, winy, winx, winh);
      clear_border(winx + winw, winy, winx, winh);
      clear_border(0, winy + winh, XMAX, YMAX - (winy + winh));
    }
    if (iShowRearView)                        // Copy rear view mirror data to main screen with border (color 119)
    {
      if (game_render_get_mode(g_pGameRenderer) == GAME_RENDER_GPU) {
        /* Straight (non-flipped) copy, matching the SW pixel loop below which
         * increments (not decrements) its source pointer -- the rear-view
         * mirror shows the backward camera view directly, unlike the
         * left-right-reversed side-view mirror below. */
        game_render_composite_mirror_pass(g_pGameRenderer,
            (winw - iRearViewWinWidth - 1) / 2, 0,
            iRearViewWinWidth, iRearViewWinHeight,
            false, 0x77);
      } else {
      // Position rear view mirror horizontally centered
      pRearViewDestPtr = &scrbuf[(winw - iRearViewWinWidth - 1) / 2];
      //pRearViewDestPtr = &scrbuf[(winw - iRearViewWinWidth - 1) / 2 + winw * ((11 * scr_size - (__CFSHL__((11 * scr_size) >> 31, 6) + ((11 * scr_size) >> 31 << 6))) >> 6)];
      pRearViewSrcPtr = mirbuf;
      memset(pRearViewDestPtr, 0x77, iRearViewWinWidth + 2);
      iRearViewRowCounter = 0;
      for (pRearViewRowPtr = &pRearViewDestPtr[winw];
            iRearViewRowCounter < iRearViewWinHeight;
            pRearViewRowPtr = &pRearViewPixelPtr[iRearViewWinWidthCopy - iRearViewWinWidth - 1]) {
        *pRearViewRowPtr = 0x77;
        iRearViewPixelCounter = 0;
        for (pRearViewPixelPtr = pRearViewRowPtr + 1; iRearViewPixelCounter < iRearViewWinWidth; *(pRearViewPixelPtr - 1) = byRearViewPixelData) {
          ++pRearViewPixelPtr;
          byRearViewPixelData = *pRearViewSrcPtr++;
          ++iRearViewPixelCounter;
        }
        iRearViewWinWidthCopy = winw;
        *pRearViewPixelPtr = 0x77;
        ++iRearViewRowCounter;
      }
      memset(pRearViewRowPtr, 0x77, iRearViewWinWidth + 2);
      }
    }
    if (screenready)
      goto LABEL_14;
    goto LABEL_59;
  }
  if (Play_View == 2)                         // Handle side view mode (Play_View == 2)
  {
    iOriginalScrSize = scr_size;
    xbase = 159;
    scr_size /= 2;
    winw = (320 * scr_size) >> 6;
    iMirrorWinWidth = (320 * scr_size) >> 6;
    time_shown = -1;
    winx = 0;
    winy = 0;
    winh = (200 * scr_size) >> 7;
    iMirrorWinHeight = (200 * scr_size) >> 7;
    game_render_begin_mirror_pass(g_pGameRenderer, 0.5f); /* scr_size /= 2 above */
    draw_road(mirbuf, ViewType[0], -DriveView[0] - 1, 0, 0);// Draw mirrored view (negative DriveView for reverse perspective)
    game_render_end_mirror_pass(g_pGameRenderer,
                                 iMirrorWinWidth * 4, iMirrorWinHeight * 4);
    xbase = 159;
    winw = (320 * iOriginalScrSize + 32) >> 6;
    winh = (200 * iOriginalScrSize + 32) >> 6;
    time_shown = 0;
    shown_panel = 0;
    winx = (XMAX - ((320 * iOriginalScrSize + 32) >> 6)) / 2;
    winy = (YMAX - ((200 * iOriginalScrSize + 32) >> 6)) / 2;
    scr_size = iOriginalScrSize;
    draw_road(scrbuf, ViewType[0], DriveView[0], 0, 0);
    iMirrorXOffset = (winw - iMirrorWinWidth - 1) / 2;
    // Calculate vertical offset for rear view mirror position  
    iMirrorYOffset = (11 * scr_size) / 64;
    //iMirrorYOffset = (11 * scr_size - (__CFSHL__((11 * scr_size) >> 31, 6) + ((11 * scr_size) >> 31 << 6))) >> 6;
    if (clear_borders) {
      clear_borders = 0;
      clear_border(0, 0, XMAX, winy);
      clear_border(0, winy, winx, winh);
      clear_border(winx + winw, winy, winx, winh);
      clear_border(0, winh + winy, XMAX, YMAX - (winh + winy));
    }
    if (game_render_get_mode(g_pGameRenderer) == GAME_RENDER_GPU) {
      /* Flipped (mirrored) copy, matching the SW pixel loop below which
       * decrements its source pointer -- this is a true left-right mirror. */
      game_render_composite_mirror_pass(g_pGameRenderer,
          iMirrorXOffset, iMirrorYOffset,
          iMirrorWinWidth, iMirrorWinHeight,
          true, 0x70);
    } else {
    pDestPtr = &scrbuf[iMirrorXOffset + iMirrorYOffset * winw];// Copy side mirror view with border (color 112/0x70)
    pMirrorSrcPtr = &mirbuf[iMirrorWinWidth - 1];
    memset(pDestPtr, 0x70, iMirrorWinWidth + 2);
    iRowCounter = 0;
    for (pCurrentRowPtr = &pDestPtr[winw]; iRowCounter < iMirrorWinHeight; pCurrentRowPtr = &pPixelPtr[iWinWidthCopy - iMirrorWinWidth - 1]) {
      *pCurrentRowPtr = 0x70;
      iPixelCounter = 0;
      for (pPixelPtr = pCurrentRowPtr + 1; iPixelCounter < iMirrorWinWidth; *(pPixelPtr - 1) = byPixelData) {
        ++pPixelPtr;
        byPixelData = *pMirrorSrcPtr--;         // Copy pixels in reverse order for mirror effect
        ++iPixelCounter;
      }
      iWinWidthCopy = winw;
      ++iRowCounter;
      *pPixelPtr = 0x70;
      pMirrorSrcPtr += 2 * iMirrorWinWidth;
    }
    memset(pCurrentRowPtr, 0x70, iMirrorWinWidth + 2);
    }
    if (screenready) {
    LABEL_14:
      game_copypic(scrbuf, screen, ViewType[0]);// Copy screen buffer to final display and initialize animated elements
      init_animate_ads();
      game_render_end_frame(g_pGameRenderer);
      return;
    }
    goto LABEL_59;
  }
LABEL_30:
  pMainScrPtr = scrbuf;
  if (player_type == 2)                       // Handle 2-player split screen mode
  {                                             // Setup split screen for 2-player mode
    if (SVGA_ON)
      scr_size = 64;
    else
      scr_size = 32;
    winw = XMAX;
    xbase = 319;
    winx = 0;
    winy = 0;
    winh = YMAX / 2 - 2;
    if (clear_borders)
      memset(&scrbuf[winh * winw], 0, 4 * winw);
    game_render_begin_2p_pass(g_pGameRenderer);
    game_render_set_active_view_slot(g_pGameRenderer, 0);
    game_render_secondary_view_will_queue(g_pGameRenderer);
    draw_road(scrbuf, ViewType[0], DriveView[0], -1, 0);// Draw player 1 view (top half)
    if (game_render_get_mode(g_pGameRenderer) == GAME_RENDER_GPU)
      game_render_flush_player_view(g_pGameRenderer, 0, winw, winh, XMAX, YMAX,
                                     0, 0, winw, winh);
    shown_panel = 0;
    game_render_set_active_view_slot(g_pGameRenderer, 1);
    game_render_secondary_view_will_queue(g_pGameRenderer);
    draw_road(&scrbuf[winw * (winh + 4)], ViewType[1], DriveView[1], -1, 1);// Draw player 2 view (bottom half)
    if (game_render_get_mode(g_pGameRenderer) == GAME_RENDER_GPU) {
      game_render_flush_player_view(g_pGameRenderer, 1, winw, winh, XMAX, YMAX,
                                     0, winh + 4, winw, winh);
      game_render_draw_2p_divider(g_pGameRenderer, XMAX, YMAX, winh, 4);
    }
    game_render_end_2p_pass(g_pGameRenderer);
    time_shown = -1;
    if (SVGA_ON)
      scr_size = 128;
    else
      scr_size = 64;
    draw_type = 0;
    winw = XMAX;
    winh = YMAX;
    game_copypic(scrbuf, screen, ViewType[0]);
    draw_type = 2;
    goto LABEL_59;
  }
  // CHEAT_MODE_WIDESCREEN
  bool bCinemaActive = (cheat_mode & CHEAT_MODE_WIDESCREEN) != 0 && !paused;
  bool bGPUMode = game_render_get_mode(g_pGameRenderer) == GAME_RENDER_GPU;
  /* Widens the GPU 3D camera to the real window's native aspect ratio (no
   * bars) -- selected via the "(native)" Render Scale options, available
   * independently of the CINEMA cheat code (previously this only worked
   * while CINEMA was active, via a separate checkbox). */
  bool bRenderNative = g_bRenderNative && bGPUMode;
  /* Unconditional every frame -- Native mode's hudW/hudH/GPU-viewport override
   * persists across scene_render_gpu_end_frame, so any frame that ISN'T a
   * Native frame must positively undo a possible previous frame's override
   * rather than assume it starts clean. See game_render_reset_cinema_native. */
  game_render_reset_cinema_native(g_pGameRenderer);
  if ((cheat_mode & CHEAT_MODE_WIDESCREEN) == 0 || paused)     // Handle single player normal/widescreen mode
  {                                             // Standard windowed mode or paused state
    if ((cheat_mode & CHEAT_MODE_WIDESCREEN) != 0) {
      if (SVGA_ON)
        scr_size = 128;
      else
        scr_size = 64;
    }
    winh = (200 * scr_size + 32) >> 6;
    winw = (320 * scr_size + 32) >> 6;
    winx = (XMAX - ((320 * scr_size + 32) >> 6)) / 2;
    xbase = 159;
    winy = (YMAX - ((200 * scr_size + 32) >> 6)) / 2;
    if (clear_borders) {
      clear_borders = 0;
      clear_border(0, 0, XMAX, winy);
      clear_border(0, winy, winx, winh);
      clear_border(winx + winw, winy, winx, winh);
      iYMaxCopy = YMAX;
      iWinHeightPlusY = winh + winy;
      iXMaxCopy = XMAX;
      goto LABEL_44;
    }
    /* Widescreen support outside of the CINEMA cheat: same wide-camera
     * mechanism as the CINEMA+native case below, layered onto otherwise
     * completely normal single-player rendering -- see
     * game_render_begin_cinema_native's comment (game_render.c). */
    if (bRenderNative)
      game_render_begin_cinema_native(g_pGameRenderer, winh);
  } else if (bRenderNative) {                          // CINEMA active + "(native)" Render Scale:
                                                        // widen the GPU 3D camera to the real
                                                        // window's native aspect ratio, no bars --
                                                        // see game_render_begin_cinema_native's
                                                        // comment for why the HUD/SW reference
                                                        // frame below is deliberately left at
                                                        // EXACTLY the same values normal single-
                                                        // player mode uses (this is what makes the
                                                        // HUD render pixel-for-pixel identical to
                                                        // normal mode instead of blurry/stretched).
    if (SVGA_ON)
      scr_size = 128;
    else
      scr_size = 64;
    winh = (200 * scr_size + 32) >> 6;
    winw = (320 * scr_size + 32) >> 6;
    winx = 0;
    winy = 0;
    xbase = 159;
    pMainScrPtr = scrbuf;  // no offset -- fills the whole canvas, no bars to hide
    game_render_begin_cinema_native(g_pGameRenderer, winh);
  } else {                                             // Widescreen cheat mode (cheat_mode & 0x40) and not paused
    if (SVGA_ON)
      scr_size = 64;
    else
      scr_size = 32;
    winh = YMAX / 2;
    winw = XMAX;
    winx = 0;
    xbase = 319;
    winy = YMAX / 4;  // Position window at 1/4 down from top of screen
    //winy = (YMAX - (__CFSHL__(YMAX >> 31, 2) + 4 * (YMAX >> 31))) >> 2;
    memset(scrbuf, 0, winw * winy);
    memset(&scrbuf[winw * (winy + winh)], 0, winw * (YMAX - (winy + winh)));
    pMainScrPtr = &scrbuf[winw * winy];
    if (clear_borders) {
      clear_borders = 0;
      // Clear top border area (top quarter of screen)
      clear_border(0, 0, XMAX, YMAX / 4);
      //clear_border(0, 0, XMAX, (YMAX - (__CFSHL__(YMAX >> 31, 2) + 4 * (YMAX >> 31))) >> 2);
      iXMaxCopy = XMAX;
      iWinHeightPlusY = winh + winy;
      iYMaxCopy = YMAX;
    LABEL_44:
      clear_border(0, iWinHeightPlusY, iXMaxCopy, iYMaxCopy - iWinHeightPlusY);
    }
    /* GPU letterbox parity fix: SW already gets a real letterbox for free
     * (scrbuf IS the visible frame there); GPU needs the shrunk 3D content
     * rendered into its own texture and composited into the (winx,winy,
     * winw,winh) sub-rect with real opaque black bars -- see
     * game_render_begin_cinema_pass's comment. */
    if (bGPUMode)
      game_render_begin_cinema_pass(g_pGameRenderer, winw, winh);
  }
  draw_road(pMainScrPtr, ViewType[0], DriveView[0], -1, 0);// Draw main road view
  if (bCinemaActive && !bRenderNative && bGPUMode) {
    game_render_end_cinema_pass(g_pGameRenderer, winw * 2, winh * 2);
    game_render_composite_cinema_view(g_pGameRenderer, winx, winy, winw, winh);
  }
  // CHEAT_MODE_WIDESCREEN
  if ((cheat_mode & CHEAT_MODE_WIDESCREEN) == 0)               // Check for widescreen cheat mode to copy to final screen
  {
  LABEL_59:
    init_animate_ads();                         // Initialize animated advertisements and return
    game_render_end_frame(g_pGameRenderer);
    return;
  }
  if (SVGA_ON)
    scr_size = 128;
  else
    scr_size = 64;
  winw = XMAX;
  winh = YMAX;
  init_animate_ads();
  game_render_end_frame(g_pGameRenderer);
}

//-------------------------------------------------------------------------------------------------
//00011800
void draw_road(uint8 *pScrPtr, int iCarIdx, unsigned int uiViewMode, int iCopyImmediately, int iChaseCamIdx)
{
  double dSubscaleMultiplier; // st7
  double dTextureSubscaleMultiplier; // st7

  // TEX_OFF_PERSPECTIVE_CORRECTION
  if ((textures_off & TEX_OFF_PERSPECTIVE_CORRECTION) != 0 || game_track == 13) {                                             // Set minimum subdivision size for texture-less rendering
    if (gfx_size == 1)
      min_sub_size = 64;
    else
      min_sub_size = 128;
  } else if (gfx_size == 1)                     // Set minimum subdivision size for textured rendering
  {
    min_sub_size = 4;
  } else {
    min_sub_size = 8;
  }
  if (SVGA_ON)                                // Calculate base subscale multiplier based on graphics mode
    dSubscaleMultiplier = (double)scr_size * 2.0;// SVGA mode: 2x multiplier for higher detail
  else
    dSubscaleMultiplier = (double)scr_size * 4.0;// VGA mode: 4x multiplier for standard detail
  subscale = (float)dSubscaleMultiplier;
  // TEX_OFF_PERSPECTIVE_CORRECTION
  if ((textures_off & TEX_OFF_PERSPECTIVE_CORRECTION) != 0 || game_track == 13) {
    if (SVGA_ON)
      dTextureSubscaleMultiplier = (double)scr_size * 1.1;// SVGA texture-less: 1.1x multiplier (slightly higher detail)
    else
      dTextureSubscaleMultiplier = (double)scr_size * 1.2;// VGA texture-less: 1.2x multiplier (slightly higher detail)
    subscale = (float)dTextureSubscaleMultiplier;
  }
  // Gameplay frame phase: camera/projection setup.
  screen_pointer = pScrPtr;                     // Set global screen buffer pointer for rendering functions
  game_render_set_target(g_pGameRenderer, pScrPtr, winw, winw, winh);
  calculateview(uiViewMode, iCarIdx, iChaseCamIdx); // Calculate camera view matrix and projection parameters
  noclip_camera_apply();
  chase_look_apply(); // Debug "Free Camera": hold RMB + move mouse to free-look, gated on the debug-overlay checkbox
  extern float viewx, viewy, viewz;
  extern int worlddirn, VIEWDIST;
  GameRenderCamera cam = {
      .viewX = viewx,
      .viewY = viewy,
      .viewZ = viewz,
      .cosYaw = SDL_cosf(ANGLE_TO_RADIANS(worlddirn)),
      .sinYaw = SDL_sinf(ANGLE_TO_RADIANS(worlddirn)),
      .fovScale = (float)VIEWDIST,
      .renderChunkIdx = Car[iCarIdx].nCurrChunk,
  };
  game_render_set_camera(g_pGameRenderer, &cam);

  extern float vk1, vk2, vk3, vk4, vk5, vk6, vk7, vk8, vk9;
  GameRenderProjection proj = {
      .view = {{vk1, vk2, vk3},
               {vk4, vk5, vk6},
               {vk7, vk8, vk9}},
      .screenScale = scr_size,
      .centerX = xbase,
      .centerY = ybase,
      .texHalfRes = gfx_size,
  };
  game_render_set_projection(g_pGameRenderer, &proj);

  // Gameplay frame phase: atmosphere. Sky/horizon stays outside the depth-sorted 3D queue.
  game_render_draw_sky(g_pGameRenderer, &cam, &proj); // Draw sky/horizon background

  unsigned int uiVisibilityViewMode = g_bNoclip ? 3u : uiViewMode;

  // Gameplay frame phase: visibility/entity production.
  CalcVisibleTrack(iCarIdx, uiVisibilityViewMode); // Calculate which track segments are visible from current viewpoint
  DrawCars(iCarIdx, uiVisibilityViewMode);         // Prepare visible cars (excluding current player if in chase cam)
  CalcVisibleBuildings();                       // Calculate visibility and prepare building rendering data

  // Gameplay frame phase: 3D queue production and sorted dispatch.
  DrawTrack3(pScrPtr, iChaseCamIdx, iCarIdx, &cam, &proj); // Render track surface, buildings, and track-side objects

  // Gameplay frame phase: capture/present.
  if (iCopyImmediately)                       // Check if immediate screen copy is requested
  {                                             // Copy rendered frame to display if screen is ready
    if (screenready)
      game_copypic(pScrPtr, screen, iCarIdx);
  }
}

//-------------------------------------------------------------------------------------------------
//00011930
#if defined(IS_ANDROID)
int main(int argc, const char **argv, const char **envp);
int SDL_main(int argc, char *argv[])
{
  return main(argc, (const char **)argv, NULL);
}
#endif

int main(int argc, const char **argv, const char **envp)
{
  int consumed = 0;
  int iCrashHandlerEnabled = 1;
  int iPlayer1NameOverride = 0;
  char whiplash_root[260] = { 0 };
  char szPlayer1NameOverride[ROLLER_PLAYER_NAME_BYTES] = { 0 };
  const char *midi_root = NULL;

  for (int i = 1; i < argc;) {
    int consumed = -1;
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      print_usage(stdout, argv[0]);
      return 0;
    } else if (strcmp(argv[i], "--no-crash-handler") == 0) {
      iCrashHandlerEnabled = 0;
      consumed = 1;
    } else if (strcmp(argv[i], "--whiplash-root") == 0) {
      if (i + 1 < argc) {
        strncpy(whiplash_root, argv[i + 1], sizeof(whiplash_root));
        consumed = 2;
      } else {
        cli_fprintf(stderr, "ERROR: '--whiplash-root' needs an argument\n");
        return 1;
      }
    } else if (strcmp(argv[i], "--midi-root") == 0) {
      if (i + 1 < argc) {
        midi_root = argv[i + 1];
        consumed = 2;
      } else {
        cli_fprintf(stderr, "ERROR: '--midi-root' needs an argument\n");
        return 1;
      }
    } else if (strcmp(argv[i], "--player1name") == 0) {
      if (i + 1 < argc) {
        if (!cli_player_name_copy(szPlayer1NameOverride, argv[i + 1])) {
          cli_fprintf(stderr, "ERROR: '--player1name' must contain at least one letter or digit\n");
          return 1;
        }
        iPlayer1NameOverride = 1;
        consumed = 2;
      } else {
        cli_fprintf(stderr, "ERROR: '--player1name' needs an argument\n");
        return 1;
      }
    } else if (strcmp(argv[i], "--local-ip") == 0) {
      if (i + 1 < argc) {
        ROLLERCommsSetLocalIP(argv[i + 1]);
        consumed = 2;
      } else {
        cli_fprintf(stderr, "ERROR: '--local-ip' needs an argument\n");
        return 1;
      }
    } else if (strcmp(argv[i], "--port") == 0) {
      if (i + 1 < argc) {
        int iPort = atoi(argv[i + 1]);
        if (iPort <= 0 || iPort > 65535) {
          cli_fprintf(stderr, "ERROR: '--port' must be 1-65535\n");
          return 1;
        }
        ROLLERCommsSetLocalPort((uint16_t)iPort);
        consumed = 2;
      } else {
        cli_fprintf(stderr, "ERROR: '--port' needs an argument\n");
        return 1;
      }
    } else if (strcmp(argv[i], "--peer") == 0) {
      if (i + 1 < argc) {
        char szPeerBuf[64];
        strncpy(szPeerBuf, argv[i + 1], sizeof(szPeerBuf) - 1);
        szPeerBuf[sizeof(szPeerBuf) - 1] = '\0';
        char *pszColon = strrchr(szPeerBuf, ':');
        if (!pszColon) {
          cli_fprintf(stderr, "ERROR: '--peer' expects IP:PORT format\n");
          return 1;
        }
        *pszColon = '\0';
        int iPeerPort = atoi(pszColon + 1);
        if (iPeerPort <= 0 || iPeerPort > 65535) {
          cli_fprintf(stderr, "ERROR: '--peer' port must be 1-65535\n");
          return 1;
        }
        ROLLERCommsSetPeer(szPeerBuf, (uint16_t)iPeerPort);
        consumed = 2;
      } else {
        cli_fprintf(stderr, "ERROR: '--peer' needs an argument\n");
        return 1;
      }
    } else if (strcmp(argv[i], "--net-slot") == 0) {
      if (i + 1 < argc) {
        network_slot = atoi(argv[i + 1]);
        consumed = 2;
      } else {
        cli_fprintf(stderr, "ERROR: '--net-slot' needs an argument\n");
        return 1;
      }
    } else if (strcmp(argv[i], "--snapshot") == 0) {
      if (i + 1 < argc) {
        SnapshotSetReplay(argv[i + 1]);
        g_bSnapshotMode = 1;
        consumed = 2;
      } else {
        cli_fprintf(stderr, "ERROR: '--snapshot' needs an argument\n");
        return 1;
      }
    } else if (strcmp(argv[i], "--snapshot-scene") == 0) {
      if (i + 1 < argc) {
        SnapshotSetScene(argv[i + 1]);
        g_bSnapshotMode = 1;
        consumed = 2;
      } else {
        cli_fprintf(stderr, "ERROR: '--snapshot-scene' needs an argument\n");
        return 1;
      }
    } else if (strcmp(argv[i], "--frames") == 0) {
      if (i + 1 < argc) {
        if (SnapshotParseFrames(argv[i + 1]) != 0) {
          cli_fprintf(stderr, "ERROR: '--frames' must be a comma-separated list of non-negative integers\n");
          return 1;
        }
        consumed = 2;
      } else {
        cli_fprintf(stderr, "ERROR: '--frames' needs an argument\n");
        return 1;
      }
    } else if (strcmp(argv[i], "--out") == 0) {
      if (i + 1 < argc) {
        SnapshotSetOutDir(argv[i + 1]);
        consumed = 2;
      } else {
        cli_fprintf(stderr, "ERROR: '--out' needs an argument\n");
        return 1;
      }
    }
    if (consumed < 0) {
      cli_fprintf(stderr, "ERROR: Unknown argument '%s'\n", argv[i]);
      print_usage(stderr, argv[0]);
      return 1;
    }
    i += consumed;
  }

#if defined(IS_ANDROID)
  if (!whiplash_root[0]) {
    const char *szExt = SDL_GetAndroidExternalStoragePath();
    if (!szExt) {
      ErrorBoxExit("External storage is not available. Cannot locate game data.");
      return 1;
    }
    strncpy(whiplash_root, szExt, sizeof(whiplash_root) - 1);
    whiplash_root[sizeof(whiplash_root) - 1] = '\0';
  }
#endif

  if (g_bSnapshotMode) {
    if (g_SnapshotConfig.eKind == SNAPSHOT_KIND_REPLAY && g_SnapshotConfig.szReplayName[0] == '\0') {
      cli_fprintf(stderr, "ERROR: '--snapshot' requires a replay filename\n");
      return 1;
    }
    if (g_SnapshotConfig.eKind == SNAPSHOT_KIND_SCENE && g_SnapshotConfig.szSceneName[0] == '\0') {
      cli_fprintf(stderr, "ERROR: '--snapshot-scene' requires a scene name\n");
      return 1;
    }
    if (g_SnapshotConfig.eKind == SNAPSHOT_KIND_NONE) {
      cli_fprintf(stderr, "ERROR: snapshot mode requires '--snapshot' or '--snapshot-scene'\n");
      return 1;
    }
    if (g_SnapshotConfig.iNumFrames == 0) {
      cli_fprintf(stderr, "ERROR: snapshot mode requires '--frames N[,M,...]'\n");
      return 1;
    }
    if (g_SnapshotConfig.szOutDir[0] == '\0') {
      cli_fprintf(stderr, "ERROR: snapshot mode requires '--out DIR'\n");
      return 1;
    }
    iCrashHandlerEnabled = 0;
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    srand(0);
    ROLLERsrand(0);
  }

  if (iCrashHandlerEnabled)
    InitCrashHandler(whiplash_root);

  if (InitSDL(whiplash_root, midi_root) != 0) {
    return 1;
  }

  if (!g_bSnapshotMode) {
    // The CD-image extraction prompt, replays directory bootstrap, and audio
    // probe are all interactive or audio-device-dependent: skip in headless
    // snapshot mode. The replay file is loaded directly from cwd later.
    InitFATDATA(whiplash_root);
    InitREPLAYS(whiplash_root);
    InitTRACKS(whiplash_root);
    ROLLERGetAudioInfo();
  }

  ROLLERCommsSetCommandBase(0x686C6361u);          // Initialize communication system with base command
  oldmode = readmode();                         // Save current video mode
  blankpal();
  SVGA_ON = 0;                                  // Disable SVGA mode for initial screen setup
  init_screen();
  blankpal();
  SVGA_ON = -1;
  init_screen();                                // Enable SVGA mode and reinitialize screen
  blankpal();
  test_w95();                                   // Test for Windows 95 compatibility
  //harderr((int)criticalhandler, __CS__);        // Install critical error handler
  // network_slot defaults to 0 (host); --net-slot sets it before we get here
  player1_car = 0;
  player2_car = 1;
  name_copy(player_names[0], "HUMAN");          // Initialize default player names
  name_copy(player_names[player2_car], "PLAYER 2");
  textures_off = 0;
  frontend_on = -1;
  claim_key_int();
  max_mem = 0;                                  // Initialize memory tracking
  setdirectory(whiplash_root);
  memset(mem_blocks, 0, sizeof(mem_blocks));
  cheat_mode = 0;
  load_language_map();
  if (g_bSnapshotMode) {
    // Bypass fatal.ini so the user's local settings can't drift the
    // captured pixels.
    SnapshotApplyFixedSettings();
  } else {
    load_fatal_config();
  }
  if (iPlayer1NameOverride) {
    player_name_copy_bytes(player_names[player1_car], szPlayer1NameOverride);
    player_name_copy_bytes(my_name, szPlayer1NameOverride);
  }
  if ((textures_off & TEX_OFF_WIDESCREEN) != 0)           // Check for debug cheat flags in textures_off
  {
    cheat_mode |= CHEAT_MODE_WIDESCREEN;
    textures_off ^= TEX_OFF_WIDESCREEN;
  }
  if ((textures_off & TEX_OFF_PANEL_RESTRICTED) != 0)           // Check for false starts cheat flag
  {
    false_starts = -1;

    // BYTE1 is the second byte
    textures_off ^= TEX_OFF_PANEL_RESTRICTED;
    //uiTexturesOff = textures_off;
    //BYTE1(uiTexturesOff) = BYTE1(textures_off) ^ 0x40;
    //textures_off = uiTexturesOff;
  } else {
    false_starts = 0;
  }
  findintrofiles();
  initmusic();
  tick_on = 0;
  ROLLERremove("../REPLAYS/REPLAY.TMP");
  readsoundconfig();
  if (strcmp(languagename, "Brazilian") == 0)
    g_bBrazilianMayte = true;
  InputLoadConfig();
  loadcheatnames();
  cdxinit();
  GetFirstCDDrive();
  ResetDrive();
  StopTrack();
  StopTrack();
  print_data = 0;
  TrackSelect = 0;

  // this initializes the timer system and calls
  // claim_ticktimer which sets up a callback to
  // increment ticks
  Initialise_SOS();
  bool bUseFixedMachineSpeed = g_bSnapshotMode;
#if defined(IS_WASM)
  bUseFixedMachineSpeed = true;
#endif
  if (bUseFixedMachineSpeed) {
    // Snapshot mode has no SDL tick timer. The WASM clock starts in the rAF
    // main loop below, so it also cannot advance during this busy calibration.
    // Pick a sane modern-machine value for both non-calibrating paths.
    machine_speed = 9000;
  } else {
    check_machine_speed();
  }
  if (fatal_ini_loaded)                       // Apply performance settings based on machine speed
  {
    if (view_limit) {
      if (machine_speed >= 2800)
        view_limit = 32;
      else
        view_limit = 24;
    }
  } else {                                             // Auto-configure graphics settings for slower machines
    if (machine_speed < 9000)
      textures_off |= TEX_OFF_PERSPECTIVE_CORRECTION;
    if (machine_speed < 5000)
      textures_off |= TEX_OFF_PANEL_RESTRICTED;
    if (machine_speed < 4600) {
      textures_off |= TEX_OFF_GLASS_WALLS;
    }
    if (machine_speed < 4300)
      textures_off |= TEX_OFF_CLOUDS;
    if (machine_speed < 4000)
      view_limit = 32;
    if (machine_speed < 3750) {
      textures_off |= TEX_OFF_PANEL_OFF;
    }
    if (machine_speed < 3500) {
      textures_off |= TEX_OFF_HORIZON;
    }
    if (machine_speed < 3250)
      textures_off |= TEX_OFF_BUILDINGS;
    if (machine_speed < 3000)
      game_size = 48;
    if (machine_speed < 3800)
      allengines = 0;
    if (machine_speed < 2900)
      replay_record = 0;
    if (machine_speed < 2800)
      view_limit = 24;
  }
  InitCarStructs();                             // Initialize car data structures
  init();
  SnapshotApplyFixedRecords();
  if (g_bSnapshotMode && g_SnapshotConfig.eKind == SNAPSHOT_KIND_SCENE) {
    SnapshotEnsureMenuRenderer();
    int iSceneRc = SnapshotRunScene();
    doexit();
    return iSceneRc;
  }
  print_data = 0;
  tick_on = 0;
  time_to_start = 0;                            // Initialize race state variables
  replaytype = 2;
  start_race = 0;
  countdown = 144;
  gosound = 3;
  delayread = 0;
  delaywrite = 6;
  writeptr = 0;
  readptr = 0;
  winner_mode = 0;
  intro = -1;
  race_set_track(TrackLoad);                    // Start initial intro replay through the dispatcher.
  frontend_run_game_loop(g_bSnapshotMode ? eFRONTEND_STATE_RACING : eFRONTEND_STATE_COPYRIGHT);
  //__asm { int     10h; Reset video mode and exit game }// Reset video mode and exit game
  if (!frontend_shutdown_complete())
    doexit();
  return 0;
}

//-------------------------------------------------------------------------------------------------
//00012050
void play_game_init()
{
  //uint32 uiTexturesOff; // edx
  //uint32 uiCheatMode; // ebx
  //int iNumCarsBytes; // ebx
  //unsigned int uiLoopCounter; // eax
  int iTeamMateIndex; // ebp
  int iNonCompetitorFlag; // edx
  int iPlayerSearchIndex; // ebp
  int i; // eax
  int iCurrentLaps; // eax
  int iSong; // eax
  int iNetworkMesMode; // ebp
  char *szPaletteFile; // eax
  int iAlternateViewType; // eax
  int iTurnRateCalc; // ecx
  int iSteeringSensitivity; // edx
  int iTurnRateCalc2; // ecx
  int iSteeringSensitivity2; // edx
  tData *pLocalDataAy; // edx
  int iTrackLen; // ecx
  double dTotalTrackDistance; // st7
  double dTotalTrackDistance2; // st6
  int iChunkIdx; // eax
  int iSavedNetworkMesMode; // ebp
  //int iCarLoopCounter; // eax
  //int iCarArraySize; // edx

  DeathView[0] = -1;                            // Initialize game state variables to default values
  DeathView[1] = -1;
  check_set = 0;
  I_Would_Like_To_Quit = 0;
  Quit_Count = 0;
  dostopsamps = 0;
  my_number = -1;
  network_error = 0;
  network_sync_error = 0;
  network_mistake = 0;
  view1_cnt = 0;
  view0_cnt = 0;
  network_buggered = 0;
  trying_to_exit = 0;
  gave_up = 0;
  define_mode = 0;
  next_resync = -1;
  sync_errors = 0;
  game_frame = -1;
  already_quit = 0;
  if (network_on)                             // Clear CD error flag in network mode
    cd_error = 0;
  send_finished = 0;
  fudge_wait = -1;
  memset(player_checks, -1, sizeof(player_checks));
  memset(player_syncs, 0, sizeof(player_syncs));
  read_check = 0;
  write_check = 0;
  if (!svga_possible || no_mem)               // Disable SVGA if not possible or insufficient memory
    game_svga = 0;
  SVGA_ON = game_svga;
  if (no_mem) {
    fre((void **)&scrbuf);
    /* GPU HUD overlay compositing writes hudW*hudH bytes into scrbuf every frame;
     * keep the full allocation in GPU mode even when running low-memory. */
    scrbuf = (uint8 *)getbuffer(
        game_render_get_mode(g_pGameRenderer) == GAME_RENDER_GPU ? 256000u : 64000u);
  }
  if (gfx_size == 1)                          // Set minimum sub-frame size based on graphics quality setting
    min_sub_size = 4;
  else
    min_sub_size = 8;
  cdchecked = 0;
  paused = 0;
  tick_on = 0;
  start_race = 0;
  game_overs = 0;
  if (replaytype != 2)                        // Handle random seed for network championship mode
  {
    if (game_type == 1 && player_type == 1) {
      network_champ_on = random_seed;
      if (!random_seed)
        network_champ_on = 666;
    } else if (!winner_mode) {
      network_champ_on = 0;
    }
  }
  replay_player = player_type;
  frame_number = 0;
  writeptr = 0;
  readptr = 0;
  if (replaytype == 2)                        // Special handling for replay mode - copy competitor settings
  {
    replay_cheat = cheat_mode;
    if ((cheat_mode & CHEAT_MODE_WIDESCREEN) != 0)
    {
      textures_off |= TEX_OFF_WIDESCREEN;
      //uiTexturesOff = textures_off;
      //BYTE1(uiTexturesOff) = BYTE1(textures_off) | 0x20;
      //textures_off = uiTexturesOff;

      cheat_mode ^= CHEAT_MODE_WIDESCREEN;
      //uiCheatMode = cheat_mode;
      //LOBYTE(uiCheatMode) = cheat_mode ^ 0x40;
      //cheat_mode = uiCheatMode;
    }
    player_type = 0;
    if (numcars > 0)                          // Copy competitor flags from non_competitors to result_competing array
    {

      for (int i = 0; i < numcars; ++i) {
        result_competing[i] = non_competitors[i];
      }
      //iNumCarsBytes = 4 * numcars;
      //uiLoopCounter = 0;
      //do {
      //  uiLoopCounter += 4;
      //  LODWORD(result_best[uiLoopCounter / 4 + 15]) = TrackArrow_variable_1[uiLoopCounter / 4];// references adjacent data
      //} while ((int)uiLoopCounter < iNumCarsBytes);
    }
  }
  memset(copy_multiple, 0, sizeof(copy_multiple));
  draw_type = player_type;
  local_players = (player_type == 2) + 1;       // Set up network teammate and messaging system
  if (network_on) {
    iTeamMateIndex = (player1_car & 1) != 0 ? player1_car - 1 : player1_car + 1;
    team_mate = iTeamMateIndex;
    iNonCompetitorFlag = non_competitors[iTeamMateIndex];
    network_mes_mode = iTeamMateIndex;
    if (iNonCompetitorFlag) {
      if (players <= 1) {
        iPlayerSearchIndex = -1;
      } else {
        iPlayerSearchIndex = -1;
        for (i = 0; i < MAX_PLAYERS; ++i) {
          if (i != player1_car && human_control[i]) {
            iPlayerSearchIndex = i;
            break;
          }
        }
      }
      network_mes_mode = iPlayerSearchIndex;
      team_mate = -1;
    }
  }
  if (player_type != 2) {
    player2_car = -1;
    ViewType[1] = -1;
  }
  FirstTick = -1;
  dead_humans = 0;
  ticks = 0;
  tick_on = -1;
  load_language_file(szIngameEng, 0);
  load_language_file(szConfigEng_0, 1);
  if (frontendspeechptr) {
    SDL_Log("Front end speech memory still allocated!!!!!!!\n");
    doexit();
    return;
  }
  if ((cheat_mode & CHEAT_MODE_DEATH_MODE) != 0)
  {
    game_level = level;
    game_dam = damage_level;
    damage_level = 3;
    level = 0;
  }
  disable_messages = 0;
  Fatality = -1;
  Destroyed = 0;
  ahead_sect = -2;
  ahead_time = 0;
  game_count[0] = -2;
  game_count[1] = -2;
  set_game_scale(0, 32768.0f);
  set_game_scale(1, 32768.0f);
  memset(repsample, 1, sizeof(repsample));
  memcpy(newrepsample, repsample, sizeof(newrepsample));
  autoswitch = -1;
  game_req = 0;
  mirbuf = getbuffer(0x7D00u);                  // Allocate mirror buffer for rear-view display
  screenready = 0;
  racing = -1;
  if (replaytype != 2)
    replayedit = 0;
  if (!winner_mode) {
    SelectedView[0] = game_view[0];
    SelectedView[1] = game_view[1];
  }
  race_started = 0;
  rud_turn[0] = 0;
  finishers = 0;
  human_finishers = 0;
  setreplaytrack();
  loadtrack(game_track, 0);                     // Load track data and initialize game world
  LoadGenericCarTextures();
  InitCars();
  if (game_type >= 2)                         // Set lap count based on game type (championship vs single race)
  {
    NoOfLaps = 5;
    if (network_on)
      countdown = 144;
    else
      countdown = -72;
  } else {
    iCurrentLaps = cur_laps[level];
    NoOfLaps = iCurrentLaps;
    if (competitors == 2)
      NoOfLaps = iCurrentLaps / 2;
    if (infinite_laps)
      NoOfLaps = 0;
  }
  startreplay();
  LoadPanel();
  initclouds();
  if (!w95 || MusicCD)                        // Start background music if not in Windows 95 or using Music CD
  {
    if (!winner_mode && !loading_replay) {
      if (replaytype == 2)
        iSong = titlesong;
      else if (TrackLoad == TRACK_LOAD_COMMUNITY && nummusictracks > 0)
        iSong = rand() % nummusictracks + 1;
      else
        iSong = TrackLoad;
      startmusic(iSong);
    }
    holdmusic = -1;
  }
  loadsamples();                                // Initialize audio system - load samples, setup collisions and sounds
  initcollisions();
  initsounds();
  set_black_palette_now();
  init_screen();
  if (intro || replaytype == 2)               // Set screen size based on intro mode or replay
  {
    iNetworkMesMode = network_mes_mode;
    if (SVGA_ON)
      scr_size = 128;
    else
      scr_size = 64;
  } else {
    iNetworkMesMode = network_mes_mode;
    scr_size = game_size;
  }
  network_mes_mode = iNetworkMesMode;
  clear_borders = -1;
  if ((cheat_mode & CHEAT_MODE_FREAKY) != 0)
    szPaletteFile = szCheatPal;
  else
    szPaletteFile = szPal;
  setpal(szPaletteFile);
  FindShades();
  InitRemaps();
  if (player_type == 2) {
    if (!DuoViews[SelectedView[0]])
      viewplus(0);
    if (!DuoViews[SelectedView[1]])
      viewplus(1);
  } else if (SelectedView[0] == 7) {
    iAlternateViewType = (ViewType[0] & 1) != 0 ? ViewType[0] - 1 : ViewType[0] + 1;
    if ((Car[iAlternateViewType].byLives & 0x80u) != 0)
      SelectedView[0] = 0;
  }
  select_view(0);                               // Initialize camera views for each player
  if (Play_View == 1)
    doteaminit();
  else
    initcarview(player1_car, 0);
  if (player_type == 2) {
    select_view(1);
    initcarview(player2_car, 1);
  }
  initnearcars();
  ticks = 0;
  p_eng[0] = &CarEngines.engines[Car[player1_car].byCarDesignIdx];// Setup engine pointers and calculate joystick sensitivity values
  if (player_type == 2)
    p_eng[1] = &CarEngines.engines[Car[player2_car].byCarDesignIdx];
  iTurnRateCalc = p_eng[0]->iMaxTurnRate + 2 * p_eng[0]->iTurnDecayRate;
  iSteeringSensitivity = p_eng[0]->iSteeringSensitivity;
  //TODO look at this, cast to uint32 and bit shift or multiply?
  p_joyk1[0] = ((iSteeringSensitivity * (p_eng[0]->iMaxTurnRate - p_eng[0]->iTurnDecayRate)) * 256) / iTurnRateCalc;
  p_joyk2[0] = ((3 * iSteeringSensitivity * p_eng[0]->iTurnDecayRate) << 16) / iTurnRateCalc;
  if (player_type == 2) {
    iTurnRateCalc2 = p_eng[1]->iMaxTurnRate + 2 * p_eng[1]->iTurnDecayRate;
    iSteeringSensitivity2 = p_eng[1]->iSteeringSensitivity;
    //TODO look at this, cast to uint32 and bit shift or multiply?
    p_joyk1[1] = ((iSteeringSensitivity2 * (p_eng[1]->iMaxTurnRate - p_eng[1]->iTurnDecayRate)) * 256) / iTurnRateCalc2;
    p_joyk2[1] = ((3 * iSteeringSensitivity2 * p_eng[1]->iTurnDecayRate) << 16) / iTurnRateCalc2;
  }
  start_race = -1;
  set_palette(0);
  fadedin = 0;
  totaltrackdistance = 0;
  if (TRAK_LEN > 0)                           // Calculate total track distance by summing all section lengths
  {
    pLocalDataAy = localdata;
    iTrackLen = TRAK_LEN;
    dTotalTrackDistance = (double)totaltrackdistance;
    iChunkIdx = 0;
    do {
      dTotalTrackDistance2 = 2.0 * pLocalDataAy->fTrackHalfLength + dTotalTrackDistance;
      ++pLocalDataAy;
      dTotalTrackDistance2 = floor(dTotalTrackDistance2);//_CHP();
      dTotalTrackDistance = dTotalTrackDistance2;
      ++iChunkIdx;
    } while (iChunkIdx < iTrackLen);
    totaltrackdistance = (int)dTotalTrackDistance2;
  }
  iSavedNetworkMesMode = network_mes_mode;
  averagesectionlen = totaltrackdistance / TRAK_LEN;
  if (winner_mode || game_type == 2)          // Reset car gear and pitch values for winner mode or wrecker races
  {

    for (int i = 0; i < numcars; i++)
    {
        Car[i].byGearAyMax = 0;
        Car[i].iPitchDynamicOffset = 0;
    }
    //if (numcars > 0) {
    //  iCarLoopCounter = 0;
    //  iCarArraySize = 308 * numcars;
    //  do {
    //    iCarLoopCounter += 308;
    //    *((_BYTE *)&CarBox.hitboxAy[14][1].fX + iCarLoopCounter) = 0;// references adjacent data
    //    *(float *)((char *)&CarBox.hitboxAy[14][0].fZ + iCarLoopCounter) = 0.0;// references adjacent data
    //  } while (iCarLoopCounter < iCarArraySize);
    //}

    race_started = -1;
  }
  network_mes_mode = iSavedNetworkMesMode;

  g_pGameRenderer = game_render_create(ROLLERGetGPUDevice(), ROLLERGetWindow());
  {
      MenuRenderer *pMR = GetMenuRenderer();
      if (pMR && menu_render_get_pending_mode(pMR) == MENU_RENDER_GPU)
          game_render_set_mode(g_pGameRenderer, GAME_RENDER_GPU);
  }
  if (intro)
      game_render_set_force_gpu_load(g_pGameRenderer, true);
  game_render_set_texture_filter(g_pGameRenderer, g_iTextureFilter);
  game_render_set_anisotropy_level(g_pGameRenderer, g_iAnisotropyLevel);
  game_render_set_trilinear(g_pGameRenderer, g_bTrilinear);
  game_render_set_lod_bias(g_pGameRenderer, g_fLodBias);
  game_render_set_render_scale(g_pGameRenderer, g_fRenderScale);
  game_render_set_fog_density(g_pGameRenderer, g_fFogDensity);
  game_render_set_fog_color(g_pGameRenderer,
      ((g_uFogColor >> 16) & 0xFF) / 255.0f,
      ((g_uFogColor >>  8) & 0xFF) / 255.0f,
      ( g_uFogColor        & 0xFF) / 255.0f);
  game_render_set_gamma(g_pGameRenderer, g_fGamma);
  game_render_set_fog_start(g_pGameRenderer, g_fFogStart);
  game_render_set_saturation(g_pGameRenderer, g_fSaturation);
  game_render_set_contrast(g_pGameRenderer, g_fContrast);
  game_render_set_vignette(g_pGameRenderer, g_fVigStrength);
  game_render_set_brightness(g_pGameRenderer, g_fBrightness);
  game_render_set_fov_multiplier(g_pGameRenderer, g_fFovMultiplier);
  game_render_set_emulate_software_track_borders(g_pGameRenderer, g_bEmulateSoftwareTrackBorders);
  game_render_set_antialiasing(g_pGameRenderer, g_iAntiAliasing);
  game_render_set_vsync(g_pGameRenderer, g_bVsync);

  // Register already-loaded pixel textures with the renderer.
  // These were loaded before the renderer existed, so the NULL-guarded
  // calls in graphics.c were skipped.
  if (texture_vga) {
    int texH = gfx_size ? ((NoOfTextures + 7) / 8) * 32
                        : ((NoOfTextures + 3) / 4) * 64;
    game_render_load_texture(g_pGameRenderer, texture_vga, 256, texH,
                              0, gfx_size);
  }
  if (building_vga) {
    int bldH = gfx_size ? ((BldTextures + 7) / 8) * 32
                        : ((BldTextures + 3) / 4) * 64;
    game_render_load_texture(g_pGameRenderer, building_vga, 256, bldH,
                              17, gfx_size);
  }
  if (cargen_vga) {
    int cgH = gfx_size ? ((num_textures[18] + 7) / 8) * 32
                       : ((num_textures[18] + 3) / 4) * 64;
    game_render_load_texture(g_pGameRenderer, cargen_vga, 256, cgH,
                              18, gfx_size);
  }
  for (int i = 0; i < 16; i++) {
    if (cartex_vga[i]) {
      int half = gfx_size ? 1 : 0;
      int cH = half ? ((num_textures[i] + 7) / 8) * 32
                    : ((num_textures[i] + 3) / 4) * 64;
      game_render_load_texture(g_pGameRenderer, cartex_vga[i], 256, cH,
                                i + 1, half);
    }
  }

  // Register HUD sprite blocks (rev_vga) with the game renderer.
  // Slot 0: minitext font, 1: font6, 2: panel2 HUD sprites,
  // 3: font3 zoom text, 4: pancar life/kill icons.
  for (int i = 0; i < 5; i++) {
    if (rev_vga[i])
      game_render_load_blocks(g_pGameRenderer, i, rev_vga[i], pal_addr);
  }
}

//-------------------------------------------------------------------------------------------------
//00012930
void play_game_uninit()
{
  int iReplayType; // edx
  int iView; // eax
  //int iMaxOffset; // ebx
  //unsigned int iOffset; // eax

  g_bNoclip = false;
  noclip_camera_set_input_enabled(false);
  noclip_camera_reset();

  if (g_pGameRenderer) {
    game_render_destroy(g_pGameRenderer);
    g_pGameRenderer = NULL;
  }

  //_disable();
  if (!winner_mode && replaytype != 2 && network_on) {
    name_copy(player_names[0], player_names[wConsoleNode]);
    manual_control[0] = manual_control[wConsoleNode];
    player_invul[0] = player_invul[wConsoleNode];
    Players_Cars[0] = Players_Cars[wConsoleNode];
    wConsoleNode = 0;
  }
  frontend_on = -1;
  //_enable();
  iReplayType = replaytype;
  if (replaytype == 2)
    cheat_mode = replay_cheat;
  // CHEAT_MODE_DEATH_MODE
  if ((cheat_mode & CHEAT_MODE_DEATH_MODE) != 0) {
    level = game_level;
    damage_level = game_dam;
  }
  if (replaytype != 2 && !winner_mode) {
    game_size = scr_size;
    game_svga = SVGA_ON;
    iView = DeathView[0];
    if (DeathView[0] < 0)
      iView = SelectedView[0];
    game_view[0] = iView;
    if (DeathView[1] < 0)
      game_view[1] = SelectedView[1];
    else
      game_view[1] = DeathView[1];
    result_p1 = player1_car;
    if (player_type == 2)
      result_p2 = player2_car;
  }
  if (replaytype == 2) {
    // TEX_OFF_WIDESCREEN
    if ((textures_off & TEX_OFF_WIDESCREEN) != 0) {
      // TEX_OFF_WIDESCREEN
      textures_off ^= TEX_OFF_WIDESCREEN;
      // CHEAT_MODE_WIDESCREEN
      cheat_mode |= CHEAT_MODE_WIDESCREEN;
    }
    player_type = replay_player;
  }
  stopreplay();
  tick_on = 0;
  if (network_error == 666) {
    network_buggered = 666;
  } else if (network_sync_error) {
    network_buggered = 456;
  } else {
    network_buggered = network_error;
  }
  network_error = 0;
  network_sync_error = 0;
  set_black_palette_now();
  if (!loading_replay)
    stopmusic();
  stopallsamples();
  releasesamples();
  free_game_memory();
  if (iReplayType == 2) {
    network_buggered = 0;
    if (numcars > 0) {

      for (int i = 0; i < numcars; ++i) {
        non_competitors[i] = result_competing[i];
      }
      //iMaxOffset = 4 * numcars;
      //iOffset = 0;
      //do {
      //  iOffset += 4;
      //  TrackArrow_variable_1[iOffset / 4] = LODWORD(result_best[iOffset / 4 + 15]);// references adjacent data
      //} while ((int)iOffset < iMaxOffset);
    }
  } else {
    if (!g_bSnapshotMode) SaveRecords();
    save_fatal_config();
  }
  if (no_mem) {
    fre((void **)&scrbuf);
    scrbuf = (uint8 *)getbuffer(256000u);
  }
  tick_on = -1;
}

//-------------------------------------------------------------------------------------------------
//00013A10
void game_keys()
{
  unsigned int uiKeyCode; // eax
  int iExtendedKey; // eax
  int iGameReqState; // eax
  int iProcessedKey; // eax
  bool bToggleState; // zf
  //uint32 uiTextureFlags1; // eax
  //uint32 uiTextureFlags2; // eax
  //uint32 uiTextureFlags3; // ecx
  //uint32 uiTextureFlags4; // eax

  if (define_mode)
    EXIT_FUNCTION:
  return;
  iProcessedKey = 0;
  //JUMPOUT(0x1381B);
  while (1) {
    do {
      while (1) {
        do {
          while (1) {
            while (1) {
              while (1) {
              PROCESS_NEXT_KEY:
                if (iProcessedKey)
                  goto EXIT_FUNCTION;
                iProcessedKey = -1;
                if (!fatkbhit())
                  goto EXIT_FUNCTION;               // Check if a key is available in the keyboard buffer
                uiKeyCode = fatgetch();         // Get the key code from the keyboard buffer
                if (intro || winner_mode)     // Handle intro/winner mode - any key exits these modes
                {
                  if (uiKeyCode) {
                    uiKeyCode = -1;
                    racing = 0;
                  } else {
                    fatgetch();
                    uiKeyCode = -1;
                    racing = 0;
                  }
                }
                if (trying_to_exit)           // Handle exit confirmation (Y/N prompt)
                {
                  if (uiKeyCode) {                             // Check for 'Y' or 'y' to confirm exit (ASCII 121='y', 89='Y')
                    if (uiKeyCode == 121 || uiKeyCode == 89) {
                      racing = 0;
                      quit_game = -1;
                      scr_size = req_size;
                    }
                    trying_to_exit = 0;
                  } else {
                    trying_to_exit = 0;
                    fatgetch();
                  }
                  uiKeyCode = -1;
                }
                if (uiKeyCode)
                  break;
                iExtendedKey = fatgetch();      // Process extended keys (arrow keys, function keys)
                if (iExtendedKey != 75 && iExtendedKey != 77 && iExtendedKey != 72 && iExtendedKey != 80)// Check for arrow keys (75=Left, 77=Right, 72=Up, 80=Down)
                {                               // Apply Shift modifier (keys[42]=LShift, keys[54]=RShift, adds 25)
                  if (keys[WHIP_SCANCODE_LSHIFT] || keys[WHIP_SCANCODE_RSHIFT]) {
                    iExtendedKey += 25;
                  } else if (keys[WHIP_SCANCODE_LALT])          // Apply Alt modifier (keys[56]=Alt, adds 45)
                  {
                    iExtendedKey += 45;
                  }
                }
                switch (iExtendedKey) {
                  case WHIP_SCANCODE_F1:
                    if (network_on)           // F1 (0x3B) - Network message controls / Replay car selection
                      mesminus();
                    if (replaytype == 2)
                      carminus();
                    break;
                  case WHIP_SCANCODE_F2:
                    if (network_on)           // F2 (0x3C) - Network message controls / Replay car selection
                      mesplus();
                    if (replaytype == 2)
                      carplus();
                    break;
                  case WHIP_SCANCODE_F3:
                    if (view0_cnt < 0)        // F3 (0x3D) - Previous view for player 1
                    {
                      view0_cnt = 18;
                      viewminus(0);
                    }
                    break;
                  case WHIP_SCANCODE_F4:
                    if (view0_cnt < 0)        // F4 (0x3E) - Next view for player 1
                    {
                      view0_cnt = 18;
                      viewplus(0);
                    }
                    break;
                  case WHIP_SCANCODE_F7:
                    if (player_type == 2 && view1_cnt < 0) {
                      view1_cnt = 18;
                      viewminus(1);
                    }
                    break;
                  case WHIP_SCANCODE_F8:
                    if (player_type == 2 && view1_cnt < 0) {
                      view1_cnt = 18;
                      viewplus(1);
                    }
                    break;
                  case WHIP_SCANCODE_F9:
                    if (++names_on > 2)       // F9 (0x43) - Toggle player names display (0=off, 1=on, 2=detailed)
                      names_on = 0;
                    break;
                  case WHIP_SCANCODE_F10:
                    if (I_Would_Like_To_Quit && Quit_Count <= 0)// F10 (0x44) - Quit game (if quit sequence active)
                    {
                      I_Want_Out = -1;
                      stopallsamples();
                    }
                    break;
                  case WHIP_SCANCODE_F12:
                    showversion = showversion == 0;// F12 (0x46) - Toggle version display
                    break;
                  case WHIP_SCANCODE_UP:
                    if (game_req) {
                      if (!pausewindow && req_edit > 0) {
                        --req_edit;
                        if (req_edit == 3)
                          --req_edit;
                      }
                      if (pausewindow == 2 && control_select < 4)
                        ++control_select;
                      if (pausewindow == 3 && graphic_mode < 16)
                        ++graphic_mode;
                      if (pausewindow == 4) {
                        if (sound_edit <= 1) {
                          if (!sound_edit)
                            sound_edit = 7;
                        } else {
                          --sound_edit;
                        }
                      }
                    } else if (replaytype == 2 && game_req != replaypanel) {
                      switch (game_req + controlicon) {
                        case 7:
                        case 8:
                        case 15:
                        case 16:
                        case 17:
                          controlicon -= 6;
                          break;
                        case 9:
                        case 10:
                        case 11:
                        case 12:
                        case 13:
                        case 14:
                          controlicon -= 5;
                          break;
                        default:
                          continue;
                      }
                    }
                    break;
                  case WHIP_SCANCODE_LEFT:
                    if (game_req) {
                      if (pausewindow == 4) {
                        switch (sound_edit) {
                          case 1:
                            EngineVolume -= pausewindow;// Left Arrow - Decrease Engine Volume
                            if (EngineVolume < 0)
                              EngineVolume = 0;
                            continue;
                          case 2:
                            SFXVolume -= pausewindow;// Left Arrow - Decrease SFX Volume
                            if (SFXVolume < 0)
                              SFXVolume = 0;
                            continue;
                          case 3:
                            SpeechVolume -= pausewindow;// Left Arrow - Decrease Speech Volume
                            if (SpeechVolume < 0)
                              SpeechVolume = 0;
                            continue;
                          case 4:
                            MusicVolume -= pausewindow;// Left Arrow - Decrease Music Volume
                            if (MusicVolume < 0)
                              MusicVolume = 0;
                            MusicSetMasterVolume(MusicVolume);
                            continue;
                          default:
                            continue;
                        }
                      }
                    } else if (replaytype == 2 && game_req != replaypanel && controlicon != 1 && controlicon != 7 && controlicon != 12) {
                      --controlicon;
                    }
                    break;
                  case WHIP_SCANCODE_RIGHT:
                    iGameReqState = game_req;
                    if (game_req) {
                      if (pausewindow == 4) {
                        switch (sound_edit) {
                          case 1:
                            EngineVolume += 4;
                            if (EngineVolume >= 128)
                              EngineVolume = 127;
                            break;
                          case 2:
                            SFXVolume += pausewindow;
                            if (SFXVolume >= 128)
                              SFXVolume = 127;
                            break;
                          case 3:
                            SpeechVolume += pausewindow;
                            if (SpeechVolume >= 128)
                              SpeechVolume = 127;
                            break;
                          case 4:
                            MusicVolume += pausewindow;
                            if (MusicVolume >= 128)
                              MusicVolume = 127;
                            MusicSetMasterVolume(MusicVolume);
                            break;
                          default:
                            continue;
                        }
                      }
                    } else if (replaytype == 2 && game_req != replaypanel && controlicon != 6 && controlicon != 11 && controlicon != 25) {
                      if (controlicon != 17 || game_req != replayedit)
                        iGameReqState = 1;
                      if (iGameReqState)
                        ++controlicon;
                    }
                    break;
                  case WHIP_SCANCODE_DOWN:
                    if (game_req) {
                      if (!pausewindow && req_edit < 6) {
                        ++req_edit;
                        if (req_edit == 3)
                          ++req_edit;
                      }
                      if (pausewindow == 2 && control_select > 0)
                        --control_select;
                      if (pausewindow == 3 && graphic_mode > 0)
                        --graphic_mode;
                      if (pausewindow == 4 && sound_edit > 0) {
                        if (sound_edit >= 7)
                          sound_edit = 0;
                        else
                          ++sound_edit;
                      }
                    } else if (replaytype == 2 && game_req != replaypanel) {
                      switch (controlicon) {
                        case 1:
                        case 2:
                        case 3:
                        case 9:
                        case 10:
                        case 11:
                          controlicon += 6;
                          break;
                        case 4:
                        case 5:
                        case 6:
                        case 7:
                        case 8:
                          controlicon += 5;
                          break;
                        default:
                          continue;
                      }
                    }
                    break;
                  case 0x54:
                    controlicon = 9;
                    Rreverseplay();
                    break;
                  case 0x55:
                    controlicon = 9;
                    Rplay();
                    break;
                  case 0x56:
                    controlicon = 9;
                    Rspeedminus();
                    break;
                  case 0x57:
                    controlicon = 9;
                    Rspeedplus();
                    break;
                  case 0x58:
                    controlicon = 9;
                    Rframeminus();
                    break;
                  case 0x59:
                    controlicon = 9;
                    Rframeplus();
                    break;
                  case 0x5C:
                    controlicon = 9;
                    Rstart();
                    break;
                  case 0x5D:
                    controlicon = 9;
                    Rend();
                    break;
                  case 0x68:
                    controlicon = 9;
                    rtoggleedit();
                    break;
                  case 0x69:
                    controlicon = 9;
                    rstartblock();
                    break;
                  case 0x6A:
                    controlicon = 9;
                    rselectblock();
                    break;
                  case 0x6B:
                    controlicon = 9;
                    rdeleteblock();
                    break;
                  case 0x6C:
                    controlicon = 9;
                    rstoreview();
                    break;
                  case 0x6D:
                    controlicon = 9;
                    rremoveview();
                    break;
                  case 0x6E:
                    controlicon = 9;
                    rpreviouscut();
                    break;
                  case 0x6F:
                    controlicon = 9;
                    rnextcut();
                    break;
                  case 0x70:
                    controlicon = 9;
                    rstartassemble();
                    break;
                  default:
                    continue;
                }
              }
              if (uiKeyCode < 0x2D)
                break;
              if (uiKeyCode <= 0x2D) {                                 // '-' key (0x2D) - Decrease screen size
              DECREASE_SCREEN_SIZE:
                if (replaytype != 2) {
                  if (game_req) {
                    if (pausewindow == 3 && graphic_mode == 2) {
                      if (SVGA_ON) {
                        req_size -= 16;
                        if (req_size < 64)
                          req_size = 64;
                      } else {
                        req_size -= 8;
                        if (req_size < 32)
                          req_size = 32;
                      }
                    }
                  } else if (SVGA_ON) {
                    scr_size -= 16;
                    if (scr_size >= 64)
                      goto SET_CLEAR_BORDERS;
                    scr_size = 64;
                  } else {
                    scr_size -= 8;
                    if (scr_size >= 32)
                      SET_CLEAR_BORDERS:
                    clear_borders = -1;
                    else
                      scr_size = 32;
                  }
                }
              } else if (uiKeyCode < 0x4C) {
                if (uiKeyCode >= 0x3D) {
                  if (uiKeyCode <= 0x3D) {                             // '+' key (0x3D) - Increase screen size
                  INCREASE_SCREEN_SIZE:
                    if (replaytype != 2) {
                      if (game_req) {
                        if (pausewindow == 3 && graphic_mode == 2) {
                          if (SVGA_ON) {
                            req_size += 16;
                            if (req_size > 128)
                              req_size = 128;
                            if (req_size == 128)
                              replaypanel = -1;
                          } else {
                            req_size += 8;
                            if (req_size > 64)
                              req_size = 64;
                            if (req_size == 64)
                              replaypanel = -1;
                          }
                        }
                      } else if (SVGA_ON) {
                        scr_size += 16;
                        if (scr_size > 128)
                          scr_size = 128;
                        if (scr_size == 128)
                          replaypanel = -1;
                      } else {
                        scr_size += 8;
                        if (scr_size > 64)
                          scr_size = 64;
                        if (scr_size == 64)
                          replaypanel = -1;
                      }
                    }
                  } else if (uiKeyCode == 68) {
                    if (keys[WHIP_SCANCODE_LALT]) {
                      controlicon = 9;
                      if (replaytype == 2)
                        filingmenu = 3;
                    }
                  }
                }
              } else if (uiKeyCode <= 0x4C) {
                if (keys[WHIP_SCANCODE_LALT]) {
                  controlicon = 9;
                  if (replaytype == 2)
                    filingmenu = 1;
                }
              } else if (uiKeyCode >= 0x53) {
                if (uiKeyCode <= 0x53) {
                  if (keys[WHIP_SCANCODE_LALT]) {
                    controlicon = 9;
                    if (replaytype == 2)
                      filingmenu = 2;
                  }
                } else if (uiKeyCode == 95) {
                  goto DECREASE_SCREEN_SIZE;
                }
              }
            }
            if (uiKeyCode < 0x1B)
              break;
            if (uiKeyCode <= 0x1B) {                                   // Escape key - Exit pause menus
              if (game_req && pausewindow) {
                pausewindow = 0;
              } else if (filingmenu) {
                filingmenu = 0;
                lastfile = 0;
              } else if (!network_on || replaytype == 2) {
              REQUEST_PAUSE:
                pause_request = -1;
              } else if (active_nodes == network_on) {
                if (I_Would_Like_To_Quit == -1) {
                  if (Quit_Count <= 0)
                    I_Would_Like_To_Quit = 0;
                } else {
                  I_Would_Like_To_Quit = -1;
                  Quit_Count = 18;
                }
              }
            } else if (uiKeyCode >= 0x20) {
              if (uiKeyCode <= 0x20) {
                if (replaytype == 2) {
                  replaypanel = replaypanel == 0;
                  controlicon = 9;
                  rotpoint = currentreplayframe;
                }
              } else if (uiKeyCode == 43) {
                goto INCREASE_SCREEN_SIZE;
              }
            }
          }
        } while (uiKeyCode != 13);
        if (!game_req)
          break;
        switch (pausewindow) {
          case 0:
            switch (req_edit) {
              case 0:
                goto REQUEST_PAUSE;
              case 1:
                paused = 0;
                racing = 0;
                gave_up = -1;
                scr_size = req_size;
                goto PROCESS_NEXT_KEY;
              case 2:
                sound_edit = 0;
                pausewindow = 4;
                goto PROCESS_NEXT_KEY;
              case 3:
                pausewindow = 2;
                controlrelease = -1;
                define_mode = 0;
                control_select = 0;
                control_edit = -1;
                goto PROCESS_NEXT_KEY;
              case 4:
                pausewindow = 2;
                controlrelease = -1;
                define_mode = 0;
                control_select = 0;
                control_edit = -1;
                goto PROCESS_NEXT_KEY;
              case 5:
                graphic_mode = 0;
                pausewindow = 3;
                goto PROCESS_NEXT_KEY;
              case 6:
                trying_to_exit = -1;
                goto PROCESS_NEXT_KEY;
              default:
                goto PROCESS_NEXT_KEY;
            }
          case 1:
            pausewindow = 2;
            calibrate_mode = 0;
            control_select = 0;
            control_edit = -1;
            define_mode = 0;
            break;
          case 2:
            if (control_select) {
              if ((unsigned int)control_select <= 2) {
                define_mode = control_select == 1 ? -2 : -1;
                control_edit = 0;
                disable_keyboard();
                controlrelease = -1;
                memcpy(oldkeys, userkey, 0xCu);
                memcpy(&oldkeys[12], &userkey[12], 2u);
                InputBackupBindings();
                InputCaptureBegin();
              } else if (control_select == 3 || control_select == 4) {
                define_mode = control_select == 3 ? -2 : -1;
                control_edit = 6;
                disable_keyboard();
                controlrelease = -1;
                memcpy(oldkeys, userkey, 0xCu);
                memcpy(&oldkeys[12], &userkey[12], 2u);
                InputBackupBindings();
                InputCaptureBegin();
              }
            } else {
            EXIT_PAUSE_MENU:
              pausewindow = 0;
            }
            break;
          case 3:
            switch (graphic_mode) {
              case 0:
                goto EXIT_PAUSE_MENU;
              case 1:
                if (svga_possible && !no_mem || SVGA_ON) {
                  SVGA_ON = SVGA_ON == 0;
                  init_screen();
                  req_size = scr_size;
                }
                goto PROCESS_NEXT_KEY;
              case 2:
                if (replaytype != 2) {
                  if (SVGA_ON) {
                    req_size += 16;
                    if (req_size > 128)
                      req_size = 64;
                    if (req_size == 128)
                      replaypanel = -1;
                  } else {
                    req_size += 8;
                    if (req_size > 64)
                      req_size = 32;
                    if (req_size == 64)
                      replaypanel = -1;
                  }
                }
                goto PROCESS_NEXT_KEY;
              case 3:
                if (view_limit) {
                  view_limit = 0;
                } else if (machine_speed >= 2800) {
                  view_limit = 32;
                } else {
                  view_limit = 24;
                }
                goto PROCESS_NEXT_KEY;
              case 4:
                // 0x20 = TEX_OFF_PANEL_OFF
                if ((textures_off & TEX_OFF_PANEL_OFF) != 0)// Panel textures toggle: 0x20=TEX_OFF_PANEL_OFF, 0x40000=TEX_OFF_PANEL_RESTRICTED
                {
                  //uiTextureFlags1 = textures_off;
                  //LOBYTE(uiTextureFlags1) = textures_off ^ 0x20;
                  //textures_off = uiTextureFlags1 | 0x40000;
                  textures_off ^= TEX_OFF_PANEL_OFF;
                  textures_off |= TEX_OFF_PANEL_RESTRICTED;
                }
                // 0x40000 = TEX_OFF_PANEL_RESTRICTED
                else if ((textures_off & TEX_OFF_PANEL_RESTRICTED) != 0) {
                  textures_off ^= TEX_OFF_PANEL_RESTRICTED;
                } else {
                  textures_off |= TEX_OFF_PANEL_OFF;
                }
                goto PROCESS_NEXT_KEY;
              case 5:
                // 0x8 = TEX_OFF_CLOUDS
                textures_off ^= TEX_OFF_CLOUDS;             // Toggle clouds textures (0x8 = TEX_OFF_CLOUDS)
                goto PROCESS_NEXT_KEY;
              case 6:
                // 0x100 = TEX_OFF_SHADOWS
                textures_off ^= TEX_OFF_SHADOWS;         // Toggle shadows (0x100 = TEX_OFF_SHADOWS)
                goto PROCESS_NEXT_KEY;
              case 7:
                // 0x2 = TEX_OFF_ROAD_TEXTURES
                textures_off ^= TEX_OFF_ROAD_TEXTURES;
                goto PROCESS_NEXT_KEY;
              case 8:
                // 0x80 = TEX_OFF_BUILDING_TEXTURES
                //uiTextureFlags2 = textures_off;
                //LOBYTE(uiTextureFlags2) = textures_off ^ 0x80;
                //textures_off = uiTextureFlags2;
                textures_off ^= TEX_OFF_BUILDING_TEXTURES;
                goto PROCESS_NEXT_KEY;
              case 9:
                // 0x1 = TEX_OFF_GROUND_TEXTURES
                textures_off ^= TEX_OFF_GROUND_TEXTURES;
                goto PROCESS_NEXT_KEY;
              case 10:
                // 0x4 = TEX_OFF_WALL_TEXTURES
                textures_off ^= TEX_OFF_WALL_TEXTURES;
                goto PROCESS_NEXT_KEY;
              case 11:
                //uiTextureFlags3 = textures_off;
                //// 0x40 = TEX_OFF_CAR_TEXTURES
                //LOBYTE(uiTextureFlags3) = textures_off ^ 0x40;
                //textures_off = uiTextureFlags3;
                textures_off ^= TEX_OFF_CAR_TEXTURES;
                goto PROCESS_NEXT_KEY;
              case 12:
                //uiTextureFlags4 = textures_off;
                //// 0x10 = TEX_OFF_HORIZON
                //LOBYTE(uiTextureFlags4) = textures_off ^ 0x10;
                //textures_off = uiTextureFlags4;
                textures_off ^= TEX_OFF_HORIZON;
                goto PROCESS_NEXT_KEY;
              case 13:
                // 0x800 = TEX_OFF_GLASS_WALLS
                textures_off ^= TEX_OFF_GLASS_WALLS;
                goto PROCESS_NEXT_KEY;
              case 14:
                // 0x200 = TEX_OFF_BUILDINGS
                textures_off ^= TEX_OFF_BUILDINGS;
                goto PROCESS_NEXT_KEY;
              case 15:
                if (++names_on > 2)
                  names_on = 0;
                goto PROCESS_NEXT_KEY;
              case 16:
                // 0x80000 = TEX_OFF_PERSPECTIVE_CORRECTION
                textures_off ^= TEX_OFF_PERSPECTIVE_CORRECTION;
                goto PROCESS_NEXT_KEY;
              default:
                goto PROCESS_NEXT_KEY;
            }
          case 4:
            switch (sound_edit) {
              case 0:
                goto EXIT_PAUSE_MENU;
              case 5:
                allengines = allengines == 0;
                goto PROCESS_NEXT_KEY;
              case 6:
                if (SoundCard) {
                  bToggleState = soundon != 0;
                  soundon = soundon == 0;
                  if (!bToggleState)
                    loadsamples();
                } else {
                  soundon = 0;
                }
                goto PROCESS_NEXT_KEY;
              case 7:
                if (MusicBackendAvailable()) {
                  musicon = musicon == 0;
                  reinitmusic();
                } else {
                  musicon = 0;
                }
                goto PROCESS_NEXT_KEY;
              default:
                goto PROCESS_NEXT_KEY;
            }
          default:
            goto PROCESS_NEXT_KEY;
        }
      }
    } while (replaytype != 2 || game_req == replaypanel && controlicon != 9 || !ricon[controlicon].pFunc);
    if ((unsigned int)controlicon < 0xE) {
    EXECUTE_REPLAY_FN:
      //TODO
      ((void (*)(void))ricon[controlicon].pFunc)();
    } else if ((unsigned int)controlicon <= 0xE) {
      viewminus(0);
    } else {
      if (controlicon != 15)
        goto EXECUTE_REPLAY_FN;
      viewplus(0);
    }
  }
}

//-------------------------------------------------------------------------------------------------
//00014B80
void mesminus()
{
  int iNewMesMode; // ecx
  int iMesModeMinus; // eax
  int iMesModeComp; // edx

  iNewMesMode = network_mes_mode;
  iMesModeMinus = network_mes_mode;
  iMesModeComp = network_mes_mode;
  do {
    if (iMesModeMinus < 0)
      iMesModeMinus = numcars;
    if (--iMesModeMinus == team_mate || iMesModeMinus < 0 && players > 2 || human_control[iMesModeMinus] && iMesModeMinus != player1_car) {
      iMesModeComp = -666;
      iNewMesMode = iMesModeMinus;
    }
  } while (iMesModeComp == iNewMesMode);
  network_mes_mode = iNewMesMode;
}

//-------------------------------------------------------------------------------------------------
//00014BF0
void mesplus()
{
  int iNewMesMode; // ecx
  int iMesModePlus; // eax
  int iMesModeComp; // edx

  iNewMesMode = network_mes_mode;
  iMesModePlus = network_mes_mode;
  iMesModeComp = network_mes_mode;
  do {
    if (++iMesModePlus >= numcars)
      iMesModePlus = -1;
    if (iMesModePlus == team_mate || iMesModePlus < 0 && players > 2 || human_control[iMesModePlus] && iMesModePlus != player1_car) {
      iMesModeComp = -666;
      iNewMesMode = iMesModePlus;
    }
  } while (iMesModeComp == iNewMesMode);
  network_mes_mode = iNewMesMode;
}

//-------------------------------------------------------------------------------------------------
//00014C60
void carminus()
{
  do {
    if (--ViewType[0] < 0)
      ViewType[0] = numcars - 1;
  } while ((Car[ViewType[0]].byLives & 0x80u) != 0);
  if (Play_View == 1)
    doteaminit();
  else
    initcarview(ViewType[0], 0);
  sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);                        // SOUND_SAMPLE_BUTTON
}

//-------------------------------------------------------------------------------------------------
//00014CD0
void carplus()
{
  do {
    if (++ViewType[0] >= numcars)
      ViewType[0] = 0;
  } while ((Car[ViewType[0]].byLives & 0x80u) != 0);
  if (Play_View == 1)
    doteaminit();
  else
    initcarview(ViewType[0], 0);
  sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);                        // SOUND_SAMPLE_BUTTON
}

//-------------------------------------------------------------------------------------------------
//00014D50
void viewminus(int iPlayer)
{
  int iPlayerIndex; // eax
  int iViewIndex; // edx
  int iPrevView; // ebx
  int iOtherPlayer; // ebx
  int iPrevViewSingle; // edi
  int iOtherPlayerSingle; // ebx
  int iPrevViewDuo; // esi

  iPlayerIndex = iPlayer;
  iViewIndex = iPlayer;                         // Check if we're in replay mode (replaytype == 2)
  if (replaytype != 2) {
    if ((Car[ViewType[iPlayerIndex]].byStatusFlags & 4) != 0)
      return;                                   // Don't change view if current car is active (bit 2 set in status flags)
    if (player_type == 2) {
      do {
        iPrevViewDuo = SelectedView[iViewIndex] - 1;
        SelectedView[iViewIndex] = iPrevViewDuo;

        // Wrap around to view 8 if we've gone below view 0
        if (iPrevViewDuo < 0)
          SelectedView[iViewIndex] = 8;
      } while (!DuoViews[SelectedView[iViewIndex]]);// Two-player mode: cycle backwards through views allowed for duo play
    } else {
      do {
        // Single player mode: cycle backwards through allowed views
        iPrevViewSingle = SelectedView[iViewIndex] - 1;
        SelectedView[iViewIndex] = iPrevViewSingle;
        if (iPrevViewSingle == 7) {
          iOtherPlayerSingle = (ViewType[iPlayerIndex] & 1) != 0 ? ViewType[iPlayerIndex] - 1 : ViewType[iPlayerIndex] + 1;
          // View 7 special case: check if other player's car is dead (bit 7 set)
          if ((Car[iOtherPlayerSingle].byLives & 0x80u) != 0)
            --SelectedView[iViewIndex];
        }
        if (SelectedView[iViewIndex] < 0)
          SelectedView[iViewIndex] = 8;
      } while (!AllowedViews[SelectedView[iViewIndex]]);
    }
    select_view(iPlayer);
    if (Play_View != 1)
      goto INIT_CAR_VIEW;
  INIT_TEAM_VIEW:
      // If team view (Play_View == 1), initialize team display
    doteaminit();
    goto PLAY_BUTTON_SOUND;
  }
  do {
    // Replay mode: cycle backwards through replay-allowed views
    iPrevView = SelectedView[iViewIndex] - 1;
    SelectedView[iViewIndex] = iPrevView;
    if (iPrevView == 7) {
      iOtherPlayer = (ViewType[iPlayerIndex] & 1) != 0 ? ViewType[iPlayerIndex] - 1 : ViewType[iPlayerIndex] + 1;

      // Replay mode view 7: skip if other player is dead
      if ((Car[iOtherPlayer].byLives & 0x80u) != 0)
        --SelectedView[iViewIndex];
    }
    if (SelectedView[iViewIndex] < 0)
      SelectedView[iViewIndex] = 8;
  } while (!ReplayViews[SelectedView[iViewIndex]]);

  // Apply the selected view
  select_view(iPlayer);
  if (Play_View == 1)
    goto INIT_TEAM_VIEW;
INIT_CAR_VIEW:
  initcarview(ViewType[iPlayer], iPlayer);      // Initialize car view for non-team modes
PLAY_BUTTON_SOUND:
  sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);                        // SOUND_SAMPLE_BUTTON
}

//-------------------------------------------------------------------------------------------------
//00014F20
void viewplus(int iPlayer)
{
  int iPlayerIndex; // eax
  int iViewIndex; // edx
  int iNextView; // ebx
  int iOtherPlayer; // ebx
  int iNextViewDuo; // ebx
  int iOtherPlayerDuo; // ebx
  int iSelectedViewDuo; // esi
  int iPlayer_1; // [esp+0h] [ebp-1Ch]

  iPlayer_1 = iPlayer;
  iPlayerIndex = iPlayer;
  iViewIndex = iPlayer;                         // Check if we're in replay mode (replaytype == 2)
  if (replaytype != 2) {
    if ((Car[ViewType[iPlayerIndex]].byStatusFlags & 4) != 0)// CAR_STATUS_ACTIVE
      return;                                   // Don't change view if current car is active (bit 2 set in status flags)
    if (player_type == 2) {
      do {
        iSelectedViewDuo = SelectedView[iViewIndex] + 1;
        SelectedView[iViewIndex] = iSelectedViewDuo;
        if (iSelectedViewDuo >= 9)            // Wrap around to view 0 if we've gone past view 8
          SelectedView[iViewIndex] = 0;
      } while (!DuoViews[SelectedView[iViewIndex]]);// Two-player mode: cycle through views allowed for duo play
    } else {
      do {
        // Single player mode: cycle through allowed views
        iNextViewDuo = SelectedView[iViewIndex] + 1;
        SelectedView[iViewIndex] = iNextViewDuo;
        if (iNextViewDuo == 7) {
          iOtherPlayerDuo = (ViewType[iPlayerIndex] & 1) != 0 ? ViewType[iPlayerIndex] - 1 : ViewType[iPlayerIndex] + 1;
          // View 7 special case: check if other player's car is dead (bit 7 set)
          if ((Car[iOtherPlayerDuo].byLives & 0x80u) != 0)
            ++SelectedView[iViewIndex];
        }
        if (SelectedView[iViewIndex] >= 9)
          SelectedView[iViewIndex] = 0;
      } while (!AllowedViews[SelectedView[iViewIndex]]);
    }

    // Apply the selected view
    select_view(iPlayer_1);
    if (Play_View != 1)
      goto INIT_CAR_VIEW;
  INIT_TEAM_VIEW:
      // If team view (Play_View == 1), initialize team display
    doteaminit();
    goto PLAY_BUTTON_SOUND;
  }
  do {
    iNextView = SelectedView[iViewIndex] + 1;   // Replay mode: cycle through replay-allowed views
    SelectedView[iViewIndex] = iNextView;
    if (iNextView == 7) {
      iOtherPlayer = (ViewType[iPlayerIndex] & 1) != 0 ? ViewType[iPlayerIndex] - 1 : ViewType[iPlayerIndex] + 1;
      if ((Car[iOtherPlayer].byLives & 0x80u) != 0)// Replay mode view 7: skip if other player is dead
        ++SelectedView[iViewIndex];
    }
    if (SelectedView[iViewIndex] >= 9)
      SelectedView[iViewIndex] = 0;
  } while (!ReplayViews[SelectedView[iViewIndex]]);
  select_view(iPlayer_1);
  if (Play_View == 1)
    goto INIT_TEAM_VIEW;
INIT_CAR_VIEW:
  initcarview(ViewType[iPlayer_1], iPlayer_1);  // Initialize car view for non-team modes
PLAY_BUTTON_SOUND:
  sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);                        // SOUND_SAMPLE_BUTTON
}

//-------------------------------------------------------------------------------------------------
//00015120
void game_copypic(uint8 *pSrc, uint8 *pDest, int iCarIdx)
{
  int iBufferIndex; // eax
  char byChar; // dh
  int j; // edx
  char byNameChar; // bl
  int k; // eax
  int i; // eax
  int iSoundSample; // eax
  int iPlayerCar; // ecx
  const char *pMessageStr; // eax
  int iKillCount; // ecx
  int iPlayerCarForSound; // ecx
  int iKillSample; // eax
  int iSoundSampleForWinner; // eax
  int iWinnerKillCount; // edx
  int iWinnerPlayerCar; // ecx
  int iWinnerSoundSample; // eax
  int iSavedScrSize; // edi
  int iSavedScrSize2; // edi
  int iSavedScrSize3; // edi
  char *pQuitMessage; // edx
  int iFrameCount; // ebx

  screen_pointer = pSrc;                        // Set global screen pointer to source image buffer
  if (network_on)                             // Handle network messaging if multiplayer is enabled
  {                                             // Check if we have received a network message
    if (message_received < 0) {                                           // Handle outgoing network messages
      if (message_sent >= 0) {                                         // Special case for message type 4
        if (message_sent == 4) {
          small_zoom(&language_buffer[6528]);
        } else {
          small_zoom(&language_buffer[6464]);
          for (i = 0; i < 14; ++i) { //24 is too long for network_messages
              buffer[i] = network_messages[message_sent][i];
          }
          //for (i = 0; i < 24; currentdir[i + 255] = *((_BYTE *)&head_y + 14 * message_sent + i + 3))
          //  ++i;
          subzoom(buffer);
        }
        message_sent = -1;
      }
    } else {
      iBufferIndex = 0;                         // Process incoming network message - start building display buffer
      if (language_buffer[6400])              // Copy language buffer text if present
      {
        do {
          byChar = language_buffer[++iBufferIndex + 6400];
          buffer[iBufferIndex] = language_buffer[iBufferIndex + 6399];
          //currentdir[iBufferIndex + 255] = language_buffer[iBufferIndex + 6399];
        } while (byChar);
      }
      for (j = 0; j < 9; ++j)                 // Append player name to message buffer (9 chars max)
      {
        ++iBufferIndex;
        byNameChar = player_names[message_received][j];
        buffer[iBufferIndex] = byNameChar;
        //currentdir[iBufferIndex + 255] = byNameChar;
      }
      small_zoom(buffer);

      for (k = 0; k < (int)sizeof(received_message); ++k) {
        buffer[k] = received_message[k];
      }
      buffer[sizeof(received_message)] = '\0';
      //for (k = 0; k < 24; currentdir[k + 255] = p_data[k + 13])// Copy received message data (24 bytes) with unrolled loop
      //{
      //  k += 8;
      //  currentdir[k + 248] = p_data[k + 6];
      //  currentdir[k + 249] = p_data[k + 7];
      //  currentdir[k + 250] = p_data[k + 8];
      //  currentdir[k + 251] = p_data[k + 9];
      //  currentdir[k + 252] = p_data[k + 10];
      //  currentdir[k + 253] = p_data[k + 11];
      //  currentdir[k + 254] = p_data[k + 12];
      //}
      subzoom(buffer);
      message_received = -1;
    }
  }
  if (Car[ViewType[0]].byDebugDamage)         // Handle damage indicator for player 1
  {
    if (game_count[0] == -2)
      start_zoom(&language_buffer[1600], 0);
    if (game_count[0] > 0 && Car[ViewType[0]].fHealth < 90.0)
      game_count[0] = 72;
  }
  if (player_type == 2 && Car[ViewType[1]].byDebugDamage)// Handle damage indicator for player 2 (split screen mode)
  {
    if (game_count[1] == -2)
      start_zoom(&language_buffer[1600], 1);
    if (game_count[1] > 0 && Car[ViewType[1]].fHealth < 90.0)
      game_count[1] = 72;
  }
  if (!time_shown)                            // Main UI rendering branch - check if time display is shown
  {
    if (shown_panel || winner_mode) {                                           // Handle winner mode display and announcements
    RENDER_UI_ELEMENTS:
      if (winner_mode) {                                         // Championship mode winner announcements
        if (champ_mode) {
          if (game_count[0] == -2 && !winner_done) {
            switch (champ_zoom) {
              case 0:
                if (racers - 1 == champ_car)  // Show winner car image
                  pMessageStr = &language_buffer[1344];
                else
                  pMessageStr = &language_buffer[64 * champ_car + 384];
                start_zoom(pMessageStr, 0);
                goto ADVANCE_WINNER_SEQUENCE;
              case 1:
                start_zoom(driver_names[champorder[champ_car]], winner_done);// Show driver name
                goto ADVANCE_WINNER_SEQUENCE;
              case 3:
                speechsample(SOUND_SAMPLE_CONGRAT, 0x8000, 18, player1_car);// Play congratulations sound and show message
                start_zoom(&config_buffer[6272], 0);
                goto ADVANCE_WINNER_SEQUENCE;
              case 4:
                start_zoom(&config_buffer[6336], 0);
                goto ADVANCE_WINNER_SEQUENCE;
              case 5:
                start_zoom(&config_buffer[6400], 0);
                goto ADVANCE_WINNER_SEQUENCE;
              case 6:
                if (total_wins[champorder[0]] > 0)// Announce race wins if any
                  speechsample(total_wins[champorder[0]] + SOUND_SAMPLE_FATAL, 0x8000, 18, player1_car);// 1 win starts at SOUND_SAMPLE_1RACE
                goto ADVANCE_WINNER_SEQUENCE;
              case 7:
                iKillCount = total_kills[champorder[0]];// Announce kill statistics
                if (iKillCount > 0) {                               // Lots of kills (17+) gets special sound
                  if (iKillCount >= 17) {
                    iKillSample = SOUND_SAMPLE_FATLOTS;          // SOUND_SAMPLE_FATLOTS
                    iPlayerCarForSound = player1_car;
                  } else {
                    iPlayerCarForSound = player1_car;
                    iKillSample = total_kills[champorder[0]] + SOUND_SAMPLE_8RACE;// 1 kill starts at SOUND_SAMPLE_ONE
                  }
                  speechsample(iKillSample, 0x8000, 18, iPlayerCarForSound);
                }
                ++champ_zoom;
                break;
              default:
                break;                          // Championship zoom sequence state machine
            }
          }
        } else if (game_count[0] == -2 && !winner_done)// Single race winner announcements (non-championship)
        {
          switch (champ_zoom) {
            case 0:
              speechsample(SOUND_SAMPLE_WON, 0x8000, 18, player1_car);// SOUND_SAMPLE_WON
              start_zoom(&config_buffer[5952], 0);
              goto ADVANCE_WINNER_SEQUENCE;
            case 1:
              start_zoom(&config_buffer[6016], 0);
              goto ADVANCE_WINNER_SEQUENCE;
            case 2:
              if (result_kills[result_order[0]] > 0 || result_order[0] == FastestLap)// Check for kills or fastest lap achievements
                speechsample(SOUND_SAMPLE_STAT, 0x8000, 18, player1_car);// SOUND_SAMPLE_STAT
              goto ADVANCE_WINNER_SEQUENCE;
            case 3:
              if (result_order[0] == FastestLap)// Fastest lap announcement
              {                                 // Check if it's a new lap record
                if (BestTime == TrackRecordLap(game_track))
                  iSoundSampleForWinner = SOUND_SAMPLE_NEWLAP;  // SOUND_SAMPLE_NEWLAP
                else
                  iSoundSampleForWinner = SOUND_SAMPLE_NEWFAST;  // SOUND_SAMPLE_NEWFAST
                speechsample(iSoundSampleForWinner, 0x8000, 18, player1_car);
              }
              goto ADVANCE_WINNER_SEQUENCE;
            case 4:
              iWinnerKillCount = result_kills[result_order[0]];
              if (iWinnerKillCount > 0) {
                if (iWinnerKillCount >= 17) {
                  iWinnerSoundSample = SOUND_SAMPLE_FATLOTS;     // SOUND_SAMPLE_FATLOTS
                  iWinnerPlayerCar = player1_car;
                } else {
                  iWinnerPlayerCar = player1_car;
                  iWinnerSoundSample = result_kills[result_order[0]] + SOUND_SAMPLE_8RACE;// 1 kill starts at SOUND_SAMPLE_ONE
                }
                speechsample(iWinnerSoundSample, 0x8000, 18, iWinnerPlayerCar);
              }
            ADVANCE_WINNER_SEQUENCE:
              ++champ_zoom;
              break;
            default:
              break;
          }
        }
      }
      if (intro && game_count[0] == -2)       // Handle intro sequence display
      {
        start_zoom(&language_buffer[1792], 0);
        subzoom(&language_buffer[1856]);
      }
      if (network_on && paused)               // Show pause message in network games
      {
        sprintf(buffer, "%s", &language_buffer[6272]);
        start_zoom(buffer, 0);
        sprintf(buffer, "%s", player_names[pauser]);
        subzoom(buffer);
        set_game_scale(0, 64.0f);
        game_count[0] = 2;
      }
      if (iCarIdx == player1_car || winner_mode || intro)// Render zoom messages for player 1 or in special modes
      {                                         // Choose font based on zoom and screen resolution
        if (zoom_size[0]) {
          ZoomString(zoom_mes[0], font3_ascii, rev_vga[3], 0, font3_offsets);
        } else if (winh < 200) {
          ZoomString(zoom_mes[0], ascii_conv3, rev_vga[0], 0, font6_offsets);
        } else {
          ZoomString(zoom_mes[0], font6_ascii, rev_vga[1], 0, font6_offsets);
        }
        if (sub_on[0]) {
          if (winh < 200)
            ZoomSub(zoom_sub[0], ascii_conv3, rev_vga[0], 0, font6_offsets);
          else
            ZoomSub(zoom_sub[0], font6_ascii, rev_vga[1], 0, font6_offsets);
        }
      }
      if (iCarIdx == player2_car)             // Render zoom messages for player 2 (split screen)
      {
        if (zoom_size[1]) {
          ZoomString(zoom_mes[1], font3_ascii, rev_vga[3], 1, font3_offsets);
        } else if (winh < 200) {
          ZoomString(zoom_mes[1], ascii_conv3, rev_vga[0], 1, font6_offsets);
        } else {
          ZoomString(zoom_mes[1], font6_ascii, rev_vga[1], 1, font6_offsets);
        }
        if (sub_on[1]) {
          if (winh < 200)
            ZoomSub(zoom_sub[1], ascii_conv3, rev_vga[0], 1, font6_offsets);
          else
            ZoomSub(zoom_sub[1], font6_ascii, rev_vga[1], 1, font6_offsets);
        }
      }
      goto HANDLE_SPECIAL_MODES;
    }
    shown_panel = -1;                           // Hide panel and handle race countdown logic
    if (replaytype != 2) {
      if (game_type != 2) {
        if (countdown <= -72)
          goto DRAW_EFFECTS_AND_PANEL;                        // Race has started (countdown <= -72)
        if (countdown < 0 && gosound >= 1)    // Play GO sound when countdown reaches 0
        {
          gosound = 0;
          speechsample(SOUND_SAMPLE_GO, 0x8000, 0, player1_car);// SOUND_SAMPLE_GO
        }
        if ((unsigned int)countdown < 0x48)   // Pre-race countdown phase (0-72 frames)
        {                                       // Play engine sound during countdown
          if (gosound >= 2) {
            gosound = 1;
            speechsample(SOUND_SAMPLE_ENGINES, 0x8000, 0, player1_car);// SOUND_SAMPLE_ENGINES
          }
          if (countdown < 18)                 // Show ready message in final countdown (18 frames)
          {
            if (strcmp(&language_buffer[1664], zoom_mes[0])) {
              start_zoom(&language_buffer[1664], 0);
              if (player_type == 2)
                start_zoom(&language_buffer[1664], 1);
            }
          }
        }
        if (countdown < 72 || countdown >= 144 || !fadedin || gosound < 3)
          goto DRAW_EFFECTS_AND_PANEL;
        iSoundSample = SOUND_SAMPLE_DRIVERS;                      // SOUND_SAMPLE_DRIVERS
        gosound = 2;
        iPlayerCar = player1_car;
        goto PLAY_COUNTDOWN_SOUND;
      }
      if (gosound >= 1 && active_nodes == network_on) {
        iSoundSample = SOUND_SAMPLE_GO;                       // SOUND_SAMPLE_GO
        iPlayerCar = player1_car;
        gosound = 0;
      PLAY_COUNTDOWN_SOUND:
        speechsample(iSoundSample, 0x8000, 0, iPlayerCar);
      }
    }
  DRAW_EFFECTS_AND_PANEL:
    draw_smoke(pSrc, iCarIdx);
    if (replaytype != 2 && (textures_off & 0x20) == 0)
      test_panel(pSrc, iCarIdx);
    goto RENDER_UI_ELEMENTS;
  }
HANDLE_SPECIAL_MODES:
  if (replaytype == 2 && !intro)              // Handle replay mode display
    displayreplay();
  if (game_req)                               // Handle game menu requests and exit confirmation
  {
    if (draw_type != 2) {
      game_render_draw_pause_darken(g_pGameRenderer); // GPU-only: SW darkens via blankwindow() inside display_paused()
      display_paused();
      if (trying_to_exit) {
        int iPromptY = (scr_size * 96) >> 6;
        int iPromptH = (scr_size * 14) >> 6;

        if (iPromptH <= 0)
          iPromptH = 1;
        frontend_mouse_register_rect(FRONTEND_PAUSE_MOUSE_QUIT_PROMPT_ID,
                                     0, iPromptY,
                                     winw > 0 ? winw : XMAX,
                                     iPromptH);
        if ((frames & 0xFu) < 8)
          prt_centrecol(rev_vga[1], &language_buffer[6592], 160, 100, 171);
        frontend_mouse_draw_hover_box(FRONTEND_PAUSE_MOUSE_QUIT_PROMPT_ID,
                                      0, 0);
      }
    }
  }
  if (!winner_mode && replaytype != 2)        // Network status and waiting messages
  {                                             // Waiting for players message (blinking)
    if (network_on && active_nodes < network_on && (frames & 0xFu) < 8) {
      if (winh >= 200) {
        prt_centrecol(rev_vga[1], "WAITING FOR PLAYERS", 160, 100, 207);
      } else {
        iSavedScrSize = scr_size;
        scr_size = 64;
        mini_prt_centre(rev_vga[0], "WAITING FOR PLAYERS", winw / 2, winh / 2);
        scr_size = iSavedScrSize;
      }
    }
    if (network_on && finished_car[player1_car] && (frames & 0xFu) < 8 && !I_Would_Like_To_Quit && lastsample < -180)// Please wait for race to end message
    {
      iSavedScrSize2 = scr_size;
      scr_size = 64;
      mini_prt_centre(rev_vga[0], "PLEASE WAIT", winw / 2, winh / 2);
      mini_prt_centre(rev_vga[0], "FOR RACE TO END", winw / 2, winh / 2 + 16);
      scr_size = iSavedScrSize2;
    }
    if (I_Would_Like_To_Quit && (frames & 0xFu) < 8)// F10 to quit confirmation message
    {
      iSavedScrSize3 = scr_size;
      scr_size = 64;
      if (game_type == 1)
        pQuitMessage = szF10ToQuitChamp;
      else
        pQuitMessage = szF10ToQuitGame;
      mini_prt_centre(rev_vga[0], pQuitMessage, winw / 2, winh / 2);
      mini_prt_centre(rev_vga[0], "ESC TO CANCEL", winw / 2, winh / 2 + 14);
      frontend_mouse_draw_hover_box(RACE_MOUSE_QUIT_PROMPT, 0, 0);
      scr_size = iSavedScrSize3;
    }
  }
  if (draw_type != 2)                         // Frame rate calculation and timing
  {
    curr_time = ticks;
    iFrameCount = ++frame_count;
    if (ticks - start_time >= 36) {
      frame_rate = iFrameCount;
      frame_count = 0;
      start_time = curr_time;
    }
  }
  touch_ui_render_game(winw > 0 ? winw : XMAX, winh > 0 ? winh : YMAX);
  if (draw_type != 2)                         // Final screen buffer copy to destination
    copypic(pSrc, pDest);
}

//-------------------------------------------------------------------------------------------------
//00015CE0
void test_w95()
{
  /*
  REGS regs; // [esp+0h] [ebp-34h] BYREF
  struct SREGS sregs; // [esp+1Ch] [ebp-18h] BYREF

  // AX=1600h(5632) is a Microsoft-defined multiplex interrupt function used to
  // query the windows environment. This returns:
  // * AL = 0xFF if Windows is not running (i.e., plain DOS)
  // * AL = major version and AH = minor version if Windows is running (e.g., AL=4, AH=0 for Windows 95)
  memset(&sregs, 0, sizeof(sregs));
  regs.w.ax = 5632;
  int386x(47, &regs, &regs, &sregs);
  w95 = regs.h.al;
  if (regs.h.al < 3u || regs.h.al > 4u)
    w95 = 0;
  else
    w95 = -1;*/

  //for our purposes we'll pretend to be in DOS
  w95 = 0;
}

//-------------------------------------------------------------------------------------------------
//00015D50
void *malloc2(int iSize, void *pPtr, int *pRegsDi)
{
  void *result; // eax
  result = malloc(iSize);
  pPtr = result;
  return result;
}

//-------------------------------------------------------------------------------------------------
//00015E00
void free2(void *ptr)
{
  free(ptr);
  --hibuffers;
}

//-------------------------------------------------------------------------------------------------
