/** fMSX: portable MSX emulator ******************************/
/**                                                         **/
/**                         Common.h                        **/
/**                                                         **/
/** This file contains standard screen refresh drivers      **/
/** common for X11, VGA, and other "chunky" bitmapped video **/
/** implementations. It also includes dummy sound drivers   **/
/** for fMSX.                                               **/
/**                                                         **/
/** Copyright (C) Marat Fayzullin 1994-2021                 **/
/**     You are not allowed to distribute this software     **/
/**     commercially. Please, notify me, if you make any    **/
/**     changes to this file.                               **/
/*************************************************************/

static int FirstLine = 10 + BORDER;/* First scanline in XBuf */

static void  Sprites(uint8_t Y,uint16_t *Line);
static void  ColorSprites(uint8_t Y,uint8_t *ZBuf);
static uint16_t *RefreshBorder(uint8_t Y,uint16_t C);
static void  ClearLine(uint16_t *P,uint16_t C);
static uint16_t YJKColor(int Y,int J,int K);

extern void PutImage(void);

/** ClearLine() **********************************************/
/** Clear 256 uint16_ts from P with color C.                   **/
/*************************************************************/
static void ClearLine(uint16_t *P,uint16_t C)
{
  int J;

  for(J=0;J<256;J++) P[J]=C;
}

/** YJKColor() ***********************************************/
/** Given a color in YJK format, return the corresponding   **/
/** palette entry.                                          **/
/*************************************************************/
INLINE uint16_t YJKColor(int Y,int J,int K)
{
  // See http://map.grauw.nl/articles/yjk/
  // YJK566 (17 bits of information, 131072 values) translates to RGB555 (15 bits, 32768 values)
  int R=(Y+J)<<3;
  int G=(Y+K)<<3;
  int B=(5*Y-2*J-K)<<1;

  // .. but because of overlapping & clipping that results in 16384 + 2884 = 19268 distinct colours
  // (SCREEN10+11 YJK466, 16bits: 8192 + 4307 = 12499, plus 16 palette colours)
  R=R<0? 0:R>255? 255:R;
  G=G<0? 0:G>255? 255:G;
  B=B<0? 0:B>255? 255:B;

  return PIXEL(R,G,B);
}

/** RefreshBorder() ******************************************/
/** This function is called from RefreshLine#() to refresh  **/
/** the screen border. It returns a pointer to the start of **/
/** scanline Y in XBuf or 0 if scanline is beyond XBuf.     **/
/*************************************************************/
uint16_t *RefreshBorder(uint8_t Y,uint16_t C)
{
  uint16_t *P;
  int H,L,A;

  /* First line number in the buffer */
  if(!Y) FirstLine=(ScanLines212?0:10)+BORDER+VAdjust;

  /* Return 0 if we've run out of the screen buffer due to overscan */
  if(Y>(OverscanMode?MAX_SCANLINE:211)) return(0);

  /* Set up the transparent color */
  XPal[0]=(!BGColor||SolidColor0)? XPal0:XPal[BGColor];

  /* Start of the buffer */
  P=(uint16_t *)XBuf;

  if(HiResMode)
  {
    if(!Y)
      /* Paint top of the screen */
      for(L=FirstLine;L>0;L--)
      {
        for(A=((WIDTH*L)<<1)-1,H=WIDTH;H>0;H--,A--)
        {
          if (InterlacedMode)
          {
            if (OddPage)
            { // even lines are black
              P[A]=0;
              P[A-WIDTH]=C;
            }
            else
            { // odd lines are black
              P[A]=C;
              P[A-WIDTH]=0;
            }
          }
          else // progressive: only touch this frame's lines
          {
            if (OddPage)
              P[A-WIDTH]=C;
            else
              P[A]=C;
          }
        }
      }

    /* Start of the line */
    P+=(WIDTH*(FirstLine+Y))<<1;

    if (OddPage)
    {
      if (InterlacedMode)
        // erase previous frame's line
        for(A=(WIDTH<<1)-1,H=WIDTH;H>0;H--,A--) P[A]=0;
    }
    else
    {
      if (InterlacedMode)
        // erase previous frame's line
        for(H=WIDTH-1;H>=0;H--) P[H]=0;
      P+=WIDTH; // interlace offset
    }
  }
  else // standard mode
  {
    /* Paint top of the screen */
    if(!Y) for(H=WIDTH*FirstLine-1;H>=0;H--) P[H]=C;

    /* Start of the line */
    P+=WIDTH*(FirstLine+Y);
  }

  /* Paint left/right borders */
  for(H=(WIDTH-256)/2+HAdjust;H>0;H--) P[H-1]=C;
  for(H=(WIDTH-256)/2-HAdjust;H>0;H--) P[WIDTH-H]=C;

  /* Paint bottom of the screen */
  H=ScanLines212? 212:192;

  if(HiResMode)
  {
    if(Y==H-1)
      for(L=MAX_HEIGHT-Y;L>0;L--)
      {
        for(A=((WIDTH*L)<<1)-1,H=WIDTH;H>0;H--,A--)
        {
          if (InterlacedMode)
            // erase previous frame's line
            P[A]=0;
          P[A+WIDTH]=C;
        }
      }
  }
  else // standard mode
    if(Y==H-1) for(H=WIDTH*(HEIGHT-H-FirstLine+1)-1;H>=WIDTH;H--) P[H]=C;

  /* Return pointer to the scanline in XBuf */
  return(P+(WIDTH-256)/2+HAdjust);
}

