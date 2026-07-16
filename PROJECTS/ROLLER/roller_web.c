#include "roller.h"
#include "rollercd.h"

#include <SDL3/SDL.h>
#include <emscripten/emscripten.h>
#include <string.h>

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
  SaveDefaultFatalIni(szOutDir);

  return ROLLERdirexists(szFatdataPath) ? 1 : 0;
}
