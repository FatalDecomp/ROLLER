#include "roller.h"
#include "rollersound.h"
#include "rollerinput.h"
#include "3d.h"
#include "sound.h"
#include "frontend.h"
#include "func2.h"
#include "graphics.h"
#include "menu_render.h"
#include "debug_overlay.h"
#include "snapshot.h"
#include "rollercd.h"
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <ctype.h>
#include <SDL3_image/SDL_image.h>
#include <wildmidi_lib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <cdio/cdio.h>
#include <cdio/iso9660.h>
#include <cdio/disc.h>
#include <cdio/cd_types.h>
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
#ifdef IS_LINUX
#include <linux/cdrom.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#define CDROM_SUPPORT 1
#endif
//-------------------------------------------------------------------------------------------------

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
bool g_bPaletteSet = false;
bool g_bForceMaxDraw = true;
bool g_bAINoCheatStart = false;  //  Set true to not give AI cars an advantage during race start
bool g_bFixCarMenuBug = true;
int g_iCurrentSong = 0;
uint64 g_ullTimer150Ms = 0;

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
DebugOverlay *ROLLERGetDebugOverlay(void) { return s_pDebugOverlay; }

static void DeferGPUPresentation(int iFrames)
{
  if (s_iGPUPresentSkipFrames < iFrames)
    s_iGPUPresentSkipFrames = iFrames;
}

bool ROLLERGpuPresentationSuspended(void)
{
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

  // Convert indexed framebuffer directly into mapped transfer buffer
  void *mapped = SDL_MapGPUTransferBuffer(s_pGPUDevice, s_pTransferBuffer, false);
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
  if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmdBuf, s_pWindow,
          &swapchainTex, &swW, &swH) || !swapchainTex) {
    SDL_CancelGPUCommandBuffer(cmdBuf);
    return;
  }

  // Blit with aspect-ratio preservation
  SDL_GPUBlitInfo blitInfo = {0};
  blitInfo.source.texture = s_pGameTexture;
  blitInfo.source.w = winw;
  blitInfo.source.h = winh;

  float fWindowAspect = (float)swW / (float)swH;
  float fTextureAspect = (float)winw / (float)winh;

  if (fWindowAspect > fTextureAspect) {
    Uint32 dstW = (Uint32)(fTextureAspect * swH);
    blitInfo.destination.texture = swapchainTex;
    blitInfo.destination.x = (swW - dstW) / 2;
    blitInfo.destination.y = 0;
    blitInfo.destination.w = dstW;
    blitInfo.destination.h = swH;
  } else {
    Uint32 dstH = (Uint32)(swW / fTextureAspect);
    blitInfo.destination.texture = swapchainTex;
    blitInfo.destination.x = 0;
    blitInfo.destination.y = (swH - dstH) / 2;
    blitInfo.destination.w = swW;
    blitInfo.destination.h = dstH;
  }
  blitInfo.filter = SDL_GPU_FILTER_NEAREST;
  blitInfo.load_op = SDL_GPU_LOADOP_CLEAR;
  blitInfo.clear_color = (SDL_FColor){0.0f, 0.0f, 0.0f, 1.0f};

  SDL_BlitGPUTexture(cmdBuf, &blitInfo);
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
  if (!SDL_WaitAndAcquireGPUSwapchainTexture(pCmdBuf, s_pWindow,
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

    debug_overlay_handle_event(s_pDebugOverlay, &e);
    if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_GRAVE)
      debug_overlay_toggle(s_pDebugOverlay);
  }

  PresentDebugOverlayOnly();
}

//-------------------------------------------------------------------------------------------------

void ToggleFullscreen()
{
  if (!s_pWindow)
    return;

  SDL_WindowFlags uiFlags = SDL_GetWindowFlags(s_pWindow);
  bool bFullscreen = (uiFlags & SDL_WINDOW_FULLSCREEN) != 0;

  DeferGPUPresentation(3);
  if (s_pGPUDevice)
    SDL_WaitForGPUIdle(s_pGPUDevice);

  if (!SDL_SetWindowFullscreen(s_pWindow, !bFullscreen)) {
    SDL_Log("SDL_SetWindowFullscreen failed: %s", SDL_GetError());
  } else {
    SDL_SyncWindow(s_pWindow);
  }

  if (s_pGPUDevice)
    SDL_WaitForGPUIdle(s_pGPUDevice);
  DeferGPUPresentation(3);
}

