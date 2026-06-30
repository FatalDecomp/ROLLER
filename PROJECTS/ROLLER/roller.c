#include "roller.h"
#include "scene_render_gpu.h"
#include "crt_filter.h"
#include "rollersound.h"
#include "rollerinput.h"
#include "3d.h"
#include "sound.h"
#include "frontend.h"
#include "func2.h"
#include "graphics.h"
#include "menu_render.h"
#include "moving.h"
#include "debug_overlay.h"
#include "snapshot.h"
#include "rollercd.h"
#include "view.h"
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#if !defined(IS_ANDROID)
#include <SDL3_image/SDL_image.h>
#endif
#if defined(IS_ANDROID)
#include <jni.h>
#include <SDL3/SDL_system.h>
#endif
#if !defined(IS_ANDROID)
#include <wildmidi_lib.h>
#endif
#include <fcntl.h>
#include <sys/stat.h>
#if !defined(IS_ANDROID)
#include <cdio/cdio.h>
#include <cdio/iso9660.h>
#include <cdio/disc.h>
#include <cdio/cd_types.h>
#endif
#ifdef IS_WINDOWS
#include <io.h>
#include <direct.h>
#include <windows.h>
#include <mmsystem.h>
#include <digitalv.h>
#pragma comment(lib, "winmm.lib")
#define chdir _chdir
#define open _open
#define close _close
#define S_ISDIR(mode) (((mode) & _S_IFMT) == _S_IFDIR)
#else
#include <stdlib.h>
#include <inttypes.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/ioctl.h>
#define O_BINARY 0 //linux does not differentiate between text and binary
#endif
#if defined(IS_ANDROID)
#define CDROM_SUPPORT 0
#elif defined(IS_LINUX)
#include <linux/cdrom.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#define CDROM_SUPPORT 1
#else
#define CDROM_SUPPORT 0
#endif
//-------------------------------------------------------------------------------------------------

#define ROLLER_DIALOG_MAX_FILES 32

typedef struct
{
  uint32 uiHandle;
  uint64 ullTargetSDLTicksNS;
  uint64 ullLastSDLTicksNS;
  uint64 ullCurrSDLTicksNS;
} tTimerData;

typedef struct
{
  char szPath[ROLLER_MAX_PATH];
  char aszPaths[ROLLER_DIALOG_MAX_FILES][ROLLER_MAX_PATH];
  int iNumPaths;
  bool bFolder;
  bool bDone;
  bool bCancelled;
} tDialogResult;

//-------------------------------------------------------------------------------------------------

static SDL_Window *s_pWindow = NULL;
static SDL_GPUDevice *s_pGPUDevice = NULL;
static SDL_GPUTexture *s_pGameTexture = NULL;
static SDL_GPUTransferBuffer *s_pTransferBuffer = NULL;
static int s_iGPUPresentSkipFrames = 0;
/* Hard-disables GPU swapchain presentation while set. Used on Android during the
 * InitFATDATA import flow, where showing native dialogs (message box + SAF file
 * picker) destroys/recreates the Activity surface; driving the SDL Vulkan renderer
 * against the in-flux surface corrupts the Adreno driver (libvulkan crash /
 * destroyed-mutex abort on the HWUI RenderThread). */
static bool s_bGpuPresentDisabled = false;
bool g_bPaletteSet = false;
bool g_bForceMaxDraw = true;
bool g_bNoCollisionLimit = true;
bool g_bAirborneCollisions = false;
bool g_bAINoCheatStart = false;  //  Set true to not give AI cars an advantage during race start
bool g_bFixCarMenuBug = true;
bool g_bImprovedJumpLanding = true;
bool g_bNoclip = false;
int   g_iTextureFilter   = 0;
int   g_iAnisotropyLevel = 3;     /* default 16x */
bool  g_bTrilinear       = false;
float g_fLodBias         = 0.0f;
float g_fRenderScale     = 1.0f;
int   g_iAntiAliasing    = 0;     /* 0=off, 1=2x, 2=4x, 3=8x */
bool  g_bVsync           = true;  /* SDL3 default is vsync on */
int   g_iFpsDisplay      = 0;     /* 0=off, 1..4=corner position */
float    g_fFogDensity   = 0.0f;
float    g_fGamma        = 1.0f;
float    g_fFogStart     = 0.0f;
uint32_t g_uFogColor     = 0xB3BFCCu;  /* matches fogColor[] default {0.70, 0.75, 0.80} */
float g_fSaturation      = 1.0f;
float g_fContrast        = 1.0f;
float g_fVigStrength     = 0.0f;
float g_fBrightness      = 0.0f;
float g_fFovMultiplier   = 1.0f;
bool  g_bWireframe       = false;
int   g_iCullMode        = 0;
bool  g_bCRTFilter       = false;
bool  g_bSignsOnTop      = false;
bool  g_bSurfaceDebugViz = false;
bool  g_bSurfaceLog      = false;
int   g_iSurfaceLogId    = -2;
bool  g_bTexUVMap        = false;
bool  g_pendingClickQuery = false;
float g_clickQueryNX      = 0.0f;
float g_clickQueryNY      = 0.0f;
bool  g_bKeepWindowSize  = false;
int g_iCurrentSong = 0;
uint64 g_ullTimer150Ms = 0;

#if defined(IS_ANDROID)
#define ROLLER_RESIZE_DEFER_FRAMES 6
#else
#define ROLLER_RESIZE_DEFER_FRAMES 2
#endif

#define ROLLER_PRESENT_ASPECT ((float)640.0f / (float)400.0f)

SDL_GPUDevice *ROLLERGetGPUDevice(void) { return s_pGPUDevice; }
SDL_Window *ROLLERGetWindow(void) { return s_pWindow; }

static MenuRenderer *s_pMenuRenderer = NULL;
MenuRenderer *GetMenuRenderer(void) { return s_pMenuRenderer; }

void SnapshotEnsureMenuRenderer(void)
{
  if (!s_pMenuRenderer)
    s_pMenuRenderer = menu_render_create(NULL, NULL);
}

static DebugOverlay *s_pDebugOverlay = NULL;
static CRTFilter    *s_pCRTFilter    = NULL;
DebugOverlay *ROLLERGetDebugOverlay(void) { return s_pDebugOverlay; }
CRTFilter    *ROLLERGetCRTFilter(void)    { return s_pCRTFilter; }

/* Deferred SHIFT key press — held until we know whether TAB follows (SHIFT+TAB
 * toggles split screen) or SHIFT is released alone (fires normally). */
static SDL_Event s_pendingShiftDown;
/* Timestamp (ms) when the current SHIFT press began; 0 = not held. */
static uint64 s_shiftPressedMs = 0;
/* Next frame deadline (ms) for the background FPS cap; 0 = not active. */
static uint64 s_nextPauseFrameMs = 0;
bool g_bShiftFrozen = false;
bool g_bShiftFreezeEnabled = false;
int  g_iFpsBackground = 0; /* 0=off, else target fps (15/30/60) */

static void UpdateMouseCursorVisibility(void)
{
  if (!s_pWindow)
    return;

  SDL_WindowFlags uiFlags = SDL_GetWindowFlags(s_pWindow);
  bool bFullscreen = (uiFlags & SDL_WINDOW_FULLSCREEN) != 0;
  bool bCursorScreen = frontend_on || game_req || replaytype == 2 ||
                       debug_overlay_visible(s_pDebugOverlay);
  bool bHideCursor = bFullscreen && !bCursorScreen;
  bool bCursorVisible = SDL_CursorVisible();

  if (bCursorVisible == !bHideCursor)
    return;

  if (bHideCursor) {
    if (!SDL_HideCursor())
      SDL_Log("SDL_HideCursor failed: %s", SDL_GetError());
  } else {
    if (!SDL_ShowCursor())
      SDL_Log("SDL_ShowCursor failed: %s", SDL_GetError());
  }
}

static void DeferGPUPresentation(int iFrames)
{
  if (s_iGPUPresentSkipFrames < iFrames)
    s_iGPUPresentSkipFrames = iFrames;
}

bool ROLLERGpuPresentationSuspended(void)
{
  if (s_bGpuPresentDisabled)
    return true;

  if (!s_pWindow)
    return true;

  if (s_iGPUPresentSkipFrames > 0) {
    --s_iGPUPresentSkipFrames;
    return true;
  }

  SDL_WindowFlags uiFlags = SDL_GetWindowFlags(s_pWindow);
  if (uiFlags & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED))
    return true;

  int iPixelW = 0;
  int iPixelH = 0;
  if (!SDL_GetWindowSizeInPixels(s_pWindow, &iPixelW, &iPixelH) ||
      iPixelW <= 0 || iPixelH <= 0)
    return true;

  return false;
}

void ROLLERGetPresentViewport(Uint32 uiTargetW, Uint32 uiTargetH,
                              float fContentAspect,
                              SDL_GPUViewport *pViewport)
{
  int iWindowW = 0;
  int iWindowH = 0;
  float fTargetW = (float)uiTargetW;
  float fTargetH = (float)uiTargetH;
  float fAdjustedAspect = fContentAspect;
  float fTargetAspect;

  if (!pViewport)
    return;

  pViewport->x = 0.0f;
  pViewport->y = 0.0f;
  pViewport->w = fTargetW;
  pViewport->h = fTargetH;
  pViewport->min_depth = 0.0f;
  pViewport->max_depth = 1.0f;

  if (uiTargetW == 0 || uiTargetH == 0 || fContentAspect <= 0.0f)
    return;

  if (s_pWindow &&
      SDL_GetWindowSizeInPixels(s_pWindow, &iWindowW, &iWindowH) &&
      iWindowW > 0 && iWindowH > 0 &&
      (iWindowW != (int)uiTargetW || iWindowH != (int)uiTargetH)) {
    fAdjustedAspect = fContentAspect *
                      ((float)iWindowH * fTargetW) /
                      ((float)iWindowW * fTargetH);
  }

  if (fAdjustedAspect <= 0.0f)
    fAdjustedAspect = fContentAspect;

  fTargetAspect = fTargetW / fTargetH;
  if (fTargetAspect > fAdjustedAspect) {
    pViewport->h = fTargetH;
    pViewport->w = fAdjustedAspect * fTargetH;
    pViewport->x = (fTargetW - pViewport->w) * 0.5f;
  } else {
    pViewport->w = fTargetW;
    pViewport->h = fTargetW / fAdjustedAspect;
    pViewport->y = (fTargetH - pViewport->h) * 0.5f;
  }
}

SDL_Mutex *g_pTimerMutex = NULL;
tTimerData timerDataAy[MAX_TIMERS] = { 0 };
SDL_Mutex *g_pDigiMutex = NULL;

