/** fMSX: portable MSX emulator ******************************/
/**                                                         **/
/**                         V9938.c                         **/
/**                                                         **/
/** This file contains implementation for the V9938 special **/
/** graphical operations.                                   **/
/**                                                         **/
/** Copyright (C) Marat Fayzullin 1994-2021                 **/
/**     You are not allowed to distribute this software     **/
/**     commercially. Please, notify me, if you make any    **/
/**     changes to this file.                               **/
/**                                                         **/
/** Completely rewritten by Alex Wulms:                     **/
/**  - VDP Command execution 'in parallel' with CPU         **/
/**  - Corrected behaviour of VDP commands                  **/
/**  - Made it easier to implement correct S7/8 mapping     **/
/**    by concentrating VRAM access in one single place     **/
/**  - Made use of the 'in parallel' VDP command exec       **/
/**    and correct timing. You must call the function       **/
/**    LoopVDP() from LoopZ80 in MSX.c. You must call it    **/
/**    exactly 256 times per screen refresh.                **/
/** Started on       : 11-11-1999                           **/
/** Beta release 1 on:  9-12-1999                           **/
/** Beta release 2 on: 20-01-2000                           **/
/**  - Corrected behaviour of VRM <-> Z80 transfer          **/
/**  - Improved performance of the code                     **/
/** Public release 1.0: 20-04-2000                          **/
/*************************************************************/
#include "V9938.h"
#include <string.h>

extern retro_log_printf_t log_cb;

/*************************************************************/
/** Other useful defines                                    **/
/*************************************************************/
#define VDP_VRMP5(X, Y) (VRAM + ((Y&1023)<<7) + ((X&255)>>1))
#define VDP_VRMP6(X, Y) (VRAM + ((Y&1023)<<7) + ((X&511)>>2))
#define VDP_VRMP7(X, Y) (VRAM + ((Y&511)<<8) + ((X&511)>>1))
#define VDP_VRMP8(X, Y) (VRAM + ((Y&511)<<8) + (X&255))

#define VDP_VRMP(M, X, Y) VDPVRMP(M, X, Y)
#define VDP_POINT(M, X, Y) VDPpoint(M, X, Y)
#define VDP_PSET(M, X, Y, C, O) VDPpset(M, X, Y, C, O)

#define CM_ABRT  0x0
#define CM_POINT 0x4
#define CM_PSET  0x5
#define CM_SRCH  0x6
#define CM_LINE  0x7
#define CM_LMMV  0x8
#define CM_LMMM  0x9
#define CM_LMCM  0xA
#define CM_LMMC  0xB
#define CM_HMMV  0xC
#define CM_HMMM  0xD
#define CM_YMMM  0xE
#define CM_HMMC  0xF

/*************************************************************/
/* Many VDP commands are executed in some kind of loop but   */
/* essentially, there are only a few basic loop structures   */
/* that are re-used. We define the loop structures that are  */
/* re-used here so that they have to be entered only once    */
/*************************************************************/
#define pre_loop \
    while ((cnt-=delta) > 0) {

/* Loop over DX, DY */
#define post__x_y(MX) \
    if (!--ANX || ((ADX+=TX)&MX)) { \
      if (!(--NY&1023) || (DY+=TY)==-1) \
        break; \
      else { \
        ADX=DX; \
        ANX=NX; \
      } \
    } \
  }

/* Loop over DX, SY, DY */
#define post__xyy(MX) \
    if ((ADX+=TX)&MX) { \
      if (!(--NY&1023) || (SY+=TY)==-1 || (DY+=TY)==-1) \
        break; \
      else \
        ADX=DX; \
    } \
  }

/* Loop over SX, DX, SY, DY */
#define post_xxyy(MX) \
    if (!--ANX || ((ASX+=TX)&MX) || ((ADX+=TX)&MX)) { \
      if (!(--NY&1023) || (SY+=TY)==-1 || (DY+=TY)==-1) \
        break; \
      else { \
        ASX=SX; \
        ADX=DX; \
        ANX=NX; \
      } \
    } \
  }

/*************************************************************/
/** Structures and stuff                                    **/
/*************************************************************/
static struct {
  int SX,SY;
  int DX,DY;
  int TX,TY;
  int NX,NY;
  int MX;
  int ASX,ADX,ANX;
  uint8_t CL;
  uint8_t LO;
  uint8_t CM;
} MMC;

