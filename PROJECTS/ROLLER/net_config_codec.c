#include "net_config.h"
#include "net_config_internal.h"

#include <string.h>

static uint16 NetSessionConfigRead16(const uint8 *pData)
{
  return (uint16)((uint16)pData[0] | ((uint16)pData[1] << 8));
}

static uint32 NetSessionConfigRead32(const uint8 *pData)
{
  return (uint32)pData[0] |
         ((uint32)pData[1] << 8) |
         ((uint32)pData[2] << 16) |
         ((uint32)pData[3] << 24);
}

static int32 NetSessionConfigReadS32(const uint8 *pData)
{
  uint32 uiValue = NetSessionConfigRead32(pData);
  int32 iValue;
  memcpy(&iValue, &uiValue, sizeof(iValue));
  return iValue;
}

static void NetSessionConfigWrite16(uint8 *pData, uint16 unValue)
{
  pData[0] = (uint8)unValue;
  pData[1] = (uint8)(unValue >> 8);
}

static void NetSessionConfigWrite32(uint8 *pData, uint32 uiValue)
{
  pData[0] = (uint8)uiValue;
  pData[1] = (uint8)(uiValue >> 8);
  pData[2] = (uint8)(uiValue >> 16);
  pData[3] = (uint8)(uiValue >> 24);
}

static int NetSessionConfigStringValid(const char *szValue, int iCapacity,
                                       int iRequireValue)
{
  int iChar;
  if (!szValue || iCapacity <= 0)
    return 0;
  for (iChar = 0; iChar < iCapacity; ++iChar) {
    unsigned char byChar = (unsigned char)szValue[iChar];
    if (!byChar)
      return !iRequireValue || iChar > 0;
    if (byChar < 32 || byChar > 126)
      return 0;
  }
  return 0;
}

uint16 NetSessionConfigTickRateFromFlags(int32 iLevelFlags)
{
  if ((iLevelFlags & NET_SESSION_100HZ_FLAG) != 0)
    return 100;
  if ((iLevelFlags & NET_SESSION_50HZ_FLAG) != 0)
    return 50;
  return 36;
}

int NetSessionConfigValidate(const tNetSessionConfig *pConfig)
{
  int iCommunity;
  if (!pConfig || pConfig->unProtocolVersion != NET_PROTOCOL_VERSION)
    return 0;
  if (pConfig->unTickRateHz != 36 && pConfig->unTickRateHz != 50 &&
      pConfig->unTickRateHz != 100)
    return 0;
  if (!pConfig->bySnapshotInterval || !pConfig->byMaxPlayers ||
      pConfig->byMaxPlayers > NET_SESSION_MAX_PLAYERS)
    return 0;
  if (pConfig->byHostIsDedicated > 1 || pConfig->byPauseAllowed > 1 ||
      (pConfig->byHostIsDedicated && pConfig->byPauseAllowed))
    return 0;
  if (pConfig->iTrackLoad < NET_SESSION_FIRST_STOCK_TRACK ||
      pConfig->iTrackLoad > NET_SESSION_COMMUNITY_TRACK)
    return 0;
  if (pConfig->iGameType < 0 || pConfig->iGameType > 2 ||
      pConfig->iManualControl < 1 || pConfig->iManualControl > 2)
    return 0;
  if ((pConfig->iLevelFlags & ~NET_SESSION_ALLOWED_LEVEL_FLAGS) != 0 ||
      (pConfig->iLevelFlags & NET_SESSION_LEVEL_MASK) > 5)
    return 0;
  if ((pConfig->iLevelFlags & NET_SESSION_100HZ_FLAG) != 0 &&
      (pConfig->iLevelFlags & NET_SESSION_50HZ_FLAG) == 0)
    return 0;
  if (NetSessionConfigTickRateFromFlags(pConfig->iLevelFlags) !=
      pConfig->unTickRateHz)
    return 0;
  if (pConfig->iGameType < 2) {
    if (pConfig->iCompetitors != 2 && pConfig->iCompetitors != 8 &&
        pConfig->iCompetitors != 16)
      return 0;
  } else if (pConfig->iCompetitors != 1) {
    return 0;
  }
  if (pConfig->iDamageLevel < 0 || pConfig->iDamageLevel > 2 ||
      (pConfig->iTextureMode != 0 && pConfig->iTextureMode != 1))
    return 0;

  iCommunity = pConfig->iTrackLoad == NET_SESSION_COMMUNITY_TRACK;
  if (!NetSessionConfigStringValid(pConfig->szCommunityTrack,
                                   sizeof(pConfig->szCommunityTrack),
                                   iCommunity))
    return 0;
  if ((!iCommunity && (pConfig->szCommunityTrack[0] ||
                       pConfig->uiCommunityTrackCRC != 0)) ||
      (iCommunity && pConfig->uiCommunityTrackCRC != pConfig->uiTrackCRC))
    return 0;
  return NetSessionConfigStringValid(pConfig->szBuildHash,
                                     sizeof(pConfig->szBuildHash), 0);
}

