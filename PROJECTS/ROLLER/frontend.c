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
#include "control.h"
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
//symbol names added by ROLLER
char szSelectEng[] = "select.eng";      //000A1980
char szConfigEng[] = "config.eng";      //000A198C
//char szFArielRaw[] = "FARIEL.RAW";      //000A1A70
//char szFDesilvaRaw[] = "FDESILVA.RAW";  //000A1A7C
//char szFPulseRaw[] = "FPULSE.RAW";      //000A1A8C
//char szFGlobalRaw[] = "FGLOBAL.RAW";    //000A1A98
//char szFMillionRaw[] = "FMILLION.RAW";  //000A1AA4
//char szFMissionRaw[] = "FMISSION.RAW";  //000A1AB4
//char szFZizinRaw[] = "FZIZIN.RAW";      //000A1AC4
//char szFReiseRaw[] = "FREISE.RAW";      //000A1AD0

//-------------------------------------------------------------------------------------------------

int false_starts = -1;    //000A4AB8
float TrackZs[25] =       //000A4A54
{
  400000.0, 800000.0, 400000.0, 500000.0, 400000.0,
  400000.0, 500000.0, 500000.0, 400000.0, 400000.0,
  400000.0, 800000.0, 800000.0, 800000.0, 800000.0,
  800000.0, 800000.0, 800000.0, 400000.0, 500000.0,
  400000.0, 400000.0, 500000.0, 500000.0, 400000.0
};
int death_race = 0;       //000A4ABC
int head_x = 186;         //000A4AC0
int head_y = 8;           //000A4AC4
char network_messages[5][14] = { //000A4AC8
  "SLOWCOACH",
  "OUT OF MY WAY",
  "YOU DIE",
  "SUCKER",
  "I WON!"
};
int competitors = 16;     //000A4B70
int manual_control[16] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }; //000A4B74
int smallcars[2][16] =    //000A4BB4
{
  { 1, 2, 3, 6, 4, 5, 7, 8, 1, 1, 1, 1, 1, 1, 1, 1 },
  { 12, 13, 14, 15, 16, 17, 18, 19, 1, 1, 1, 1, 1, 1, 1, 1 }
};
int infinite_laps = 0;    //000A4C34
int Players_Cars[16] = { 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7 }; //000A4C38
int points[16] = { 25, 18, 16, 14, 12, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 }; //000A4C78
int player_type = 0;      //000A4CB8
int cup_won = 0;          //000A4CBC
int game_type = 0;        //000A4CC0
int car_pies[112] =       //000A4CC4
{
  4, 5, 3, 4, 3, 6, 4,
  1, 8, 2, 3, 4, 4, 8,
  5, 3, 4, 2, 7, 5, 4,
  6, 2, 7, 6, 6, 1, 2,
  7, 7, 6, 7, 1, 8, 3,
  2, 6, 1, 4, 2, 7, 7,
  3, 4, 5, 1, 8, 3, 4,
  8, 1, 8, 7, 5, 2, 1,
  1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1
};
int track_laps[25][6] = //000A4E84
{
  { 12, 10, 8, 6, 4, 2 },
  { 10, 8, 7, 5, 4, 2 },
  { 18, 15, 12, 10, 7, 4 },
  { 15, 13, 10, 8, 5, 3 },
  { 18, 15, 12, 10, 7, 4 },
  { 23, 19, 16, 12, 9, 5 },
  { 15, 13, 10, 8, 5, 3 },
  { 15, 13, 10, 8, 5, 3 },
  { 17, 14, 11, 9, 6, 3 },
  { 12, 10, 8, 6, 4, 2 },
  { 12, 10, 8, 6, 4, 2 },
  { 12, 10, 8, 6, 4, 2 },
  { 12, 10, 8, 6, 4, 2 },
  { 12, 10, 8, 6, 4, 2 },
  { 12, 10, 8, 6, 4, 2 },
  { 12, 10, 8, 6, 4, 2 },
  { 12, 10, 8, 6, 4, 2 },
  { 12, 10, 8, 6, 4, 2 },
  { 12, 10, 8, 6, 4, 2 },
  { 12, 10, 8, 6, 4, 2 },
  { 12, 10, 8, 6, 4, 2 },
  { 12, 10, 8, 6, 4, 2 },
  { 12, 10, 8, 6, 4, 2 },
  { 12, 10, 8, 6, 4, 2 },
  { 12, 10, 8, 6, 4, 2 }
};
char race_posn[16][5] =   //000A5200
{
  " 1st",
  " 2nd",
  " 3rd",
  " 4th",
  " 5th",
  " 6th",
  " 7th",
  " 8th",
  " 9th",
  "10th",
  "11th",
  "12th",
  "13th",
  "14th",
  "15th",
  "last"
};
int Selected_Drives[9] = { 0, 2, 0, 5, 3, 4, 1, 0, 6 }; //000A5250
int Selected_Play[9] = { 0, 0, 2, 0, 0, 0, 0, 1, 0 }; //000A5274
int DuoViews[9]     = { -1, -1,  0, -1,  0, 0,  0,  0, 0 }; //000A5298
int AllowedViews[9] = { -1, -1, -1, -1,  0, 0,  0, -1, 0 }; //000A52BC
int ReplayViews[9]  = { -1, -1, -1, -1, -1, 0, -1, -1, 0 }; //000A52E0
int replay_record = 1;    //000A5304
int last_replay = -1;     //000A5308
int last_type = 0;        //000A530C
int SelectedView[2] = { 0, 0 }; //000A5310
int network_champ_on = 0; //000A5318
void *font_vga = NULL;    //000A531C
void *title_vga = NULL;   //000A5320
tBlockHeader *front_vga[16] = { NULL }; //000A5324
int font1_offsets[104] =  //000A5364
{
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, -2, 0, 0, 0, 0, 0, 0, -2, -2, 0, 0,
  -2, -2, 0, 0, 0, 0, 0, 0, 0, 0, -2, -2,
  0, 0, 0, 0, 0, 0, -2, 0, -1, -4, 0, -4,
  0, -4, -4, -4, -4, -4, -5, 0, -4, -4, -4,
  -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, 0,
  -4, 0, -4, -4, -4, -4, -4, 0, 0, 0
};
char font1_ascii[256] =   //000A5504
{
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0x24, 0x25, 0x3E, 0x27, 0x28, 0x2A, 0x3D,
  0x2C, 0x2D, 0x2B, 0x2F, 0x3F, 0x38, 0x40, 0x41, 0x1A, 0x1B,
  0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x32, 0x3C,
  0x35, 0x39, 0x36, 0x37, 0x33, 0x00, 0x01, 0x02, 0x03, 0x04,
  0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E,
  0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
  0x19, 0x3A, 0x43, 0x3B, 0x29, 0x2E, 0xFF, 0x00, 0x01, 0x02,
  0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C,
  0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
  0x17, 0x18, 0x19, 0x30, 0x42, 0x31, 0x34, 0xFF, 0x67, 0xFF,
  0xFF, 0x4B, 0x4D, 0x4A, 0x4C, 0xFF, 0x52, 0x53, 0x50, 0x57,
  0x56, 0x54, 0x4D, 0x4E, 0x51, 0x4F, 0x4F, 0x5A, 0x5C, 0x5A,
  0x62, 0x61, 0x47, 0x5B, 0x63, 0xFF, 0x26, 0xFF, 0xFF, 0x44,
  0x49, 0x55, 0x58, 0x60, 0x5E, 0x5E, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0x45, 0x46, 0x5D, 0x64, 0x65, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x66, 0xFF, 0xFF, 0xFF, 0xFF,
  0x48, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x5F, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
int font2_offsets[96] =   //000A5604
{
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, -1, 0, 0, 0, -2, 0, 0, -2, -2, 0, 0,
  0, 0, -2, -2, 0, 0, 0, 0, 0, 0, 0, -1,
  0, 0, 0, 0, 0, 0, 0, -2, -2, -2, -2, 0,
  -2, -2, -2, -2, -2, -2, -2, -2, -2, 0, 0, 0,
  -2, -2, -2, -3, -2, -2, 0, 0, 0, 0, 0, 0
};
char font2_ascii[256] =     //000A5784
{
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0x24, 0x25, 0x3C, 0x27, 0x28, 0x2A, 0x3B,
  0x2C, 0x2D, 0x2B, 0x2F, 0x37, 0x3F, 0x38, 0x39, 0x1A, 0x1B,
  0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x5A, 0x3A,
  0x36, 0x40, 0x35, 0x34, 0x31, 0x00, 0x01, 0x02, 0x03, 0x04,
  0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E,
  0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
  0x19, 0x3D, 0xFF, 0x3E, 0x29, 0x2E, 0xFF, 0x00, 0x01, 0x02,
  0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C,
  0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
  0x17, 0x18, 0x19, 0x32, 0xFF, 0x33, 0x30, 0xFF, 0x42, 0xFF,
  0xFF, 0x44, 0x46, 0x43, 0xFF, 0xFF, 0xFF, 0x48, 0x59, 0x4A,
  0xFF, 0x4B, 0xFF, 0xFF, 0x49, 0xFF, 0x41, 0x4E, 0x50, 0x4C,
  0x56, 0x58, 0xFF, 0xFF, 0x57, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0x45, 0x49, 0x4F, 0x55, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0x47, 0x54, 0x53, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x51, 0xFF, 0xFF, 0xFF, 0xFF,
  0x52, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
int font3_offsets[72] =    //000A5884
{
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  -1, -6, 0, -6, 0, -6, -6, -6, -6, -6, -7, 0,
  0, -6, -6, -6, -6, -6, -6, -6, -6, 0, -6, -6,
  -6, -6, -6, -6, 0, -6, -6, -6, -6, -6, 0, 0
};
char font3_ascii[256] =   //000A59A4
{
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0x24, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x1A, 0x1B,
  0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x01, 0x02, 0x03, 0x04,
  0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E,
  0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
  0x19, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x01, 0x02,
  0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C,
  0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
  0x17, 0x18, 0x19, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x30, 0xFF,
  0xFF, 0x2B, 0x2D, 0x2A, 0x2C, 0xFF, 0x33, 0x34, 0x32, 0x38,
  0x37, 0x35, 0x2D, 0x2E, 0x31, 0x2F, 0x2F, 0x3D, 0x3F, 0x3C,
  0x43, 0x42, 0x27, 0x3E, 0x44, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0x29, 0x36, 0x3B, 0x41, 0x3A, 0x3A, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0x25, 0x26, 0x39, 0x45, 0x46, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x47, 0xFF, 0xFF, 0xFF, 0xFF,
  0x28, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x40, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
int font4_offsets[80] =    //000A5AA4
{
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, -5, 0, 0, 0, 0, 0, -7, 0, 0, -6,
  0, -7, -7, -7, -7, -7, -10, 0, -7, -7, -7, -7,
  -7, -7, -7, -7, -7, -1, -7, -7, -7, -7, -7, 0,
  -7, -7, -6, -6, -6, 0, 0, 0
};
int font5_offsets[80] =   //000A5BE4
{
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, -2, 0, 0, 0, 0, 0, -5, 0, 0, -5,
  0, -5, -5, -5, -5, -5, -7, 0, -5, -5, -5, -5,
  -5, -5, -5, -5, -5, -1, -5, -5, -5, -5, -5, 0,
  -5, -5, -5, -5, -5, 0, 0, 0
};
char font4_ascii[256] =      //000A5D24
{
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0x29, 0x24, 0x2A, 0x25, 0xFF, 0x1A, 0x1B,
  0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x27, 0xFF,
  0xFF, 0x2B, 0xFF, 0xFF, 0xFF, 0x00, 0x01, 0x02, 0x03, 0x04,
  0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E,
  0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
  0x19, 0xFF, 0xFF, 0xFF, 0x26, 0x28, 0xFF, 0x00, 0x01, 0x02,
  0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C,
  0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
  0x17, 0x18, 0x19, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0x33, 0x36, 0x32, 0x34, 0xFF, 0x3A, 0x3B, 0x39, 0x3F,
  0x3E, 0x3C, 0x35, 0x36, 0x38, 0x37, 0x37, 0x4A, 0x4C, 0x48,
  0x44, 0xFF, 0x2F, 0x4B, 0x45, 0xFF, 0xFF, 0xFF, 0xFF, 0x2D,
  0x31, 0x3D, 0x49, 0x42, 0x40, 0x40, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0x2C, 0x2E, 0xFF, 0x46, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x47, 0xFF, 0xFF, 0xFF, 0xFF,
  0x30, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x41, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
char font1_ascii_br[256] = // Fatal Brazilian/Saspanish - object_3_variable_1644
{
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0x24, 0x25, 0x3E, 0x27, 0x28, 0x2A, 0x3D,
  0x2C, 0x2D, 0x2B, 0x2F, 0x3F, 0x38, 0x40, 0x41, 0x1A, 0x1B,
  0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x32, 0x3C,
  0x35, 0x39, 0x36, 0x37, 0x33, 0x00, 0x01, 0x02, 0x03, 0x04,
  0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E,
  0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
  0x19, 0x3A, 0x43, 0x3B, 0x29, 0x2E, 0xFF, 0x00, 0x01, 0x02,
  0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C,
  0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
  0x17, 0x18, 0x19, 0x30, 0x42, 0x31, 0x34, 0xFF, 0x67, 0xFF,
  0xFF, 0x4B, 0x4D, 0x4A, 0x4C, 0xFF, 0x52, 0x53, 0x50, 0x57,
  0x56, 0x54, 0x4D, 0x4E, 0x51, 0x4F, 0x4F, 0x5A, 0x5C, 0x5A,
  0x62, 0x61, 0x47, 0x5B, 0x63, 0xFF, 0x26, 0xFF, 0xFF, 0x44,
  0x49, 0x55, 0x58, 0x60, 0x5E, 0x5E, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0x49, 0x4A, 0x4B, 0x4C, 0x65, 0xFF, 0xFF, 0x67,
  0x50, 0x51, 0x52, 0xFF, 0xFF, 0x55, 0xFF, 0xFF, 0xFF, 0x5E,
  0xFF, 0x59, 0xFF, 0x5B, 0xFF, 0xFF, 0xFF, 0x60, 0x61, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x66, 0xFF, 0xFF, 0xFF, 0xFF,
  0x48, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x55, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
char font2_ascii_br[256] = // Fatal Brazilian/Saspanish - object_3_variable_1646
{
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0x24, 0x25, 0x3C, 0x27, 0x28, 0x2A, 0x3B,
  0x2C, 0x2D, 0x2B, 0x2F, 0x37, 0x3F, 0x38, 0x39, 0x1A, 0x1B,
  0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x5A, 0x3A,
  0x36, 0x40, 0x35, 0x34, 0x31, 0x00, 0x01, 0x02, 0x03, 0x04,
  0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E,
  0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
  0x19, 0x3D, 0xFF, 0x3E, 0x29, 0x2E, 0xFF, 0x00, 0x01, 0x02,
  0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C,
  0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
  0x17, 0x18, 0x19, 0x32, 0xFF, 0x33, 0x30, 0xFF, 0x42, 0xFF,
  0xFF, 0x44, 0x46, 0x0D, 0xFF, 0xFF, 0xFF, 0x48, 0x59, 0x4A,
  0xFF, 0x4B, 0xFF, 0xFF, 0x49, 0xFF, 0x41, 0x4E, 0x50, 0x4C,
  0x56, 0x58, 0xFF, 0xFF, 0x57, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0x45, 0x49, 0x4F, 0x55, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0x45, 0x43, 0x44, 0x46, 0x53, 0xFF, 0xFF, 0x42,
  0x49, 0x59, 0x5B, 0xFF, 0xFF, 0x4A, 0xFF, 0xFF, 0xFF, 0x5C,
  0xFF, 0x4D, 0xFF, 0x50, 0xFF, 0xFF, 0xFF, 0x55, 0x58, 0x26,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x51, 0xFF, 0xFF, 0xFF, 0xFF,
  0x52, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x4A, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
char font3_ascii_br[256] = // Fatal Brazilian/Saspanish - object_3_variable_1648
{
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0x24, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x1A, 0x1B,
  0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x01, 0x02, 0x03, 0x04,
  0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E,
  0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
  0x19, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x01, 0x02,
  0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C,
  0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
  0x17, 0x18, 0x19, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x30, 0xFF,
  0xFF, 0x2B, 0x2D, 0x2A, 0x2C, 0xFF, 0x33, 0x34, 0x32, 0x38,
  0x37, 0x35, 0x2D, 0x2E, 0x31, 0x2F, 0x2F, 0x3D, 0x3F, 0x3C,
  0x43, 0x42, 0x27, 0x3E, 0x44, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0x29, 0x36, 0x3B, 0x41, 0x3A, 0x3A, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0x29, 0x2A, 0x2B, 0x2C, 0x46, 0xFF, 0xFF, 0x30,
  0x31, 0x32, 0x33, 0xFF, 0xFF, 0x36, 0xFF, 0xFF, 0xFF, 0x3A,
  0xFF, 0x3C, 0xFF, 0x3E, 0xFF, 0xFF, 0xFF, 0x41, 0x42, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x47, 0xFF, 0xFF, 0xFF, 0xFF,
  0x28, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x36, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
char font4_ascii_br[256] = // Fatal Brazilian/Saspanish - object_3_variable_1651
{
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0x29, 0x24, 0x2A, 0x25, 0xFF, 0x1A, 0x1B,
  0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x27, 0xFF,
  0xFF, 0x2B, 0xFF, 0xFF, 0xFF, 0x00, 0x01, 0x02, 0x03, 0x04,
  0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E,
  0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
  0x19, 0xFF, 0xFF, 0xFF, 0x26, 0x28, 0xFF, 0x00, 0x01, 0x02,
  0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C,
  0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
  0x17, 0x18, 0x19, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0x33, 0x36, 0x32, 0x34, 0xFF, 0x3A, 0x3B, 0x39, 0x3F,
  0x3E, 0x3C, 0x35, 0x36, 0x38, 0x37, 0x37, 0x4A, 0x4C, 0x48,
  0x44, 0xFF, 0x2F, 0x4B, 0x45, 0xFF, 0xFF, 0xFF, 0xFF, 0x2D,
  0x31, 0x3D, 0x49, 0x42, 0x40, 0x40, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0x31, 0x32, 0x33, 0x34, 0xFF, 0xFF, 0xFF, 0x4D,
  0x38, 0x39, 0x3A, 0xFF, 0xFF, 0x3D, 0xFF, 0xFF, 0xFF, 0x40,
  0xFF, 0x49, 0xFF, 0x4B, 0xFF, 0xFF, 0xFF, 0x42, 0x43, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x47, 0xFF, 0xFF, 0xFF, 0xFF,
  0x30, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x3D, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
tPoint sel_posns[12] =
{
  { 33, 60 },
  { 33, 82 },
  { 33, 104 },
  { 33, 126 },
  { 33, 148 },
  { 33, 170 },
  { 33, 192 },
  { 33, 214 },
  { 33, 236 },
  { 33, 258 },
  { 33, 280 },
  { 33, 336 }
};
int no_clear = 0;         //000A5E84
char *descript[8] =       //000A5E88
{
  "FARIEL.RAW",
  "FDESILVA.RAW",
  "FPULSE.RAW",
  "FGLOBAL.RAW",
  "FMILLION.RAW",
  "FMISSION.RAW",
  "FZIZIN.RAW",
  "FREISE.RAW"
};
char comp_name[16][15] =  //000A5EA8
{
  "REISE WAGON 2:",
  "REISE WAGON 1:",
  "ZIZIN 2:",
  "ZIZIN 1:",
  "MISSION 2:",
  "MISSION 1:",
  "MILLION 2:",
  "MILLION 1:",
  "GLOBAL 2:",
  "GLOBAL 1:",
  "PULSE 2:",
  "PULSE 1:",
  "DESILVA 2:",
  "DESILVA 1:",
  "AUTO ARIEL 2:",
  "AUTO ARIEL 1:"
};
int network_test = 0;     //000A5F98
char cheat_names[32][9];  //0016F8F0
char player_names[16][9]; //0016FA10
int DeathView[2];         //0016FAA0
int teamorder[8];         //0016FAA8
int champorder[16];       //0016FAC8
int team_wins[16];        //0016FB08
int human_control[16];    //0016FB48
int total_wins[16];       //0016FB88
int allocated_cars[14];   //0016FBC8
int team_kills[16];       //0016FC00
int total_kills[16];      //0016FC40
int team_points[8];       //0016FC80
int championship_points[16]; //0016FCA0
int team_fasts[16];       //0016FCE0
int total_fasts[16];      //0016FD20
int non_competitors[16];  //0016FDE0
int player_started[16];   //0016FE20
int player_invul[16];     //0016FE60
int p_texs_off;           //0016FEA0
int p_tex_size;           //0016FEA4
int quit_game;            //0016FEA8
int players;              //0016FEAC
int front_fade;           //0016FEB0
int frontend_on;          //0016FEB4
int cd_cheat;             //0016FEB8
//net_test                //0016FEBC
int my_control;           //0016FEC0
int my_car;               //0016FEC4
int my_number;            //0016FEC8
int my_invul;             //0016FECC
int FastestLap;           //0016FED0
int racers;               //0016FED4
uint32 cheat_mode;        //0016FED8
int Race;                 //0016FEDC
int switch_types;         //0016FEE0
int players_waiting;      //0016FEE4
int switch_sets;          //0016FEE8
int time_to_start;        //0016FEEC
int I_Quit;               //0016FEF0
int StartPressed;         //0016FEF4
int waste;                //0016FEF8
int switch_same;          //0016FEFC
int car_request;          //0016FF00
uint8 oldkeys[14];        //0016FF04
char my_name[14];         //0016FF12

//-------------------------------------------------------------------------------------------------
// Replace accented characters with non-accented equivalents in the font table - add by ROLLER
void font_ascii_replace_accent(char *font)
{
  font[0xc7] = font['C']; // Ç
  font[0xe7] = font['c']; // ç

  font[0xc0] = font['A']; // À
  font[0xc1] = font['A']; // Á
  font[0xc2] = font['A']; // Â
  font[0xc3] = font['A']; // Ã
  font[0xe0] = font['a']; // à
  font[0xe1] = font['a']; // á
  font[0xe2] = font['a']; // â
  font[0xe3] = font['a']; // ã

  font[0xd3] = font['O']; // Ó
  font[0xd5] = font['O']; // Õ
  font[0xf3] = font['o']; // ó
  font[0xf5] = font['o']; // õ

  font[0xcd] = font['I']; // Í
  font[0xed] = font['i']; // í

  font[0xd9] = font['U']; // Ù
  font[0xda] = font['U']; // Ú
  font[0xfa] = font['u']; // ú
  font[0xf9] = font['u']; // ù

  font[0xc9] = font['E']; // É
  font[0xc8] = font['E']; // È
  font[0xc9] = font['E']; // Ê
  font[0xe9] = font['e']; // é
  font[0xe8] = font['e']; // è
  font[0xea] = font['e']; // ê

  font[0xd1] = font['N']; // Ñ
  font[0xf1] = font['n']; // ñ
}

//-------------------------------------------------------------------------------------------------
//0003F5B0
void title_screens()
{
  winx = 0;
  winy = 0;
  winw = XMAX;
  mirror = 0;
  SVGA_ON = 0;
  winh = YMAX;
  init_screen();
  SVGA_ON = -1;
  init_screen();
  winx = 0;
  winw = XMAX;
  winy = 0;
  winh = YMAX;
  mirror = 0;

  // added by ROLLER, check to see if title.bm and title.pal exist
  // these files were not shipped in the USA localization
  bool bHasTitle = ROLLERfexists("title.bm") && ROLLERfexists("title.pal");

  setpal(bHasTitle ? "title.pal" : "whipped.pal");
  front_vga[0] = (tBlockHeader*)load_picture(bHasTitle ? "title.bm" : "whipped.bm");

  if (front_vga[0] && scrbuf) //check added by ROLLER
    display_picture(scrbuf, front_vga[0]);

  copypic(scrbuf, screen);
  loadfatalsample();
  fade_palette(32);
  if ((cheat_mode & (CHEAT_MODE_KILLER_OPPONENTS | CHEAT_MODE_DEATH_MODE)) != 0)
    dospeechsample(SOUND_SAMPLE_FATAL, 0x8000);
  disable_keyboard();
  if ((cheat_mode & (CHEAT_MODE_KILLER_OPPONENTS | CHEAT_MODE_DEATH_MODE)) != 0)
    waitsampledone(SOUND_SAMPLE_FATAL);
  fre((void**)&front_vga[0]);
  freefatalsample();

  // Add by ROLLER, check language to change font table to support Brazilian / Saspanish.
  if (strcmp(languagename, "Brazilian") == 0 || strcmp(languagename, "Saspanish") == 0) {
    SDL_Log("Language: update font1_ascii and font2_ascii to support '%s'", languagename);
    memcpy(font1_ascii, font1_ascii_br, 256);
    memcpy(font2_ascii, font2_ascii_br, 256);
    memcpy(font3_ascii, font3_ascii_br, 256);
    memcpy(font4_ascii, font4_ascii_br, 256);
    font_ascii_replace_accent((char *)font6_ascii);
    font_ascii_replace_accent((char *)ascii_conv3);
  } 
}

//-------------------------------------------------------------------------------------------------
//0003F6B0
void copy_screens()
{
  SVGA_ON = -1;
  init_screen();
  winx = 0;
  winw = XMAX;
  winy = 0;
  winh = YMAX;
  mirror = 0;

  setpal("gremlin.pal");
  front_vga[0] = (tBlockHeader *)load_picture("gremlin.bm");
  
  display_picture(scrbuf, front_vga[0]);

  fade_palette(32);
  copypic(scrbuf, screen);
  disable_keyboard();
  ticks = 0;
#ifndef _DEBUG
  while (ticks < 180) {
    UpdateSDL();
    UpdateSDLWindow();
  }
#endif
  fre((void**)&front_vga[0]);
  fade_palette(0);
}

//-------------------------------------------------------------------------------------------------
//0003F7B0
void select_screen()
{
  int iMenuSelection; // esi
  int iContinue; // ebp
  int iQuitConfirmed; // edi
  int iPlayer1Car; // eax
  int iNoClear; // eax
  int iPlayer; // edx
  uint8 *pBuf; // edx
  int iPlayer1Car2; // eax
  eCarType carType; // eax
  eCarType carType2; // ebx
  int iCarTexLoaded; // ecx
  int iLoadCarTextures; // edx
  void **ppCartexVgaItr2; // edx
  eCarType cartype3; // eax
  eCarType cartype4; // ebx
  int iCarTexLoaded2; // edx
  int iCurLaps; // eax
  double dRecordLap; // st7
  int iMinutes; // ecx
  int iSeconds; // ebx
  int iBlockIdx2; // ebx
  int iPlayerIdx; // edx
  int iPlayerIdx2; // eax
  uint8 **ppCartexVgaItr_2; // edx
  void **ppCartexVgaToFree_1; // eax
  eCarType carType_2; // eax
  eCarType carTypeBackup; // ebx
  int iCarTexLoaded_1; // edx
  int iCarIdx; // ecx
  int iPlayerIdx_1; // eax
  int iPlayerOffset; // edx
  uint8 **ppCartexVgaItr_1; // edx
  void **ppCartexVgaToFree; // eax
  eCarType iCar; // eax
  eCarType carTypeToLoad; // ebx
  int iCartexLoaded; // ecx
  int iLoadCarTextures_1; // edx
  uint8 byKey; // al
  uint8 byKey2; // al
  void **ppCartexVgaToFree_2; // edx
  eCarType carType_1; // eax
  eCarType carTypeSelected; // ebx
  int iCartexLoaded2; // edx
  int16 nNewYaw; // ax
  int iControl; // edx
  int j; // eax
  int iNonCompetitorIdx; // edx
  int iRacersIdx; // esi
  int iNonCompetitorIdx2; // edx
  int m; // esi
  int iTargetPos; // edx
  int iHumanIdx; // ebx
  int iGridIdx; // eax
  int iNonHumanIdx; // eax
  int iSwapGrid1; // ecx
  int iSwapGrid2; // edx
  int iInitScreen; // [esp+0h] [ebp-54h]
  int16 nFrames; // [esp+4h] [ebp-50h]
  int iRotation; // [esp+Ch] [ebp-48h]
  eCarDesignIndex iBlockIdx; // [esp+10h] [ebp-44h]
  int iLoadCarTex2; // [esp+2Ch] [ebp-28h]
  int iLoadCarTexFlag; // [esp+30h] [ebp-24h]
  int iLoadCarTextures2; // [esp+34h] [ebp-20h]

  // Initialize game state
  time_to_start = 0;
  StartPressed = 0;
  load_language_file(szSelectEng, 0);
  load_language_file(szConfigEng, 1);
  iInitScreen = -1;
  restart_net = 0;
  if (!time_to_start) {
    while (1) {
      cup_won = (textures_off & TEX_OFF_PREMIER_CUP_AVAILABLE) != 0;
      if ((textures_off & TEX_OFF_BONUS_CUP_AVAILABLE) != 0) {
        cup_won |= 2;
        //LOBYTE(cup_won) = cup_won | 2;
      }
      loadfatalsample();
      iContinue = 0;
      iRotation = 0;
      player1_car = 0;
      player2_car = 1;
      if (!network_on) {
        players = 1;
        if (iInitScreen)
          tick_on = 0;
      }
      front_fade = 0;
      frontend_on = -1;
      p_tex_size = gfx_size;

      // Load graphical assets
      front_vga[0] = (tBlockHeader*)load_picture("frontend.bm");
      front_vga[1] = (tBlockHeader*)load_picture("selhead.bm");
      front_vga[2] = (tBlockHeader*)load_picture("font2.bm");
      front_vga[3] = (tBlockHeader*)load_picture("carnames.bm");
      front_vga[4] = (tBlockHeader*)load_picture("opticon2.bm");
      front_vga[5] = (tBlockHeader*)load_picture("selicons.bm");
      front_vga[6] = (tBlockHeader*)load_picture("selexit.bm");
      front_vga[15] = (tBlockHeader *)load_picture("font1.bm");
      fade_palette(0);
      iQuitConfirmed = 0;
      SVGA_ON = -1;
      if (iInitScreen)
        init_screen();
      winx = 0;
      winw = XMAX;
      winy = 0;
      winh = YMAX;
      mirror = 0;
      setpal("frontend.pal");
      if (network_on) {
        Players_Cars[0] = my_car;
        name_copy(player_names[player1_car], my_name);
        iPlayer1Car = player1_car;
        manual_control[player1_car] = my_control;
        player_invul[iPlayer1Car] = my_invul;
        player_type = 1;
        if ((!game_type || game_type == 2) && last_replay != 2) {
          iNoClear = no_clear;
          if (!no_clear && network_on > 0) {
            iPlayer = 0;
            do {
              Players_Cars[iPlayer++] = -1;
              ++iNoClear;
            } while (iNoClear < network_on);
          }
        }
      }
      if (game_type >= 3)
        game_type = last_type;
      replaytype = 0;
      if (network_on && iInitScreen) {
        remove_messages(-1);
        reset_network(0);
      }
      tick_on = -1;
      FindShades();
      check_cars();
      Car[0].nYaw = 0;
      Car[0].nRoll = 0;
      Car[0].pos.fX = 0.0;
      Car[0].pos.fY = 0.0;
      Car[0].pos.fZ = 0.0;
      intro = 0;
      Car[0].nPitch = 0;
      pBuf = (uint8 *)trybuffer(300000u);
      front_vga[7] = (tBlockHeader *)pBuf;
      iMenuSelection = 8;
      if (no_mem)
        goto LABEL_24;
      if (pBuf)
        gfx_size = no_mem;
      else
        LABEL_24:
      gfx_size = 1;
      fre((void **)&front_vga[7]);
      set_starts(0);
      car_texs_loaded[0] = 0;
      for (int i = 1; i < 16; ++i) {
        car_texs_loaded[i] = -1;
      }
      //for (i = 2; i != 16; SmokePt[i + 1023] = -1)// reference to car_texs_loaded
      //{
      //  i += 7;
      //  SmokePt[i + 1017] = -1;
      //  SmokePt[i + 1018] = -1;
      //  SmokePt[i + 1019] = -1;
      //  SmokePt[i + 1020] = -1;
      //  SmokePt[i + 1021] = -1;
      //  SmokePt[i + 1022] = -1;
      //}
      iPlayer1Car2 = Players_Cars[player1_car];
      iBlockIdx = iPlayer1Car2;
      LoadCarTextures = 0;
      if (game_type == 1) {
        loadtrack(TrackLoad, -1);
        fre((void **)&front_vga[3]);
        front_vga[3] = (tBlockHeader *)load_picture("trkname.bm");
        front_vga[13] = (tBlockHeader *)load_picture("bonustrk.bm");
        front_vga[14] = (tBlockHeader *)load_picture("cupicons.bm");
      } else if (iPlayer1Car2 >= 0) {
        carType = CarDesigns[iPlayer1Car2].carType;
        carType2 = carType;
        iCarTexLoaded = car_texs_loaded[carType];
        iLoadCarTextures = 1;
        if (iCarTexLoaded == -1) {
          LoadCarTexture(carType, 1u);
          car_texmap[iBlockIdx] = 1;
          car_texs_loaded[carType2] = 1;
          iLoadCarTextures = 2;
        } else {
          car_texmap[iBlockIdx] = iCarTexLoaded;
        }
        LoadCarTextures = iLoadCarTextures;
      }
      NoOfTextures = 255;
      if (SVGA_ON)
        scr_size = 128;
      else
        scr_size = 64;
      if (!dontrestart)
        startmusic(optionssong);
      dontrestart = 0;
      holdmusic = -1;
      ticks = 0;
      frames = 0;
      if (network_on && iInitScreen) {
        broadcast_mode = -667;
        while (broadcast_mode)
          UpdateSDL();
        name_copy(player_names[player1_car], my_name);
      }
    LABEL_45:
      if (!iContinue)
        break;
      my_car = Players_Cars[player1_car];
      name_copy(my_name, player_names[player1_car]);
      iControl = manual_control[player1_car];
      my_invul = player_invul[player1_car];
      my_control = iControl;
      last_replay = replaytype;
      if (quit_game && network_on) {
        broadcast_mode = -666;
        while (broadcast_mode)
          UpdateSDL();
        tick_on = 0;
        ticks = 0;
        while (ticks < 3)
          ;
        close_network();
      }
      releasesamples();
      if (game_type != 4 && game_type != 3)
        holdmusic = 0;
      fade_palette(0);
      if (game_type != 4 && game_type != 3)
        stopmusic();
      front_fade = 0;
      Players_Cars[player1_car] = iBlockIdx;

      for (int i = 0; i < 16; ++i) {
        fre((void**)&front_vga[i]);
      }
      //ppFrontVgaItr = front_vga;
      //do {
      //  pFrontVgaToFre = (void **)ppFrontVgaItr++;
      //  fre(pFrontVgaToFre);
      //} while (ppFrontVgaItr != &front_vga[16]);

      if (iBlockIdx >= CAR_DESIGN_AUTO) {
        for (int i = 0; i < 16; ++i) {
          fre((void**)&cartex_vga[i]);
        }
        //ppCartexVgaItr = cartex_vga;
        //do {
        //  pCartexVgaToFre = (void **)ppCartexVgaItr++;
        //  fre(pCartexVgaToFre);
        //} while (ppCartexVgaItr != &cartex_vga[16]);
        remove_mapsels();
      }
      gfx_size = p_tex_size;
      no_clear = 0;
      if (!quit_game && !intro) {
        check_cars();
        if (network_on) {
          if (iMenuSelection == 8 && !intro)
            NetworkWait();
        }
      }
      if (iMenuSelection < 8 || !network_on || intro)
        time_to_start = 45;
      if (time_to_start)
        goto LABEL_232;
    }
    iInitScreen = 0;
    if (switch_types) {
      game_type = switch_types - 1;
      switch_types = 0;
      if (!game_type && competitors == 1)
        competitors = 16;
      if (iBlockIdx >= CAR_DESIGN_AUTO) {
        car_texs_loaded[CarDesigns[iBlockIdx].carType] = -1;
        ppCartexVgaItr2 = (void **)cartex_vga;
        do
          fre(ppCartexVgaItr2++);
        while (ppCartexVgaItr2 != (void **)&cartex_vga[16]);
        remove_mapsels();
      }
      if (game_type == 1) {
        loadtrack(TrackLoad, -1);
        fre((void **)&front_vga[3]);
        fre((void **)&front_vga[13]);
        fre((void **)&front_vga[14]);
        front_vga[3] = (tBlockHeader *)load_picture("trkname.bm");
        front_vga[13] = (tBlockHeader *)load_picture("bonustrk.bm");
        front_vga[14] = (tBlockHeader *)load_picture("cupicons.bm");
        Race = ((uint8)TrackLoad - 1) & 7;
      } else {
        fre((void **)&front_vga[3]);
        fre((void **)&front_vga[13]);
        fre((void **)&front_vga[14]);
        front_vga[3] = (tBlockHeader *)load_picture("carnames.bm");
        if (iBlockIdx >= CAR_DESIGN_AUTO) {
          cartype3 = CarDesigns[iBlockIdx].carType;
          cartype4 = cartype3;
          iLoadCarTextures2 = 1;
          iCarTexLoaded2 = car_texs_loaded[cartype3];
          if (iCarTexLoaded2 == -1) {
            LoadCarTexture(cartype3, 1u);
            car_texs_loaded[cartype4] = 1;
            car_texmap[iBlockIdx] = 1;
            iLoadCarTextures2 = 2;
          } else {
            car_texmap[iBlockIdx] = iCarTexLoaded2;
          }
          LoadCarTextures = iLoadCarTextures2;
        }
        network_champ_on = 0;
      }
    }
    nFrames = frames;
    frames = 0;
    if (ticks > 1080 && !iQuitConfirmed && !network_on) {
      intro = -1;
      iContinue = -1;
      replaytype = 2;
    }
    check_cars();

    display_picture(scrbuf, front_vga[0]);
    display_block(scrbuf, front_vga[1], 0, head_x, head_y, 0);
    display_block(scrbuf, front_vga[6], 0, 36, 2, 0);
    if (iMenuSelection >= 8) {
      display_block(scrbuf, front_vga[6], 3, 52, 334, 0);
    } else {
      display_block(scrbuf, front_vga[6], 1, 52, 334, 0);
      front_text(front_vga[2], "~", font2_ascii, font2_offsets, sel_posns[iMenuSelection].x, sel_posns[iMenuSelection].y, 0x8Fu, 0);
    }
    if (game_type == 1) {
      front_text(front_vga[2], language_buffer, font2_ascii, font2_offsets, sel_posns[1].x + 132, sel_posns[1].y + 7, 0x8Fu, 2u);
      if (Race)
        front_text(front_vga[2], &language_buffer[128], font2_ascii, font2_offsets, sel_posns[3].x + 132, sel_posns[3].y + 7, 0x8Fu, 2u);
      else
        front_text(front_vga[2], &language_buffer[64], font2_ascii, font2_offsets, sel_posns[3].x + 132, sel_posns[3].y + 7, 0x8Fu, 2u);
    } else {
      front_text(front_vga[2], &language_buffer[256], font2_ascii, font2_offsets, sel_posns[1].x + 132, sel_posns[1].y + 7, 0x8Fu, 2u);
      front_text(front_vga[2], &language_buffer[320], font2_ascii, font2_offsets, sel_posns[3].x + 132, sel_posns[3].y + 7, 0x8Fu, 2u);
    }
    front_text(front_vga[2], &language_buffer[192], font2_ascii, font2_offsets, sel_posns[0].x + 132, sel_posns[0].y + 7, 0x8Fu, 2u);
    front_text(front_vga[2], config_buffer, font2_ascii, font2_offsets, sel_posns[2].x + 132, sel_posns[2].y + 7, 0x8Fu, 2u);
    front_text(front_vga[2], &language_buffer[384], font2_ascii, font2_offsets, sel_posns[4].x + 132, sel_posns[4].y + 7, 0x8Fu, 2u);
    front_text(front_vga[2], &language_buffer[448], font2_ascii, font2_offsets, sel_posns[5].x + 132, sel_posns[5].y + 7, 0x8Fu, 2u);
    front_text(front_vga[2], &language_buffer[512], font2_ascii, font2_offsets, sel_posns[6].x + 132, sel_posns[6].y + 7, 0x8Fu, 2u);
    front_text(front_vga[2], &config_buffer[640], font2_ascii, font2_offsets, sel_posns[7].x + 132, sel_posns[7].y + 7, 0x8Fu, 2u);
    if (game_type == 1) {
      display_block(scrbuf, front_vga[14], (TrackLoad - 1) / 8, 500, 300, 0);
      if (TrackLoad <= 0) {
        if (TrackLoad)
          front_text(front_vga[2], "EDITOR", font2_ascii, font2_offsets, 190, 350, 0x8Fu, 0);
        else
          front_text(front_vga[2], "TRACK ZERO", font2_ascii, font2_offsets, 190, 350, 0x8Fu, 0);
      } else if (TrackLoad >= 17) {
        display_block(scrbuf, front_vga[13], TrackLoad - 17, 190, 356, 0);
      } else {
        display_block(scrbuf, front_vga[3], TrackLoad - 1, 190, 356, 0);
      }
      show_3dmap(cur_TrackZ, 1280, iRotation);
      if (game_type < 2) {
        iCurLaps = cur_laps[level];
        NoOfLaps = iCurLaps;
        if (competitors == 2)
          NoOfLaps = iCurLaps / 2;
        sprintf(buffer, "%s: %i", &language_buffer[4544], NoOfLaps);
        front_text(front_vga[15], buffer, font1_ascii, font1_offsets, 420, 16, 0x8Fu, 1u);
        front_text(front_vga[15], &language_buffer[4608], font1_ascii, font1_offsets, 420, 34, 0x8Fu, 1u);
        if (RecordCars[TrackLoad] < 0) {
          sprintf(buffer, "%s", RecordNames[TrackLoad]);
        } else {
          dRecordLap = RecordLaps[TrackLoad] * 100.0;
          //_CHP();
          int iLapTime = (int)dRecordLap;
          iMinutes = iLapTime / 6000;
          iSeconds = (iLapTime / 100) % 60;
          int iHundredths = iLapTime % 100;
          sprintf(
              buffer,
              "%s - %s - %02i:%02i:%02i",
              RecordNames[TrackLoad],
              CompanyNames[RecordCars[TrackLoad] & 0xF],
              iMinutes,
              iSeconds,
              iHundredths);

          //dRecordLap = RecordLaps[TrackLoad] * 100.0;
          ////_CHP();
          //SET_LODWORD(llRecordLap, (int)dRecordLap);
          //SET_HIDWORD(llRecordLap, (int)dRecordLap >> 31);
          //iMinutes = llRecordLap / 6000;
          //SET_LODWORD(llRecordLap, (int)dRecordLap);
          //iSeconds = (int)(llRecordLap / 100) % 60;
          //SET_LODWORD(llRecordLap, (int)dRecordLap);
          //sprintf(
          //  buffer,
          //  "%s - %s - %02i:%02i:%02i",
          //  RecordNames[TrackLoad],
          //  CompanyNames[RecordCars[TrackLoad] & 0xF],
          //  iMinutes,
          //  iSeconds,
          //  (unsigned int)(llRecordLap % 100));
        }
        front_text(front_vga[15], buffer, font1_ascii, font1_offsets, 420, 52, 0x8Fu, 1u);
      }
    } else if (iBlockIdx >= CAR_DESIGN_AUTO) {
      if (iBlockIdx == CAR_DESIGN_F1WACK)
        DrawCar(scrbuf + 34640, CAR_DESIGN_F1WACK, 6000.0, 1280, 0);
      else
        DrawCar(scrbuf + 34640, iBlockIdx, 2200.0, 1280, 0);
      if (iBlockIdx < CAR_DESIGN_SUICYCO)
        display_block(scrbuf, front_vga[3], iBlockIdx, 190, 356, 0);
    }
    display_block(scrbuf, front_vga[5], player_type, -4, 247, 0);
    display_block(scrbuf, front_vga[5], game_type + 5, 135, 247, 0);
    switch (iMenuSelection) {
      case 1:
        if (game_type == 1)
          iBlockIdx2 = 8;
        else
          iBlockIdx2 = 1;
        display_block(scrbuf, front_vga[4], iBlockIdx2, 76, 257, -1);
        break;
      case 3:
        if (game_type == 1 && Race > 0)
          goto LABEL_102;
        display_block(scrbuf, front_vga[4], 3, 76, 257, -1);
        break;
      case 6:
      LABEL_102:
        display_block(scrbuf, front_vga[4], 9, 76, 257, -1);
        break;
      case 7:
        display_block(scrbuf, front_vga[4], 6, 76, 257, -1);
        break;
      case 8:
        display_block(scrbuf, front_vga[4], 7, 76, 257, -1);
        break;
      default:
        display_block(scrbuf, front_vga[4], iMenuSelection, 76, 257, -1);
        break;
    }
    if (iBlockIdx < CAR_DESIGN_AUTO)
      front_text(front_vga[15], &language_buffer[4160], font1_ascii, font1_offsets, 400, 200, 0xE7u, 1u);
    if (iQuitConfirmed)
      front_text(front_vga[15], &language_buffer[3456], font1_ascii, font1_offsets, 400, 250, 0xE7u, 1u);
    show_received_mesage();
    copypic(scrbuf, screen);
    if (switch_same > 0) {
      if (game_type != 1 && switch_same - 666 != iBlockIdx) {
        if (iBlockIdx >= CAR_DESIGN_AUTO) {
          car_texs_loaded[CarDesigns[iBlockIdx].carType] = -1;
          ppCartexVgaItr_2 = cartex_vga;
          do {
            ppCartexVgaToFree_1 = (void **)ppCartexVgaItr_2++;
            fre(ppCartexVgaToFree_1);
          } while (ppCartexVgaItr_2 != &cartex_vga[16]);
          remove_mapsels();
        }
        carType_2 = CarDesigns[switch_same - 666].carType;
        carTypeBackup = carType_2;
        iLoadCarTexFlag = 1;
        iCarTexLoaded_1 = car_texs_loaded[carType_2];
        iCarIdx = switch_same - 666;
        if (iCarTexLoaded_1 == -1) {
          LoadCarTexture(carType_2, 1u);
          car_texs_loaded[carTypeBackup] = 1;
          car_texmap[iCarIdx] = 1;
          iLoadCarTexFlag = 2;
        } else {
          car_texmap[iCarIdx] = iCarTexLoaded_1;
        }
        LoadCarTextures = iLoadCarTexFlag;
      }
      iPlayerIdx_1 = 0;
      if (players > 0) {
        iPlayerOffset = 0;
        do {
          iPlayerOffset += 4;
          *(int *)((char *)&infinite_laps + iPlayerOffset) = switch_same - 666;
          ++iPlayerIdx_1;
        } while (iPlayerIdx_1 < players);
      }

      cheat_mode |= CHEAT_MODE_CLONES;
      //iCheatMode = cheat_mode;
      //BYTE1(iCheatMode) = BYTE1(cheat_mode) | 0x40;
      //cheat_mode = iCheatMode;

      iBlockIdx = switch_same - 666;
    } else if (switch_same < 0) {
      switch_same = 0;
      iPlayerIdx = 0;
      if (players > 0) {
        iPlayerIdx2 = 0;
        do {
          Players_Cars[iPlayerIdx2++] = -1;
          ++iPlayerIdx;
        } while (iPlayerIdx < players);
      }

      cheat_mode &= ~CHEAT_MODE_CLONES;
      //iCheatMode2 = cheat_mode;
      //BYTE1(iCheatMode2) = BYTE1(cheat_mode) & 0xBF;
      //cheat_mode = iCheatMode2;
    }
    if (switch_sets) {
      if (game_type != 1 && iBlockIdx >= CAR_DESIGN_AUTO) {
        ppCartexVgaItr_1 = cartex_vga;
        car_texs_loaded[CarDesigns[iBlockIdx].carType] = -1;
        do {
          ppCartexVgaToFree = (void **)ppCartexVgaItr_1++;
          fre(ppCartexVgaToFree);
        } while (ppCartexVgaItr_1 != &cartex_vga[16]);
        remove_mapsels();
        iCar = CarDesigns[iBlockIdx].carType;
        carTypeToLoad = iCar;
        iCartexLoaded = car_texs_loaded[iCar];
        iLoadCarTextures_1 = 1;
        if (iCartexLoaded == -1) {
          LoadCarTexture(iCar, 1u);
          car_texmap[iBlockIdx] = 1;
          car_texs_loaded[carTypeToLoad] = 1;
          iLoadCarTextures_1 = 2;
        } else {
          car_texmap[iBlockIdx] = iCartexLoaded;
        }
        LoadCarTextures = iLoadCarTextures_1;
      }
      switch_sets = 0;
    }
    if (!front_fade) {
      front_fade = -1;
      fade_palette(32);
      frames = 0;
      if (network_on) {
        while (broadcast_mode)
          UpdateSDL();
        broadcast_mode = -1;
        while (broadcast_mode)
          UpdateSDL();
      }
    }
    print_data = 0;
    while (1) {
      UpdateSDL();
      while (1) {
        UpdateSDL();
        if (!fatkbhit()) {
          nNewYaw = Car[0].nYaw + 32 * nFrames;
          //HIBYTE(nNewYaw) &= 0x3Fu;
          nNewYaw &= 0x3FFF;
          Car[0].nYaw = nNewYaw;
          iRotation = ((uint16)iRotation + 32 * nFrames) & 0x3FFF;
          goto LABEL_45;
        }
        ticks = 0;
        byKey = fatgetch();
        if (iQuitConfirmed)
          break;
        if (byKey) {
          if (byKey == 13)                    // KEY_ENTER
          {
            if (iBlockIdx >= CAR_DESIGN_AUTO) {
              ppCartexVgaToFree_2 = (void **)cartex_vga;
              car_texs_loaded[CarDesigns[iBlockIdx].carType] = -1;
              do
                fre(ppCartexVgaToFree_2++);
              while (ppCartexVgaToFree_2 != (void **)&cartex_vga[16]);
              remove_mapsels();
            }
            fre((void **)&front_vga[3]);
            fre((void **)&front_vga[13]);
            fre((void **)&front_vga[14]);
            switch (iMenuSelection) {
              case 0:
                sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);
                select_car();
                break;
              case 1:
                sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);
                if (game_type == 1)
                  select_disk();
                else
                  select_track();
                break;
              case 2:
                sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);
                select_configure();
                break;
              case 3:
                sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);
                if (game_type == 1 && Race > 0) {
                  last_type = game_type;
                  game_type = 3;
                  iContinue = -1;
                } else {
                  select_players();
                }
                break;
              case 4:
                sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);
                select_type();
                break;
              case 5:
                iContinue = -1;
                sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);
                replaytype = 2;
                break;
              case 6:
                last_type = game_type;
                game_type = 4;
                iContinue = -1;
                break;
              case 7:
                iQuitConfirmed = -1;
                sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);
                break;
              case 8:
                if (iBlockIdx >= CAR_DESIGN_AUTO) {
                  iContinue = -1;
                  sfxsample(SOUND_SAMPLE_START, 0x8000);
                  netCD = 0;
                  int iTargetTicks = ticks + 108;
                  if (soundon && iTargetTicks > ticks) {
                    while (iTargetTicks > ticks)
                      UpdateSDL();
                  }
                  while (fatkbhit()) {
                    UpdateSDL();
                    fatgetch();
                  }
                  replaytype = replay_record;
                }
                break;
              default:
                break;
            }
            fre((void **)&front_vga[3]);
            fre((void **)&front_vga[13]);
            fre((void **)&front_vga[14]);
            iBlockIdx = Players_Cars[player1_car];
            if (game_type == 1) {
              loadtrack(TrackLoad, -1);
              front_vga[3] = (tBlockHeader *)load_picture("trkname.bm");
              front_vga[13] = (tBlockHeader *)load_picture("bonustrk.bm");
              front_vga[14] = (tBlockHeader *)load_picture("cupicons.bm");
            } else {
              front_vga[3] = (tBlockHeader *)load_picture("carnames.bm");
              if (iBlockIdx >= CAR_DESIGN_AUTO) {
                carType_1 = CarDesigns[iBlockIdx].carType;
                iLoadCarTex2 = 1;
                carTypeSelected = carType_1;
                iCartexLoaded2 = car_texs_loaded[carType_1];
                if (iCartexLoaded2 == -1) {
                  LoadCarTexture(carType_1, 1u);
                  car_texs_loaded[carTypeSelected] = 1;
                  car_texmap[iBlockIdx] = 1;
                  iLoadCarTex2 = 2;
                } else {
                  car_texmap[iBlockIdx] = iCartexLoaded2;
                }
                LoadCarTextures = iLoadCarTex2;
              }
            }
            ticks = 0;
            frames = 0;
          }
        } else {
          byKey2 = fatgetch();
          if (byKey2 >= 0x48u) {
            if (byKey2 <= 0x48u)              // KEY_DOWN
            {
              if (--iMenuSelection < 0)
                iMenuSelection = 0;
            } else if (byKey2 == 80 && ++iMenuSelection > 8)// KEY_UP
            {
              iMenuSelection = 8;
            }
          }
        }
      }
      if (byKey < 0x59u) {
        if (!byKey)
          fatgetch();
      LABEL_155:
        iQuitConfirmed = 0;
      } else {
        if (byKey > 0x59u && byKey != 0x79)
          goto LABEL_155;
        iContinue = -1;
        quit_game = -1;
      }
    }
  }
LABEL_232:
  tick_on = -1;
  messages = -1;
  demo_count = 0;
  demo_control = -1;
  old_mode = -1;
  demo_mode = 0;
  if (replaytype != 2 && !quit_game && game_type < 3)
    AllocateCars();
  if (iMenuSelection == 8 && time_to_start && !intro) {
    localCD = -1;
    if (replaytype != 2) {
      if (player_type && player_type != 2) {
        localCD = cdpresent();
        if (localCD)
          netCD = -1;
      } else {
        localCD = cdpresent();
        //removed by ROLLER, don't error for no CD
        //if (!localCD)
        //  cd_error = -1;
      }
    }
    Race = ((uint8)TrackLoad - 1) & 7;
    if (game_type == 1 && !Race) {
      memset(championship_points, 0, sizeof(championship_points));
      memset(team_points, 0, sizeof(team_points));
      memset(total_kills, 0, sizeof(total_kills));
      memset(total_fasts, 0, sizeof(total_fasts));
      memset(total_wins, 0, sizeof(total_wins));
      memset(team_kills, 0, sizeof(team_kills));
      memset(team_fasts, 0, sizeof(team_fasts));
      memset(team_wins, 0, sizeof(team_wins));
    }
    racers = competitors;
    if (competitors == 2) {
      racers = players;
      if (players < 2)
        racers = competitors;
    }
    if (competitors == 1)
      racers = players;
    j = racers;
    iNonCompetitorIdx = 0;
    if (racers > 0) {
      int iMaxRacers = racers;
      int iNonCompetitorIdx = 0;
      for (int i = 0; i < iMaxRacers; i++)
      {
        // Skip occupied non-competitor slots
        while (non_competitors[iNonCompetitorIdx] != 0)
        {
          iNonCompetitorIdx++;
        }
        // Store the index of the first available non-competitor slot
        grid[i] = iNonCompetitorIdx; 
        // Move to next slot for next iteration
        iNonCompetitorIdx++;
      }
      //iMaxRacerOffset = 4 * racers;
      //iGridOffset = 0;
      //do {
      //  for (j = 4 * iNonCompetitorIdx; *(int *)((char *)non_competitors + j); j += 4)
      //    ++iNonCompetitorIdx;
      //  grid[iGridOffset / 4u] = iNonCompetitorIdx;
      //  iGridOffset += 4;
      //  ++iNonCompetitorIdx;
      //} while (iGridOffset < iMaxRacerOffset);
    }
    iRacersIdx = racers;
    iNonCompetitorIdx2 = 0;
    if (racers < numcars) {
      for (int i = racers; i < numcars; i++)
      {
        // Skip empty non-competitor slots (find occupied ones)
        while (non_competitors[iNonCompetitorIdx2] == 0)
        {
          iNonCompetitorIdx2++;
        }
        iRacersIdx++;
        // Store the index of the occupied non-competitor slot
        grid[i] = iNonCompetitorIdx2;
        // Move to next slot for next iteration
        iNonCompetitorIdx2++;
      }
      //iRacersOffset2 = 4 * racers;
      //iMaxRacersOffset2 = 4 * numcars;
      //do {
      //  for (j = 4 * iNonCompetitorIdx2; !*(int *)((char *)non_competitors + j); j += 4)
      //    ++iNonCompetitorIdx2;
      //  ++iRacersIdx;
      //  grid[iRacersOffset2 / 4u] = iNonCompetitorIdx2;
      //  iRacersOffset2 += 4;
      //  ++iNonCompetitorIdx2;
      //} while (iRacersOffset2 < iMaxRacersOffset2);
    }
    if (game_type == 1 && Race > 0) {
      if (racers > 0) {
        for (int i = 0; i <= racers; i++)
        {
          grid[i] = champorder[i];
        }
        //iRacersBytes = 4 * racers;
        //iOffset = 0;
        //do {
        //  iOffset += 4;
        //  // offsets into grid and champorder
        //  finished_car[iOffset / 4 + 15] = teamorder[iOffset / 4 + 7];
        //} while ((int)iOffset < iRacersBytes);
      }
    } else {
      int iShuffleIterations = 6 * racers;
      for (int k = 0; k < iShuffleIterations; k++)
      {
          // Generate two random indices within the racers range
          //int iRandIdx1 = rand() % racers;
          //int iRandIdx2 = rand() % racers;
          int iRandIdx1 = GetHighOrderRand(racers, rand());
          int iRandIdx2 = GetHighOrderRand(racers, rand());
          
          // Swap grid elements
          int iGridTemp = grid[iRandIdx1];
          grid[iRandIdx1] = grid[iRandIdx2];
          grid[iRandIdx2] = iGridTemp;
      }
      //iRacers = racers;
      //iRacersOffset = 6 * racers;
      //for (k = 0; k < iRacersOffset; grid[j] = iGridTemp) {
      //  iRandVal1 = rand(j);
      //  iRandIdx1 = (iRacers * iRandVal1
      //             - (__CFSHL__((iRacers * iRandVal1) >> 31, 15)
      //                + ((iRacers * iRandVal1) >> 31 << 15))) >> 15;
      //  iRandVal2 = rand(iRandIdx1);
      //  j = (iRacers * iRandVal2 - (__CFSHL__((iRacers * iRandVal2) >> 31, 15) + ((iRacers * iRandVal2) >> 31 << 15))) >> 15;
      //  iGridTemp = grid[iRandIdx1];
      //  grid[iRandIdx1] = grid[j];
      //  ++k;
      //}

      for (m = 0; m < players; ++m) {
        // calculate target starting pos based on difficulty
        if (level && (cheat_mode & 2) == 0)
          iTargetPos = racers - 2 * level * players;
        else
          iTargetPos = racers - players;
        if (iTargetPos < 0)
          iTargetPos = 0;

        // find the first human-controlled player in grid
        iHumanIdx = 0;
        for (iGridIdx = 0; !human_control[grid[iGridIdx]]; ++iGridIdx)
          ++iHumanIdx;

        // if human is starting too far back, move them forward
        if (iHumanIdx < iTargetPos) {
          // Find first non-human player at or after target pos
          for (iNonHumanIdx = iTargetPos; ; ++iNonHumanIdx) {
            iSwapGrid1 = grid[iNonHumanIdx];
            if (!human_control[iSwapGrid1])
              break;
            ++iTargetPos;
          }
          iSwapGrid2 = grid[iHumanIdx];
          grid[iHumanIdx] = iSwapGrid1;
          grid[iNonHumanIdx] = iSwapGrid2;
        }
      }

    }
  }
  StartPressed = 0;
  if (game_type != 4 && game_type != 3)
    stopmusic();
}

//-------------------------------------------------------------------------------------------------
//000411D0
void select_disk()
{
  int iSelectedSlot; // esi
  unsigned int uiMenuMode; // edi
  unsigned int uiCupIndex; // eax
  uint8 bySlotColor; // al
  int iSlotNumber; // ecx
  unsigned int uiSaveCupIndex; // eax
  uint8 byCupColor1; // al
  uint8 byCupColor2; // al
  uint8 byCupColor3; // al
  uint8 byTrackColor; // al
  uint8 byDifficultyColor; // al
  uint8 byLevelColor; // al
  uint8 byDamageColor; // al
  uint8 byPlayerTypeColor; // al
  uint8 byEmptySlotColor; // al
  uint32 uiUpdatedCheatFlags; // eax
  int iCheatSetLoop; // ebx
  uint8 byInputKey; // al
  uint8 byExtendedKey; // al
  int iSlotYPosition; // [esp+0h] [ebp-24h]
  int iChampResult; // [esp+4h] [ebp-20h]
  int iExitFlag; // [esp+8h] [ebp-1Ch]
  int iSaveTrackNumber; // [esp+Ch] [ebp-18h]
  int iY; // [esp+10h] [ebp-14h]
  int iSaveArrayIndex; // [esp+14h] [ebp-10h]
  int iStatusMessage; // [esp+18h] [ebp-Ch]
  int iSlotLoop; // [esp+1Ch] [ebp-8h]
  int iMenuCursor; // [esp+20h] [ebp-4h]

  fade_palette(0);                              // Initialize screen fade and menu state variables
  uiMenuMode = 0;
  front_fade = 0;
  iExitFlag = 0;
  iMenuCursor = 2;
  iStatusMessage = 0;
  check_saves();                                // Check save file status and scan for existing championship saves
  do {                                             // Handle game type switches and updates
    if (switch_types) {
      game_type = switch_types - 1;
      if (switch_types == 1 && competitors == 1)
        competitors = 16;
      switch_types = 0;
      if (game_type == 1)
        Race = ((uint8)TrackLoad - 1) & 7;
      else
        network_champ_on = 0;
    }
    if (!uiMenuMode)
      iSelectedSlot = 0;
    display_picture(scrbuf, front_vga[0]);
    display_block(scrbuf, front_vga[6], 0, 36, 2, 0);
    display_block(scrbuf, front_vga[1], 0, head_x, head_y, 0);
    if (iMenuCursor >= 2)                     // Draw selection cursor
    {
      display_block(scrbuf, front_vga[6], 4, 62, 336, -1);
    } else {
      display_block(scrbuf, front_vga[6], 2, 62, 336, -1);
      front_text(front_vga[2], "~", font2_ascii, font2_offsets, sel_posns[iMenuCursor].x, sel_posns[iMenuCursor].y, 0x8Fu, 0);
    }
    front_text(front_vga[2], &language_buffer[576], font2_ascii, font2_offsets, sel_posns[0].x + 132, sel_posns[0].y + 7, 0x8Fu, 2u); // Display main menu options: "Load Game" and "Save Game"
    front_text(front_vga[2], &language_buffer[640], font2_ascii, font2_offsets, sel_posns[1].x + 132, sel_posns[1].y + 7, 0x8Fu, 2u);
    front_text(front_vga[15], &language_buffer[704], font1_ascii, font1_offsets, 400, 270, 0xABu, 1u);// CURRENT GAME INFO: Display current championship settings and progress
    front_text(front_vga[15], &language_buffer[768], font1_ascii, font1_offsets, 400, 290, 0x8Fu, 2u);
    uiCupIndex = (TrackLoad - 1) / 8;
    //uiCupIndex = (TrackLoad - 1 - (__CFSHL__((TrackLoad - 1) >> 31, 3) + 8 * ((TrackLoad - 1) >> 31))) >> 3;// Show current cup name based on track group
    if (uiCupIndex) {
      if (uiCupIndex <= 1) {
        front_text(front_vga[15], &language_buffer[896], font1_ascii, font1_offsets, 405, 290, 0x8Fu, 0);
        goto LABEL_20;
      }
      if (uiCupIndex == 2) {
        front_text(front_vga[15], &language_buffer[4928], font1_ascii, font1_offsets, 405, 290, 0x8Fu, 0);
        goto LABEL_20;
      }
    }
    front_text(front_vga[15], &language_buffer[832], font1_ascii, font1_offsets, 405, 290, 0x8Fu, 0);
  LABEL_20:
    front_text(front_vga[15], &language_buffer[960], font1_ascii, font1_offsets, 400, 308, 0x8Fu, 2u);
    front_text(front_vga[15], CompanyNames[Race], font1_ascii, font1_offsets, 405, 308, 0x8Fu, 0);
    front_text(front_vga[15], &language_buffer[1024], font1_ascii, font1_offsets, 400, 326, 0x8Fu, 2u);
    if ((unsigned int)competitors < 8) {
      if (competitors == 2)
        front_text(front_vga[15], &language_buffer[1088], font1_ascii, font1_offsets, 405, 326, 0x8Fu,0);
    } else if ((unsigned int)competitors <= 8) {
      front_text(front_vga[15], &language_buffer[1152], font1_ascii, font1_offsets, 405, 326, 0x8Fu, 0);
    } else if (competitors == 16) {
      front_text(front_vga[15], &language_buffer[1216], font1_ascii, font1_offsets, 405, 326, 0x8Fu, 0);
    }
    front_text(front_vga[15], &language_buffer[1280], font1_ascii, font1_offsets, 400, 344, 0x8Fu, 2u);
    front_text(front_vga[15], &language_buffer[64 * level + 1472], font1_ascii, font1_offsets, 405, 344, 0x8Fu, 0);
    front_text(front_vga[15], &language_buffer[1344], font1_ascii, font1_offsets, 400, 362, 0x8Fu, 2u);
    front_text(front_vga[15], &language_buffer[64 * damage_level + 1856], font1_ascii, font1_offsets, 405, 362, 0x8Fu, 0);
    front_text(front_vga[15], &language_buffer[1408], font1_ascii, font1_offsets, 400, 380, 0x8Fu, 2u);
    if (player_type == 1 && net_type) {
      if ((unsigned int)net_type >= (unsigned int)player_type && (unsigned int)net_type <= 2)
        front_text(front_vga[15], &language_buffer[2304], font1_ascii, font1_offsets, 405, 380, 0x8Fu,0);
    } else {
      front_text(front_vga[15], &language_buffer[64 * player_type + 2112], font1_ascii, font1_offsets, 405, 380, 0x8Fu, 0);
    }
    iSlotLoop = 0;                              // SAVE SLOT DISPLAY: Show all 4 championship save slots with their details
    iSlotYPosition = 56;
    iSaveArrayIndex = 0;
    iY = 74;
    do {
      sprintf(buffer, "%s %i:", &language_buffer[2432], iSlotLoop + 1);// Display slot number with highlighting for currently selected slot
      if (iSelectedSlot == iSlotLoop + 1)
        bySlotColor = 0xAB;
      else
        bySlotColor = 0x8F;
      front_text(front_vga[15], buffer, font1_ascii, font1_offsets, 300, iSlotYPosition, bySlotColor, 2u);
      iSlotNumber = iSlotLoop + 1;
      if (save_status[iSaveArrayIndex].iSlotUsed)// Show save slot contents: cup, track, difficulty, damage, player type
      {
        uiSaveCupIndex = (save_status[iSaveArrayIndex].iPackedTrack - 1) / 8;
        iSaveTrackNumber = ((save_status[iSaveArrayIndex].iPackedTrack - 1) % 8) + 1;
        //uiSaveCupIndex = (save_status[iSaveArrayIndex].iPackedTrack
        //                - 1
        //                - (__CFSHL__((save_status[iSaveArrayIndex].iPackedTrack - 1) >> 31, 3)
        //                   + 8 * ((save_status[iSaveArrayIndex].iPackedTrack - 1) >> 31))) >> 3;
        //iSaveTrackNumber = ((LOBYTE(save_status[iSaveArrayIndex].iPackedTrack) - 1) & 7) + 1;
        if (uiSaveCupIndex) {
          if (uiSaveCupIndex <= 1) {
            if (iSelectedSlot == iSlotNumber)
              byCupColor2 = 0xAB;
            else
              byCupColor2 = 0x8F;
            front_text(front_vga[15], &language_buffer[896], font1_ascii, font1_offsets, 305, iSlotYPosition, byCupColor2, 0);
          } else if (uiSaveCupIndex == 2) {
            if (iSelectedSlot == iSlotNumber)
              byCupColor3 = 0xAB;
            else
              byCupColor3 = 0x8F;
            front_text(front_vga[15], &language_buffer[4928], font1_ascii, font1_offsets, 305, iSlotYPosition, byCupColor3, 0);
          }
        } else {
          if (iSelectedSlot == iSlotNumber)
            byCupColor1 = 0xAB;
          else
            byCupColor1 = 0x8F;
          front_text(front_vga[15], &language_buffer[832], font1_ascii, font1_offsets, 305, iSlotYPosition, byCupColor1, 0);
        }
        sprintf(buffer, "%s %i", &language_buffer[256], iSaveTrackNumber);
        if (iSelectedSlot == iSlotLoop + 1)
          byTrackColor = 0xAB;
        else
          byTrackColor = 0x8F;
        front_text(front_vga[15], "-", font1_ascii, font1_offsets, 470, iSlotYPosition, byTrackColor, 0);
        if (iSelectedSlot == iSlotLoop + 1)
          byDifficultyColor = 0xAB;
        else
          byDifficultyColor = 0x8F;
        front_text(front_vga[15], buffer, font1_ascii, font1_offsets, 480, iSlotYPosition, byDifficultyColor, 0);
        if (iSelectedSlot == iSlotLoop + 1)
          byLevelColor = 0xAB;
        else
          byLevelColor = 0x8F;
        front_text(front_vga[15], &language_buffer[64 * save_status[iSaveArrayIndex].iDifficulty + 1472], font1_ascii, font1_offsets, 460, iY,  byLevelColor, 2u);
        if (iSelectedSlot == iSlotLoop + 1)
          byDamageColor = 0xAB;
        else
          byDamageColor = 0x8F;
        front_text(front_vga[15], "-", font1_ascii, font1_offsets, 470, iY, byDamageColor, 0);
        if (iSelectedSlot == iSlotLoop + 1)
          byPlayerTypeColor = 0xAB;
        else
          byPlayerTypeColor = 0x8F;
        front_text(front_vga[15], &language_buffer[64 * save_status[iSaveArrayIndex].iPlayerType + 2112], font1_ascii, font1_offsets, 480, iY, byPlayerTypeColor, 0);
      } else {                                         // Display "Empty" for unused save slots
        if (iSelectedSlot == iSlotNumber)
          byEmptySlotColor = 0xAB;
        else
          byEmptySlotColor = 0x8F;
        front_text(front_vga[15], &language_buffer[2496], font1_ascii, font1_offsets, 305, iSlotYPosition, byEmptySlotColor, 0);
      }
      iSlotYPosition += 40;
      ++iSaveArrayIndex;
      iY += 40;
      ++iSlotLoop;
    } while (iSlotLoop < 4);
    switch (iStatusMessage) {
      case 0:
        if (network_on)                       // Case 0: Network save restriction message
          front_text(front_vga[15], &language_buffer[4864], font1_ascii, font1_offsets, 400, 230, 0xE7u, 1u);
        break;
      case 1:
        if (iChampResult)                     // Case 1: Load operation messages (success/confirmation)
          front_text(front_vga[15], &language_buffer[2624], font1_ascii, font1_offsets, 400, 230, 0xE7u, 1u);
        else
          front_text(front_vga[15], &language_buffer[2560], font1_ascii, font1_offsets, 400, 230, 0xE7u, 1u);
        break;
      case 2:
        front_text(front_vga[15], &language_buffer[2688], font1_ascii, font1_offsets, 400, 230, 0xE7u, 1u); // Case 2: Save operation success message
        break;
      case 4:
        front_text(front_vga[15], &language_buffer[2752], font1_ascii, font1_offsets, 400, 230, 0xE7u, 1u); // Case 4: Empty slot selected (no save to load)
        break;
      default:
        break;                                  // STATUS MESSAGES: Display operation results and warnings
    }
    show_received_mesage();
    copypic(scrbuf, screen);
    if (switch_same > 0)                      // CHEAT MODE HANDLING: Process switch_same command for player synchronization
    {
      iCheatSetLoop = 0;
      if (players > 0) {
        for (int i = 0; i < players; i++)
        {
            Players_Cars[i] = switch_same - 666;
        }
        //iPlayersCarsOffset = 0;
        //do {
        //  iPlayersCarsOffset += 4;
        //  ++iCheatSetLoop;
        //  *(int *)((char *)&infinite_laps + iPlayersCarsOffset) = switch_same - 666;// offset into Players_Cars
        //} while (iCheatSetLoop < players);
      }
      uiUpdatedCheatFlags = cheat_mode;
      //BYTE1(uiUpdatedCheatFlags) = BYTE1(cheat_mode) | 0x40;// |= CHEAT_MODE_CLONES;
      uiUpdatedCheatFlags |= CHEAT_MODE_CLONES;
    } else {
      if (switch_same >= 0)
        goto LABEL_95;
      switch_same = 0;

      for (int i = 0; i < players; i++)
      {
          Players_Cars[i] = -1;
      }
      //iCheatResetLoop = 0;
      //if (players > 0) {
      //  iCheatResetOffset = 0;
      //  do {
      //    iCheatResetOffset += 4;
      //    ++iCheatResetLoop;
      //    *(int *)((char *)&infinite_laps + iCheatResetOffset) = -1;// offset into Players_Cars
      //  } while (iCheatResetLoop < players);
      //}
      uiUpdatedCheatFlags = cheat_mode;
      //BYTE1(uiUpdatedCheatFlags) = BYTE1(cheat_mode) & 0xBF;// &= ~CHEAT_MODE_CLONES
      uiUpdatedCheatFlags &= ~CHEAT_MODE_CLONES;
    }
    cheat_mode = uiUpdatedCheatFlags;
  LABEL_95:
    if (!front_fade)                          // Handle screen fade-in effect
    {
      front_fade = -1;
      fade_palette(32);
      frames = 0;
    }
    while (fatkbhit())                        // KEYBOARD INPUT PROCESSING: Handle navigation and save/load operations
    {
      byInputKey = fatgetch();
      if (byInputKey < 0xDu) {                                         // Arrow keys: Navigate between menu options and save slots
        if (!byInputKey) {
          byExtendedKey = fatgetch();
          if (byExtendedKey >= 0x48u) {
            if (byExtendedKey <= 0x48u) {                                   // Up arrow: Move up in save slot selection or main menu
              if (uiMenuMode) {
                if (iSelectedSlot > 1) {
                  iStatusMessage = 0;
                  --iSelectedSlot;
                }
              } else if (iMenuCursor > 0) {
                iStatusMessage = 0;
                --iMenuCursor;
              }
            } else if (byExtendedKey == 80) {                                   // Down arrow: Move down in save slot selection or main menu
              if (uiMenuMode) {
                if (iSelectedSlot < 4) {
                  iStatusMessage = 0;
                  ++iSelectedSlot;
                }
              } else if (iMenuCursor < 2) {
                iStatusMessage = 0;
                ++iMenuCursor;
              }
            }
          }
        }
      } else if (byInputKey <= 0xDu) {                                         // Enter key: Execute save/load operation based on current menu and selection
        if (uiMenuMode) {
          if (uiMenuMode <= 1) {
            save_champ(iSelectedSlot);          // SAVE operation: Save current championship to selected slot
            uiMenuMode = 0;
            check_saves();
            iStatusMessage = 2;
          } else {
            iStatusMessage = 1;
            if (save_status[iSelectedSlot - 1].iSlotUsed) {
              uiMenuMode = 0;                   // LOAD operation: Load championship from selected slot
              iChampResult = load_champ(iSelectedSlot);
            } else {
              iStatusMessage = 4;
            }
          }
        } else if (iMenuCursor) {
          if ((unsigned int)iMenuCursor <= 1) {
            uiMenuMode = 2;                     // \"Save Game\" selected: Enter save slot selection mode
            iSelectedSlot = 1;
          } else if (iMenuCursor == 2) {
            goto LABEL_128;
          }
        } else {
          uiMenuMode = 1;                       // \"Load Game\" selected: Enter save slot selection mode
          iSelectedSlot = 1;
        }
      } else if (byInputKey == 27) {                                         // Escape key: Cancel operation or exit to main menu
        if (uiMenuMode) {
          uiMenuMode = 0;
          iStatusMessage = 0;
        } else {
        LABEL_128:
          iExitFlag = -1;
        }
      }
      UpdateSDL();
    }
    UpdateSDL();
  } while (!iExitFlag);                         // MAIN MENU LOOP - Handle UI rendering and input processing
  fade_palette(0);                              // CLEANUP: Fade screen and exit save/load menu
  front_fade = 0;
}

//-------------------------------------------------------------------------------------------------
//00041CA0
void select_car()
{
  eCarType currentCarType; // ebx
  int iSelectedCar; // ebp
  eCarDesignIndex iPlayer1Car; // edi
  eCarType carType; // eax
  int iLoadCarTextures; // edx
  int iCartexLoaded; // esi
  int iCarIdx; // eax
  int iCarPieOffset; // edx
  int iInvertedPieValue; // ebx
  int iCurrentCarSelectorPos; // esi
  int iPlayerNameX; // eax
  int iAnimationCounter; // eax
  int iPieChartIndex; // edx
  int iCarStatsOffset; // ebx
  int iCurrentPieValue; // ecx
  int iNextPieValue; // ecx
  int iNewZoomDistance; // ebx
  int iCarAllocationStatus; // edx
  eCarDesignIndex iCarDesignIndex; // eax
  uint8 **ppTextureArray; // edi
  void **ppCurrentTexture; // eax
  int iPlayerCarIndex; // eax
  eCarType eNewCarType; // eax
  int iTextureLoadedStatus; // ecx
  int iLoadTextureFlag; // edx
  int iPlayerLoopCounter; // edx
  unsigned int uiNavigationDirection; // ebx
  uint8 byInputKey; // al
  int16 nRotationAngle; // ax
  uint8 **ppCleanupTextureArray; // edx
  void **ppCleanupTexture; // eax
  float fCarDrawDistance; // [esp+0h] [ebp-80h]
  unsigned int uiPlayerIndex; // [esp+8h] [ebp-78h]
  int blockIdxAy[7]; // [esp+Ch] [ebp-74h] BYREF
  int iStatAnimationFrame; // [esp+28h] [ebp-58h]
  int iOriginalCarSelection; // [esp+2Ch] [ebp-54h]
  int iActivePlayer; // [esp+30h] [ebp-50h]
  int iDelayBeforeRotation; // [esp+34h] [ebp-4Ch]
  int iZoomSpeed; // [esp+38h] [ebp-48h]
  int iZoomDistance; // [esp+3Ch] [ebp-44h]
  int byMenuExitFlag; // [esp+40h] [ebp-40h]
  unsigned int uiNetworkPlayerIndex; // [esp+44h] [ebp-3Ch]
  char *szPlayerName; // [esp+48h] [ebp-38h]
  char *szCurrentCompanyName = '\0'; // [esp+4Ch] [ebp-34h]
  int iTargetPieValue; // [esp+50h] [ebp-30h]
  int iNextCarIndex; // [esp+58h] [ebp-28h]
  unsigned int uiNetworkLoop; // [esp+5Ch] [ebp-24h]
  int iPieChartY; // [esp+60h] [ebp-20h]
  int iNetworkPlayerCount; // [esp+64h] [ebp-1Ch]

  fade_palette(0);                              // Initialize screen fade and prepare UI graphics
  front_fade = 0;
  front_vga[3] = (tBlockHeader*)load_picture("carnames.bm");   // Load car company name graphics (carnames.bm) and car selection UI (selcar2.bm)
  front_vga[7] = (tBlockHeader*)load_picture("selcar2.bm");    // Load car selection UI bitmap
  car_request = 0;
  byMenuExitFlag = 0;                           // Initialize selection state: byMenuExitFlag=0 (stay in menu), iOriginalCarSelection=-1 (no backup)
  iOriginalCarSelection = -1;                   // byExitCode = 0 - stay in selection, -1 = exit to menu
  iPlayer1Car = Players_Cars[player1_car];      // iSavedCarIndex = -1 initially, stores original car selection
  if (game_type == 1)                         // Championship mode: restrict car changes except for first race (Race == 0)
  {
    Race = ((uint8)TrackLoad - 1) & 7;
    if ((((uint8)TrackLoad - 1) & 7) != 0)
      iOriginalCarSelection = iPlayer1Car;
  }
  if (iPlayer1Car >= CAR_DESIGN_AUTO) {
    carType = CarDesigns[iPlayer1Car].carType;
    currentCarType = carType;
    iLoadCarTextures = 1;
    iCartexLoaded = car_texs_loaded[carType];
    if (iCartexLoaded == -1) {
      LoadCarTexture(carType, 1u);              // Car texture not loaded yet - load it now and mark as loaded
      iLoadCarTextures = 2;
      car_texmap[iPlayer1Car] = 1;
      car_texs_loaded[currentCarType] = 1;
    } else {
      car_texmap[iPlayer1Car] = iCartexLoaded;
    }
    LoadCarTextures = iLoadCarTextures;
  }
  if (iPlayer1Car < CAR_DESIGN_AUTO)          // Initialize car statistics pie chart display blocks (7 stats: Speed, Acceleration, etc.)
  {
    memset(blockIdxAy, 8, 7 * sizeof(int));
    //_STOSD(blockIdxAy, 8, currentCarType * 4, 7u);
  } else {
    iCarIdx = 0;
    iCarPieOffset = 7 * iPlayer1Car;
    do {
      ++iCarIdx;
      iInvertedPieValue = 9 - car_pies[iCarPieOffset++];
      blockIdxAy[iCarIdx - 1] = iInvertedPieValue;
    } while (iCarIdx < 7);
  }
  iZoomDistance = 2000;                         // Setup 3D car zoom animation: start at 2000 units, zoom out to 40000 (-2000 speed)
  iZoomSpeed = -2000;
  if (iPlayer1Car >= CAR_DESIGN_SUICYCO)       // Position car selector cursor
    iCurrentCarSelectorPos = 8;
  else
    iCurrentCarSelectorPos = iPlayer1Car;
  if (iCurrentCarSelectorPos < 0)             // Clamp car selector position to valid range (0-8, where 8 = random)
    iCurrentCarSelectorPos = 8;
  iDelayBeforeRotation = 36;                    // 36-frame delay before starting car rotation animation
  iActivePlayer = 0;
  frames = 0;
  if (!byMenuExitFlag) {
    if (iOriginalCarSelection >= 0) //check added by ROLLER for zig build
      szCurrentCompanyName = CompanyNames[iOriginalCarSelection];
    do {                                           // Handle game type switches (race type, championship, etc.)
      if (switch_types) {
        game_type = switch_types - 1;
        if (switch_types == 1 && competitors == 1)
          competitors = 16;
        switch_types = 0;
        if (game_type == 1)
          Race = ((uint8)TrackLoad - 1) & 7;
        else
          network_champ_on = 0;
      }
      iStatAnimationFrame = frames;
      frames = 0;
      if (SoundCard
        && front_fade
        && SampleHandleCar[84].handles[0] != -1
        && DIGISampleDone(SampleHandleCar[84].handles[0])) {//DIGISampleDone(*(int *)&DIGIHandle, SampleHandleCar[84].handles[0])) {
        frontendsample(0x8000);
        SampleHandleCar[84].handles[0] = -1;
      }
      display_picture(scrbuf, front_vga[0]);    // RENDER FRAME
      if (player_type == 2)                   // Two-player mode
      {
        if (iActivePlayer)
          display_block(scrbuf, front_vga[1], 6, head_x, head_y, 0);
        else
          display_block(scrbuf, front_vga[1], 5, head_x, head_y, 0);
        display_block(scrbuf, front_vga[1], 7, 200, 56, 0);
      } else {
        display_block(scrbuf, front_vga[1], 1, head_x, head_y, 0);
      }
      display_block(scrbuf, front_vga[6], 0, 36, 2, 0);
      if (iPlayer1Car < CAR_DESIGN_AUTO) {
        front_text(front_vga[15], &language_buffer[4160], font1_ascii, font1_offsets, 400, 200, 0xE7u, 1u);
      } else {                                         // Render 3D car model with zoom/rotation
        if (iPlayer1Car == CAR_DESIGN_F1WACK) {
          DrawCar(scrbuf + 34640, CAR_DESIGN_F1WACK, 6000.0, 1280, 0);
        } else if (iDelayBeforeRotation) {
          DrawCar(scrbuf + 34640, iPlayer1Car, 2200.0, 1280, 0);
        } else {
          fCarDrawDistance = (float)iZoomDistance;
          DrawCar(scrbuf + 34640, iPlayer1Car, fCarDrawDistance, 1280, 0);
        }
        if (iPlayer1Car < CAR_DESIGN_SUICYCO)
          display_block(scrbuf, front_vga[3], iPlayer1Car, 190, 356, 0);
      }
      display_block(scrbuf, front_vga[7], 0, 560, 20, 0);// Draw car statistics as 7 pie chart segments (speed, acceleration, braking, etc.)
      uiNetworkLoop = 0;
      iPieChartY = 19;                          // Draw 7 pie chart segments (iY starts at 19, increments by 51)
      do {
        display_block(scrbuf, front_vga[7], blockIdxAy[uiNetworkLoop / 4], 568, iPieChartY, 0);
        uiNetworkLoop += 4;
        iPieChartY += 51;
      } while (uiNetworkLoop != 28);
      display_block(scrbuf, front_vga[5], player_type, -4, 247, 0);
      display_block(scrbuf, front_vga[5], game_type + 5, 135, 247, 0);
      display_block(scrbuf, front_vga[4], 0, 76, 257, -1);
      if (iCurrentCarSelectorPos >= 8)        // Draw car selection cursor
      {
        display_block(scrbuf, front_vga[6], 4, 62, 336, -1);
      } else {
        display_block(scrbuf, front_vga[6], 2, 62, 336, -1);
        front_text(front_vga[2], "~", font2_ascii, font2_offsets, sel_posns[iCurrentCarSelectorPos].x, sel_posns[iCurrentCarSelectorPos].y, 0x8Fu, 0);
      }
      front_text(front_vga[2], "AUTO ARIEL", font2_ascii, font2_offsets, sel_posns[0].x + 132, sel_posns[0].y + 7, 0x8Fu, 2u);                                    // Draw all car company names
      front_text(front_vga[2], "DESILVA", font2_ascii, font2_offsets, sel_posns[1].x + 132, sel_posns[1].y + 7, 0x8Fu, 2u);
      front_text(front_vga[2], "PULSE", font2_ascii, font2_offsets, sel_posns[2].x + 132, sel_posns[2].y + 7, 0x8Fu, 2u);
      front_text(front_vga[2], "GLOBAL", font2_ascii, font2_offsets, sel_posns[3].x + 132, sel_posns[3].y + 7, 0x8Fu, 2u);
      front_text(front_vga[2], "MILLION PLUS", font2_ascii, font2_offsets, sel_posns[4].x + 132, sel_posns[4].y + 7, 0x8Fu, 2u);
      front_text(front_vga[2], "MISSION", font2_ascii, font2_offsets, sel_posns[5].x + 132, sel_posns[5].y + 7, 0x8Fu, 2u);
      front_text(front_vga[2], "ZIZIN", font2_ascii, font2_offsets, sel_posns[6].x + 132, sel_posns[6].y + 7, 0x8Fu, 2u);
      front_text(front_vga[2], "REISE WAGON", font2_ascii, font2_offsets, sel_posns[7].x + 132, sel_posns[7].y + 7, 0x8Fu, 2u);
      if (iCurrentCarSelectorPos < 8 && network_on && (cheat_mode & 0x4000) == 0)// Network mode: show which players have allocated which cars
      {
        front_text(front_vga[15], &language_buffer[4672], font1_ascii, font1_offsets, 380, 380, 0x8Fu, 2u);
        if (allocated_cars[iCurrentCarSelectorPos]) {
          iPlayerNameX = 385;
          iNetworkPlayerCount = 0;
          if (players > 0) {
            uiNetworkPlayerIndex = 0;
            szPlayerName = player_names[0];
            do {
              if (iCurrentCarSelectorPos == Players_Cars[uiNetworkPlayerIndex / 4]) {
                if (iPlayerNameX == 385)
                  uiPlayerIndex = 0;
                else
                  uiPlayerIndex = 2;
                front_text(front_vga[15], szPlayerName, font1_ascii, font1_offsets, iPlayerNameX, 380, 0x8Fu, uiPlayerIndex);
                iPlayerNameX = 620;
              }
              uiNetworkPlayerIndex += 4;
              szPlayerName += 9;
              ++iNetworkPlayerCount;
            } while (iNetworkPlayerCount < players);
          }
        } else {
          front_text(front_vga[15], &language_buffer[4736], font1_ascii, font1_offsets, 385, 380, 0x83u, 0);
        }
      }
      if (iOriginalCarSelection >= 0)         // Display current player's selected car company name at bottom
      {
        if (iActivePlayer)
          sprintf(buffer, "%s %s", &language_buffer[2880], CompanyNames[Players_Cars[player2_car]]);
        else
          sprintf(buffer, "%s %s", &language_buffer[2816], szCurrentCompanyName);
        scale_text(front_vga[15], buffer, font1_ascii, font1_offsets, 375, 316, 231, 1u, 170, 550);
      }
      show_received_mesage();
      copypic(scrbuf, screen);
      iAnimationCounter = iDelayBeforeRotation; // ANIMATION UPDATE: Rotate pie chart segments during delay period
      if (iDelayBeforeRotation) {
        if (iPlayer1Car >= CAR_DESIGN_AUTO) {
          iPieChartIndex = 0;
          iCarStatsOffset = 7 * iPlayer1Car;
          do {
            iTargetPieValue = 9 - car_pies[iCarStatsOffset];
            iCurrentPieValue = blockIdxAy[iPieChartIndex];
            if (iCurrentPieValue != iTargetPieValue) {
              iNextPieValue = iCurrentPieValue + 1;
              blockIdxAy[iPieChartIndex] = iNextPieValue;
              if (iNextPieValue > 8)
                blockIdxAy[iPieChartIndex] = 1;
            }
            ++iPieChartIndex;
            ++iCarStatsOffset;
          } while (iPieChartIndex < 7);
        }
      } else {
        for (int i = 0; i < 7; i++) {
            blockIdxAy[i]++;
            if (blockIdxAy[i] > 8) {
                blockIdxAy[i] = 1;
            }
        }
        //do {
        //  int iIncrementedPieValue = *(int *)((char *)blockIdxAy + iAnimationCounter) + 1;
        //  *(int *)((char *)blockIdxAy + iAnimationCounter) = iIncrementedPieValue;
        //  if (iIncrementedPieValue > 8)
        //    *(int *)((char *)blockIdxAy + iAnimationCounter) = 1;
        //  iAnimationCounter += 4;
        //} while (iAnimationCounter != 28);
      }
      if (!iDelayBeforeRotation)              // ZOOM ANIMATION: Handle 3D car zoom in/out with car switching logic
      {
        iNewZoomDistance = iStatAnimationFrame * iZoomSpeed + iZoomDistance;
        iZoomDistance = iNewZoomDistance;
        if (iNewZoomDistance <= 40000)        // Handle car zoom in/out animation and car selection
        {
          if (iNewZoomDistance < 4000) {
            iZoomDistance = 4000;
            iDelayBeforeRotation = 72;
          }
        } else {
          iCarAllocationStatus = allocated_cars[iSelectedCar];// Zoom reached maximum - switch cars if available, otherwise restore previous car
          iZoomDistance = 40000;
          iZoomSpeed = -iZoomSpeed;
          if (iCarAllocationStatus < 2) {                                     // Free old car textures and load new car's textures
            if (iPlayer1Car >= CAR_DESIGN_AUTO) {
              iCarDesignIndex = iPlayer1Car;
              ppTextureArray = cartex_vga;
              car_texs_loaded[CarDesigns[iCarDesignIndex].carType] = -1;
              do {
                ppCurrentTexture = (void **)ppTextureArray++;
                fre(ppCurrentTexture);
              } while (ppTextureArray != &cartex_vga[16]);
              remove_mapsels();
              if (frontendspeechptr)
                fre((void **)&frontendspeechptr);
            }
            if (game_type == 1 && Race > 0) {
              iPlayer1Car = iSelectedCar;
            } else {
              if (iActivePlayer)
                iPlayerCarIndex = player2_car;
              else
                iPlayerCarIndex = player1_car;
              iPlayer1Car = iSelectedCar;
              Players_Cars[iPlayerCarIndex] = iSelectedCar;
            }
            if (iPlayer1Car >= CAR_DESIGN_AUTO) {
              eNewCarType = CarDesigns[iPlayer1Car].carType;
              iTextureLoadedStatus = car_texs_loaded[eNewCarType];

              if (iTextureLoadedStatus == -1) {
                  // Texture not loaded yet - load it now
                  LoadCarTexture(eNewCarType, 1u);
                  car_texmap[iPlayer1Car] = 1;
                  car_texs_loaded[eNewCarType] = 1;
                  iLoadTextureFlag = 2;
              } else {
                  // Texture already loaded - use existing
                  car_texmap[iPlayer1Car] = iTextureLoadedStatus;
                  iLoadTextureFlag = 1;
              }
              //eNewCarType = CarDesigns[iPlayer1Car].carType;
              //eCarTypeBackup = eNewCarType;
              //uiCarArrayOffset = 4 * iPlayer1Car;
              //iTextureLoadedStatus = car_texs_loaded[eNewCarType];
              //iLoadTextureFlag = 1;
              //if (iTextureLoadedStatus == -1) {
              //  LoadCarTexture(eNewCarType, 1u);
              //  car_texmap[uiCarArrayOffset / 4] = 1;
              //  car_texs_loaded[eCarTypeBackup] = 1;
              //  iLoadTextureFlag = 2;
              //} else {
              //  car_texmap[uiCarArrayOffset / 4] = iTextureLoadedStatus;
              //}
              LoadCarTextures = iLoadTextureFlag;
              if (!network_on)
                check_cars();
            }
          } else {
            if (iActivePlayer)
              iPlayer1Car = Players_Cars[player2_car];
            else
              iPlayer1Car = Players_Cars[player1_car];
            iSelectedCar = iPlayer1Car;
          }
          if (iPlayer1Car >= CAR_DESIGN_AUTO) {
            sfxsample(SOUND_SAMPLE_CARIN, 0x8000);
            if (iPlayer1Car < CAR_DESIGN_SUICYCO)
              loadfrontendsample(descript[iPlayer1Car]);
            if (!SamplePtr[SOUND_SAMPLE_CARIN])
              frontendsample(0x8000);
          }
          broadcast_mode = -1;
          while (broadcast_mode) {
            UpdateSDL();
          }
          frames = 0;
        }
      }
      if (car_request < 0)                    // NETWORK CAR REQUESTS: Handle incoming car selection requests from other players
      {
        iDelayBeforeRotation = 0;
        iZoomSpeed = 2000;
        iSelectedCar = -car_request - 1;
        sfxsample(SOUND_SAMPLE_CAROUT, 0x8000);
        car_request = 0;
        if ((cheat_mode & 0x4000) != 0)
          switch_same = iSelectedCar + 666;
      }
      if (switch_same > 0)                    // CHEAT MODE: Handle "switch_same" command to force all players to same car
      {
        if (switch_same - 666 != Players_Cars[player1_car]) {
          iSelectedCar = switch_same - 666;
          iZoomSpeed = 2000;
          iDelayBeforeRotation = 0;
          sfxsample(SOUND_SAMPLE_CAROUT, 0x8000);

          if (players > 0) {
              for (iPlayerLoopCounter = 0; iPlayerLoopCounter < players; iPlayerLoopCounter++) {
                  Players_Cars[iPlayerLoopCounter] = switch_same - 666;
              }
          }
          //iPlayerLoopCounter = 0;
          //if (players > 0) {
          //  iPlayerArrayOffset = 0;
          //  do {
          //    iPlayerArrayOffset += 4;
          //    ++iPlayerLoopCounter;
          //    *(int *)((char *)&infinite_laps + iPlayerArrayOffset) = switch_same - 666;// offset into Players_Cars
          //  } while (iPlayerLoopCounter < players);
          //}

          cheat_mode |= CHEAT_MODE_CLONES;
        }
      } else if (switch_same < 0) {
        switch_same = 0;
        iDelayBeforeRotation = 0;
        iZoomSpeed = 2000;
        iSelectedCar = -1;
        sfxsample(SOUND_SAMPLE_CAROUT, 0x8000);
        cheat_mode &= ~CHEAT_MODE_CLONES;
      }
      if (switch_sets) {
        iSelectedCar = iPlayer1Car;
        iDelayBeforeRotation = 0;
        iZoomSpeed = 2000;
        sfxsample(SOUND_SAMPLE_CAROUT, 0x8000);
        switch_sets = 0;
      }
      if (!front_fade)                        // Screen fade-in: Load car voice sample and fade palette to visible
      {
        if (iPlayer1Car < CAR_DESIGN_SUICYCO)
          loadfrontendsample(descript[iPlayer1Car]);
        front_fade = -1;
        fade_palette(32);
        frontendsample(0x8000);
        frames = 0;
      }
      uiNavigationDirection = 0;
      iNextCarIndex = iCurrentCarSelectorPos + 1;
      while (fatkbhit())                      // KEYBOARD INPUT PROCESSING: Handle all user input and navigation
      {
        byInputKey = fatgetch();
        if (byInputKey < 0x20u) {
          if (byInputKey < 0xDu) {
            if (!byInputKey) {
              switch ((uint8)fatgetch()) {
                case 0x48u:
                case 0x4Bu:
                  goto LABEL_152;               // Arrow keys: Handle up/down/left/right navigation between cars
                case 0x4Du:
                case 0x50u:
                  goto LABEL_153;
                default:
                  continue;
              }
            }
          } else if (byInputKey <= 0xDu)        // Enter key: Confirm car selection or trigger car switching animation
          {
            if (iCurrentCarSelectorPos != 8 && iCurrentCarSelectorPos != iPlayer1Car || iCurrentCarSelectorPos == 8) {
              remove_frontendspeech();
              sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);
            }
            if (iCurrentCarSelectorPos == iPlayer1Car
              && SoundCard
              && frontendspeechhandle != -1
              && DIGISampleDone(frontendspeechhandle)) { //DIGISampleDone(*(int *)&DIGIHandle, frontendspeechhandle)) {
              frontendspeechhandle = -1;
              frontendsample(0x8000);
            }
            if (iCurrentCarSelectorPos == 8) {
              byMenuExitFlag = -1;
            } else if (iPlayer1Car != iCurrentCarSelectorPos
                   && (allocated_cars[iCurrentCarSelectorPos] < 2 || game_type == 1 && Race > 0)) {
              if (network_on) {
                car_request = iNextCarIndex;
                broadcast_mode = -9999;
                while (broadcast_mode)
                  UpdateSDL();
              } else {
                iDelayBeforeRotation = 0;
                iZoomSpeed = 2000;
                iSelectedCar = iCurrentCarSelectorPos;
                sfxsample(SOUND_SAMPLE_CAROUT, 0x8000);
              }
            }
          } else if (byInputKey == 27)          // Escape key: Exit to main menu
          {
            byMenuExitFlag = -1;
            remove_frontendspeech();
            sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);
          }
        } else if (byInputKey <= 0x20u) {                                       // Space key: Switch between Player 1 and Player 2 in two-player mode
          if (player_type == 2) {
            if (iActivePlayer) {
              iActivePlayer = 0;
              iDelayBeforeRotation = 0;
              iZoomSpeed = 2000;
              iSelectedCar = Players_Cars[player1_car];
            } else {
              iDelayBeforeRotation = 0;
              iActivePlayer = 1;
              iZoomSpeed = 2000;
              iSelectedCar = Players_Cars[player2_car];
            }
          }
        } else if (byInputKey < 0x2Du) {
          if (byInputKey == 43)
            LABEL_153:
          uiNavigationDirection = 1;
        } else {
          if (byInputKey <= 0x2Du)
            goto LABEL_152;
          if (byInputKey >= 0x3Du) {
            if (byInputKey <= 0x3Du)
              goto LABEL_153;
            if (byInputKey == 95)
              LABEL_152:
            uiNavigationDirection = 2;
          }
        }
        UpdateSDL();

      }
      if (uiNavigationDirection)              // Apply navigation input to move car selector cursor
      {
        if (uiNavigationDirection > 1) {
          if (--iCurrentCarSelectorPos < 0)
            iCurrentCarSelectorPos = 0;
        } else if (++iCurrentCarSelectorPos > 8) {
          iCurrentCarSelectorPos = 8;
        }
      }
      nRotationAngle = Car[0].nYaw + 32 * iStatAnimationFrame;// Update 3D car rotation angle for spinning animation
      nRotationAngle &= 0x3FFFu;
      Car[0].nYaw = nRotationAngle;

      UpdateSDL();
    } while (!byMenuExitFlag);                  // MAIN SELECTION LOOP - Handle UI rendering, input, and car switching
  }
  fade_palette(0);                              // CLEANUP: Fade screen, free graphics memory, restore original car selection
  fre((void **)&front_vga[7]);
  if (frontendspeechptr)
    fre((void **)&frontendspeechptr);
  front_fade = 0;
  if (iPlayer1Car >= CAR_DESIGN_AUTO) {
    ppCleanupTextureArray = cartex_vga;
    car_texs_loaded[CarDesigns[iPlayer1Car].carType] = -1;
    do {
      ppCleanupTexture = (void **)ppCleanupTextureArray++;
      fre(ppCleanupTexture);
    } while (ppCleanupTextureArray != &cartex_vga[16]);
    remove_mapsels();
  }
  if (iOriginalCarSelection >= 0)
    Players_Cars[player1_car] = iOriginalCarSelection;
}

