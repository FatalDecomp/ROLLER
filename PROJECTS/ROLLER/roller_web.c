#include "roller.h"
#include "rollercd.h"
#include "rollerinput.h"
#include "roller_web.h"
#include "phone_ui.h"
#include "frontend.h"
#include "replay.h"
#include "sound.h"

#include <SDL3/SDL.h>
#include <emscripten/emscripten.h>
#include <string.h>

EM_JS(int, ROLLERWebShowTextDialogJS,
      (int iTarget, const char *szCurrentValue), {
  const showDialog = Module["rollerShowTextDialog"];
  if (typeof showDialog !== "function")
    return 0;

  return showDialog(iTarget, UTF8ToString(szCurrentValue)) ? 1 : 0;
});

int ROLLERWebShowTextDialog(eROLLERWebTextDialog eTarget,
                            const char *szCurrentValue)
{
  if (eTarget <= ROLLER_WEB_TEXT_DIALOG_NONE ||
      eTarget > ROLLER_WEB_TEXT_DIALOG_REPLAY || !szCurrentValue)
    return 0;

  return ROLLERWebShowTextDialogJS((int)eTarget, szCurrentValue);
}

//-------------------------------------------------------------------------------------------------

EMSCRIPTEN_KEEPALIVE
void ROLLERWebTextDialogComplete(int iTarget, const char *szValue, int iAccepted)
{
  if (iTarget == (int)ROLLER_WEB_TEXT_DIALOG_NAME) {
    frontend_config_web_name_entry_complete(szValue, iAccepted);
  } else if (iTarget == (int)ROLLER_WEB_TEXT_DIALOG_REPLAY) {
    replay_web_name_entry_complete(szValue, iAccepted);
  }
}

EMSCRIPTEN_KEEPALIVE
void ROLLERWebSetPhoneMode(int iPhoneMode)
{
  ROLLERSetPhoneUIActive(iPhoneMode);
  SDL_Log("Web phone UI: %s", ROLLERPhoneUIActive() ? "enabled" : "disabled");
}

//-------------------------------------------------------------------------------------------------

EMSCRIPTEN_KEEPALIVE
void ROLLERWebSetAccel(float fX, float fY, float fZ)
{
  if (ROLLERPhoneUIActive())
    InputPhoneSetWebAccel(fX, fY, fZ);
}

//-------------------------------------------------------------------------------------------------

EMSCRIPTEN_KEEPALIVE
int ROLLERWebSetPhoneControls(int iControls)
{
  if (iControls < (int)PHONE_CONTROLS_DISABLED ||
      iControls > (int)PHONE_CONTROLS_TOUCH_TURN)
    return 0;

  InputPhoneSetWebControls((ePhoneControls)iControls);
  SDL_Log("Web phone controls: scheme %d", iControls);
  return 1;
}

//-------------------------------------------------------------------------------------------------

EMSCRIPTEN_KEEPALIVE
int ROLLERWebGetPhoneControls(void)
{
  return InputPhoneGetControls();
}

//-------------------------------------------------------------------------------------------------

EMSCRIPTEN_KEEPALIVE
int ROLLERWebSetWindowSize(int iWidth, int iHeight)
{
  SDL_Window *pWindow = ROLLERGetWindow();

  if (!pWindow || iWidth <= 0 || iHeight <= 0)
    return 0;

  return SDL_SetWindowSize(pWindow, iWidth, iHeight) ? 1 : 0;
}

//-------------------------------------------------------------------------------------------------

static void ROLLERWebChooseExtractedMusicSource(const char *szOutDir)
{
  char szAudioPath[ROLLER_MAX_PATH];
  size_t nOutDirLength = strlen(szOutDir);
  const char *szSeparator =
    (szOutDir[nOutDirLength - 1] == '/' || szOutDir[nOutDirLength - 1] == '\\')
      ? "" : "/";
  int iLength = SDL_snprintf(szAudioPath, sizeof(szAudioPath),
                             "%s%saudio/track02.wav", szOutDir, szSeparator);
  int bUseCD = iLength > 0 && (size_t)iLength < sizeof(szAudioPath) &&
               ROLLERfexists(szAudioPath);

  MusicCard = 0;
  MusicCD = bUseCD ? -1 : 0;
  MusicOS = 0;
  MusicOPL = bUseCD ? 0 : -1;
  SDL_Log("Web CD image import: default music source is %s",
          bUseCD ? "extracted CD audio" : "MIDI OPL3");
}

// This export runs before main(). ExtractFATDATA only logs and accesses the
// filesystem; its startup-overlay hook returns while InitSDL's window is NULL.
// SaveDefaultFatalIni uses the same filesystem-backed configuration writers as
// the native import flow, whose renderer and window lookups are NULL-safe.
EMSCRIPTEN_KEEPALIVE
int ROLLERWebExtractFATDATA(const char *szImagePath, const char *szOutDir)
{
  char szFatdataPath[ROLLER_MAX_PATH];
  size_t nOutDirLength;
  const char *szSeparator;
  int iLength;

  if (!szImagePath || !szImagePath[0] || !szOutDir || !szOutDir[0]) {
    SDL_Log("Web CD image import: image path and output directory are required");
    return 0;
  }

  nOutDirLength = strlen(szOutDir);
  szSeparator = (szOutDir[nOutDirLength - 1] == '/' ||
                 szOutDir[nOutDirLength - 1] == '\\') ? "" : "/";
  iLength = SDL_snprintf(szFatdataPath, sizeof(szFatdataPath), "%s%sFATDATA",
                         szOutDir, szSeparator);
  if (iLength < 0 || (size_t)iLength >= sizeof(szFatdataPath)) {
    SDL_Log("Web CD image import: output path is too long: '%s'", szOutDir);
    return 0;
  }

  ExtractFATDATA(szImagePath, szOutDir);
  if (!ROLLERdirexists(szFatdataPath)) {
    SDL_Log("Web CD image import: extraction did not create '%s'", szFatdataPath);
    return 0;
  }

  // SaveDefaultFatalIni also writes ROLLER.INI. Select the source now so the
  // first retail boot does not load a pre-main MIDI default over the CD-audio
  // choice made by InitFATDATA.
  ROLLERWebChooseExtractedMusicSource(szOutDir);
  SaveDefaultFatalIni(szOutDir);

  return 1;
}
