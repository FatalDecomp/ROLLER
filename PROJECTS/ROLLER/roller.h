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
extern float g_fDrawDistanceFraction; /* 0.0 = normal draw distance, 1.0 = whole track (old "Infinite draw distance" checkbox) */
extern bool g_bNoCollisionLimit;
extern bool g_bAirborneCollisions;
extern bool g_bAINoCheatStart;
extern bool g_bFixCarMenuBug;
extern bool g_bImprovedJumpLanding;
extern bool g_bNoclip;
extern bool g_bShowCarOnExplosion; /* true = keep car mesh visible during explosion animation */
extern bool g_bFreeCamera; /* debug: hold RMB + move mouse to free-look in any view (see view.c chase_look_apply) */
extern int   g_iTextureFilter;   /* 0=nearest, 1=bilinear, 2=anisotropic */
extern int   g_iAnisotropyLevel; /* 0=2x, 1=4x, 2=8x, 3=16x */
extern bool  g_bTrilinear;       /* true = blend linearly between mip levels */
extern bool  g_bDisableMipmaps;  /* true = clamp sampler to mip 0 (debug) */
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
extern float g_fMirrorFov;       /* rearview/side mirror FOV multiplier; 0.75 = default (matches SW), <1 = zoom in, >1 = zoom out */
extern bool  g_bWireframe;       /* wireframe rendering */
extern int   g_iCullMode;        /* 0=default, 1=none, 2=back, 3=front — debug only, not saved */
extern bool  g_bCRTFilter;      /* CRT scanline + phosphor mask post-process */
extern bool  g_bSignsOnTop;     /* signs use COMPARE_ALWAYS depth (always on top); false = depth-tested */
extern bool  g_bSurfaceDebugViz; /* draw surface-type + flag labels on each quad */
extern bool  g_bSurfaceLog;      /* enable pair UV SDL_Log (not saved to INI) */
extern int   g_iSurfaceLogId;    /* -2=disabled(empty), -1=all, >=0=specific surfIdx (not saved) */
extern bool  g_bRenderStatsLog;  /* enable periodic drawcmd/vertex-count SDL_Log (not saved to INI) */
extern bool  g_bPickTextures;    /* debug: enable right-click surface pick (see "Pick Textures" checkbox) */
extern bool  g_bPickTexturesPNG; /* debug: also save the picked tile/pair atlas region as a PNG next to the exe (see "Pick Textures as PNG" checkbox) */
extern bool  g_pendingClickQuery; /* right-click surface pick: set by event loop, cleared after render */
extern float g_clickQueryNX;     /* normalised [0,1] click X within game viewport */
extern float g_clickQueryNY;     /* normalised [0,1] click Y within game viewport */
extern bool  g_bChaseLookRightDown; /* debug free-look (view.c): RMB currently held */
extern float g_fChaseLookAccumX; /* debug free-look: accumulated raw xrel since last consumed */
extern float g_fChaseLookAccumY; /* debug free-look: accumulated raw yrel since last consumed */
extern bool  g_bKeepWindowSize; /* persist window size to ROLLER.INI */
extern bool g_bRepeat;
extern int g_iNumTracks;
extern int g_iCurrentSong;
extern SDL_AtomicInt iTicksPending;
extern bool g_bShiftFrozen;        /* true while SHIFT is held with freeze enabled — blocks all tick steps */
extern bool g_bShiftFreezeEnabled; /* debug overlay checkbox: enable hold-SHIFT-to-freeze */
extern int  g_iFpsBackground;      /* background FPS cap: 0=off, else the target fps (15/30/60) */

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
