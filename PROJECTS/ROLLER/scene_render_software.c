#include "scene_render_software.h"
#include "3d.h"
#include "drawtrk3.h"

#include "car.h"
#include "game_render.h"
#include "graphics.h"
#include "func2.h"
#include "polytex.h"
#include "roller.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#define SCENE_RENDER_MAX_TEXTURE_SLOTS 32

/* SubpolyType values passed to dodivide for texture routing.
 * -1 = wide wall (2048x1024), 0 = standard track (1024x1024),
 * 666 = building (other_texture[] lookup). */
#define SUBPOLY_WALL     (-1)
#define SUBPOLY_STANDARD   0
#define SUBPOLY_BUILDING 666

//-------------------------------------------------------------------------------------------------
//000257E0
static void dodivide(float fX0_3D, float fY0_3D, float fZ0_3D,
              float fX1_3D, float fY1_3D, float fZ1_3D,
              float fX2_3D, float fY2_3D, float fZ2_3D,
              float fX3_3D, float fY3_3D, float fZ3_3D,
              int iScreenX0, int iScreenY0, int iScreenX1, int iScreenY1, int iScreenX2, int iScreenY2, int iScreenX3, int iScreenY3,
              int iTexU, int iTexV, int iTexWid, int iTexHgt)
{
  int iCullFlag; // edx
  float fMaxZ; // eax
  float fMaxZ_temp; // eax
  float fMaxZ_final; // eax
  float fMinZ; // eax
  float fMinZ_temp; // eax
  float fMinZ_final; // eax
  int iMaxScreenX; // eax
  int iMaxScreenX_temp; // eax
  int iMinScreenX; // eax
  int iMinScreenX_temp; // eax
  int iMaxScreenY; // eax
  int iMaxScreenY_temp; // eax
  int iMinScreenY; // eax
  int iMinScreenY_temp; // eax
  tPolyParams *pPolyParams; // edx
  int iPolyType; // eax
  uint8 *pFrameBuf; // eax
  double dViewDist; // st7
  double dInvZ; // st6
  double dScreenScale; // st5
  double dProjX; // st4
  double dProjY; // st7
  double dViewDist_1; // st7
  double dInvZ_1; // st6
  double dScreenScale_1; // st5
  double dProjX_1; // st4
  double dProjY_1; // st7
  double dViewDist_2; // st7
  double dInvZ_2; // st6
  double dScreenScale_2; // st5
  double dProjX_2; // st4
  double dProjY_2; // st7
  double dViewDist_3; // st7
  double dInvZ_3; // st6
  double dScreenScale_3; // st5
  double dProjX_3; // st4
  double dProjY_3; // st7
  double dViewDist_4; // st7
  double dInvZ_4; // st6
  double dScreenScale_4; // st5
  double dProjX_4; // st4
  double dProjY_4; // st7
  double dViewDist_5; // st7
  double dInvZ_5; // st6
  double dScreenScale_5; // st5
  double dProjX_5; // st4
  double dProjY_5; // st7
  double dViewDist_6; // st7
  double dInvZ_6; // st6
  double dScreenScale_6; // st5
  double dProjX_6; // st4
  double dProjY_6; // st7
  double dViewDist_7; // st7
  double dInvZ_7; // st6
  double dScreenScale_7; // st5
  double dProjX_7; // st4
  double dProjY_7; // st7
  double dViewDist_8; // st7
  double dInvZ_8; // st6
  double dScreenScale_8; // st5
  double dProjX_8; // st4
  double dProjY_8; // st7
  int iTexU_right; // edx
  int iScreenCenterY; // [esp-28h] [ebp-180h]
  tPolyParams *pPolyParamsLocal; // [esp+Ch] [ebp-14Ch]
  float fMidZ_Edge01; // [esp+10h] [ebp-148h]
  float fMidZ_Edge23; // [esp+14h] [ebp-144h]
  float fMidZ_Edge03; // [esp+18h] [ebp-140h]
  int iRenderFlag; // [esp+1Ch] [ebp-13Ch]
  int iMaxScreenCoord; // [esp+28h] [ebp-130h]
  int iMinScreenCoord; // [esp+30h] [ebp-128h]
  float fMinZ_edge; // [esp+3Ch] [ebp-11Ch]
  float fMaxZ_quad; // [esp+54h] [ebp-104h]
  int iMaxScreenX_quad; // [esp+5Ch] [ebp-FCh]
  int iMinScreenY_quad; // [esp+64h] [ebp-F4h]
  int iScreenCenterX_calc; // [esp+78h] [ebp-E0h]
  float fMidZ_quad; // [esp+7Ch] [ebp-DCh]
  int iScreenY_mid30; // [esp+8Ch] [ebp-CCh]
  int iScreenX_mid30; // [esp+90h] [ebp-C8h]
  int iScreenY_mid12; // [esp+94h] [ebp-C4h]
  int iScreenX_mid23; // [esp+98h] [ebp-C0h]
  float fX_mid01; // [esp+9Ch] [ebp-BCh]
  float fY_mid01; // [esp+A0h] [ebp-B8h]
  int iScreenX_mid12; // [esp+A4h] [ebp-B4h]
  float fZ_mid30; // [esp+A8h] [ebp-B0h]
  float fY_mid30; // [esp+ACh] [ebp-ACh]
  float fX_mid30; // [esp+B0h] [ebp-A8h]
  float fZ_mid12; // [esp+B4h] [ebp-A4h]
  float fY_mid12; // [esp+B8h] [ebp-A0h]
  float fX_mid12; // [esp+BCh] [ebp-9Ch]
  float fZ_mid12_1; // [esp+C0h] [ebp-98h]
  float fY_mid12_1; // [esp+C4h] [ebp-94h]
  float fX_mid12_1; // [esp+C8h] [ebp-90h]
  int iScreenY_mid12_1; // [esp+CCh] [ebp-8Ch]
  int iScreenX_mid12_1; // [esp+D0h] [ebp-88h]
  int iScreenX_subdiv1; // [esp+D4h] [ebp-84h]
  int iTexV_bottom; // [esp+D8h] [ebp-80h]
  int iScreenY_mid01; // [esp+E4h] [ebp-74h]
  int iScreenY_mid23; // [esp+E8h] [ebp-70h]
  int iScreenX_mid01; // [esp+ECh] [ebp-6Ch]
  int iScreenY_mid30_1; // [esp+F0h] [ebp-68h]
  int iScreenX_mid23_1; // [esp+F4h] [ebp-64h]
  int iScreenY_mid30_2; // [esp+FCh] [ebp-5Ch]
  int iScreenX_mid12_2; // [esp+100h] [ebp-58h]
  int iScreenX_mid30_1; // [esp+104h] [ebp-54h]
  float fY_mid12_2; // [esp+108h] [ebp-50h]
  float fZ_mid12_2; // [esp+10Ch] [ebp-4Ch]
  float fZ_mid30_1; // [esp+110h] [ebp-48h]
  float fX_mid30_1; // [esp+114h] [ebp-44h]
  float fX_mid12_2; // [esp+118h] [ebp-40h]
  float fY_mid30_1; // [esp+11Ch] [ebp-3Ch]
  float fY_mid23; // [esp+120h] [ebp-38h]
  float fZ_mid23; // [esp+124h] [ebp-34h]
  float fX_mid23; // [esp+128h] [ebp-30h]
  int iScreenY_center; // [esp+12Ch] [ebp-2Ch]
  float fX_center; // [esp+130h] [ebp-28h]
  float fY_center; // [esp+134h] [ebp-24h]
  float fZ_center; // [esp+138h] [ebp-20h]
  int iScreenX_center; // [esp+13Ch] [ebp-1Ch]

  while (2) {
    iCullFlag = 0;
    divtype = 0;                                // Reset subdivision type

    // Find maximum Z value among all 4 verts
    if (fZ0_3D <= (double)fZ1_3D)
      fMaxZ = fZ1_3D;
    else
      fMaxZ = fZ0_3D;
    fMaxZ_quad = fMaxZ;

    if (fZ2_3D <= (double)fZ3_3D)
      fMaxZ_temp = fZ3_3D;
    else
      fMaxZ_temp = fZ2_3D;
    if (fMaxZ_quad <= (double)fMaxZ_temp) {
      if (fZ2_3D <= (double)fZ3_3D)
        fMaxZ_final = fZ3_3D;
      else
        fMaxZ_final = fZ2_3D;
    } else if (fZ0_3D <= (double)fZ1_3D) {
      fMaxZ_final = fZ1_3D;
    } else {
      fMaxZ_final = fZ0_3D;
    }

    // Cull if polygon is too close to camera (Z < 80)
    if (fMaxZ_final < 80.0)
      iCullFlag = -1;
    if (fZ0_3D >= (double)fZ1_3D)
      fMinZ = fZ1_3D;
    else
      fMinZ = fZ0_3D;
    fMinZ_edge = fMinZ;

    if (fZ2_3D >= (double)fZ3_3D)
      fMinZ_temp = fZ3_3D;
    else
      fMinZ_temp = fZ2_3D;

    if (fMinZ_edge >= (double)fMinZ_temp) {
      if (fZ2_3D >= (double)fZ3_3D)
        fMinZ_final = fZ3_3D;
      else
        fMinZ_final = fZ2_3D;
    } else if (fZ0_3D >= (double)fZ1_3D) {
      fMinZ_final = fZ1_3D;
    } else {
      fMinZ_final = fZ0_3D;
    }

    // Check if polygon is far enough away from camera
    if (fMinZ_final >= 1.0) {
      // Find bounding box of screen coords
      // Max X coord
      if (iScreenX1 >= iScreenX0)
        iMaxScreenCoord = iScreenX1;
      else
        iMaxScreenCoord = iScreenX0;
      iMaxScreenX = iScreenX2;
      if (iScreenX2 <= iScreenX3)
        iMaxScreenX = iScreenX3;
      if (iMaxScreenCoord <= iMaxScreenX) {
        iMaxScreenX_temp = iScreenX2;
        if (iScreenX2 <= iScreenX3)
          iMaxScreenX_temp = iScreenX3;
      } else {
        iMaxScreenX_temp = iScreenX0;
        if (iScreenX1 >= iScreenX0)
          iMaxScreenX_temp = iScreenX1;
      }

      // cull if bounding box is completely off-screen (left)
      if (iMaxScreenX_temp < 0)
        iCullFlag = -1;

      // Minimum X coord
      if (iScreenX1 <= iScreenX0)
        iMaxScreenX_quad = iScreenX1;
      else
        iMaxScreenX_quad = iScreenX0;
      iMinScreenX = iScreenX2;
      if (iScreenX2 >= iScreenX3)
        iMinScreenX = iScreenX3;
      if (iMaxScreenX_quad >= iMinScreenX) {
        iMinScreenX_temp = iScreenX2;
        if (iScreenX2 >= iScreenX3)
          iMinScreenX_temp = iScreenX3;
      } else {
        iMinScreenX_temp = iScreenX0;
        if (iScreenX1 <= iScreenX0)
          iMinScreenX_temp = iScreenX1;
      }

      // Cull if bounding box is completely off-screen (right)
      if (iMinScreenX_temp >= winw)
        iCullFlag = -1;

      // Maximum Y coord
      if (iScreenY0 <= iScreenY1)
        iMinScreenY_quad = iScreenY1;
      else
        iMinScreenY_quad = iScreenY0;
      iMaxScreenY = iScreenY2;
      if (iScreenY2 <= iScreenY3)
        iMaxScreenY = iScreenY3;
      if (iMinScreenY_quad <= iMaxScreenY) {
        iMaxScreenY_temp = iScreenY2;
        if (iScreenY2 <= iScreenY3)
          iMaxScreenY_temp = iScreenY3;
      } else if (iScreenY0 <= iScreenY1) {
        iMaxScreenY_temp = iScreenY1;
      } else {
        iMaxScreenY_temp = iScreenY0;
      }

      // Cull if bounding box is completely off-screen (top)
      if (iMaxScreenY_temp < 0)
        iCullFlag = -1;

      // Minimum Y coord
      if (iScreenY0 >= iScreenY1)
        iMinScreenCoord = iScreenY1;
      else
        iMinScreenCoord = iScreenY0;
      iMinScreenY = iScreenY2;
      if (iScreenY2 >= iScreenY3)
        iMinScreenY = iScreenY3;
      if (iMinScreenCoord >= iMinScreenY) {
        iMinScreenY_temp = iScreenY2;
        if (iScreenY2 >= iScreenY3)
          iMinScreenY_temp = iScreenY3;
      } else if (iScreenY0 >= iScreenY1) {
        iMinScreenY_temp = iScreenY1;
      } else {
        iMinScreenY_temp = iScreenY0;
      }

      // Cull if bounding box is completely off-screen (bottom)
      if (iMinScreenY_temp >= winh)
        iCullFlag = -1;

      // Render polygon
      iRenderFlag = -1;
    } else {
      // Polygon is too close, check if we should subdivide based on tex size
      if (iTexWid > min_sub_size)
        ++divtype;                              // subdivide horizontally
      if (iTexHgt > min_sub_size)
        divtype += 2;                           // subdivide vertically
      iRenderFlag = 0;
    }

    // If polygon is not culled
    if (!iCullFlag) {
      // Additional subdivision checks based on polygon screen size
      if (!divtype && !flatpol) {
        // Calculate perimeter in screen space to determine if polygon is large enough to subdivide
        polyxsize = abs(iScreenY3 - iScreenY2) + abs(iScreenY0 - iScreenY1) + abs(iScreenX3 - iScreenX2) + abs(iScreenX0 - iScreenX1);
        polyysize = abs(iScreenY1 - iScreenY2) + abs(iScreenY0 - iScreenY3) + abs(iScreenX1 - iScreenX2) + abs(iScreenX0 - iScreenX3);
        if (polyxsize > small_poly && iTexWid > min_sub_size)
          ++divtype;                            // Subdivide horizontally if pol is large enough
        if (polyysize > small_poly && iTexHgt > min_sub_size)
          divtype += 2;                         // Subdivide vertically if pol is large enough
      }

      // Calculate midpoints for potential subdivision
      fMidZ_Edge23 = fZ0_3D + fZ1_3D;
      fMidZ_Edge01 = fY0_3D + fY1_3D;
      fMidZ_Edge03 = fX0_3D + fX1_3D;
      fY_mid01 = fMidZ_Edge23 * 0.5f;            // Midpoint between verts 0 and 1
      fMidZ_quad = fMidZ_Edge01 * 0.5f;
      iScreenCenterX_calc = winw / 2;           // screen center x
      fX_mid01 = fMidZ_Edge03 * 0.5f;

      switch (divtype) {
        case 0:                                 // No subdivision - render the polygon
          if (!iRenderFlag)
            return;                             // don't render if flag is not set

          // Set up pol params for rendering
          pPolyParamsLocal = subpoly;
          pPolyParams = subpoly;
          subpoly->vertices[0].x = iScreenX0;
          pPolyParams->vertices[2].x = iScreenX2;
          pPolyParams->vertices[2].y = iScreenY2;
          pPolyParams->vertices[3].x = iScreenX3;
          pPolyParams->vertices[3].y = iScreenY3;

          // Set up texture coords in 16.16 fixed point
          startsx[0] = (iTexWid + iTexU - 1) << 12;
          startsx[1] = iTexU << 12;
          startsx[2] = iTexU << 12;
          startsx[3] = (iTexWid + iTexU - 1) << 12;
          startsy[0] = iTexV << 12;
          startsy[1] = iTexV << 12;
          pPolyParams->vertices[0].y = iScreenY0;
          pPolyParams->vertices[1].x = iScreenX1;
          startsy[2] = (iTexHgt + iTexV - 1) << 12;
          startsy[3] = (iTexHgt + iTexV - 1) << 12;

          iPolyType = subpolytype;
          pPolyParams->vertices[1].y = iScreenY1;

          // Render based on pol type
          if (iPolyType < 1) {
            if (iPolyType < -2) {
              // Check if tex should be applied
            LABEL_111:
              if ((subpoly->iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) != 0)// SURFACE_FLAG_APPLY_TEXTURE
              {
                // Render textured pol with car texture
                //TODO is this correct?
                game_render_quad_screen(
                  g_pGameRenderer,
                  subpoly,
                  game_render_get_texture_handle(g_pGameRenderer, car_texmap[subpolytype - 3]),
                  NULL);
                //POLYTEX(
                //  (&horizon_vga)[*(&car_draw_order[15].iChunkIdx + subpolytype)],// offset into car_texmap
                //  subptr,
                //  subpoly,
                //  *(&car_draw_order[15].iChunkIdx + subpolytype),// offset into car_texmap
                //  gfx_size);
                goto LABEL_115;
              }
              pFrameBuf = subptr;
              pPolyParams = subpoly;
            LABEL_114:
              game_render_quad_screen(g_pGameRenderer, pPolyParams, TEXTURE_HANDLE_INVALID, NULL); // render flat pol
            LABEL_115:
                          // Debug: draw pol outline if showsub is enabled
              if (showsub) {
                compout(subptr, subpoly->vertices[0].x, subpoly->vertices[0].y, subpoly->vertices[1].x, subpoly->vertices[1].y, 0x9Fu);// 0x9F is light blue in PALETTE.PAL
                compout(subptr, subpoly->vertices[1].x, subpoly->vertices[1].y, subpoly->vertices[2].x, subpoly->vertices[2].y, 0x9Fu);
                compout(subptr, subpoly->vertices[2].x, subpoly->vertices[2].y, subpoly->vertices[3].x, subpoly->vertices[3].y, 0x9Fu);
                compout(subptr, subpoly->vertices[0].x, subpoly->vertices[0].y, subpoly->vertices[1].x, subpoly->vertices[1].y, 0x9Fu);
              }
              return;
            }
            if ((pPolyParams->iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) == 0)// SURFACE_FLAG_APPLY_TEXTURE
              goto LABEL_106;
          } else if (iPolyType > 1) {
            if (iPolyType <= 2) {
            LABEL_107:
              pFrameBuf = subptr;
              goto LABEL_114;
            }
            if (iPolyType != 666)
              goto LABEL_111;
            if ((pPolyParams->iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) != 0)// SURFACE_FLAG_APPLY_TEXTURE
            {
              game_render_quad_screen(g_pGameRenderer, pPolyParamsLocal, game_render_get_texture_handle(g_pGameRenderer, 17), NULL);
              goto LABEL_115;
            }
          LABEL_106:
            pPolyParams = pPolyParamsLocal;
            goto LABEL_107;
          }
          // Default texture rendering
          game_render_quad_screen(g_pGameRenderer, pPolyParamsLocal, game_render_get_texture_handle(g_pGameRenderer, 0), NULL);
          goto LABEL_115;
        case 1:                                 // Horiz subdivision only
          // Calculate midpoint between verts 0 and 1, and 2 and 3
          if (fY_mid01 < 1.0 || isnan(fY_mid01)) {
            if ((double)iScreenCenterX_calc <= fX_mid01)
              iScreenX_mid12 = 20000;
            else
              iScreenX_mid12 = -20000;
            if ((double)(winh / 2) <= fMidZ_quad)
              iScreenX_subdiv1 = 20000;
            else
              iScreenX_subdiv1 = -20000;
          } else {
            // Project 3D coords into screen space
            dViewDist = (double)VIEWDIST;
            dInvZ = 1.0 / fY_mid01;
            dScreenScale = (double)scr_size;
            dProjX = (dViewDist * fX_mid01 * dInvZ + (double)xbase) * dScreenScale * 0.015625;
            dProjX = round(dProjX);//_CHP();
            iScreenX_mid12 = (int)dProjX;
            dProjY = dScreenScale * (199.0 - dInvZ * (dViewDist * fMidZ_quad) - (double)ybase) * 0.015625;
            dProjY = round(dProjY);//_CHP();
            iScreenX_subdiv1 = (int)dProjY;
          }

          // Calculate midpoint between verts 1 and 2
          fX_mid12 = (fX2_3D + fX3_3D) * 0.5f;
          fY_mid12 = (fY2_3D + fY3_3D) * 0.5f;
          fZ_mid12 = (fZ2_3D + fZ3_3D) * 0.5f;

          if (fZ_mid12 < 1.0 || isnan(fZ_mid12)) {
            if ((double)(winw / 2) <= fX_mid12)
              iScreenX_mid23 = 20000;
            else
              iScreenX_mid23 = -20000;
            if ((double)(winh / 2) <= fY_mid12)
              iScreenY_mid12 = 20000;
            else
              iScreenY_mid12 = -20000;
          } else {
            // Project to screen space
            dViewDist_1 = (double)VIEWDIST;
            dInvZ_1 = 1.0 / fZ_mid12;
            dScreenScale_1 = (double)scr_size;
            dProjX_1 = (dViewDist_1 * fX_mid12 * dInvZ_1 + (double)xbase) * dScreenScale_1 * 0.015625;
            dProjX_1 = round(dProjX_1);//_CHP();
            iScreenX_mid23 = (int)dProjX_1;
            dProjY_1 = dScreenScale_1 * (199.0 - dInvZ_1 * (dViewDist_1 * fY_mid12) - (double)ybase) * 0.015625;
            dProjY_1 = round(dProjY_1);//_CHP();
            iScreenY_mid12 = (int)dProjY_1;
          }

          // Half tex width for subdivision
          iTexWid >>= 1;

          // Check flip type for tex coord handling
          if ((fliptype & 1) != 0) {
            // Flip horiz - swap coords
            iScreenCenterY = iScreenX1;
            iScreenX1 = iScreenX_mid12;

            // Recursively subdivide left half
            dodivide(
              fX_mid01,
              fMidZ_quad,
              fY_mid01,
              fX1_3D,
              fY1_3D,
              fZ1_3D,
              fX2_3D,
              fY2_3D,
              fZ2_3D,
              fX_mid12,
              fY_mid12,
              fZ_mid12,
              iScreenX_mid12,
              iScreenX_subdiv1,
              iScreenCenterY,
              iScreenY1,
              iScreenX2,
              iScreenY2,
              iScreenX_mid23,
              iScreenY_mid12,
              iTexWid + iTexU,
              iTexV,
              iTexWid,
              iTexHgt);

            // Update coords for right half
            fX1_3D = fMidZ_Edge03 * 0.5f;
            fY1_3D = fMidZ_Edge01 * 0.5f;
            fZ1_3D = fMidZ_Edge23 * 0.5f;
            fX2_3D = (fX2_3D + fX3_3D) * 0.5f;
            fY2_3D = (fY2_3D + fY3_3D) * 0.5f;
            fZ2_3D = (fZ2_3D + fZ3_3D) * 0.5f;
          } else {
            // No flip - normal subdivision
            dodivide(
              fX_mid01,
              fMidZ_quad,
              fY_mid01,
              fX1_3D,
              fY1_3D,
              fZ1_3D,
              fX2_3D,
              fY2_3D,
              fZ2_3D,
              fX_mid12,
              fY_mid12,
              fZ_mid12,
              iScreenX_mid12,
              iScreenX_subdiv1,
              iScreenX1,
              iScreenY1,
              iScreenX2,
              iScreenY2,
              iScreenX_mid23,
              iScreenY_mid12,
              iTexU,
              iTexV,
              iTexWid,
              iTexHgt);

            // Update coords for right half
            fX1_3D = fMidZ_Edge03 * 0.5f;
            fY1_3D = fMidZ_Edge01 * 0.5f;
            fZ1_3D = fMidZ_Edge23 * 0.5f;
            fX2_3D = (fX2_3D + fX3_3D) * 0.5f;
            fY2_3D = (fY2_3D + fY3_3D) * 0.5f;
            iTexU += iTexWid;                   // Advance texture U coord
            fZ2_3D = (fZ2_3D + fZ3_3D) * 0.5f;
            iScreenX1 = iScreenX_mid12;
          }
          iScreenX2 = iScreenX_mid23;
          iScreenY1 = iScreenX_subdiv1;
          iScreenY2 = iScreenY_mid12;
          continue;
        case 2:                                 // Vertical flip
          // Calculate midpoint between verts 1 and 2
          fX_mid12_1 = (fX1_3D + fX2_3D) * 0.5f;
          fY_mid12_1 = (fY1_3D + fY2_3D) * 0.5f;
          fZ_mid12_1 = (fZ1_3D + fZ2_3D) * 0.5f;

          // Too close to camera
          if (fZ_mid12_1 < 1.0 || isnan(fZ_mid12_1)) {
            if ((double)iScreenCenterX_calc <= fX_mid12_1)
              iScreenX_mid12_1 = 20000;
            else
              iScreenX_mid12_1 = -20000;
            if ((double)(winh / 2) <= fY_mid12_1)
              iScreenY_mid12_1 = 20000;
            else
              iScreenY_mid12_1 = -20000;
          } else {
            dViewDist_2 = (double)VIEWDIST;
            dInvZ_2 = 1.0 / fZ_mid12_1;
            dScreenScale_2 = (double)scr_size;
            dProjX_2 = (dViewDist_2 * fX_mid12_1 * dInvZ_2 + (double)xbase) * dScreenScale_2 * 0.015625;
            dProjX_2 = round(dProjX_2);//_CHP();
            iScreenX_mid12_1 = (int)dProjX_2;
            dProjY_2 = dScreenScale_2 * (199.0 - dInvZ_2 * (dViewDist_2 * fY_mid12_1) - (double)ybase) * 0.015625;
            dProjY_2 = round(dProjY_2);//_CHP();
            iScreenY_mid12_1 = (int)dProjY_2;
          }

          // Update for bottom-right quad
          fX_mid30 = (fX3_3D + fX0_3D) * 0.5f;
          fY_mid30 = (fY3_3D + fY0_3D) * 0.5f;
          fZ_mid30 = (fZ3_3D + fZ0_3D) * 0.5f;
          if (fZ_mid30 < 1.0 || isnan(fZ_mid30)) {
            if ((double)(winw / 2) <= fX_mid30)
              iScreenX_mid30 = 20000;
            else
              iScreenX_mid30 = -20000;
            if ((double)(winh / 2) <= fY_mid30)
              iScreenY_mid30 = 20000;
            else
              iScreenY_mid30 = -20000;
          } else {
            dViewDist_3 = (double)VIEWDIST;
            dInvZ_3 = 1.0 / fZ_mid30;
            dScreenScale_3 = (double)scr_size;
            dProjX_3 = (dViewDist_3 * fX_mid30 * dInvZ_3 + (double)xbase) * dScreenScale_3 * 0.015625;
            dProjX_3 = round(dProjX_3);//_CHP();
            iScreenX_mid30 = (int)dProjX_3;
            dProjY_3 = dScreenScale_3 * (199.0 - dInvZ_3 * (dViewDist_3 * fY_mid30) - (double)ybase) * 0.015625;
            dProjY_3 = round(dProjY_3);//_CHP();
            iScreenY_mid30 = (int)dProjY_3;
          }

          // Half texture height for subdivision
          iTexHgt >>= 1;

          // Vertical flip
          if ((fliptype & 2) != 0) {
            // Recursively subdivide top half
            dodivide(
              fX0_3D,
              fY0_3D,
              fZ0_3D,
              fX1_3D,
              fY1_3D,
              fZ1_3D,
              fX_mid12_1,
              fY_mid12_1,
              fZ_mid12_1,
              fX_mid30,
              fY_mid30,
              fZ_mid30,
              iScreenX0,
              iScreenY0,
              iScreenX1,
              iScreenY1,
              iScreenX_mid12_1,
              iScreenY_mid12_1,
              iScreenX_mid30,
              iScreenY_mid30,
              iTexU,
              iTexHgt + iTexV,                  // bottom half of texture
              iTexWid,
              iTexHgt);

            // Update coords for bottom half
            fX0_3D = (fX3_3D + fX0_3D) * 0.5f;
            fY0_3D = (fY3_3D + fY0_3D) * 0.5f;
            fZ0_3D = (fZ3_3D + fZ0_3D) * 0.5f;
            fX1_3D = (fX1_3D + fX2_3D) * 0.5f;
            iScreenY1 = iScreenY_mid12_1;
            fY1_3D = (fY1_3D + fY2_3D) * 0.5f;
            iScreenX1 = iScreenX_mid12_1;
            fZ1_3D = (fZ1_3D + fZ2_3D) * 0.5f;
            iScreenY0 = iScreenY_mid30;
          } else {
            // No flip - normal subdivision
            // Recursively subdivide top half
            dodivide(
              fX0_3D,
              fY0_3D,
              fZ0_3D,
              fX1_3D,
              fY1_3D,
              fZ1_3D,
              fX_mid12_1,
              fY_mid12_1,
              fZ_mid12_1,
              fX_mid30,
              fY_mid30,
              fZ_mid30,
              iScreenX0,
              iScreenY0,
              iScreenX1,
              iScreenY1,
              iScreenX_mid12_1,
              iScreenY_mid12_1,
              iScreenX_mid30,
              iScreenY_mid30,
              iTexU,
              iTexV,                            // top half of tex
              iTexWid,
              iTexHgt);

            // Update coords for bottom half
            fX0_3D = (fX3_3D + fX0_3D) * 0.5f;
            fY0_3D = (fY3_3D + fY0_3D) * 0.5f;
            fZ0_3D = (fZ3_3D + fZ0_3D) * 0.5f;
            iTexV += iTexHgt;                   // Advance texture V coord to bottom half
            fX1_3D = (fX1_3D + fX2_3D) * 0.5f;
            iScreenY1 = iScreenY_mid12_1;
            fY1_3D = (fY1_3D + fY2_3D) * 0.5f;
            iScreenY0 = iScreenY_mid30;
            fZ1_3D = (fZ1_3D + fZ2_3D) * 0.5f;
            iScreenX1 = iScreenX_mid12_1;
          }
          iScreenX0 = iScreenX_mid30;
          continue;                             // Process bottom half in next iteration
        case 3:                                 // Both horiz and vert subdivision
          // Calculate all edge midpoints

          // midpoint of edge 0-1 (top edge)
          if (fY_mid01 < 1.0 || isnan(fY_mid01)) {
            if ((double)iScreenCenterX_calc <= fX_mid01)
              iScreenX_mid01 = 20000;
            else
              iScreenX_mid01 = -20000;
            if ((double)(winh / 2) <= fMidZ_quad)
              iScreenY_mid01 = 20000;
            else
              iScreenY_mid01 = -20000;
          } else {
            dViewDist_4 = (double)VIEWDIST;
            dInvZ_4 = 1.0 / fY_mid01;
            dScreenScale_4 = (double)scr_size;
            dProjX_4 = (dViewDist_4 * fX_mid01 * dInvZ_4 + (double)xbase) * dScreenScale_4 * 0.015625;
            dProjX_4 = round(dProjX_4);//_CHP();
            iScreenX_mid01 = (int)dProjX_4;
            dProjY_4 = dScreenScale_4 * (199.0 - dInvZ_4 * (dViewDist_4 * fMidZ_quad) - (double)ybase) * 0.015625;
            dProjY_4 = round(dProjY_4);//_CHP();
            iScreenY_mid01 = (int)dProjY_4;
          }

          // Midpoint of edge 1-2 (right edge)
          fX_mid12_2 = (fX1_3D + fX2_3D) * 0.5f;
          fY_mid12_2 = (fY1_3D + fY2_3D) * 0.5f;
          fZ_mid12_2 = (fZ1_3D + fZ2_3D) * 0.5f;
          if (fZ_mid12_2 < 1.0 || isnan(fZ_mid12_2)) {
            if ((double)(winw / 2) <= fX_mid12_2)
              iScreenX_mid12_2 = 20000;
            else
              iScreenX_mid12_2 = -20000;
            if ((double)(winh / 2) <= fY_mid12_2)
              iScreenY_mid23 = 20000;
            else
              iScreenY_mid23 = -20000;
          } else {
            dViewDist_5 = (double)VIEWDIST;
            dInvZ_5 = 1.0 / fZ_mid12_2;
            dScreenScale_5 = (double)scr_size;
            dProjX_5 = (dViewDist_5 * fX_mid12_2 * dInvZ_5 + (double)xbase) * dScreenScale_5 * 0.015625;
            dProjX_5 = round(dProjX_5);//_CHP();
            iScreenX_mid12_2 = (int)dProjX_5;
            dProjY_5 = dScreenScale_5 * (199.0 - dInvZ_5 * (dViewDist_5 * fY_mid12_2) - (double)ybase) * 0.015625;
            dProjY_5 = round(dProjY_5);//_CHP();
            iScreenY_mid23 = (int)dProjY_5;
          }

          // Midpoint fo edge 2-3 (bottom edge)
          fX_mid23 = (fX2_3D + fX3_3D) * 0.5f;
          fY_mid23 = (fY2_3D + fY3_3D) * 0.5f;
          fZ_mid23 = (fZ2_3D + fZ3_3D) * 0.5f;
          if (fZ_mid23 < 1.0 || isnan(fZ_mid23)) {
            if ((double)(winw / 2) <= fX_mid23)
              iScreenX_mid23_1 = 20000;
            else
              iScreenX_mid23_1 = -20000;
            if ((double)(winh / 2) <= fY_mid23)
              iScreenY_mid30_1 = 20000;
            else
              iScreenY_mid30_1 = -20000;
          } else {
            dViewDist_6 = (double)VIEWDIST;
            dInvZ_6 = 1.0 / fZ_mid23;
            dScreenScale_6 = (double)scr_size;
            dProjX_6 = (dViewDist_6 * fX_mid23 * dInvZ_6 + (double)xbase) * dScreenScale_6 * 0.015625;
            dProjX_6 = round(dProjX_6);//_CHP();
            iScreenX_mid23_1 = (int)dProjX_6;
            dProjY_6 = dScreenScale_6 * (199.0 - dInvZ_6 * (dViewDist_6 * fY_mid23) - (double)ybase) * 0.015625;
            dProjY_6 = round(dProjY_6);//_CHP();
            iScreenY_mid30_1 = (int)dProjY_6;
          }

          // Midpoint of edge 3-0 (left edge)
          fX_mid30_1 = (fX3_3D + fX0_3D) * 0.5f;
          fY_mid30_1 = (fY3_3D + fY0_3D) * 0.5f;
          fZ_mid30_1 = (fZ3_3D + fZ0_3D) * 0.5f;
          if (fZ_mid30_1 < 1.0 || isnan(fZ_mid30_1)) {
            if ((double)(winw / 2) <= fX_mid30_1)
              iScreenX_mid30_1 = 20000;
            else
              iScreenX_mid30_1 = -20000;
            if ((double)(winh / 2) <= fY_mid30_1)
              iScreenY_mid30_2 = 20000;
            else
              iScreenY_mid30_2 = -20000;
          } else {
            dViewDist_7 = (double)VIEWDIST;
            dInvZ_7 = 1.0 / fZ_mid30_1;
            dScreenScale_7 = (double)scr_size;
            dProjX_7 = (dViewDist_7 * fX_mid30_1 * dInvZ_7 + (double)xbase) * dScreenScale_7 * 0.015625;
            dProjX_7= round(dProjX_7);//_CHP();
            iScreenX_mid30_1 = (int)dProjX_7;
            dProjY_7 = dScreenScale_7 * (199.0 - dInvZ_7 * (dViewDist_7 * fY_mid30_1) - (double)ybase) * 0.015625;
            dProjY_7 = round(dProjY_7);//_CHP();
            iScreenY_mid30_2 = (int)dProjY_7;
          }

          // Calculate center point of quad
          fX_center = (fX_mid01 + fX_mid23) * 0.5f;
          fY_center = (fMidZ_quad + fY_mid23) * 0.5f;
          fZ_center = (fY_mid01 + fZ_mid23) * 0.5f;
          if (fZ_center < 1.0 || isnan(fZ_center)) {
            if ((double)(winw / 2) <= fX_center)
              iScreenX_center = 20000;
            else
              iScreenX_center = -20000;
            if ((double)(winh / 2) <= fY_center)
              iScreenY_center = 20000;
            else
              iScreenY_center = -20000;
          } else {
            dViewDist_8 = (double)VIEWDIST;
            dInvZ_8 = 1.0 / fZ_center;
            dScreenScale_8 = (double)scr_size;
            dProjX_8 = (dViewDist_8 * fX_center * dInvZ_8 + (double)xbase) * dScreenScale_8 * 0.015625;
            dProjX_8 = round(dProjX_8);//_CHP();
            iScreenX_center = (int)dProjX_8;
            dProjY_8 = dScreenScale_8 * (199.0 - dInvZ_8 * (dViewDist_8 * fY_center) - (double)ybase) * 0.015625;
            dProjY_8 = round(dProjY_8);//_CHP();
            iScreenY_center = (int)dProjY_8;
          }

          // Quarter tex dimensions
          iTexWid >>= 1;
          iTexHgt >>= 1;
          iTexV_bottom = iTexHgt + iTexV;
          iTexU_right = iTexWid + iTexU;

          // Handle all 4 flip combinations
          switch (fliptype) {
            case 0:                             // no flip
              // Top-left quadrant
              dodivide(
                fX_mid01,
                fMidZ_quad,
                fY_mid01,
                fX1_3D,
                fY1_3D,
                fZ1_3D,
                fX_mid12_2,
                fY_mid12_2,
                fZ_mid12_2,
                fX_center,
                fY_center,
                fZ_center,
                iScreenX_mid01,
                iScreenY_mid01,
                iScreenX1,
                iScreenY1,
                iScreenX_mid12_2,
                iScreenY_mid23,
                iScreenX_center,
                iScreenY_center,
                iTexU,
                iTexV,
                iTexWid,
                iTexHgt);

              // Top-right quadrant
              dodivide(
                fX0_3D,
                fY0_3D,
                fZ0_3D,
                fX_mid01,
                fMidZ_quad,
                fY_mid01,
                fX_center,
                fY_center,
                fZ_center,
                fX_mid30_1,
                fY_mid30_1,
                fZ_mid30_1,
                iScreenX0,
                iScreenY0,
                iScreenX_mid01,
                iScreenY_mid01,
                iScreenX_center,
                iScreenY_center,
                iScreenX_mid30_1,
                iScreenY_mid30_2,
                iTexU_right,
                iTexV,
                iTexWid,
                iTexHgt);

              iScreenY1 = iScreenY_center;

              // bottom-right quadrant
              dodivide(
                fX_center,
                fY_center,
                fZ_center,
                fX_mid12_2,
                fY_mid12_2,
                fZ_mid12_2,
                fX2_3D,
                fY2_3D,
                fZ2_3D,
                fX_mid23,
                fY_mid23,
                fZ_mid23,
                iScreenX_center,
                iScreenY_center,
                iScreenX_mid12_2,
                iScreenY_mid23,
                iScreenX2,
                iScreenY2,
                iScreenX_mid23_1,
                iScreenY_mid30_1,
                iTexU,
                iTexV_bottom,
                iTexWid,
                iTexHgt);

              // Update for bottom-left quadrant (processed in next iteration)
              fX0_3D = (fX3_3D + fX0_3D) * 0.5f;
              fY0_3D = (fY3_3D + fY0_3D) * 0.5f;
              fZ0_3D = (fZ3_3D + fZ0_3D) * 0.5f;
              fX1_3D = (fX_mid01 + fX_mid23) * 0.5f;
              fY1_3D = (fMidZ_quad + fY_mid23) * 0.5f;
              fZ1_3D = (fY_mid01 + fZ_mid23) * 0.5f;
              fX2_3D = (fX2_3D + fX3_3D) * 0.5f;
              fY2_3D = (fY2_3D + fY3_3D) * 0.5f;
              fZ2_3D = (fZ2_3D + fZ3_3D) * 0.5f;
              iScreenX0 = iScreenX_mid30_1;
              iTexU += iTexWid;
              iScreenX2 = iScreenX_mid23_1;
              iScreenX1 = iScreenX_center;
              iScreenY2 = iScreenY_mid30_1;
              iScreenY0 = iScreenY_mid30_2;
              iTexV += iTexHgt;
              continue;
            case 1:                             // horiz flip
              // top-right quadrant (flipped to top-left)
              dodivide(
                fX_mid01,
                fMidZ_quad,
                fY_mid01,
                fX1_3D,
                fY1_3D,
                fZ1_3D,
                fX_mid12_2,
                fY_mid12_2,
                fZ_mid12_2,
                fX_center,
                fY_center,
                fZ_center,
                iScreenX_mid01,
                iScreenY_mid01,
                iScreenX1,
                iScreenY1,
                iScreenX_mid12_2,
                iScreenY_mid23,
                iScreenX_center,
                iScreenY_center,
                iTexWid + iTexU,                // right side tex
                iTexV,
                iTexWid,
                iTexHgt);

              // Top-left quadrant (flipped to top-right)
              dodivide(
                fX0_3D,
                fY0_3D,
                fZ0_3D,
                fX_mid01,
                fMidZ_quad,
                fY_mid01,
                fX_center,
                fY_center,
                fZ_center,
                fX_mid30_1,
                fY_mid30_1,
                fZ_mid30_1,
                iScreenX0,
                iScreenY0,
                iScreenX_mid01,
                iScreenY_mid01,
                iScreenX_center,
                iScreenY_center,
                iScreenX_mid30_1,
                iScreenY_mid30_2,
                iTexU,                          // left side tex
                iTexV,
                iTexWid,
                iTexHgt);
              iScreenX1 = iScreenX_center;

              // bottom-right quadrant (flipped to bottom-left)
              dodivide(
                fX_center,
                fY_center,
                fZ_center,
                fX_mid12_2,
                fY_mid12_2,
                fZ_mid12_2,
                fX2_3D,
                fY2_3D,
                fZ2_3D,
                fX_mid23,
                fY_mid23,
                fZ_mid23,
                iScreenX_center,
                iScreenY_center,
                iScreenX_mid12_2,
                iScreenY_mid23,
                iScreenX2,
                iScreenY2,
                iScreenX_mid23_1,
                iScreenY_mid30_1,
                iTexU_right,
                iTexV_bottom,
                iTexWid,
                iTexHgt);

              // Update for bottom-left quadrant (flipped to bottom-right)
              fX0_3D = (fX3_3D + fX0_3D) * 0.5f;
              fY0_3D = (fY3_3D + fY0_3D) * 0.5f;
              fZ0_3D = (fZ3_3D + fZ0_3D) * 0.5f;
              fX1_3D = (fX_mid01 + fX_mid23) * 0.5f;
              fY1_3D = (fMidZ_quad + fY_mid23) * 0.5f;
              fZ1_3D = (fY_mid01 + fZ_mid23) * 0.5f;
              fX2_3D = (fX2_3D + fX3_3D) * 0.5f;
              fY2_3D = (fY2_3D + fY3_3D) * 0.5f;
              fZ2_3D = (fZ2_3D + fZ3_3D) * 0.5f;
              iScreenX0 = iScreenX_mid30_1;
              iScreenX2 = iScreenX_mid23_1;
              iScreenY1 = iScreenY_center;
              iScreenY2 = iScreenY_mid30_1;
              iScreenY0 = iScreenY_mid30_2;
              iTexV += iTexHgt;
              continue;
            case 2:                             // vertical flip
              // Top-left quadrant (flipped to bottom-left)
              dodivide(
                fX_mid01,
                fMidZ_quad,
                fY_mid01,
                fX1_3D,
                fY1_3D,
                fZ1_3D,
                fX_mid12_2,
                fY_mid12_2,
                fZ_mid12_2,
                fX_center,
                fY_center,
                fZ_center,
                iScreenX_mid01,
                iScreenY_mid01,
                iScreenX1,
                iScreenY1,
                iScreenX_mid12_2,
                iScreenY_mid23,
                iScreenX_center,
                iScreenY_center,
                iTexU,
                iTexV_bottom,                   // bottom tex
                iTexWid,
                iTexHgt);

              // Top-right quadrant (flipped to bottom-right)
              dodivide(
                fX0_3D,
                fY0_3D,
                fZ0_3D,
                fX_mid01,
                fMidZ_quad,
                fY_mid01,
                fX_center,
                fY_center,
                fZ_center,
                fX_mid30_1,
                fY_mid30_1,
                fZ_mid30_1,
                iScreenX0,
                iScreenY0,
                iScreenX_mid01,
                iScreenY_mid01,
                iScreenX_center,
                iScreenY_center,
                iScreenX_mid30_1,
                iScreenY_mid30_2,
                iTexU_right,
                iTexV_bottom,                   // bottom-right tex
                iTexWid,
                iTexHgt);

              iScreenY1 = iScreenY_center;

              // bottom-right quadrant (flipped to top-right)
              dodivide(
                fX_center,
                fY_center,
                fZ_center,
                fX_mid12_2,
                fY_mid12_2,
                fZ_mid12_2,
                fX2_3D,
                fY2_3D,
                fZ2_3D,
                fX_mid23,
                fY_mid23,
                fZ_mid23,
                iScreenX_center,
                iScreenY_center,
                iScreenX_mid12_2,
                iScreenY_mid23,
                iScreenX2,
                iScreenY2,
                iScreenX_mid23_1,
                iScreenY_mid30_1,
                iTexU,
                iTexV,                          // top tex
                iTexWid,
                iTexHgt);

              // Update for bottom-left quadrant (flipped to top-left)
              fX0_3D = (fX3_3D + fX0_3D) * 0.5f;
              fY0_3D = (fY3_3D + fY0_3D) * 0.5f;
              fZ0_3D = (fZ3_3D + fZ0_3D) * 0.5f;
              fX1_3D = (fX_mid01 + fX_mid23) * 0.5f;
              fY1_3D = (fMidZ_quad + fY_mid23) * 0.5f;
              fZ1_3D = (fY_mid01 + fZ_mid23) * 0.5f;
              fX2_3D = (fX2_3D + fX3_3D) * 0.5f;
              fY2_3D = (fY2_3D + fY3_3D) * 0.5f;
              fZ2_3D = (fZ2_3D + fZ3_3D) * 0.5f;
              iTexU += iTexWid;
              iScreenX0 = iScreenX_mid30_1;
              iScreenX1 = iScreenX_center;
              iScreenX2 = iScreenX_mid23_1;
              iScreenY0 = iScreenY_mid30_2;
              iScreenY2 = iScreenY_mid30_1;
              continue;
            case 3:                             // both horiz and vert flip
              // top-right quadrant (flipped to bottom-left)
              dodivide(
                fX_mid01,
                fMidZ_quad,
                fY_mid01,
                fX1_3D,
                fY1_3D,
                fZ1_3D,
                fX_mid12_2,
                fY_mid12_2,
                fZ_mid12_2,
                fX_center,
                fY_center,
                fZ_center,
                iScreenX_mid01,
                iScreenY_mid01,
                iScreenX1,
                iScreenY1,
                iScreenX_mid12_2,
                iScreenY_mid23,
                iScreenX_center,
                iScreenY_center,
                iTexWid + iTexU,                // right tex
                iTexV_bottom,                   // bottom tex
                iTexWid,
                iTexHgt);

              // top-left quadrant (flipped to bottom-right)
              dodivide(
                fX0_3D,
                fY0_3D,
                fZ0_3D,
                fX_mid01,
                fMidZ_quad,
                fY_mid01,
                fX_center,
                fY_center,
                fZ_center,
                fX_mid30_1,
                fY_mid30_1,
                fZ_mid30_1,
                iScreenX0,
                iScreenY0,
                iScreenX_mid01,
                iScreenY_mid01,
                iScreenX_center,
                iScreenY_center,
                iScreenX_mid30_1,
                iScreenY_mid30_2,
                iTexU,                          // left tex
                iTexV_bottom,                   // bottom tex
                iTexWid,
                iTexHgt);

              iScreenX1 = iScreenX_center;

              // bottom-right quadrant (flipped to top-left)
              dodivide(
                fX_center,
                fY_center,
                fZ_center,
                fX_mid12_2,
                fY_mid12_2,
                fZ_mid12_2,
                fX2_3D,
                fY2_3D,
                fZ2_3D,
                fX_mid23,
                fY_mid23,
                fZ_mid23,
                iScreenX_center,
                iScreenY_center,
                iScreenX_mid12_2,
                iScreenY_mid23,
                iScreenX2,
                iScreenY2,
                iScreenX_mid23_1,
                iScreenY_mid30_1,
                iTexU_right,
                iTexV,                          // top tex
                iTexWid,
                iTexHgt);

              // update for bottom-left quadrant (flipped to top-right)
              fX0_3D = (fX3_3D + fX0_3D) * 0.5f;
              fY0_3D = (fY3_3D + fY0_3D) * 0.5f;
              fZ0_3D = (fZ3_3D + fZ0_3D) * 0.5f;
              fX1_3D = (fX_mid01 + fX_mid23) * 0.5f;
              fY1_3D = (fMidZ_quad + fY_mid23) * 0.5f;
              fZ1_3D = (fY_mid01 + fZ_mid23) * 0.5f;
              fX2_3D = (fX2_3D + fX3_3D) * 0.5f;
              fY2_3D = (fY2_3D + fY3_3D) * 0.5f;
              fZ2_3D = (fZ2_3D + fZ3_3D) * 0.5f;
              iScreenX0 = iScreenX_mid30_1;
              iScreenY1 = iScreenY_center;
              iScreenX2 = iScreenX_mid23_1;
              iScreenY0 = iScreenY_mid30_2;
              iScreenY2 = iScreenY_mid30_1;
              continue;
            default:
              return;
          }
          return;
        default:
          return;
      }
    }
    break;
  }
}


