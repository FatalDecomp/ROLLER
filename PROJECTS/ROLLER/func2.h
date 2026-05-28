#ifndef _ROLLER_FUNC2_H
#define _ROLLER_FUNC2_H
//-------------------------------------------------------------------------------------------------
#include "types.h"
#include "func3.h"
//-------------------------------------------------------------------------------------------------

typedef enum
{
  USERKEY_P1LEFT = 0,
  USERKEY_P1RIGHT = 1,
  USERKEY_P1UP = 2,
  USERKEY_P1DOWN = 3,
  USERKEY_P1UPGEAR = 4,
  USERKEY_P1DOWNGEAR = 5,
  USERKEY_P2LEFT = 6,
  USERKEY_P2RIGHT = 7,
  USERKEY_P2UP = 8,
  USERKEY_P2DOWN = 9,
  USERKEY_P2UPGEAR = 10,
  USERKEY_P2DOWNGEAR = 11,
  USERKEY_P1CHEAT = 12,
  USERKEY_P2CHEAT = 13
} eUserKey;

//-------------------------------------------------------------------------------------------------

extern int dam_remap[256];
extern int write_key;
extern int read_key;
extern uint8 mapping[];
extern int twoparter;
extern int font6_offsets[88];
extern char font6_ascii[256];
extern char ascii_conv3[256];
extern char *keyname[140];
extern int userkey[14];
extern uint8 key_buffer[];
extern int new_zoom[2];
extern char config_buffer[8192];
extern char language_buffer[8192];
extern int zoom_x;
extern int zoom_y;
extern int no_mem;
extern uint8 *screen_pointer;
extern int lots_of_mem;

//-------------------------------------------------------------------------------------------------

void draw_smoke(uint8 *pScrBuf, int iPlayerCarIdx);
void test_panel(uint8 *pScrBuf, int iPlayerCarIdx);
void ZoomString(const char *szStr, const char *mappingTable, tBlockHeader *pBlockHeader, int iPlayerIdx, int *pCharVOffsets);
void ZoomSub(const char *szText, const char *mappingTable, tBlockHeader *pBlockHeader, int iPlayerIdx, int *pCharVOffsets);
void zoom_letter(tBlockHeader *pBlockHeader, uint8 byCharCode, int *puiXPos, int *puiYPos, const char *mappingTable, float fZoomFactor);
void print_block(uint8 *pDest, tBlockHeader *pBlockHeader, int iBlockIdx);
void print_damage(uint8 *pDest, tBlockHeader *pBlockHeader, int iCarIdx);
void print_pos(int iX, int iY, int iDriverIdx);
void free_game_memory();
int readmode();
void key_handler(uint8 byScancode);
void claim_key_int();
int fatkbhit();
int fatgetch();
int control_key_matches_required_pair_type(int iControlIdx, int iKey);
int control_key_is_duplicate_in_player_set(int iControlIdx, int iKey);
void release_key_int();
void clear_border(int x, int y, int iWidth, int iLines);
void setdirectory(const char *szAppPath);
void FindShades();
int nearest_colour(int iR, int iB, int iG);
void select_view(int iPlayer);
void mini_prt_string(tBlockHeader *pBlockHeader, const char *szStr, int iX, int iY);
void mini_prt_string_rev(tBlockHeader *pBlockHeader, const char *szText, int iX, int iY);
void mini_prt_right(tBlockHeader *pBlockHeader, const char *szText, int iX, int iY);
void mini_prt_centre(tBlockHeader *pBlockHeader, const char *szStr, int iX, int iY);
void prt_right(tBlockHeader *pBlockHeader, const char *szStr, int iX, int iY);
void prt_string(tBlockHeader *pBlockHeader, const char *szStr, int iX, int iY);
void prt_letter(tBlockHeader *pBlockHeader, char byChar, int *piXPos, int *piYPos, int iFontType);
void prt_letter_rev(tBlockHeader *pBlockHeader, char byChar, int *piXPos, int *piYPos, int iFontType);
void prt_stringcol(tBlockHeader *pBlockHeader, const char *szStr, int iX, int iY, uint8 byColor);
void prt_rightcol(tBlockHeader *pBlockHeader, const char *szStr, int iX, int iY, uint8 byColor);
void prt_centrecol(tBlockHeader *pBlockHeader, const char *szStr, int iX, int iY, uint8 byColor);
void prt_lettercol(tBlockHeader *pBlockHeader, char byChar, int *piXPos, int *piYPos, uint8 byColor);
void display_paused();
void enable_keyboard();
void disable_keyboard();
void save_fatal_config();
void load_fatal_config();
void getconfigvalue(const char *szConfigText, const char *szVarName, int *piOutVal, int iMin, int iMax);
void getconfigvalueuc(const char *szConfigText, const char *szVarName, uint8 *pbyOutVal, int iMin, int iMax);
void displaycalibrationbar(int iX, int iY, int iValue);
void blankwindow(int iX1, int iY1, int iX2, int iY2);
void volumebar(int iX, int iVolume);
void LoadRecords();
void SaveRecords();
uint8 *copy_int(uint8 *pDest, uint32 uiValue);
void ShowATime(float fTime, int iX, int iY);
void setmodex();
void copyscreenmodex(uint8 *pSrc, uint8 *pDest);
void start_zoom(const char *szStr, int iPlayerIdx);
void small_zoom(const char *szStr);
void subzoom(const char *szStr);
void make_time(char *szTimeStr, float fTime);
void check_machine_speed();
void load_language_file(char *szFilename, int iUseConfigBuffer);
void do_blip(int iCarIdx);

//-------------------------------------------------------------------------------------------------
#endif
