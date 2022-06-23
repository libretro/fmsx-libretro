/** EMULib Emulation Library *********************************/
/**                                                         **/
/**                          I8255.h                        **/
/**                                                         **/
/** This file contains emulation for the i8255 parallel     **/
/** port interface (PPI) chip from Intel. See I8255.h for   **/
/** the actual code.                                        **/
/**                                                         **/
/** Copyright (C) Marat Fayzullin 2001-2021                 **/
/**     You are not allowed to distribute this software     **/
/**     commercially. Please, notify me, if you make any    **/
/**     changes to this file.                               **/
/*************************************************************/
#ifndef I8255_H
#define I8255_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** I8255 ****************************************************/
/** This data structure stores i8255 state and port values. **/
/*************************************************************/
typedef struct
{
  uint8_t R[4];         /* Registers    */
  uint8_t Rout[3];      /* Output ports */
  uint8_t Rin[3];       /* Input ports  */
} I8255;

/** Reset8255 ************************************************/
/** Reset the i8255 chip. Set all data to 0x00. Set all     **/
/** ports to "input" mode.                                  **/
/*************************************************************/
void Reset8255(I8255 *D);

/** Write8255 ************************************************/
/** Write value V into i8255 register A. Returns 0 when A   **/
/** is out of range, 1 otherwise.                           **/
/*************************************************************/
uint8_t Write8255(I8255 *D,uint8_t A,uint8_t V);

/** Read8255 *************************************************/
/** Read value from an i8255 register A. Returns 0 when A   **/
/** is out of range.                                        **/
/*************************************************************/
uint8_t Read8255(I8255 *D,uint8_t A);

#ifdef __cplusplus
}
#endif
#endif /* I8255_H */
