#include "network.h"
#include "sound.h"
#include "3d.h"
#include "func3.h"
#include "frontend.h"
#include "moving.h"
#include "car.h"
#include "control.h"
#include "colision.h"
#include "roller.h"
#include "rollercomms.h"
//-------------------------------------------------------------------------------------------------

int sync_errors = 0;        //000A6100
int net_type = 1;           //000A6104
int slave_pause = 0;        //000A6108
int net_started = 0;        //000A610C
int next_resync = 0;        //000A6110
_NETNOW_NODE_ADDR gamers_address[4][16]; //00178070
int gamers_playing[4];      //00178470
char gamers_names[4][144];  //00178480
uint32 test_mini[2];        //001786C0
int test_multiple[16];      //001786C8
tRecordPacket p_record;     //00178708
int net_players[16];        //00178718
int16 player_checks[512][16]; //00178758
int address[64];            //0017C758
int player_ready[16];       //0017C858
int16 player_syncs[16];     //0017C898
int syncptr;                //0017C928
int syncleft;               //0017C92C
int syncnode;               //0017C930
int syncframe;              //0017C934
int received_seed;          //0017C938
int received_records;       //0017C93C
int frame_number;           //0017C940
int start_multiple;         //0017C944
tSyncHeader p_header;       //0017C948
int test_seed;              //0017C958
int resync;                 //0017C95C
int message_received;       //0017C960
int my_age;                 //0017C964
int message_number;         //0017C968
int message_node;           //0017C96C
int read_check;             //0017C970
int write_check;            //0017C974
int test;                   //0017C978
int network_mistake;        //0017C97C
int pauser;                 //0017C980
uint32 broadcast_mode;      //0017C984
int message_sent;           //0017C988
int random_seed;            //0017C98C
int dostopsamps;            //0017C990
int lost_message;           //0017C994
int duff_message;           //0017C998
int check_set;              //0017C99C
int master;                 //0017C9A0
tSyncHeader in_header;      //0017C9A4
int active_nodes;           //0017C9B0
int net_quit;               //0017C9B4
tDataPacket slave_data;     //0017C9B8
char p_data[14];            //0017C9BE
char received_message[14];  //0017C9CC
int16 wConsoleNode;         //0017C9DA

//-------------------------------------------------------------------------------------------------
//0004EB30
void Initialise_Network(int iSelectNetSlot)
{
  int iResetNetwork; // [esp+0h] [ebp-1Ch]

  iResetNetwork = 0;
  active_nodes = 0;
  net_quit = 0;
  master = 0xFFFFFD65;
  wConsoleNode = 0;
  network_on = 0;
  net_loading = 0;
  memset(copy_multiple, 0, sizeof(copy_multiple));

  for (int i = 0; i < 16; ++i) {
    player_ready[i] = 0;
    for (int j = 0; j < 512; ++j) {
      player_checks[j][i] = -1;
    }
  }

  message_node = -1;
  message_number = -1;
  message_received = -1;
  message_sent = -1;
  time_to_start = 0;
  received_seed = 0;
  frame_number = 0;
  active_nodes = 0;

  if ((net_type || !net_started) && !ROLLERCommsInitSystem(20u))
    iResetNetwork = -1;
  if (!iResetNetwork) {
    I_Quit = 0;
    broadcast_mode = 0;
    writeptr = 0;
    readptr = 0;
    remove_messages(-1);
    if (!net_type)
      net_started = -1;

    for (int i = 0; i < 16; ++i) {
      net_players[i] = 0;
      player_started[i] = 0;
    }

    players_waiting = 0;
    if (!network_champ_on)
      Players_Cars[player1_car] = -1;
    my_age = 0;
    ROLLERCommsGetNetworkAddr(address);
    ROLLERCommsGetConsoleNode();
    network_on = 1;
    if (iSelectNetSlot || network_slot < 0)
      broadcast_mode = 0xFFFFFE38;              // force slave broadcast?
    else
      broadcast_mode = -1;
    tick_on = -1;
    while (broadcast_mode)
      UpdateSDL(); //added by ROLLER
    ROLLERCommsSortNodes();
    received_records = 1;
    switch_sets = 0;
    switch_same = 0;
    switch_types = 0;
  }
}

//-------------------------------------------------------------------------------------------------
//0004ED70
void close_network()
{
  int iNode; // edx
  int i; // edx

  network_slot = -1;
  if (network_on) {
    tick_on = 0;
    while (ROLLERCommsPostListen())
      ;
    iNode = 0;
    wConsoleNode = ROLLERCommsNetAddrToNode(address);
    while (iNode < wConsoleNode) {
      if (ROLLERCommsDeleteNode(0)) {
        printf("FAILED TO DELETE NODE a (%d)!!!\n", iNode);
        doexit();
      }
      ROLLERCommsSortNodes();
      ++iNode;
    }
    for (i = wConsoleNode + 1; i < network_on; ++i) {
      if (ROLLERCommsDeleteNode(1)) {
        printf("FAILED TO DELETE NODE b (%d)!!!\n", i);
        doexit();
      }
      ROLLERCommsSortNodes();
    }
    ROLLERCommsGetActiveNodes();
    wConsoleNode = ROLLERCommsNetAddrToNode(address);
    if (exiting)
      goto LABEL_18;
  } else if ((net_started || net_type) && exiting) {
  LABEL_18:
    ;
    ROLLERCommsSetType(1);
    ROLLERCommsUnInitSystem();
    ROLLERCommsSetType(0);
    ROLLERCommsUnInitSystem();
  }

  for (int i = 0; i < 16; ++i)
      net_players[i] = -667;

  if (network_on && !frontend_on) {
    Players_Cars[0] = my_car;
    name_copy(player_names[0], my_name);
    manual_control[0] = my_control;
    player_invul[0] = my_invul;
  }
  if (network_on)
    player_type = 0;
  network_on = 0;
  player1_car = 0;
  active_nodes = 0;
  wConsoleNode = 0;
  received_seed = 0;
  frame_number = 0;
  time_to_start = 0;
  if (game_type != 1 && replaytype != 2)
    network_champ_on = 0;
  broadcast_mode = 0;
  clear_network_game();
  if (net_type)
    ROLLERCommsUnInitSystem();
}

//-------------------------------------------------------------------------------------------------
//0004EF60
void send_net_error()
{
  int i; // esi

  if (network_on) {
    p_header.byConsoleNode = (uint8)wConsoleNode;
    p_header.uiId = PACKET_ID_NET_ERROR;
    if (wConsoleNode == master) {
      for (i = 0; i < network_on; ++i) {
        if (i != wConsoleNode) {
          ROLLERCommsSendData(&p_header, sizeof(tSyncHeader), p_data, 0, i);
        }
      }
    }
  }
}

//-------------------------------------------------------------------------------------------------
//0004EFD0
void send_game_error()
{
  int iDataSent = 0; // eax
  int i; // esi

  if (network_on) {
    p_header.byConsoleNode = (uint8)wConsoleNode;
    p_header.uiId = PACKET_ID_GAME_ERROR;
    network_mistake = -1;
    if (wConsoleNode == master) {
      for (i = 0; i < network_on; ++i) {
        if (i != wConsoleNode) {
          iDataSent = ROLLERCommsSendData(&p_header, sizeof(tSyncHeader), p_data, 0, i);
          if (!iDataSent)
            i = network_on;
        }
      }
      if (iDataSent)
        network_error = 666;
    }
  }
}

//-------------------------------------------------------------------------------------------------
//0004F070
void send_network_sync_error()
{
  int i; // esi

  if (network_on) {
    p_header.byConsoleNode = (uint8)wConsoleNode;
    p_header.uiId = PACKET_ID_SYNC_ERROR;
    if (wConsoleNode == master) {
      for (i = 0; i < network_on; ++i) {
        if (i != wConsoleNode) {
          ROLLERCommsSendData(&p_header, sizeof(tSyncHeader), p_data, 0, i);
        }
      }
    } else {
      ROLLERCommsSendData(&p_header, sizeof(tSyncHeader), p_data, 0, master);
    }
  }
}

//-------------------------------------------------------------------------------------------------
//0004F100
void send_resync(int iFrameNumber)
{
  if (network_on) {
    resync = iFrameNumber;
    p_header.byConsoleNode = (uint8)wConsoleNode;
    p_header.uiId = PACKET_ID_RESYNC;
    ROLLERCommsSendData(&p_header, sizeof(tSyncHeader), &resync, sizeof(int32), master);
  }
}

//-------------------------------------------------------------------------------------------------
//0004F150
void send_nocd_error()
{
  int i; // esi

  if (network_on) {
    p_header.byConsoleNode = (uint8)wConsoleNode;
    p_header.uiId = PACKET_ID_NOCD;
    if (wConsoleNode == (int16)master) {
      for (i = 0; i < network_on; ++i) {
        if (i != wConsoleNode) {
          ROLLERCommsSendData(&p_header, sizeof(tSyncHeader), p_data, 0, i);
        }
      }
    } else {
      ROLLERCommsSendData(&p_header, sizeof(tSyncHeader), p_data, 0, master);
    }
  }
}

//-------------------------------------------------------------------------------------------------
//0004F1E0
void send_quit()
{
  int i; // esi

  if (network_on) {
    p_header.byConsoleNode = (uint8)wConsoleNode;
    p_header.uiId = PACKET_ID_QUIT;
    if (wConsoleNode == master) {
      for (i = 0; i < network_on; ++i) {
        if (i != wConsoleNode) {
          while (!ROLLERCommsSendData(&p_header, sizeof(tSyncHeader), p_data, 0, i))
            UpdateSDL(); //added by ROLLER
        }
      }
      net_quit = -1;
    } else {
      while (!ROLLERCommsSendData(&p_header, sizeof(tSyncHeader), p_data, 0, master))
        UpdateSDL(); //added by ROLLER
    }
  }
}

