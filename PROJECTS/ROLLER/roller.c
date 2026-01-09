#include "roller.h"
#include "3d.h"
#include "sound.h"
#include "frontend.h"
#include "func2.h"
#include "graphics.h"
#include "config.h"
#include <assert.h>
#include <string.h>
#include <ctype.h>
#include <SDL3_image/SDL_image.h>
#include <wildmidi_lib.h>
#include <fcntl.h>
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
//TODO: Linux/Mac would need libcdio or similar
#endif

//-------------------------------------------------------------------------------------------------

#define MAX_TIMERS 16

//-------------------------------------------------------------------------------------------------

typedef struct
{
  uint32 uiHandle;
  uint64 ullTargetSDLTicksNS;
  uint64 ullLastSDLTicksNS;
  uint64 ullCurrSDLTicksNS;
} tTimerData;

//-------------------------------------------------------------------------------------------------

static SDL_Window *s_pWindow = NULL;
static SDL_Renderer *s_pRenderer = NULL;
static SDL_Texture *s_pWindowTexture = NULL;
static SDL_Texture *s_pDebugTexture = NULL;
SDL_Gamepad *g_pController1 = NULL;
SDL_Gamepad *g_pController2 = NULL;
tJoyPos g_rollerJoyPos;
SDL_JoystickID g_joyId1 = 0;
SDL_JoystickID g_joyId2 = 0;
bool g_bPaletteSet = false;
bool g_bForceMaxDraw = false; //TODO: figure out why this causes some flickering, also load from INI file
uint8 testbuf[4096];
static uint8 *s_pRGBBuffer = NULL;
static uint8 *s_pDebugBuffer = NULL;
uint64 g_ullTimer150Ms = 0;

SDL_Mutex *g_pTimerMutex = NULL;
tTimerData timerDataAy[MAX_TIMERS] = { 0 };

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

void ConvertIndexedToRGB(const uint8 *pIndexed, const tColor *pPalette, uint8 *pRGB, int width, int height)
{
  if (!pIndexed || !pPalette || !pRGB)
    return;

  for (int i = 0; i < width * height; ++i) {
    const tColor *c = &pPalette[pIndexed[i]];
    pRGB[i * 3 + 0] = (c->byR * 255) / 63;
    pRGB[i * 3 + 1] = (c->byG * 255) / 63;
    pRGB[i * 3 + 2] = (c->byB * 255) / 63;
  }
}

//-------------------------------------------------------------------------------------------------

void UpdateSDLWindow()
{
  if (!g_bPaletteSet)
    return;

  ConvertIndexedToRGB(scrbuf, pal_addr, s_pRGBBuffer, winw, winh);
  ConvertIndexedToRGB(testbuf, pal_addr, s_pDebugBuffer, 64, 64);

  SDL_UpdateTexture(s_pWindowTexture, NULL, s_pRGBBuffer, winw * 3);
  SDL_UpdateTexture(s_pDebugTexture, NULL, s_pDebugBuffer, 64 * 3);

  // Get current window size
  int iWindowWidth, iWindowHeight;
  SDL_GetCurrentRenderOutputSize(s_pRenderer, &iWindowWidth, &iWindowHeight);

  // Get original texture size
  int iTexWidth = winw;
  int iTexHeight = winh;
  SDL_FRect src;
  src.h = (float)winh;
  src.w = (float)winw;
  src.x = 0;
  src.y = 0;

  // Calculate aspect ratio-preserving destination rectangle
  SDL_FRect dst;
  float fWindowAspect = (float)iWindowWidth / (float)iWindowHeight;
  float fTextureAspect = (float)iTexWidth / (float)iTexHeight;

  if (fWindowAspect > fTextureAspect) {
      // Window is wider than texture
    dst.h = (float)iWindowHeight;
    dst.w = fTextureAspect * iWindowHeight;
    dst.x = (iWindowWidth - dst.w) / 2;
    dst.y = 0;
  } else {
      // Window is taller than texture
    dst.w = (float)iWindowWidth;
    dst.h = iWindowWidth / fTextureAspect;
    dst.x = 0;
    dst.y = (iWindowHeight - dst.h) / 2;
  }

  SDL_RenderClear(s_pRenderer);
  SDL_RenderTexture(s_pRenderer, s_pWindowTexture, &src, &dst);

  //SDL_FRect debugRect;
  //debugRect.h = 64;
  //debugRect.w = 64;
  //debugRect.x = 0;
  //debugRect.y = 0;
  //SDL_RenderTexture(s_pRenderer, s_pDebugTexture, NULL, &debugRect);

  SDL_RenderPresent(s_pRenderer);
}

//-------------------------------------------------------------------------------------------------

void ToggleFullscreen()
{
  static bool s_bIsFullscreen = false;
  s_bIsFullscreen = !s_bIsFullscreen;
  SDL_SetWindowFullscreen(s_pWindow, s_bIsFullscreen ? SDL_WINDOW_FULLSCREEN : 0);
}

