#include "comms.h"
#include "sound.h"
#include "frontend.h"
#include "func2.h"
#include "3d.h"
#include "network.h"
#include "rollercomms.h"
#include <math.h>
#include <float.h>
#include <string.h>
//-------------------------------------------------------------------------------------------------

int serial_port = 2;    //000AFD98
int modem_port = 2;     //000AFD9C
int modem_baud = 19200; //000AFDA0
int no_16550 = 0;       //000AFDA4
int current_modem = 0;  //000AFDA8
char modem_initstring[51] = "ATX"; //000AFDAC
char modem_phone[53] = { 0 }; //000AFDDF
int modem_tone = -1;    //000AFE14
int modem_call = -1;    //000AFE18
int modemstrs;          //001A2BA0
char *modembuffer;      //001A2BA4
int modems;             //001A2BA8
tModemBuffer2 modembuffer2; //001A2BAC

//-------------------------------------------------------------------------------------------------
//00075DC0
int select_comport(int iIconIdx)
{                                               // BIOS interrupt 11h - Equipment determination (detect available COM ports)
  unsigned int uiKey; // esi
  int iAvailableComPorts; // edi
  char byCom1Color; // al
  char byCom2Color; // al
  char byCom3Color; // al
  char byCom4Color; // al
  unsigned int uiTempKey; // eax
  unsigned int uiExtendedKey; // eax
  int iExitFlag; // [esp+4h] [ebp-4h]

  //__asm { int     11h; BIOS interrupt 11h - Equipment determination(detect available COM ports) }
  iExitFlag = 0;                                // Initialize exit flag (0 = continue loop, -1 = exit)

  //Set this to 4 for modern systems to always show 4 comm ports
  iAvailableComPorts = 4;// ((int)(unsigned __int16)iIconIdx >> 9) & 7;// Extract COM port count from BIOS equipment word (AX bits 9-11) - DECOMPILER ERROR: uses AX from INT 11h, not parameter a1

  if ((unsigned int)iAvailableComPorts < 2)   // Ensure minimum of 2 COM ports are available
    iAvailableComPorts = 2;
  if (iAvailableComPorts < serial_port)       // Validate current serial port selection against available ports
    serial_port = 2;
  check16550(serial_port);                      // Test 16550 UART availability on selected serial port
  ROLLERCommsSetComBaudRate(9600);                 // Set communication baud rate to 9600 bps
  do {
    display_picture(scrbuf, front_vga[0]);      // Display main background picture
    display_block(scrbuf, front_vga[1], 3, head_x, head_y, 0);
    display_block(scrbuf, front_vga[6], 0, 36, 2, 0);// Display UI element at position (36, 2)
    display_block(scrbuf, front_vga[5], iIconIdx, -4, 247, 0);// Display gear selection indicator at bottom left
    display_block(scrbuf, front_vga[5], game_type + 5, 135, 247, 0);// Display game type indicator (5 + game_type offset)
    display_block(scrbuf, front_vga[4], 4, 76, 257, -1);// Display UI element at position (76, 257) with special color
    display_block(scrbuf, front_vga[6], 4, 62, 336, -1);// Display UI element at position (62, 336) with special color
    scale_text(front_vga[15], &language_buffer[5312], font1_ascii, font1_offsets, 400, 75, 143, 1u, 200, 640);// Display main menu title text
    scale_text(front_vga[15], &language_buffer[3008], font1_ascii, font1_offsets, 400, 93, 143, 1u, 200, 640);// Display COM port selection instructions
    if (serial_port == 1)                     // Set color for COM 1 text (highlighted if selected)
      byCom1Color = 0xAB;
    else
      byCom1Color = 0x8F;
    scale_text(front_vga[15], "COM 1", font1_ascii, font1_offsets, 400, 135, byCom1Color, 1u, 200, 640);// Display 'COM 1' option text
    if (serial_port == 2)                     // Set color for COM 2 text (highlighted if selected)
      byCom2Color = 0xAB;
    else
      byCom2Color = 0x8F;
    scale_text(front_vga[15], "COM 2", font1_ascii, font1_offsets, 400, 153, byCom2Color, 1u, 200, 640);// Display 'COM 2' option text
    if (iAvailableComPorts >= 3)              // Check if 3 or more COM ports are available
    {                                           // Set color for COM 3 text (highlighted if selected)
      if (serial_port == 3)
        byCom3Color = 0xAB;
      else
        byCom3Color = 0x8F;
      scale_text(front_vga[15], "COM 3", font1_ascii, font1_offsets, 400, 171, byCom3Color, 1u, 200, 640);// Display 'COM 3' option text (if available)
    }
    if (iAvailableComPorts >= 4)              // Check if 4 or more COM ports are available
    {                                           // Set color for COM 4 text (highlighted if selected)
      if (serial_port == 4)
        byCom4Color = 0xAB;
      else
        byCom4Color = 0x8F;
      scale_text(front_vga[15], "COM 4", font1_ascii, font1_offsets, 400, 189, byCom4Color, 1u, 200, 640);// Display 'COM 4' option text (if available)
    }
    if (no_16550)                             // Check if 16550 UART error occurred
      scale_text(front_vga[15], &language_buffer[6976], font1_ascii, font1_offsets, 400, 351, 231, 1u, 200, 640);// Display 16550 error message at bottom of screen
    copypic(scrbuf, screen);                    // Copy completed frame buffer to screen
    while (fatkbhit())                        // Process all pending keyboard input
    {
      uiTempKey = fatgetch();                   // Get next character from keyboard buffer
      uiKey = uiTempKey;
      if (uiTempKey < 0xD)                    // Check if key code is less than 13 (Enter key)
      {                                         // Check for extended key code (0 = function/arrow keys)
        if (!uiTempKey) {
          uiExtendedKey = fatgetch();           // Get extended key code (second byte)
          uiKey = uiExtendedKey;
          if (uiExtendedKey >= 0x48)          // Check if key code is 0x48 or higher (Up arrow = 0x48)
          {                                     // Check for Up arrow key (0x48)
            if (uiExtendedKey <= 0x48) {                                   // Move to previous COM port (minimum COM 1)
              if (serial_port != 1)
                --serial_port;
            LABEL_38:
              check16550(serial_port);          // Test 16550 UART on newly selected COM port
            } else if (uiExtendedKey == 0x50)   // Check for Down arrow key (0x50)
            {                                   // Move to next COM port (maximum available ports)
              if (iAvailableComPorts != serial_port)
                ++serial_port;
              goto LABEL_38;
            }
          }
        }
      } else if (uiTempKey <= 0xD || uiTempKey == 0x1B)// Check for Enter (13) or Escape (27) keys
      {
        iExitFlag = -1;                         // Set exit flag to exit menu loop
      }
    }
  } while (!iExitFlag);                         // Continue loop until user presses Enter or Escape
  ROLLERCommsSetComPort(serial_port);              // Configure communications system with selected COM port
  return (uiKey != 13) - 1;                     // Return 0 if Enter pressed, -1 if Escape pressed
}

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

