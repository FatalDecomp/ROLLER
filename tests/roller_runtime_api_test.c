#include "roller_runtime.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) \
  do { \
    if (!(condition)) { \
      fprintf(stderr, "roller_runtime_api_test failed at line %d: %s\n", \
              __LINE__, #condition); \
      return 1; \
    } \
  } while (0)

static eRollerRuntimeResult ROLLER_RUNTIME_CALL test_advance(void *pUserData, uint32_t uiTickIndex)
{
  int *piAdvanceCount = (int *)pUserData;
  if (!piAdvanceCount)
    return ROLLER_RUNTIME_RESULT_INVALID_ARGUMENT;
  *piAdvanceCount += (int)uiTickIndex + 1;
  return ROLLER_RUNTIME_RESULT_OK;
}

int main(void)
{
  RollerRuntime *pRuntime = (RollerRuntime *)0x1;
  int iAdvanceCount = 0;
  tRollerRuntimeConfig config = {
    .uiStructSize = sizeof(config),
    .uiVersion = ROLLER_RUNTIME_API_VERSION,
    .uiFlags = ROLLER_RUNTIME_FLAG_HEADLESS | ROLLER_RUNTIME_FLAG_DETERMINISTIC,
  };
  tRollerRuntimeInputSource source = {
    .uiStructSize = sizeof(source),
    .uiVersion = ROLLER_RUNTIME_API_VERSION,
    .pUserData = &iAdvanceCount,
    .pfnAdvance = test_advance,
  };

  CHECK(RollerRuntime_Create(NULL, &pRuntime) == ROLLER_RUNTIME_RESULT_INVALID_ARGUMENT);
  CHECK(RollerRuntime_Create(&config, NULL) == ROLLER_RUNTIME_RESULT_INVALID_ARGUMENT);

  config.uiVersion++;
  pRuntime = NULL;
  CHECK(RollerRuntime_Create(&config, &pRuntime) == ROLLER_RUNTIME_RESULT_INVALID_VERSION);
  CHECK(pRuntime == NULL);

  config.uiVersion = ROLLER_RUNTIME_API_VERSION;
  CHECK(RollerRuntime_Create(&config, &pRuntime) == ROLLER_RUNTIME_RESULT_OK);
  CHECK(pRuntime != NULL);
  CHECK(RollerRuntime_GetStatus(pRuntime) == ROLLER_RUNTIME_STATUS_CREATED);
  CHECK(strcmp(RollerRuntime_GetLastError(pRuntime), "") == 0);

  CHECK(RollerRuntime_SetInputSource(NULL, &source) == ROLLER_RUNTIME_RESULT_INVALID_ARGUMENT);
  CHECK(RollerRuntime_SetInputSource(pRuntime, NULL) == ROLLER_RUNTIME_RESULT_INVALID_ARGUMENT);

  source.uiVersion++;
  CHECK(RollerRuntime_SetInputSource(pRuntime, &source) == ROLLER_RUNTIME_RESULT_INVALID_VERSION);

  source.uiVersion = ROLLER_RUNTIME_API_VERSION;
  source.pfnAdvance = NULL;
  CHECK(RollerRuntime_SetInputSource(pRuntime, &source) == ROLLER_RUNTIME_RESULT_INVALID_ARGUMENT);

  source.pfnAdvance = test_advance;
  CHECK(RollerRuntime_SetInputSource(pRuntime, &source) == ROLLER_RUNTIME_RESULT_OK);
  CHECK(RollerRuntime_GetStatus(pRuntime) == ROLLER_RUNTIME_STATUS_READY);
  CHECK(RollerRuntime_ClearInputSource(pRuntime) == ROLLER_RUNTIME_RESULT_OK);
  CHECK(RollerRuntime_GetStatus(pRuntime) == ROLLER_RUNTIME_STATUS_CREATED);

  RollerRuntime_Destroy(pRuntime);
  RollerRuntime_Destroy(NULL);
  return 0;
}
