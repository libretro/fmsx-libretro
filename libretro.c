#include "libretro.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

#include <compat/strl.h>
#include <compat/posix_string.h>
#include <streams/file_stream_transforms.h>
#include <file/file_path.h>

#ifdef PSP
#include <pspgu.h>
#endif

#include "MSX.h"
#include "EMULib.h"
#include "Sound.h"
#include "FDIDisk.h"

static bool video_mode_dynamic=false;
static unsigned frame_number=0;
static unsigned fps;
static uint16_t* image_buffer;
static unsigned image_buffer_width;
static unsigned image_buffer_height;

static uint16_t XPal[80];
static uint16_t BPal[256];
static uint16_t XPal0;
static bool PaletteFrozen=false;

#ifndef PATH_MAX
#define PATH_MAX  4096
#endif

#ifdef _WIN32
#define SLASH '\\'
#else
#define SLASH '/'
#endif

static char base_dir[PATH_MAX];
static char temp_buffer[PATH_MAX];
static char DSKName_buffer[PATH_MAX];
static char FntName_buffer[PATH_MAX];
static char AutoType_buffer[1024];
char *autotype=0;
#define BOOT_FRAME_COUNT 400  // a guesstimate when diskless boot is done

#define FLUSH_NEVER     0
#define FLUSH_IMMEDIATE 1
#define FLUSH_ON_CLOSE  2
#define FLUSH_TO_SRAM   3
static int disk_flush=FLUSH_NEVER;
static bool phantom_disk = false;

/* .dsk swap support */
struct retro_disk_control_callback dskcb;
unsigned disk_index = 0;
unsigned num_disk_images = 0;
char disk_paths[MAXDISKS][PATH_MAX];
bool disk_inserted = false;

extern int MCFCount;
int current_cheat;

#define SRAM_HEADER 0xA5
static bool sram_save_phase=false;
int sram_size = 0;
uint8_t *sram_content = NULL;
uint8_t *sram_disk_ptr = NULL;

extern uint8_t *RAMData;
extern uint8_t *VRAM;
extern int RAMPages;
extern int VRAMPages;
extern uint8_t RTC[4][13];

extern int VPeriod;

extern uint8_t DiskROMLoaded;
bool require_disk_rom = false;

#define SND_RATE 48000

// in screen mode 6 & 7 (512px wide), Wide.h doubles WIDTH
#define BORDER 8
#define WIDTH  (256+(BORDER<<1))
#define HEIGHT (212+(BORDER<<1))
#define MAX_HEIGHT      (256+BORDER)
#define MAX_SCANLINE    (PALVideo?255:242)

#ifdef PSP
#define PIXEL(R,G,B)    (uint16_t)(((31*(B)/255)<<11)|((63*(G)/255)<<5)|(31*(R)/255))
#elif defined(PS2)
#define PIXEL(R,G,B)    (uint16_t)(((31*(B)/255)<<10)|((31*(G)/255)<<5)|(31*(R)/255))
#else
#define PIXEL(R,G,B)    (uint16_t)(((31*(R)/255)<<11)|((63*(G)/255)<<5)|(31*(B)/255))
#endif

int fmsx_log_level = RETRO_LOG_WARN;
retro_log_printf_t log_cb = NULL;
static retro_video_refresh_t video_cb = NULL;
static retro_input_poll_t input_poll_cb = NULL;
static retro_input_state_t input_state_cb = NULL;
static retro_environment_t environ_cb = NULL;
static retro_audio_sample_batch_t audio_batch_cb = NULL;

#define HIRES_OFF           0
#define HIRES_INTERLACED    1
#define HIRES_PROGRESSIVE   2
static int hires_mode = HIRES_OFF;
static bool overscan = false;
#define HiResMode           (InterlaceON&&hires_mode!=HIRES_OFF)
#define InterlacedMode      (hires_mode==HIRES_INTERLACED)
#define OverscanMode        (overscan)
#define OddPage             (frame_number&1)

#define XBuf image_buffer
#define WBuf image_buffer
int LastScanline;
#include "CommonMux.h"

static bool libretro_supports_bitmasks = false;

static retro_perf_tick_t max_frame_ticks = 0;

static unsigned port0_device;

