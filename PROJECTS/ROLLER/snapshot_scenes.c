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

static int SnapshotRenderSettled(void (*render_scene)(void))
{
  int iMaxPresents = g_SnapshotConfig.iMaxFrame;
  if (iMaxPresents < 1)
    iMaxPresents = 1;

  for (int i = g_SnapshotConfig.iPresentFrame; i < iMaxPresents; ++i) {
    render_scene();
    if (g_SnapshotConfig.iCapturedCount >= g_SnapshotConfig.iNumFrames)
      break;
  }

  return SnapshotSceneCapturedAll();
}

static int SnapshotRenderMenuMain(void)
{
  return SnapshotRenderSettled(snapshot_render_menu_main);
}

static int SnapshotRenderMenuSelectCar(void)
{
  return SnapshotRenderSettled(snapshot_render_menu_select_car);
}

static int SnapshotRenderMenuSelectTrack(void)
{
  return SnapshotRenderSettled(snapshot_render_menu_select_track);
}

static int SnapshotRenderMenuSelectType(void)
{
  return SnapshotRenderSettled(snapshot_render_menu_select_type);
}

static int SnapshotRenderMenuSelectDisk(void)
{
  return SnapshotRenderSettled(snapshot_render_menu_select_disk);
}

static int SnapshotRenderWinnerRace(void)
{
  return SnapshotRenderSettled(snapshot_render_winner_race);
}

static int SnapshotRenderWinnerChampionship(void)
{
  return SnapshotRenderSettled(snapshot_render_winner_championship);
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
  if (strcmp(g_SnapshotConfig.szSceneName, "winner-race") == 0)
    return SnapshotRenderWinnerRace();
  if (strcmp(g_SnapshotConfig.szSceneName, "winner-championship") == 0)
    return SnapshotRenderWinnerChampionship();

  fprintf(stderr, "ERROR: unknown snapshot scene '%s'\n", g_SnapshotConfig.szSceneName);
  return 1;
}
