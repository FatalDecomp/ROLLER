#ifndef _ROLLER_NETWORK_H
#define _ROLLER_NETWORK_H
//-------------------------------------------------------------------------------------------------
#include "types.h"
//-------------------------------------------------------------------------------------------------

#ifdef NET_LEGACY_NETWORK_IMPLEMENTATION
#define network_initialise_begin NetLegacyImpl_network_initialise_begin
#define network_initialise_update NetLegacyImpl_network_initialise_update
#define network_initialise_active NetLegacyImpl_network_initialise_active
#define close_network NetLegacyImpl_close_network
#define send_net_error NetLegacyImpl_send_net_error
#define send_network_sync_error NetLegacyImpl_send_network_sync_error
#define send_resync NetLegacyImpl_send_resync
#define send_quit NetLegacyImpl_send_quit
#define send_ready NetLegacyImpl_send_ready
#define send_record_to_master NetLegacyImpl_send_record_to_master
#define send_record_to_slaves NetLegacyImpl_send_record_to_slaves
#define send_mes NetLegacyImpl_send_mes
#define send_seed NetLegacyImpl_send_seed
#define send_single NetLegacyImpl_send_single
#define send_pause NetLegacyImpl_send_pause
#define send_slot NetLegacyImpl_send_slot
#define transmitpausetoslaves NetLegacyImpl_transmitpausetoslaves
#define send_multiple NetLegacyImpl_send_multiple
#define receive_multiple NetLegacyImpl_receive_multiple
#define receive_all_singles NetLegacyImpl_receive_all_singles
#define do_sync_stuff NetLegacyImpl_do_sync_stuff
#define TransmitInit NetLegacyImpl_TransmitInit
#define CheckNewNodes NetLegacyImpl_CheckNewNodes
#define FoundNodes NetLegacyImpl_FoundNodes
#define SendPlayerInfo NetLegacyImpl_SendPlayerInfo
#define SendAMessage NetLegacyImpl_SendAMessage
#define BroadcastNews NetLegacyImpl_BroadcastNews
#define network_broadcast_wait_start \
  NetLegacyImpl_network_broadcast_wait_start
#define network_broadcast_wait_update \
  NetLegacyImpl_network_broadcast_wait_update
#define network_broadcast_wait_active \
  NetLegacyImpl_network_broadcast_wait_active
#define remove_messages NetLegacyImpl_remove_messages
#define reset_network NetLegacyImpl_reset_network
#define clear_network_game NetLegacyImpl_clear_network_game
#define reset_net_wait NetLegacyImpl_reset_net_wait
#define send_broadcast NetLegacyImpl_send_broadcast
#endif

#define PACKET_ID_TRANSMIT_INIT 0x686C6361
#define PACKET_ID_SEND_MES      0x686C6363
#define PACKET_ID_QUIT          0x686C6364
#define PACKET_ID_SINGLE        0x686C6365
#define PACKET_ID_PLAYER_CARS   0x686C6366
#define PACKET_ID_READY         0x686C6367
#define PACKET_ID_SEED          0x686C6368
#define PACKET_ID_PAUSE         0x686C6369
#define PACKET_ID_PLAYER_INFO   0x686C636A
#define PACKET_ID_RECORD        0x686C636B
#define PACKET_ID_NET_ERROR     0x686C636C
#define PACKET_ID_SYNC_ERROR    0x686C636D
#define PACKET_ID_GAME_ERROR    0x686C636E
#define PACKET_ID_NOCD          0x686C636F
#define PACKET_ID_RESYNC        0x686C6370
#define PACKET_ID_SLOT          0x686C6371
#define PACKET_ID_SEND_HERE     0x686C6372
#define PACKET_ID_MESSAGE       0x686C6373
#define PACKET_ID_MULTIPLE      0x686C6374

#define NETWORK_COMMUNITY_TRACK_FILENAME (ROLLER_MAX_PATH - 16)

//-------------------------------------------------------------------------------------------------

typedef struct
{
  uint32 uiUnk1;
  uint32 uiId;
  uint8 byConsoleNode;
  uint16 unFrameId;
} tSyncHeader;

//-------------------------------------------------------------------------------------------------

typedef struct {
  int32 address[4];
  char szPlayerName[9];
  //padding byte
  //padding byte
  //padding byte
  int32 iNetworkOn;
  int32 iMyAge;
  int32 iCarIdx;
  int32 iTrackLoad;
  int32 iGameType;
  int32 iManualControl;
  int32 iLevelFlags;
  int32 iCompetitors;
  int32 iDamageLevel;
  int32 iStartPressed;
  int32 iTimeToStart;
  int32 iFalseStart;
  int32 iTextureMode;
  int32 iNetworkChampOn;
  int32 iNetworkSlot;
  char default_names[16][9];
  char szCommunityTrack[NETWORK_COMMUNITY_TRACK_FILENAME];
  uint32 uiCommunityTrackCRC;
  uint32 uiTrackCRC;
} tTransmitInitPacket;