// Scancode translation table (SDL scancode -> PC set1 scancode)
uint8 sdl_to_set1[] = {
    [SDL_SCANCODE_ESCAPE] = WHIP_SCANCODE_ESCAPE,
    [SDL_SCANCODE_1] = WHIP_SCANCODE_1,
    [SDL_SCANCODE_2] = WHIP_SCANCODE_2,
    [SDL_SCANCODE_3] = WHIP_SCANCODE_3,
    [SDL_SCANCODE_4] = WHIP_SCANCODE_4,
    [SDL_SCANCODE_5] = WHIP_SCANCODE_5,
    [SDL_SCANCODE_6] = WHIP_SCANCODE_6,
    [SDL_SCANCODE_7] = WHIP_SCANCODE_7,
    [SDL_SCANCODE_8] = WHIP_SCANCODE_8,
    [SDL_SCANCODE_9] = WHIP_SCANCODE_9,
    [SDL_SCANCODE_0] = WHIP_SCANCODE_0,
    [SDL_SCANCODE_MINUS] = WHIP_SCANCODE_MINUS,
    [SDL_SCANCODE_EQUALS] = WHIP_SCANCODE_EQUALS,
    [SDL_SCANCODE_BACKSPACE] = WHIP_SCANCODE_BACKSPACE,
    [SDL_SCANCODE_TAB] = WHIP_SCANCODE_TAB,
    [SDL_SCANCODE_Q] = WHIP_SCANCODE_Q,
    [SDL_SCANCODE_W] = WHIP_SCANCODE_W,
    [SDL_SCANCODE_E] = WHIP_SCANCODE_E,
    [SDL_SCANCODE_R] = WHIP_SCANCODE_R,
    [SDL_SCANCODE_T] = WHIP_SCANCODE_T,
    [SDL_SCANCODE_Y] = WHIP_SCANCODE_Y,
    [SDL_SCANCODE_U] = WHIP_SCANCODE_U,
    [SDL_SCANCODE_I] = WHIP_SCANCODE_I,
    [SDL_SCANCODE_O] = WHIP_SCANCODE_O,
    [SDL_SCANCODE_P] = WHIP_SCANCODE_P,
    [SDL_SCANCODE_LEFTBRACKET] = WHIP_SCANCODE_LEFTBRACKET,
    [SDL_SCANCODE_RIGHTBRACKET] = WHIP_SCANCODE_RIGHTBRACKET,
    [SDL_SCANCODE_RETURN] = WHIP_SCANCODE_RETURN,
    [SDL_SCANCODE_LCTRL] = WHIP_SCANCODE_LCTRL,
    [SDL_SCANCODE_A] = WHIP_SCANCODE_A,
    [SDL_SCANCODE_S] = WHIP_SCANCODE_S,
    [SDL_SCANCODE_D] = WHIP_SCANCODE_D,
    [SDL_SCANCODE_F] = WHIP_SCANCODE_F,
    [SDL_SCANCODE_G] = WHIP_SCANCODE_G,
    [SDL_SCANCODE_H] = WHIP_SCANCODE_H,
    [SDL_SCANCODE_J] = WHIP_SCANCODE_J,
    [SDL_SCANCODE_K] = WHIP_SCANCODE_K,
    [SDL_SCANCODE_L] = WHIP_SCANCODE_L,
    [SDL_SCANCODE_SEMICOLON] = WHIP_SCANCODE_SEMICOLON,
    [SDL_SCANCODE_APOSTROPHE] = WHIP_SCANCODE_APOSTROPHE,
    [SDL_SCANCODE_GRAVE] = WHIP_SCANCODE_GRAVE,
    [SDL_SCANCODE_LSHIFT] = WHIP_SCANCODE_LSHIFT,
    [SDL_SCANCODE_BACKSLASH] = WHIP_SCANCODE_BACKSLASH,
    [SDL_SCANCODE_Z] = WHIP_SCANCODE_Z,
    [SDL_SCANCODE_X] = WHIP_SCANCODE_X,
    [SDL_SCANCODE_C] = WHIP_SCANCODE_C,
    [SDL_SCANCODE_V] = WHIP_SCANCODE_V,
    [SDL_SCANCODE_B] = WHIP_SCANCODE_B,
    [SDL_SCANCODE_N] = WHIP_SCANCODE_N,
    [SDL_SCANCODE_M] = WHIP_SCANCODE_M,
    [SDL_SCANCODE_COMMA] = WHIP_SCANCODE_COMMA,
    [SDL_SCANCODE_PERIOD] = WHIP_SCANCODE_PERIOD,
    [SDL_SCANCODE_SLASH] = WHIP_SCANCODE_SLASH,
    [SDL_SCANCODE_RSHIFT] = WHIP_SCANCODE_RSHIFT,
    [SDL_SCANCODE_KP_MULTIPLY] = WHIP_SCANCODE_KP_MULTIPLY,
    [SDL_SCANCODE_LALT] = WHIP_SCANCODE_LALT,
    [SDL_SCANCODE_SPACE] = WHIP_SCANCODE_SPACE,
    [SDL_SCANCODE_CAPSLOCK] = WHIP_SCANCODE_CAPSLOCK,
    [SDL_SCANCODE_F1] = WHIP_SCANCODE_F1,
    [SDL_SCANCODE_F2] = WHIP_SCANCODE_F2,
    [SDL_SCANCODE_F3] = WHIP_SCANCODE_F3,
    [SDL_SCANCODE_F4] = WHIP_SCANCODE_F4,
    [SDL_SCANCODE_F5] = WHIP_SCANCODE_F5,
    [SDL_SCANCODE_F6] = WHIP_SCANCODE_F6,
    [SDL_SCANCODE_F7] = WHIP_SCANCODE_F7,
    [SDL_SCANCODE_F8] = WHIP_SCANCODE_F8,
    [SDL_SCANCODE_F9] = WHIP_SCANCODE_F9,
    [SDL_SCANCODE_F10] = WHIP_SCANCODE_F10,
    [SDL_SCANCODE_F11] = WHIP_MAPPED_F11,
    [SDL_SCANCODE_F12] = WHIP_MAPPED_F12,
    [SDL_SCANCODE_KP_7] = WHIP_SCANCODE_KP_7,
    [SDL_SCANCODE_KP_8] = WHIP_SCANCODE_KP_8,
    [SDL_SCANCODE_KP_9] = WHIP_SCANCODE_KP_9,
    [SDL_SCANCODE_KP_MINUS] = WHIP_SCANCODE_KP_MINUS,
    [SDL_SCANCODE_KP_4] = WHIP_SCANCODE_KP_4,
    [SDL_SCANCODE_KP_5] = WHIP_SCANCODE_KP_5,
    [SDL_SCANCODE_KP_6] = WHIP_SCANCODE_KP_6,
    [SDL_SCANCODE_KP_PLUS] = WHIP_SCANCODE_KP_PLUS,
    [SDL_SCANCODE_KP_1] = WHIP_SCANCODE_KP_1,
    [SDL_SCANCODE_KP_2] = WHIP_SCANCODE_KP_2,
    [SDL_SCANCODE_KP_3] = WHIP_SCANCODE_KP_3,
    [SDL_SCANCODE_KP_0] = WHIP_SCANCODE_KP_0,
    [SDL_SCANCODE_KP_PERIOD] = WHIP_SCANCODE_KP_PERIOD,
    [SDL_SCANCODE_RIGHT] = WHIP_SCANCODE_RIGHT,
    [SDL_SCANCODE_LEFT] = WHIP_SCANCODE_LEFT,
    [SDL_SCANCODE_DOWN] = WHIP_SCANCODE_DOWN,
    [SDL_SCANCODE_UP] = WHIP_SCANCODE_UP,
};

//-------------------------------------------------------------------------------------------------

static void ConvertIndexedToRGBA(const uint8 *pIndexed, const tColor *pPalette,
                                  uint8 *pRGBA, int width, int height)
{
  if (!pIndexed || !pPalette || !pRGBA) return;

  for (int i = 0; i < width * height; ++i) {
    const tColor *c = &pPalette[pIndexed[i]];
    pRGBA[i * 4 + 0] = (c->byR * 255) / 63;
    pRGBA[i * 4 + 1] = (c->byG * 255) / 63;
    pRGBA[i * 4 + 2] = (c->byB * 255) / 63;
    pRGBA[i * 4 + 3] = 255;
  }
}

//-------------------------------------------------------------------------------------------------

bool ROLLERTryAcquireGPUSwapchainTexture(SDL_GPUCommandBuffer *pCmdBuf, SDL_Window *pWindow,
                                         SDL_GPUTexture **ppSwapchainTex,
                                         Uint32 *puiSwapchainW, Uint32 *puiSwapchainH)
{
  if (ppSwapchainTex)
    *ppSwapchainTex = NULL;
  if (!pCmdBuf || !pWindow)
    return false;

  return SDL_AcquireGPUSwapchainTexture(pCmdBuf, pWindow, ppSwapchainTex,
                                        puiSwapchainW, puiSwapchainH);
}

//-------------------------------------------------------------------------------------------------

void UpdateSDLWindow()
{
  if (g_bSnapshotMode) {
    SnapshotPresent();
    return;
  }

  if (!g_bPaletteSet) return;
  if (ROLLERGpuPresentationSuspended()) return;

  // Acquire command buffer
  SDL_GPUCommandBuffer *cmdBuf = SDL_AcquireGPUCommandBuffer(s_pGPUDevice);
  if (!cmdBuf) return;

  // Convert indexed framebuffer directly into mapped transfer buffer.
  // cycle=true: never blocks; SDL3 creates a new slot if the previous one is
  // still in flight, rather than waiting for GPU completion.
  void *mapped = SDL_MapGPUTransferBuffer(s_pGPUDevice, s_pTransferBuffer, true);
  ConvertIndexedToRGBA(scrbuf, pal_addr, (uint8 *)mapped, winw, winh);
  SDL_UnmapGPUTransferBuffer(s_pGPUDevice, s_pTransferBuffer);

  SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(cmdBuf);

  SDL_GPUTextureTransferInfo src = {0};
  src.transfer_buffer = s_pTransferBuffer;

  SDL_GPUTextureRegion dstRegion = {0};
  dstRegion.texture = s_pGameTexture;
  dstRegion.w = winw;
  dstRegion.h = winh;
  dstRegion.d = 1;

  SDL_UploadToGPUTexture(copyPass, &src, &dstRegion, false);
  SDL_EndGPUCopyPass(copyPass);

  // Acquire swapchain and blit
  SDL_GPUTexture *swapchainTex;
  Uint32 swW, swH;
  if (!ROLLERTryAcquireGPUSwapchainTexture(cmdBuf, s_pWindow,
          &swapchainTex, &swW, &swH) || !swapchainTex) {
    SDL_CancelGPUCommandBuffer(cmdBuf);
    return;
  }

  // Blit with aspect-ratio preservation
  SDL_GPUBlitInfo blitInfo = {0};
  SDL_GPUViewport viewport = {0};
  blitInfo.source.texture = s_pGameTexture;
  blitInfo.source.w = winw;
  blitInfo.source.h = winh;

  ROLLERGetPresentViewport(swW, swH, ROLLER_PRESENT_ASPECT, &viewport);
  blitInfo.destination.texture = swapchainTex;
  blitInfo.destination.x = (Uint32)(viewport.x + 0.5f);
  blitInfo.destination.y = (Uint32)(viewport.y + 0.5f);
  blitInfo.destination.w = (Uint32)(viewport.w + 0.5f);
  blitInfo.destination.h = (Uint32)(viewport.h + 0.5f);
  if (blitInfo.destination.x >= swW)
    blitInfo.destination.x = 0;
  if (blitInfo.destination.y >= swH)
    blitInfo.destination.y = 0;
  if (blitInfo.destination.w < 1)
    blitInfo.destination.w = 1;
  if (blitInfo.destination.h < 1)
    blitInfo.destination.h = 1;
  if (blitInfo.destination.x + blitInfo.destination.w > swW)
    blitInfo.destination.w = swW - blitInfo.destination.x;
  if (blitInfo.destination.y + blitInfo.destination.h > swH)
    blitInfo.destination.h = swH - blitInfo.destination.y;
  blitInfo.filter = SDL_GPU_FILTER_NEAREST;
  blitInfo.load_op = SDL_GPU_LOADOP_CLEAR;
  blitInfo.clear_color = (SDL_FColor){0.0f, 0.0f, 0.0f, 1.0f};

  if (g_bCRTFilter && s_pCRTFilter) {
    crt_filter_apply(s_pCRTFilter, cmdBuf, s_pGameTexture, winw, winh,
                     swapchainTex,
                     blitInfo.destination.x, blitInfo.destination.y,
                     blitInfo.destination.w, blitInfo.destination.h);
  } else {
    SDL_BlitGPUTexture(cmdBuf, &blitInfo);
  }
  debug_overlay_render(s_pDebugOverlay, cmdBuf, swapchainTex, swW, swH);
  SDL_SubmitGPUCommandBuffer(cmdBuf);
}

//-------------------------------------------------------------------------------------------------

static void PresentDebugOverlayOnly(void)
{
  if (!s_pGPUDevice || !s_pWindow || !s_pDebugOverlay)
    return;
  if (ROLLERGpuPresentationSuspended())
    return;

  SDL_GPUCommandBuffer *pCmdBuf = SDL_AcquireGPUCommandBuffer(s_pGPUDevice);
  if (!pCmdBuf)
    return;

  SDL_GPUTexture *pSwapchainTex;
  Uint32 uiSwapchainW, uiSwapchainH;
  if (!ROLLERTryAcquireGPUSwapchainTexture(pCmdBuf, s_pWindow,
          &pSwapchainTex, &uiSwapchainW, &uiSwapchainH) || !pSwapchainTex) {
    SDL_CancelGPUCommandBuffer(pCmdBuf);
    return;
  }

  SDL_GPUColorTargetInfo ct = {0};
  ct.texture = pSwapchainTex;
  ct.load_op = SDL_GPU_LOADOP_CLEAR;
  ct.store_op = SDL_GPU_STOREOP_STORE;
  ct.clear_color = (SDL_FColor){0.0f, 0.0f, 0.0f, 1.0f};

  SDL_GPURenderPass *pRp = SDL_BeginGPURenderPass(pCmdBuf, &ct, 1, NULL);
  SDL_EndGPURenderPass(pRp);

  debug_overlay_render(s_pDebugOverlay, pCmdBuf, pSwapchainTex, uiSwapchainW, uiSwapchainH);
  SDL_SubmitGPUCommandBuffer(pCmdBuf);
}

void ROLLERRefreshStartupOverlay()
{
  if (!s_pWindow || !s_pDebugOverlay)
    return;

  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    UpdateSDLAudioEvents(e);
    if (e.type == SDL_EVENT_QUIT) {
      ShutdownSDL();
      exit(0);
    }

    if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_GRAVE)
      debug_overlay_toggle(s_pDebugOverlay);
    else
      (void)debug_overlay_handle_event(s_pDebugOverlay, &e);
  }

  UpdateMouseCursorVisibility();
  PresentDebugOverlayOnly();
}

//-------------------------------------------------------------------------------------------------

