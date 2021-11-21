/** EMULib Emulation Library *********************************/
/**                                                         **/
/**                          Sound.c                        **/
/**                                                         **/
/** This file file implements core part of the sound API.   **/
/** See Sound.h for declarations.                           **/
/**                                                         **/
/** Copyright (C) Marat Fayzullin 1996-2018                 **/
/**     You are not allowed to distribute this software     **/
/**     commercially. Please, notify me, if you make any    **/
/**     changes to this file.                               **/
/*************************************************************/
#include "Sound.h"

#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#else
#include <unistd.h>
#endif

typedef unsigned char byte;
typedef unsigned short word;

static struct
{
  int Type;                       /* Channel type (SND_*)             */
  int Freq;                       /* Channel frequency (Hz)           */
  int Volume;                     /* Channel volume (0..255)          */

  const signed char *Data;        /* Wave data (-128..127 each)       */
  int Length;                     /* Wave length in Data              */
  int Rate;                       /* Wave playback rate (or 0Hz)      */
  int Pos;                        /* Wave current position in Data    */  

  int Count;                      /* Phase counter                    */
} WaveCH[SND_CHANNELS] =
{
  { SND_MELODIC,0,0,0,0,0,0,0 },
  { SND_MELODIC,0,0,0,0,0,0,0 },
  { SND_MELODIC,0,0,0,0,0,0,0 },
  { SND_MELODIC,0,0,0,0,0,0,0 },
  { SND_MELODIC,0,0,0,0,0,0,0 },
  { SND_MELODIC,0,0,0,0,0,0,0 },
  { SND_MELODIC,0,0,0,0,0,0,0 },
  { SND_MELODIC,0,0,0,0,0,0,0 },
  { SND_MELODIC,0,0,0,0,0,0,0 },
  { SND_MELODIC,0,0,0,0,0,0,0 },
  { SND_MELODIC,0,0,0,0,0,0,0 },
  { SND_MELODIC,0,0,0,0,0,0,0 },
  { SND_MELODIC,0,0,0,0,0,0,0 },
  { SND_MELODIC,0,0,0,0,0,0,0 },
  { SND_MELODIC,0,0,0,0,0,0,0 },
  { SND_MELODIC,0,0,0,0,0,0,0 }
};

/** RenderAudio() Variables *******************************************/
static int SndRate    = 0;        /* Sound rate (0=Off)               */
static int NoiseGen   = 0x10000;  /* Noise generator seed             */
static int NoiseOut   = 16;       /* NoiseGen bit used for output     */
static int NoiseXor   = 14;       /* NoiseGen bit used for XORing     */
int MasterSwitch      = 0xFFFF;   /* Switches to turn channels on/off */
int MasterVolume      = 192;      /* Master volume                    */

/** GetSndRate() *********************************************/
/** Get current sampling rate used for synthesis.           **/
/*************************************************************/
unsigned int GetSndRate(void) { return(SndRate); }

/** Sound() **************************************************/
/** Generate sound of given frequency (Hz) and volume       **/
/** (0..255) via given channel. Setting Freq=0 or Volume=0  **/
/** turns sound off.                                        **/
/*************************************************************/
void Sound(int Channel,int Freq,int Volume)
{
  /* All parameters have to be valid */
  if((Channel<0)||(Channel>=SND_CHANNELS)) return;
  Freq   = Freq<0? 0:Freq;
  Volume = Volume<0? 0:Volume>255? 255:Volume;

  /* Modify channel parameters */ 
  WaveCH[Channel].Volume = Volume;
  WaveCH[Channel].Freq   = Freq;

  /* When disabling sound, reset waveform */
  if(!Freq||!Volume)
  {
    WaveCH[Channel].Pos    = 0;
    WaveCH[Channel].Count  = 0;
  }
}

/** SetSound() ***********************************************/
/** Set sound type at a given channel.                      **/
/*************************************************************/
void SetSound(int Channel,int Type)
{
  /* Channel has to be valid */
  if((Channel<0)||(Channel>=SND_CHANNELS)) return;

  /* Set wave channel type */
  WaveCH[Channel].Type = Type;
}

/** SetChannels() ********************************************/
/** Set master volume (0..255) and switch channels on/off.  **/
/** Each channel N has corresponding bit 2^N in Switch. Set **/
/** or reset this bit to turn the channel on or off.        **/ 
/*************************************************************/
void SetChannels(int Volume,int Switch)
{
  /* Volume has to be valid */
  Volume = Volume<0? 0:Volume>255? 255:Volume;

  /* Modify wave master settings */ 
  MasterVolume = Volume;
  MasterSwitch = Switch&((1<<SND_CHANNELS)-1);
}

/** SetNoise() ***********************************************/
/** Initialize random noise generator to the given Seed and **/
/** then take random output from OUTBit and XOR it with     **/
/** XORBit.                                                 **/
/*************************************************************/
void SetNoise(int Seed,int OUTBit,int XORBit)
{
  NoiseGen = Seed;
  NoiseOut = OUTBit;
  NoiseXor = XORBit;
}