//-------------------------------------------------------------------------------------------------
//00076270
int select_modemstuff(int iIconIdx)
{
  bool bIsAnswer; // ebp
  int iResult; // eax
  int iMenuPos; // ebp
  char *szDestPtr; // edi
  char *szSrcPtr; // esi
  int iTemp; // ecx
  char *szTemp2; // ebx
  tKeycodePair keypair; // rax
  unsigned int uiStringLen; // kr08_4
  int iModemResponse; // eax
  int iModemRespCopy; // edx
  int iMaxCOMPorts; // [esp+Ch] [ebp-3Ch]
  int bExitRequested; // [esp+10h] [ebp-38h]
  int iWaitingForAnswer; // [esp+14h] [ebp-34h]
  float fInitTime; // [esp+18h] [ebp-30h]
  int iMessageTimeout; // [esp+20h] [ebp-28h]
  float fDialTime; // [esp+24h] [ebp-24h]
  int iDisplayMessage; // [esp+28h] [ebp-20h]
  unsigned int unInputMode; // [esp+2Ch] [ebp-1Ch]

  iMessageTimeout = 0;                          // Initialize variables
  unInputMode = 0;
  bExitRequested = 0;
  fInitTime = 0.0;
  fDialTime = 0.0;
  iWaitingForAnswer = 0;
  iDisplayMessage = -1;
  bIsAnswer = modem_call == 0;                  // Check if this is answer mode (incoming call)
  loadmodemnames();                             // Load available modem configurations
  iMenuPos = bIsAnswer + 5;                     // Calculate menu position (answer mode offset by 5)
  if (current_modem >= 0)                     // Copy modem init string if modem is selected
  {
    iResult = 51 * *(int *)&modembuffer[56 * current_modem + 52];
    szDestPtr = modem_initstring;
    szSrcPtr = &modembuffer2.bufPtr[iResult];
    //TODO
    //do {
    //  LOBYTE(iResult) = *szSrcPtr;
    //  *szDestPtr = *szSrcPtr;
    //  if (!(uint8)iResult)
    //    break;
    //  LOBYTE(iResult) = szSrcPtr[1];
    //  szSrcPtr += 2;
    //  szDestPtr[1] = iResult;
    //  szDestPtr += 2;
    //} while ((uint8)iResult);
  }
  //TODO
  //__asm { int     11h; BIOS equipment determination to detect COM ports }// BIOS equipment determination to detect COM ports
  iTemp = modem_port;
  iMaxCOMPorts = ((int)(uint16)iResult >> 9) & 7;// Extract number of COM ports from BIOS data
  if (modem_port < 2)                         // Ensure minimum 2 COM ports available
    iMaxCOMPorts = 2;
  if (iMaxCOMPorts < modem_port)
    modem_port = 2;
  check16550(modem_port);                       // Check if COM port has 16550 UART
  modem_baud = 19200;                           // Set default baud rate to 19200
  ROLLERCommsSetComBaudRate(19200);
  do {
    //szTemp2 = (char *)unInputMode;              // Main UI loop - display menu and handle input
    display_essentials(iIconIdx, iMenuPos, unInputMode);
    if (iDisplayMessage == 93 || iDisplayMessage == 94 || iDisplayMessage == 95 || iDisplayMessage == 96 || iDisplayMessage == 104)// Display flashing status messages
    {
      if ((frames & 0xFu) < 8) {
        //szTemp2 = font1_ascii;
        //iTemp = (int)font1_offsets;
        scale_text(front_vga[15], &language_buffer[64 * iDisplayMessage], font1_ascii, font1_offsets, 400, 279, 143, 1u, 200, 640);
      }
    } else if (iDisplayMessage >= 0) {
      //szTemp2 = font1_ascii;
      //iTemp = (int)font1_offsets;
      scale_text(front_vga[15], &language_buffer[64 * iDisplayMessage], font1_ascii, font1_offsets, 400, 279, 231, 1u, 200, 640);
    }
    //keypair.iCharacter = (int)screen;
    copypic(scrbuf, screen);
    if (iMessageTimeout && iMessageTimeout + 72 < frames)
      bExitRequested = -1;
    while (fatkbhit()) {
      keypair.iKeycode = fatgetch();
      iTemp = unInputMode;
      //szTemp2 = (char *)keypair.iKeycode;
      keypair.iCharacter = keypair.iKeycode;
      if (unInputMode) {
        if (unInputMode <= 1) {                                       // String input mode - edit init string or phone number
          if (iMenuPos == 3)
            szTemp2 = modem_initstring;
          else
            szTemp2 = modem_phone;
          uiStringLen = (uint32)strlen(szTemp2) + 1;
          iTemp = uiStringLen - 1;
          if (keypair.iKeycode < 8u) {
            if (!keypair.iKeycode)
              goto LABEL_57;
          LABEL_63:
            if (keys[WHIP_SCANCODE_LSHIFT] || keys[WHIP_SCANCODE_RSHIFT])         // Handle shifted characters for special symbols
            {
              switch (keypair.iKeycode) {
                case '#':
                  keypair.iCharacter = '~';
                  break;
                case '\'':
                  keypair.iCharacter = '@';
                  break;
                case ',':
                  keypair.iCharacter = '<';
                  break;
                case '-':
                  keypair.iCharacter = '_';
                  break;
                case '.':
                  keypair.iCharacter = '>';
                  break;
                case '/':
                  keypair.iCharacter = '?';
                  break;
                case '0':
                  keypair.iCharacter = ')';
                  break;
                case '1':
                  keypair.iCharacter = '!';
                  break;
                case '2':
                  keypair.iCharacter = '"';
                  break;
                case '3':
                  keypair.iCharacter = 0x9C;
                  break;
                case '4':
                  keypair.iCharacter = '$';
                  break;
                case '5':
                  keypair.iCharacter = '%';
                  break;
                case '6':
                  keypair.iCharacter = '^';
                  break;
                case '7':
                  keypair.iCharacter = '&';
                  break;
                case '8':
                  keypair.iCharacter = '*';
                  break;
                case '9':
                  keypair.iCharacter = '(';
                  break;
                case ';':
                  keypair.iCharacter = ':';
                  break;
                case '=':
                  keypair.iCharacter = '+';
                  break;
                default:
                  break;
              }
            }
            iTemp = (uint32)strlen(szTemp2);
            if ((unsigned int)iTemp < 50 && keypair.iCharacter != '\x7F') {
              szTemp2[uiStringLen] = '\0';
              szTemp2[uiStringLen - 1] = keypair.iCharacter;
              if (iMenuPos == 3 && current_modem >= 0)
                current_modem = -current_modem - 1;
            }
          } else if (keypair.iKeycode <= 8u) {
            if (iTemp > 0) {
              szTemp2[iTemp - 1] = '\0';
              if (iMenuPos == 3) {
                iTemp = current_modem;
                if (current_modem >= 0)
                  current_modem = -current_modem - 1;
              }
            }
          } else {
            if (keypair.iKeycode < 0xDu || keypair.iKeycode > 0xDu && keypair.iKeycode != 27)
              goto LABEL_63;
            szTemp2 = (char *)-1;
            unInputMode = 0;
            iMessageTimeout = 0;
            iDisplayMessage = -1;
          }
        } else if (keypair.iKeycode) {
          if (keypair.iKeycode == 27) {
            unInputMode = 0;
            iDisplayMessage = -1;
            iMessageTimeout = 0;
            fInitTime = 0.0;
            fDialTime = 0.0;
            iWaitingForAnswer = 0;
            display_essentials(iIconIdx, iMenuPos, 0);
            //iTemp = (int)font1_offsets;
            //szTemp2 = font1_ascii;
            scale_text(front_vga[15], &language_buffer[6912], font1_ascii, font1_offsets, 400, 279, 143, 1u, 200, 640);
            //keypair.iCharacter = (int)screen;
            copypic(scrbuf, screen);
            close_network();
          }
        } else {
        LABEL_57:
          fatgetch();
        }
      } else {
        iDisplayMessage = -1;                   // Menu navigation mode
        if (keypair.iKeycode < 0xDu) {
          if (!keypair.iKeycode) {
            keypair.iKeycode = fatgetch();
            keypair.iCharacter = keypair.iKeycode;
            if (keypair.iKeycode >= 0x48u) {
              if (keypair.iKeycode <= 0x48u) {
                if (iMenuPos)
                  --iMenuPos;
              } else if (keypair.iKeycode == 80 && iMenuPos < 6) {
                ++iMenuPos;
              }
            }
          }
        } else if (keypair.iKeycode <= 0xDu) {
          keypair.iCharacter = iMenuPos;
          szTemp2 = 0;
          display_essentials(iIconIdx, iMenuPos, 0);
          switch (iMenuPos) {
            case 0:
              select_modem(iIconIdx);           // Menu 0: Select modem type
              break;
            case 1:
              //szTemp2 = (char *)(modem_port + 1);// Menu 1: Cycle through COM ports
              iTemp = iMaxCOMPorts;
              modem_port = modem_port + 1;// (int)szTemp2;
              if (modem_port > iMaxCOMPorts)
                modem_port = 1;
              check16550(modem_port);
              break;
            case 2:
              modem_tone = modem_tone == 0;     // Menu 2: Toggle tone/pulse dialing
              break;
            case 3:
            case 4:
              unInputMode = 1;                  // Menu 3/4: Edit init string or phone number
              break;
            case 5:
            case 6:
              unInputMode = 2;                  // Menu 5/6: Start call (5=dial out, 6=answer)
              iDisplayMessage = 104;
              //szTemp2 = font1_ascii;
              //iTemp = (int)font1_offsets;
              modem_call = (iMenuPos != 5) - 1;
              scale_text(front_vga[15], &language_buffer[6656], font1_ascii, font1_offsets, 400, 279, 143, 1u, 200, 640);
              copypic(scrbuf, screen);
              ROLLERCommsSetComPort(modem_port);
              Initialise_Network(0);
              ROLLERModemHangUp();
              //TODO
              //keypair = (tKeycodePair)clock();
              //fDialTime = (float)(unsigned int)keypair.iKeycode;
              break;
            default:
              continue;                         // Execute menu selection
          }
        } else if (keypair.iKeycode == 27) {
          bExitRequested = -1;
          iMessageTimeout = 0;
          fInitTime = 0.0;
          fDialTime = 0.0;
          iWaitingForAnswer = 0;
        }
      }
    }
    if (unInputMode == 2 && !iMessageTimeout) // Handle modem responses during call
    {
      //TODO
      iModemResponse = 0;// gssModemCheckResponse(0, keypair.iCharacter, (int)szTemp2, iTemp);
      iModemRespCopy = iModemResponse;
      if (iModemResponse >= 0) {
        switch (iModemResponse) {
          case 0:
            if (fabs(fInitTime) != 0)// Response 0: Modem ready - init or dial
            {
              //TODO
              //if (modem_call)
              //  gssModemDial((int)modem_phone, modem_tone);
              iModemRespCopy = modem_call;
              fInitTime = 0.0;
              szTemp2 = (char *)-1;
              iWaitingForAnswer = -1;
              iDisplayMessage = (modem_call != 0) + 93;
            }
            if (fabs(fDialTime) != 0) {
              //TODO
              //gssModemInit((int)modem_initstring, iModemRespCopy, (int)szTemp2, iTemp);
              fDialTime = 0.0;
              //fInitTime = (float)(unsigned int)clock();
            }
            break;
          case 1:
            iDisplayMessage = 95;               // Response 1: Modem busy
            iMessageTimeout = frames;
            break;
          case 2:
            if (iWaitingForAnswer && !modem_call)// Response 2: Incoming call detected
            {
              //TODO
              //gssModemAnswer();
              iDisplayMessage = 96;
            }
            break;
          case 3:
          case 4:
          case 5:
          case 6:
          case 7:
            iDisplayMessage = iModemResponse + 94;// Response 3-7: Connection errors
            close_network();
            unInputMode = 0;
            break;
          default:
            break;
        }
      }
    }
    //TODO
    if (fabs(fInitTime) != 0)// && ((double)(unsigned int)clock() - fInitTime) * 0.0099999998 > 5.0)// Check for dial timeout (5 seconds)
    {
      close_network();
      iTemp = 105;
      fInitTime = 0.0;
      fDialTime = 0.0;
      iWaitingForAnswer = 0;
      unInputMode = 0;
      iDisplayMessage = 105;
    }
    if (fabs(fDialTime) != 0)// Check for init timeout (5 seconds)
    {
      iTemp = 0;
      //TODO
      //if (((double)(unsigned int)clock() - fDialTime) * 0.0099999998 > 5.0) {
      //  close_network();
      //  fInitTime = 0.0;
      //  fDialTime = 0.0;
      //  iWaitingForAnswer = 0;
      //  unInputMode = 0;
      //  iDisplayMessage = 105;
      //}
    }
  } while (!bExitRequested);
  fre((void **)&modembuffer);                   // Cleanup - free modem buffers
  fre((void **)&modembuffer2.bufPtr);
  if (iMessageTimeout)
    return -1;
  //TODO
  //gssModemHangUp();
  close_network();
  return 0;
}

