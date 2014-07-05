#include "libretro.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>


static retro_log_printf_t log_cb;
static retro_video_refresh_t video_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static retro_environment_t environ_cb;
static retro_audio_sample_batch_t audio_batch_cb = NULL;

static uint16_t* image_buffer;
static unsigned image_buffer_width;
static unsigned image_buffer_height;

void retro_get_system_info(struct retro_system_info *info)
{
   info->library_name = "fMSX";
   info->library_version = "v0.0.1";
   info->need_fullpath = true;
   info->block_extract = false;
   info->valid_extensions = "rom|mx2";
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   info->geometry.base_width = image_buffer_width ;
   info->geometry.base_height = image_buffer_height ;
   info->geometry.max_width = 640 ;
   info->geometry.max_height = 480 ;
   info->geometry.aspect_ratio = 0;
   info->timing.fps = 60.0;
   info->timing.sample_rate = 44100.0;
}


static struct retro_perf_callback perf_cb;


void retro_init(void)
{
   int i;
   struct retro_log_callback log;


   if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
      log_cb = log.log;
   else
      log_cb = NULL;

   image_buffer = malloc(640*480*sizeof(uint16_t));
   image_buffer_width =  272;
   image_buffer_height =  228;

   environ_cb(RETRO_ENVIRONMENT_GET_PERF_INTERFACE, &perf_cb);

}

void retro_deinit(void)
{
   if (image_buffer)
      free(image_buffer);

   image_buffer = NULL;
   image_buffer_width = 0;
   image_buffer_height = 0;

   perf_cb.perf_log();
}

void retro_set_environment(retro_environment_t cb)
{
   environ_cb = cb;
}

void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t unused) { }
void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }

void retro_set_controller_port_device(unsigned port, unsigned device)
{
}

void retro_reset(void)
{
}

size_t retro_serialize_size(void)
{
   return 0;
}

bool retro_serialize(void *data, size_t size)
{
   (void)data;
   (void)size;
   return false;
}

bool retro_unserialize(const void *data, size_t size)
{
   (void)data;
   (void)size;
   return false;
}

void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned a, bool b, const char * c) {}

#include "MSX.h"
#include "Help.h"
#include "EMULib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#define WIDTH       272                   /* Buffer width    */
#define HEIGHT      228                   /* Buffer height   */

/* Press/Release keys in the background KeyState */
#define XKBD_SET(K) XKeyState[Keys[K][0]]&=~Keys[K][1]
#define XKBD_RES(K) XKeyState[Keys[K][0]]|=Keys[K][1]

/* Combination of EFF_* bits */
int UseEffects  = EFF_SCALE|EFF_SAVECPU|EFF_MITSHM|EFF_VARBPP|EFF_SYNC;

int InMenu;                /* 1: In MenuMSX(), ignore keys   */
int UseZoom     = 2;       /* Zoom factor (1=no zoom)        */
int UseSound    = 22050;   /* Audio sampling frequency (Hz)  */
int SyncFreq    = 60;      /* Sync frequency (0=sync off)    */
int FastForward;           /* Fast-forwarded UPeriod backup  */
int SndSwitch;             /* Mask of enabled sound channels */
int SndVolume;             /* Master volume for audio        */
int OldScrMode;            /* fMSX "ScrMode" variable storage*/

const char *Title     = "fMSX Unix 3.9";  /* Program version */

Image NormScreen;          /* Main screen image              */
Image WideScreen;          /* Wide screen image              */
static pixel *WBuf;        /* From Wide.h                    */
static pixel *XBuf;        /* From Common.h                  */
static unsigned int XPal[80];
static unsigned int BPal[256];
static unsigned int XPal0;

const char *Disks[2][MAXDISKS+1];         /* Disk names      */
volatile byte XKeyState[20]; /* Temporary KeyState array     */

void PutImage(void);
#include "CommonMux.h"

void PutImage(void)
{
   video_cb(image_buffer,image_buffer_width,image_buffer_height,image_buffer_width * 2);

}