//-------------------------------------------------------------------------------------------------

int InitSDL(const char *whiplash_root, const char *midi_root)
{
  if (!SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_JOYSTICK)) {
    ErrorBoxExit("Couldn't initialize SDL: %s", SDL_GetError());
    return 1;
  }

  if (whiplash_root) {
    if (chdir(whiplash_root) != 0) {
      ErrorBoxExit("Could not changed working directory to '%s'", whiplash_root);
      return 1;
    }
  } else {
    // Change to the base path of the application
    whiplash_root = SDL_GetBasePath();
    if (whiplash_root) {
      chdir(whiplash_root);
    }
  }

  ROLLERGetAudioInfo();

  // check if the ./FATDATA/FATAL.INI to ensure the game can run
  if (!ROLLERfexists("./FATDATA/FATAL.INI")) {
    ErrorBoxExit("The folder FATDATA does not exist.\nROLLER requires the FATDATA folder assets from a retail copy of the game.");
  }

  g_pTimerMutex = SDL_CreateMutex();

  if (!SDL_CreateWindowAndRenderer("ROLLER", 640, 400, SDL_WINDOW_RESIZABLE, &s_pWindow, &s_pRenderer)) {
    ErrorBoxExit("Couldn't create window/renderer: %s", SDL_GetError());
    return 1;
  }
  s_pWindowTexture = SDL_CreateTexture(s_pRenderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, 640, 400);
  SDL_SetTextureScaleMode(s_pWindowTexture, SDL_SCALEMODE_NEAREST);
  s_pDebugTexture = SDL_CreateTexture(s_pRenderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, 64, 64);
  SDL_SetTextureScaleMode(s_pDebugTexture, SDL_SCALEMODE_NEAREST);
  s_pRGBBuffer = malloc(640 * 400 * 3);
  s_pDebugBuffer = malloc(64 * 64 * 3);
  SDL_Surface *pIcon = IMG_Load("roller.ico");
  SDL_SetWindowIcon(s_pWindow, pIcon);

  // Move the window to the display where the mouse is currently located
  float mouseX, mouseY;
  SDL_GetGlobalMouseState(&mouseX, &mouseY);
  int displayIndex = SDL_GetDisplayForPoint(&(SDL_Point) { (int)mouseX, (int)mouseY });
  int sdl_window_centered = SDL_WINDOWPOS_CENTERED_DISPLAY(displayIndex);
  SDL_SetWindowPosition(s_pWindow, sdl_window_centered, sdl_window_centered);

  // Initialize game controllers
  SDL_InitSubSystem(SDL_INIT_GAMEPAD);

  // Open game controllers
  int iCount;
  SDL_JoystickID *joystickAy = SDL_GetGamepads(&iCount);
  if (!g_pController1 && iCount > 0) {
    g_pController1 = SDL_OpenGamepad(joystickAy[0]);
    g_joyId1 = joystickAy[0];
  }
  if (!g_pController2 && iCount > 1) {
    g_pController2 = SDL_OpenGamepad(joystickAy[1]);
    g_joyId2 = joystickAy[1];
  }
  memset(&g_rollerJoyPos, 0, sizeof(tJoyPos));

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

void ShutdownSDL()
{
  MIDI_Shutdown();

  if (g_pController1) SDL_CloseGamepad(g_pController1);
  if (g_pController2) SDL_CloseGamepad(g_pController2);
  SDL_QuitSubSystem(SDL_INIT_GAMEPAD);

  SDL_DestroyRenderer(s_pRenderer);
  SDL_DestroyWindow(s_pWindow);
  SDL_DestroyTexture(s_pWindowTexture);
  SDL_DestroyTexture(s_pDebugTexture);

  if (s_pRGBBuffer) free(s_pRGBBuffer);
  if (s_pDebugBuffer) free(s_pDebugBuffer);

  SDL_Quit();
}