//-------------------------------------------------------------------------------------------------
//0004F290
void send_ready()
{
  int iDataSent; // eax

  if (network_on) {
    p_header.byConsoleNode = (uint8)wConsoleNode;
    if (localCD)
      p_header.uiId = PACKET_ID_READY;
    else
      p_header.uiId = PACKET_ID_NOCD;
    if (wConsoleNode != master || player_ready[master]) {
      do {
        //_disable();
        iDataSent = ROLLERCommsSendData(&p_header, sizeof(tSyncHeader), p_data, 0, master);
        //_enable();
        UpdateSDL(); //added by ROLLER
      } while (!iDataSent);
    } else {
      player_ready[master] = -1;
      active_nodes++;
    }
  }
}

//-------------------------------------------------------------------------------------------------
//0004F350
void send_record_to_master(int iRecordIdx)
{
  if (network_on) {
    p_header.byConsoleNode = (uint8)wConsoleNode;
    p_header.uiId = PACKET_ID_RECORD;
    p_record.fRecordLap = RecordLaps[iRecordIdx];
    strncpy(p_record.szRecordName, RecordNames[iRecordIdx], sizeof(p_record.szRecordName));
    p_record.unRecordCar = RecordCars[iRecordIdx];

    while (!ROLLERCommsSendData(&p_header, sizeof(tSyncHeader), &p_record, sizeof(tRecordPacket), master))
      UpdateSDL(); //added by ROLLER
  }
}

//-------------------------------------------------------------------------------------------------
//0004F400
void send_record_to_slaves(int iRecordIdx)
{
  if (network_on) {
    p_header.byConsoleNode = (uint8)wConsoleNode;
    p_header.uiId = PACKET_ID_RECORD;
    p_record.fRecordLap = RecordLaps[iRecordIdx];
    strncpy(p_record.szRecordName, RecordNames[iRecordIdx], sizeof(p_record.szRecordName));
    p_record.unRecordCar = RecordCars[iRecordIdx];

    for (int i = 0; i < network_on; ++i) {
      iRecordIdx = wConsoleNode;
      if (wConsoleNode != i) {
        while (!ROLLERCommsSendData(&p_header, sizeof(tSyncHeader), &p_record, sizeof(tRecordPacket), i)) {
          UpdateSDL(); //added by ROLLER
        }
      }
    }
  }
}

//-------------------------------------------------------------------------------------------------
//0004F4E0
void send_mes(int iNetworkMessageIdx, int iNode)
{
  char *szNetworkMessage; // eax
  int iNodeIdx; // esi
  int iNetPlayers; // edi

  if (network_on) {
    szNetworkMessage = network_messages[iNetworkMessageIdx];
    if (*szNetworkMessage) {
      message_sent = iNetworkMessageIdx;

      strncpy(p_data, network_messages[iNetworkMessageIdx], sizeof(p_data));

      // setup header
      p_header.uiId = PACKET_ID_SEND_MES;
      p_header.byConsoleNode = (uint8)wConsoleNode;
      if (iNode == -1) {
        iNodeIdx = 0;
        if (network_on > 0) {
          iNetPlayers = 0;
          do {
            if (iNodeIdx != wConsoleNode && net_players[iNetPlayers]) {
              while (!ROLLERCommsSendData(&p_header, sizeof(tSyncHeader), p_data, sizeof(p_data), iNodeIdx))
                UpdateSDL(); //added by ROLLER
            }
            ++iNetPlayers;
            ++iNodeIdx;
          } while (iNodeIdx < network_on);
        }
      } else {
        while (!ROLLERCommsSendData(&p_header, sizeof(tSyncHeader), p_data, sizeof(p_data), iNode))
          UpdateSDL(); //added by ROLLER
      }
    } else {
      message_sent = 4;
    }
  }
}

//-------------------------------------------------------------------------------------------------
//0004F5E0
void send_seed(int iRandomSeed)
{
  if (network_on) {
    test_seed = iRandomSeed;
    p_header.uiId = PACKET_ID_SEED;
    p_header.byConsoleNode = (uint8)wConsoleNode;
    for (int i = 0;  i < network_on; ++i) {
      if (i != wConsoleNode) {
        while (!ROLLERCommsSendData(&p_header, 12, &test_seed, 4, i))
          UpdateSDL(); //added by ROLLER
      }
    }

    srand(iRandomSeed);
    random_seed = iRandomSeed;
  }
}

//-------------------------------------------------------------------------------------------------
//0004F660
void send_single(uint32 uiData)
{
  if (!network_on) return;

  // Initialize header and data packet
  p_header.uiId = PACKET_ID_SINGLE;
  p_header.byConsoleNode = (uint8)player1_car;

  slave_data.uiData = uiData;

  // Calculate checksum or sequence number if valid read position
  if (read_check == write_check || read_check < 0) {
    // Invalid state - set default value
    slave_data.nChecksum = 0xFE00;
  } else {
    // Compute special value using player_checks array
    short nChecksum = player_checks[read_check][player1_car];

    // Create composite value:
    // - Lower 7 bits of checksum shifted to bits 9-15
    // - Bit 8 from read_check
    // - Lower 8 bits from read_check
    unsigned int uiComposite = (nChecksum & 0x7F) << 9;
    uiComposite |= (read_check & 0x100);  // Bit 8
    uiComposite |= (read_check & 0xFF);    // Bits 0-7

    slave_data.nChecksum = (short)uiComposite;
  }

  //TODO network
  ROLLERCommsSendData(&p_header, sizeof(p_header), &slave_data, sizeof(tDataPacket), master);

  // Mark check as processed
  player_checks[read_check][player1_car] = -1;  // 0xFFFF

  // Advance read position if valid and not caught up
  if (read_check != write_check && read_check >= 0) {
    read_check = (read_check + 1) & 0x1FF;  // Wrap around at 512
  }
}

//-------------------------------------------------------------------------------------------------
//0004F750
void send_pause()
{
  tSyncHeader syncHeader; // [esp+0h] [ebp-1Ch] BYREF
  bool bPaused; // [esp+Ch] [ebp-10h] BYREF

  if (network_on) {
    syncHeader.byConsoleNode = (uint8)wConsoleNode;
    syncHeader.uiId = PACKET_ID_PAUSE;
    bPaused = paused == 0;
    while (!ROLLERCommsSendData(&syncHeader, sizeof(tSyncHeader), &bPaused, sizeof(bool), master))
      UpdateSDL(); //added by ROLLER
  }
}

//-------------------------------------------------------------------------------------------------
//0004F7B0
void send_slot()
{
  tSyncHeader syncHeader;
  int iSlot;// = network_slot?

  if (network_on) {
    syncHeader.byConsoleNode = (uint8)network_slot;
    syncHeader.uiId = PACKET_ID_SLOT;
    while (!ROLLERCommsSendData(&syncHeader, sizeof(tSyncHeader), &iSlot, sizeof(int), 21))
      UpdateSDL(); //added by ROLLER
  }
}

//-------------------------------------------------------------------------------------------------
//0004F800
void transmitpausetoslaves()
{
  int iNode; // esi
  int iNetPlayerIdx; // edi
  tSyncHeader syncHeader; // [esp+0h] [ebp-28h] BYREF
  bool bPaused; // [esp+Ch] [ebp-1Ch] BYREF

  if (network_on) {
    syncHeader.byConsoleNode = pauser;
    syncHeader.uiId = PACKET_ID_PAUSE;
    bPaused = paused;
    iNode = 0;
    if (network_on > 0) {
      iNetPlayerIdx = 0;
      do {
        if (iNode != master && net_players[iNetPlayerIdx]) {
          while (!ROLLERCommsSendData(&syncHeader, sizeof(tSyncHeader), &bPaused, sizeof(bool), iNode))
            UpdateSDL(); //added by ROLLER
        }
        ++iNetPlayerIdx;
        ++iNode;
      } while (iNode < network_on);
    }
  }
}

//-------------------------------------------------------------------------------------------------
//0004F880
void send_multiple()
{
  int iCarIdx; // eax
  int *pTestMultiple; // esi
  tCopyData iCopyMultipleVal2; // edi
  int iNode; // esi
  int iNodeIsValid; // eax
  int iNode2; // edi
  int iSendCopyMultiple; // edx
  tCopyData *pCopyMultiple; // ebx
  int *pTestMini; // esi
  int i; // eax
  tCopyData iCopyMultipleVal; // edx
  int iNode3; // edi
  int iNode4; // edi

  if (network_on) {
    p_header.byConsoleNode = (uint8)wConsoleNode;
    p_header.uiId = PACKET_ID_PLAYER_CARS;                 // PACKET_ID_PLAYER_CARS
    p_header.unFrameId = frame_number;
    if (net_type) {
      iSendCopyMultiple = -1;
      if (memcmp((const char *)&copy_multiple[((int16)writeptr - 1) & 0x1FF][player_to_car[0]], (const char *)&copy_multiple[writeptr][player_to_car[0]], 4))
        iSendCopyMultiple = 0;
      pCopyMultiple = copy_multiple[((int16)writeptr - 1) & 0x1FF];
      if (memcmp((const char *)&pCopyMultiple[player_to_car[1]], (const char *)&copy_multiple[writeptr][player_to_car[1]], 4))
        iSendCopyMultiple = 0;
      if (!frame_number)
        iSendCopyMultiple = 0;
      if (iSendCopyMultiple) {
        p_header.uiId = PACKET_ID_MULTIPLE;             // PACKET_ID_MULTIPLE
        iNode = abs(start_multiple);
        iNodeIsValid = -1;
        if (iNode >= 0) {
          iNode4 = iNode;
          do {
            if (!iNodeIsValid)
              break;
            if (iNode == master || !net_players[iNode4])
              iNodeIsValid = -1;
            else {
              iNodeIsValid = ROLLERCommsSendData(&p_header, sizeof(tSyncHeader), pCopyMultiple, 1, iNode);
            }
            if (iNodeIsValid) {
              --iNode4;
              --iNode;
            }
          } while (iNode >= 0);
        }
      } else {
        pTestMini = test_mini;
        for (i = 0; i < 2; ++i) {
          ++pTestMini;
          iCopyMultipleVal = copy_multiple[writeptr][player_to_car[i]];
          *(pTestMini - 1) = iCopyMultipleVal.uiFullData;
        }
        iNode = abs(start_multiple);
        iNodeIsValid = -1;
        if (iNode >= 0) {
          iNode3 = iNode;
          do {
            if (!iNodeIsValid)
              break;
            if (iNode == master || !net_players[iNode3])
              iNodeIsValid = -1;
            else {
              iNodeIsValid = ROLLERCommsSendData(&p_header, sizeof(tSyncHeader), test_mini, 8, iNode);
            }
            if (iNodeIsValid) {
              --iNode3;
              --iNode;
            }
          } while (iNode >= 0);
        }
      }
    } else {
      iCarIdx = 0;
      if (numcars > 0) {
        pTestMultiple = test_multiple;
        do {
          iCopyMultipleVal2 = copy_multiple[writeptr][iCarIdx++];
          *pTestMultiple++ = iCopyMultipleVal2.uiFullData;
        } while (iCarIdx < numcars);
      }
      iNode = abs(start_multiple);
      iNodeIsValid = -1;
      if (iNode >= 0) {
        iNode2 = iNode;
        do {
          if (!iNodeIsValid)
            break;
          iNodeIsValid = iNode == master || !net_players[iNode2] ? -1 : ROLLERCommsSendData(&p_header, sizeof(tSyncHeader), test_multiple, 64, iNode);
          if (iNodeIsValid) {
            --iNode2;
            --iNode;
          }
        } while (iNode >= 0);
      }
    }
    if (iNodeIsValid) {
      start_multiple = network_on - 1;
      ++frame_number;
    } else {
      start_multiple = -iNode;
    }
  }
}