typedef struct {
    uint8 *pixels;
    int width;
    int height;
    int tex_idx;
    int texHalfRes;
    int in_use;
} SceneTextureSlot;

struct SceneRendererSoftware {
    SceneRenderCamera camera;
    SceneRenderProjection proj;

    uint8 *targetBuffer;
    int targetStride;
    int targetWidth;
    int targetHeight;
    int viewportX;
    int viewportY;
    int viewportW;
    int viewportH;

    SceneTextureSlot texSlots[SCENE_RENDER_MAX_TEXTURE_SLOTS];
    SceneTextureHandle texIdxToHandle[32];
};

static void subdivide(uint8 *pDest, tPolyParams *polyParams,
                      float fX0_3D, float fY0_3D, float fZ0_3D,
                      float fX1_3D, float fY1_3D, float fZ1_3D,
                      float fX2_3D, float fY2_3D, float fZ2_3D,
                      float fX3_3D, float fY3_3D, float fZ3_3D,
                      int iSubpolyType,
                      int bHalfResTex)
{
  int iX0; // ebp
  int iX1; // edi
  int iX3; // esi
  int iTexHgt; // ebx
  int iY3; // [esp+0h] [ebp-28h]
  int iY0; // [esp+4h] [ebp-24h]
  int iY1; // [esp+8h] [ebp-20h]
  int iY2; // [esp+Ch] [ebp-1Ch]
  int iX2; // [esp+10h] [ebp-18h]

  if ((polyParams->iSurfaceType & SURFACE_FLAG_SKIP_RENDER) != 0)// SURFACE_FLAG_SKIP_RENDER
    return;

  // setup globals for dodivide
  subptr = pDest;
  subpoly = polyParams;

  // Extract screen coords from pol verts
  iX0 = polyParams->vertices[0].x;
  iX1 = polyParams->vertices[1].x;
  iY0 = polyParams->vertices[0].y;
  iY3 = polyParams->vertices[3].y;
  iY1 = polyParams->vertices[1].y;
  subpolytype = iSubpolyType;
  iX2 = polyParams->vertices[2].x;
  iY2 = polyParams->vertices[2].y;
  iX3 = polyParams->vertices[3].x;

  // determine tex flipping mode
  fliptype = (polyParams->iSurfaceType & SURFACE_FLAG_FLIP_HORIZ) != 0;// SURFACE_FLAG_FLIP_HORIZ
  if ((polyParams->iSurfaceType & SURFACE_FLAG_FLIP_VERT) != 0)// SURFACE_FLAG_FLIP_VERT
    fliptype += 2;                              // 0=none, 1=horiz, 2=vert, 3=both

  // set flat pol flag if SURFACE_FLAG_APPLY_TEXTURE is not set
  // This disables screen-size based subdivision for untextured pol
  flatpol = ((subpoly->iSurfaceType & SURFACE_FLAG_APPLY_TEXTURE) != 0) - 1;// SURFACE_FLAG_APPLY_TEXTURE

  // Determine tex dimensions based on pol type
  if (subpolytype >= 0) {
    // Standard pol type
    iTexHgt = 1024;
    tex_wid = 1024;
    goto LABEL_9;
  }
  if (subpolytype != -1) {
    // wide tex
    iTexHgt = 2048;
    tex_wid = 1024;
  LABEL_9:
    tex_hgt = iTexHgt;
    goto LABEL_10;
  }
  // type -1: tall tex
  tex_wid = 2048;
  tex_hgt = 1024;
LABEL_10:
  // Apply half-res tex mode if requested
  if (bHalfResTex) {
    tex_wid >>= 1;
    tex_hgt >>= 1;
  }
  dodivide(
    fX0_3D,
    fY0_3D,
    fZ0_3D,
    fX1_3D,
    fY1_3D,
    fZ1_3D,
    fX2_3D,
    fY2_3D,
    fZ2_3D,
    fX3_3D,
    fY3_3D,
    fZ3_3D,
    iX0,
    iY0,
    iX1,
    iY1,
    iX2,
    iY2,
    iX3,
    iY3,
    0,
    0,
    tex_wid,
    tex_hgt);

  // Reset tex coords to default values
  // Clean slate for next pol
  set_starts(0);

  // Debug mode: draw pol outline if showsub flag is enabled
  if (showsub) {
    // Edge 0-1 (top)
    if (fZ0_3D >= 1.0 && fZ1_3D >= 1.0)
      compout(subptr, iX0, iY0, iX1, iY1, 0xF3u);// 0xF3 is blue in PALETTE.PAL
    // Edge 1-2 (right)
    if (fZ1_3D >= 1.0 && fZ2_3D >= 1.0)
      compout(subptr, iX1, iY1, iX2, iY2, 0xF3u);
    // Edge 2-3 (bottom)
    if (fZ2_3D >= 1.0 && fZ3_3D >= 1.0)
      compout(subptr, iX2, iY2, iX3, iY3, 0xF3u);
    // Edge 3-0 (left)
    if (fZ3_3D >= 1.0 && fZ0_3D >= 1.0)
      compout(subptr, iX3, iY3, iX0, iY0, 0xF3u);
  }
}