void ToggleFullscreen()
{
#if defined(IS_ANDROID)
  return;
#endif
  if (!s_pWindow)
    return;

  SDL_WindowFlags uiFlags = SDL_GetWindowFlags(s_pWindow);
  bool bFullscreen = (uiFlags & SDL_WINDOW_FULLSCREEN) != 0;

  /* Save windowed size before entering fullscreen so we can restore it. */
  static int s_iWndW = 0, s_iWndH = 0;
  if (!bFullscreen) {
    SDL_GetWindowSize(s_pWindow, &s_iWndW, &s_iWndH);
    if (s_iWndW < 320 || s_iWndH < 200) { s_iWndW = 640; s_iWndH = 400; }
  }

  DeferGPUPresentation(3);
  if (s_pGPUDevice)
    SDL_WaitForGPUIdle(s_pGPUDevice);

  if (!SDL_SetWindowFullscreen(s_pWindow, !bFullscreen)) {
    SDL_Log("SDL_SetWindowFullscreen failed: %s", SDL_GetError());
  } else {
    SDL_SyncWindow(s_pWindow);
    if (bFullscreen) {
      /* SDL doesn't always shrink the window on Windows; restore explicitly. */
      int rW = (s_iWndW >= 320) ? s_iWndW : 640;
      int rH = (s_iWndH >= 200) ? s_iWndH : 400;
      SDL_SetWindowSize(s_pWindow, rW, rH);
      SDL_SyncWindow(s_pWindow);
    }
  }

  if (s_pGPUDevice)
    SDL_WaitForGPUIdle(s_pGPUDevice);
  DeferGPUPresentation(3);
  UpdateMouseCursorVisibility();
}

//-------------------------------------------------------------------------------------------------

int InitSDL(char *whiplash_root, const char *midi_root)
{
  SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
#if defined(IS_ANDROID)
  SDL_SetHint(SDL_HINT_ORIENTATIONS,
              "LandscapeLeft LandscapeRight Portrait PortraitUpsideDown");
#endif
#if defined(_WIN32)
  SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_DIRECTINPUT,
                          InputGetWindowsBackend() == INPUT_WINDOWS_BACKEND_SDL_DINPUT ? "1" : "0",
                          SDL_HINT_OVERRIDE);
#endif

  Uint32 uiSdlInitFlags = SDL_INIT_VIDEO;
  if (!g_bSnapshotMode)
    uiSdlInitFlags |= SDL_INIT_AUDIO;
  if (!SDL_Init(uiSdlInitFlags)) {
    ErrorBoxExit("Couldn't initialize SDL: %s", SDL_GetError());
    return 1;
  }

  if (strlen(whiplash_root)) {
    if (chdir(whiplash_root) != 0) {
      ErrorBoxExit("Could not changed working directory to '%s'", whiplash_root);
      return 1;
    }
  } else {
#if defined(IS_ANDROID)
    const char *szExt = SDL_GetAndroidExternalStoragePath();
    if (!szExt) {
      ErrorBoxExit("External storage is not available. Cannot locate game data.");
      return 1;
    }
    strncpy(whiplash_root, szExt, 259);
    whiplash_root[259] = '\0';
    chdir(whiplash_root);
#else
    // Change to the base path of the application
    const char *szBasePath = SDL_GetBasePath();
    if (szBasePath) {
      strncpy(whiplash_root, szBasePath, 259);
      whiplash_root[259] = '\0';
      chdir(whiplash_root);
    }
#endif
  }

  g_pTimerMutex = SDL_CreateMutex();
  g_pDigiMutex = SDL_CreateMutex();

  if (g_bSnapshotMode) {
    // Headless capture path: skip window/GPU/audio/MIDI init entirely.
    // The dummy SDL_VIDEO_DRIVER hint set by main() lets SDL_INIT_VIDEO
    // succeed without a display server.
    return 0;
  }

#if defined(_WIN32)
  InputLoadStartupConfig();
#endif

  SDL_WindowFlags uiWindowFlags = SDL_WINDOW_RESIZABLE;
#if defined(_WIN32)
  /* Hide until InputLoadConfig shows it at the correct size; prevents the
   * white-border flash that occurs when the window is resized on startup. */
  uiWindowFlags |= SDL_WINDOW_HIDDEN;
#endif
  s_pWindow = SDL_CreateWindow("ROLLER", 640, 400, uiWindowFlags);
  if (!s_pWindow) {
    ErrorBoxExit("Couldn't create window: %s", SDL_GetError());
    return 1;
  }

  s_pGPUDevice = SDL_CreateGPUDevice(
    SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_DXIL,
    false, NULL);
  if (!s_pGPUDevice) {
    ErrorBoxExit("Couldn't create GPU device: %s", SDL_GetError());
    return 1;
  }

  if (!SDL_ClaimWindowForGPUDevice(s_pGPUDevice, s_pWindow)) {
    ErrorBoxExit("Couldn't claim window for GPU device: %s", SDL_GetError());
    return 1;
  }

  /* Apply vsync immediately after claiming — the present mode is a window-level
   * setting that covers all GPU rendering (menu, overlay, scene).  The game
   * renderer's deferred mechanism handles runtime changes but isn't called until
   * a race begins, leaving menu frames uncapped otherwise.
   * Vsync-on: MAILBOX (no tearing, lower latency than VSYNC/FIFO) if supported, else VSYNC.
   * Vsync-off: IMMEDIATE (uncapped, no sync). */
  {
    bool supportsMailbox = SDL_WindowSupportsGPUPresentMode(s_pGPUDevice, s_pWindow,
            SDL_GPU_PRESENTMODE_MAILBOX);
    SDL_GPUPresentMode mode = g_bVsync
        ? (supportsMailbox ? SDL_GPU_PRESENTMODE_MAILBOX : SDL_GPU_PRESENTMODE_VSYNC)
        : SDL_GPU_PRESENTMODE_IMMEDIATE;
    SDL_SetGPUSwapchainParameters(s_pGPUDevice, s_pWindow,
        SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode);
  }

  s_pMenuRenderer = menu_render_create(s_pGPUDevice, s_pWindow);
  s_pDebugOverlay = debug_overlay_create(s_pGPUDevice, s_pWindow);

  // GPU texture for game framebuffer presentation
  SDL_GPUTextureCreateInfo texInfo = {0};
  texInfo.type = SDL_GPU_TEXTURETYPE_2D;
  texInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  texInfo.width = 640;
  texInfo.height = 400;
  texInfo.layer_count_or_depth = 1;
  texInfo.num_levels = 1;
  texInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
  s_pGameTexture = SDL_CreateGPUTexture(s_pGPUDevice, &texInfo);
  if (!s_pGameTexture) {
    ErrorBoxExit("Couldn't create GPU texture: %s", SDL_GetError());
    return 1;
  }

  s_pCRTFilter = crt_filter_create(s_pGPUDevice, s_pWindow);
  if (!s_pCRTFilter)
    SDL_Log("crt_filter: failed to create (CRT filter will be unavailable)");

  // Transfer buffer for CPU->GPU framebuffer upload
  SDL_GPUTransferBufferCreateInfo tbInfo = {0};
  tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  tbInfo.size = 640 * 400 * 4;
  s_pTransferBuffer = SDL_CreateGPUTransferBuffer(s_pGPUDevice, &tbInfo);
  if (!s_pTransferBuffer) {
    ErrorBoxExit("Couldn't create GPU transfer buffer: %s", SDL_GetError());
    return 1;
  }

#if !defined(IS_ANDROID)
  SDL_Surface *pIcon = IMG_Load("roller.ico");
  SDL_SetWindowIcon(s_pWindow, pIcon);
#endif

  // Move the window to the display where the mouse is currently located
  float mouseX, mouseY;
  SDL_GetGlobalMouseState(&mouseX, &mouseY);
  int displayIndex = SDL_GetDisplayForPoint(&(SDL_Point) { (int)mouseX, (int)mouseY });
  int sdl_window_centered = SDL_WINDOWPOS_CENTERED_DISPLAY(displayIndex);
  SDL_SetWindowPosition(s_pWindow, sdl_window_centered, sdl_window_centered);

  InputInit();

  char localMidiPath[256];
  if (midi_root) {
    SDL_strlcpy(localMidiPath, midi_root, sizeof(localMidiPath));
  } else {
#if defined(IS_ANDROID)
    midi_root = SDL_GetAndroidExternalStoragePath();
#else
    midi_root = SDL_GetBasePath();
#endif
    if (midi_root) {
      SDL_strlcpy(localMidiPath, midi_root, sizeof(localMidiPath));
    } else {
      SDL_strlcpy(localMidiPath, "./", sizeof(localMidiPath));
    }
  }

  size_t lenMidiPath = strlen(localMidiPath);
  if (lenMidiPath > 0 &&
      localMidiPath[lenMidiPath - 1] != '/' &&
      localMidiPath[lenMidiPath - 1] != '\\') {
    SDL_strlcat(localMidiPath, "/", sizeof(localMidiPath));
  }
  SDL_strlcat(localMidiPath, "midi/wildmidi.cfg", sizeof(localMidiPath));
  // Initialize MIDI with WildMidi
  if (!MIDI_Init(localMidiPath)) {
    SDL_Log("Failed to initialize WildMidi. Please check your configuration file '%s'.", localMidiPath);
  }
  if (!MIDI_OS_Init()) {
    SDL_Log("Failed to initialize OS MIDI (rtmidi).");
  }
  if (!MIDI_OPL_Init()) {
    SDL_Log("Failed to initialize OPL3 MIDI (libADLMIDI).");
  }

  return 0;
}

//-------------------------------------------------------------------------------------------------

void SDLCALL FileCallback(void *pUserData, const char *const *filelist, int iFilter)
{
  tDialogResult *pResult = (tDialogResult *)pUserData;
  int i;

  (void)iFilter;
  pResult->szPath[0] = '\0';
  pResult->iNumPaths = 0;

  if (!filelist || !filelist[0]) {
    pResult->bCancelled = true;
  } else {
    for (i = 0; filelist[i]; i++) {
#if defined(IS_ANDROID)
      SDL_Log("CD image picker selected: %s", filelist[i]);
#endif
      if (pResult->iNumPaths >= ROLLER_DIALOG_MAX_FILES) {
        SDL_Log("CD image picker selected more than %d files; ignoring '%s'",
                ROLLER_DIALOG_MAX_FILES, filelist[i]);
        continue;
      }

      SDL_strlcpy(pResult->aszPaths[pResult->iNumPaths], filelist[i],
                  ROLLER_MAX_PATH);
      if (pResult->iNumPaths == 0)
        SDL_strlcpy(pResult->szPath, filelist[i], ROLLER_MAX_PATH);
      pResult->iNumPaths++;
    }

    if (pResult->iNumPaths == 0)
      pResult->bCancelled = true;
  }
  pResult->bDone = true;
}

//-------------------------------------------------------------------------------------------------

#if defined(IS_ANDROID)
static void AndroidShowFatdataInstructions(void)
{
  const char *szExternal = SDL_GetAndroidExternalStoragePath();
  const char *szShown = szExternal ? szExternal : "<external storage unavailable>";
  char szMessage[1024];

  snprintf(szMessage, sizeof(szMessage),
           "ROLLER needs the FATDATA assets from a retail copy of the game.\n\n"
           "Copy your game data so the folders end up here:\n\n"
           "%s/FATDATA\n"
           "%s/TRACKS    (community tracks)\n"
           "%s/REPLAYS   (saved replays)\n\n"
           "For CD music, also copy your extracted track WAVs to:\n"
           "%s/audio\n\n"
           "Then relaunch ROLLER.",
           szShown, szShown, szShown, szShown);

  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION,
                           "FATDATA not found", szMessage, s_pWindow);
}

//-------------------------------------------------------------------------------------------------

static void AndroidShowExtractionFailed(void)
{
  SDL_ShowSimpleMessageBox(
      SDL_MESSAGEBOX_ERROR, "Extraction failed",
      "No game data could be extracted from the files you selected.\n\n"
      "For a CUE/BIN image you must select the .CUE file AND all of its "
      ".BIN/audio files together - selecting only the .CUE or only the .BIN "
      "will not work, because the other files cannot be read on their own.\n\n"
      "Please try again and select the .CUE and every file it references.",
      s_pWindow);
}

//-------------------------------------------------------------------------------------------------

static bool AndroidPromptForCdImage(void)
{
  SDL_MessageBoxButtonData buttons[] = {
    { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 0, "Select Image" },
    { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 1, "Cancel" },
  };

  SDL_MessageBoxData msgbox = {
      SDL_MESSAGEBOX_INFORMATION,
      s_pWindow,
      "FATDATA not found",
      "Select your retail game CD image to extract the game data and music.\n\n"
      "- CUE/BIN image: select the .CUE file AND all of its .BIN/audio files "
      "together (tap each one in the picker).\n"
      "- Single ISO: select the .ISO file.\n\n"
      "Extraction may take a minute.",
      SDL_arraysize(buttons),
      buttons,
      NULL
  };

  int iButtonID = 1;
  if (!SDL_ShowMessageBox(&msgbox, &iButtonID)) {
    SDL_Log("Android CD image prompt failed: %s", SDL_GetError());
    return false;
  }

  return iButtonID == 0;
}

//-------------------------------------------------------------------------------------------------

