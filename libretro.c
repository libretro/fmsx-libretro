#include "libretro.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

#include <compat/posix_string.h>
#include <streams/file_stream_transforms.h>
#include <file/file_path.h>

#include "MSX.h"
#include "EMULib.h"
#include "Sound.h"

static unsigned frame_number=0;
static unsigned fps;
static uint16_t* image_buffer;
static unsigned image_buffer_width;
static unsigned image_buffer_height;

static uint16_t XPal[80];
static uint16_t BPal[256];
static uint16_t XPal0;
static byte PaletteFrozen=0;

#ifndef PATH_MAX
#define PATH_MAX  4096
#endif

#ifdef _WIN32
#define SLASH '\\'
#else
#define SLASH '/'
#endif

static char base_dir[PATH_MAX];
static char PathName_buffer[PATH_MAX];
static char DSKName_buffer[PATH_MAX];
static char FntName_buffer[PATH_MAX];
static char AutoType_buffer[1024];
char *autotype=0;
#define BOOT_FRAME_COUNT 400  // a guesstimate when diskless boot is done

#define FLUSH_NEVER     0
#define FLUSH_IMMEDIATE 1
#define FLUSH_ON_CLOSE  2
int disk_flush=FLUSH_NEVER;

extern int MCFCount;
int current_cheat;

extern byte *RAMData;
extern int RAMPages ;

#define SND_RATE 48000
#define AUDIO_BUFFER_SIZE 1024 // .78 frames at 60Hz, .94 frames at 50Hz

// in screen mode 6 & 7 (512px wide), Wide.h doubles WIDTH
#define BORDER 8
#define WIDTH  (256+(BORDER<<1))
#define HEIGHT (212+(BORDER<<1))

#ifdef PSP
#define PIXEL(R,G,B)    (pixel)(((31*(B)/255)<<11)|((63*(G)/255)<<5)|(31*(R)/255))
#elif defined(PS2)
#define PIXEL(R,G,B)    (pixel)(((31*(B)/255)<<10)|((31*(G)/255)<<5)|(31*(R)/255))
#else
#define PIXEL(R,G,B)    (pixel)(((31*(R)/255)<<11)|((63*(G)/255)<<5)|(31*(B)/255))
#endif

#define XBuf image_buffer
#define WBuf image_buffer
#include "CommonMux.h"
#include "Missing.h"

retro_log_printf_t log_cb = NULL;
static retro_video_refresh_t video_cb = NULL;
static retro_input_poll_t input_poll_cb = NULL;
static retro_input_state_t input_state_cb = NULL;
static retro_environment_t environ_cb = NULL;
static retro_audio_sample_batch_t audio_batch_cb = NULL;

static bool libretro_supports_bitmasks = false;

static retro_perf_tick_t max_frame_ticks = 0;

static unsigned port0_device;