/*************************************************************/
/** Function prototypes                                     **/
/*************************************************************/
static uint8_t *VDPVRMP(uint8_t M, int X, int Y);

static uint8_t VDPpoint5(int SX, int SY);
static uint8_t VDPpoint6(int SX, int SY);
static uint8_t VDPpoint7(int SX, int SY);
static uint8_t VDPpoint8(int SX, int SY);

static uint8_t VDPpoint(uint8_t SM, 
                     int SX, int SY);

static void VDPpsetlowlevel(uint8_t *P, uint8_t CL,
                            uint8_t M, uint8_t OP);

static void VDPpset5(int DX, int DY,
                     uint8_t CL, uint8_t OP);
static void VDPpset6(int DX, int DY,
                     uint8_t CL, uint8_t OP);
static void VDPpset7(int DX, int DY,
                     uint8_t CL, uint8_t OP);
static void VDPpset8(int DX, int DY,
                     uint8_t CL, uint8_t OP);

static void VDPpset(uint8_t SM,
                    int DX, int DY,
                    uint8_t CL, uint8_t OP);

static int GetVdpTimingValue(int *);

static void SrchEngine(void);
static void LineEngine(void);
static void LmmvEngine(void);
static void LmmmEngine(void);
static void LmcmEngine(void);
static void LmmcEngine(void);
static void HmmvEngine(void);
static void HmmmEngine(void);
static void YmmmEngine(void);
static void HmmcEngine(void);

/*************************************************************/
/** Variables visible only in this module                   **/
/*************************************************************/
static uint8_t Mask[4] = { 0x0F,0x03,0x0F,0xFF };
static int  PPB[4]  = { 2,4,2,1 };
static int  PPL[4]  = { 256,512,512,256 };
static int  VdpOpsCnt=1;
static void (*VdpEngine)(void)=0;

                      /*  SprOn SprOn SprOf SprOf */
                      /*  ScrOf ScrOn ScrOf ScrOn */
static int srch_timing[8]={ 818, 1025,  818,  830,   /* ntsc */
                            696,  854,  696,  684 }; /* pal  */
static int line_timing[8]={ 1063, 1259, 1063, 1161,
                            904,  1026, 904,  953 };
static int hmmv_timing[8]={ 439,  549,  439,  531,
                            366,  439,  366,  427 };
static int lmmv_timing[8]={ 873,  1135, 873, 1056,
                            732,  909,  732,  854 };
static int ymmm_timing[8]={ 586,  952,  586,  610,
                            488,  720,  488,  500 };
static int hmmm_timing[8]={ 818,  1111, 818,  854,
                            684,  879,  684,  708 };
static int lmmm_timing[8]={ 1160, 1599, 1160, 1172, 
                            964,  1257, 964,  977 };


/** VDPVRMP() **********************************************/
/** Calculate addr of a pixel in vram                       **/
/*************************************************************/
INLINE uint8_t *VDPVRMP(uint8_t M,int X,int Y)
{
  switch(M)
  {
    case 0: return VDP_VRMP5(X,Y);
    case 1: return VDP_VRMP6(X,Y);
    case 2: return VDP_VRMP7(X,Y);
    case 3: return VDP_VRMP8(X,Y);
  }

  return(VRAM);
}

/** VDPpoint5() ***********************************************/
/** Get a pixel on screen 5                                 **/
/*************************************************************/
INLINE uint8_t VDPpoint5(int SX, int SY)
{
  return (*VDP_VRMP5(SX, SY) >>
          (((~SX)&1)<<2)
         )&15;
}

/** VDPpoint6() ***********************************************/
/** Get a pixel on screen 6                                 **/
/*************************************************************/
INLINE uint8_t VDPpoint6(int SX, int SY)
{
  return (*VDP_VRMP6(SX, SY) >>
          (((~SX)&3)<<1)
         )&3;
}

/** VDPpoint7() ***********************************************/
/** Get a pixel on screen 7                                 **/
/*************************************************************/
INLINE uint8_t VDPpoint7(int SX, int SY)
{
  return (*VDP_VRMP7(SX, SY) >>
          (((~SX)&1)<<2)
         )&15;
}

/** VDPpoint8() ***********************************************/
/** Get a pixel on screen 8                                 **/
/*************************************************************/
INLINE uint8_t VDPpoint8(int SX, int SY)
{
  return *VDP_VRMP8(SX, SY);
}

