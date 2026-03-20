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
// 3D preview viewport (GPU backend only; software backend ignores these)
#define PREVIEW_X         248
#define PREVIEW_W         300
#define PREVIEW_H         330
#define CAR_PREVIEW_Y      57
#define TRACK_PREVIEW_Y    5
void save_params()
{
  p_tex_size = gfx_size;
}

//-------------------------------------------------------------------------------------------------
//00049C60
void reset_params()
{
  gfx_size = p_tex_size;
}

//-------------------------------------------------------------------------------------------------
//00049C70
void loadcheatnames()
{
  char buffer[0x400]; // Temporary buffer for file contents
  FILE *fp;
  int iSize;
  char *szTok;
  int iCheatIdx = 0;

  // Try to open PASSWORD.INI to get its size
  iSize = ROLLERfilelength("PASSWORD.INI");

  fp = ROLLERfopen("PASSWORD.INI", "rb");
  if (!fp)
    return;

  // Read file into buffer
  fread(buffer, iSize, 1, fp);
  fclose(fp);

  // Decode the read contents
  decode((uint8 *)buffer, iSize, 23, 37);

  // Tokenize buffer and load cheat names
  szTok = strtok(buffer, "\n\t\r");

  while (szTok) {
    // Check for end marker string
    if (strcmp(szTok, "#") == 0)
      break;

    // Copy token into cheat_names[iCheatIdx]
    strncpy(cheat_names[iCheatIdx], szTok, 9);

    iCheatIdx++;
    szTok = strtok(NULL, "\n\t\r");
  }

  // Set final cheat name entry to end marker
  cheat_names[iCheatIdx][0] = '#';
  cheat_names[iCheatIdx][1] = '\0';

  // Wipe the buffer
  memset(buffer, 0, 0x57);

  // Perform another decode on cheat_names
  decode((uint8 *)cheat_names, 288, 43, 87);
}