static void scene_render_sw_bind_target(SceneRendererSoftware *sw) {
    if (!sw || !sw->targetBuffer)
        return;
    screen_pointer = sw->targetBuffer + sw->viewportY * sw->targetStride + sw->viewportX;
}

SceneRendererSoftware *scene_render_sw_create(SDL_GPUDevice *device,
                                              SDL_Window *window) {
    (void)device;
    (void)window;
    SceneRendererSoftware *sw = calloc(1, sizeof(SceneRendererSoftware));
    return sw;
}

void scene_render_sw_destroy(SceneRendererSoftware *sw) {
    free(sw);
}

void scene_render_sw_set_target(SceneRendererSoftware *sw, uint8 *buffer,
                                int stride, int width, int height) {
    if (!sw)
        return;
    sw->targetBuffer = buffer;
    sw->targetStride = stride;
    sw->targetWidth = width;
    sw->targetHeight = height;
    if (sw->viewportW == 0 && sw->viewportH == 0) {
        sw->viewportX = 0;
        sw->viewportY = 0;
        sw->viewportW = width;
        sw->viewportH = height;
    }
    scene_render_sw_bind_target(sw);
}

void scene_render_sw_set_viewport(SceneRendererSoftware *sw,
                                  int x, int y, int w, int h) {
    if (!sw)
        return;
    sw->viewportX = x;
    sw->viewportY = y;
    sw->viewportW = w;
    sw->viewportH = h;
    scene_render_sw_bind_target(sw);
}

