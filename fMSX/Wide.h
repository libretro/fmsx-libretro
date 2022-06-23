/** fMSX: portable MSX emulator ******************************/
/**                                                         **/
/**                           Wide.h                        **/
/**                                                         **/
/** This file contains wide (512pix) screen refresh drivers **/
/** common for X11, VGA, and other "chunky" bitmapped video **/
/** implementations. It also includes dummy sound drivers   **/
/** for fMSX.                                               **/
/**                                                         **/
/** Copyright (C) Marat Fayzullin 1994-2021                 **/
/**     You are not allowed to distribute this software     **/
/**     commercially. Please, notify me, if you make any    **/
/**     changes to this file.                               **/
/*************************************************************/
#ifndef NARROW

/** ClearLine512() *******************************************/
/** Clear 512 pixels from P with color C.                   **/
/*************************************************************/
static void ClearLine512(uint16_t *P,uint16_t C)
{
  int J;

  for(J=0;J<512;J++) P[J]=C;
}

/** RefreshBorder512() ***************************************/
/** This function is called from RefreshLine#() to refresh  **/
/** the screen border. It returns a pointer to the start of **/
/** scanline Y in XBuf or 0 if scanline is beyond XBuf.     **/
/*************************************************************/
uint16_t *RefreshBorder512(uint8_t Y,uint16_t C)
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
  P=(uint16_t *)WBuf;

  if(HiResMode)
  {
    if(!Y)
      /* Paint top of the screen */
      for(L=FirstLine;L>0;L--)
      {
        for(A=((WIDTH*L)<<2)-1,H=WIDTH<<1;H>0;H--,A--)
        {
          if (InterlacedMode)
          {
            if (OddPage)
            { // even lines are black
              P[A]=0;
              P[A-(WIDTH<<1)]=C;
            }
            else
            { // odd lines are black
              P[A]=C;
              P[A-(WIDTH<<1)]=0;
            }
          }
          else // progressive: only touch this frame's lines
          {
            if (OddPage)
              P[A-(WIDTH<<1)]=C;
            else
              P[A]=C;
          }
        }
      }

    /* Start of the line */
    P+=(WIDTH*(FirstLine+Y))<<2;

    if (OddPage)
    {
      if (InterlacedMode)
        // erase previous frame's line
        for(A=(WIDTH<<2)-1,H=WIDTH<<1;H>0;H--,A--) P[A]=0;
    }
    else
    {
      if (InterlacedMode)
        // erase previous frame's line
        for(H=(WIDTH<<2)-1;H>=0;H--) P[H]=0;
      P+=WIDTH<<1; // interlace offset
    }
  }
  else // standard mode
  {
    /* Paint top of the screen */
    if(!Y) for(H=2*WIDTH*FirstLine-1;H>=0;H--) P[H]=C;

    /* Start of the line */
    P+=2*WIDTH*(FirstLine+Y);
  }

  /* Paint left/right borders */
  for(H=(WIDTH-256)+2*HAdjust;H>0;H--) P[H-1]=C;
  for(H=(WIDTH-256)-2*HAdjust;H>0;H--) P[2*WIDTH-H]=C;

  /* Paint bottom of the screen */
  H=ScanLines212? 212:192;

  if(HiResMode)
  {
    if(Y==H-1)
    {
      for(L=MAX_HEIGHT-Y;L>0;L--)
      {
        for(A=((WIDTH*L)<<2)-1,H=WIDTH<<1;H>0;H--,A--)
        {
          if (InterlacedMode)
            // erase previous frame's line
            P[A]=0;
          P[A+(WIDTH<<1)]=C;
        }
      }
    }
  }
  else // standard mode
    if(Y==H-1) for(H=2*WIDTH*(HEIGHT-H-FirstLine+1)-1;H>=2*WIDTH;H--) P[H]=C;

  /* Return pointer to the scanline in XBuf */
  return(P+WIDTH-256+2*HAdjust);
}

