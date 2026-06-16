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
#include "rollercomms.h"
#include "menu_render.h"
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

#define FRONTEND_MOUSE_MAX_HITBOXES 128

typedef struct
{
  int iId;
  int iX;
  int iY;
  int iWidth;
  int iHeight;
} tFrontendMouseHitbox;

static tFrontendMouseHitbox s_frontendMouseHitboxes[FRONTEND_MOUSE_MAX_HITBOXES];
static int s_iFrontendMouseHitboxCount = 0;
static int s_iFrontendMouseVirtualWidth = 640;
static int s_iFrontendMouseVirtualHeight = 400;
static int s_iFrontendMouseVirtualX = 0;
static int s_iFrontendMouseVirtualY = 0;
static int s_iFrontendMouseValid = 0;
static int s_iFrontendMouseClickVirtualX = 0;
static int s_iFrontendMouseClickVirtualY = 0;
static int s_iFrontendMouseClickValid = 0;
static int s_iFrontendMouseHoveredId = -1;
static int s_iFrontendMouseClickedId = -1;
static float s_fFrontendMouseWindowX = 0.0f;
static float s_fFrontendMouseWindowY = 0.0f;
static float s_fFrontendMouseClickWindowX = 0.0f;
static float s_fFrontendMouseClickWindowY = 0.0f;
static int s_iFrontendMouseWindowPosValid = 0;
static uint32 s_uiFrontendMouseMotionSeq = 0;
static uint32 s_uiFrontendMouseConsumedMotionSeq = 0;
static uint32 s_uiFrontendMouseClickSeq = 0;
static uint32 s_uiFrontendMouseConsumedClickSeq = 0;
static float s_fFrontendMouseWheelY = 0.0f;

//-------------------------------------------------------------------------------------------------

static void frontend_mouse_map_window_point(float fWindowX, float fWindowY,
                                            int *piVirtualX, int *piVirtualY,
                                            int *piValid)
{
  SDL_Window *pWindow = ROLLERGetWindow();
  int iWindowWidth = 0;
  int iWindowHeight = 0;
  float fWindowAspect;
  float fVirtualAspect;
  float fBaseX = 0.0f;
  float fBaseY = 0.0f;
  float fBaseWidth;
  float fBaseHeight;
  float fVirtualX;
  float fVirtualY;

  *piValid = 0;
  *piVirtualX = 0;
  *piVirtualY = 0;

  if (!pWindow || s_iFrontendMouseVirtualWidth <= 0 ||
      s_iFrontendMouseVirtualHeight <= 0)
    return;

  SDL_GetWindowSize(pWindow, &iWindowWidth, &iWindowHeight);
  if (iWindowWidth <= 0 || iWindowHeight <= 0)
    return;

  fWindowAspect = (float)iWindowWidth / (float)iWindowHeight;
  fVirtualAspect = (float)s_iFrontendMouseVirtualWidth /
                   (float)s_iFrontendMouseVirtualHeight;

  if (fWindowAspect > fVirtualAspect) {
    fBaseHeight = (float)iWindowHeight;
    fBaseWidth = fVirtualAspect * fBaseHeight;
    fBaseX = ((float)iWindowWidth - fBaseWidth) * 0.5f;
  } else {
    fBaseWidth = (float)iWindowWidth;
    fBaseHeight = fBaseWidth / fVirtualAspect;
    fBaseY = ((float)iWindowHeight - fBaseHeight) * 0.5f;
  }

  if (fBaseWidth <= 0.0f || fBaseHeight <= 0.0f)
    return;

  fVirtualX = (fWindowX - fBaseX) *
              (float)s_iFrontendMouseVirtualWidth / fBaseWidth;
  fVirtualY = (fWindowY - fBaseY) *
              (float)s_iFrontendMouseVirtualHeight / fBaseHeight;

  if (fVirtualX < 0.0f || fVirtualY < 0.0f ||
      fVirtualX >= (float)s_iFrontendMouseVirtualWidth ||
      fVirtualY >= (float)s_iFrontendMouseVirtualHeight)
    return;

  *piVirtualX = (int)fVirtualX;
  *piVirtualY = (int)fVirtualY;
  *piValid = -1;
}

