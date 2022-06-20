/** EMULib Emulation Library *********************************/
/**                                                         **/
/**                          Sound.h                        **/
/**                                                         **/
/** This file defines standard sound generation API.        **/
/** See Sound.c and the sound drivers for the code.         **/ 
/**                                                         **/
/** Copyright (C) Marat Fayzullin 1996-2021                 **/
/**     You are not allowed to distribute this software     **/
/**     commercially. Please, notify me, if you make any    **/
/**     changes to this file.                               **/
/*************************************************************/
#ifndef SOUND_H
#define SOUND_H

#include "EMULib.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_BUFFER_SIZE 1024 // .78 frames at 60Hz, .94 frames at 50Hz

                               /* SetSound() arguments:      */
#define SND_MELODIC     0      /* Melodic sound (default)    */
#define SND_RECTANGLE   0      /* Rectangular wave           */
#define SND_TRIANGLE    1      /* Triangular wave (1/2 rect.)*/
#define SND_NOISE       2      /* White noise                */
#define SND_PERIODIC    3      /* Periodic noise (not im-ed) */
#define SND_WAVE        4      /* Wave sound set by SetWave()*/

                               /* Drum() arguments:          */
#define DRM_CLICK       0      /* Click (default)            */

/** InitSound() **********************************************/
/** Initialize RenderSound() with given parameters.         **/
/*************************************************************/
unsigned int InitSound(unsigned int Rate);

/** RenderAndPlayAudio() *************************************/
/** Render and play a given number of samples. Returns the  **/
/** number of samples actually played.                      **/
/*************************************************************/
unsigned int RenderAndPlayAudio(unsigned int Samples);

/** Sound() **************************************************/
/** Generate sound of given frequency (Hz) and volume       **/
/** (0..255) via given channel. Setting Freq=0 or Volume=0  **/
/** turns sound off.                                        **/
/*************************************************************/
void Sound(int Channel,int Freq,int Volume);

/** SetSound() ***********************************************/
/** Set sound type at a given channel.                      **/
/*************************************************************/
void SetSound(int Channel,int NewType);

/** SetChannels() ********************************************/
/** Set master volume (0..255) and switch channels on/off.  **/
/** Each channel N has corresponding bit 2^N in Switch. Set **/
/** or reset this bit to turn the channel on or off.        **/ 
/*************************************************************/
void SetChannels(int Volume,int Switch);

/** SetNoise() ***********************************************/
/** Initialize random noise generator to the given Seed and **/
/** then take random output from OUTBit and XOR it with     **/
/** XORBit.                                                 **/
/*************************************************************/
void SetNoise(int Seed,int OUTBit,int XORBit);

/** SetWave() ************************************************/
/** Set waveform for a given channel. The channel will be   **/
/** marked with sound type SND_WAVE. Set Rate=0 if you want **/
/** waveform to be an instrument or set it to the waveform  **/
/** own playback rate.                                      **/
/*************************************************************/
void SetWave(int Channel,const signed char *Data,int Length,int Rate);

#ifdef __cplusplus
}
#endif
#endif /* SOUND_H */