//-------------------------------------------------------------------------------------------------
//000769D0
void loadmodemnames()
{
  FILE *pFileModem; // ecx
  char *pszToken; // eax
  int iModemOffset; // ebp
  char *pszModemData; // eax
  char *pszSrc; // esi
  char *pszDest; // edi
  char byChar1; // al
  char byChar2; // al
  FILE *pFileModemStr; // ecx
  char *pszStrToken; // eax
  int iStrCount; // ebp
  char *pszStrData; // eax
  int iStrOffset; // edx
  char *pszStrSrc; // esi
  char *pszStrDest; // edi
  char byStrChar1; // al
  char byStrChar2; // al
  char szBuffer[512]; // [esp+0h] [ebp-224h] BYREF
  char *pszDelimiters; // [esp+200h] [ebp-24h]
  int iStrOffsetCurrent; // [esp+204h] [ebp-20h]
  int iModemIndex; // [esp+208h] [ebp-1Ch]

  pszDelimiters = " ,\n\t\r";                   // Set up delimiters for parsing config files
  pFileModem = fopen("MODEM.INI", "r");         // Open MODEM.INI file for reading modem configurations
  fgets(szBuffer, 512, pFileModem);             // Read first line containing number of modems
  pszToken = strtok(szBuffer, " ,\n\t\r");      // Parse modem count from first token
  modems = strtol(pszToken, 0, 10);             // Convert string to integer and store global modem count
  modembuffer = (char *)getbuffer(56 * modems); // Allocate buffer for modem data (56 bytes per modem)
  if (!modembuffer)                           // If allocation failed, reset modem count to 0
    modems = 0;
  iModemIndex = 0;                              // Initialize modem index counter
  if (modems > 0) {
    iModemOffset = 0;
    do {
      fgets(szBuffer, 512, pFileModem);         // Read next line from MODEM.INI for each modem entry
      pszModemData = strtok(szBuffer, " ");     // Parse modem type ID from first token
      *(int *)&modembuffer[iModemOffset + 52] = strtol(pszModemData, 0, 10) - 1;// Store modem type (subtract 1 for 0-based index) at offset +52
      pszSrc = strtok(0, "\r\n");               // Get remainder of line (modem name string)
      pszDest = &modembuffer[iModemOffset];     // Set destination pointer to current modem buffer slot
      do {
        byChar1 = *pszSrc;                      // Copy modem name string 2 bytes at a time for efficiency
        *pszDest = *pszSrc;
        if (!byChar1)
          break;
        byChar2 = pszSrc[1];
        pszSrc += 2;
        pszDest[1] = byChar2;
        pszDest += 2;
      } while (byChar2);
      iModemOffset += 56;                       // Advance to next modem slot (56 bytes per entry)
      ++iModemIndex;
    } while (iModemIndex < modems);
  }
  fclose(pFileModem);                           // Close MODEM.INI file
  pFileModemStr = fopen("MODEMSTR.INI", "r");   // Open MODEMSTR.INI file for reading modem command strings
  fgets(szBuffer, 512, pFileModemStr);          // Read first line containing number of modem strings
  pszStrToken = strtok(szBuffer, pszDelimiters);// Parse modem string count from first token
  modemstrs = strtol(pszStrToken, 0, 10);       // Convert string to integer and store global string count
  modembuffer2.bufPtr = (char *)getbuffer(51 * modemstrs);// Allocate buffer for modem strings (51 bytes per string)
  if (!modembuffer2.bufPtr)                   // If allocation failed, reset string count to 0
    modemstrs = 0;
  iStrCount = 0;                                // Initialize string counter and offset variables
  if (modemstrs > 0) {
    iStrOffsetCurrent = 0;
    do {
      fgets(szBuffer, 512, pFileModemStr);      // Read next line from MODEMSTR.INI for each string entry
      pszStrData = strtok(szBuffer, "\t");// Parse modem command string using tab delimiter
      iStrOffset = iStrOffsetCurrent;
      pszStrSrc = pszStrData;
      pszStrDest = &modembuffer2.bufPtr[iStrOffsetCurrent];// Set destination pointer to current string buffer slot
      ++iStrCount;
      do {
        byStrChar1 = *pszStrSrc;                // Copy modem command string 2 bytes at a time
        *pszStrDest = *pszStrSrc;
        if (!byStrChar1)
          break;
        byStrChar2 = pszStrSrc[1];
        pszStrSrc += 2;
        pszStrDest[1] = byStrChar2;
        pszStrDest += 2;
      } while (byStrChar2);
      iStrOffsetCurrent = iStrOffset + 51;      // Advance to next string slot (51 bytes per entry)
    } while (iStrCount < modemstrs);
  }
  fclose(pFileModemStr);                        // Close MODEMSTR.INI file
  if (current_modem >= modems)                // Validate current_modem index - clamp to valid range
    current_modem = modems - 1;                 // If current modem >= total modems, set to last valid index
  if (-modems > current_modem)                // Clamp current_modem to minimum valid range
    current_modem = -modems;
}