typedef struct
{
   int retro;
   int fmsx;
   char *name;
}keymap_t;
keymap_t keymap[] = // only need to map the 88 basic keys; not SHIFTed ones
{
   { RETROK_LEFT,       KBD_LEFT     , "left"},
   { RETROK_UP,         KBD_UP       , "up"},
   { RETROK_RIGHT,      KBD_RIGHT    , "right"},
   { RETROK_DOWN,       KBD_DOWN     , "down"},
   { RETROK_LSHIFT,     KBD_SHIFT    , "shift"},
   { RETROK_RSHIFT,     KBD_SHIFT    , "shift"},
   { RETROK_LCTRL,      KBD_CONTROL  , "ctrl"},
   { RETROK_RCTRL,      KBD_CONTROL  , "ctrl"},
   { RETROK_LALT,       KBD_GRAPH    , "graph"},
   { RETROK_BACKSPACE,  KBD_BS       , "backspace"},
   { RETROK_TAB,        KBD_TAB      , "tab"},
   { RETROK_ESCAPE,     KBD_ESCAPE   , "escape"},
   { RETROK_SPACE,      KBD_SPACE    , "space"},
   { RETROK_CAPSLOCK,   KBD_CAPSLOCK , "capslock"},
   { RETROK_END,        KBD_SELECT   , "select"},
   { RETROK_HOME,       KBD_HOME     , "home"},
   { RETROK_RETURN,     KBD_ENTER    , "enter"},
   { RETROK_DELETE,     KBD_DELETE   , "del"},
   { RETROK_INSERT,     KBD_INSERT   , "insert"},
   { RETROK_PAGEDOWN,   KBD_COUNTRY  , "country"},
   { RETROK_PAGEUP,     KBD_DEAD     , "dead"},
   { RETROK_PAUSE,      KBD_STOP     , "stop"},
   { RETROK_F1,         KBD_F1       , "f1"},
   { RETROK_F2,         KBD_F2       , "f2"},
   { RETROK_F3,         KBD_F3       , "f3"},
   { RETROK_F4,         KBD_F4       , "f4"},
   { RETROK_F5,         KBD_F5       , "f5"},
   { RETROK_KP0,        KBD_NUMPAD0  , "keypad0"},
   { RETROK_KP1,        KBD_NUMPAD1  , "keypad1"},
   { RETROK_KP2,        KBD_NUMPAD2  , "keypad2"},
   { RETROK_KP3,        KBD_NUMPAD3  , "keypad3"},
   { RETROK_KP4,        KBD_NUMPAD4  , "keypad4"},
   { RETROK_KP5,        KBD_NUMPAD5  , "keypad5"},
   { RETROK_KP6,        KBD_NUMPAD6  , "keypad6"},
   { RETROK_KP7,        KBD_NUMPAD7  , "keypad7"},
   { RETROK_KP8,        KBD_NUMPAD8  , "keypad8"},
   { RETROK_KP9,        KBD_NUMPAD9  , "keypad9"},
   { RETROK_KP_MULTIPLY,KBD_NUMMUL   , "kp_multiply"},
   { RETROK_KP_PLUS,    KBD_NUMPLUS  , "kp_plus"},
   { RETROK_KP_DIVIDE,  KBD_NUMDIV   , "kp_divide"},
   { RETROK_KP_MINUS,   KBD_NUMMINUS , "kp_minus"},
   { RETROK_KP_ENTER,   KBD_NUMCOMMA , "kp_comma"},
   { RETROK_KP_PERIOD,  KBD_NUMDOT   , "kp_period"}, // KP_PERIOD seem unreachable from RA host keyboard under Linux (KEY_KPDOT == 83)?
   { RETROK_BACKQUOTE,  '`'          , "backquote"},
   { RETROK_MINUS,      '-'          , "minus"},
   { RETROK_EQUALS,     '='          , "equals"},
   { RETROK_LEFTBRACKET,'['          , "leftbracket"},
   { RETROK_RIGHTBRACKET,']'         , "rightbracket"},
   { RETROK_BACKSLASH,  '\\'         , "backslash"},
   { RETROK_SEMICOLON,  ';'          , "semicolon"},
   { RETROK_QUOTE,      '\''         , "quote"},
   { RETROK_COMMA,      ','          , "comma"},
   { RETROK_PERIOD,     '.'          , "period"},
   { RETROK_SLASH,      '/'          , "slash"},
   { RETROK_0,          '0'          , "0"},
   { RETROK_1,          '1'          , "1"},
   { RETROK_2,          '2'          , "2"},
   { RETROK_3,          '3'          , "3"},
   { RETROK_4,          '4'          , "4"},
   { RETROK_5,          '5'          , "5"},
   { RETROK_6,          '6'          , "6"},
   { RETROK_7,          '7'          , "7"},
   { RETROK_8,          '8'          , "8"},
   { RETROK_9,          '9'          , "9"},
   { RETROK_a,          'a'          , "a"},
   { RETROK_b,          'b'          , "b"},
   { RETROK_c,          'c'          , "c"},
   { RETROK_d,          'd'          , "d"},
   { RETROK_e,          'e'          , "e"},
   { RETROK_f,          'f'          , "f"},
   { RETROK_g,          'g'          , "g"},
   { RETROK_h,          'h'          , "h"},
   { RETROK_i,          'i'          , "i"},
   { RETROK_j,          'j'          , "j"},
   { RETROK_k,          'k'          , "k"},
   { RETROK_l,          'l'          , "l"},
   { RETROK_m,          'm'          , "m"},
   { RETROK_n,          'n'          , "n"},
   { RETROK_o,          'o'          , "o"},
   { RETROK_p,          'p'          , "p"},
   { RETROK_q,          'q'          , "q"},
   { RETROK_r,          'r'          , "r"},
   { RETROK_s,          's'          , "s"},
   { RETROK_t,          't'          , "t"},
   { RETROK_u,          'u'          , "u"},
   { RETROK_v,          'v'          , "v"},
   { RETROK_w,          'w'          , "w"},
   { RETROK_x,          'x'          , "x"},
   { RETROK_y,          'y'          , "y"},
   { RETROK_z,          'z'          , "z"}
};

int joystate;
#define JOY_SET(K, port) joystate |= K << (8 * port)

