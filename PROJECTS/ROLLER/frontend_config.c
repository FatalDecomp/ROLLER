#include "frontend.h"
#include "graphics.h"
#include "3d.h"
#include "func2.h"
#include "sound.h"
#include "roller.h"
#include "car.h"
#include "moving.h"
#include "network.h"
#include "loadtrak.h"
#include "control.h"
#include "drawtrk3.h"
#include "cdx.h"
#include "polytex.h"
#include "comms.h"
#include "colision.h"
#include "rollercomms.h"
#include "menu_render.h"
#include "snapshot.h"
#include <fcntl.h>
#include <string.h>
#ifdef IS_WINDOWS
#include <io.h>
#define open _open
#define close _close
#else
#include <inttypes.h>
#include <unistd.h>
#define O_BINARY 0 //linux does not differentiate between text and binary
#endif

// File-static state for frontend config screen (persists across dispatcher ticks)
static int iFrontendConfigExitFlag;
static int iFrontendConfigMenuSelection;
static int iFrontendConfigEditingName;
static int iFrontendConfigControlsInEdit;
static int iFrontendConfigState;
static int iFrontendConfigVolumeSelection;
static int iFrontendConfigVideoState;
static int iFrontendConfigControlSelection;
static int iFrontendConfigSelectedCar;
static int iFrontendConfigNameLength;
static char szFrontendConfigNewNameBuf[12];
static tJoyPos jFrontendConfigJoyPos;
static int iFrontendConfigGraphicsState;
static int iFrontendConfigNetworkState;
static int iFrontendConfigBroadcastWaitAction;

enum {
  FRONTEND_CONFIG_BROADCAST_WAIT_NONE = 0,
  FRONTEND_CONFIG_BROADCAST_WAIT_CHECK_PLAYER1_NAME,
  FRONTEND_CONFIG_BROADCAST_WAIT_CHECK_PLAYER1_NAME_AND_CARS
};

static void frontend_config_begin_broadcast_wait(int iBroadcastMode, int iAction)
{
  iFrontendConfigBroadcastWaitAction = iAction;
  network_broadcast_wait_start(iBroadcastMode, 1);
}

static void frontend_config_finish_broadcast_wait(void)
{
  int iAction = iFrontendConfigBroadcastWaitAction;
  iFrontendConfigBroadcastWaitAction = FRONTEND_CONFIG_BROADCAST_WAIT_NONE;

  switch (iAction) {
    case FRONTEND_CONFIG_BROADCAST_WAIT_CHECK_PLAYER1_NAME_AND_CARS:
      if (!network_on)
        waste = CheckNames(player_names[player1_car], player1_car);
      check_cars();
      break;
    case FRONTEND_CONFIG_BROADCAST_WAIT_CHECK_PLAYER1_NAME:
      if (!network_on)
        waste = CheckNames(player_names[player1_car], player1_car);
      break;
    default:
      break;
  }
}

static int frontend_config_update_broadcast_wait(void)
{
  if (!network_broadcast_wait_active())
    return 0;

  if (network_broadcast_wait_update())
    frontend_config_finish_broadcast_wait();

  return -1;
}

void frontend_config_enter(void)
{
  fade_palette(0);
  iFrontendConfigExitFlag = 0;
  iFrontendConfigMenuSelection = 7;
  iFrontendConfigEditingName = 0;
  iFrontendConfigControlsInEdit = 0;
  front_fade = 0;
  iFrontendConfigState = 0;
  iFrontendConfigBroadcastWaitAction = FRONTEND_CONFIG_BROADCAST_WAIT_NONE;
  {
    extern tColor palette[];
    memcpy(pal_addr, palette, 256 * sizeof(tColor));
    palette_brightness = 32;
  }
}

void frontend_config_exit(void)
{
  MenuRenderer *mr = GetMenuRenderer();
  menu_render_begin_fade(mr, 0, 32);
  menu_render_fade_wait(mr, fade_redraw_bg, mr);
  palette_brightness = 0;
  for (int i = 0; i < 256; i++) {
    pal_addr[i].byR = 0;
    pal_addr[i].byB = 0;
    pal_addr[i].byG = 0;
  }
  front_fade = 0;
}