//-------------------------------------------------------------------------------------------------
//00042D40
void select_configure()
{
  char *szString; // eax
  int iCharIndex; // edx
  int iCarIndex; // esi
  const char *szCarName; // edi
  uint8 byTextColor1; // al
  uint8 byTextColor2; // al
  uint8 byTextColor3; // al
  uint8 byTextColor4; // al
  char byChar; // al
  int iFontWidth; // eax
  uint8 byTextColor5; // al
  uint8 byTextColor6; // al
  uint8 byTextColor7; // al
  int iTemp1; // edi
  uint8 byTextColor8; // al
  uint8 byTextColor9; // al
  uint8 byColor; // al
  int iSelectedCar_1; // edi
  uint8 byColor_1; // al
  uint8 byColor_2; // al
  char byVolumeColor1; // al
  char byVolumeColor2; // al
  char byVolumeColor3; // al
  char byVolumeColor4; // al
  char byColor_3; // al
  char byColor_4; // al
  char byColor_5; // al
  char byColor_6; // al
  char byColor_7; // al
  char byColor_9; // al
  char byColor_8; // al
  char byColor_10; // al
  char byColor_11; // al
  char byColor_13; // al
  char byColor_12; // al
  char byColor_14; // al
  int byColor_15; // ebx
  int byColor_16; // ebx
  int byColor_17; // ebx
  int iVolumeSelection_1; // ecx
  int byColor_18; // ebx
  int iConfigState_1; // edi
  int iJoyCalibValue1; // ebx
  char *szJoyStatus1; // edx
  int iJoyCalibValue2; // ebx
  char *szJoyStatus2; // edx
  int iX2CalibrationVal; // ebx
  char *szX2Text; // edx
  int iConfigState_2; // edi
  int iY2CalibrationVal; // ebx
  char *szY2Text; // edx
  int iKeyFound; // ebx
  int iKeyIndex; // eax
  int iKeyCounter; // edx
  char byColor_19; // al
  char byColor_20; // al
  char byColor_21; // al
  char byColor_22; // al
  char byColor_23; // al
  int iControlLoop; // esi
  const char *szControlName; // edi
  uint8 byControlColor; // al
  char byColor_24; // al
  uint8 byColor_25; // al
  char byColor_26; // al
  int iControlIndex2; // esi
  const char *szText; // edi
  uint8 byColor_27; // al
  uint8 byColor_28; // al
  uint8 byColor_29; // al
  uint8 byColor_30; // al
  int iKeyCheckLoop; // eax
  int iFoundKey; // ecx
  int iKeySearchIndex; // edx
  int iJoyValue1; // eax
  int iJoyValue2; // eax
  int iJoyValue3; // eax
  int iJoyValue4; // eax
  int iDuplicateCheck; // ebx
  int i; // eax
  int iEditIndex; // eax
  int iControlState; // ebx
  char byColor_31; // al
  char byColor_32; // al
  char *szText_1; // edx
  char byColor_33; // al
  char byColor_34; // al
  char byColor_36; // al
  char byColor_105; // al
  char byColor_35; // al
  char byColor_37; // al
  char byColor_38; // al
  char byColor_39; // al
  char byColor_40; // al
  char byColor_41; // al
  char byColor_42; // al
  char byColor_43; // al
  char byColor_44; // al
  char byColor_45; // al
  char byColor_46; // al
  char byColor_47; // al
  char byColor_48; // al
  char byColor_49; // al
  char byColor_50; // al
  char byColor_51; // al
  char byColor_52; // al
  char byColor_53; // al
  char byColor_54; // al
  char byColor_55; // al
  char byColor_57; // al
  char byColor_56; // al
  char byColor_58; // al
  char byColor_59; // al
  char byColor_60; // al
  char byColor_61; // al
  char byColor_62; // al
  char byColor_63; // al
  char byColor_64; // al
  char byColor_65; // al
  char byColor_66; // al
  char byColor_67; // al
  char byColor_68; // al
  char byColor_69; // al
  char byColor_70; // al
  char byColor_71; // al
  char byColor_72; // al
  char byColor_73; // al
  char byColor_74; // al
  int iReturnValue; // eax
  char byColor_76; // al
  char byColor_75; // al
  char byColor_77; // al
  char byColor_78; // al
  char byColor_79; // al
  char byColor_80; // al
  char byColor_81; // al
  char byColor_82; // al
  char byColor_83; // al
  char byColor_84; // al
  char byColor_85; // al
  char byColor_86; // al
  char byColor_87; // al
  char byColor_88; // al
  char byColor_89; // al
  char byColor_90; // al
  char byColor_91; // al
  char byColor_92; // al
  char byColor_93; // al
  char byColor_94; // al
  char byColor_95; // al
  char byColor_96; // di
  uint8 byColor_97; // si
  char *szMemPtr; // eax
  int iFontChar; // edx
  uint8 byColor_106; // al
  uint8 byColor_98; // al
  char byColor_99; // al
  uint8 byColor_100; // al
  char byColor_101; // al
  uint8 byColor_102; // al
  char byColor_103; // al
  uint8 byColor_104; // al
  int iNetworkState_1; // edi
  int iX; // eax
  int iPlayerIdx_1; // eax
  //int iPlayersCarsOffset_1; // edx
  int iPlayerIdx; // eax
  //int iPlayersCarsOffset; // edx
  //uint32 uiTempCheatMode; // eax
  unsigned int uiKeyCode; // eax
  unsigned int uiExtendedKey; // eax
  int iMenuDir; // edi
  int iMenuDir2; // edi
  int iKeyInput; // eax
  int iKeyChar; // ecx
  unsigned int uiArrowKey; // eax
  int iPrevSelectedCar; // edx
  int iNextSelectedCar; // edx
  int iNameLength_1; // eax
  int iEditingName_1; // edi
  int j; // ecx
  int iPlayer2Car; // edx
  int k; // ecx
  int iDefaultNamesIdx_1; // edx
  int iDefaultNamesCharItr; // ecx
  int iDefaultNamesIdx; // edx
  int m; // ecx
  int n; // ecx
  int iAIDriverIdx; // eax
  //int v189; // ecx
  //int iOffset; // eax
  //char v191; // dl
  int ii; // ecx
  int iPlayer2Car_1; // edx
  int jj; // ecx
  int iAIDriverIdx_1; // eax
  int v196; // ecx
  int v197; // edx
  int iNameLength_2; // edi
  unsigned int uiKey_5; // eax
  int iNextVolumeSelection; // edi
  unsigned int uiKey; // eax
  unsigned int uiKey_1; // eax
  unsigned int uiKey_6; // eax
  int iNextControlSelection; // edi
  int iDisplayIndex; // edi
  unsigned int uiKey_2; // eax
  uint32 uiTexOffTemp_7; // eax
  uint32 uiTexOffTemp; // edx
  uint32 uiTexOffTemp_1; // eax
  uint32 uiTexOffTemp_2; // ecx
  uint32 uiTexOffTemp_3; // ebx
  uint32 uiTexOffTemp_4; // edx
  uint32 uiTexOffTemp_5; // eax
  unsigned int uiKey_3; // eax
  int iPlayerIndex; // esi
  uint32 uiTexOffTemp_6; // edx
  unsigned int uiKey_4; // eax
  unsigned int uiDataValue1; // edx
  int iGameIndex; // ecx
  unsigned int uiDataValue2; // eax
  int iPlayerIndex2; // esi
  unsigned int uiDataValue3; // eax
  int iDataIndex; // edx
  int iCounterVar; // ecx
  char byTempFlag; // dl
  bool kk; // zf
  char byStatusFlag; // bl
  int iResultValue; // eax
  int iCalculation; // ebx
  char byTempChar1; // [esp-10h] [ebp-16h]
  char byTempChar2; // [esp-10h] [ebp-16h]
  uint8 byTempValue; // [esp-8h] [ebp-Eh]
  tJoyPos pJoyPos; // [esp+0h] [ebp-6h] BYREF
  char szNewNameBuf[12]; // [esp+20h] [ebp+1Ah] BYREF
  int iY; // [esp+2Ch] [ebp+26h]
  int iTextPosX; // [esp+30h] [ebp+2Ah]
  int iGraphicsState; // [esp+34h] [ebp+2Eh]
  int iNetworkState; // [esp+38h] [ebp+32h]
  int iControlsInEdit; // [esp+3Ch] [ebp+36h]
  int iControlSelection; // [esp+40h] [ebp+3Ah]
  int iVideoState; // [esp+44h] [ebp+3Eh]
  int iVolumeSelection; // [esp+48h] [ebp+42h]
  int iSelectedCar; // [esp+4Ch] [ebp+46h]
  int iConfigState; // [esp+50h] [ebp+4Ah]
  int iNameLength; // [esp+54h] [ebp+4Eh]
  int iEditingName; // [esp+58h] [ebp+52h]
  int iMenuSelection; // [esp+5Ch] [ebp+56h]
  int iExitFlag; // [esp+60h] [ebp+5Ah]
  int iCarDisplay; // [esp+64h] [ebp+5Eh]
  int iDimmedColor; // [esp+68h] [ebp+62h]
  int iActiveColor; // [esp+6Ch] [ebp+66h]
  int iY_2; // [esp+70h] [ebp+6Ah]
  int iY_1; // [esp+74h] [ebp+6Eh]
  int iTextPosY; // [esp+78h] [ebp+72h]
  int iNormalColor; // [esp+7Ch] [ebp+76h]
  int iHighlightColor; // [esp+80h] [ebp+7Ah]
  int iCarLoop; // [esp+84h] [ebp+7Eh]

  // Init config menu
  fade_palette(0);
  iExitFlag = 0;
  iMenuSelection = 7;
  iEditingName = 0;
  iControlsInEdit = 0;
  front_fade = 0;
  iConfigState = 0;

  // Main config loop
  while (2) {
    UpdateSDL();
    if (switch_types) {
      game_type = switch_types - 1;
      if (switch_types == 1 && competitors == 1)
        competitors = 16;
      switch_types = 0;
      if (game_type == 1)
        Race = ((uint8)TrackLoad - 1) & 7;
      else
        network_champ_on = 0;
    }

    // Draw background and ui elements
    display_picture(scrbuf, front_vga[0]);
    display_block(scrbuf, front_vga[1], 0, head_x, head_y, 0);
    display_block(scrbuf, front_vga[6], 0, 36, 2, 0);
    display_block(scrbuf, front_vga[5], player_type, -4, 247, 0);
    display_block(scrbuf, front_vga[5], game_type + 5, 135, 247, 0);
    display_block(scrbuf, front_vga[4], 1, 76, 257, -1);

    // draw menu selector
    if (iMenuSelection >= 7) {
      // no menu item selected (exit)
      display_block(scrbuf, front_vga[6], 4, 62, 336, -1);
    } else {
      // draw menu selector
      display_block(scrbuf, front_vga[6], 2, 62, 336, -1);
      front_text(front_vga[2], "~", font2_ascii, font2_offsets, sel_posns[iMenuSelection].x, sel_posns[iMenuSelection].y, 0x8Fu, 0);
    }

    // menu options labels
    front_text(front_vga[2], &config_buffer[3968], font2_ascii, font2_offsets, sel_posns[0].x + 132, sel_posns[0].y + 7, 0x8Fu, 2u);
    front_text(front_vga[2], &config_buffer[256], font2_ascii, font2_offsets, sel_posns[1].x + 132, sel_posns[1].y + 7, 0x8Fu, 2u);
    front_text(front_vga[2], &config_buffer[1664], font2_ascii, font2_offsets, sel_posns[2].x + 132, sel_posns[2].y + 7, 0x8Fu, 2u);
    front_text(front_vga[2], &config_buffer[4032], font2_ascii, font2_offsets, sel_posns[3].x + 132, sel_posns[3].y + 7, 0x8Fu, 2u);
    front_text(front_vga[2], &config_buffer[4096], font2_ascii, font2_offsets, sel_posns[4].x + 132, sel_posns[4].y + 7, 0x8Fu, 2u);
    front_text(front_vga[2], &config_buffer[4160], font2_ascii, font2_offsets, sel_posns[5].x + 132, sel_posns[5].y + 7, 0x8Fu, 2u);

    // network option if enabled
    if (network_on)
      front_text(front_vga[2], &config_buffer[5568], font2_ascii, font2_offsets, sel_posns[6].x + 132, sel_posns[6].y + 7, 0x8Fu, 2u);

    // Config state machine
    switch (iMenuSelection) {
      case 0:
        if (iEditingName == 1) {
          iHighlightColor = 0xAB;
          iNormalColor = 0xA5;
        } else {
          iHighlightColor = 0xA5;
          iNormalColor = 0xAB;
        }
        if (iConfigState != 1) {
          iHighlightColor = 0x8F;
          iNormalColor = 0x8F;
        }
        if (iEditingName == 1) {
          iTextPosX = 0;
          szString = szNewNameBuf;
          while (*szString) {
            iCharIndex = (uint8)font1_ascii[(uint8)*szString++];
            if (iCharIndex == 255)
              iTextPosX += 8;
            else
              iTextPosX += front_vga[15][iCharIndex].iWidth + 1;
          }
          iTextPosX += 430;
          iY = 374 - 18 * iSelectedCar;
        }

        // Init car display loop
        iCarLoop = 0;
        iDimmedColor = iHighlightColor - 2;
        iCarDisplay = 15;
        iCarIndex = 18;
        iActiveColor = iNormalColor - 4;
        szCarName = comp_name[0];
        iTextPosY = 50;

        // Display all 16 cars in reverse order
        do {
          // Check if car slot is allocated to a player
          if ((iCarLoop & 1) >= allocated_cars[iCarDisplay / 2]) {
            // Car is available for Ai palyers
            if (iCarIndex == iSelectedCar && iEditingName == 1) {
              // Selected car with name being edited
              front_text(front_vga[15], szCarName, font1_ascii, font1_offsets, 425, iTextPosY, iNormalColor, 2u);
              if (iCarIndex == iSelectedCar)
                byTextColor3 = iHighlightColor;
              else
                byTextColor3 = 0x8F;
              front_text(front_vga[15], szNewNameBuf, font1_ascii, font1_offsets, 430, iTextPosY, byTextColor3, 0);
            } else {
              // Selected car with default name displayed
              if (iCarIndex == iSelectedCar)
                byTextColor4 = iNormalColor;
              else
                byTextColor4 = 0x8F;
              front_text(front_vga[15], szCarName, font1_ascii, font1_offsets, 425, iTextPosY, byTextColor4, 2u);
              if (iCarIndex == iSelectedCar)
                byChar = iHighlightColor;
              else
                byChar = 0x8F;

              // Display AI name (using bit toggle for paired names)
              byTempValue = byChar;
              iFontWidth = iCarDisplay;
              //LOBYTE(iFontWidth) = iCarDisplay ^ 1;
              iFontWidth = iCarDisplay ^ 1;
              front_text(front_vga[15], default_names[iFontWidth], font1_ascii, font1_offsets, 430, iTextPosY, byTempValue, 0);
            }
          } else {
            // Car is allocated to a human player
            if (iCarIndex == iSelectedCar)
              byTextColor1 = iActiveColor;
            else
              byTextColor1 = 0x8B;
            front_text(front_vga[15], szCarName, font1_ascii, font1_offsets, 425, iTextPosY, byTextColor1, 2u);
            if (iCarIndex == iSelectedCar)
              byTextColor2 = iDimmedColor;
            else
              byTextColor2 = 0x7F;

            // Display human player name
            front_text(front_vga[15], player_names[car_to_player[14 - (iCarLoop & 0xFE) + (iCarLoop & 1)]], font1_ascii, font1_offsets, 430, iTextPosY, byTextColor2, 0);
          }

          // Move to next car
          --iCarIndex;
          szCarName += 15;
          --iCarDisplay;
          iTextPosY += 18;
          ++iCarLoop;
        } while (iCarLoop < 16);

        // Display player 2 configuration (if in 2-player mode)
        if (player_type == 2) {
          if (iSelectedCar == 2 && iEditingName == 1) {
            // Player 2 name being edited
            if (iSelectedCar == player_type)
              byTextColor5 = iNormalColor;
            else
              byTextColor5 = 0x8F;
            front_text(front_vga[15], &config_buffer[4288], font1_ascii, font1_offsets, 425, 338, byTextColor5, player_type);
            if (iSelectedCar == 2)
              byTextColor6 = iHighlightColor;
            else
              byTextColor6 = 0x8F;
            front_text(front_vga[15], szNewNameBuf, font1_ascii, font1_offsets, 430, 338, byTextColor6, 0);
          } else {
            // Player 2 name display mode
            if (iSelectedCar == 2)
              byTextColor7 = iNormalColor;
            else
              byTextColor7 = 0x8F;
            iTemp1 = iSelectedCar;
            front_text(front_vga[15], &config_buffer[4288], font1_ascii, font1_offsets, 425, 338, byTextColor7, 2u);
            if (iTemp1 == 2)
              byTextColor8 = iHighlightColor;
            else
              byTextColor8 = 0x8F;
            front_text(front_vga[15], player_names[player2_car], font1_ascii, font1_offsets, 430, 338, byTextColor8, 0);
          }
        }

        // Display player 1 configuration
        if (iSelectedCar == 1 && iEditingName == 1) {
          // Player 1 name being edited
          front_text(front_vga[15], &config_buffer[4224], font1_ascii, font1_offsets, 425, 356, iNormalColor, 2u);
          if (iSelectedCar == 1)
            byTextColor9 = iHighlightColor;
          else
            byTextColor9 = 0x8F;
          front_text(front_vga[15], szNewNameBuf, font1_ascii, font1_offsets, 430, 356, byTextColor9, 0);
        } else {
          // Player 1 name display mode
          if (iSelectedCar == 1)
            byColor = iNormalColor;
          else
            byColor = 0x8F;
          iSelectedCar_1 = iSelectedCar;
          front_text(front_vga[15], &config_buffer[4224], font1_ascii, font1_offsets, 425, 356, byColor, 2u);
          if (iSelectedCar_1 == 1)
            byColor_1 = iHighlightColor;
          else
            byColor_1 = 0x8F;
          front_text(front_vga[15], player_names[player1_car], font1_ascii, font1_offsets, 430, 356, byColor_1, 0);
        }

        // Display "BACK" option
        if (iSelectedCar)
          byColor_2 = 0x8F;
        else
          byColor_2 = iNormalColor;
        front_text(front_vga[15], &config_buffer[832], font1_ascii, font1_offsets, 420, 374, byColor_2, 2u);

        // Display blinking cursor when editing names
        if (iEditingName == 1) {
          if ((frames & 0xFu) < 8)            // blink cursor based on frame counter
            front_text(front_vga[15], "_", font1_ascii, font1_offsets, iTextPosX, iY, 0xABu, 0);
          szNewNameBuf[iNameLength] = 0;
        }
        goto RENDER_FRAME;                      // skip to end of switch
      case 1:
        // Audio/volume config
        if (iConfigState != 2)
          iVolumeSelection = -1;

        // Engine volume
        if (iVolumeSelection == 1)
          byVolumeColor1 = 0xAB;
        else
          byVolumeColor1 = 0x8F;
        scale_text(front_vga[15], &config_buffer[2304], font1_ascii, font1_offsets, 425, 80, byVolumeColor1, 2u, 200, 640);

        // SFX volume
        if (iVolumeSelection == 2)
          byVolumeColor2 = 0xAB;
        else
          byVolumeColor2 = 0x8F;
        scale_text(front_vga[15], &config_buffer[2368], font1_ascii, font1_offsets, 425, 104, byVolumeColor2, 2u, 200, 640);

        // Speech volume
        if (iVolumeSelection == 3)
          byVolumeColor3 = 0xAB;
        else
          byVolumeColor3 = 0x8F;
        scale_text(front_vga[15], &config_buffer[2432], font1_ascii, font1_offsets, 425, 128, byVolumeColor3, 2u, 200, 640);

        // Music volume
        if (iVolumeSelection == 4)
          byVolumeColor4 = 0xAB;
        else
          byVolumeColor4 = 0x8F;
        scale_text(front_vga[15], &config_buffer[2496], font1_ascii, font1_offsets, 425, 152, byVolumeColor4, 2u, 200, 640);

        // Engine options
        if (iVolumeSelection == 5)
          byColor_3 = 0xAB;
        else
          byColor_3 = 0x8F;
        scale_text(front_vga[15], &config_buffer[2560], font1_ascii, font1_offsets, 425, 176, byColor_3, 2u, 200, 640);
        if (allengines) {
          if (iVolumeSelection == 5)
            byColor_4 = 0xAB;
          else
            byColor_4 = 0x8F;
          // ALL ENGINES
          scale_text(front_vga[15], &config_buffer[2752], font1_ascii, font1_offsets, 430, 176, byColor_4, 0, 200, 640);
        } else {
          if (iVolumeSelection == 5)
            byColor_5 = 0xAB;
          else
            byColor_5 = 0x8F;
          // STARTERS & TURBOS
          scale_text(front_vga[15], &config_buffer[2816], font1_ascii, font1_offsets, 430, 176, byColor_5, 0, 200, 640);
        }

        // Sound effects options
        if (iVolumeSelection == 6)
          byColor_6 = 0xAB;
        else
          byColor_6 = 0x8F;
        scale_text(front_vga[15], &config_buffer[2880], font1_ascii, font1_offsets, 425, 200, byColor_6, 2u, 200, 640);
        if (soundon) {
          if (iVolumeSelection == 6)
            byColor_7 = 0xAB;
          else
            byColor_7 = 0x8F;
          // ON
          scale_text(front_vga[15], &config_buffer[2624], font1_ascii, font1_offsets, 430, 200, byColor_7, 0, 200, 640);
        } else if (SoundCard) {
          if (iVolumeSelection == 6)
            byColor_8 = 0xAB;
          else
            byColor_8 = 0x8F;
          // OFF
          scale_text(front_vga[15], &config_buffer[2688], font1_ascii, font1_offsets, 430, 200, byColor_8, soundon, 200, 640);
        } else {
          if (iVolumeSelection == 6)
            byColor_9 = 0xAB;
          else
            byColor_9 = 0x8F;
          // DISABLED
          scale_text(front_vga[15], &config_buffer[6848], font1_ascii, font1_offsets, 430, 200, byColor_9, soundon, 200, 640);
        }

        // Music options
        if (iVolumeSelection == 7)
          byColor_10 = 0xAB;
        else
          byColor_10 = 0x8F;
        scale_text(front_vga[15], &config_buffer[2944], font1_ascii, font1_offsets, 425, 224, byColor_10, 2u, 200, 640);
        if (musicon) {
          if (iVolumeSelection == 7)
            byColor_11 = 0xAB;
          else
            byColor_11 = 0x8F;
          // ON
          scale_text(front_vga[15], &config_buffer[2624], font1_ascii, font1_offsets, 430, 224, byColor_11, 0, 200, 640);
        } else if (MusicCard || MusicCD) {
          if (iVolumeSelection == 7)
            byColor_12 = 0xAB;
          else
            byColor_12 = 0x8F;
          // OFF
          scale_text(front_vga[15], &config_buffer[2688], font1_ascii, font1_offsets, 430, 224, byColor_12, 0, 200, 640);
        } else {
          if (iVolumeSelection == 7)
            byColor_13 = 0xAB;
          else
            byColor_13 = 0x8F;
          // DISABLED
          scale_text(front_vga[15], &config_buffer[6848], font1_ascii, font1_offsets, 430, 224, byColor_13, musicon, 200, 640);
        }

        // Back option
        if (iVolumeSelection)
          byColor_14 = 0x8F;
        else
          byColor_14 = 0xAB;
        scale_text(front_vga[15], &config_buffer[832], font1_ascii, font1_offsets, 420, 248, byColor_14, 2u, 200, 640);

        // Display volume bars
        if (iVolumeSelection == 1)
          byColor_15 = 0xAB;
        else
          byColor_15 = 0xA5;
        front_volumebar(80, EngineVolume, byColor_15);
        if (iVolumeSelection == 2)
          byColor_16 = 0xAB;
        else
          byColor_16 = 0xA5;
        front_volumebar(104, SFXVolume, byColor_16);
        if (iVolumeSelection == 3)
          byColor_17 = 0xAB;
        else
          byColor_17 = 0xA5;
        iVolumeSelection_1 = iVolumeSelection;
        front_volumebar(128, SpeechVolume, byColor_17);
        if (iVolumeSelection_1 == 4)
          byColor_18 = 0xAB;
        else
          byColor_18 = 0xA5;
        front_volumebar(152, MusicVolume, byColor_18);
        goto RENDER_FRAME;
      case 2:
        // Joystick calibration
        if (iConfigState == 3) {
          ReadJoys(&pJoyPos);
          //_disable();

          // Update calibration ranges for all axes
          if (pJoyPos.iJ1XAxis < JAXmin)
            JAXmin = pJoyPos.iJ1XAxis;
          if (pJoyPos.iJ1XAxis > JAXmax)
            JAXmax = pJoyPos.iJ1XAxis;

          if (pJoyPos.iJ1YAxis < JAYmin)
            JAYmin = pJoyPos.iJ1YAxis;
          if (pJoyPos.iJ1YAxis > JAYmax)
            JAYmax = pJoyPos.iJ1YAxis;

          if (pJoyPos.iJ2XAxis < JBXmin)
            JBXmin = pJoyPos.iJ2XAxis;
          if (pJoyPos.iJ2XAxis > JBXmax)
            JBXmax = pJoyPos.iJ2XAxis;

          if (pJoyPos.iJ2YAxis < JBYmin)
            JBYmin = pJoyPos.iJ2YAxis;
          if (pJoyPos.iJ2YAxis > JBYmax)
            JBYmax = pJoyPos.iJ2YAxis;

          if (JAXmin == JAXmax)
            JAXmax = JAXmin + 1;
          if (JAYmin == JAYmax)
            JAYmax = JAYmin + 1;

          if (JBXmin == JBXmax)
            JBXmax = JBXmin + 1;
          if (JBYmin == JBYmax)
            JBYmax = JBYmin + 1;
          //_enable();
        }

        // Display calibration instructions when active
        if (iConfigState == 3) {
          // MOVE JOYSTICKS TO FULL EXTENTS
          scale_text(front_vga[15], &config_buffer[2112], font1_ascii, font1_offsets, 400, 60, 143, 1u, 200, 640);
          // THEN PRESS ANY KEY
          scale_text(front_vga[15], &config_buffer[2176], font1_ascii, font1_offsets, 400, 78, 143, 1u, 200, 640);
        }

        iConfigState_1 = iConfigState;

        // X1 axis display
        scale_text(front_vga[15], &config_buffer[1728], font1_ascii, font1_offsets, 400, 110, 143, 1u, 200, 640);
        if (iConfigState_1 == 3) {
          // Show calibration bar
          if (x1ok && JAXmax - JAXmin >= 100)
            iJoyCalibValue1 = 140 * (2 * pJoyPos.iJ1XAxis - JAXmax - JAXmin) / (JAXmax - JAXmin);
          else
            iJoyCalibValue1 = 0;
          front_displaycalibrationbar(300, 128, iJoyCalibValue1);
        } else {
          // Show status text
          if (x1ok)
            szJoyStatus1 = &config_buffer[2048];
          else
            szJoyStatus1 = &config_buffer[1984];
          scale_text(front_vga[15], szJoyStatus1, font1_ascii, font1_offsets, 400, 128, 143, 1u, 200, 640);
        }

        // Y1 axis display
        scale_text(front_vga[15], &config_buffer[1792], font1_ascii, font1_offsets, 400, 160, 143, 1u, 200, 640);
        if (iConfigState == 3) {
          // Show Calibration bar
          if (y1ok && JAYmax - JAYmin >= 100)
            iJoyCalibValue2 = 140 * (2 * pJoyPos.iJ1YAxis - JAYmax - JAYmin) / (JAYmax - JAYmin);
          else
            iJoyCalibValue2 = 0;
          front_displaycalibrationbar(300, 178, iJoyCalibValue2);
        } else {
          // Show status text
          if (y1ok)
            szJoyStatus2 = &config_buffer[2048];
          else
            szJoyStatus2 = &config_buffer[1984];
          scale_text(front_vga[15], szJoyStatus2, font1_ascii, font1_offsets, 400, 178, 143, 1u, 200, 640);
        }

        // X2 axis display
        scale_text(front_vga[15], &config_buffer[1856], font1_ascii, font1_offsets, 400, 210, 143, 1u, 200, 640);
        if (iConfigState == 3) {
          // Calibration bar
          if (x2ok && JBXmax - JBXmin >= 100)
            iX2CalibrationVal = 140 * (2 * pJoyPos.iJ2XAxis - JBXmax - JBXmin) / (JBXmax - JBXmin);
          else
            iX2CalibrationVal = 0;
          front_displaycalibrationbar(300, 228, iX2CalibrationVal);
        } else {
          // status text
          if (x2ok)
            szX2Text = &config_buffer[2048];
          else
            szX2Text = &config_buffer[1984];
          scale_text(front_vga[15], szX2Text, font1_ascii, font1_offsets, 400, 228, 143, 1u, 200, 640);
        }

        iConfigState_2 = iConfigState;

        // Y2 axis display
        scale_text(front_vga[15], &config_buffer[1920], font1_ascii, font1_offsets, 400, 260, 143, 1u, 200, 640);
        if (iConfigState_2 == 3) {
          // Calibration bar
          if (y2ok && JBYmax - JBYmin >= 100)
            iY2CalibrationVal = 140 * (2 * pJoyPos.iJ2YAxis - JBYmax - JBYmin) / (JBYmax - JBYmin);
          else
            iY2CalibrationVal = 0;
          front_displaycalibrationbar(300, 278, iY2CalibrationVal);
        } else {
          // Status text
          if (y2ok)
            szY2Text = &config_buffer[2048];
          else
            szY2Text = &config_buffer[1984];
          scale_text(front_vga[15], szY2Text, font1_ascii, font1_offsets, 400, 278, 143, 1u, 200, 640);
        }
        goto RENDER_FRAME;
      case 3:
        // Keyboard control config
        if (iConfigState == 4) {
          if (controlrelease) {
            // Check if all keys have been released
            iKeyFound = -1;
            iKeyIndex = 0;
            iKeyCounter = 0;
            do {
              if (keyname[iKeyCounter] && keys[iKeyIndex])
                iKeyFound = 0;                  // key still pressed
              ++iKeyIndex;
              ++iKeyCounter;
            } while (iKeyIndex < 128);
            if (iKeyFound)                    // all keys released
              controlrelease = 0;
          }
        } else {
          iControlSelection = -1;               // reset control selection
        }

        // Display player 2 controls (if in 2 player mode)
        if (player_type == 2) {
          // Format player 2 control method string
          if (manual_control[player2_car] == 2)
            sprintf(buffer, "%s %s", &config_buffer[4480], &config_buffer[4544]);
          else
            sprintf(buffer, "%s %s", &config_buffer[4480], &config_buffer[4608]);
          if (iControlSelection == 4)
            byColor_19 = 0xAB;
          else
            byColor_19 = 0x8F;
          scale_text(front_vga[15], buffer, font1_ascii, font1_offsets, 420, 60, byColor_19, 1u, 200, 640);

          // Player 2 customize controls option
          if (iControlSelection == 3)
            byColor_20 = 0xAB;
          else
            byColor_20 = 0x8F;
          // CUSTOMIZE PLAYER 2
          scale_text(front_vga[15], &config_buffer[704], font1_ascii, font1_offsets, 420, 78, byColor_20, 1u, 200, 640);
        }

        // Display player 1 controls
        if (manual_control[player1_car] == 2)
          sprintf(buffer, "%s %s", &config_buffer[4416], &config_buffer[4544]);
        else
          sprintf(buffer, "%s %s", &config_buffer[4416], &config_buffer[4608]);
        if (iControlSelection == 2)
          byColor_21 = 0xAB;
        else
          byColor_21 = 0x8F;
        scale_text(front_vga[15], buffer, font1_ascii, font1_offsets, 420, 96, byColor_21, 1u, 200, 640);
        // Player 1 customize controls option
        if (iControlSelection == 1)
          byColor_22 = 0xAB;
        else
          byColor_22 = 0x8F;
        // CUSTOMIZE PLAYER 1
        scale_text(front_vga[15], &config_buffer[768], font1_ascii, font1_offsets, 420, 114, byColor_22, 1u, 200, 640);

        // Back option
        if (iControlSelection)
          byColor_23 = 0x8F;
        else
          byColor_23 = 0xAB;
        scale_text(front_vga[15], &config_buffer[832], font1_ascii, font1_offsets, 420, 132, byColor_23, 1u, 200, 640);

        // Display player 1 control customization screen
        if (iControlSelection == 1 || iControlSelection == 2) {
          iControlLoop = 0;
          szControlName = &config_buffer[896];  // start of control name strings
          iY_1 = 200;
          // Display all 6 basic controls for player 1
          do {
            if (iControlLoop == control_edit)
              byControlColor = 0xAB;
            else
              byControlColor = 0x8F;
            front_text(front_vga[15], szControlName, font1_ascii, font1_offsets, 475, iY_1, byControlColor, 2u);
            if (iControlLoop == control_edit)
              byColor_24 = 0xAB;
            else
              byColor_24 = 0x8F;
            scale_text(front_vga[15], keyname[userkey[iControlLoop]], font1_ascii, font1_offsets, 480, iY_1, byColor_24, 0, 200, 640);
            szControlName += 64;                // next control name
            ++iControlLoop;
            iY_1 += 18;
          } while (iControlLoop < 6);
          if (Players_Cars[player1_car] >= 8) {
            if (control_edit == 12)
              byColor_25 = 0xAB;
            else
              byColor_25 = 0x8F;
            front_text(front_vga[15], "CHEAT:", font1_ascii, font1_offsets, 475, 308, byColor_25, 2u);
            if (control_edit == 12)
              byColor_26 = 0xAB;
            else
              byColor_26 = 0x8F;
            scale_text(front_vga[15], keyname[userkey[12]], font1_ascii, font1_offsets, 480, 308, byColor_26, 0, 200, 640);
          }
        }
        // Display Player 2 control customization screen
        else if (iControlSelection == 3 || iControlSelection == 4) {
          iControlIndex2 = 6;
          szText = &config_buffer[1280];
          iY_2 = 200;
          // Display all 6 controls for player 2
          do {
            if (iControlIndex2 == control_edit)
              byColor_27 = 0xAB;
            else
              byColor_27 = 0x8F;
            front_text(front_vga[15], szText, font1_ascii, font1_offsets, 475, iY_2, byColor_27, 2u);
            if (iControlIndex2 == control_edit)
              byColor_28 = 0xAB;
            else
              byColor_28 = 0x8F;
            front_text(front_vga[15], keyname[userkey[iControlIndex2]], font1_ascii, font1_offsets, 480, iY_2, byColor_28, 0);
            szText += 64;                       // Next control name
            ++iControlIndex2;
            iY_2 += 18;
          } while (iControlIndex2 < 12);

          // Display cheat key option for player 2
          if (Players_Cars[player2_car] >= 8) {
            if (control_edit == 13)
              byColor_29 = 0xAB;
            else
              byColor_29 = 0x8F;
            front_text(front_vga[15], "CHEAT:", font1_ascii, font1_offsets, 475, 308, byColor_29, 2u);
            if (control_edit == 13)
              byColor_30 = 0xAB;
            else
              byColor_30 = 0x8F;
            front_text(front_vga[15], keyname[userkey[13]], font1_ascii, font1_offsets, 480, 308, byColor_30, 0);
          }
        }

        // Handle active key mapping process
        if (!iControlsInEdit || iConfigState != 4)
          goto RENDER_FRAME;

        // Key detection and mapping logic
        iKeyCheckLoop = controlrelease;
        if (controlrelease)
          goto CHECK_CONTROL_INPUT;             // wait for key release

        // Scan for pressed keys
        iFoundKey = -1;
        iKeySearchIndex = 0;
        do {
          if (keyname[iKeySearchIndex] && keys[iKeyCheckLoop])
            iFoundKey = iKeyCheckLoop;
          ++iKeyCheckLoop;
          ++iKeySearchIndex;
        } while (iKeyCheckLoop < 128);

        // If no keyboard key pressed check joystick buttons
        if (iFoundKey == -1) {
          ReadJoys(&pJoyPos);
          if (pJoyPos.iJ1Button1)
            iFoundKey = 128;
          if (pJoyPos.iJ1Button2)
            iFoundKey = 129;
          if (pJoyPos.iJ2Button1)
            iFoundKey = 130;
          if (pJoyPos.iJ2Button2)
            iFoundKey = 131;
        }

        // If still no input check joystick axis movements
        if (iFoundKey == -1) {
          if (y2ok) {
            iJoyValue1 = 100 * (2 * pJoyPos.iJ2YAxis - JBYmax - JBYmin) / (JBYmax - JBYmin);
            if (iJoyValue1 < -50)
              iFoundKey = 138;
            if (iJoyValue1 > 50)
              iFoundKey = 139;
          }
          if (x2ok) {
            iJoyValue2 = 100 * (2 * pJoyPos.iJ2XAxis - JBXmax - JBXmin) / (JBXmax - JBXmin);
            if (iJoyValue2 < -50)
              iFoundKey = 136;
            if (iJoyValue2 > 50)
              iFoundKey = 137;
          }
          if (y1ok) {
            iJoyValue3 = 100 * (2 * pJoyPos.iJ1YAxis - JAYmax - JAYmin) / (JAYmax - JAYmin);
            if (iJoyValue3 < -50)
              iFoundKey = 134;
            if (iJoyValue3 > 50)
              iFoundKey = 135;
          }
          if (x1ok) {
            iJoyValue4 = 100 * (2 * pJoyPos.iJ1XAxis - JAXmax - JAXmin) / (JAXmax - JAXmin);
            if (iJoyValue4 < -50)
              iFoundKey = 132;
            if (iJoyValue4 > 50)
              iFoundKey = 133;
          }
        }

        // Validate input type compatibility for throttle controls
        //TODO
        //if ( iFoundKey != -1
        //          && (control_edit == 1 || control_edit == 7)
        //          && (userkey[control_edit] <= 0x83u && iFoundKey > 131 || userkey[control_edit] > 0x83u && iFoundKey <= 131) ) {
        ////if (iFoundKey != -1
        ////  && (control_edit == 1 || control_edit == 7)
        ////  && (*((_BYTE *)&keyname[139] + control_edit + 3) <= 0x83u && iFoundKey > 131 || *((_BYTE *)&keyname[139] + control_edit + 3) > 0x83u && iFoundKey <= 131)) {
        //  iFoundKey = -1;                       // reject incompatible input type
        //}

        if (iFoundKey == -1)
          goto CHECK_CONTROL_INPUT;

        // Check for duplicate key assignments
        iDuplicateCheck = 0;
        for (i = 0; i < control_edit; ++i) {
          if (userkey[i] == iFoundKey)
            iDuplicateCheck = -1;
        }
        if (iDuplicateCheck)
          goto CHECK_CONTROL_INPUT;             // Reject duplicate assignment

        // Assign the new key
        iEditIndex = control_edit + 1;
        iControlState = iControlsInEdit;
        controlrelease = -1;

        userkey[control_edit] = iFoundKey;
        //*((_BYTE *)&keyname[139] + iEditIndex + 3) = iFoundKey;

        // Handle completion logic for each player
        control_edit = iEditIndex;
        if (iControlState == 1)               // Player 1
        {
          if (iEditIndex < 6)
            goto CHECK_CONTROL_INPUT;
          // Check for cheat key mapping
          if (Players_Cars[player1_car] >= 8 && control_edit < 12) {
            control_edit = 12;                  // jump to cheat key
            goto CHECK_CONTROL_INPUT;
          }
        } else                                    // Player 2
        {
          if (iEditIndex < 12)
            goto CHECK_CONTROL_INPUT;
          if (Players_Cars[player2_car] >= 8 && control_edit < 13) {
            control_edit = 13;                  // jump to cheat key
            goto CHECK_CONTROL_INPUT;
          }
        }

        // All controls mapped, exit editing mode
        iControlsInEdit = 0;
        control_edit = -1;
        enable_keyboard();
      CHECK_CONTROL_INPUT:
              // Handle ESC key to restore original key mappings
        if (keys[1]) {
          memcpy(userkey, oldkeys, 0xCu);      // restore original player 1 keys
          memcpy(&userkey[12], &oldkeys[12], 2u);// restore original cheat keys
          enable_keyboard();
          iControlsInEdit = 0;
          control_edit = -1;
          check_joystick_usage();
        }
      RENDER_FRAME:
              // Display any received network messages
        show_received_mesage();

        // render
        copypic(scrbuf, screen);

        // Handle CHEAT_MODE_CLONES
        if (switch_same > 0) {
          // Clone all player cars to same type
          iPlayerIdx = 0;
          if (players > 0) {

            for (iPlayerIdx = 0; iPlayerIdx < players; iPlayerIdx++)
            {
              Players_Cars[iPlayerIdx] = switch_same - 666;
            }
            //iPlayersCarsOffset = 0;
            //do {
            //  iPlayersCarsOffset += 4;
            //  ++iPlayerIdx;
            //  *(int *)((char *)&infinite_laps + iPlayersCarsOffset) = switch_same - 666;// offset into Players_Cars
            //} while (iPlayerIdx < players);

          }

          cheat_mode |= CHEAT_MODE_CLONES;
          //uiTempCheatMode = cheat_mode;
          //BYTE1(uiTempCheatMode) = BYTE1(cheat_mode) | 0x40;
          //cheat_mode = uiTempCheatMode;
        } else if (switch_same < 0) {
          // Reset car cloning
          switch_same = 0;
          iPlayerIdx_1 = 0;
          if (players > 0) {

            for (iPlayerIdx_1 = 0; iPlayerIdx_1 < players; iPlayerIdx_1++)
            {
              Players_Cars[iPlayerIdx_1] = -1;
            }
            //iPlayersCarsOffset_1 = 0;
            //do {
            //  iPlayersCarsOffset_1 += 4;
            //  ++iPlayerIdx_1;
            //  *(int *)((char *)&infinite_laps + iPlayersCarsOffset_1) = -1;// offset into Players_Cars
            //} while (iPlayerIdx_1 < players);

          }

          cheat_mode &= ~CHEAT_MODE_CLONES;
          //cheat_mode &= ~0x4000u;
        }

        // Init screen fade
        if (!front_fade) {
          front_fade = -1;
          fade_palette(32);
          frames = 0;
        }

        // Process keyboard input when not editing controls
        if (!iControlsInEdit) {
          while (fatkbhit()) {
            UpdateSDL();
            switch (iConfigState) {
              case 0:                           // MAIN MENU NAVIGATION
                uiKeyCode = fatgetch();
                if (uiKeyCode < 0xD) {
                  if (!uiKeyCode)             // extended key
                  {
                    uiExtendedKey = fatgetch();
                    if (uiExtendedKey >= 0x48)// Up arrow
                    {
                      if (uiExtendedKey <= 0x48) {
                        iMenuDir2 = --iMenuSelection;
                        // Skip network option if disabled
                        if (!network_on && iMenuDir2 == 6)
                          iMenuSelection = 5;
                        if (iMenuSelection < 0)
                          iMenuSelection = 0;
                      } else if (uiExtendedKey == 80)// Down arrow
                      {
                        iMenuDir = ++iMenuSelection;
                        // Skip network option if disabled
                        if (!network_on && iMenuDir == 6)
                          iMenuSelection = 7;
                        if (iMenuSelection > 7)
                          iMenuSelection = 7;
                      }
                    }
                  }
                } else if (uiKeyCode <= 0xD)    // enter key
                {
                  switch (iMenuSelection) {
                    case 0:                     // drivers
                      iConfigState = 1;
                      iSelectedCar = 0;
                      break;
                    case 1:                     // audio
                      iConfigState = 2;
                      iVolumeSelection = 0;
                      break;
                    case 2:                     // joystick
                      iConfigState = 3;
                      check_joystickpresence();
                      break;
                    case 3:                     // controls
                      iConfigState = 4;
                      iControlSelection = 0;
                      iControlsInEdit = 0;
                      Joy1used = 0;
                      Joy2used = 0;
                      controlrelease = -1;
                      control_edit = -1;
                      break;
                    case 4:                     // video
                      iConfigState = 5;
                      iVideoState = 0;
                      break;
                    case 5:                     // graphics
                      iConfigState = 6;
                      iGraphicsState = 0;
                      break;
                    case 6:                     // network
                      iConfigState = 7;
                      iNetworkState = 0;
                      iEditingName = 0;
                      break;
                    case 7:                     // exit
                      sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);
                      iExitFlag = -1;
                      break;
                    default:
                      continue;
                  }
                } else if (uiKeyCode == 27)     // ESC key
                {
                  iExitFlag = -1;
                  sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);
                }
                continue;
              case 1:                           // DRIVER/CAR CONFIG
                iKeyInput = fatgetch();
                iKeyChar = iKeyInput;
                if ((unsigned int)iKeyInput < 8) {
                  if (iKeyInput)
                    goto HANDLE_CHAR_INPUT;     // Handle character input

                  // Handle arrow keys for car selection
                  uiArrowKey = fatgetch();
                  if (!iEditingName && uiArrowKey >= 0x48) {
                    if (uiArrowKey <= 0x48)   // up arrow
                    {
                      iNextSelectedCar = ++iSelectedCar;
                      // Skip player 2 slot in single player mode
                      if (player_type != 2 && iNextSelectedCar == 2)
                        iSelectedCar = 3;
                      if (iSelectedCar > 18)
                        iSelectedCar = 18;
                    } else if (uiArrowKey == 80)// down arrow
                    {
                      iPrevSelectedCar = --iSelectedCar;
                      // Skip player 2 slot in single player mode
                      if (player_type != 2 && iPrevSelectedCar == 2)
                        iSelectedCar = 1;
                      if (iSelectedCar < 0)
                        iSelectedCar = 0;
                    }
                  }
                } else if ((unsigned int)iKeyInput <= 8)// backspace
                {
                  if (iEditingName) {
                    // Handle backspace in name editing
                    iNameLength_1 = iNameLength;
                    szNewNameBuf[iNameLength] = 0;
                    if (iNameLength_1 > 0) {
                      iNameLength = iNameLength_1 - 1;
                      szNewNameBuf[iNameLength_1 - 1] = 0;
                    }
                  }
                } else if ((unsigned int)iKeyInput < 0xD)// regular character input
                {
                HANDLE_CHAR_INPUT:
                  if (iEditingName) {
                    // Convert lowercase to uppercase
                    if (iKeyInput >= 97 && iKeyInput <= 122)
                      iKeyChar = iKeyInput - 32;

                    // Accept alphanumeric chars and space
                    if ((iKeyChar == 32 || iKeyChar >= 65 && iKeyChar <= 90 || iKeyChar >= 48 && iKeyChar <= 57) && iNameLength < 8) {
                      iNameLength_2 = iNameLength + 1;
                      szNewNameBuf[iNameLength] = iKeyChar;
                      iNameLength = iNameLength_2;
                      szNewNameBuf[iNameLength_2] = 0;
                    }
                  }
                } else if ((unsigned int)iKeyInput <= 0xD)// enter key
                {
                  iEditingName_1 = iEditingName;
                  if (iEditingName) {
                    // Save edited name
                    iEditingName = 0;
                    if (iSelectedCar) {
                      if ((unsigned int)iSelectedCar <= 1) {

                        // Save player 1 name

                        for (j = 0; j < 9; j++)
                        {
                            player_names[player1_car][j] = szNewNameBuf[j];
                        }
                        //for (j = 0; j < 9; cheat_names[player1_car + 31][j + 8] = *((_BYTE *)&pJoyPos.iJ2YAxis + j + 3))
                        //  ++j;

                        broadcast_mode = -669;
                        while (broadcast_mode)
                          UpdateSDL();
                        if (!network_on)
                          waste = CheckNames(player_names[player1_car], player1_car);
                        check_cars();
                      } else {
                        if (iSelectedCar != 2)
                          goto SAVE_AI_DRIVER_NAME;
                        iPlayer2Car = player2_car;

                        for (k = 0; k < 9; k++) 
                        {
                          player_names[player2_car][k] = szNewNameBuf[k];
                        }
                        //for (k = 0; k < 9; cheat_names[iPlayer2Car + 31][k + 8] = *((_BYTE *)&pJoyPos.iJ2YAxis + k + 3))
                        //  ++k;

                        waste = CheckNames(player_names[iPlayer2Car], iPlayer2Car);
                        check_cars();
                      }
                    } else                        // AI players
                    {
                    SAVE_AI_DRIVER_NAME:
                      iDefaultNamesIdx_1 = iSelectedCar - 3;
                      iDefaultNamesIdx_1 = (iSelectedCar - 3) ^ 1;// Toggle for paired AI drivers
                      //LOBYTE(iDefaultNamesIdx_1) = (iSelectedCar - 3) ^ 1;// Toggle for paired AI drivers
                      iDefaultNamesCharItr = 0;
                      iDefaultNamesIdx = iDefaultNamesIdx_1;

                      do
                      {
                        default_names[iDefaultNamesIdx][iDefaultNamesCharItr] = szNewNameBuf[iDefaultNamesCharItr];
                        ++iDefaultNamesCharItr;
                      }
                      while (iDefaultNamesCharItr < 9);
                      //do {
                      //  ++iDefaultNamesCharItr;
                      //  *((_BYTE *)&team_col[15] + iDefaultNamesCharItr + iDefaultNamesIdx * 9 + 3) = *((_BYTE *)&pJoyPos.iJ2YAxis + iDefaultNamesCharItr + 3);// offset into default_names
                      //} while (iDefaultNamesCharItr < 9);

                      // Set default name if empty
                      if (!default_names[iDefaultNamesIdx][0]) {
                        sprintf(buffer, "comp %i", iSelectedCar - 2);
                        name_copy(default_names[iDefaultNamesIdx], buffer);
                      }
                      broadcast_mode = -1;
                      while (broadcast_mode)
                        UpdateSDL();
                    }
                  } else {
                    // Start editing name
                    if (!iSelectedCar)
                      goto EXIT_NAME_EDITING;
                    iEditingName = 1;
                    if (iSelectedCar >= 3) {
                      // Check if AI car slot is editable
                      if ((((uint8)iSelectedCar - 3) & 1) != 0)
                        iEditingName = allocated_cars[(iSelectedCar - 3) / 2] <= 0;
                      else
                        iEditingName = allocated_cars[(iSelectedCar - 3) / 2] <= 1;
                    }

                    if (iEditingName == 1) {
                      iNameLength = 0;
                      if ((unsigned int)iSelectedCar <= 1)// Load player 1 name
                      {
                        for (m = 0; m < 9; m++) 
                        {
                          szNewNameBuf[m] = player_names[player1_car][m];
                        }
                        //for (m = 0; m < 9; *((_BYTE *)&pJoyPos.iJ2YAxis + m + 3) = cheat_names[player1_car + 31][m + 8])
                        //  ++m;
                      } else if (iSelectedCar == 2)// Load player 2 name
                      {
                        for (n = 0; n < 9; n++) 
                        {
                          szNewNameBuf[n] = player_names[player2_car][n];
                        }
                        //for (n = 0; n < 9; *((_BYTE *)&pJoyPos.iJ2YAxis + n + 3) = cheat_names[player2_car + 31][n + 8])
                        //  ++n;
                      } else                      // Load AI driver name
                      {
                        iAIDriverIdx = iSelectedCar - 3;
                        iAIDriverIdx ^= 1;  // Toggle the lowest bit
                        for (int i = 0; i < 9; i++)
                        {
                          szNewNameBuf[i] = default_names[iAIDriverIdx][i];
                        }
                        //iAIDriverIdx = iSelectedCar - 3;
                        //LOBYTE(iAIDriverIdx) = (iSelectedCar - 3) ^ 1;
                        //v189 = 0;
                        //iOffset = 9 * iAIDriverIdx;
                        //do {
                        //  ++v189;
                        //  v191 = default_names[0][iOffset++];
                        //  *((_BYTE *)&pJoyPos.iJ2YAxis + v189 + 3) = v191;
                        //} while (v189 < 9);

                      }

                      // Calculate current name length
                      while (szNewNameBuf[iNameLength])
                        ++iNameLength;
                    }

                  }
                } else {
                  if (iKeyInput != 27)        // ESC key
                    goto HANDLE_CHAR_INPUT;
                  iEditingName_1 = iEditingName;
                  if (iEditingName) {
                    // Cancel editing, restore original name
                    iEditingName = 0;
                    if (iSelectedCar) {
                      if ((unsigned int)iSelectedCar <= 1)// player 1
                      {

                        // Restore player 1 name

                        for (ii = 0; ii < 9; ii++) 
                        {
                          player_names[player1_car][ii] = szNewNameBuf[ii];
                        }
                        //for (ii = 0; ii < 9; cheat_names[player1_car + 31][ii + 8] = *((_BYTE *)&pJoyPos.iJ2YAxis + ii + 3))
                        //  ++ii;
                        broadcast_mode = -669;
                        while (broadcast_mode)
                          UpdateSDL();
                        if (!network_on)
                          waste = CheckNames(player_names[player1_car], player1_car);
                      } else {
                        if (iSelectedCar != 2)// player 2
                          goto CANCEL_AI_NAME_EDIT;
                        iPlayer2Car_1 = player2_car;

                        // Restore player 2 name

                        for (jj = 0; jj < 9; jj++) 
                        {
                          player_names[player2_car][jj] = szNewNameBuf[jj];
                        }
                        //for (jj = 0; jj < 9; cheat_names[iPlayer2Car_1 + 31][jj + 8] = *((_BYTE *)&pJoyPos.iJ2YAxis + jj + 3))
                        //  ++jj;
                        waste = CheckNames(player_names[iPlayer2Car_1], iPlayer2Car_1);
                      }
                    } else                        // AI driver
                    {
                    CANCEL_AI_NAME_EDIT:
                      // Restore AI driver name
                      iAIDriverIdx_1 = iSelectedCar - 3;
                      iAIDriverIdx_1 ^= 1;  // Toggle between paired AI drivers
                      for (v196 = 0; v196 < 9; v196++)
                      {
                        default_names[iAIDriverIdx_1][v196] = szNewNameBuf[v196];
                      }
                      iAIDriverIdx_1 = iSelectedCar - 3;
                      iAIDriverIdx_1 = (iSelectedCar - 3) ^ 1;
                      //LOBYTE(iAIDriverIdx_1) = (iSelectedCar - 3) ^ 1;
                      v196 = 0;
                      v197 = iAIDriverIdx_1;
                      
                      
                      //do {
                      //  ++v196;
                      //  *((_BYTE *)&team_col[15] + v196 + v197 * 9 + 3) = *((_BYTE *)&pJoyPos.iJ2YAxis + v196 + 3);
                      //} while (v196 < 9);

                      if (!default_names[iAIDriverIdx_1][0]) {
                        sprintf(buffer, "comp %i", iSelectedCar - 2);
                        name_copy(default_names[v197], buffer);
                      }
                      broadcast_mode = -1;
                      while (broadcast_mode)
                        UpdateSDL();
                    }
                  } else {
                  EXIT_NAME_EDITING:
                    iConfigState = iEditingName_1;
                  }
                }
                continue;
              case 2:
                // Audio/Volume config
                uiKey_5 = fatgetch();
                if (uiKey_5 < 0xD) {
                  if (!uiKey_5)               // Extended key
                  {
                    switch (fatgetch()) {
                      case 0x48:                // UP arrow
                        if (iVolumeSelection) {
                          if (iVolumeSelection > 1)
                            --iVolumeSelection;
                        } else {
                          iVolumeSelection = 7;
                        }
                        break;
                      case 0x4B:                // Left arrow - decrease volume
                        switch (iVolumeSelection) {
                          case 1:               // Engine volume
                            EngineVolume -= 4;
                            if (EngineVolume < 0)
                              EngineVolume = 0;
                            break;
                          case 2:               // SFX volume
                            SFXVolume -= 4;
                            if (SFXVolume < 0)
                              SFXVolume = 0;
                            break;
                          case 3:               // Speech volume
                            SpeechVolume -= 4;
                            if (SpeechVolume < 0)
                              SpeechVolume = 0;
                            break;
                          case 4:               // Music volume
                            MusicVolume -= 4;
                            if (MusicVolume < 0)
                              MusicVolume = 0;
                            // Update hardware volume
                            if (MusicCard)
                              MIDISetMasterVolume(MusicVolume);
                              //sosMIDISetMasterVolume(MusicVolume);
                            if (MusicCD)
                              goto SET_CD_VOLUME;// CONTINUE_AUDIO_INPUT: Continue processing audio menu input
                            break;
                          default:
                            continue;
                        }
                        break;
                      case 0x4D:                // Right arrow - increase volume
                        switch (iVolumeSelection) {
                          case 1:               // Engine volume
                            EngineVolume += 4;
                            if (EngineVolume >= 128)
                              EngineVolume = 127;
                            break;
                          case 2:               // SFX volume
                            SFXVolume += 4;
                            if (SFXVolume >= 128)
                              SFXVolume = 127;
                            break;
                          case 3:               // Speech volume
                            SpeechVolume += 4;
                            if (SpeechVolume >= 128)
                              SpeechVolume = 127;
                            break;
                          case 4:               // Music volume
                            MusicVolume += 4;
                            if (MusicVolume >= 128)
                              MusicVolume = 127;
                            // Update hardware volume
                            if (MusicCard)
                              MIDISetMasterVolume(MusicVolume);
                              //sosMIDISetMasterVolume(MusicVolume);
                            if (MusicCD)
                              SET_CD_VOLUME:
                            SetAudioVolume(MusicVolume);
                            break;
                          default:
                            continue;
                        }
                        break;
                      case 0x50:                // Down arrow
                        iNextVolumeSelection = iVolumeSelection;
                        if (iVolumeSelection > 0) {
                          ++iVolumeSelection;
                          if (iNextVolumeSelection + 1 > 7)
                            iVolumeSelection = 0;
                        }
                        break;
                      default:
                        continue;
                    }
                  }
                } else if (uiKey_5 <= 0xD)      // Enter key
                {
                  switch (iVolumeSelection) {
                    case 0:                     // Back
                      goto EXIT_AUDIO_MENU;     // Return to main menu
                    case 5:                     // Toggle engine mode
                      allengines = allengines == 0;
                      break;
                    case 6:                     // Toggle sound effects
                      if (SoundCard) {
                        kk = soundon != 0;
                        soundon = soundon == 0;
                        if (!kk)
                          loadfatalsample();
                      } else {
                        soundon = 0;
                      }
                      break;
                    case 7:                     // Toggle music
                      if (MusicCard || MusicCD) {
                        musicon = musicon == 0;
                        reinitmusic();
                      } else {
                        musicon = MusicCard;
                      }
                      break;
                    default:
                      continue;
                  }
                } else if (uiKey_5 == 0x1B)     // ESC key
                {
                EXIT_AUDIO_MENU_2:
                  iConfigState = 0;
                }
                continue;
              case 3:                           // JOYSTICK CALIBRATION INPUT
                uiKey = fatgetch();
                if (uiKey < 0xD) {
                  if (!uiKey)
                    fatgetch();                 // Consume extended key
                } else if (uiKey <= 0xD || uiKey == 0x1B)// Enter or ESC
                {
                  remove_uncalibrated();
                  iConfigState = 0;             // Return to main menu
                }
                continue;
              case 4:                           // CONTROL CONFIG INPUT
                uiKey_1 = fatgetch();
                if (uiKey_1 < 0xD) {
                  if (!uiKey_1)               // Extended key
                  {
                    uiKey_6 = fatgetch();
                    if (uiKey_6 >= 0x48) {
                      if (uiKey_6 <= 0x48)    // Up arrow
                      {
                        if (!iControlsInEdit) {
                          iNextControlSelection = ++iControlSelection;
                          if (player_type == 2) {
                            if (iNextControlSelection > 4)
                              iControlSelection = 4;
                          } else if (iNextControlSelection > 2) {
                            iControlSelection = 2;
                          }
                        }
                      } else if (uiKey_6 == 80 && !iControlsInEdit && --iControlSelection < 0) {
                        iControlSelection = iControlsInEdit;
                      }
                    }
                  }
                } else if (uiKey_1 <= 0xD)      // Enter key
                {
                  switch (iControlSelection) {
                    case 0:                     // Back
                      goto EXIT_CONTROLS_MENU;  // Return to main menu
                    case 1:                     // Customize Player 1
                      control_edit = 0;
                      disable_keyboard();
                      memcpy(oldkeys, userkey, 0xCu);// Backup current keys
                      memcpy(&oldkeys[12], &userkey[12], 2u);// Backup cheat keys
                      iControlsInEdit = 1;
                      controlrelease = -1;
                      break;
                    case 2:                     // Toggle player 1 control method
                      if (manual_control[player1_car] == 2)
                        manual_control[player1_car] = 1;// Switch to keyboard
                      else
                        manual_control[player1_car] = 2;// Switch to joystick
                      broadcast_mode = -1;
                      while (broadcast_mode)
                        UpdateSDL();
                      break;
                    case 3:                     // Customize player 2
                      iControlsInEdit = 2;
                      control_edit = 6;         // Start with player 2 controls
                      disable_keyboard();
                      memcpy(oldkeys, userkey, 0xCu);// backup current keys
                      memcpy(&oldkeys[12], &userkey[12], 2u);// backup cheat keys
                      controlrelease = -1;
                      break;
                    case 4:
                      if (manual_control[player2_car] == 2)
                        manual_control[player2_car] = 1;// switch to keyboard
                      else
                        manual_control[player2_car] = 2;// switch to joystick
                      break;
                    default:
                      continue;
                  }
                } else if (uiKey_1 == 27) {
                  iConfigState = 0;
                }
                continue;
              case 5:                           // VIDEO
                uiKey_2 = fatgetch();
                if (uiKey_2 < 0xD) {
                  if (!uiKey_2) {
                    switch (fatgetch()) {
                      case 0x48:
                        if (++iVideoState > 16)
                          iVideoState = 16;
                        break;
                      case 0x4B:
                        if (iVideoState == 2) {
                          if (game_svga) {
                            game_size -= 16;
                            if (game_size < 64)
                              game_size = 64;
                          } else {
                            game_size -= 8;
                            if (game_size < 32)
                              game_size = 32;
                          }
                        }
                        break;
                      case 0x4D:
                        if (iVideoState == 2) {
                          if (game_svga) {
                            game_size += 16;
                            if (game_size > 128)
                              game_size = 128;
                          } else {
                            game_size += 8;
                            if (game_size > 64)
                              game_size = 64;
                          }
                        }
                        break;
                      case 0x50:
                        if (--iVideoState < 0)
                          iVideoState = 0;
                        break;
                      default:
                        continue;
                    }
                  }
                } else if (uiKey_2 <= 0xD) {
                  switch (iVideoState) {
                    case 0:
                      iConfigState = 0;
                      break;
                    case 1:
                      if (game_svga) {
                        game_svga = 0;
                        game_size /= 2;
                      } else if (svga_possible && !no_mem) {
                        game_svga = -1;
                        game_size *= 2;
                      }
                      break;
                    case 2:
                      if (game_svga) {
                        game_size += 16;
                        if (game_size > 128)
                          game_size = 64;
                      } else {
                        game_size += 8;
                        if (game_size > 64)
                          game_size = 32;
                      }
                      break;
                    case 3:
                      if (view_limit) {
                        view_limit = 0;
                      } else if (machine_speed >= 2800) {
                        view_limit = 32;
                      } else {
                        view_limit = 24;
                      }
                      break;
                    case 4: //PANEL ON
                      if ((textures_off & TEX_OFF_PANEL_OFF) != 0) {
                        uiTexOffTemp_7 = textures_off;
                        uiTexOffTemp_7 = textures_off ^ TEX_OFF_PANEL_OFF;
                        //LOBYTE(uiTexOffTemp_7) = textures_off ^ 0x20;
                        textures_off = uiTexOffTemp_7 | TEX_OFF_PANEL_RESTRICTED;
                      } else if ((textures_off & TEX_OFF_PANEL_RESTRICTED) != 0) {
                        textures_off ^= TEX_OFF_PANEL_RESTRICTED;
                      } else {
                        textures_off |= TEX_OFF_PANEL_OFF;
                      }
                      break;
                    case 5:
                      uiTexOffTemp = textures_off;
                      uiTexOffTemp = textures_off ^ TEX_OFF_CLOUDS;
                      //LOBYTE(uiTexOffTemp) = textures_off ^ 8;
                      textures_off = uiTexOffTemp;
                      break;
                    case 6:
                      uiTexOffTemp_1 = textures_off;
                      uiTexOffTemp_1 = textures_off ^ TEX_OFF_SHADOWS;
                      //BYTE1(uiTexOffTemp_1) = BYTE1(textures_off) ^ 1;
                      textures_off = uiTexOffTemp_1;
                      break;
                    case 7:
                      textures_off ^= TEX_OFF_ROAD_TEXTURES;
                      break;
                    case 8:
                      textures_off ^= TEX_OFF_BUILDING_TEXTURES;
                      break;
                    case 9:
                      uiTexOffTemp_2 = textures_off;
                      uiTexOffTemp_2 = textures_off ^ TEX_OFF_GROUND_TEXTURES;
                      //LOBYTE(uiTexOffTemp_2) = textures_off ^ 1;
                      textures_off = uiTexOffTemp_2;
                      break;
                    case 10:
                      uiTexOffTemp_3 = textures_off;
                      uiTexOffTemp_3 = textures_off ^ TEX_OFF_WALL_TEXTURES;
                      //LOBYTE(uiTexOffTemp_3) = textures_off ^ 4;
                      textures_off = uiTexOffTemp_3;
                      break;
                    case 11:
                      uiTexOffTemp_4 = textures_off;
                      uiTexOffTemp_4 = textures_off ^ TEX_OFF_CAR_TEXTURES;
                      //LOBYTE(uiTexOffTemp_4) = textures_off ^ 0x40;
                      textures_off = uiTexOffTemp_4;
                      break;
                    case 12:
                      uiTexOffTemp_5 = textures_off;
                      uiTexOffTemp_5 = textures_off ^ TEX_OFF_HORIZON;
                      //LOBYTE(uiTexOffTemp_5) = textures_off ^ 0x10;
                      textures_off = uiTexOffTemp_5;
                      break;
                    case 13:
                      textures_off ^= TEX_OFF_GLASS_WALLS;
                      break;
                    case 14:
                      textures_off ^= TEX_OFF_BUILDINGS;
                      break;
                    case 15:
                      if (++names_on > 2)
                        names_on = 0;
                      break;
                    case 16:
                      textures_off ^= TEX_OFF_PERSPECTIVE_CORRECTION;
                      break;
                    default:
                      continue;
                  }
                } else if (uiKey_2 == 27) {
                EXIT_AUDIO_MENU:
                  iConfigState = 0;
                }
                continue;
              case 6:                           // GRAPHICS
                uiKey_3 = fatgetch();
                if (uiKey_3 < 0xD) {
                  if (!uiKey_3) {
                    switch (fatgetch()) {
                      case 0x48:
                        iPlayerIndex = ++iGraphicsState;
                        if (player_type == 2) {
                          if (iPlayerIndex > 6)
                            iGraphicsState = 6;
                        } else if (iPlayerIndex > 5) {
                          iGraphicsState = 5;
                        }
                        break;
                      case 0x50:
                        if (--iGraphicsState < 0)
                          iGraphicsState = 0;
                        break;
                      default:
                        continue;
                    }
                  }
                } else if (uiKey_3 <= 0xD) {
                  switch (iGraphicsState) {
                    case 0:
                      goto EXIT_AUDIO_MENU_2;
                    case 1:
                      false_starts = false_starts == 0;
                      broadcast_mode = -1;
                      while (broadcast_mode)
                        UpdateSDL();
                      break;
                    case 2:
                      if (lots_of_mem)
                        p_tex_size = p_tex_size != 1;
                      break;
                    case 3:
                      replay_record = replay_record == 0;
                      break;
                    case 4:
                      uiTexOffTemp_6 = textures_off;
                      uiTexOffTemp_6 = textures_off ^ TEX_OFF_KMH;
                      //BYTE1(uiTexOffTemp_6) = BYTE1(textures_off) ^ 4;
                      textures_off = uiTexOffTemp_6;
                      break;
                    case 5:
                      do {
                        if (++game_view[0] == 9)
                          game_view[0] = 0;
                      } while (!AllowedViews[game_view[0]]);
                      break;
                    case 6:
                      do {
                        if (++game_view[1] == 9)
                          game_view[1] = 0;
                      } while (!AllowedViews[game_view[1]]);
                      break;
                    default:
                      continue;
                  }
                } else if (uiKey_3 == 27) {
                EXIT_CONTROLS_MENU:
                  iDisplayIndex = 0;
                  goto LABEL_900;
                }
                continue;
              case 7:                           // NETWORK
                uiKey_4 = fatgetch();
                uiDataValue1 = uiKey_4;
                iGameIndex = uiKey_4;
                if (uiKey_4 < 8) {
                  if (uiKey_4)
                    goto LABEL_1028;
                  iPlayerIndex2 = iEditingName;
                  uiDataValue3 = fatgetch();
                  if (!iPlayerIndex2 && uiDataValue3 >= 0x48) {
                    if (uiDataValue3 <= 0x48) {
                      if (++iNetworkState > 5)
                        iNetworkState = 5;
                    } else if (uiDataValue3 == 80 && --iNetworkState < 0) {
                      iNetworkState = uiDataValue1;
                    }
                  }
                } else {
                  uiDataValue2 = 14 * (4 - iNetworkState);
                  if (uiDataValue1 <= 8) {
                    if (iEditingName) {
                      iDataIndex = iNameLength;
                      network_messages[0][iNameLength + uiDataValue2] = 0;
                      if (iDataIndex > 0) {
                        iNameLength = iDataIndex - 1;
                        network_messages[0][iDataIndex - 1 + uiDataValue2] = 0;
                      }
                    }
                  } else {
                    if (uiDataValue1 < 0xD)
                      goto LABEL_1028;
                    if (uiDataValue1 <= 0xD) {
                      if (iEditingName) {
                        iEditingName = uiDataValue1 ^ iGameIndex;
                      } else if (iNetworkState) {
                        if (iNetworkState == 5) {
                          select_messages();
                        } else {
                          iNameLength = iEditingName;
                          iCounterVar = 14 * (4 - iNetworkState);
                          byTempFlag = network_messages[uiDataValue2 / 0xE][0];
                          iEditingName = 1;
                          for (kk = byTempFlag == 0; !kk; kk = byStatusFlag == 0) {
                            byStatusFlag = network_messages[0][++iCounterVar];
                            ++iNameLength;
                          }
                        }
                      } else {
                        iConfigState = iEditingName;
                      }
                    } else if (uiDataValue1 == 27) {
                      iDisplayIndex = iEditingName;
                      if (iEditingName)
                        iEditingName = 0;
                      else
                        LABEL_900:
                      iConfigState = iDisplayIndex;
                    } else {
                    LABEL_1028:
                      if (iEditingName && iNameLength < 13) {
                        if (iGameIndex >= 97 && iGameIndex <= 122)
                          iGameIndex -= 32;
                        if (iGameIndex >= 65 && iGameIndex <= 90 || iGameIndex >= 48 && iGameIndex <= 57 || iGameIndex == 32 || iGameIndex == 46 || iGameIndex == 39) {
                          iResultValue = 14 * (4 - iNetworkState);
                          iCalculation = iNameLength + 1;
                          network_messages[0][iNameLength + iResultValue] = iGameIndex;
                          iNameLength = iCalculation;
                          network_messages[0][iCalculation + iResultValue] = 0;
                        }
                      }
                    }
                  }
                }
                break;
              default:
                continue;
            }
          }
        }
        if (!iExitFlag)
          continue;
        fade_palette(0);
        front_fade = 0;
        return;
      case 4:
        if (iConfigState != 5)
          iVideoState = -1;
        if (iVideoState == 16)
          byColor_31 = 0xAB;
        else
          byColor_31 = 0x8F;
        scale_text(front_vga[15], &config_buffer[6912], font1_ascii, font1_offsets, 435, 60, byColor_31, 2u, 200, 640);
        if ((textures_off & TEX_OFF_PERSPECTIVE_CORRECTION) != 0) {
          if (iVideoState == 16)
            byColor_32 = 0xAB;
          else
            byColor_32 = 0x8F;
          byTempChar1 = byColor_32;
          szText_1 = &config_buffer[2688];
        } else {
          if (iVideoState == 16)
            byColor_33 = 0xAB;
          else
            byColor_33 = 0x8F;
          byTempChar1 = byColor_33;
          szText_1 = &config_buffer[2624];
        }
        scale_text(front_vga[15], szText_1, font1_ascii, font1_offsets, 440, 60, byTempChar1, 0, 200, 640);
        sprintf(buffer, "%s:", &config_buffer[3968]);
        if (iVideoState == 15)
          byColor_34 = 0xAB;
        else
          byColor_34 = 0x8F;
        scale_text(front_vga[15], buffer, font1_ascii, font1_offsets, 435, 80, byColor_34, 2u, 200, 640);
        if (names_on) {
          if (names_on == 2) {
            if (iVideoState == 15)
              byColor_105 = 0xAB;
            else
              byColor_105 = 0x8F;
            scale_text(front_vga[15], &config_buffer[2816], font1_ascii, font1_offsets, 440, 80, byColor_105, 0, 200, 640);
          } else {
            if (iVideoState == 15)
              byColor_35 = 0xAB;
            else
              byColor_35 = 0x8F;
            scale_text(front_vga[15], &config_buffer[2624], font1_ascii, font1_offsets, 440, 80, byColor_35, 0, 200, 640);
          }
        } else {
          if (iVideoState == 15)
            byColor_36 = 0xAB;
          else
            byColor_36 = 0x8F;
          scale_text(front_vga[15], &config_buffer[2688], font1_ascii, font1_offsets, 440, 80, byColor_36, 0, 200, 640);
        }
        if (iVideoState == 14)
          byColor_37 = 0xAB;
        else
          byColor_37 = 0x8F;
        scale_text(front_vga[15], &config_buffer[3008], font1_ascii, font1_offsets, 435, 100, byColor_37, 2u, 200, 640);
        if ((textures_off & TEX_OFF_BUILDINGS) != 0) {
          if (iVideoState == 14)
            byColor_38 = 0xAB;
          else
            byColor_38 = 0x8F;
          scale_text(front_vga[15], &config_buffer[2688], font1_ascii, font1_offsets, 440, 100, byColor_38, 0, 200, 640);
        } else {
          if (iVideoState == 14)
            byColor_39 = 0xAB;
          else
            byColor_39 = 0x8F;
          scale_text(front_vga[15], &config_buffer[2624], font1_ascii, font1_offsets, 440, 100, byColor_39, 0, 200, 640);
        }
        if (iVideoState == 13)
          byColor_40 = 0xAB;
        else
          byColor_40 = 0x8F;
        scale_text(front_vga[15], &config_buffer[3072], font1_ascii, font1_offsets, 435, 120, byColor_40, 2u, 200, 640);
        if ((textures_off & TEX_OFF_GLASS_WALLS) != 0) {
          if (iVideoState == 13)
            byColor_41 = 0xAB;
          else
            byColor_41 = 0x8F;
          scale_text(front_vga[15], &config_buffer[2688], font1_ascii, font1_offsets, 440, 120, byColor_41, 0, 200, 640);
        } else {
          if (iVideoState == 13)
            byColor_42 = 0xAB;
          else
            byColor_42 = 0x8F;
          scale_text(front_vga[15], &config_buffer[2624], font1_ascii, font1_offsets, 440, 120, byColor_42, 0, 200, 640);
        }
        if (iVideoState == 12)
          byColor_43 = 0xAB;
        else
          byColor_43 = 0x8F;
        scale_text(front_vga[15], &config_buffer[3200], font1_ascii, font1_offsets, 435, 140, byColor_43, 2u, 200, 640);
        if ((textures_off & TEX_OFF_HORIZON) != 0) {
          if (iVideoState == 12)
            byColor_44 = 0xAB;
          else
            byColor_44 = 0x8F;
          scale_text(front_vga[15], &config_buffer[2688], font1_ascii, font1_offsets, 440, 140, byColor_44, 0, 200, 640);
        } else {
          if (iVideoState == 12)
            byColor_45 = 0xAB;
          else
            byColor_45 = 0x8F;
          scale_text(front_vga[15], &config_buffer[2624], font1_ascii, font1_offsets, 440, 140, byColor_45, 0, 200, 640);
        }
        if (iVideoState == 11)
          byColor_46 = 0xAB;
        else
          byColor_46 = 0x8F;
        scale_text(front_vga[15], &config_buffer[3136], font1_ascii, font1_offsets, 435, 160, byColor_46, 2u, 200, 640);
        if ((textures_off & TEX_OFF_CAR_TEXTURES) != 0) {
          if (iVideoState == 11)
            byColor_47 = 0xAB;
          else
            byColor_47 = 0x8F;
          scale_text(front_vga[15], &config_buffer[2688], font1_ascii, font1_offsets, 440, 160, byColor_47, 0, 200, 640);
        } else {
          if (iVideoState == 11)
            byColor_48 = 0xAB;
          else
            byColor_48 = 0x8F;
          scale_text(front_vga[15], &config_buffer[2624], font1_ascii, font1_offsets, 440, 160, byColor_48, 0, 200, 640);
        }
        if (iVideoState == 10)
          byColor_49 = 0xAB;
        else
          byColor_49 = 0x8F;
        scale_text(front_vga[15], &config_buffer[3264], font1_ascii, font1_offsets, 435, 180, byColor_49, 2u, 200, 640);
        if ((textures_off & TEX_OFF_WALL_TEXTURES) != 0) {
          if (iVideoState == 10)
            byColor_50 = 0xAB;
          else
            byColor_50 = 0x8F;
          scale_text(front_vga[15], &config_buffer[2688], font1_ascii, font1_offsets, 440, 180, byColor_50, 0, 200, 640);
        } else {
          if (iVideoState == 10)
            byColor_51 = 0xAB;
          else
            byColor_51 = 0x8F;
          scale_text(front_vga[15], &config_buffer[2624], font1_ascii, font1_offsets, 440, 180, byColor_51, 0, 200, 640);
        }
        if (iVideoState == 9)
          byColor_52 = 0xAB;
        else
          byColor_52 = 0x8F;
        scale_text(front_vga[15], &config_buffer[3328], font1_ascii, font1_offsets, 435, 200, byColor_52, 2u, 200, 640);
        if ((textures_off & TEX_OFF_GROUND_TEXTURES) != 0) {
          if (iVideoState == 9)
            byColor_53 = 0xAB;
          else
            byColor_53 = 0x8F;
          scale_text(front_vga[15], &config_buffer[2688], font1_ascii, font1_offsets, 440, 200, byColor_53, 0, 200, 640);
        } else {
          if (iVideoState == 9)
            byColor_54 = 0xAB;
          else
            byColor_54 = 0x8F;
          scale_text(front_vga[15], &config_buffer[2624], font1_ascii, font1_offsets, 440, 200, byColor_54, 0, 200, 640);
        }
        if (iVideoState == 8)
          byColor_55 = 0xAB;
        else
          byColor_55 = 0x8F;
        scale_text(front_vga[15], &config_buffer[3392], font1_ascii, font1_offsets, 435, 220, byColor_55, 2u, 200, 640);
        if ((textures_off & TEX_OFF_BUILDING_TEXTURES) == 0) {
          if (iVideoState == 8)
            byColor_56 = 0xAB;
          else
            byColor_56 = 0x8F;
          scale_text(front_vga[15], &config_buffer[2624], font1_ascii, font1_offsets, 440, 220, byColor_56, 0, 200, 640);
        } else {
          if (iVideoState == 8)
            byColor_57 = 0xAB;
          else
            byColor_57 = 0x8F;
          scale_text(front_vga[15], &config_buffer[2688], font1_ascii, font1_offsets, 440, 220, byColor_57, 0, 200, 640);
        }
        if (iVideoState == 7)
          byColor_58 = 0xAB;
        else
          byColor_58 = 0x8F;
        scale_text(front_vga[15], &config_buffer[3456], font1_ascii, font1_offsets, 435, 240, byColor_58, 2u, 200, 640);
        if ((textures_off & TEX_OFF_ROAD_TEXTURES) != 0) {
          if (iVideoState == 7)
            byColor_59 = 0xAB;
          else
            byColor_59 = 0x8F;
          scale_text(front_vga[15], &config_buffer[2688], font1_ascii, font1_offsets, 440, 240, byColor_59, 0, 200, 640);
        } else {
          if (iVideoState == 7)
            byColor_60 = 0xAB;
          else
            byColor_60 = 0x8F;
          scale_text(front_vga[15], &config_buffer[2624], font1_ascii, font1_offsets, 440, 240, byColor_60, 0, 200, 640);
        }
        if (iVideoState == 6)
          byColor_61 = 0xAB;
        else
          byColor_61 = 0x8F;
        scale_text(front_vga[15], &config_buffer[3520], font1_ascii, font1_offsets, 435, 260, byColor_61, 2u, 200, 640);
        if ((textures_off & TEX_OFF_SHADOWS) != 0) {
          if (iVideoState == 6)
            byColor_62 = 0xAB;
          else
            byColor_62 = 0x8F;
          scale_text(front_vga[15], &config_buffer[2688], font1_ascii, font1_offsets, 440, 260, byColor_62, 0, 200, 640);
        } else {
          if (iVideoState == 6)
            byColor_63 = 0xAB;
          else
            byColor_63 = 0x8F;
          scale_text(front_vga[15], &config_buffer[2624], font1_ascii, font1_offsets, 440, 260, byColor_63, 0, 200, 640);
        }
        if (iVideoState == 5)
          byColor_64 = 0xAB;
        else
          byColor_64 = 0x8F;
        scale_text(front_vga[15], &config_buffer[3584], font1_ascii, font1_offsets, 435, 280, byColor_64, 2u, 200, 640);
        if ((textures_off & TEX_OFF_CLOUDS) != 0) {
          if (iVideoState == 5)
            byColor_65 = 0xAB;
          else
            byColor_65 = 0x8F;
          scale_text(front_vga[15], &config_buffer[2688], font1_ascii, font1_offsets, 440, 280, byColor_65, 0, 200, 640);
        } else {
          if (iVideoState == 5)
            byColor_66 = 0xAB;
          else
            byColor_66 = 0x8F;
          scale_text(front_vga[15], &config_buffer[2624], font1_ascii, font1_offsets, 440, 280, byColor_66, 0, 200, 640);
        }
        if (iVideoState == 4)
          byColor_67 = 0xAB;
        else
          byColor_67 = 0x8F;
        scale_text(front_vga[15], &config_buffer[3648], font1_ascii, font1_offsets, 435, 300, byColor_67, 2u, 200, 640);
        if ((textures_off & TEX_OFF_PANEL_OFF) != 0) {
          if (iVideoState == 4)
            byColor_68 = 0xAB;
          else
            byColor_68 = 0x8F;
          scale_text(front_vga[15], &config_buffer[2688], font1_ascii, font1_offsets, 440, 300, byColor_68, 0, 200, 640);
        } else if ((textures_off & TEX_OFF_PANEL_RESTRICTED) != 0) {
          if (iVideoState == 4)
            byColor_69 = 0xAB;
          else
            byColor_69 = 0x8F;
          scale_text(front_vga[15], &config_buffer[3776], font1_ascii, font1_offsets, 440, 300, byColor_69, 0, 200, 640);
        } else {
          if (iVideoState == 4)
            byColor_70 = 0xAB;
          else
            byColor_70 = 0x8F;
          scale_text(front_vga[15], &config_buffer[2624], font1_ascii, font1_offsets, 440, 300, byColor_70, 0, 200, 640);
        }
        if (iVideoState == 3)
          byColor_71 = 0xAB;
        else
          byColor_71 = 0x8F;
        scale_text(front_vga[15], &config_buffer[3712], font1_ascii, font1_offsets, 435, 320, byColor_71, 2u, 200, 640);
        if (view_limit) {
          if (iVideoState == 3)
            byColor_72 = 0xAB;
          else
            byColor_72 = 0x8F;
          scale_text(front_vga[15], &config_buffer[3776], font1_ascii, font1_offsets, 440, 320, byColor_72, 0, 200, 640);
        } else {
          if (iVideoState == 3)
            byColor_73 = 0xAB;
          else
            byColor_73 = 0x8F;
          scale_text(front_vga[15], &config_buffer[3840], font1_ascii, font1_offsets, 440, 320, byColor_73, 0, 200, 640);
        }
        if (iVideoState == 2)
          byColor_74 = 0xAB;
        else
          byColor_74 = 0x8F;
        scale_text(front_vga[15], &config_buffer[3904], font1_ascii, font1_offsets, 435, 340, byColor_74, 2u, 200, 640);
        if (game_svga)
          iReturnValue = (100 * game_size) >> 7;
          //iReturnValue = (100 * game_size) % 128;
          //iReturnValue = (100 * game_size - (__CFSHL__((100 * game_size) >> 31, 7) + ((100 * game_size) >> 31 << 7))) >> 7;
        else
          iReturnValue = (100 * game_size) >> 6;
          //iReturnValue = (100 * game_size) % 64;
          //iReturnValue = (100 * game_size - (__CFSHL__((100 * game_size) >> 31, 6) + ((100 * game_size) >> 31 << 6))) >> 6;
        sprintf(buffer, "%i %%", iReturnValue);
        if (iVideoState == 2)
          byColor_76 = 0xAB;
        else
          byColor_76 = 0x8F;
        scale_text(front_vga[15], buffer, font1_ascii, font1_offsets, 440, 340, byColor_76, 0, 200, 640);
        if (game_svga) {
          if (iVideoState == 1)
            byColor_75 = 0xAB;
          else
            byColor_75 = 0x8F;
          scale_text(front_vga[15], &config_buffer[512], font1_ascii, font1_offsets, 440, 360, byColor_75, 1u, 20, 640);
        } else {
          if (iVideoState == 1)
            byColor_77 = 0xAB;
          else
            byColor_77 = 0x8F;
          scale_text(front_vga[15], &config_buffer[448], font1_ascii, font1_offsets, 440, 360, byColor_77, 1u, 200, 640);
        }
        if (iVideoState)
          byColor_78 = 0x8F;
        else
          byColor_78 = 0xAB;
        scale_text(front_vga[15], &config_buffer[832], font1_ascii, font1_offsets, 430, 380, byColor_78, 2u, 200, 640);
        goto RENDER_FRAME;
      case 5:
        if (iConfigState != 6)
          iGraphicsState = -1;
        if (player_type == 2) {
          sprintf(buffer, "%s %s", &config_buffer[4480], &config_buffer[4864]);
          if (iGraphicsState == 6)
            byColor_79 = 0xAB;
          else
            byColor_79 = 0x8F;
          scale_text(front_vga[15], buffer, font1_ascii, font1_offsets, 435, 78, byColor_79, 2u, 200, 640);
          if (iGraphicsState == 6)
            byColor_80 = 0xAB;
          else
            byColor_80 = 0x8F;
          scale_text(front_vga[15], &config_buffer[64 * game_view[1] + 4928], font1_ascii, font1_offsets, 440, 78, byColor_80, 0, 200, 640);
        }
        sprintf(buffer, "%s %s", &config_buffer[4416], &config_buffer[4864]);
        if (iGraphicsState == 5)
          byColor_81 = 0xAB;
        else
          byColor_81 = 0x8F;
        scale_text(front_vga[15], buffer, font1_ascii, font1_offsets, 435, 96, byColor_81, 2u, 200, 640);
        if (iGraphicsState == 5)
          byColor_82 = 0xAB;
        else
          byColor_82 = 0x8F;
        scale_text(front_vga[15], &config_buffer[64 * game_view[0] + 4928], font1_ascii, font1_offsets, 440, 96, byColor_82, 0, 200, 640);
        if (iGraphicsState == 4)
          byColor_83 = 0xAB;
        else
          byColor_83 = 0x8F;
        scale_text(front_vga[15], &config_buffer[5440], font1_ascii, font1_offsets, 435, 114, byColor_83, 2u, 200, 640);
        if ((textures_off & TEX_OFF_KMH) != 0) {
          if (iGraphicsState == 4)
            byColor_84 = 0xAB;
          else
            byColor_84 = 0x8F;
          scale_text(front_vga[15], "KMH", font1_ascii, font1_offsets, 440, 114, byColor_84, 0, 200, 640);
        } else {
          if (iGraphicsState == 4)
            byColor_85 = 0xAB;
          else
            byColor_85 = 0x8F;
          scale_text(front_vga[15], "MPH", font1_ascii, font1_offsets, 440, 114, byColor_85, 0, 200, 640);
        }
        if (iGraphicsState == 3)
          byColor_86 = 0xAB;
        else
          byColor_86 = 0x8F;
        scale_text(front_vga[15], &config_buffer[5504], font1_ascii, font1_offsets, 435, 132, byColor_86, 2u, 200, 640);
        if (replay_record) {
          if (iGraphicsState == 3)
            byColor_87 = 0xAB;
          else
            byColor_87 = 0x8F;
          scale_text(front_vga[15], &config_buffer[2624], font1_ascii, font1_offsets, 440, 132, byColor_87, 0, 200, 640);
        } else {
          if (iGraphicsState == 3)
            byColor_88 = 0xAB;
          else
            byColor_88 = 0x8F;
          scale_text(front_vga[15], &config_buffer[2688], font1_ascii, font1_offsets, 440, 132, byColor_88, 0, 200, 640);
        }
        if (iGraphicsState == 2)
          byColor_89 = 0xAB;
        else
          byColor_89 = 0x8F;
        scale_text(front_vga[15], &config_buffer[4672], font1_ascii, font1_offsets, 435, 150, byColor_89, 2u, 200, 640);
        if (p_tex_size == 1) {
          if (iGraphicsState == 2)
            byColor_90 = 0xAB;
          else
            byColor_90 = 0x8F;
          scale_text(front_vga[15], &config_buffer[4736], font1_ascii, font1_offsets, 440, 150, byColor_90, 0, 200, 640);
        } else {
          if (iGraphicsState == 2)
            byColor_91 = 0xAB;
          else
            byColor_91 = 0x8F;
          scale_text(front_vga[15], &config_buffer[4800], font1_ascii, font1_offsets, 440, 150, byColor_91, 0, 200, 640);
        }
        if (iGraphicsState == 1)
          byColor_92 = 0xAB;
        else
          byColor_92 = 0x8F;
        scale_text(front_vga[15], &config_buffer[5888], font1_ascii, font1_offsets, 435, 168, byColor_92, 2u, 200, 640);
        if (false_starts) {
          if (iGraphicsState == 1)
            byColor_93 = 0xAB;
          else
            byColor_93 = 0x8F;
          scale_text(front_vga[15], &config_buffer[2624], font1_ascii, font1_offsets, 440, 168, byColor_93, 0, 200, 640);
        } else {
          if (iGraphicsState == 1)
            byColor_94 = 0xAB;
          else
            byColor_94 = 0x8F;
          scale_text(front_vga[15], &config_buffer[2688], font1_ascii, font1_offsets, 440, 168, byColor_94, 0, 200, 640);
        }
        if (iGraphicsState)
          byColor_95 = 0x8F;
        else
          byColor_95 = 0xAB;
        scale_text(front_vga[15], &config_buffer[832], font1_ascii, font1_offsets, 430, 186, byColor_95, 2u, 200, 640);
        goto RENDER_FRAME;
      case 6:
        if (iEditingName == 1) {
          byColor_96 = 0xAB;
          byColor_97 = 0xA5;
        } else {
          byColor_96 = 0xA5;
          byColor_97 = 0xAB;
        }
        if (iConfigState != 7) {
          byColor_96 = 0x8F;
          byColor_97 = 0x8F;
        }
        if (iEditingName == 1) {
          iTextPosX = 0;
          for (szMemPtr = network_messages[4 - iNetworkState]; *szMemPtr; ++szMemPtr) {
            iFontChar = (uint8)font1_ascii[(uint8)*szMemPtr];
            if (iFontChar == 255)
              iTextPosX += 8;
            else
              iTextPosX += front_vga[15][iFontChar].iWidth + 1;
          }
          iTextPosX += 390;
          iY = 140 - 18 * iNetworkState;
        }
        if (iNetworkState == 5)
          byColor_106 = byColor_97;
        else
          byColor_106 = 0x8F;
        front_text(front_vga[15], &language_buffer[7296], font1_ascii, font1_offsets, 390, 50, byColor_106, 1u);
        if (iNetworkState == 4)
          byColor_98 = byColor_97;
        else
          byColor_98 = 0x8F;
        front_text(front_vga[15], &config_buffer[5632], font1_ascii, font1_offsets, 385, 68, byColor_98, 2u);
        if (iNetworkState == 4)
          byColor_99 = byColor_96;
        else
          byColor_99 = 0x8F;
        scale_text(front_vga[15], network_messages[0], font1_ascii, font1_offsets, 390, 68, byColor_99, 0, 200, 630);
        if (iNetworkState == 3)
          byColor_100 = byColor_97;
        else
          byColor_100 = 0x8F;
        front_text(front_vga[15], &config_buffer[5696], font1_ascii, font1_offsets, 385, 86, byColor_100, 2u);
        if (iNetworkState == 3)
          byColor_101 = byColor_96;
        else
          byColor_101 = 0x8F;
        scale_text(front_vga[15], network_messages[1], font1_ascii, font1_offsets, 390, 86, byColor_101, 0, 200, 630);
        if (iNetworkState == 2)
          byColor_102 = byColor_97;
        else
          byColor_102 = 0x8F;
        front_text(front_vga[15], &config_buffer[5760], font1_ascii, font1_offsets, 385, 104, byColor_102, 2u);
        if (iNetworkState == 2)
          byColor_103 = byColor_96;
        else
          byColor_103 = 0x8F;
        scale_text(front_vga[15], network_messages[2], font1_ascii, font1_offsets, 390, 104, byColor_103, 0, 200, 630);
        if (iNetworkState == 1)
          byColor_104 = byColor_97;
        else
          byColor_104 = 0x8F;
        front_text(front_vga[15], &config_buffer[5824], font1_ascii, font1_offsets, 385, 122, byColor_104, 2u);
        if (iNetworkState != 1)
          byColor_96 = 0x8F;
        byTempChar2 = byColor_96;
        iNetworkState_1 = iNetworkState;
        scale_text(front_vga[15], network_messages[3], font1_ascii, font1_offsets, 390, 122, byTempChar2, 0, 200, 630);
        if (iNetworkState_1)
          byColor_97 = 0x8F;
        front_text(front_vga[15], &config_buffer[832], font1_ascii, font1_offsets, 390, 140, byColor_97, 1u);
        if (iEditingName == 1 && (frames & 0xFu) < 8) {
          iX = stringwidth(network_messages[4 - iNetworkState]) + 390;
          if (iX <= 620)
            front_text(front_vga[15], "_", font1_ascii, font1_offsets, iX, iY, 0xABu, 0);
          else
            front_text(front_vga[15], "_", font1_ascii, font1_offsets, 621, iY, 0xABu, 0);
        }
        goto RENDER_FRAME;
      default:
        goto RENDER_FRAME;
    }
  }
}

