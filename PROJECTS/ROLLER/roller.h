#ifndef _ROLLER_ROLLER_H
#define _ROLLER_ROLLER_H
//-------------------------------------------------------------------------------------------------
#include "types.h"
#include "sound.h"
#include <stdio.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_gamepad.h>
//-------------------------------------------------------------------------------------------------

extern SDL_Mutex *g_pDigiMutex;
extern bool g_bPaletteSet;
extern bool g_bForceMaxDraw;
extern bool g_bNoCollisionLimit;
extern bool g_bAirborneCollisions;
extern bool g_bAINoCheatStart;
extern bool g_bFixCarMenuBug;
extern bool g_bImprovedJumpLanding;
extern bool g_bNoclip;
extern int   g_iTextureFilter;   /* 0=nearest, 1=bilinear, 2=anisotropic */
extern int   g_iAnisotropyLevel; /* 0=2x, 1=4x, 2=8x, 3=16x */
extern bool  g_bTrilinear;       /* true = blend linearly between mip levels */
extern float g_fLodBias;         /* mip LOD bias; 0 = neutral */
extern float g_fRenderScale;     /* render resolution multiplier; 1.0 = native */
extern int   g_iAntiAliasing;    /* 0=off, 1=MSAA 2x, 2=MSAA 4x, 3=MSAA 8x */
extern bool  g_bVsync;           /* true = vsync on */
extern int   g_iFpsDisplay;      /* 0=off, 1=top-left, 2=top-right, 3=bottom-left, 4=bottom-right */
extern float    g_fFogDensity;   /* exponential-squared fog coefficient; 0.0 = off */
extern float    g_fGamma;        /* output gamma; 1.0 = neutral */
extern float    g_fFogStart;     /* view-space depth before which fog is suppressed */
extern uint32_t g_uFogColor;     /* fog RGB as 0xRRGGBB; default 0xB3BFCC */
extern float g_fSaturation;      /* colour saturation; 1.0 = neutral */
extern float g_fContrast;        /* contrast; 1.0 = neutral */
extern float g_fVigStrength;     /* vignette strength; 0.0 = off */
extern float g_fBrightness;      /* additive brightness; 0.0 = neutral */
extern float g_fFovMultiplier;   /* FOV multiplier; 1.0 = native */
extern bool  g_bWireframe;       /* wireframe rendering */
extern bool  g_bCRTFilter;      /* CRT scanline + phosphor mask post-process */
extern bool g_bRepeat;
extern int g_iNumTracks;
extern int g_iCurrentSong;
extern SDL_AtomicInt iTicksPending;

//-------------------------------------------------------------------------------------------------

// GPU device accessors
SDL_GPUDevice *ROLLERGetGPUDevice(void);
SDL_Window *ROLLERGetWindow(void);

// Debug overlay accessor
struct DebugOverlay;
struct DebugOverlay *ROLLERGetDebugOverlay(void);
struct CRTFilter    *ROLLERGetCRTFilter(void);

// Menu renderer accessor
typedef struct MenuRenderer MenuRenderer;
MenuRenderer *GetMenuRenderer(void);
void SnapshotEnsureMenuRenderer(void);

// functions added by ROLLER
int InitSDL(char *data_root, const char *midi_root);
void InitFATDATA(const char *szDataRoot);
void InitREPLAYS(const char *szDataRoot);
void InitTRACKS(const char *szDataRoot);
int ROLLERAudioMusicAvailable(void);
void ShutdownSDL();
void UpdateSDL();
void UpdateSDLWindow();
bool ROLLERTryAcquireGPUSwapchainTexture(SDL_GPUCommandBuffer *pCmdBuf, SDL_Window *pWindow,
                                         SDL_GPUTexture **ppSwapchainTex,
                                         Uint32 *puiSwapchainW, Uint32 *puiSwapchainH);
bool ROLLERGpuPresentationSuspended(void);
void ROLLERGetPresentViewport(Uint32 uiTargetW, Uint32 uiTargetH,
                              float fContentAspect,
                              SDL_GPUViewport *pViewport);
void ROLLERRefreshStartupOverlay();

bool ROLLERfexists(const char *szFile);
const char *ROLLERfindpath(const char *szFile); // case-insensitive path resolution (no-op on Windows)
bool ROLLERdirexists(const char *szDir);
FILE *ROLLERfopen(const char *szFile, const char *szMode); //tries to open file with both all caps and all lower case
int ROLLERopen(const char *szFile, int iOpenFlags); //tries to open file with both all caps and all lower case
int ROLLERremove(const char *szFile); //tries to remove file with both all caps and all lower case
int ROLLERrename(const char *szOldName, const char *szNewName); //tries to rename file with both all caps and all lower case
uint32 ROLLERAddTimer(Uint32 uiFrequencyHz, SDL_NSTimerCallback callback, void *userdata);
void ROLLERRemoveTimer(uint32 uiHandle);
int ROLLERfilelength(const char *szFile);
void ROLLERsrand(unsigned int uiSeed);
int ROLLERrandRaw(void);
int ROLLERrand();
Uint64 SDLTickTimerCallback(void *userdata, SDL_TimerID timerID, Uint64 interval);
Uint64 SDLS7TimerCallback(void *userdata, SDL_TimerID timerID, Uint64 interval);
int IsCDROMDevice(const char *szPath);
void ReplaceExtension(char *szFilename, const char *szNewExt);
void ErrorBoxExit(const char *szErrorMsgFormat, ...);
void autoselectsoundlanguage();
int GetHighOrderRand(int iRange, int iRandValue);
int ReadUnalignedInt(const void *pData);
void ROLLERGetAudioInfo();
void ROLLERStopTrack();
void ROLLERPlayTrack(int iTrack);
void ROLLERPlayTrack4(int iStartTrack);
void ROLLERSetAudioVolume(int iVolume);
void UpdateAudioTracks();
void CleanupAudioCD();

//-------------------------------------------------------------------------------------------------
#endif
