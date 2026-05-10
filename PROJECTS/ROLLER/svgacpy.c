#include "svgacpy.h"
#include "3d.h"
#include <assert.h>
#include <string.h>
//-------------------------------------------------------------------------------------------------

int Vbytesperline; // 00178068
int winrange;      // 0017806C

//-------------------------------------------------------------------------------------------------
// 0004E4B0
int vesastart(int iX, int iY) {
  // RMI.ecx = iX;
  // RMI.edx = iY;
  // RMI.es = vmode_inf >> 4;                      // Convert vmode_inf to
  // segment address RMI.ebx = 0;                                  //
  // Subfunction: Set Display Start RMI.eax = 0x4F07; // VESA Function: Set/Get
  // Display Start
  //
  //// Make real-mode interrupt call
  // realmode(0x10u);
  //
  //// Check VESA function support
  // if (LOBYTE(RMI.eax) == 0x4F)
  //   return BYTE1(RMI.eax) != 0;                 // Check function success
  //   status
  // puts("VESA function not supported.");
  // return 2;

  return 0;
}

//-------------------------------------------------------------------------------------------------
// 0004E530
int tryvesa(int iModeNumber) {
  // RMI.ecx = iModeNumber;                        // VESA mode number to query
  // RMI.es = vmode_inf >> 4;                      // Convert vmode_inf to
  // segment address RMI.eax = 0x4F01;                             // VESA
  // Function: Get Mode Information RMI.edi = 0; // Offset part of buffer
  // pointer
  //
  //// Make real-mode interrupt call
  // realmode(0x10u);
  //
  //// Check VESA function support
  // if (LOBYTE(RMI.eax) == 0x4F)
  //   return BYTE1(RMI.eax) != 0;                 // Check function success
  //   status
  //
  // puts("VESA function not supported.");
  // return 2;

  return 0;
}

//-------------------------------------------------------------------------------------------------
// 0004E5A0
void *VESASource(int iX, int iY, int iOffset) {
  //// Get bytes per pixel from VESA mode info
  // unsigned char bpp_byte = *(unsigned char *)(vmode_inf[0] + 0x19);
  // int iBytesPerPixel = bpp_byte >> 3;  // Convert bits to bytes
  //
  //// Calculate buffer offset: y * pitch + x * bpp + additional offset
  // int iBufferOffset = iY * Vbytesperline + iX * iBytesPerPixel + iOffset;
  //
  //// Calculate window index and window-relative offset
  // int iWindowIndex = iBufferOffset / winrange;
  // int iWindowOffset = iBufferOffset % winrange;
  //
  //// Set new window if needed
  // if (iWindowIndex != vmode_inf[1]) {
  //   union REGS regs;
  //   regs.x.ax = 0x4F05;      // VESA function: Window Control
  //   regs.x.bx = 0x0001;      // Set window (subfunction 1 for source)
  //   regs.x.dx = window_index; // Window position
  //   int386(0x10, &regs);    // Call VESA BIOS
  //
  //   vmode_inf[1] = window_index; // Update current window index
  // }
  //
  //// Return pointer to source location
  // return (uint8 *)VesaSBase + iWindowOffset;

  return NULL;
}

//-------------------------------------------------------------------------------------------------
// 0004E630
void *VESADest(int iX, int iY, int iOffset) {
  //// Get bytes per pixel from VESA mode info
  // unsigned char bpp_byte = *(unsigned char *)(vmode_inf + 0x19);
  // int iBytesPerPixel = bpp_byte >> 3;  // Convert bits to bytes
  //
  //// Calculate buffer offset: y * pitch + x * bpp + additional offset
  // int iBufferOffset = iY * Vbytesperline + iX * iBytesPerPixel + iOffset;
  //
  //// Calculate window index and window-relative offset
  // int window_index = iBufferOffset / winrange;
  // int window_offset = iBufferOffset % winrange;
  //
  //// Set new window if needed
  // if (window_index != vmode_inf[2]) {
  //   union REGS regs;
  //   regs.x.ax = 0x4F05;      // VESA function: Window Control
  //   regs.x.bx = 0x0000;      // Set window (subfunction 0 for destination)
  //   regs.x.dx = window_index; // Window position
  //   int386(0x10, &regs);    // Call VESA BIOS
  //
  //   vmode_inf[2] = window_index; // Update current window index
  // }
  //
  //// Return pointer to destination location
  // return (char *)VesaDBase + window_offset;

  return NULL;
}