typedef struct
{
   int retro;
   int fmsx;
}keymap_t;
keymap_t keymap[] = // only need to map the 88 basic keys; not SHIFTed ones
{
   { RETROK_LEFT,       KBD_LEFT     },
   { RETROK_UP,         KBD_UP       },
   { RETROK_RIGHT,      KBD_RIGHT    },
   { RETROK_DOWN,       KBD_DOWN     },
   { RETROK_LSHIFT,     KBD_SHIFT    },
   { RETROK_RSHIFT,     KBD_SHIFT    },
   { RETROK_LCTRL,      KBD_CONTROL  },
   { RETROK_RCTRL,      KBD_CONTROL  },
   { RETROK_LALT,       KBD_GRAPH    },
   { RETROK_BACKSPACE,  KBD_BS       },
   { RETROK_TAB,        KBD_TAB      },
   { RETROK_ESCAPE,     KBD_ESCAPE   },
   { RETROK_SPACE,      KBD_SPACE    },
   { RETROK_CAPSLOCK,   KBD_CAPSLOCK },
   { RETROK_END,        KBD_SELECT   },
   { RETROK_HOME,       KBD_HOME     },
   { RETROK_RETURN,     KBD_ENTER    },
   { RETROK_DELETE,     KBD_DELETE   },
   { RETROK_INSERT,     KBD_INSERT   },
   { RETROK_PAGEDOWN,   KBD_COUNTRY  },
   { RETROK_PAGEUP,     KBD_DEAD     },
   { RETROK_PAUSE,      KBD_STOP     },
   { RETROK_F1,         KBD_F1       },
   { RETROK_F2,         KBD_F2       },
   { RETROK_F3,         KBD_F3       },
   { RETROK_F4,         KBD_F4       },
   { RETROK_F5,         KBD_F5       },
   { RETROK_KP0,        KBD_NUMPAD0  },
   { RETROK_KP1,        KBD_NUMPAD1  },
   { RETROK_KP2,        KBD_NUMPAD2  },
   { RETROK_KP3,        KBD_NUMPAD3  },
   { RETROK_KP4,        KBD_NUMPAD4  },
   { RETROK_KP5,        KBD_NUMPAD5  },
   { RETROK_KP6,        KBD_NUMPAD6  },
   { RETROK_KP7,        KBD_NUMPAD7  },
   { RETROK_KP8,        KBD_NUMPAD8  },
   { RETROK_KP9,        KBD_NUMPAD9  },
   { RETROK_KP_MULTIPLY,KBD_NUMMUL   },
   { RETROK_KP_PLUS,    KBD_NUMPLUS  },
   { RETROK_KP_DIVIDE,  KBD_NUMDIV   },
   { RETROK_KP_MINUS,   KBD_NUMMINUS },
   { RETROK_KP_ENTER,   KBD_NUMCOMMA },
   { RETROK_KP_PERIOD,  KBD_NUMDOT   }, // KP_PERIOD seem unreachable from RA host keyboard under Linux (KEY_KPDOT == 83)?
   { RETROK_BACKQUOTE,  '`' },
   { RETROK_MINUS,      '-' },
   { RETROK_EQUALS,     '=' },
   { RETROK_LEFTBRACKET,'[' },
   { RETROK_RIGHTBRACKET,']' },
   { RETROK_BACKSLASH,  '\\' },
   { RETROK_SEMICOLON,  ';' },
   { RETROK_QUOTE,      '\'' },
   { RETROK_COMMA,      ',' },
   { RETROK_PERIOD,     '.' },
   { RETROK_SLASH,      '/' },
   { RETROK_0,          '0' },
   { RETROK_1,          '1' },
   { RETROK_2,          '2' },
   { RETROK_3,          '3' },
   { RETROK_4,          '4' },
   { RETROK_5,          '5' },
   { RETROK_6,          '6' },
   { RETROK_7,          '7' },
   { RETROK_8,          '8' },
   { RETROK_9,          '9' },
   { RETROK_a,          'a' },
   { RETROK_b,          'b' },
   { RETROK_c,          'c' },
   { RETROK_d,          'd' },
   { RETROK_e,          'e' },
   { RETROK_f,          'f' },
   { RETROK_g,          'g' },
   { RETROK_h,          'h' },
   { RETROK_i,          'i' },
   { RETROK_j,          'j' },
   { RETROK_k,          'k' },
   { RETROK_l,          'l' },
   { RETROK_m,          'm' },
   { RETROK_n,          'n' },
   { RETROK_o,          'o' },
   { RETROK_p,          'p' },
   { RETROK_q,          'q' },
   { RETROK_r,          'r' },
   { RETROK_s,          's' },
   { RETROK_t,          't' },
   { RETROK_u,          'u' },
   { RETROK_v,          'v' },
   { RETROK_w,          'w' },
   { RETROK_x,          'x' },
   { RETROK_y,          'y' },
   { RETROK_z,          'z' }
};

int joystate;
#define JOY_SET(K, port) joystate |= K << (8 * port)

keymap_t keybemu0_map[] =
{
{ RETRO_DEVICE_ID_JOYPAD_UP,       JST_UP },
{ RETRO_DEVICE_ID_JOYPAD_DOWN,   JST_DOWN },
{ RETRO_DEVICE_ID_JOYPAD_LEFT,   JST_LEFT },
{ RETRO_DEVICE_ID_JOYPAD_RIGHT, JST_RIGHT },
{ RETRO_DEVICE_ID_JOYPAD_A,     JST_FIREA },
{ RETRO_DEVICE_ID_JOYPAD_B,     JST_FIREB },
{ RETRO_DEVICE_ID_JOYPAD_X,        KBD_F3 },
{ RETRO_DEVICE_ID_JOYPAD_Y,     KBD_SPACE },
{ RETRO_DEVICE_ID_JOYPAD_START,    KBD_F1 },
{ RETRO_DEVICE_ID_JOYPAD_SELECT,   KBD_F2 },
{ RETRO_DEVICE_ID_JOYPAD_L,        KBD_F4 },
{ RETRO_DEVICE_ID_JOYPAD_R,        KBD_F5 },
{ RETRO_DEVICE_ID_JOYPAD_L2,    KBD_GRAPH },
{ RETRO_DEVICE_ID_JOYPAD_R2,  KBD_CONTROL },
{ RETRO_DEVICE_ID_JOYPAD_L3,    KBD_ENTER },
{ RETRO_DEVICE_ID_JOYPAD_R3,   KBD_ESCAPE },
};

keymap_t keybemu1_map[] =
{
{ RETRO_DEVICE_ID_JOYPAD_UP,       KBD_UP },
{ RETRO_DEVICE_ID_JOYPAD_DOWN,   KBD_DOWN },
{ RETRO_DEVICE_ID_JOYPAD_LEFT,   KBD_LEFT },
{ RETRO_DEVICE_ID_JOYPAD_RIGHT, KBD_RIGHT },
{ RETRO_DEVICE_ID_JOYPAD_B,     KBD_ENTER },
{ RETRO_DEVICE_ID_JOYPAD_A,     KBD_SPACE },
{ RETRO_DEVICE_ID_JOYPAD_X,           'n' },
{ RETRO_DEVICE_ID_JOYPAD_Y,           'm' },
{ RETRO_DEVICE_ID_JOYPAD_SELECT,   KBD_F4 },
{ RETRO_DEVICE_ID_JOYPAD_START,    KBD_F1 },
{ RETRO_DEVICE_ID_JOYPAD_L,        KBD_F2 },
{ RETRO_DEVICE_ID_JOYPAD_R,        KBD_F3 },
{ RETRO_DEVICE_ID_JOYPAD_L2,    KBD_GRAPH },
{ RETRO_DEVICE_ID_JOYPAD_R2,  KBD_CONTROL },
{ RETRO_DEVICE_ID_JOYPAD_L3,       KBD_F5 },
{ RETRO_DEVICE_ID_JOYPAD_R3,   KBD_ESCAPE },
};

