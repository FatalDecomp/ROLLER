#ifndef _ROLLER_COMMS_H
#define _ROLLER_COMMS_H
//-------------------------------------------------------------------------------------------------
#include "types.h"
//-------------------------------------------------------------------------------------------------

typedef struct {
  char *bufPtr;
  char buffer[100];
} tModemBuffer2;

//-------------------------------------------------------------------------------------------------

typedef struct {
  int iKeycode;
  int iCharacter;
} tKeycodePair;

//-------------------------------------------------------------------------------------------------

extern int serial_port;
extern int modem_port;
extern int modem_baud;
extern int no_16550;
extern int current_modem;
extern char modem_initstring[51];
extern char modem_phone[53];
extern int modem_tone;
extern int modem_call;
extern int modemstrs;
extern char *modembuffer;
extern int modems;
extern tModemBuffer2 modembuffer2;

//-------------------------------------------------------------------------------------------------

int select_comport(int iIconIdx);
int stringwidth(char *szString);
int select_modemstuff(int iIconIdx);
void loadmodemnames();
void select_modem(int iIconIdx);
char *getprintstring(const char *szStr, int iEditMode);
void display_essentials(int iSelectedItem, int iHighlightedItem, int iEditMode);
void check16550(int iPort);

//-------------------------------------------------------------------------------------------------
#endif
