/** EMULib Emulation Library *********************************/
/**                                                         **/
/**                        EMULib.h                         **/
/**                                                         **/
/** This file contains platform-independent definitions and **/
/** declarations for the emulation library.                 **/
/**                                                         **/
/** Copyright (C) Marat Fayzullin 1996-2021                 **/
/**     You are not allowed to distribute this software     **/
/**     commercially. Please, notify me, if you make any    **/
/**     changes to this file.                               **/
/*************************************************************/
#ifndef EMULIB_H
#define EMULIB_H

/** Button Bits **********************************************/
/** Bits returned by GetJoystick() and WaitJoystick().      **/
/*************************************************************/
#define BTN_LEFT     0x0001
#define BTN_RIGHT    0x0002
#define BTN_UP       0x0004
#define BTN_DOWN     0x0008
#define BTN_FIREA    0x0010
#define BTN_FIREB    0x0020
#define BTN_FIREL    0x0040
#define BTN_FIRER    0x0080
#define BTN_START    0x0100
#define BTN_SELECT   0x0200
#define BTN_EXIT     0x0400
#define BTN_FIREX    0x0800
#define BTN_FIREY    0x1000
#define BTN_FFWD     0x2000
#define BTN_MENU     0x4000
#define BTN_ALL      0x7FFF
#define BTN_OK       (BTN_FIREA|BTN_START)
#define BTN_FIRE     (BTN_FIREA|BTN_FIREB|BTN_FIREL|BTN_FIRER|BTN_FIREX|BTN_FIREY)
#define BTN_ARROWS   (BTN_LEFT|BTN_RIGHT|BTN_UP|BTN_DOWN)
#define BTN_SHIFT    CON_SHIFT
#define BTN_CONTROL  CON_CONTROL
#define BTN_ALT      CON_ALT
#define BTN_MODES    (BTN_SHIFT|BTN_CONTROL|BTN_ALT)

/** Mouse Bits ***********************************************/
/** Bits returned by GetMouse() and WaitKeyOrMouse().       **/
/*************************************************************/
#define MSE_RIGHT    0x80000000
#define MSE_LEFT     0x40000000
#define MSE_BUTTONS  (MSE_RIGHT|MSE_LEFT)
#define MSE_YPOS     0x3FFF0000
#define MSE_XPOS     0x0000FFFF

/** Sound ****************************************************/
// fMSX emulates:
// - AY8910 PSG: 3 melodic + 3 noise
// - YM2413 OPLL: 9 FM sound channels, or 6 music + 5 drums
// - SCC: 5 wave channels
// fMSX does not support FM-PAC drums nor instruments (uses triangle waves), and plays either FM-PAC or SCC.
// Thus: either 15, 12 or 11 channels active at once.
#define SND_CHANNELS    16     /* Number of sound channels   */
#define SND_BITS        8
#define SND_BUFSIZE     (1<<SND_BITS)

/** RGB ******************************************************/
#define PIXEL(R,G,B)  (pixel)(((31*(R)/255)<<11)|((63*(G)/255)<<5)|(31*(B)/255))

#ifdef __cplusplus
extern "C" {
#endif

/** pixel ****************************************************/
/** Pixels may be either 8bit, or 16bit, or 32bit. When no  **/
/** BPP* specified, we assume the pixel to have the largest **/
/** size and default to GetColor().                         **/
/*************************************************************/
#ifndef PIXEL_TYPE_DEFINED
#define PIXEL_TYPE_DEFINED
#if defined(BPP32) || defined(BPP24)
typedef unsigned int pixel;
#elif defined(BPP16)
typedef unsigned short pixel;
#elif defined(BPP8)
typedef unsigned char pixel;
#else
typedef unsigned int pixel;
#endif
#endif

/** sample ***************************************************/
/** Audio samples may be either 8bit or 16bit.              **/
/*************************************************************/
#ifndef SAMPLE_TYPE_DEFINED
#define SAMPLE_TYPE_DEFINED
#ifdef BPS16
typedef signed short sample;
#else
typedef signed char sample;
#endif
#endif

/** GetFreeAudio() *******************************************/
/** Get the amount of free samples in the audio buffer.     **/
/*************************************************************/
unsigned int GetFreeAudio(void);

/** WriteAudio() *********************************************/
/** Write up to a given number of samples to audio buffer.  **/
/** Returns the number of samples written.                  **/
/*************************************************************/
unsigned int WriteAudio(sample *Data,unsigned int Length);

#ifdef __cplusplus
}
#endif
#endif /* EMULIB_H */