int NetSessionConfigEncode(const tNetSessionConfig *pConfig, void *pData,
                           int iCapacity)
{
  uint8 *pBytes = (uint8 *)pData;
  if (!pData || iCapacity < NET_SESSION_CONFIG_WIRE_SIZE ||
      !NetSessionConfigValidate(pConfig))
    return 0;
  NetSessionConfigWrite16(pBytes, pConfig->unProtocolVersion);
  NetSessionConfigWrite16(pBytes + 2, pConfig->unTickRateHz);
  pBytes[4] = pConfig->bySnapshotInterval;
  pBytes[5] = pConfig->byMaxPlayers;
  pBytes[6] = pConfig->byHostIsDedicated;
  pBytes[7] = pConfig->byPauseAllowed;
  NetSessionConfigWrite32(pBytes + 8, (uint32)pConfig->iTrackLoad);
  NetSessionConfigWrite32(pBytes + 12, (uint32)pConfig->iGameType);
  NetSessionConfigWrite32(pBytes + 16, (uint32)pConfig->iManualControl);
  NetSessionConfigWrite32(pBytes + 20, (uint32)pConfig->iLevelFlags);
  NetSessionConfigWrite32(pBytes + 24, (uint32)pConfig->iCompetitors);
  NetSessionConfigWrite32(pBytes + 28, (uint32)pConfig->iDamageLevel);
  NetSessionConfigWrite32(pBytes + 32, (uint32)pConfig->iTextureMode);
  NetSessionConfigWrite32(pBytes + 36, (uint32)pConfig->iNetworkChampOn);
  NetSessionConfigWrite32(pBytes + 40, pConfig->uiRandomSeed);
  NetSessionConfigWrite32(pBytes + 44, pConfig->uiTrackCRC);
  NetSessionConfigWrite32(pBytes + 48, pConfig->uiCommunityTrackCRC);
  memcpy(pBytes + 52, pConfig->szCommunityTrack,
         sizeof(pConfig->szCommunityTrack));
  memcpy(pBytes + 52 + sizeof(pConfig->szCommunityTrack),
         pConfig->szBuildHash, sizeof(pConfig->szBuildHash));
  return NET_SESSION_CONFIG_WIRE_SIZE;
}

int NetSessionConfigDecode(tNetSessionConfig *pConfig, const void *pData,
                           int iLength)
{
  const uint8 *pBytes = (const uint8 *)pData;
  tNetSessionConfig config;
  if (!pConfig || !pData || iLength != NET_SESSION_CONFIG_WIRE_SIZE)
    return 0;
  memset(&config, 0, sizeof(config));
  config.unProtocolVersion = NetSessionConfigRead16(pBytes);
  config.unTickRateHz = NetSessionConfigRead16(pBytes + 2);
  config.bySnapshotInterval = pBytes[4];
  config.byMaxPlayers = pBytes[5];
  config.byHostIsDedicated = pBytes[6];
  config.byPauseAllowed = pBytes[7];
  config.iTrackLoad = NetSessionConfigReadS32(pBytes + 8);
  config.iGameType = NetSessionConfigReadS32(pBytes + 12);
  config.iManualControl = NetSessionConfigReadS32(pBytes + 16);
  config.iLevelFlags = NetSessionConfigReadS32(pBytes + 20);
  config.iCompetitors = NetSessionConfigReadS32(pBytes + 24);
  config.iDamageLevel = NetSessionConfigReadS32(pBytes + 28);
  config.iTextureMode = NetSessionConfigReadS32(pBytes + 32);
  config.iNetworkChampOn = NetSessionConfigReadS32(pBytes + 36);
  config.uiRandomSeed = NetSessionConfigRead32(pBytes + 40);
  config.uiTrackCRC = NetSessionConfigRead32(pBytes + 44);
  config.uiCommunityTrackCRC = NetSessionConfigRead32(pBytes + 48);
  memcpy(config.szCommunityTrack, pBytes + 52,
         sizeof(config.szCommunityTrack));
  memcpy(config.szBuildHash,
         pBytes + 52 + sizeof(config.szCommunityTrack),
         sizeof(config.szBuildHash));
  if (!NetSessionConfigValidate(&config))
    return 0;
  *pConfig = config;
  return 1;
}