//-------------------------------------------------------------------------------------------------
//0004A5C0
int CheckNames(char *szPlayerName, int iPlayerIdx)
{
  int iCheatIdx = 0;

  // Decode cheat names list
  decode((uint8*)cheat_names, 288, 43, 87);

  // Skip processing if cheat list is empty
  if (cheat_names[0][0] == '#') {
    // Re-encode cheat names
    decode((uint8*)cheat_names, 288, 43, 87);
    return 0;
  }

  char *szCurrCheat = (char *)cheat_names;

  // Process all cheat names until terminator '#'
  while (*szCurrCheat != '#') {
      // Check if player name matches current cheat name
    if (name_cmp(szPlayerName, szCurrCheat)) {
      // Handle cheats
      if (iCheatIdx <= 25) {
        switch (iCheatIdx) {
          case 0: //SUICYCO (cheat car, explode opponent)
            Players_Cars[iPlayerIdx] = CAR_DESIGN_SUICYCO;
            name_copy(szPlayerName, "DAMIAN");
            cheat_mode |= CHEAT_MODE_CHEAT_CAR;
            break;
          case 1: //MAYTE (cheat car, top speed)
            Players_Cars[iPlayerIdx] = CAR_DESIGN_MAYTE;
            name_copy(szPlayerName, "DAMON");
            cheat_mode |= CHEAT_MODE_CHEAT_CAR;
            break;
          case 2: //2X4B523P (cheat car, flip opponent)
            Players_Cars[iPlayerIdx] = CAR_DESIGN_2X4B523P;
            name_copy(szPlayerName, "GRAHAM");
            cheat_mode |= CHEAT_MODE_CHEAT_CAR;
            break;
          case 3: //TINKLE (cheat car, jump opponent)
            Players_Cars[iPlayerIdx] = CAR_DESIGN_TINKLE;
            name_copy(szPlayerName, "KEV");
            cheat_mode |= CHEAT_MODE_CHEAT_CAR;
            break;
          case 4: //LOVEBUN (formula car)
            Players_Cars[iPlayerIdx] = CAR_DESIGN_F1WACK;
            name_copy(szPlayerName, "LISA");
            cheat_mode |= CHEAT_MODE_CHEAT_CAR;
            break;
          case 5: //DR DEATH (death mode)
            name_copy(szPlayerName, "PAT");
            cheat_mode |= CHEAT_MODE_DEATH_MODE;
            break;
          case 6: //SUPERMAN (invincible human car)
            name_copy(szPlayerName, "MARK");
            cheat_mode |= CHEAT_MODE_INVINCIBLE;
            player_invul[iPlayerIdx] = -1;
            break;
          case 7: //REMOVE
            name_copy(szPlayerName, "MR BRUSH");
            if (cheat_mode & CHEAT_MODE_CLONES) {
              // Handle player cars and infinite laps
              short nPlayer1Car = (short)player1_car;
              if ((short)nPlayer1Car == iPlayerIdx) {
                for (int i = 0; i < players; i++) {
                  infinite_laps = -1;
                }
              } else {
                for (int i = 0; i < players; i++) {
                  if (i != (int)nPlayer1Car) {
                    Players_Cars[i] = -1;
                  }
                }
              }
              switch_same = -1;
            }
            if (cheat_mode & CHEAT_MODE_50HZ_TIMER) {
              release_ticktimer();
              claim_ticktimer(36);
            }
            cheat_mode = 0;
            player_invul[iPlayerIdx] = 0;
            CalcCarSizes();
            break;
          case 8: //TOPTUNES (alternate voice)
            name_copy(szPlayerName, "DJ SFX");
            cheat_samples = -1;
            releasesamples();
            loadfatalsample();
            break;
          case 9: //GOLDBOY (unlock premier cup)
            name_copy(szPlayerName, "PHIL");
            cup_won |= 1;
            textures_off |= TEX_OFF_PREMIER_CUP_AVAILABLE;
            break;
          case 10: //CUP WON (view end sequence)
            name_copy(szPlayerName, "LAZY");
            cheat_mode |= CHEAT_MODE_END_SEQUENCE;
            break;
          case 11: //I WON (view race history)
            name_copy(szPlayerName, "IDOL");
            cheat_mode |= CHEAT_MODE_RACE_HISTORY;
            break;
          case 12: //CINEMA (widescreen)
            name_copy(szPlayerName, "LUMIERE");
            cheat_mode |= CHEAT_MODE_WIDESCREEN;
            break;
          case 13: //ROLL EM (view credits)
            name_copy(szPlayerName, "MR CRED");
            cheat_mode |= CHEAT_MODE_CREDITS;
            break;
          case 14: //FORMULA1 (advanced car set)
            name_copy(szPlayerName, "NEIL");
            cheat_mode |= CHEAT_MODE_ADVANCED_CARS;
            break;
          case 15: //MREPRISE (bonus cup unlocked)
            name_copy(szPlayerName, "MR BONUS");
            textures_off |= TEX_OFF_BONUS_CUP_AVAILABLE;
            cup_won |= 2;
            break;
          case 16: //DUEL (killer opponents)
            name_copy(szPlayerName, "MR EVIL");
            cheat_mode |= CHEAT_MODE_KILLER_OPPONENTS;
            break;
          case 17: //PROCESS
          {
            float A, B;
            memcpy(&A, (uint32_t[]) { 1249905654 }, 4);
            memcpy(&B, (uint32_t[]) { 1245708284 }, 4);
            float res_float = (float)((float)(A / B) * 3145727.0f);
            uint32_t res_bits;
            memcpy(&res_bits, &res_float, 4);
            if (res_bits == 1249905654u) {
              name_copy(szPlayerName, "TYPE B");
            } else {
              name_copy(szPlayerName, "TYPE A");
            }
            break;
          }
          case 18: //MRFROSTY (icy roads)
            name_copy(szPlayerName, "SNOWMAN");
            cheat_mode |= CHEAT_MODE_ICY_ROAD;
            break;
          case 19: //MR ZOOM (50Hz timer)
            name_copy(szPlayerName, "SPEEDY");
            cheat_mode |= CHEAT_MODE_50HZ_TIMER;
            release_ticktimer();
            claim_ticktimer(50);
            break;
          case 20: //TACHYONS (100Hz timer)
            name_copy(szPlayerName, "NUCLEAR!");
            cheat_mode |= CHEAT_MODE_100HZ_TIMER | CHEAT_MODE_50HZ_TIMER;
            release_ticktimer();
            claim_ticktimer(100);
            break;
          case 21: //YOTARACE (double track size)
            name_copy(szPlayerName, "GULLIVER");
            cheat_mode |= CHEAT_MODE_DOUBLE_TRACK;
            break;
          case 22: //CLONES
            //removed?
            //cheat_mode |= CHEAT_MODE_CLONES
            break;
          case 23: //TINYTOTS (tiny cars)
            name_copy(szPlayerName, "TINYTIM");
            cheat_mode |= CHEAT_MODE_TINY_CARS;
            CalcCarSizes();
            break;
          case 24: //WARPGATE (warp)
            name_copy(szPlayerName, "HEADACHE");
            cheat_mode |= CHEAT_MODE_WARP;
            break;
          case 25: //FREAKY (invert colors)
            name_copy(szPlayerName, "PAINTER");
            cheat_mode |= CHEAT_MODE_FREAKY;
            break;
          default:
            break;
        }
      }
    }
    // Handle empty player name
    else if (*szPlayerName == '\0') {
      name_copy(szPlayerName, "MR DULL");
      if (iPlayerIdx == 0) {
        cheat_mode |= CHEAT_MODE_GRAYSCALE;
      }
      break;
    }
    //cheats added by ROLLER
    else if (name_cmp(szPlayerName, "CHRSTINE")) {
      Players_Cars[iPlayerIdx] = CAR_DESIGN_DEATH;
      name_copy(szPlayerName, "MR EVIL");
      cheat_mode |= CHEAT_MODE_CHEAT_CAR;
      break;
    }

    // Move to next cheat name
    szCurrCheat += 9;
    iCheatIdx++;
  }
  // Re-encode cheat names
  decode((uint8*)cheat_names, 288, 43, 87);

  return 0;
}

//-------------------------------------------------------------------------------------------------
//0004AB30