/** SetWave() ************************************************/
/** Set waveform for a given channel. The channel will be   **/
/** marked with sound type SND_WAVE. Set Rate=0 if you want **/
/** waveform to be an instrument or set it to the waveform  **/
/** own playback rate.                                      **/
/*************************************************************/
void SetWave(int Channel,const signed char *Data,int Length,int Rate)
{
  unsigned int J;

  /* Channel and waveform length have to be valid */
  if((Channel<0)||(Channel>=SND_CHANNELS)||(Length<=0)) return;

  /* Set wave channel parameters */
  WaveCH[Channel].Type   = SND_WAVE;
  WaveCH[Channel].Length = Length;
  WaveCH[Channel].Rate   = Rate;
  WaveCH[Channel].Pos    = Length? WaveCH[Channel].Pos%Length:0;
  WaveCH[Channel].Count  = 0;
  WaveCH[Channel].Data   = Data;
}

/** GetWave() ************************************************/
/** Get current read position for the buffer set with the   **/
/** SetWave() call. Returns 0 if no buffer has been set, or **/
/** if there is no playrate set (i.e. wave is instrument).  **/
/*************************************************************/
const signed char *GetWave(int Channel)
{
  /* Channel has to be valid */
  if((Channel<0)||(Channel>=SND_CHANNELS)) return(0);

  /* Return current read position */
  return(
    WaveCH[Channel].Rate&&(WaveCH[Channel].Type==SND_WAVE)?
    WaveCH[Channel].Data+WaveCH[Channel].Pos:0
  );
}

/** InitSound() **********************************************/
/** Initialize RenderSound() with given parameters.         **/
/*************************************************************/
unsigned int InitSound(unsigned int Rate,unsigned int Latency)
{
  int I;

  /* Shut down current sound */
  TrashSound();

  /* Initialize internal variables (keeping MasterVolume/MasterSwitch!) */
  SndRate  = 0;

  /* Reset sound parameters */
  for(I=0;I<SND_CHANNELS;I++)
  {
    /* NOTICE: Preserving Type value! */
    WaveCH[I].Count  = 0;
    WaveCH[I].Volume = 0;
    WaveCH[I].Freq   = 0;
  }

  /* Rate=0 means silence */
  if(!Rate) { SndRate=0;return(0); }

  /* Done */
  SetChannels(MasterVolume,MasterSwitch);
  return(SndRate=Rate);
}

/** TrashSound() *********************************************/
/** Shut down RenderSound() driver.                         **/
/*************************************************************/
void TrashSound(void)
{
  /* Sound is now off */
  SndRate = 0;
}

