/*
 * The legacy renderer still obtains process-wide platform services and
 * configuration globals from roller.c.  A game executable compiles roller.c
 * directly; roller-core uses this headless host wrapper so the static archive
 * is a complete, consumer-linkable library without the game entry point or
 * real sound/CD implementations.
 */
#if !defined(ROLLER_EDITOR_CORE)
#error editor_core_host.c is only for roller-core
#endif

/* roller_core_error.c owns the recoverable editor error boundary. */
#define ErrorBoxExit roller_game_ErrorBoxExit
#include "roller.c"
#undef ErrorBoxExit

void SaveDefaultFatalIni(const char *szFatdata)
{
  (void)szFatdata;
}

void ExtractFATDATA(const char *szImagePath, const char *szOutDir)
{
  (void)szImagePath;
  (void)szOutDir;
}
