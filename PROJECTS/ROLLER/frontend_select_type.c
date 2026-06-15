#include "frontend.h"
#include "graphics.h"
#include "3d.h"
#include "func2.h"
#include "sound.h"
#include "roller.h"
#include "car.h"
#include "moving.h"
#include "network.h"
#include "loadtrak.h"
#include "control.h"
#include "drawtrk3.h"
#include "cdx.h"
#include "polytex.h"
#include "comms.h"
#include "colision.h"
#include "rollercomms.h"
#include "menu_render.h"
#include "snapshot.h"
#include <fcntl.h>
#include <string.h>
#ifdef IS_WINDOWS
#include <io.h>
#define open _open
#define close _close
#else
#include <inttypes.h>
#include <unistd.h>
#define O_BINARY 0 //linux does not differentiate between text and binary
#endif
//-------------------------------------------------------------------------------------------------

static int iFrontendTypeMenuSelection = 0;
static int iFrontendTypeCurrentOption = 5;
static int iFrontendTypeCheatModesAvailable = 0;
static int iFrontendTypeExitFlag = 0;
static int iFrontendTypeExitFading = 0;
static int iFrontendTypeSkipColor = 0;
static int iFrontendTypeBlockIdx = 0;
static int iFrontendTypeBroadcastWaitAction = 0;
static int iFrontendTypeCloseNetworkPending = 0;
static int iFrontendTypeCloseNetworkStartFrame = 0;
static int iFrontendTypeOriginalGameType = 0;
static int iFrontendTypeOriginalCompetitors = 0;

enum {
  eTYPE_BROADCAST_WAIT_NONE = 0,
  eTYPE_BROADCAST_WAIT_EXIT,
  eTYPE_BROADCAST_WAIT_CLOSE_NETWORK
};

#define MENU_COLOR_RED 0xE7u

//-------------------------------------------------------------------------------------------------

static void frontend_type_select_run_snapshot(void);

//-------------------------------------------------------------------------------------------------

void snapshot_render_menu_select_type(void)
{
  snapshot_setup_frontend_menu_state(0);
  for (int i = 0; i < 5; ++i)
    SnapshotQueueRawKey(0x48); // Up arrow: real input path from Exit to Game Type.
  frontend_type_select_run_snapshot();
}

//-------------------------------------------------------------------------------------------------

static void frontend_type_select_black_palette(void)
{
  palette_brightness = 0;
  for (int i = 0; i < 256; ++i) {
    pal_addr[i].byR = 0;
    pal_addr[i].byB = 0;
    pal_addr[i].byG = 0;
  }
}

//-------------------------------------------------------------------------------------------------

static void frontend_type_select_finish_exit(void)
{
  iFrontendTypeExitFlag = -1;
  if (eFrontendCurrentState == eFRONTEND_STATE_TYPE_SELECT) {
    MenuRenderer *mr = GetMenuRenderer();
    menu_render_begin_fade(mr, 0, 32);
    iFrontendTypeExitFading = 1;
  }
}

//-------------------------------------------------------------------------------------------------

static void frontend_type_select_begin_broadcast_wait(int iMode, int iAction)
{
  iFrontendTypeBroadcastWaitAction = iAction;
  network_broadcast_wait_start(iMode, 1);
}

//-------------------------------------------------------------------------------------------------

static void frontend_type_select_finish_broadcast_wait(void)
{
  int iAction = iFrontendTypeBroadcastWaitAction;
  iFrontendTypeBroadcastWaitAction = eTYPE_BROADCAST_WAIT_NONE;

  switch (iAction) {
    case eTYPE_BROADCAST_WAIT_EXIT:
      frontend_type_select_finish_exit();
      break;
    case eTYPE_BROADCAST_WAIT_CLOSE_NETWORK:
      iFrontendTypeCloseNetworkStartFrame = frames;
      iFrontendTypeCloseNetworkPending = -1;
      break;
    default:
      break;
  }
}

//-------------------------------------------------------------------------------------------------

static int frontend_type_select_update_broadcast_wait(void)
{
  if (iFrontendTypeCloseNetworkPending) {
    if ((uint16)(frames - iFrontendTypeCloseNetworkStartFrame) < 3)
      return -1;

    iFrontendTypeCloseNetworkPending = 0;
    iFrontendTypeCloseNetworkStartFrame = 0;
    close_network();
    network_champ_on = 0;
    return -1;
  }

  if (!network_broadcast_wait_active())
    return 0;

  if (network_broadcast_wait_update())
    frontend_type_select_finish_broadcast_wait();

  return -1;
}