static bool AndroidCreateCdImageStagingDir(const char *szDataRoot,
                                           char *szOutDir,
                                           size_t nOutDirSize)
{
  char szImportDir[ROLLER_MAX_PATH];
  time_t tNow = time(NULL);
  Uint64 uiTicks = SDL_GetTicks();

  SDL_snprintf(szImportDir, sizeof(szImportDir), "%s/import", szDataRoot);
  if (!SDL_CreateDirectory(szImportDir) && !ROLLERdirexists(szImportDir)) {
    SDL_Log("Android CD image import: failed to create '%s': %s",
            szImportDir, SDL_GetError());
    return false;
  }

  for (int i = 0; i < 100; i++) {
    int nWritten = SDL_snprintf(szOutDir, nOutDirSize,
                                "%s/cdimage-%lld-%llu-%02d",
                                szImportDir, (long long)tNow,
                                (unsigned long long)uiTicks, i);
    if (nWritten < 0 || (size_t)nWritten >= nOutDirSize) {
      SDL_Log("Android CD image import: staging path is too long");
      return false;
    }

    if (SDL_CreateDirectory(szOutDir))
      return true;
  }

  SDL_Log("Android CD image import: could not create a unique staging directory under '%s'",
          szImportDir);
  return false;
}

//-------------------------------------------------------------------------------------------------

static bool AndroidStageContentUri(const char *szUri, const char *szDestDir,
                                   char *szOutPath, size_t nOutPathSize)
{
  JNIEnv *pEnv;
  jobject activity = NULL;
  jclass activityClass = NULL;
  jmethodID stageContentUri = NULL;
  jstring jUri = NULL;
  jstring jDestDir = NULL;
  jstring jStagedPath = NULL;
  const char *szStagedPath = NULL;
  bool bOk = false;

  szOutPath[0] = '\0';

  pEnv = (JNIEnv *)SDL_GetAndroidJNIEnv();
  if (!pEnv)
    return false;

  activity = (jobject)SDL_GetAndroidActivity();
  if (!activity)
    return false;

  activityClass = (*pEnv)->GetObjectClass(pEnv, activity);
  if (!activityClass)
    goto cleanup;

  stageContentUri = (*pEnv)->GetMethodID(
      pEnv, activityClass, "stageContentUri",
      "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
  if (!stageContentUri)
    goto cleanup;

  jUri = (*pEnv)->NewStringUTF(pEnv, szUri);
  jDestDir = (*pEnv)->NewStringUTF(pEnv, szDestDir);
  if (!jUri || !jDestDir)
    goto cleanup;

  jStagedPath = (jstring)(*pEnv)->CallObjectMethod(
      pEnv, activity, stageContentUri, jUri, jDestDir);
  if ((*pEnv)->ExceptionCheck(pEnv)) {
    (*pEnv)->ExceptionDescribe(pEnv);
    (*pEnv)->ExceptionClear(pEnv);
    goto cleanup;
  }

  if (!jStagedPath)
    goto cleanup;

  szStagedPath = (*pEnv)->GetStringUTFChars(pEnv, jStagedPath, NULL);
  if (!szStagedPath)
    goto cleanup;

  if (SDL_strlcpy(szOutPath, szStagedPath, nOutPathSize) >= nOutPathSize) {
    SDL_Log("Android CD image import: staged path is too long: '%s'",
            szStagedPath);
    szOutPath[0] = '\0';
    goto cleanup;
  }

  bOk = szOutPath[0] != '\0';

cleanup:
  if (jStagedPath && szStagedPath)
    (*pEnv)->ReleaseStringUTFChars(pEnv, jStagedPath, szStagedPath);
  if (jStagedPath)
    (*pEnv)->DeleteLocalRef(pEnv, jStagedPath);
  if (jDestDir)
    (*pEnv)->DeleteLocalRef(pEnv, jDestDir);
  if (jUri)
    (*pEnv)->DeleteLocalRef(pEnv, jUri);
  if (activityClass)
    (*pEnv)->DeleteLocalRef(pEnv, activityClass);
  (*pEnv)->DeleteLocalRef(pEnv, activity);
  return bOk;
}

//-------------------------------------------------------------------------------------------------

static bool AndroidPathEndsWithIgnoreCase(const char *szPath, const char *szExt)
{
  size_t nPath = strlen(szPath);
  size_t nExt = strlen(szExt);

  if (nPath < nExt)
    return false;

  return SDL_strcasecmp(szPath + nPath - nExt, szExt) == 0;
}

//-------------------------------------------------------------------------------------------------

static const char *AndroidPathFilename(const char *szPath)
{
  const char *szSlash = strrchr(szPath, '/');
  const char *szBackslash = strrchr(szPath, '\\');
  const char *szName = szPath;

  if (szSlash && szSlash + 1 > szName)
    szName = szSlash + 1;
  if (szBackslash && szBackslash + 1 > szName)
    szName = szBackslash + 1;

  return szName;
}

//-------------------------------------------------------------------------------------------------

static int AndroidCdImagePriority(const char *szPath)
{
  // Only a .cue or .iso is a valid extraction entry. A bare .bin has no track or
  // sector layout without its .cue, and handing a raw .bin to libcdio's
  // DRIVER_UNKNOWN probe corrupts the heap and crashes. .bin files are still
  // *staged* (as cue siblings) - they just can't be the entry on their own.
  if (AndroidPathEndsWithIgnoreCase(szPath, ".cue"))
    return 0;
  if (AndroidPathEndsWithIgnoreCase(szPath, ".iso"))
    return 1;

  return -1;
}

//-------------------------------------------------------------------------------------------------

static bool AndroidChooseCdImageEntry(char aszStagedPaths[][ROLLER_MAX_PATH],
                                      int iNumPaths,
                                      char *szOutEntry,
                                      size_t nOutEntrySize)
{
  int iBest = -1;
  int iBestPriority = 100;

  for (int i = 0; i < iNumPaths; i++) {
    int iPriority = AndroidCdImagePriority(aszStagedPaths[i]);
    if (iPriority < 0)
      continue;

    if (iBest < 0 || iPriority < iBestPriority ||
        (iPriority == iBestPriority &&
         SDL_strcasecmp(AndroidPathFilename(aszStagedPaths[i]),
                        AndroidPathFilename(aszStagedPaths[iBest])) < 0)) {
      iBest = i;
      iBestPriority = iPriority;
    }
  }

  if (iBest < 0)
    return false;

  if (SDL_strlcpy(szOutEntry, aszStagedPaths[iBest], nOutEntrySize) >=
      nOutEntrySize) {
    SDL_Log("Android CD image import: selected entry path is too long: '%s'",
            aszStagedPaths[iBest]);
    szOutEntry[0] = '\0';
    return false;
  }

  SDL_Log("Android CD image import: selected staged entry '%s'", szOutEntry);
  return true;
}

//-------------------------------------------------------------------------------------------------

static bool AndroidStageCdImageSelection(const tDialogResult *pResult,
                                         const char *szDataRoot,
                                         char *szOutEntry,
                                         size_t nOutEntrySize)
{
  char szStagingDir[ROLLER_MAX_PATH];
  char aszStagedPaths[ROLLER_DIALOG_MAX_FILES][ROLLER_MAX_PATH];
  int iNumStaged = 0;

  szOutEntry[0] = '\0';

  if (!pResult || pResult->iNumPaths <= 0)
    return false;

  if (!AndroidCreateCdImageStagingDir(szDataRoot, szStagingDir,
                                      sizeof(szStagingDir)))
    return false;

  SDL_Log("Android CD image import: staging into '%s'", szStagingDir);

  for (int i = 0; i < pResult->iNumPaths; i++) {
    char szStagedPath[ROLLER_MAX_PATH];
    if (!AndroidStageContentUri(pResult->aszPaths[i], szStagingDir,
                                szStagedPath, sizeof(szStagedPath))) {
      SDL_Log("Android CD image import: failed to stage '%s'",
              pResult->aszPaths[i]);
      return false;
    }

    SDL_Log("Android CD image import: staged '%s' -> '%s'",
            pResult->aszPaths[i], szStagedPath);
    SDL_strlcpy(aszStagedPaths[iNumStaged++], szStagedPath, ROLLER_MAX_PATH);
  }

  if (!AndroidChooseCdImageEntry(aszStagedPaths, iNumStaged, szOutEntry,
                                 nOutEntrySize)) {
    SDL_Log("Android CD image import: no .cue or .iso entry in selection (a .bin alone is not enough)");
    return false;
  }

  return true;
}

//-------------------------------------------------------------------------------------------------
#endif

void InitFATDATA(const char *szDataRoot)
{
  if (!szDataRoot)
    return;

  // check if data folder exists (case-insensitive for linux)
  if (!ROLLERdirexists("./FATDATA") && !ROLLERdirexists("./fatdata")) {
#if defined(IS_ANDROID)
    bool bSelectedFiles = false;
    bool bImported = false;

    // Showing native dialogs (message box + SAF picker) below destroys and
    // recreates the Activity surface. Driving the SDL Vulkan renderer against the
    // in-flux surface crashes the Adreno driver, so suppress all GPU presentation
    // for the whole import flow. ROLLERRefreshStartupOverlay() still pumps events
    // (needed for the picker callback and SDL_EVENT_QUIT); it just won't present.
    s_bGpuPresentDisabled = true;

    if (AndroidPromptForCdImage()) {
      SDL_DialogFileFilter filters[] = { { "CD Images", "iso;bin;cue;ISO;BIN;CUE" } };
      tDialogResult result = { 0 };

      SDL_ShowOpenFileDialog(FileCallback, &result, s_pWindow, filters, 1,
                             szDataRoot, true);

      while (!result.bDone) {
        ROLLERRefreshStartupOverlay();
        SDL_Delay(10);
      }

      if (!result.bCancelled && result.iNumPaths > 0) {
        bSelectedFiles = true;
        char szStagedEntry[ROLLER_MAX_PATH];
        if (AndroidStageCdImageSelection(&result, szDataRoot, szStagedEntry,
                                         sizeof(szStagedEntry))) {
          ROLLERRefreshStartupOverlay();
          ExtractFATDATA(szStagedEntry, szDataRoot);
          SaveDefaultFatalIni(szDataRoot); //save default config after extraction so all users will have svga, sfx, and music on by default
          ROLLERRefreshStartupOverlay();
          bImported = ROLLERdirexists("./FATDATA") || ROLLERdirexists("./fatdata");
        }
      }
    }

    if (!bImported) {
      if (bSelectedFiles) {
        // The user picked files but no FATDATA came out - almost always because
        // they selected only the .CUE or only the .BIN (the sibling files were
        // never granted/staged). Tell them exactly what to select.
        SDL_Log("Android CD image import did not produce FATDATA; showing extraction-failed error");
        AndroidShowExtractionFailed();
      } else {
        // Cancelled or nothing selected: fall back to the side-load instructions.
        AndroidShowFatdataInstructions();
      }
    }

    // Re-enable GPU presentation now that all native dialogs are dismissed.
    s_bGpuPresentDisabled = false;
    DeferGPUPresentation(ROLLER_RESIZE_DEFER_FRAMES);
#else
    debug_overlay_set_visible(s_pDebugOverlay, true);
    PresentDebugOverlayOnly();

    SDL_MessageBoxButtonData buttons[] = {
      { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 0, "Select Image" },
      { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 1, "Cancel" },
    };

    SDL_MessageBoxData msgbox = {
        SDL_MESSAGEBOX_INFORMATION,
        s_pWindow,
        "FATDATA not found",
        "Choose a CD image (ISO, BIN/CUE) to extract the game data:",
        SDL_arraysize(buttons),
        buttons,
        NULL
    };

    int iButtonID;
    tDialogResult result = { 0 };
    if (SDL_ShowMessageBox(&msgbox, &iButtonID) && iButtonID == 0) {
      #ifdef IS_WINDOWS
        SDL_DialogFileFilter filters[] = { { "CD Images", "iso;bin;cue" } };
      #else
        SDL_DialogFileFilter filters[] = { { "CD Images", "iso;bin;cue;ISO;BIN;CUE" } };
      #endif

      SDL_ShowOpenFileDialog(FileCallback, &result, s_pWindow, filters, 1, szDataRoot, false);

      while (!result.bDone) {
        ROLLERRefreshStartupOverlay();
        SDL_Delay(10);
      }

      if (!result.bCancelled) {
        ROLLERRefreshStartupOverlay();
        ExtractFATDATA(result.szPath, szDataRoot);        
        SaveDefaultFatalIni(szDataRoot); //save default config after extraction so all users will have svga, sfx, and music on by default
        ROLLERRefreshStartupOverlay();
      }
    }
#endif
  }

  //check if extraction was successful
  if (!ROLLERdirexists("./FATDATA") && !ROLLERdirexists("./fatdata")) {
    ErrorBoxExit("The folder FATDATA does not exist.\nROLLER requires the FATDATA folder assets from a retail copy of the game.");
  }
  
  // if the extracted audio tracks are present, enable CD music.
  if (ROLLERAudioMusicAvailable()) {
    MusicCard = 0;
    MusicCD = -1;
  } else {
    MusicCard = -1;
    MusicCD = 0;
  }
}

//-------------------------------------------------------------------------------------------------

static bool ROLLERFindAudioTrackPath(int iTrack, char *szOutPath, size_t uiOutPathSize)
{
  char szTrackFile[ROLLER_MAX_PATH];
  const char *apszTrackPaths[] = {
    "./audio/track%02d.wav",
    "../audio/track%02d.wav"
  };

  for (int iPath = 0; iPath < (int)(sizeof(apszTrackPaths) / sizeof(apszTrackPaths[0])); ++iPath) {
    if (snprintf(szTrackFile, sizeof(szTrackFile), apszTrackPaths[iPath], iTrack) >=
        (int)sizeof(szTrackFile))
      continue;

    FILE *pTrack = ROLLERfopen(szTrackFile, "rb");
    if (pTrack) {
      const char *szResolved = ROLLERfindpath(szTrackFile);
      fclose(pTrack);

      if (szResolved) {
        if (szOutPath && uiOutPathSize > 0) {
#ifdef IS_WINDOWS
          SDL_strlcpy(szOutPath, szResolved, uiOutPathSize);
#else
          if (szResolved[0] == '/' || szResolved[0] == '\\') {
            SDL_strlcpy(szOutPath, szResolved, uiOutPathSize);
          } else {
            char szCwd[ROLLER_MAX_PATH];
            if (getcwd(szCwd, sizeof(szCwd))) {
              snprintf(szOutPath, uiOutPathSize, "%s/%s", szCwd, szResolved);
            } else {
              SDL_strlcpy(szOutPath, szResolved, uiOutPathSize);
            }
          }
#endif
        }
        return true;
      }
    }
  }

  return false;
}

//-------------------------------------------------------------------------------------------------

int ROLLERAudioMusicAvailable(void)
{
  char szTrackFile[ROLLER_MAX_PATH];
  return ROLLERFindAudioTrackPath(2, szTrackFile, sizeof(szTrackFile)) ? -1 : 0;
}

//-------------------------------------------------------------------------------------------------

static void ROLLERCreateUserDataDir(const char *szDir)
{
#ifdef IS_WINDOWS
  mkdir(szDir);
#else
#if defined(IS_ANDROID)
  mkdir(szDir, 0777);
  chmod(".", 0777);
  chmod(szDir, 0777);
#else
  mkdir(szDir, 0755);
#endif
#endif
}

//-------------------------------------------------------------------------------------------------

void InitREPLAYS(const char *szDataRoot)
{
  ROLLERCreateUserDataDir("./REPLAYS");
}

//-------------------------------------------------------------------------------------------------

void InitTRACKS(const char *szDataRoot)
{
  ROLLERCreateUserDataDir("./TRACKS");
}

//-------------------------------------------------------------------------------------------------

void ShutdownSDL()
{
  if (!g_bSnapshotMode) {
    DIGIClearAllStream();
    MIDI_Shutdown();
    MIDI_OS_Shutdown();
    MIDI_OPL_Shutdown();

    InputShutdown();

    debug_overlay_destroy(s_pDebugOverlay);
    s_pDebugOverlay = NULL;
    crt_filter_destroy(s_pCRTFilter);
    s_pCRTFilter = NULL;
    menu_render_destroy(s_pMenuRenderer);
    SDL_ReleaseGPUTexture(s_pGPUDevice, s_pGameTexture);
    SDL_ReleaseGPUTransferBuffer(s_pGPUDevice, s_pTransferBuffer);
    SDL_ReleaseWindowFromGPUDevice(s_pGPUDevice, s_pWindow);
    SDL_DestroyGPUDevice(s_pGPUDevice);
    SDL_DestroyWindow(s_pWindow);
  }

  if (g_pDigiMutex) {
    SDL_DestroyMutex(g_pDigiMutex);
    g_pDigiMutex = NULL;
  }
  if (g_pTimerMutex) {
    SDL_DestroyMutex(g_pTimerMutex);
    g_pTimerMutex = NULL;
  }

  SDL_Quit();
}

//-------------------------------------------------------------------------------------------------
#if _DEBUG
bool debugEnable = false;
void UpdateDebugLoop()
{
  if (debugEnable) {

    void *front_vga_font1 = load_picture("font1.bm");
    void *front_vga_font2 = load_picture("font2.bm");
    void *front_vga_font3 = load_picture("font3.bm");
    void *front_vga_font4 = load_picture("font4.bm");

    void *front_vga_font = front_vga_font1;
    void *font_ascii = &font1_ascii;
    void *font_offsets = &font1_offsets;

    char buffer[256] = { 0 };
    char text[32] = { 0 };
    int value = 0;
    int font = 0;

    int _scr_size = scr_size; // Backup scr_size

    LoadPanel(); // Load rev_vga array
    scr_size = 64; // scale text size
    screen_pointer = scrbuf; // Set screen pointer to scrbuf

    strcpy(text, "Debug font ascii");

    while (debugEnable) {

      uint8 size = 24; // Font size

      if (value < 0) value = 0;
      if (font < 0) font = 0;
      if (font > 3) font = 3;

      // Set font
      if (font == 0) {
        front_vga_font = front_vga_font1;
        font_ascii = &font1_ascii;
        font_offsets = &font1_offsets;
      } else if (font == 1) {
        front_vga_font = front_vga_font2;
        font_ascii = &font2_ascii;
        font_offsets = &font2_offsets;
      } else if (font == 2) {
        front_vga_font = front_vga_font3;
        font_ascii = &font3_ascii;
        font_offsets = &font3_offsets;
        size = 40;
      } else {
        front_vga_font = front_vga_font4;
        font_ascii = &font4_ascii;
        font_offsets = &font4_offsets;
        size = 40;
      }

      // clear screen - set scrbuf to 0 - black
      memset(scrbuf, 0, SVGA_ON ? 256000 : 64000);

      uint8 color_white = 0x8Fu;
      uint8 color_red = 0xE7u;

      // Mini text print
      scr_size = 64; // scale text size
      mini_prt_centre(rev_vga[0], "0123456789", 320, 240 - 8);
      prt_centrecol(rev_vga[1], "0123456789", 320, 240, color_white);
      scr_size = 128; // scale text size
      mini_prt_centre(rev_vga[0], "ABCDEFGHIJKLMNOPQRSTUVWXYZ", 320 / 2, (240 + 8) / 2);
      prt_centrecol(rev_vga[1], "ABCDEFGHIJKLMNOPQRSTUVWXYZ", 320 / 2, (240 + 24) / 2, color_white);

      // Mini text print with config_buffer
      //mini_prt_centre(rev_vga[0], &config_buffer[value * 64], 320 / 2, (240 + 40) / 2); // This fail with `-`

      prt_centrecol(rev_vga[1], &config_buffer[value * 64], 320 / 2, (240 + 56) / 2, color_white);
      scr_size = 256; // scale text size
      prt_centrecol(rev_vga[1], &config_buffer[value * 64], 320 / 4, (240 + 72) / 4, color_white);


      sprintf(buffer, "%s", text);
      front_text((tBlockHeader *)front_vga_font, buffer, font_ascii, font_offsets, 0, size / 2, color_white, 0);

      sprintf(buffer, "%i-%i", value, font);
      front_text((tBlockHeader *)front_vga_font, buffer, font_ascii, font_offsets, 640 - size / 2, size / 2, color_white, 2);

      front_text((tBlockHeader *)front_vga_font, &config_buffer[value * 64], font_ascii, font_offsets, 0, 0 + size + size / 2, color_white, 0);

      for (size_t j = 0; j < 8; j++) {
        for (size_t i = 0; i < 32; i++) {
          buffer[i] = (char)(i + 32 * j);
        }
        buffer[32] = '\0';
        front_text((tBlockHeader *)front_vga_font, buffer, font_ascii, font_offsets, 640 - size / 2, size / 2 + size * ((int)j + 1), color_white, 2);
      }

      SDL_Event e;
      while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) {
          quit_game = 1;
          doexit();
          return;
        }
        if (e.type == SDL_EVENT_KEY_DOWN) {
          if (e.key.key == SDLK_UP) {
            value++;
          }
          if (e.key.key == SDLK_DOWN) {
            value--;
          }

          if (e.key.key == SDLK_LEFT) {
            font--;
          }
          if (e.key.key == SDLK_RIGHT) {
            font++;
          }

          if (e.key.key == SDLK_D) {
            debugEnable = !debugEnable;
            continue;
          }
          if (e.key.key == SDLK_ESCAPE) {
            debugEnable = !debugEnable;
            continue;
          }
        }
      }
      UpdateSDLWindow();
    }

    fre((void **)&front_vga_font4);
    fre((void **)&front_vga_font3);
    fre((void **)&front_vga_font2);
    fre((void **)&front_vga_font1);

    scr_size = _scr_size; // Restore scr_size
  }
}
#endif
//-------------------------------------------------------------------------------------------------