//-------------------------------------------------------------------------------------------------

int InitSDL(char *whiplash_root, const char *midi_root)
{
  SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
#if defined(_WIN32)
  SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_DIRECTINPUT, "0", SDL_HINT_OVERRIDE);
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
    // Change to the base path of the application
    strncpy(whiplash_root, SDL_GetBasePath(), 260);
    if (whiplash_root) {
      chdir(whiplash_root);
    }
  }

  g_pTimerMutex = SDL_CreateMutex();
  g_pDigiMutex = SDL_CreateMutex();

  if (g_bSnapshotMode) {
    // Headless capture path: skip window/GPU/audio/MIDI init entirely.
    // The dummy SDL_VIDEO_DRIVER hint set by main() lets SDL_INIT_VIDEO
    // succeed without a display server.
    return 0;
  }

  s_pWindow = SDL_CreateWindow("ROLLER", 640, 400, SDL_WINDOW_RESIZABLE);
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

  // Transfer buffer for CPU->GPU framebuffer upload
  SDL_GPUTransferBufferCreateInfo tbInfo = {0};
  tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  tbInfo.size = 640 * 400 * 4;
  s_pTransferBuffer = SDL_CreateGPUTransferBuffer(s_pGPUDevice, &tbInfo);
  if (!s_pTransferBuffer) {
    ErrorBoxExit("Couldn't create GPU transfer buffer: %s", SDL_GetError());
    return 1;
  }

  SDL_Surface *pIcon = IMG_Load("roller.ico");
  SDL_SetWindowIcon(s_pWindow, pIcon);

  // Move the window to the display where the mouse is currently located
  float mouseX, mouseY;
  SDL_GetGlobalMouseState(&mouseX, &mouseY);
  int displayIndex = SDL_GetDisplayForPoint(&(SDL_Point) { (int)mouseX, (int)mouseY });
  int sdl_window_centered = SDL_WINDOWPOS_CENTERED_DISPLAY(displayIndex);
  SDL_SetWindowPosition(s_pWindow, sdl_window_centered, sdl_window_centered);

  InputInit();

  char localMidiPath[256];
  if (midi_root) {
    strcpy(localMidiPath, midi_root);
    size_t lenMidiPath = strlen(localMidiPath);
    if (lenMidiPath > 0 && (localMidiPath[lenMidiPath - 1] != '/' || localMidiPath[lenMidiPath - 1] != '\\')) {
      localMidiPath[lenMidiPath] = '/';
      localMidiPath[lenMidiPath+1] = '\0';
    }
  } else {
    midi_root = SDL_GetBasePath();
    if (midi_root) {
      strcpy(localMidiPath, midi_root);
    } else {
      strcpy(localMidiPath, "./");
    }
  }
  strcat(localMidiPath, "midi/wildmidi.cfg");
  // Initialize MIDI with WildMidi
  if (!MIDI_Init(localMidiPath)) {
    SDL_Log("Failed to initialize WildMidi. Please check your configuration file '%s'.", localMidiPath);
  }

  return 0;
}

//-------------------------------------------------------------------------------------------------

void SDLCALL FileCallback(void *pUserData, const char *const *filelist, int iFilter)
{
  tDialogResult *pResult = (tDialogResult *)pUserData;

  if (!filelist || !filelist[0]) {
    pResult->bCancelled = true;
  } else {
    SDL_strlcpy(pResult->szPath, filelist[0], ROLLER_MAX_PATH);
  }
  pResult->bDone = true;
}

//-------------------------------------------------------------------------------------------------

void InitFATDATA(const char *szDataRoot)
{
  if (!szDataRoot)
    return;

  // check if data folder exists (case-insensitive for linux)
  if (!ROLLERdirexists("./FATDATA") && !ROLLERdirexists("./fatdata")) {
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
  }

  //check if extraction was successful
  if (!ROLLERdirexists("./FATDATA") && !ROLLERdirexists("./fatdata")) {
    ErrorBoxExit("The folder FATDATA does not exist.\nROLLER requires the FATDATA folder assets from a retail copy of the game.");
  }
  
  // if the extracted audio tracks are present, enable CD music.
  FILE *pTrack = ROLLERfopen("./audio/track02.wav", "rb");
  if (pTrack) {
    fclose(pTrack);
    MusicCard = 0;
    MusicCD = -1;
  }
}

//-------------------------------------------------------------------------------------------------

