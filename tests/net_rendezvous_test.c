#include "net_rendezvous.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(iCondition) do { if (!(iCondition)) { \
  fprintf(stderr, "%d: %s\n", __LINE__, #iCondition); exit(1); \
} } while (0)

typedef struct {
  uint64 ullNowMs;
  tNetAddress source;
  uint8 abIncoming[NET_MAX_PAYLOAD];
  int iIncomingLength, iIncomingReady;
  tNetAddress destination;
  uint8 abOutgoing[NET_MAX_PAYLOAD];
  int iOutgoingLength, iSendCount;
} tRvzTestTransport;

typedef struct {
  uint32 uiNext;
  int iFail;
} tRvzTestRandom;

static void TestWrite16(uint8 *pData, uint16 unValue)
{
  pData[0] = (uint8)unValue;
  pData[1] = (uint8)(unValue >> 8);
}

static void TestWrite32(uint8 *pData, uint32 uiValue)
{
  pData[0] = (uint8)uiValue;
  pData[1] = (uint8)(uiValue >> 8);
  pData[2] = (uint8)(uiValue >> 16);
  pData[3] = (uint8)(uiValue >> 24);
}

static void TestWrite64(uint8 *pData, uint64 ullValue)
{
  TestWrite32(pData, (uint32)ullValue);
  TestWrite32(pData + 4, (uint32)(ullValue >> 32));
}

static uint16 TestRead16(const uint8 *pData)
{
  return (uint16)(pData[0] | ((uint16)pData[1] << 8));
}

static uint32 TestRead32(const uint8 *pData)
{
  return (uint32)pData[0] | ((uint32)pData[1] << 8) |
      ((uint32)pData[2] << 16) | ((uint32)pData[3] << 24);
}

static int TestSend(void *pContext, const tNetAddress *pTo,
                    const void *pData, int iLength)
{
  tRvzTestTransport *pTransport = (tRvzTestTransport *)pContext;
  CHECK(pTo && pData && iLength > 0 && iLength <= NET_MAX_PAYLOAD);
  pTransport->destination = *pTo;
  memcpy(pTransport->abOutgoing, pData, (size_t)iLength);
  pTransport->iOutgoingLength = iLength;
  ++pTransport->iSendCount;
  return iLength;
}

static int TestReceive(void *pContext, tNetAddress *pFrom,
                       void *pData, int iCapacity)
{
  tRvzTestTransport *pTransport = (tRvzTestTransport *)pContext;
  if (!pTransport->iIncomingReady)
    return 0;
  CHECK(pFrom && iCapacity >= pTransport->iIncomingLength);
  *pFrom = pTransport->source;
  memcpy(pData, pTransport->abIncoming,
         (size_t)pTransport->iIncomingLength);
  pTransport->iIncomingReady = 0;
  return pTransport->iIncomingLength;
}

static uint64 TestNow(void *pContext)
{
  return ((tRvzTestTransport *)pContext)->ullNowMs;
}

static int TestRandom(void *pContext, void *pData, int iLength)
{
  tRvzTestRandom *pRandom = (tRvzTestRandom *)pContext;
  uint8 *pBytes = (uint8 *)pData;
  int iByte;
  if (pRandom->iFail)
    return 0;
  for (iByte = 0; iByte < iLength; ++iByte) {
    if (!(iByte & 3))
      ++pRandom->uiNext;
    pBytes[iByte] = (uint8)(pRandom->uiNext >> ((iByte & 3) * 8));
  }
  return 1;
}

static tNetTransport TestEndpoint(tRvzTestTransport *pTransport)
{
  tNetTransport endpoint;
  endpoint.pContext = pTransport;
  endpoint.pSend = TestSend;
  endpoint.pReceive = TestReceive;
  endpoint.pNowMs = TestNow;
  return endpoint;
}

static tNetAddress TestAddress(unsigned iAddress, uint16 unPort)
{
  tNetAddress address;
  memset(&address, 0, sizeof(address));
  address.byFamily = NET_ADDR_IPV4;
  address.abAddress[0] = 10;
  address.abAddress[1] = (uint8)(iAddress >> 16);
  address.abAddress[2] = (uint8)(iAddress >> 8);
  address.abAddress[3] = (uint8)iAddress;
  address.unPort = unPort;
  return address;
}

