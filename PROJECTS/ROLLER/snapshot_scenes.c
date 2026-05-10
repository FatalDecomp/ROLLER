#include "snapshot_scenes.h"
#include "snapshot.h"
#include "3d.h"
#include "frontend.h"
#include <stdio.h>
#include <string.h>

static int SnapshotSceneCapturedAll(void)
{
  return g_SnapshotConfig.iCapturedCount == g_SnapshotConfig.iNumFrames ? 0 : 1;
}


int SnapshotRunScene(void)
{
  if (strcmp(g_SnapshotConfig.szSceneName, "menu-main") == 0) {
    snapshot_render_menu_main();
    return SnapshotSceneCapturedAll();
  }
  if (strcmp(g_SnapshotConfig.szSceneName, "menu-select-car") == 0) {
    snapshot_render_menu_select_car();
    return SnapshotSceneCapturedAll();
  }
  if (strcmp(g_SnapshotConfig.szSceneName, "menu-select-track") == 0) {
    snapshot_render_menu_select_track();
    return SnapshotSceneCapturedAll();
  }
  if (strcmp(g_SnapshotConfig.szSceneName, "menu-select-type") == 0) {
    snapshot_render_menu_select_type();
    return SnapshotSceneCapturedAll();
  }
  if (strcmp(g_SnapshotConfig.szSceneName, "menu-select-players") == 0) {
    snapshot_render_menu_select_players();
    return SnapshotSceneCapturedAll();
  }
  if (strcmp(g_SnapshotConfig.szSceneName, "menu-select-disk") == 0) {
    snapshot_render_menu_select_disk();
    return SnapshotSceneCapturedAll();
  }
  if (strcmp(g_SnapshotConfig.szSceneName, "menu-configure") == 0) {
    snapshot_render_menu_configure();
    return SnapshotSceneCapturedAll();
  }
  if (strcmp(g_SnapshotConfig.szSceneName, "winner-race") == 0) {
    snapshot_render_winner_race();
    return SnapshotSceneCapturedAll();
  }
  if (strcmp(g_SnapshotConfig.szSceneName, "winner-championship") == 0) {
    snapshot_render_winner_championship();
    return SnapshotSceneCapturedAll();
  }
  if (strcmp(g_SnapshotConfig.szSceneName, "championship-over") == 0) {
    snapshot_render_championship_over();
    return SnapshotSceneCapturedAll();
  }

  fprintf(stderr, "ERROR: unknown snapshot scene '%s'\n", g_SnapshotConfig.szSceneName);
  return 1;
}