/** RenderAudio() ********************************************/
/** Render given number of melodic sound samples into an    **/
/** integer buffer for mixing.                              **/
/*************************************************************/
void RenderAudio(int *Wave,unsigned int Samples)
{
  int J,K,I,L1,L2,V,A1;
#ifdef WAVE_INTERPOLATION
  /* Keep GCC happy about variable initialization */
  int A2 = 0;
  int L  = 0;
  int N  = 0;
#endif

  /* Exit if wave sound not initialized */
  if(SndRate<8192) return;

  /* Waveform generator */
  for(J=0;J<SND_CHANNELS;J++)
    if(WaveCH[J].Freq&&(V=WaveCH[J].Volume)&&(MasterSwitch&(1<<J)))
      switch(WaveCH[J].Type)
      {
        case SND_WAVE: /* Custom Waveform */
          /* Waveform data must have correct length! */
          if(WaveCH[J].Length<=0) break;
          /* Start counting */
          K  = WaveCH[J].Rate>0?
               (SndRate<<15)/WaveCH[J].Freq/WaveCH[J].Rate
             : (SndRate<<15)/WaveCH[J].Freq/WaveCH[J].Length;
          L1 = WaveCH[J].Pos%WaveCH[J].Length;
          L2 = WaveCH[J].Count;
          A1 = WaveCH[J].Data[L1]*V;
#if !defined(WAVE_INTERPOLATION)
          /* Add waveform to the buffer */
          for(I=0;I<Samples;I++)
          {
            /* If next step... */
            if(L2>=K)
            {
              L1 = (L1+L2/K)%WaveCH[J].Length;
              A1 = WaveCH[J].Data[L1]*V;
              L2 = L2%K;
            }
            /* Output waveform */
            Wave[I]+=A1;
            /* Next waveform step */
            L2+=0x8000;
          }
#else /* WAVE_INTERPOLATION */
          /* If expecting interpolation... */
          if(L2<K)
          {
            /* Compute interpolation parameters */
            A2 = WaveCH[J].Data[(L1+1)%WaveCH[J].Length]*V;
            L  = (L2>>15)+1;
            N  = ((K-(L2&0x7FFF))>>15)+1;
          }
          /* Add waveform to the buffer */
          for(I=0;I<Samples;I++)
            if(L2<K)
            {
              /* Interpolate linearly */
              Wave[I]+=A1+L*(A2-A1)/N;
              /* Next waveform step */
              L2+=0x8000;
              /* Next interpolation step */
              L++;
            }
            else
            {
              L1 = (L1+L2/K)%WaveCH[J].Length;
              L2 = (L2%K)+0x8000;
              A1 = WaveCH[J].Data[L1]*V;
              Wave[I]+=A1;
              /* If expecting interpolation... */
              if(L2<K)
              {
                /* Compute interpolation parameters */
                A2 = WaveCH[J].Data[(L1+1)%WaveCH[J].Length]*V;
                L  = 1;
                N  = ((K-L2)>>15)+1;
              }
            }
#endif /* WAVE_INTERPOLATION */
          /* End counting */
          WaveCH[J].Pos   = L1;
          WaveCH[J].Count = L2;
          break;

        case SND_NOISE: /* White Noise */
          /* For high frequencies, recompute volume */
          if(WaveCH[J].Freq<SndRate)
            K=((unsigned int)WaveCH[J].Freq<<16)/SndRate;
          else
          {
            V = V*SndRate/WaveCH[J].Freq;
            K = 0x10000;
          }
          L1=WaveCH[J].Count;
          for(I=0;I<Samples;I++)
          {
            /* Use NoiseOut bit for output */
            Wave[I]+=((NoiseGen>>NoiseOut)&1? 127:-128)*V;
            L1+=K;
            if(L1&0xFFFF0000)
            {
              /* XOR NoiseOut and NoiseXOR bits and feed them back */
              NoiseGen=
                (((NoiseGen>>NoiseOut)^(NoiseGen>>NoiseXor))&1)
              | ((NoiseGen<<1)&((2<<NoiseOut)-1));
              L1&=0xFFFF;
            }
          }
          WaveCH[J].Count=L1;
          break;

        case SND_MELODIC:  /* Melodic Sound   */
        case SND_TRIANGLE: /* Triangular Wave */
        default:           /* Default Sound   */
          /* Do not allow frequencies that are too high */
          if(WaveCH[J].Freq>=SndRate/2) break;
          K=0x10000*WaveCH[J].Freq/SndRate;
          L1=WaveCH[J].Count;
#if !defined(SLOW_MELODIC_AUDIO)
          for(I=0;I<Samples;I++,L1+=K)
            Wave[I]+=((L1-K)^(L1+K))&0x8000? 0:(L1&0x8000? 127:-128)*V;
#else /* SLOW_MELODIC_AUDIO */
          for(I=0;I<Samples;I++,L1+=K)
          {
            L2 = L1+K;
            A1 = L1&0x8000? 127:-128;
            if((L1^L2)&0x8000)
              A1=A1*(0x8000-(L1&0x7FFF)-(L2&0x7FFF))/K;
            Wave[I]+=A1*V;
          }
#endif /* SLOW_MELODIC_AUDIO */
          WaveCH[J].Count=L1&0xFFFF;
          break;
      }
}

/** PlayAudio() **********************************************/
/** Normalize and play given number of samples from the mix **/
/** buffer. Returns the number of samples actually played.  **/
/*************************************************************/
unsigned int PlayAudio(int *Wave,unsigned int Samples)
{
  sample Buf[256];
  unsigned int I,J,K;
  int D;

  /* Exit if wave sound not initialized */
  if(SndRate<8192) return(0);

  /* Check if the buffer contains enough free space */
  J = GetFreeAudio();
  if(J<Samples) Samples=J;

  /* Spin until all samples played or WriteAudio() fails */
  for(K=I=J=0;(K<Samples)&&(I==J);K+=I)
  {
    /* Compute number of samples to convert */
    J = sizeof(Buf)/sizeof(sample);
    J = Samples-K>J? J:Samples-K;

    /* Convert samples */
    for(I=0;I<J;++I)
    {
      D      = ((*Wave++)*MasterVolume)>>8;
      D      = D>32767? 32767:D<-32768? -32768:D;
#if defined(BPU16)
      Buf[I] = D+32768;
#elif defined(BPS16)
      Buf[I] = D;
#elif defined(BPU8)
      Buf[I] = (D>>8)+128;
#else
      Buf[I] = D>>8;
#endif
    }

    /* Play samples */
    I = WriteAudio(Buf,J);
  }

  /* Return number of samples played */
  return(K);
}

/** RenderAndPlayAudio() *************************************/
/** Render and play a given number of samples. Returns the  **/
/** number of samples actually played.                      **/
/*************************************************************/
unsigned int RenderAndPlayAudio(unsigned int Samples)
{
  int Buf[256];
  unsigned int J,I;

  /* Exit if wave sound not initialized */
  if(SndRate<8192) return(0);

  J       = GetFreeAudio();
  Samples = Samples<J? Samples:J;
 
  /* Render and play sound */
  for(I=0;I<Samples;I+=J)
  {
    J = Samples-I;
    J = J<sizeof(Buf)/sizeof(Buf[0])? J:sizeof(Buf)/sizeof(Buf[0]);
    memset(Buf,0,J*sizeof(Buf[0]));
    RenderAudio(Buf,J);
    if(PlayAudio(Buf,J)<J) { I+=J;break; }
  }

  /* Return number of samples rendered */
  return(I);
}