uint8 songId = 4;
void playMusic()
{
  MIDIDigi_ClearBuffer();
  MIDISetMasterVolume(127);
  uint8 *songBuffer;
  uint32 songLen;
  SDL_Log("Song[%i]: %s", songId, Song[songId]);
  loadfile((const char *)&Song[songId], (void *)&songBuffer, &songLen, 0);
  MIDIDigi_PlayBuffer(songBuffer, songLen);
  fre((void **)&songBuffer);
  songId = (songId + 1) % 9;
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
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    UpdateSDLAudioEvents(e);
    if (e.type == SDL_EVENT_QUIT) {
      quit_game = 1;
      doexit();
    }
    if (e.type == SDL_EVENT_KEY_DOWN) {

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
      if (sc < SDL_arraysize(sdl_to_set1) && sdl_to_set1[sc]) {
        uint8 byRawCode = sdl_to_set1[sc];
        if (e.type == SDL_EVENT_KEY_UP) {
          byRawCode |= 0x80;  // Set high bit for release
        }
        key_handler(byRawCode);
      }
    }

    if (e.type == SDL_EVENT_JOYSTICK_AXIS_MOTION) {
      if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) {
        if (e.gaxis.which == g_joyId1)
          g_rollerJoyPos.iJ1XAxis = ((e.gaxis.value + 32768) * 10000) / 65536;
        else if (e.gaxis.which == g_joyId2)
          g_rollerJoyPos.iJ2XAxis = ((e.gaxis.value + 32768) * 10000) / 65536;
      } else if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) {
        if (e.gaxis.which == g_joyId1)
          g_rollerJoyPos.iJ1YAxis = ((e.gaxis.value + 32768) * 10000) / 65536;
        else if (e.gaxis.which == g_joyId2)
          g_rollerJoyPos.iJ2YAxis = ((e.gaxis.value + 32768) * 10000) / 65536;
      }
    } else if (e.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN) {
      if (e.gbutton.button == 0) {
        if (e.gbutton.which == g_joyId1)
          g_rollerJoyPos.iJ1Button1 = 1;
        else if (e.gbutton.which == g_joyId2)
          g_rollerJoyPos.iJ2Button1 = 1;
      } else if (e.gbutton.button == 1) {
        if (e.gbutton.which == g_joyId1)
          g_rollerJoyPos.iJ1Button2 = 1;
        else if (e.gbutton.which == g_joyId2)
          g_rollerJoyPos.iJ2Button2 = 1;
      }
    } else if (e.type == SDL_EVENT_JOYSTICK_BUTTON_UP) {
      if (e.gbutton.button == 0) {
        if (e.gbutton.which == g_joyId1)
          g_rollerJoyPos.iJ1Button1 = 0;
        else if (e.gbutton.which == g_joyId2)
          g_rollerJoyPos.iJ2Button1 = 0;
      } else if (e.gbutton.button == 1) {
        if (e.gbutton.which == g_joyId1)
          g_rollerJoyPos.iJ1Button2 = 0;
        else if (e.gbutton.which == g_joyId2)
          g_rollerJoyPos.iJ2Button2 = 0;
      }
    }
  }
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

//--------------------------------------------------------------------------------------------------
#pragma region MIDI

#define MIDI_RATE 44100 // not sure if this is the correct rate
SDL_AudioStream *midi_stream;
float midi_volume;
midi *midi_music;

void MIDI_AudioStreamCallback(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount)
{
  //int available = SDL_GetAudioStreamAvailable(stream);
  //SDL_Log("MIDI_AudioStreamCallback[%i]: %i - %i", available, additional_amount, total_amount);

  //if (available != 0) return;

  if (midi_music) {
    void *output_buffer;
    int32_t res = 0;
    uint32_t samples = total_amount;

    output_buffer = malloc(samples);
    if (output_buffer != NULL)
      memset(output_buffer, 0, samples);

    if ((res = WildMidi_GetOutput(midi_music, output_buffer, samples)) > 0) {
      SDL_PutAudioStreamData(stream, output_buffer, res);
    }

    free(output_buffer);
  }
}

bool MIDI_Init(const char *config_file)
{
  long version = WildMidi_GetVersion();
  SDL_Log("MIDI_Init: Initializing libWildMidi %ld.%ld.%ld",
                      (version >> 16) & 255,
                      (version >> 8) & 255,
                      (version) & 255);

  uint16_t rate = MIDI_RATE;
  uint16_t mixer_options = 0;

  if (WildMidi_Init(config_file, rate, mixer_options) == -1) {
    SDL_Log("MIDI_Init: WildMidi_GetError: %s", WildMidi_GetError());
    WildMidi_ClearError();
    return false;
  }

  SDL_AudioSpec wav_spec;
  wav_spec.channels = 2;
  wav_spec.freq = MIDI_RATE;
  wav_spec.format = SDL_AUDIO_S16;

  midi_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &wav_spec, MIDI_AudioStreamCallback, NULL);
  midi_volume = 1.0f; // Default volume

  return true;
}

