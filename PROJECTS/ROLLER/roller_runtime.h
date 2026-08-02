#ifndef ROLLER_RUNTIME_H
#define ROLLER_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#  if defined(ROLLER_RUNTIME_BUILD_SHARED)
#    if defined(ROLLER_RUNTIME_EXPORTS)
#      define ROLLER_RUNTIME_API __declspec(dllexport)
#    else
#      define ROLLER_RUNTIME_API __declspec(dllimport)
#    endif
#  else
#    define ROLLER_RUNTIME_API
#  endif
#  define ROLLER_RUNTIME_CALL __cdecl
#elif defined(__GNUC__) || defined(__clang__)
#  define ROLLER_RUNTIME_API __attribute__((visibility("default")))
#  define ROLLER_RUNTIME_CALL
#else
#  define ROLLER_RUNTIME_API
#  define ROLLER_RUNTIME_CALL
#endif

#if defined(__cplusplus)
extern "C" {
#endif

#define ROLLER_RUNTIME_API_VERSION 1u

typedef struct RollerRuntime RollerRuntime;

typedef uint32_t eRollerRuntimeResult;
enum
{
  ROLLER_RUNTIME_RESULT_OK = 0u,
  ROLLER_RUNTIME_RESULT_INVALID_ARGUMENT = 1u,
  ROLLER_RUNTIME_RESULT_INVALID_VERSION = 2u,
  ROLLER_RUNTIME_RESULT_OUT_OF_MEMORY = 3u,
  ROLLER_RUNTIME_RESULT_INVALID_STATE = 4u,
  ROLLER_RUNTIME_RESULT_STEP_FAILED = 5u,
};

typedef uint32_t eRollerRuntimeStatus;
enum
{
  ROLLER_RUNTIME_STATUS_EMPTY = 0u,
  ROLLER_RUNTIME_STATUS_CREATED = 1u,
  ROLLER_RUNTIME_STATUS_READY = 2u,
  ROLLER_RUNTIME_STATUS_RUNNING = 3u,
  ROLLER_RUNTIME_STATUS_FINISHED = 4u,
  ROLLER_RUNTIME_STATUS_FAILED = 5u,
};

enum
{
  ROLLER_RUNTIME_FLAG_HEADLESS = 1u << 0,
  ROLLER_RUNTIME_FLAG_DETERMINISTIC = 1u << 1,
};

typedef eRollerRuntimeResult (ROLLER_RUNTIME_CALL *RollerRuntimeInputAdvanceFn)(
    void *pUserData, uint32_t uiTickIndex);

typedef struct
{
  uint32_t uiStructSize;
  uint32_t uiVersion;
  uint32_t uiFlags;
} tRollerRuntimeConfig;

typedef struct
{
  uint32_t uiStructSize;
  uint32_t uiVersion;
  void *pUserData;
  RollerRuntimeInputAdvanceFn pfnAdvance;
} tRollerRuntimeInputSource;

ROLLER_RUNTIME_API eRollerRuntimeResult ROLLER_RUNTIME_CALL
RollerRuntime_Create(const tRollerRuntimeConfig *pConfig,
                     RollerRuntime **ppRuntime);

ROLLER_RUNTIME_API void ROLLER_RUNTIME_CALL
RollerRuntime_Destroy(RollerRuntime *pRuntime);

ROLLER_RUNTIME_API eRollerRuntimeResult ROLLER_RUNTIME_CALL
RollerRuntime_SetInputSource(RollerRuntime *pRuntime,
                             const tRollerRuntimeInputSource *pSource);

ROLLER_RUNTIME_API eRollerRuntimeResult ROLLER_RUNTIME_CALL
RollerRuntime_ClearInputSource(RollerRuntime *pRuntime);

ROLLER_RUNTIME_API eRollerRuntimeResult ROLLER_RUNTIME_CALL
RollerRuntime_Step(RollerRuntime *pRuntime, uint32_t uiTicks);

ROLLER_RUNTIME_API eRollerRuntimeStatus ROLLER_RUNTIME_CALL
RollerRuntime_GetStatus(const RollerRuntime *pRuntime);

ROLLER_RUNTIME_API const char *ROLLER_RUNTIME_CALL
RollerRuntime_GetLastError(const RollerRuntime *pRuntime);

#if defined(__cplusplus)
}
#endif

#endif