/** Sprites() ************************************************/
/** This function is called from RefreshLine#() to refresh  **/
/** sprites in SCREENs 1-3.                                 **/
/*************************************************************/
void Sprites(uint8_t Y,uint16_t *Line)
{
  static const uint8_t SprHeights[4] = { 8,16,16,32 };
  uint16_t *P,C;
  uint8_t OH,IH,*PT,*AT;
  unsigned int M;
  int L,K;

  /* No extra sprites yet */
  VDPStatus[0]&=~0x5F;

  /* Assign initial values before counting */
  OH = SprHeights[VDP[1]&0x03];
  IH = SprHeights[VDP[1]&0x02];
  AT = SprTab-4;
  Y += VScroll;
  C  = MAXSPRITE1+1;
  M  = 0;

  /* Count displayed sprites */
  for(L=0;L<32;++L)
  {
    M<<=1;AT+=4;        /* Iterating through SprTab      */
    K=AT[0];            /* K = sprite Y coordinate       */
    if(K==208) break;   /* Iteration terminates if Y=208 */
    if(K>256-IH) K-=256; /* Y coordinate may be negative  */

    /* Mark all valid sprites with 1s, break at MAXSPRITE1 sprites */
    if((Y>K)&&(Y<=K+OH))
    {
      /* If we exceed the maximum number of sprites per line... */
      if(!--C)
      {
        /* Set 5thSprite flag in the VDP status register */
        VDPStatus[0]|=0x40;
        /* Stop drawing sprites, unless all-sprites option enabled */
        if(!OPTION(MSX_ALLSPRITE)) break;
      }

      /* Mark sprite as ready to draw */
      M|=1;
    }
  }

  /* Mark last checked sprite (5th in line, Y=208, or sprite #31) */
  VDPStatus[0]|=L<32? L:31;

  /* Draw all marked sprites */
  for(;M;M>>=1,AT-=4)
    if(M&1)
    {
      C=AT[3];                  /* C = sprite attributes */
      L=C&0x80? AT[1]-32:AT[1]; /* Sprite may be shifted left by 32 */
      C&=0x0F;                  /* C = sprite color */

      if((L<256)&&(L>-OH)&&C)
      {
        K=AT[0];                /* K = sprite Y coordinate */
        if(K>256-IH) K-=256;      /* Y coordinate may be negative */

        P=Line+L;
        K  = Y-K-1;
        PT = SprGen+((int)(IH>8? AT[2]&0xFC:AT[2])<<3)+(OH>IH? (K>>1):K);
        C=XPal[C];

        /* Mask 1: clip left sprite boundary */
        K=L>=0? 0xFFFF:(0x10000>>(OH>IH? (-L>>1):-L))-1;

        /* Mask 2: clip right sprite boundary */
        L+=(int)OH-257;
        if(L>=0)
        {
          L=(IH>8? 0x0002:0x0200)<<(OH>IH? (L>>1):L);
          K&=~(L-1);
        }

        /* Get and clip the sprite data */
        K&=((int)PT[0]<<8)|(IH>8? PT[16]:0x00);

        /* If output size is bigger than the input size... */
        if(OH>IH)
        {
          /* Big (zoomed) sprite */

          /* Draw left 16 uint16_ts of the sprite */
          if(K&0xFF00)
          {
            if(K&0x8000) P[1]=P[0]=C;
            if(K&0x4000) P[3]=P[2]=C;
            if(K&0x2000) P[5]=P[4]=C;
            if(K&0x1000) P[7]=P[6]=C;
            if(K&0x0800) P[9]=P[8]=C;
            if(K&0x0400) P[11]=P[10]=C;
            if(K&0x0200) P[13]=P[12]=C;
            if(K&0x0100) P[15]=P[14]=C;
          }

          /* Draw right 16 uint16_ts of the sprite */
          if(K&0x00FF)
          {
            if(K&0x0080) P[17]=P[16]=C;
            if(K&0x0040) P[19]=P[18]=C;
            if(K&0x0020) P[21]=P[20]=C;
            if(K&0x0010) P[23]=P[22]=C;
            if(K&0x0008) P[25]=P[24]=C;
            if(K&0x0004) P[27]=P[26]=C;
            if(K&0x0002) P[29]=P[28]=C;
            if(K&0x0001) P[31]=P[30]=C;
          }
        }
        else
        {
          /* Normal (unzoomed) sprite */

          /* Draw left 8 uint16_ts of the sprite */
          if(K&0xFF00)
          {
            if(K&0x8000) P[0]=C;
            if(K&0x4000) P[1]=C;
            if(K&0x2000) P[2]=C;
            if(K&0x1000) P[3]=C;
            if(K&0x0800) P[4]=C;
            if(K&0x0400) P[5]=C;
            if(K&0x0200) P[6]=C;
            if(K&0x0100) P[7]=C;
          }

          /* Draw right 8 uint16_ts of the sprite */
          if(K&0x00FF)
          {
            if(K&0x0080) P[8]=C;
            if(K&0x0040) P[9]=C;
            if(K&0x0020) P[10]=C;
            if(K&0x0010) P[11]=C;
            if(K&0x0008) P[12]=C;
            if(K&0x0004) P[13]=C;
            if(K&0x0002) P[14]=C;
            if(K&0x0001) P[15]=C;
          }
        }
      }
    }
}