void MIDIDigi_PlayBuffer(uint8 *midi_buffer, uint32 midi_length)
{
  midi *midi_ptr = WildMidi_OpenBuffer(midi_buffer, midi_length);
  if (!midi_ptr) {
    SDL_Log("WildMidi_OpenBuffer failed: %s", WildMidi_GetError());
    return;
  }

  // Enable Loop
  WildMidi_SetOption(midi_ptr, WM_MO_LOOP, WM_MO_LOOP);
  // Disable Loop
  WildMidi_SetOption(midi_ptr, WM_MO_LOOP, 0);

  struct _WM_Info *wm_info = WildMidi_GetInfo(midi_ptr);
  if (wm_info) {

    int apr_mins = wm_info->approx_total_samples / (MIDI_RATE * 60);
    int apr_secs = (wm_info->approx_total_samples % (MIDI_RATE * 60)) / MIDI_RATE;

    SDL_Log("MIDIDigi_PlayBuffer: [Approx %2um %2us Total]", apr_mins, apr_secs);

    SDL_Log("MIDIDigi_PlayBuffer: Total Samples %i", wm_info->approx_total_samples);
    SDL_Log("MIDIDigi_PlayBuffer: Current Samples %i", wm_info->current_sample);
    SDL_Log("MIDIDigi_PlayBuffer: Total Midi time %i", wm_info->total_midi_time);
    SDL_Log("MIDIDigi_PlayBuffer: Mix Options %i", wm_info->mixer_options);
  }

  SDL_AudioStream *stream = midi_stream;

  if (stream != NULL) {
    float master_volume = (float)MIDIGetMasterVolume() / 127.0f; // Normalize to [0.0, 1.0] range
    SDL_SetAudioStreamGain(stream, midi_volume * master_volume); // Set the gain for the audio stream
    SDL_Log("MIDIDigi_PlayBuffer: Volume: %f", midi_volume * master_volume);

    void *output_buffer;
    uint32_t len = 0;
    int32_t res = 0;
    uint32_t samples = 16384;

    output_buffer = malloc(samples);
    if (output_buffer != NULL)
      memset(output_buffer, 0, samples);

    uint32_t total_pcm_bytes = 0;

    while ((res = WildMidi_GetOutput(midi_ptr, output_buffer, samples)) > 0) {
      SDL_PutAudioStreamData(stream, output_buffer, res);
      total_pcm_bytes += res;
      if (total_pcm_bytes > 64e6) {
        SDL_Log("MIDIDigi_PlayBuffer: Stopping put audio stream due to large buffer size.");
        break;
      }
    }

    free(output_buffer);

    SDL_ResumeAudioStreamDevice(stream);

    SDL_Log("MIDIDigi_PlayBuffer: Total: %i", total_pcm_bytes);
  }

  WildMidi_Close(midi_ptr);
}

void MIDIDigi_ClearBuffer()
{
  if (midi_stream) {
    SDL_PauseAudioStreamDevice(midi_stream);
    SDL_ClearAudioStream(midi_stream);
  }
  MIDI_CloseMidiBuffer();
}

void MIDI_Shutdown()
{
  if (midi_stream) {
    SDL_PauseAudioStreamDevice(midi_stream);
    SDL_DestroyAudioStream(midi_stream);
    midi_stream = NULL;
  }
  MIDI_CloseMidiBuffer();
  WildMidi_Shutdown();
}

/// <summary>
/// Close midi buffer
/// </summary>
void MIDI_CloseMidiBuffer()
{
  if (midi_music) {
    WildMidi_Close(midi_music);
    midi_music = NULL;
  }
}

/// <summary>
/// Initializes the MIDI audio stream if it hasn't been initialized yet.
/// </summary>
void MIDIInitStream()
{
  if (!midi_stream) {
    SDL_Log("MIDIInitStream: initialize 'midi_stream'.");
    SDL_AudioSpec wav_spec;
    wav_spec.channels = 2;
    wav_spec.freq = MIDI_RATE;
    wav_spec.format = SDL_AUDIO_S16;
    midi_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &wav_spec, MIDI_AudioStreamCallback, NULL);
  }

  // Set Volume Stream
  float master_volume = (float)MIDIGetMasterVolume() / 127.0f; // Normalize to [0.0, 1.0] range
  SDL_SetAudioStreamGain(midi_stream, midi_volume * master_volume); // Set the gain for the audio stream
  SDL_Log("MIDIInitSong: Volume: %f", midi_volume * master_volume);
}

void MIDIClearStream()
{
  if (midi_stream) {
    SDL_PauseAudioStreamDevice(midi_stream);
    SDL_ClearAudioStream(midi_stream);
    midi_stream = NULL;
  }
}

/// <summary>
/// Initializes a MIDI song for playback using the provided song data.
/// This function closes any currently loaded MIDI song, opens the new song buffer,
/// enables looping, logs song information, and sets the audio stream volume.
/// </summary>
/// <param name="data">Pointer to a tInitSong structure containing the MIDI song data and its length.</param>
void MIDIInitSong(tInitSong *data)
{
  MIDIStopSong();

  SDL_Log("MIDIInitSong: Midi - Length: %i", data->iLength);

  MIDI_CloseMidiBuffer();

  midi_music = WildMidi_OpenBuffer(((uint8 *)data->pData), data->iLength);
  if (!midi_music) {
    SDL_Log("MIDIInitSong: WildMidi_OpenBuffer failed: %s", WildMidi_GetError());
    return;
  }
  // Enable WildMidi_GetOutput Loop
  WildMidi_SetOption(midi_music, WM_MO_LOOP, WM_MO_LOOP);

  // Get Info
  struct _WM_Info *wm_info = WildMidi_GetInfo(midi_music);
  if (wm_info) {

    int apr_mins = wm_info->approx_total_samples / (MIDI_RATE * 60);
    int apr_secs = (wm_info->approx_total_samples % (MIDI_RATE * 60)) / MIDI_RATE;

    SDL_Log("MIDIInitSong: Approx %2um %2us Total", apr_mins, apr_secs);

    SDL_Log("MIDIInitSong: Total Samples %i", wm_info->approx_total_samples);
    SDL_Log("MIDIInitSong: Current Samples %i", wm_info->current_sample);
    SDL_Log("MIDIInitSong: Total Midi time %i", wm_info->total_midi_time);
    SDL_Log("MIDIInitSong: Mix Options %i", wm_info->mixer_options);
  }

  MIDIInitStream();
}

