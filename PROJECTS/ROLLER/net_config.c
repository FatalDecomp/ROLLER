#include "net_config.h"
#include "net_config_internal.h"

#include "loadtrak.h"
#include "types.h"

#include <string.h>

_Static_assert(TRACK_LOAD_COMMUNITY == NET_SESSION_COMMUNITY_TRACK,
               "session and legacy community track indices agree");

#if defined(__has_include)
#if __has_include("build_info.h")
#include "build_info.h"
#else
#include "build_info_default.h"
#endif
#else
#include "build_info_default.h"
#endif

extern int TrackLoad;
extern uint32 textures_off;
extern int game_type;
extern int manual_control[16];
extern int competitors;
extern int damage_level;
extern int network_champ_on;
extern int level;
extern int cheat_mode;
extern int player_invul[16];
extern int random_seed;
void ROLLERsrand(unsigned int uiSeed);

static int32 NetSessionConfigBuildLevelFlags(void)
{
  int32 iFlags = level;
  if ((cheat_mode & CHEAT_MODE_DEATH_MODE) != 0)
    iFlags |= NET_SESSION_DEATH_FLAG;
  if (player_invul[0])
    iFlags |= NET_SESSION_INVUL_FLAG;
  if ((cheat_mode & CHEAT_MODE_KILLER_OPPONENTS) != 0)
    iFlags |= NET_SESSION_KILLER_FLAG;
  if ((cheat_mode & CHEAT_MODE_ICY_ROAD) != 0)
    iFlags |= NET_SESSION_ICY_FLAG;
  if ((cheat_mode & CHEAT_MODE_50HZ_TIMER) != 0)
    iFlags |= NET_SESSION_50HZ_FLAG;
  if ((cheat_mode & CHEAT_MODE_DOUBLE_TRACK) != 0)
    iFlags |= NET_SESSION_DOUBLE_TRACK_FLAG;
  if ((cheat_mode & CHEAT_MODE_100HZ_TIMER) != 0)
    iFlags |= NET_SESSION_100HZ_FLAG;
  if ((cheat_mode & CHEAT_MODE_CLONES) != 0)
    iFlags |= NET_SESSION_CLONES_FLAG;
  if ((cheat_mode & CHEAT_MODE_TINY_CARS) != 0)
    iFlags |= NET_SESSION_TINY_FLAG;
  return iFlags;
}

static int NetSessionConfigBuildTrack(tNetSessionConfig *pConfig)
{
  const char *szPath;
  if (TrackLoad == TRACK_LOAD_COMMUNITY) {
    if (g_iCommunityTrackSel < 0 ||
        g_iCommunityTrackSel >= g_iCommunityTrackCount)
      return 0;
    szPath = community_track_path();
    if (!szPath)
      return 0;
    strncpy(pConfig->szCommunityTrack,
            g_aszCommunityTracks[g_iCommunityTrackSel],
            sizeof(pConfig->szCommunityTrack) - 1);
    pConfig->szCommunityTrack[sizeof(pConfig->szCommunityTrack) - 1] = '\0';
    pConfig->uiTrackCRC = community_track_crc(szPath);
    pConfig->uiCommunityTrackCRC = pConfig->uiTrackCRC;
    return 1;
  }

  if (TrackLoad < 1 || TrackLoad >= TRACK_LOAD_COMMUNITY ||
      !names[TrackLoad] || !names[TrackLoad][0])
    return 0;
  pConfig->uiTrackCRC = community_track_crc(names[TrackLoad]);
  return 1;
}

void NetSessionConfigOptionsDefault(tNetSessionConfigOptions *pOptions)
{
  if (!pOptions)
    return;
  memset(pOptions, 0, sizeof(*pOptions));
  pOptions->bySnapshotInterval = NET_SESSION_DEFAULT_SNAPSHOT_INTERVAL;
  pOptions->byMaxPlayers = 8;
  pOptions->byPauseAllowed = 1;
  memcpy(&pOptions->uiRandomSeed, &random_seed,
         sizeof(pOptions->uiRandomSeed));
  strncpy(pOptions->szBuildHash, BUILD_GIT_HASH,
          sizeof(pOptions->szBuildHash) - 1);
}