/** VDPpoint() ************************************************/
/** Get a pixel on a screen                                 **/
/*************************************************************/
INLINE uint8_t VDPpoint(uint8_t SM, int SX, int SY)
{
  switch(SM)
  {
    case 0: return VDPpoint5(SX,SY);
    case 1: return VDPpoint6(SX,SY);
    case 2: return VDPpoint7(SX,SY);
    case 3: return VDPpoint8(SX,SY);
  }

  return(0);
}

/** VDPpsetlowlevel() ****************************************/
/** Low level function to set a pixel on a screen           **/
/** Make it inline to make it fast                          **/
/*************************************************************/
INLINE void VDPpsetlowlevel(uint8_t *P, uint8_t CL, uint8_t M, uint8_t OP)
{
  switch (OP)
  {
    case 0: *P = (*P & M) | CL; break;
    case 1: *P = *P & (CL | M); break;
    case 2: *P |= CL; break;
    case 3: *P ^= CL; break;
    case 4: *P = (*P & M) | ~(CL | M); break;
    case 8: if (CL) *P = (*P & M) | CL; break;
    case 9: if (CL) *P = *P & (CL | M); break;
    case 10: if (CL) *P |= CL; break;
    case 11:  if (CL) *P ^= CL; break;
    case 12:  if (CL) *P = (*P & M) | ~(CL|M); break;
  }
}

/** VDPpset5() ***********************************************/
/** Set a pixel on screen 5                                 **/
/*************************************************************/
INLINE void VDPpset5(int DX, int DY, uint8_t CL, uint8_t OP)
{
  uint8_t SH = ((~DX)&1)<<2;

  VDPpsetlowlevel(VDP_VRMP5(DX, DY),
                  CL << SH, ~(15<<SH), OP);
}

/** VDPpset6() ***********************************************/
/** Set a pixel on screen 6                                 **/
/*************************************************************/
INLINE void VDPpset6(int DX, int DY, uint8_t CL, uint8_t OP)
{
  uint8_t SH = ((~DX)&3)<<1;

  VDPpsetlowlevel(VDP_VRMP6(DX, DY),
                  CL << SH, ~(3<<SH), OP);
}

/** VDPpset7() ***********************************************/
/** Set a pixel on screen 7                                 **/
/*************************************************************/
INLINE void VDPpset7(int DX, int DY, uint8_t CL, uint8_t OP)
{
  uint8_t SH = ((~DX)&1)<<2;

  VDPpsetlowlevel(VDP_VRMP7(DX, DY),
                  CL << SH, ~(15<<SH), OP);
}

/** VDPpset8() ***********************************************/
/** Set a pixel on screen 8                                 **/
/*************************************************************/
INLINE void VDPpset8(int DX, int DY, uint8_t CL, uint8_t OP)
{
  VDPpsetlowlevel(VDP_VRMP8(DX, DY),
                  CL, 0, OP);
}

/** VDPpset() ************************************************/
/** Set a pixel on a screen                                 **/
/*************************************************************/
INLINE void VDPpset(uint8_t SM, int DX, int DY, uint8_t CL, uint8_t OP)
{
  switch (SM) {
    case 0: VDPpset5(DX, DY, CL, OP); break;
    case 1: VDPpset6(DX, DY, CL, OP); break;
    case 2: VDPpset7(DX, DY, CL, OP); break;
    case 3: VDPpset8(DX, DY, CL, OP); break;
  }
}

/** GetVdpTimingValue() **************************************/
/** Get timing value for a certain VDP command              **/
/*************************************************************/
static int GetVdpTimingValue(int *timing_values)
{
  return(timing_values[((VDP[1]>>6)&1)|(VDP[8]&2)|((VDP[9]<<1)&4)]);
}

