#include "net_config.h"
#include "net_session.h"
#include "loadtrak.h"
#include "types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(iCondition) do { if (!(iCondition)) { fprintf(stderr, "%d: %s\n", __LINE__, #iCondition); exit(1); } } while (0)

int TrackLoad;
uint32 textures_off;
int game_type;
int manual_control[16];
int competitors;
int damage_level;
int network_champ_on;
int level;
int cheat_mode;
int player_invul[16];
int random_seed;
char *names[25];
char g_aszCommunityTracks[MAX_COMMUNITY_TRACKS][MAX_COMMUNITY_TRACK_FILENAME];
int g_iCommunityTrackCount;
int g_iCommunityTrackSel;
int g_iCommunityTrackTop;
int g_iCommunityTrackMissing;
uint32 g_uiCommunityTrackCRC;
int g_iTrackLoadGeneration;

static uint32 g_uiSeedApplied;

typedef struct
{
  uint64 ullState;
} tNetTestRandom;

static int NetTestRandomBytes(void *pContext, void *pData, int iLength)
{
  tNetTestRandom *pRandom = (tNetTestRandom *)pContext;
  uint8 *pBytes = (uint8 *)pData;
  int iByte;
  for (iByte = 0; iByte < iLength; ++iByte) {
    pRandom->ullState = pRandom->ullState * 6364136223846793005ull + 1;
    pBytes[iByte] = (uint8)(pRandom->ullState >> 32);
  }
  return 1;
}

static tNetAddress NetTestAddress(uint16 unPort)
{
  tNetAddress address;
  memset(&address, 0, sizeof(address));
  address.abAddress[0] = 127;
  address.abAddress[3] = 1;
  address.unPort = unPort;
  address.byFamily = NET_ADDR_IPV4;
  return address;
}

void ROLLERsrand(unsigned int uiSeed)
{
  g_uiSeedApplied = uiSeed;
}

const char *community_track_path(void)
{
  if (g_iCommunityTrackSel < 0 ||
      g_iCommunityTrackSel >= g_iCommunityTrackCount)
    return NULL;
  return g_aszCommunityTracks[g_iCommunityTrackSel];
}

uint32 community_track_crc(const char *szPath)
{
  uint32 uiHash = 2166136261u;
  if (!szPath || !szPath[0])
    return 0;
  while (*szPath) {
    uiHash ^= (uint8)*szPath++;
    uiHash *= 16777619u;
  }
  return uiHash;
}

typedef struct
{
  int iTrackLoad;
  uint32 uiTexturesOff;
  int iGameType;
  int iManualControl;
  int iCompetitors;
  int iDamageLevel;
  int iNetworkChampOn;
  int iLevel;
  int iCheatMode;
  int iPlayerInvul;
  int iRandomSeed;
} tNetTestGlobals;

static tNetTestGlobals NetTestCaptureGlobals(void)
{
  tNetTestGlobals globals;
  globals.iTrackLoad = TrackLoad;
  globals.uiTexturesOff = textures_off;
  globals.iGameType = game_type;
  globals.iManualControl = manual_control[0];
  globals.iCompetitors = competitors;
  globals.iDamageLevel = damage_level;
  globals.iNetworkChampOn = network_champ_on;
  globals.iLevel = level;
  globals.iCheatMode = cheat_mode;
  globals.iPlayerInvul = player_invul[0];
  globals.iRandomSeed = random_seed;
  return globals;
}

static void NetTestSetHostGlobals(void)
{
  static char szTrack[] = "TRACK7.TRK";
  memset(manual_control, 0, sizeof(manual_control));
  memset(player_invul, 0, sizeof(player_invul));
  memset(names, 0, sizeof(names));
  names[7] = szTrack;
  TrackLoad = 7;
  textures_off = TEX_OFF_ADVANCED_CARS | TEX_OFF_CLOUDS;
  game_type = 0;
  manual_control[0] = 2;
  competitors = 8;
  damage_level = 2;
  network_champ_on = 0;
  level = 4;
  cheat_mode = CHEAT_MODE_DEATH_MODE | CHEAT_MODE_KILLER_OPPONENTS |
      CHEAT_MODE_ICY_ROAD | CHEAT_MODE_50HZ_TIMER |
      CHEAT_MODE_100HZ_TIMER | CHEAT_MODE_DOUBLE_TRACK |
      CHEAT_MODE_TINY_CARS | CHEAT_MODE_WARP;
  player_invul[0] = -1;
  random_seed = (int)0x81234567u;
  g_iCommunityTrackCount = 0;
  g_iCommunityTrackSel = -1;
  g_iCommunityTrackMissing = 0;
  g_uiCommunityTrackCRC = 0;
}