//-------------------------------------------------------------------------------------------------
//00042D40
void frontend_config_update(void)
{
  char *szString; // eax
  int iCharIndex; // edx
  int iCarIndex; // esi
  const char *szCarName; // edi
  uint8 byTextColor1; // al
  uint8 byTextColor2; // al
  uint8 byTextColor3; // al
  uint8 byTextColor4; // al
  char byChar; // al
  int iFontWidth; // eax
  uint8 byTextColor5; // al
  uint8 byTextColor6; // al
  uint8 byTextColor7; // al
  int iTemp1; // edi
  uint8 byTextColor8; // al
  uint8 byTextColor9; // al
  uint8 byColor; // al
  int iFrontendConfigSelectedCar_1; // edi
  uint8 byColor_1; // al
  uint8 byColor_2; // al
  char byVolumeColor1; // al
  char byVolumeColor2; // al
  char byVolumeColor3; // al
  char byVolumeColor4; // al
  char byColor_3; // al
  char byColor_4; // al
  char byColor_5; // al
  char byColor_6; // al
  char byColor_7; // al
  char byColor_9; // al
  char byColor_8; // al
  char byColor_10; // al
  char byColor_11; // al
  char byColor_13; // al
  char byColor_12; // al
  char byColor_14; // al
  int byColor_15; // ebx
  int byColor_16; // ebx
  int byColor_17; // ebx
  int iFrontendConfigVolumeSelection_1; // ecx
  int byColor_18; // ebx
  int iFrontendConfigState_1; // edi
  int iJoyCalibValue1; // ebx
  char *szJoyStatus1; // edx
  int iJoyCalibValue2; // ebx
  char *szJoyStatus2; // edx
  int iX2CalibrationVal; // ebx
  char *szX2Text; // edx
  int iFrontendConfigState_2; // edi
  int iY2CalibrationVal; // ebx
  char *szY2Text; // edx
  int iKeyFound; // ebx
  int iKeyIndex; // eax
  int iKeyCounter; // edx
  char byColor_19; // al
  char byColor_20; // al
  char byColor_21; // al
  char byColor_22; // al
  char byColor_23; // al
  int iControlLoop; // esi
  const char *szControlName; // edi
  uint8 byControlColor; // al
  char byColor_24; // al
  uint8 byColor_25; // al
  char byColor_26; // al
  int iControlIndex2; // esi
  const char *szText; // edi
  uint8 byColor_27; // al
  uint8 byColor_28; // al
  uint8 byColor_29; // al
  uint8 byColor_30; // al
  int iKeyCheckLoop; // eax
  int iFoundKey; // ecx
  int iKeySearchIndex; // edx
  int iJoyValue1; // eax
  int iJoyValue2; // eax
  int iJoyValue3; // eax
  int iJoyValue4; // eax
  int iDuplicateCheck; // ebx
  int i; // eax
  int iEditIndex; // eax
  int iControlState; // ebx
  char byColor_31; // al
  char byColor_32; // al
  char *szText_1; // edx
  char byColor_33; // al
  char byColor_34; // al
  char byColor_36; // al
  char byColor_105; // al
  char byColor_35; // al
  char byColor_37; // al
  char byColor_38; // al
  char byColor_39; // al
  char byColor_40; // al
  char byColor_41; // al
  char byColor_42; // al
  char byColor_43; // al
  char byColor_44; // al
  char byColor_45; // al
  char byColor_46; // al
  char byColor_47; // al
  char byColor_48; // al
  char byColor_49; // al
  char byColor_50; // al
  char byColor_51; // al
  char byColor_52; // al
  char byColor_53; // al
  char byColor_54; // al
  char byColor_55; // al
  char byColor_57; // al
  char byColor_56; // al
  char byColor_58; // al
  char byColor_59; // al
  char byColor_60; // al
  char byColor_61; // al
  char byColor_62; // al
  char byColor_63; // al
  char byColor_64; // al
  char byColor_65; // al
  char byColor_66; // al
  char byColor_67; // al
  char byColor_68; // al
  char byColor_69; // al
  char byColor_70; // al
  char byColor_71; // al
  char byColor_72; // al
  char byColor_73; // al
  char byColor_74; // al
  int iReturnValue; // eax
  char byColor_76; // al
  char byColor_75; // al
  char byColor_77; // al
  char byColor_78; // al
  char byColor_79; // al
  char byColor_80; // al
  char byColor_81; // al
  char byColor_82; // al
  char byColor_83; // al
  char byColor_84; // al
  char byColor_85; // al
  char byColor_86; // al
  char byColor_87; // al
  char byColor_88; // al
  char byColor_89; // al
  char byColor_90; // al
  char byColor_91; // al
  char byColor_92; // al
  char byColor_93; // al
  char byColor_94; // al
  char byColor_95; // al
  char byColor_96; // di
  uint8 byColor_97; // si
  char *szMemPtr; // eax
  int iFontChar; // edx
  uint8 byColor_106; // al
  uint8 byColor_98; // al
  char byColor_99; // al
  uint8 byColor_100; // al
  char byColor_101; // al
  uint8 byColor_102; // al
  char byColor_103; // al
  uint8 byColor_104; // al
  int iFrontendConfigNetworkState_1; // edi
  int iX; // eax
  int iPlayerIdx_1; // eax
  //int iPlayersCarsOffset_1; // edx
  int iPlayerIdx; // eax
  //int iPlayersCarsOffset; // edx
  //uint32 uiTempCheatMode; // eax
  unsigned int uiKeyCode; // eax
  unsigned int uiExtendedKey; // eax
  int iMenuDir; // edi
  int iMenuDir2; // edi
  int iKeyInput; // eax
  int iKeyChar; // ecx
  unsigned int uiArrowKey; // eax
  int iPrevSelectedCar; // edx
  int iNextSelectedCar; // edx
  int iFrontendConfigNameLength_1; // eax
  int iFrontendConfigEditingName_1; // edi
  int j; // ecx
  int iPlayer2Car; // edx
  int k; // ecx
  int iDefaultNamesIdx_1; // edx
  int iDefaultNamesCharItr; // ecx
  int iDefaultNamesIdx; // edx
  int m; // ecx
  int n; // ecx
  int iAIDriverIdx; // eax
  //int v189; // ecx
  //int iOffset; // eax
  //char v191; // dl
  int ii; // ecx
  int iPlayer2Car_1; // edx
  int jj; // ecx
  int iAIDriverIdx_1; // eax
  int v196; // ecx
  int v197; // edx
  int iFrontendConfigNameLength_2; // edi
  unsigned int uiKey_5; // eax
  int iNextVolumeSelection; // edi
  unsigned int uiKey; // eax
  unsigned int uiKey_1; // eax
  unsigned int uiKey_6; // eax
  int iNextControlSelection; // edi
  int iDisplayIndex; // edi
  unsigned int uiKey_2; // eax
  uint32 uiTexOffTemp_7; // eax
  uint32 uiTexOffTemp; // edx
  uint32 uiTexOffTemp_1; // eax
  uint32 uiTexOffTemp_2; // ecx
  uint32 uiTexOffTemp_3; // ebx
  uint32 uiTexOffTemp_4; // edx
  uint32 uiTexOffTemp_5; // eax
  unsigned int uiKey_3; // eax
  int iPlayerIndex; // esi
  uint32 uiTexOffTemp_6; // edx
  unsigned int uiKey_4; // eax
  unsigned int uiDataValue1; // edx
  int iGameIndex; // ecx
  unsigned int uiDataValue2; // eax
  int iPlayerIndex2; // esi
  unsigned int uiDataValue3; // eax
  int iDataIndex; // edx
  int iCounterVar; // ecx
  char byTempFlag; // dl
  bool kk; // zf
  char byStatusFlag; // bl
  int iResultValue; // eax
  int iCalculation; // ebx
  char byTempChar1; // [esp-10h] [ebp-16h]
  char byTempChar2; // [esp-10h] [ebp-16h]
  uint8 byTempValue; // [esp-8h] [ebp-Eh]
  int iY; // [esp+2Ch] [ebp+26h]
  int iTextPosX; // [esp+30h] [ebp+2Ah]
  int iCarDisplay; // [esp+64h] [ebp+5Eh]
  int iDimmedColor; // [esp+68h] [ebp+62h]
  int iActiveColor; // [esp+6Ch] [ebp+66h]
  int iY_2; // [esp+70h] [ebp+6Ah]
  int iY_1; // [esp+74h] [ebp+6Eh]
  int iTextPosY; // [esp+78h] [ebp+72h]
  int iNormalColor; // [esp+7Ch] [ebp+76h]
  int iHighlightColor; // [esp+80h] [ebp+7Ah]
  int iCarLoop; // [esp+84h] [ebp+7Eh]

  if (switch_types) {
      game_type = switch_types - 1;
      if (switch_types == 1 && competitors == 1)
        competitors = 16;
      switch_types = 0;
      if (game_type == 1)
        Race = ((uint8)TrackLoad - 1) & 7;
      else
        network_champ_on = 0;
    }

    // Draw background and ui elements (GPU)
    MenuRenderer *mr = GetMenuRenderer();
    menu_render_begin_frame(mr);
    if (!front_fade) {
      front_fade = -1;
      menu_render_begin_fade(mr, 1, 32);
    }
    menu_render_background(mr, 0);
    menu_render_sprite(mr, 1, 0, head_x, head_y, 0, pal_addr);
    menu_render_sprite(mr, 6, 0, 36, 2, 0, pal_addr);
    menu_render_sprite(mr, 5, player_type, -4, 247, 0, pal_addr);
    menu_render_sprite(mr, 5, game_type + 5, 135, 247, 0, pal_addr);
    menu_render_sprite(mr, 4, 1, 76, 257, -1, pal_addr);

    // draw menu selector
    if (iFrontendConfigMenuSelection >= 7) {
      // no menu item selected (exit)
      menu_render_sprite(mr, 6, 4, 62, 336, -1, pal_addr);
    } else {
      // draw menu selector
      menu_render_sprite(mr, 6, 2, 62, 336, -1, pal_addr);
      menu_render_text(mr, 2, "~", font2_ascii, font2_offsets, sel_posns[iFrontendConfigMenuSelection].x, sel_posns[iFrontendConfigMenuSelection].y, 0x8Fu, 0, pal_addr);
    }

    // menu options labels
    menu_render_text(mr, 2, &config_buffer[3968], font2_ascii, font2_offsets, sel_posns[0].x + 132, sel_posns[0].y + 7, 0x8Fu, 2u, pal_addr);
    menu_render_text(mr, 2, &config_buffer[256], font2_ascii, font2_offsets, sel_posns[1].x + 132, sel_posns[1].y + 7, 0x8Fu, 2u, pal_addr);
    menu_render_text(mr, 2, &config_buffer[1664], font2_ascii, font2_offsets, sel_posns[2].x + 132, sel_posns[2].y + 7, 0x8Fu, 2u, pal_addr);
    menu_render_text(mr, 2, &config_buffer[4032], font2_ascii, font2_offsets, sel_posns[3].x + 132, sel_posns[3].y + 7, 0x8Fu, 2u, pal_addr);
    menu_render_text(mr, 2, &config_buffer[4096], font2_ascii, font2_offsets, sel_posns[4].x + 132, sel_posns[4].y + 7, 0x8Fu, 2u, pal_addr);
    menu_render_text(mr, 2, &config_buffer[4160], font2_ascii, font2_offsets, sel_posns[5].x + 132, sel_posns[5].y + 7, 0x8Fu, 2u, pal_addr);

    // network option if enabled
    if (network_on)
      menu_render_text(mr, 2, &config_buffer[5568], font2_ascii, font2_offsets, sel_posns[6].x + 132, sel_posns[6].y + 7, 0x8Fu, 2u, pal_addr);

    // Config state machine
    switch (iFrontendConfigMenuSelection) {
      case 0:
        if (iFrontendConfigEditingName == 1) {
          iHighlightColor = 0xAB;
          iNormalColor = 0xA5;
        } else {
          iHighlightColor = 0xA5;
          iNormalColor = 0xAB;
        }
        if (iFrontendConfigState != 1) {
          iHighlightColor = 0x8F;
          iNormalColor = 0x8F;
        }
        if (iFrontendConfigEditingName == 1) {
          iTextPosX = 0;
          szString = szFrontendConfigNewNameBuf;
          while (*szString) {
            iCharIndex = (uint8)font1_ascii[(uint8)*szString++];
            if (iCharIndex == 255)
              iTextPosX += 8;
            else
              iTextPosX += front_vga[15][iCharIndex].iWidth + 1;
          }
          iTextPosX += 430;
          iY = 374 - 18 * iFrontendConfigSelectedCar;
        }

        // Init car display loop
        iCarLoop = 0;
        iDimmedColor = iHighlightColor - 2;
        iCarDisplay = 15;
        iCarIndex = 18;
        iActiveColor = iNormalColor - 4;
        szCarName = comp_name[0];
        iTextPosY = 50;

        // Display all 16 cars in reverse order
        do {
          // Check if car slot is allocated to a player
          if ((iCarLoop & 1) >= allocated_cars[iCarDisplay / 2]) {
            // Car is available for Ai palyers
            if (iCarIndex == iFrontendConfigSelectedCar && iFrontendConfigEditingName == 1) {
              // Selected car with name being edited
              menu_render_text(mr, 15, szCarName, font1_ascii, font1_offsets, 425, iTextPosY, iNormalColor, 2u, pal_addr);
              if (iCarIndex == iFrontendConfigSelectedCar)
                byTextColor3 = iHighlightColor;
              else
                byTextColor3 = 0x8F;
              menu_render_text(mr, 15, szFrontendConfigNewNameBuf, font1_ascii, font1_offsets, 430, iTextPosY, byTextColor3, 0, pal_addr);
            } else {
              // Selected car with default name displayed
              if (iCarIndex == iFrontendConfigSelectedCar)
                byTextColor4 = iNormalColor;
              else
                byTextColor4 = 0x8F;
              menu_render_text(mr, 15, szCarName, font1_ascii, font1_offsets, 425, iTextPosY, byTextColor4, 2u, pal_addr);
              if (iCarIndex == iFrontendConfigSelectedCar)
                byChar = iHighlightColor;
              else
                byChar = 0x8F;

              // Display AI name (using bit toggle for paired names)
              byTempValue = byChar;
              iFontWidth = iCarDisplay;
              //LOBYTE(iFontWidth) = iCarDisplay ^ 1;
              iFontWidth = iCarDisplay ^ 1;
              menu_render_text(mr, 15, default_names[iFontWidth], font1_ascii, font1_offsets, 430, iTextPosY, byTempValue, 0, pal_addr);
            }
          } else {
            // Car is allocated to a human player
            if (iCarIndex == iFrontendConfigSelectedCar)
              byTextColor1 = iActiveColor;
            else
              byTextColor1 = 0x8B;
            menu_render_text(mr, 15, szCarName, font1_ascii, font1_offsets, 425, iTextPosY, byTextColor1, 2u, pal_addr);
            if (iCarIndex == iFrontendConfigSelectedCar)
              byTextColor2 = iDimmedColor;
            else
              byTextColor2 = 0x7F;

            // Display human player name
            menu_render_text(mr, 15, player_names[car_to_player[14 - (iCarLoop & 0xFE) + (iCarLoop & 1)]], font1_ascii, font1_offsets, 430, iTextPosY, byTextColor2, 0, pal_addr);
          }

          // Move to next car
          --iCarIndex;
          szCarName += 15;
          --iCarDisplay;
          iTextPosY += 18;
          ++iCarLoop;
        } while (iCarLoop < 16);

        // Display player 2 configuration (if in 2-player mode)
        if (player_type == 2) {
          if (iFrontendConfigSelectedCar == 2 && iFrontendConfigEditingName == 1) {
            // Player 2 name being edited
            if (iFrontendConfigSelectedCar == player_type)
              byTextColor5 = iNormalColor;
            else
              byTextColor5 = 0x8F;
            menu_render_text(mr, 15, &config_buffer[4288], font1_ascii, font1_offsets, 425, 338, byTextColor5, player_type, pal_addr);
            if (iFrontendConfigSelectedCar == 2)
              byTextColor6 = iHighlightColor;
            else
              byTextColor6 = 0x8F;
            menu_render_text(mr, 15, szFrontendConfigNewNameBuf, font1_ascii, font1_offsets, 430, 338, byTextColor6, 0, pal_addr);
          } else {
            // Player 2 name display mode
            if (iFrontendConfigSelectedCar == 2)
              byTextColor7 = iNormalColor;
            else
              byTextColor7 = 0x8F;
            iTemp1 = iFrontendConfigSelectedCar;
            menu_render_text(mr, 15, &config_buffer[4288], font1_ascii, font1_offsets, 425, 338, byTextColor7, 2u, pal_addr);
            if (iTemp1 == 2)
              byTextColor8 = iHighlightColor;
            else
              byTextColor8 = 0x8F;
            menu_render_text(mr, 15, player_names[player2_car], font1_ascii, font1_offsets, 430, 338, byTextColor8, 0, pal_addr);
          }
        }

        // Display player 1 configuration
        if (iFrontendConfigSelectedCar == 1 && iFrontendConfigEditingName == 1) {
          // Player 1 name being edited
          menu_render_text(mr, 15, &config_buffer[4224], font1_ascii, font1_offsets, 425, 356, iNormalColor, 2u, pal_addr);
          if (iFrontendConfigSelectedCar == 1)
            byTextColor9 = iHighlightColor;
          else
            byTextColor9 = 0x8F;
          menu_render_text(mr, 15, szFrontendConfigNewNameBuf, font1_ascii, font1_offsets, 430, 356, byTextColor9, 0, pal_addr);
        } else {
          // Player 1 name display mode
          if (iFrontendConfigSelectedCar == 1)
            byColor = iNormalColor;
          else
            byColor = 0x8F;
          iFrontendConfigSelectedCar_1 = iFrontendConfigSelectedCar;
          menu_render_text(mr, 15, &config_buffer[4224], font1_ascii, font1_offsets, 425, 356, byColor, 2u, pal_addr);
          if (iFrontendConfigSelectedCar_1 == 1)
            byColor_1 = iHighlightColor;
          else
            byColor_1 = 0x8F;
          menu_render_text(mr, 15, player_names[player1_car], font1_ascii, font1_offsets, 430, 356, byColor_1, 0, pal_addr);
        }

        // Display "BACK" option
        if (iFrontendConfigSelectedCar)
          byColor_2 = 0x8F;
        else
          byColor_2 = iNormalColor;
        menu_render_text(mr, 15, &config_buffer[832], font1_ascii, font1_offsets, 420, 374, byColor_2, 2u, pal_addr);

        // Display blinking cursor when editing names
        if (iFrontendConfigEditingName == 1) {
          if ((frames & 0xFu) < 8)            // blink cursor based on frame counter
            menu_render_text(mr, 15, "_", font1_ascii, font1_offsets, iTextPosX, iY, 0xABu, 0, pal_addr);
          szFrontendConfigNewNameBuf[iFrontendConfigNameLength] = 0;
        }
        goto RENDER_FRAME;                      // skip to end of switch
      case 1:
        // Audio/volume config
        if (iFrontendConfigState != 2)
          iFrontendConfigVolumeSelection = -1;

        // Engine volume
        if (iFrontendConfigVolumeSelection == 1)
          byVolumeColor1 = 0xAB;
        else
          byVolumeColor1 = 0x8F;
        menu_render_scaled_text(mr, 15, &config_buffer[2304], font1_ascii, font1_offsets, 425, 80, byVolumeColor1, 2u, 200, 640, pal_addr);

        // SFX volume
        if (iFrontendConfigVolumeSelection == 2)
          byVolumeColor2 = 0xAB;
        else
          byVolumeColor2 = 0x8F;
        menu_render_scaled_text(mr, 15, &config_buffer[2368], font1_ascii, font1_offsets, 425, 104, byVolumeColor2, 2u, 200, 640, pal_addr);

        // Speech volume
        if (iFrontendConfigVolumeSelection == 3)
          byVolumeColor3 = 0xAB;
        else
          byVolumeColor3 = 0x8F;
        menu_render_scaled_text(mr, 15, &config_buffer[2432], font1_ascii, font1_offsets, 425, 128, byVolumeColor3, 2u, 200, 640, pal_addr);

        // Music volume
        if (iFrontendConfigVolumeSelection == 4)
          byVolumeColor4 = 0xAB;
        else
          byVolumeColor4 = 0x8F;
        menu_render_scaled_text(mr, 15, &config_buffer[2496], font1_ascii, font1_offsets, 425, 152, byVolumeColor4, 2u, 200, 640, pal_addr);

        // Engine options
        if (iFrontendConfigVolumeSelection == 5)
          byColor_3 = 0xAB;
        else
          byColor_3 = 0x8F;
        menu_render_scaled_text(mr, 15, &config_buffer[2560], font1_ascii, font1_offsets, 425, 176, byColor_3, 2u, 200, 640, pal_addr);
        if (allengines) {
          if (iFrontendConfigVolumeSelection == 5)
            byColor_4 = 0xAB;
          else
            byColor_4 = 0x8F;
          // ALL ENGINES
          menu_render_scaled_text(mr, 15, &config_buffer[2752], font1_ascii, font1_offsets, 430, 176, byColor_4, 0, 200, 640, pal_addr);
        } else {
          if (iFrontendConfigVolumeSelection == 5)
            byColor_5 = 0xAB;
          else
            byColor_5 = 0x8F;
          // STARTERS & TURBOS
          menu_render_scaled_text(mr, 15, &config_buffer[2816], font1_ascii, font1_offsets, 430, 176, byColor_5, 0, 200, 640, pal_addr);
        }

        // Sound effects options
        if (iFrontendConfigVolumeSelection == 6)
          byColor_6 = 0xAB;
        else
          byColor_6 = 0x8F;
        menu_render_scaled_text(mr, 15, &config_buffer[2880], font1_ascii, font1_offsets, 425, 200, byColor_6, 2u, 200, 640, pal_addr);
        if (soundon) {
          if (iFrontendConfigVolumeSelection == 6)
            byColor_7 = 0xAB;
          else
            byColor_7 = 0x8F;
          // ON
          menu_render_scaled_text(mr, 15, &config_buffer[2624], font1_ascii, font1_offsets, 430, 200, byColor_7, 0, 200, 640, pal_addr);
        } else if (SoundCard) {
          if (iFrontendConfigVolumeSelection == 6)
            byColor_8 = 0xAB;
          else
            byColor_8 = 0x8F;
          // OFF
          menu_render_scaled_text(mr, 15, &config_buffer[2688], font1_ascii, font1_offsets, 430, 200, byColor_8, soundon, 200, 640, pal_addr);
        } else {
          if (iFrontendConfigVolumeSelection == 6)
            byColor_9 = 0xAB;
          else
            byColor_9 = 0x8F;
          // DISABLED
          menu_render_scaled_text(mr, 15, &config_buffer[6848], font1_ascii, font1_offsets, 430, 200, byColor_9, soundon, 200, 640, pal_addr);
        }

        // Music options
        if (iFrontendConfigVolumeSelection == 7)
          byColor_10 = 0xAB;
        else
          byColor_10 = 0x8F;
        menu_render_scaled_text(mr, 15, &config_buffer[2944], font1_ascii, font1_offsets, 425, 224, byColor_10, 2u, 200, 640, pal_addr);
        if (musicon) {
          if (iFrontendConfigVolumeSelection == 7)
            byColor_11 = 0xAB;
          else
            byColor_11 = 0x8F;
          // ON
          menu_render_scaled_text(mr, 15, &config_buffer[2624], font1_ascii, font1_offsets, 430, 224, byColor_11, 0, 200, 640, pal_addr);
        } else if (MusicCard || MusicCD) {
          if (iFrontendConfigVolumeSelection == 7)
            byColor_12 = 0xAB;
          else
            byColor_12 = 0x8F;
          // OFF
          menu_render_scaled_text(mr, 15, &config_buffer[2688], font1_ascii, font1_offsets, 430, 224, byColor_12, 0, 200, 640, pal_addr);
        } else {
          if (iFrontendConfigVolumeSelection == 7)
            byColor_13 = 0xAB;
          else
            byColor_13 = 0x8F;
          // DISABLED
          menu_render_scaled_text(mr, 15, &config_buffer[6848], font1_ascii, font1_offsets, 430, 224, byColor_13, musicon, 200, 640, pal_addr);
        }

        // Back option
        if (iFrontendConfigVolumeSelection)
          byColor_14 = 0x8F;
        else
          byColor_14 = 0xAB;
        menu_render_scaled_text(mr, 15, &config_buffer[832], font1_ascii, font1_offsets, 420, 248, byColor_14, 2u, 200, 640, pal_addr);

        // Display volume bars
        if (iFrontendConfigVolumeSelection == 1)
          byColor_15 = 0xAB;
        else
          byColor_15 = 0xA5;
        front_volumebar(80, EngineVolume, byColor_15);
        if (iFrontendConfigVolumeSelection == 2)
          byColor_16 = 0xAB;
        else
          byColor_16 = 0xA5;
        front_volumebar(104, SFXVolume, byColor_16);
        if (iFrontendConfigVolumeSelection == 3)
          byColor_17 = 0xAB;
        else
          byColor_17 = 0xA5;
        iFrontendConfigVolumeSelection_1 = iFrontendConfigVolumeSelection;
        front_volumebar(128, SpeechVolume, byColor_17);
        if (iFrontendConfigVolumeSelection_1 == 4)
          byColor_18 = 0xAB;
        else
          byColor_18 = 0xA5;
        front_volumebar(152, MusicVolume, byColor_18);
        goto RENDER_FRAME;
      case 2:
        // Joystick calibration
        if (iFrontendConfigState == 3) {
          ReadJoys(&jFrontendConfigJoyPos);
          //_disable();

          // Update calibration ranges for all axes
          if (jFrontendConfigJoyPos.iJ1XAxis < JAXmin)
            JAXmin = jFrontendConfigJoyPos.iJ1XAxis;
          if (jFrontendConfigJoyPos.iJ1XAxis > JAXmax)
            JAXmax = jFrontendConfigJoyPos.iJ1XAxis;

          if (jFrontendConfigJoyPos.iJ1YAxis < JAYmin)
            JAYmin = jFrontendConfigJoyPos.iJ1YAxis;
          if (jFrontendConfigJoyPos.iJ1YAxis > JAYmax)
            JAYmax = jFrontendConfigJoyPos.iJ1YAxis;

          if (jFrontendConfigJoyPos.iJ2XAxis < JBXmin)
            JBXmin = jFrontendConfigJoyPos.iJ2XAxis;
          if (jFrontendConfigJoyPos.iJ2XAxis > JBXmax)
            JBXmax = jFrontendConfigJoyPos.iJ2XAxis;

          if (jFrontendConfigJoyPos.iJ2YAxis < JBYmin)
            JBYmin = jFrontendConfigJoyPos.iJ2YAxis;
          if (jFrontendConfigJoyPos.iJ2YAxis > JBYmax)
            JBYmax = jFrontendConfigJoyPos.iJ2YAxis;

          if (JAXmin == JAXmax)
            JAXmax = JAXmin + 1;
          if (JAYmin == JAYmax)
            JAYmax = JAYmin + 1;

          if (JBXmin == JBXmax)
            JBXmax = JBXmin + 1;
          if (JBYmin == JBYmax)
            JBYmax = JBYmin + 1;
          //_enable();
        }

        // Display calibration instructions when active
        if (iFrontendConfigState == 3) {
          // MOVE JOYSTICKS TO FULL EXTENTS
          menu_render_scaled_text(mr, 15, &config_buffer[2112], font1_ascii, font1_offsets, 400, 60, 143, 1u, 200, 640, pal_addr);
          // THEN PRESS ANY KEY
          menu_render_scaled_text(mr, 15, &config_buffer[2176], font1_ascii, font1_offsets, 400, 78, 143, 1u, 200, 640, pal_addr);
        }

        iFrontendConfigState_1 = iFrontendConfigState;

        // X1 axis display
        menu_render_scaled_text(mr, 15, &config_buffer[1728], font1_ascii, font1_offsets, 400, 110, 143, 1u, 200, 640, pal_addr);
        if (iFrontendConfigState_1 == 3) {
          // Show calibration bar
          if (x1ok && JAXmax - JAXmin >= 100)
            iJoyCalibValue1 = 140 * (2 * jFrontendConfigJoyPos.iJ1XAxis - JAXmax - JAXmin) / (JAXmax - JAXmin);
          else
            iJoyCalibValue1 = 0;
          front_displaycalibrationbar(300, 128, iJoyCalibValue1);
        } else {
          // Show status text
          if (x1ok)
            szJoyStatus1 = &config_buffer[2048];
          else
            szJoyStatus1 = &config_buffer[1984];
          menu_render_scaled_text(mr, 15, szJoyStatus1, font1_ascii, font1_offsets, 400, 128, 143, 1u, 200, 640, pal_addr);
        }

        // Y1 axis display
        menu_render_scaled_text(mr, 15, &config_buffer[1792], font1_ascii, font1_offsets, 400, 160, 143, 1u, 200, 640, pal_addr);
        if (iFrontendConfigState == 3) {
          // Show Calibration bar
          if (y1ok && JAYmax - JAYmin >= 100)
            iJoyCalibValue2 = 140 * (2 * jFrontendConfigJoyPos.iJ1YAxis - JAYmax - JAYmin) / (JAYmax - JAYmin);
          else
            iJoyCalibValue2 = 0;
          front_displaycalibrationbar(300, 178, iJoyCalibValue2);
        } else {
          // Show status text
          if (y1ok)
            szJoyStatus2 = &config_buffer[2048];
          else
            szJoyStatus2 = &config_buffer[1984];
          menu_render_scaled_text(mr, 15, szJoyStatus2, font1_ascii, font1_offsets, 400, 178, 143, 1u, 200, 640, pal_addr);
        }

        // X2 axis display
        menu_render_scaled_text(mr, 15, &config_buffer[1856], font1_ascii, font1_offsets, 400, 210, 143, 1u, 200, 640, pal_addr);
        if (iFrontendConfigState == 3) {
          // Calibration bar
          if (x2ok && JBXmax - JBXmin >= 100)
            iX2CalibrationVal = 140 * (2 * jFrontendConfigJoyPos.iJ2XAxis - JBXmax - JBXmin) / (JBXmax - JBXmin);
          else
            iX2CalibrationVal = 0;
          front_displaycalibrationbar(300, 228, iX2CalibrationVal);
        } else {
          // status text
          if (x2ok)
            szX2Text = &config_buffer[2048];
          else
            szX2Text = &config_buffer[1984];
          menu_render_scaled_text(mr, 15, szX2Text, font1_ascii, font1_offsets, 400, 228, 143, 1u, 200, 640, pal_addr);
        }

        iFrontendConfigState_2 = iFrontendConfigState;

        // Y2 axis display
        menu_render_scaled_text(mr, 15, &config_buffer[1920], font1_ascii, font1_offsets, 400, 260, 143, 1u, 200, 640, pal_addr);
        if (iFrontendConfigState_2 == 3) {
          // Calibration bar
          if (y2ok && JBYmax - JBYmin >= 100)
            iY2CalibrationVal = 140 * (2 * jFrontendConfigJoyPos.iJ2YAxis - JBYmax - JBYmin) / (JBYmax - JBYmin);
          else
            iY2CalibrationVal = 0;
          front_displaycalibrationbar(300, 278, iY2CalibrationVal);
        } else {
          // Status text
          if (y2ok)
            szY2Text = &config_buffer[2048];
          else
            szY2Text = &config_buffer[1984];
          menu_render_scaled_text(mr, 15, szY2Text, font1_ascii, font1_offsets, 400, 278, 143, 1u, 200, 640, pal_addr);
        }
        goto RENDER_FRAME;
      case 3:
        // Keyboard control config
        if (iFrontendConfigState == 4) {
          if (controlrelease) {
            // Check if all keys have been released
            iKeyFound = -1;
            iKeyIndex = 0;
            iKeyCounter = 0;
            do {
              if (keyname[iKeyCounter] && keys[iKeyIndex])
                iKeyFound = 0;                  // key still pressed
              ++iKeyIndex;
              ++iKeyCounter;
            } while (iKeyIndex < 128);
            if (iKeyFound)                    // all keys released
              controlrelease = 0;
          }
        } else {
          iFrontendConfigControlSelection = -1;               // reset control selection
        }

        // Display player 2 controls (if in 2 player mode)
        if (player_type == 2) {
          // Format player 2 control method string
          if (manual_control[player2_car] == 2)
            sprintf(buffer, "%s %s", &config_buffer[4480], &config_buffer[4544]);
          else
            sprintf(buffer, "%s %s", &config_buffer[4480], &config_buffer[4608]);
          if (iFrontendConfigControlSelection == 4)
            byColor_19 = 0xAB;
          else
            byColor_19 = 0x8F;
          menu_render_scaled_text(mr, 15, buffer, font1_ascii, font1_offsets, 420, 60, byColor_19, 1u, 200, 640, pal_addr);

          // Player 2 customize controls option
          if (iFrontendConfigControlSelection == 3)
            byColor_20 = 0xAB;
          else
            byColor_20 = 0x8F;
          // CUSTOMIZE PLAYER 2
          menu_render_scaled_text(mr, 15, &config_buffer[704], font1_ascii, font1_offsets, 420, 78, byColor_20, 1u, 200, 640, pal_addr);
        }

        // Display player 1 controls
        if (manual_control[player1_car] == 2)
          sprintf(buffer, "%s %s", &config_buffer[4416], &config_buffer[4544]);
        else
          sprintf(buffer, "%s %s", &config_buffer[4416], &config_buffer[4608]);
        if (iFrontendConfigControlSelection == 2)
          byColor_21 = 0xAB;
        else
          byColor_21 = 0x8F;
        menu_render_scaled_text(mr, 15, buffer, font1_ascii, font1_offsets, 420, 96, byColor_21, 1u, 200, 640, pal_addr);
        // Player 1 customize controls option
        if (iFrontendConfigControlSelection == 1)
          byColor_22 = 0xAB;
        else
          byColor_22 = 0x8F;
        // CUSTOMIZE PLAYER 1
        menu_render_scaled_text(mr, 15, &config_buffer[768], font1_ascii, font1_offsets, 420, 114, byColor_22, 1u, 200, 640, pal_addr);

        // Back option
        if (iFrontendConfigControlSelection)
          byColor_23 = 0x8F;
        else
          byColor_23 = 0xAB;
        menu_render_scaled_text(mr, 15, &config_buffer[832], font1_ascii, font1_offsets, 420, 132, byColor_23, 1u, 200, 640, pal_addr);

        // Display player 1 control customization screen
        if (iFrontendConfigControlSelection == 1 || iFrontendConfigControlSelection == 2) {
          iControlLoop = 0;
          szControlName = &config_buffer[896];  // start of control name strings
          iY_1 = 200;
          // Display all 6 basic controls for player 1
          do {
            if (iControlLoop == control_edit)
              byControlColor = 0xAB;
            else
              byControlColor = 0x8F;
            menu_render_text(mr, 15, szControlName, font1_ascii, font1_offsets, 475, iY_1, byControlColor, 2u, pal_addr);
            if (iControlLoop == control_edit)
              byColor_24 = 0xAB;
            else
              byColor_24 = 0x8F;
            menu_render_scaled_text(mr, 15, keyname[userkey[iControlLoop]], font1_ascii, font1_offsets, 480, iY_1, byColor_24, 0, 200, 640, pal_addr);
            szControlName += 64;                // next control name
            ++iControlLoop;
            iY_1 += 18;
          } while (iControlLoop < 6);
          if (Players_Cars[player1_car] >= 8) {
            if (control_edit == 12)
              byColor_25 = 0xAB;
            else
              byColor_25 = 0x8F;
            menu_render_text(mr, 15, "CHEAT:", font1_ascii, font1_offsets, 475, 308, byColor_25, 2u, pal_addr);
            if (control_edit == 12)
              byColor_26 = 0xAB;
            else
              byColor_26 = 0x8F;
            menu_render_scaled_text(mr, 15, keyname[userkey[12]], font1_ascii, font1_offsets, 480, 308, byColor_26, 0, 200, 640, pal_addr);
          }
        }
        // Display Player 2 control customization screen
        else if (iFrontendConfigControlSelection == 3 || iFrontendConfigControlSelection == 4) {
          iControlIndex2 = 6;
          szText = &config_buffer[1280];
          iY_2 = 200;
          // Display all 6 controls for player 2
          do {
            if (iControlIndex2 == control_edit)
              byColor_27 = 0xAB;
            else
              byColor_27 = 0x8F;
            menu_render_text(mr, 15, szText, font1_ascii, font1_offsets, 475, iY_2, byColor_27, 2u, pal_addr);
            if (iControlIndex2 == control_edit)
              byColor_28 = 0xAB;
            else
              byColor_28 = 0x8F;
            menu_render_text(mr, 15, keyname[userkey[iControlIndex2]], font1_ascii, font1_offsets, 480, iY_2, byColor_28, 0, pal_addr);
            szText += 64;                       // Next control name
            ++iControlIndex2;
            iY_2 += 18;
          } while (iControlIndex2 < 12);

          // Display cheat key option for player 2
          if (Players_Cars[player2_car] >= 8) {
            if (control_edit == 13)
              byColor_29 = 0xAB;
            else
              byColor_29 = 0x8F;
            menu_render_text(mr, 15, "CHEAT:", font1_ascii, font1_offsets, 475, 308, byColor_29, 2u, pal_addr);
            if (control_edit == 13)
              byColor_30 = 0xAB;
            else
              byColor_30 = 0x8F;
            menu_render_text(mr, 15, keyname[userkey[13]], font1_ascii, font1_offsets, 480, 308, byColor_30, 0, pal_addr);
          }
        }

        // Handle active key mapping process
        if (!iFrontendConfigControlsInEdit || iFrontendConfigState != 4)
          goto RENDER_FRAME;

        // Key detection and mapping logic
        iKeyCheckLoop = controlrelease;
        if (controlrelease)
          goto CHECK_CONTROL_INPUT;             // wait for key release

        // Scan for pressed keys
        iFoundKey = -1;
        iKeySearchIndex = 0;
        do {
          if (keyname[iKeySearchIndex] && keys[iKeyCheckLoop])
            iFoundKey = iKeyCheckLoop;
          ++iKeyCheckLoop;
          ++iKeySearchIndex;
        } while (iKeyCheckLoop < 128);

        // If no keyboard key pressed check joystick buttons
        if (iFoundKey == -1) {
          ReadJoys(&jFrontendConfigJoyPos);
          if (jFrontendConfigJoyPos.iJ1Button1)
            iFoundKey = 128;
          if (jFrontendConfigJoyPos.iJ1Button2)
            iFoundKey = 129;
          if (jFrontendConfigJoyPos.iJ2Button1)
            iFoundKey = 130;
          if (jFrontendConfigJoyPos.iJ2Button2)
            iFoundKey = 131;
        }

        // If still no input check joystick axis movements
        if (iFoundKey == -1) {
          if (y2ok) {
            iJoyValue1 = 100 * (2 * jFrontendConfigJoyPos.iJ2YAxis - JBYmax - JBYmin) / (JBYmax - JBYmin);
            if (iJoyValue1 < -50)
              iFoundKey = 138;
            if (iJoyValue1 > 50)
              iFoundKey = 139;
          }
          if (x2ok) {
            iJoyValue2 = 100 * (2 * jFrontendConfigJoyPos.iJ2XAxis - JBXmax - JBXmin) / (JBXmax - JBXmin);
            if (iJoyValue2 < -50)
              iFoundKey = 136;
            if (iJoyValue2 > 50)
              iFoundKey = 137;
          }
          if (y1ok) {
            iJoyValue3 = 100 * (2 * jFrontendConfigJoyPos.iJ1YAxis - JAYmax - JAYmin) / (JAYmax - JAYmin);
            if (iJoyValue3 < -50)
              iFoundKey = 134;
            if (iJoyValue3 > 50)
              iFoundKey = 135;
          }
          if (x1ok) {
            iJoyValue4 = 100 * (2 * jFrontendConfigJoyPos.iJ1XAxis - JAXmax - JAXmin) / (JAXmax - JAXmin);
            if (iJoyValue4 < -50)
              iFoundKey = 132;
            if (iJoyValue4 > 50)
              iFoundKey = 133;
          }
        }

        if (iFoundKey != -1 && !control_key_matches_required_pair_type(control_edit, iFoundKey))
          iFoundKey = -1;                       // reject incompatible steering pair type

        if (iFoundKey == -1)
          goto CHECK_CONTROL_INPUT;

        // Check for duplicate key assignments
        iDuplicateCheck = control_key_is_duplicate_in_player_set(control_edit, iFoundKey);
        if (iDuplicateCheck)
          goto CHECK_CONTROL_INPUT;             // Reject duplicate assignment

        // Assign the new key
        iEditIndex = control_edit + 1;
        iControlState = iFrontendConfigControlsInEdit;
        controlrelease = -1;

        userkey[control_edit] = iFoundKey;
        //*((_BYTE *)&keyname[139] + iEditIndex + 3) = iFoundKey;

        // Handle completion logic for each player
        control_edit = iEditIndex;
        if (iControlState == 1)               // Player 1
        {
          if (iEditIndex < 6)
            goto CHECK_CONTROL_INPUT;
          // Check for cheat key mapping
          if (Players_Cars[player1_car] >= 8 && control_edit < 12) {
            control_edit = 12;                  // jump to cheat key
            goto CHECK_CONTROL_INPUT;
          }
        } else                                    // Player 2
        {
          if (iEditIndex < 12)
            goto CHECK_CONTROL_INPUT;
          if (Players_Cars[player2_car] >= 8 && control_edit < 13) {
            control_edit = 13;                  // jump to cheat key
            goto CHECK_CONTROL_INPUT;
          }
        }

        // All controls mapped, exit editing mode
        iFrontendConfigControlsInEdit = 0;
        control_edit = -1;
        enable_keyboard();
      CHECK_CONTROL_INPUT:
              // Handle ESC key to restore original key mappings
        if (keys[1]) {
          memcpy(userkey, oldkeys, 0xCu);      // restore original player 1 keys
          memcpy(&userkey[12], &oldkeys[12], 2u);// restore original cheat keys
          enable_keyboard();
          iFrontendConfigControlsInEdit = 0;
          control_edit = -1;
          check_joystick_usage();
        }
      RENDER_FRAME:
              // Display any received network messages
        show_received_mesage();

        // render (GPU)
        menu_render_end_frame(mr);
        if (SnapshotShouldStop())
          return;

        // Handle CHEAT_MODE_CLONES
        if (switch_same > 0) {
          // Clone all player cars to same type
          iPlayerIdx = 0;
          if (players > 0) {

            for (iPlayerIdx = 0; iPlayerIdx < players; iPlayerIdx++)
            {
              Players_Cars[iPlayerIdx] = switch_same - 666;
            }
            //iPlayersCarsOffset = 0;
            //do {
            //  iPlayersCarsOffset += 4;
            //  ++iPlayerIdx;
            //  *(int *)((char *)&infinite_laps + iPlayersCarsOffset) = switch_same - 666;// offset into Players_Cars
            //} while (iPlayerIdx < players);

          }

          cheat_mode |= CHEAT_MODE_CLONES;
          //uiTempCheatMode = cheat_mode;
          //BYTE1(uiTempCheatMode) = BYTE1(cheat_mode) | 0x40;
          //cheat_mode = uiTempCheatMode;
        } else if (switch_same < 0) {
          // Reset car cloning
          switch_same = 0;
          iPlayerIdx_1 = 0;
          if (players > 0) {

            for (iPlayerIdx_1 = 0; iPlayerIdx_1 < players; iPlayerIdx_1++)
            {
              Players_Cars[iPlayerIdx_1] = -1;
            }
            //iPlayersCarsOffset_1 = 0;
            //do {
            //  iPlayersCarsOffset_1 += 4;
            //  ++iPlayerIdx_1;
            //  *(int *)((char *)&infinite_laps + iPlayersCarsOffset_1) = -1;// offset into Players_Cars
            //} while (iPlayerIdx_1 < players);

          }

          cheat_mode &= ~CHEAT_MODE_CLONES;
          //cheat_mode &= ~0x4000u;
        }

        if (frontend_config_update_broadcast_wait())
          return;

        // Process keyboard input when not editing controls
        if (!iFrontendConfigControlsInEdit) {
          while (fatkbhit()) {
            switch (iFrontendConfigState) {
              case 0:                           // MAIN MENU NAVIGATION
                uiKeyCode = fatgetch();
                if (uiKeyCode < 0xD) {
                  if (!uiKeyCode)             // extended key
                  {
                    uiExtendedKey = fatgetch();
                    if (uiExtendedKey >= 0x48)// Up arrow
                    {
                      if (uiExtendedKey <= 0x48) {
                        iMenuDir2 = --iFrontendConfigMenuSelection;
                        // Skip network option if disabled
                        if (!network_on && iMenuDir2 == 6)
                          iFrontendConfigMenuSelection = 5;
                        if (iFrontendConfigMenuSelection < 0)
                          iFrontendConfigMenuSelection = 0;
                      } else if (uiExtendedKey == 80)// Down arrow
                      {
                        iMenuDir = ++iFrontendConfigMenuSelection;
                        // Skip network option if disabled
                        if (!network_on && iMenuDir == 6)
                          iFrontendConfigMenuSelection = 7;
                        if (iFrontendConfigMenuSelection > 7)
                          iFrontendConfigMenuSelection = 7;
                      }
                    }
                  }
                } else if (uiKeyCode <= 0xD)    // enter key
                {
                  switch (iFrontendConfigMenuSelection) {
                    case 0:                     // drivers
                      iFrontendConfigState = 1;
                      iFrontendConfigSelectedCar = 0;
                      break;
                    case 1:                     // audio
                      iFrontendConfigState = 2;
                      iFrontendConfigVolumeSelection = 0;
                      break;
                    case 2:                     // joystick
                      iFrontendConfigState = 3;
                      check_joystickpresence();
                      break;
                    case 3:                     // controls
                      iFrontendConfigState = 4;
                      iFrontendConfigControlSelection = 0;
                      iFrontendConfigControlsInEdit = 0;
                      Joy1used = 0;
                      Joy2used = 0;
                      controlrelease = -1;
                      control_edit = -1;
                      break;
                    case 4:                     // video
                      iFrontendConfigState = 5;
                      iFrontendConfigVideoState = 0;
                      break;
                    case 5:                     // graphics
                      iFrontendConfigState = 6;
                      iFrontendConfigGraphicsState = 0;
                      break;
                    case 6:                     // network
                      iFrontendConfigState = 7;
                      iFrontendConfigNetworkState = 0;
                      iFrontendConfigEditingName = 0;
                      break;
                    case 7:                     // exit
                      sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);
                      iFrontendConfigExitFlag = -1;
                      break;
                    default:
                      continue;
                  }
                } else if (uiKeyCode == 27)     // ESC key
                {
                  iFrontendConfigExitFlag = -1;
                  sfxsample(SOUND_SAMPLE_BUTTON, 0x8000);
                }
                continue;
              case 1:                           // DRIVER/CAR CONFIG
                iKeyInput = fatgetch();
                iKeyChar = iKeyInput;
                if ((unsigned int)iKeyInput < 8) {
                  if (iKeyInput)
                    goto HANDLE_CHAR_INPUT;     // Handle character input

                  // Handle arrow keys for car selection
                  uiArrowKey = fatgetch();
                  if (!iFrontendConfigEditingName && uiArrowKey >= 0x48) {
                    if (uiArrowKey <= 0x48)   // up arrow
                    {
                      iNextSelectedCar = ++iFrontendConfigSelectedCar;
                      // Skip player 2 slot in single player mode
                      if (player_type != 2 && iNextSelectedCar == 2)
                        iFrontendConfigSelectedCar = 3;
                      if (iFrontendConfigSelectedCar > 18)
                        iFrontendConfigSelectedCar = 18;
                    } else if (uiArrowKey == 80)// down arrow
                    {
                      iPrevSelectedCar = --iFrontendConfigSelectedCar;
                      // Skip player 2 slot in single player mode
                      if (player_type != 2 && iPrevSelectedCar == 2)
                        iFrontendConfigSelectedCar = 1;
                      if (iFrontendConfigSelectedCar < 0)
                        iFrontendConfigSelectedCar = 0;
                    }
                  }
                } else if ((unsigned int)iKeyInput <= 8)// backspace
                {
                  if (iFrontendConfigEditingName) {
                    // Handle backspace in name editing
                    iFrontendConfigNameLength_1 = iFrontendConfigNameLength;
                    szFrontendConfigNewNameBuf[iFrontendConfigNameLength] = 0;
                    if (iFrontendConfigNameLength_1 > 0) {
                      iFrontendConfigNameLength = iFrontendConfigNameLength_1 - 1;
                      szFrontendConfigNewNameBuf[iFrontendConfigNameLength_1 - 1] = 0;
                    }
                  }
                } else if ((unsigned int)iKeyInput < 0xD)// regular character input
                {
                HANDLE_CHAR_INPUT:
                  if (iFrontendConfigEditingName) {
                    // Convert lowercase to uppercase
                    if (iKeyInput >= 97 && iKeyInput <= 122)
                      iKeyChar = iKeyInput - 32;

                    // Accept alphanumeric chars and space
                    if ((iKeyChar == 32 || iKeyChar >= 65 && iKeyChar <= 90 || iKeyChar >= 48 && iKeyChar <= 57) && iFrontendConfigNameLength < 8) {
                      iFrontendConfigNameLength_2 = iFrontendConfigNameLength + 1;
                      szFrontendConfigNewNameBuf[iFrontendConfigNameLength] = iKeyChar;
                      iFrontendConfigNameLength = iFrontendConfigNameLength_2;
                      szFrontendConfigNewNameBuf[iFrontendConfigNameLength_2] = 0;
                    }
                  }
                } else if ((unsigned int)iKeyInput <= 0xD)// enter key
                {
                  iFrontendConfigEditingName_1 = iFrontendConfigEditingName;
                  if (iFrontendConfigEditingName) {
                    // Save edited name
                    iFrontendConfigEditingName = 0;
                    if (iFrontendConfigSelectedCar) {
                      if ((unsigned int)iFrontendConfigSelectedCar <= 1) {

                        // Save player 1 name

                        for (j = 0; j < 9; j++)
                        {
                            player_names[player1_car][j] = szFrontendConfigNewNameBuf[j];
                        }
                        //for (j = 0; j < 9; cheat_names[player1_car + 31][j + 8] = *((_BYTE *)&pJoyPos.iJ2YAxis + j + 3))
                        //  ++j;

                        frontend_config_begin_broadcast_wait(
                            -669, FRONTEND_CONFIG_BROADCAST_WAIT_CHECK_PLAYER1_NAME_AND_CARS);
                        return;
                      } else {
                        if (iFrontendConfigSelectedCar != 2)
                          goto SAVE_AI_DRIVER_NAME;
                        iPlayer2Car = player2_car;

                        for (k = 0; k < 9; k++)
                        {
                          player_names[player2_car][k] = szFrontendConfigNewNameBuf[k];
                        }
                        //for (k = 0; k < 9; cheat_names[iPlayer2Car + 31][k + 8] = *((_BYTE *)&pJoyPos.iJ2YAxis + k + 3))
                        //  ++k;

                        waste = CheckNames(player_names[iPlayer2Car], iPlayer2Car);
                        check_cars();
                      }
                    } else                        // AI players
                    {
                    SAVE_AI_DRIVER_NAME:
                      iDefaultNamesIdx_1 = iFrontendConfigSelectedCar - 3;
                      iDefaultNamesIdx_1 = (iFrontendConfigSelectedCar - 3) ^ 1;// Toggle for paired AI drivers
                      //LOBYTE(iDefaultNamesIdx_1) = (iFrontendConfigSelectedCar - 3) ^ 1;// Toggle for paired AI drivers
                      iDefaultNamesCharItr = 0;
                      iDefaultNamesIdx = iDefaultNamesIdx_1;

                      do
                      {
                        default_names[iDefaultNamesIdx][iDefaultNamesCharItr] = szFrontendConfigNewNameBuf[iDefaultNamesCharItr];
                        ++iDefaultNamesCharItr;
                      }
                      while (iDefaultNamesCharItr < 9);
                      //do {
                      //  ++iDefaultNamesCharItr;
                      //  *((_BYTE *)&team_col[15] + iDefaultNamesCharItr + iDefaultNamesIdx * 9 + 3) = *((_BYTE *)&pJoyPos.iJ2YAxis + iDefaultNamesCharItr + 3);// offset into default_names
                      //} while (iDefaultNamesCharItr < 9);

                      // Set default name if empty
                      if (!default_names[iDefaultNamesIdx][0]) {
                        sprintf(buffer, "comp %i", iFrontendConfigSelectedCar - 2);
                        name_copy(default_names[iDefaultNamesIdx], buffer);
                      }
                      frontend_config_begin_broadcast_wait(-1, FRONTEND_CONFIG_BROADCAST_WAIT_NONE);
                      return;
                    }
                  } else {
                    // Start editing name
                    if (!iFrontendConfigSelectedCar)
                      goto EXIT_NAME_EDITING;
                    iFrontendConfigEditingName = 1;
                    if (iFrontendConfigSelectedCar >= 3) {
                      // Check if AI car slot is editable
                      if ((((uint8)iFrontendConfigSelectedCar - 3) & 1) != 0)
                        iFrontendConfigEditingName = allocated_cars[(iFrontendConfigSelectedCar - 3) / 2] <= 0;
                      else
                        iFrontendConfigEditingName = allocated_cars[(iFrontendConfigSelectedCar - 3) / 2] <= 1;
                    }

                    if (iFrontendConfigEditingName == 1) {
                      iFrontendConfigNameLength = 0;
                      if ((unsigned int)iFrontendConfigSelectedCar <= 1)// Load player 1 name
                      {
                        for (m = 0; m < 9; m++)
                        {
                          szFrontendConfigNewNameBuf[m] = player_names[player1_car][m];
                        }
                        //for (m = 0; m < 9; *((_BYTE *)&pJoyPos.iJ2YAxis + m + 3) = cheat_names[player1_car + 31][m + 8])
                        //  ++m;
                      } else if (iFrontendConfigSelectedCar == 2)// Load player 2 name
                      {
                        for (n = 0; n < 9; n++)
                        {
                          szFrontendConfigNewNameBuf[n] = player_names[player2_car][n];
                        }
                        //for (n = 0; n < 9; *((_BYTE *)&pJoyPos.iJ2YAxis + n + 3) = cheat_names[player2_car + 31][n + 8])
                        //  ++n;
                      } else                      // Load AI driver name
                      {
                        iAIDriverIdx = iFrontendConfigSelectedCar - 3;
                        iAIDriverIdx ^= 1;  // Toggle the lowest bit
                        for (int i = 0; i < 9; i++)
                        {
                          szFrontendConfigNewNameBuf[i] = default_names[iAIDriverIdx][i];
                        }
                        //iAIDriverIdx = iFrontendConfigSelectedCar - 3;
                        //LOBYTE(iAIDriverIdx) = (iFrontendConfigSelectedCar - 3) ^ 1;
                        //v189 = 0;
                        //iOffset = 9 * iAIDriverIdx;
                        //do {
                        //  ++v189;
                        //  v191 = default_names[0][iOffset++];
                        //  *((_BYTE *)&pJoyPos.iJ2YAxis + v189 + 3) = v191;
                        //} while (v189 < 9);

                      }

                      // Calculate current name length
                      while (szFrontendConfigNewNameBuf[iFrontendConfigNameLength])
                        ++iFrontendConfigNameLength;
                    }

                  }
                } else {
                  if (iKeyInput != 27)        // ESC key
                    goto HANDLE_CHAR_INPUT;
                  iFrontendConfigEditingName_1 = iFrontendConfigEditingName;
                  if (iFrontendConfigEditingName) {
                    // Cancel editing, restore original name
                    iFrontendConfigEditingName = 0;
                    if (iFrontendConfigSelectedCar) {
                      if ((unsigned int)iFrontendConfigSelectedCar <= 1)// player 1
                      {

                        // Restore player 1 name

                        for (ii = 0; ii < 9; ii++)
                        {
                          player_names[player1_car][ii] = szFrontendConfigNewNameBuf[ii];
                        }
                        //for (ii = 0; ii < 9; cheat_names[player1_car + 31][ii + 8] = *((_BYTE *)&pJoyPos.iJ2YAxis + ii + 3))
                        //  ++ii;
                        frontend_config_begin_broadcast_wait(
                            -669, FRONTEND_CONFIG_BROADCAST_WAIT_CHECK_PLAYER1_NAME);
                        return;
                      } else {
                        if (iFrontendConfigSelectedCar != 2)// player 2
                          goto CANCEL_AI_NAME_EDIT;
                        iPlayer2Car_1 = player2_car;

                        // Restore player 2 name

                        for (jj = 0; jj < 9; jj++)
                        {
                          player_names[player2_car][jj] = szFrontendConfigNewNameBuf[jj];
                        }
                        //for (jj = 0; jj < 9; cheat_names[iPlayer2Car_1 + 31][jj + 8] = *((_BYTE *)&pJoyPos.iJ2YAxis + jj + 3))
                        //  ++jj;
                        waste = CheckNames(player_names[iPlayer2Car_1], iPlayer2Car_1);
                      }
                    } else                        // AI driver
                    {
                    CANCEL_AI_NAME_EDIT:
                      // Restore AI driver name
                      iAIDriverIdx_1 = iFrontendConfigSelectedCar - 3;
                      iAIDriverIdx_1 ^= 1;  // Toggle between paired AI drivers
                      for (v196 = 0; v196 < 9; v196++)
                      {
                        default_names[iAIDriverIdx_1][v196] = szFrontendConfigNewNameBuf[v196];
                      }
                      iAIDriverIdx_1 = iFrontendConfigSelectedCar - 3;
                      iAIDriverIdx_1 = (iFrontendConfigSelectedCar - 3) ^ 1;
                      //LOBYTE(iAIDriverIdx_1) = (iFrontendConfigSelectedCar - 3) ^ 1;
                      v196 = 0;
                      v197 = iAIDriverIdx_1;


                      //do {
                      //  ++v196;
                      //  *((_BYTE *)&team_col[15] + v196 + v197 * 9 + 3) = *((_BYTE *)&pJoyPos.iJ2YAxis + v196 + 3);
                      //} while (v196 < 9);

                      if (!default_names[iAIDriverIdx_1][0]) {
                        sprintf(buffer, "comp %i", iFrontendConfigSelectedCar - 2);
                        name_copy(default_names[v197], buffer);
                      }
                      frontend_config_begin_broadcast_wait(-1, FRONTEND_CONFIG_BROADCAST_WAIT_NONE);
                      return;
                    }
                  } else {
                  EXIT_NAME_EDITING:
                    iFrontendConfigState = iFrontendConfigEditingName_1;
                  }
                }
                continue;
              case 2:
                // Audio/Volume config
                uiKey_5 = fatgetch();
                if (uiKey_5 < 0xD) {
                  if (!uiKey_5)               // Extended key
                  {
                    switch (fatgetch()) {
                      case 0x48:                // UP arrow
                        if (iFrontendConfigVolumeSelection) {
                          if (iFrontendConfigVolumeSelection > 1)
                            --iFrontendConfigVolumeSelection;
                        } else {
                          iFrontendConfigVolumeSelection = 7;
                        }
                        break;
                      case 0x4B:                // Left arrow - decrease volume
                        switch (iFrontendConfigVolumeSelection) {
                          case 1:               // Engine volume
                            EngineVolume -= 4;
                            if (EngineVolume < 0)
                              EngineVolume = 0;
                            break;
                          case 2:               // SFX volume
                            SFXVolume -= 4;
                            if (SFXVolume < 0)
                              SFXVolume = 0;
                            break;
                          case 3:               // Speech volume
                            SpeechVolume -= 4;
                            if (SpeechVolume < 0)
                              SpeechVolume = 0;
                            break;
                          case 4:               // Music volume
                            MusicVolume -= 4;
                            if (MusicVolume < 0)
                              MusicVolume = 0;
                            // Update hardware volume
                            if (MusicCard)
                              MIDISetMasterVolume(MusicVolume);
                              //sosMIDISetMasterVolume(MusicVolume);
                            if (MusicCD)
                              goto SET_CD_VOLUME;// CONTINUE_AUDIO_INPUT: Continue processing audio menu input
                            break;
                          default:
                            continue;
                        }
                        break;
                      case 0x4D:                // Right arrow - increase volume
                        switch (iFrontendConfigVolumeSelection) {
                          case 1:               // Engine volume
                            EngineVolume += 4;
                            if (EngineVolume >= 128)
                              EngineVolume = 127;
                            break;
                          case 2:               // SFX volume
                            SFXVolume += 4;
                            if (SFXVolume >= 128)
                              SFXVolume = 127;
                            break;
                          case 3:               // Speech volume
                            SpeechVolume += 4;
                            if (SpeechVolume >= 128)
                              SpeechVolume = 127;
                            break;
                          case 4:               // Music volume
                            MusicVolume += 4;
                            if (MusicVolume >= 128)
                              MusicVolume = 127;
                            // Update hardware volume
                            if (MusicCard)
                              MIDISetMasterVolume(MusicVolume);
                              //sosMIDISetMasterVolume(MusicVolume);
                            if (MusicCD)
                              SET_CD_VOLUME:
                            SetAudioVolume(MusicVolume);
                            break;
                          default:
                            continue;
                        }
                        break;
                      case 0x50:                // Down arrow
                        iNextVolumeSelection = iFrontendConfigVolumeSelection;
                        if (iFrontendConfigVolumeSelection > 0) {
                          ++iFrontendConfigVolumeSelection;
                          if (iNextVolumeSelection + 1 > 7)
                            iFrontendConfigVolumeSelection = 0;
                        }
                        break;
                      default:
                        continue;
                    }
                  }
                } else if (uiKey_5 <= 0xD)      // Enter key
                {
                  switch (iFrontendConfigVolumeSelection) {
                    case 0:                     // Back
                      goto EXIT_AUDIO_MENU;     // Return to main menu
                    case 5:                     // Toggle engine mode
                      allengines = allengines == 0;
                      break;
                    case 6:                     // Toggle sound effects
                      if (SoundCard) {
                        kk = soundon != 0;
                        soundon = soundon == 0;
                        if (!kk)
                          loadfatalsample();
                      } else {
                        soundon = 0;
                      }
                      break;
                    case 7:                     // Toggle music
                      if (MusicCard || MusicCD) {
                        musicon = musicon == 0;
                        reinitmusic();
                      } else {
                        musicon = MusicCard;
                      }
                      break;
                    default:
                      continue;
                  }
                } else if (uiKey_5 == 0x1B)     // ESC key
                {
                EXIT_AUDIO_MENU_2:
                  iFrontendConfigState = 0;
                }
                continue;
              case 3:                           // JOYSTICK CALIBRATION INPUT
                uiKey = fatgetch();
                if (uiKey < 0xD) {
                  if (!uiKey)
                    fatgetch();                 // Consume extended key
                } else if (uiKey <= 0xD || uiKey == 0x1B)// Enter or ESC
                {
                  remove_uncalibrated();
                  iFrontendConfigState = 0;             // Return to main menu
                }
                continue;
              case 4:                           // CONTROL CONFIG INPUT
                uiKey_1 = fatgetch();
                if (uiKey_1 < 0xD) {
                  if (!uiKey_1)               // Extended key
                  {
                    uiKey_6 = fatgetch();
                    if (uiKey_6 >= 0x48) {
                      if (uiKey_6 <= 0x48)    // Up arrow
                      {
                        if (!iFrontendConfigControlsInEdit) {
                          iNextControlSelection = ++iFrontendConfigControlSelection;
                          if (player_type == 2) {
                            if (iNextControlSelection > 4)
                              iFrontendConfigControlSelection = 4;
                          } else if (iNextControlSelection > 2) {
                            iFrontendConfigControlSelection = 2;
                          }
                        }
                      } else if (uiKey_6 == 80 && !iFrontendConfigControlsInEdit && --iFrontendConfigControlSelection < 0) {
                        iFrontendConfigControlSelection = iFrontendConfigControlsInEdit;
                      }
                    }
                  }
                } else if (uiKey_1 <= 0xD)      // Enter key
                {
                  switch (iFrontendConfigControlSelection) {
                    case 0:                     // Back
                      goto EXIT_CONTROLS_MENU;  // Return to main menu
                    case 1:                     // Customize Player 1
                      control_edit = 0;
                      disable_keyboard();
                      memcpy(oldkeys, userkey, 0xCu);// Backup current keys
                      memcpy(&oldkeys[12], &userkey[12], 2u);// Backup cheat keys
                      iFrontendConfigControlsInEdit = 1;
                      controlrelease = -1;
                      break;
                    case 2:                     // Toggle player 1 control method
                      if (manual_control[player1_car] == 2)
                        manual_control[player1_car] = 1;// Switch to keyboard
                      else
                        manual_control[player1_car] = 2;// Switch to joystick
                      frontend_config_begin_broadcast_wait(-1, FRONTEND_CONFIG_BROADCAST_WAIT_NONE);
                      return;
                      break;
                    case 3:                     // Customize player 2
                      iFrontendConfigControlsInEdit = 2;
                      control_edit = 6;         // Start with player 2 controls
                      disable_keyboard();
                      memcpy(oldkeys, userkey, 0xCu);// backup current keys
                      memcpy(&oldkeys[12], &userkey[12], 2u);// backup cheat keys
                      controlrelease = -1;
                      break;
                    case 4:
                      if (manual_control[player2_car] == 2)
                        manual_control[player2_car] = 1;// switch to keyboard
                      else
                        manual_control[player2_car] = 2;// switch to joystick
                      break;
                    default:
                      continue;
                  }
                } else if (uiKey_1 == 27) {
                  iFrontendConfigState = 0;
                }
                continue;
              case 5:                           // VIDEO
                uiKey_2 = fatgetch();
                if (uiKey_2 < 0xD) {
                  if (!uiKey_2) {
                    switch (fatgetch()) {
                      case 0x48:
                        if (++iFrontendConfigVideoState > 16)
                          iFrontendConfigVideoState = 16;
                        break;
                      case 0x4B:
                        if (iFrontendConfigVideoState == 2) {
                          if (game_svga) {
                            game_size -= 16;
                            if (game_size < 64)
                              game_size = 64;
                          } else {
                            game_size -= 8;
                            if (game_size < 32)
                              game_size = 32;
                          }
                        }
                        break;
                      case 0x4D:
                        if (iFrontendConfigVideoState == 2) {
                          if (game_svga) {
                            game_size += 16;
                            if (game_size > 128)
                              game_size = 128;
                          } else {
                            game_size += 8;
                            if (game_size > 64)
                              game_size = 64;
                          }
                        }
                        break;
                      case 0x50:
                        if (--iFrontendConfigVideoState < 0)
                          iFrontendConfigVideoState = 0;
                        break;
                      default:
                        continue;
                    }
                  }
                } else if (uiKey_2 <= 0xD) {
                  switch (iFrontendConfigVideoState) {
                    case 0:
                      iFrontendConfigState = 0;
                      break;
                    case 1:
                      if (game_svga) {
                        game_svga = 0;
                        game_size /= 2;
                      } else if (svga_possible && !no_mem) {
                        game_svga = -1;
                        game_size *= 2;
                      }
                      break;
                    case 2:
                      if (game_svga) {
                        game_size += 16;
                        if (game_size > 128)
                          game_size = 64;
                      } else {
                        game_size += 8;
                        if (game_size > 64)
                          game_size = 32;
                      }
                      break;
                    case 3:
                      if (view_limit) {
                        view_limit = 0;
                      } else if (machine_speed >= 2800) {
                        view_limit = 32;
                      } else {
                        view_limit = 24;
                      }
                      break;
                    case 4: //PANEL ON
                      if ((textures_off & TEX_OFF_PANEL_OFF) != 0) {
                        uiTexOffTemp_7 = textures_off;
                        uiTexOffTemp_7 = textures_off ^ TEX_OFF_PANEL_OFF;
                        //LOBYTE(uiTexOffTemp_7) = textures_off ^ 0x20;
                        textures_off = uiTexOffTemp_7 | TEX_OFF_PANEL_RESTRICTED;
                      } else if ((textures_off & TEX_OFF_PANEL_RESTRICTED) != 0) {
                        textures_off ^= TEX_OFF_PANEL_RESTRICTED;
                      } else {
                        textures_off |= TEX_OFF_PANEL_OFF;
                      }
                      break;
                    case 5:
                      uiTexOffTemp = textures_off;
                      uiTexOffTemp = textures_off ^ TEX_OFF_CLOUDS;
                      //LOBYTE(uiTexOffTemp) = textures_off ^ 8;
                      textures_off = uiTexOffTemp;
                      break;
                    case 6:
                      uiTexOffTemp_1 = textures_off;
                      uiTexOffTemp_1 = textures_off ^ TEX_OFF_SHADOWS;
                      //BYTE1(uiTexOffTemp_1) = BYTE1(textures_off) ^ 1;
                      textures_off = uiTexOffTemp_1;
                      break;
                    case 7:
                      textures_off ^= TEX_OFF_ROAD_TEXTURES;
                      break;
                    case 8:
                      textures_off ^= TEX_OFF_BUILDING_TEXTURES;
                      break;
                    case 9:
                      uiTexOffTemp_2 = textures_off;
                      uiTexOffTemp_2 = textures_off ^ TEX_OFF_GROUND_TEXTURES;
                      //LOBYTE(uiTexOffTemp_2) = textures_off ^ 1;
                      textures_off = uiTexOffTemp_2;
                      break;
                    case 10:
                      uiTexOffTemp_3 = textures_off;
                      uiTexOffTemp_3 = textures_off ^ TEX_OFF_WALL_TEXTURES;
                      //LOBYTE(uiTexOffTemp_3) = textures_off ^ 4;
                      textures_off = uiTexOffTemp_3;
                      break;
                    case 11:
                      uiTexOffTemp_4 = textures_off;
                      uiTexOffTemp_4 = textures_off ^ TEX_OFF_CAR_TEXTURES;
                      //LOBYTE(uiTexOffTemp_4) = textures_off ^ 0x40;
                      textures_off = uiTexOffTemp_4;
                      break;
                    case 12:
                      uiTexOffTemp_5 = textures_off;
                      uiTexOffTemp_5 = textures_off ^ TEX_OFF_HORIZON;
                      //LOBYTE(uiTexOffTemp_5) = textures_off ^ 0x10;
                      textures_off = uiTexOffTemp_5;
                      break;
                    case 13:
                      textures_off ^= TEX_OFF_GLASS_WALLS;
                      break;
                    case 14:
                      textures_off ^= TEX_OFF_BUILDINGS;
                      break;
                    case 15:
                      if (++names_on > 2)
                        names_on = 0;
                      break;
                    case 16:
                      textures_off ^= TEX_OFF_PERSPECTIVE_CORRECTION;
                      break;
                    default:
                      continue;
                  }
                } else if (uiKey_2 == 27) {
                EXIT_AUDIO_MENU:
                  iFrontendConfigState = 0;
                }
                continue;
              case 6:                           // GRAPHICS
                uiKey_3 = fatgetch();
                if (uiKey_3 < 0xD) {
                  if (!uiKey_3) {
                    switch (fatgetch()) {
                      case 0x48:
                        iPlayerIndex = ++iFrontendConfigGraphicsState;
                        if (player_type == 2) {
                          if (iPlayerIndex > 6)
                            iFrontendConfigGraphicsState = 6;
                        } else if (iPlayerIndex > 5) {
                          iFrontendConfigGraphicsState = 5;
                        }
                        break;
                      case 0x50:
                        if (--iFrontendConfigGraphicsState < 0)
                          iFrontendConfigGraphicsState = 0;
                        break;
                      default:
                        continue;
                    }
                  }
                } else if (uiKey_3 <= 0xD) {
                  switch (iFrontendConfigGraphicsState) {
                    case 0:
                      goto EXIT_AUDIO_MENU_2;
                    case 1:
                      false_starts = false_starts == 0;
                      frontend_config_begin_broadcast_wait(-1, FRONTEND_CONFIG_BROADCAST_WAIT_NONE);
                      return;
                      break;
                    case 2:
                      if (lots_of_mem)
                        p_tex_size = p_tex_size != 1;
                      break;
                    case 3:
                      replay_record = replay_record == 0;
                      break;
                    case 4:
                      uiTexOffTemp_6 = textures_off;
                      uiTexOffTemp_6 = textures_off ^ TEX_OFF_KMH;
                      //BYTE1(uiTexOffTemp_6) = BYTE1(textures_off) ^ 4;
                      textures_off = uiTexOffTemp_6;
                      break;
                    case 5:
                      do {
                        if (++game_view[0] == 9)
                          game_view[0] = 0;
                      } while (!AllowedViews[game_view[0]]);
                      break;
                    case 6:
                      do {
                        if (++game_view[1] == 9)
                          game_view[1] = 0;
                      } while (!AllowedViews[game_view[1]]);
                      break;
                    default:
                      continue;
                  }
                } else if (uiKey_3 == 27) {
                EXIT_CONTROLS_MENU:
                  iDisplayIndex = 0;
                  goto LABEL_900;
                }
                continue;
              case 7:                           // NETWORK
                uiKey_4 = fatgetch();
                uiDataValue1 = uiKey_4;
                iGameIndex = uiKey_4;
                if (uiKey_4 < 8) {
                  if (uiKey_4)
                    goto LABEL_1028;
                  iPlayerIndex2 = iFrontendConfigEditingName;
                  uiDataValue3 = fatgetch();
                  if (!iPlayerIndex2 && uiDataValue3 >= 0x48) {
                    if (uiDataValue3 <= 0x48) {
                      if (++iFrontendConfigNetworkState > 5)
                        iFrontendConfigNetworkState = 5;
                    } else if (uiDataValue3 == 80 && --iFrontendConfigNetworkState < 0) {
                      iFrontendConfigNetworkState = uiDataValue1;
                    }
                  }
                } else {
                  uiDataValue2 = 14 * (4 - iFrontendConfigNetworkState);
                  if (uiDataValue1 <= 8) {
                    if (iFrontendConfigEditingName) {
                      iDataIndex = iFrontendConfigNameLength;
                      network_messages[0][iFrontendConfigNameLength + uiDataValue2] = 0;
                      if (iDataIndex > 0) {
                        iFrontendConfigNameLength = iDataIndex - 1;
                        network_messages[0][iDataIndex - 1 + uiDataValue2] = 0;
                      }
                    }
                  } else {
                    if (uiDataValue1 < 0xD)
                      goto LABEL_1028;
                    if (uiDataValue1 <= 0xD) {
                      if (iFrontendConfigEditingName) {
                        iFrontendConfigEditingName = uiDataValue1 ^ iGameIndex;
                      } else if (iFrontendConfigNetworkState) {
                        if (iFrontendConfigNetworkState == 5) {
                          select_messages();
                        } else {
                          iFrontendConfigNameLength = iFrontendConfigEditingName;
                          iCounterVar = 14 * (4 - iFrontendConfigNetworkState);
                          byTempFlag = network_messages[uiDataValue2 / 0xE][0];
                          iFrontendConfigEditingName = 1;
                          for (kk = byTempFlag == 0; !kk; kk = byStatusFlag == 0) {
                            byStatusFlag = network_messages[0][++iCounterVar];
                            ++iFrontendConfigNameLength;
                          }
                        }
                      } else {
                        iFrontendConfigState = iFrontendConfigEditingName;
                      }
                    } else if (uiDataValue1 == 27) {
                      iDisplayIndex = iFrontendConfigEditingName;
                      if (iFrontendConfigEditingName)
                        iFrontendConfigEditingName = 0;
                      else
                        LABEL_900:
                      iFrontendConfigState = iDisplayIndex;
                    } else {
                    LABEL_1028:
                      if (iFrontendConfigEditingName && iFrontendConfigNameLength < 13) {
                        if (iGameIndex >= 97 && iGameIndex <= 122)
                          iGameIndex -= 32;
                        if (iGameIndex >= 65 && iGameIndex <= 90 || iGameIndex >= 48 && iGameIndex <= 57 || iGameIndex == 32 || iGameIndex == 46 || iGameIndex == 39) {
                          iResultValue = 14 * (4 - iFrontendConfigNetworkState);
                          iCalculation = iFrontendConfigNameLength + 1;
                          network_messages[0][iFrontendConfigNameLength + iResultValue] = iGameIndex;
                          iFrontendConfigNameLength = iCalculation;
                          network_messages[0][iCalculation + iResultValue] = 0;
                        }
                      }
                    }
                  }
                }
                break;
              default:
                continue;
            }
          }
        }
        if (!iFrontendConfigExitFlag)
          return;
        eFrontendNextState = eFRONTEND_STATE_MAIN_MENU;
        return;
      case 4:
        if (iFrontendConfigState != 5)
          iFrontendConfigVideoState = -1;
        if (iFrontendConfigVideoState == 16)
          byColor_31 = 0xAB;
        else
          byColor_31 = 0x8F;
        menu_render_scaled_text(mr, 15, &config_buffer[6912], font1_ascii, font1_offsets, 435, 60, byColor_31, 2u, 200, 640, pal_addr);
        if ((textures_off & TEX_OFF_PERSPECTIVE_CORRECTION) != 0) {
          if (iFrontendConfigVideoState == 16)
            byColor_32 = 0xAB;
          else
            byColor_32 = 0x8F;
          byTempChar1 = byColor_32;
          szText_1 = &config_buffer[2688];
        } else {
          if (iFrontendConfigVideoState == 16)
            byColor_33 = 0xAB;
          else
            byColor_33 = 0x8F;
          byTempChar1 = byColor_33;
          szText_1 = &config_buffer[2624];
        }
        menu_render_scaled_text(mr, 15, szText_1, font1_ascii, font1_offsets, 440, 60, byTempChar1, 0, 200, 640, pal_addr);
        sprintf(buffer, "%s:", &config_buffer[3968]);
        if (iFrontendConfigVideoState == 15)
          byColor_34 = 0xAB;
        else
          byColor_34 = 0x8F;
        menu_render_scaled_text(mr, 15, buffer, font1_ascii, font1_offsets, 435, 80, byColor_34, 2u, 200, 640, pal_addr);
        if (names_on) {
          if (names_on == 2) {
            if (iFrontendConfigVideoState == 15)
              byColor_105 = 0xAB;
            else
              byColor_105 = 0x8F;
            menu_render_scaled_text(mr, 15, &config_buffer[2816], font1_ascii, font1_offsets, 440, 80, byColor_105, 0, 200, 640, pal_addr);
          } else {
            if (iFrontendConfigVideoState == 15)
              byColor_35 = 0xAB;
            else
              byColor_35 = 0x8F;
            menu_render_scaled_text(mr, 15, &config_buffer[2624], font1_ascii, font1_offsets, 440, 80, byColor_35, 0, 200, 640, pal_addr);
          }
        } else {
          if (iFrontendConfigVideoState == 15)
            byColor_36 = 0xAB;
          else
            byColor_36 = 0x8F;
          menu_render_scaled_text(mr, 15, &config_buffer[2688], font1_ascii, font1_offsets, 440, 80, byColor_36, 0, 200, 640, pal_addr);
        }
        if (iFrontendConfigVideoState == 14)
          byColor_37 = 0xAB;
        else
          byColor_37 = 0x8F;
        menu_render_scaled_text(mr, 15, &config_buffer[3008], font1_ascii, font1_offsets, 435, 100, byColor_37, 2u, 200, 640, pal_addr);
        if ((textures_off & TEX_OFF_BUILDINGS) != 0) {
          if (iFrontendConfigVideoState == 14)
            byColor_38 = 0xAB;
          else
            byColor_38 = 0x8F;
          menu_render_scaled_text(mr, 15, &config_buffer[2688], font1_ascii, font1_offsets, 440, 100, byColor_38, 0, 200, 640, pal_addr);
        } else {
          if (iFrontendConfigVideoState == 14)
            byColor_39 = 0xAB;
          else
            byColor_39 = 0x8F;
          menu_render_scaled_text(mr, 15, &config_buffer[2624], font1_ascii, font1_offsets, 440, 100, byColor_39, 0, 200, 640, pal_addr);
        }
        if (iFrontendConfigVideoState == 13)
          byColor_40 = 0xAB;
        else
          byColor_40 = 0x8F;
        menu_render_scaled_text(mr, 15, &config_buffer[3072], font1_ascii, font1_offsets, 435, 120, byColor_40, 2u, 200, 640, pal_addr);
        if ((textures_off & TEX_OFF_GLASS_WALLS) != 0) {
          if (iFrontendConfigVideoState == 13)
            byColor_41 = 0xAB;
          else
            byColor_41 = 0x8F;
          menu_render_scaled_text(mr, 15, &config_buffer[2688], font1_ascii, font1_offsets, 440, 120, byColor_41, 0, 200, 640, pal_addr);
        } else {
          if (iFrontendConfigVideoState == 13)
            byColor_42 = 0xAB;
          else
            byColor_42 = 0x8F;
          menu_render_scaled_text(mr, 15, &config_buffer[2624], font1_ascii, font1_offsets, 440, 120, byColor_42, 0, 200, 640, pal_addr);
        }
        if (iFrontendConfigVideoState == 12)
          byColor_43 = 0xAB;
        else
          byColor_43 = 0x8F;
        menu_render_scaled_text(mr, 15, &config_buffer[3200], font1_ascii, font1_offsets, 435, 140, byColor_43, 2u, 200, 640, pal_addr);
        if ((textures_off & TEX_OFF_HORIZON) != 0) {
          if (iFrontendConfigVideoState == 12)
            byColor_44 = 0xAB;
          else
            byColor_44 = 0x8F;
          menu_render_scaled_text(mr, 15, &config_buffer[2688], font1_ascii, font1_offsets, 440, 140, byColor_44, 0, 200, 640, pal_addr);
        } else {
          if (iFrontendConfigVideoState == 12)
            byColor_45 = 0xAB;
          else
            byColor_45 = 0x8F;
          menu_render_scaled_text(mr, 15, &config_buffer[2624], font1_ascii, font1_offsets, 440, 140, byColor_45, 0, 200, 640, pal_addr);
        }
        if (iFrontendConfigVideoState == 11)
          byColor_46 = 0xAB;
        else
          byColor_46 = 0x8F;
        menu_render_scaled_text(mr, 15, &config_buffer[3136], font1_ascii, font1_offsets, 435, 160, byColor_46, 2u, 200, 640, pal_addr);
        if ((textures_off & TEX_OFF_CAR_TEXTURES) != 0) {
          if (iFrontendConfigVideoState == 11)
            byColor_47 = 0xAB;
          else
            byColor_47 = 0x8F;
          menu_render_scaled_text(mr, 15, &config_buffer[2688], font1_ascii, font1_offsets, 440, 160, byColor_47, 0, 200, 640, pal_addr);
        } else {
          if (iFrontendConfigVideoState == 11)
            byColor_48 = 0xAB;
          else
            byColor_48 = 0x8F;
          menu_render_scaled_text(mr, 15, &config_buffer[2624], font1_ascii, font1_offsets, 440, 160, byColor_48, 0, 200, 640, pal_addr);
        }
        if (iFrontendConfigVideoState == 10)
          byColor_49 = 0xAB;
        else
          byColor_49 = 0x8F;
        menu_render_scaled_text(mr, 15, &config_buffer[3264], font1_ascii, font1_offsets, 435, 180, byColor_49, 2u, 200, 640, pal_addr);
        if ((textures_off & TEX_OFF_WALL_TEXTURES) != 0) {
          if (iFrontendConfigVideoState == 10)
            byColor_50 = 0xAB;
          else
            byColor_50 = 0x8F;
          menu_render_scaled_text(mr, 15, &config_buffer[2688], font1_ascii, font1_offsets, 440, 180, byColor_50, 0, 200, 640, pal_addr);
        } else {
          if (iFrontendConfigVideoState == 10)
            byColor_51 = 0xAB;
          else
            byColor_51 = 0x8F;
          menu_render_scaled_text(mr, 15, &config_buffer[2624], font1_ascii, font1_offsets, 440, 180, byColor_51, 0, 200, 640, pal_addr);
        }
        if (iFrontendConfigVideoState == 9)
          byColor_52 = 0xAB;
        else
          byColor_52 = 0x8F;
        menu_render_scaled_text(mr, 15, &config_buffer[3328], font1_ascii, font1_offsets, 435, 200, byColor_52, 2u, 200, 640, pal_addr);
        if ((textures_off & TEX_OFF_GROUND_TEXTURES) != 0) {
          if (iFrontendConfigVideoState == 9)
            byColor_53 = 0xAB;
          else
            byColor_53 = 0x8F;
          menu_render_scaled_text(mr, 15, &config_buffer[2688], font1_ascii, font1_offsets, 440, 200, byColor_53, 0, 200, 640, pal_addr);
        } else {
          if (iFrontendConfigVideoState == 9)
            byColor_54 = 0xAB;
          else
            byColor_54 = 0x8F;
          menu_render_scaled_text(mr, 15, &config_buffer[2624], font1_ascii, font1_offsets, 440, 200, byColor_54, 0, 200, 640, pal_addr);
        }
        if (iFrontendConfigVideoState == 8)
          byColor_55 = 0xAB;
        else
          byColor_55 = 0x8F;
        menu_render_scaled_text(mr, 15, &config_buffer[3392], font1_ascii, font1_offsets, 435, 220, byColor_55, 2u, 200, 640, pal_addr);
        if ((textures_off & TEX_OFF_BUILDING_TEXTURES) == 0) {
          if (iFrontendConfigVideoState == 8)
            byColor_56 = 0xAB;
          else
            byColor_56 = 0x8F;
          menu_render_scaled_text(mr, 15, &config_buffer[2624], font1_ascii, font1_offsets, 440, 220, byColor_56, 0, 200, 640, pal_addr);
        } else {
          if (iFrontendConfigVideoState == 8)
            byColor_57 = 0xAB;
          else
            byColor_57 = 0x8F;
          menu_render_scaled_text(mr, 15, &config_buffer[2688], font1_ascii, font1_offsets, 440, 220, byColor_57, 0, 200, 640, pal_addr);
        }
        if (iFrontendConfigVideoState == 7)
          byColor_58 = 0xAB;
        else
          byColor_58 = 0x8F;
        menu_render_scaled_text(mr, 15, &config_buffer[3456], font1_ascii, font1_offsets, 435, 240, byColor_58, 2u, 200, 640, pal_addr);
        if ((textures_off & TEX_OFF_ROAD_TEXTURES) != 0) {
          if (iFrontendConfigVideoState == 7)
            byColor_59 = 0xAB;
          else
            byColor_59 = 0x8F;
          menu_render_scaled_text(mr, 15, &config_buffer[2688], font1_ascii, font1_offsets, 440, 240, byColor_59, 0, 200, 640, pal_addr);
        } else {
          if (iFrontendConfigVideoState == 7)
            byColor_60 = 0xAB;
          else
            byColor_60 = 0x8F;
          menu_render_scaled_text(mr, 15, &config_buffer[2624], font1_ascii, font1_offsets, 440, 240, byColor_60, 0, 200, 640, pal_addr);
        }
        if (iFrontendConfigVideoState == 6)
          byColor_61 = 0xAB;
        else
          byColor_61 = 0x8F;
        menu_render_scaled_text(mr, 15, &config_buffer[3520], font1_ascii, font1_offsets, 435, 260, byColor_61, 2u, 200, 640, pal_addr);
        if ((textures_off & TEX_OFF_SHADOWS) != 0) {
          if (iFrontendConfigVideoState == 6)
            byColor_62 = 0xAB;
          else
            byColor_62 = 0x8F;
          menu_render_scaled_text(mr, 15, &config_buffer[2688], font1_ascii, font1_offsets, 440, 260, byColor_62, 0, 200, 640, pal_addr);
        } else {
          if (iFrontendConfigVideoState == 6)
            byColor_63 = 0xAB;
          else
            byColor_63 = 0x8F;
          menu_render_scaled_text(mr, 15, &config_buffer[2624], font1_ascii, font1_offsets, 440, 260, byColor_63, 0, 200, 640, pal_addr);
        }
        if (iFrontendConfigVideoState == 5)
          byColor_64 = 0xAB;
        else
          byColor_64 = 0x8F;
        menu_render_scaled_text(mr, 15, &config_buffer[3584], font1_ascii, font1_offsets, 435, 280, byColor_64, 2u, 200, 640, pal_addr);
        if ((textures_off & TEX_OFF_CLOUDS) != 0) {
          if (iFrontendConfigVideoState == 5)
            byColor_65 = 0xAB;
          else
            byColor_65 = 0x8F;
          menu_render_scaled_text(mr, 15, &config_buffer[2688], font1_ascii, font1_offsets, 440, 280, byColor_65, 0, 200, 640, pal_addr);
        } else {
          if (iFrontendConfigVideoState == 5)
            byColor_66 = 0xAB;
          else
            byColor_66 = 0x8F;
          menu_render_scaled_text(mr, 15, &config_buffer[2624], font1_ascii, font1_offsets, 440, 280, byColor_66, 0, 200, 640, pal_addr);
        }
        if (iFrontendConfigVideoState == 4)
          byColor_67 = 0xAB;
        else
          byColor_67 = 0x8F;
        menu_render_scaled_text(mr, 15, &config_buffer[3648], font1_ascii, font1_offsets, 435, 300, byColor_67, 2u, 200, 640, pal_addr);
        if ((textures_off & TEX_OFF_PANEL_OFF) != 0) {
          if (iFrontendConfigVideoState == 4)
            byColor_68 = 0xAB;
          else
            byColor_68 = 0x8F;
          menu_render_scaled_text(mr, 15, &config_buffer[2688], font1_ascii, font1_offsets, 440, 300, byColor_68, 0, 200, 640, pal_addr);
        } else if ((textures_off & TEX_OFF_PANEL_RESTRICTED) != 0) {
          if (iFrontendConfigVideoState == 4)
            byColor_69 = 0xAB;
          else
            byColor_69 = 0x8F;
          menu_render_scaled_text(mr, 15, &config_buffer[3776], font1_ascii, font1_offsets, 440, 300, byColor_69, 0, 200, 640, pal_addr);
        } else {
          if (iFrontendConfigVideoState == 4)
            byColor_70 = 0xAB;
          else
            byColor_70 = 0x8F;
          menu_render_scaled_text(mr, 15, &config_buffer[2624], font1_ascii, font1_offsets, 440, 300, byColor_70, 0, 200, 640, pal_addr);
        }
        if (iFrontendConfigVideoState == 3)
          byColor_71 = 0xAB;
        else
          byColor_71 = 0x8F;
        menu_render_scaled_text(mr, 15, &config_buffer[3712], font1_ascii, font1_offsets, 435, 320, byColor_71, 2u, 200, 640, pal_addr);
        if (view_limit) {
          if (iFrontendConfigVideoState == 3)
            byColor_72 = 0xAB;
          else
            byColor_72 = 0x8F;
          menu_render_scaled_text(mr, 15, &config_buffer[3776], font1_ascii, font1_offsets, 440, 320, byColor_72, 0, 200, 640, pal_addr);
        } else {
          if (iFrontendConfigVideoState == 3)
            byColor_73 = 0xAB;
          else
            byColor_73 = 0x8F;
          menu_render_scaled_text(mr, 15, &config_buffer[3840], font1_ascii, font1_offsets, 440, 320, byColor_73, 0, 200, 640, pal_addr);
        }
        if (iFrontendConfigVideoState == 2)
          byColor_74 = 0xAB;
        else
          byColor_74 = 0x8F;
        menu_render_scaled_text(mr, 15, &config_buffer[3904], font1_ascii, font1_offsets, 435, 340, byColor_74, 2u, 200, 640, pal_addr);
        if (game_svga)
          iReturnValue = (100 * game_size) >> 7;
          //iReturnValue = (100 * game_size) % 128;
          //iReturnValue = (100 * game_size - (__CFSHL__((100 * game_size) >> 31, 7) + ((100 * game_size) >> 31 << 7))) >> 7;
        else
          iReturnValue = (100 * game_size) >> 6;
          //iReturnValue = (100 * game_size) % 64;
          //iReturnValue = (100 * game_size - (__CFSHL__((100 * game_size) >> 31, 6) + ((100 * game_size) >> 31 << 6))) >> 6;
        sprintf(buffer, "%i %%", iReturnValue);
        if (iFrontendConfigVideoState == 2)
          byColor_76 = 0xAB;
        else
          byColor_76 = 0x8F;
        menu_render_scaled_text(mr, 15, buffer, font1_ascii, font1_offsets, 440, 340, byColor_76, 0, 200, 640, pal_addr);
        if (game_svga) {
          if (iFrontendConfigVideoState == 1)
            byColor_75 = 0xAB;
          else
            byColor_75 = 0x8F;
          menu_render_scaled_text(mr, 15, &config_buffer[512], font1_ascii, font1_offsets, 440, 360, byColor_75, 1u, 20, 640, pal_addr);
        } else {
          if (iFrontendConfigVideoState == 1)
            byColor_77 = 0xAB;
          else
            byColor_77 = 0x8F;
          menu_render_scaled_text(mr, 15, &config_buffer[448], font1_ascii, font1_offsets, 440, 360, byColor_77, 1u, 200, 640, pal_addr);
        }
        if (iFrontendConfigVideoState)
          byColor_78 = 0x8F;
        else
          byColor_78 = 0xAB;
        menu_render_scaled_text(mr, 15, &config_buffer[832], font1_ascii, font1_offsets, 430, 380, byColor_78, 2u, 200, 640, pal_addr);
        goto RENDER_FRAME;
      case 5:
        if (iFrontendConfigState != 6)
          iFrontendConfigGraphicsState = -1;
        if (player_type == 2) {
          sprintf(buffer, "%s %s", &config_buffer[4480], &config_buffer[4864]);
          if (iFrontendConfigGraphicsState == 6)
            byColor_79 = 0xAB;
          else
            byColor_79 = 0x8F;
          menu_render_scaled_text(mr, 15, buffer, font1_ascii, font1_offsets, 435, 78, byColor_79, 2u, 200, 640, pal_addr);
          if (iFrontendConfigGraphicsState == 6)
            byColor_80 = 0xAB;
          else
            byColor_80 = 0x8F;
          menu_render_scaled_text(mr, 15, &config_buffer[64 * game_view[1] + 4928], font1_ascii, font1_offsets, 440, 78, byColor_80, 0, 200, 640, pal_addr);
        }
        sprintf(buffer, "%s %s", &config_buffer[4416], &config_buffer[4864]);
        if (iFrontendConfigGraphicsState == 5)
          byColor_81 = 0xAB;
        else
          byColor_81 = 0x8F;
        menu_render_scaled_text(mr, 15, buffer, font1_ascii, font1_offsets, 435, 96, byColor_81, 2u, 200, 640, pal_addr);
        if (iFrontendConfigGraphicsState == 5)
          byColor_82 = 0xAB;
        else
          byColor_82 = 0x8F;
        menu_render_scaled_text(mr, 15, &config_buffer[64 * game_view[0] + 4928], font1_ascii, font1_offsets, 440, 96, byColor_82, 0, 200, 640, pal_addr);
        if (iFrontendConfigGraphicsState == 4)
          byColor_83 = 0xAB;
        else
          byColor_83 = 0x8F;
        menu_render_scaled_text(mr, 15, &config_buffer[5440], font1_ascii, font1_offsets, 435, 114, byColor_83, 2u, 200, 640, pal_addr);
        if ((textures_off & TEX_OFF_KMH) != 0) {
          if (iFrontendConfigGraphicsState == 4)
            byColor_84 = 0xAB;
          else
            byColor_84 = 0x8F;
          menu_render_scaled_text(mr, 15, "KMH", font1_ascii, font1_offsets, 440, 114, byColor_84, 0, 200, 640, pal_addr);
        } else {
          if (iFrontendConfigGraphicsState == 4)
            byColor_85 = 0xAB;
          else
            byColor_85 = 0x8F;
          menu_render_scaled_text(mr, 15, "MPH", font1_ascii, font1_offsets, 440, 114, byColor_85, 0, 200, 640, pal_addr);
        }
        if (iFrontendConfigGraphicsState == 3)
          byColor_86 = 0xAB;
        else
          byColor_86 = 0x8F;
        menu_render_scaled_text(mr, 15, &config_buffer[5504], font1_ascii, font1_offsets, 435, 132, byColor_86, 2u, 200, 640, pal_addr);
        if (replay_record) {
          if (iFrontendConfigGraphicsState == 3)
            byColor_87 = 0xAB;
          else
            byColor_87 = 0x8F;
          menu_render_scaled_text(mr, 15, &config_buffer[2624], font1_ascii, font1_offsets, 440, 132, byColor_87, 0, 200, 640, pal_addr);
        } else {
          if (iFrontendConfigGraphicsState == 3)
            byColor_88 = 0xAB;
          else
            byColor_88 = 0x8F;
          menu_render_scaled_text(mr, 15, &config_buffer[2688], font1_ascii, font1_offsets, 440, 132, byColor_88, 0, 200, 640, pal_addr);
        }
        if (iFrontendConfigGraphicsState == 2)
          byColor_89 = 0xAB;
        else
          byColor_89 = 0x8F;
        menu_render_scaled_text(mr, 15, &config_buffer[4672], font1_ascii, font1_offsets, 435, 150, byColor_89, 2u, 200, 640, pal_addr);
        if (p_tex_size == 1) {
          if (iFrontendConfigGraphicsState == 2)
            byColor_90 = 0xAB;
          else
            byColor_90 = 0x8F;
          menu_render_scaled_text(mr, 15, &config_buffer[4736], font1_ascii, font1_offsets, 440, 150, byColor_90, 0, 200, 640, pal_addr);
        } else {
          if (iFrontendConfigGraphicsState == 2)
            byColor_91 = 0xAB;
          else
            byColor_91 = 0x8F;
          menu_render_scaled_text(mr, 15, &config_buffer[4800], font1_ascii, font1_offsets, 440, 150, byColor_91, 0, 200, 640, pal_addr);
        }
        if (iFrontendConfigGraphicsState == 1)
          byColor_92 = 0xAB;
        else
          byColor_92 = 0x8F;
        menu_render_scaled_text(mr, 15, &config_buffer[5888], font1_ascii, font1_offsets, 435, 168, byColor_92, 2u, 200, 640, pal_addr);
        if (false_starts) {
          if (iFrontendConfigGraphicsState == 1)
            byColor_93 = 0xAB;
          else
            byColor_93 = 0x8F;
          menu_render_scaled_text(mr, 15, &config_buffer[2624], font1_ascii, font1_offsets, 440, 168, byColor_93, 0, 200, 640, pal_addr);
        } else {
          if (iFrontendConfigGraphicsState == 1)
            byColor_94 = 0xAB;
          else
            byColor_94 = 0x8F;
          menu_render_scaled_text(mr, 15, &config_buffer[2688], font1_ascii, font1_offsets, 440, 168, byColor_94, 0, 200, 640, pal_addr);
        }
        if (iFrontendConfigGraphicsState)
          byColor_95 = 0x8F;
        else
          byColor_95 = 0xAB;
        menu_render_scaled_text(mr, 15, &config_buffer[832], font1_ascii, font1_offsets, 430, 186, byColor_95, 2u, 200, 640, pal_addr);
        goto RENDER_FRAME;
      case 6:
        if (iFrontendConfigEditingName == 1) {
          byColor_96 = 0xAB;
          byColor_97 = 0xA5;
        } else {
          byColor_96 = 0xA5;
          byColor_97 = 0xAB;
        }
        if (iFrontendConfigState != 7) {
          byColor_96 = 0x8F;
          byColor_97 = 0x8F;
        }
        if (iFrontendConfigEditingName == 1) {
          iTextPosX = 0;
          for (szMemPtr = network_messages[4 - iFrontendConfigNetworkState]; *szMemPtr; ++szMemPtr) {
            iFontChar = (uint8)font1_ascii[(uint8)*szMemPtr];
            if (iFontChar == 255)
              iTextPosX += 8;
            else
              iTextPosX += front_vga[15][iFontChar].iWidth + 1;
          }
          iTextPosX += 390;
          iY = 140 - 18 * iFrontendConfigNetworkState;
        }
        if (iFrontendConfigNetworkState == 5)
          byColor_106 = byColor_97;
        else
          byColor_106 = 0x8F;
        menu_render_text(mr, 15, &language_buffer[7296], font1_ascii, font1_offsets, 390, 50, byColor_106, 1u, pal_addr);
        if (iFrontendConfigNetworkState == 4)
          byColor_98 = byColor_97;
        else
          byColor_98 = 0x8F;
        menu_render_text(mr, 15, &config_buffer[5632], font1_ascii, font1_offsets, 385, 68, byColor_98, 2u, pal_addr);
        if (iFrontendConfigNetworkState == 4)
          byColor_99 = byColor_96;
        else
          byColor_99 = 0x8F;
        menu_render_scaled_text(mr, 15, network_messages[0], font1_ascii, font1_offsets, 390, 68, byColor_99, 0, 200, 630, pal_addr);
        if (iFrontendConfigNetworkState == 3)
          byColor_100 = byColor_97;
        else
          byColor_100 = 0x8F;
        menu_render_text(mr, 15, &config_buffer[5696], font1_ascii, font1_offsets, 385, 86, byColor_100, 2u, pal_addr);
        if (iFrontendConfigNetworkState == 3)
          byColor_101 = byColor_96;
        else
          byColor_101 = 0x8F;
        menu_render_scaled_text(mr, 15, network_messages[1], font1_ascii, font1_offsets, 390, 86, byColor_101, 0, 200, 630, pal_addr);
        if (iFrontendConfigNetworkState == 2)
          byColor_102 = byColor_97;
        else
          byColor_102 = 0x8F;
        menu_render_text(mr, 15, &config_buffer[5760], font1_ascii, font1_offsets, 385, 104, byColor_102, 2u, pal_addr);
        if (iFrontendConfigNetworkState == 2)
          byColor_103 = byColor_96;
        else
          byColor_103 = 0x8F;
        menu_render_scaled_text(mr, 15, network_messages[2], font1_ascii, font1_offsets, 390, 104, byColor_103, 0, 200, 630, pal_addr);
        if (iFrontendConfigNetworkState == 1)
          byColor_104 = byColor_97;
        else
          byColor_104 = 0x8F;
        menu_render_text(mr, 15, &config_buffer[5824], font1_ascii, font1_offsets, 385, 122, byColor_104, 2u, pal_addr);
        if (iFrontendConfigNetworkState != 1)
          byColor_96 = 0x8F;
        byTempChar2 = byColor_96;
        iFrontendConfigNetworkState_1 = iFrontendConfigNetworkState;
        menu_render_scaled_text(mr, 15, network_messages[3], font1_ascii, font1_offsets, 390, 122, byTempChar2, 0, 200, 630, pal_addr);
        if (iFrontendConfigNetworkState_1)
          byColor_97 = 0x8F;
        menu_render_text(mr, 15, &config_buffer[832], font1_ascii, font1_offsets, 390, 140, byColor_97, 1u, pal_addr);
        if (iFrontendConfigEditingName == 1 && (frames & 0xFu) < 8) {
          iX = stringwidth(network_messages[4 - iFrontendConfigNetworkState]) + 390;
          if (iX <= 620)
            menu_render_text(mr, 15, "_", font1_ascii, font1_offsets, iX, iY, 0xABu, 0, pal_addr);
          else
            menu_render_text(mr, 15, "_", font1_ascii, font1_offsets, 621, iY, 0xABu, 0, pal_addr);
        }
        goto RENDER_FRAME;
      default:
        goto RENDER_FRAME;
    }
}

