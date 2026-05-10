#include "snapshot_scenes.h"
#include "snapshot.h"
#include "frontend.h"
#include <stdio.h>
#include <string.h>

static int SnapshotRenderMenuMain(void)
{
  snapshot_render_menu_main();
  return g_SnapshotConfig.iCapturedCount == g_SnapshotConfig.iNumFrames ? 0 : 1;
}

int SnapshotRunScene(void)
{
  if (strcmp(g_SnapshotConfig.szSceneName, "menu-main") == 0)
    return SnapshotRenderMenuMain();

  fprintf(stderr, "ERROR: unknown snapshot scene '%s'\n", g_SnapshotConfig.szSceneName);
  return 1;
}