/** ColorSprites() *******************************************/
/** This function is called from RefreshLine#() to refresh  **/
/** color sprites in SCREENs 4-8. The result is returned in **/
/** ZBuf, whose size must be 320 bytes (32+256+32).         **/
/*************************************************************/
void ColorSprites(uint8_t Y,uint8_t *ZBuf)
{
  static const uint8_t SprHeights[4] = { 8,16,16,32 };
  uint8_t C,IH,OH,J,OrThem;
  uint8_t *P,*PT,*AT;
  int L,K;
  unsigned int M;

  /* No extra sprites yet */
  VDPStatus[0]&=~0x5F;

  /* Clear ZBuffer and exit if sprites are off */
  memset(ZBuf+32,0,256);
  if(SpritesOFF) return;

  /* Assign initial values before counting */
  OrThem = 0x00;
  OH = SprHeights[VDP[1]&0x03];
  IH = SprHeights[VDP[1]&0x02];
  AT = SprTab-4;
  C  = MAXSPRITE2+1;
  M  = 0;

  /* Count displayed sprites */
  for(L=0;L<32;++L)
  {
    M<<=1;AT+=4;              /* Iterating through SprTab      */
    K=AT[0];                  /* Read Y from SprTab            */
    if(K==216) break;         /* Iteration terminates if Y=216 */
    K=(uint8_t)(K-VScroll);      /* Sprite's actual Y coordinate  */
    if(K>256-IH) K-=256;       /* Y coordinate may be negative  */

    /* Mark all valid sprites with 1s, break at MAXSPRITE2 sprites */
    if((Y>K)&&(Y<=K+OH))
    {
      /* If we exceed the maximum number of sprites per line... */
      if(!--C)
      {
        /* Set 9thSprite flag in the VDP status register */
        VDPStatus[0]|=0x40;
        /* Stop drawing sprites, unless all-sprites option enabled */
        if(!OPTION(MSX_ALLSPRITE)) break;
      }

      /* Mark sprite as ready to draw */
      M|=1;
    }
  }

  /* Mark last checked sprite (9th in line, Y=216, or sprite #31) */
  VDPStatus[0]|=L<32? L:31;

  /* Draw all marked sprites */
  for(;M;M>>=1,AT-=4)
    if(M&1)
    {
      K=(uint8_t)(AT[0]-VScroll); /* K = sprite Y coordinate */
      if(K>256-IH) K-=256;        /* Y coordinate may be negative */

      J=Y-K-1;
      J = OH>IH? (J>>1):J;
      C=SprTab[-0x0200+((AT-SprTab)<<2)+J];
      OrThem|=C&0x40;

      if(C&0x0F)
      {
        PT = SprGen+((int)(IH>8? AT[2]&0xFC:AT[2])<<3)+J;
        P=ZBuf+AT[1]+(C&0x80? 0:32);
        C&=0x0F;
        J=PT[0];

        if(OrThem&0x20)
        {
          if(OH>IH)
          {
            if(J&0x80) { P[0]|=C;P[1]|=C; }
            if(J&0x40) { P[2]|=C;P[3]|=C; }
            if(J&0x20) { P[4]|=C;P[5]|=C; }
            if(J&0x10) { P[6]|=C;P[7]|=C; }
            if(J&0x08) { P[8]|=C;P[9]|=C; }
            if(J&0x04) { P[10]|=C;P[11]|=C; }
            if(J&0x02) { P[12]|=C;P[13]|=C; }
            if(J&0x01) { P[14]|=C;P[15]|=C; }
            if(IH>8)
            {
              J=PT[16];
              if(J&0x80) { P[16]|=C;P[17]|=C; }
              if(J&0x40) { P[18]|=C;P[19]|=C; }
              if(J&0x20) { P[20]|=C;P[21]|=C; }
              if(J&0x10) { P[22]|=C;P[23]|=C; }
              if(J&0x08) { P[24]|=C;P[25]|=C; }
              if(J&0x04) { P[26]|=C;P[27]|=C; }
              if(J&0x02) { P[28]|=C;P[29]|=C; }
              if(J&0x01) { P[30]|=C;P[31]|=C; }
            }
          }
          else
          {
            if(J&0x80) P[0]|=C;
            if(J&0x40) P[1]|=C;
            if(J&0x20) P[2]|=C;
            if(J&0x10) P[3]|=C;
            if(J&0x08) P[4]|=C;
            if(J&0x04) P[5]|=C;
            if(J&0x02) P[6]|=C;
            if(J&0x01) P[7]|=C;
            if(IH>8)
            {
              J=PT[16];
              if(J&0x80) P[8]|=C;
              if(J&0x40) P[9]|=C;
              if(J&0x20) P[10]|=C;
              if(J&0x10) P[11]|=C;
              if(J&0x08) P[12]|=C;
              if(J&0x04) P[13]|=C;
              if(J&0x02) P[14]|=C;
              if(J&0x01) P[15]|=C;
            }
          }
        }
        else
        {
          if(OH>IH)
          {
            if(J&0x80) P[0]=P[1]=C;
            if(J&0x40) P[2]=P[3]=C;
            if(J&0x20) P[4]=P[5]=C;
            if(J&0x10) P[6]=P[7]=C;
            if(J&0x08) P[8]=P[9]=C;
            if(J&0x04) P[10]=P[11]=C;
            if(J&0x02) P[12]=P[13]=C;
            if(J&0x01) P[14]=P[15]=C;
            if(IH>8)
            {
              J=PT[16];
              if(J&0x80) P[16]=P[17]=C;
              if(J&0x40) P[18]=P[19]=C;
              if(J&0x20) P[20]=P[21]=C;
              if(J&0x10) P[22]=P[23]=C;
              if(J&0x08) P[24]=P[25]=C;
              if(J&0x04) P[26]=P[27]=C;
              if(J&0x02) P[28]=P[29]=C;
              if(J&0x01) P[30]=P[31]=C;
            }
          }
          else
          {
            if(J&0x80) P[0]=C;
            if(J&0x40) P[1]=C;
            if(J&0x20) P[2]=C;
            if(J&0x10) P[3]=C;
            if(J&0x08) P[4]=C;
            if(J&0x04) P[5]=C;
            if(J&0x02) P[6]=C;
            if(J&0x01) P[7]=C;
            if(IH>8)
            {
              J=PT[16];
              if(J&0x80) P[8]=C;
              if(J&0x40) P[9]=C;
              if(J&0x20) P[10]=C;
              if(J&0x10) P[11]=C;
              if(J&0x08) P[12]=C;
              if(J&0x04) P[13]=C;
              if(J&0x02) P[14]=C;
              if(J&0x01) P[15]=C;
            }
          }
        }
      }

      /* Update overlapping flag */
      OrThem>>=1;
    }
}