/** SrchEgine()** ********************************************/
/** Search a dot                                            **/
/*************************************************************/
void SrchEngine(void)
{
  int SX=MMC.SX;
  int SY=MMC.SY;
  int TX=MMC.TX;
  int ANX=MMC.ANX;
  uint8_t CL=MMC.CL;
  int delta = GetVdpTimingValue(srch_timing);
  int cnt = VdpOpsCnt;

#define pre_srch \
    pre_loop \
      if ((
#define post_srch(MX) \
           ==CL) ^ANX) { \
      VDPStatus[2]|=0x10; /* Border detected */ \
      break; \
    } \
    if ((SX+=TX) & MX) { \
      VDPStatus[2]&=0xEF; /* Border not detected */ \
      break; \
    } \
  } 

  switch (ScrMode) {
    case 5: pre_srch VDPpoint5(SX, SY) post_srch(256)
            break;
    case 6: pre_srch VDPpoint6(SX, SY) post_srch(512)
            break;
    case 7: pre_srch VDPpoint7(SX, SY) post_srch(512)
            break;
    case 8: pre_srch VDPpoint8(SX, SY) post_srch(256)
            break;
  }

  if ((VdpOpsCnt=cnt)>0) {
    /* Command execution done */
    VDPStatus[2]&=0xFE;
    VdpEngine=0;
    /* Update SX in VDP registers */
    VDPStatus[8]=SX&0xFF;
    VDPStatus[9]=(SX>>8)|0xFE;
  }
  else {
    MMC.SX=SX;
  }
}

/** LineEgine()** ********************************************/
/** Draw a line                                             **/
/*************************************************************/
void LineEngine(void)
{
  int DX=MMC.DX;
  int DY=MMC.DY;
  int TX=MMC.TX;
  int TY=MMC.TY;
  int NX=MMC.NX;
  int NY=MMC.NY;
  int ASX=MMC.ASX;
  int ADX=MMC.ADX;
  uint8_t CL=MMC.CL;
  uint8_t LO=MMC.LO;
  int delta = GetVdpTimingValue(line_timing);
  int cnt = VdpOpsCnt;

#define post_linexmaj(MX) \
      DX+=TX; \
      if ((ASX-=NY)<0) { \
        ASX+=NX; \
        DY+=TY; \
      } \
      ASX&=1023; /* Mask to 10 bits range */ \
      if (ADX++==NX || (DX&MX)) \
        break; \
    }
#define post_lineymaj(MX) \
      DY+=TY; \
      if ((ASX-=NY)<0) { \
        ASX+=NX; \
        DX+=TX; \
      } \
      ASX&=1023; /* Mask to 10 bits range */ \
      if (ADX++==NX || (DX&MX)) \
        break; \
    }

  if ((VDP[45]&0x01)==0)
    /* X-Axis is major direction */
    switch (ScrMode) {
      case 5: pre_loop VDPpset5(DX, DY, CL, LO); post_linexmaj(256)
              break;
      case 6: pre_loop VDPpset6(DX, DY, CL, LO); post_linexmaj(512)
              break;
      case 7: pre_loop VDPpset7(DX, DY, CL, LO); post_linexmaj(512)
              break;
      case 8: pre_loop VDPpset8(DX, DY, CL, LO); post_linexmaj(256)
              break;
    }
  else
    /* Y-Axis is major direction */
    switch (ScrMode) {
      case 5: pre_loop VDPpset5(DX, DY, CL, LO); post_lineymaj(256)
              break;
      case 6: pre_loop VDPpset6(DX, DY, CL, LO); post_lineymaj(512)
              break;
      case 7: pre_loop VDPpset7(DX, DY, CL, LO); post_lineymaj(512)
              break;
      case 8: pre_loop VDPpset8(DX, DY, CL, LO); post_lineymaj(256)
              break;
    }

  if ((VdpOpsCnt=cnt)>0) {
    /* Command execution done */
    VDPStatus[2]&=0xFE;
    VdpEngine=0;
    VDP[38]=DY & 0xFF;
    VDP[39]=(DY>>8) & 0x03;
  }
  else {
    MMC.DX=DX;
    MMC.DY=DY;
    MMC.ASX=ASX;
    MMC.ADX=ADX;
  }
}