void UpdateSDL()
{
  game_render_set_debug_overlay(g_pGameRenderer, s_pDebugOverlay);
  game_render_set_crt_filter(g_pGameRenderer, g_bCRTFilter ? s_pCRTFilter : NULL);
  SDL_PumpEvents();
  SDL_Event e;
  while (SDL_PeepEvents(&e, 1, SDL_GETEVENT, SDL_EVENT_FIRST, SDL_EVENT_LAST) > 0) {
    UpdateSDLAudioEvents(e);
    if (e.type == SDL_EVENT_QUIT) {
      quit_game = 1;
      eFrontendNextState = eFRONTEND_STATE_SHUTDOWN;
      continue;
    }
    if (e.type >= SDL_EVENT_WINDOW_FIRST && e.type <= SDL_EVENT_WINDOW_LAST) {
      switch (e.type) {
        case SDL_EVENT_WINDOW_RESIZED:
          if (g_bKeepWindowSize &&
              !(SDL_GetWindowFlags(s_pWindow) & SDL_WINDOW_FULLSCREEN)) {
            int iW = 0, iH = 0;
            if (SDL_GetWindowSize(s_pWindow, &iW, &iH) && iW >= 320 && iH >= 200) {
              extern int g_iSavedWindowWidth, g_iSavedWindowHeight;
              g_iSavedWindowWidth  = iW;
              g_iSavedWindowHeight = iH;
              InputSaveConfig();
            }
          }
          /* fall through */
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_MINIMIZED:
        case SDL_EVENT_WINDOW_RESTORED:
        case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        case SDL_EVENT_WINDOW_SAFE_AREA_CHANGED:
        case SDL_EVENT_WINDOW_OCCLUDED:
        case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
        case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
          DeferGPUPresentation(ROLLER_RESIZE_DEFER_FRAMES);
          break;
        default:
          break;
      }
    }
    if (e.type == SDL_EVENT_KEY_DOWN) {
      if (e.key.scancode == SDL_SCANCODE_GRAVE) {
        debug_overlay_toggle(s_pDebugOverlay);
        continue;
      }
      if (e.key.scancode == SDL_SCANCODE_TAB && (e.key.mod & SDL_KMOD_SHIFT)) {
        s_pendingShiftDown.type = 0; /* consumed by SHIFT+TAB */
        if (g_pGameRenderer) {
          bool newSplit = !game_render_is_split_screen(g_pGameRenderer);
          if (newSplit && game_render_get_mode(g_pGameRenderer) != GAME_RENDER_GPU) {
            MenuRenderer *pTabRenderer = GetMenuRenderer();
            if (pTabRenderer)
              menu_render_set_mode(pTabRenderer, MENU_RENDER_GPU);
            game_render_set_mode(g_pGameRenderer, GAME_RENDER_GPU);
            InputSaveConfig();
          }
          game_render_set_split_screen(g_pGameRenderer, newSplit);
          texture_uv_map_dump(g_iSurfaceLogId);
          texture_uv_map_reset();
          g_bTexUVMap = true;
        }
        continue;
      }
      /* Defer SHIFT so it doesn't fire intro-skip before we know if TAB follows. */
      if (e.key.scancode == SDL_SCANCODE_LSHIFT || e.key.scancode == SDL_SCANCODE_RSHIFT) {
        s_pendingShiftDown = e;
        if (s_shiftPressedMs == 0)
          s_shiftPressedMs = SDL_GetTicks();
        continue;
      }
    }
    if (e.type == SDL_EVENT_KEY_UP) {
      if (e.key.scancode == SDL_SCANCODE_LSHIFT || e.key.scancode == SDL_SCANCODE_RSHIFT) {
        s_shiftPressedMs = 0;
        if (s_pendingShiftDown.type) {
          if (g_bShiftFreezeEnabled) {
            /* Freeze enabled: discard DOWN+UP so SHIFT never reaches the DOS
             * key buffer while the freeze checkbox is on. */
            s_pendingShiftDown.type = 0;
            continue;
          }
          /* SHIFT released without TAB — write the deferred DOWN into the DOS
           * key buffer directly, then let the UP fall through to do the same. */
          SDL_Scancode sc = s_pendingShiftDown.key.scancode;
          if ((size_t)sc < SDL_arraysize(sdl_to_set1) && sdl_to_set1[sc])
            key_handler(sdl_to_set1[sc]);
          s_pendingShiftDown.type = 0;
          /* no continue — SHIFT UP falls through to key_handler */
        } else {
          /* SHIFT was consumed by SHIFT+TAB — suppress the UP too. */
          continue;
        }
      }
    }

    /* Right-click surface pick: record normalised game-viewport coords before
     * the overlay consumes the event. Works whether the overlay is open or not. */
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_RIGHT) {
      int wpx = 0, wpy = 0;
      if (SDL_GetWindowSizeInPixels(s_pWindow, &wpx, &wpy) && wpx > 0 && wpy > 0) {
        SDL_GPUViewport vp;
        ROLLERGetPresentViewport((Uint32)wpx, (Uint32)wpy, ROLLER_PRESENT_ASPECT, &vp);
        if (vp.w > 0.0f && vp.h > 0.0f) {
          g_clickQueryNX      = (e.button.x - vp.x) / vp.w;
          g_clickQueryNY      = (e.button.y - vp.y) / vp.h;
          g_pendingClickQuery = true;
        }
      }
    }

    if (debug_overlay_handle_event(s_pDebugOverlay, &e))
      continue;

    InputHandleEvent(&e);
    frontend_mouse_handle_event(&e);

    if (e.type == SDL_EVENT_KEY_DOWN) {

      if (e.key.scancode == SDL_SCANCODE_TAB) {
        MenuRenderer *pTabRenderer = GetMenuRenderer();
        if (pTabRenderer) {
          int bGPU = !(menu_render_get_pending_mode(pTabRenderer) == MENU_RENDER_GPU);
          menu_render_set_mode(pTabRenderer, bGPU ? MENU_RENDER_GPU : MENU_RENDER_SOFTWARE);
          game_render_set_mode(g_pGameRenderer, bGPU ? GAME_RENDER_GPU : GAME_RENDER_SOFTWARE);
          InputSaveConfig();
        }
        continue;
      }

#if _DEBUG
      if (e.key.key == SDLK_D) { // Add by ROLLER
        if (SDL_GetModState() & (SDL_KMOD_LCTRL | SDL_KMOD_RCTRL)) {
          if (front_vga[2] != NULL) { // Check if front_vga is loaded, loaded in main menu.
            debugEnable = !debugEnable;
            continue;
          }
        }
      }
#endif // _DEBUG

      //if (e.key.key == SDLK_ESCAPE) {
      //  quit_game = 1;
      //} else if (e.key.key == SDLK_F11) {
      //  ToggleFullscreen();
      //  continue;
      if (e.key.key == SDLK_RETURN) {
        SDL_Keymod mod = SDL_GetModState();
        if (mod & (SDL_KMOD_LALT | SDL_KMOD_RALT)) {
          ToggleFullscreen();
          continue;
        }
      }
    }

    if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
      SDL_Scancode sc = e.key.scancode;

      // Handle pause key as a special sequence
      if (sc == SDL_SCANCODE_PAUSE) {
        if (e.type == SDL_EVENT_KEY_DOWN) {
          key_handler(0xE1);
          key_handler(0x1D);
          key_handler(0x45);
        } else {
          key_handler(0xE1 | 0x80);
          key_handler(0x1D | 0x80);
          key_handler(0x45 | 0x80);
        }
        return;
      }

      // Translate SDL scancode to set1 scancode
      if ((size_t)sc < SDL_arraysize(sdl_to_set1) && sdl_to_set1[sc]) {
        uint8 byRawCode = sdl_to_set1[sc];
        if (e.type == SDL_EVENT_KEY_UP) {
          byRawCode |= 0x80;  // Set high bit for release
        }
        key_handler(byRawCode);
      }
    }

  }
  InputUpdate();
  noclip_camera_update();
  InputUpdateMenuControls();
  UpdateMouseCursorVisibility();
  //UpdateSDLWindow();