//-------------------------------------------------------------------------------------------------
//0004FBA0
void receive_multiple()
{
  tPlayerInfoPacket playerInfoPacket; // [esp+0h] [ebp-60h] BYREF
  char szMesPacket[14]; // [esp+28h] [ebp-38h] BYREF
  void *pPacket; // [esp+38h] [ebp-28h] BYREF
  int iPaused; // [esp+40h] [ebp-20h] BYREF
  int *pSeed; // [esp+44h] [ebp-1Ch]

  if (network_on && !net_quit) {
    pSeed = &test_seed;
    while (ROLLERCommsGetHeader(&in_header, 12, &pPacket)) {
      switch (in_header.uiId) {
        case PACKET_ID_SEND_MES:
          ROLLERCommsGetBlock(pPacket, szMesPacket, 14);
          message_received = in_header.byConsoleNode;
          strncpy(received_message, szMesPacket, sizeof(received_message));
          goto LABEL_54;
        case PACKET_ID_QUIT:
          net_quit = -1;
          ROLLERCommsPostListen();
          continue;
        case PACKET_ID_PLAYER_CARS:
          if (net_type) {
            if (network_on > 0) {
              for (int i = 0; i < network_on; ++i) {
                player_ready[i] = -1;
              }
            }
            active_nodes = network_on;
            net_loading = 0;
            ROLLERCommsGetBlock(pPacket, test_mini, 8);
            for (int i = 0; i < 2; ++i) {
              copy_multiple[writeptr][player_to_car[i]].uiFullData = test_mini[i];
            }
          } else {
            if (network_on > 0) {

              for (int i = 0; i < network_on; ++i) {
                player_ready[i] = -1;
              }
            }
            active_nodes = network_on;
            net_loading = 0;
            ROLLERCommsGetBlock(pPacket, &copy_multiple[writeptr], 64);
          }
          network_timeout = frames;
          ++ticks_received;
          if (in_header.unFrameId == frame_number) {
            ++frame_number;
            writeptr = (writeptr + 1) & 0x1FF;
            next_resync = -1;
            ROLLERCommsPostListen();
            continue;
          }
          if (in_header.unFrameId <= frame_number)
            goto LABEL_54;
          ++lost_message;
          ROLLERCommsPostListen();
          if (in_header.unFrameId - frame_number > 500) {
            send_network_sync_error();
            network_sync_error = -1;
            ROLLERCommsPostListen();
            continue;
          }
          if (frames <= next_resync)
            goto LABEL_54;
          if (ROLLERCommsGetType())
            ROLLERclrrx();
          goto LABEL_17;
        case PACKET_ID_SEED:
          ROLLERCommsGetBlock(pPacket, &test_seed, 4);
          srand(*pSeed);
          random_seed = *pSeed;
          received_seed = -1;
          ROLLERCommsPostListen();
          continue;
        case PACKET_ID_PAUSE:
          ROLLERCommsGetBlock(pPacket, &iPaused, 4);
          if (paused != iPaused && iPaused)
            dostopsamps = -1;
          paused = iPaused;
          if (!iPaused)
            goto LABEL_54;
          pauser = in_header.byConsoleNode;
          ROLLERCommsPostListen();
          continue;
        case PACKET_ID_PLAYER_INFO:
          ROLLERCommsGetBlock(pPacket, &playerInfoPacket, sizeof(tPlayerInfoPacket));
          Players_Cars[in_header.byConsoleNode] = playerInfoPacket.iPlayerCar;
          check_cars();
          name_copy(player_names[in_header.byConsoleNode], playerInfoPacket.szPlayerName);
          TrackLoad = playerInfoPacket.iTrackLoad;
          game_type = playerInfoPacket.iGameType;
          competitors = playerInfoPacket.iCompetitors;
          level = playerInfoPacket.iLevel;
          damage_level = playerInfoPacket.iDamageLevel & 0xF;
          if ((playerInfoPacket.iDamageLevel & 0x40) != 0)
            player_invul[in_header.byConsoleNode] = -1;
          else
            player_invul[in_header.byConsoleNode] = 0;
          manual_control[in_header.byConsoleNode] = playerInfoPacket.iManualControl;
          ROLLERCommsPostListen();
          continue;
        case PACKET_ID_NET_ERROR:
          network_error = 999;
          ROLLERCommsPostListen();
          continue;
        case PACKET_ID_SYNC_ERROR:
          goto LABEL_13;
        case PACKET_ID_GAME_ERROR:
          network_error = 666;
          ROLLERCommsPostListen();
          continue;
        case PACKET_ID_NOCD:
          //cd_error = -1;
          ROLLERCommsPostListen();
          continue;
        case PACKET_ID_SEND_HERE:
          load_times[in_header.byConsoleNode] = in_header.unFrameId;
          ROLLERCommsPostListen();
          continue;
        case PACKET_ID_MULTIPLE:
          if (in_header.unFrameId == frame_number) {

            int iPrevIdx = (writeptr - 1) & 0x1FF;
            int iCar0 = player_to_car[0];
            int iCar1 = player_to_car[1];
            copy_multiple[writeptr][iCar0] = copy_multiple[iPrevIdx][iCar0];
            copy_multiple[writeptr][iCar1] = copy_multiple[iPrevIdx][iCar1];

            network_timeout = frames;
            ++ticks_received;
            ++frame_number;
            writeptr = (writeptr + 1) & 0x1FF;
            next_resync = -1;
            ROLLERCommsPostListen();
            continue;
          }
          if (in_header.unFrameId <= frame_number)
            goto LABEL_54;
          ++lost_message;
          ROLLERCommsPostListen();
          if (in_header.unFrameId - frame_number <= 500) {
            if (frames <= next_resync) {
            LABEL_54:
              ;
              ROLLERCommsPostListen();
            } else {
              if (ROLLERCommsGetType()) {
                ROLLERclrrx();
              }
            LABEL_17:
              ++sync_errors;
              send_resync(frame_number);
              next_resync = frames + 16;
              ROLLERCommsPostListen();
            }
          } else {
            send_network_sync_error();
          LABEL_13:
            network_sync_error = -1;
            ROLLERCommsPostListen();
          }
          break;
        default:
          ++duff_message;
          goto LABEL_54;
      }
    }
  }
}

//-------------------------------------------------------------------------------------------------
//000501B0
void receive_all_singles()
{
  int byConsoleNode; // eax
  tTransmitInitPacket transmitInitPacket; // [esp+0h] [ebp-140h] BYREF
  tPlayerInfoPacket playerInfoPacket; // [esp+E8h] [ebp-58h] BYREF
  char szMesPacket[14]; // [esp+110h] [ebp-30h] BYREF
  void *pData; // [esp+120h] [ebp-20h] BYREF
  int iFrame = 0;

  if (network_on) {
    while (ROLLERCommsGetHeader(&in_header, sizeof(tSyncHeader), &pData)) {
      byConsoleNode = in_header.byConsoleNode;
      if (net_players[car_to_player[byConsoleNode]]) {
        switch (in_header.uiId) {
          case PACKET_ID_TRANSMIT_INIT:
            ROLLERCommsGetBlock(pData, &transmitInitPacket, sizeof(tTransmitInitPacket));
            if (transmitInitPacket.iTimeToStart != 0xFFFFFE38)
              goto LABEL_31;
            send_slot();
            ROLLERCommsPostListen();
            continue;
          case PACKET_ID_SEND_MES:
            ROLLERCommsGetBlock(pData, szMesPacket, 14);
            message_received = in_header.byConsoleNode;

            strncpy(received_message, szMesPacket, sizeof(received_message));

            goto LABEL_31;
          case PACKET_ID_QUIT:
            ROLLERCommsPostListen();
            send_quit();
            ROLLERCommsPostListen();
            continue;
          case PACKET_ID_SINGLE:
            ROLLERCommsGetBlock(pData, &slave_data, 6);
            copy_multiple[writeptr][in_header.byConsoleNode].uiFullData = slave_data.uiData;
            net_time[car_to_player[in_header.byConsoleNode]] = frames;
            ROLLERCommsPostListen();
            continue;
          case PACKET_ID_READY:
            if (player_ready[in_header.byConsoleNode])
              goto LABEL_31;
            ++active_nodes;
            player_ready[byConsoleNode] = -1;
            netCD = -1;
            ROLLERCommsPostListen();
            continue;
          case PACKET_ID_PAUSE:
            slave_pause = -1;
            pauser = in_header.byConsoleNode;
            ROLLERCommsPostListen();
            continue;
          case PACKET_ID_PLAYER_INFO: 
            ROLLERCommsGetBlock(pData, &playerInfoPacket, sizeof(tPlayerInfoPacket));
            Players_Cars[in_header.byConsoleNode] = playerInfoPacket.iPlayerCar;
            check_cars();
            name_copy(player_names[in_header.byConsoleNode], playerInfoPacket.szPlayerName);
            TrackLoad = playerInfoPacket.iTrackLoad;
            game_type = playerInfoPacket.iGameType;
            competitors = playerInfoPacket.iCompetitors;
            level = playerInfoPacket.iLevel;
            damage_level = playerInfoPacket.iDamageLevel & 0xF;
            if ((playerInfoPacket.iDamageLevel & 0x40) != 0)
              player_invul[in_header.byConsoleNode] = -1;
            else
              player_invul[in_header.byConsoleNode] = 0;
            manual_control[in_header.byConsoleNode] = playerInfoPacket.iManualControl;
            ROLLERCommsPostListen();
            continue;
          case PACKET_ID_SYNC_ERROR:
            goto LABEL_23;
          case PACKET_ID_NOCD:
            if (player_ready[in_header.byConsoleNode])
              goto LABEL_31;
            ++active_nodes;
            player_ready[byConsoleNode] = -1;
            ROLLERCommsPostListen();
            continue;
          case PACKET_ID_RESYNC: 
            ++lost_message;
            ROLLERCommsPostListen();
            syncnode = in_header.byConsoleNode;
            ROLLERCommsGetBlock(pData, &iFrame, 4);
            syncframe = iFrame;
            if (frame_number - iFrame <= 500) {
              if (ROLLERCommsGetType())
                ROLLERclrtx();
              syncleft = frame_number - syncframe;
              syncptr = syncframe & 0x1FF;
              if (check_set && syncleft > 7) {
                memset(player_checks, -1, sizeof(player_checks));
                check_set = 0;
              }
              do_sync_stuff();
              ROLLERCommsPostListen();
            } else {
            LABEL_23:
              ROLLERCommsPostListen();
              send_network_sync_error();
              network_sync_error = -1;
              ROLLERCommsPostListen();
            }
            break;
          case PACKET_ID_SEND_HERE:
            load_times[in_header.byConsoleNode] = in_header.unFrameId;
            ROLLERCommsPostListen();
            break;
          default:
            ++duff_message;
            goto LABEL_31;
        }
      } else {
      LABEL_31:
        ;
        ROLLERCommsPostListen();
      }
    }
  }
}