//-------------------------------------------------------------------------------------------------

typedef struct
{
  float fRecordLap;
  char szRecordName[9];
  //padding byte
  uint16 unRecordCar;
} tRecordPacket;

//-------------------------------------------------------------------------------------------------

typedef struct
{
  char szPlayerName[9];
  //padding byte
  //padding byte
  //padding byte
  int iPlayerCar;
  int iTrackLoad;
  int iGameType;
  int iManualControl;
  int iLevel;
  int iCompetitors;
  int iDamageLevel;
  char szCommunityTrack[NETWORK_COMMUNITY_TRACK_FILENAME];
  uint32 uiCommunityTrackCRC;
  uint32 uiTrackCRC;
} tPlayerInfoPacket;

//-------------------------------------------------------------------------------------------------

typedef struct
{
  char szMessage[32];
  char szPlayerName[9];
  //padding byte
  //padding byte
  //padding byte
  int iNetworkSlot;
} tMessagePacket;

//-------------------------------------------------------------------------------------------------

#pragma pack(push, 1)
typedef struct
{
  uint32 uiData;
  int16 nChecksum;
} tDataPacket;
#pragma pack(pop)

//-------------------------------------------------------------------------------------------------

typedef struct
{
  uint8 bNode[16];
} _NETBIOS_LOCAL_TARGET;

typedef struct
{
  uint8 bNetwork[4];
  uint8 bNode[6];
} _IPX_INTERNET_ADDR;

typedef struct
{
  _IPX_INTERNET_ADDR sInternetAddr;
  uint8 bImmediate[6];
} _IPX_LOCAL_TARGET;

typedef union
{
  _IPX_LOCAL_TARGET sIPX;
  _NETBIOS_LOCAL_TARGET sNETBIOS;
} _NETNOW_NODE_ADDR;

//-------------------------------------------------------------------------------------------------

extern int sync_errors;
extern int net_type;
extern int slave_pause;
extern int net_started;
extern int next_resync;
extern _NETNOW_NODE_ADDR gamers_address[4][16];
extern int gamers_playing[4];
extern char gamers_names[4][144];
extern uint32 test_mini[2];
extern int test_multiple[16];
extern tRecordPacket p_record;
extern int net_players[16];
extern int16 player_checks[512][16];
extern int address[64];
extern int player_ready[16];
extern int16 player_syncs[16];
extern int syncptr;
extern int syncleft;
extern int syncnode;
extern int syncframe;
extern int received_seed;
extern int received_records;
extern int frame_number;
extern int start_multiple;
extern tSyncHeader p_header;
extern int test_seed;
extern int resync;
extern int message_received;
extern int my_age;
extern int message_number;
extern int message_node;
extern int read_check;
extern int write_check;
extern int test;
extern int network_mistake;
extern int pauser;
extern uint32 broadcast_mode;
extern int message_sent;
extern int random_seed;
extern int dostopsamps;
extern int lost_message;
extern int duff_message;
extern int check_set;
extern int master;
extern tSyncHeader in_header;
extern int active_nodes;
extern int net_quit;
extern tDataPacket slave_data;
extern char p_data[14];
extern char received_message[14];
extern int16 wConsoleNode;
extern int g_iNetworkTrackFileCRCMismatch;

//-------------------------------------------------------------------------------------------------

void network_initialise_begin(int iSelectNetSlot);
int network_initialise_update(void);
int network_initialise_active(void);
void close_network();
void send_net_error();
void send_network_sync_error();
void send_resync(int iFrameNumber);
void send_quit();
void send_ready();
void send_record_to_master(int iRecordIdx);
void send_record_to_slaves(int iRecordIdx);
void send_mes(int iNetworkMessageIdx, int iNode);
void send_seed(int iRandomSeed);
void send_single(uint32 uiData);
void send_pause();
void send_slot();
void transmitpausetoslaves();
void send_multiple();
int receive_multiple(); // returns number of copy_multiple slots received
void receive_all_singles();
void do_sync_stuff();
int TransmitInit();
void CheckNewNodes();
void FoundNodes();
void SendPlayerInfo();
void SendAMessage();
void BroadcastNews();
void network_broadcast_wait_start(int iBroadcastMode, int iRepeatCount);
int network_broadcast_wait_update(void);
int network_broadcast_wait_active(void);
void remove_messages(int iClear);
void reset_network(int iResetBroadcastMode);
void clear_network_game();
void reset_net_wait();
unsigned int send_broadcast(unsigned int uiBroadcastMode);

//-------------------------------------------------------------------------------------------------
#endif