static tRvzSessionInfo TestInfo(const char *szName, const char *szBuild,
                                uint8 byPlayers)
{
  tRvzSessionInfo info;
  memset(&info, 0, sizeof(info));
  info.unPort = 9999;
  info.unTickRateHz = 36;
  info.byPlayers = byPlayers;
  info.byMaxPlayers = 8;
  memcpy(info.szName, szName, strlen(szName));
  memcpy(info.szTrack, "TRACK5", 6);
  memcpy(info.szBuildHash, szBuild, strlen(szBuild));
  return info;
}

static int TestTransact(tNetRendezvous *pRendezvous,
                        tRvzTestTransport *pTransport,
                        const tNetAddress *pSource, uint16 unSequence,
                        uint64 ullToken, uint8 byType,
                        const void *pPayload, uint16 unPayloadLength,
                        tNetRendezvousPacket *pResponse)
{
  int iBefore = pTransport->iSendCount;
  pTransport->source = *pSource;
  pTransport->iIncomingLength = NetRendezvousBuildPacket(
      pTransport->abIncoming, sizeof(pTransport->abIncoming), unSequence,
      ullToken, byType, pPayload, unPayloadLength);
  CHECK(pTransport->iIncomingLength > 0);
  pTransport->iIncomingReady = 1;
  CHECK(NetRendezvousPump(pRendezvous) == 1);
  if (pTransport->iSendCount == iBefore)
    return 0;
  CHECK(pTransport->iSendCount == iBefore + 1);
  CHECK(NetRendezvousParsePacket(pTransport->abOutgoing,
                                 pTransport->iOutgoingLength, pResponse));
  CHECK(pResponse->unSequence == unSequence);
  CHECK(pTransport->destination.byFamily == pSource->byFamily);
  CHECK(pTransport->destination.unPort == pSource->unPort);
  return 1;
}

static void TestEncodeRegister(uint8 *pPayload, uint64 ullNonce,
                               const tRvzSessionInfo *pInfo)
{
  TestWrite64(pPayload, ullNonce);
  NetRendezvousEncodeSessionInfo(pPayload + 8, pInfo);
}

static void TestRegister(tNetRendezvous *pRendezvous,
                         tRvzTestTransport *pTransport,
                         const tNetAddress *pSource, uint64 ullNonce,
                         const tRvzSessionInfo *pInfo, uint16 unSequence,
                         uint32 *pSessionId, uint64 *pToken)
{
  uint8 abPayload[sizeof(tRvzRegisterRequest)];
  tNetRendezvousPacket response;
  TestEncodeRegister(abPayload, ullNonce, pInfo);
  CHECK(TestTransact(pRendezvous, pTransport, pSource, unSequence, 0,
                     NET_RVZ_MSG_REGISTER, abPayload, sizeof(abPayload),
                     &response));
  CHECK(response.byType == NET_RVZ_MSG_REGISTERED);
  CHECK(response.unPayloadLength == sizeof(tRvzAck));
  CHECK(TestRead32(response.pPayload) != 0);
  CHECK(TestRead32(response.pPayload + 4) == NET_RVZ_SESSION_LEASE_MS);
  CHECK(response.ullToken != 0);
  if (pSessionId)
    *pSessionId = TestRead32(response.pPayload);
  if (pToken)
    *pToken = response.ullToken;
}