void snapshot_render_menu_configure(void)
{
  snapshot_setup_frontend_menu_state(0);
  frontend_config_enter();
  frontend_config_update();
}

//-------------------------------------------------------------------------------------------------
//00046EA0
void front_displaycalibrationbar(int iY, int iX, int iValue)
{
  int iClampedValue; // edi
  uint8 *pScreenPos; // ecx
  int iIndicatorPos; // edi
  int i; // esi
  int iScreenWidth; // ebp

  iClampedValue = iValue;
  if (iValue < -100)
    iClampedValue = -100;
  if (iClampedValue > 100)
    iClampedValue = 100;
  pScreenPos = &scrbuf[iY + winw * iX];
  if (current_mode) {
    iIndicatorPos = iClampedValue + 103;
    for (i = 0; i < 17; ++i) {
      if (i && i != 16) {
        *pScreenPos = 0x8F;
        pScreenPos[206] = 0x8F;
        pScreenPos[iIndicatorPos] = 0xAB;
        pScreenPos[iIndicatorPos - 1] = 0xAB;
        pScreenPos[iIndicatorPos - 2] = 0xAB;
        pScreenPos[iIndicatorPos + 1] = 0xAB;
        pScreenPos[iIndicatorPos + 2] = 0xAB;
        pScreenPos[103] = 0xE7;
        pScreenPos[104] = 0xE7;
        iScreenWidth = winw;
        pScreenPos[102] = 0xE7;
        pScreenPos += iScreenWidth;
      } else {
        memset(pScreenPos, 0x8F, 0xCEu);
        pScreenPos += winw;
      }
    }
  }
}