keymap_t joymap[] =
{
{ RETRO_DEVICE_ID_JOYPAD_UP,       JST_UP },
{ RETRO_DEVICE_ID_JOYPAD_DOWN,   JST_DOWN },
{ RETRO_DEVICE_ID_JOYPAD_LEFT,   JST_LEFT },
{ RETRO_DEVICE_ID_JOYPAD_RIGHT, JST_RIGHT },
{ RETRO_DEVICE_ID_JOYPAD_A,     JST_FIREA },
{ RETRO_DEVICE_ID_JOYPAD_B,     JST_FIREB },
};

void retro_get_system_info(struct retro_system_info *info)
{
   info->library_name = "fMSX";
#ifndef GIT_VERSION
#define GIT_VERSION ""
#endif
   info->library_version  = "6.0" GIT_VERSION;
   info->need_fullpath    = true;
   info->block_extract    = false;
   info->valid_extensions = "rom|mx1|mx2|dsk|fdi|cas|m3u";
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   info->geometry.base_width   = image_buffer_width;
   info->geometry.base_height  = image_buffer_height;
   info->geometry.max_width    = 640;
   info->geometry.max_height   = 480;
   info->geometry.aspect_ratio = 0;
   info->timing.fps            = fps;
   info->timing.sample_rate    = SND_RATE;
}

void retro_init(void)
{
   int i;
   struct retro_log_callback log;

   if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
      log_cb = log.log;
   else
      log_cb = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_INPUT_BITMASKS, NULL))
      libretro_supports_bitmasks = true;
}

void retro_deinit(void)
{
   log_cb(RETRO_LOG_INFO, "maximum frame ticks : %llu\n", max_frame_ticks);

   libretro_supports_bitmasks = false;
}

static void set_input_descriptors(void)
{
   struct retro_input_descriptor descriptors_keyb_emu0[] = {
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,   "Stick Left" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,       "Stick Up" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,   "Stick Down" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Stick Right" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,          "Fire B" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,          "Fire A" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,              "F3" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,           "Space" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,         "F2" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,          "F1" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,              "F4" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,              "F5" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,          "Graph" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,           "Ctrl" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3,          "Enter" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3,         "Escape" },
      { 0 },
   };
   struct retro_input_descriptor descriptors_keyb_emu1[] = {
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,   "Arrow Left" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,       "Arrow Up" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,   "Arrow Down" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Arrow Right" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,           "Enter" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,           "Space" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,               "N" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,               "M" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,         "F4" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,          "F1" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,              "F2" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,              "F3" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,          "Graph" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,           "Ctrl" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3,             "F5" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3,         "Escape" },
      { 0 },
   };
   struct retro_input_descriptor descriptors_joystick0[] = {
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,   "Stick Left" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,       "Stick Up" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,   "Stick Down" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Stick Right" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,          "Fire A" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,          "Fire B" },
      { 0 },
   };
   struct retro_input_descriptor descriptors_joystick1[] = {
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,   "Stick Left" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,       "Stick Up" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,   "Stick Down" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Stick Right" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,          "Fire A" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,          "Fire B" },
      { 0 },
   };
   struct retro_input_descriptor descriptors[32];
   struct retro_input_descriptor *out_ptr = descriptors;
   struct retro_input_descriptor *in_ptr;

   if (port0_device == RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 0))
      in_ptr = descriptors_keyb_emu0;
   else if (port0_device == RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 1))
      in_ptr = descriptors_keyb_emu1;
   else if (port0_device == RETRO_DEVICE_JOYPAD)
      in_ptr = descriptors_joystick0;
   else
      in_ptr = NULL;

   if(in_ptr)
   {
      while(in_ptr->description)
         *(out_ptr++) = *(in_ptr)++;
   }

   in_ptr = descriptors_joystick1;
   while(in_ptr->description)
      *(out_ptr++) = *(in_ptr)++;

   out_ptr->description = NULL;

   environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, descriptors);
}