int NetSessionConfigBuild(tNetSessionConfig *pConfig,
                          const tNetSessionConfigOptions *pOptions)
{
  tNetSessionConfig config;
  if (!pConfig || !pOptions)
    return 0;
  memset(&config, 0, sizeof(config));
  config.unProtocolVersion = NET_PROTOCOL_VERSION;
  config.bySnapshotInterval = pOptions->bySnapshotInterval;
  config.byMaxPlayers = pOptions->byMaxPlayers;
  config.byHostIsDedicated = pOptions->byHostIsDedicated;
  config.byPauseAllowed = pOptions->byPauseAllowed;
  config.iTrackLoad = TrackLoad;
  config.iGameType = game_type;
  config.iManualControl = manual_control[0];
  config.iLevelFlags = NetSessionConfigBuildLevelFlags();
  config.unTickRateHz = NetSessionConfigTickRateFromFlags(config.iLevelFlags);
  config.iCompetitors = competitors;
  config.iDamageLevel = damage_level;
  config.iTextureMode = (textures_off & TEX_OFF_ADVANCED_CARS) != 0;
  config.iNetworkChampOn = network_champ_on;
  config.uiRandomSeed = pOptions->uiRandomSeed;
  memcpy(config.szBuildHash, pOptions->szBuildHash,
         sizeof(config.szBuildHash));
  if (!NetSessionConfigBuildTrack(&config) ||
      !NetSessionConfigValidate(&config))
    return 0;
  *pConfig = config;
  return 1;
}

int NetSessionConfigApply(const tNetSessionConfig *pConfig)
{
  int iRandomSeed;
  int iCommunityIdx = -1;
  int iIdx;
  const int iManagedCheats = CHEAT_MODE_DEATH_MODE |
      CHEAT_MODE_KILLER_OPPONENTS | CHEAT_MODE_ICY_ROAD |
      CHEAT_MODE_50HZ_TIMER | CHEAT_MODE_DOUBLE_TRACK |
      CHEAT_MODE_100HZ_TIMER | CHEAT_MODE_CLONES | CHEAT_MODE_TINY_CARS;
  if (!NetSessionConfigValidate(pConfig))
    return 0;

  if (pConfig->iTrackLoad == TRACK_LOAD_COMMUNITY) {
    for (iIdx = 0; iIdx < g_iCommunityTrackCount; ++iIdx) {
      if (strcmp(g_aszCommunityTracks[iIdx],
                 pConfig->szCommunityTrack) == 0) {
        iCommunityIdx = iIdx;
        break;
      }
    }
  }

  TrackLoad = pConfig->iTrackLoad;
  game_type = pConfig->iGameType;
  manual_control[0] = pConfig->iManualControl;
  level = pConfig->iLevelFlags & NET_SESSION_LEVEL_MASK;
  competitors = pConfig->iCompetitors;
  damage_level = pConfig->iDamageLevel;
  network_champ_on = pConfig->iNetworkChampOn;

  cheat_mode &= ~iManagedCheats;
  if ((pConfig->iLevelFlags & NET_SESSION_DEATH_FLAG) != 0)
    cheat_mode |= CHEAT_MODE_DEATH_MODE;
  if ((pConfig->iLevelFlags & NET_SESSION_KILLER_FLAG) != 0)
    cheat_mode |= CHEAT_MODE_KILLER_OPPONENTS;
  if ((pConfig->iLevelFlags & NET_SESSION_ICY_FLAG) != 0)
    cheat_mode |= CHEAT_MODE_ICY_ROAD;
  if ((pConfig->iLevelFlags & NET_SESSION_50HZ_FLAG) != 0)
    cheat_mode |= CHEAT_MODE_50HZ_TIMER;
  if ((pConfig->iLevelFlags & NET_SESSION_DOUBLE_TRACK_FLAG) != 0)
    cheat_mode |= CHEAT_MODE_DOUBLE_TRACK;
  if ((pConfig->iLevelFlags & NET_SESSION_100HZ_FLAG) != 0)
    cheat_mode |= CHEAT_MODE_100HZ_TIMER;
  if ((pConfig->iLevelFlags & NET_SESSION_CLONES_FLAG) != 0)
    cheat_mode |= CHEAT_MODE_CLONES;
  if ((pConfig->iLevelFlags & NET_SESSION_TINY_FLAG) != 0)
    cheat_mode |= CHEAT_MODE_TINY_CARS;
  player_invul[0] =
      (pConfig->iLevelFlags & NET_SESSION_INVUL_FLAG) ? -1 : 0;

  if (pConfig->iTextureMode)
    textures_off |= TEX_OFF_ADVANCED_CARS;
  else
    textures_off &= ~TEX_OFF_ADVANCED_CARS;

  if (pConfig->iTrackLoad == TRACK_LOAD_COMMUNITY) {
    g_iCommunityTrackSel = iCommunityIdx;
    g_iCommunityTrackMissing = iCommunityIdx < 0 ? -1 : 0;
    g_uiCommunityTrackCRC = pConfig->uiCommunityTrackCRC;
  } else {
    g_iCommunityTrackMissing = 0;
    g_uiCommunityTrackCRC = 0;
  }

  memcpy(&iRandomSeed, &pConfig->uiRandomSeed, sizeof(iRandomSeed));
  random_seed = iRandomSeed;
  ROLLERsrand(pConfig->uiRandomSeed);
  return 1;
}