keymap_t keybemu0_map[] = // Joystick + emulated keyboard
{
// first 6: joystick
{ RETRO_DEVICE_ID_JOYPAD_UP,       JST_UP },
{ RETRO_DEVICE_ID_JOYPAD_DOWN,   JST_DOWN },
{ RETRO_DEVICE_ID_JOYPAD_LEFT,   JST_LEFT },
{ RETRO_DEVICE_ID_JOYPAD_RIGHT, JST_RIGHT },
{ RETRO_DEVICE_ID_JOYPAD_A,     JST_FIREA },
{ RETRO_DEVICE_ID_JOYPAD_B,     JST_FIREB },
// rest: keyboard
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

keymap_t keybemu1_map[] = // Emulated keyboard with a fixed mapping
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

keymap_t keybemu2_map[] = // Custom keyboard; all keyboard keys are possible
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

keymap_t joymap[] = // MSX joystick
{
{ RETRO_DEVICE_ID_JOYPAD_UP,       JST_UP },
{ RETRO_DEVICE_ID_JOYPAD_DOWN,   JST_DOWN },
{ RETRO_DEVICE_ID_JOYPAD_LEFT,   JST_LEFT },
{ RETRO_DEVICE_ID_JOYPAD_RIGHT, JST_RIGHT },
{ RETRO_DEVICE_ID_JOYPAD_A,     JST_FIREA },
{ RETRO_DEVICE_ID_JOYPAD_B,     JST_FIREB },
};

static int custom_keyboard_name_to_fmsx(const char *name)
{
    int i;
    for(i = 0; i < sizeof(keymap) / sizeof(keymap_t); i++)
       if(!strcmp(name, keymap[i].name))
          return keymap[i].fmsx;
    return 0;
}

static char *custom_keyboard_fmsx_to_name(int fmsx)
{
    int i;
    for(i=0; i<sizeof(keymap)/sizeof(keymap_t);i++)
       if(fmsx==keymap[i].fmsx)
          return keymap[i].name;
    return "";
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
   struct retro_input_descriptor descriptors_keyb_emu2[] = {
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,     custom_keyboard_fmsx_to_name(keybemu2_map[0].fmsx) },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,   custom_keyboard_fmsx_to_name(keybemu2_map[1].fmsx) },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,   custom_keyboard_fmsx_to_name(keybemu2_map[2].fmsx) },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT,  custom_keyboard_fmsx_to_name(keybemu2_map[3].fmsx) },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,      custom_keyboard_fmsx_to_name(keybemu2_map[4].fmsx) },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,      custom_keyboard_fmsx_to_name(keybemu2_map[5].fmsx) },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,      custom_keyboard_fmsx_to_name(keybemu2_map[6].fmsx) },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,      custom_keyboard_fmsx_to_name(keybemu2_map[7].fmsx) },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, custom_keyboard_fmsx_to_name(keybemu2_map[8].fmsx) },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,  custom_keyboard_fmsx_to_name(keybemu2_map[9].fmsx) },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,      custom_keyboard_fmsx_to_name(keybemu2_map[10].fmsx) },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,      custom_keyboard_fmsx_to_name(keybemu2_map[11].fmsx) },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,     custom_keyboard_fmsx_to_name(keybemu2_map[12].fmsx) },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,     custom_keyboard_fmsx_to_name(keybemu2_map[13].fmsx) },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3,     custom_keyboard_fmsx_to_name(keybemu2_map[14].fmsx) },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3,     custom_keyboard_fmsx_to_name(keybemu2_map[15].fmsx) },
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
   else if (port0_device == RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 2))
      in_ptr = descriptors_keyb_emu2;
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

static char *custom_keyboard_values(char *prefix, char *def)
{
    int i;
    char values[4096];
    values[0]=0;
    strcat(values, prefix);
    strcat(values, def);
    for(i=0; i<sizeof(keymap)/sizeof(keymap_t);i++)
    {
        if ((i > 0 && !strcmp(keymap[i-1].name, keymap[i].name)) || !strcmp(keymap[i].name, def))
           continue;
        strcat(values, "|");
        strcat(values, keymap[i].name);
    }
    return strdup(values);
}

static void flush_disk(void)
{
   int R,num_patched_images;
   bool found=false;

   if(!FDD[0].Dirty)
      return;

   switch(disk_flush)
   {
   case FLUSH_ON_CLOSE:
   case FLUSH_IMMEDIATE:
      if ((R=SaveFDI(&FDD[0],DSKName_buffer,FMT_MSXDSK))!=FDI_SAVE_OK && log_cb) { }
      FDD[0].Dirty = 0; // set to false anyway to prevent spamming
      break;

   case FLUSH_TO_SRAM:
      if (num_disk_images > 0 && filestream_exists(DSKName_buffer))
      {
         num_patched_images=sram_content[2];
         for(R=0;R<num_patched_images;R++)
         {
            if(sram_content[3+R] == disk_index)
            {
               found=true;
               break;
            }
         }
         if (!found)
         {
            sram_content[3+num_patched_images] = disk_index;
            sram_content[2]=++num_patched_images;
         }
         sram_disk_ptr = &sram_content[3 + MAXDISKS + (R * (1 + 720 * 1024)) + 1];
      }
      // initial save, i.e., non-existent .dsk image, _will_ go to disk
      if ((R=SaveFDI(&FDD[0],DSKName_buffer,filestream_exists(DSKName_buffer)?FMT_MEMORY:FMT_MSXDSK))!=FDI_SAVE_OK && log_cb) { }
      FDD[0].Dirty = 0;
      break;
   }
}

static void patch_disk(void)
{
   int i;

   if (sram_content && num_disk_images > 0)
   {
      sram_disk_ptr=NULL;
      for(i=0;i<sram_content[2];i++)
      {
         if(sram_content[3+i] == disk_index)
         {
            sram_disk_ptr = &sram_content[3 + MAXDISKS + (i * (1 + 720 * 1024)) + 1];
            break;
         }
      }
   }
   if (sram_disk_ptr && sram_disk_ptr[-1] == SRAM_HEADER && DiskData && DiskSize <= sram_size-1)
   {
      memcpy(DiskData, sram_disk_ptr, DiskSize);
      DiskData = NULL;
      DiskSize = 0;
   }
}