/** RefreshLineF() *******************************************/
/** Dummy refresh function called for non-existing screens. **/
/*************************************************************/
void RefreshLineF(uint8_t Y)
{
  uint16_t *P=RefreshBorder(Y,XPal[BGColor]);
  if(P) ClearLine(P,XPal[BGColor]);
}

/** RefreshLine0() *******************************************/
/** Refresh line Y (0..191/211) of SCREEN0.                 **/
/*************************************************************/
void RefreshLine0(uint8_t Y)
{
  uint16_t FC;
  uint8_t X,*T,*G;
  uint16_t BC=XPal[BGColor];
  uint16_t *P=RefreshBorder(Y,BC);
  if(!P) return;

  if(!ScreenON) ClearLine(P,BC);
  else
  {
    LastScanline = Y + FirstLine;
    P[0]=P[1]=P[2]=P[3]=P[4]=P[5]=P[6]=P[7]=P[8]=BC;

    G=(FontBuf&&(Mode&MSX_FIXEDFONT)? FontBuf:ChrGen)+((Y+VScroll)&0x07);
    T=ChrTab+40*(Y>>3);
    FC=XPal[FGColor];
    P+=9;

    for(X=0;X<40;X++,T++,P+=6)
    {
      Y=G[(int)*T<<3];
      P[0]=Y&0x80? FC:BC;P[1]=Y&0x40? FC:BC;
      P[2]=Y&0x20? FC:BC;P[3]=Y&0x10? FC:BC;
      P[4]=Y&0x08? FC:BC;P[5]=Y&0x04? FC:BC;
    }

    P[0]=P[1]=P[2]=P[3]=P[4]=P[5]=P[6]=BC;
  }
}

