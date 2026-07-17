#ifndef _ROLLER_REPLAY_H
#define _ROLLER_REPLAY_H
//-------------------------------------------------------------------------------------------------
#include "types.h"
#include "func3.h"
#include "frontend.h"
#include <stdio.h>
#include <stdlib.h>
//-------------------------------------------------------------------------------------------------

#define REPLAY_SPEED_MIN      -8192
#define REPLAY_SPEED_MAX      8192
#define REPLAY_NORMAL_SPEED   256

//-------------------------------------------------------------------------------------------------

#pragma pack(push, 1)
typedef struct
{
  uint8 byView;
  uint8 byCarIdx;
  int iFrame;
} tReplayCamera;
#pragma pack(pop)

//-------------------------------------------------------------------------------------------------

typedef struct
{
  int16 nX;
  int16 nY;
  void *pFunc;
} tRIcon;

//-------------------------------------------------------------------------------------------------

#pragma pack(push, 1)
typedef struct
{
  int iPackedPosX;
  int iPackedPosY;
  int iPackedPosZ;
  int16 nCurrChunk;
  int16 nRollPacked;
  int16 nPitchPacked;
  int16 nDesiredYaw;
  int16 nActualYaw;
  int16 nSpeedAndStatus;
  uint8 byRPMPacked;
  uint8 byHealthAndStunned;
  char byMiscCarData;
  char byLap;
  char byDeathTimer;
  uint8 byDamageIntensity;
} tReplayData;
#pragma pack(pop)

//-------------------------------------------------------------------------------------------------

extern int disciconpressed;
extern int rotpoint;
extern int replaypanel;
extern int controlicon;
extern int replayspeeds[9];
extern char *replayname[9];
extern int lastintro;
extern int filingmenu;
extern int filefile;
extern int filefiles;
extern int replaysetspeed;
extern int replaydirection;
extern int lastfile;
extern int lastautocut;
extern int pend_view_init;
extern int replayedit;
extern char replayfilename[32];
extern char *views[8];
extern int loading_replay;
extern tRIcon ricon[26];
extern char *replayhelp[26];
extern int lsdsel;
extern tPoint rrotate[8];
extern tReplayCamera camera[100];
extern int disabled[4096];
extern char temp_names[16][9];
extern char newrepsample[16];
extern char repsample[16];
extern char repvolume[16];
extern int topfile;
extern char filename[500][9];
extern int oldtrack;
extern int oldtextures;
extern int replayheader;
extern FILE *replayfile;
extern int replayspeed;
extern int oldcars;
extern int replayframes;
extern int discfull;
extern int replayblock;
extern int currentreplayframe;
extern int lastreplayframe;
extern int introfiles;
extern int newreplayframe;
extern int replayselect;
extern int slowing;
extern int rewinding;
extern int forwarding;
extern int replaystart;
extern int cuts;
extern char selectfilename[30];
extern char rememberfilename[34];

//-------------------------------------------------------------------------------------------------;

void setreplaytrack();
void startreplay();
void stopreplay();
void DoReplayData();
void Rplay();
void Rreverseplay();
void Rframeplus();
void Rframeminus();
void Rspeedplus();
void Rspeedminus();
void DoRstop();
void Rrewindstart();
void Rforwardstart();
void ROldStatus();
void Rstart();
void Rend();
unsigned int readdisable(int iFrame);
void cleardisable(int iFrame);
void setdisable(int iFrame);
void findnextvalid();
void findlastvalid();
void Rassemble();
void storecut();
void removecut();
void displayreplay();
int compare(const void *szStr1, const void *szStr2);
void warning(int iX1, int iY1, int iX2, int iY2, char *szWarning);
void lsd(int iX1, int iY1, int iX2, int iY2);
void scandirectory(const char *szPattern);
void fileselect(int iBoxX0, int iBoxY0, int iBoxX1, int iBoxY1, int iTextX, int iTextY, const char *szText, const char *szPattern, int iFileIdx);
void previouscut();
void nextcut();
void loadreplay();
void savereplay();
void deletereplay();
void findintrofiles();
void displaycontrolpanel();
void rtoggleedit();
void rstartblock();
void rselectblock();
void rdeleteblock();
void rstoreview();
void rremoveview();
void rpreviouscut();
void rnextcut();
void rstartassemble();
void replayicon(uint8 *pDest, tBlockHeader *pBlockHeader, int iBlockIdx, int iX, int iY, int iScreenWidth, int byTransparentColor);
void replaypanelletter(char c, int *piX, int *piY, int iScreenWidth);
void replaypanelstring(const char *szStr, int iX, int iY, int iScreenWidth);
void displaypaneltime(int iTime, int iX, int iY, int iScreenWidth);
void discmenu();
void replay_control_panel_update_mouse_state(void);
int replay_control_panel_mouse_held_icon(void);
#if defined(IS_WASM)
void replay_web_name_entry_complete(const char *szValue, int iAccepted);
#endif
void initsoundlag(uint32 uiTicks);
void resetsmoke();

//-------------------------------------------------------------------------------------------------
#endif