//-------------------------------------------------------------------------------------------------

void frontend_mouse_handle_event(const SDL_Event *pEvent)
{
  if (!pEvent)
    return;

  switch (pEvent->type) {
    case SDL_EVENT_MOUSE_MOTION:
      s_fFrontendMouseWindowX = pEvent->motion.x;
      s_fFrontendMouseWindowY = pEvent->motion.y;
      s_iFrontendMouseWindowPosValid = -1;
      ++s_uiFrontendMouseMotionSeq;
      break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
      s_fFrontendMouseWindowX = pEvent->button.x;
      s_fFrontendMouseWindowY = pEvent->button.y;
      s_iFrontendMouseWindowPosValid = -1;
      if (pEvent->button.button == SDL_BUTTON_LEFT) {
        s_fFrontendMouseClickWindowX = pEvent->button.x;
        s_fFrontendMouseClickWindowY = pEvent->button.y;
        ++s_uiFrontendMouseClickSeq;
      }
      break;
    case SDL_EVENT_MOUSE_WHEEL:
      s_fFrontendMouseWheelY += pEvent->wheel.y;
      break;
    default:
      break;
  }
}

//-------------------------------------------------------------------------------------------------

void frontend_mouse_begin_frame(int iVirtualWidth, int iVirtualHeight)
{
  if (iVirtualWidth > 0)
    s_iFrontendMouseVirtualWidth = iVirtualWidth;
  if (iVirtualHeight > 0)
    s_iFrontendMouseVirtualHeight = iVirtualHeight;

  if (!s_iFrontendMouseWindowPosValid) {
    float fMouseX = 0.0f;
    float fMouseY = 0.0f;

    SDL_GetMouseState(&fMouseX, &fMouseY);
    s_fFrontendMouseWindowX = fMouseX;
    s_fFrontendMouseWindowY = fMouseY;
    s_iFrontendMouseWindowPosValid = -1;
  }

  frontend_mouse_map_window_point(s_fFrontendMouseWindowX,
                                  s_fFrontendMouseWindowY,
                                  &s_iFrontendMouseVirtualX,
                                  &s_iFrontendMouseVirtualY,
                                  &s_iFrontendMouseValid);
  frontend_mouse_map_window_point(s_fFrontendMouseClickWindowX,
                                  s_fFrontendMouseClickWindowY,
                                  &s_iFrontendMouseClickVirtualX,
                                  &s_iFrontendMouseClickVirtualY,
                                  &s_iFrontendMouseClickValid);

  s_iFrontendMouseHitboxCount = 0;
  s_iFrontendMouseHoveredId = -1;
  s_iFrontendMouseClickedId = -1;
}

//-------------------------------------------------------------------------------------------------

void frontend_mouse_register_rect(int iId, int iX, int iY, int iWidth, int iHeight)
{
  tFrontendMouseHitbox *pHitbox;

  if (iWidth < 0) {
    iX += iWidth;
    iWidth = -iWidth;
  }
  if (iHeight < 0) {
    iY += iHeight;
    iHeight = -iHeight;
  }
  if (iWidth <= 0 || iHeight <= 0)
    return;

  if (s_iFrontendMouseHitboxCount < FRONTEND_MOUSE_MAX_HITBOXES) {
    pHitbox = &s_frontendMouseHitboxes[s_iFrontendMouseHitboxCount++];
    pHitbox->iId = iId;
    pHitbox->iX = iX;
    pHitbox->iY = iY;
    pHitbox->iWidth = iWidth;
    pHitbox->iHeight = iHeight;
  }

  if (s_iFrontendMouseValid &&
      s_iFrontendMouseVirtualX >= iX &&
      s_iFrontendMouseVirtualY >= iY &&
      s_iFrontendMouseVirtualX < iX + iWidth &&
      s_iFrontendMouseVirtualY < iY + iHeight)
    s_iFrontendMouseHoveredId = iId;

  if (s_iFrontendMouseClickValid &&
      s_iFrontendMouseClickVirtualX >= iX &&
      s_iFrontendMouseClickVirtualY >= iY &&
      s_iFrontendMouseClickVirtualX < iX + iWidth &&
      s_iFrontendMouseClickVirtualY < iY + iHeight)
    s_iFrontendMouseClickedId = iId;
}