//-------------------------------------------------------------------------------------------------
// 0004E6C0
int VESAmode(int *vesaModeAy) {
  // int iRetVal; // ebx
  // int16 *pnModes; // edx
  // int iMode; // eax
  // int iNextMode; // ecx
  // uint16 unModeInf; // ax
  // union REGS regs; // [esp+0h] [ebp-3Ch] BYREF
  // struct SREGS sregs; // [esp+1Ch] [ebp-20h] BYREF
  // memset(&sregs, 0, sizeof(sregs));
  //
  //// Initialize VESA controller info if needed
  // if (!vesa_inf) {
  //   // Allocate DOS memory for VESA info
  //   regs.w.ax = 0x100;
  //   regs.w.bx = 0x10;
  //   int386x(0x31, &regs, &regs, &sregs);
  //   vesa_inf = (char *)(16 * regs.w.ax);        // Convert segment to linear
  //   address if (regs.x.cflag) {
  //     puts("Internal error : VESA display could not allocate core memory.");
  //     return -1;
  //   }
  //
  //   // Get VESA controller information
  //   RMI.edi = 0;                                // Offset 0 in segment
  //   RMI.es = (16 * regs.w.ax) >> 4;             // Segment part
  //   RMI.eax = 0x4F00;                           // VESA get controller info
  //   realmode(0x10u);
  //
  //   // Check for VESA support
  //   if (LOBYTE(RMI.eax) != 0x4F)
  //     goto LABEL_5;
  //   if (BYTE1(RMI.eax)) {
  //   LABEL_7:
  //     puts("VESA function call failed.");
  //     return -1;
  //   }
  //
  //   // Verify VESA signature
  //   if (strncmp(vesa_inf, "VESA", 4))
  //     puts("Internal error : VESA not initialised.");
  // }
  //
  //// Initialize mode info buffer if needed
  // if (vmode_inf[0]
  //   || (regs.w.ax = 0x100,
  //       regs.w.bx = 0x10,
  //       int386x(0x31, &regs, &regs, &sregs),
  //       vmode_inf[0] = 16 * regs.w.ax,
  //       !regs.x.cflag)) {
  //   iRetVal = -1;
  //   if (*vesaModeAy != -1) {
  //     do {
  //       if (iRetVal != -1)
  //         break;
  //
  //       // Get pointer to mode list from VESA info
  //       for (pnModes = (int16 *)(((*(int *)(vesa_inf + 14) >> 12) & 0xFFFF0)
  //                                + (unsigned __int16)*(_DWORD *)(vesa_inf +
  //                                14)); ; ++pnModes) {
  //         iMode = *pnModes;
  //         if (iMode == -1)
  //           break;
  //         if (iMode == *vesaModeAy && !tryvesa(iMode)) {
  //           iRetVal = *pnModes;
  //           break;
  //         }
  //       }
  //       iNextMode = vesaModeAy[1];
  //       ++vesaModeAy;
  //     } while (iNextMode != -1);
  //   }
  //   if (iRetVal <= -1) {
  //     puts(NoSupportedVesaMode);                // No supported VESA mode
  //     available.
  //   } else {
  //     // Set up mode parameters
  //     winrange = *(__int16 *)(vmode_inf[0] + 4) << 10;// Win size in KB ->
  //     bytes Vbytesperline = *(__int16 *)(vmode_inf[0] + 16);// Bytes per
  //     scanline
  //
  //     // Calculate base addresses (segment * 16)
  //     unModeInf = *(_WORD *)(vmode_inf[0] + 10);
  //     VesaSBase = 16 * *(unsigned __int16 *)(vmode_inf[0] + 8);
  //     RMI.ebx = iRetVal;
  //     VesaDBase = 16 * unModeInf;
  //     RMI.eax = 0x4F02;
  //     realmode(0x10u);
  //
  //     // Verify mode set success
  //     if (LOBYTE(RMI.eax) != 0x4F) {
  //     LABEL_5:
  //       puts("VESA function not supported.");
  //       return -1;
  //     }
  //     if (BYTE1(RMI.eax))
  //       goto LABEL_7;
  //   }
  //   return iRetVal;
  // } else {
  //   puts("Internal error : VESA display could not allocate core memory.");
  //   return -1;
  // }

  return 0; // added by ROLLER, pretend to be VESA mode 0
}