static void NetTestBuildEncodeApply(void)
{
  tNetSessionConfigOptions options;
  tNetSessionConfig hostConfig, clientConfig;
  tNetTestGlobals expected, actual;
  uint8 abWire[NET_SESSION_CONFIG_WIRE_SIZE];

  NetTestSetHostGlobals();
  NetSessionConfigOptionsDefault(&options);
  options.byMaxPlayers = 8;
  memcpy(options.szBuildHash, "abc1234", 8);
  CHECK(NetSessionConfigBuild(&hostConfig, &options));
  CHECK(hostConfig.unTickRateHz == 100);
  CHECK(hostConfig.iTrackLoad == TrackLoad);
  CHECK(hostConfig.uiTrackCRC == community_track_crc(names[TrackLoad]));
  CHECK(hostConfig.iGameType == game_type);
  CHECK(NetSessionConfigEncode(&hostConfig, abWire, sizeof(abWire)) ==
        NET_SESSION_CONFIG_WIRE_SIZE);
  CHECK(abWire[0] == NET_PROTOCOL_VERSION && abWire[1] == 0);
  CHECK(abWire[2] == 100 && abWire[3] == 0);
  CHECK(abWire[8] == 7 && abWire[9] == 0);
  CHECK(abWire[40] == 0x67 && abWire[41] == 0x45 &&
        abWire[42] == 0x23 && abWire[43] == 0x81);
  CHECK(NetSessionConfigDecode(&clientConfig, abWire, sizeof(abWire)));
  CHECK(clientConfig.iTrackLoad == hostConfig.iTrackLoad);
  CHECK(clientConfig.uiTrackCRC == hostConfig.uiTrackCRC);
  CHECK(clientConfig.iGameType == hostConfig.iGameType);

  expected = NetTestCaptureGlobals();
  TrackLoad = 1;
  textures_off = TEX_OFF_CLOUDS;
  game_type = 2;
  manual_control[0] = 1;
  competitors = 1;
  damage_level = 0;
  network_champ_on = 123;
  level = 0;
  cheat_mode = CHEAT_MODE_WARP;
  player_invul[0] = 0;
  random_seed = 1;
  CHECK(NetSessionConfigApply(&clientConfig));
  actual = NetTestCaptureGlobals();
  CHECK(memcmp(&actual, &expected, sizeof(actual)) == 0);
  CHECK(g_uiSeedApplied == options.uiRandomSeed);
}

static void NetTestCommunityTrack(void)
{
  tNetSessionConfigOptions options;
  tNetSessionConfig config;
  uint32 uiCRC;
  NetTestSetHostGlobals();
  strcpy(g_aszCommunityTracks[0], "FAVORITE.TRK");
  strcpy(g_aszCommunityTracks[1], "OTHER.TRK");
  g_iCommunityTrackCount = 2;
  g_iCommunityTrackSel = 0;
  TrackLoad = TRACK_LOAD_COMMUNITY;
  NetSessionConfigOptionsDefault(&options);
  CHECK(NetSessionConfigBuild(&config, &options));
  uiCRC = community_track_crc("FAVORITE.TRK");
  CHECK(strcmp(config.szCommunityTrack, "FAVORITE.TRK") == 0);
  CHECK(config.uiTrackCRC == uiCRC);
  CHECK(config.uiCommunityTrackCRC == uiCRC);
  g_iCommunityTrackSel = 1;
  g_iCommunityTrackMissing = -1;
  g_uiCommunityTrackCRC = 0;
  CHECK(NetSessionConfigApply(&config));
  CHECK(g_iCommunityTrackSel == 0);
  CHECK(!g_iCommunityTrackMissing);
  CHECK(g_uiCommunityTrackCRC == uiCRC);
}