//-------------------------------------------------------------------------------------------------

static int frontend_type_select_championship_unavailable(void)
{
  return stock_track_demo_only() &&
         iFrontendTypeMenuSelection == 1 &&
         game_type == 1;
}

//-------------------------------------------------------------------------------------------------

static void frontend_type_select_restore_invalid_championship(void)
{
  if (!frontend_type_select_championship_unavailable())
    return;

  game_type = iFrontendTypeOriginalGameType == 1 ? 0 :
                                                  iFrontendTypeOriginalGameType;
  competitors = iFrontendTypeOriginalCompetitors;
  if (game_type == 2)
    competitors = 1;
}

//-------------------------------------------------------------------------------------------------

static void frontend_type_select_request_exit(void)
{
  if (iFrontendTypeSkipColor) {
    frontend_type_select_finish_exit();
    return;
  }

  network_champ_on = 0;
  if (stock_track_demo_only() && game_type == 1)
    game_type = 0;
  int iTrackUpperLimit = 8 * iFrontendTypeBlockIdx + 8;
  int iTrackLowerLimit = 8 * iFrontendTypeBlockIdx + 1;
  if (game_type) {
    if ((unsigned int)game_type <= 1) {
      Race = 0;
      memset(championship_points, 0, sizeof(championship_points));
      memset(team_points, 0, sizeof(team_points));
      memset(total_kills, 0, sizeof(total_kills));
      memset(total_fasts, 0, sizeof(total_fasts));
      memset(total_wins, 0, sizeof(total_wins));
      memset(team_kills, 0, sizeof(team_kills));
      memset(team_fasts, 0, sizeof(team_fasts));
      memset(team_wins, 0, sizeof(team_wins));
      TrackLoad = 8 * iFrontendTypeBlockIdx + 1;
    } else if (game_type == 2) {
      NoOfLaps = 5;
      competitors = 1;
      if (TrackLoad != TRACK_LOAD_COMMUNITY &&
          (iTrackLowerLimit > TrackLoad || iTrackUpperLimit < TrackLoad))
        TrackLoad =
            8 * iFrontendTypeBlockIdx + (((uint8)TrackLoad - 1) & 7) + 1;
    }
  } else if (TrackLoad != TRACK_LOAD_COMMUNITY &&
             (iTrackLowerLimit > TrackLoad || iTrackUpperLimit < TrackLoad)) {
    TrackLoad = 8 * iFrontendTypeBlockIdx + (((uint8)TrackLoad - 1) & 7) + 1;
  }
  frontend_type_select_begin_broadcast_wait(-1, eTYPE_BROADCAST_WAIT_EXIT);
}

//-------------------------------------------------------------------------------------------------

static void frontend_type_select_apply_type_switch(void)
{
  if (!switch_types)
    return;

  game_type = switch_types - 1;
  if (switch_types == 1 && competitors == 1)
    competitors = 16;
  switch_types = 0;
  if (game_type == 1) {
    if (TrackLoad == TRACK_LOAD_COMMUNITY)
      TrackLoad = 1;
    Race = ((uint8)TrackLoad - 1) & 7;
  } else {
    network_champ_on = 0;
  }
}

//-------------------------------------------------------------------------------------------------

static void frontend_type_select_apply_same_car_switch(void)
{
  if (switch_same > 0) {
    for (int i = 0; i < players; ++i)
      Players_Cars[i] = switch_same - 666;
    cheat_mode |= CHEAT_MODE_CLONES;
  } else if (switch_same < 0) {
    switch_same = 0;
    for (int i = 0; i < players; ++i)
      Players_Cars[i] = -1;
    cheat_mode &= ~CHEAT_MODE_CLONES;
  }
}

//-------------------------------------------------------------------------------------------------

