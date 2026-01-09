#ifndef _ROLLER_3D_H
#define _ROLLER_3D_H
//-------------------------------------------------------------------------------------------------
#include "types.h"
#include "engines.h"
#include "frontend.h"
//-------------------------------------------------------------------------------------------------

enum eTrakColour
{
  TRAK_COLOUR_LEFT_LANE  = 0,
  TRAK_COLOUR_CENTER     = 1,
  TRAK_COLOUR_RIGHT_LANE = 2,
  TRAK_COLOUR_LEFT_WALL  = 3,
  TRAK_COLOUR_RIGHT_WALL = 4,
  TRAK_COLOUR_ROOF       = 5,
};

enum eGroundColour
{
  GROUND_COLOUR_LUOWALL  = 0,
  GROUND_COLOUR_LLOWALL  = 1,
  GROUND_COLOUR_OFLOOR   = 2,
  GROUND_COLOUR_RLOWALL  = 3,
  GROUND_COLOUR_RUOWALL  = 4,
};

enum eWhipKeyScancodes
{
  WHIP_SCANCODE_ESCAPE = 0x01,
  WHIP_SCANCODE_1 = 0x02,
  WHIP_SCANCODE_2 = 0x03,
  WHIP_SCANCODE_3 = 0x04,
  WHIP_SCANCODE_4 = 0x05,
  WHIP_SCANCODE_5 = 0x06,
  WHIP_SCANCODE_6 = 0x07,
  WHIP_SCANCODE_7 = 0x08,
  WHIP_SCANCODE_8 = 0x09,
  WHIP_SCANCODE_9 = 0x0A,
  WHIP_SCANCODE_0 = 0x0B,
  WHIP_SCANCODE_MINUS = 0x0C,
  WHIP_SCANCODE_EQUALS = 0x0D,
  WHIP_SCANCODE_BACKSPACE = 0x0E,
  WHIP_SCANCODE_TAB = 0x0F,
  WHIP_SCANCODE_Q = 0x10,
  WHIP_SCANCODE_W = 0x11,
  WHIP_SCANCODE_E = 0x12,
  WHIP_SCANCODE_R = 0x13,
  WHIP_SCANCODE_T = 0x14,
  WHIP_SCANCODE_Y = 0x15,
  WHIP_SCANCODE_U = 0x16,
  WHIP_SCANCODE_I = 0x17,
  WHIP_SCANCODE_O = 0x18,
  WHIP_SCANCODE_P = 0x19,
  WHIP_SCANCODE_LEFTBRACKET = 0x1A,
  WHIP_SCANCODE_RIGHTBRACKET = 0x1B,
  WHIP_SCANCODE_RETURN = 0x1C,
  WHIP_SCANCODE_LCTRL = 0x1D,
  WHIP_SCANCODE_A = 0x1E,
  WHIP_SCANCODE_S = 0x1F,
  WHIP_SCANCODE_D = 0x20,
  WHIP_SCANCODE_F = 0x21,
  WHIP_SCANCODE_G = 0x22,
  WHIP_SCANCODE_H = 0x23,
  WHIP_SCANCODE_J = 0x24,
  WHIP_SCANCODE_K = 0x25,
  WHIP_SCANCODE_L = 0x26,
  WHIP_SCANCODE_SEMICOLON = 0x27,
  WHIP_SCANCODE_APOSTROPHE = 0x28,
  WHIP_SCANCODE_GRAVE = 0x29,
  WHIP_SCANCODE_LSHIFT = 0x2A,
  WHIP_SCANCODE_BACKSLASH = 0x2B,
  WHIP_SCANCODE_Z = 0x2C,
  WHIP_SCANCODE_X = 0x2D,
  WHIP_SCANCODE_C = 0x2E,
  WHIP_SCANCODE_V = 0x2F,
  WHIP_SCANCODE_B = 0x30,
  WHIP_SCANCODE_N = 0x31,
  WHIP_SCANCODE_M = 0x32,
  WHIP_SCANCODE_COMMA = 0x33,
  WHIP_SCANCODE_PERIOD = 0x34,
  WHIP_SCANCODE_SLASH = 0x35,
  WHIP_SCANCODE_RSHIFT = 0x36,
  WHIP_SCANCODE_KP_MULTIPLY = 0x37,
  WHIP_SCANCODE_LALT = 0x38,
  WHIP_SCANCODE_SPACE = 0x39,
  WHIP_SCANCODE_CAPSLOCK = 0x3A,
  WHIP_SCANCODE_F1 = 0x3B,
  WHIP_SCANCODE_F2 = 0x3C,
  WHIP_SCANCODE_F3 = 0x3D,
  WHIP_SCANCODE_F4 = 0x3E,
  WHIP_SCANCODE_F5 = 0x3F,
  WHIP_SCANCODE_F6 = 0x40,
  WHIP_SCANCODE_F7 = 0x41,
  WHIP_SCANCODE_F8 = 0x42,
  WHIP_SCANCODE_F9 = 0x43,
  WHIP_SCANCODE_F10 = 0x44,
  WHIP_SCANCODE_F11 = 0x45,
  WHIP_SCANCODE_F12 = 0x46,
  WHIP_SCANCODE_KP_7 = 0x47,
  WHIP_SCANCODE_KP_8 = 0x48,
  WHIP_SCANCODE_KP_9 = 0x49,
  WHIP_SCANCODE_KP_MINUS = 0x4A,
  WHIP_SCANCODE_KP_4 = 0x4B,
  WHIP_SCANCODE_KP_5 = 0x4C,
  WHIP_SCANCODE_KP_6 = 0x4D,
  WHIP_SCANCODE_KP_PLUS = 0x4E,
  WHIP_SCANCODE_KP_1 = 0x4F,
  WHIP_SCANCODE_KP_2 = 0x50,
  WHIP_SCANCODE_KP_3 = 0x51,
  WHIP_SCANCODE_KP_0 = 0x52,
  WHIP_SCANCODE_KP_PERIOD = 0x53,
  WHIP_SCANCODE_RIGHT = 0x4D,
  WHIP_SCANCODE_LEFT = 0x4B,
  WHIP_SCANCODE_DOWN = 0x50,
  WHIP_SCANCODE_UP = 0x48,
  WHIP_MAPPED_F11 = 0x57,
  WHIP_MAPPED_F12 = 0x58,
  WHIP_SCANCODE_J1B1 = 0x80,
  WHIP_SCANCODE_J1B2 = 0x81,
  WHIP_SCANCODE_J2B1 = 0x82,
  WHIP_SCANCODE_J2B2 = 0x83
};