//-------------------------------------------------------------------------------------------------
//00046EA0
void front_displaycalibrationbar(int iY, int iX, int iValue)
{
  int iClampedValue; // edi
  uint8 *pScreenPos; // ecx
  int iIndicatorPos; // edi
  int i; // esi
  int iScreenWidth; // ebp

  iClampedValue = iValue;
  if (iValue < -100)
    iClampedValue = -100;
  if (iClampedValue > 100)
    iClampedValue = 100;
  pScreenPos = &scrbuf[iY + winw * iX];
  if (current_mode) {
    iIndicatorPos = iClampedValue + 103;
    for (i = 0; i < 17; ++i) {
      if (i && i != 16) {
        *pScreenPos = 0x8F;
        pScreenPos[206] = 0x8F;
        pScreenPos[iIndicatorPos] = 0xAB;
        pScreenPos[iIndicatorPos - 1] = 0xAB;
        pScreenPos[iIndicatorPos - 2] = 0xAB;
        pScreenPos[iIndicatorPos + 1] = 0xAB;
        pScreenPos[iIndicatorPos + 2] = 0xAB;
        pScreenPos[103] = 0xE7;
        pScreenPos[104] = 0xE7;
        iScreenWidth = winw;
        pScreenPos[102] = 0xE7;
        pScreenPos += iScreenWidth;
      } else {
        memset(pScreenPos, 0x8F, 0xCEu);
        pScreenPos += winw;
      }
    }
  }
}