//-------------------------------------------------------------------------------------------------
//00046F40
void front_volumebar(int iY, int iVolumeLevel, int iFillColor)
{
  uint8 *pbyScreenPos; // ecx
  int iRow; // esi
  int iScreenWidth; // eax

  pbyScreenPos = &scrbuf[winw * iY + 430];      // Calculate starting position in screen buffer (430 pixels from left edge)
  for (iRow = 0; iRow < 17; ++iRow)           // Draw 17 rows for the volume bar
  {                                             // Skip first and last row (draw border on those)
    if (iRow && iRow != 16) {
      *pbyScreenPos = 0x8F;                     // Draw left border (0x8F is white in PALETTE.PAL)
      memset(pbyScreenPos + 1, iFillColor, 160 * iVolumeLevel / 127);// Fill volume bar based on level (160 * volume / 127 pixels wide)
      iScreenWidth = winw;
      pbyScreenPos[161] = 0x8F;                 // Draw right border  (0x8F is white in PALETTE.PAL)
      pbyScreenPos += iScreenWidth;
    } else {
      memset(pbyScreenPos, 0x8F, 0xA2u);        // Draw top/bottom border - fill entire width (162 pixels) with border color
      pbyScreenPos += winw;
    }
  }
}

//-------------------------------------------------------------------------------------------------
