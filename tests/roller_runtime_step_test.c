#include "roller_runtime.h"

#include <stdio.h>

int g_runtime_test_frontend_on = 0;
int g_runtime_test_tick_clock_calls = 0;
int g_runtime_test_game_tick_calls = 0;
int g_runtime_test_clear_pending_calls = 0;
int g_runtime_test_input_advance_calls = 0;
uint32_t g_runtime_test_last_tick_index = 999u;

void runtime_test_tick_clock_step(void)
{
  g_runtime_test_tick_clock_calls++;
}

void runtime_test_game_tick_step(void)
{
  g_runtime_test_game_tick_calls++;
}

void runtime_test_clear_pending_ticks(void)
{
  g_runtime_test_clear_pending_calls++;
}

static eRollerRuntimeResult ROLLER_RUNTIME_CALL input_advance(void *pUserData, uint32_t uiTickIndex)
{
  int *piAccumulator = (int *)pUserData;
  if (!piAccumulator)
    return ROLLER_RUNTIME_RESULT_INVALID_ARGUMENT;
  g_runtime_test_input_advance_calls++;
  g_runtime_test_last_tick_index = uiTickIndex;
  *piAccumulator += 1;
  return ROLLER_RUNTIME_RESULT_OK;
}

#define CHECK(condition) \
  do { \
    if (!(condition)) { \
      fprintf(stderr, "roller_runtime_step_test failed at line %d: %s\n", \
              __LINE__, #condition); \
      return 1; \
    } \
  } while (0)

int main(void)
{
  RollerRuntime runtime;
  int iInputAccumulator = 0;
  tRollerRuntimeConfig config = {
    .uiStructSize = sizeof(config),
    .uiVersion = ROLLER_RUNTIME_API_VERSION,
  };
  tRollerRuntimeInputSource source = {
    .uiStructSize = sizeof(source),
    .uiVersion = ROLLER_RUNTIME_API_VERSION,
    .pUserData = &iInputAccumulator,
    .pfnAdvance = input_advance,
  };

  CHECK(RollerRuntime_Step(NULL, 1) == ROLLER_RUNTIME_RESULT_INVALID_ARGUMENT);
  runtime = (RollerRuntime){0};
  CHECK(RollerRuntime_Step(&runtime, 1) == ROLLER_RUNTIME_RESULT_INVALID_STATE);

  runtime = RollerRuntime_Create(NULL);
  CHECK(RollerRuntime_Step(&runtime, 1) == ROLLER_RUNTIME_RESULT_INVALID_STATE);

  runtime = RollerRuntime_Create(&config);
  CHECK(RollerRuntime_GetStatus(&runtime) == ROLLER_RUNTIME_STATUS_CREATED);
  CHECK(RollerRuntime_Step(&runtime, 1) == ROLLER_RUNTIME_RESULT_INVALID_STATE);

  CHECK(RollerRuntime_SetInputSource(&runtime, &source) == ROLLER_RUNTIME_RESULT_OK);
  CHECK(RollerRuntime_Step(&runtime, 0) == ROLLER_RUNTIME_RESULT_INVALID_ARGUMENT);
  CHECK(RollerRuntime_Step(&runtime, 3) == ROLLER_RUNTIME_RESULT_OK);
  CHECK(g_runtime_test_input_advance_calls == 3);
  CHECK(g_runtime_test_last_tick_index == 2u);
  CHECK(iInputAccumulator == 3);
  CHECK(g_runtime_test_tick_clock_calls == 3);
  CHECK(g_runtime_test_clear_pending_calls == 3);
  CHECK(g_runtime_test_game_tick_calls == 3);
  CHECK(RollerRuntime_GetStatus(&runtime) == ROLLER_RUNTIME_STATUS_RUNNING);

  g_runtime_test_frontend_on = 1;
  CHECK(RollerRuntime_Step(&runtime, 2) == ROLLER_RUNTIME_RESULT_OK);
  CHECK(g_runtime_test_input_advance_calls == 5);
  CHECK(g_runtime_test_tick_clock_calls == 5);
  CHECK(g_runtime_test_clear_pending_calls == 5);
  CHECK(g_runtime_test_game_tick_calls == 3);

  RollerRuntime_Destroy(&runtime);
  return 0;
}