//-------------------------------------------------------------------------------------------------

static int frontend_mouse_text_width(tBlockHeader *pFont,
                                     const char *szText,
                                     const char *szMappingTable)
{
  int iWidth = 0;

  if (!pFont || !szText || !szMappingTable)
    return 0;

  while (*szText) {
    uint8 byMapped = (uint8)szMappingTable[(uint8)*szText++];

    if (byMapped == 0xFF)
      iWidth += 8;
    else
      iWidth += pFont[byMapped].iWidth + 1;
  }

  return iWidth;
}

//-------------------------------------------------------------------------------------------------

static void frontend_mouse_text_height(tBlockHeader *pFont,
                                       const char *szText,
                                       const char *szMappingTable,
                                       int *pCharVOffsets,
                                       int iScaleSize,
                                       int *piTop,
                                       int *piHeight)
{
  int iAnyGlyph = 0;
  int iTop = 0;
  int iBottom = 8;

  if (!pFont || !szText || !szMappingTable) {
    *piTop = 0;
    *piHeight = 0;
    return;
  }

  while (*szText) {
    uint8 byMapped = (uint8)szMappingTable[(uint8)*szText++];

    if (byMapped != 0xFF) {
      int iOffset = pCharVOffsets ? pCharVOffsets[byMapped] : 0;
      int iGlyphHeight = (pFont[byMapped].iHeight * iScaleSize + 63) >> 6;

      if (!iAnyGlyph || iOffset < iTop)
        iTop = iOffset;
      if (!iAnyGlyph || iOffset + iGlyphHeight > iBottom)
        iBottom = iOffset + iGlyphHeight;
      iAnyGlyph = -1;
    }
  }

  if (!iAnyGlyph) {
    iTop = 0;
    iBottom = (8 * iScaleSize + 63) >> 6;
  }

  *piTop = iTop;
  *piHeight = iBottom - iTop;
}

//-------------------------------------------------------------------------------------------------

void frontend_mouse_register_text(int iId, tBlockHeader *pFont, const char *szText,
                                  const char *szMappingTable, int *pCharVOffsets,
                                  int iX, int iY, int iAlignment)
{
  int iWidth;
  int iTop;
  int iHeight;

  iWidth = frontend_mouse_text_width(pFont, szText, szMappingTable);
  if (iWidth <= 0)
    return;

  frontend_mouse_text_height(pFont, szText, szMappingTable, pCharVOffsets,
                             64, &iTop, &iHeight);

  if (iAlignment == 1)
    iX -= iWidth / 2;
  else if (iAlignment == 2)
    iX -= iWidth;

  frontend_mouse_register_rect(iId, iX, iY + iTop, iWidth, iHeight);
}

//-------------------------------------------------------------------------------------------------