void retro_set_environment(retro_environment_t cb)
{
   struct retro_vfs_interface_info vfs_iface_info;
   bool no_content = true;
   static const struct retro_controller_description port0[] = {
   { "Joystick + Emulated Keyboard",   RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 0) },
   { "Emulated Keyboard",              RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 1) },
   { "Keyboard",                       RETRO_DEVICE_KEYBOARD },
   { "Joystick",                       RETRO_DEVICE_JOYPAD },
   };
   static const struct retro_controller_description port1[] = {
   { "Joystick",                       RETRO_DEVICE_JOYPAD}
   };
   static const struct retro_controller_info ports[] = {
      { port0, 4 },
      { port1, 1 },
      { 0 },
   };
   static const struct retro_variable vars[] = {
      { "fmsx_mode", "MSX Mode; MSX2+|MSX1|MSX2" },
      { "fmsx_video_mode", "MSX Video Mode; NTSC|PAL" },
      { "fmsx_mapper_type_mode", "MSX Mapper Type Mode; "
            "Guess|"
            "Generic 8kB|"
            "Generic 16kB|"
            "Konami5 8kB|"
            "Konami4 8kB|"
            "ASCII 8kB|"
            "ASCII 16kB|"
            "GameMaster2|"
            "FMPAC"
      },
      { "fmsx_ram_pages", "MSX Main Memory; Auto|64KB|128KB|256KB|512KB|4MB" },
      { "fmsx_vram_pages", "MSX Video Memory; Auto|32KB|64KB|128KB|192KB" },
      { "fmsx_simbdos", "Simulate DiskROM disk access calls; No|Yes" },
      { "fmsx_autospace", "Use autofire on SPACE; No|Yes" },
      { "fmsx_allsprites", "Show all sprites; No|Yes" },
      { "fmsx_font", "Text font; standard|DEFAULT.FNT|ITALIC.FNT|INTERNAT.FNT|CYRILLIC.FNT|KOREAN.FNT|JAPANESE.FNT" },
      { "fmsx_flush_disk", "Save changes to .dsk; Never|Immediate|On close" },
      { NULL, NULL },
   };

   environ_cb = cb;

   cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void*)ports);
   cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void*)vars);

   cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_content);

   vfs_iface_info.required_interface_version = 1;
   vfs_iface_info.iface                      = NULL;
   if (cb(RETRO_ENVIRONMENT_GET_VFS_INTERFACE, &vfs_iface_info))
      filestream_vfs_init(&vfs_iface_info);
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
   if(port == 0)
   {
      port0_device = device;
      set_input_descriptors();
   }
}

/* .dsk swap support */
struct retro_disk_control_callback dskcb;
unsigned disk_index = 0;
unsigned disk_images = 0;
char disk_paths[MAXDISKS][PATH_MAX];
bool disk_inserted = false;

bool set_eject_state(bool ejected)
{
   disk_inserted = !ejected;
   if (!disk_inserted) ChangeDisk(0, NULL);
   return true;
}

bool get_eject_state(void)
{
   return !disk_inserted;
}

unsigned get_image_index(void)
{
   return disk_index;
}

bool set_image_index(unsigned index)
{
   disk_index = index;

   if(disk_index == disk_images)
   {
      //retroarch is trying to set "no disk in tray"
      ChangeDisk(0, NULL);
      return true;
   }

   strncpy(DSKName_buffer, disk_paths[disk_index], PATH_MAX-1);
   DSKName_buffer[PATH_MAX-1] = 0;
   DSKName[0]=DSKName_buffer;
   if(!ChangeDisk(0,DSKName[0]) && log_cb) {
      log_cb(RETRO_LOG_ERROR, "%s %s\n", "could not load", disk_paths[disk_index]);
      return false;
   }

   return true;
}

unsigned get_num_images(void)
{
   return disk_images;
}

bool add_image_index(void)
{
   if (disk_images >= MAXDISKS)
      return false;

   disk_images++;
   return true;
}

bool replace_image_index(unsigned index, const struct retro_game_info *info)
{
   char *dot = strrchr(info->path, '.');
   if (!dot || strcasecmp(dot, ".dsk"))
      return false; /* can't swap a cart or tape into a disk slot */

   strcpy(disk_paths[index], info->path);
   return true;
}

void attach_disk_swap_interface(void)
{
   dskcb.set_eject_state = set_eject_state;
   dskcb.get_eject_state = get_eject_state;
   dskcb.set_image_index = set_image_index;
   dskcb.get_image_index = get_image_index;
   dskcb.get_num_images  = get_num_images;
   dskcb.add_image_index = add_image_index;
   dskcb.replace_image_index = replace_image_index;

   environ_cb(RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE, &dskcb);
}

static bool read_m3u(const char *file)
{
   char line[PATH_MAX];
   char name[PATH_MAX];
   RFILE *f = rfopen(file, "r");

   if (!f)
      return false;

   while (rfgets(line, sizeof(line), f) && disk_images < MAXDISKS)
   {
      char *carriage_return = NULL;
      char *newline         = NULL;

      if (line[0] == '#')
         continue;

      carriage_return = strchr(line, '\r');
      if (carriage_return)
         *carriage_return = '\0';

      newline = strchr(line, '\n');
      if (newline) *newline = '\0';

      if (line[0] != '\0')
      {
#ifdef _WIN32
         if (isalpha(line[0]) && line[1] == ':')
#else
         if (line[0] == SLASH)
#endif
            strncpy(name, line, sizeof(name));
         else if (snprintf(name, sizeof(name), "%s%c%s", base_dir, SLASH, line) < 0)
            name[sizeof(name)-1] = 0;
         strcpy(disk_paths[disk_images++], name);
      }
   }

   rfclose(f);
   return (disk_images != 0);
}

