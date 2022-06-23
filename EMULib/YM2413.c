/** EMULib Emulation Library *********************************/
/**                                                         **/
/**                         YM2413.c                        **/
/**                                                         **/
/** This file contains emulation for the OPLL sound chip    **/
/** produced by Yamaha (also see OPL2, OPL3, OPL4 chips).   **/
/** See YM2413.h for declarations.                          **/
/**                                                         **/
/** Copyright (C) Marat Fayzullin 1996-2021                 **/
/**     You are not allowed to distribute this software     **/
/**     commercially. Please, notify me, if you make any    **/
/**     changes to this file.                               **/
/*************************************************************/

#include "YM2413.h"
#include "Sound.h"
#include <string.h>

/** Reset2413() **********************************************/
/** Reset the sound chip and use sound channels from the    **/
/** one given in First.                                     **/
/*************************************************************/
void Reset2413(YM2413 *D,int First)
{
  int J;

  /* All registers filled with 0x00 by default */
  memset(D->R,0x00,sizeof(D->R));

  /* Set initial frequencies, volumes, and instruments */
  for(J=0;J<YM2413_CHANNELS;J++)
  {
    SetSound(J+First,SND_MELODIC);
    D->Freq[J]   = 0;
    D->Volume[J] = 0;
    D->R[J+0x30] = 0x0F;
  }

  D->First    = First;
  D->Sync     = YM2413_ASYNC;
  D->Changed  = (1<<YM2413_CHANNELS)-1;
  D->Latch    = 0;
}

/** WrCtrl2413() *********************************************/
/** Write a value V to the OPLL Control Port.               **/
/*************************************************************/
void WrCtrl2413(YM2413 *D,uint8_t V)
{
  D->Latch=V&0x3F;
}

/** WrData2413() *********************************************/
/** Write a value V to the OPLL Data Port.                  **/
/*************************************************************/
void WrData2413(YM2413 *D,uint8_t V)
{
  Write2413(D,D->Latch,V);
}

/** Write2413() **********************************************/
/** Call this function to output a value V into the sound   **/
/** chip.                                                   **/
/*************************************************************/
void Write2413(YM2413 *D,uint8_t R,uint8_t V)
{
  uint8_t C,Oct;
  int Frq;

  /* OPLL registers are 0..63 */
  R&=0x3F;

  /* Lowest 4 bits are channel number */
  C=R&0x0F;

  switch(R>>4)
  {
    case 0:
      switch(C) // register 0-7 User Tone not supported by fMSX
      {
        case 0x0E: // drums - not supported by fMSX, except for muting ch.6-8
          if(V==D->R[R]) return;
          /* Keep all drums off when drum mode is off */
          if(!(V&0x20)) V&=0xE0;
          /* If drum mode was turned on... */
          if((V^D->R[R])&V&0x20)
          {
            /* Turn off melodic channels 6,7,8 */
            D->Freq[6]=D->Freq[7]=D->Freq[8]=0;
            /* Mark channels 6,7,8 as changed */
            D->Changed|=0x1C0;
          }
          /* Done */
          break;
      }
      break;

    case 1: // frequency LSB
      if((C>8)||(V==D->R[R])) return;
      if(!YM2413_DRUMS(D)||(C<6))
        if(D->R[R+0x10]&0x10)
        {
          /* Set channel frequency */
          Oct=D->R[R+0x10];
          Frq=((int)(Oct&0x01)<<8)+V;
          Oct=(Oct&0x0E)>>1;
          D->Freq[C]=(3125*Frq*(1<<Oct))>>15;

          /* Mark channel as changed */
          D->Changed|=1<<C;
        }
      /* Done */
      break;

    case 2: // frequency MSB, octave, key on/off, sustain on/off (last 2 ignored)
      if(C>8) return;
      if(!YM2413_DRUMS(D)||(C<6))
      {
        /* Depending on whether channel is on/off... */
        if(!(V&0x10)) D->Freq[C]=0;
        else
        {
          /* Set channel frequency */
          Frq=((int)(V&0x01)<<8)+D->R[R-0x10];
          Oct=(V&0x0E)>>1;
          D->Freq[C]=(3125*Frq*(1<<Oct))>>15;
        }

        /* Mark channel as changed */
        D->Changed|=1<<C;
      }
      /* Done */
      break;

    case 3: // instrument & volume - instrument ignored
      if((C>8)||(V==D->R[R])) return;
      /* Register any volume changes */
      if((V^D->R[R])&0x0F)
      {
        /* Set channel volume */
        D->Volume[C]=255*(~V&0x0F)/15;
        /* Mark channel as changed */
        D->Changed|=1<<C;
      }
      /* Done */
      break;
  }

  /* Write value into the register */
  D->R[R]=V;

  /* For asynchronous mode, make Sound() calls right away */
  if(!D->Sync&&(D->Changed))
    Sync2413(D,YM2413_FLUSH);
}

/** Sync2413() ***********************************************/
/** Flush all accumulated changes by issuing Sound() calls  **/
/** and set the synchronization on/off. The second argument **/
/** should be YM2413_SYNC/YM2413_ASYNC to set/reset sync,   **/
/** or YM2413_FLUSH to leave sync mode as it is.            **/
/*************************************************************/
void Sync2413(YM2413 *D,uint8_t Sync)
{
  int J,I;

  /* Change sync mode as requested */
  if(Sync!=YM2413_FLUSH) D->Sync=Sync;

  /* Convert channel freq/volume changes into Sound() calls */
  for(J=0,I=D->Changed;I&&(J<YM2413_CHANNELS);++J,I>>=1)
    if(I&1) Sound(J+D->First,D->Freq[J],D->Volume[J]);

  D->Changed=0x000;
}