//-------------------------------------------------------------------------------------------------
//00076C30
void select_modem(int iIconIdx)
{
  int iStartIdx; // eax
  int iCurrentIdx; // esi
  int iYPosition; // edi
  char byTextColor; // al
  unsigned int uiKeyCode; // eax
  int iExtendedKey; // kr00_4
  int iMaxIdx; // eax
  char *pszDestInit; // edi
  char *pszSrcInit; // esi
  char byChar1; // al
  char byChar2; // al
  int iEndIdx; // [esp+4h] [ebp-10h]
  int iExitFlag; // [esp+8h] [ebp-Ch]
  int iModemOffset; // [esp+Ch] [ebp-8h]
  int iSelectedModem; // [esp+10h] [ebp-4h]

  iExitFlag = 0;                                // Initialize exit flag (0 = continue loop, -1 = exit)
  iSelectedModem = current_modem;               // Get current modem selection for display
  if (current_modem < 0)                      // Handle negative modem index (convert to positive for display)
    iSelectedModem = -current_modem - 1;
  while (!iExitFlag)                          // Main display and input loop
  {
    display_picture(scrbuf, front_vga[0]);      // Display main background picture
    display_block(scrbuf, front_vga[1], 3, head_x, head_y, 0);
    display_block(scrbuf, front_vga[6], 0, 36, 2, 0);// Display UI element at position (36, 2)
    display_block(scrbuf, front_vga[5], iIconIdx, -4, 247, 0);// Display gear selection indicator at bottom left
    display_block(scrbuf, front_vga[5], game_type + 5, 135, 247, 0);// Display game type indicator (5 + game_type offset)
    display_block(scrbuf, front_vga[4], 4, 76, 257, -1);// Display UI element at position (76, 257) with special color
    display_block(scrbuf, front_vga[6], 4, 62, 336, -1);// Display UI element at position (62, 336) with special color
    scale_text(front_vga[15], &language_buffer[6784], font1_ascii, font1_offsets, 400, 75, 143, 1u, 200, 640);// Display 'Select Modem' title text
    iStartIdx = iSelectedModem - 7;             // Calculate display window start (7 items before current)
    if (iSelectedModem - 7 < 0)               // Ensure start index is not negative
      iStartIdx = 0;
    iEndIdx = iSelectedModem + 7;               // Calculate display window end (7 items after current)
    if (iSelectedModem + 7 >= modems)         // Ensure end index does not exceed available modems
      iEndIdx = modems - 1;
    iCurrentIdx = iStartIdx;                    // Initialize loop variables for modem list display
    if (iStartIdx <= iEndIdx)                 // Check if there are modems to display
    {
      iModemOffset = 56 * iStartIdx;            // Calculate byte offset into modem buffer (56 bytes per modem)
      iYPosition = -18 * iSelectedModem + 18 * iStartIdx + 237;// Calculate Y position for first visible modem (18 pixels per line)
      do {                                         // Set highlight color for currently selected modem
        if (iCurrentIdx == iSelectedModem)
          byTextColor = 0xAB;
        else
          byTextColor = 0x8F;
        ++iCurrentIdx;                          // Move to next modem in list
        scale_text(front_vga[15], &modembuffer[iModemOffset], font1_ascii, font1_offsets, 400, iYPosition, byTextColor, 1u, 200, 640);// Display modem name from buffer (56 bytes per entry)
        iYPosition += 18;                       // Move to next line (18 pixels down)
        iModemOffset += 56;                     // Move to next modem data (56 bytes forward)
      } while (iCurrentIdx <= iEndIdx);         // Continue until all visible modems are displayed
    }
    copypic(scrbuf, screen);                    // Copy completed frame buffer to screen
    while (fatkbhit())                        // Process all pending keyboard input
    {
      uiKeyCode = fatgetch();                   // Get next character from keyboard buffer
      if (uiKeyCode < 0xD)                    // Check if key code is less than 13 (Enter key)
      {                                         // Check for extended key code (0 = function/arrow keys)
        if (!uiKeyCode) {
          iExtendedKey = fatgetch() - 71;       // Get extended key and subtract 71 (base for navigation keys)
          iMaxIdx = modems - 1;
          switch (iExtendedKey) {
            case 0:
              iSelectedModem = 0;               // HOME key (71) - Jump to first modem
              continue;
            case 1:
              if (!iSelectedModem)
                continue;                       // UP key (72) - Move to previous modem
              iMaxIdx = iSelectedModem - 1;
            LABEL_26:
              iSelectedModem = iMaxIdx;         // LEFT key (75) and END key (79) - Move to previous modem
              break;
            case 2:
              iSelectedModem -= 15;             // PAGE UP key (73) - Move up 15 modems
              if (iSelectedModem < 0)
                iSelectedModem = 0;
              continue;
            case 8:
              goto LABEL_26;
            case 9:
              if (iMaxIdx > iSelectedModem)   // RIGHT key (77) and DOWN key (80) - Move to next modem
                ++iSelectedModem;
              continue;
            case 10:
              iSelectedModem += 15;             // PAGE DOWN key (81) - Move down 15 modems
              if (iMaxIdx < iSelectedModem)
                iSelectedModem = modems - 1;
              continue;
            default:
              continue;
          }
        }
      } else if (uiKeyCode <= 0xD)              // Check for Enter key (13) to select modem
      {
        current_modem = iSelectedModem;         // Save selected modem as current choice
        pszDestInit = modem_initstring;         // Set destination pointer for modem init string copy
        pszSrcInit = &modembuffer2.bufPtr[51 * *(int *)&modembuffer[56 * iSelectedModem + 52]];// Calculate source pointer to modem init string (51 bytes per init string)
        iExitFlag = -1;                         // Set exit flag to leave menu
        do {
          byChar1 = *pszSrcInit;                // Copy modem initialization string (2 bytes per iteration)
          *pszDestInit = *pszSrcInit;
          if (!byChar1)
            break;                              // Check for null terminator to end string copy
          byChar2 = pszSrcInit[1];              // Copy second byte of character pair
          pszSrcInit += 2;
          pszDestInit[1] = byChar2;
          pszDestInit += 2;
        } while (byChar2);                      // Continue until null terminator found
      } else if (uiKeyCode == 27)               // Check for Escape key (27) to cancel selection
      {
        iExitFlag = -1;                         // Set exit flag to leave menu without selection
      }
    }
  }
  exit(0);
  //JUMPOUT(0x76174);
}