//-------------------------------------------------------------------------------------------------
//00050600
void do_sync_stuff()
{
  int iSyncPtr; // esi
  uint8 byConsoleNode2; // al
  int iNumCars; // edx
  int iCarIdx; // eax
  int *pTestMultiple; // edx
  uint32 uiSyncData2; // ebx
  int iSendDataSuccess2; // eax
  uint8 byConsoleNode; // al
  int *pTestMini; // edx
  int i; // eax
  uint32 uiSyncData; // ebx
  int iSendDataSuccess; // eax

  iSyncPtr = syncptr;
  if (net_type) {
    do {
      if (!syncleft)
        break;

      // Prepare sync header
      byConsoleNode = (uint8)wConsoleNode;
      in_header.uiId = 0x686C6366;              // fclh
      in_header.byConsoleNode = byConsoleNode;
      pTestMini = test_mini;
      in_header.unFrameId = syncframe;

      // Build minimal sync for two local players
      for (i = 0; i < 2; ++i) {
        ++pTestMini;
        uiSyncData = copy_multiple[iSyncPtr][player_to_car[i]].uiFullData;
        *(pTestMini - 1) = uiSyncData;
      }
      syncptr = iSyncPtr;

      // Send data
      iSendDataSuccess = ROLLERCommsSendData(&in_header, 12, test_mini, 8, syncnode);

      // Advance state
      iSyncPtr = ((uint16)syncptr + 1) & 0x1FF;  // ensure this index doesn't go beyond 511
      --syncleft;
      ++syncframe;
    } while (iSendDataSuccess);
  } else {
    while (syncleft) {
      // Prepare sync header
      byConsoleNode2 = (uint8)wConsoleNode;
      in_header.uiId = 0x686C6366;              // fclh
      in_header.byConsoleNode = byConsoleNode2;
      iNumCars = numcars;
      in_header.unFrameId = syncframe;
      iCarIdx = 0;

      // Build sync payload with data for all cars
      if (iNumCars > 0) {
        pTestMultiple = test_multiple;
        do {
          uiSyncData2 = copy_multiple[iSyncPtr][iCarIdx++].uiFullData;
          *pTestMultiple++ = uiSyncData2;
        } while (iCarIdx < numcars);
      }
      syncptr = iSyncPtr;

      // Send data
      iSendDataSuccess2 = ROLLERCommsSendData(&in_header, 12, test_multiple, 64, syncnode);

      // Advance state
      iSyncPtr = ((uint16)syncptr + 1) & 0x1FF;
      --syncleft;
      ++syncframe;
      if (!iSendDataSuccess2) {
        syncptr = ((uint16)syncptr + 1) & 0x1FF; // ensure this index doesn't go beyond 511
        return;
      }
    }
  }
  syncptr = iSyncPtr;
}

//-------------------------------------------------------------------------------------------------
//00050790
int TransmitInit()
{
  int iSuccess; // eax
  int32 iCarIdx; // eax
  int32 iGameType; // eax
  int32 iLevelFlags; // eax
  char *szDefaultNamesDst; // ecx
  char *szDefaultNameItr; // ebx
  tTransmitInitPacket initPacket; // [esp+0h] [ebp-108h] BYREF
  tSyncHeader header;

  iSuccess = -1;
  if (network_on) {
    header.byConsoleNode = player1_car;
    header.uiId = PACKET_ID_TRANSMIT_INIT;
    initPacket.iTrackLoad = TrackLoad;
    name_copy(initPacket.szPlayerName, player_names[wConsoleNode]);
    initPacket.address[0] = address[0];
    initPacket.address[1] = address[1];
    initPacket.address[2] = address[2];
    initPacket.address[3] = address[3];
    if (broadcast_mode == -9999)
      iCarIdx = car_request - 1;
    else
      iCarIdx = Players_Cars[wConsoleNode];
    initPacket.iCarIdx = iCarIdx;
    if (net_type)
      ++my_age;
    initPacket.iMyAge = my_age;
    initPacket.iNetworkOn = network_on;
    if (game_type <= 2)
      iGameType = game_type;
    else
      iGameType = last_type;
    initPacket.iGameType = iGameType;
    initPacket.iCompetitors = competitors;
    initPacket.iManualControl = manual_control[wConsoleNode];
    iLevelFlags = level;
    if ((cheat_mode & CHEAT_MODE_DEATH_MODE) != 0)
      iLevelFlags = level | 0x0100;
    if (player_invul[player1_car])
      iLevelFlags |= 0x0200;
    if ((cheat_mode & CHEAT_MODE_KILLER_OPPONENTS) != 0)
      iLevelFlags |= 0x0400;
    if ((cheat_mode & CHEAT_MODE_ICY_ROAD) != 0)
      iLevelFlags |= 0x0800;
    if ((cheat_mode & CHEAT_MODE_50HZ_TIMER) != 0)
      iLevelFlags |= 0x1000;
    if ((cheat_mode & CHEAT_MODE_DOUBLE_TRACK) != 0)
      iLevelFlags |= 0x2000;
    if ((cheat_mode & CHEAT_MODE_100HZ_TIMER) != 0)
      iLevelFlags |= 0x4000;
    if ((cheat_mode & CHEAT_MODE_CLONES) != 0)
      iLevelFlags |= 0x8000;
    if ((cheat_mode & CHEAT_MODE_TINY_CARS) != 0)
      iLevelFlags |= 0x10000;
    initPacket.iLevelFlags = iLevelFlags;
    initPacket.iDamageLevel = damage_level;
    initPacket.iStartPressed = StartPressed;
    if (time_to_start == 45)
      time_to_start = 0;
    initPacket.iTimeToStart = time_to_start;
    initPacket.iFalseStart = false_starts;
    initPacket.iTextureMode = (textures_off & TEX_OFF_ADVANCED_CARS) != 0;
    initPacket.iNetworkChampOn = network_champ_on;
    szDefaultNamesDst = initPacket.default_names[0];
    szDefaultNameItr = default_names[0];
    initPacket.iNetworkSlot = network_slot;
    do {
      name_copy(szDefaultNamesDst, szDefaultNameItr);
      szDefaultNameItr += 9;
      szDefaultNamesDst += 9;
    } while (szDefaultNameItr != default_names[16]);

    return ROLLERCommsSendData(
             &header,
             sizeof(tSyncHeader),
             &initPacket,
             sizeof(tTransmitInitPacket),
             21);
  }
  return iSuccess;
}

//-------------------------------------------------------------------------------------------------
//000509B0
void StartNode(int iNode)
{
  my_age = 0;
  ROLLERCommsGetNetworkAddr(address);
  ROLLERCommsGetConsoleNode();
  network_on = 1;
  if (iNode || network_slot < 0)
    broadcast_mode = 0xFFFFFE38;
  else
    broadcast_mode = -1;
  tick_on = -1;
}

