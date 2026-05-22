#ifndef _ROLLER_FUNCTION_H
#define _ROLLER_FUNCTION_H
//-------------------------------------------------------------------------------------------------
#include "3d.h"
#include "car.h"
#include <stdbool.h>
//-------------------------------------------------------------------------------------------------

extern int firework_colours[16];
extern float mapsize[25];
extern int mapsect[25];
extern int invulnerable[16];

//-------------------------------------------------------------------------------------------------

void firework_display_one_tick(void);

void finish_race();
double getbankz(float fInput, int iChunkIdx, tData *pData);
void calculate_aheadbehindtime(int iCarIdx, float *pfAheadTime, float *pfBehindTime);
void initnearcars();
void initpits();
void dodamage(tCar *pCar, float fDamage);
void doviewtend(tCar *pCar, int iFrameDelta, int iViewIdx);
void changemateto(int iCarIndex, int iNewStrategy);
bool linevalid(int iChunkIdx, float fStartCoord, float fEndCoord);
void analysefalloff(tCar *pCar);
void showmap(uint8 *pScrPtr, int iCarIdx);
void firework_display();

//-------------------------------------------------------------------------------------------------
#endif