//-------------------------------------------------------------------------------------------------

typedef struct
{
  void *pBuf;
  uint32 uiSize;
  void *pAlsoBuf; //seems to be set to the same thing as pBuf in W95
  int iRegsDi;    //unused by W95
} tMemBlock;

//-------------------------------------------------------------------------------------------------

typedef struct
{
  tVec3 pointAy[4];
  float fTrackHalfLength;
  float fTrackHalfWidth;
  int iPitch;
  int iYaw;
  int iRoll;
  int iInnerLanePitchAngle;
  int iOuterLanePitchAngle;
  tVec3 gravity;
  int iBankDelta;
  int iBankAngleDelta;
  float fAILine1;
  float fAILine2;
  float fAILine3;
  float fAILine4;
  int iCenterGrip;
  int iLeftShoulderGrip;
  int iRightShoulderGrip;
  float fAIMaxSpeed;
} tData;

//-------------------------------------------------------------------------------------------------

typedef struct
{
  tVec3 pointAy[6];
} tGroundPt;

//-------------------------------------------------------------------------------------------------

typedef struct
{
  tPoint screen;
  tVec3 projected;
} tScreenPt;

//-------------------------------------------------------------------------------------------------

typedef struct
{
  int iUnk1;
  int iClipCount;
  tScreenPt screenPtAy[6];
} tTrackScreenXYZ;

//-------------------------------------------------------------------------------------------------

typedef struct
{
  int16 nRenderPriority;
  int16 nChunkIdx;
  float fZDepth;
} tTrackZOrderEntry;

//-------------------------------------------------------------------------------------------------