static void TestProtocolAndLifecycle(void)
{
  tRvzTestTransport transport = {0};
  tRvzTestRandom random = {100, 0};
  tNetRendezvous *pRendezvous = NetRendezvousCreate(
      TestEndpoint(&transport), TestRandom, &random);
  tNetAddress host = TestAddress(1, 40000);
  tNetAddress browser = TestAddress(2, 40001);
  tRvzSessionInfo info = TestInfo("SPEED SERVER", "build-a", 1);
  tNetRendezvousPacket response;
  tNetRendezvousStats stats;
  uint8 abPayload[sizeof(tRvzRegisterRequest)];
  uint8 abList[sizeof(tRvzListRequest)] = {0};
  uint8 abInfo[sizeof(tRvzSessionInfo)];
  uint8 abId[4];
  uint32 uiSessionId, uiDuplicateId;
  uint64 ullToken, ullDuplicateToken;

  CHECK(pRendezvous);
  CHECK(!NetRendezvousParsePacket("bad", 3, &response));
  TestRegister(pRendezvous, &transport, &host, 0x1122334455667788ull,
               &info, 1, &uiSessionId, &ullToken);
  CHECK(uiSessionId && ullToken);

  TestRegister(pRendezvous, &transport, &host, 0x1122334455667788ull,
               &info, 2, &uiDuplicateId, &ullDuplicateToken);
  CHECK(uiDuplicateId == uiSessionId && ullDuplicateToken == ullToken);
  NetRendezvousGetStats(pRendezvous, &stats);
  CHECK(stats.iActiveSessions == 1 && stats.iSessionHighWater == 1);

  info.uiSessionId = uiSessionId;
  info.byPlayers = 3;
  NetRendezvousEncodeSessionInfo(abInfo, &info);
  CHECK(TestTransact(pRendezvous, &transport, &host, 3, ullToken,
                     NET_RVZ_MSG_HEARTBEAT, abInfo, sizeof(abInfo),
                     &response));
  CHECK(response.byType == NET_RVZ_MSG_ACK &&
        TestRead32(response.pPayload) == uiSessionId);

  CHECK(TestTransact(pRendezvous, &transport, &host, 4, ullToken + 1,
                     NET_RVZ_MSG_HEARTBEAT, abInfo, sizeof(abInfo),
                     &response));
  CHECK(response.byType == NET_RVZ_MSG_ERROR &&
        response.pPayload[0] == NET_RVZ_ERROR_NOT_FOUND);

  CHECK(TestTransact(pRendezvous, &transport, &browser, 5, 0,
                     NET_RVZ_MSG_LIST, abList, sizeof(abList), &response));
  CHECK(response.byType == NET_RVZ_MSG_LIST_PAGE);
  CHECK(response.unPayloadLength == sizeof(tRvzListPageHeader) +
                                     sizeof(tRvzSessionInfo));
  CHECK(TestRead16(response.pPayload) == 0);
  CHECK(TestRead16(response.pPayload + 2) == 1);
  CHECK(TestRead16(response.pPayload + 4) == 1);
  CHECK(response.pPayload[6] == 1 && response.pPayload[7] == 0);
  NetRendezvousDecodeSessionInfo(&info,
      response.pPayload + sizeof(tRvzListPageHeader));
  CHECK(info.uiSessionId == uiSessionId && info.unPort == host.unPort &&
        info.byPlayers == 3 && strcmp(info.szName, "SPEED SERVER") == 0);

  memset(abList, 0, sizeof(abList));
  abList[2] = 1;
  memcpy(abList + 4, "other-build", 11);
  CHECK(TestTransact(pRendezvous, &transport, &browser, 6, 0,
                     NET_RVZ_MSG_LIST, abList, sizeof(abList), &response));
  CHECK(response.byType == NET_RVZ_MSG_LIST_PAGE &&
        TestRead16(response.pPayload + 4) == 0 &&
        response.pPayload[6] == 0);
  memset(abList, 0, sizeof(abList));
  TestWrite16(abList, 0xffffu);
  CHECK(TestTransact(pRendezvous, &transport, &browser, 61, 0,
                     NET_RVZ_MSG_LIST, abList, sizeof(abList), &response));
  CHECK(response.byType == NET_RVZ_MSG_LIST_PAGE &&
        TestRead16(response.pPayload) == 0xffffu &&
        response.pPayload[6] == 0);
  abList[3] = 1;
  CHECK(TestTransact(pRendezvous, &transport, &browser, 62, 0,
                     NET_RVZ_MSG_LIST, abList, sizeof(abList), &response));
  CHECK(response.byType == NET_RVZ_MSG_ERROR &&
        response.pPayload[0] == NET_RVZ_ERROR_INVALID);

  TestWrite32(abId, uiSessionId);
  CHECK(TestTransact(pRendezvous, &transport, &host, 7, ullToken,
                     NET_RVZ_MSG_UNREGISTER, abId, sizeof(abId), &response));
  CHECK(response.byType == NET_RVZ_MSG_ACK &&
        TestRead32(response.pPayload + 4) == 0);
  NetRendezvousGetStats(pRendezvous, &stats);
  CHECK(stats.iActiveSessions == 0 && stats.ullAuthRejects == 1);

  info = TestInfo("BAD", "build-a", 1);
  info.szName[4] = 'X';
  TestEncodeRegister(abPayload, 99, &info);
  CHECK(TestTransact(pRendezvous, &transport, &host, 8, 0,
                     NET_RVZ_MSG_REGISTER, abPayload, sizeof(abPayload),
                     &response));
  CHECK(response.byType == NET_RVZ_MSG_ERROR &&
        response.pPayload[0] == NET_RVZ_ERROR_INVALID);

  random.iFail = 1;
  info = TestInfo("NO RANDOM", "build-a", 1);
  TestEncodeRegister(abPayload, 100, &info);
  CHECK(TestTransact(pRendezvous, &transport, &host, 9, 0,
                     NET_RVZ_MSG_REGISTER, abPayload, sizeof(abPayload),
                     &response));
  CHECK(response.byType == NET_RVZ_MSG_ERROR &&
        response.pPayload[0] == NET_RVZ_ERROR_RANDOM_UNAVAILABLE);

  transport.abIncoming[0] = 0;
  transport.iIncomingLength = 1;
  transport.iIncomingReady = 1;
  CHECK(NetRendezvousPump(pRendezvous) == 1);
  NetRendezvousGetStats(pRendezvous, &stats);
  CHECK(stats.ullMalformedPackets == 1);
  NetRendezvousDestroy(pRendezvous);
}