/** RefreshScr6() ********************************************/
/** Function to be called to update SCREEN 6.               **/
/*************************************************************/
void RefreshLine6(uint8_t Y)
{
  uint8_t X,*T,*R,C;
  uint8_t ZBuf[304];
  uint16_t *P=RefreshBorder512(Y,XPal[BGColor&0x03]);
  if(!P) return;

  if(!ScreenON) ClearLine512(P,XPal[BGColor&0x03]);
  else
  {
    LastScanline = Y + FirstLine;
    ColorSprites(Y,ZBuf);
    R=ZBuf+32;
    T=ChrTab+(((int)(Y+VScroll)<<7)&ChrTabM&0x7FFF);
    if (FlipEvenOdd && OddPage && VRAM<=T-0x8000) T-=0x8000;

    for(X=0;X<64;X++)
    {
      C=R[0];P[0]=XPal[C? C:T[0]>>6];
      C=R[0];P[1]=XPal[C? C:(T[0]>>4)&0x03];
      C=R[1];P[2]=XPal[C? C:(T[0]>>2)&0x03];
      C=R[1];P[3]=XPal[C? C:T[0]&0x03];
      C=R[2];P[4]=XPal[C? C:T[1]>>6];
      C=R[2];P[5]=XPal[C? C:(T[1]>>4)&0x03];
      C=R[3];P[6]=XPal[C? C:(T[1]>>2)&0x03];
      C=R[3];P[7]=XPal[C? C:T[1]&0x03];
      R+=4;P+=8;T+=2;
    }
  }
}
  
/** RefreshScr7() ********************************************/
/** Function to be called to update SCREEN 7.               **/
/*************************************************************/
void RefreshLine7(uint8_t Y)
{
  uint8_t C,X,*T,*R;
  uint8_t ZBuf[304];
  uint16_t *P=RefreshBorder512(Y,XPal[BGColor]);
  if(!P) return;

  if(!ScreenON) ClearLine512(P,XPal[BGColor]);
  else
  {
    LastScanline = Y + FirstLine;
    ColorSprites(Y,ZBuf);
    R=ZBuf+32;
    T=ChrTab+(((int)(Y+VScroll)<<8)&ChrTabM&0xFFFF);
    if (FlipEvenOdd && OddPage && VRAM<=T-0x10000) T-=0x10000;

    for(X=0;X<64;X++)
    {
      C=R[0];P[0]=XPal[C? C:T[0]>>4];
      C=R[0];P[1]=XPal[C? C:T[0]&0x0F];
      C=R[1];P[2]=XPal[C? C:T[1]>>4];
      C=R[1];P[3]=XPal[C? C:T[1]&0x0F];
      C=R[2];P[4]=XPal[C? C:T[2]>>4];
      C=R[2];P[5]=XPal[C? C:T[2]&0x0F];
      C=R[3];P[6]=XPal[C? C:T[3]>>4];
      C=R[3];P[7]=XPal[C? C:T[3]&0x0F];
      R+=4;P+=8;T+=4;
    }
  }
}

/** RefreshTx80() ********************************************/
/** Function to be called to update TEXT80.                 **/
/*************************************************************/
void RefreshLineTx80(uint8_t Y)
{
  uint16_t FC;
  uint8_t X,M,*T,*C,*G;
  uint16_t BC=XPal[BGColor];
  uint16_t *P=RefreshBorder512(Y,BC);
  if(!P) return;

  if(!ScreenON) ClearLine512(P,BC);
  else
  {
    LastScanline = Y + FirstLine;
    G=(FontBuf&&(Mode&MSX_FIXEDFONT)? FontBuf:ChrGen)+(Y&0x07);
    T=ChrTab+((80*(Y>>3))&ChrTabM);
    C=ColTab+((10*(Y>>3))&ColTabM);

    P[0]=P[1]=P[2]=P[3]=P[4]=P[5]=P[6]=P[7]=P[8]=
    P[9]=P[10]=P[11]=P[12]=P[13]=P[14]=P[15]=
    P[16]=P[17]=XPal[BGColor];
    P+=18;

    for(X=M=0;X<80;X++,T++,P+=6)
    {
      if(!(X&0x07)) M=*C++;
      if(M&0x80) { FC=XPal[XFGColor];BC=XPal[XBGColor]; }
      else       { FC=XPal[FGColor];BC=XPal[BGColor]; }
      M<<=1;
      Y=*(G+((int)*T<<3));
      P[0]=Y&0x80? FC:BC;
      P[1]=Y&0x40? FC:BC;
      P[2]=Y&0x20? FC:BC;
      P[3]=Y&0x10? FC:BC;
      P[4]=Y&0x08? FC:BC;
      P[5]=Y&0x04? FC:BC;
    }

    P[0]=P[1]=P[2]=P[3]=P[4]=P[5]=P[6]=P[7]=P[8]=
    P[9]=P[10]=P[11]=P[12]=P[13]=XPal[BGColor];
  }
}

#endif /* !NARROW */