void MIDIStartSong()
{
  if (!midi_stream) {
    SDL_Log("MIDIStartSong: 'midi_stream' is not initialized.");
    return;
  }

  SDL_Log("MIDIStartSong: Play Audio Stream.");
  SDL_ResumeAudioStreamDevice(midi_stream);
}

void MIDIStopSong()
{
  if (!midi_stream) {
    SDL_Log("MIDIStopSong: 'midi_stream' is not initialized.");
    return;
  }

  SDL_Log("MIDIStopSong: Pause Audio Stream.");
  SDL_PauseAudioStreamDevice(midi_stream);
}

int8 MIDIMasterVolume = 127; // Default master volume (0-127)
/// <summary>
/// Set the master volume for MIDI playback. (0-127)
/// </summary>
void MIDISetMasterVolume(int8 volume)
{
  if (volume > 127) volume = 127;
  if (volume < 0) volume = 0;
  MIDIMasterVolume = volume;

  SDL_Log("MIDISetMasterVolume: %i", volume);

  float master_volume = (float)volume / 127.0f; // Normalize to [0.0, 1.0] range

  // Change the gain for the MIDI stream
  SDL_SetAudioStreamGain(midi_stream, midi_volume * master_volume);
}

/// <summary>
/// Get the current master volume level for MIDI playback. (0-127)
/// </summary>
int MIDIGetMasterVolume()
{
  return MIDIMasterVolume;
}

#pragma endregion
//-------------------------------------------------------------------------------------------------
#pragma region DIGI
#define NUM_DIGI_STREAMS 32
SDL_AudioStream *digi_stream[NUM_DIGI_STREAMS];
float digi_volume[NUM_DIGI_STREAMS];
tSampleData digi_sample_data[NUM_DIGI_STREAMS];

void mono_to_stereo_u8(const Uint8 *in, int in_length, Uint8 *out)
{
  int frames = in_length; // 1 byte per mono sample
  for (int i = 0; i < frames; i++) {
    Uint8 sample = in[i];
    out[2 * i] = sample; // Left
    out[2 * i + 1] = sample; // Right
  }
}

void apply_pan_u8(Uint8 *raw, int length, float pan)
{
  int frames = length / 2; // 2 channels per frame

  float left_gain = (pan <= 0) ? 1.0f : 1.0f - pan;
  float right_gain = (pan >= 0) ? 1.0f : 1.0f + pan;

  for (int i = 0; i < frames; i++) {
      // Convert from unsigned (0–255) to signed (-128…127)
    int l = (int)raw[2 * i] - 128;
    int r = (int)raw[2 * i + 1] - 128;

    // Apply pan
    l = (int)(l * left_gain);
    r = (int)(r * right_gain);

    // Clamp back to signed range
    if (l > 127) l = 127; if (l < -128) l = -128;
    if (r > 127) r = 127; if (r < -128) r = -128;

    // Convert back to unsigned (0–255)
    raw[2 * i] = (Uint8)(l + 128);
    raw[2 * i + 1] = (Uint8)(r + 128);
  }
}

void DIGI_AudioStreamCallback(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount)
{
  tSampleData *data = (tSampleData *)userdata;
  if (data && data->pSample && additional_amount > 0)
    SDL_PutAudioStreamData(stream, data->pSample, data->iLength);
}