#if _DEBUG
  UpdateDebugLoop(); // Add by ROLLER
#endif // _DEBUG
  uint64 ullCurTicksMs = SDL_GetTicks();
  if (ullCurTicksMs > g_ullTimer150Ms) {
    g_ullTimer150Ms = ullCurTicksMs + 150;
    UpdateAudioTracks();
  }

  /* SDL3 GPU's VSYNC present mode queues frames without blocking the CPU when
   * enough swapchain images are available, leaving the render loop spinning at
   * uncapped rates.  Sleep the remaining frame budget so the render rate stays
   * near the monitor refresh rate when vsync is on. */
  {
    static uint64 s_targetFrameNs = 0;
    static uint64 s_nextFrameNs   = 0;
    static bool   s_vsyncWas      = true; /* init=true (vsync default) so first
                                             frame re-applies when config=off */

    if (g_bVsync != s_vsyncWas) {
      s_vsyncWas      = g_bVsync;
      s_targetFrameNs = 0;
      s_nextFrameNs   = 0;
      if (s_pGPUDevice && s_pWindow) {
        bool supportsMailbox = SDL_WindowSupportsGPUPresentMode(s_pGPUDevice, s_pWindow,
                SDL_GPU_PRESENTMODE_MAILBOX);
        SDL_GPUPresentMode mode = g_bVsync
            ? (supportsMailbox ? SDL_GPU_PRESENTMODE_MAILBOX : SDL_GPU_PRESENTMODE_VSYNC)
            : SDL_GPU_PRESENTMODE_IMMEDIATE;
        if (!SDL_SetGPUSwapchainParameters(s_pGPUDevice, s_pWindow,
                SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode))
          SDL_Log("roller: SDL_SetGPUSwapchainParameters failed: %s", SDL_GetError());
      }
    }

    if (g_bVsync && g_pGameRenderer &&
        game_render_get_mode(g_pGameRenderer) == GAME_RENDER_GPU) {
      if (s_targetFrameNs == 0 && s_pWindow) {
        SDL_DisplayID disp = SDL_GetDisplayForWindow(s_pWindow);
        const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(disp);
        float hz = (mode && mode->refresh_rate > 0.0f) ? mode->refresh_rate : 60.0f;
        s_targetFrameNs = (uint64)(1e9 / (double)hz);
      }

      uint64 now = SDL_GetTicksNS();
      if (s_nextFrameNs > 0 && now < s_nextFrameNs)
        SDL_DelayNS(s_nextFrameNs - now);

      now = SDL_GetTicksNS();
      if (s_nextFrameNs == 0 || now > s_nextFrameNs + s_targetFrameNs)
        s_nextFrameNs = now + s_targetFrameNs;
      else
        s_nextFrameNs += s_targetFrameNs;
    }
  }

  /* Shift freeze: block all tick steps so the engine image holds still. */
  g_bShiftFrozen = g_bShiftFreezeEnabled && s_shiftPressedMs != 0;
  if (g_bShiftFrozen)
    SDL_SetAtomicInt(&iTicksPending, 0);

  /* Background FPS cap: active when paused, shift-frozen, or the window is
   * minimized, and the user has selected a limit other than Off. */
  bool bWindowMinimized = s_pWindow &&
      (SDL_GetWindowFlags(s_pWindow) & SDL_WINDOW_MINIMIZED) != 0;
  bool bDebugOverlayVisible = debug_overlay_visible(s_pDebugOverlay);
  bool bApplyBgCap = g_iFpsBackground > 0 &&
      (eFrontendCurrentState == eFRONTEND_STATE_PAUSE_OVERLAY ||
       g_bShiftFrozen || bWindowMinimized || bDebugOverlayVisible);
  if (bApplyBgCap) {
    uint64 frameMs = 1000u / (uint64)g_iFpsBackground;
    uint64 now = SDL_GetTicks();
    if (s_nextPauseFrameMs == 0)
      s_nextPauseFrameMs = now;
    if (now < s_nextPauseFrameMs)
      SDL_Delay((uint32)(s_nextPauseFrameMs - now));
    s_nextPauseFrameMs = SDL_GetTicks() + frameMs;
  } else {
    s_nextPauseFrameMs = 0;
  }
}

//-------------------------------------------------------------------------------------------------

#ifndef IS_WINDOWS
static int s_findpath_append(char *szPath, int iCurLen, int iMaxLen, const char *szComp)
{
  int iCompLen = (int)strlen(szComp);
  if (iCurLen == 0) {
    if (iCompLen >= iMaxLen) return -1;
    memcpy(szPath, szComp, iCompLen + 1);
    return iCompLen;
  }
  if (iCurLen == 1 && szPath[0] == '/') {
    if (1 + iCompLen >= iMaxLen) return -1;
    memcpy(szPath + 1, szComp, iCompLen + 1);
    return 1 + iCompLen;
  }
  if (iCurLen + 1 + iCompLen >= iMaxLen) return -1;
  szPath[iCurLen] = '/';
  memcpy(szPath + iCurLen + 1, szComp, iCompLen + 1);
  return iCurLen + 1 + iCompLen;
}
#endif

const char *ROLLERfindpath(const char *szFile)
{
#ifdef IS_WINDOWS
  return szFile;
#else
  static char szResolved[260];
  char szInput[260];
  strncpy(szInput, szFile, sizeof(szInput) - 1);
  szInput[sizeof(szInput) - 1] = '\0';

  bool bAbsolute = (szInput[0] == '/');
  szResolved[0] = '\0';
  int iResolvedLen = 0;
  if (bAbsolute) {
    szResolved[0] = '/';
    szResolved[1] = '\0';
    iResolvedLen = 1;
  }

  char *pSave = NULL;
  char *pToken = strtok_r(szInput, "/", &pSave);
  if (!pToken) return NULL;

  while (pToken) {
    const char *szScanDir = (iResolvedLen == 0) ? "." : szResolved;

    char szExact[260];
    int iExactLen;
    if (iResolvedLen == 0)
      iExactLen = snprintf(szExact, sizeof(szExact), "%s", pToken);
    else if (iResolvedLen == 1 && szResolved[0] == '/')
      iExactLen = snprintf(szExact, sizeof(szExact), "/%s", pToken);
    else
      iExactLen = snprintf(szExact, sizeof(szExact), "%s/%s", szResolved, pToken);

    struct stat sb;
    if (iExactLen > 0 && iExactLen < (int)sizeof(szExact) && stat(szExact, &sb) == 0) {
      iResolvedLen = s_findpath_append(szResolved, iResolvedLen, sizeof(szResolved), pToken);
      if (iResolvedLen < 0) return NULL;
      pToken = strtok_r(NULL, "/", &pSave);
      continue;
    }

    DIR *pDir = opendir(szScanDir);
    if (!pDir) return NULL;

    char szFound[256] = { 0 };
    struct dirent *pEntry;
    while ((pEntry = readdir(pDir)) != NULL) {
      if (strcasecmp(pEntry->d_name, pToken) == 0) {
        strncpy(szFound, pEntry->d_name, sizeof(szFound) - 1);
        break;
      }
    }
    closedir(pDir);

    if (szFound[0] == '\0') return NULL;

    iResolvedLen = s_findpath_append(szResolved, iResolvedLen, sizeof(szResolved), szFound);
    if (iResolvedLen < 0) return NULL;

    pToken = strtok_r(NULL, "/", &pSave);
  }

  return szResolved;
#endif
}

//-------------------------------------------------------------------------------------------------

bool ROLLERfexists(const char *szFile)
{
  FILE *pFile = fopen(szFile, "r");
  if (pFile) {
    fclose(pFile);
    return true;
  }

#ifdef IS_WINDOWS
  char szUpper[260] = { 0 };
  char szLower[260] = { 0 };
  int iLength = (int)strlen(szFile);

  for (int i = 0; i < iLength && i < (int)sizeof(szUpper); ++i) {
    szUpper[i] = toupper(szFile[i]);
    szLower[i] = tolower(szFile[i]);
  }

  pFile = fopen(szUpper, "r");
  if (pFile) {
    fclose(pFile);
    return true;
  }

  pFile = fopen(szLower, "r");
  if (pFile) {
    fclose(pFile);
    return true;
  }
#else
  const char *szResolved = ROLLERfindpath(szFile);
  if (szResolved) {
    pFile = fopen(szResolved, "r");
    if (pFile) { fclose(pFile); return true; }
  }
#endif

  return false;
}

//-------------------------------------------------------------------------------------------------

bool ROLLERdirexists(const char *szDir)
{
  struct stat sb;
  if (stat(szDir, &sb) == 0 && S_ISDIR(sb.st_mode)) {
    return true;
  }

#ifdef IS_WINDOWS
  char szUpper[260] = { 0 };
  char szLower[260] = { 0 };
  int iLength = (int)strlen(szDir);

  for (int i = 0; i < iLength && i < (int)sizeof(szUpper); ++i) {
    szUpper[i] = toupper(szDir[i]);
    szLower[i] = tolower(szDir[i]);
  }

  if (stat(szUpper, &sb) == 0 && S_ISDIR(sb.st_mode)) {
    return true;
  }

  if (stat(szLower, &sb) == 0 && S_ISDIR(sb.st_mode)) {
    return true;
  }
#else
  const char *szResolved = ROLLERfindpath(szDir);
  if (szResolved && stat(szResolved, &sb) == 0 && S_ISDIR(sb.st_mode)) {
    return true;
  }
#endif

  return false;
}

//-------------------------------------------------------------------------------------------------