//-------------------------------------------------------------------------------------------------
//00046F40
void front_volumebar(int iY, int iVolumeLevel, int iFillColor)
{
  uint8 *pbyScreenPos; // ecx
  int iRow; // esi
  int iScreenWidth; // eax

  pbyScreenPos = &scrbuf[winw * iY + 430];      // Calculate starting position in screen buffer (430 pixels from left edge)
  for (iRow = 0; iRow < 17; ++iRow)           // Draw 17 rows for the volume bar
  {                                             // Skip first and last row (draw border on those)
    if (iRow && iRow != 16) {
      *pbyScreenPos = 0x8F;                     // Draw left border (0x8F is white in PALETTE.PAL)
      memset(pbyScreenPos + 1, iFillColor, 160 * iVolumeLevel / 127);// Fill volume bar based on level (160 * volume / 127 pixels wide)
      iScreenWidth = winw;
      pbyScreenPos[161] = 0x8F;                 // Draw right border  (0x8F is white in PALETTE.PAL)
      pbyScreenPos += iScreenWidth;
    } else {
      memset(pbyScreenPos, 0x8F, 0xA2u);        // Draw top/bottom border - fill entire width (162 pixels) with border color
      pbyScreenPos += winw;
    }
  }
}

//-------------------------------------------------------------------------------------------------
//00047000
void select_players()
{
  unsigned int uiSelectedPlayerType; // esi
  int iNetworkStatus; // edi
  uint32 uiCheatArrayOffset; // eax
  int iCheatPlayerLoop; // edx
  int iCheatPlayerIndex; // edx
  int iPlayerCarIndex; // ecx
  char byMenuColor1; // al
  char byMenuColor2; // al
  char byMenuColor3; // al
  char byMenuColor4; // al
  char byMenuColor5; // al
  uint8 byInputKey; // al
  uint8 byExtendedKey; // al
  int iNetworkMode; // [esp+8h] [ebp-20h]
  int iPlayerIndex; // [esp+Ch] [ebp-1Ch]
  int iY; // [esp+10h] [ebp-18h]
  int iComPortStatus; // [esp+14h] [ebp-14h]
  char *szText; // [esp+18h] [ebp-10h]
  int iPlayerListCount; // [esp+1Ch] [ebp-Ch]
  int iNetworkSetupFlag; // [esp+20h] [ebp-8h]
  int iExitFlag; // [esp+24h] [ebp-4h]

  iComPortStatus = 0;// gss16550(2);                 // Initialize COM port status and screen fade
  iExitFlag = 0;
  fade_palette(0);
  uiSelectedPlayerType = player_type;
  front_fade = 0;
  if (player_type == 1 && net_type)           // Map network types to player selection modes: Serial=3, Modem=4
  {
    if ((unsigned int)net_type <= 1) {
      uiSelectedPlayerType = 3;
    } else if (net_type == 2) {
      uiSelectedPlayerType = 4;
    }
  }
  iNetworkSetupFlag = 0;
  if (uiSelectedPlayerType == 1 || uiSelectedPlayerType == 3 || uiSelectedPlayerType == 4)// Set network mode flag: -1 for single player and network modes, 0 for two-player
    iNetworkMode = -1;
  else
    iNetworkMode = 0;
  iNetworkStatus = 0;
  do {                                             // Handle game type switches (race type, championship, etc.)
    if (switch_types) {
      game_type = switch_types - 1;
      if (switch_types == 1 && competitors == 1)
        competitors = 16;
      switch_types = 0;
      if (game_type == 1)
        Race = ((uint8)TrackLoad - 1) & 7;
      else
        network_champ_on = 0;
    }
    display_picture(scrbuf, front_vga[0]);
    display_block(scrbuf, front_vga[1], 3, head_x, head_y, 0);
    display_block(scrbuf, front_vga[6], 0, 36, 2, 0);
    display_block(scrbuf, front_vga[5], uiSelectedPlayerType, -4, 247, 0);
    display_block(scrbuf, front_vga[5], game_type + 5, 135, 247, 0);
    display_block(scrbuf, front_vga[4], 4, 76, 257, -1);
    display_block(scrbuf, front_vga[6], 4, 62, 336, -1);
    if (iNetworkStatus && (uiSelectedPlayerType == 1 || uiSelectedPlayerType == 3 || uiSelectedPlayerType == 4))// Show connection status message for network modes
      scale_text(front_vga[15], &language_buffer[4992], font1_ascii, font1_offsets, 400, 300, 231, 1u, 200, 640);
    if ((uiSelectedPlayerType == 3 || uiSelectedPlayerType == 4) && !iComPortStatus)// Show COM port error message if networking failed to initialize
      scale_text(front_vga[15], &language_buffer[8064], font1_ascii, font1_offsets, 400, 300, 231, 1u, 200, 640);
    do {
      uiCheatArrayOffset = broadcast_mode;
      UpdateSDL();
    } while (broadcast_mode);
    if (switch_same > 0)                      // CHEAT MODE HANDLING: Process switch_same command for car synchronization
    {
      for (iCheatPlayerIndex = 0; iCheatPlayerIndex < players; ++iCheatPlayerIndex)
      {
        Players_Cars[iCheatPlayerIndex] = switch_same - 666;
      }
      //for (iCheatPlayerIndex = 0;
      //      iCheatPlayerIndex < players;
      //      *(int *)((char *)&infinite_laps + uiCheatArrayOffset) = switch_same - 666) {
      //  uiCheatArrayOffset += 4;
      //  ++iCheatPlayerIndex;
      //}
      if ((cheat_mode & 0x4000) == 0)
        broadcast_mode = -1;
      while (broadcast_mode)
        UpdateSDL();
      cheat_mode |= CHEAT_MODE_CLONES;
    } else if (switch_same < 0) {
      switch_same = broadcast_mode;
      for (iCheatPlayerLoop = 0; iCheatPlayerLoop < players; ++iCheatPlayerLoop)
      {
        Players_Cars[iCheatPlayerLoop] = -1;
      }
      //for (iCheatPlayerLoop = 0; iCheatPlayerLoop < players; *(int *)((char *)&infinite_laps + uiCheatArrayOffset) = -1) {
      //  uiCheatArrayOffset += 4;
      //  ++iCheatPlayerLoop;
      //}
      cheat_mode &= ~CHEAT_MODE_CLONES;
    }
    if (iNetworkMode)                         // NETWORK MODE UI: Show connection info and player list
    {
      if (iNetworkSetupFlag) {
        while (broadcast_mode)
          UpdateSDL();
        broadcast_mode = -667;
        while (broadcast_mode)
          UpdateSDL();
        iNetworkSetupFlag = 0;
      }
      if (net_type) {
        if ((unsigned int)net_type <= 1) {
          scale_text(front_vga[15], &language_buffer[5056], font1_ascii, font1_offsets, 400, 60, 143, 1u, 200, 640);
        } else if (net_type == 2) {
          scale_text(front_vga[15], &language_buffer[5120], font1_ascii, font1_offsets, 400, 60, 143, 1u, 200, 640);
        }
      } else {
        scale_text(front_vga[15], &language_buffer[4096], font1_ascii, font1_offsets, 400, 60, 143, 1u, 200, 640);
      }
      iPlayerListCount = 0;
      if (network_on > 0)                     // Display connected players and their selected cars
      {
        iPlayerIndex = 0;
        iY = 80;
        szText = player_names[0];
        do {
          scale_text(front_vga[15], szText, font1_ascii, font1_offsets, 336, iY, 143, 2u, 200, 640);
          iPlayerCarIndex = Players_Cars[iPlayerIndex];
          if (iPlayerCarIndex < 0)
            scale_text(front_vga[15], &language_buffer[4160], font1_ascii, font1_offsets, 340, iY, 131, 0, 200, 640);
          else
            scale_text(front_vga[15], CompanyNames[iPlayerCarIndex], font1_ascii, font1_offsets, 342, iY, 143, 0, 200, 640);
          ++iPlayerIndex;
          szText += 9;
          iY += 18;
          ++iPlayerListCount;

          UpdateSDL();
        } while (iPlayerListCount < network_on);
      }
      if (net_type) {
        if ((unsigned int)net_type <= 1) {
          scale_text(front_vga[15], &language_buffer[5184], font1_ascii, font1_offsets, 400, 380, 231, 1u, 200, 640);
        } else if (net_type == 2) {
          scale_text(front_vga[15], &language_buffer[5248], font1_ascii, font1_offsets, 400, 380, 231, 1u, 200, 640);
        }
      } else {
        scale_text(front_vga[15], &language_buffer[4224], font1_ascii, font1_offsets, 400, 380, 231, 1u, 200, 640);
      }
      scale_text(front_vga[15], &language_buffer[7104], font1_ascii, font1_offsets, 400, 360, 231, 1u, 200, 640);
    } else {
      scale_text(front_vga[15], &language_buffer[2944], font1_ascii, font1_offsets, 400, 75, 143, 1u, 200, 640);// MENU MODE UI: Show player selection options with highlighting
      scale_text(front_vga[15], &language_buffer[3008], font1_ascii, font1_offsets, 400, 93, 143, 1u, 200, 640);
      if (uiSelectedPlayerType)               // Highlight current selection
        byMenuColor1 = 0x8F;
      else
        byMenuColor1 = 0xAB;
      scale_text(front_vga[15], &language_buffer[2112], font1_ascii, font1_offsets, 400, 135, byMenuColor1, 1u, 200, 640);
      if (uiSelectedPlayerType == 2)
        byMenuColor2 = 0xAB;
      else
        byMenuColor2 = 0x8F;
      scale_text(front_vga[15], &language_buffer[2240], font1_ascii, font1_offsets, 400, 153, byMenuColor2, 1u, 200, 640);
      if (uiSelectedPlayerType == 1)
        byMenuColor3 = 0xAB;
      else
        byMenuColor3 = 0x8F;
      scale_text(front_vga[15], &language_buffer[2176], font1_ascii, font1_offsets, 400, 171, byMenuColor3, 1u, 200, 640);
      if (uiSelectedPlayerType == 3)
        byMenuColor4 = 0xAB;
      else
        byMenuColor4 = 0x8F;
      scale_text(front_vga[15], &language_buffer[2304], font1_ascii, font1_offsets, 400, 189, byMenuColor4, 1u, 200, 640);
      if (uiSelectedPlayerType == 4)
        byMenuColor5 = 0xAB;
      else
        byMenuColor5 = 0x8F;
      scale_text(front_vga[15], &language_buffer[2368], font1_ascii, font1_offsets, 400, 207, byMenuColor5, 1u, 200, 640);
    }
    show_received_mesage();
    copypic(scrbuf, screen);
    if (!front_fade)                          // Handle screen fade-in effect
    {
      front_fade = -1;
      fade_palette(32);
      frames = 0;
    }
    while (fatkbhit())                        // KEYBOARD INPUT PROCESSING: Handle navigation and selection
    {
      byInputKey = fatgetch();
      if (byInputKey < 0x4Du) {
        if (byInputKey < 0xDu) {                                       // Handle extended keys (arrow keys for navigation)
          if (!byInputKey) {
            byExtendedKey = fatgetch();
            if (byExtendedKey >= 0x48u) {
              if (byExtendedKey <= 0x48u) {                                 // Up arrow: Navigate through player selection options
                if (!iNetworkMode) {
                  switch (uiSelectedPlayerType) {
                    case 1u:
                      uiSelectedPlayerType = 2;
                      iNetworkStatus = 0;
                      break;
                    case 2u:
                      uiSelectedPlayerType = 0;
                      iNetworkStatus = 0;
                      break;
                    case 3u:
                      uiSelectedPlayerType = 1;
                      iNetworkStatus = 0;
                      break;
                    case 4u:
                      uiSelectedPlayerType = 3;
                      iNetworkStatus = 0;
                      break;
                    default:
                      continue;
                  }
                }
              } else if (byExtendedKey == 80 && !iNetworkMode)// Down arrow: Navigate through player selection options
              {
                switch (uiSelectedPlayerType) {
                  case 0u:
                    uiSelectedPlayerType = 2;
                    iNetworkStatus = 0;
                    break;
                  case 1u:
                    uiSelectedPlayerType = 3;
                    iNetworkStatus = 0;
                    break;
                  case 2u:
                    uiSelectedPlayerType = 1;
                    iNetworkStatus = 0;
                    break;
                  case 3u:
                    uiSelectedPlayerType = 4;
                    iNetworkStatus = 0;
                    break;
                  default:
                    continue;
                }
              }
            }
          }
        } else if (byInputKey <= 0xDu || byInputKey == 27)// Enter/Escape: Confirm selection or exit menu
        {
          switch (uiSelectedPlayerType) {
            case 0u:
            case 2u:
              goto LABEL_128;
            case 1u:
            case 3u:
            case 4u:
              if (uiSelectedPlayerType != 1 && !iComPortStatus)
                continue;
              if (uiSelectedPlayerType == 1)
                net_type = 0;
              if (uiSelectedPlayerType == 3)
                net_type = 1;
              if (uiSelectedPlayerType == 4)
                net_type = 2;
              //gssCommsSetType(net_type);
              if (iNetworkMode) {
              LABEL_128:
                iExitFlag = -1;
                continue;
              }
              if (iNetworkStatus)
                goto LABEL_159;
              if (net_type)                   // NETWORK SETUP: Initialize communication for selected network type
              {
                if ((unsigned int)net_type <= 1) {
                  if (select_comport(uiSelectedPlayerType))
                    goto LABEL_153;
                } else {
                  if (net_type != 2)
                    goto LABEL_156;
                  if (select_modemstuff(uiSelectedPlayerType)) {
                  LABEL_153:
                    network_slot = 0;
                    goto LABEL_156;
                  }
                }
                network_on = 0;
                network_slot = -1;
              } else {
                network_slot = select_netslot();// IPX network: Select network slot and handle connection
                if (network_slot >= 0) {
                  broadcast_mode = -1;
                  while (broadcast_mode)
                    UpdateSDL();
                } else if (network_slot == -2) {
                  broadcast_mode = -666;
                  while (broadcast_mode)
                    UpdateSDL();
                  close_network();
                } else {
                  iNetworkStatus = -1;
                }
              }
            LABEL_156:
              if (uiSelectedPlayerType == 3 && network_slot >= 0)
                Initialise_Network(0);
            LABEL_159:
              if (network_on) {
                iNetworkSetupFlag = -1;
                iNetworkMode = -1;
              }
              break;
            default:
              continue;
          }
        }
      } else if (byInputKey <= 0x4Du) {
      LABEL_119:
        if (network_on)
          select_messages();
      } else if (byInputKey < 0x6Du)            // M/m keys: Open message selection (network mode only)
      {
        if (byInputKey == 81)
          goto LABEL_121;
      } else {
        if (byInputKey <= 0x6Du)
          goto LABEL_119;
        if (byInputKey == 113) {                                       // Q/q keys: Quit network and return to player selection
        LABEL_121:
          if (network_on) {
            broadcast_mode = -666;
            while (broadcast_mode)
              UpdateSDL();
            frames = 0;
            while (frames < 3)
              ;
            close_network();
            iNetworkMode = 0;
            uiSelectedPlayerType = 0;
          }
        }
      }
      UpdateSDL();
    }
    UpdateSDL();
  } while (!iExitFlag);                         // MAIN SELECTION LOOP - Handle UI rendering and input processing
  if (uiSelectedPlayerType < 3)               // CLEANUP: Set final player type and network settings based on selection
  {
    if (uiSelectedPlayerType != 1)
      goto LABEL_169;
    player_type = 1;
    net_type = 0;
  } else if (uiSelectedPlayerType <= 3) {
    player_type = 1;
    net_type = 1;
  } else if (uiSelectedPlayerType == 4) {
    player_type = 1;
    net_type = 2;
  } else {
  LABEL_169:
    player_type = uiSelectedPlayerType;
  }
  fade_palette(0);
  front_fade = 0;
}