static void frontend_type_select_draw(void)
{
  char *pszTextBuffer = NULL;
  char *szText;
  char byGameModeColor1;
  char byGameModeColor2;
  char byGameModeColor3;
  char byDifficultyColor1;
  char byDifficultyColor2;
  char byDifficultyColor3;
  char byDifficultyColor4;
  char byDifficultyColor5;
  char byDifficultyColor6;
  char byCompetitorColor1;
  char byCompetitorColor2;
  char byCompetitorColor3;
  char byDamageColor1;
  char byDamageColor2;
  char byDamageColor3;
  char byTextureColor1;
  char byTextureColor2;
  char byFinalTextColor = 0;
  int iTextYPosition = 0;
  char byCompetitorMenuColor;
  char byTextColor;
  char byDamageMenuColor;
  char byTextureMenuColor;
  char byDifficultyMenuColor;
  int iNetworkDisplayY;
  int iY;
  int iNetworkPlayerCount;
  MenuRenderer *mr = GetMenuRenderer();

  menu_render_begin_frame(mr);
  if (!front_fade) {
    front_fade = -1;
    menu_render_begin_fade(mr, 1, 32);
  }
  menu_render_background(mr, 0);
  menu_render_sprite(mr, 1, 4, head_x, head_y, 0, pal_addr);
  menu_render_sprite(mr, 6, 0, 36, 2, 0, pal_addr);
  menu_render_sprite(mr, 5, player_type, -4, 247, 0, pal_addr);
  menu_render_sprite(mr, 5, game_type + 5, 135, 247, 0, pal_addr);
  menu_render_sprite(mr, 4, 4, 76, 257, -1, pal_addr);
  if (cup_won && !iFrontendTypeSkipColor)
    menu_render_sprite(mr, 1, 8, 200, 56, 0, pal_addr);

  if (iFrontendTypeCurrentOption >= 5) {
    menu_render_sprite(mr, 6, 4, 62, 336, -1, pal_addr);
  } else {
    menu_render_sprite(mr, 6, 2, 62, 336, -1, pal_addr);
    menu_render_text(mr, 2, "~", font2_ascii, font2_offsets,
                     sel_posns[iFrontendTypeCurrentOption].x,
                     sel_posns[iFrontendTypeCurrentOption].y, 0x8Fu, 0,
                     pal_addr);
  }

  if (iFrontendTypeSkipColor) {
    menu_render_text(mr, 2, &language_buffer[3136], font2_ascii,
                     font2_offsets, sel_posns[0].x + 132,
                     sel_posns[0].y + 7, 0x8Fu, 2u, pal_addr);
  } else {
    menu_render_text(mr, 2, &language_buffer[384], font2_ascii,
                     font2_offsets, sel_posns[0].x + 132,
                     sel_posns[0].y + 7, 0x8Fu, 2u, pal_addr);
    menu_render_text(mr, 2, &language_buffer[3200], font2_ascii,
                     font2_offsets, sel_posns[1].x + 132,
                     sel_posns[1].y + 7, 0x8Fu, 2u, pal_addr);
    menu_render_text(mr, 2, &language_buffer[3264], font2_ascii,
                     font2_offsets, sel_posns[2].x + 132,
                     sel_posns[2].y + 7, 0x8Fu, 2u, pal_addr);
    menu_render_text(mr, 2, &language_buffer[3328], font2_ascii,
                     font2_offsets, sel_posns[3].x + 132,
                     sel_posns[3].y + 7, 0x8Fu, 2u, pal_addr);
    if (iFrontendTypeCheatModesAvailable)
      menu_render_text(mr, 2, &language_buffer[4288], font2_ascii,
                       font2_offsets, sel_posns[4].x + 132,
                       sel_posns[4].y + 7, 0x8Fu, 2u, pal_addr);
  }

  menu_render_sprite(mr, 14, iFrontendTypeBlockIdx, 500, 300, 0, pal_addr);
  if (iFrontendTypeSkipColor) {
    menu_render_scaled_text(mr, 15, &language_buffer[3392], font1_ascii,
                            font1_offsets, 400, 75, 143, 1u, 200, 640,
                            pal_addr);
    menu_render_scaled_text(mr, 15, &language_buffer[1280], font1_ascii,
                            font1_offsets, 400, 100, 143, 2u, 200, 640,
                            pal_addr);
    if ((cheat_mode & 2) != 0)
      menu_render_scaled_text(mr, 15, &language_buffer[2048], font1_ascii,
                              font1_offsets, 405, 100, 143, 0, 200, 640,
                              pal_addr);
    else
      menu_render_scaled_text(mr, 15, &language_buffer[64 * level + 1472],
                              font1_ascii, font1_offsets, 405, 100, 143, 0,
                              200, 640, pal_addr);
    menu_render_scaled_text(mr, 15, &language_buffer[1344], font1_ascii,
                            font1_offsets, 400, 118, 143, 2u, 200, 640,
                            pal_addr);
    if ((cheat_mode & 2) != 0)
      menu_render_scaled_text(mr, 15, &language_buffer[2048], font1_ascii,
                              font1_offsets, 405, 118, 143, 0, 200, 640,
                              pal_addr);
    else
      menu_render_scaled_text(mr, 15,
                              &language_buffer[64 * damage_level + 1856],
                              font1_ascii, font1_offsets, 405, 118, 143, 0,
                              200, 640, pal_addr);
    menu_render_scaled_text(mr, 15, &language_buffer[1024], font1_ascii,
                            font1_offsets, 400, 136, 143, 2u, 200, 640,
                            pal_addr);
    if ((unsigned int)competitors < 8) {
      if (competitors == 2)
        menu_render_scaled_text(mr, 15, &language_buffer[1088], font1_ascii,
                                font1_offsets, 405, 136, 143, 0, 200, 640,
                                pal_addr);
    } else if ((unsigned int)competitors <= 8) {
      menu_render_scaled_text(mr, 15, &language_buffer[1152], font1_ascii,
                              font1_offsets, 405, 136, 143, 0, 200, 640,
                              pal_addr);
    } else if (competitors == 16) {
      menu_render_scaled_text(mr, 15, &language_buffer[1216], font1_ascii,
                              font1_offsets, 405, 136, 143, 0, 200, 640,
                              pal_addr);
    }

    if (network_on) {
      menu_render_scaled_text(mr, 15, &language_buffer[4672], font1_ascii,
                              font1_offsets, 400, 154, 143, 1u, 200, 640,
                              pal_addr);
      iNetworkPlayerCount = 0;
      szText = player_names[0];
      iY = 28;
      iNetworkDisplayY = 172;
      while (iNetworkPlayerCount < players) {
        if (iNetworkPlayerCount >= 8)
          menu_render_scaled_text(mr, 15, szText, font1_ascii,
                                  font1_offsets, 405, iY, 143, 0, 200, 640,
                                  pal_addr);
        else
          menu_render_scaled_text(mr, 15, szText, font1_ascii,
                                  font1_offsets, 400, iNetworkDisplayY, 143,
                                  2u, 200, 640, pal_addr);
        szText += 9;
        iY += 18;
        iNetworkDisplayY += 18;
        ++iNetworkPlayerCount;
      }
    }
  }

  switch (iFrontendTypeCurrentOption) {
    case 0:
      if (!iFrontendTypeSkipColor) {
        menu_render_scaled_text(mr, 15, &language_buffer[384], font1_ascii,
                                font1_offsets, 400, 75, 143, 1u, 200, 640,
                                pal_addr);
        if (iFrontendTypeMenuSelection == 1) {
          menu_render_scaled_text(mr, 15, &language_buffer[3584],
                                  font1_ascii, font1_offsets, 400, 93, 143,
                                  1u, 200, 640, pal_addr);
          byTextColor = -85;
        } else {
          byTextColor = -87;
        }
        byGameModeColor1 = game_type ? -113 : byTextColor;
        menu_render_scaled_text(mr, 15, &language_buffer[3648],
                                font1_ascii, font1_offsets, 400, 135,
                                byGameModeColor1, 1u, 200, 640, pal_addr);
        byGameModeColor2 = game_type == 1 ? byTextColor : -113;
        menu_render_scaled_text(mr, 15, &language_buffer[3520],
                                font1_ascii, font1_offsets, 400, 153,
                                byGameModeColor2, 1u, 200, 640, pal_addr);
        byGameModeColor3 = game_type == 2 ? byTextColor : -113;
        byFinalTextColor = byGameModeColor3;
        iTextYPosition = 171;
        pszTextBuffer = &language_buffer[3712];
        if (frontend_type_select_championship_unavailable()) {
          menu_render_scaled_text(mr, 15, "NOT AVAILABLE IN THIS VERSION",
                                  font1_ascii, font1_offsets, 400, 225,
                                  MENU_COLOR_RED, 1u, 200, 640, pal_addr);
        }
      } else if (iFrontendTypeMenuSelection == 6) {
        menu_render_scaled_text(mr, 15, &language_buffer[3456], font1_ascii,
                                font1_offsets, 400, 320, 231, 1u, 200, 640,
                                pal_addr);
        byFinalTextColor = -25;
        iTextYPosition = 338;
        pszTextBuffer = &language_buffer[3520];
      }
      break;
    case 1:
      menu_render_scaled_text(mr, 15, &language_buffer[3776], font1_ascii,
                              font1_offsets, 400, 75, 143, 1u, 200, 640,
                              pal_addr);
      if (iFrontendTypeMenuSelection == 2) {
        menu_render_scaled_text(mr, 15, &language_buffer[3840], font1_ascii,
                                font1_offsets, 400, 93, 143, 1u, 200, 640,
                                pal_addr);
        byDifficultyMenuColor = -85;
      } else {
        byDifficultyMenuColor = -87;
      }
      if ((cheat_mode & 2) == 0) {
        byDifficultyColor1 = level == 5 ? byDifficultyMenuColor : -113;
        menu_render_scaled_text(mr, 15, &language_buffer[1792],
                                font1_ascii, font1_offsets, 400, 135,
                                byDifficultyColor1, 1u, 200, 640, pal_addr);
        byDifficultyColor2 = level == 4 ? byDifficultyMenuColor : -113;
        menu_render_scaled_text(mr, 15, &language_buffer[1728],
                                font1_ascii, font1_offsets, 400, 153,
                                byDifficultyColor2, 1u, 200, 640, pal_addr);
        byDifficultyColor3 = level == 3 ? byDifficultyMenuColor : -113;
        menu_render_scaled_text(mr, 15, &language_buffer[1664],
                                font1_ascii, font1_offsets, 400, 171,
                                byDifficultyColor3, 1u, 200, 640, pal_addr);
        byDifficultyColor4 = level == 2 ? byDifficultyMenuColor : -113;
        menu_render_scaled_text(mr, 15, &language_buffer[1600],
                                font1_ascii, font1_offsets, 400, 189,
                                byDifficultyColor4, 1u, 200, 640, pal_addr);
        byDifficultyColor5 = level == 1 ? byDifficultyMenuColor : -113;
        menu_render_scaled_text(mr, 15, &language_buffer[1536],
                                font1_ascii, font1_offsets, 400, 207,
                                byDifficultyColor5, 1u, 200, 640, pal_addr);
        byDifficultyColor6 = level ? -113 : byDifficultyMenuColor;
        byFinalTextColor = byDifficultyColor6;
        iTextYPosition = 225;
        pszTextBuffer = &language_buffer[1472];
      } else {
        menu_render_scaled_text(mr, 15, &language_buffer[2048],
                                font1_ascii, font1_offsets, 400, 135,
                                byDifficultyMenuColor, 1u, 200, 640,
                                pal_addr);
      }
      break;
    case 2:
      menu_render_scaled_text(mr, 15, &language_buffer[3904], font1_ascii,
                              font1_offsets, 400, 75, 143, 1u, 200, 640,
                              pal_addr);
      if (iFrontendTypeMenuSelection == 3) {
        menu_render_scaled_text(mr, 15, &language_buffer[3008], font1_ascii,
                                font1_offsets, 400, 93, 143, 1u, 200, 640,
                                pal_addr);
        byDamageMenuColor = -85;
      } else {
        byDamageMenuColor = -87;
      }
      if (competitors != 1) {
        byCompetitorColor1 = competitors == 2 ? byDamageMenuColor : -113;
        menu_render_scaled_text(mr, 15, &language_buffer[1088],
                                font1_ascii, font1_offsets, 400, 135,
                                byCompetitorColor1, 1u, 200, 640, pal_addr);
        byCompetitorColor2 = competitors == 8 ? byDamageMenuColor : -113;
        menu_render_scaled_text(mr, 15, &language_buffer[1152],
                                font1_ascii, font1_offsets, 400, 153,
                                byCompetitorColor2, 1u, 200, 640, pal_addr);
        byCompetitorColor3 = competitors == 16 ? byDamageMenuColor : -113;
        byFinalTextColor = byCompetitorColor3;
        iTextYPosition = 171;
        pszTextBuffer = &language_buffer[1216];
      } else {
        menu_render_scaled_text(mr, 15, &language_buffer[3968],
                                font1_ascii, font1_offsets, 400, 135,
                                byDamageMenuColor, 1u, 200, 640, pal_addr);
      }
      break;
    case 3:
      menu_render_scaled_text(mr, 15, &language_buffer[4032], font1_ascii,
                              font1_offsets, 400, 75, 143, 1u, 200, 640,
                              pal_addr);
      if (iFrontendTypeMenuSelection == 4) {
        menu_render_scaled_text(mr, 15, &language_buffer[3840], font1_ascii,
                                font1_offsets, 400, 93, 143, 1u, 200, 640,
                                pal_addr);
        byTextureMenuColor = -85;
      } else {
        byTextureMenuColor = -87;
      }
      if ((cheat_mode & 2) == 0) {
        byDamageColor1 = damage_level ? -113 : byTextureMenuColor;
        menu_render_scaled_text(mr, 15, &language_buffer[1856],
                                font1_ascii, font1_offsets, 400, 135,
                                byDamageColor1, 1u, 200, 640, pal_addr);
        byDamageColor2 = damage_level == 1 ? byTextureMenuColor : -113;
        menu_render_scaled_text(mr, 15, &language_buffer[1920],
                                font1_ascii, font1_offsets, 400, 153,
                                byDamageColor2, 1u, 200, 640, pal_addr);
        byDamageColor3 = damage_level == 2 ? byTextureMenuColor : -113;
        byFinalTextColor = byDamageColor3;
        iTextYPosition = 171;
        pszTextBuffer = &language_buffer[1984];
      } else {
        menu_render_scaled_text(mr, 15, &language_buffer[2048],
                                font1_ascii, font1_offsets, 400, 135,
                                byTextureMenuColor, 1u, 200, 640, pal_addr);
      }
      break;
    case 4:
      menu_render_scaled_text(mr, 15, &language_buffer[4288], font1_ascii,
                              font1_offsets, 400, 75, 143, 1u, 200, 640,
                              pal_addr);
      if (iFrontendTypeMenuSelection == 5) {
        menu_render_scaled_text(mr, 15, &language_buffer[4480], font1_ascii,
                                font1_offsets, 400, 93, 143, 1u, 200, 640,
                                pal_addr);
        byCompetitorMenuColor = -85;
      } else {
        byCompetitorMenuColor = -87;
      }
      byTextureColor1 =
          (textures_off & TEX_OFF_ADVANCED_CARS) != 0 ? -113
                                                      : byCompetitorMenuColor;
      menu_render_scaled_text(mr, 15, &language_buffer[4352], font1_ascii,
                              font1_offsets, 400, 135, byTextureColor1, 1u,
                              200, 640, pal_addr);
      byTextureColor2 =
          (textures_off & TEX_OFF_ADVANCED_CARS) != 0 ? byCompetitorMenuColor
                                                      : -113;
      byFinalTextColor = byTextureColor2;
      iTextYPosition = 153;
      pszTextBuffer = &language_buffer[4416];
      break;
    default:
      break;
  }

  if (pszTextBuffer)
    menu_render_scaled_text(mr, 15, pszTextBuffer, font1_ascii,
                            font1_offsets, 400, iTextYPosition,
                            byFinalTextColor, 1u, 200, 640, pal_addr);

  show_received_mesage();
  menu_render_end_frame(mr);
}

