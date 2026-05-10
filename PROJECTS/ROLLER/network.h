#ifndef _ROLLER_NETWORK_H
#define _ROLLER_NETWORK_H
//-------------------------------------------------------------------------------------------------
#include "types.h"
//-------------------------------------------------------------------------------------------------

#define PACKET_ID_TRANSMIT_INIT 0x686C6361
#define PACKET_ID_SEND_MES 0x686C6363
#define PACKET_ID_QUIT 0x686C6364
#define PACKET_ID_SINGLE 0x686C6365
#define PACKET_ID_PLAYER_CARS 0x686C6366
#define PACKET_ID_READY 0x686C6367
#define PACKET_ID_SEED 0x686C6368
#define PACKET_ID_PAUSE 0x686C6369
#define PACKET_ID_PLAYER_INFO 0x686C636A
#define PACKET_ID_RECORD 0x686C636B
#define PACKET_ID_NET_ERROR 0x686C636C
#define PACKET_ID_SYNC_ERROR 0x686C636D
#define PACKET_ID_GAME_ERROR 0x686C636E
#define PACKET_ID_NOCD 0x686C636F
#define PACKET_ID_RESYNC 0x686C6370
#define PACKET_ID_SLOT 0x686C6371
#define PACKET_ID_SEND_HERE 0x686C6372
#define PACKET_ID_MESSAGE 0x686C6373
#define PACKET_ID_MULTIPLE 0x686C6374

//-------------------------------------------------------------------------------------------------

typedef struct {
  uint32 uiUnk1;
  uint32 uiId;
  uint8 byConsoleNode;
  uint16 unFrameId;
} tSyncHeader;

//-------------------------------------------------------------------------------------------------

typedef struct {
  int32 address[4];
  char szPlayerName[9];
  // padding byte
  // padding byte
  // padding byte
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
} tTransmitInitPacket;

//-------------------------------------------------------------------------------------------------

typedef struct {
  float fRecordLap;
  char szRecordName[9];
  // padding byte
  uint16 unRecordCar;
} tRecordPacket;

//-------------------------------------------------------------------------------------------------

typedef struct {
  char szPlayerName[9];
  // padding byte
  // padding byte
  // padding byte
  int iPlayerCar;
  int iTrackLoad;
  int iGameType;
  int iManualControl;
  int iLevel;
  int iCompetitors;
  int iDamageLevel;
} tPlayerInfoPacket;

//-------------------------------------------------------------------------------------------------

typedef struct {
  char szMessage[32];
  char szPlayerName[9];
  // padding byte
  // padding byte
  // padding byte
  int iNetworkSlot;
} tMessagePacket;

//-------------------------------------------------------------------------------------------------

#pragma pack(push, 1)
typedef struct {
  uint32 uiData;
  int16 nChecksum;
} tDataPacket;
#pragma pack(pop)

//-------------------------------------------------------------------------------------------------

typedef struct {
  uint8 bNode[16];
} _NETBIOS_LOCAL_TARGET;

typedef struct {
  uint8 bNetwork[4];
  uint8 bNode[6];
} _IPX_INTERNET_ADDR;

typedef struct {
  _IPX_INTERNET_ADDR sInternetAddr;
  uint8 bImmediate[6];
} _IPX_LOCAL_TARGET;

typedef union {
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

//-------------------------------------------------------------------------------------------------

void Initialise_Network(int iSelectNetSlot);
void close_network();
void send_net_error();
void send_game_error();
void send_network_sync_error();
void send_resync(int iFrameNumber);
void send_nocd_error();
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
void receive_multiple();
void receive_all_singles();
void do_sync_stuff();
int TransmitInit();
void StartNode(int iNode);
void CheckNewNodes();
void FoundNodes();
void SendPlayerInfo();
void prepare_net_message(int iMessageMode, int iMessageNumber);
void SendAMessage();
void BroadcastNews();
void remove_messages(int iClear);
void reset_network(int iResetBroadcastMode);
void clear_network_game();
void reset_net_wait();
unsigned int send_broadcast(unsigned int uiBroadcastMode);
void send_here(int iNode, int iFrame);

//-------------------------------------------------------------------------------------------------
#endif