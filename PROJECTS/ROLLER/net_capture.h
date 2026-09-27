#ifndef ROLLER_NET_CAPTURE_H
#define ROLLER_NET_CAPTURE_H

#include "net_transport.h"

typedef struct tNetPacketCapture tNetPacketCapture;
typedef struct tNetPacketPlayback tNetPacketPlayback;

/* Captures successful transport calls. The wrapped transport and capture
   object must remain alive until every user of the returned endpoint is
   destroyed. NetPacketCaptureClose flushes the file and returns zero if a
   write failed. */
tNetPacketCapture *NetPacketCaptureCreate(tNetTransport transport,
                                          const char *szPath);
tNetTransport NetPacketCaptureEndpoint(tNetPacketCapture *pCapture);
int NetPacketCaptureClose(tNetPacketCapture *pCapture);

/* Playback has a caller-controlled monotonic clock. Incoming records become
   readable at their captured timestamp; outgoing records must be reproduced
   byte-for-byte, with the same destination and ordering. */
tNetPacketPlayback *NetPacketPlaybackCreate(const char *szPath);
void NetPacketPlaybackDestroy(tNetPacketPlayback *pPlayback);
tNetTransport NetPacketPlaybackEndpoint(tNetPacketPlayback *pPlayback);
int NetPacketPlaybackAdvance(tNetPacketPlayback *pPlayback, uint64 ullNowMs);
uint64 NetPacketPlaybackFirstMs(const tNetPacketPlayback *pPlayback);
uint64 NetPacketPlaybackLastMs(const tNetPacketPlayback *pPlayback);
int NetPacketPlaybackComplete(const tNetPacketPlayback *pPlayback);
int NetPacketPlaybackOk(const tNetPacketPlayback *pPlayback);

#endif