void scene_render_sw_set_camera(SceneRendererSoftware *sw,
                                const SceneRenderCamera *camera) {
    extern float viewx, viewy, viewz;
    extern float fcos, fsin;
    extern int VIEWDIST;
    if (!sw || !camera)
        return;
    sw->camera = *camera;
    viewx = camera->viewX;
    viewy = camera->viewY;
    viewz = camera->viewZ;
    fcos = camera->cosYaw;
    fsin = camera->sinYaw;
    VIEWDIST = (int)camera->fovScale;
}

void scene_render_sw_set_projection(SceneRendererSoftware *sw,
                                    const SceneRenderProjection *proj) {
    extern float vk1, vk2, vk3, vk4, vk5, vk6, vk7, vk8, vk9;
    extern int scr_size, xbase, ybase, gfx_size;
    if (!sw || !proj)
        return;
    sw->proj = *proj;
    // Write through to globals for legacy code (subdivide, POLYTEX, etc.)
    vk1 = proj->view[0][0]; vk2 = proj->view[0][1]; vk3 = proj->view[0][2];
    vk4 = proj->view[1][0]; vk5 = proj->view[1][1]; vk6 = proj->view[1][2];
    vk7 = proj->view[2][0]; vk8 = proj->view[2][1]; vk9 = proj->view[2][2];
    scr_size = proj->screenScale;
    xbase = proj->centerX;
    ybase = proj->centerY;
    gfx_size = proj->texHalfRes;
}

