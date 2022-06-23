/** fMSX: portable MSX emulator ******************************/
/**                                                         **/
/**                         V9938.h                         **/
/**                                                         **/
/** This file contains declarations for V9938 special       **/
/** graphical operations support implemented in V9938.c.    **/
/**                                                         **/
/** Copyright (C) Marat Fayzullin 1994-2021                 **/
/**     You are not allowed to distribute this software     **/
/**     commercially. Please, notify me, if you make any    **/
/**     changes to this file.                               **/
/*************************************************************/
#ifndef V9938_H
#define V9938_H

#include "MSX.h"

/** VDPWrite() ***********************************************/
/** Use this function to transfer pixel(s) from CPU to VDP. **/
/*************************************************************/
void VDPWrite(uint8_t V);

/** VDPRead() ************************************************/
/** Use this function to transfer pixel(s) from VDP to CPU. **/
/*************************************************************/
uint8_t VDPRead(void);

/** VDPDraw() ************************************************/
/** Perform a given V9938 graphical operation.              **/
/*************************************************************/
uint8_t VDPDraw(uint8_t Op);

/** LoopVDP() ************************************************/
/** Perform a number of steps of the active operation       **/
/*************************************************************/
void LoopVDP(void);

#endif /* V9938_H */
