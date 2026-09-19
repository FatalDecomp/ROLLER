#ifndef ROLLER_NET_CONFIG_H
#define ROLLER_NET_CONFIG_H

#include "net_protocol.h"

#define NET_SESSION_CONFIG_WIRE_SIZE 312
#define NET_SESSION_DEFAULT_SNAPSHOT_INTERVAL 2
#define NET_SESSION_MAX_PLAYERS 16
#define NET_SESSION_FIRST_STOCK_TRACK 1
#define NET_SESSION_COMMUNITY_TRACK 25

typedef struct
{
  uint8 bySnapshotInterval;
  uint8 byMaxPlayers;
  uint8 byHostIsDedicated;
  uint8 byPauseAllowed;
  uint32 uiRandomSeed;
  char szBuildHash[16];
} tNetSessionConfigOptions;

void NetSessionConfigOptionsDefault(tNetSessionConfigOptions *pOptions);
int NetSessionConfigBuild(tNetSessionConfig *pConfig,
                          const tNetSessionConfigOptions *pOptions);
int NetSessionConfigValidate(const tNetSessionConfig *pConfig);
int NetSessionConfigEncode(const tNetSessionConfig *pConfig, void *pData,
                           int iCapacity);
int NetSessionConfigDecode(tNetSessionConfig *pConfig, const void *pData,
                           int iLength);
int NetSessionConfigApply(const tNetSessionConfig *pConfig);

#endif