bool retro_load_game(const struct retro_game_info *info)
{

   enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;

   if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
   {
      if (log_cb)
         log_cb(RETRO_LOG_INFO, "RGB565 is not supported.\n");
      return false;
   }

//   ROMName[0] = "Castle.rom";
   ROMName[0] = "mg1.mx2";
//fMSX
   extern const char *Title;/* Program title                       */
   extern int   UseSound;   /* Sound mode                          */
   extern int   UseZoom;    /* Zoom factor (#ifdef UNIX)           */
   extern int   UseEffects; /* EFF_* bits, ORed (UNIX/MAEMO/MSDOS) */
   extern int   UseStatic;  /* Use static colors (#ifdef MSDOS)    */
   extern int   FullScreen; /* Use 640x480 screen (#ifdef MSDOS)   */
   extern int   SyncFreq;   /* Sync scr updates (UNIX/MAEMO/MSDOS) */
   extern int   ARGC;       /* argc/argv from main (#ifdef UNIX)   */
   extern char **ARGV;

   /** Zero-terminated arrays of disk names for each drive ******/
   extern const char *Disks[2][MAXDISKS+1];


   int CartCount,TypeCount;
   int JoyCount,DiskCount[2];
   int N,J;

   Verbose=1;

   /* Clear everything */
   CartCount=TypeCount=JoyCount=0;
   DiskCount[0]=DiskCount[1]=0;

   /* Default disk images */
   Disks[0][1]=Disks[1][1]=0;
   Disks[0][0]=DSKName[0];
   Disks[1][0]=DSKName[1];
   UPeriod=100;
   Mode=MSX_NTSC|MSX_GUESSA|MSX_MSX2P;
//   Mode=MSX_NTSC|MSX_GUESSA|MSX_MSX1;
   SETJOYTYPE(0,1);
   ProgDir=".";

   XBuf = image_buffer;
   WBuf = image_buffer;

   static Image fMSX_image;
   fMSX_image.Cropped = 0;
   fMSX_image.D = 16;
   fMSX_image.Data = image_buffer;
   fMSX_image.W = image_buffer_width;
   fMSX_image.H = image_buffer_height;
   fMSX_image.L = image_buffer_width;




//UNIX
   GenericSetVideo(&fMSX_image,0,0,image_buffer_width,image_buffer_height);

   /* Set all colors to black */
   for(J=0;J<80;J++)
      SetColor(J, 0, 0, 0);
#define PIXEL(R,G,B)    (pixel)(((31*(R)/255)<<11)|((63*(G)/255)<<5)|(31*(B)/255))
   /* Create SCREEN8 palette (GGGRRRBB) */
   for(J=0;J<256;J++)
     BPal[J]=PIXEL(((J>>2)&0x07)*255/7,((J>>5)&0x07)*255/7,(J&0x03)*255/3);

   /* Initialize temporary keyboard array */
   memset((void *)XKeyState,0xFF,sizeof(XKeyState));


   /* Initialize sound */
   InitSound(UseSound,150);
   SndSwitch=(1<<MAXCHANNELS)-1;
   SndVolume=255/MAXCHANNELS;
   SetChannels(SndVolume,SndSwitch);

   /* Initialize sync timer if needed */
//   if((SyncFreq>0)&&!SetSyncTimer(SyncFreq*UPeriod/100)) SyncFreq=0;







   return true;
}
#include <sys/time.h>
static volatile int TimerReady = 0;
static int TimerON    = 0;
static void TimerHandler(int Arg)
{
  /* Mark sync timer as "ready" */
  TimerReady=1;
  /* Repeat signal next time */
  signal(Arg,TimerHandler);
}
int SetSyncTimer(int Hz)
{
  struct itimerval TimerValue;

  /* Compute and set timer period */
  TimerValue.it_interval.tv_sec  =
  TimerValue.it_value.tv_sec     = 0;
  TimerValue.it_interval.tv_usec =
  TimerValue.it_value.tv_usec    = Hz? 1000000L/Hz:0;

  /* Set timer */
  if(setitimer(ITIMER_REAL,&TimerValue,NULL)) return(0);

  /* Set timer signal */
  signal(SIGALRM,Hz? TimerHandler:SIG_DFL);

  /* Done */
  TimerON=Hz;
  return(1);
}


void SetColor(byte N,byte R,byte G,byte B)
{
  if(N) XPal[N]=PIXEL(R,G,B); else XPal0=PIXEL(R,G,B);
}


unsigned int InitAudio(unsigned int Rate,unsigned int Latency)
{
   return 0;
}

void TrashAudio(void)
{
}

int PauseAudio(int Switch)
{
   return 1;
}

unsigned int GetFreeAudio(void)
{
  return 800;
}

unsigned int WriteAudio(sample *Data,unsigned int Length)
{
   return Length;
}

unsigned int Joystick(void)
{
   return 0;
}

void Keyboard(void)
{
  /* Everything is done in Joystick() */
}

unsigned int Mouse(byte N)
{

}

unsigned int GetJoystick(void)
{
   return 1;
}
bool retro_load_game_special(unsigned a, const struct retro_game_info *b, size_t c)
{
   return false;
}

void retro_unload_game(void)
{
}

unsigned retro_get_region(void)
{
   return RETRO_REGION_NTSC;
}

void *retro_get_memory_data(unsigned id)
{
   return NULL;
}

size_t retro_get_memory_size(unsigned id)
{
   return 0;
}

#define RETRO_PERFORMANCE_INIT(name) static struct retro_perf_counter name = {#name}; if (!name.registered) perf_cb.perf_register(&(name))
#define RETRO_PERFORMANCE_START(name) perf_cb.perf_start(&(name))
#define RETRO_PERFORMANCE_STOP(name) perf_cb.perf_stop(&(name))

void retro_run(void)
{
   int i,j;

   RETRO_PERFORMANCE_INIT(core_retro_run);
   RETRO_PERFORMANCE_START(core_retro_run);

   input_poll_cb();

   StartMSX(Mode,RAMPages,VRAMPages);

   RETRO_PERFORMANCE_STOP(core_retro_run);

   video_cb(image_buffer, image_buffer_width, image_buffer_height, image_buffer_width * sizeof(uint16_t));


}

unsigned retro_api_version(void) { return RETRO_API_VERSION; }