//-------------------------------------------------------------------------------------------------
//00050A60
void CheckNewNodes()
{
  int iNode; // edx
  //int iMsgCopyIndex; // eax
  int iNumGamersInSlot; // ebp
  int iGamerIndex; // edi
  //char *pTxInitPacketByteCmp; // edx
  int iAddressMatchFlag; // ebx
  //char *szAddressPtr; // eax
  //char *szAddressEndPtr; // esi
  int iSlotIndex; // edi
  char *szGamerNamesPtr; // eax
  _NETNOW_NODE_ADDR *pNetNowNodeAddr4; // edi
  int iSlotIndex2; // eax
  int iFoundNodeOffset; // ebp
  int iNetworkNodeIndex; // edi
  //char *pTxInitPacketByteCmp_1; // edx
  int iAddressMatchFlag2; // ebx
  //char *pNodeAddrByteCmp; // eax
  //char *pByteCmpEnd; // esi
  //_NETNOW_NODE_ADDR *pNetNowNodeAddr3; // edi
  int iTimeToStart; // eax
  int iCheckNamesResult; // ebp
  //char *szTxInitPacketNamesItr2; // ebx
  //char *szDefaultNamesItr2; // esi
  //uint32 iTempCheatMode4; // ecx
  //uint32 iTempCheatMode5; // eax
  //uint32 iTempCheatMode6; // ebx
  //uint32 iTempCheatMode7; // ebx
  unsigned int uiTimerHz; // eax
  //uint32 iTempCheatMode8; // eax
  //uint32 iTempCheatMode9; // eax
  int i; // eax
  int iNodeIndex; // ebp
  int iCheckNamesResult2; // ecx
  //uint32 uiTempCheatMode; // edx
  //uint32 uiTempCheatMode2; // edx
  //uint32 iTempCheatMode3; // ebx
  unsigned int uiTimerHz2; // eax
  //uint32 iTempCheatMode; // eax
  //uint32 iTempCheatMode2; // edx
  //char *szPacketNamesIt; // ebx
  //char *szDefaultNamesItr; // esi
  int iPlayerIndex; // eax
  //int j; // eax
  int byConsoleNode; // edx
  tTransmitInitPacket transmitInitPacket; // [esp+0h] [ebp-18Ch] BYREF
  tMessagePacket messagePacket; // [esp+E8h] [ebp-A4h] BYREF
  tPlayerInfoPacket playerInfoPacket; // [esp+118h] [ebp-74h] BYREF
  tRecordPacket recordPacket; // [esp+140h] [ebp-4Ch] BYREF
  tSyncHeader syncHeader; // [esp+150h] [ebp-3Ch] BYREF
  void *pPacket2; // [esp+15Ch] [ebp-30h] BYREF
  int iMaxGamerIndex; // [esp+160h] [ebp-2Ch]
  int *pTestSeed; // [esp+164h] [ebp-28h]
  int iTxInitPacketNetworkSlotMinus1; // [esp+168h] [ebp-24h]
  //_NETNOW_NODE_ADDR *szGamerAddressPtr; // [esp+16Ch] [ebp-20h]
  int iFoundGamerIndex; // [esp+170h] [ebp-1Ch]

  pTestSeed = &test_seed;                       // Initialize pointer to test seed for random number generation

  while (ROLLERCommsGetHeader(&syncHeader, sizeof(tSyncHeader), &pPacket2))// Main packet processing loop - handle incoming network packets from other nodes
  {                                             // Handle PACKET_ID_TRANSMIT_INIT (0x686C6361) - player initialization packets
    switch (syncHeader.uiId) {
      case PACKET_ID_TRANSMIT_INIT:                         // PACKET_ID_TRANSMIT_INIT
        transmitInitPacket.iNetworkChampOn = 0;
        transmitInitPacket.iNetworkSlot = 0;
        ROLLERCommsGetBlock(pPacket2, &transmitInitPacket, sizeof(tTransmitInitPacket));
        if (network_slot >= 0)                // Process init packet when we are already connected (network_slot >= 0)
        {
          if (transmitInitPacket.iNetworkSlot < 0 && !I_Quit) {
            send_broadcast(0xFFFFFE37);
            test = 3;
            ROLLERCommsPostListen();
            continue;
          }
          test = 4;
          iFoundNodeOffset = 0;
          for (iNetworkNodeIndex = 0; iNetworkNodeIndex < network_on; ++iNetworkNodeIndex)// Search through existing network nodes to find matching address
          {

            //This byte-by-byte comparison could be simplified to: 
            iAddressMatchFlag2 = memcmp(&address[iNetworkNodeIndex], &transmitInitPacket.address, sizeof(_NETNOW_NODE_ADDR));
            //pTxInitPacketByteCmp_1 = (char *)&transmitInitPacket;
            //iAddressMatchFlag2 = 0;
            //pNodeAddrByteCmp = (char *)&address[iNetworkNodeIndex];
            //pByteCmpEnd = pNodeAddrByteCmp + 16;
            //do {                                   // Compare network addresses byte by byte to detect duplicate nodes
            //  if (*pNodeAddrByteCmp != *pTxInitPacketByteCmp_1)
            //    iAddressMatchFlag2 = -1;
            //  ++pNodeAddrByteCmp;
            //  ++pTxInitPacketByteCmp_1;
            //} while (pNodeAddrByteCmp != pByteCmpEnd);

            if (!iAddressMatchFlag2) {
              iFoundNodeOffset = iNetworkNodeIndex + 10;
              iNetworkNodeIndex = network_on + 6;
            }
          }
          if (!iFoundNodeOffset) {
            test = 5;
            if (time_to_start == -1)
              goto LABEL_40;
            if (transmitInitPacket.iNetworkChampOn != network_champ_on)
              goto LABEL_40;
            if (transmitInitPacket.iNetworkSlot != network_slot)
              goto LABEL_40;
            if (network_on >= 16)
              goto LABEL_40;
            if (I_Quit)
              goto LABEL_40;
            test = 20;                          // Add new network node if all validation checks pass
            if ((unsigned int)ROLLERCommsAddNode(&transmitInitPacket.address) > 1)
              goto LABEL_40;
            test = 6;
            ROLLERCommsSortNodes();

            memcpy(&address[network_on], &transmitInitPacket.address, sizeof(_NETNOW_NODE_ADDR));
            //pNetNowNodeAddr3 = &address[network_on];// Manual address copying - could be simplified to: memcpy(&address[network_on], &transmitInitPacket.address, sizeof(_NETNOW_NODE_ADDR));
            //*(_DWORD *)pNetNowNodeAddr3->sIPX.sInternetAddr.bNetwork = *(_DWORD *)transmitInitPacket.address.sIPX.sInternetAddr.bNetwork;
            //pNetNowNodeAddr3 = (_NETNOW_NODE_ADDR *)((char *)pNetNowNodeAddr3 + 4);
            //*(_DWORD *)pNetNowNodeAddr3->sIPX.sInternetAddr.bNetwork = *(_DWORD *)&transmitInitPacket.address.sNETBIOS.bNode[4];
            //*(_QWORD *)&pNetNowNodeAddr3->sNETBIOS.bNode[4] = *(_QWORD *)&transmitInitPacket.address.sNETBIOS.bNode[8];

            iTimeToStart = transmitInitPacket.iTimeToStart;
            switch_types = 0;
            if (transmitInitPacket.iTimeToStart == -667)
              iTimeToStart = 0;
            if (iTimeToStart) {
              if (time_to_start != -666)
                goto LABEL_40;
            LABEL_64:
              reset_network(-1);
              ROLLERCommsPostListen();
              continue;
            }
            test = 7;
            name_copy(player_names[network_on], transmitInitPacket.szPlayerName);
            Players_Cars[network_on] = transmitInitPacket.iCarIdx;
            manual_control[network_on] = transmitInitPacket.iManualControl;
            player_started[network_on] = transmitInitPacket.iStartPressed;
            iCheckNamesResult = CheckNames(player_names[network_on], network_on);
            check_cars();
            if (my_age < transmitInitPacket.iMyAge) {

              for (int i = 0; i < 16; i++) {
                name_copy(default_names[i], transmitInitPacket.default_names[i]);
              }
              //szTxInitPacketNamesItr2 = transmitInitPacket.default_names[0];
              //szDefaultNamesItr2 = default_names[0];
              //do {
              //  name_copy(szDefaultNamesItr2, szTxInitPacketNamesItr2);
              //  szTxInitPacketNamesItr2 += 9;
              //  szDefaultNamesItr2 += 9;
              //} while (szTxInitPacketNamesItr2 != (char *)&messagePacket);// messagePacket is immediately after transmitInitPacket

              my_age = transmitInitPacket.iMyAge;
              TrackLoad = transmitInitPacket.iTrackLoad;
              game_type = transmitInitPacket.iGameType;
              if (transmitInitPacket.iGameType == 1)
                Race = ((uint8)TrackLoad - 1) & 7;
              else
                network_champ_on = 0;
              competitors = transmitInitPacket.iCompetitors;
              level = transmitInitPacket.iLevelFlags;
              if ((transmitInitPacket.iLevelFlags & 0x100) != 0)// Process level flags and update cheat mode settings accordingly
              {
                cheat_mode |= CHEAT_MODE_DEATH_MODE;
                //iTempCheatMode4 = cheat_mode;
                //LOBYTE(iTempCheatMode4) = cheat_mode | 2;
                //cheat_mode = iTempCheatMode4;
              }
              player_invul[network_on] = ((level & 0x200) == 0) - 1;
              if ((level & 0x400) != 0) {
                cheat_mode |= CHEAT_MODE_KILLER_OPPONENTS;
                //iTempCheatMode5 = cheat_mode;
                //BYTE1(iTempCheatMode5) = BYTE1(cheat_mode) | 2;
                //cheat_mode = iTempCheatMode5;
              }
              if ((level & 0x800) != 0) {
                cheat_mode |= CHEAT_MODE_ICY_ROAD;
                //iTempCheatMode6 = cheat_mode;
                //BYTE1(iTempCheatMode6) = BYTE1(cheat_mode) | 4;
                //cheat_mode = iTempCheatMode6;
              }
              if ((level & 0x1000) != 0) {
                if ((level & 0x4000) != 0) {
                  if ((cheat_mode & 0x2000) == 0) {
                    cheat_mode |= (CHEAT_MODE_50HZ_TIMER | CHEAT_MODE_100HZ_TIMER);
                    //iTempCheatMode7 = cheat_mode;
                    //BYTE1(iTempCheatMode7) = BYTE1(cheat_mode) | 0x28;
                    //cheat_mode = iTempCheatMode7;
                    release_ticktimer();
                    uiTimerHz = 100;
                    goto LABEL_83;
                  }
                } else if ((cheat_mode & 0x800) == 0) {
                  cheat_mode |= CHEAT_MODE_50HZ_TIMER;
                  //iTempCheatMode8 = cheat_mode;
                  //BYTE1(iTempCheatMode8) = BYTE1(cheat_mode) | 8;
                  //cheat_mode = iTempCheatMode8;
                  release_ticktimer();
                  uiTimerHz = 50;
                LABEL_83:
                  claim_ticktimer(uiTimerHz);
                }
              }
              if ((level & 0x2000) != 0)
                cheat_mode |= CHEAT_MODE_DOUBLE_TRACK;
              if ((level & 0x10000) != 0) {
                cheat_mode |= CHEAT_MODE_TINY_CARS;
                //iTempCheatMode9 = cheat_mode;
                //BYTE1(iTempCheatMode9) = BYTE1(cheat_mode) | 0x80;
                //cheat_mode = iTempCheatMode9;
              }
              if ((level & 0x8000) != 0 && switch_same >= 0)
                switch_same = transmitInitPacket.iCarIdx + 666;
              level &= 0xFu;
              damage_level = transmitInitPacket.iDamageLevel;
              false_starts = transmitInitPacket.iFalseStart;
              if (transmitInitPacket.iTextureMode) {
                // TEX_OFF_ADVANCED_CARS
                if ((textures_off & TEX_OFF_ADVANCED_CARS) == 0) {
                  textures_off |= TEX_OFF_ADVANCED_CARS;
                  goto LABEL_96;
                }
              }
              // TEX_OFF_ADVANCED_CARS
              else if ((textures_off & TEX_OFF_ADVANCED_CARS) != 0) {
                textures_off &= ~TEX_OFF_ADVANCED_CARS;
              LABEL_96:
                switch_sets = -1;
              }
            }
            players_waiting = 0;
            for (i = 0; i <= network_on; ++i) // Count how many players have pressed start and are waiting
            {
              if (player_started[i])
                ++players_waiting;
            }
            ++my_age;
            ++network_on;
            if (!I_Quit)
              send_broadcast(0xFFFFFFFF);
            if (!iCheckNamesResult)
              goto LABEL_40;
          LABEL_104:
            cheat_mode = 0;
            ROLLERCommsPostListen();
            continue;
          }
          test = 8;
          iNodeIndex = iFoundNodeOffset - 10;
          ++my_age;
          if (iNodeIndex <= 0)
            goto LABEL_40;
          test = 9;
          if (!transmitInitPacket.iTimeToStart) {
            test = 10;
            name_copy(player_names[iNodeIndex], transmitInitPacket.szPlayerName);
            Players_Cars[iNodeIndex] = transmitInitPacket.iCarIdx;
            iCheckNamesResult2 = CheckNames(player_names[iNodeIndex], iNodeIndex);
            check_cars();
            TrackLoad = transmitInitPacket.iTrackLoad;
            if (transmitInitPacket.iGameType < 3) {
              if (transmitInitPacket.iGameType == game_type)
                game_type = transmitInitPacket.iGameType;
              else
                switch_types = transmitInitPacket.iGameType + 1;
            }
            competitors = transmitInitPacket.iCompetitors;
            level = transmitInitPacket.iLevelFlags;
            if ((transmitInitPacket.iLevelFlags & 0x100) != 0) {
              cheat_mode |= CHEAT_MODE_DEATH_MODE;
              //uiTempCheatMode = cheat_mode;
              //LOBYTE(uiTempCheatMode) = cheat_mode | 2;
              //cheat_mode = uiTempCheatMode;
            }
            player_invul[iNodeIndex] = ((level & 0x200) == 0) - 1;
            if ((level & 0x400) != 0)
              cheat_mode |= CHEAT_MODE_KILLER_OPPONENTS;
            if ((level & 0x800) != 0) {
              cheat_mode |= CHEAT_MODE_ICY_ROAD;
              //uiTempCheatMode2 = cheat_mode;
              //BYTE1(uiTempCheatMode2) = BYTE1(cheat_mode) | 4;
              //cheat_mode = uiTempCheatMode2;
            }
            if ((level & 0x1000) != 0) {
              if ((level & 0x4000) != 0) {
                if ((cheat_mode & 0x2000) == 0) {
                  cheat_mode |= (CHEAT_MODE_50HZ_TIMER | CHEAT_MODE_100HZ_TIMER);
                  //iTempCheatMode3 = cheat_mode;
                  //BYTE1(iTempCheatMode3) = BYTE1(cheat_mode) | 0x28;
                  //cheat_mode = iTempCheatMode3;
                  release_ticktimer();
                  uiTimerHz2 = 100;
                  goto LABEL_130;
                }
              } else if ((cheat_mode & 0x800) == 0) {
                cheat_mode |= CHEAT_MODE_50HZ_TIMER;
                //iTempCheatMode = cheat_mode;
                //BYTE1(iTempCheatMode) = BYTE1(cheat_mode) | 8;
                //cheat_mode = iTempCheatMode;
                release_ticktimer();
                uiTimerHz2 = 50;
              LABEL_130:
                claim_ticktimer(uiTimerHz2);
              }
            }
            if ((level & 0x2000) != 0)
              cheat_mode |= CHEAT_MODE_DOUBLE_TRACK;
            if ((level & 0x10000) != 0) {
              cheat_mode |= CHEAT_MODE_TINY_CARS;
              //iTempCheatMode2 = cheat_mode;
              //BYTE1(iTempCheatMode2) = BYTE1(cheat_mode) | 0x80;
              //cheat_mode = iTempCheatMode2;
            }
            if ((level & 0x8000) != 0 && switch_same >= 0)
              switch_same = transmitInitPacket.iCarIdx + 666;
            level &= 0xFu;
            damage_level = transmitInitPacket.iDamageLevel;

            for (int i = 0; i < 16; i++) {
              name_copy(default_names[i], transmitInitPacket.default_names[i]);
            }
            //szPacketNamesIt = transmitInitPacket.default_names[0];
            //szDefaultNamesItr = default_names[0];
            //do {
            //  name_copy(szDefaultNamesItr, szPacketNamesIt);
            //  szPacketNamesIt += 9;
            //  szDefaultNamesItr += 9;
            //} while (szPacketNamesIt != (char *)&messagePacket);

            false_starts = transmitInitPacket.iFalseStart;
            if (transmitInitPacket.iTextureMode) {
              // TEX_OFF_ADVANCED_CARS
              if ((textures_off & TEX_OFF_ADVANCED_CARS) == 0) {
                textures_off |= TEX_OFF_ADVANCED_CARS;
                goto LABEL_145;
              }
            }
            // TEX_OFF_ADVANCED_CARS
            else if ((textures_off & TEX_OFF_ADVANCED_CARS) != 0) {
              textures_off &= ~TEX_OFF_ADVANCED_CARS;
            LABEL_145:
              switch_sets = -1;
            }
            manual_control[iNodeIndex] = transmitInitPacket.iManualControl;
            player_started[iNodeIndex] = transmitInitPacket.iStartPressed;
            players_waiting = 0;
            for (iPlayerIndex = 0; iPlayerIndex < network_on; ++iPlayerIndex) {
              if (player_started[iPlayerIndex])
                ++players_waiting;
            }
            if (!iCheckNamesResult2)
              goto LABEL_40;
            goto LABEL_104;
          }
          if (transmitInitPacket.iTimeToStart < ~665u) {
            if (transmitInitPacket.iTimeToStart == -667) {
              send_broadcast(0xFFFFFFFF);
              ROLLERCommsPostListen();
              continue;
            }
          } else {
            if (transmitInitPacket.iTimeToStart <= ~665u)
              goto LABEL_64;
            if (transmitInitPacket.iTimeToStart == -457)
              goto LABEL_40;
          }
          time_to_start = -1;
          FoundNodes();
          ROLLERCommsPostListen();
        } else {
          test = 1;
          if (transmitInitPacket.iNetworkSlot < 0)
            goto LABEL_40;
          test = 2;
          iTxInitPacketNetworkSlotMinus1 = transmitInitPacket.iNetworkSlot - 1;
          if ((unsigned int)(transmitInitPacket.iNetworkSlot - 1) > 4)
            goto LABEL_40;
          if (transmitInitPacket.iTimeToStart < -666)
            goto LABEL_24;
          if (transmitInitPacket.iTimeToStart <= -666) {
            send_broadcast(0xFFFFFE38);
            gamers_playing[iTxInitPacketNetworkSlotMinus1] = 0;
            ROLLERCommsPostListen();
            continue;
          }
          if (transmitInitPacket.iTimeToStart != -1) {
          LABEL_24:
            if (!transmitInitPacket.iNetworkChampOn) {
              iFoundGamerIndex = 0;
              iNumGamersInSlot = gamers_playing[iTxInitPacketNetworkSlotMinus1];
              if (iNumGamersInSlot < 0) {
                iNumGamersInSlot = 0;
                gamers_playing[iTxInitPacketNetworkSlotMinus1] = 0;
              }
              iGamerIndex = 0;
              if (iNumGamersInSlot > 0) {
                //szGamerAddressPtr = gamers_address[iTxInitPacketNetworkSlotMinus1];
                iMaxGamerIndex = iNumGamersInSlot + 678;
                do {

                  iAddressMatchFlag = memcmp(&gamers_address[iTxInitPacketNetworkSlotMinus1][iGamerIndex], &transmitInitPacket.address, sizeof(_NETNOW_NODE_ADDR));
                  //pTxInitPacketByteCmp = (char *)&transmitInitPacket;
                  //iAddressMatchFlag = 0;
                  //szAddressPtr = (char *)&szGamerAddressPtr[iGamerIndex];
                  //szAddressEndPtr = szAddressPtr + 16;
                  //do {
                  //  if (*szAddressPtr != *pTxInitPacketByteCmp)
                  //    iAddressMatchFlag = -1;
                  //  ++szAddressPtr;
                  //  ++pTxInitPacketByteCmp;
                  //} while (szAddressPtr != szAddressEndPtr);

                  if (!iAddressMatchFlag) {
                    iFoundGamerIndex = iGamerIndex + 678;
                    iGamerIndex = iMaxGamerIndex;
                  }
                  ++iGamerIndex;
                } while (iGamerIndex < iNumGamersInSlot);
              }
              if (!transmitInitPacket.iNetworkChampOn) {
                iSlotIndex = iTxInitPacketNetworkSlotMinus1;
                szGamerNamesPtr = gamers_names[iTxInitPacketNetworkSlotMinus1];
                if (iFoundGamerIndex) {
                  name_copy(&szGamerNamesPtr[9 * iFoundGamerIndex - 6102], transmitInitPacket.szPlayerName);
                } else {
                  name_copy(&szGamerNamesPtr[9 * iNumGamersInSlot], transmitInitPacket.szPlayerName);
                  pNetNowNodeAddr4 = &gamers_address[iSlotIndex][iNumGamersInSlot];
                  iSlotIndex2 = iTxInitPacketNetworkSlotMinus1;

                  memcpy(&gamers_address[iSlotIndex][iNumGamersInSlot], &transmitInitPacket.address, sizeof(_NETNOW_NODE_ADDR));
                  //*(_DWORD *)pNetNowNodeAddr4->sIPX.sInternetAddr.bNetwork = *(_DWORD *)transmitInitPacket.address.sIPX.sInternetAddr.bNetwork;
                  //pNetNowNodeAddr4 = (_NETNOW_NODE_ADDR *)((char *)pNetNowNodeAddr4 + 4);
                  //*(_DWORD *)pNetNowNodeAddr4->sIPX.sInternetAddr.bNetwork = *(_DWORD *)&transmitInitPacket.address.sNETBIOS.bNode[4];
                  //*(_QWORD *)&pNetNowNodeAddr4->sNETBIOS.bNode[4] = *(_QWORD *)&transmitInitPacket.address.sNETBIOS.bNode[8];

                  ++gamers_playing[iSlotIndex2];
                }
              }
              goto LABEL_40;
            }
          }
          gamers_playing[iTxInitPacketNetworkSlotMinus1] = -2;
          ROLLERCommsPostListen();
        }
        break;
      case PACKET_ID_QUIT:                         // PACKET_ID_QUIT
        net_quit = -1;                          // Handle PACKET_ID_QUIT (0x686C6364) - player disconnection notification
        ROLLERCommsPostListen();
        continue;
      case PACKET_ID_READY:                         // PACKET_ID_READY
        if (player_ready[syncHeader.byConsoleNode])// Handle PACKET_ID_READY (0x686C6367) - player ready status
          goto LABEL_40;
        ++active_nodes;
        player_ready[syncHeader.byConsoleNode] = -1;
        netCD = -1;
        ROLLERCommsPostListen();
        continue;
      case PACKET_ID_SEED:                         // PACKET_ID_SEED
        ROLLERCommsGetBlock(pPacket2, &test_seed, 4);// Handle PACKET_ID_SEED (0x686C6368) - synchronize random seed across network
        srand(*pTestSeed);
        random_seed = *pTestSeed;
        received_seed = -1;
        ROLLERCommsPostListen();
        continue;
      case PACKET_ID_PLAYER_INFO:                         // PACKET_ID_PLAYER_INFO
        ROLLERCommsGetBlock(pPacket2, &playerInfoPacket, 40);// Handle PACKET_ID_PLAYER_INFO (0x686C636A) - update player information
        byConsoleNode = syncHeader.byConsoleNode;
        Players_Cars[syncHeader.byConsoleNode] = playerInfoPacket.iPlayerCar;
        name_copy(player_names[byConsoleNode], playerInfoPacket.szPlayerName);
        TrackLoad = playerInfoPacket.iTrackLoad;
        game_type = playerInfoPacket.iGameType;
        competitors = playerInfoPacket.iCompetitors;
        level = playerInfoPacket.iLevel;
        damage_level = playerInfoPacket.iDamageLevel & 0xF;
        if ((playerInfoPacket.iDamageLevel & 0x40) != 0)
          player_invul[syncHeader.byConsoleNode] = -1;
        else
          player_invul[syncHeader.byConsoleNode] = 0;
        manual_control[syncHeader.byConsoleNode] = playerInfoPacket.iManualControl;
        goto LABEL_40;
      case PACKET_ID_RECORD:                         // PACKET_ID_RECORD
        ++received_records;                     // Handle PACKET_ID_RECORD (0x686C636B) - update lap record if faster than current
        ROLLERCommsGetBlock(pPacket2, &recordPacket, 16);
        if (recordPacket.fRecordLap < (double)RecordLaps[TrackLoad]) {
          RecordLaps[TrackLoad] = recordPacket.fRecordLap;
          RecordCars[TrackLoad] = (int16)recordPacket.unRecordCar;

          strncpy(RecordNames[TrackLoad], recordPacket.szRecordName, 9);
          //for (j = 0; j < 9; *((_BYTE *)&fudge_wait + 9 * TrackLoad + j + 3) = recordPacket.szRecordName[j - 1])// fudge_wait is offset into RecordNames
          //  ++j;
        }
        goto LABEL_40;
      case PACKET_ID_NOCD:                         // PACKET_ID_NOCD
        if (player_ready[syncHeader.byConsoleNode])
          goto LABEL_40;
        ++active_nodes;
        player_ready[syncHeader.byConsoleNode] = -1;
        ROLLERCommsPostListen();
        continue;
      case PACKET_ID_SLOT:                         // PACKET_ID_SLOT
        //TODO: ensure this is accurate
        gamers_playing[syncHeader.byConsoleNode - 1] = -2;
        //*(_DWORD *)&gamers_address[3][15].sNETBIOS.bNode[4 * syncHeader.byConsoleNode + 12] = -2;
        ROLLERCommsPostListen();
        continue;
      case PACKET_ID_SEND_HERE:                         // PACKET_ID_SEND_HERE
        load_times[syncHeader.byConsoleNode] = syncHeader.unFrameId;
        ROLLERCommsPostListen();
        continue;
      case PACKET_ID_MESSAGE:                         // PACKET_ID_MESSAGE
        ROLLERCommsGetBlock(pPacket2, &messagePacket, 48);// Handle PACKET_ID_MESSAGE (0x686C6373) - receive and process chat messages
        iNode = syncHeader.byConsoleNode;
        if (iNode != ROLLERCommsNetAddrToNode(address) && messagePacket.iNetworkSlot == network_slot) {
          rec_status = 36;
          name_copy(rec_mes_name, messagePacket.szPlayerName);

          strncpy(rec_mes_buf, messagePacket.szMessage, sizeof(rec_mes_buf));
          //for (iMsgCopyIndex = 0; iMsgCopyIndex < 32; LoadCarTextures_variable_1[iMsgCopyIndex] = transmitInitPacket.default_names[15][iMsgCopyIndex + 8])
          //  ++iMsgCopyIndex;
        }
        goto LABEL_40;
      default:
      LABEL_40:
        ROLLERCommsPostListen();
        continue;
    }
  }
}

