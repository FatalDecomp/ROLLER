#ifndef _ROLLER_FUNC3_H
#define _ROLLER_FUNC3_H
//-------------------------------------------------------------------------------------------------
#include "types.h"
//-------------------------------------------------------------------------------------------------

typedef struct
{
  int iSlotUsed;
  int iPackedTrack;
  int iDifficulty;
  int iPlayerType;
  int iUnused1;
  int iUnused2;
} tSaveStatus;

//-------------------------------------------------------------------------------------------------

extern char save_slots[4][13];
extern int credit_order[25];
extern char round_pals[8][13];
extern char round_pics[8][13];
extern char send_buffer[32];
extern int send_message_to;
extern int rec_status;
extern char rec_mes_buf[32];
extern char send_mes_buf[32];
extern tSaveStatus save_status[4];
extern int result_lap[16];
extern int result_order[16];
extern float result_time[16];
extern int result_design[16];
extern float result_best[16];
extern int result_competing[16];
extern int result_control[16];
extern int result_lives[16];
extern int result_kills[16];
extern int send_status;
extern char rec_mes_name[12];
extern int restart_net;
extern float BestTime;
extern int result_p1;
extern int result_p2;
extern int result_p2_pos;
extern int result_p1_pos;

//-------------------------------------------------------------------------------------------------

typedef struct
{
  int iWidth;
  int iHeight;
  int iDataOffset;
} tBlockHeader;

//-------------------------------------------------------------------------------------------------

int winner_screen(int carDesign, char byFlags);
void StoreResult();
void RaceResult();
void TimeTrials(int iDriverIdx);
void ChampionshipStandings();
void TeamStandings();
void ShowLapRecords();
void show_3dmap(float fZ, int iElevation, int iYaw);
void DrawCar(uint8 *pScrBuf, int iCarDesignIndex, float fDistance, int iAngle, char byAnimFrame);
void championship_winner();
void print_mem_used(const char *szMsg);
uint8 *try_load_picture(const char *szFile);
void save_champ(int iSlot);
int load_champ(int iSlot);
uint8 *lod_champ_char(uint8 *pSrc, int *piValue);
uint8 *sav_champ_char(uint8 *pSrc, int *piValue); //actually loads an int
uint8 *sav_champ_int(uint8 *pDest, int iValue);
void check_saves();
void ResultRoundUp();
void RollCredits();
void ChampionshipOver();
void EndChampSequence();
void network_fucked();
void no_cd();
int name_cmp(char *szName1, char *szName2);
void name_copy(char *szDest, char *szSrc);
void loadtracksample(int track_number);
void front_letter(tBlockHeader *pFont, uint8 byCharIdx, int *iX, int *iY, const char *szStr, uint8 byColorReplace);
void scale_letter(tBlockHeader *pFont, uint8 byChar, int *iCursorX, int *iCursorY, char *mappingTable, char byColorReplace, int iScaleSize);
void front_text(tBlockHeader *pFont,
                const char *szText,
                const uint8 *mappingTable,
                int *pCharVOffsets,
                int iX,
                int iY,
                uint8 byColorReplace,
                int iAlignment);
void scale_text(tBlockHeader *pFont,
                char *szText,
                const char *mappingTable,
                int *pCharVOffsets,
                int iX,
                int iY,
                char byColorReplace,
                unsigned int uiAlignment,
                int iClipLeft,
                int iClipRight);
void clear_screen(void *pDest, int iVal);
void display_picture(void *pDest, const void *pSrc);
void display_block(uint8 *pDest, tBlockHeader *pSrc, int iBlockIdx, int iX, int iY, int iTransparentColor);
uint8 *load_picture(const char *szFile);
void AllocateCars();
void check_cars();
void select_messages();
void show_received_mesage();
int select_netslot();

//-------------------------------------------------------------------------------------------------
#endif