static void NetTestHostClientAgreement(void)
{
  tNetTransportSim *pSim;
  tNetChannel *pHostChannel, *pClientChannel;
  tNetConnection *pClientConnection;
  tNetSessionHost *pHost;
  tNetSessionClient *pClient;
  tNetSessionConfigOptions options;
  tNetSessionConfig hostConfig, clientConfig;
  tNetTestRandom random = {0x123456789abcdef0ull};
  tNetAddress hostAddress = NetTestAddress(1);
  int iTime;

  NetTestSetHostGlobals();
  NetSessionConfigOptionsDefault(&options);
  CHECK(NetSessionConfigBuild(&hostConfig, &options));
  pSim = NetTransportSimCreate(2021);
  CHECK(pSim);
  pHostChannel = NetChannelCreate(NetTransportSimEndpoint(pSim, 1));
  pClientChannel = NetChannelCreate(NetTransportSimEndpoint(pSim, 0));
  CHECK(pHostChannel && pClientChannel);
  pHost = NetSessionHostCreate(pHostChannel, options.byMaxPlayers,
                               NetTestRandomBytes, &random);
  CHECK(pHost && NetSessionHostSetConfig(pHost, &hostConfig));
  pClientConnection = NetChannelAddConnection(pClientChannel, &hostAddress, 0, 0);
  CHECK(pClientConnection);
  pClient = NetSessionClientCreate(pClientConnection, NET_PROTOCOL_VERSION,
                                   1, "Driver");
  CHECK(pClient && NetSessionClientStart(pClient));
  for (iTime = 0; iTime <= 500; ++iTime) {
    CHECK(NetTransportSimAdvance(pSim, (uint64)iTime));
    NetPump();
    NetSessionHostPump(pHost);
    NetSessionClientPump(pClient);
  }
  CHECK(NetSessionClientState(pClient) == NET_JOIN_ACCEPTED);
  CHECK(NetSessionClientGetConfig(pClient, &clientConfig));
  CHECK(clientConfig.iTrackLoad == hostConfig.iTrackLoad);
  CHECK(clientConfig.uiTrackCRC == hostConfig.uiTrackCRC);
  CHECK(clientConfig.iGameType == hostConfig.iGameType);

  NetSessionClientDestroy(pClient);
  NetSessionHostDestroy(pHost);
  NetChannelDestroy(pClientChannel);
  NetChannelDestroy(pHostChannel);
  NetTransportSimDestroy(pSim);
}

static void NetTestDecodeRejectsWithoutMutation(void)
{
  tNetSessionConfigOptions options;
  tNetSessionConfig config, sentinel;
  tNetTestGlobals before, after;
  uint8 abWire[NET_SESSION_CONFIG_WIRE_SIZE];
  int aiOffsets[] = {0, 2, 5, 6, 7, 8, 12, 16, 20, 24, 28, 32};
  int iCase;

  NetTestSetHostGlobals();
  NetSessionConfigOptionsDefault(&options);
  CHECK(NetSessionConfigBuild(&config, &options));
  CHECK(NetSessionConfigEncode(&config, abWire, sizeof(abWire)));
  memset(&sentinel, 0xA5, sizeof(sentinel));
  for (iCase = 0; iCase < (int)(sizeof(aiOffsets) / sizeof(aiOffsets[0]));
       ++iCase) {
    uint8 abBad[NET_SESSION_CONFIG_WIRE_SIZE];
    tNetSessionConfig output = sentinel;
    memcpy(abBad, abWire, sizeof(abBad));
    abBad[aiOffsets[iCase]] = 0xFF;
    CHECK(!NetSessionConfigDecode(&output, abBad, sizeof(abBad)));
    CHECK(memcmp(&output, &sentinel, sizeof(output)) == 0);
  }
  CHECK(!NetSessionConfigDecode(&config, abWire, sizeof(abWire) - 1));

  before = NetTestCaptureGlobals();
  config.iDamageLevel = 99;
  CHECK(!NetSessionConfigApply(&config));
  after = NetTestCaptureGlobals();
  CHECK(memcmp(&before, &after, sizeof(before)) == 0);

  CHECK(NetSessionConfigEncode(&sentinel, abWire, sizeof(abWire)) == 0);
  memcpy(abWire + 52, "unterminated", 12);
  memset(abWire + 64, 'X', NET_COMMUNITY_TRACK_FILENAME - 12);
  CHECK(!NetSessionConfigDecode(&config, abWire, sizeof(abWire)));
}

static void NetTestDedicatedCannotPause(void)
{
  tNetSessionConfigOptions options;
  tNetSessionConfig config, sentinel;
  NetTestSetHostGlobals();
  NetSessionConfigOptionsDefault(&options);
  options.byHostIsDedicated = 1;
  options.byPauseAllowed = 1;
  memset(&sentinel, 0x3C, sizeof(sentinel));
  config = sentinel;
  CHECK(!NetSessionConfigBuild(&config, &options));
  CHECK(memcmp(&config, &sentinel, sizeof(config)) == 0);
  options.byPauseAllowed = 0;
  CHECK(NetSessionConfigBuild(&config, &options));
}

int main(void)
{
  CHECK(sizeof(tNetSessionConfig) == NET_SESSION_CONFIG_WIRE_SIZE);
  NetTestBuildEncodeApply();
  NetTestCommunityTrack();
  NetTestHostClientAgreement();
  NetTestDecodeRejectsWithoutMutation();
  NetTestDedicatedCannotPause();
  puts("NET-E2-S1 session configuration acceptance passed");
  return 0;
}