static void TestLimitsPaginationAndExpiry(void)
{
  tRvzTestTransport transport = {0};
  tRvzTestRandom random = {1000, 0};
  tNetRendezvous *pRendezvous = NetRendezvousCreate(
      TestEndpoint(&transport), TestRandom, &random);
  tRvzSessionInfo info = TestInfo("LIMIT", "build-limit", 1);
  tNetRendezvousPacket response;
  tNetRendezvousStats stats;
  tNetAddress source;
  uint8 abPayload[sizeof(tRvzRegisterRequest)];
  uint8 abList[sizeof(tRvzListRequest)] = {0};
  int iSession;

  CHECK(pRendezvous);
  source = TestAddress(10, 41000);
  for (iSession = 0; iSession < NET_RVZ_MAX_SESSIONS_PER_IP; ++iSession)
    TestRegister(pRendezvous, &transport, &source,
                 100 + (uint64)iSession, &info, (uint16)iSession,
                 NULL, NULL);
  TestEncodeRegister(abPayload, 999, &info);
  CHECK(TestTransact(pRendezvous, &transport, &source, 10, 0,
                     NET_RVZ_MSG_REGISTER, abPayload, sizeof(abPayload),
                     &response));
  CHECK(response.byType == NET_RVZ_MSG_ERROR &&
        response.pPayload[0] == NET_RVZ_ERROR_SOURCE_LIMIT);

  for (iSession = NET_RVZ_MAX_SESSIONS_PER_IP;
       iSession < 13; ++iSession) {
    source = TestAddress(10 + (unsigned)iSession,
                         (uint16)(41000 + iSession));
    TestRegister(pRendezvous, &transport, &source,
                 100 + (uint64)iSession, &info, (uint16)iSession,
                 NULL, NULL);
  }
  source = TestAddress(1000, 42000);
  CHECK(TestTransact(pRendezvous, &transport, &source, 20, 0,
                     NET_RVZ_MSG_LIST, abList, sizeof(abList), &response));
  CHECK(TestRead16(response.pPayload + 2) == 2 &&
        TestRead16(response.pPayload + 4) == 13 &&
        response.pPayload[6] == 12 && response.pPayload[7] == 1);
  TestWrite16(abList, 1);
  CHECK(TestTransact(pRendezvous, &transport, &source, 21, 0,
                     NET_RVZ_MSG_LIST, abList, sizeof(abList), &response));
  CHECK(response.pPayload[6] == 1 && response.pPayload[7] == 0);

  for (iSession = 13; iSession < NET_RVZ_MAX_SESSIONS; ++iSession) {
    source = TestAddress(100 + (unsigned)(iSession / 4),
                         (uint16)(43000 + iSession));
    TestRegister(pRendezvous, &transport, &source,
                 1000 + (uint64)iSession, &info, (uint16)iSession,
                 NULL, NULL);
  }
  NetRendezvousGetStats(pRendezvous, &stats);
  CHECK(stats.iActiveSessions == NET_RVZ_MAX_SESSIONS &&
        stats.iSessionHighWater == NET_RVZ_MAX_SESSIONS);
  source = TestAddress(5000, 50000);
  TestEncodeRegister(abPayload, 99999, &info);
  CHECK(TestTransact(pRendezvous, &transport, &source, 30, 0,
                     NET_RVZ_MSG_REGISTER, abPayload, sizeof(abPayload),
                     &response));
  CHECK(response.byType == NET_RVZ_MSG_ERROR &&
        response.pPayload[0] == NET_RVZ_ERROR_FULL);

  transport.ullNowMs = NET_RVZ_SESSION_LEASE_MS;
  CHECK(NetRendezvousPump(pRendezvous) == 0);
  NetRendezvousGetStats(pRendezvous, &stats);
  CHECK(stats.iActiveSessions == 0 &&
        stats.ullExpiredSessions == NET_RVZ_MAX_SESSIONS);
  NetRendezvousDestroy(pRendezvous);
}

