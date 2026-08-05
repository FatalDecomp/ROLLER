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
  RollerRuntime runtime;
  RollerRuntime *pHeapRuntime = (RollerRuntime *)0x1;
  int iAdvanceCount = 0;
  tRollerRuntimeConfig config = {
    .uiStructSize = sizeof(config),
    .uiVersion = ROLLER_RUNTIME_API_VERSION,
  };
  tRollerRuntimeInputSource source = {
    .uiStructSize = sizeof(source),
    .uiVersion = ROLLER_RUNTIME_API_VERSION,
    .pUserData = &iAdvanceCount,
    .pfnAdvance = test_advance,
  };

  runtime = RollerRuntime_Create(NULL);
  CHECK(RollerRuntime_GetStatus(&runtime) == ROLLER_RUNTIME_STATUS_FAILED);
  CHECK(strcmp(RollerRuntime_GetLastError(&runtime), "") != 0);
  CHECK(RollerRuntime_ClearInputSource(&runtime) == ROLLER_RUNTIME_RESULT_INVALID_STATE);

  CHECK(RollerRuntime_SetInputSource(&runtime, &source) == ROLLER_RUNTIME_RESULT_INVALID_STATE);


  config.uiVersion++;
  runtime = RollerRuntime_Create(&config);
  CHECK(RollerRuntime_GetStatus(&runtime) == ROLLER_RUNTIME_STATUS_FAILED);
  pHeapRuntime = NULL;
  CHECK(RollerRuntime_New(&config, &pHeapRuntime) == ROLLER_RUNTIME_RESULT_INVALID_VERSION);
  CHECK(pHeapRuntime == NULL);

  config.uiVersion = ROLLER_RUNTIME_API_VERSION;
  CHECK(RollerRuntime_New(NULL, &pHeapRuntime) == ROLLER_RUNTIME_RESULT_INVALID_ARGUMENT);
  CHECK(RollerRuntime_New(&config, NULL) == ROLLER_RUNTIME_RESULT_INVALID_ARGUMENT);

  runtime = RollerRuntime_Create(&config);
  CHECK(RollerRuntime_GetStatus(&runtime) == ROLLER_RUNTIME_STATUS_CREATED);
  CHECK(strcmp(RollerRuntime_GetLastError(&runtime), "") == 0);

  CHECK(RollerRuntime_SetInputSource(NULL, &source) == ROLLER_RUNTIME_RESULT_INVALID_ARGUMENT);
  CHECK(RollerRuntime_SetInputSource(&runtime, NULL) == ROLLER_RUNTIME_RESULT_INVALID_ARGUMENT);

  source.uiVersion++;
  CHECK(RollerRuntime_SetInputSource(&runtime, &source) == ROLLER_RUNTIME_RESULT_INVALID_VERSION);

  source.uiVersion = ROLLER_RUNTIME_API_VERSION;
  source.pfnAdvance = NULL;
  CHECK(RollerRuntime_SetInputSource(&runtime, &source) == ROLLER_RUNTIME_RESULT_INVALID_ARGUMENT);

  source.pfnAdvance = test_advance;
  CHECK(RollerRuntime_SetInputSource(&runtime, &source) == ROLLER_RUNTIME_RESULT_OK);
  CHECK(RollerRuntime_GetStatus(&runtime) == ROLLER_RUNTIME_STATUS_READY);
  CHECK(RollerRuntime_ClearInputSource(&runtime) == ROLLER_RUNTIME_RESULT_OK);
  CHECK(RollerRuntime_GetStatus(&runtime) == ROLLER_RUNTIME_STATUS_CREATED);
  RollerRuntime_Destroy(&runtime);
  CHECK(RollerRuntime_GetStatus(&runtime) == ROLLER_RUNTIME_STATUS_EMPTY);
  CHECK(RollerRuntime_SetInputSource(&runtime, &source) == ROLLER_RUNTIME_RESULT_INVALID_STATE);
  CHECK(RollerRuntime_ClearInputSource(&runtime) == ROLLER_RUNTIME_RESULT_INVALID_STATE);
  runtime = (RollerRuntime){0};
  CHECK(RollerRuntime_SetInputSource(&runtime, &source) == ROLLER_RUNTIME_RESULT_INVALID_STATE);
  CHECK(RollerRuntime_ClearInputSource(&runtime) == ROLLER_RUNTIME_RESULT_INVALID_STATE);



  CHECK(RollerRuntime_New(&config, &pHeapRuntime) == ROLLER_RUNTIME_RESULT_OK);
  CHECK(pHeapRuntime != NULL);
  CHECK(RollerRuntime_GetStatus(pHeapRuntime) == ROLLER_RUNTIME_STATUS_CREATED);
  RollerRuntime_Delete(pHeapRuntime);
  RollerRuntime_Delete(NULL);

  return 0;
}
