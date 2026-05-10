#include "mouse.h"
#include "3d.h"
//-------------------------------------------------------------------------------------------------
// 00075AA0
void initmouse() {
  // REGS registers; // [esp+0h] [ebp-24h] BYREF
  // registers.w.ax = 0;                           // function 0 = reset mouse
  // and get status int386(0x33, &registers, &registers);         // 0x33 =
  // mouse services mouse = registers.h.al;                       // mouse
  // installed flag
}

//-------------------------------------------------------------------------------------------------
// 00075AD0
void mousexy() {
  // int iMouseX; // ecx
  // int iMouseY; // esi
  // REGS registers; // [esp+0h] [ebp-2Ch] BYREF
  //
  // registers.w.ax = 0xB;                         // 0xb = Get Mouse Motion
  // Counters int386(0x33, &registers, &registers);         // 0x33 = mouse
  // services
  //
  //// get mouse x and y deltas
  // dxmouse = (int16)registers.w.cx;
  // dymouse = (int16)registers.w.dx;
  //
  //// calculate new mouse position
  // iMouseX = (int16)registers.w.cx + mousex;
  // iMouseY = mousey - (int16)registers.w.dx;
  //
  //// clamp to screen bounds
  // if (iMouseX > XMAX - 1)
  //   iMouseX = XMAX - 1;
  // if (iMouseY > YMAX - 1)
  //   iMouseY = YMAX - 1;
  // if (iMouseX < 0)
  //   iMouseX = 0;
  // if (iMouseY < 0)
  //   iMouseY = 0;
  //
  //// store updated position
  // mousey = iMouseY;
  // mousex = iMouseX;
}

//-------------------------------------------------------------------------------------------------
// 00075B60
int mousebut(uint16 unMask) {
  // REGS regs; // [esp+0h] [ebp-28h] BYREF
  // if (!mouse)
  //   return 0;
  // regs.w.ax = 5;                                // Function 5: Get button
  // status regs.w.bx = 0; int386(0x33, &regs, &regs);                   // 0x33
  // = mouse services return unMask & regs.w.ax;                    // Return
  // masked button status
  return 0;
}

//-------------------------------------------------------------------------------------------------
// 00075BB0
void checkmouse() {
  // char byCurrPressed; // cl
  // REGS regs; // [esp+0h] [ebp-2Ch] BYREF
  // char byPrevPressed; // [esp+1Ch] [ebp-10h]
  //
  // byPrevPressed = mpressed;
  // if (mouse) {
  //   regs.w.ax = 5;                              // Function 5: Get button
  //   status regs.w.bx = 0; int386(0x33, &regs, &regs);                 // 0x33
  //   = Mouse driver interrupt byCurrPressed = regs.h.al & 3;              //
  //   Mask to left+right button bits
  // } else {
  //   byCurrPressed = 0;
  // }
  // mpressed = byCurrPressed;
  // if (byCurrPressed) {
  //   if (byPrevPressed)
  //     jpressed = 0;                             // Held down, not new
  //   else
  //     jpressed = -1;                            // New press
  // } else {
  //   jpressed = 0;                               // Not pressed
  // }
}

//-------------------------------------------------------------------------------------------------
// 00075C30
void drmouse(int iX0, int iY0, uint8 *pData, uint8 *pScreenBase) {
  int iScreenWidth;  // edi
  uint8 *pDataPlus1; // ebx
  int iHotX;         // eax
  uint8 *pDataItr2;  // ebx
  int iYItr;         // ebp
  int v10;           // esi
  uint8 *pRowPtr;    // edx
  int i;             // eax
  int iPixel;        // ecx
  uint8 *pScreenBuf; // [esp+0h] [ebp-1Ch]
  int iYEnd;         // [esp+4h] [ebp-18h]
  int iXStart;       // [esp+8h] [ebp-14h]

  iScreenWidth = XMAX;
  pDataPlus1 = pData + 1;

  // Read cursor hotspot offset from image data
  iHotX = *(pDataPlus1 - 1);
  pDataItr2 = pDataPlus1 + 1;

  // Calculate top-left position accounting for hotspot
  iXStart = iX0 - iHotX;
  iYItr = iY0 - *(pDataItr2 - 1);

  // Offset into screen buffer
  pScreenBuf = &pScreenBase[iX0 - iHotX];

  // Calculate bounding box (16x16 image)
  v10 = iX0 - iHotX + 16;
  iYEnd = iYItr + 16;
  do {
    // Row start pos in screen buf
    pRowPtr = &pScreenBuf[iScreenWidth * iYItr];

    // Iterate through cursor columns
    for (i = iXStart; i < v10; ++pRowPtr) {
      iPixel = *pDataItr2++;

      // Draw non-transparent pixels within horizontal scren bounds
      if (iPixel && i > 0 && i < iScreenWidth)
        *pRowPtr = iPixel; // draw pixel to screen
      ++i;
    }
    ++iYItr;
  } while (iYItr != iYEnd);
  XMAX = iScreenWidth;
}

//-------------------------------------------------------------------------------------------------
// 00075CC0
void plotmouse() {
  int iScreenWidth;  // ecx
  int iMouseX;       // esi
  uint8 *pScreenBuf; // eax

  iScreenWidth = XMAX;
  iMouseX = mousex;

  // Get the location of the mouse in scrbuf (320x200)
  pScreenBuf = &scrbuf[mousex + XMAX * (199 - mousey)];

  // Draw a crosshair mouse cursor, color 0x8F (white in PALETTE.PAL)

  // horizontal arms (with bounds checking)
  if (mousex > 3)
    *(pScreenBuf - 4) = 0x8F;
  if (iMouseX > 2)
    *(pScreenBuf - 3) = 0x8F;
  *pScreenBuf = 0x8F;
  if (iMouseX < iScreenWidth - 3)
    pScreenBuf[3] = 0x8F;
  if (iMouseX < iScreenWidth - 4)
    pScreenBuf[4] = 0x8F;

  // vertical arms (no Y bounds checking)
  pScreenBuf[-4 * iScreenWidth] = 0x8F;
  pScreenBuf[-3 * iScreenWidth] = 0x8F;
  pScreenBuf[3 * iScreenWidth] = 0x8F;
  pScreenBuf[4 * iScreenWidth] = 0x8F;
  mousex = iMouseX;
  XMAX = iScreenWidth;
}

//-------------------------------------------------------------------------------------------------