//-------------------------------------------------------------------------------------------------

static void frontend_type_select_handle_enter(void)
{
  sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);

  if (iFrontendTypeMenuSelection) {
    if (frontend_type_select_championship_unavailable())
      return;

    if (game_type == 1) {
      Race = 0;
      TrackLoad = 8 * iFrontendTypeBlockIdx + 1;
    }
    iFrontendTypeMenuSelection = 0;
    frontend_type_select_begin_broadcast_wait(-1, eTYPE_BROADCAST_WAIT_NONE);
    return;
  }

  switch (iFrontendTypeCurrentOption) {
    case 0:
      iFrontendTypeOriginalGameType = game_type;
      iFrontendTypeOriginalCompetitors = competitors;
      iFrontendTypeMenuSelection = iFrontendTypeSkipColor ? 6 : 1;
      break;
    case 1:
      iFrontendTypeMenuSelection = 2;
      break;
    case 2:
      iFrontendTypeMenuSelection = 3;
      break;
    case 3:
      iFrontendTypeMenuSelection = 4;
      break;
    case 4:
      iFrontendTypeMenuSelection = 5;
      break;
    case 5:
      frontend_type_select_request_exit();
      break;
    default:
      break;
  }
}

//-------------------------------------------------------------------------------------------------

static void frontend_type_select_handle_extended_key(uint8 byExtendedKey)
{
  switch (iFrontendTypeMenuSelection) {
    case 0:
      if (byExtendedKey == 0x48u) {
        if (iFrontendTypeSkipColor) {
          iFrontendTypeCurrentOption = 0;
        } else {
          --iFrontendTypeCurrentOption;
          if (!iFrontendTypeCheatModesAvailable &&
              iFrontendTypeCurrentOption == 4)
            iFrontendTypeCurrentOption = 3;
          if (iFrontendTypeCurrentOption < 0)
            iFrontendTypeCurrentOption = 0;
        }
      } else if (byExtendedKey == 0x50u) {
        if (iFrontendTypeSkipColor) {
          iFrontendTypeCurrentOption = 5;
        } else {
          if (iFrontendTypeCheatModesAvailable) {
            ++iFrontendTypeCurrentOption;
          } else if (++iFrontendTypeCurrentOption > 3) {
            iFrontendTypeCurrentOption = 5;
          }
          if (iFrontendTypeCurrentOption > 5)
            iFrontendTypeCurrentOption = 5;
        }
      }
      break;
    case 1:
      if (byExtendedKey == 0x48u) {
        if (--game_type < 0)
          game_type = 0;
        if (competitors == 1)
          competitors = 16;
      } else if (byExtendedKey == 0x50u) {
        if (++game_type < 2) {
          if (competitors == 1)
            competitors = 16;
        } else {
          game_type = 2;
          competitors = 1;
        }
      }
      break;
    case 2:
      if (byExtendedKey == 0x48u) {
        if (levels[++level] <= 0.0)
          --level;
      } else if (byExtendedKey == 0x50u && --level < 0) {
        level = 0;
      }
      break;
    case 3:
      if (byExtendedKey == 0x48u) {
        if (game_type < 2 && (unsigned int)competitors >= 8) {
          if ((unsigned int)competitors <= 8)
            competitors = 2;
          else if (competitors == 16)
            competitors = 8;
        }
      } else if (byExtendedKey == 0x50u && game_type < 2 &&
                 (unsigned int)competitors >= 2) {
        if ((unsigned int)competitors <= 2)
          competitors = 8;
        else if (competitors == 8)
          competitors = 16;
      }
      break;
    case 4:
      if (byExtendedKey == 0x48u) {
        if (--damage_level < 0)
          damage_level = 0;
      } else if (byExtendedKey == 0x50u && ++damage_level > 2) {
        damage_level = 2;
      }
      break;
    case 5:
      if (byExtendedKey == 0x48u)
        textures_off &= ~TEX_OFF_ADVANCED_CARS;
      else if (byExtendedKey == 0x50u)
        textures_off |= TEX_OFF_ADVANCED_CARS;
      break;
    default:
      break;
  }
}