/* .dsk swap support */
static bool set_eject_state(bool ejected)
{
   disk_inserted = !ejected;
   if (!disk_inserted)
   {
      flush_disk();
      ChangeDisk(0, NULL);
      sram_disk_ptr=NULL;
   }
   return true;
}

static bool get_eject_state(void)
{
   return !disk_inserted;
}

static unsigned get_image_index(void)
{
   return disk_index;
}

static bool set_image_index(unsigned index)
{
   disk_index = index;

   if(disk_index >= num_disk_images)
   {
      ChangeDisk(0,NULL);
      sram_disk_ptr=NULL;
      return true;
   }

   strncpy(DSKName_buffer, disk_paths[disk_index], PATH_MAX-1);
   DSKName_buffer[PATH_MAX-1] = 0;
   DSKName[0]=DSKName_buffer;
   if(ChangeDisk(0,DSKName[0]))
   {
      if (disk_flush==FLUSH_TO_SRAM)
         patch_disk();
   }
   else if (phantom_disk)
      ChangeDisk(0,"");
   else
      return false;

   return true;
}

static unsigned get_num_images(void)
{
   return num_disk_images;
}

static bool add_image_index(void)
{
   if (num_disk_images >= MAXDISKS)
      return false;

   num_disk_images++;
   if (sram_content) sram_content[1] = num_disk_images;

   return true;
}

static bool replace_image_index(unsigned index, const struct retro_game_info *info)
{
   char *dot = strrchr(info->path, '.');
   if (!dot || strcasecmp(dot, ".dsk"))
      return false; /* can't swap a cart or tape into a disk slot */

   strcpy(disk_paths[index], info->path);
   return true;
}

static void attach_disk_swap_interface(void)
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

   while (rfgets(line, sizeof(line), f) && num_disk_images < MAXDISKS)
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
         strcpy(disk_paths[num_disk_images++], name);
      }
   }

   rfclose(f);
   return (num_disk_images != 0);
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

static void show_message(const char* msg, unsigned number_of_frames)
{
   struct retro_message message;
   message.msg    = msg;
   message.frames = number_of_frames;
   environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE, &message);
}

static void update_fps(void)
{
   int freq;

   if (video_mode_dynamic)
   {
      Mode&=~MSX_PAL;
      if (PALVideo) Mode|=MSX_PAL;

      VPeriod = (VIDEO(MSX_PAL)? VPERIOD_PAL:VPERIOD_NTSC)/6;

      freq = VIDEO(MSX_PAL) ? 50 : 60;
      if (fps != freq)
      {
         snprintf(temp_buffer, sizeof(temp_buffer), "switched to %dHz", freq);
         show_message(temp_buffer, fps);
      }
   }

   fps = VIDEO(MSX_PAL) ? 50 : 60;
}

static void cleanup_sram(void)
{
   sram_save_phase=false;
   if (sram_content) free(sram_content);
   sram_content = NULL;
   sram_disk_ptr = NULL;
   sram_size = 0;
}

