#ifndef _ROLLER_COMMS_H
#define _ROLLER_COMMS_H
//-------------------------------------------------------------------------------------------------
#include "types.h"
//-------------------------------------------------------------------------------------------------

extern int serial_port;
extern int modem_port;
extern int modem_baud;
extern int current_modem;
extern char modem_initstring[51];
extern char modem_phone[53];
extern int modem_tone;
extern int modem_call;

//-------------------------------------------------------------------------------------------------

int stringwidth(char *szString);

//-------------------------------------------------------------------------------------------------
#endif