int DIGISampleStart(tSampleData *data)
{
  int index = -1;
  for (int i = 0; i < NUM_DIGI_STREAMS; ++i) {
    if (!digi_stream[i] || DIGISampleDone(i)) {
      index = i;
      break;
    }
  }
  if (index < 0) {
    //SDL_Log("DIGISampleStart: No available audio stream slots for digital sample.");
    return index; // No available stream slots
  }

  float volume = (float)data->iVolume / 0x7FFF; // Convert volume to [0.0, 1.0] range
  int iFlags = data->iFlags;
  int iPan = data->iPan;

  if (digi_stream[index]) {
    //audio stream is available but needs to be destroyed
    SDL_PauseAudioStreamDevice(digi_stream[index]);
    SDL_DestroyAudioStream(digi_stream[index]);
    digi_stream[index] = NULL;
    memset(&digi_sample_data[index], 0, sizeof(tSampleData));
  }

  if (!digi_stream[index]) {
    SDL_AudioSpec spec;
    spec.channels = 1; // Mono
    spec.freq = 11025; // Sample rate
    spec.format = SDL_AUDIO_U8; // 8-bit unsigned audio
    //spec.channels = 2; // Stereo
    //spec.freq = 11025; // Sample rate
    //spec.format = SDL_AUDIO_U8; // 8-bit unsigned audio
    //SDL_Log("DIGISampleStart[%i]: channels: %i, freq: %i, format: %i", index, spec.channels, spec.freq, spec.format);
    if (iFlags != 18176) //one of these means loop the audio
      digi_stream[index] = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    else {
      //need to copy what's in SampleData first
      digi_sample_data[index].pSample = data->pSample;
      digi_sample_data[index].iLength = data->iLength;
      digi_sample_data[index].iSampleIndex = data->iSampleIndex;

      digi_stream[index] = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, DIGI_AudioStreamCallback, &digi_sample_data[index]);
    }
  } else {
    assert(0); //should never happen
  }

  if (!digi_stream[index]) {
    //SDL_Log("DIGISampleStart: Couldn't create audio stream: %s", SDL_GetError());
    return -1;
  }

  // Set pitch in the stream
  SDL_SetAudioStreamFrequencyRatio(digi_stream[index], 1.0); // pitch
  DIGISetPitch(index, data->iPitch);

  // Remember the volume for this stream
  digi_volume[index] = volume;

  // Set the gain for the audio stream
  float master_volume = (float)DIGIGetMasterVolume() / 0x7FFF; // Normalize to [0.0, 1.0] range
  SDL_SetAudioStreamGain(digi_stream[index], volume * master_volume);

  // Put audio data into the stream
  SDL_PutAudioStreamData(digi_stream[index], ((Uint8 *)data->pSample), data->iLength);
  SDL_ResumeAudioStreamDevice(digi_stream[index]);

  return index;
}

/// <summary>
/// Check if a digital sample is done playing.
/// </summary>
/// <param name="index"></param>
bool DIGISampleDone(int index)
{
  return DIGISampleAvailable(index) == 0;
}

int DIGISampleAvailable(int index)
{
  if (index < 0 || index >= NUM_DIGI_STREAMS) {
    return 0;
  }
  if (digi_stream[index]) {
    return SDL_GetAudioStreamAvailable(digi_stream[index]);
  }
  return 0;
}

int DIGIMasterVolume = 0x7FFF; // Default master volume (0-0x7FFF)
/// <summary>
/// Set the master volume for all digital audio streams.
/// </summary>
/// <param name="volume">Volume level (0-0x7FFF).</param>
void DIGISetMasterVolume(int volume)
{
  if (volume > 0x7FFF) volume = 0x7FFF;
  if (volume < 0) volume = 0;
  DIGIMasterVolume = volume;

  SDL_Log("DIGISetMasterVolume: %x", volume);

  float normalized_volume = (float)volume / 0x7FFF; // Normalize to [0.0, 1.0] range

  for (size_t i = 0; i < NUM_DIGI_STREAMS; i++) {
    if (digi_stream[i]) {
      SDL_SetAudioStreamGain(digi_stream[i], digi_volume[i] * normalized_volume);
    }
  }
}

/// <summary>
/// Get the current master volume level.
/// </summary>
/// <returns>The master volume level (0-0x7FFF).</returns>
int DIGIGetMasterVolume()
{
  return DIGIMasterVolume;
}

void DIGIStopSample(int index)
{
  if (index < 0 || index >= NUM_DIGI_STREAMS) {
    SDL_Log("DIGIStopSample: Invalid stream index: %d", index);
    return;
  }
  if (digi_stream[index]) {
    SDL_PauseAudioStreamDevice(digi_stream[index]);
    SDL_DestroyAudioStream(digi_stream[index]);
    digi_stream[index] = NULL;
    memset(&digi_sample_data[index], 0, sizeof(tSampleData));
  }
}

void DIGIClearAllStream()
{
  for (int i = 0; i < NUM_DIGI_STREAMS; i++) {
    if (digi_stream[i]) {
      SDL_PauseAudioStreamDevice(digi_stream[i]);
      SDL_ClearAudioStream(digi_stream[i]);
      digi_stream[i] = NULL;
      memset(&digi_sample_data[i], 0, sizeof(tSampleData));
    }
  }
}

void PlayAudioSampleWait(int iIndex)
{
  if (iIndex >= 120) return;
  SDL_Log("Play Sample[%i]: %s", iIndex, Sample[iIndex]);
  loadasample(iIndex);
  PlayAudioDataWait(SamplePtr[iIndex], SampleLen[iIndex]);
}