SceneTextureHandle scene_render_sw_load_texture(SceneRendererSoftware *sw,
                                                uint8 *pixelData,
                                                int width, int height,
                                                int tex_idx, int texHalfRes) {
    if (!sw)
        return SCENE_TEXTURE_HANDLE_INVALID;

    if (tex_idx >= 0 && tex_idx < 32) {
        SceneTextureHandle old = sw->texIdxToHandle[tex_idx];
        if (old != SCENE_TEXTURE_HANDLE_INVALID)
            scene_render_sw_free_texture(sw, old);
    }

    for (int i = 1; i < SCENE_RENDER_MAX_TEXTURE_SLOTS; i++) {
        if (!sw->texSlots[i].in_use) {
            sw->texSlots[i].pixels = pixelData;
            sw->texSlots[i].width = width;
            sw->texSlots[i].height = height;
            sw->texSlots[i].tex_idx = tex_idx;
            sw->texSlots[i].texHalfRes = texHalfRes;
            sw->texSlots[i].in_use = 1;
            if (tex_idx >= 0 && tex_idx < 32)
                sw->texIdxToHandle[tex_idx] = i;
            return (SceneTextureHandle)i;
        }
    }
    return SCENE_TEXTURE_HANDLE_INVALID;
}

void scene_render_sw_free_texture(SceneRendererSoftware *sw,
                                  SceneTextureHandle handle) {
    if (!sw || handle <= 0 || handle >= SCENE_RENDER_MAX_TEXTURE_SLOTS)
        return;
    int tex_idx = sw->texSlots[handle].tex_idx;
    if (tex_idx >= 0 && tex_idx < 32 && sw->texIdxToHandle[tex_idx] == handle)
        sw->texIdxToHandle[tex_idx] = SCENE_TEXTURE_HANDLE_INVALID;
    memset(&sw->texSlots[handle], 0, sizeof(SceneTextureSlot));
}