//-------------------------------------------------------------------------------------------------

static void frontend_type_select_handle_space(void)
{
  int iCupIncrement;

  if (iFrontendTypeSkipColor)
    return;

  iCupIncrement = ++iFrontendTypeBlockIdx;
  if ((cup_won & 1) == 0 && iCupIncrement == 1)
    iFrontendTypeBlockIdx = 2;
  if ((cup_won & 2) == 0 && iFrontendTypeBlockIdx == 2)
    iFrontendTypeBlockIdx = 3;
  if (iFrontendTypeBlockIdx > 2)
    iFrontendTypeBlockIdx = 0;
  TrackLoad = 8 * iFrontendTypeBlockIdx + (((uint8)TrackLoad - 1) & 7) + 1;
  frontend_type_select_begin_broadcast_wait(-1, eTYPE_BROADCAST_WAIT_NONE);
}

//-------------------------------------------------------------------------------------------------

static void frontend_type_select_handle_championship_reset(void)
{
  if (!iFrontendTypeSkipColor || iFrontendTypeMenuSelection != 6)
    return;

  game_type = 0;
  iFrontendTypeSkipColor = 0;
  iFrontendTypeMenuSelection = 0;
  if (!network_on)
    return;

  if (Race <= 0) {
    frontend_type_select_begin_broadcast_wait(-1, eTYPE_BROADCAST_WAIT_NONE);
  } else {
    frontend_type_select_begin_broadcast_wait(
        -666, eTYPE_BROADCAST_WAIT_CLOSE_NETWORK);
  }
}