/** LmmvEngine() *********************************************/
/** VDP -> Vram                                             **/
/*************************************************************/
void LmmvEngine(void)
{
  int DX=MMC.DX;
  int DY=MMC.DY;
  int TX=MMC.TX;
  int TY=MMC.TY;
  int NX=MMC.NX;
  int NY=MMC.NY;
  int ADX=MMC.ADX;
  int ANX=MMC.ANX;
  uint8_t CL=MMC.CL;
  uint8_t LO=MMC.LO;
  int delta = GetVdpTimingValue(lmmv_timing);
  int cnt = VdpOpsCnt;

  switch (ScrMode) {
    case 5: pre_loop VDPpset5(ADX, DY, CL, LO); post__x_y(256)
            break;
    case 6: pre_loop VDPpset6(ADX, DY, CL, LO); post__x_y(512)
            break;
    case 7: pre_loop VDPpset7(ADX, DY, CL, LO); post__x_y(512)
            break;
    case 8: pre_loop VDPpset8(ADX, DY, CL, LO); post__x_y(256)
            break;
  }

  if ((VdpOpsCnt=cnt)>0) {
    /* Command execution done */
    VDPStatus[2]&=0xFE;
    VdpEngine=0;
    if (!NY)
      DY+=TY;
    VDP[38]=DY & 0xFF;
    VDP[39]=(DY>>8) & 0x03;
    VDP[42]=NY & 0xFF;
    VDP[43]=(NY>>8) & 0x03;
  }
  else {
    MMC.DY=DY;
    MMC.NY=NY;
    MMC.ANX=ANX;
    MMC.ADX=ADX;
  }
}

/** LmmmEngine() *********************************************/
/** Vram -> Vram                                            **/
/*************************************************************/
void LmmmEngine(void)
{
  int SX=MMC.SX;
  int SY=MMC.SY;
  int DX=MMC.DX;
  int DY=MMC.DY;
  int TX=MMC.TX;
  int TY=MMC.TY;
  int NX=MMC.NX;
  int NY=MMC.NY;
  int ASX=MMC.ASX;
  int ADX=MMC.ADX;
  int ANX=MMC.ANX;
  uint8_t LO=MMC.LO;
  int delta = GetVdpTimingValue(lmmm_timing);
  int cnt = VdpOpsCnt;

  switch (ScrMode) {
    case 5: pre_loop VDPpset5(ADX, DY, VDPpoint5(ASX, SY), LO); post_xxyy(256)
            break;
    case 6: pre_loop VDPpset6(ADX, DY, VDPpoint6(ASX, SY), LO); post_xxyy(512)
            break;
    case 7: pre_loop VDPpset7(ADX, DY, VDPpoint7(ASX, SY), LO); post_xxyy(512)
            break;
    case 8: pre_loop VDPpset8(ADX, DY, VDPpoint8(ASX, SY), LO); post_xxyy(256)
            break;
  }

  if ((VdpOpsCnt=cnt)>0) {
    /* Command execution done */
    VDPStatus[2]&=0xFE;
    VdpEngine=0;
    if (!NY) {
      SY+=TY;
      DY+=TY;
    }
    else
      if (SY==-1)
        DY+=TY;
    VDP[42]=NY & 0xFF;
    VDP[43]=(NY>>8) & 0x03;
    VDP[34]=SY & 0xFF;
    VDP[35]=(SY>>8) & 0x03;
    VDP[38]=DY & 0xFF;
    VDP[39]=(DY>>8) & 0x03;
  }
  else {
    MMC.SY=SY;
    MMC.DY=DY;
    MMC.NY=NY;
    MMC.ANX=ANX;
    MMC.ASX=ASX;
    MMC.ADX=ADX;
  }
}

/** LmcmEngine() *********************************************/
/** Vram -> CPU                                             **/
/*************************************************************/
void LmcmEngine(void)
{
  if ((VDPStatus[2]&0x80)!=0x80)
  {
    VDPStatus[7]=VDP[44]=VDP_POINT(ScrMode-5, MMC.ASX, MMC.SY);
    VdpOpsCnt-=GetVdpTimingValue(lmmv_timing);
    VDPStatus[2]|=0x80;

    if (!--MMC.ANX || ((MMC.ASX+=MMC.TX)&MMC.MX)) {
      if (!(--MMC.NY & 1023) || (MMC.SY+=MMC.TY)==-1) {
        VDPStatus[2]&=0xFE;
        VdpEngine=0;
        if (!MMC.NY)
          MMC.DY+=MMC.TY;
        VDP[42]=MMC.NY & 0xFF;
        VDP[43]=(MMC.NY>>8) & 0x03;
        VDP[34]=MMC.SY & 0xFF;
        VDP[35]=(MMC.SY>>8) & 0x03;
      }
      else {
        MMC.ASX=MMC.SX;
        MMC.ANX=MMC.NX;
      }
    }
  }
}

