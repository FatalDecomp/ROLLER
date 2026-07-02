#include "platform_log.h"
#include "types.h"
#include <SDL3/SDL.h>
//-------------------------------------------------------------------------------------------------

#if defined(IS_ANDROID)
#include <android/log.h>

#define ROLLER_ANDROID_LOG_TAG "ROLLER"

static SDL_LogOutputFunction s_pPrevLogFn = NULL;
static void *s_pPrevLogUserdata = NULL;
static bool s_bInstalled = false;

//-------------------------------------------------------------------------------------------------

static int AndroidLogPriority(SDL_LogPriority priority)
{
  switch (priority) {
  case SDL_LOG_PRIORITY_VERBOSE:  return ANDROID_LOG_VERBOSE;
  case SDL_LOG_PRIORITY_DEBUG:    return ANDROID_LOG_DEBUG;
  case SDL_LOG_PRIORITY_INFO:     return ANDROID_LOG_INFO;
  case SDL_LOG_PRIORITY_WARN:     return ANDROID_LOG_WARN;
  case SDL_LOG_PRIORITY_ERROR:    return ANDROID_LOG_ERROR;
  case SDL_LOG_PRIORITY_CRITICAL: return ANDROID_LOG_FATAL;
  default:                        return ANDROID_LOG_INFO;
  }
}

//-------------------------------------------------------------------------------------------------

static const char *SdlLogCategoryName(int iCategory)
{
  switch (iCategory) {
  case SDL_LOG_CATEGORY_APPLICATION: return "app";
  case SDL_LOG_CATEGORY_ERROR:       return "error";
  case SDL_LOG_CATEGORY_ASSERT:      return "assert";
  case SDL_LOG_CATEGORY_SYSTEM:      return "system";
  case SDL_LOG_CATEGORY_AUDIO:       return "audio";
  case SDL_LOG_CATEGORY_VIDEO:       return "video";
  case SDL_LOG_CATEGORY_RENDER:      return "render";
  case SDL_LOG_CATEGORY_INPUT:       return "input";
  case SDL_LOG_CATEGORY_TEST:        return "test";
  case SDL_LOG_CATEGORY_GPU:         return "gpu";
  default:                           return NULL;
  }
}

//-------------------------------------------------------------------------------------------------

static void AndroidLogCallback(void *pUserdata, int iCategory,
                               SDL_LogPriority priority,
                               const char *pMessage)
{
  const char *pszCategory = SdlLogCategoryName(iCategory);
  (void)pUserdata;

  if (!pMessage)
    pMessage = "";

  if (pszCategory) {
    __android_log_print(AndroidLogPriority(priority), ROLLER_ANDROID_LOG_TAG,
                        "[%s] %s", pszCategory, pMessage);
  } else {
    __android_log_print(AndroidLogPriority(priority), ROLLER_ANDROID_LOG_TAG,
                        "[category %d] %s", iCategory, pMessage);
  }
}
#endif

//-------------------------------------------------------------------------------------------------

void ROLLERInstallPlatformLogSink(void)
{
#if defined(IS_ANDROID)
  if (s_bInstalled)
    return;

  SDL_GetLogOutputFunction(&s_pPrevLogFn, &s_pPrevLogUserdata);
  SDL_SetLogOutputFunction(AndroidLogCallback, NULL);
  s_bInstalled = true;
#endif
}

//-------------------------------------------------------------------------------------------------

void ROLLERRestorePlatformLogSink(void)
{
#if defined(IS_ANDROID)
  SDL_LogOutputFunction pCurrentLogFn = NULL;
  void *pCurrentLogUserdata = NULL;

  if (!s_bInstalled)
    return;

  SDL_GetLogOutputFunction(&pCurrentLogFn, &pCurrentLogUserdata);
  if (pCurrentLogFn == AndroidLogCallback && pCurrentLogUserdata == NULL)
    SDL_SetLogOutputFunction(s_pPrevLogFn, s_pPrevLogUserdata);

  s_pPrevLogFn = NULL;
  s_pPrevLogUserdata = NULL;
  s_bInstalled = false;
#endif
}

//-------------------------------------------------------------------------------------------------