static void extract_directory(char *buf, const char *path, size_t size)
{
   char *base = NULL;

   strncpy(buf, path, size - 1);
   buf[size - 1] = '\0';

   base = strrchr(buf, '/');
   if (!base)
      base = strrchr(buf, '\\');

   if (base)
      *base = '\0';
   else
      buf[0] = '\0';
}
/* end .dsk swap support */

void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t unused) { }
void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }

static void update_fps(void)
{
   fps = (Mode & MSX_PAL) ? 50 : 60;
}

void retro_reset(void)
{
   ResetMSX(Mode,RAMPages,VRAMPages);
   frame_number=0;
   update_fps();
}

size_t retro_serialize_size(void)
{
   return 0x100000;
}

bool retro_serialize(void *data, size_t size)
{
   if (!SaveState(data, size))
      return false;

   return true;
}

bool retro_unserialize(const void *data, size_t size)
{
   if (LoadState((unsigned char*)data, size) == 0)
      return false;

   return true;
}

void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned index, bool enabled, const char *code) {}

void PutImage(void)
{
   ExitNow = 1;
}

static void check_variables(void)
{
   bool reset_sfx = false;
   struct retro_variable var;
   int ModeRAM = 0;
   int ModeVRAM = 0;

   var.key = "fmsx_mode";
   var.value = NULL;

   Mode = 0;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "MSX1") == 0)
      {
         Mode |= MSX_MSX1;
         ModeRAM = 4;
         ModeVRAM = 2;
      }
      else if (strcmp(var.value, "MSX2") == 0)
      {
         Mode |= MSX_MSX2;
         ModeRAM = 8;
         ModeVRAM = 8;
      }
      else if (strcmp(var.value, "MSX2+") == 0)
      {
         Mode |= MSX_MSX2P;
         ModeRAM = 16;
         ModeVRAM = 8;
      }
   }
   else
   {
      Mode |= MSX_MSX2P;
      ModeRAM = 16;
      ModeVRAM = 8;
   }

   var.key = "fmsx_video_mode";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "NTSC") == 0)
         Mode |= MSX_NTSC;
      else if (strcmp(var.value, "PAL") == 0)
         Mode |= MSX_PAL;
   }
   else
   {
      Mode |= MSX_NTSC;
   }

   var.key = "fmsx_mapper_type_mode";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "Guess") == 0)
         Mode |= MSX_GUESSA;
      else if (strcmp(var.value, "Generic 8kB") == 0)
         SETROMTYPE(0,MAP_GEN8);
      else if (strcmp(var.value, "Generic 16kB") == 0)
         SETROMTYPE(0,MAP_GEN16);
      else if (strcmp(var.value, "Konami5 8kB") == 0)
         SETROMTYPE(0,MAP_KONAMI5);
      else if (strcmp(var.value, "Konami4 8kB") == 0)
         SETROMTYPE(0,MAP_KONAMI4);
      else if (strcmp(var.value, "ASCII 8kB") == 0)
         SETROMTYPE(0,MAP_ASCII8);
      else if (strcmp(var.value, "ASCII 16kB") == 0)
         SETROMTYPE(0,MAP_ASCII16);
      else if (strcmp(var.value, "GameMaster2") == 0)
         SETROMTYPE(0,MAP_GMASTER2);
      else if (strcmp(var.value, "FMPAC") == 0)
         SETROMTYPE(0,MAP_FMPAC);
      else
         Mode |= MSX_GUESSA;
   }
   else
   {
      Mode |= MSX_GUESSA;
   }

   var.key = "fmsx_simbdos";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value && strcmp(var.value, "Yes") == 0)
      Mode |= MSX_PATCHBDOS;

   var.key = "fmsx_flush_disk";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "Never") == 0)
         disk_flush=FLUSH_NEVER;
      else if (strcmp(var.value, "Immediate") == 0)
         disk_flush=FLUSH_IMMEDIATE;
      else if (strcmp(var.value, "On close") == 0)
         disk_flush=FLUSH_ON_CLOSE;
   }

   var.key = "fmsx_autospace";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value && strcmp(var.value, "Yes") == 0)
      Mode |= MSX_AUTOSPACE;

   var.key = "fmsx_allsprites";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value && strcmp(var.value, "Yes") == 0)
      Mode |= MSX_ALLSPRITE;

   var.key = "fmsx_ram_pages";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "Auto") == 0)
         RAMPages = ModeRAM;
      else if (strcmp(var.value, "64KB") == 0)
         RAMPages = 4;
      else if (strcmp(var.value, "128KB") == 0)
         RAMPages = 8;
      else if (strcmp(var.value, "256KB") == 0)
         RAMPages = 16;
      else if (strcmp(var.value, "512KB") == 0)
         RAMPages = 32;
      else if (strcmp(var.value, "4MB") == 0)
         RAMPages = 256;
   }
   else
   {
      RAMPages = ModeRAM;
   }

   var.key = "fmsx_vram_pages";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "Auto") == 0)
         VRAMPages = ModeVRAM;
      else if (strcmp(var.value, "32KB") == 0)
         VRAMPages = 2;
      else if (strcmp(var.value, "64KB") == 0)
         VRAMPages = 4;
      else if (strcmp(var.value, "128KB") == 0)
         VRAMPages = 8;
      else if (strcmp(var.value, "192KB") == 0)
         VRAMPages = 12;
   }
   else
   {
      VRAMPages = ModeVRAM;
   }

   var.key = "fmsx_font";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value && strcmp(var.value, "standard") != 0)
   {
        strncpy(FntName_buffer, var.value, sizeof(FntName_buffer)-1);
        FntName_buffer[sizeof(FntName_buffer)-1] = 0;
        FNTName=FntName_buffer;
        Mode |= MSX_FIXEDFONT;
   }

   update_fps();
}