//-------------------------------------------------------------------------------------------------
//00047AE0
void select_type()
{
  int iMenuSelection; // edi
  int iCurrentOption; // esi
  char *pszTextBuffer; // edx
  char byGameModeColor1; // al
  char byGameModeColor2; // al
  char byGameModeColor3; // al
  char byDifficultyColor1; // al
  char byDifficultyColor2; // al
  char byDifficultyColor3; // al
  char byDifficultyColor4; // al
  char byDifficultyColor5; // al
  char byDifficultyColor6; // al
  char byCompetitorColor1; // al
  char byCompetitorColor2; // al
  char byCompetitorColor3; // al
  char byDamageColor1; // al
  char byDamageColor2; // al
  char byDamageColor3; // al
  char byTextureColor1; // al
  char byTextureColor2; // al
  uint32 uiUpdatedCheatFlags; // eax
  int iCheatSetLoop; // eax
  uint8 byInputKey; // al
  uint8 byExtendedKey; // al
  int iCupIncrement; // edx
  int iTrackUpperLimit; // edx
  int iTrackLowerLimit; // ebx
  int iTextYPosition; // [esp-14h] [ebp-48h]
  char byFinalTextColor; // [esp-10h] [ebp-44h]
  int iCheatModesAvailable; // [esp+0h] [ebp-34h]
  int iExitFlag; // [esp+4h] [ebp-30h]
  int iSkipColor; // [esp+8h] [ebp-2Ch]
  char byCompetitorMenuColor; // [esp+Ch] [ebp-28h]
  char byTextColor; // [esp+10h] [ebp-24h]
  char byDamageMenuColor; // [esp+14h] [ebp-20h]
  char byTextureMenuColor; // [esp+18h] [ebp-1Ch]
  char byDifficultyMenuColor; // [esp+1Ch] [ebp-18h]
  int iNetworkDisplayY; // [esp+20h] [ebp-14h]
  int iY; // [esp+24h] [ebp-10h]
  char *szText; // [esp+28h] [ebp-Ch]
  int iNetworkPlayerCount; // [esp+2Ch] [ebp-8h]
  int iBlockIdx; // [esp+30h] [ebp-4h]

  iExitFlag = 0;                                // Initialize exit flag and check game restrictions
  if (game_type == 1 && Race > 0)             // Championship mode after first race - restrict options to track selection only
    iSkipColor = -1;
  else
    iSkipColor = 0;
  if ((cheat_mode & TEX_OFF_SHADOWS) != 0 || (textures_off & TEX_OFF_CAR_SET_AVAILABLE) != 0)
    iCheatModesAvailable = -1;
  else
    iCheatModesAvailable = 0;
  iBlockIdx = (TrackLoad - 1) / 8;// Calculate cup index from current track (TrackLoad): cups are groups of 8 tracks
  front_vga[14] = (tBlockHeader *)load_picture("cupicons.bm");  // Load cup icons graphics and initialize screen
  iMenuSelection = 0;
  fade_palette(0);
  front_fade = 0;
  iCurrentOption = 5;
  do {                                             // Handle game type switches and updates
    if (switch_types) {
      game_type = switch_types - 1;
      if (switch_types == 1 && competitors == 1)
        competitors = 16;
      switch_types = 0;
      if (game_type == 1)
        Race = ((uint8)TrackLoad - 1) & 7;
      else
        network_champ_on = 0;
    }
    display_picture(scrbuf, front_vga[0]);      // RENDER FRAME: Draw background, UI elements, and cup winner icon
    display_block(scrbuf, front_vga[1], 4, head_x, head_y, 0);
    display_block(scrbuf, front_vga[6], 0, 36, 2, 0);
    display_block(scrbuf, front_vga[5], player_type, -4, 247, 0);
    display_block(scrbuf, front_vga[5], game_type + 5, 135, 247, 0);
    display_block(scrbuf, front_vga[4], 4, 76, 257, -1);
    if (cup_won && !iSkipColor)
      display_block(scrbuf, front_vga[1], 8, 200, 56, 0);
    if (iCurrentOption >= 5)                  // Draw selection cursor or \"Random\" indicator (option 5 = Random)
    {
      display_block(scrbuf, front_vga[6], 4, 62, 336, -1);
    } else {
      display_block(scrbuf, front_vga[6], 2, 62, 336, -1);
      front_text(front_vga[2], "~", font2_ascii, font2_offsets, sel_posns[iCurrentOption].x, sel_posns[iCurrentOption].y, 0x8Fu, 0);
    }
    if (iSkipColor)                           // OPTION LABELS: Display main option categories (Game Type/Difficulty/etc.)
    {
      front_text(front_vga[2], &language_buffer[3136], font2_ascii, font2_offsets, sel_posns[0].x + 132, sel_posns[0].y + 7, 0x8Fu, 2u);
    } else {
      front_text(front_vga[2], &language_buffer[384], font2_ascii, font2_offsets, sel_posns[0].x + 132, sel_posns[0].y + 7, 0x8Fu, 2u);
      front_text(front_vga[2], &language_buffer[3200], font2_ascii, font2_offsets, sel_posns[1].x + 132, sel_posns[1].y + 7, 0x8Fu, 2u);
      front_text(front_vga[2], &language_buffer[3264], font2_ascii, font2_offsets, sel_posns[2].x + 132, sel_posns[2].y + 7, 0x8Fu, 2u);
      front_text(front_vga[2], &language_buffer[3328], font2_ascii, font2_offsets, sel_posns[3].x + 132, sel_posns[3].y + 7, 0x8Fu, 2u);
      if (iCheatModesAvailable)
        front_text(front_vga[2], &language_buffer[4288], font2_ascii, font2_offsets, sel_posns[4].x + 132, sel_posns[4].y + 7, 0x8Fu, 2u);
    }
    display_block(scrbuf, front_vga[14], iBlockIdx, 500, 300, 0);// Display cup icon corresponding to current track selection
    if (iSkipColor)                           // CHAMPIONSHIP MODE UI: Show current settings and restrictions
    {
      scale_text(front_vga[15], &language_buffer[3392], font1_ascii, font1_offsets, 400, 75, 143, 1u, 200, 640);
      scale_text(front_vga[15], &language_buffer[1280], font1_ascii, font1_offsets, 400, 100, 143, 2u, 200, 640);
      if ((cheat_mode & 2) != 0)
        scale_text(front_vga[15], &language_buffer[2048], font1_ascii, font1_offsets, 405, 100, 143, 0, 200, 640);
      else
        scale_text(front_vga[15], &language_buffer[64 * level + 1472], font1_ascii, font1_offsets, 405, 100, 143, 0, 200, 640);
      scale_text(front_vga[15], &language_buffer[1344], font1_ascii, font1_offsets, 400, 118, 143, 2u, 200, 640);
      if ((cheat_mode & 2) != 0)
        scale_text(front_vga[15], &language_buffer[2048], font1_ascii, font1_offsets, 405, 118, 143, 0, 200, 640);
      else
        scale_text(front_vga[15], &language_buffer[64 * damage_level + 1856], font1_ascii, font1_offsets, 405, 118, 143, 0, 200, 640);
      scale_text(front_vga[15], &language_buffer[1024], font1_ascii, font1_offsets, 400, 136, 143, 2u, 200, 640);
      if ((unsigned int)competitors < 8) {
        if (competitors == 2)
          scale_text(front_vga[15], &language_buffer[1088], font1_ascii, font1_offsets, 405, 136, 143, 0, 200, 640);
      } else if ((unsigned int)competitors <= 8) {
        scale_text(front_vga[15], &language_buffer[1152], font1_ascii, font1_offsets, 405, 136, 143, 0, 200, 640);
      } else if (competitors == 16) {
        scale_text(front_vga[15], &language_buffer[1216], font1_ascii, font1_offsets, 405, 136, 143, 0, 200, 640);
      }
      if (network_on)                         // NETWORK MODE: Display connected players list
      {
        scale_text(front_vga[15], &language_buffer[4672], font1_ascii, font1_offsets, 400, 154, 143, 1u, 200, 640);
        iNetworkPlayerCount = 0;
        if (players > 0) {
          szText = player_names[0];
          iY = 28;
          iNetworkDisplayY = 172;
          do {
            if (iNetworkPlayerCount >= 8)
              scale_text(front_vga[15], szText, font1_ascii, font1_offsets, 405, iY, 143, 0, 200, 640);
            else
              scale_text(front_vga[15], szText, font1_ascii, font1_offsets, 400, iNetworkDisplayY, 143, 2u, 200, 640);
            szText += 9;
            iY += 18;
            iNetworkDisplayY += 18;
            ++iNetworkPlayerCount;

            UpdateSDL();
          } while (iNetworkPlayerCount < players);
        }
      }
    }
    switch (iCurrentOption) {
      case 0:
        if (!iSkipColor)                      // Option 0 - Game Type: Show race modes with highlighting for current selection
        {
          scale_text(front_vga[15], &language_buffer[384], font1_ascii, font1_offsets, 400, 75, 143, 1u, 200, 640);
          if (iMenuSelection == 1) {
            scale_text(front_vga[15], &language_buffer[3584], font1_ascii, font1_offsets, 400, 93, 143, 1u, 200, 640);
            byTextColor = -85;
          } else {
            byTextColor = -87;
          }
          if (game_type)
            byGameModeColor1 = -113;
          else
            byGameModeColor1 = byTextColor;
          scale_text(front_vga[15], &language_buffer[3648], font1_ascii, font1_offsets, 400, 135, byGameModeColor1, 1u, 200, 640);
          if (game_type == 1)
            byGameModeColor2 = byTextColor;
          else
            byGameModeColor2 = -113;
          scale_text(front_vga[15], &language_buffer[3520], font1_ascii, font1_offsets, 400, 153, byGameModeColor2, 1u, 200, 640);
          if (game_type == 2)
            byGameModeColor3 = byTextColor;
          else
            byGameModeColor3 = -113;
          byFinalTextColor = byGameModeColor3;
          iTextYPosition = 171;
          pszTextBuffer = &language_buffer[3712];
          goto LABEL_130;
        }
        if (iMenuSelection == 6) {
          scale_text(front_vga[15], &language_buffer[3456], font1_ascii, font1_offsets, 400, 320, 231, 1u, 200, 640);
          byFinalTextColor = -25;
          iTextYPosition = 338;
          pszTextBuffer = &language_buffer[3520];
          goto LABEL_130;
        }
        break;
      case 1:
        scale_text(front_vga[15], &language_buffer[3776], font1_ascii, font1_offsets, 400, 75, 143, 1u, 200, 640);// Option 1 - Difficulty: Show skill levels with cheat mode override
        if (iMenuSelection == 2) {
          scale_text(front_vga[15], &language_buffer[3840], font1_ascii, font1_offsets, 400, 93, 143, 1u, 200, 640);
          byDifficultyMenuColor = -85;
        } else {
          byDifficultyMenuColor = -87;
        }
        if ((cheat_mode & 2) == 0) {
          if (level == 5)
            byDifficultyColor1 = byDifficultyMenuColor;
          else
            byDifficultyColor1 = -113;
          scale_text(front_vga[15], &language_buffer[1792], font1_ascii, font1_offsets, 400, 135, byDifficultyColor1, 1u, 200, 640);
          if (level == 4)
            byDifficultyColor2 = byDifficultyMenuColor;
          else
            byDifficultyColor2 = -113;
          scale_text(front_vga[15], &language_buffer[1728], font1_ascii, font1_offsets, 400, 153, byDifficultyColor2, 1u, 200, 640);
          if (level == 3)
            byDifficultyColor3 = byDifficultyMenuColor;
          else
            byDifficultyColor3 = -113;
          scale_text(front_vga[15], &language_buffer[1664], font1_ascii, font1_offsets, 400, 171, byDifficultyColor3, 1u, 200, 640);
          if (level == 2)
            byDifficultyColor4 = byDifficultyMenuColor;
          else
            byDifficultyColor4 = -113;
          scale_text(front_vga[15], &language_buffer[1600], font1_ascii, font1_offsets, 400, 189, byDifficultyColor4, 1u, 200, 640);
          if (level == 1)
            byDifficultyColor5 = byDifficultyMenuColor;
          else
            byDifficultyColor5 = -113;
          scale_text(front_vga[15], &language_buffer[1536], font1_ascii, font1_offsets, 400, 207, byDifficultyColor5, 1u, 200, 640);
          if (level)
            byDifficultyColor6 = -113;
          else
            byDifficultyColor6 = byDifficultyMenuColor;
          byFinalTextColor = byDifficultyColor6;
          iTextYPosition = 225;
          pszTextBuffer = &language_buffer[1472];
          goto LABEL_130;
        }
        scale_text(front_vga[15], &language_buffer[2048], font1_ascii, font1_offsets, 400, 135, byDifficultyMenuColor, 1u, 200, 640);
        break;
      case 2:
        scale_text(front_vga[15], &language_buffer[3904], font1_ascii, font1_offsets, 400, 75, 143, 1u, 200, 640);// Option 2 - Competitors: Show number of opponent cars (2/8/16 or \"Just Me\")
        if (iMenuSelection == 3) {
          scale_text(front_vga[15], &language_buffer[3008], font1_ascii, font1_offsets, 400, 93, 143, 1u, 200, 640);
          byDamageMenuColor = -85;
        } else {
          byDamageMenuColor = -87;
        }
        if (competitors != 1) {
          if (competitors == 2)
            byCompetitorColor1 = byDamageMenuColor;
          else
            byCompetitorColor1 = -113;
          scale_text(front_vga[15], &language_buffer[1088], font1_ascii, font1_offsets, 400, 135, byCompetitorColor1, 1u, 200, 640);
          if (competitors == 8)
            byCompetitorColor2 = byDamageMenuColor;
          else
            byCompetitorColor2 = -113;
          scale_text(front_vga[15], &language_buffer[1152], font1_ascii, font1_offsets, 400, 153, byCompetitorColor2, 1u, 200, 640);
          if (competitors == 16)
            byCompetitorColor3 = byDamageMenuColor;
          else
            byCompetitorColor3 = -113;
          byFinalTextColor = byCompetitorColor3;
          iTextYPosition = 171;
          pszTextBuffer = &language_buffer[1216];
          goto LABEL_130;
        }
        scale_text(front_vga[15], &language_buffer[3968], font1_ascii, font1_offsets, 400, 135, byDamageMenuColor, 1u, 200, 640);
        break;
      case 3:
        scale_text(front_vga[15], &language_buffer[4032], font1_ascii, font1_offsets, 400, 75, 143, 1u, 200, 640); // Option 3 - Damage: Show car damage levels (None/Cosmetic/Realistic)
        if (iMenuSelection == 4) {
          scale_text(front_vga[15], &language_buffer[3840], font1_ascii, font1_offsets, 400, 93, 143, 1u, 200, 640);
          byTextureMenuColor = -85;
        } else {
          byTextureMenuColor = -87;
        }
        if ((cheat_mode & 2) == 0) {
          if (damage_level)
            byDamageColor1 = -113;
          else
            byDamageColor1 = byTextureMenuColor;
          scale_text(front_vga[15], &language_buffer[1856], font1_ascii, font1_offsets, 400, 135, byDamageColor1, 1u, 200, 640);
          if (damage_level == 1)
            byDamageColor2 = byTextureMenuColor;
          else
            byDamageColor2 = -113;
          scale_text(front_vga[15], &language_buffer[1920], font1_ascii, font1_offsets, 400, 153, byDamageColor2, 1u, 200, 640);
          if (damage_level == 2)
            byDamageColor3 = byTextureMenuColor;
          else
            byDamageColor3 = -113;
          byFinalTextColor = byDamageColor3;
          iTextYPosition = 171;
          pszTextBuffer = &language_buffer[1984];
          goto LABEL_130;
        }
        scale_text(front_vga[15], &language_buffer[2048], font1_ascii, font1_offsets, 400, 135, byTextureMenuColor, 1u, 200, 640);
        break;
      case 4:
        scale_text(front_vga[15], &language_buffer[4288], font1_ascii, font1_offsets, 400, 75, 143, 1u, 200, 640);// Option 4 - Textures: Show texture quality options (On/Off)
        if (iMenuSelection == 5) {
          scale_text(front_vga[15], &language_buffer[4480], font1_ascii, font1_offsets, 400, 93, 143, 1u, 200, 640);
          byCompetitorMenuColor = -85;
        } else {
          byCompetitorMenuColor = -87;
        }
        if ((textures_off & TEX_OFF_ADVANCED_CARS) != 0)
          byTextureColor1 = -113;
        else
          byTextureColor1 = byCompetitorMenuColor;
        scale_text(front_vga[15], &language_buffer[4352], font1_ascii, font1_offsets, 400, 135, byTextureColor1, 1u, 200, 640);
        if ((textures_off & TEX_OFF_ADVANCED_CARS) != 0)
          byTextureColor2 = byCompetitorMenuColor;
        else
          byTextureColor2 = -113;
        byFinalTextColor = byTextureColor2;
        iTextYPosition = 153;
        pszTextBuffer = &language_buffer[4416];
      LABEL_130:
        scale_text(front_vga[15], pszTextBuffer, font1_ascii, font1_offsets, 400, iTextYPosition, byFinalTextColor, 1u, 200, 640);
        break;
      default:
        break;                                  // OPTION-SPECIFIC UI: Display details for currently selected option
    }
    show_received_mesage();
    copypic(scrbuf, screen);
    if (switch_same > 0)                      // CHEAT MODE HANDLING: Process switch_same command for player synchronization
    {
      iCheatSetLoop = 0;
      if (players > 0) {
        // Set all players to the same car in cheat mode
        for (int i = 0; i < players; i++) {
            Players_Cars[i] = switch_same - 666;
        }
        //iCheatArrayOffset2 = 0;
        //do {
        //  iCheatArrayOffset2 += 4;
        //  ++iCheatSetLoop;
        //  *(int *)((char *)&infinite_laps + iCheatArrayOffset2) = switch_same - 666;
        //} while (iCheatSetLoop < players);
      }
      uiUpdatedCheatFlags = cheat_mode;
      uiUpdatedCheatFlags |= CHEAT_MODE_CLONES;
    } else {
      if (switch_same >= 0)
        goto LABEL_142;
      switch_same = 0;
      for (int i = 0; i < players; i++) {
        Players_Cars[i] = -1;
      }
      //iCheatResetLoop1 = 0;
      //switch_same = 0;
      //if (players > 0) {
      //  iCheatArrayOffset1 = 0;
      //  do {
      //    iCheatArrayOffset1 += 4;
      //    ++iCheatResetLoop1;
      //    *(int *)((char *)&infinite_laps + iCheatArrayOffset1) = -1;
      //  } while (iCheatResetLoop1 < players);
      //}
      uiUpdatedCheatFlags = cheat_mode;
      uiUpdatedCheatFlags &= ~CHEAT_MODE_CLONES;
    }
    cheat_mode = uiUpdatedCheatFlags;
  LABEL_142:
    if (!front_fade)                          // Handle screen fade-in effect
    {
      front_fade = -1;
      fade_palette(32);
      frames = 0;
    }
    while (fatkbhit())                        // KEYBOARD INPUT PROCESSING: Handle navigation and option changes
    {
      byInputKey = fatgetch();
      if (byInputKey < 0x1Bu) {
        if (byInputKey) {                                       // Enter key: Confirm selection or navigate into sub-options
          if (byInputKey == 13) {
            sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);
            if (iMenuSelection) {
              if (game_type == 1) {
                Race = 0;
                TrackLoad = 8 * iBlockIdx + 1;
              }
              iMenuSelection = 0;
              broadcast_mode = -1;
              while (broadcast_mode)
                UpdateSDL();
            } else {
              switch (iCurrentOption) {
                case 0:
                  if (iSkipColor)
                    iMenuSelection = 6;
                  else
                    iMenuSelection = 1;
                  break;
                case 1:
                  iMenuSelection = 2;
                  break;
                case 2:
                  iMenuSelection = 3;
                  break;
                case 3:
                  iMenuSelection = 4;
                  break;
                case 4:
                  iMenuSelection = 5;
                  break;
                case 5:
                  goto LABEL_248;
                default:
                  continue;
              }
            }
          }
        } else {
          byExtendedKey = fatgetch();           // Arrow keys: Navigate through options and change values
          switch (iMenuSelection) {
            case 0:
              if (byExtendedKey >= 0x48u)     // Main menu navigation: Up/Down arrows move between option categories
              {
                if (byExtendedKey <= 0x48u) {
                  if (iSkipColor) {
                    iCurrentOption = 0;
                  } else {
                    --iCurrentOption;
                    if (!iCheatModesAvailable && iCurrentOption == 4)
                      iCurrentOption = 3;
                    if (iCurrentOption < 0)
                      iCurrentOption = 0;
                  }
                } else if (byExtendedKey == 80) {
                  if (iSkipColor) {
                    iCurrentOption = 5;
                  } else {
                    if (iCheatModesAvailable) {
                      ++iCurrentOption;
                    } else if (++iCurrentOption > 3) {
                      iCurrentOption = 5;
                    }
                    if (iCurrentOption > 5)
                      iCurrentOption = 5;
                  }
                }
              }
              break;
            case 1:
              if (byExtendedKey >= 0x48u)     // Game Type option: Navigate between race modes and adjust competitors
              {
                if (byExtendedKey <= 0x48u) {
                  if (--game_type < 0)
                    game_type = 0;
                  if (competitors == 1)
                    goto LABEL_187;
                } else if (byExtendedKey == 80) {
                  if (++game_type < 2) {
                    if (competitors == 1)
                      competitors = 16;
                  } else {
                    game_type = 2;
                    competitors = 1;
                  }
                }
              }
              break;
            case 2:
              if (byExtendedKey >= 0x48u) {
                if (byExtendedKey <= 0x48u) {
                  if (levels[++level] <= 0.0)
                    --level;
                } else if (byExtendedKey == 80 && --level < 0) {
                  level = 0;
                }
              }
              break;
            case 3:
              if (byExtendedKey >= 0x48u) {
                if (byExtendedKey <= 0x48u) {
                  if (game_type < 2 && (unsigned int)competitors >= 8) {
                    if ((unsigned int)competitors <= 8) {
                      competitors = 2;
                    } else if (competitors == 16) {
                      competitors = 8;
                    }
                  }
                } else if (byExtendedKey == 80 && game_type < 2 && (unsigned int)competitors >= 2) {
                  if ((unsigned int)competitors <= 2) {
                    competitors = 8;
                  } else if (competitors == 8) {
                  LABEL_187:
                    competitors = 16;
                  }
                }
              }
              break;
            case 4:
              if (byExtendedKey >= 0x48u) {
                if (byExtendedKey <= 0x48u) {
                  if (--damage_level < 0)
                    damage_level = 0;
                } else if (byExtendedKey == 80 && ++damage_level > 2) {
                  damage_level = 2;
                }
              }
              break;
            case 5:
              if (byExtendedKey >= 0x48u) {
                if (byExtendedKey <= 0x48u) {
                  textures_off &= ~TEX_OFF_ADVANCED_CARS;
                } else if (byExtendedKey == 80) {
                  textures_off |= TEX_OFF_ADVANCED_CARS;
                }
              }
              break;
            default:
              continue;
          }
        }
      } else if (byInputKey <= 0x1Bu)           // Escape key: Exit to main menu or back to option selection
      {
        if (iMenuSelection)
          iMenuSelection = 0;
        else
          LABEL_248:
        iExitFlag = -1;
      } else if (byInputKey < 0x59u) {                                         // Space key: Cycle through cups (track groups) if not in championship
        if (byInputKey == 32 && !iSkipColor) {
          iCupIncrement = ++iBlockIdx;
          if ((cup_won & 1) == 0 && iCupIncrement == 1)
            iBlockIdx = 2;
          if ((cup_won & 2) == 0 && iBlockIdx == 2)
            iBlockIdx = 3;
          if (iBlockIdx > 2)
            iBlockIdx = 0;
          TrackLoad = 8 * iBlockIdx + (((uint8)TrackLoad - 1) & 7) + 1;
          broadcast_mode = -1;
          while (broadcast_mode)
            UpdateSDL();
        }
      } else if (byInputKey <= 0x59u || byInputKey == 121) {                                         // Y/y key: \"Yes\" - confirm championship exit and reset to race mode
        if (iSkipColor) {
          if (iMenuSelection == 6) {
            game_type = 0;
            iSkipColor = 0;
            iMenuSelection = 0;
            if (network_on) {
              if (Race <= 0) {
                broadcast_mode = -1;
                while (broadcast_mode)
                  UpdateSDL();
              } else {
                broadcast_mode = -666;
                while (broadcast_mode)
                  UpdateSDL();
                frames = 0;
                while (frames < 3)
                  ;
                close_network();
                network_champ_on = 0;
              }
            }
          }
        }
      }
      UpdateSDL();
    }
    UpdateSDL();
  } while (!iExitFlag);                         // MAIN SELECTION LOOP - Handle UI rendering and input processing
  if (!iSkipColor)                            // CLEANUP: Set final game parameters and track selection based on options
  {
    network_champ_on = 0;
    iTrackUpperLimit = 8 * iBlockIdx + 8;
    iTrackLowerLimit = 8 * iBlockIdx + 1;
    if (game_type)                            // Initialize championship mode: reset all statistics and set starting track
    {
      if ((unsigned int)game_type <= 1) {
        Race = 0;
        memset(championship_points, 0, sizeof(championship_points));
        memset(team_points, 0, sizeof(team_points));
        memset(total_kills, 0, sizeof(total_kills));
        memset(total_fasts, 0, sizeof(total_fasts));
        memset(total_wins, 0, sizeof(total_wins));
        memset(team_kills, 0, sizeof(team_kills));
        memset(team_fasts, 0, sizeof(team_fasts));
        memset(team_wins, 0, sizeof(team_wins));
        TrackLoad = 8 * iBlockIdx + 1;
      } else if (game_type == 2) {
        NoOfLaps = 5;
        competitors = 1;
        if (iTrackLowerLimit > TrackLoad || iTrackUpperLimit < TrackLoad)
          TrackLoad = 8 * iBlockIdx + (((uint8)TrackLoad - 1) & 7) + 1;
      }
    } else if (iTrackLowerLimit > TrackLoad || iTrackUpperLimit < TrackLoad) {
      TrackLoad = 8 * iBlockIdx + (((uint8)TrackLoad - 1) & 7) + 1;
    }
    broadcast_mode = -1;
    while (broadcast_mode)
      UpdateSDL();
  }
  fade_palette(0);
  fre((void **)&front_vga[14]);
  front_fade = 0;
}