void frontend_mouse_register_scaled_text(int iId, tBlockHeader *pFont,
                                         const char *szText,
                                         const char *szMappingTable,
                                         int *pCharVOffsets, int iX, int iY,
                                         unsigned int uiAlignment,
                                         int iClipLeft, int iClipRight)
{
  int iTextWidth;
  int iLeftBound;
  int iRightBound;
  int iScaleSize = 64;
  int iScaledWidth = 0;
  int iTop;
  int iHeight;
  const char *szIter;

  iTextWidth = frontend_mouse_text_width(pFont, szText, szMappingTable);
  if (iTextWidth <= 0)
    return;

  if (uiAlignment == 1) {
    iLeftBound = iX - iTextWidth / 2;
    iRightBound = iX + iTextWidth / 2;
  } else if (uiAlignment == 2) {
    iLeftBound = iX - iTextWidth;
    iRightBound = iX;
  } else {
    iLeftBound = iX;
    iRightBound = iX + iTextWidth;
  }

  if (iClipLeft > iLeftBound || iRightBound > iClipRight) {
    int iAvailableWidth;

    if (iClipLeft <= iLeftBound)
      iAvailableWidth = iClipRight - iLeftBound;
    else if (iRightBound <= iClipRight)
      iAvailableWidth = iRightBound - iClipLeft;
    else
      iAvailableWidth = iClipRight - iClipLeft;

    if (iAvailableWidth <= 0)
      return;

    iScaleSize = (iAvailableWidth << 6) / iTextWidth;
    if (iScaleSize <= 0)
      iScaleSize = 1;
    if (iScaleSize > 64)
      iScaleSize = 64;
  }

  for (szIter = szText; *szIter; ++szIter) {
    uint8 byMapped = (uint8)szMappingTable[(uint8)*szIter];

    if (byMapped == 0xFF)
      iScaledWidth += (8 * iScaleSize) >> 6;
    else
      iScaledWidth += (iScaleSize * (pFont[byMapped].iWidth + 1)) >> 6;
  }

  if (iScaledWidth <= 0)
    return;

  frontend_mouse_text_height(pFont, szText, szMappingTable, pCharVOffsets,
                             iScaleSize, &iTop, &iHeight);

  if (uiAlignment == 1)
    iX -= iScaledWidth / 2;
  else if (uiAlignment == 2)
    iX -= iScaledWidth;

  frontend_mouse_register_rect(iId, iX, iY + iTop, iScaledWidth, iHeight);
}

//-------------------------------------------------------------------------------------------------

int frontend_mouse_take_hovered_id(void)
{
  if (s_uiFrontendMouseMotionSeq == s_uiFrontendMouseConsumedMotionSeq)
    return -1;

  s_uiFrontendMouseConsumedMotionSeq = s_uiFrontendMouseMotionSeq;
  return s_iFrontendMouseHoveredId;
}

//-------------------------------------------------------------------------------------------------

int frontend_mouse_peek_clicked_id(void)
{
  return s_iFrontendMouseClickedId;
}

//-------------------------------------------------------------------------------------------------

int frontend_mouse_consume_click(void)
{
  if (s_uiFrontendMouseClickSeq == s_uiFrontendMouseConsumedClickSeq)
    return -1;

  s_uiFrontendMouseConsumedClickSeq = s_uiFrontendMouseClickSeq;
  return s_iFrontendMouseClickedId;
}

//-------------------------------------------------------------------------------------------------

int frontend_mouse_consume_click_anywhere(void)
{
  if (s_uiFrontendMouseClickSeq == s_uiFrontendMouseConsumedClickSeq)
    return 0;

  s_uiFrontendMouseConsumedClickSeq = s_uiFrontendMouseClickSeq;
  return -1;
}

//-------------------------------------------------------------------------------------------------

int frontend_mouse_take_wheel_y(void)
{
  int iWheelY = 0;

  while (s_fFrontendMouseWheelY >= 1.0f) {
    ++iWheelY;
    s_fFrontendMouseWheelY -= 1.0f;
  }
  while (s_fFrontendMouseWheelY <= -1.0f) {
    --iWheelY;
    s_fFrontendMouseWheelY += 1.0f;
  }

  return iWheelY;
}

//-------------------------------------------------------------------------------------------------

void frontend_mouse_press_accept(void)
{
  key_handler(WHIP_SCANCODE_RETURN);
  key_handler(WHIP_SCANCODE_RETURN | 0x80);
}

//-------------------------------------------------------------------------------------------------