static void set_image_buffer_size(uint8_t screen_mode)
{
   image_buffer_height = (LastScanline<HEIGHT || !OverscanMode) ? HEIGHT : (LastScanline+1);
   if((screen_mode==6)||(screen_mode==7)||(screen_mode==MAXSCREEN+1))
      image_buffer_width = WIDTH<<1;
   else
      image_buffer_width = WIDTH;
   if (frame_number==0)
      image_buffer_height = HEIGHT;
   else if (HiResMode)
      image_buffer_height <<= 1;
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
      else if (strcmp(var.value, "Dynamic") == 0)
      {
         video_mode_dynamic = true;
         if (MODEL(MSX_MSX2))
            Mode |= MSX_PAL;
         else
            Mode |= MSX_NTSC;
         fps = VIDEO(MSX_PAL) ? 50 : 60;
      }
   }
   else
   {
      Mode |= MSX_NTSC;
   }

   var.key = "fmsx_hires";
   var.value = NULL;

   hires_mode = HIRES_OFF;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "Interlaced") == 0)
         hires_mode = HIRES_INTERLACED;
      else if (strcmp(var.value, "Progressive") == 0)
         hires_mode = HIRES_PROGRESSIVE;
   }

   var.key = "fmsx_overscan";
   var.value = NULL;
   overscan = environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value && strcmp(var.value, "Yes") == 0;

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

   var.key = "fmsx_game_master";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value && strcmp(var.value, "Yes") == 0)
      Mode |= MSX_GMASTER;

   var.key = "fmsx_phantom_disk";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value && strcmp(var.value, "Yes") == 0)
      phantom_disk=true;

   var.key = "fmsx_dos2";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value && strcmp(var.value, "Yes") == 0)
   {
      Mode |= MSX_MSXDOS2;
   }

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
      else if (strcmp(var.value, "To/From SRAM") == 0)
         disk_flush=FLUSH_TO_SRAM;
   }

   var.key = "fmsx_autospace";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value && strcmp(var.value, "Yes") == 0)
      Mode |= MSX_AUTOSPACE;

   var.key = "fmsx_allsprites";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value && strcmp(var.value, "Yes") == 0)
      Mode |= MSX_ALLSPRITE;

   var.key = "fmsx_ym2413_core";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value && strcmp(var.value, "NukeYKT") == 0)
      Mode |= MSX_NUKEYKT;

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
      VRAMPages = ModeVRAM;

   var.key = "fmsx_scci_megaram";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value && strcmp(var.value, "No") == 0)
      Mode |= MSX_NO_MEGARAM;

   var.key = "fmsx_log_level";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "Off") == 0)
         fmsx_log_level = RETRO_LOG_WARN;
      else if (strcmp(var.value, "Info") == 0)
         fmsx_log_level = RETRO_LOG_INFO;
      else if (strcmp(var.value, "Debug") == 0)
         fmsx_log_level = RETRO_LOG_DEBUG;
      else if (strcmp(var.value, "Spam") == 0)
         fmsx_log_level = -1;
   }
   else
      fmsx_log_level = RETRO_LOG_WARN;

   var.key = "fmsx_font";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value && strcmp(var.value, "standard") != 0)
   {
        strncpy(FntName_buffer, var.value, sizeof(FntName_buffer)-1);
        FntName_buffer[sizeof(FntName_buffer)-1] = 0;
        FNTName=FntName_buffer;
        Mode |= MSX_FIXEDFONT;
   }

   var.key = "fmsx_custom_keyboard_up"; var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) keybemu2_map[0].fmsx = custom_keyboard_name_to_fmsx(var.value);
   var.key = "fmsx_custom_keyboard_down"; var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) keybemu2_map[1].fmsx = custom_keyboard_name_to_fmsx(var.value);
   var.key = "fmsx_custom_keyboard_left"; var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) keybemu2_map[2].fmsx = custom_keyboard_name_to_fmsx(var.value);
   var.key = "fmsx_custom_keyboard_right"; var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) keybemu2_map[3].fmsx = custom_keyboard_name_to_fmsx(var.value);
   var.key = "fmsx_custom_keyboard_b"; var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) keybemu2_map[4].fmsx = custom_keyboard_name_to_fmsx(var.value);
   var.key = "fmsx_custom_keyboard_a"; var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) keybemu2_map[5].fmsx = custom_keyboard_name_to_fmsx(var.value);
   var.key = "fmsx_custom_keyboard_x"; var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) keybemu2_map[6].fmsx = custom_keyboard_name_to_fmsx(var.value);
   var.key = "fmsx_custom_keyboard_y"; var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) keybemu2_map[7].fmsx = custom_keyboard_name_to_fmsx(var.value);
   var.key = "fmsx_custom_keyboard_select"; var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) keybemu2_map[8].fmsx = custom_keyboard_name_to_fmsx(var.value);
   var.key = "fmsx_custom_keyboard_start"; var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) keybemu2_map[9].fmsx = custom_keyboard_name_to_fmsx(var.value);
   var.key = "fmsx_custom_keyboard_l"; var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) keybemu2_map[10].fmsx = custom_keyboard_name_to_fmsx(var.value);
   var.key = "fmsx_custom_keyboard_r"; var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) keybemu2_map[11].fmsx = custom_keyboard_name_to_fmsx(var.value);
   var.key = "fmsx_custom_keyboard_l2"; var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) keybemu2_map[12].fmsx = custom_keyboard_name_to_fmsx(var.value);
   var.key = "fmsx_custom_keyboard_r2"; var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) keybemu2_map[13].fmsx = custom_keyboard_name_to_fmsx(var.value);
   var.key = "fmsx_custom_keyboard_l3"; var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) keybemu2_map[14].fmsx = custom_keyboard_name_to_fmsx(var.value);
   var.key = "fmsx_custom_keyboard_r3"; var.value = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) keybemu2_map[15].fmsx = custom_keyboard_name_to_fmsx(var.value);

   update_fps();
}

static void replace_ext(char *fname, const char *ext)
{
    char *end = fname + strlen(fname);
    char *cur = end;
    while (cur > fname && *cur != '.')
        --cur;
    if (*cur == '.' && end - cur > strlen(ext))
        strcpy(cur+1, ext);
}

static void setup_tape_autotype(void)
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

static void set_extension(char *buffer, int maxidx, const char *path, const char *ext)
{
   strncpy(buffer, path, maxidx);
   buffer[maxidx]=0;
   replace_ext(buffer, ext);
}

static bool try_loading_cht(const char *path, const char *ext)
{
   set_extension(temp_buffer, sizeof(temp_buffer)-1, path, ext);
   return (filestream_exists(temp_buffer) && LoadCHT(temp_buffer) > 0);
}

static bool try_loading_mcf(const char *path, const char *ext)
{
   set_extension(temp_buffer, sizeof(temp_buffer)-1, path, ext);
   return (filestream_exists(temp_buffer) && LoadMCF(temp_buffer) > 0);
}

static bool try_loading_palette(const char *path, const char *ext)
{
   set_extension(temp_buffer, sizeof(temp_buffer)-1, path, ext);
   if (filestream_exists(temp_buffer) && LoadPAL(temp_buffer) == 16)
      PaletteFrozen = true;
   return PaletteFrozen;
}

static void toggle_frequency(void)
{
   VDP[9] = VDP[9] ^ 0x02;
   WrZ80(0xFFE8, RdZ80(0xFFE8) ^ 0x02);
   update_fps();
}

static void load_core_specific_cheats(const char* path)
{
   current_cheat=0;

   if (try_loading_cht(path, "cht")
    || try_loading_cht(path, "CHT")
    || try_loading_mcf(path, "mcf")
    || try_loading_mcf(path, "MCF")) {}
}