//-------------------------------------------------------------------------------------------------
//00049070
void select_track()
{
  int iAnimationTimer; // edi
  int iSelectedTrack; // ebp
  int iCurrentTrack; // esi
  int iLapsForLevel; // eax
  double dZoomInterpolation; // st7
  unsigned int uiKeyDirection; // ecx
  uint8 byKey; // al
  uint8 byKey_1; // al
  float fZ_1; // [esp+4h] [ebp-4Ch]
  float fTargetZoom; // [esp+8h] [ebp-48h]
  int iFrameCount; // [esp+Ch] [ebp-44h]
  int iBlockIdx; // [esp+10h] [ebp-40h]
  int iSoundFlag; // [esp+14h] [ebp-3Ch]
  int iYaw; // [esp+18h] [ebp-38h]
  int iPrevTrackLoad; // [esp+1Ch] [ebp-34h]
  float fZ; // [esp+20h] [ebp-30h]
  int iCupOffset; // [esp+24h] [ebp-2Ch]
  int iTrackInCup; // [esp+28h] [ebp-28h]
  int iExitFlag; // [esp+2Ch] [ebp-24h]
  int iCalculatedTrack; // [esp+30h] [ebp-20h]

  iAnimationTimer = 36;
  fade_palette(0);                              // Initialize track selection screen - fade out and reset state
  iSoundFlag = 0;
  front_fade = 0;
  iExitFlag = 0;
  if (TrackLoad > 0)                          // Set initial track selection based on current TrackLoad and game type
  {
    iSelectedTrack = ((uint8)TrackLoad - 1) & 7;
    if (game_type == 1)
      iSelectedTrack = 8;
  } else {
    iSelectedTrack = 0;
  }
  iCurrentTrack = TrackLoad;
  fre((void **)&front_vga[3]);                  // Load track selection UI graphics and 3D track preview
  if (TrackLoad >= 0)
    loadtrack(TrackLoad, -1);
  front_vga[3] = (tBlockHeader *)load_picture("trkname.bm");
  front_vga[13] = (tBlockHeader *)load_picture("bonustrk.bm");
  iYaw = 0;
  front_vga[14] = (tBlockHeader *)load_picture("cupicons.bm");
  frames = 0;
  fZ = cur_TrackZ;
  fZ_1 = cur_TrackZ;
  fTargetZoom = -(cur_TrackZ + -10000000.0f) * 0.05f;
  do {                                             // Main track selection display loop
    if (game_type == 1)
      iSelectedTrack = 8;
    iFrameCount = frames;
    frames = 0;
    iPrevTrackLoad = TrackLoad;
    //if (SoundCard && front_fade && SampleHandleCar[86].handles[0] != -1 && sosDIGISampleDone(*(int *)&DIGIHandle, SampleHandleCar[86].handles[0]))// Handle frontend car engine sound sample playback
    if (SoundCard && front_fade && SampleHandleCar[86].handles[0] != -1 && DIGISampleDone(SampleHandleCar[86].handles[0]))// Handle frontend car engine sound sample playback
    {
      frontendsample(0x8000);
      SampleHandleCar[86].handles[0] = -1;
    }
    if (switch_types)                         // Handle game type switching (championship/single race/team game)
    {
      game_type = switch_types - 1;
      if (switch_types == 1 && competitors == 1)
        competitors = 16;
      switch_types = 0;
      if (game_type == 1)
        Race = ((uint8)TrackLoad - 1) & 7;
      else
        network_champ_on = 0;
    }
    display_picture(scrbuf, front_vga[0]);      // Draw main background and UI elements
    display_block(scrbuf, front_vga[1], 2, head_x, head_y, 0);
    if (cup_won)                              // Show cup trophy if championship won
      display_block(scrbuf, front_vga[1], 8, 480, 388, 0);
    display_block(scrbuf, front_vga[6], 0, 36, 2, 0);
    display_block(scrbuf, front_vga[5], player_type, -4, 247, 0);// Display player type and game type indicators
    display_block(scrbuf, front_vga[5], game_type + 5, 135, 247, 0);
    display_block(scrbuf, front_vga[4], 1, 76, 257, -1);
    if (iSelectedTrack >= 8)                  // Draw track selector: championship view or individual track selector
    {
      display_block(scrbuf, front_vga[6], 4, 62, 336, -1);
    } else {
      display_block(scrbuf, front_vga[6], 2, 62, 336, -1);
      front_text(front_vga[2], "~", font2_ascii, font2_offsets, sel_posns[iSelectedTrack].x, sel_posns[iSelectedTrack].y, 0x8Fu, 0);
    }
    front_text(front_vga[2], "AUTO ARIEL", font2_ascii, font2_offsets, sel_posns[0].x + 132, sel_posns[0].y + 7, 0x8Fu, 2u);// Display hardcoded track names (should use track name data)
    front_text(front_vga[2], "DESILVA", font2_ascii, font2_offsets, sel_posns[1].x + 132, sel_posns[1].y + 7, 0x8Fu, 2u);
    front_text(front_vga[2], "PULSE", font2_ascii, font2_offsets, sel_posns[2].x + 132, sel_posns[2].y + 7, 0x8Fu, 2u);
    front_text(front_vga[2], "GLOBAL", font2_ascii, font2_offsets, sel_posns[3].x + 132, sel_posns[3].y + 7, 0x8Fu, 2u);
    front_text(front_vga[2], "MILLION PLUS", font2_ascii, font2_offsets, sel_posns[4].x + 132, sel_posns[4].y + 7, 0x8Fu, 2u);
    front_text(front_vga[2], "MISSION", font2_ascii, font2_offsets, sel_posns[5].x + 132, sel_posns[5].y + 7, 0x8Fu, 2u);
    front_text(front_vga[2], "ZIZIN", font2_ascii, font2_offsets, sel_posns[6].x + 132, sel_posns[6].y + 7, 0x8Fu, 2u);
    front_text(front_vga[2], "REISE WAGON", font2_ascii, font2_offsets, sel_posns[7].x + 132, sel_posns[7].y + 7, 0x8Fu, 2u);
    iBlockIdx = (TrackLoad - 1) / 8;  // Calculate cup icon index (8 tracks per cup)
    //iBlockIdx = (TrackLoad - 1 - (__CFSHL__((TrackLoad - 1) >> 31, 3) + 8 * ((TrackLoad - 1) >> 31))) >> 3;// Calculate cup icon index for display
    if (TrackLoad >= 0) {                                           // Render 3D track preview
      if (iAnimationTimer)
        show_3dmap(fZ, 1280, iYaw);
      else
        show_3dmap(fZ_1, 1280, iYaw);
      if (game_type >= 2)                     // Set number of laps based on game type and difficulty level
      {
        NoOfLaps = 5;
      } else {
        iLapsForLevel = cur_laps[level];
        NoOfLaps = iLapsForLevel;
        if (competitors == 2)
          NoOfLaps = iLapsForLevel / 2;
      }
      sprintf(buffer, "%s: %i", &language_buffer[4544], NoOfLaps);
      front_text(front_vga[15], buffer, font1_ascii, font1_offsets, 420, 16, 0x8Fu, 1u);
      front_text(front_vga[15], &language_buffer[4608], font1_ascii, font1_offsets, 420, 34, 0x8Fu, 1u);
      if (RecordCars[TrackLoad] < 0)          // Display track record holder and lap time
      {
        sprintf(buffer, "%s", RecordNames[TrackLoad]);
      } else {
        // Format lap time as MM:SS:CC (minutes:seconds:centiseconds)
        int iTotalCentiseconds = (int)(RecordLaps[TrackLoad] * 100.0);
        int iRecordMinutes = iTotalCentiseconds / 6000;
        int iRecordSeconds = (iTotalCentiseconds / 100) % 60;
        int iRecordCentiseconds = iTotalCentiseconds % 100;

        sprintf(buffer, "%s - %s - %02i:%02i:%02i",
            RecordNames[TrackLoad],
            CompanyNames[RecordCars[TrackLoad] & 0xF],
            iRecordMinutes,
            iRecordSeconds,
            iRecordCentiseconds);
        //dRecordTimeFloat = RecordLaps[TrackLoad] * 100.0;// Format lap time as MM:SS:CC (minutes:seconds:centiseconds)
        //_CHP();
        //LODWORD(llTimeCalc) = (int)dRecordTimeFloat;
        //HIDWORD(llTimeCalc) = (int)dRecordTimeFloat >> 31;
        //iRecordMinutes = llTimeCalc / 6000;
        //LODWORD(llTimeCalc) = (int)dRecordTimeFloat;
        //iRecordSeconds = (int)(llTimeCalc / 100) % 60;
        //LODWORD(llTimeCalc) = (int)dRecordTimeFloat;
        //sprintf(
        //  buffer,
        //  "%s - %s - %02i:%02i:%02i",
        //  RecordNames[TrackLoad],
        //  CompanyNames[RecordCars[TrackLoad] & 0xF],
        //  iRecordMinutes,
        //  iRecordSeconds,
        //  (unsigned int)(llTimeCalc % 100));
      }
      front_text(front_vga[15], buffer, font1_ascii, font1_offsets, 420, 52, 0x8Fu, 1u);
    }
    display_block(scrbuf, front_vga[14], iBlockIdx, 500, 300, 0);// Display cup icon and track name/type
    if (TrackLoad <= 0) {
      if (TrackLoad)
        front_text(front_vga[2], "EDITOR", font2_ascii, font2_offsets, 190, 350, 0x8Fu, 0);
      else
        front_text(front_vga[2], "TRACK ZERO", font2_ascii, font2_offsets, 190, 350, 0x8Fu, 0);
    } else if (TrackLoad >= 17)                 // Display track name: bonus tracks (17+), regular tracks (1-16), or special tracks
    {
      display_block(scrbuf, front_vga[13], TrackLoad - 17, 190, 356, 0);
    } else {
      display_block(scrbuf, front_vga[3], TrackLoad - 1, 190, 356, 0);
    }
    show_received_mesage();
    copypic(scrbuf, screen);
    if (switch_same > 0)                      // Handle same-car mode activation/deactivation
    {

      for (int i = 0; i < players; i++) {
          Players_Cars[i] = switch_same - 666;
      }
      //iPlayerLoop2 = 0;                         // Activate same-car mode: set all players to same car type
      //if (players > 0) {
      //  iArrayOffset2 = 0;
      //  do {
      //    iArrayOffset2 += 4;
      //    ++iPlayerLoop2;
      //    *(int *)((char *)&infinite_laps + iArrayOffset2) = switch_same - 666;
      //  } while (iPlayerLoop2 < players);
      //}

      cheat_mode |= CHEAT_MODE_CLONES;
    } else if (switch_same < 0) {

      switch_same = 0;
      for (int i = 0; i < players; i++) {
          Players_Cars[i] = -1;
      }
      //iPlayerLoop1 = 0;                         // Deactivate same-car mode: reset all player car selections
      //switch_same = 0;
      //if (players > 0) {
      //  iArrayOffset1 = 0;
      //  do {
      //    iArrayOffset1 += 4;
      //    ++iPlayerLoop1;
      //    *(int *)((char *)&infinite_laps + iArrayOffset1) = -1;
      //  } while (iPlayerLoop1 < players);
      //}

      cheat_mode &= ~CHEAT_MODE_CLONES;
    }
    if (!iAnimationTimer)                     // Handle 3D camera zoom animation
    {
      dZoomInterpolation = (double)iFrameCount * fTargetZoom + fZ_1;
      fZ_1 = (float)dZoomInterpolation;
      if (dZoomInterpolation <= 10000000.0) {
        if (fZ_1 < fZ) {
          fZ_1 = fZ;
          iAnimationTimer = 72;
          fTargetZoom = -fTargetZoom;  // Reverse zoom direction
        }
      } else {
        fZ_1 = 10000000.0;                      // Camera reached max zoom - load new track and reset animation
        if (iCurrentTrack == TrackLoad)
          iPrevTrackLoad = -1;
        TrackLoad = iCurrentTrack;
        if (frontendspeechptr)
          fre((void **)&frontendspeechptr);
        if (TrackLoad >= 0)
          loadtrack(TrackLoad, -1);
        loadtracksample(TrackLoad);
        fZ = cur_TrackZ;
        iSoundFlag = -1;
        fTargetZoom = (cur_TrackZ + -10000000.0f) * 0.05f;
        if (iPrevTrackLoad != -1) {
          broadcast_mode = -1;
          while (broadcast_mode)
            UpdateSDL();
        }
        iPrevTrackLoad = -1;
        frames = 0;
      }
    }
    if (!front_fade)                          // Initialize screen fade and sound effects on first display
    {
      loadtracksample(TrackLoad);
      front_fade = -1;
      fade_palette(32);
      frontendsample(0x8000);
      frames = 0;
    }
    if (TrackLoad != iPrevTrackLoad)          // Track changed - play selection sound and reset animation
    {
      if (!iSoundFlag)
        sfxsample(SOUND_SAMPLE_TRACK, 0x8000);
      iSoundFlag = 0;
      iCurrentTrack = TrackLoad;
      iAnimationTimer = 0;
    }
    iCupOffset = 8 * iBlockIdx;                 // Calculate track selection values for input handling
    iTrackInCup = iSelectedTrack + 1;
    uiKeyDirection = 0;
    iCalculatedTrack = 8 * iBlockIdx + iSelectedTrack + 1;
    while (fatkbhit())                        // Process keyboard input for track selection
    {
      UpdateSDL();
      byKey = fatgetch();
      if (byKey < 0xDu) {
        if (!byKey) {
          byKey_1 = fatgetch();                 // Handle arrow keys: Up (0x48) and Down (0x50)
          if (byKey_1 >= 0x48u) {
            if (byKey_1 <= 0x48u) {
              uiKeyDirection = 2;
            } else if (byKey_1 == 0x50) {
              uiKeyDirection = 1;
            }
          }
        }
      } else if (byKey <= 0xDu) {                                         // Enter key pressed - confirm track selection
        if (iSelectedTrack != 8 && iCurrentTrack != iCalculatedTrack || iSelectedTrack == 8) {
          remove_frontendspeech();
          sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);
        }
        //if (iCurrentTrack == iCalculatedTrack && SoundCard && frontendspeechhandle != -1 && sosDIGISampleDone(*(int *)&DIGIHandle, frontendspeechhandle)) {
        if (iCurrentTrack == iCalculatedTrack && SoundCard && frontendspeechhandle != -1 && DIGISampleDone(frontendspeechhandle)) {
          frontendspeechhandle = -1;
          frontendsample(0x8000);
        }
        if (iSelectedTrack == 8) {
          iExitFlag = -1;
        } else if (game_type != 1 && iCurrentTrack != iCupOffset + iTrackInCup) {
          sfxsample(SOUND_SAMPLE_TRACK, 0x8000);
          iCurrentTrack = iCupOffset + iTrackInCup;
          if (iAnimationTimer) {
            iAnimationTimer = 0;
          } else if (fTargetZoom < 0.0) {
            fTargetZoom = -fTargetZoom;  // Reverse zoom direction
          }
        }
      } else if (byKey >= 0x1Bu) {
        if (byKey <= 0x1Bu) {
          remove_frontendspeech();              // Escape key pressed - exit track selection
          sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);
          iExitFlag = -1;
        } else if (byKey == 32 && game_type != 1 && TrackLoad > 0)// Space key pressed - cycle through cup categories
        {
          iCurrentTrack += 8;
          sfxsample(SOUND_SAMPLE_TRACK, 0x8000);
          if (iCurrentTrack > 8 && iCurrentTrack < 17 && (cup_won & 1) == 0)// Skip locked cup categories based on championship progress
            iCurrentTrack += 8;
          if (iCurrentTrack > 16 && iCurrentTrack < 25 && (cup_won & 2) == 0)
            iCurrentTrack += 8;
          if (iCurrentTrack > 24)
            iCurrentTrack -= 24;
          if (iAnimationTimer) {
            iAnimationTimer = 0;
          } else if (fTargetZoom < 0.0) {
            fTargetZoom = -fTargetZoom;  // Reverse zoom direction
          }
        }
      }
    }
    if (uiKeyDirection)                       // Process up/down arrow key movement in track list
    {
      if (uiKeyDirection > 1) {
        if (game_type != 1 && --iSelectedTrack < 0)
          iSelectedTrack = 0;
      } else if (game_type != 1 && ++iSelectedTrack > 8) {
        iSelectedTrack = 8;
      }
    }
    iYaw = ((uint16)iYaw + 32 * (uint16)iFrameCount) & 0x3FFF;

    UpdateSDL();
  } while (!iExitFlag);
  fade_palette(0);                              // Cleanup: fade out, free graphics memory, restore car selection graphics
  front_fade = 0;
  fre((void **)&front_vga[3]);
  fre((void **)&front_vga[13]);
  fre((void **)&front_vga[14]);
  front_vga[3] = (tBlockHeader *)load_picture("carnames.bm");
  if (frontendspeechptr)
    fre((void **)&frontendspeechptr);
}