void set_image_buffer_size(byte screen_mode)
{
   if((screen_mode==6)||(screen_mode==7)||(screen_mode==MAXSCREEN+1))
       image_buffer_width = WIDTH<<1;
   else
       image_buffer_width = WIDTH;
   image_buffer_height = HEIGHT;
}

void replace_ext(char *fname, const char *ext)
{
    char *end = fname + strlen(fname);
    char *cur = end;
    while (cur > fname && *cur != '.') --cur;
    if (*cur == '.' && end - cur > strlen(ext)) {
        strcpy(cur+1, ext);
    }
}

void setup_tape_autotype()
{
   switch (tape_type)
   {
   case ASCII_TAPE: // LOAD"CAS:",R
      if(MODEL(MSX_MSX2P))
         strcpy(AutoType_buffer, "\05l\05o\05a\05d\05""2\05c\05a\05s'\01\05""2,\01\05r\x0d\01");
      else
         strcpy(AutoType_buffer, "\05l\05o\05a\05d\05'\05c\05a\05s\05:\05',\01\05r\x0d\01");
      break;
   case BINARY_TAPE: // BLOAD"CAS:",R
      if(MODEL(MSX_MSX2P))
         strcpy(AutoType_buffer, "\05b\05l\05o\05a\05d\05""2\05c\05a\05s'\01\05""2,\01\05r\x0d\01");
      else
         strcpy(AutoType_buffer, "\05b\05l\05o\05a\05d\05'\05c\05a\05s\05:\05',\01\05r\x0d\01");
      break;
   case BASIC_TAPE: // CLOAD <enter> RUN
      strcpy(AutoType_buffer, "\05c\05l\05o\05a\05d\x0d\01\05r\05u\05n\x0d\01");
      break;
   }
   if (tape_type != NO_TAPE)
      autotype = (char*)&AutoType_buffer;
}

void set_extension(char *buffer, int maxidx, const char *path, const char *ext)
{
   strncpy(buffer, path, maxidx);
   PathName_buffer[maxidx]=0;
   replace_ext(buffer, ext);
}

bool try_loading_cht(const char *path, const char *ext)
{
   set_extension(PathName_buffer, sizeof(PathName_buffer)-1, path, ext);
   return (filestream_exists(PathName_buffer) && LoadCHT(PathName_buffer) > 0);
}

bool try_loading_mcf(const char *path, const char *ext)
{
   set_extension(PathName_buffer, sizeof(PathName_buffer)-1, path, ext);
   return (filestream_exists(PathName_buffer) && LoadMCF(PathName_buffer) > 0);
}

bool try_loading_palette(const char *path, const char *ext)
{
   set_extension(PathName_buffer, sizeof(PathName_buffer)-1, path, ext);
   if (filestream_exists(PathName_buffer) && LoadPAL(PathName_buffer) == 16)
   {
      PaletteFrozen=1;
      return true;
   }
   return false;
}

void show_message(const char* msg, unsigned number_of_frames)
{
   struct retro_message message;
   message.msg = msg;
   message.frames = number_of_frames;
   environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE, &message);
}

RETRO_CALLCONV void keyboard_event(bool down, unsigned keycode, uint32_t character, uint16_t key_modifiers)
{
   if (down)
   {
      switch(keycode)
      {
      case RETROK_F6:
         RewindTape();
         show_message("Cassette tape rewound", fps);
         break;

      case RETROK_F7:
         if (MCFCount > 0)
         {
            // cycle through MCF cheats
            current_cheat=(++current_cheat)%(MCFCount+1);
            if (current_cheat==0)
            {
               Cheats(CHTS_OFF);
               show_message("Disabled cheats", fps);
            }
            else if (ApplyMCFCheat(current_cheat-1))
            {
               char msg[1024], *note;
               int value;
               Cheats(CHTS_ON);
               note = GetMCFNoteAndValue(current_cheat-1, &value);
               snprintf(msg, sizeof(msg), "Enabled cheat %s: %d", note, value);
               show_message(msg, fps);
            }
         }
         else // just toggle CHT cheats on/off all at once
            Cheats(!Cheats(CHTS_QUERY));
         break;
      }
   }
}