/** RefreshLine1() *******************************************/
/** Refresh line Y (0..191/211) of SCREEN1, including       **/
/** sprites in this line.                                   **/
/*************************************************************/
void RefreshLine1(uint8_t Y)
{
  uint16_t FC,BC;
  uint8_t K,X,*T,*G;
  uint16_t *P=RefreshBorder(Y,XPal[BGColor]);
  if(!P)
     return;

  if(!ScreenON)
     ClearLine(P,XPal[BGColor]);
  else
  {
    LastScanline = Y + FirstLine;
    Y+=VScroll;
    G=(FontBuf&&(Mode&MSX_FIXEDFONT)? FontBuf:ChrGen)+(Y&0x07);
    T=ChrTab+((int)(Y&0xF8)<<2);

    for(X=0;X<32;X++,T++,P+=8)
    {
      K=ColTab[*T>>3];
      FC=XPal[K>>4];
      BC=XPal[K&0x0F];
      K=G[(int)*T<<3];
      P[0]=K&0x80? FC:BC;P[1]=K&0x40? FC:BC;
      P[2]=K&0x20? FC:BC;P[3]=K&0x10? FC:BC;
      P[4]=K&0x08? FC:BC;P[5]=K&0x04? FC:BC;
      P[6]=K&0x02? FC:BC;P[7]=K&0x01? FC:BC;
    }

    if(!SpritesOFF) Sprites(Y,P-256);
  }
}

/** RefreshLine2() *******************************************/
/** Refresh line Y (0..191/211) of SCREEN2, including       **/
/** sprites in this line.                                   **/
/*************************************************************/
void RefreshLine2(uint8_t Y)
{
  uint16_t FC,BC;
  uint8_t K,X,*T;
  int I,J;
  uint16_t *P=RefreshBorder(Y,XPal[BGColor]);
  if(!P) return;

  if(!ScreenON) ClearLine(P,XPal[BGColor]);
  else
  {
    LastScanline = Y + FirstLine;
    Y+=VScroll;
    T=ChrTab+((int)(Y&0xF8)<<2);
    I=((int)(Y&0xC0)<<5)+(Y&0x07);

    for(X=0;X<32;X++,T++,P+=8)
    {
      J=(int)*T<<3;
      K=ColTab[(I+J)&ColTabM];
      FC=XPal[K>>4];
      BC=XPal[K&0x0F];
      K=ChrGen[(I+J)&ChrGenM];
      P[0]=K&0x80? FC:BC;P[1]=K&0x40? FC:BC;
      P[2]=K&0x20? FC:BC;P[3]=K&0x10? FC:BC;
      P[4]=K&0x08? FC:BC;P[5]=K&0x04? FC:BC;
      P[6]=K&0x02? FC:BC;P[7]=K&0x01? FC:BC;
    }

    if(!SpritesOFF) Sprites(Y,P-256);
  }
}

/** RefreshLine3() *******************************************/
/** Refresh line Y (0..191/211) of SCREEN3, including       **/
/** sprites in this line.                                   **/
/*************************************************************/
void RefreshLine3(uint8_t Y)
{
  uint8_t X,K,*T,*G;
  uint16_t *P=RefreshBorder(Y,XPal[BGColor]);
  if(!P) return;

  if(!ScreenON) ClearLine(P,XPal[BGColor]);
  else
  {
    LastScanline = Y + FirstLine;
    Y+=VScroll;
    T=ChrTab+((int)(Y&0xF8)<<2);
    G=ChrGen+((Y&0x1C)>>2);

    for(X=0;X<32;X++,T++,P+=8)
    {
      K=G[(int)*T<<3];
      P[0]=P[1]=P[2]=P[3]=XPal[K>>4];
      P[4]=P[5]=P[6]=P[7]=XPal[K&0x0F];
    }

    if(!SpritesOFF) Sprites(Y,P-256);
  }
}