//-------------------------------------------------------------------------------------------------
//00049C50
void save_params()
{
  p_tex_size = gfx_size;
}

//-------------------------------------------------------------------------------------------------
//00049C60
void reset_params()
{
  gfx_size = p_tex_size;
}

//-------------------------------------------------------------------------------------------------
//00049C70
void NetworkWait()
{
  int iPlayerLoop1; // esi
  int iPlayerLoop2; // esi
  int iPlayerDisplayLoop; // ebp
  int iPlayerIndex; // esi
  int iTextYPos; // edi
  int iCarType; // ecx
  int iCarTypeForSprite; // ebx
  unsigned int uiKeyPressed; // eax
  int iSavedScrSize; // [esp+0h] [ebp-2Ch]
  int iCarSpriteYOffset; // [esp+4h] [ebp-28h]
  int iContinueLoop; // [esp+8h] [ebp-24h]
  char *szCurrentPlayerName; // [esp+Ch] [ebp-20h]
  int iY; // [esp+10h] [ebp-1Ch]

  iSavedScrSize = scr_size;                     // Initialize network wait screen - save screen size and setup display
  front_fade = 0;
  tick_on = -1;
  frontend_on = -1;
  clear_network_game();
  netCD = 0;
  cd_error = 0;
  SVGA_ON = -1;
  network_test = 1;
  init_screen();
  front_vga[0] = (tBlockHeader *)load_picture("result.bm");// Load UI graphics: result screen, fonts, car sprites, and text tables
  front_vga[1] = (tBlockHeader *)load_picture("font2.bm");
  front_vga[2] = (tBlockHeader *)load_picture("smallcar.bm");
  front_vga[3] = (tBlockHeader *)load_picture("tabtext.bm");
  front_vga[15] = (tBlockHeader *)load_picture("font1.bm");
  iContinueLoop = -1;
  setpal("result.pal");
  if (network_on) {
    while (1) {
      UpdateSDL();
      if (!iContinueLoop)
        goto LABEL_83;
      if (switch_types)                       // Handle game type switching (championship/single race/team game)
      {
        game_type = switch_types - 1;
        if (switch_types == 1 && competitors == 1)
          competitors = 16;
        switch_types = 0;
        if (game_type == 1)
          Race = ((uint8)TrackLoad - 1) & 7;
        else
          network_champ_on = 0;
      }
      if (switch_same <= 0)
        break;                                  // Check for same-car mode activation

      // Force all players to use the same car type in same-car mode
      for (iPlayerLoop2 = 0; iPlayerLoop2 < players; iPlayerLoop2++) {
          Players_Cars[iPlayerLoop2] = switch_same - 666;  // Convert switch value to car type
      }
      //iPlayerLoop2 = 0;
      //if (players > 0) {
      //  iArrayOffset2 = 0;                      // Set same-car mode for all players (switch_same - 666 magic number)
      //  do {
      //    iArrayOffset2 += 4;
      //    ++iPlayerLoop2;
      //    *(int *)((char *)&infinite_laps + iArrayOffset2) = switch_same - 666;
      //  } while (iPlayerLoop2 < players);
      //}

      cheat_mode |= CHEAT_MODE_CLONES;

      if (Players_Cars[player1_car] < 0)      // Check if player1 car is invalid (negative) - disconnect from network
      {
        StartPressed = 0;
        time_to_start = 0;
        broadcast_mode = -670;
        while (broadcast_mode)
          ;
        iContinueLoop = 0;
      LABEL_25:
        --players_waiting;
      }
    LABEL_26:
      check_cars();
      display_picture(scrbuf, front_vga[0]);    // Main network lobby display loop - show waiting screen
      sprintf(buffer, "%s: %i", &language_buffer[64], players);
      front_text(front_vga[1], buffer, font2_ascii, font2_offsets, 16, 4, 0x8Fu, 0);
      sprintf(buffer, "%s: %i", &language_buffer[256], TrackLoad);
      front_text(front_vga[1], buffer, font2_ascii, font2_offsets, 16, 24, 0x8Fu, 0);
      if (game_type)                          // Display game type text based on current mode
      {
        if ((unsigned int)game_type <= 1) {
          sprintf(buffer, "%s", &language_buffer[3520]);
        } else if (game_type == 2) {
          sprintf(buffer, "%s", &language_buffer[3712]);
        }
      } else {
        sprintf(buffer, "%s", &language_buffer[3648]);
      }
      front_text(front_vga[1], buffer, font2_ascii, font2_offsets, 200, 4, 0x8Fu, 1u);
      if (players_waiting == network_on)      // Show flashing "Ready to start" message when all players connected
      {
        if ((frames & 0xFu) < 8)
          front_text(front_vga[1], &language_buffer[4800], font2_ascii, font2_offsets, 200, 22, 0x8Fu, 1u);
        if (time_to_start)
          iContinueLoop = 0;
      }
      iPlayerDisplayLoop = 0;
      if (players > 0) {
        iPlayerIndex = 0;                       // Display player list with names, car selections, and connection status
        iY = 44;
        szCurrentPlayerName = player_names[0];
        iTextYPos = 49;
        do {                                       // Show flashing indicator for player1, solid for others
          if (player_started[iPlayerIndex] && (!iPlayerDisplayLoop && (frames & 0xFu) < 8 || iPlayerDisplayLoop > 0))
            display_block(scrbuf, front_vga[2], 0, 13, iY, 0);
          sprintf(buffer, "%i", iPlayerDisplayLoop + 1);
          iCarSpriteYOffset = 22 * iPlayerDisplayLoop;
          front_text(front_vga[1], buffer, font2_ascii, font2_offsets, 33, iTextYPos, 0x8Fu, 0);
          sprintf(buffer, "%s", szCurrentPlayerName);
          front_text(front_vga[1], buffer, font2_ascii, font2_offsets, 85, iTextYPos, 0x8Fu, 0);
          iCarType = Players_Cars[iPlayerIndex];
          if (iCarType >= 0)                  // Display car company name and sprite if valid car selected
          {
            sprintf(buffer, "%s", CompanyNames[iCarType]);
            front_text(front_vga[1], buffer, font2_ascii, font2_offsets, 218, iTextYPos, 0x8Fu, 0);
            iCarTypeForSprite = Players_Cars[iPlayerIndex];
            if (iCarTypeForSprite < 8) {
              if ((textures_off & TEX_OFF_ADVANCED_CARS) != 0)
                display_block(scrbuf, front_vga[2], smallcars[1][iCarTypeForSprite], 165, iCarSpriteYOffset + 46, 0);
              else
                display_block(scrbuf, front_vga[2], smallcars[0][iCarTypeForSprite], 165, iCarSpriteYOffset + 46, 0);
            } else {
              front_text(front_vga[1], "CHEAT", font2_ascii, font2_offsets, 165, iTextYPos, 0x8Fu, 0);
            }
          } else {
            front_text(front_vga[1], &language_buffer[4160], font2_ascii, font2_offsets, 218, iTextYPos, 0x8Fu, 0);
          }
          ++iPlayerIndex;
          iTextYPos += 22;
          ++iPlayerDisplayLoop;
          iY += 22;
          szCurrentPlayerName += 9;
        } while (iPlayerDisplayLoop < players);
      }
      if (time_to_start)
        iContinueLoop = 0;
      if (iContinueLoop) {
        show_received_mesage();
        copypic(scrbuf, screen);
        if (!front_fade) {
          front_fade = -1;                      // Initialize screen fade and network synchronization
          fade_palette(32);
          broadcast_mode = -668;
          while (broadcast_mode)
            UpdateSDL();
          broadcast_mode = -668;
          while (broadcast_mode)
            UpdateSDL();
          frames = 0;
        }
      }
      while (fatkbhit())                      // Handle keyboard input for network lobby
      {
        UpdateSDL();
        uiKeyPressed = fatgetch();
        if (uiKeyPressed < 0xD) {
          if (!uiKeyPressed)
            fatgetch();
        } else if (uiKeyPressed <= 0xD) {                                       // Enter key pressed - start race if all players ready
          if (players_waiting == network_on && !time_to_start) {
            iContinueLoop = 0;
            broadcast_mode = -671;
            while (broadcast_mode)
              UpdateSDL();
            broadcast_mode = -671;
            while (broadcast_mode)
              UpdateSDL();
            broadcast_mode = -671;
            while (broadcast_mode)
              UpdateSDL();
            time_to_start = -1;
          }
        } else if (uiKeyPressed == 27 && !time_to_start && !restart_net)// Escape key pressed - leave network game
        {
          StartPressed = 0;
          time_to_start = 0;
          broadcast_mode = -670;
          while (broadcast_mode)
            UpdateSDL();
          iContinueLoop = 0;
          --players_waiting;
          no_clear = -1;
        }
      }
    }
    if (switch_same >= 0)
      goto LABEL_26;

    for (iPlayerLoop1 = 0; iPlayerLoop1 < players; iPlayerLoop1++) {
        Players_Cars[iPlayerLoop1] = -1;
    }
    //iPlayerLoop1 = 0;                           // Reset same-car mode - clear player settings
    //if (players > 0) {
    //  iArrayOffset1 = 0;
    //  do {
    //    iArrayOffset1 += 4;
    //    ++iPlayerLoop1;
    //    *(int *)((char *)&infinite_laps + iArrayOffset1) = -1;
    //  } while (iPlayerLoop1 < players);
    //}

    cheat_mode &= ~CHEAT_MODE_CLONES;

    switch_same = 0;
    StartPressed = 0;
    time_to_start = 0;
    broadcast_mode = -670;
    while (broadcast_mode)
      ;
    iContinueLoop = 0;
    goto LABEL_25;
  }
LABEL_83:
  while (ticks + 18 > ticks)                  // Wait loop for network synchronization timing
    ;
  if (time_to_start)                          // Final network synchronization before race starts
  {
    broadcast_mode = -314;
    while (broadcast_mode)
      ;
    if (wConsoleNode == master)               // Master node waits for all client records, then broadcasts seed
    {
      while (received_records < network_on)
        ;
      broadcast_mode = -2718;
      while (broadcast_mode)
        ;
    } else {                                           // Client nodes wait for random seed from master
      while (!received_seed)
        ;
    }
  }
  check_cars();                                 // Cleanup: fade screen, free graphics memory, restore screen size
  fade_palette(0);
  front_fade = 0;
  fre((void **)front_vga);
  fre((void **)&front_vga[1]);
  fre((void **)&front_vga[2]);
  fre((void **)&front_vga[3]);
  fre((void **)&front_vga[15]);
  scr_size = iSavedScrSize;
}