/** LmmcEngine() *********************************************/
/** CPU -> Vram                                             **/
/*************************************************************/
void LmmcEngine(void)
{
  if ((VDPStatus[2]&0x80)!=0x80) {
    uint8_t SM=ScrMode-5;

    VDPStatus[7]=VDP[44]&=Mask[SM];
    VDP_PSET(SM, MMC.ADX, MMC.DY, VDP[44], MMC.LO);
    VdpOpsCnt-=GetVdpTimingValue(lmmv_timing);
    VDPStatus[2]|=0x80;

    if (!--MMC.ANX || ((MMC.ADX+=MMC.TX)&MMC.MX)) {
      if (!(--MMC.NY&1023) || (MMC.DY+=MMC.TY)==-1) {
        VDPStatus[2]&=0xFE;
        VdpEngine=0;
        if (!MMC.NY)
          MMC.DY+=MMC.TY;
        VDP[42]=MMC.NY & 0xFF;
        VDP[43]=(MMC.NY>>8) & 0x03;
        VDP[38]=MMC.DY & 0xFF;
        VDP[39]=(MMC.DY>>8) & 0x03;
      }
      else {
        MMC.ADX=MMC.DX;
        MMC.ANX=MMC.NX;
      }
    }
  }
}

/** HmmvEngine() *********************************************/
/** VDP --> Vram                                            **/
/*************************************************************/
void HmmvEngine(void)
{
  int DX=MMC.DX;
  int DY=MMC.DY;
  int TX=MMC.TX;
  int TY=MMC.TY;
  int NX=MMC.NX;
  int NY=MMC.NY;
  int ADX=MMC.ADX;
  int ANX=MMC.ANX;
  uint8_t CL=MMC.CL;
  int delta = GetVdpTimingValue(hmmv_timing);
  int cnt = VdpOpsCnt;

  switch (ScrMode) {
    case 5: pre_loop *VDP_VRMP5(ADX, DY) = CL; post__x_y(256)
            break;
    case 6: pre_loop *VDP_VRMP6(ADX, DY) = CL; post__x_y(512)
            break;
    case 7: pre_loop *VDP_VRMP7(ADX, DY) = CL; post__x_y(512)
            break;
    case 8: pre_loop *VDP_VRMP8(ADX, DY) = CL; post__x_y(256)
            break;
  }

  if ((VdpOpsCnt=cnt)>0) {
    /* Command execution done */
    VDPStatus[2]&=0xFE;
    VdpEngine=0;
    if (!NY)
      DY+=TY;
    VDP[42]=NY & 0xFF;
    VDP[43]=(NY>>8) & 0x03;
    VDP[38]=DY & 0xFF;
    VDP[39]=(DY>>8) & 0x03;
  }
  else {
    MMC.DY=DY;
    MMC.NY=NY;
    MMC.ANX=ANX;
    MMC.ADX=ADX;
  }
}

/** HmmmEngine() *********************************************/
/** Vram -> Vram                                            **/
/*************************************************************/
void HmmmEngine(void)
{
  int SX=MMC.SX;
  int SY=MMC.SY;
  int DX=MMC.DX;
  int DY=MMC.DY;
  int TX=MMC.TX;
  int TY=MMC.TY;
  int NX=MMC.NX;
  int NY=MMC.NY;
  int ASX=MMC.ASX;
  int ADX=MMC.ADX;
  int ANX=MMC.ANX;
  int delta = GetVdpTimingValue(hmmm_timing);
  int cnt = VdpOpsCnt;

  switch (ScrMode) {
    case 5: pre_loop *VDP_VRMP5(ADX, DY) = *VDP_VRMP5(ASX, SY); post_xxyy(256)
            break;
    case 6: pre_loop *VDP_VRMP6(ADX, DY) = *VDP_VRMP6(ASX, SY); post_xxyy(512)
            break;
    case 7: pre_loop *VDP_VRMP7(ADX, DY) = *VDP_VRMP7(ASX, SY); post_xxyy(512)
            break;
    case 8: pre_loop *VDP_VRMP8(ADX, DY) = *VDP_VRMP8(ASX, SY); post_xxyy(256)
            break;
  }

  if ((VdpOpsCnt=cnt)>0) {
    /* Command execution done */
    VDPStatus[2]&=0xFE;
    VdpEngine=0;
    if (!NY) {
      SY+=TY;
      DY+=TY;
    }
    else
      if (SY==-1)
        DY+=TY;
    VDP[42]=NY & 0xFF;
    VDP[43]=(NY>>8) & 0x03;
    VDP[34]=SY & 0xFF;
    VDP[35]=(SY>>8) & 0x03;
    VDP[38]=DY & 0xFF;
    VDP[39]=(DY>>8) & 0x03;
  }
  else {
    MMC.SY=SY;
    MMC.DY=DY;
    MMC.NY=NY;
    MMC.ANX=ANX;
    MMC.ASX=ASX;
    MMC.ADX=ADX;
  }
}

