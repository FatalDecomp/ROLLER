#ifndef _ROLLER_COLISION_H
#define _ROLLER_COLISION_H
//-------------------------------------------------------------------------------------------------
#include "car.h"
//-------------------------------------------------------------------------------------------------

extern float damage_levels[4];
extern float coldist[33][33];
extern int damage_level;

//-------------------------------------------------------------------------------------------------

void testcollisions();
void testcoll(tCar *pCar1, tCar *pCar2, int iDistanceSteps);
void initcollisions();
int checkintersect(double dCar1PosX, double dCar1PosY, double dAngleIdx1,
                   double dCar2PosX, double dCar2PosY, double dAngleIdx2,
                   double dCarBaseX, double dCarBaseY);

//-------------------------------------------------------------------------------------------------
#endif