//-------------------------------------------------------------------------------------------------
//000516F0
void FoundNodes()
{
  if (!master) return;  // Only process if we're the master

  // Update network state
  ROLLERCommsSortNodes();
  network_on = ROLLERCommsGetActiveNodes();
  wConsoleNode = ROLLERCommsGetConsoleNode();

  // Reset master flag and set player count
  master = 0;
  start_multiple = network_on - 1;

  // Transfer player settings to network slot
  Players_Cars[wConsoleNode] = Players_Cars[player1_car];
  manual_control[wConsoleNode] = manual_control[player1_car];
  player_invul[wConsoleNode] = player_invul[player1_car];

  // Copy player name to network slot
  name_copy(player_names[wConsoleNode], player_names[player1_car]);

  // Update player1 to current network slot
  player1_car = wConsoleNode;

  // Initialize net_players array
  if (network_on > 0) {
    for (int i = 0; i < network_on; i++) {
      net_players[i] = -1;  // 0xFFFFFFFF
    }
  }
}

//-------------------------------------------------------------------------------------------------
//000517D0
void SendPlayerInfo()
{
  tPlayerInfoPacket playerInfo; // [esp+0h] [ebp-44h] BYREF

  if (network_on) {
    //setup playerInfo packet
    name_copy(playerInfo.szPlayerName, player_names[player1_car]);
    playerInfo.iPlayerCar = Players_Cars[player1_car];
    playerInfo.iTrackLoad = TrackLoad;
    playerInfo.iGameType = game_type;
    playerInfo.iCompetitors = competitors;
    playerInfo.iLevel = level;
    playerInfo.iDamageLevel = damage_level;
    if (player_invul[player1_car])
      playerInfo.iDamageLevel |= 0x00000040;
    playerInfo.iManualControl = manual_control[player1_car];
    
    //setup header
    p_header.byConsoleNode = (uint8)wConsoleNode;
    p_header.uiId = PACKET_ID_PLAYER_INFO;

    //send to all nodes
    for (int i = 0; i < network_on; ++i) {
      if (wConsoleNode != i) {
        while (!ROLLERCommsSendData(&p_header, sizeof(tSyncHeader), &playerInfo, sizeof(tPlayerInfoPacket), i))
          UpdateSDL(); //added by ROLLER
      }
    }
  }
}

