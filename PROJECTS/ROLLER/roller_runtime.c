#include "roller_runtime.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(ROLLER_RUNTIME_TEST_SEAMS)
#if defined(ROLLER_RUNTIME_TEST_SEAMS_DEFAULTS)
static int g_runtime_test_frontend_on;
static void runtime_test_tick_clock_step(void) {}
static void runtime_test_game_tick_step(void) {}
static void runtime_test_clear_pending_ticks(void) {}
#else
extern int g_runtime_test_frontend_on;
void runtime_test_tick_clock_step(void);
void runtime_test_game_tick_step(void);
void runtime_test_clear_pending_ticks(void);
#endif
#define RUNTIME_FRONTEND_ON() (g_runtime_test_frontend_on)
#define RUNTIME_TICK_CLOCK_STEP() runtime_test_tick_clock_step()
#define RUNTIME_GAME_TICK_STEP() runtime_test_game_tick_step()
#define RUNTIME_CLEAR_PENDING_TICKS() runtime_test_clear_pending_ticks()
#else
#include "frontend.h"
#include "roller.h"
#include "sound.h"
#include <SDL3/SDL_atomic.h>
#define RUNTIME_FRONTEND_ON() (frontend_on)
#define RUNTIME_TICK_CLOCK_STEP() tick_clock_step()
#define RUNTIME_GAME_TICK_STEP() game_tick_step()
#define RUNTIME_CLEAR_PENDING_TICKS() SDL_SetAtomicInt(&iTicksPending, 0)
#endif

static void runtime_clear_error(RollerRuntime *pRuntime)
{
  if (pRuntime)
    pRuntime->szLastError[0] = '\0';
}

static void runtime_set_error(RollerRuntime *pRuntime, const char *szFormat, ...)
{
  if (!pRuntime)
    return;

  va_list args;
  va_start(args, szFormat);
  vsnprintf(pRuntime->szLastError, sizeof(pRuntime->szLastError), szFormat, args);
  va_end(args);
  pRuntime->szLastError[sizeof(pRuntime->szLastError) - 1u] = '\0';
}

static eRollerRuntimeResult runtime_validate_struct(uint32_t uiStructSize,
                                                    uint32_t uiVersion,
                                                    uint32_t uiRequiredSize)
{
  if (uiVersion != ROLLER_RUNTIME_API_VERSION)
    return ROLLER_RUNTIME_RESULT_INVALID_VERSION;
  if (uiStructSize < uiRequiredSize)
    return ROLLER_RUNTIME_RESULT_INVALID_ARGUMENT;
  return ROLLER_RUNTIME_RESULT_OK;
}

static int runtime_is_usable(const RollerRuntime *pRuntime)
{
  if (!pRuntime)
    return 0;
  return pRuntime->eStatus == ROLLER_RUNTIME_STATUS_CREATED ||
         pRuntime->eStatus == ROLLER_RUNTIME_STATUS_READY ||
         pRuntime->eStatus == ROLLER_RUNTIME_STATUS_RUNNING;
}

static eRollerRuntimeResult runtime_validate_config(
    const tRollerRuntimeConfig *pConfig)
{
  if (!pConfig)
    return ROLLER_RUNTIME_RESULT_INVALID_ARGUMENT;
  return runtime_validate_struct(pConfig->uiStructSize, pConfig->uiVersion,
                                 sizeof(*pConfig));
}

static RollerRuntime runtime_make_failed(const char *szMessage)
{
  RollerRuntime Runtime;

  memset(&Runtime, 0, sizeof(Runtime));
  Runtime.eStatus = ROLLER_RUNTIME_STATUS_FAILED;
  runtime_set_error(&Runtime, "%s", szMessage);
  return Runtime;
}

RollerRuntime ROLLER_RUNTIME_CALL
RollerRuntime_Create(const tRollerRuntimeConfig *pConfig)
{
  eRollerRuntimeResult eResult;
  RollerRuntime Runtime;

  eResult = runtime_validate_config(pConfig);
  if (eResult == ROLLER_RUNTIME_RESULT_INVALID_VERSION)
    return runtime_make_failed("runtime config version is not supported");
  if (eResult != ROLLER_RUNTIME_RESULT_OK)
    return runtime_make_failed("runtime config is invalid");

  memset(&Runtime, 0, sizeof(Runtime));
  Runtime.eStatus = ROLLER_RUNTIME_STATUS_CREATED;
  return Runtime;
}

eRollerRuntimeResult ROLLER_RUNTIME_CALL
RollerRuntime_New(const tRollerRuntimeConfig *pConfig,
                  RollerRuntime **ppRuntime)
{
  eRollerRuntimeResult eResult;
  RollerRuntime *pRuntime;

  if (!ppRuntime)
    return ROLLER_RUNTIME_RESULT_INVALID_ARGUMENT;
  *ppRuntime = NULL;

  eResult = runtime_validate_config(pConfig);
  if (eResult != ROLLER_RUNTIME_RESULT_OK)
    return eResult;

  pRuntime = (RollerRuntime *)malloc(sizeof(*pRuntime));
  if (!pRuntime)
    return ROLLER_RUNTIME_RESULT_OUT_OF_MEMORY;

  *pRuntime = RollerRuntime_Create(pConfig);
  *ppRuntime = pRuntime;
  return ROLLER_RUNTIME_RESULT_OK;
}