SceneTextureHandle scene_render_sw_get_texture_handle(SceneRendererSoftware *sw,
                                                      int tex_idx) {
    if (!sw)
        return SCENE_TEXTURE_HANDLE_INVALID;
    if (tex_idx >= 0 && tex_idx < 32)
        return sw->texIdxToHandle[tex_idx];
    return SCENE_TEXTURE_HANDLE_INVALID;
}

void scene_render_sw_quad_world_legacy(SceneRendererSoftware *sw,
                                       const SceneRenderVertex *verts,
                                       SceneTextureHandle handle,
                                       int surfaceFlags,
                                       SceneRenderLegacyQuadOptions options) {
    if (!sw || !verts || !sw->targetBuffer)
        return;

    scene_render_sw_bind_target(sw);

    const SceneRenderCamera *cam = &sw->camera;
    const SceneRenderProjection *proj = &sw->proj;

    int useCloudProjection = options.subdivideType == SCENE_RENDER_SUBDIVIDE_TYPE_CLOUD;
    int subpolyType;
    if (useCloudProjection) {
        subpolyType = SUBPOLY_STANDARD;
    } else if (options.subdivideType != SCENE_RENDER_SUBDIVIDE_TYPE_AUTO) {
        subpolyType = options.subdivideType;
    } else if (handle > 0 && handle < SCENE_RENDER_MAX_TEXTURE_SLOTS
        && sw->texSlots[handle].in_use
        && sw->texSlots[handle].tex_idx == TEXTURE_BANK_BUILDING) {
        subpolyType = SUBPOLY_BUILDING;
    } else if ((surfaceFlags & SURFACE_FLAG_TEXTURE_PAIR) && wide_on) {
        subpolyType = SUBPOLY_WALL;
    } else {
        subpolyType = SUBPOLY_STANDARD;
    }

    tPolyParams poly;
    poly.iSurfaceType = surfaceFlags;
    poly.uiNumVerts = 4;

    float subVx[4], subVy[4], subVz[4];

    if (subpolyType == SUBPOLY_BUILDING) {
        int iVx[4], iVy[4], iVz[4];
        int clippedCount = 0;
        for (int i = 0; i < 4; i++) {
            double dx = floor((double)verts[i].x - cam->viewX);
            double dy = floor((double)verts[i].y - cam->viewY);
            double dz = floor((double)verts[i].z - cam->viewZ);
            iVx[i] = (int)(dx * proj->view[0][0] + dy * proj->view[1][0] + dz * proj->view[2][0]);
            iVy[i] = (int)(dx * proj->view[0][1] + dy * proj->view[1][1] + dz * proj->view[2][1]);
            iVz[i] = (int)(dx * proj->view[0][2] + dy * proj->view[1][2] + dz * proj->view[2][2]);
        }
        int viewDist = (int)cam->fovScale;
        for (int i = 0; i < 4; i++) {
            if (iVz[i] < 80) {
                iVz[i] = 80;
                clippedCount++;
            }
            int xp = iVx[i] * viewDist / iVz[i] + proj->centerX;
            int yp = iVy[i] * viewDist / iVz[i] + proj->centerY;
            poly.vertices[i].x = (proj->screenScale * xp) >> 6;
            poly.vertices[i].y = (proj->screenScale * (199 - yp)) >> 6;
            subVx[i] = (float)iVx[i];
            subVy[i] = (float)iVy[i];
            subVz[i] = (float)iVz[i];
        }
        if (clippedCount >= 4)
            return;
    } else {
        int useCarProjection = subpolyType >= 3;
        double viewDist = (double)cam->fovScale;
        for (int i = 0; i < 4; i++) {
            double dx = (double)(verts[i].x - cam->viewX);
            double dy = (double)(verts[i].y - cam->viewY);
            double dz = (double)(verts[i].z - cam->viewZ);
            float fVx = (float)(dx * proj->view[0][0] + dy * proj->view[1][0] + dz * proj->view[2][0]);
            float fVy = (float)(dx * proj->view[0][1] + dy * proj->view[1][1] + dz * proj->view[2][1]);
            double dCameraZ = dx * proj->view[0][2] + dy * proj->view[1][2] + dz * proj->view[2][2];
            float fVz = (float)dCameraZ;
            float fProjectedZ = fVz;
            if (fProjectedZ < 80.0f) fProjectedZ = 80.0f;
            double dInvZ = 1.0 / (double)fProjectedZ;
            int xp;
            int yp;
            if (useCarProjection || useCloudProjection) {
                xp = (int)(viewDist * (double)fVx * dInvZ + (double)proj->centerX);
                yp = (int)(dInvZ * (viewDist * (double)fVy) + (double)proj->centerY);
            } else {
                xp = (int)round(viewDist * (double)fVx * dInvZ + (double)proj->centerX);
                yp = (int)round(dInvZ * (viewDist * (double)fVy) + (double)proj->centerY);
            }
            poly.vertices[i].x = (xp * proj->screenScale) >> 6;
            poly.vertices[i].y = (proj->screenScale * (199 - yp)) >> 6;
            subVx[i] = fVx;
            subVy[i] = fVy;
            subVz[i] = (useCarProjection || useCloudProjection) ? fVz : (float)((int)round(dCameraZ));
        }
    }

    if (subpolyType == SUBPOLY_WALL) set_starts(1u);

    int useDirect = 0;
    if (options.subThreshold > 0.0f) {
        float minZ = subVz[0];
        if (subVz[1] < minZ) minZ = subVz[1];
        if (subVz[2] < minZ) minZ = subVz[2];
        if (subVz[3] < minZ) minZ = subVz[3];
        if (options.subThreshold <= minZ)
            useDirect = 1;
    }

    if (useDirect && handle == SCENE_TEXTURE_HANDLE_INVALID) {
        POLYFLAT(screen_pointer, &poly);
    } else if (useDirect) {
        SceneTextureSlot *slot = &sw->texSlots[handle];
        POLYTEX(slot->pixels, screen_pointer, &poly,
                slot->tex_idx, slot->texHalfRes);
    } else {
        subdivide(screen_pointer, &poly,
                  subVx[0], subVy[0], subVz[0],
                  subVx[1], subVy[1], subVz[1],
                  subVx[2], subVy[2], subVz[2],
                  subVx[3], subVy[3], subVz[3],
                  subpolyType, proj->texHalfRes);
    }

    if (subpolyType == SUBPOLY_WALL) set_starts(0);
}
