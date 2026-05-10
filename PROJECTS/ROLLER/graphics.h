#ifndef _ROLLER_GRAPHICS_H
#define _ROLLER_GRAPHICS_H
//-------------------------------------------------------------------------------------------------
#include "types.h"
//-------------------------------------------------------------------------------------------------

extern char revs_files1[6][13];
extern char revs_files2[6][13];
extern char texture_file[13];
extern char bldtex_file[13];
extern char gencartex_name[11];
extern int car_remap[4096];
extern int cargen_remap[256];
extern int bld_remap[256];
extern int num_textures[32];
extern int remap_tex[256];
extern int mode_c[256];
extern int gfx_size;
extern int BldTextures;
extern int NoOfTextures;

//-------------------------------------------------------------------------------------------------

void plotxyz(float fWorldX, float fWorldY, float fWorldZ, char byColor);
void linexyz(uint8 *pScrBuf, float fX1, float fY1, float fZ1, float fX2,
             float fY2, float fZ2, uint8 byColor);
void compout(uint8 *pScrBuf, int iX0, int iY0, int iX1, int iY1, uint8 byColor);
void line(uint8 *pScrBuf, int iX0, int iY0, int iX1, int iY1, uint8 byColor);
void LoadPanel();
void InitRemaps();
void LoadGenericCarTextures();
void LoadCarTexture(int iCartexIdx, uint8 byTexSlotIdx);
void LoadBldTextures();
void LoadTextures();
void init_remap(uint8 *pTextureBaseAddr, int iRemapType, int iNumBlocks,
                int iIsLowRes);
void sort_small_texture(uint8 *pDest, uint8 *pSrc, int iNumBlocks);
void sort_texture(uint8 *pTexData, int iNumTextures);
void sort_mini_texture(uint8 *pTexData, int iNumTextures);
void drbox(int iX, int iY, int iWidth, int iHeight, uint8 byColor);
void box(int iX, int iY, int iWidth, int iHeight, uint8 byBorderColor);
void SetVideoMode(uint8 byVideoMode);

//-------------------------------------------------------------------------------------------------
#endif