/** YmmmEngine() *********************************************/
/** Vram -> Vram                                            **/
/*************************************************************/
void YmmmEngine(void)
{
  int SY=MMC.SY;
  int DX=MMC.DX;
  int DY=MMC.DY;
  int TX=MMC.TX;
  int TY=MMC.TY;
  int NY=MMC.NY;
  int ADX=MMC.ADX;
  int delta = GetVdpTimingValue(ymmm_timing);
  int cnt = VdpOpsCnt;

  switch (ScrMode) {
    case 5: pre_loop *VDP_VRMP5(ADX, DY) = *VDP_VRMP5(ADX, SY); post__xyy(256)
            break;
    case 6: pre_loop *VDP_VRMP6(ADX, DY) = *VDP_VRMP6(ADX, SY); post__xyy(512)
            break;
    case 7: pre_loop *VDP_VRMP7(ADX, DY) = *VDP_VRMP7(ADX, SY); post__xyy(512)
            break;
    case 8: pre_loop *VDP_VRMP8(ADX, DY) = *VDP_VRMP8(ADX, SY); post__xyy(256)
            break;
  }

  if ((VdpOpsCnt=cnt)>0) {
    /* Command execution done */
    VDPStatus[2]&=0xFE;
    VdpEngine=0;
    if (!NY) {
      SY+=TY;
      DY+=TY;
    }
    else
      if (SY==-1)
        DY+=TY;
    VDP[42]=NY & 0xFF;
    VDP[43]=(NY>>8) & 0x03;
    VDP[34]=SY & 0xFF;
    VDP[35]=(SY>>8) & 0x03;
    VDP[38]=DY & 0xFF;
    VDP[39]=(DY>>8) & 0x03;
  }
  else {
    MMC.SY=SY;
    MMC.DY=DY;
    MMC.NY=NY;
    MMC.ADX=ADX;
  }
}

/** HmmcEngine() *********************************************/
/** CPU -> Vram                                             **/
/*************************************************************/
void HmmcEngine(void)
{
  if ((VDPStatus[2]&0x80)!=0x80) {

    *VDP_VRMP(ScrMode-5, MMC.ADX, MMC.DY)=VDP[44];
    VdpOpsCnt-=GetVdpTimingValue(hmmv_timing);
    VDPStatus[2]|=0x80;

    if (!--MMC.ANX || ((MMC.ADX+=MMC.TX)&MMC.MX)) {
      if (!(--MMC.NY&1023) || (MMC.DY+=MMC.TY)==-1) {
        VDPStatus[2]&=0xFE;
        VdpEngine=0;
        if (!MMC.NY)
          MMC.DY+=MMC.TY;
        VDP[42]=MMC.NY & 0xFF;
        VDP[43]=(MMC.NY>>8) & 0x03;
        VDP[38]=MMC.DY & 0xFF;
        VDP[39]=(MMC.DY>>8) & 0x03;
      }
      else {
        MMC.ADX=MMC.DX;
        MMC.ANX=MMC.NX;
      }
    }
  }
}

/** VDPWrite() ***********************************************/
/** Use this function to transfer pixel(s) from CPU to VDP. **/
/*************************************************************/
void VDPWrite(uint8_t V)
{
  VDPStatus[2]&=0x7F;
  VDPStatus[7]=VDP[44]=V;
  if(VdpEngine&&(VdpOpsCnt>0)) VdpEngine();
}

/** VDPRead() ************************************************/
/** Use this function to transfer pixel(s) from VDP to CPU. **/
/*************************************************************/
uint8_t VDPRead(void)
{
  VDPStatus[2]&=0x7F;
  if(VdpEngine&&(VdpOpsCnt>0)) VdpEngine();
  return(VDP[44]);
}