/** RefreshLine4() *******************************************/
/** Refresh line Y (0..191/211) of SCREEN4, including       **/
/** sprites in this line.                                   **/
/*************************************************************/
void RefreshLine4(uint8_t Y)
{
  uint16_t FC,BC;
  uint8_t K,X,C,*T,*R;
  int I,J;
  uint8_t ZBuf[320];
  uint16_t *P=RefreshBorder(Y,XPal[BGColor]);
  if(!P) return;

  if(!ScreenON) ClearLine(P,XPal[BGColor]);
  else
  {
    LastScanline = Y + FirstLine;
    ColorSprites(Y,ZBuf);
    R=ZBuf+32;
    Y+=VScroll;
    T=ChrTab+((int)(Y&0xF8)<<2);
    I=((int)(Y&0xC0)<<5)+(Y&0x07);

    for(X=0;X<32;X++,R+=8,P+=8,T++)
    {
      J=(int)*T<<3;
      K=ColTab[(I+J)&ColTabM];
      FC=XPal[K>>4];
      BC=XPal[K&0x0F];
      K=ChrGen[(I+J)&ChrGenM];

      C=R[0];P[0]=C? XPal[C]:(K&0x80)? FC:BC;
      C=R[1];P[1]=C? XPal[C]:(K&0x40)? FC:BC;
      C=R[2];P[2]=C? XPal[C]:(K&0x20)? FC:BC;
      C=R[3];P[3]=C? XPal[C]:(K&0x10)? FC:BC;
      C=R[4];P[4]=C? XPal[C]:(K&0x08)? FC:BC;
      C=R[5];P[5]=C? XPal[C]:(K&0x04)? FC:BC;
      C=R[6];P[6]=C? XPal[C]:(K&0x02)? FC:BC;
      C=R[7];P[7]=C? XPal[C]:(K&0x01)? FC:BC;
    }
  }
}

/** RefreshLine5() *******************************************/
/** Refresh line Y (0..191/211) of SCREEN5, including       **/
/** sprites in this line.                                   **/
/*************************************************************/
void RefreshLine5(uint8_t Y)
{
  uint8_t I,X,*T,*R;
  uint8_t ZBuf[320];
  uint16_t *P=RefreshBorder(Y,XPal[BGColor]);
  if(!P) return;

  if(!ScreenON) ClearLine(P,XPal[BGColor]);
  else
  {
    LastScanline = Y + FirstLine;
    ColorSprites(Y,ZBuf);
    R=ZBuf+32;
    T=ChrTab+(((int)(Y+VScroll)<<7)&ChrTabM&0x7FFF);
    if (FlipEvenOdd && OddPage && VRAM<=T-0x8000) T-=0x8000;

    for(X=0;X<16;X++,R+=16,P+=16,T+=8)
    {
      I=R[0];P[0]=XPal[I? I:T[0]>>4];
      I=R[1];P[1]=XPal[I? I:T[0]&0x0F];
      I=R[2];P[2]=XPal[I? I:T[1]>>4];
      I=R[3];P[3]=XPal[I? I:T[1]&0x0F];
      I=R[4];P[4]=XPal[I? I:T[2]>>4];
      I=R[5];P[5]=XPal[I? I:T[2]&0x0F];
      I=R[6];P[6]=XPal[I? I:T[3]>>4];
      I=R[7];P[7]=XPal[I? I:T[3]&0x0F];
      I=R[8];P[8]=XPal[I? I:T[4]>>4];
      I=R[9];P[9]=XPal[I? I:T[4]&0x0F];
      I=R[10];P[10]=XPal[I? I:T[5]>>4];
      I=R[11];P[11]=XPal[I? I:T[5]&0x0F];
      I=R[12];P[12]=XPal[I? I:T[6]>>4];
      I=R[13];P[13]=XPal[I? I:T[6]&0x0F];
      I=R[14];P[14]=XPal[I? I:T[7]>>4];
      I=R[15];P[15]=XPal[I? I:T[7]&0x0F];
    }
  }
}

/** RefreshLine8() *******************************************/
/** Refresh line Y (0..191/211) of SCREEN8, including       **/
/** sprites in this line.                                   **/
/*************************************************************/
void RefreshLine8(uint8_t Y)
{
  static uint8_t SprToScr[16] =
  {
    0x00,0x02,0x10,0x12,0x80,0x82,0x90,0x92,
    0x49,0x4B,0x59,0x5B,0xC9,0xCB,0xD9,0xDB
  };
  uint8_t C,X,*T,*R;
  uint8_t ZBuf[320];
  uint16_t *P=RefreshBorder(Y,BPal[VDP[7]]);
  if(!P) return;

  if(!ScreenON) ClearLine(P,BPal[VDP[7]]);
  else
  {
    LastScanline = Y + FirstLine;
    ColorSprites(Y,ZBuf);
    R=ZBuf+32;
    T=ChrTab+(((int)(Y+VScroll)<<8)&ChrTabM&0xFFFF);
    if (FlipEvenOdd && OddPage && VRAM<=T-0x10000) T-=0x10000;

    for(X=0;X<32;X++,T+=8,R+=8,P+=8)
    {
      C=R[0];P[0]=BPal[C? SprToScr[C]:T[0]];
      C=R[1];P[1]=BPal[C? SprToScr[C]:T[1]];
      C=R[2];P[2]=BPal[C? SprToScr[C]:T[2]];
      C=R[3];P[3]=BPal[C? SprToScr[C]:T[3]];
      C=R[4];P[4]=BPal[C? SprToScr[C]:T[4]];
      C=R[5];P[5]=BPal[C? SprToScr[C]:T[5]];
      C=R[6];P[6]=BPal[C? SprToScr[C]:T[6]];
      C=R[7];P[7]=BPal[C? SprToScr[C]:T[7]];
    }
  }
}