//-------------------------------------------------------------------------------------------------
//000518F0
void prepare_net_message(int iMessageMode, int iMessageNumber)
{
  message_node = iMessageMode;
  message_number = iMessageNumber;
}

//-------------------------------------------------------------------------------------------------
//00051900
void SendAMessage()
{
  int iNode; // [esp+4h] [ebp-4Ch]
  tMessagePacket messagePacket; // [esp+8h] [ebp-48h] BYREF
  tSyncHeader syncHeader; // [esp+38h] [ebp-18h] BYREF

  if (!broadcast_mode && send_message_to >= 0) {
    syncHeader.byConsoleNode = ROLLERCommsNetAddrToNode(address);
    syncHeader.uiId = PACKET_ID_MESSAGE;
    name_copy(messagePacket.szPlayerName, player_names[player1_car]);

    strncpy(messagePacket.szMessage, rec_mes_buf, sizeof(messagePacket.szMessage));

    messagePacket.iNetworkSlot = network_slot;

    if (send_message_to)
      iNode = ROLLERCommsNetAddrToNode(&address[send_message_to]);
    else
      iNode = 21;

    if (ROLLERCommsSendData(&syncHeader, sizeof(tSyncHeader), &messagePacket, sizeof(tMessagePacket), iNode))
      send_status = 18;
    else
      send_status = -18;
    send_message_to = -1;
  }
}