/** VDPDraw() ************************************************/
/** Perform a given V9938 operation Op.                     **/
/*************************************************************/
uint8_t VDPDraw(uint8_t Op)
{
  int SM;

  /* V9938 ops only work in SCREENs 5-8 */
  if (ScrMode<5)
    return(0);

  SM = ScrMode-5;         /* Screen mode index 0..3  */

  MMC.CM = Op>>4;
  if ((MMC.CM & 0x0C) != 0x0C && MMC.CM != 0)
    /* Dot operation: use only relevant bits of color */
    VDPStatus[7]=(VDP[44]&=Mask[SM]);

  switch(Op>>4) {
    case CM_ABRT:
      VDPStatus[2]&=0xFE;
      VdpEngine=0;  
      return 1;
    case CM_POINT:
      VDPStatus[2]&=0xFE;
      VdpEngine=0;  
      VDPStatus[7]=VDP[44]=
                   VDP_POINT(SM, VDP[32]+((int)VDP[33]<<8),
                                 VDP[34]+((int)VDP[35]<<8));
      return 1;
    case CM_PSET:
      VDPStatus[2]&=0xFE;
      VdpEngine=0;  
      VDP_PSET(SM, 
               VDP[36]+((int)VDP[37]<<8),
               VDP[38]+((int)VDP[39]<<8),
               VDP[44],
               Op&0x0F);
      return 1;
    case CM_SRCH:
      VdpEngine=SrchEngine;
      break;
    case CM_LINE:
      VdpEngine=LineEngine;
      break;
    case CM_LMMV:
      VdpEngine=LmmvEngine;
      break;
    case CM_LMMM:
      VdpEngine=LmmmEngine;
      break;
    case CM_LMCM:
      VdpEngine=LmcmEngine;
      break;
    case CM_LMMC:
      VdpEngine=LmmcEngine;
      break;
    case CM_HMMV:
      VdpEngine=HmmvEngine;
      break;
    case CM_HMMM:
      VdpEngine=HmmmEngine;
      break;
    case CM_YMMM:
      VdpEngine=YmmmEngine;
      break;
    case CM_HMMC:
      VdpEngine=HmmcEngine;  
      break;
    default:
        return(0);
  }

  /* Fetch unconditional arguments */
  MMC.SX = (VDP[32]+((int)VDP[33]<<8)) & 511;
  MMC.SY = (VDP[34]+((int)VDP[35]<<8)) & 1023;
  MMC.DX = (VDP[36]+((int)VDP[37]<<8)) & 511;
  MMC.DY = (VDP[38]+((int)VDP[39]<<8)) & 1023;
  MMC.NY = (VDP[42]+((int)VDP[43]<<8)) & 1023;
  MMC.TY = VDP[45]&0x08? -1:1;
  MMC.MX = PPL[SM]; 
  MMC.CL = VDP[44];
  MMC.LO = Op&0x0F;

  /* Argument depends on uint8_t or dot operation */
  if ((MMC.CM & 0x0C) == 0x0C) {
    MMC.TX = VDP[45]&0x04? -PPB[SM]:PPB[SM];
    MMC.NX = ((VDP[40]+((int)VDP[41]<<8)) & 1023)/PPB[SM];
  }
  else {
    MMC.TX = VDP[45]&0x04? -1:1;
    MMC.NX = (VDP[40]+((int)VDP[41]<<8)) & 1023;
  }

  /* X loop variables are treated specially for LINE command */
  if (MMC.CM == CM_LINE) {
    MMC.ASX=((MMC.NX-1)>>1);
    MMC.ADX=0;
  }
  else {
    MMC.ASX = MMC.SX;
    MMC.ADX = MMC.DX;
  }    

  /* NX loop variable is treated specially for SRCH command */
  if (MMC.CM == CM_SRCH)
    MMC.ANX=(VDP[45]&0x02)!=0; /* Do we look for "==" or "!="? */
  else
    MMC.ANX = MMC.NX;

  /* Command execution started */
  VDPStatus[2]|=0x01;

  /* Start execution if we still have time slices */
  if(VdpEngine&&(VdpOpsCnt>0)) VdpEngine();

  /* Operation successfull initiated */
  return(1);
}

/** LoopVDP() ************************************************/
/** Run X steps of active VDP command                       **/
/*************************************************************/
void LoopVDP(void)
{
  if(VdpOpsCnt<=0)
  {
    VdpOpsCnt+=12500;
    if(VdpEngine&&(VdpOpsCnt>0)) VdpEngine();
  }
  else
  {
    VdpOpsCnt=12500;
    if(VdpEngine) VdpEngine();
  }
}

