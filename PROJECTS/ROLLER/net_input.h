#ifndef ROLLER_NET_INPUT_H
#define ROLLER_NET_INPUT_H

#include "net_protocol.h"
#include "sound.h"

#define NET_INPUT_MAX_LOCAL_PLAYERS 2
#define NET_INPUT_BATCH_MAX_BYTES \
  ((int)sizeof(tNetInputBatchHeader) + \
   NET_INPUT_REDUNDANCY * NET_INPUT_MAX_LOCAL_PLAYERS * 4)

/* The flags a client may send (4.13).  Everything else, including the legacy
   lockstep FLAG_DISCONNECT, FLAG_MASTER_CHANGE and FLAG_FINISHED bits and the
   F1..F4 strategy keys (chat in modern mode), is stripped by the host. */
#define NET_INPUT_ALLOWED_FLAGS \
  (BUTTON_FLAG_ACCEL | BUTTON_FLAG_BRAKE | BUTTON_FLAG_UPGEAR | \
   BUTTON_FLAG_DOWNGEAR | BUTTON_FLAG_SPECIAL | BUTTON_FLAG_PHONE_THROTTLE)

typedef struct
{
  uint32 uiFirstTick, uiLastDecodedSnapshotTick;
  uint8 byCount, byLocalPlayers;
  tCarInputData aInputs[NET_INPUT_REDUNDANCY][NET_INPUT_MAX_LOCAL_PLAYERS];
} tNetInputBatch;

/* Little-endian NET_MSG_INPUT payload.  Returns the byte count, or 0. */
int NetInputBatchEncode(const tNetInputBatch *pBatch, uint8 *pBytes,
                        int iCapacity);
/* Rejects the whole message on any structural fault; pBatch is untouched. */
int NetInputBatchDecode(const uint8 *pBytes, int iLength,
                        tNetInputBatch *pBatch);

int NetInputFeedbackEncode(const tNetInputFeedback *pFeedback, uint8 *pBytes,
                           int iCapacity);
int NetInputFeedbackDecode(const uint8 *pBytes, int iLength,
                           tNetInputFeedback *pFeedback);

/* D9: clamp to defined bits and ranges.  Steering is limited to the range
   readuserdata can produce for this car's engine.  Returns nonzero when
   anything had to be changed. */
int NetInputClamp(tCarInputData *pInput, int iCar);

#endif