//-------------------------------------------------------------------------------------------------

static void frontend_type_select_handle_input(void)
{
  while (fatkbhit()) {
    uint8 byInputKey = fatgetch();

    if (byInputKey < 0x1Bu) {
      if (byInputKey) {
        if (byInputKey == 13)
          frontend_type_select_handle_enter();
      } else {
        uint8 byExtendedKey = fatgetch();

        if (byExtendedKey >= 0x48u)
          frontend_type_select_handle_extended_key(byExtendedKey);
      }
    } else if (byInputKey <= 0x1Bu) {
      if (iFrontendTypeMenuSelection) {
        frontend_type_select_restore_invalid_championship();
        iFrontendTypeMenuSelection = 0;
      } else {
        frontend_type_select_request_exit();
      }
    } else if (byInputKey < 0x59u) {
      if (byInputKey == 32)
        frontend_type_select_handle_space();
    } else if (byInputKey == 0x59u || byInputKey == 121) {
      frontend_type_select_handle_championship_reset();
    }

    if (network_broadcast_wait_active() || iFrontendTypeCloseNetworkPending)
      return;

    if (iFrontendTypeExitFlag)
      return;
  }
}

//-------------------------------------------------------------------------------------------------

void frontend_type_select_enter(void)
{
  iFrontendTypeExitFlag = 0;
  iFrontendTypeExitFading = 0;
  iFrontendTypeBroadcastWaitAction = eTYPE_BROADCAST_WAIT_NONE;
  iFrontendTypeCloseNetworkPending = 0;
  iFrontendTypeCloseNetworkStartFrame = 0;
  if (stock_track_demo_only() && game_type == 1) {
    game_type = 0;
    Race = 0;
    network_champ_on = 0;
    if (competitors == 1)
      competitors = 16;
  }
  if (game_type == 1 && Race > 0)
    iFrontendTypeSkipColor = -1;
  else
    iFrontendTypeSkipColor = 0;
  if ((cheat_mode & TEX_OFF_SHADOWS) != 0 ||
      (textures_off & TEX_OFF_CAR_SET_AVAILABLE) != 0)
    iFrontendTypeCheatModesAvailable = -1;
  else
    iFrontendTypeCheatModesAvailable = 0;
  if (TrackLoad == TRACK_LOAD_COMMUNITY)
    iFrontendTypeBlockIdx = 0;
  else
    iFrontendTypeBlockIdx = (TrackLoad - 1) / 8;
  front_vga[14] = (tBlockHeader *)load_picture("cupicons.bm");
  iFrontendTypeMenuSelection = 0;
  frontend_type_select_black_palette();
  front_fade = 0;
  memcpy(pal_addr, palette, 256 * sizeof(tColor));
  palette_brightness = 32;
  {
    MenuRenderer *mr = GetMenuRenderer();

    if (mr)
      menu_render_load_blocks(mr, 14, front_vga[14], palette);
  }
  frames = 0;
  iFrontendTypeCurrentOption = 5;
}

