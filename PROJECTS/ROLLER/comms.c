#include "comms.h"
#include "frontend.h"

//-------------------------------------------------------------------------------------------------

int serial_port = 2;    //000AFD98
int modem_port = 2;     //000AFD9C
int modem_baud = 19200; //000AFDA0
int current_modem = 0;  //000AFDA8
char modem_initstring[51] = "ATX"; //000AFDAC
char modem_phone[53] = { 0 }; //000AFDDF
int modem_tone = -1;    //000AFE14
int modem_call = -1;    //000AFE18

//-------------------------------------------------------------------------------------------------
//00076180
int stringwidth(char *szString)
{
  int iStringWidth; // eax
  int iCharBlockIdx; // ebx

  iStringWidth = 0;
  while (*szString) {
    iCharBlockIdx = (uint8)font1_ascii[(uint8)*szString++];
    if (iCharBlockIdx == 255)
      iStringWidth += 8;
    else
      iStringWidth += front_vga[15][iCharBlockIdx].iWidth + 1;
  }
  return iStringWidth;
}