static size_t filesize(const char* FileName)
{
  size_t Len = -1;
  RFILE *F;

  if(!(F=rfopen(FileName,"rb"))) return Len;
  if(rfseek(F,0,SEEK_END)<0)    { rfclose(F);return Len; }
  Len=rftell(F);
  rfclose(F);
  return Len;
}

static void handle_tape_autotype(void)
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

void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t unused) { }
void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }

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

      case RETROK_F8:
         if (video_mode_dynamic) toggle_frequency();
         break;

      case RETROK_1:
      case RETROK_2:
      case RETROK_3:
      case RETROK_4:
      case RETROK_5:
      case RETROK_6:
      case RETROK_7:
      case RETROK_8:
      case RETROK_9:
         if (key_modifiers&RETROKMOD_CTRL && disk_inserted)
         {
            flush_disk();
            set_image_index(keycode-RETROK_1);
         }
         break;
      }
   }
}


bool retro_load_game(const struct retro_game_info *info)
{
   int i;
   static char ROMName_buffer[PATH_MAX];
   static char CasName_buffer[PATH_MAX];
   struct retro_keyboard_callback keyboard_event_callback;
   enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;
   bool have_image = false;
   char *dot;
   size_t len;

   if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
      return false;

   image_buffer = (uint16_t*)malloc(640*480*sizeof(uint16_t));

   environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &ProgDir);

   if (info) extract_directory(base_dir, info->path, sizeof(base_dir));
   disk_index = 0;
   num_disk_images = 0;
   disk_inserted = false;

   keyboard_event_callback.callback = keyboard_event;
   environ_cb(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, &keyboard_event_callback);

   check_variables();
   set_input_descriptors();

   switch (fmsx_log_level)
   {
   case RETRO_LOG_INFO: // fMSX information logged
   case RETRO_LOG_DEBUG: // fMSX debug details logged
   case -1: // same as RETRO_LOG_DEBUG but also log unknown I/O ports (spams a lot..)
   case RETRO_LOG_WARN: // only RetroArch/libretro-related messages; no fMSX-specifics
   default:
      break;
   }

   UPeriod=100;

   if (info && path_is_valid(info->path))
   {
      dot = strrchr(info->path, '.');
      if (dot && ( !strcasecmp(dot, ".rom") || !strcasecmp(dot, ".mx1") || !strcasecmp(dot, ".mx2") ))
      {
         strcpy(ROMName_buffer, info->path);
         ROMName[0]=ROMName_buffer;
         have_image = true;
      }
      else if (dot && ( !strcasecmp(dot, ".dsk") || !strcasecmp(dot, ".fdi") ))
      {
         strcpy(DSKName_buffer, info->path);
         DSKName[0]       = DSKName_buffer;
         have_image       = true;
         require_disk_rom = true;
         len              = filesize(DSKName_buffer);
         if (len > 0 && disk_flush==FLUSH_TO_SRAM) {
            sram_size = 1 + len;
            sram_content = malloc(sram_size);
            memset(sram_content, 0, sram_size);
            sram_disk_ptr=&sram_content[1];
         }
      }
      else if (dot && !strcasecmp(dot, ".cas"))
      {
         strcpy(CasName_buffer, info->path);
         CasName=CasName_buffer;
         have_image = true;
      }
      else if (dot && !strcasecmp(dot, ".m3u"))
      {
         if (!read_m3u(info->path))
            return false;
         set_image_index(0);
         disk_inserted = true;
         attach_disk_swap_interface();
         have_image = true;
         require_disk_rom = true;

         if (disk_flush==FLUSH_TO_SRAM)
         {
            // format:
            // Byte 0: 0xA5
            // Byte 1: total images
            // Byte 2: # patched disk images
            // Byte 3..34(==3+MAXDISKS-1): index of patched image
            // repeat MAXDISKS times:
            // Byte 35: 0xA5
            // Byte 36..36+720KiB-1: first patched image
            sram_size = 3 + MAXDISKS + (MAXDISKS * (1 + 720 * 1024));
            sram_content = malloc(sram_size);
            memset(sram_content, 0, sram_size);
            sram_content[0] = SRAM_HEADER;
            sram_content[1] = num_disk_images;
         }
      }

      if (try_loading_palette(info->path, "pal") || try_loading_palette(info->path, "PAL")) {}
   }
   else
   {
      ROMName[0]=NULL;
      DSKName[0]=NULL;
      DSKName_buffer[0]=0;
      CasName=NULL;
   }

   SETJOYTYPE(0,JOY_STICK);
   SETJOYTYPE(1,JOY_STICK);

   set_image_buffer_size(0);

   for(i = 0; i < 80; i++)
      SetColor(i, 0, 0, 0);

   // setup fixed SCREEN 8 palette: RGB332
   for(i = 0; i < 256; i++)
      BPal[i]=PIXEL(((i>>2)&0x07)*255/7,((i>>5)&0x07)*255/7,(i&0x03)*255/3);

   InitSound(SND_RATE);
   SetChannels(255/MAXCHANNELS, (1<<MAXCHANNELS)-1);

   StartMSX(Mode,RAMPages,VRAMPages);
   if (!have_image && phantom_disk)
   {
      if (info && (dot = strrchr(info->path, '.')) && !strcasecmp(dot, ".dsk"))
         strcpy(DSKName_buffer, info->path);
      ChangeDisk(0,"");
      require_disk_rom = true;
   }
   if (info) load_core_specific_cheats(info->path);
   update_fps();
   if (require_disk_rom && !DiskROMLoaded)
      show_message("DISK.ROM not loaded; content will not start", 10 * fps);
   setup_tape_autotype();

   // retro_get_memory_size/data not yet invoked at this point
   return true;
}