//-------------------------------------------------------------------------------------------------
// 0004E940
void svgacopy(uint8 *pSrc, int16 iX, int16 iY, int iWidth, int iHeight) {
  return; // added by ROLLER

  // Calculate initial video memory offset
  int iOffset = iX * Vbytesperline + iY;
  int iWindowIdx = iOffset / winrange;
  int iWindowOffset = iOffset % winrange;
  uint8 *pVideoPtr = (uint8 *)0xA0000 + iWindowOffset;

  // Set initial VESA window
  // union REGS regs;
  // regs.x.ax = 0x4F05;      // VESA function 05h: Window Control
  // regs.x.bx = 0x0000;      // BH=0 (Window A), BL=0 (Set window position)
  // regs.x.dx = iWindowIdx;  // Window position in granularity units
  // int386(0x10, &regs, &regs);

  while (iHeight > 0) {
    // Calculate rows that fit in current window
    int iBytesLeft = winrange - iWindowOffset;
    int iRowsInWindow = iBytesLeft / Vbytesperline;

    if (iRowsInWindow <= 0)
      iRowsInWindow = 1; // At least 1 partial row
    if (iRowsInWindow > iHeight)
      iRowsInWindow = iHeight;

    // Copy full rows
    for (int row = 0; row < iRowsInWindow; row++) {
      // Copy entire width
      memcpy(pVideoPtr, pSrc, iWidth);
      pVideoPtr += Vbytesperline;
      pSrc += winw;
    }

    iHeight -= iRowsInWindow;
    if (iHeight <= 0)
      break;

    // Update position for next segment
    iOffset += iRowsInWindow * Vbytesperline;
    iWindowIdx = iOffset / winrange;
    iWindowOffset = iOffset % winrange;

    // Set new VESA window
    // union REGS regs;
    // regs.x.ax = 0x4F05;      // VESA function 05h: Window Control
    // regs.x.bx = 0x0000;      // BH=0 (Window A), BL=0 (Set window position)
    // regs.x.dx = iWindowIdx;  // Window position in granularity units
    // int386(0x10, &regs, &regs);
    pVideoPtr = (uint8 *)0xA0000 + iWindowOffset;

    // Handle partial row at window boundary
    int iSegment1 = Vbytesperline - iWindowOffset;
    if (iSegment1 > iWidth)
      iSegment1 = iWidth;

    memcpy(pVideoPtr, pSrc, iSegment1);

    // Handle remainder if row crosses window boundary
    if (iSegment1 < iWidth) {
      int iSegment2 = iWidth - iSegment1;
      // union REGS regs;
      // regs.x.ax = 0x4F05;      // VESA function 05h: Window Control
      // regs.x.bx = 0x0000;      // BH=0 (Window A), BL=0 (Set window position)
      // regs.x.dx = iWindowIdx + 1;  // Window position in granularity units
      // int386(0x10, &regs, &regs);
      memcpy((unsigned char *)0xA0000, pSrc + iSegment1, iSegment2);
      iWindowOffset = iSegment2;
      pVideoPtr = (unsigned char *)0xA0000 + iSegment2;
    }

    // Move to next row
    pSrc += winw;
    iHeight--;
  }

  // Reset to window 0 before returning
  // union REGS regs;
  // regs.x.ax = 0x4F05;      // VESA function 05h: Window Control
  // regs.x.bx = 0x0000;      // BH=0 (Window A), BL=0 (Set window position)
  // regs.x.dx = 0;           // Window position in granularity units
  // int386(0x10, &regs, &regs);
}

//-------------------------------------------------------------------------------------------------
