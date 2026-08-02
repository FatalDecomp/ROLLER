#include "roller_runtime.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct RollerRuntime
{
  uint32_t uiFlags;
  eRollerRuntimeStatus eStatus;
  tRollerRuntimeInputSource InputSource;
  int iHasInputSource;
  char szLastError[512];
};

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

eRollerRuntimeResult ROLLER_RUNTIME_CALL
RollerRuntime_Create(const tRollerRuntimeConfig *pConfig,
                     RollerRuntime **ppRuntime)
{
  eRollerRuntimeResult eResult;
  RollerRuntime *pRuntime;

  if (!ppRuntime)
    return ROLLER_RUNTIME_RESULT_INVALID_ARGUMENT;
  *ppRuntime = NULL;
  if (!pConfig)
    return ROLLER_RUNTIME_RESULT_INVALID_ARGUMENT;

  eResult = runtime_validate_struct(pConfig->uiStructSize, pConfig->uiVersion,
                                    sizeof(*pConfig));
  if (eResult != ROLLER_RUNTIME_RESULT_OK)
    return eResult;

  pRuntime = (RollerRuntime *)calloc(1, sizeof(*pRuntime));
  if (!pRuntime)
    return ROLLER_RUNTIME_RESULT_OUT_OF_MEMORY;

  pRuntime->uiFlags = pConfig->uiFlags;
  pRuntime->eStatus = ROLLER_RUNTIME_STATUS_CREATED;
  *ppRuntime = pRuntime;
  return ROLLER_RUNTIME_RESULT_OK;
}

void ROLLER_RUNTIME_CALL RollerRuntime_Destroy(RollerRuntime *pRuntime)
{
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
  memset(&pRuntime->InputSource, 0, sizeof(pRuntime->InputSource));
  pRuntime->iHasInputSource = 0;
  pRuntime->eStatus = ROLLER_RUNTIME_STATUS_CREATED;
  return ROLLER_RUNTIME_RESULT_OK;
}

eRollerRuntimeResult ROLLER_RUNTIME_CALL
RollerRuntime_Step(RollerRuntime *pRuntime, uint32_t uiTicks)
{
  (void)uiTicks;
  if (!pRuntime)
    return ROLLER_RUNTIME_RESULT_INVALID_ARGUMENT;
  runtime_set_error(pRuntime, "runtime stepping is not connected yet");
  pRuntime->eStatus = ROLLER_RUNTIME_STATUS_FAILED;
  return ROLLER_RUNTIME_RESULT_STEP_FAILED;
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