void InitREPLAYS(const char *szDataRoot)
{
  #ifdef IS_WINDOWS
  mkdir("./REPLAYS");
  #else
  mkdir("./REPLAYS", 0755);
  #endif
}

//-------------------------------------------------------------------------------------------------

void ShutdownSDL()
{
  if (!g_bSnapshotMode) {
    DIGIClearAllStream();
    MIDI_Shutdown();

    InputShutdown();

    debug_overlay_destroy(s_pDebugOverlay);
    s_pDebugOverlay = NULL;
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
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_MINIMIZED:
        case SDL_EVENT_WINDOW_RESTORED:
        case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
        case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
          DeferGPUPresentation(2);
          break;
        default:
          break;
      }
    }
    debug_overlay_handle_event(s_pDebugOverlay, &e);
    InputHandleEvent(&e);

    if (e.type == SDL_EVENT_KEY_DOWN) {
      if (e.key.scancode == SDL_SCANCODE_GRAVE) {
        debug_overlay_toggle(s_pDebugOverlay);
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
      if (e.key.key == SDLK_F10) {
        if (frontend_on) {
          MenuRenderer *mr = GetMenuRenderer();
          MenuRenderMode mode = menu_render_get_pending_mode(mr);
          menu_render_set_mode(mr, mode == MENU_RENDER_GPU
            ? MENU_RENDER_SOFTWARE : MENU_RENDER_GPU);
          InputSaveConfig();
          SDL_Log("Menu render mode: %s",
            mode == MENU_RENDER_GPU ? "software" : "GPU");
        }
        continue;
      } else if (e.key.key == SDLK_RETURN) {
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
      if (sc < SDL_arraysize(sdl_to_set1) && sdl_to_set1[sc]) {
        uint8 byRawCode = sdl_to_set1[sc];
        if (e.type == SDL_EVENT_KEY_UP) {
          byRawCode |= 0x80;  // Set high bit for release
        }
        key_handler(byRawCode);
      }
    }

  }
  InputUpdate();
  InputUpdateMenuControls();
  //UpdateSDLWindow();
#if _DEBUG
  UpdateDebugLoop(); // Add by ROLLER
#endif // _DEBUG
  uint64 ullCurTicksMs = SDL_GetTicks();
  if (ullCurTicksMs > g_ullTimer150Ms) {
    g_ullTimer150Ms = ullCurTicksMs + 150;
    UpdateAudioTracks();
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
    char szTrackFile[256];
    FILE *fp;

    // Look for ripped tracks
    for (int iTrack = 2; iTrack <= 99; iTrack++) {
      sprintf(szTrackFile, "./audio/track%02d.wav", iTrack);
      fp = ROLLERfopen(szTrackFile, "rb");

      if (fp) {
        fclose(fp);
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
  }
}

//-------------------------------------------------------------------------------------------------

void ROLLERPlayTrack(int iTrack)
{
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
    }
#elif defined(IS_LINUX)
    if (g_iCDHandle >= 0) {
      struct cdrom_ti ti;
      ti.cdti_trk0 = iTrack;
      ti.cdti_ind0 = 0;
      ti.cdti_trk1 = iTrack;
      ti.cdti_ind1 = 0;
      ioctl(g_iCDHandle, CDROMPLAYTRKIND, &ti);
    }
#endif
  } else {
      // Play from file
    char szTrackFile[256];
    SDL_AudioSpec spec;

    sprintf(szTrackFile, "../audio/track%02d.wav", iTrack);
    FILE *fp = ROLLERfopen(szTrackFile, "rb");
    if (fp) {
      fclose(fp);

      SDL_IOStream *io = SDL_IOFromFile(szTrackFile, "rb");
      if (io) {
        if (SDL_LoadWAV_IO(io, true, &spec, &g_pAudioData, &g_uiAudioLen)) {

          g_pCurrentStream = SDL_OpenAudioDeviceStream(
              SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
              &spec, NULL, NULL);

          if (g_pCurrentStream) {
            SDL_PutAudioStreamData(g_pCurrentStream, g_pAudioData, g_uiAudioLen);
            SDL_ResumeAudioStreamDevice(g_pCurrentStream);
            float fGain = g_iCDVolume / 255.0f;
            SDL_SetAudioStreamGain(g_pCurrentStream, fGain);
          }
        }
      }
    }
    // Add OGG/MP3 support here if using SDL_mixer
  }

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
            if (mciStatusParms.dwReturn != g_iCurrentTrack) {
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