//-------------------------------------------------------------------------------------------------
//00076F10
char *getprintstring(const char *szStr, int iEditMode)
{
  char *pszDest; // edi
  const char *pszSrc; // esi
  char byChar1; // al
  char byChar2; // al
  unsigned int uiStringLen; // ecx
  unsigned int uiCursorPos; // kr08_4
  unsigned int uiLastCharIdx; // ecx
  char *pszTruncDest; // edi
  const char *pszTruncSrc; // esi
  char byTruncChar1; // al
  char byTruncChar2; // al
  unsigned int uiTruncLen; // ecx
  unsigned int uiFinalLen; // ecx

  pszDest = modembuffer2.buffer;                // Initialize destination pointer to modembuffer2[1] (skip first byte)
  pszSrc = szStr;                               // Initialize source pointer to input string
  do {
    byChar1 = *pszSrc;                          // Copy string 2 bytes at a time to destination buffer
    *pszDest = *pszSrc;
    if (!byChar1)
      break;
    byChar2 = pszSrc[1];
    pszSrc += 2;
    pszDest[1] = byChar2;
    pszDest += 2;
  } while (byChar2);
  if (iEditMode)                              // If in edit mode, add cursor and handle text width
  {
    uiCursorPos = (int)strlen(modembuffer2.buffer) + 1;// Get string length and add underscore cursor at end
    uiLastCharIdx = uiCursorPos - 1;
    modembuffer2.buffer[uiLastCharIdx] = '_';   // Place underscore cursor at end of string (ASCII 95 = '_')
    modembuffer2.buffer[uiLastCharIdx + 1] = '\0';
    if (stringwidth(modembuffer2.buffer) <= 240)// Check if string width fits in 240 pixel limit
    {
      uiFinalLen = uiCursorPos;
    } else {
      pszTruncDest = &modembuffer2.buffer[3];   // String too wide - truncate with ellipsis (...)
      pszTruncSrc = szStr;
      memset(modembuffer2.buffer, 46, 3);       // Set first 3 chars to dots (0x2E2E2E = "...")
      do {
        byTruncChar1 = *pszTruncSrc;            // Copy remainder of source string after ellipsis
        *pszTruncDest = *pszTruncSrc;
        if (!byTruncChar1)
          break;
        byTruncChar2 = pszTruncSrc[1];
        pszTruncSrc += 2;
        pszTruncDest[1] = byTruncChar2;
        pszTruncDest += 2;
      } while (byTruncChar2);
      modembuffer2.buffer[uiLastCharIdx + 4] = '\0';
      modembuffer2.buffer[uiLastCharIdx + 3] = '_';
      uiTruncLen = uiCursorPos;
      while (stringwidth(modembuffer2.buffer) > 240)// Shrink string from beginning until it fits width limit
        memmove(&modembuffer2.buffer[3], &modembuffer2.buffer[4], uiTruncLen--);
      uiFinalLen = uiTruncLen + 3;
    }
    if ((frames & 0xFu) >= 8)                 // Blinking cursor: hide cursor every 8 frames
      modembuffer2.buffer[uiFinalLen - 1] = '\0';
  } else if (stringwidth((char *)szStr) > 240)  // Not in edit mode - just check if string fits
  {
    uiStringLen = (int)strlen(modembuffer2.buffer);
    do {
      modembuffer2.buffer[uiStringLen - 1] = '.';// Truncate from end with "..." until string fits
      modembuffer2.buffer[uiStringLen] = '.';
      modembuffer2.buffer[uiStringLen + 1] = '.';
      modembuffer2.buffer[uiStringLen-- + 2] = '\0';
    } while (stringwidth(modembuffer2.buffer) > 240);
  }
  return modembuffer2.buffer;                   // Return pointer to formatted string in modembuffer2[1]
}

