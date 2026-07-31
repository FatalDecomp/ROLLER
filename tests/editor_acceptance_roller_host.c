/* The editor acceptance executable needs the legacy globals and file helpers
 * still owned by roller.c, but supplies its own focused main entry point. */
#define main roller_game_main
#define ErrorBoxExit roller_game_ErrorBoxExit
#include "../PROJECTS/ROLLER/roller.c"

void SaveDefaultFatalIni(const char *szFatdata)
{
    (void)szFatdata;
}

void ExtractFATDATA(const char *szImagePath, const char *szOutDir)
{
    (void)szImagePath;
    (void)szOutDir;
}
