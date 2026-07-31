#include "roller_core_error.h"

#if defined(ROLLER_EDITOR_CORE)

#include <stdarg.h>
#include <stdio.h>

#if defined(_MSC_VER)
#define ROLLER_CORE_THREAD_LOCAL __declspec(thread)
#else
#define ROLLER_CORE_THREAD_LOCAL _Thread_local
#endif

static ROLLER_CORE_THREAD_LOCAL char s_szCoreError[512];
static ROLLER_CORE_THREAD_LOCAL int s_iCoreErrorPending;

void ErrorBoxExit(const char *szErrorMsgFormat, ...)
{
    va_list Args;

    va_start(Args, szErrorMsgFormat);
    vsnprintf(s_szCoreError, sizeof(s_szCoreError),
              szErrorMsgFormat ? szErrorMsgFormat : "", Args);
    va_end(Args);
    s_szCoreError[sizeof(s_szCoreError) - 1u] = '\0';
    s_iCoreErrorPending = 1;
    fprintf(stderr, "roller-core recovered legacy fatal error: %s\n",
            s_szCoreError);
}

void roller_core_error_clear(void)
{
    s_szCoreError[0] = '\0';
    s_iCoreErrorPending = 0;
}

int roller_core_error_pending(void)
{
    return s_iCoreErrorPending;
}

const char *roller_core_error_message(void)
{
    return s_szCoreError;
}

#endif