bool retro_load_game_special(unsigned a, const struct retro_game_info *b, size_t c)
{
   return false;
}

void SetColor(uint8_t N,uint8_t R,uint8_t G,uint8_t B)
{
  if(PaletteFrozen && N<16) return;
  if(N)
     XPal[N]=PIXEL(R,G,B);
  else
     XPal0=PIXEL(R,G,B);
}

unsigned int WriteAudio(int16_t *Data,unsigned int Length)
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

/* TODO/FIXME - not implemented yet */
unsigned int Mouse(uint8_t N)
{
   return 0;
}

void PutImage(void)
{
   set_image_buffer_size(ScrMode);

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

   update_fps();
}

void retro_reset(void)
{
   flush_disk();

   ResetMSX(Mode,RAMPages,VRAMPages);
   frame_number=0;
   update_fps();
}

size_t retro_serialize_size(void)
{
   // max 5MiB: 1778B hardware state, <=4MiB RAM, <=192KiB VRAM
   // Zipped that will be just a few KiB.
   return 0x500000;
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

void retro_unload_game(void)
{
   // when this is invoked, data is already saved to .rtc/.srm
   if (image_buffer)
      free(image_buffer);

   image_buffer = NULL;
   image_buffer_width = 0;
   image_buffer_height = 0;

   flush_disk();
   cleanup_sram();
   num_disk_images = 0;

   TrashMSX();
}

unsigned retro_get_region(void)
{
   if (fps==60)
     return RETRO_REGION_NTSC;
   return RETRO_REGION_PAL;
}

void *retro_get_memory_data(unsigned id)
{
   switch(id)
   {
   case RETRO_MEMORY_SAVE_RAM:
      return sram_content;
   case RETRO_MEMORY_RTC:
      return RTC;
   case RETRO_MEMORY_SYSTEM_RAM:
      return RAMData;
   case RETRO_MEMORY_VIDEO_RAM:
      return VRAM;
   }
   return NULL;
}

size_t retro_get_memory_size(unsigned id)
{
   size_t size = 0;

   switch(id)
   {
   case RETRO_MEMORY_SAVE_RAM:
      if (require_disk_rom && sram_save_phase && disk_flush==FLUSH_TO_SRAM) {
         flush_disk();
         if (sram_content[0] != SRAM_HEADER) {
            // no changes to save
            cleanup_sram();
         }
         if (num_disk_images > 0)
            sram_size = 3 + MAXDISKS + (sram_content[2] * (1 + 720 * 1024));
      }
      size = sram_size;
      break;
   case RETRO_MEMORY_RTC:
      // disable loading/saving fMSX CMOS.ROM, and detect changes before enabling this
      //size = sizeof(RTC);
      break;
   case RETRO_MEMORY_SYSTEM_RAM:
      size = RAMPages*0x4000;
      break;
   case RETRO_MEMORY_VIDEO_RAM:
      size = VRAMPages*0x4000;
      break;
   }

   return size;
}

void retro_set_environment(retro_environment_t cb)
{
   struct retro_vfs_interface_info vfs_iface_info;
   bool no_content = true;

   static const struct retro_controller_description port0[] = {
   { "Joystick + Emulated Keyboard",   RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 0) },
   { "Emulated Keyboard",              RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 1) },
   { "Custom Keyboard",                RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 2) },
   { "Keyboard",                       RETRO_DEVICE_KEYBOARD },
   { "Joystick",                       RETRO_DEVICE_JOYPAD },
   };
   static const struct retro_controller_description port1[] = {
   { "Joystick",                       RETRO_DEVICE_JOYPAD}
   };
   static const struct retro_controller_info ports[] = {
      { port0, 5 },
      { port1, 1 },
      { 0 },
   };
   char* up_value = custom_keyboard_values("Custom keyboard RetroPad up; ",         custom_keyboard_fmsx_to_name(keybemu1_map[ 0].fmsx));
   char* down_value = custom_keyboard_values("Custom keyboard RetroPad down; ",     custom_keyboard_fmsx_to_name(keybemu1_map[ 1].fmsx));
   char* left_value = custom_keyboard_values("Custom keyboard RetroPad left; ",     custom_keyboard_fmsx_to_name(keybemu1_map[ 2].fmsx));
   char* right_value = custom_keyboard_values("Custom keyboard RetroPad right; ",   custom_keyboard_fmsx_to_name(keybemu1_map[ 3].fmsx));
   char* b_value = custom_keyboard_values("Custom keyboard RetroPad b; ",           custom_keyboard_fmsx_to_name(keybemu1_map[ 4].fmsx));
   char* a_value = custom_keyboard_values("Custom keyboard RetroPad a; ",           custom_keyboard_fmsx_to_name(keybemu1_map[ 5].fmsx));
   char* x_value = custom_keyboard_values("Custom keyboard RetroPad x; ",           custom_keyboard_fmsx_to_name(keybemu1_map[ 6].fmsx));
   char* y_value = custom_keyboard_values("Custom keyboard RetroPad y; ",           custom_keyboard_fmsx_to_name(keybemu1_map[ 7].fmsx));
   char* select_value = custom_keyboard_values("Custom keyboard RetroPad select; ", custom_keyboard_fmsx_to_name(keybemu1_map[ 8].fmsx));
   char* start_value = custom_keyboard_values("Custom keyboard RetroPad start; ",   custom_keyboard_fmsx_to_name(keybemu1_map[ 9].fmsx));
   char* l_value = custom_keyboard_values("Custom keyboard RetroPad l; ",           custom_keyboard_fmsx_to_name(keybemu1_map[10].fmsx));
   char* r_value = custom_keyboard_values("Custom keyboard RetroPad r; ",           custom_keyboard_fmsx_to_name(keybemu1_map[11].fmsx));
   char* l2_value = custom_keyboard_values("Custom keyboard RetroPad l2; ",         custom_keyboard_fmsx_to_name(keybemu1_map[12].fmsx));
   char* r2_value = custom_keyboard_values("Custom keyboard RetroPad r2; ",         custom_keyboard_fmsx_to_name(keybemu1_map[13].fmsx));
   char* l3_value = custom_keyboard_values("Custom keyboard RetroPad l3; ",         custom_keyboard_fmsx_to_name(keybemu1_map[14].fmsx));
   char* r3_value = custom_keyboard_values("Custom keyboard RetroPad r3; ",         custom_keyboard_fmsx_to_name(keybemu1_map[15].fmsx));
   const struct retro_variable vars[] = {
      { "fmsx_mode", "MSX Mode; MSX2+|MSX1|MSX2" },
      { "fmsx_video_mode", "MSX Video Mode; NTSC|PAL|Dynamic" },
      { "fmsx_hires", "Support high resolution; Off|Interlaced|Progressive" },
      { "fmsx_overscan", "Support overscan; No|Yes" },
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
      { "fmsx_scci_megaram", "Enable SCC-I 128kB MegaRAM; Yes|No" },
      { "fmsx_ym2413_core", "YM2413 (FM-PAC / MSX-MUSIC) core; fMSX|NukeYKT" },
      { "fmsx_log_level", "fMSX logging; Off|Info|Debug|Spam" },
      { "fmsx_game_master", "Support Game Master; No|Yes" },
      { "fmsx_simbdos", "Simulate DiskROM disk access calls; No|Yes" },
      { "fmsx_autospace", "Use autofire on SPACE; No|Yes" },
      { "fmsx_allsprites", "Show all sprites; No|Yes" },
      { "fmsx_font", "Text font; standard|DEFAULT.FNT|ITALIC.FNT|INTERNAT.FNT|CYRILLIC.FNT|KOREAN.FNT|JAPANESE.FNT" },
      { "fmsx_flush_disk", "Save disk changes; Never|Immediate|On close|To/From SRAM" },
      { "fmsx_phantom_disk", "Create empty disk when none loaded; No|Yes" },
      { "fmsx_dos2", "Load MSXDOS2.ROM when found; No|Yes" },
      { "fmsx_custom_keyboard_up", up_value},
      { "fmsx_custom_keyboard_down", down_value},
      { "fmsx_custom_keyboard_left", left_value},
      { "fmsx_custom_keyboard_right", right_value},
      { "fmsx_custom_keyboard_a", a_value},
      { "fmsx_custom_keyboard_b", b_value},
      { "fmsx_custom_keyboard_y", y_value},
      { "fmsx_custom_keyboard_x", x_value},
      { "fmsx_custom_keyboard_start", start_value},
      { "fmsx_custom_keyboard_select", select_value},
      { "fmsx_custom_keyboard_l", l_value},
      { "fmsx_custom_keyboard_r", r_value},
      { "fmsx_custom_keyboard_l2", l2_value},
      { "fmsx_custom_keyboard_r2", r2_value},
      { "fmsx_custom_keyboard_l3", l3_value},
      { "fmsx_custom_keyboard_r3", r3_value},
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

   free(up_value);
   free(down_value);
   free(left_value);
   free(right_value);
   free(a_value);
   free(b_value);
   free(y_value);
   free(x_value);
   free(start_value);
   free(select_value);
   free(l_value);
   free(r_value);
   free(l2_value);
   free(r2_value);
   free(l3_value);
   free(r3_value);
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
   if(port == 0)
   {
      port0_device = device;
      set_input_descriptors();
   }
}