void DIGISetSampleVolume(int iHandle, int iVolume)
{
  if (!digi_stream[iHandle])
    return; //DIGI stream not found

  float fStreamVolume = (float)iVolume / 0x7FFF; // Convert volume to [0.0, 1.0] range

  // udpate saved volume
  digi_volume[iHandle] = fStreamVolume;

  // Set the gain for the audio stream
  float fMasterVolume = (float)DIGIGetMasterVolume() / 0x7FFF; // Normalize to [0.0, 1.0] range
  SDL_SetAudioStreamGain(digi_stream[iHandle], fStreamVolume * fMasterVolume);
}

void DIGISetPitch(int iHandle, int iPitch)
{
  if (!digi_stream[iHandle])
    return; //DIGI stream not found

  float fStreamPitch = (float)iPitch / 0x10000;
  SDL_SetAudioStreamFrequencyRatio(digi_stream[iHandle], fStreamPitch);
}

void DIGISetPanLocation(int iHandle, int iPan)
{
  float fStreamPan = ((float)((int32_t)iPan) / (int32_t)0x8000) - 1.0f;
  //SDL_Log("DIGISetPanLocation[%i]: %f | %i", iHandle, fStreamPan, iPan);
}

void PlayAudioDataWait(Uint8 *buffer, Uint32 length)
{
  // https://wiki.libsdl.org/SDL3/QuickReference
  SDL_AudioSpec wav_spec;
  wav_spec.channels = 1; // Stereo
  wav_spec.freq = 11025; // Sample rate
  wav_spec.format = SDL_AUDIO_U8; // 8-bit unsigned audio

  SDL_AudioStream *stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &wav_spec, NULL, NULL);
  if (!stream) {
    SDL_Log("Couldn't create audio stream: %s", SDL_GetError());
    return;
  }

  float volume = 0.5f;
  SDL_SetAudioStreamGain(stream, volume); // Set the gain for the audio stream
  SDL_PutAudioStreamData(stream, buffer, length);
  SDL_ResumeAudioStreamDevice(stream);

  // wait from the audio stream to finish playing
  while (SDL_GetAudioStreamAvailable(stream) > 0) {
    SDL_Delay(10);
  }

  SDL_ClearAudioStream(stream);
}
#pragma endregion
//-------------------------------------------------------------------------------------------------
/// <summary>
/// Handle SDL audio device events for MIDI and digital audio streams.
/// </summary>
/// <param name="e"></param>
void UpdateSDLAudioEvents(SDL_Event e)
{
  if (e.type == SDL_EVENT_AUDIO_DEVICE_REMOVED) {
    SDL_AudioDeviceEvent *ade = (SDL_AudioDeviceEvent *)&e;
    SDL_Log("UpdateSDLAudioEvents: Audio device removed: %d", ade->which);
    DIGIClearAllStream();
    MIDIClearStream();
  }
  if (e.type == SDL_EVENT_AUDIO_DEVICE_ADDED) {
    SDL_AudioDeviceEvent *ade = (SDL_AudioDeviceEvent *)&e;
    SDL_Log("UpdateSDLAudioEvents: Audio device Added: %d", ade->which);
    DIGIClearAllStream();
    MIDIClearStream();
    MIDIInitStream();
    MIDIStartSong(); // Force music to continue playing
  }
}
//-------------------------------------------------------------------------------------------------