//-------------------------------------------------------------------------------------------------
//0004A440
void loadcheatnames()
{
  char buffer[0x400]; // Temporary buffer for file contents
  FILE *fp;
  int iSize;
  char *szTok;
  int iCheatIdx = 0;

  // Try to open PASSWORD.INI to get its size
  iSize = ROLLERfilelength("PASSWORD.INI");

  fp = ROLLERfopen("PASSWORD.INI", "rb");
  if (!fp)
    return;

  // Read file into buffer
  fread(buffer, iSize, 1, fp);
  fclose(fp);

  // Decode the read contents
  decode((uint8 *)buffer, iSize, 23, 37);

  // Tokenize buffer and load cheat names
  szTok = strtok(buffer, "\n\t\r");

  while (szTok) {
    // Check for end marker string
    if (strcmp(szTok, "#") == 0)
      break;

    // Copy token into cheat_names[iCheatIdx]
    strncpy(cheat_names[iCheatIdx], szTok, 9);

    iCheatIdx++;
    szTok = strtok(NULL, "\n\t\r");
  }

  // Set final cheat name entry to end marker
  cheat_names[iCheatIdx][0] = '#';
  cheat_names[iCheatIdx][1] = '\0';

  // Wipe the buffer
  memset(buffer, 0, 0x57);

  // Perform another decode on cheat_names
  decode((uint8 *)cheat_names, 288, 43, 87);
}

//-------------------------------------------------------------------------------------------------
//0004A5C0
int CheckNames(char *szPlayerName, int iPlayerIdx)
{
  int iCheatIdx = 0;

  // Decode cheat names list
  decode((uint8*)cheat_names, 288, 43, 87);

  // Skip processing if cheat list is empty
  if (cheat_names[0][0] == '#') {
    // Re-encode cheat names
    decode((uint8*)cheat_names, 288, 43, 87);
    return 0;
  }

  char *szCurrCheat = (char *)cheat_names;

  // Process all cheat names until terminator '#'
  while (*szCurrCheat != '#') {
      // Check if player name matches current cheat name
    if (name_cmp(szPlayerName, szCurrCheat)) {
      // Handle cheats
      if (iCheatIdx <= 25) {
        switch (iCheatIdx) {
          case 0: //SUICYCO (cheat car, explode opponent)
            Players_Cars[iPlayerIdx] = CAR_DESIGN_SUICYCO;
            name_copy(szPlayerName, "DAMIAN");
            cheat_mode |= CHEAT_MODE_CHEAT_CAR;
            break;
          case 1: //MAYTE (cheat car, top speed)
            Players_Cars[iPlayerIdx] = CAR_DESIGN_MAYTE;
            name_copy(szPlayerName, "DAMON");
            cheat_mode |= CHEAT_MODE_CHEAT_CAR;
            break;
          case 2: //2X4B523P (cheat car, flip opponent)
            Players_Cars[iPlayerIdx] = CAR_DESIGN_2X4B523P;
            name_copy(szPlayerName, "GRAHAM");
            cheat_mode |= CHEAT_MODE_CHEAT_CAR;
            break;
          case 3: //TINKLE (cheat car, jump opponent)
            Players_Cars[iPlayerIdx] = CAR_DESIGN_TINKLE;
            name_copy(szPlayerName, "KEV");
            cheat_mode |= CHEAT_MODE_CHEAT_CAR;
            break;
          case 4: //LOVEBUN (formula car)
            Players_Cars[iPlayerIdx] = CAR_DESIGN_F1WACK;
            name_copy(szPlayerName, "LISA");
            cheat_mode |= CHEAT_MODE_CHEAT_CAR;
            break;
          case 5: //DR DEATH (death mode)
            name_copy(szPlayerName, "PAT");
            cheat_mode |= CHEAT_MODE_DEATH_MODE;
            break;
          case 6: //SUPERMAN (invincible human car)
            name_copy(szPlayerName, "MARK");
            cheat_mode |= CHEAT_MODE_INVINCIBLE;
            player_invul[iPlayerIdx] = -1;
            break;
          case 7: //REMOVE
            name_copy(szPlayerName, "MR BRUSH");
            if (cheat_mode & CHEAT_MODE_CLONES) {
              // Handle player cars and infinite laps
              short nPlayer1Car = (short)player1_car;
              if ((short)nPlayer1Car == iPlayerIdx) {
                for (int i = 0; i < players; i++) {
                  infinite_laps = -1;
                }
              } else {
                for (int i = 0; i < players; i++) {
                  if (i != (int)nPlayer1Car) {
                    Players_Cars[i] = -1;
                  }
                }
              }
              switch_same = -1;
            }
            if (cheat_mode & CHEAT_MODE_50HZ_TIMER) {
              release_ticktimer();
              claim_ticktimer(36);
            }
            cheat_mode = 0;
            player_invul[iPlayerIdx] = 0;
            CalcCarSizes();
            break;
          case 8: //TOPTUNES (alternate voice)
            name_copy(szPlayerName, "DJ SFX");
            cheat_samples = -1;
            releasesamples();
            loadfatalsample();
            break;
          case 9: //GOLDBOY (unlock premier cup)
            name_copy(szPlayerName, "PHIL");
            cup_won |= 1;
            textures_off |= TEX_OFF_PREMIER_CUP_AVAILABLE;
            break;
          case 10: //CUP WON (view end sequence)
            name_copy(szPlayerName, "LAZY");
            cheat_mode |= CHEAT_MODE_END_SEQUENCE;
            break;
          case 11: //I WON (view race history)
            name_copy(szPlayerName, "IDOL");
            cheat_mode |= CHEAT_MODE_RACE_HISTORY;
            break;
          case 12: //CINEMA (widescreen)
            name_copy(szPlayerName, "LUMIERE");
            cheat_mode |= CHEAT_MODE_WIDESCREEN;
            break;
          case 13: //ROLL EM (view credits)
            name_copy(szPlayerName, "MR CRED");
            cheat_mode |= CHEAT_MODE_CREDITS;
            break;
          case 14: //FORMULA1 (advanced car set)
            name_copy(szPlayerName, "NEIL");
            cheat_mode |= CHEAT_MODE_ADVANCED_CARS;
            break;
          case 15: //MREPRISE (bonus cup unlocked)
            name_copy(szPlayerName, "MR BONUS");
            textures_off |= TEX_OFF_BONUS_CUP_AVAILABLE;
            cup_won |= 2;
            break;
          case 16: //DUEL (killer opponents)
            name_copy(szPlayerName, "MR EVIL");
            cheat_mode |= CHEAT_MODE_KILLER_OPPONENTS;
            break;
          case 17: //PROCESS
          {
            float A, B;
            memcpy(&A, (uint32_t[]) { 1249905654 }, 4);
            memcpy(&B, (uint32_t[]) { 1245708284 }, 4);
            float res_float = (float)((float)(A / B) * 3145727.0f);
            uint32_t res_bits;
            memcpy(&res_bits, &res_float, 4);
            if (res_bits == 1249905654u) {
              name_copy(szPlayerName, "TYPE B");
            } else {
              name_copy(szPlayerName, "TYPE A");
            }
            break;
          }
          case 18: //MRFROSTY (icy roads)
            name_copy(szPlayerName, "SNOWMAN");
            cheat_mode |= CHEAT_MODE_ICY_ROAD;
            break;
          case 19: //MR ZOOM (50Hz timer)
            name_copy(szPlayerName, "SPEEDY");
            cheat_mode |= CHEAT_MODE_50HZ_TIMER;
            release_ticktimer();
            claim_ticktimer(50);
            break;
          case 20: //TACHYONS (100Hz timer)
            name_copy(szPlayerName, "NUCLEAR!");
            cheat_mode |= CHEAT_MODE_100HZ_TIMER | CHEAT_MODE_50HZ_TIMER;
            release_ticktimer();
            claim_ticktimer(100);
            break;
          case 21: //YOTARACE (double track size)
            name_copy(szPlayerName, "GULLIVER");
            cheat_mode |= CHEAT_MODE_DOUBLE_TRACK;
            break;
          case 22: //CLONES
            //removed?
            //cheat_mode |= CHEAT_MODE_CLONES
            break;
          case 23: //TINYTOTS (tiny cars)
            name_copy(szPlayerName, "TINYTIM");
            cheat_mode |= CHEAT_MODE_TINY_CARS;
            CalcCarSizes();
            break;
          case 24: //WARPGATE (warp)
            name_copy(szPlayerName, "HEADACHE");
            cheat_mode |= CHEAT_MODE_WARP;
            break;
          case 25: //FREAKY (invert colors)
            name_copy(szPlayerName, "PAINTER");
            cheat_mode |= CHEAT_MODE_FREAKY;
            break;
          default:
            break;
        }
      }
    }
    // Handle empty player name
    else if (*szPlayerName == '\0') {
      name_copy(szPlayerName, "MR DULL");
      if (iPlayerIdx == 0) {
        cheat_mode |= CHEAT_MODE_GRAYSCALE;
      }
      break;
    }
    //cheats added by ROLLER
    else if (name_cmp(szPlayerName, "CHRSTINE")) {
      Players_Cars[iPlayerIdx] = CAR_DESIGN_DEATH;
      name_copy(szPlayerName, "MR EVIL");
      cheat_mode |= CHEAT_MODE_CHEAT_CAR;
      break;
    }

    // Move to next cheat name
    szCurrCheat += 9;
    iCheatIdx++;
  }
  // Re-encode cheat names
  decode((uint8*)cheat_names, 288, 43, 87);

  return 0;
}

//-------------------------------------------------------------------------------------------------
//0004AB30
void restart_net_game()
{
  int iActualCompetitors; // edi
  int iGridIndex; // edx
  int iCompetitorLoop; // ecx
  int i; // eax
  int iTotalCars; // esi
  int iCarIndex; // edx
  int iGridOffset; // ecx
  int iNonCompSearch; // eax
  int j; // eax
  int iAISearch; // ebx
  int iRandRange; // eax
  int iFirstSwapPos; // ecx
  int iSecondRand; // eax
  int iSecondSwapPos; // eax
  int iTempCarId; // ebp
  int k; // esi
  int iHandicapPos; // edx
  int iHumanPos; // ebx
  int iOrderLoop; // eax
  int m; // eax
  int iAICarId; // ecx
  int iHumanCarId; // edx
  int iMaxCarOffset; // [esp+4h] [ebp-1Ch]

  SVGA_ON = -1;                                 // Initialize graphics and game state
  init_screen();
  winx = 0;
  winy = 0;
  winw = XMAX;
  mirror = 0;
  winh = YMAX;
  frontend_on = -1;
  time_to_start = 0;
  StartPressed = 0;
  tick_on = -1;
  load_language_file(szSelectEng, 0);           // Load language files for interface
  load_language_file(szConfigEng, 1);
  remove_messages(-1);
  reset_network(0);                             // Reset network and wait for broadcast mode to clear
  broadcast_mode = -667;
  while (broadcast_mode)
    UpdateSDL();
  no_clear = 0;
  if (!quit_game && !intro) {
    check_cars();                               // Check cars and wait for network if not quitting or in intro
    NetworkWait();
  }
  if (replaytype != 2 && !quit_game)
    AllocateCars();
  Race = ((uint8)TrackLoad - 1) & 7;            // Calculate current race number (0-7)
  if (game_type == 1 && !Race) {
    memset(championship_points, 0, sizeof(championship_points));// Reset championship statistics for new championship
    memset(team_points, 0, sizeof(team_points));
    memset(total_kills, 0, sizeof(total_kills));
    memset(total_fasts, 0, sizeof(total_fasts));
    memset(total_wins, 0, sizeof(total_wins));
    memset(team_kills, 0, sizeof(team_kills));
    memset(team_fasts, 0, sizeof(team_fasts));
    memset(team_wins, 0, sizeof(team_wins));
  }
  iActualCompetitors = competitors;
  if (competitors == 2)                       // Determine actual number of competitors based on game type
  {
    iActualCompetitors = players;
    if (players < 2)
      iActualCompetitors = competitors;
  }
  if (competitors == 1)
    iActualCompetitors = players;
  iGridIndex = 0;
  if (iActualCompetitors > 0) {                                             // Fill grid with competing cars (skip non-competitors)
    for (iCompetitorLoop = 0; iCompetitorLoop < iActualCompetitors; ++iCompetitorLoop) {
      for (i = iGridIndex; non_competitors[i]; ++i)
        ++iGridIndex;
      grid[iCompetitorLoop] = iGridIndex++;
    }
  }
  iTotalCars = iActualCompetitors;
  iCarIndex = 0;
  if (iActualCompetitors < numcars) {
    iGridOffset = 4 * iActualCompetitors;       // Fill remaining grid positions with AI cars
    iMaxCarOffset = 4 * numcars;
    do {
      for (iNonCompSearch = iCarIndex; !non_competitors[iNonCompSearch]; ++iNonCompSearch)
        ++iCarIndex;
      ++iTotalCars;
      grid[iGridOffset / 4u] = iCarIndex;
      iGridOffset += 4;
      ++iCarIndex;
    } while (iGridOffset < iMaxCarOffset);
  }
  if (game_type == 1 && Race > 0) {
    if (iActualCompetitors > 0) {
      for (j = 0; j < iActualCompetitors; ++j) {
          grid[j] = champorder[j];
      }
      //for (j = 0; j < iActualCompetitors; finished_car[j + 15] = teamorder[j + 7])
      //  ++j;
    }
  } else {
    racers = iActualCompetitors;
    for (iAISearch = 0; iAISearch < 6 * iActualCompetitors; grid[iSecondSwapPos] = iTempCarId)// Shuffle grid positions randomly for non-championship races
    {
      iRandRange = rand();
      //iFirstSwapPos = iRandRange % iActualCompetitors;  // Get random position within grid bounds
      iFirstSwapPos = GetHighOrderRand(iActualCompetitors, iRandRange);
      //iFirstSwapPos = (iActualCompetitors * iRandRange - (__CFSHL__((iActualCompetitors * iRandRange) >> 31, 15) + ((iActualCompetitors * iRandRange) >> 31 << 15))) >> 15;
      iSecondRand = rand();
      //iSecondSwapPos = iSecondRand % iActualCompetitors;  // Get second random position within grid bounds
      iSecondSwapPos = GetHighOrderRand(iActualCompetitors, iSecondRand);
      //iSecondSwapPos = (iActualCompetitors * iSecondRand - (__CFSHL__((iActualCompetitors * iSecondRand) >> 31, 15) + ((iActualCompetitors * iSecondRand) >> 31 << 15))) >> 15;
      iTempCarId = grid[iFirstSwapPos];
      grid[iFirstSwapPos] = grid[iSecondSwapPos];
      ++iAISearch;
    }
    iActualCompetitors = racers;
    for (k = 0; k < players; ++k)             // Position human players based on difficulty level
    {                                           // Calculate starting position handicap based on level
      if (level && (cheat_mode & 2) == 0)
        iHandicapPos = iActualCompetitors - 2 * level * players;
      else
        iHandicapPos = iActualCompetitors - players;
      if (iHandicapPos < 0)
        iHandicapPos = 0;
      iHumanPos = 0;
      for (iOrderLoop = 0; !human_control[grid[iOrderLoop]]; ++iOrderLoop)// Find human player in grid and ensure proper positioning
        ++iHumanPos;
      if (iHumanPos < iHandicapPos) {
        for (m = iHandicapPos; ; ++m) {
          iAICarId = grid[m];
          if (!human_control[iAICarId])
            break;
          ++iHandicapPos;
        }
        iHumanCarId = grid[iHumanPos];
        grid[iHumanPos] = iAICarId;
        grid[m] = iHumanCarId;
      }
    }
  }
  StartPressed = 0;                             // Finalize restart state
  restart_net = 0;
  racers = iActualCompetitors;
}

//-------------------------------------------------------------------------------------------------