extern int champ_track[16];
extern int exiting;
extern int dontrestart;
extern int champ_mode;
extern int cd_error;
extern int game_count[2];
extern int lastblip[2];
extern float game_scale[2];
extern int define_mode;
extern int calibrate_mode;
extern int graphic_mode;
extern int calibrate_select;
extern int sound_edit;
extern int showversion;
extern int game_svga;
extern int game_size;
extern int game_view[2];
extern int svga_possible;
extern int autoswitch;
extern int hibuffers;
extern int lobuffers;
extern int mem_used;
extern int mem_used_low;
extern int current_mode;
extern int names_on;
extern tCarEngine *p_eng[2];
extern int messages;
extern int SVGA_ON;
extern int TrackLoad;
extern int paused;
extern int pause_request;
extern char alltrackflag;
extern int dxmouse;
extern int dymouse;
extern int mousex;
extern int mousey;
extern int mbut;
extern int oldbut;
extern int mclick;
extern int mouse;
extern int wide_on;
extern int network_on;
extern char Banks_On;
extern char Buildings_On;
extern char Road_On;
extern char Walls_On;
extern char Play_View;
extern int DriveView[2];
extern int mirror;
extern float TopViewHeight;
extern int mpressed;
extern int jpressed;
extern uint8 *screen;
extern uint8 *scrbuf;
extern uint8 *mirbuf;
extern uint8 *texture_vga;
extern uint8 *building_vga;
extern uint8 *horizon_vga;
extern uint8 *cartex_vga[16];
extern uint8 *cargen_vga;
extern tBlockHeader *rev_vga[16];
extern int firstrun;
extern int lagdone;
extern int language;
extern int GroundColour[MAX_TRACK_CHUNKS][5];
extern int TrakColour[MAX_TRACK_CHUNKS][6];
extern int HorizonColour[MAX_TRACK_CHUNKS];
extern tData localdata[MAX_TRACK_CHUNKS];
extern tGroundPt GroundPt[MAX_TRACK_CHUNKS];
extern float hor_scan[800];
extern tGroundPt TrakPt[MAX_TRACK_CHUNKS];
extern tTrackScreenXYZ GroundScreenXYZ[MAX_TRACK_CHUNKS];
extern tTrackScreenXYZ TrackScreenXYZ[MAX_TRACK_CHUNKS];
extern uint8 shade_palette[4096];
extern tColor palette[256];
extern float tsin[16384];
extern float ptan[16384];
extern float tcos[16384];
extern char buffer[128];
extern uint8 blank_line[640];
extern tTrackZOrderEntry TrackView[6500];
extern int p_joyk1[2];
extern int p_joyk2[2];
extern tMemBlock mem_blocks[128];
extern int zoom_size[2];
extern char zoom_mes[2][24];
extern int sub_on[2];
extern char zoom_sub[2][24];
extern int champ_go[16];
extern int game_overs;
extern int averagesectionlen;
extern int racing;
extern int totaltrackdistance;
extern int disable_messages;
extern int curr_time;
extern volatile int ticks;
extern int frame_rate;
extern int frame_count;
extern float k1;
extern float k2;
extern float k3;
extern float k4;
extern float k5;
extern float k6;
extern float k7;
extern float k8;
extern float k9;
extern float tatn[1025];
extern uint32 textures_off;
extern int tex_count;
extern int vtilt;
extern int worldtilt;
extern float worldx;
extern float worldy;
extern float worldz;
extern int worldelev;
extern int velevation;
extern int vdirection;
extern int scr_size;
extern int ybase;
extern int xbase;
extern int winx;
extern int winy;
extern float ext_y;
extern float ext_z;
extern float viewx;
extern float viewy;
extern float viewz;
extern int worlddirn;
extern char keys[140];
extern int oldmode;
extern int clear_borders;
extern float DDX;
extern float DDY;
extern float DDZ;
extern float ext_x;
extern int test_f1;
extern int test_f2;
extern int test_f3;
extern int print_data;
extern int demo_control;
extern int tick_on;
extern int old_mode;
extern int demo_mode;
extern int demo_count;
extern int start_race;
extern int NoOfLaps;
extern int human_finishers;
extern int finishers;
extern int countdown;
extern int screenready;
extern int shown_panel;
extern int start_cd;
extern int game_level;
extern int max_mem;
extern int game_req;
extern int game_dam;
extern int pausewindow;
extern int scrmode;
extern int control_select;
extern int req_size;
extern int intro;
extern int shifting;
extern int fadedin;
extern int control_edit;
extern int req_edit;
extern int controlrelease;
extern float subscale;
extern int fatal_ini_loaded;
extern int machine_speed;
extern int netCD;
extern int localCD;
extern int dead_humans;
extern int I_Want_Out;
extern int champ_car;
extern int champ_zoom;
extern int replay_player;
extern int team_mate;
extern int winner_done;
extern int winner_mode;
extern int network_mes_mode;
extern int cdchecked;
extern int network_slot;
extern int trying_to_exit;
extern int local_players;
extern int draw_type;
extern int network_buggered;
extern int champ_count;
extern int replay_cheat;
extern int w95;
extern int gave_up;
extern int champ_size;
extern int send_finished;
extern int game_frame;
extern int warp_angle;
extern int game_track;
extern int prev_track;
extern int view0_cnt;
extern int view1_cnt;
extern int I_Would_Like_To_Quit;
extern int Quit_Count;
extern int winh;
extern int winw;
extern int VIEWDIST;
extern int YMAX;
extern int XMAX;
extern int time_shown;
extern int player2_car;
extern int player1_car;

//-------------------------------------------------------------------------------------------------

void copypic(uint8 *pSrc, uint8 *pDest);
void init_screen();
void init();
void *getbuffer(uint32 uiSize);
void *trybuffer(uint32 uiSize);
void fre(void **ppData);
void doexit();
void firework_screen();
void updatescreen();
void draw_road(uint8 *pScrPtr, int iCarIdx, unsigned int uiViewMode, int iCopyImmediately, int iChaseCamIdx);
int main(int argc, const char **argv, const char **envp);
void play_game_init();
void play_game_uninit();
void winner_race();
void champion_race();
void play_game(int iTrack);
void game_keys();
void mesminus();
void mesplus();
void carminus();
void carplus();
void viewminus(int iPlayer);
void viewplus(int iPlayer);
void game_copypic(uint8 *pSrc, uint8 *pDest, int iCarIdx);
void test_w95();
void *malloc2(int iSize, void *pPtr, int *pRegsDi);
void free2(void *ptr);

//-------------------------------------------------------------------------------------------------
#endif