bool ROLLERfexists(const char *szFile)
{
  FILE *pFile = fopen(szFile, "r");
  if (pFile) {
    fclose(pFile);
    return true;
  }

  char szUpper[260] = { 0 };
  char szLower[260] = { 0 };
  int iLength = (int)strlen(szFile);

  for (int i = 0; i < iLength && i < sizeof(szUpper); ++i) {
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

  return false;
}

//-------------------------------------------------------------------------------------------------

FILE *ROLLERfopen(const char *szFile, const char *szMode)
{
  FILE *pFile = fopen(szFile, szMode);
  if (pFile) return pFile;

  char szUpper[260] = { 0 };
  char szLower[260] = { 0 };
  int iLength = (int)strlen(szFile);

  for (int i = 0; i < iLength && i < sizeof(szUpper); ++i) {
    szUpper[i] = toupper(szFile[i]);
    szLower[i] = tolower(szFile[i]);
  }

  pFile = fopen(szUpper, szMode);
  if (pFile) return pFile;

  pFile = fopen(szLower, szMode);
  return pFile;
}

//-------------------------------------------------------------------------------------------------

int ROLLERopen(const char *szFile, int iOpenFlags)
{
  int iHandle = open(szFile, iOpenFlags);
  if (iHandle != -1) return iHandle;

  char szUpper[260] = { 0 };
  char szLower[260] = { 0 };
  int iLength = (int)strlen(szFile);

  for (int i = 0; i < iLength && i < sizeof(szUpper); ++i) {
    szUpper[i] = toupper(szFile[i]);
    szLower[i] = tolower(szFile[i]);
  }

  iHandle = open(szUpper, iOpenFlags);
  if (iHandle != -1) return iHandle;

  iHandle = open(szLower, iOpenFlags);
  return iHandle;
}

//-------------------------------------------------------------------------------------------------

int ROLLERremove(const char *szFile)
{
  int iSuccess = remove(szFile);
  if (iSuccess == 0)
    return 0;

  char szUpper[260] = { 0 };
  char szLower[260] = { 0 };
  int iLength = (int)strlen(szFile);

  for (int i = 0; i < iLength && i < sizeof(szUpper); ++i) {
    szUpper[i] = toupper(szFile[i]);
    szLower[i] = tolower(szFile[i]);
  }

  iSuccess = remove(szUpper);
  if (iSuccess == 0) return 0;

  iSuccess = remove(szLower);
  return iSuccess;
}

//-------------------------------------------------------------------------------------------------

int ROLLERrename(const char *szOldName, const char *szNewName)
{
  int iSuccess = rename(szOldName, szNewName);
  if (iSuccess == 0)
    return 0;

  char szUpper[260] = { 0 };
  char szLower[260] = { 0 };
  int iLength = (int)strlen(szOldName);

  for (int i = 0; i < iLength && i < sizeof(szUpper); ++i) {
    szUpper[i] = toupper(szOldName[i]);
    szLower[i] = tolower(szOldName[i]);
  }

  iSuccess = rename(szUpper, szNewName);
  if (iSuccess == 0) return 0;

  iSuccess = rename(szLower, szNewName);
  return iSuccess;
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
  SDL_LockMutex(g_pTimerMutex);
  SDL_RemoveTimer(uiHandle);
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

int ROLLERrand()
{
  return GetHighOrderRand(0x7FFF, rand());
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

Uint64 SDLTickTimerCallback(void *userdata, SDL_TimerID timerID, Uint64 interval)
{
  tickhandler();
  uint64 ullRet = 0;

  SDL_LockMutex(g_pTimerMutex);
  tTimerData *pTimerData = GetTimerData(timerID);
  if (!pTimerData) {
    assert(0);
    ErrorBoxExit("Tick timer handle not found!");
  }

  pTimerData->ullCurrSDLTicksNS = SDL_GetTicksNS();
  int64 llNSSinceLast = (int64)pTimerData->ullCurrSDLTicksNS - (int64)pTimerData->ullLastSDLTicksNS;
  int64 llDelta = llNSSinceLast - (int64)pTimerData->ullTargetSDLTicksNS;
  if (llDelta < 0)
    llDelta = 0;
  pTimerData->ullLastSDLTicksNS = pTimerData->ullCurrSDLTicksNS;
  ullRet = pTimerData->ullTargetSDLTicksNS - llDelta;
  SDL_UnlockMutex(g_pTimerMutex);

  return ullRet;
}

//-------------------------------------------------------------------------------------------------

Uint64 SDLS7TimerCallback(void *userdata, SDL_TimerID timerID, Uint64 interval)
{
  SOSTimerCallbackS7();
  uint64 ullRet = 0;

  SDL_LockMutex(g_pTimerMutex);
  tTimerData *pTimerData = GetTimerData(timerID);
  if (!pTimerData) {
    assert(0);
    ErrorBoxExit("S7 timer handle not found!");
  }

  pTimerData->ullCurrSDLTicksNS = SDL_GetTicksNS();
  int64 llNSSinceLast = (int64)pTimerData->ullCurrSDLTicksNS - (int64)pTimerData->ullLastSDLTicksNS;
  int64 llDelta = llNSSinceLast - (int64)pTimerData->ullTargetSDLTicksNS;
  if (llDelta < 0)
    llDelta = 0;
  pTimerData->ullLastSDLTicksNS = pTimerData->ullCurrSDLTicksNS;
  ullRet = pTimerData->ullTargetSDLTicksNS - llDelta;
  SDL_UnlockMutex(g_pTimerMutex);

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
  return (int)(((double)iRange * iRandValue) / (RAND_MAX + 1.0));
}

//-------------------------------------------------------------------------------------------------

int ReadUnalignedInt(const void *pData)
{
  const uint8 *pBytes = (const uint8*)pData;
  return (uint32)pBytes[0] | ((uint32)pBytes[1] << 8) | ((uint32)pBytes[2] << 16) | ((uint32)pBytes[3] << 24);
}

//-------------------------------------------------------------------------------------------------

void LBAToMSF(uint32 uiLBA, uint8 *pbyMinute, uint8 *pbySecond, uint8 *pbyFrame)
{
  uint32 uiAdjustedLBA = uiLBA + 150;  // Add CD lead-in offset
  *pbyFrame = uiAdjustedLBA % 75;
  *pbySecond = (uiAdjustedLBA / 75) % 60;
  *pbyMinute = (uiAdjustedLBA / 75) / 60;
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