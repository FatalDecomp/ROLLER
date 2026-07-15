/*
 * Native crash handlers cannot run in a browser sandbox. Development wasm
 * builds use Emscripten assertions instead, so initialization is a no-op.
 */
#include "crashdump.h"

void InitCrashHandler(const char *szDataRoot)
{
  (void)szDataRoot;
}