//-------------------------------------------------------------------------------------------------
//00077060
void display_essentials(int iSelectedItem, int iHighlightedItem, int iEditMode)
{
  char byTextColor1; // al
  char byTextColor2; // al
  char byTextColor3; // al
  char byTextColor4; // al
  char byTextColor5; // al
  char byTextColor6; // al
  char byTextColor7; // al
  char byTextColor8; // al
  char byTextColor9; // al
  bool bIsInitEditMode; // edx
  char *pszInitString; // eax
  char byTextColor10; // al
  char byTextColor11; // al
  bool bIsPhoneEditMode; // edx
  char *pszPhoneString; // eax
  char byTextColor12; // al
  char byTextColor13; // al
  char byInitTextColor; // [esp-10h] [ebp-78h]
  char byPhoneTextColor; // [esp-10h] [ebp-78h]
  char szText[100]; // [esp+0h] [ebp-68h] BYREF
  int iBlockIdx; // [esp+64h] [ebp-4h]

  iBlockIdx = iSelectedItem;                    // Store selected block index for gear selection display
  display_picture(scrbuf, front_vga[0]);        // Display main background picture (modem config screen)
  display_block(scrbuf, front_vga[1], 3, head_x, head_y, 0);// Display driver head icon at current position
  display_block(scrbuf, front_vga[6], 0, 36, 2, 0);// Display UI element at position (36, 2)
  display_block(scrbuf, front_vga[5], iBlockIdx, -4, 247, 0);// Display selected gear indicator at bottom left
  display_block(scrbuf, front_vga[5], game_type + 5, 135, 247, 0);// Display game type indicator (5 + game_type offset)
  display_block(scrbuf, front_vga[4], 4, 76, 257, -1);// Display UI element at position (76, 257) with special color (-1)
  display_block(scrbuf, front_vga[6], 4, 62, 336, -1);// Display UI element at position (62, 336) with special color (-1)
  scale_text(front_vga[15], &language_buffer[5376], font1_ascii, font1_offsets, 400, 75, 143, 1u, 200, 640);// Display main menu title text
  if (modembuffer)                            // Check if modem data is available
  {                                             // Check if no modem is currently selected (negative index)
    if (current_modem < 0) {                                           // Set color for 'no modem' text based on highlight state
      if (iHighlightedItem)
        byTextColor2 = 0x8F;
      else
        byTextColor2 = 0xAB;
      scale_text(front_vga[15], &language_buffer[6848], font1_ascii, font1_offsets, 400, 117, byTextColor2, 1u, 200, 640);// Display 'no modem selected' message
    } else {                                           // Set color for modem name based on highlight state
      if (iHighlightedItem)
        byTextColor1 = 0x8F;
      else
        byTextColor1 = 0xAB;
      scale_text(front_vga[15], &modembuffer[56 * current_modem], font1_ascii, font1_offsets, 400, 117, byTextColor1, 1u, 200, 640);// Display selected modem name (56 bytes per modem entry)
    }
  }
  sprintf(szText, "COM %d", modem_port);        // Format COM port number into text buffer
  if (iHighlightedItem == 1)                  // Set colors for COM port label (item 1)
    byTextColor3 = 0xAB;
  else
    byTextColor3 = 0x8F;
  scale_text(front_vga[15], &language_buffer[5440], font1_ascii, font1_offsets, 400, 135, byTextColor3, 2u, 200, 640);// Display COM port label text
  if (iHighlightedItem == 1)                  // Set colors for COM port value (item 1)
    byTextColor4 = 0xAB;
  else
    byTextColor4 = 0x8F;
  scale_text(front_vga[15], szText, font1_ascii, font1_offsets, 400, 135, byTextColor4, 0, 200, 640);// Display COM port number (e.g. 'COM 1')
  if (iHighlightedItem == 2)                  // Set colors for dial mode label (item 2)
    byTextColor5 = 0xAB;
  else
    byTextColor5 = 0x8F;
  scale_text(front_vga[15], &language_buffer[5504], font1_ascii, font1_offsets, 400, 153, byTextColor5, 2u, 200, 640);// Display dial mode label text
  if (modem_tone)                             // Check if tone dialing is enabled
  {                                             // Set colors for 'TONE' mode display
    if (iHighlightedItem == 2)
      byTextColor6 = 0xAB;
    else
      byTextColor6 = 0x8F;
    scale_text(front_vga[15], &language_buffer[5568], font1_ascii, font1_offsets, 400, 153, byTextColor6, 0, 200, 640);// Display 'TONE' dialing mode text
  } else {                                             // Set colors for 'PULSE' mode display
    if (iHighlightedItem == 2)
      byTextColor7 = 0xAB;
    else
      byTextColor7 = 0x8F;
    scale_text(front_vga[15], &language_buffer[5632], font1_ascii, font1_offsets, 400, 153, byTextColor7, 0, 200, 640);// Display 'PULSE' dialing mode text
  }
  if (iHighlightedItem == 3)                  // Set colors for modem init string label (item 3)
    byTextColor8 = 0xAB;
  else
    byTextColor8 = 0x8F;
  scale_text(front_vga[15], &language_buffer[5696], font1_ascii, font1_offsets, 400, 171, byTextColor8, 2u, 200, 640);// Display modem initialization string label
  if (iHighlightedItem == 3)                  // Set colors for init string value (item 3)
    byTextColor9 = 0xAB;
  else
    byTextColor9 = 0x8F;
  byInitTextColor = byTextColor9;
  bIsInitEditMode = iHighlightedItem == 3 && iEditMode == 1;// Check if init string is in edit mode (item 3 + edit flag)
  pszInitString = (char *)getprintstring(modem_initstring, bIsInitEditMode);// Get printable version of init string (handles edit cursor)
  scale_text(front_vga[15], pszInitString, font1_ascii, font1_offsets, 400, 171, byInitTextColor, 0, 200, 640);// Display modem initialization string
  if (iHighlightedItem == 4)                  // Set colors for phone number label (item 4)
    byTextColor10 = 0xAB;
  else
    byTextColor10 = 0x8F;
  scale_text(front_vga[15], &language_buffer[5760], font1_ascii, font1_offsets, 400, 189, byTextColor10, 2u, 200, 640);// Display phone number label text
  if (iHighlightedItem == 4)                  // Set colors for phone number value (item 4)
    byTextColor11 = 0xAB;
  else
    byTextColor11 = 0x8F;
  byPhoneTextColor = byTextColor11;
  bIsPhoneEditMode = iHighlightedItem == 4 && iEditMode == 1;// Check if phone number is in edit mode (item 4 + edit flag)
  pszPhoneString = (char *)getprintstring(modem_phone, bIsPhoneEditMode);// Get printable version of phone number (handles edit cursor)
  scale_text(front_vga[15], pszPhoneString, font1_ascii, font1_offsets, 400, 189, byPhoneTextColor, 0, 200, 640);// Display phone number string
  if (iHighlightedItem == 5)                  // Set colors for 'Accept' button (item 5)
    byTextColor12 = 0xAB;
  else
    byTextColor12 = 0x8F;
  scale_text(front_vga[15], &language_buffer[5824], font1_ascii, font1_offsets, 400, 225, byTextColor12, 1u, 200, 640);// Display 'Accept' button text
  if (iHighlightedItem == 6)                  // Set colors for 'Cancel' button (item 6)
    byTextColor13 = 0xAB;
  else
    byTextColor13 = 0x8F;
  scale_text(front_vga[15], &language_buffer[5888], font1_ascii, font1_offsets, 400, 243, byTextColor13, 1u, 200, 640);// Display 'Cancel' button text
  if (no_16550)                               // Check if 16550 UART error occurred
    scale_text(front_vga[15], &language_buffer[64 * no_16550], font1_ascii, font1_offsets, 400, 351, 231, 1u, 200, 640);// Display 16550 error message (64 bytes per error code)
}

//-------------------------------------------------------------------------------------------------
//00077560
void check16550(int iPort)
{                                               // Test if 16550 UART is available at the specified port
  //TODO
  //if (gss16550(iPort)) {
  if (true) {
    no_16550 = 0;                               // 16550 UART detected - clear error flag (0 = success)
  } else if (MusicCD)                           // 16550 not found - check if CD music is enabled
  {
    no_16550 = 110;                             // CD music available - set error code 110 (CD audio fallback)
  } else {
    no_16550 = 109;                             // No 16550 and no CD music - set error code 109 (no audio support)
  }
  if (no_16550 == 109)                        // Check if complete audio failure occurred (code 109)
  {
    musicon = 0;                                // Disable music playback due to hardware failure
    reinitmusic();                              // Reinitialize music system with new settings
  }
  if (no_16550)                               // Check if any 16550 error occurred
    soundon = 0;                                // Disable all sound effects if 16550 hardware issues detected
}

//-------------------------------------------------------------------------------------------------