FILE *ROLLERfopen(const char *szFile, const char *szMode)
{
  FILE *pFile = fopen(szFile, szMode);
  if (pFile) return pFile;

#ifdef IS_WINDOWS
  char szUpper[260] = { 0 };
  char szLower[260] = { 0 };
  int iLength = (int)strlen(szFile);

  for (int i = 0; i < iLength && i < (int)sizeof(szUpper); ++i) {
    szUpper[i] = toupper(szFile[i]);
    szLower[i] = tolower(szFile[i]);
  }

  pFile = fopen(szUpper, szMode);
  if (pFile) return pFile;

  pFile = fopen(szLower, szMode);
  return pFile;
#else
  const char *szResolved = ROLLERfindpath(szFile);
  if (szResolved) return fopen(szResolved, szMode);
  return NULL;
#endif
}

//-------------------------------------------------------------------------------------------------

int ROLLERopen(const char *szFile, int iOpenFlags)
{
  int iHandle = open(szFile, iOpenFlags);
  if (iHandle != -1) return iHandle;

#ifdef IS_WINDOWS
  char szUpper[260] = { 0 };
  char szLower[260] = { 0 };
  int iLength = (int)strlen(szFile);

  for (int i = 0; i < iLength && i < (int)sizeof(szUpper); ++i) {
    szUpper[i] = toupper(szFile[i]);
    szLower[i] = tolower(szFile[i]);
  }

  iHandle = open(szUpper, iOpenFlags);
  if (iHandle != -1) return iHandle;

  iHandle = open(szLower, iOpenFlags);
  return iHandle;
#else
  const char *szResolved = ROLLERfindpath(szFile);
  if (szResolved) return open(szResolved, iOpenFlags);
  return -1;
#endif
}

//-------------------------------------------------------------------------------------------------

int ROLLERremove(const char *szFile)
{
  int iSuccess = remove(szFile);
  if (iSuccess == 0) return 0;

#ifdef IS_WINDOWS
  char szUpper[260] = { 0 };
  char szLower[260] = { 0 };
  int iLength = (int)strlen(szFile);

  for (int i = 0; i < iLength && i < (int)sizeof(szUpper); ++i) {
    szUpper[i] = toupper(szFile[i]);
    szLower[i] = tolower(szFile[i]);
  }

  iSuccess = remove(szUpper);
  if (iSuccess == 0) return 0;

  iSuccess = remove(szLower);
  return iSuccess;
#else
  const char *szResolved = ROLLERfindpath(szFile);
  if (szResolved) return remove(szResolved);
  return iSuccess;
#endif
}

//-------------------------------------------------------------------------------------------------

int ROLLERrename(const char *szOldName, const char *szNewName)
{
  int iSuccess = rename(szOldName, szNewName);
  if (iSuccess == 0) return 0;

#ifdef IS_WINDOWS
  char szUpper[260] = { 0 };
  char szLower[260] = { 0 };
  int iLength = (int)strlen(szOldName);

  for (int i = 0; i < iLength && i < (int)sizeof(szUpper); ++i) {
    szUpper[i] = toupper(szOldName[i]);
    szLower[i] = tolower(szOldName[i]);
  }

  iSuccess = rename(szUpper, szNewName);
  if (iSuccess == 0) return 0;

  iSuccess = rename(szLower, szNewName);
  return iSuccess;
#else
  const char *szResolved = ROLLERfindpath(szOldName);
  if (szResolved) return rename(szResolved, szNewName);
  return iSuccess;
#endif
}

//-------------------------------------------------------------------------------------------------

uint32 ROLLERAddTimer(Uint32 uiFrequencyHz, SDL_NSTimerCallback callback, void *userdata)
{
  SDL_LockMutex(g_pTimerMutex);
  uint32 uiHandle = SDL_AddTimerNS(HZ_TO_NS(uiFrequencyHz), callback, userdata);

  //find empty timer slot
  bool bFoundSlot = false;
  for (int i = 0; i < MAX_TIMERS; ++i) {
    if (timerDataAy[i].uiHandle == 0) {
      bFoundSlot = true;
      timerDataAy[i].uiHandle = uiHandle;
      timerDataAy[i].ullTargetSDLTicksNS = HZ_TO_NS(uiFrequencyHz);
      timerDataAy[i].ullLastSDLTicksNS = SDL_GetTicksNS();
      break;
    }
  }
  SDL_UnlockMutex(g_pTimerMutex);

  if (!bFoundSlot) {
    //too many timers!
    assert(0);
    ErrorBoxExit("Too many timers!");
  }

  return uiHandle;
}

//-------------------------------------------------------------------------------------------------

void ROLLERRemoveTimer(uint32 uiHandle)
{
  SDL_RemoveTimer(uiHandle);

  SDL_LockMutex(g_pTimerMutex);
  //clear timer data
  for (int i = 0; i < MAX_TIMERS; ++i) {
    if (timerDataAy[i].uiHandle == uiHandle) {
      memset(&timerDataAy[i], 0, sizeof(tTimerData));
    }
  }
  SDL_UnlockMutex(g_pTimerMutex);
}

//-------------------------------------------------------------------------------------------------

int ROLLERfilelength(const char *szFile)
{
#ifdef IS_WINDOWS
  int iFileHandle = ROLLERopen(szFile, O_RDONLY | O_BINARY); //0x200 is O_BINARY in WATCOM/h/fcntl.h

  if (iFileHandle == -1)
    return -1;

  int iSize = _filelength(iFileHandle);

  close(iFileHandle);
  return iSize;
#else
  FILE *fp = ROLLERfopen(szFile, "rb");
  if (!fp)
    return -1;

  fseek(fp, 0, SEEK_END);
  int iSize = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  fclose(fp);
  return iSize;
#endif
}

//-------------------------------------------------------------------------------------------------

static uint32 g_uiRandState = 1;

void ROLLERsrand(unsigned int uiSeed)
{
  g_uiRandState = (uint32)uiSeed;
}

//-------------------------------------------------------------------------------------------------

int ROLLERrandRaw(void)
{
  g_uiRandState = g_uiRandState * 1103515245u + 12345u;
  return (int)((g_uiRandState >> 16) & 0x7FFFu);
}

//-------------------------------------------------------------------------------------------------

int ROLLERrand()
{
  return GetHighOrderRand(0x7FFF, ROLLERrandRaw());
}

//-------------------------------------------------------------------------------------------------
//g_pTimerMutex MUST BE LOCKED before calling this function
tTimerData *GetTimerData(SDL_TimerID timerID)
{
  for (int i = 0; i < MAX_TIMERS; ++i) {
    if (timerDataAy[i].uiHandle == timerID) {
      return &timerDataAy[i];
    }
  }
  return NULL;
}

//-------------------------------------------------------------------------------------------------

static bool ROLLERGetTimerInterval(SDL_TimerID timerID, uint64 *pUllInterval)
{
  SDL_LockMutex(g_pTimerMutex);
  tTimerData *pTimerData = GetTimerData(timerID);
  if (!pTimerData) {
    SDL_UnlockMutex(g_pTimerMutex);
    return false;
  }

  pTimerData->ullCurrSDLTicksNS = SDL_GetTicksNS();
  int64 llNSSinceLast = (int64)pTimerData->ullCurrSDLTicksNS - (int64)pTimerData->ullLastSDLTicksNS;
  int64 llDelta = llNSSinceLast - (int64)pTimerData->ullTargetSDLTicksNS;
  if (llDelta < 0)
    llDelta = 0;
  pTimerData->ullLastSDLTicksNS = pTimerData->ullCurrSDLTicksNS;
  *pUllInterval = pTimerData->ullTargetSDLTicksNS - llDelta;
  SDL_UnlockMutex(g_pTimerMutex);

  return true;
}

//-------------------------------------------------------------------------------------------------

Uint64 SDLTickTimerCallback(void *userdata, SDL_TimerID timerID, Uint64 interval)
{
  tick_clock_step();
  uint64 ullRet = 0;

  if (!ROLLERGetTimerInterval(timerID, &ullRet))
    return 0;

  return ullRet;
}

//-------------------------------------------------------------------------------------------------

Uint64 SDLS7TimerCallback(void *userdata, SDL_TimerID timerID, Uint64 interval)
{
  SOSTimerCallbackS7();
  uint64 ullRet = 0;

  if (!ROLLERGetTimerInterval(timerID, &ullRet))
    return 0;

  return ullRet;
}

//-------------------------------------------------------------------------------------------------

int IsCDROMDevice(const char *szPath)
{
#if CDROM_SUPPORT
  int fd = open(szPath, O_RDONLY | O_NONBLOCK);
  if (fd < 0)
    return 0;

  int result = ioctl(fd, CDROM_GET_CAPABILITY, 0);
  close(fd);
  return (result != -1);
#else
  return 0;
#endif
}

//-------------------------------------------------------------------------------------------------

void ReplaceExtension(char *szFilename, const char *szNewExt)
{
  char *szDot = strrchr(szFilename, '.');
  char *szSlash = strrchr(szFilename, '/');
  char *szBackslash = strrchr(szFilename, '\\');

  char *szLastSeparator = (szSlash > szBackslash) ? szSlash : szBackslash;

  if (szDot && (szLastSeparator == NULL || szDot > szLastSeparator)) {
    strcpy(szDot, szNewExt);
  } else {
    strcat(szFilename, szNewExt);
  }
}

//-------------------------------------------------------------------------------------------------

void ErrorBoxExit(const char *szErrorMsgFormat, ...)
{
  va_list args;
  va_start(args, szErrorMsgFormat);
  char szErrorMsg[2048];
  int iLen = vsnprintf(szErrorMsg, sizeof(szErrorMsg) - 1, szErrorMsgFormat, args);
  if (iLen >= 0)
    szErrorMsg[iLen] = '\0';
  va_end(args);

  SDL_ShowMessageBox(&(SDL_MessageBoxData)
  {
    .title = "ROLLER",
      .message = szErrorMsg,
      .flags = SDL_MESSAGEBOX_ERROR,
      .numbuttons = 1,
      .buttons = (SDL_MessageBoxButtonData[]){
        {.flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, .text = "OK" }
    },
  }, NULL);

  ShutdownSDL();
  exit(0);
}

//-------------------------------------------------------------------------------------------------

void autoselectsoundlanguage() // Add by ROLLER to auto-select languagename when config.ini is not found
{
  SDL_Log("autoselectsoundlanguage: config.ini not found");

  // Set default language as English
  sscanf(lang[0], "%s", languagename);
  language = 0;

  for (int i = 0; i < languages; i++) {
    char audioFileName[32];
    char textFileName[32];

    const char *szTextExt = (char *)TextExt + i * 4;
    const char *szLangExt = (const char *)SampleExt + i * 4;

    snprintf(textFileName, sizeof(textFileName), "./CONFIG.%s", szTextExt); // e.g., CONFIG.ENG, CONFIG.FRA, CONFIG.GER, CONFIG.BPO, CONFIG.SAS.
    snprintf(audioFileName, sizeof(audioFileName), "./GO.%s", szLangExt); // e.g., GO.RAW, GO.RFR, GO.RGE, GO.RBP, GO.RSS.

    //SDL_Log("lang[%i]: %s", i, lang[i]);
    //SDL_Log("textFileName[%i]: %s", i, textFileName);
    //SDL_Log("audioFileName[%i]: %s", i, audioFileName);
    if (ROLLERfexists(textFileName) && ROLLERfexists(audioFileName)) {
      sscanf(lang[i], "%s", languagename);
      language = i;
      SDL_Log("autoselectsoundlanguage: select language[%i]: %s - %s %s", language, languagename, szTextExt, szLangExt);
      break;
    }
  }
}

//-------------------------------------------------------------------------------------------------

int GetHighOrderRand(int iRange, int iRandValue)
{
  int64 llProduct = (int64)iRange * iRandValue;
  return (int)(llProduct >> ROLLER_RAND_BITS);
}

//-------------------------------------------------------------------------------------------------

int ReadUnalignedInt(const void *pData)
{
  const uint8 *pBytes = (const uint8*)pData;
  return (uint32)pBytes[0] | ((uint32)pBytes[1] << 8) | ((uint32)pBytes[2] << 16) | ((uint32)pBytes[3] << 24);
}

//-------------------------------------------------------------------------------------------------

// Globals for CD audio management
int g_iNumTracks = 0;
int g_iCurrentTrack = -1;
int g_iStartTrack = -1;   // For PlayTrack4
int g_iTrackCount = 0;    // For PlayTrack4
int g_iCDVolume = 0;
bool g_bRepeat = false;
bool g_bUsingRealCD = false;
bool g_bGotAudioInfo = false;
bool g_bSentCDVolWarning = false;

// For fallback to audio files if no CD
static SDL_AudioStream *g_pCurrentStream = NULL;
static uint8 *g_pAudioData = NULL;
static uint32 g_uiAudioLen = 0;

#ifdef IS_WINDOWS
static MCIDEVICEID g_wDeviceID = 0;
#else
static int g_iCDHandle = -1;
#endif