/** RefreshLine10() ******************************************/
/** Refresh line Y (0..191/211) of SCREEN10/11, including   **/
/** sprites in this line.                                   **/
/*************************************************************/
void RefreshLine10(uint8_t Y)
{
  uint8_t C,X,*T,*R;
  int J,K;
  uint8_t ZBuf[320];
  uint16_t *P=RefreshBorder(Y,BPal[VDP[7]]);
  if(!P) return;

  if(!ScreenON) ClearLine(P,BPal[VDP[7]]);
  else
  {
    LastScanline = Y + FirstLine;
    ColorSprites(Y,ZBuf);
    R=ZBuf+32;
    T=ChrTab+(((int)(Y+VScroll)<<8)&ChrTabM&0xFFFF);
    if (FlipEvenOdd && OddPage && VRAM<=T-0x10000) T-=0x10000;

    /* Draw first 4 uint16_ts */
    C=R[0];P[0]=C? XPal[C]:BPal[VDP[7]];
    C=R[1];P[1]=C? XPal[C]:BPal[VDP[7]];
    C=R[2];P[2]=C? XPal[C]:BPal[VDP[7]];
    C=R[3];P[3]=C? XPal[C]:BPal[VDP[7]];
    R+=4;P+=4;

    for(X=0;X<63;X++,T+=4,R+=4,P+=4)
    {
      K=(T[0]&0x07)|((T[1]&0x07)<<3);
      if(K&0x20) K-=64;
      J=(T[2]&0x07)|((T[3]&0x07)<<3);
      if(J&0x20) J-=64;

      C=R[0];Y=T[0]>>3;P[0]=C? XPal[C]:Y&1? XPal[Y>>1]:YJKColor(Y,J,K);
      C=R[1];Y=T[1]>>3;P[1]=C? XPal[C]:Y&1? XPal[Y>>1]:YJKColor(Y,J,K);
      C=R[2];Y=T[2]>>3;P[2]=C? XPal[C]:Y&1? XPal[Y>>1]:YJKColor(Y,J,K);
      C=R[3];Y=T[3]>>3;P[3]=C? XPal[C]:Y&1? XPal[Y>>1]:YJKColor(Y,J,K);
    }
  }
}

/** RefreshLine12() ******************************************/
/** Refresh line Y (0..191/211) of SCREEN12, including      **/
/** sprites in this line.                                   **/
/*************************************************************/
void RefreshLine12(uint8_t Y)
{
  uint8_t C,X,*T,*R;
  int J,K;
  uint8_t ZBuf[320];
  uint16_t *P=RefreshBorder(Y,BPal[VDP[7]]);
  if(!P) return;

  if(!ScreenON) ClearLine(P,BPal[VDP[7]]);
  else
  {
    LastScanline = Y + FirstLine;
    ColorSprites(Y,ZBuf);
    R = ZBuf+32;
    T = ChrTab
      + (((int)(Y+VScroll)<<8)&ChrTabM&0xFFFF)
      + (HScroll512&&(HScroll>255)? 0x10000:0)
      + (HScroll&0xFC);
    if (FlipEvenOdd && OddPage && VRAM<=T-0x10000) T-=0x10000;

    /* Draw first 4 uint16_ts */
    C=R[0];P[0]=C? XPal[C]:BPal[VDP[7]];
    C=R[1];P[1]=C? XPal[C]:BPal[VDP[7]];
    C=R[2];P[2]=C? XPal[C]:BPal[VDP[7]];
    C=R[3];P[3]=C? XPal[C]:BPal[VDP[7]];
    R+=4;P+=4;

    for(X=1;X<64;X++,T+=4,R+=4,P+=4)
    {
      K=(T[0]&0x07)|((T[1]&0x07)<<3);
      if(K&0x20) K-=64;
      J=(T[2]&0x07)|((T[3]&0x07)<<3);
      if(J&0x20) J-=64;

      C=R[0];P[0]=C? XPal[C]:YJKColor(T[0]>>3,J,K);
      C=R[1];P[1]=C? XPal[C]:YJKColor(T[1]>>3,J,K);
      C=R[2];P[2]=C? XPal[C]:YJKColor(T[2]>>3,J,K);
      C=R[3];P[3]=C? XPal[C]:YJKColor(T[3]>>3,J,K);
    }
  }
}