void load_core_specific_cheats(const char* path)
{
   current_cheat=0;

   if (try_loading_cht(path, "cht")
    || try_loading_cht(path, "CHT")
    || try_loading_mcf(path, "mcf")
    || try_loading_mcf(path, "MCF")) {}
}

bool retro_load_game(const struct retro_game_info *info)
{
   int i;
   static char ROMName_buffer[PATH_MAX];
   static char CasName_buffer[PATH_MAX];
   struct retro_keyboard_callback keyboard_event_callback;
   enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;

   if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
   {
      if (log_cb)
         log_cb(RETRO_LOG_INFO, "RGB565 is not supported.\n");
      return false;
   }

   image_buffer = (uint16_t*)malloc(640*480*sizeof(uint16_t));

   environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &ProgDir);

   if (info) extract_directory(base_dir, info->path, sizeof(base_dir));
   disk_index = 0;
   disk_images = 0;
   disk_inserted = false;

   keyboard_event_callback.callback = keyboard_event;
   environ_cb(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, &keyboard_event_callback);

   check_variables();
   set_input_descriptors();

   Verbose=1;

   UPeriod=100;

   if (info && path_is_valid(info->path))
   {
      char *dot = strrchr(info->path, '.');
      if (dot && ( !strcasecmp(dot, ".rom") || !strcasecmp(dot, ".mx1") || !strcasecmp(dot, ".mx2") ))
      {
         strcpy(ROMName_buffer, info->path);
         ROMName[0]=ROMName_buffer;
      }
      else if (dot && ( !strcasecmp(dot, ".dsk") || !strcasecmp(dot, ".fdi") ))
      {
         strcpy(DSKName_buffer, info->path);
         DSKName[0]=DSKName_buffer;
      }
      else if (dot && !strcasecmp(dot, ".cas"))
      {
         strcpy(CasName_buffer, info->path);
         CasName=CasName_buffer;
      }
      else if (dot && !strcasecmp(dot, ".m3u"))
      {
         if (!read_m3u(info->path))
         {
            if (log_cb) log_cb(RETRO_LOG_ERROR, "%s %s\n", "failed to read", info->path);
            return false;
         }
         set_image_index(0);
         disk_inserted = true;
         attach_disk_swap_interface();
      }

      if (try_loading_palette(info->path, "pal") || try_loading_palette(info->path, "PAL")) {}
   }
   else
   {
      ROMName[0]=NULL;
      DSKName[0]=NULL;
      CasName=NULL;
   }

   SETJOYTYPE(0,JOY_STICK);
   SETJOYTYPE(1,JOY_STICK);

   set_image_buffer_size(0);

   for(i = 0; i < 80; i++)
      SetColor(i, 0, 0, 0);

   for(i = 0; i < 256; i++)
     BPal[i]=PIXEL(((i>>2)&0x07)*255/7,((i>>5)&0x07)*255/7,(i&0x03)*255/3);

   InitSound(SND_RATE, 0);
   SetChannels(255/MAXCHANNELS, (1<<MAXCHANNELS)-1);

   ExitNow = 1;
   StartMSX(Mode,RAMPages,VRAMPages);
   if (info) load_core_specific_cheats(info->path);
   update_fps();
   setup_tape_autotype();
   return true;
}

void SetColor(byte N,byte R,byte G,byte B)
{
  if(PaletteFrozen && N<16) return;
  if(N)
     XPal[N]=PIXEL(R,G,B);
  else
     XPal0=PIXEL(R,G,B);
}

unsigned int GetFreeAudio(void)
{
  return AUDIO_BUFFER_SIZE;
}

unsigned int WriteAudio(sample *Data,unsigned int Length)
{
   static uint16_t audio_buf[AUDIO_BUFFER_SIZE * 2];
   int i;
   if (Length > AUDIO_BUFFER_SIZE)
      Length = AUDIO_BUFFER_SIZE;
   for (i=0; i < Length; i++)
   {
      audio_buf[i << 1]       = Data[i];
      audio_buf[(i << 1) + 1] = Data[i];
   }

   return audio_batch_cb((const int16_t*)audio_buf, Length);
}

unsigned int Joystick(void)
{
   return joystate;
}

unsigned int Mouse(byte N)
{
   // not implemented yet
   return 0;
}

bool retro_load_game_special(unsigned a,
      const struct retro_game_info *b, size_t c)
{
   return false;
}

void retro_unload_game(void)
{
   if (image_buffer)
      free(image_buffer);

   image_buffer = NULL;
   image_buffer_width = 0;
   image_buffer_height = 0;

   if(disk_flush==FLUSH_ON_CLOSE && FDD[0].Dirty)
   {
      SaveFDI(&FDD[0],DSKName[0],FMT_MSXDSK);
      FDD[0].Dirty = 0;
   }

   TrashMSX();
}

unsigned retro_get_region(void)
{
   return RETRO_REGION_NTSC;
}

void *retro_get_memory_data(unsigned id)
{
   if ( id == RETRO_MEMORY_SYSTEM_RAM )
      return RAMData;

   return NULL;
}