//-------------------------------------------------------------------------------------------------

void frontend_type_select_update(void)
{
  frontend_type_select_apply_type_switch();
  frontend_type_select_draw();
  if (SnapshotShouldStop())
    return;

  if (iFrontendTypeExitFading) {
    MenuRenderer *mr = GetMenuRenderer();
    if (!menu_render_fade_active(mr)) {
      iFrontendTypeExitFading = 0;
      eFrontendNextState = eFRONTEND_STATE_MAIN_MENU;
    }
    return;
  }

  if (frontend_type_select_update_broadcast_wait())
    return;

  frontend_type_select_apply_same_car_switch();
  frontend_type_select_handle_input();
}

void frontend_type_select_exit(void)
{
  iFrontendTypeExitFading = 0;
  if (!SnapshotShouldStop())
    frontend_type_select_black_palette();
  fre((void **)&front_vga[14]);
  front_fade = 0;

  if (eFrontendCurrentState == eFRONTEND_STATE_TYPE_SELECT)
    frontend_menu_resume_from_child();
}

//-------------------------------------------------------------------------------------------------

static void frontend_type_select_run_snapshot(void)
{
  frontend_type_select_enter();
  while (!iFrontendTypeExitFlag && !SnapshotShouldStop()) {
    frontend_type_select_update();
    if (!iFrontendTypeExitFlag && !SnapshotShouldStop())
      UpdateSDLWindow();
  }
  if (!SnapshotShouldStop())
    frontend_type_select_exit();
}

//-------------------------------------------------------------------------------------------------
