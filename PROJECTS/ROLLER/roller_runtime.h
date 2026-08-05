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

/*
 * SDL-free fixed-step ROLLER simulation runtime.
 *
 * RollerRuntime owns no replay files, file-format parsing, rendering resources,
 * or SDL objects. Replay/tools/editor hosts feed input/timeline data through an
 * input source, step the runtime, then render from the existing game state via
 * separate renderer APIs.
 */
#define ROLLER_RUNTIME_API_VERSION 2u

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

typedef eRollerRuntimeResult (ROLLER_RUNTIME_CALL *RollerRuntimeInputAdvanceFn)(
    void *pUserData, uint32_t uiTickIndex);

/*
 * Config/input-source structs include uiStructSize and uiVersion so callers can
 * be rejected cleanly if they were compiled against a different runtime API.
 * Set uiStructSize to sizeof(the struct being passed) and uiVersion to
 * ROLLER_RUNTIME_API_VERSION.
 */
typedef struct
{
  uint32_t uiStructSize;
  uint32_t uiVersion;
} tRollerRuntimeConfig;

typedef struct
{
  uint32_t uiStructSize;
  uint32_t uiVersion;
  void *pUserData;
  RollerRuntimeInputAdvanceFn pfnAdvance;
} tRollerRuntimeInputSource;

/*
 * Runtime state is public so embedders can place it on the stack or inside
 * their own allocation systems. Treat the fields as private: initialize with
 * RollerRuntime_Create(), mutate through the RollerRuntime_* functions, and
 * finish with RollerRuntime_Destroy(). Recompile consumers when
 * ROLLER_RUNTIME_API_VERSION changes; the visible struct layout is API, not a
 * persistent save-data format.
 */
typedef struct RollerRuntime
{
  eRollerRuntimeStatus eStatus;
  tRollerRuntimeInputSource InputSource;
  int iHasInputSource;
  char szLastError[512];
} RollerRuntime;

/* Creates a caller-owned runtime value. No heap allocation is performed. */
ROLLER_RUNTIME_API RollerRuntime ROLLER_RUNTIME_CALL
RollerRuntime_Create(const tRollerRuntimeConfig *pConfig);

/* Allocates a runtime with malloc(), then initializes it with RollerRuntime_Create(). */
ROLLER_RUNTIME_API eRollerRuntimeResult ROLLER_RUNTIME_CALL
RollerRuntime_New(const tRollerRuntimeConfig *pConfig,
                  RollerRuntime **ppRuntime);

/* Releases runtime-owned references but does not free the RollerRuntime object. */
ROLLER_RUNTIME_API void ROLLER_RUNTIME_CALL
RollerRuntime_Destroy(RollerRuntime *pRuntime);

/* Calls RollerRuntime_Destroy(), then frees a runtime allocated by RollerRuntime_New(). */
ROLLER_RUNTIME_API void ROLLER_RUNTIME_CALL
RollerRuntime_Delete(RollerRuntime *pRuntime);

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