size_t retro_get_memory_size(unsigned id)
{
   if ( id == RETRO_MEMORY_SYSTEM_RAM )
      return RAMPages*0x4000;
   return 0;
}

void handle_tape_autotype()
{
   if (frame_number < BOOT_FRAME_COUNT && autotype)
      KBD_SET(KBD_SHIFT); // press shift during boot to skip loading DiskROM. Most tape games need the extra memory.
   else if (frame_number > BOOT_FRAME_COUNT && (frame_number & 3) == 0 && autotype && *autotype)
   {
      KBD_SET(*autotype);
      autotype++;
      if (*autotype > 1) KBD_SET(*autotype);
      autotype++;
   }
}

#ifdef PSP
#include <pspgu.h>
#endif
void retro_run(void)
{
   byte currentScreenMode;
   int i,j;
   bool updated = false;
   int16_t joypad_bits[2];

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated)
         && updated)
      check_variables();

   input_poll_cb();

   for (j = 0; j < 2; j++)
   {
      if (libretro_supports_bitmasks)
         joypad_bits[j] = input_state_cb(
               j, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
      else
      {
         joypad_bits[j] = 0;
         for (i = 0; i < (RETRO_DEVICE_ID_JOYPAD_R3+1); i++)
            joypad_bits[j] |= input_state_cb(
                  j, RETRO_DEVICE_JOYPAD, 0, i) ? (1 << i) : 0;
      }
   }

   for (i=0; i < sizeof(Keys)/2; i++)
      if(i != KBD_SPACE || !(OPTION(MSX_AUTOSPACE)))
         KBD_RES(i);

   joystate = 0;

   switch(port0_device)
   {
   case RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 0):
      for (i = 0; i < sizeof(joymap) / sizeof(keymap_t); i++)
         if (joypad_bits[0] & (1 << keybemu0_map[i].retro))
            JOY_SET(keybemu0_map[i].fmsx, 0);
      for (; i < sizeof(keybemu0_map) / sizeof(keymap_t); i++)
         if (joypad_bits[0] & (1 << keybemu0_map[i].retro))
            KBD_SET(keybemu0_map[i].fmsx);
      break;

   case RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 1):
      for (i = 0; i < sizeof(joymap) / sizeof(keymap_t); i++)
         if (joypad_bits[0] & (1 << keybemu1_map[i].retro))
            JOY_SET(keybemu1_map[i].fmsx, 0);
      for (; i < sizeof(keybemu1_map) / sizeof(keymap_t); i++)
         if (joypad_bits[0] & (1 << keybemu1_map[i].retro))
            KBD_SET(keybemu1_map[i].fmsx);
      break;

   case RETRO_DEVICE_JOYPAD:
      for (i = 0; i < sizeof(joymap) / sizeof(keymap_t); i++)
         if (joypad_bits[0] & (1 << joymap[i].retro))
            JOY_SET(joymap[i].fmsx, 0);
      break;

   case RETRO_DEVICE_KEYBOARD:
      for (i=0; i < sizeof(keymap)/sizeof(keymap_t); i++)
         if (input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, keymap[i].retro))
            KBD_SET(keymap[i].fmsx);
      break;
   }

   for (i = 0; i < sizeof(joymap) / sizeof(keymap_t); i++)
   {
      if (joypad_bits[1] & (1 << joymap[i].retro))
         JOY_SET(joymap[i].fmsx, 1);
   }

   handle_tape_autotype();

   currentScreenMode = ScrMode;
   RunZ80(&CPU);
   RenderAndPlayAudio(SND_RATE / fps);
   if (currentScreenMode != ScrMode) {
      set_image_buffer_size(ScrMode);
   }

   fflush(stdout);

   // debounce 1s before flushing to .dsk
   if(disk_flush==FLUSH_IMMEDIATE && FDD[0].Dirty && ++FDD[0].Dirty >= fps)
   {
      SaveFDI(&FDD[0],DSKName[0],FMT_MSXDSK);
      FDD[0].Dirty = 0;
   }

#ifdef PSP
   static unsigned int __attribute__((aligned(16))) d_list[32];
   void* const texture_vram_p = (void*) (0x44200000 - (640 * 480)); // max VRAM address - frame size

   sceKernelDcacheWritebackRange(XBuf, 256*240 );
   sceGuStart(GU_DIRECT, d_list);
   sceGuCopyImage(GU_PSM_5650, 0, 0, image_buffer_width, image_buffer_height, image_buffer_width, image_buffer, 0, 0, image_buffer_width, texture_vram_p);

   sceGuTexSync();
   sceGuTexImage(0, 512, 256, image_buffer_width, texture_vram_p);
   sceGuTexMode(GU_PSM_5650, 0, 0, GU_FALSE);
   sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
   sceGuDisable(GU_BLEND);
   sceGuFinish();

   video_cb(texture_vram_p, image_buffer_width, image_buffer_height, image_buffer_width * sizeof(uint16_t));
#else
   video_cb(image_buffer, image_buffer_width, image_buffer_height, image_buffer_width * sizeof(uint16_t));
#endif
   frame_number++;
}

unsigned retro_api_version(void) { return RETRO_API_VERSION; }