#ifdef NARROW

/** RefreshLine6() *******************************************/
/** Refresh line Y (0..191/211) of SCREEN6, including       **/
/** sprites in this line.                                   **/
/*************************************************************/
void RefreshLine6(uint8_t Y)
{
  uint8_t X,*T,*R,C;
  uint8_t ZBuf[320];
  uint16_t *P=RefreshBorder(Y,XPal[BGColor&0x03]);
  if(!P) return;

  if(!ScreenON) ClearLine(P,XPal[BGColor&0x03]);
  else
  {
    LastScanline = Y + FirstLine;
    ColorSprites(Y,ZBuf);
    R=ZBuf+32;
    T=ChrTab+(((int)(Y+VScroll)<<7)&ChrTabM&0x7FFF);
    if (FlipEvenOdd && OddPage && VRAM<=T-0x8000) T-=0x8000;

    for(X=0;X<32;X++)
    {
      C=R[0];P[0]=XPal[C? C:T[0]>>6];
      C=R[1];P[1]=XPal[C? C:(T[0]>>2)&0x03];
      C=R[2];P[2]=XPal[C? C:T[1]>>6];
      C=R[3];P[3]=XPal[C? C:(T[1]>>2)&0x03];
      C=R[4];P[4]=XPal[C? C:T[2]>>6];
      C=R[5];P[5]=XPal[C? C:(T[2]>>2)&0x03];
      C=R[6];P[6]=XPal[C? C:T[3]>>6];
      C=R[7];P[7]=XPal[C? C:(T[3]>>2)&0x03];
      R+=8;P+=8;T+=4;
    }
  }
}
  
/** RefreshLine7() *******************************************/
/** Refresh line Y (0..191/211) of SCREEN7, including       **/
/** sprites in this line.                                   **/
/*************************************************************/
void RefreshLine7(uint8_t Y)
{
  uint8_t C,X,*T,*R;
  uint8_t ZBuf[320];
  uint16_t *P=RefreshBorder(Y,XPal[BGColor]);
  if(!P) return;

  if(!ScreenON) ClearLine(P,XPal[BGColor]);
  else
  {
    LastScanline = Y + FirstLine;
    ColorSprites(Y,ZBuf);
    R=ZBuf+32;
    T=ChrTab+(((int)(Y+VScroll)<<8)&ChrTabM&0xFFFF);
    if (FlipEvenOdd && OddPage && VRAM<=T-0x10000) T-=0x10000;

    for(X=0;X<32;X++)
    {
      C=R[0];P[0]=XPal[C? C:T[0]>>4];
      C=R[1];P[1]=XPal[C? C:T[1]>>4];
      C=R[2];P[2]=XPal[C? C:T[2]>>4];
      C=R[3];P[3]=XPal[C? C:T[3]>>4];
      C=R[4];P[4]=XPal[C? C:T[4]>>4];
      C=R[5];P[5]=XPal[C? C:T[5]>>4];
      C=R[6];P[6]=XPal[C? C:T[6]>>4];
      C=R[7];P[7]=XPal[C? C:T[7]>>4];
      R+=8;P+=8;T+=8;
    }
  }
}

/** RefreshLineTx80() ****************************************/
/** Refresh line Y (0..191/211) of TEXT80.                  **/
/*************************************************************/
void RefreshLineTx80(uint8_t Y)
{
  uint16_t FC;
  uint8_t X,M,*T,*C,*G;
  uint16_t BC=XPal[BGColor];
  uint16_t *P=RefreshBorder(Y,BC);
  if(!P) return;

  if(!ScreenON) ClearLine(P,BC);
  else
  {
    LastScanline = Y + FirstLine;
    P[0]=P[1]=P[2]=P[3]=P[4]=P[5]=P[6]=P[7]=P[8]=BC;
    G=(FontBuf&&(Mode&MSX_FIXEDFONT)? FontBuf:ChrGen)+((Y+VScroll)&0x07);
    T=ChrTab+((80*(Y>>3))&ChrTabM);
    C=ColTab+((10*(Y>>3))&ColTabM);
    P+=9;

    for(X=0,M=0x00;X<80;X++,T++,P+=3)
    {
      if(!(X&0x07)) M=*C++;
      if(M&0x80) { FC=XPal[XFGColor];BC=XPal[XBGColor]; }
      else       { FC=XPal[FGColor];BC=XPal[BGColor]; }
      M<<=1;
      Y=*(G+((int)*T<<3));
      P[0]=Y&0xC0? FC:BC;
      P[1]=Y&0x30? FC:BC;
      P[2]=Y&0x0C? FC:BC;
    }

    P[0]=P[1]=P[2]=P[3]=P[4]=P[5]=P[6]=XPal[BGColor];
  }
}

#endif /* NARROW */