void ROLLER_RUNTIME_CALL RollerRuntime_Destroy(RollerRuntime *pRuntime)
{
  if (!pRuntime)
    return;
  memset(pRuntime, 0, sizeof(*pRuntime));
}

void ROLLER_RUNTIME_CALL RollerRuntime_Delete(RollerRuntime *pRuntime)
{
  if (!pRuntime)
    return;
  RollerRuntime_Destroy(pRuntime);
  free(pRuntime);
}

eRollerRuntimeResult ROLLER_RUNTIME_CALL
RollerRuntime_SetInputSource(RollerRuntime *pRuntime,
                             const tRollerRuntimeInputSource *pSource)
{
  eRollerRuntimeResult eResult;

  if (!pRuntime || !pSource)
    return ROLLER_RUNTIME_RESULT_INVALID_ARGUMENT;
  runtime_clear_error(pRuntime);
  if (!runtime_is_usable(pRuntime)) {
    runtime_set_error(pRuntime, "runtime is not initialized");
    return ROLLER_RUNTIME_RESULT_INVALID_STATE;
  }


  eResult = runtime_validate_struct(pSource->uiStructSize, pSource->uiVersion,
                                    sizeof(*pSource));
  if (eResult != ROLLER_RUNTIME_RESULT_OK)
    return eResult;
  if (!pSource->pfnAdvance) {
    runtime_set_error(pRuntime, "input source requires pfnAdvance");
    return ROLLER_RUNTIME_RESULT_INVALID_ARGUMENT;
  }

  pRuntime->InputSource = *pSource;
  pRuntime->iHasInputSource = 1;
  pRuntime->eStatus = ROLLER_RUNTIME_STATUS_READY;
  return ROLLER_RUNTIME_RESULT_OK;
}

eRollerRuntimeResult ROLLER_RUNTIME_CALL
RollerRuntime_ClearInputSource(RollerRuntime *pRuntime)
{
  if (!pRuntime)
    return ROLLER_RUNTIME_RESULT_INVALID_ARGUMENT;
  runtime_clear_error(pRuntime);
  if (!runtime_is_usable(pRuntime)) {
    runtime_set_error(pRuntime, "runtime is not initialized");
    return ROLLER_RUNTIME_RESULT_INVALID_STATE;
  }

  memset(&pRuntime->InputSource, 0, sizeof(pRuntime->InputSource));
  pRuntime->iHasInputSource = 0;
  pRuntime->eStatus = ROLLER_RUNTIME_STATUS_CREATED;
  return ROLLER_RUNTIME_RESULT_OK;
}

eRollerRuntimeResult ROLLER_RUNTIME_CALL
RollerRuntime_Step(RollerRuntime *pRuntime, uint32_t uiTicks)
{
  if (!pRuntime)
    return ROLLER_RUNTIME_RESULT_INVALID_ARGUMENT;
  runtime_clear_error(pRuntime);
  if (!runtime_is_usable(pRuntime)) {
    runtime_set_error(pRuntime, "runtime is not initialized");
    return ROLLER_RUNTIME_RESULT_INVALID_STATE;
  }

  if (uiTicks == 0u) {
    runtime_set_error(pRuntime, "step tick count must be greater than zero");
    return ROLLER_RUNTIME_RESULT_INVALID_ARGUMENT;
  }
  if (!pRuntime->iHasInputSource) {
    runtime_set_error(pRuntime, "runtime requires an input source before stepping");
    return ROLLER_RUNTIME_RESULT_INVALID_STATE;
  }

  for (uint32_t i = 0; i < uiTicks; ++i) {
    eRollerRuntimeResult eAdvance = pRuntime->InputSource.pfnAdvance(
        pRuntime->InputSource.pUserData, i);
    if (eAdvance != ROLLER_RUNTIME_RESULT_OK) {
      runtime_set_error(pRuntime, "input source advance failed with result %u",
                        (unsigned)eAdvance);
      pRuntime->eStatus = ROLLER_RUNTIME_STATUS_FAILED;
      return ROLLER_RUNTIME_RESULT_STEP_FAILED;
    }
    RUNTIME_TICK_CLOCK_STEP();
    RUNTIME_CLEAR_PENDING_TICKS();
    if (!RUNTIME_FRONTEND_ON())
      RUNTIME_GAME_TICK_STEP();
  }

  pRuntime->eStatus = ROLLER_RUNTIME_STATUS_RUNNING;
  return ROLLER_RUNTIME_RESULT_OK;
}

eRollerRuntimeStatus ROLLER_RUNTIME_CALL
RollerRuntime_GetStatus(const RollerRuntime *pRuntime)
{
  return pRuntime ? pRuntime->eStatus : ROLLER_RUNTIME_STATUS_EMPTY;
}

const char *ROLLER_RUNTIME_CALL
RollerRuntime_GetLastError(const RollerRuntime *pRuntime)
{
  return pRuntime ? pRuntime->szLastError : "";
}