static void TestRateLimit(void)
{
  tRvzTestTransport transport = {0};
  tRvzTestRandom random = {2000, 0};
  tNetRendezvous *pRendezvous = NetRendezvousCreate(
      TestEndpoint(&transport), TestRandom, &random);
  tNetAddress source = TestAddress(6000, 51000);
  tNetRendezvousPacket response;
  tNetRendezvousStats stats;
  uint8 abList[sizeof(tRvzListRequest)] = {0};
  int iRequest;
  CHECK(pRendezvous);
  for (iRequest = 0; iRequest < NET_RVZ_CONTROL_BURST; ++iRequest)
    CHECK(TestTransact(pRendezvous, &transport, &source,
                       (uint16)iRequest, 0, NET_RVZ_MSG_LIST,
                       abList, sizeof(abList), &response));
  CHECK(!TestTransact(pRendezvous, &transport, &source, 100, 0,
                      NET_RVZ_MSG_LIST, abList, sizeof(abList), &response));
  transport.ullNowMs = 50;
  CHECK(TestTransact(pRendezvous, &transport, &source, 101, 0,
                     NET_RVZ_MSG_LIST, abList, sizeof(abList), &response));
  NetRendezvousGetStats(pRendezvous, &stats);
  CHECK(stats.ullRateLimitedPackets == 1);
  NetRendezvousDestroy(pRendezvous);
}

static void TestSyntheticDay(void)
{
  tRvzTestTransport transport = {0};
  tRvzTestRandom random = {3000, 0};
  tNetRendezvous *pRendezvous = NetRendezvousCreate(
      TestEndpoint(&transport), TestRandom, &random);
  tRvzSessionInfo info = TestInfo("SOAK", "build-soak", 1);
  tNetRendezvousStats stats;
  uint32 uiSecond;
  CHECK(pRendezvous);
  for (uiSecond = 0; uiSecond < 24u * 60u * 60u; ++uiSecond) {
    tNetAddress source = TestAddress(10000 + uiSecond % 512,
                                     (uint16)(52000 + uiSecond % 1000));
    transport.ullNowMs = (uint64)uiSecond * 1000;
    TestRegister(pRendezvous, &transport, &source,
                 0x100000000ull + uiSecond, &info,
                 (uint16)uiSecond, NULL, NULL);
  }
  transport.ullNowMs += NET_RVZ_SESSION_LEASE_MS;
  CHECK(NetRendezvousPump(pRendezvous) == 0);
  NetRendezvousGetStats(pRendezvous, &stats);
  CHECK(stats.iActiveSessions == 0);
  CHECK(stats.iSessionHighWater == 30);
  CHECK(stats.iRateSources <= 1024 && stats.iRateSourceHighWater <= 1024);
  CHECK(stats.ullPacketsReceived == 24u * 60u * 60u);
  CHECK(stats.ullPacketsSent == stats.ullPacketsReceived);
  NetRendezvousDestroy(pRendezvous);
}

int main(void)
{
  TestProtocolAndLifecycle();
  TestLimitsPaginationAndExpiry();
  TestRateLimit();
  TestSyntheticDay();
  puts("NET-E6-S1 rendezvous limits and virtual 24 h soak passed");
  return 0;
}