//-------------------------------------------------------------------------------------------------
//00051A30
void BroadcastNews()
{
  int iInitSuccess8; // eax
  int iInitSuccess7; // eax
  int iInitSuccess6; // eax
  int j; // eax
  int iInitSuccess5; // eax
  int iInitSuccess4; // eax
  int i; // eax
  int iInitSuccess3; // eax
  int iInitSuccess2; // eax
  int iInitSuccess10; // eax
  int iOldTimeToStart; // edx
  int iInitSuccess9; // eax
  int iInitSuccess; // eax

  if (!broadcast_mode)
    return;
  if (broadcast_mode < 0xFFFFFD63) {
    if (broadcast_mode < 0xFFFFF562) {
      if (broadcast_mode < 0xFFFFD8F1) {
      LABEL_81:
        iInitSuccess = -1;
        if (network_on)
          iInitSuccess = TransmitInit();
        if (iInitSuccess) {
          check_cars();
          broadcast_mode = 0;
        }
        return;
      }
      if (broadcast_mode <= 0xFFFFD8F1) {
        if (total_wins[car_request + 15] < 2) {
          iInitSuccess2 = -1;
          if (network_on)
            iInitSuccess2 = TransmitInit();
          if (!iInitSuccess2)
            return;
          car_request = -car_request;
          check_cars();
        } else {
          car_request = 0;
        }
        broadcast_mode = 0;
        return;
      }
      if (broadcast_mode != 0xFFFFF211)
        goto LABEL_81;
      if (wConsoleNode != master)
        send_record_to_master(TrackLoad);
    } else {
      if (broadcast_mode <= 0xFFFFF562) {
        send_record_to_slaves(TrackLoad);
        send_seed(random_seed);
        broadcast_mode = 0;
        return;
      }
      if (broadcast_mode < 0xFFFFFD61)
        goto LABEL_81;
      if (broadcast_mode <= 0xFFFFFD61) {
        if (master) {
          iInitSuccess3 = -1;
          time_to_start = -1;
          if (network_on)
            iInitSuccess3 = TransmitInit();
          if (!iInitSuccess3) {
            time_to_start = 0;
            return;
          }
          FoundNodes();
        }
        broadcast_mode = 0;
      } else {
        StartPressed = 0;
        iInitSuccess4 = -1;
        if (network_on)
          iInitSuccess4 = TransmitInit();
        if (iInitSuccess4) {
          player_started[0] = 0;
          players_waiting = 0;
          for (i = 0; i < network_on; ++i) {
            if (player_started[i])
              ++players_waiting;
          }
          broadcast_mode = 0;
        }
      }
    }
  } else if (broadcast_mode <= 0xFFFFFD63) {
    iInitSuccess5 = -1;
    if (network_on)
      iInitSuccess5 = TransmitInit();
    if (iInitSuccess5) {
      waste = CheckNames(player_names[player1_car], player1_car);
      broadcast_mode = 0;
    }
  } else if (broadcast_mode < 0xFFFFFD66) {
    if (broadcast_mode <= 0xFFFFFD64) {
      StartPressed = -1;
      iInitSuccess6 = -1;
      if (network_on)
        iInitSuccess6 = TransmitInit();
      if (iInitSuccess6) {
        player_started[0] = -1;
        players_waiting = 0;
        for (j = 0; j < network_on; ++j) {
          if (player_started[j])
            ++players_waiting;
        }
        broadcast_mode = 0;
      }
    } else {
      time_to_start = -667;
      iInitSuccess7 = -1;
      if (network_on)
        iInitSuccess7 = TransmitInit();
      time_to_start = 0;
      if (iInitSuccess7)
        broadcast_mode = 0;
    }
  } else if (broadcast_mode <= 0xFFFFFD66) {
    time_to_start = -666;
    iInitSuccess8 = -1;
    if (network_on)
      iInitSuccess8 = TransmitInit();
    time_to_start = 0;
    if (iInitSuccess8) {
      I_Quit = -1;
      broadcast_mode = 0;
    }
  } else {
    if (broadcast_mode < 0xFFFFFE38) {
      if (broadcast_mode == 0xFFFFFE37) {
        iOldTimeToStart = time_to_start;
        time_to_start = 0xFFFFFE37;
        iInitSuccess9 = -1;
        if (network_on)
          iInitSuccess9 = TransmitInit();
        if (iInitSuccess9)
          broadcast_mode = 0;
        time_to_start = iOldTimeToStart;
      }
      goto LABEL_81;
    }
    if (broadcast_mode <= 0xFFFFFE38) {
      time_to_start = 0xFFFFFE38;
      //_STOSD(gamers_playing, 0, v0, 4u);
      memset(gamers_playing, 0, sizeof(gamers_playing));
      iInitSuccess10 = -1;
      if (network_on)
        iInitSuccess10 = TransmitInit();
      if (iInitSuccess10)
        broadcast_mode = 0;
      time_to_start = 0;
    } else {
      if (broadcast_mode != 0xFFFFFEC6)
        goto LABEL_81;
      FoundNodes();
      SendPlayerInfo();
      if (wConsoleNode != master)
        send_record_to_master(TrackLoad);
      broadcast_mode = 0;
    }
  }
}

//-------------------------------------------------------------------------------------------------
//00051E20
void remove_messages(int iClear)
{
  //uint8 buffer[4]; // [esp-Ch] [ebp-10h] BYREF
  void *pPacketData;
  
  // first message processing loop
  do {
    ROLLERCommsPostListen();                       // check for incoming messages
    UpdateSDL(); //added by ROLLER
  } while (ROLLERCommsGetHeader(&in_header, sizeof(tSyncHeader), &pPacketData));// continue until there are no more messages
  
  // additional clearing if condition is true
  if (iClear) {
    while (ROLLERCommsPostListen())
      UpdateSDL(); //added by ROLLER
  }
  clear_network_game();
}

//-------------------------------------------------------------------------------------------------
//00051E60
void reset_network(int iResetBroadcastMode)
{
  int iNode; // edx
  int i; // edx

  iNode = 0;
  gamers_playing[0] = 0;
  gamers_playing[1] = 0;
  gamers_playing[2] = 0;
  gamers_playing[3] = 0;
  wConsoleNode = ROLLERCommsNetAddrToNode(address);
  while (iNode < wConsoleNode) {
    if (ROLLERCommsDeleteNode(0)) {
      printf("FAILED TO DELETE NODE c (%d)!!!\n", iNode);
      doexit();
    }
    ROLLERCommsSortNodes();
    ++iNode;
  }
  for (i = wConsoleNode + 1; i < network_on; ++i) {
    if (ROLLERCommsDeleteNode(1)) {
      printf("FAILED TO DELETE NODE (%d)!!!\n", i);
      doexit();
    }
    ROLLERCommsSortNodes();
  }

  for (int i = 0; i < 16; ++i) {
    player_started[i] = 0;
    net_players[i] = 0;
  }

  player1_car = 0;
  received_seed = 0;
  frame_number = 0;
  time_to_start = 0;
  broadcast_mode = 0;
  players_waiting = 0;
  active_nodes = 0;
  net_quit = 0;
  master = -667;
  wConsoleNode = 0;
  I_Quit = 0;
  my_age = 0;
  ROLLERCommsGetNetworkAddr(address);
  ROLLERCommsGetConsoleNode();
  network_on = 1;
  if (network_slot >= 0)
    broadcast_mode = -1;
  else
    broadcast_mode = 0xFFFFFE38;
  tick_on = -1;
  if (iResetBroadcastMode) {
    send_broadcast(broadcast_mode);
    broadcast_mode = 0;
  }
  ROLLERCommsSortNodes();
  received_records = 1;
  switch_sets = 0;
  switch_types = 0;
  players = network_on;
  message_received = -1;
  message_sent = -1;
  if (StartPressed) {
    player_started[0] = -1;
    players_waiting = 1;
  }
}

//-------------------------------------------------------------------------------------------------
//00052060
void clear_network_game()
{
  for (int iPlayer = 0; iPlayer < 16; ++iPlayer) {
    player_ready[iPlayer] = 0;
    // Start at offset = iPlayer * 2 and step by 32
    for (int iOffset = iPlayer * 2; iOffset < 0x4000 / 2; iOffset += 16)  // step is 32 bytes = 16 shorts
    {
      player_checks[iOffset][iPlayer] = -1;
    }
  }
  memset(copy_multiple, 0, sizeof(copy_multiple));
}

//-------------------------------------------------------------------------------------------------
//000520D0
void reset_net_wait()
{
  for (int i = 0; i < 16; ++i) {
    player_started[i] = 0;
  }

  time_to_start = 0;
  players_waiting = 0;
}

//-------------------------------------------------------------------------------------------------
//00052130
unsigned int send_broadcast(unsigned int uiBroadcastMode)
{
  uint32 uiOldTimeToStart;

  if (uiBroadcastMode < 0xFFFFFE38) {
    if (uiBroadcastMode == 0xFFFFFE37) {
      uiOldTimeToStart = time_to_start;
      time_to_start = 0xFFFFFE37;
      do {
        uiBroadcastMode = -1;
        if (network_on)
          uiBroadcastMode = TransmitInit();
      } while (!uiBroadcastMode);
      time_to_start = uiOldTimeToStart;
    }
  } else if (uiBroadcastMode <= 0xFFFFFE38) {
    uiOldTimeToStart = time_to_start;
    time_to_start = 0xFFFFFE38;
    do {
      uiBroadcastMode = -1;
      if (network_on)
        uiBroadcastMode = TransmitInit();
    } while (!uiBroadcastMode);
    time_to_start = uiOldTimeToStart;
  } else if (uiBroadcastMode == -1) {
    do {
      uiBroadcastMode = -1;
      if (network_on)
        uiBroadcastMode = TransmitInit();
    } while (!uiBroadcastMode);
  }
  return uiBroadcastMode;
}

//-------------------------------------------------------------------------------------------------
//000521D0
void send_here(int iNode, int iFrame)
{
  int i; // esi
  tSyncHeader syncHeader; // [esp+0h] [ebp-1Ch] BYREF
  int iData; // [esp+Ch] [ebp-10h] BYREF

  if (network_on) {
    syncHeader.byConsoleNode = iNode;
    syncHeader.unFrameId = iFrame;
    syncHeader.uiId = PACKET_ID_SEND_HERE;
    if (iNode == master) {
      for (i = 0; i < network_on; ++i) {
        if (i != master) {
          ROLLERCommsSendData(&syncHeader, sizeof(tSyncHeader), &iData, sizeof(int), i);
        }
      }
    } else {
      ROLLERCommsSendData(&syncHeader, sizeof(tSyncHeader), &iData, sizeof(int), master);
    }
  }
}

//-------------------------------------------------------------------------------------------------