void retro_run(void)
{
   int i,j;
   bool updated=false;
   int16_t joypad_bits[2];

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated)
         && updated)
      check_variables();

   if (require_disk_rom && !sram_save_phase && disk_flush==FLUSH_TO_SRAM)
   {
      // disk is immediately loaded in retro_load_game() but RetroArch reads SRAM later. So we have to patch now.
      patch_disk();
      sram_save_phase=true;
   }

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
      for (i = 0; i < sizeof(keybemu1_map) / sizeof(keymap_t); i++)
         if (joypad_bits[0] & (1 << keybemu1_map[i].retro))
            KBD_SET(keybemu1_map[i].fmsx);
      break;

   case RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 2):
      for (i = 0; i < sizeof(keybemu2_map) / sizeof(keymap_t); i++)
         if (joypad_bits[0] & (1 << keybemu2_map[i].retro))
            KBD_SET(keybemu2_map[i].fmsx);
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

   RunZ80(&CPU);
   RenderAndPlayAudio(SND_RATE / fps);

   fflush(stdout);

   // debounce 1s before flushing to .DSK or SRAM memory
   if((disk_flush==FLUSH_IMMEDIATE||disk_flush==FLUSH_TO_SRAM) && FDD[0].Dirty && ++FDD[0].Dirty >= fps)
      flush_disk();

}

unsigned retro_api_version(void) { return RETRO_API_VERSION; }

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
   libretro_supports_bitmasks = false;
}
