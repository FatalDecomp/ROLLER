#include "snapshot_scenes.h"
#include "snapshot.h"
#include "frontend.h"
#include <stdio.h>
#include <string.h>

static int SnapshotSceneCapturedAll(void)
{
  return g_SnapshotConfig.iCapturedCount == g_SnapshotConfig.iNumFrames ? 0 : 1;
}

static int SnapshotRenderMenuMain(void)
{
  snapshot_render_menu_main();
  return SnapshotSceneCapturedAll();
}

static int SnapshotRenderMenuSelectCar(void)
{
  snapshot_render_menu_select_car();
  return SnapshotSceneCapturedAll();
}

static int SnapshotRenderMenuSelectTrack(void)
{
  snapshot_render_menu_select_track();
  return SnapshotSceneCapturedAll();
}

static int SnapshotRenderMenuSelectType(void)
{
  snapshot_render_menu_select_type();
  return SnapshotSceneCapturedAll();
}

static int SnapshotRenderMenuSelectDisk(void)
{
  snapshot_render_menu_select_disk();
  return SnapshotSceneCapturedAll();
}

int SnapshotRunScene(void)
{
  if (strcmp(g_SnapshotConfig.szSceneName, "menu-main") == 0)
    return SnapshotRenderMenuMain();
  if (strcmp(g_SnapshotConfig.szSceneName, "menu-select-car") == 0)
    return SnapshotRenderMenuSelectCar();
  if (strcmp(g_SnapshotConfig.szSceneName, "menu-select-track") == 0)
    return SnapshotRenderMenuSelectTrack();
  if (strcmp(g_SnapshotConfig.szSceneName, "menu-select-type") == 0)
    return SnapshotRenderMenuSelectType();
  if (strcmp(g_SnapshotConfig.szSceneName, "menu-select-disk") == 0)
    return SnapshotRenderMenuSelectDisk();

  fprintf(stderr, "ERROR: unknown snapshot scene '%s'\n", g_SnapshotConfig.szSceneName);
  return 1;
}
