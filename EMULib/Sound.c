/** EMULib Emulation Library *********************************/
/**                                                         **/
/**                          Sound.c                        **/
/**                                                         **/
/** This file file implements core part of the sound API.   **/
/** See Sound.h for declarations.                           **/
/**                                                         **/
/** Copyright (C) Marat Fayzullin 1996-2021                 **/
/**     You are not allowed to distribute this software     **/
/**     commercially. Please, notify me, if you make any    **/
/**     changes to this file.                               **/
/*************************************************************/
#include "Sound.h"
#include "MSX.h"

#include "../NukeYKT/WrapNukeYKT.h"

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

extern YM2413_NukeYKT OPLL_NukeYKT;
extern YM2413 OPLL;

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

/** InitSound() **********************************************/
/** Initialize RenderSound() with given parameters.         **/
/*************************************************************/
unsigned int InitSound(unsigned int Rate)
{
  int I;

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

/** RenderAudio() ********************************************/
/** Render given number of melodic sound samples into an    **/
/** integer buffer for mixing.                              **/
/*************************************************************/
static void RenderAudio(int *Wave,unsigned int Samples)
{
  int J,K,I,L1,L2,V,A1;
#ifdef WAVE_INTERPOLATION
  /* Keep GCC happy about variable initialization */
  int A2 = 0;
  int L  = 0;
  int N  = 0;
#endif

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
          /* Do not allow high frequencies (GBC Frogger) */
          if(K<0x8000) break;
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
static unsigned int PlayAudio(int *Wave,unsigned int Samples)
{
  int16_t Buf[256];
  unsigned int I,K;
  int D;
  /* Check if the buffer contains enough free space */
  unsigned int J = AUDIO_BUFFER_SIZE;
  if(J<Samples) Samples=J;

  /* Spin until all samples played or WriteAudio() fails */
  for(K=I=J=0;(K<Samples)&&(I==J);K+=I)
  {
    /* Compute number of samples to convert */
    J = sizeof(Buf)/sizeof(int16_t);
    J = Samples-K>J? J:Samples-K;

    /* Convert samples */
    for(I=0;I<J;++I)
    {
      D      = ((*Wave++)*MasterVolume)>>8;
      D      = D>32767? 32767:D<-32768? -32768:D;
      Buf[I] = D;
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
  unsigned int J,I,K,idx;
  float ResampleRate,R,frac;

  /* Exit if wave sound not initialized */
  if(SndRate<8192) return(0);

  /* silence fMSX FM-PAC if NukeYKT is active */
  if(OPTION(MSX_NUKEYKT))
  {
    for(K=0;K<YM2413_CHANNELS;K++)
      Sound(K+OPLL.First,0,0);
    OPLL.Changed=0;
  }

  J       = AUDIO_BUFFER_SIZE;
  Samples = Samples<J? Samples:J;
  ResampleRate = (float)OPLL_NukeYKT.sample_write_index/Samples;

  /* Render and play sound */
  for(I=0,R=0.0;I<Samples;I+=J)
  {
    J = Samples-I;
    J = J<sizeof(Buf)/sizeof(Buf[0])? J:sizeof(Buf)/sizeof(Buf[0]);
    memset(Buf,0,J*sizeof(Buf[0]));
    RenderAudio(Buf,J);

    /*
     * Merge in YM2413 NukeYKT using simplistic linear resampler.
     * Max input range slightly outside [-1024,+1023], output [-32768,+32767] or 16b signed after amplification by 32.
     * BUT matching it to PSG volume (by hand) requires a further amplification by 4.
     * That's still a factor 4-8 less loud than fMSX's FM-PAC volume.
     */
    if(OPTION(MSX_NUKEYKT))
      for(K=0;K<J;K++,R+=ResampleRate)
      {
        idx=(unsigned int)R;
        frac=R-idx;
        Buf[K]+=(int)(.5+128.0*(OPLL_NukeYKT.samples[idx]*(1.0-frac) + OPLL_NukeYKT.samples[idx+1]*frac));
      }

    if(PlayAudio(Buf,J)<J) { I+=J;break; }
  }

  // move last sample to beginning (for linear resampler) and signal a buffer refill
  if (OPLL_NukeYKT.sample_write_index>0)
    OPLL_NukeYKT.samples[0]=OPLL_NukeYKT.samples[OPLL_NukeYKT.sample_write_index-1];
  OPLL_NukeYKT.sample_write_index=1;

  /* Return number of samples rendered */
  return I;
}
