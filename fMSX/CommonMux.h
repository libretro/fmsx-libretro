/** fMSX: portable MSX emulator ******************************/
/**                                                         **/
/**                       CommonMux.h                       **/
/**                                                         **/
/** This file instantiates MSX screen drivers for every     **/
/** possible screen depth. It includes common driver code   **/
/** from Common.h and Wide.h.                               **/
/**                                                         **/
/** Copyright (C) Marat Fayzullin 1994-2021                 **/
/**     You are not allowed to distribute this software     **/
/**     commercially. Please, notify me, if you make any    **/
/**     changes to this file.                               **/
/*************************************************************/
#ifndef COMMONMUX_H
#define COMMONMUX_H

#include "Common.h"
#include "Wide.h"

/** Screen Mode Handlers [number of screens + 1] *************/
extern void (*RefreshLine[MAXSCREEN+2])(uint8_t Y);

#define FirstLine        FirstLine_16
#define Sprites          Sprites_16
#define ColorSprites     ColorSprites_16
#define RefreshBorder    RefreshBorder_16
#define RefreshBorder512 RefreshBorder512_16
#define ClearLine        ClearLine_16
#define ClearLine512     ClearLine512_16
#define YJKColor         YJKColor_16
#define RefreshScreen    RefreshScreen_16
#define RefreshLineF     RefreshLineF_16
#define RefreshLine0     RefreshLine0_16
#define RefreshLine1     RefreshLine1_16
#define RefreshLine2     RefreshLine2_16
#define RefreshLine3     RefreshLine3_16
#define RefreshLine4     RefreshLine4_16
#define RefreshLine5     RefreshLine5_16
#define RefreshLine6     RefreshLine6_16
#define RefreshLine7     RefreshLine7_16
#define RefreshLine8     RefreshLine8_16
#define RefreshLine10    RefreshLine10_16
#define RefreshLine12    RefreshLine12_16
#define RefreshLineTx80  RefreshLineTx80_16

#endif /* COMMONMUX_H */