void ROLLERGetAudioInfo()
{
  //only get info once
  if (g_bGotAudioInfo)
    return;
  g_bGotAudioInfo = true;

  g_iNumTracks = 0;
  g_bUsingRealCD = false;

#ifdef IS_WINDOWS
    // Windows: Use MCI (Media Control Interface)
  MCI_OPEN_PARMS mciOpenParms;
  MCI_SET_PARMS mciSetParms;
  MCI_STATUS_PARMS mciStatusParms;

  // Open CD audio device
  mciOpenParms.lpstrDeviceType = (LPCSTR)MCI_DEVTYPE_CD_AUDIO;
  if (mciSendCommand(0, MCI_OPEN, MCI_OPEN_TYPE | MCI_OPEN_TYPE_ID,
                     (DWORD_PTR)&mciOpenParms) == 0) {
    g_wDeviceID = mciOpenParms.wDeviceID;

    // Set time format to tracks
    mciSetParms.dwTimeFormat = MCI_FORMAT_TMSF;
    mciSendCommand(g_wDeviceID, MCI_SET, MCI_SET_TIME_FORMAT,
                  (DWORD_PTR)&mciSetParms);

    // Get number of tracks
    mciStatusParms.dwItem = MCI_STATUS_NUMBER_OF_TRACKS;
    if (mciSendCommand(g_wDeviceID, MCI_STATUS, MCI_STATUS_ITEM,
                       (DWORD_PTR)&mciStatusParms) == 0) {
      g_iNumTracks = (int)mciStatusParms.dwReturn;
      g_bUsingRealCD = true;
    }
  }

#elif defined(IS_LINUX)
    // Linux: Try to open CD device
  const char *szCDDevices[] = {
      "/dev/cdrom",
      "/dev/sr0",
      "/dev/sr1",
      "/dev/dvd"
  };

  for (int i = 0; i < sizeof(szCDDevices) / sizeof(szCDDevices[0]); i++) {
    g_iCDHandle = open(szCDDevices[i], O_RDONLY | O_NONBLOCK);
    if (g_iCDHandle >= 0) {
      struct cdrom_tochdr tochdr;
      if (ioctl(g_iCDHandle, CDROMREADTOCHDR, &tochdr) == 0) {
          // First track is usually data (track 1), audio starts at track 2
        g_iNumTracks = tochdr.cdth_trk1;  // Last track number
        g_bUsingRealCD = true;
        break;
      }
      close(g_iCDHandle);
      g_iCDHandle = -1;
    }
  }
#endif

    // If no real CD found, check for ripped tracks
  if (!g_bUsingRealCD) {
    char szTrackFile[ROLLER_MAX_PATH];

    // Look for ripped tracks
    for (int iTrack = 2; iTrack <= 99; iTrack++) {
      if (ROLLERFindAudioTrackPath(iTrack, szTrackFile, sizeof(szTrackFile))) {
        g_iNumTracks = iTrack;  // Keep counting up
      } else if (iTrack > 2) {
        break;  // Stop at first missing track after track 2
      }
    }
  }
}

//-------------------------------------------------------------------------------------------------

void ROLLERStopTrack()
{
  SDL_Log("ROLLERStopTrack %d", g_iCurrentTrack);

  if (g_bUsingRealCD) {
#ifdef IS_WINDOWS
    if (g_wDeviceID) {
      mciSendCommand(g_wDeviceID, MCI_STOP, 0, 0);
    }
#elif defined(IS_LINUX)
    if (g_iCDHandle >= 0) {
      ioctl(g_iCDHandle, CDROMSTOP);
    }
#endif
  } else {
      // Stop file playback
    if (g_pCurrentStream) {
      SDL_DestroyAudioStream(g_pCurrentStream);
      g_pCurrentStream = NULL;
    }
    if (g_pAudioData) {
      SDL_free(g_pAudioData);
      g_pAudioData = NULL;
    }
    g_uiAudioLen = 0;
  }

  g_iCurrentTrack = -1;
}

//-------------------------------------------------------------------------------------------------

void ROLLERPlayTrack(int iTrack)
{
  int iStarted = 0;

// CD audio tracks start at 2 (track 1 is data)
  if (iTrack < 2 || iTrack > g_iNumTracks) {
    return;
  }

  ROLLERStopTrack();
  SDL_Log("ROLLERPlayTrack %d", iTrack);

  if (g_bUsingRealCD) {
#ifdef IS_WINDOWS
    if (g_wDeviceID) {
      MCI_PLAY_PARMS mciPlayParms;
      mciPlayParms.dwFrom = MCI_MAKE_TMSF(iTrack, 0, 0, 0);
      mciPlayParms.dwTo = MCI_MAKE_TMSF(iTrack + 1, 0, 0, 0);
      mciSendCommand(g_wDeviceID, MCI_PLAY, MCI_FROM | MCI_TO,
                    (DWORD_PTR)&mciPlayParms);
      iStarted = -1;
    }
#elif defined(IS_LINUX)
    if (g_iCDHandle >= 0) {
      struct cdrom_ti ti;
      ti.cdti_trk0 = iTrack;
      ti.cdti_ind0 = 0;
      ti.cdti_trk1 = iTrack;
      ti.cdti_ind1 = 0;
      ioctl(g_iCDHandle, CDROMPLAYTRKIND, &ti);
      iStarted = -1;
    }
#endif
  } else {
      // Play from file
    char szTrackFile[ROLLER_MAX_PATH];
    SDL_AudioSpec spec;

    if (ROLLERFindAudioTrackPath(iTrack, szTrackFile, sizeof(szTrackFile))) {
      SDL_IOStream *io = SDL_IOFromFile(szTrackFile, "rb");
      if (!io) {
        SDL_Log("Failed to open CD audio track '%s': %s", szTrackFile, SDL_GetError());
        return;
      }

      if (SDL_LoadWAV_IO(io, true, &spec, &g_pAudioData, &g_uiAudioLen)) {
        g_pCurrentStream = SDL_OpenAudioDeviceStream(
            SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
            &spec, NULL, NULL);

        if (g_pCurrentStream) {
          float fGain = g_iCDVolume / 255.0f;

          SDL_SetAudioStreamGain(g_pCurrentStream, fGain);
          if (SDL_PutAudioStreamData(g_pCurrentStream, g_pAudioData, g_uiAudioLen)) {
            SDL_ResumeAudioStreamDevice(g_pCurrentStream);
            SDL_Log("Started CD audio track '%s' (%u bytes)",
                    szTrackFile, (unsigned int)g_uiAudioLen);
            iStarted = -1;
          } else {
            SDL_Log("Failed to queue CD audio track '%s': %s", szTrackFile, SDL_GetError());
            SDL_DestroyAudioStream(g_pCurrentStream);
            g_pCurrentStream = NULL;
            SDL_free(g_pAudioData);
            g_pAudioData = NULL;
            g_uiAudioLen = 0;
          }
        } else {
          SDL_Log("Failed to open audio device for CD audio track '%s': %s",
                  szTrackFile, SDL_GetError());
          SDL_free(g_pAudioData);
          g_pAudioData = NULL;
          g_uiAudioLen = 0;
        }
      } else {
        SDL_Log("Failed to load CD audio WAV '%s': %s", szTrackFile, SDL_GetError());
      }
    } else {
      SDL_Log("CD audio track %02d not found in audio folder", iTrack);
    }
    // Add OGG/MP3 support here if using SDL_mixer
  }

  if (iStarted)
    g_iCurrentTrack = iTrack;
}

//-------------------------------------------------------------------------------------------------

void ROLLERPlayTrack4(int iStartTrack)
{
  g_iStartTrack = iStartTrack;
  g_iTrackCount = 4;
  g_bRepeat = false;

  ROLLERPlayTrack(iStartTrack);
}

//-------------------------------------------------------------------------------------------------

void ROLLERSetAudioVolume(int iVolume)
{
  g_iCDVolume = iVolume;

  if (g_bUsingRealCD) {
#ifdef IS_WINDOWS
    if (g_wDeviceID != 0) {
        // Method 1: Using MCI
      MCI_DGV_SETAUDIO_PARMS mciSetAudioParms;
      mciSetAudioParms.dwItem = MCI_DGV_SETAUDIO_VOLUME;
      mciSetAudioParms.dwValue = (iVolume * 1000) / 255;  // MCI uses 0-1000

      DWORD dwResult = mciSendCommand(g_wDeviceID, MCI_SETAUDIO,
                                      MCI_DGV_SETAUDIO_VALUE | MCI_DGV_SETAUDIO_ITEM,
                                      (DWORD_PTR)&mciSetAudioParms);
      if (dwResult != 0) {
        if (!g_bSentCDVolWarning) {
          SDL_Log("CD volume control not supported on this system");
          g_bSentCDVolWarning = true;
        }
      }
    }
#elif defined(IS_LINUX)
    if (g_iCDHandle >= 0) {
        // Linux CD-ROM volume control
      struct cdrom_volctrl volume;

      // All channels set to same volume (0-255 range)
      uint8 byLinuxVolume = iVolume;
      volume.channel0 = byLinuxVolume;
      volume.channel1 = byLinuxVolume;
      volume.channel2 = byLinuxVolume;
      volume.channel3 = byLinuxVolume;

      ioctl(g_iCDHandle, CDROMVOLCTRL, &volume);
    }
#endif
  } else {
      // Set volume for SDL audio stream
    if (g_pCurrentStream) {
        // SDL3 gain: 1.0 = normal, 0.0 = silence
      float fGain = iVolume / 255.0f;
      SDL_SetAudioStreamGain(g_pCurrentStream, fGain);
    }
  }
}

//-------------------------------------------------------------------------------------------------
// Call this periodically to handle track transitions and repeat
void UpdateAudioTracks(void)
{
  bool bTrackFinished = false;

  if (g_iCurrentTrack < 0) {
    return;
  }

  if (g_bUsingRealCD) {
#ifdef IS_WINDOWS
    if (g_wDeviceID != 0) {
      MCI_STATUS_PARMS mciStatusParms;

      // First check if stopped
      mciStatusParms.dwItem = MCI_STATUS_MODE;
      if (mciSendCommand(g_wDeviceID, MCI_STATUS, MCI_STATUS_ITEM,
                         (DWORD_PTR)&mciStatusParms) == 0) {
        if (mciStatusParms.dwReturn == MCI_MODE_STOP) {
          bTrackFinished = true;
        } else if (mciStatusParms.dwReturn == MCI_MODE_PLAY) {
            // Check if we're still on the same track
          mciStatusParms.dwItem = MCI_STATUS_CURRENT_TRACK;
          if (mciSendCommand(g_wDeviceID, MCI_STATUS, MCI_STATUS_ITEM,
                             (DWORD_PTR)&mciStatusParms) == 0) {
               // If we've moved past our track, it finished
            if ((int)mciStatusParms.dwReturn != g_iCurrentTrack) {
              bTrackFinished = true;
            }
          }

          // Alternative: Check position vs track length
          mciStatusParms.dwItem = MCI_STATUS_POSITION;
          if (mciSendCommand(g_wDeviceID, MCI_STATUS, MCI_STATUS_ITEM,
                             (DWORD_PTR)&mciStatusParms) == 0) {
            int iCurrentTrackPos = MCI_TMSF_TRACK(mciStatusParms.dwReturn);

            // If position shows we're on a different track or at track 0
            if (iCurrentTrackPos != g_iCurrentTrack || iCurrentTrackPos == 0) {
              bTrackFinished = true;
            }
          }
        }
      }
    }
#elif defined(IS_LINUX)
    if (g_iCDHandle >= 0) {
      struct cdrom_subchnl subchnl;
      subchnl.cdsc_format = CDROM_MSF;
      if (ioctl(g_iCDHandle, CDROMSUBCHNL, &subchnl) == 0) {
        if (subchnl.cdsc_audiostatus == CDROM_AUDIO_COMPLETED ||
            subchnl.cdsc_audiostatus == CDROM_AUDIO_NO_STATUS) {
          bTrackFinished = true;
        }
      }
    }
#endif
  } else if (g_pCurrentStream) {
      // Check file playback
    int iQueued = SDL_GetAudioStreamQueued(g_pCurrentStream);
    if (iQueued == 0) {
      bTrackFinished = true;
    }
  }

  if (bTrackFinished) {
    if (g_bRepeat) {
      SDL_Log("Repeat track %d", g_iCurrentTrack);
        // Repeat current track
      ROLLERPlayTrack(g_iCurrentTrack);
    } else if (g_iTrackCount > 1) {
      SDL_Log("Advance track");
        // PlayTrack4 sequence
      g_iTrackCount--;
      int iNextTrack = g_iCurrentTrack + 1;
      ROLLERPlayTrack(iNextTrack);
    } else {
      g_iTrackCount = 4;
      ROLLERPlayTrack(g_iStartTrack);
    }
  }
}

//-------------------------------------------------------------------------------------------------

void CleanupAudioCD(void)
{
  ROLLERStopTrack();

#if defined(IS_LINUX)
  if (g_iCDHandle >= 0) {
    close(g_iCDHandle);
    g_iCDHandle = -1;
  }
#endif
}

//-------------------------------------------------------------------------------------------------
