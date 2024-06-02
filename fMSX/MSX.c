/** fMSX: portable MSX emulator ******************************/
/**                                                         **/
/**                          MSX.c                          **/
/**                                                         **/
/** This file contains implementation for the MSX-specific  **/
/** hardware: slots, memory mapper, PPIs, VDP, PSG, clock,  **/
/** etc. Initialization code and definitions needed for the **/
/** machine-dependent drivers are also here.                **/
/**                                                         **/
/** Copyright (C) Marat Fayzullin 1994-2021                 **/
/**     You are not allowed to distribute this software     **/
/**     commercially. Please, notify me, if you make any    **/
/**     changes to this file.                               **/
/*************************************************************/

#include "MSX.h"
#include "Sound.h"
#include "Floppy.h"
#include "SHA1.h"
#include "MCF.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif
#include <time.h>

#include <compat/strl.h>
#include <streams/file_stream_transforms.h>

extern retro_log_printf_t log_cb;

#define RGB2INT(R,G,B)    ((B)|((int)(G)<<8)|((int)(R)<<16))

/* MSDOS chdir() is broken and has to be replaced :( */
#ifdef MSDOS
#include "LibMSDOS.h"
#define chdir(path) ChangeDir(path)
#endif

#ifdef _MSC_VER
#undef chdir
#undef getcwd
#define chdir _chdir
#define getcwd _getcwd
#endif

#ifdef __PS3__
#define	getcwd(a,b)	"/dev_hdd0/game/RETROARCH/USRDIR/"
#define chdir(a) 0
#endif

/** User-defined parameters for fMSX *************************/
int  Mode        = MSX_MSX2|MSX_NTSC|MSX_MSXDOS2|MSX_GUESSA|MSX_GUESSB;
uint8_t UPeriod  = 75;             /* % of frames to draw    */
int  VPeriod     = CPU_VPERIOD;    /* CPU cycles per VBlank  */
int  HPeriod     = CPU_HPERIOD;    /* CPU cycles per HBlank  */
int  RAMPages    = 4;              /* Number of RAM pages    */
int  VRAMPages   = 2;              /* Number of VRAM pages   */
int  VRAMPageMask = 0x01;          /* VRAM page mask */
uint8_t ExitNow     = 0;           /* 1 = Exit the emulator  */

/** Main hardware: CPU, RAM, VRAM, mappers *******************/
Z80 CPU;                           /* Z80 CPU state and regs */

uint8_t *VRAM,*VPAGE;                 /* Video RAM              */

uint8_t *RAM[8];                      /* Main RAM (8x8kB pages) */
uint8_t *EmptyRAM;                    /* Empty RAM page (8kB)   */
uint8_t SaveCMOS;                     /* Save CMOS.ROM on exit  */
uint8_t *MemMap[4][4][8]; /* Memory maps [PPage][SPage][Addr>>13] */

uint8_t *RAMData;                     /* RAM Mapper contents    */
uint8_t RAMMapper[4];                 /* RAM Mapper state       */
uint8_t RAMMask;                      /* RAM Mapper mask        */

uint8_t *ROMData[MAXSLOTS];           /* ROM Mapper contents    */
uint8_t ROMMapper[MAXSLOTS][4];       /* ROM Mappers state      */
uint8_t ROMMask[MAXSLOTS];            /* ROM Mapper masks       */
uint8_t ROMType[MAXSLOTS];            /* ROM Mapper types       */

uint8_t EnWrite[4];                   /* 1 if write enabled     */
uint8_t PSL[4],SSL[4];                /* Lists of current slots */
uint8_t PSLReg,SSLReg[4];   /* Storage for A8h port and (FFFFh) */

/** Memory blocks to free in TrashMSX() **********************/
void *Chunks[MAXCHUNKS];           /* Memory blocks to free  */
int NChunks;                       /* Number of memory blcks */

/** Working directory names **********************************/
const char *ProgDir = 0;           /* Program directory      */
char *WorkDir = 0;                 /* Working directory      */

/** Cartridge files used by fMSX *****************************/
const char *ROMName[MAXCARTS] = { "CARTA.ROM","CARTB.ROM" };

/** On-cartridge SRAM data ***********************************/
char *SRAMName[MAXSLOTS] = {0,0,0,0,0,0};/* Filenames (gen-d)*/
uint8_t SaveSRAM[MAXSLOTS] = {0,0,0,0,0,0}; /* Save SRAM on exit*/
uint8_t *SRAMData[MAXSLOTS];          /* SRAM (battery backed)  */

/** Disk images used by fMSX *********************************/
const char *DSKName[MAXDRIVES] = { "DRIVEA.DSK","DRIVEB.DSK" };
uint8_t DiskROMLoaded = 0;            /* 1 when DISK.ROM loaded */

/** Fixed font used by fMSX **********************************/
const char *FNTName = "DEFAULT.FNT"; /* Font file for text   */
uint8_t *FontBuf;                     /* Font for text modes    */

/** Cassette tape ********************************************/
const char *CasName = "DEFAULT.CAS";  /* Tape image file     */
RFILE *CasStream;
uint8_t tape_type = NO_TAPE;
#define TAPE_HEADER_LEN 10
// header values copied from openMSX CasImage.cc
const char ASCII_HEADER[TAPE_HEADER_LEN]  = { 0xEA,0xEA,0xEA,0xEA,0xEA,0xEA,0xEA,0xEA,0xEA,0xEA };
const char BINARY_HEADER[TAPE_HEADER_LEN] = { 0xD0,0xD0,0xD0,0xD0,0xD0,0xD0,0xD0,0xD0,0xD0,0xD0 };
const char BASIC_HEADER[TAPE_HEADER_LEN]  = { 0xD3,0xD3,0xD3,0xD3,0xD3,0xD3,0xD3,0xD3,0xD3,0xD3 };

/** Kanji font ROM *******************************************/
uint8_t *Kanji;                       /* Kanji ROM 4096x32      */
int  KanLetter;                    /* Current letter index   */
uint8_t KanCount;                     /* Byte count 0..31       */

/** Keyboard, joystick, and mouse ****************************/
volatile uint8_t KeyState[16];        /* Keyboard map state     */
uint16_t JoyState;                     /* Joystick states        */
int  MouState[2];                  /* Mouse states           */
uint8_t MouseDX[2],MouseDY[2];        /* Mouse offsets          */
uint8_t OldMouseX[2],OldMouseY[2];    /* Old mouse coordinates  */
uint8_t MCount[2];                    /* Mouse nibble counter   */

/** General I/O registers: i8255 *****************************/
I8255 PPI;                         /* i8255 PPI at A8h-ABh   */
uint8_t IOReg;                        /* Storage for AAh port   */

/** Disk controller: WD1793 **********************************/
WD1793 FDC;                        /* WD1793 at 7FF8h-7FFFh  */
FDIDisk FDD[NUM_FDI_DRIVES];       /* Floppy disk images     */

/** Sound hardware: PSG, SCC, OPLL ***************************/
AY8910 PSG;                        /* PSG registers & state  */
YM2413 OPLL;                /* OPLL registers & state (fMSX) */
YM2413_NukeYKT OPLL_NukeYKT;/*OPLL registers & state(NukeYKT)*/
SCC  SCChip;                       /* SCC registers & state  */
uint8_t SCCOn[2];                  /* !=0: SCC page active   */
uint8_t SCCIMode[2];               /* SCC-I mode register    */
uint8_t *SCCIRAM;                /* SCC-I RAM (16x8kB pages) */
uint16_t FMPACKey;                 /* MAGIC = SRAM active    */

/** Real-time clock ******************************************/
uint8_t RTCReg,RTCMode;               /* RTC register numbers   */
uint8_t RTC[4][13];                   /* RTC registers          */

/** Video processor ******************************************/
uint8_t *ChrGen,*ChrTab,*ColTab;      /* VDP tables (screen)    */
uint8_t *SprGen,*SprTab;              /* VDP tables (sprites)   */
int  ChrGenM,ChrTabM,ColTabM;      /* VDP masks (screen)     */
int  SprTabM;                      /* VDP masks (sprites)    */
uint16_t VAddr;                        /* VRAM address in VDP    */
uint8_t VKey,PKey;                    /* Status keys for VDP    */
uint8_t FGColor,BGColor;              /* Colors                 */
uint8_t XFGColor,XBGColor;            /* Second set of colors   */
uint8_t ScrMode;                      /* Current screen mode    */
uint8_t VDP[64],VDPStatus[16];        /* VDP registers          */
uint8_t IRQPending;                   /* Pending interrupts     */
int  ScanLine;                     /* Current scanline       */
uint8_t VDPData;                      /* VDP data buffer        */
uint8_t PLatch;                       /* Palette buffer         */
uint8_t ALatch;                       /* Address buffer         */
int  Palette[16];                  /* Current palette        */

/** Cheat entries ********************************************/
int MCFCount     = 0;              /* Size of MCFEntries[]   */
MCFEntry MCFEntries[MAXCHEATS];    /* Entries from .MCF file */

/** Cheat codes **********************************************/
uint8_t CheatsON    = 0;              /* 1: Cheats are on       */
int  CheatCount  = 0;              /* # cheats, <=MAXCHEATS  */
CheatCode CheatCodes[MAXCHEATS];

/** Places in DiskROM to be patched with ED FE C9 ************/
static const uint16_t DiskPatches[] =
{ 0x4010,0x4013,0x4016,0x401C,0x401F,0 };

/** Places in BIOS to be patched with ED FE C9 ***************/
static const uint16_t BIOSPatches[] =
{ 0x00E1,0x00E4,0x00E7,0x00EA,0x00ED,0x00F0,0x00F3,0 };

/** Cartridge map, by primary and secondary slots ************/
static const uint8_t CartMap[4][4] =
{ { 255,3,4,5 },{ 0,0,0,0 },{ 1,1,1,1 },{ 2,255,255,255 } };

/** Screen Mode Handlers [number of screens + 1] *************/
void (*RefreshLine[MAXSCREEN+2])(uint8_t Y) =
{
  RefreshLine0,   /* SCR 0:  TEXT 40x24  */
  RefreshLine1,   /* SCR 1:  TEXT 32x24  */
  RefreshLine2,   /* SCR 2:  BLK 256x192 */
  RefreshLine3,   /* SCR 3:  64x48x16    */
  RefreshLine4,   /* SCR 4:  BLK 256x192 */
  RefreshLine5,   /* SCR 5:  256x192x16  */
  RefreshLine6,   /* SCR 6:  512x192x4   */
  RefreshLine7,   /* SCR 7:  512x192x16  */
  RefreshLine8,   /* SCR 8:  256x192x256 */
  0,              /* SCR 9:  NONE        */
  RefreshLine10,  /* SCR 10: YAE 256x192 */
  RefreshLine10,  /* SCR 11: YAE 256x192 */
  RefreshLine12,  /* SCR 12: YJK 256x192 */
  RefreshLineTx80 /* SCR 0:  TEXT 80x24  */
};

/** VDP Address Register Masks *******************************/
static const struct { uint8_t R2,R3,R4,R5,M2,M3,M4,M5; } MSK[MAXSCREEN+2] =
{
  { 0x7F,0x00,0x3F,0x00,0x00,0x00,0x00,0x00 }, /* SCR 0:  TEXT 40x24  */
  { 0x7F,0xFF,0x3F,0xFF,0x00,0x00,0x00,0x00 }, /* SCR 1:  TEXT 32x24  */
  { 0x7F,0x80,0x3C,0xFF,0x00,0x7F,0x03,0x00 }, /* SCR 2:  BLK 256x192 */
  { 0x7F,0x00,0x3F,0xFF,0x00,0x00,0x00,0x00 }, /* SCR 3:  64x48x16    */
  { 0x7F,0x80,0x3C,0xFC,0x00,0x7F,0x03,0x03 }, /* SCR 4:  BLK 256x192 */
  { 0x60,0x00,0x00,0xFC,0x1F,0x00,0x00,0x03 }, /* SCR 5:  256x192x16  */
  { 0x60,0x00,0x00,0xFC,0x1F,0x00,0x00,0x03 }, /* SCR 6:  512x192x4   */
  { 0x20,0x00,0x00,0xFC,0x1F,0x00,0x00,0x03 }, /* SCR 7:  512x192x16  */
  { 0x20,0x00,0x00,0xFC,0x1F,0x00,0x00,0x03 }, /* SCR 8:  256x192x256 */
  { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 }, /* SCR 9:  NONE        */
  { 0x20,0x00,0x00,0xFC,0x1F,0x00,0x00,0x03 }, /* SCR 10: YAE 256x192 */
  { 0x20,0x00,0x00,0xFC,0x1F,0x00,0x00,0x03 }, /* SCR 11: YAE 256x192 */
  { 0x20,0x00,0x00,0xFC,0x1F,0x00,0x00,0x03 }, /* SCR 12: YJK 256x192 */
  { 0x7C,0xF8,0x3F,0x00,0x03,0x07,0x00,0x00 }  /* SCR 0:  TEXT 80x24  */
};

/** MegaROM Mapper Names *************************************/
static const char *ROMNames[MAXMAPPERS+1] =
{
  "GENERIC/8kB","GENERIC/16kB","KONAMI5/8kB",
  "KONAMI4/8kB","ASCII/8kB","ASCII/16kB",
  "GMASTER2/SRAM","FMPAC/SRAM","UNKNOWN"
};

/** Game Master SHA1s *************************************/
/* For Contra and Hai no Majutsushi, which need to be     */
/* inserted into slot 1 when combined with Game Master 2. */
/* Values taken from openMSX softwaredb.                  */
static const char *GameMaster2SlotSpecialSHA1s[16] =
{
  // Contra
  "1302d258c952e93666ecec12429d6d2c2f841f43",
  "90003c78975d00b1e5612fd00dffabb70d616ecd",
  "7964ba4c3c27b6a32d397157fd38dd1dc2f1e543",
  "424320762bcbbb6081b1e186e21accf758aeb935",
  "cc46a737acd729b2839ecd237d38b4a63cfb16cb",
  "bc06bd3d6f138da8f5d38b47e459b4d1942e49e4",

  // Hai no Majutsushi
  "14403ca71d287084569e6c2f4124d2712c665219",
  "25022c4f5cfc805e93026fd5fda781e9a1807474",
  "3ff54468a5acf1ad44e85bdc993044aaa3165a63",
  "d08d4e2a8d92c01551ff012a71a1f3e57fe2d09c",
  "90722d413913ab18462aa741f85b820e751bb50f",
  "c34a1c225d1a5fc41a998e23d1209b59b3b759bb",
  "f5e199a5b39bad10256d8e963f0da272ad359517",
  "4376342bf42334d98dbd3e35ef35460974269c58",
  "751571d21457d89e5c52c45657bfe9a617ae8e14",

  0
};

/** Keyboard Mapping *****************************************/
/** This keyboard mapping is used by KBD_SET()/KBD_RES()    **/
/** macros to modify KeyState[] bits.                       **/
/*************************************************************/
const uint8_t Keys[][2] =
{
  { 0,0x00 },{ 8,0x10 },{ 8,0x20 },{ 8,0x80 }, /* None,LEFT,UP,RIGHT */
  { 8,0x40 },{ 6,0x01 },{ 6,0x02 },{ 6,0x04 }, /* DOWN,SHIFT,CONTROL,GRAPH */
  { 7,0x20 },{ 7,0x08 },{ 6,0x08 },{ 7,0x40 }, /* BS,TAB,CAPSLOCK,SELECT */
  { 8,0x02 },{ 7,0x80 },{ 8,0x08 },{ 8,0x04 }, /* HOME,ENTER,DELETE,INSERT */
  { 6,0x10 },{ 7,0x10 },{ 6,0x20 },{ 6,0x40 }, /* COUNTRY,STOP,F1,F2 */
  { 6,0x80 },{ 7,0x01 },{ 7,0x02 },{ 9,0x08 }, /* F3,F4,F5,PAD0 */
  { 9,0x10 },{ 9,0x20 },{ 9,0x40 },{ 7,0x04 }, /* PAD1,PAD2,PAD3,ESCAPE */
  { 9,0x80 },{10,0x01 },{10,0x02 },{10,0x04 }, /* PAD4,PAD5,PAD6,PAD7 */
  { 8,0x01 },{ 0,0x02 },{ 2,0x01 },{ 0,0x08 }, /* SPACE,[!],["],[#] */
  { 0,0x10 },{ 0,0x20 },{ 0,0x80 },{ 2,0x01 }, /* [$],[%],[&],['] */
  { 1,0x02 },{ 0,0x01 },{ 1,0x01 },{ 1,0x08 }, /* [(],[)],[*],[=] */
  { 2,0x04 },{ 1,0x04 },{ 2,0x08 },{ 2,0x10 }, /* [,],[-],[.],[/] */
  { 0,0x01 },{ 0,0x02 },{ 0,0x04 },{ 0,0x08 }, /* 0,1,2,3 */
  { 0,0x10 },{ 0,0x20 },{ 0,0x40 },{ 0,0x80 }, /* 4,5,6,7 */
  { 1,0x01 },{ 1,0x02 },{ 1,0x80 },{ 1,0x80 }, /* 8,9,[:],[;] */
  { 2,0x04 },{ 1,0x08 },{ 2,0x08 },{ 2,0x10 }, /* [<],[=],[>],[?] */
  { 0,0x04 },{ 2,0x40 },{ 2,0x80 },{ 3,0x01 }, /* [@],A,B,C */
  { 3,0x02 },{ 3,0x04 },{ 3,0x08 },{ 3,0x10 }, /* D,E,F,G */
  { 3,0x20 },{ 3,0x40 },{ 3,0x80 },{ 4,0x01 }, /* H,I,J,K */
  { 4,0x02 },{ 4,0x04 },{ 4,0x08 },{ 4,0x10 }, /* L,M,N,O */
  { 4,0x20 },{ 4,0x40 },{ 4,0x80 },{ 5,0x01 }, /* P,Q,R,S */
  { 5,0x02 },{ 5,0x04 },{ 5,0x08 },{ 5,0x10 }, /* T,U,V,W */
  { 5,0x20 },{ 5,0x40 },{ 5,0x80 },{ 1,0x20 }, /* X,Y,Z,[[] */
  { 1,0x10 },{ 1,0x40 },{ 0,0x40 },{ 1,0x04 }, /* [\],[]],[^],[_] */
  { 2,0x02 },{ 2,0x40 },{ 2,0x80 },{ 3,0x01 }, /* [`],a,b,c */
  { 3,0x02 },{ 3,0x04 },{ 3,0x08 },{ 3,0x10 }, /* d,e,f,g */
  { 3,0x20 },{ 3,0x40 },{ 3,0x80 },{ 4,0x01 }, /* h,i,j,k */
  { 4,0x02 },{ 4,0x04 },{ 4,0x08 },{ 4,0x10 }, /* l,m,n,o */
  { 4,0x20 },{ 4,0x40 },{ 4,0x80 },{ 5,0x01 }, /* p,q,r,s */
  { 5,0x02 },{ 5,0x04 },{ 5,0x08 },{ 5,0x10 }, /* t,u,v,w */
  { 5,0x20 },{ 5,0x40 },{ 5,0x80 },{ 1,0x20 }, /* x,y,z,[{] */
  { 1,0x10 },{ 1,0x40 },{ 2,0x02 },{ 8,0x08 }, /* [|],[}],[~],DEL */
  {10,0x08 },{10,0x10 },                       /* PAD8,PAD9 */
  /* these 7 mappings are missing in fMSX */
  { 2,0x20 }, /* Int'l: DEAD / JP: _ and ろ */
  { 9,0x01 }, /* NUM* */
  { 9,0x02 }, /* NUM+ */
  { 9,0x04 }, /* NUM/ */
  {10,0x20 }, /* NUM- */
  {10,0x40 }, /* NUM, (not present on modern numeric pads!) */
  {10,0x80 }  /* NUM. */
};

/** Internal Functions ***************************************/
/** These functions are defined and internally used by the  **/
/** code in MSX.c.                                          **/
/*************************************************************/
uint8_t *LoadROM(const char *Name,int Size,uint8_t *Buf);
int  GuessROM(const uint8_t *Buf,int Size);
void SetMegaROM(int Slot,uint8_t P0,uint8_t P1,uint8_t P2,uint8_t P3);
void MapROM(uint16_t A,uint8_t V);       /* Switch MegaROM banks            */
void PSlot(uint8_t V);               /* Switch primary slots            */
void SSlot(uint8_t V);               /* Switch secondary slots          */
void VDPOut(uint8_t R,uint8_t V);       /* Write value into a VDP register */
void Printer(uint8_t V);             /* Send a character to a printer   */
void PPIOut(uint8_t New,uint8_t Old);   /* Set PPI bits (key click, etc.)  */
int  CheckSprites(void);          /* Check for sprite collisions     */
uint8_t RTCIn(uint8_t R);               /* Read RTC registers              */
uint8_t SetScreen(void);             /* Change screen mode              */
uint16_t SetIRQ(uint8_t IRQ);            /* Set/Reset IRQ                   */
uint16_t StateID(void);               /* Compute emulation state ID      */
int  ApplyCheats(void);           /* Apply RAM-based cheats          */

static int hasext(const char *FileName,const char *Ext);
static uint8_t *GetMemory(int Size); /* Get memory chunk                */
static void FreeMemory(const void *Ptr); /* Free memory chunk        */
static void FreeAllMemory(void);  /* Free all memory chunks          */

/** hasext() *************************************************/
/** Check if file name has given extension.                 **/
/*************************************************************/
static int hasext(const char *FileName,const char *Ext)
{
  const char *P;
  int J;

  /* Start searching from the end, terminate at directory name */
  for(P=FileName+strlen(FileName);(P>=FileName)&&(*P!='/')&&(*P!='\\');--P)
  {
    /* Locate start of the next extension */
    for(--P;(P>=FileName)&&(*P!='/')&&(*P!='\\')&&(*P!=*Ext);--P);
    /* If next extension not found, return FALSE */
    if((P<FileName)||(*P=='/')||(*P=='\\')) return(0);
    /* Compare with the given extension */
    for(J=0;P[J]&&Ext[J]&&(toupper(P[J])==toupper(Ext[J]));++J);
    /* If extension matches, return TRUE */
    if(!Ext[J]&&(!P[J]||(P[J]==*Ext))) return(1);
  }

  /* Extensions do not match */
  return(0);
}

/** GetMemory() **********************************************/
/** Allocate a memory chunk of given size using malloc().   **/
/** Store allocated address in Chunks[] for later disposal. **/
/*************************************************************/
static uint8_t *GetMemory(int Size)
{
  uint8_t *P;

  if((Size<=0)||(NChunks>=MAXCHUNKS)) return(0);
  P=(uint8_t *)malloc(Size);
  if(P) Chunks[NChunks++]=P;

  return(P);
}

/** FreeMemory() *********************************************/
/** Free memory allocated by a previous GetMemory() call.   **/
/*************************************************************/
static void FreeMemory(const void *Ptr)
{
  int J;

  /* Special case: we do not free EmptyRAM! */
  if(!Ptr||(Ptr==(void *)EmptyRAM)) return;

  for(J=0;(J<NChunks)&&(Ptr!=Chunks[J]);++J);
  if(J<NChunks)
  {
    free(Chunks[J]);
    for(--NChunks;J<NChunks;++J) Chunks[J]=Chunks[J+1];
  }
}

/** FreeAllMemory() ******************************************/
/** Free all memory allocated by GetMemory() calls.         **/
/*************************************************************/
static void FreeAllMemory(void)
{
  int J;

  for(J=0;J<NChunks;++J) free(Chunks[J]);
  NChunks=0;
}

/** StartMSX() ***********************************************/
/** Allocate memory, load ROM images, initialize hardware,  **/
/** CPU and start the emulation. This function returns 0 in **/
/** the case of failure.                                    **/
/*************************************************************/
int StartMSX(int NewMode,int NewRAMPages,int NewVRAMPages)
{
  /*** Joystick types: ***/
  static const char *JoyTypes[] =
  {
    "nothing","normal joystick",
    "mouse in joystick mode","mouse in real mode"
  };

  /*** CMOS ROM default values: ***/
  static const uint8_t RTCInit[4][13]  = // 4*13 _nibbles_ - only b3-0 are used
  { // see https://www.msx.org/wiki/Ricoh_RP-5C01
    {  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, // Current Time and Day - bypassed in RTCIn()
    {  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, // Alarm, Hour Mode, Year Type
//  {0xa, 0, 0, 1,0xd,1,15, 4, 7, 3, 0, 0, 0 }, // explicitly & correctly set Adjust, Screen, Beep, Logo color, Language & others, or ..
    {  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },  // .. will be initialized to default when zeroed - store with SET SCREEN
    {  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }  // Title, Passuint16_t, Prompt
  };

  int *T,I,J,K;
  uint8_t *P;
  uint16_t A;
  char *sha1;
  int FirstCart=0;

  /*** STARTUP CODE starts here: ***/

  T=(int *)"\01\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";
  /* Endian mismatch? */
#ifdef MSB_FIRST
  if(*T==1)
    return(0);
#else
  if(*T!=1)
    return(0);
#endif

  /* Zero (almost) everything */
  CasStream   = 0;
  FontBuf     = 0;
  RAMData     = 0;
  VRAM        = 0;
  Kanji       = 0;
  WorkDir     = 0;
  SaveCMOS    = 0;
  FMPACKey    = 0x0000;
  ExitNow     = 1; // libretro-fmsx: exit LoopZ80() always at scanline 192 to process a.o. controller inputs
  NChunks     = 0;
  CheatsON    = 0;
  CheatCount  = 0;
  MCFCount    = 0;
  DiskROMLoaded = 0;

  /* Zero cartridge related data */
  for(J=0;J<MAXSLOTS;++J)
  {
    ROMMask[J]  = 0;
    ROMData[J]  = 0;
    ROMType[J]  = 0;
    SRAMData[J] = 0;
    SRAMName[J] = 0;
    SaveSRAM[J] = 0;
  }

  /* UPeriod has to be in 1%..100% range */
  UPeriod=UPeriod<1? 1:UPeriod>100? 100:UPeriod;

  /* Allocate 16kB for the empty space (scratch RAM) */
  if(!(EmptyRAM=GetMemory(0x4000))) { return(0); }
  memset(EmptyRAM,NORAM,0x4000);

  /* Allocate 128kB (16x8kB pages) for SCC-I expanded RAM */
  if(!(Mode&MSX_NO_MEGARAM))
  {
    if(!(SCCIRAM=GetMemory(16*0x2000))) { return(0); }
    memset(SCCIRAM,0,16*0x2000);
  }

  /* Reset memory map to the empty space */
  for(I=0;I<4;++I)
    for(J=0;J<4;++J)
      for(K=0;K<8;++K)
        MemMap[I][J][K]=EmptyRAM;

  /* Save current directory */
  WorkDir = (char*)GetMemory(1024);
  if(ProgDir && !getcwd(WorkDir, 1024) && log_cb)
     log_cb(RETRO_LOG_ERROR,"StartMSX(): getcwd() failed\n");

  /* Set invalid modes and RAM/VRAM sizes before calling ResetMSX() */
  Mode      = ~NewMode;
  RAMPages  = 0;
  VRAMPages = 0;

  /* Try resetting MSX, allocating memory, loading ROMs */
  if((ResetMSX(NewMode,NewRAMPages,NewVRAMPages)^NewMode)&MSX_MODEL) return(0);
  if(!RAMPages||!VRAMPages) return(0);

  /* Change to the program directory */
  if(ProgDir && chdir(ProgDir)) { }

  /* Try loading font */
  if(FNTName)
    J=LoadFNT(FNTName);

  /* Try loading CMOS memory contents */
  if(!LoadROM("CMOS.ROM",sizeof(RTC),(uint8_t *)RTC))
     memcpy(RTC,RTCInit,sizeof(RTC));

  /* Try loading Kanji alphabet ROM */
  if((Kanji = LoadROM("KANJI.ROM",0x20000,0))) { }

  /* Try loading RS232 support ROM to slot */
  if((P=LoadROM("RS232.ROM",0x4000,0)))
  {
    MemMap[3][3][2]=P;
    MemMap[3][3][3]=P+0x2000;
  }

  /* Start loading system cartridges */
  J=MAXCARTS;

  /* If MSX2 or better and DiskROM present...  */
  /* ...try loading MSXDOS2 cartridge into 3:0 */
  if(!MODEL(MSX_MSX1)&&OPTION(MSX_MSXDOS2)&&(MemMap[3][1][2]!=EmptyRAM)&&!ROMData[2])
    if(LoadCart("MSXDOS2.ROM",2,MAP_GEN16))
      SetMegaROM(2,0,1,ROMMask[J]-1,ROMMask[J]);

  /* If MSX2 or better, load PAINTER cartridge */
  if(!MODEL(MSX_MSX1))
  {
    for(;(J<MAXSLOTS)&&ROMData[J];++J);
    if((J<MAXSLOTS)&&LoadCart("PAINTER.ROM",J,0)) ++J;
  }

  /* Load FMPAC cartridge */
  for(;(J<MAXSLOTS)&&ROMData[J];++J);
  if((J<MAXSLOTS)&&LoadCart("FMPAC.ROM",J,MAP_FMPAC)) ++J;

  /* Load Konami GameMaster2/GameMaster cartridges */
  J=0; // by default load GM2&GM in slot A
  if(OPTION(MSX_GMASTER))
  {
    if(ROMName[0])
    {
      sha1 = SHA1Sum(ROMName[0]);
      for(I=0;sha1 && GameMaster2SlotSpecialSHA1s[I];I++)
      {
        if(!strcmp(sha1, GameMaster2SlotSpecialSHA1s[I]))
        {
          J=1; // for 2 games, load GM2 in slot B
          break;
        }
      }
      if (sha1) free(sha1);
    }
    if(LoadCart("GMASTER2.ROM",J,MAP_GMASTER2))
    {
      if(J==0) FirstCart=1; // if GM2 in slot A then load game in slot B
    }
    else if(LoadCart("GMASTER.ROM",0,0))
      FirstCart=1; // load game in slot B
  }

  /* We are now back to working directory */
  if(WorkDir && chdir(WorkDir)) { }

  /* For each user cartridge slot, try loading cartridge */
  for(J=0;J+FirstCart<MAXCARTS;++J) LoadCart(ROMName[J],J+FirstCart,ROMGUESS(J)|ROMTYPE(J));

  /* Open cassette image */
  if(CasName&&ChangeTape(CasName)) { }

  /* Initialize floppy disk controller */
  Reset1793(&FDC,FDD,WD1793_INIT);

  /* Open disk images */
  for(J=0;J<MAXDRIVES;++J)
  {
    if(ChangeDisk(J,DSKName[J])) { }
  }

  /* Done with initialization */
  /* Start execution of the code */
  A=RunZ80(&CPU);

  /* Exiting emulation... */
  return(1);
}

/** TrashMSX() ***********************************************/
/** Free resources allocated by StartMSX().                 **/
/*************************************************************/
void TrashMSX(void)
{
  RFILE *F;
  int J;

  /* CMOS.ROM is saved in the program directory */
  if(ProgDir && chdir(ProgDir)) { }

  /* Save CMOS RAM, if present */
  if(SaveCMOS)
  {
    if(!(F=rfopen("CMOS.ROM","wb"))) SaveCMOS=0;
    else
    {
      if(rfwrite(RTC,1,sizeof(RTC),F)!=sizeof(RTC)) SaveCMOS=0;
      rfclose(F);
    }
  }

  /* Change back to working directory */
  if(WorkDir && chdir(WorkDir)) { }

  /* Eject disks, free disk buffers */
  Reset1793(&FDC,FDD,WD1793_EJECT);

  /* Close tape */
  ChangeTape(0);

  /* Eject all cartridges (will save SRAM) */
  for(J=0;J<MAXSLOTS;++J) LoadCart(0,J,ROMType[J]);

  /* Eject all disks */
  for(J=0;J<MAXDRIVES;++J) ChangeDisk(J,0);

  /* Free all remaining allocated memory */
  FreeAllMemory();
}

/** ResetMSX() ***********************************************/
/** Reset MSX hardware to new operating modes. Returns new  **/
/** modes, possibly not the same as NewMode.                **/
/*************************************************************/
int ResetMSX(int NewMode,int NewRAMPages,int NewVRAMPages)
{
  /*** VDP status register states: ***/
  static const uint8_t VDPSInit[16] = { 0x9F,0,0x6C,0,0,0,0,0,0,0,0,0,0,0,0,0 };

  /*** VDP control register states: ***/
  static const uint8_t VDPInit[64]  =
  {
    0x00,0x10,0xFF,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
  };

  /*** Initial palette: ***/
  static const unsigned int PalInit[16] =
  {
    0x00000000,0x00000000,0x0020C020,0x0060E060,
    0x002020E0,0x004060E0,0x00A02020,0x0040C0E0,
    0x00E02020,0x00E06060,0x00C0C020,0x00C0C080,
    0x00208020,0x00C040A0,0x00A0A0A0,0x00E0E0E0
  };

  uint8_t *P1,*P2;
  int J,I;

  /* If changing hardware model, load new system ROMs */
  if((Mode^NewMode)&MSX_MODEL)
  {
    /* Change to the program directory */
    if(ProgDir && chdir(ProgDir)) { }

    switch(NewMode&MSX_MODEL)
    {
      case MSX_MSX1:
        P1=LoadROM("MSX.ROM",0x8000,0);
        if(!P1) NewMode=(NewMode&~MSX_MODEL)|(Mode&MSX_MODEL);
        else
        {
          FreeMemory(MemMap[0][0][0]);
          FreeMemory(MemMap[3][1][0]);
          MemMap[0][0][0]=P1;
          MemMap[0][0][1]=P1+0x2000;
          MemMap[0][0][2]=P1+0x4000;
          MemMap[0][0][3]=P1+0x6000;
          MemMap[3][1][0]=EmptyRAM;
          MemMap[3][1][1]=EmptyRAM;
        }
        break;

      case MSX_MSX2:
        P1=LoadROM("MSX2.ROM",0x8000,0);
        P2=LoadROM("MSX2EXT.ROM",0x4000,0);
        if(!P1||!P2)
        {
          NewMode=(NewMode&~MSX_MODEL)|(Mode&MSX_MODEL);
          FreeMemory(P1);
          FreeMemory(P2);
        }
        else
        {
          FreeMemory(MemMap[0][0][0]);
          FreeMemory(MemMap[3][1][0]);
          MemMap[0][0][0]=P1;
          MemMap[0][0][1]=P1+0x2000;
          MemMap[0][0][2]=P1+0x4000;
          MemMap[0][0][3]=P1+0x6000;
          MemMap[3][1][0]=P2;
          MemMap[3][1][1]=P2+0x2000;
        }
        break;

      case MSX_MSX2P:
        P1=LoadROM("MSX2P.ROM",0x8000,0);
        P2=LoadROM("MSX2PEXT.ROM",0x4000,0);
        if(!P1||!P2)
        {
          NewMode=(NewMode&~MSX_MODEL)|(Mode&MSX_MODEL);
          FreeMemory(P1);
          FreeMemory(P2);
        }
        else
        {
          FreeMemory(MemMap[0][0][0]);
          FreeMemory(MemMap[3][1][0]);
          MemMap[0][0][0]=P1;
          MemMap[0][0][1]=P1+0x2000;
          MemMap[0][0][2]=P1+0x4000;
          MemMap[0][0][3]=P1+0x6000;
          MemMap[3][1][0]=P2;
          MemMap[3][1][1]=P2+0x2000;
        }
        break;

      default:
        /* Unknown MSX model, keep old model */
        NewMode=(NewMode&~MSX_MODEL)|(Mode&MSX_MODEL);
        break;
    }

    /* Change to the working directory */
    if(WorkDir && chdir(WorkDir)) { }
  }

  /* If hardware model changed ok, patch freshly loaded BIOS */
  if((Mode^NewMode)&MSX_MODEL)
  {
    /* Apply patches to BIOS */
    for(J=0;BIOSPatches[J];++J)
    {
      P1=MemMap[0][0][0]+BIOSPatches[J];
      P1[0]=0xED;P1[1]=0xFE;P1[2]=0xC9;
    }
  }

  /* If toggling BDOS patches... */
  if((Mode^NewMode)&MSX_PATCHBDOS)
  {
    /* Change to the program directory */
    if(ProgDir && chdir(ProgDir)) { }

    /* Try loading DiskROM */
    P1=LoadROM("DISK.ROM",0x4000,0);
    DiskROMLoaded=!!P1;

    /* Change to the working directory */
    if(WorkDir && chdir(WorkDir)) { }

    /* If failed loading DiskROM, ignore the new PATCHBDOS bit */
    if(!P1) NewMode=(NewMode&~MSX_PATCHBDOS)|(Mode&MSX_PATCHBDOS);
    else
    {
      /* Assign new DiskROM */
      FreeMemory(MemMap[3][1][2]);
      MemMap[3][1][2]=P1;
      MemMap[3][1][3]=P1+0x2000;

      /* If BDOS patching requested... */
      if(NewMode&MSX_PATCHBDOS)
      {
        /* Apply patches to BDOS */
        for(J=0;DiskPatches[J];++J)
        {
          P2=P1+DiskPatches[J]-0x4000;
          P2[0]=0xED;P2[1]=0xFE;P2[2]=0xC9;
        }
      }
    }
  }

  /* Assign new modes */
  Mode           = NewMode;

  /* Set ROM types for cartridges A/B */
  ROMType[0]     = ROMTYPE(0);
  ROMType[1]     = ROMTYPE(1);

  /* Set CPU timings */
  VPeriod        = (VIDEO(MSX_PAL)? VPERIOD_PAL:VPERIOD_NTSC)/6;
  HPeriod        = HPERIOD/6;
  CPU.IPeriod    = CPU_H240;
  CPU.IAutoReset = 0;

  /* Numbers of RAM pages should be power of 2 */
  for(J=1;J<NewRAMPages;J<<=1);
  NewRAMPages=J;

  /* Correct RAM and VRAM sizes */
  if((NewRAMPages<(MODEL(MSX_MSX1)? 4:8))||(NewRAMPages>256))
    NewRAMPages=MODEL(MSX_MSX1)? 4:8; // MSX1 min&default: 64KiB, MSX2(+) min&default: 128KiB. Max 4MiB
  if((NewVRAMPages<(MODEL(MSX_MSX1)? 2:8))||(NewVRAMPages>12))
    NewVRAMPages=MODEL(MSX_MSX1)? 2:8; // MSX1 min&default: 32KiB, MSX2(+) min&default: 128KiB. Max 192KiB (nonstandard)

  /* If changing amount of RAM... */
  if(NewRAMPages!=RAMPages)
  {
    if((P1=GetMemory(NewRAMPages*0x4000)))
    {
      memset(P1,NORAM,NewRAMPages*0x4000);
      FreeMemory(RAMData);
      RAMPages = NewRAMPages;
      RAMMask  = NewRAMPages-1;
      RAMData  = P1;
    }
  }

  /* If changing amount of VRAM... */
  if(NewVRAMPages!=VRAMPages)
  {
    if((P1=GetMemory(NewVRAMPages*0x4000)))
    {
      memset(P1,0x00,NewVRAMPages*0x4000);
      FreeMemory(VRAM);
      VRAMPages = NewVRAMPages;
      VRAM      = P1;
      for(J=1;J<VRAMPages;J<<=1);
      VRAMPageMask = J-1;
    }
  }

  /* For all slots... */
  for(J=0;J<4;++J)
  {
    /* Slot is currently read-only */
    EnWrite[J]          = 0;
    /* PSL=0:0:0:0, SSL=0:0:0:0 */
    PSL[J]              = 0;
    SSL[J]              = 0;
    /* RAMMap=3:2:1:0 */
    MemMap[3][2][J*2]   = RAMData+(3-J)*0x4000;
    MemMap[3][2][J*2+1] = MemMap[3][2][J*2]+0x2000;
    RAMMapper[J]        = 3-J;
    /* Setting address space */
    RAM[J*2]            = MemMap[0][0][J*2];
    RAM[J*2+1]          = MemMap[0][0][J*2+1];
  }

  /* For all MegaROMs... */
  for(J=0;J<MAXSLOTS;++J)
    if((I=ROMMask[J]+1)>4)
    {
      /* For normal MegaROMs, set first four pages */
      if((ROMData[J][0]=='A')&&(ROMData[J][1]=='B'))
        SetMegaROM(J,0,1,2,3);
      /* Some MegaROMs default to last pages on reset */
      else if((ROMData[J][(I-2)<<13]=='A')&&(ROMData[J][((I-2)<<13)+1]=='B'))
        SetMegaROM(J,I-2,I-1,I-2,I-1);
      /* If 'AB' signature is not found at the beginning or the end */
      /* then it is not a MegaROM but rather a plain 64kB ROM       */
    }

  /* Reset sound chips */
  Reset8910(&PSG,PSG_CLOCK,FIRST_AY8910_CHANNEL);
  ResetSCC(&SCChip,FIRST_SCC_CHANNEL);
  Reset2413(&OPLL,FIRST_YM2413_CHANNEL);
  NukeYKT_Reset2413(&OPLL_NukeYKT);
  Sync8910(&PSG,AY8910_SYNC);
  SyncSCC(&SCChip,SCC_SYNC);
  Sync2413(&OPLL,YM2413_SYNC);

  /* Reset PPI chips and slot selectors */
  Reset8255(&PPI);
  PPI.Rout[0]=PSLReg=0x00;
  PPI.Rout[2]=IOReg=0x00;
  SSLReg[0]=0x00;
  SSLReg[1]=0x00;
  SSLReg[2]=0x00;
  SSLReg[3]=0x00;

  /* Reset floppy disk controller */
  Reset1793(&FDC,FDD,WD1793_KEEP);

  /* Reset VDP */
  memcpy(VDP,VDPInit,sizeof(VDP));
  memcpy(VDPStatus,VDPSInit,sizeof(VDPStatus));

  /* Reset keyboard */
  memset((void *)KeyState,0xFF,16);

  /* Set initial palette */
  for(J=0;J<16;++J)
  {
    Palette[J]=PalInit[J];
    SetColor(J,(Palette[J]>>16)&0xFF,(Palette[J]>>8)&0xFF,Palette[J]&0xFF);
  }

  /* Reset mouse coordinates/counters */
  for(J=0;J<2;++J)
    MouState[J]=MouseDX[J]=MouseDY[J]=OldMouseX[J]=OldMouseY[J]=MCount[J]=0;

  IRQPending=0x00;                      /* No IRQs pending  */
  SCCOn[0]=SCCOn[1]=0;                  /* SCCs off for now */
  SCCIMode[0]=SCCIMode[1]=0;            /* bankselect/SCC   */
  RTCReg=RTCMode=0;                     /* Clock registers  */
  KanCount=0;KanLetter=0;               /* Kanji extension  */
  ChrTab=ColTab=ChrGen=VRAM;            /* VDP tables       */
  SprTab=SprGen=VRAM;
  ChrTabM=ColTabM=ChrGenM=SprTabM=~0;   /* VDP addr. masks  */
  VPAGE=VRAM;                           /* VRAM page        */
  FGColor=BGColor=XFGColor=XBGColor=0;  /* VDP colors       */
  ScrMode=0;                            /* Screen mode      */
  VKey=PKey=1;                          /* VDP keys         */
  VAddr=0x0000;                         /* VRAM access addr */
  ScanLine=0;                           /* Current scanline */
  VDPData=NORAM;                        /* VDP data buffer  */
  JoyState=0;                           /* Joystick state   */

  /* Set "V9958" VDP version for MSX2+ */
  if(MODEL(MSX_MSX2P)) VDPStatus[1]|=0x04;

  /* Reset CPU */
  ResetZ80(&CPU);

  /* Done */
  return(Mode);
}

/** RdZ80() **************************************************/
/** Z80 emulation calls this function to read a uint8_t from   **/
/** address A in the Z80 address space. Also see OpZ80() in **/
/** Z80.c which is a simplified code-only RdZ80() version.  **/
/*************************************************************/
uint8_t RdZ80(uint16_t A)
{
  uint8_t J,PS,SS,I;

  /* Secondary slot selector */
  if(A==0xFFFF /*&& ((PSL[3]==0 && !MODEL(MSX_MSX1)) || PSL[3]==3)*/) return(~SSLReg[PSL[3]]); // might be wrong - should only read back inverse when the primary slot is expanded. Commented code would fix that.

  J  = A>>14;           /* 16kB page number 0-3  */
  PS = PSL[J];          /* Primary slot number   */
  SS = SSL[J];          /* Secondary slot number */

  /* Floppy disk controller */
  /* 7FF8h..7FFFh Standard DiskROM  */
  /* BFF8h..BFFFh MSX-DOS BDOS      */
  /* 7F80h..7F87h Arabic DiskROM    */
  /* 7FB8h..7FBFh SV738/TechnoAhead */
  if(PS==3&&SS==1)
    switch(A)
    {
      /* Standard      MSX-DOS       Arabic        SV738            */
      case 0x7FF8: case 0xBFF8: case 0x7F80: case 0x7FB8: /* STATUS */
      case 0x7FF9: case 0xBFF9: case 0x7F81: case 0x7FB9: /* TRACK  */
      case 0x7FFA: case 0xBFFA: case 0x7F82: case 0x7FBA: /* SECTOR */
      case 0x7FFB: case 0xBFFB: case 0x7F83: case 0x7FBB: /* DATA   */
        return(Read1793(&FDC,A&0x0003));
      case 0x7FFF: case 0xBFFF: case 0x7F84: case 0x7FBC: /* SYSTEM */
        return(Read1793(&FDC,WD1793_READY));
    }

  I = CartMap[PS][SS]; /* Cartridge number      */
  if(I<2 && SCCOn[I] && ((A&0xD800)==0x9800))
  {
    J=A&0x00FF;
    return (A&0x2000) ? ReadSCCP(&SCChip,J) : ReadSCC(&SCChip,J);
  }

  /* Default to reading memory */
  return(RAM[A>>13][A&0x1FFF]);
}

/** WrZ80() **************************************************/
/** Z80 emulation calls this function to write uint8_t V to    **/
/** address A of Z80 address space.                         **/
/*************************************************************/
void WrZ80(uint16_t A,uint8_t V)
{
  /* Secondary slot selector */
  if(A==0xFFFF) { SSlot(V);return; }

  /* Floppy disk controller */
  /* 7FF8h..7FFFh Standard DiskROM  */
  /* BFF8h..BFFFh MSX-DOS BDOS      */
  /* 7F80h..7F87h Arabic DiskROM    */
  /* 7FB8h..7FBFh SV738/TechnoAhead */
  if(((A&0x3F88)==0x3F88)&&(PSL[A>>14]==3)&&(SSL[A>>14]==1))
    switch(A)
    {
      /* Standard      MSX-DOS       Arabic        SV738             */
      case 0x7FF8: case 0xBFF8: case 0x7F80: case 0x7FB8: /* COMMAND */
      case 0x7FF9: case 0xBFF9: case 0x7F81: case 0x7FB9: /* TRACK   */
      case 0x7FFA: case 0xBFFA: case 0x7F82: case 0x7FBA: /* SECTOR  */
      case 0x7FFB: case 0xBFFB: case 0x7F83: case 0x7FBB: /* DATA    */
        Write1793(&FDC,A&0x0003,V);
        return;
      case 0xBFFC: /* Standard/MSX-DOS */
      case 0x7FFC: /* Side: [xxxxxxxS] */
        Write1793(&FDC,WD1793_SYSTEM,FDC.Drive|S_DENSITY|(V&0x01? 0:S_SIDE));
        return;
      case 0xBFFD: /* Standard/MSX-DOS  */
      case 0x7FFD: /* Drive: [xxxxxxxD] */
        Write1793(&FDC,WD1793_SYSTEM,(V&0x01)|S_DENSITY|(FDC.Side? 0:S_SIDE));
        return;
      case 0x7FBC: /* Arabic/SV738 */
      case 0x7F84: /* Side/Drive/Motor: [xxxxMSDD] */
        Write1793(&FDC,WD1793_SYSTEM,(V&0x03)|S_DENSITY|(V&0x04? 0:S_SIDE));
        return;
    }

  /* Write to RAM, if enabled */
  if(EnWrite[A>>14]) { RAM[A>>13][A&0x1FFF]=V;return; }

  /* Switch MegaROM pages */
  if((A>0x3FFF)&&(A<0xC000)) MapROM(A,V);
}

/** InZ80() **************************************************/
/** Z80 emulation calls this function to read a uint8_t from   **/
/** a given I/O port.                                       **/
/*************************************************************/
uint8_t InZ80(uint16_t Port)
{
  /* MSX only uses 256 IO ports */
  Port&=0xFF;

  /* Return an appropriate port value */
  switch(Port)
  {

case 0x90: return(0xFD);                   /* Printer READY signal */
case 0xB5: return(RTCIn(RTCReg));          /* RTC registers        */

case 0xA8: /* Primary slot state   */
case 0xA9: /* Keyboard port        */
case 0xAA: /* General IO register  */
case 0xAB: /* PPI control register */
  PPI.Rin[1]=KeyState[PPI.Rout[2]&0x0F];
  return(Read8255(&PPI,Port-0xA8));

case 0xFC: /* Mapper page at 0000h */
case 0xFD: /* Mapper page at 4000h */
case 0xFE: /* Mapper page at 8000h */
case 0xFF: /* Mapper page at C000h */
  return(RAMMapper[Port-0xFC]|~RAMMask);

case 0xD9: /* Kanji support */
  Port=Kanji? Kanji[KanLetter+KanCount]:NORAM;
  KanCount=(KanCount+1)&0x1F;
  return(Port);

case 0x80: /* SIO data */
case 0x81:
case 0x82:
case 0x83:
case 0x84:
case 0x85:
case 0x86:
case 0x87:
  return(NORAM);
  /*return(Rd8251(&SIO,Port&0x07));*/

case 0x98: /* VRAM read port */
  /* Read from VRAM data buffer */
  Port=VDPData;
  /* Reset VAddr latch sequencer */
  VKey=1;
  /* Fill data buffer with a new value */
  VDPData=VPAGE[VAddr];
  /* Increment VRAM address */
  VAddr=(VAddr+1)&0x3FFF;
  /* If rolled over, modify VRAM page# */
  if(!VAddr&&(ScrMode>3))
  {
    VDP[14]=(VDP[14]+1)&VRAMPageMask;
    VPAGE=VRAM+((int)VDP[14]<<14);
  }
  return(Port);

case 0x99: /* VDP status registers */
  /* Read an appropriate status register */
  Port=VDPStatus[VDP[15]];
  /* Reset VAddr latch sequencer */
// @@@ This breaks Sir Lancelot on ColecoVision, so it must be wrong!
//  VKey=1;
  /* Update status register's contents */
  switch(VDP[15])
  {
    case 0: VDPStatus[0]&=0x5F;SetIRQ(~INT_IE0);break;
    case 1: VDPStatus[1]&=0xFE;SetIRQ(~INT_IE1);break;
    case 7: VDPStatus[7]=VDP[44]=VDPRead();break;
  }
  /* Return the status register value */
  return(Port);

case 0xA2: /* PSG input port */
  /* PSG[14] returns joystick/mouse data */
  if(PSG.Latch==14)
  {
    int DX,DY,L,J;

    /* Number of a joystick port */
    Port = (PSG.R[15]&0x40)>>6;
    L    = JOYTYPE(Port);

    /* If no joystick, return dummy value */
    if(L==JOY_NONE) return(0x7F);

    /* Compute mouse offsets, if needed */
    if(MCount[Port]==1)
    {
      /* Get new mouse coordinates */
      DX=MouState[Port]&0xFF;
      DY=(MouState[Port]>>8)&0xFF;
      /* Compute offsets and store coordinates  */
      J=OldMouseX[Port]-DX;OldMouseX[Port]=DX;DX=J;
      J=OldMouseY[Port]-DY;OldMouseY[Port]=DY;DY=J;
      /* For 512-wide mode, double horizontal offset */
      if((ScrMode==6)||((ScrMode==7)&&!ModeYJK)||(ScrMode==MAXSCREEN+1)) DX<<=1;
      /* Adjust offsets */
      MouseDX[Port]=(DX>127? 127:(DX<-127? -127:DX))&0xFF;
      MouseDY[Port]=(DY>127? 127:(DY<-127? -127:DY))&0xFF;
    }

    /* Get joystick state */
    J=~(Port? (JoyState>>8):JoyState)&0x3F;

    /* Determine return value */
    switch(MCount[Port])
    {
      case 0: Port=PSG.R[15]&(0x10<<Port)? 0x3F:J;break;
      case 1: Port=(MouseDX[Port]>>4)|(J&0x30);break;
      case 2: Port=(MouseDX[Port]&0x0F)|(J&0x30);break;
      case 3: Port=(MouseDY[Port]>>4)|(J&0x30);break;
      case 4: Port=(MouseDY[Port]&0x0F)|(J&0x30);break;
    }

    /* 6th bit is always 1 */
    return(Port|0x40);
  }

  /* PSG[15] resets mouse counters (???) */
  if(PSG.Latch==15)
  {
    /*MCount[0]=MCount[1]=0;*/
    return(PSG.R[15]&0xF0);
  }

  /* Return PSG[0-13] as they are */
  return(RdData8910(&PSG));

case 0xD0: /* FDC status  */
case 0xD1: /* FDC track   */
case 0xD2: /* FDC sector  */
case 0xD3: /* FDC data    */
case 0xD4: /* FDC IRQ/DRQ */
  /* Brazilian DiskROM I/O ports */
  return(Read1793(&FDC,Port-0xD0));

  }

  /* Return NORAM for non-existing ports */
  return(NORAM);
}

/** OutZ80() *************************************************/
/** Z80 emulation calls this function to write uint8_t V to a  **/
/** given I/O port.                                         **/
/*************************************************************/
void OutZ80(uint16_t Port,uint8_t Value)
{
  uint8_t I,J;

  Port&=0xFF;
  switch(Port)
  {
case 0x7C:
  /* OPLL Register# */
  WrCtrl2413(&OPLL,Value);
  NukeYKT_WritePort2413(&OPLL_NukeYKT,NUKEYKT_REGISTER_PORT,Value);
  return;
case 0x7D:
  /* OPLL Data      */
  WrData2413(&OPLL,Value);
  NukeYKT_WritePort2413(&OPLL_NukeYKT,NUKEYKT_DATA_PORT,Value);
  return;
case 0x91: Printer(Value);return;                 /* Printer Data   */
case 0xA0: WrCtrl8910(&PSG,Value);return;         /* PSG Register#  */
case 0xB4: RTCReg=Value&0x0F;return;              /* RTC Register#  */

case 0xD8: /* Upper bits of Kanji ROM address */
  KanLetter=(KanLetter&0x1F800)|((int)(Value&0x3F)<<5);
  KanCount=0;
  return;

case 0xD9: /* Lower bits of Kanji ROM address */
  KanLetter=(KanLetter&0x007E0)|((int)(Value&0x3F)<<11);
  KanCount=0;
  return;

case 0x80: /* SIO data */
case 0x81:
case 0x82:
case 0x83:
case 0x84:
case 0x85:
case 0x86:
case 0x87:
  return;
  /*Wr8251(&SIO,Port&0x07,Value);
  return;*/

case 0x98: /* VDP Data */
  VKey=1;
  VDPData=VPAGE[VAddr]=Value;
  VAddr=(VAddr+1)&0x3FFF;
  /* If VAddr rolled over, modify VRAM page# */
  if(!VAddr&&(ScrMode>3))
  {
    VDP[14]=(VDP[14]+1)&VRAMPageMask;
    VPAGE=VRAM+((int)VDP[14]<<14);
  }
  return;

case 0x99: /* VDP Address Latch */
  if(VKey) { ALatch=Value;VKey=0; }
  else
  {
    VKey=1;
    switch(Value&0xC0)
    {
      case 0x80:
        /* Writing into VDP registers */
        VDPOut(Value&0x3F,ALatch);
        break;
      case 0x00:
      case 0x40:
        /* Set the VRAM access address */
        VAddr=(((uint16_t)Value<<8)+ALatch)&0x3FFF;
        /* When set for reading, perform first read */
        if(!(Value&0x40))
        {
          VDPData=VPAGE[VAddr];
          VAddr=(VAddr+1)&0x3FFF;
          if(!VAddr&&(ScrMode>3))
          {
            VDP[14]=(VDP[14]+1)&VRAMPageMask;
            VPAGE=VRAM+((int)VDP[14]<<14);
          }
        }
        break;
    }
  }
  return;

case 0x9A: /* VDP Palette Latch */
  if(PKey) { PLatch=Value;PKey=0; }
  else
  {
    uint8_t R,G,B;
    /* New palette entry written */
    PKey=1;
    J=VDP[16];
    /* Compute new color components */
    R=(PLatch&0x70)*255/112;
    G=(Value&0x07)*255/7;
    B=(PLatch&0x07)*255/7;
    /* Set new color for palette entry J */
    Palette[J]=RGB2INT(R,G,B);
    SetColor(J,R,G,B);
    /* Next palette entry */
    VDP[16]=(J+1)&0x0F;
  }
  return;

case 0x9B: /* VDP Register Access */
  J=VDP[17]&0x3F;
  if(J!=17) VDPOut(J,Value);
  if(!(VDP[17]&0x80)) VDP[17]=(J+1)&0x3F;
  return;

case 0xA1: /* PSG Data */
  /* PSG[15] is responsible for joystick/mouse */
  if(PSG.Latch==15)
  {
    /* For mouse, update nibble counter      */
    /* For joystick, set nibble counter to 0 */
    if((Value&0x0C)==0x0C) MCount[1]=0;
    else if((JOYTYPE(1)==JOY_MOUSE)&&((Value^PSG.R[15])&0x20))
           MCount[1]+=MCount[1]==4? -3:1;

    /* For mouse, update nibble counter      */
    /* For joystick, set nibble counter to 0 */
    if((Value&0x03)==0x03) MCount[0]=0;
    else if((JOYTYPE(0)==JOY_MOUSE)&&((Value^PSG.R[15])&0x10))
           MCount[0]+=MCount[0]==4? -3:1;
  }

  /* Put value into a register */
  WrData8910(&PSG,Value);
  return;

case 0xA8: /* Primary slot state   */
case 0xA9: /* Keyboard port        */
case 0xAA: /* General IO register  */
case 0xAB: /* PPI control register */
  /* Write to PPI */
  Write8255(&PPI,Port-0xA8,Value);
  /* If general I/O register has changed... */
  if(PPI.Rout[2]!=IOReg) { PPIOut(PPI.Rout[2],IOReg);IOReg=PPI.Rout[2]; }
  /* If primary slot state has changed... */
  if(PPI.Rout[0]!=PSLReg) PSlot(PPI.Rout[0]);
  /* Done */
  return;

case 0xB5: /* RTC Data */
  if(RTCReg<13)
  {
    /* J = register bank# now */
    J=RTCMode&0x03;
    /* Store the value */
    RTC[J][RTCReg]=Value&0x0f;
    /* If CMOS modified, we need to save it */
    if(J>1) SaveCMOS=1;
    return;
  }
  /* RTC[13] is a register bank# */
  if(RTCReg==13) RTCMode=Value;
  return;

case 0xD0: /* FDC command */
case 0xD1: /* FDC track   */
case 0xD2: /* FDC sector  */
case 0xD3: /* FDC data    */
  /* Brazilian DiskROM I/O ports */
  Write1793(&FDC,Port-0xD0,Value);
  return;

case 0xD4: /* FDC system  */
  /* Brazilian DiskROM drive/side: [xxxSxxDx] */
  Value=((Value&0x02)>>1)|S_DENSITY|(Value&0x10? 0:S_SIDE);
  Write1793(&FDC,WD1793_SYSTEM,Value);
  return;

case 0xFC: /* Mapper page at 0000h */
case 0xFD: /* Mapper page at 4000h */
case 0xFE: /* Mapper page at 8000h */
case 0xFF: /* Mapper page at C000h */
  J=Port-0xFC;
  Value&=RAMMask;
  if(RAMMapper[J]!=Value)
  {
    I=J<<1;
    RAMMapper[J]      = Value;
    MemMap[3][2][I]   = RAMData+((int)Value<<14);
    MemMap[3][2][I+1] = MemMap[3][2][I]+0x2000;
    if((PSL[J]==3)&&(SSL[J]==2))
    {
      EnWrite[J] = 1;
      RAM[I]     = MemMap[3][2][I];
      RAM[I+1]   = MemMap[3][2][I+1];
    }
  }
  return;

  }

  /* Unknown port */
}

/** MapROM() *************************************************/
/** Switch ROM Mapper pages. This function is supposed to   **/
/** be called when ROM page registers are written to.       **/
/*************************************************************/
void MapROM(uint16_t A,uint8_t V)
{
  uint8_t *P;
  uint8_t J  = A>>14;           /* 16kB page number 0-3  */
  uint8_t PS = PSL[J];          /* Primary slot number   */
  uint8_t SS = SSL[J];          /* Secondary slot number */
  uint8_t I  = CartMap[PS][SS]; /* Cartridge number      */

  /* Drop out if no cartridge in that slot */
  if(I>=MAXSLOTS) return;

  /* SCC handling for no cart */
  /* Note: we don't do SCC-I handling if we _do_ have a ROM cart - SCC-I was only available in 2 flavours, both without ROM. */
  if(I<2 && !ROMData[I] && 0x4000<=A && A<0xC000) // only support SCC(-I) in (unexpanded) external slots 1 & 2, page 1&2
  {
    if ((A|1)==0xBFFF && !(Mode&MSX_NO_MEGARAM))
    {
      SCCIMode[I]=V;
      return;
    }
    /* all banks: SCC-I RAM; SCC regs not writeable nor SCC bank switchable */
    /* did not implement SCCIMode b0-2 for individual bank handling */
    if (SCCIMode[I] & 0x10)
    {
      RAM[A>>13][A&0x1FFF]=V;
      return;
    }
	if ((A & 0x1800) == 0x1000) {
      /* bankselect mode & writing to SCC-I mapper - switch 8kB RAM page */
      J=(A>>13)-2; /* 8kB bank number 0-3 */
      switch (J)
      {
        case 2:
          SCCOn[I]=(V&0x3F)==0x3F; // SCC enable - write xx111111 to 9000-97FF
          break;
        case 3:
          SCCOn[I]=(V&0x80); // SCC-I a.k.a. SCC+ enable - write 1xxxxxxx to B000-B7FF
          break;
      }
      V&=0x0F; // bitmask for 16 pages
      if(V!=ROMMapper[I][J] && !(Mode&MSX_NO_MEGARAM) && SCCIRAM)
      {
        RAM[J+2]=MemMap[PS][SS][J+2]=SCCIRAM+((int)V<<13);
        ROMMapper[I][J]=V;
      }
      return;
    }
  }

  /* If writing to SCC... [whether we have a ROM in this slot or not] */
  if(I<2 && SCCOn[I] && ((A&0xD800)==0x9800)) // D8==1101.1000 => ignore upper b5 for SCC/SCC-I and b0-3 for 8 mirrored SCC register blocks
  {
    /* Compute SCC register number */
    J=A&0x00FF;

    /* If using SCC+... */
    if(A&0x2000)
    {
      /* When no MegaROM present, we allow the program */
      /* to write into SCC wave buffer using EmptyRAM  */
      /* as a scratch pad.                             */
      if(!ROMData[I]&&(J<0xA0)) EmptyRAM[0x1800+J]=V;

      /* Output data to SCC chip */
      WriteSCCP(&SCChip,J,V);
  }
  else
  {
      /* When no MegaROM present, we allow the program */
      /* to write into SCC wave buffer using EmptyRAM  */
      /* as a scratch pad.                             */
      if(!ROMData[I]&&(J<0x80)) EmptyRAM[0x1800+J]=V;

      /* Output data to SCC chip */
      WriteSCC(&SCChip,J,V);
    }

    /* Done writing to SCC */   
    return;
  }

  /* If no cartridge or no mapper, exit */
  if(!ROMData[I]||!ROMMask[I]) return;

  switch(ROMType[I])
  {
    case MAP_GEN8: /* Generic 8kB cartridges (Konami, etc.) */
      /* Only interested in writes to 4000h-BFFFh */
      if((A<0x4000)||(A>0xBFFF)) break;
      J=(A-0x4000)>>13;
      /* Turn SCC on/off on writes to 9000-97FF */
      if(J==2 && I<2 && (A&0x1800)==0x1000) SCCOn[I]=(V&0x3F)==0x3F;
      /* Turn SCC-I on/off on writes to B000-B7FF */
      else if(J==3 && I<2 && (A&0x1800)==0x1000) SCCOn[I]=(V&0x80);
      /* Switch ROM pages */
      V&=ROMMask[I];
      if(V!=ROMMapper[I][J])
      {
        RAM[J+2]=MemMap[PS][SS][J+2]=ROMData[I]+((int)V<<13);
        ROMMapper[I][J]=V;
      }
      return;

    case MAP_GEN16: /* Generic 16kB cartridges (MSXDOS2, HoleInOneSpecial) */
      /* Only interested in writes to 4000h-BFFFh */
      if((A<0x4000)||(A>0xBFFF)) break;
      J=(A&0x8000)>>14;
      /* Switch ROM pages */
      V=(V<<1)&ROMMask[I];
      if(V!=ROMMapper[I][J])
      {
        RAM[J+2]=MemMap[PS][SS][J+2]=ROMData[I]+((int)V<<13);
        RAM[J+3]=MemMap[PS][SS][J+3]=RAM[J+2]+0x2000;
        ROMMapper[I][J]=V;
        ROMMapper[I][J+1]=V|1;
      }
      return;

    case MAP_KONAMI5: /* KONAMI5 8kB cartridges */
      /* Only interested in writes to 5000h/7000h/9000h/B000h */
      if((A<0x5000)||(A>0xB000)||((A&0x1FFF)!=0x1000)) break;
      J=(A-0x5000)>>13;
      /* Turn SCC on/off on writes to 9000-97FF */
      /* note that the 'if' above already filtered anything but 9000. That ignores the mirrors - see https://www.msx.org/wiki/MegaROM_Mappers#Konami.27s_MegaROMs_with_SCC */
      if(J==2 && I<2 && (A&0x1800)==0x1000) SCCOn[I]=(V&0x3F)==0x3F;
      /* Turn SCC-I on/off on writes to B000-B7FF */
      else if(J==3 && I<2 && (A&0x1800)==0x1000) SCCOn[I]=(V&0x80);
      /* Switch ROM pages */
      V&=ROMMask[I];
      if(V!=ROMMapper[I][J])
      {
        RAM[J+2]=MemMap[PS][SS][J+2]=ROMData[I]+((int)V<<13);
        ROMMapper[I][J]=V;
      }
      return;

    case MAP_KONAMI4: /* KONAMI4 8kB cartridges */
      /* Only interested in writes to 6000h/8000h/A000h */
      /* (page at 4000h is fixed) */
      if((A<0x6000)||(A>0xA000)||(A&0x1FFF)) break;
      J=(A-0x4000)>>13;
      /* Switch ROM pages */
      V&=ROMMask[I];
      if(V!=ROMMapper[I][J])
      {
        RAM[J+2]=MemMap[PS][SS][J+2]=ROMData[I]+((int)V<<13);
        ROMMapper[I][J]=V;
      }
      return;

    case MAP_ASCII8: /* ASCII 8kB cartridges */
      /* If switching pages... */
      if((A>=0x6000)&&(A<0x8000))
      {
        J=(A&0x1800)>>11;
        /* If selecting SRAM... */
        if(V&(ROMMask[I]+1))
        {
          /* Select SRAM page */
          V=0xFF;
          P=SRAMData[I];
        }
        else
        {
          /* Select ROM page */
          V&=ROMMask[I];
          P=ROMData[I]+((int)V<<13);
        }
        /* If page was actually changed... */
        if(V!=ROMMapper[I][J])
        {
          MemMap[PS][SS][J+2]=P;
          ROMMapper[I][J]=V;
          /* Only update memory when cartridge's slot selected */
          if((PSL[(J>>1)+1]==PS)&&(SSL[(J>>1)+1]==SS)) RAM[J+2]=P;
        }
        /* Done with page switch */
        return;
      }
      /* Write to SRAM */
      if((A>=0x8000)&&(A<0xC000)&&(ROMMapper[I][((A>>13)&1)+2]==0xFF))
      {
        RAM[A>>13][A&0x1FFF]=V;
        SaveSRAM[I]=1;
        /* Done with SRAM write */
        return;
      }
      break;

    case MAP_ASCII16: /*** ASCII 16kB cartridges ***/
      /* NOTE: Vauxall writes garbage to to 7xxxh */
      /* NOTE: Darwin writes valid data to 6x00h (ASCII8 mapper) */
      /* NOTE: Androgynus writes valid data to 77FFh */
      /* If switching pages... */
      if((A>=0x6000)&&(A<0x8000)&&((V<=ROMMask[I]+1)||!(A&0x0FFF)))
      {
        J=(A&0x1000)>>11;
        /* If selecting SRAM... */
        if(V&(ROMMask[I]+1))
        {
          /* Select SRAM page */
          V=0xFF;
          P=SRAMData[I];
        }
        else
        {
          /* Select ROM page */
          V=(V<<1)&ROMMask[I];
          P=ROMData[I]+((int)V<<13);
        }
        /* If page was actually changed... */
        if(V!=ROMMapper[I][J])
        {
          MemMap[PS][SS][J+2]=P;
          MemMap[PS][SS][J+3]=P+0x2000;
          ROMMapper[I][J]=V;
          ROMMapper[I][J+1]=V|1;
          /* Only update memory when cartridge's slot selected */
          if((PSL[(J>>1)+1]==PS)&&(SSL[(J>>1)+1]==SS))
          {
            RAM[J+2]=P;
            RAM[J+3]=P+0x2000;
          }
        }
        /* Done with page switch */
        return;
      }
      /* Write to SRAM */
      if((A>=0x8000)&&(A<0xC000)&&(ROMMapper[I][2]==0xFF))
      {
        P=RAM[A>>13];
        A&=0x07FF;
        P[A+0x0800]=P[A+0x1000]=P[A+0x1800]=
        P[A+0x2000]=P[A+0x2800]=P[A+0x3000]=
        P[A+0x3800]=P[A]=V;
        SaveSRAM[I]=1;
        /* Done with SRAM write */
        return;
      }
      break;

    case MAP_GMASTER2: /* Konami GameMaster2+SRAM cartridge */
      /* Switch ROM and SRAM pages, page at 4000h is fixed */
      if((A>=0x6000)&&(A<=0xA000)&&!(A&0x1FFF))
      {
        /* Figure out which ROM page gets switched */
        J=(A-0x4000)>>13;
        /* If changing SRAM page... */
        if(V&0x10)
        {
          /* Select SRAM page */
          RAM[J+2]=MemMap[PS][SS][J+2]=SRAMData[I]+(V&0x20? 0x2000:0);
          /* SRAM is now on */
          ROMMapper[I][J]=0xFF;
        }
        else
        {
          /* Compute new ROM page number */
          V&=ROMMask[I];
          /* If ROM page number has changed... */
          if(V!=ROMMapper[I][J])
          {
            RAM[J+2]=MemMap[PS][SS][J+2]=ROMData[I]+((int)V<<13);
            ROMMapper[I][J]=V;
          }
        }
        /* Done with page switch */
        return;
      }
      /* Write to SRAM */
      if((A>=0xB000)&&(A<0xC000)&&(ROMMapper[I][3]==0xFF))
      {
        RAM[5][(A&0x0FFF)|0x1000]=RAM[5][A&0x0FFF]=V;
        SaveSRAM[I]=1;
        /* Done with SRAM write */
        return;
      }
      break;

    case MAP_FMPAC: /* Panasonic FMPAC+SRAM cartridge */
      /* See if any switching occurs */
      switch(A)
      {
        case 0x7FF7: /* ROM page select */
          V=(V<<1)&ROMMask[I];
          ROMMapper[I][0]=V;
          ROMMapper[I][1]=V|1;
          /* 4000h-5FFFh contains SRAM when correct FMPACKey supplied */
          if(FMPACKey!=FMPAC_MAGIC)
          {
            P=ROMData[I]+((int)V<<13);
            RAM[2]=MemMap[PS][SS][2]=P;
            RAM[3]=MemMap[PS][SS][3]=P+0x2000;
          }
          return;
        case 0x7FF6: /* OPL1 enable/disable? */
          V&=0x11;
          return;
        case 0x5FFE: /* Write 4Dh, then (5FFFh)=69h to enable SRAM */
        case 0x5FFF: /* (5FFEh)=4Dh, then write 69h to enable SRAM */
          FMPACKey=A&1? ((FMPACKey&0x00FF)|((int)V<<8))
                      : ((FMPACKey&0xFF00)|V);
          P=FMPACKey==FMPAC_MAGIC?
            SRAMData[I]:(ROMData[I]+((int)ROMMapper[I][0]<<13));
          RAM[2]=MemMap[PS][SS][2]=P;
          RAM[3]=MemMap[PS][SS][3]=P+0x2000;
          return;
      }
      /* Write to SRAM */
      if((A>=0x4000)&&(A<0x5FFE)&&(FMPACKey==FMPAC_MAGIC))
      {
        RAM[A>>13][A&0x1FFF]=V;
        SaveSRAM[I]=1;
        return;
      }
      break;
  }

  /* No MegaROM mapper or there is an incorrect write */
}

/** PSlot() **************************************************/
/** Switch primary memory slots. This function is called    **/
/** when value in port A8h changes.                         **/
/*************************************************************/
void PSlot(uint8_t V)
{
  uint8_t J,I;

  if(PSLReg!=V)
    for(PSLReg=V,J=0;J<4;++J,V>>=2)
    {
      I          = J<<1;
      PSL[J]     = V&3;
      SSL[J]     = (SSLReg[PSL[J]]>>I)&3;
      RAM[I]     = MemMap[PSL[J]][SSL[J]][I];
      RAM[I+1]   = MemMap[PSL[J]][SSL[J]][I+1];
      EnWrite[J] = (PSL[J]==3)&&(SSL[J]==2)&&(MemMap[3][2][I]!=EmptyRAM);
    }
}

/** SSlot() **************************************************/
/** Switch secondary memory slots. This function is called  **/
/** when value in (FFFFh) changes.                          **/
/*************************************************************/
void SSlot(uint8_t V)
{
  uint8_t J,I;

  /* Cartridge slots do not have subslots, fix them at 0:0:0:0 */
  if((PSL[3]==1)||(PSL[3]==2)) V=0x00;
  /* In MSX1, slot 0 does not have subslots either */
  if(!PSL[3]&&((Mode&MSX_MODEL)==MSX_MSX1)) V=0x00;

  if(SSLReg[PSL[3]]!=V)
    for(SSLReg[PSL[3]]=V,J=0;J<4;++J,V>>=2)
    {
      if(PSL[J]==PSL[3])
      {
        I          = J<<1;
        SSL[J]     = V&3;
        RAM[I]     = MemMap[PSL[J]][SSL[J]][I];
        RAM[I+1]   = MemMap[PSL[J]][SSL[J]][I+1];
        EnWrite[J] = (PSL[J]==3)&&(SSL[J]==2)&&(MemMap[3][2][I]!=EmptyRAM);
      }
    }
}

/** SetIRQ() *************************************************/
/** Set or reset IRQ. Returns IRQ vector assigned to        **/
/** CPU.IRequest. When upper bit of IRQ is 1, IRQ is reset. **/
/*************************************************************/
uint16_t SetIRQ(uint8_t IRQ)
{
  if(IRQ&0x80) IRQPending&=IRQ; else IRQPending|=IRQ;
  CPU.IRequest=IRQPending? INT_IRQ:INT_NONE;
  return(CPU.IRequest);
}

/** SetScreen() **********************************************/
/** Change screen mode. Returns new screen mode.            **/
/*************************************************************/
uint8_t SetScreen(void)
{
  uint8_t I,J;

  switch(((VDP[0]&0x0E)>>1)|(VDP[1]&0x18))
  {
    case 0x10: J=0;break;
    case 0x00: J=1;break;
    case 0x01: J=2;break;
    case 0x08: J=3;break;
    case 0x02: J=4;break;
    case 0x03: J=5;break;
    case 0x04: J=6;break;
    case 0x05: J=7;break;
    case 0x07: J=8;break;
    case 0x12: J=MAXSCREEN+1;break;
    default:   J=ScrMode;break;
  }

  /* Recompute table addresses */
  I=(J>6)&&(J!=MAXSCREEN+1)? 11:10;
  ChrTab  = VRAM+((int)(VDP[2]&MSK[J].R2)<<I);
  ChrGen  = VRAM+((int)(VDP[4]&MSK[J].R4)<<11);
  ColTab  = VRAM+((int)(VDP[3]&MSK[J].R3)<<6)+((int)VDP[10]<<14);
  SprTab  = VRAM+((int)(VDP[5]&MSK[J].R5)<<7)+((int)VDP[11]<<15);
  SprGen  = VRAM+((int)VDP[6]<<11);
  ChrTabM = ((int)(VDP[2]|~MSK[J].M2)<<I)|((1<<I)-1);
  ChrGenM = ((int)(VDP[4]|~MSK[J].M4)<<11)|0x007FF;
  ColTabM = ((int)(VDP[3]|~MSK[J].M3)<<6)|0x1C03F;
  SprTabM = ((int)(VDP[5]|~MSK[J].M5)<<7)|0x1807F;

  /* Return new screen mode */
  ScrMode=J;
  return(J);
}

/** SetMegaROM() *********************************************/
/** Set MegaROM pages for a given slot. SetMegaROM() always **/
/** assumes 8kB pages.                                      **/
/*************************************************************/
void SetMegaROM(int Slot,uint8_t P0,uint8_t P1,uint8_t P2,uint8_t P3)
{
  uint8_t PS,SS;

  /* @@@ ATTENTION: MUST ADD SUPPORT FOR SRAM HERE!   */
  /* @@@ The FFh value must be treated as a SRAM page */

  /* Slot number must be valid */
  if((Slot<0)||(Slot>=MAXSLOTS)) return;
  /* Find primary/secondary slots */
  for(PS=0;PS<4;++PS)
  {
    for(SS=0;(SS<4)&&(CartMap[PS][SS]!=Slot);++SS);
    if(SS<4) break;
  }
  /* Drop out if slots not found */
  if(PS>=4) return;

  /* Apply masks to ROM pages */
  P0&=ROMMask[Slot];
  P1&=ROMMask[Slot];
  P2&=ROMMask[Slot];
  P3&=ROMMask[Slot];
  /* Set memory map */
  MemMap[PS][SS][2]=ROMData[Slot]+P0*0x2000;
  MemMap[PS][SS][3]=ROMData[Slot]+P1*0x2000;
  MemMap[PS][SS][4]=ROMData[Slot]+P2*0x2000;
  MemMap[PS][SS][5]=ROMData[Slot]+P3*0x2000;
  /* Set ROM mappers */
  ROMMapper[Slot][0]=P0;
  ROMMapper[Slot][1]=P1;
  ROMMapper[Slot][2]=P2;
  ROMMapper[Slot][3]=P3;
}

/** VDPOut() *************************************************/
/** Write value into a given VDP register.                  **/
/*************************************************************/
void VDPOut(uint8_t R,uint8_t V)
{
  uint8_t J;

  switch(R)
  {
    case  0: /* Reset HBlank interrupt if disabled */
             if((VDPStatus[1]&0x01)&&!(V&0x10))
             {
               VDPStatus[1]&=0xFE;
               SetIRQ(~INT_IE1);
             }
             /* Set screen mode */
             if(VDP[0]!=V) { VDP[0]=V;SetScreen(); }
             break;
    case  1: /* Set/Reset VBlank interrupt if enabled or disabled */
             if(VDPStatus[0]&0x80) SetIRQ(V&0x20? INT_IE0:~INT_IE0);
             /* Set screen mode */
             if(VDP[1]!=V) { VDP[1]=V;SetScreen(); }
             break;
    case  2: J=(ScrMode>6)&&(ScrMode!=MAXSCREEN+1)? 11:10;
             ChrTab  = VRAM+((int)(V&MSK[ScrMode].R2)<<J);
             ChrTabM = ((int)(V|~MSK[ScrMode].M2)<<J)|((1<<J)-1);
             break;
    case  3: ColTab  = VRAM+((int)(V&MSK[ScrMode].R3)<<6)+((int)VDP[10]<<14);
             ColTabM = ((int)(V|~MSK[ScrMode].M3)<<6)|0x1C03F;
             break;
    case  4: ChrGen  = VRAM+((int)(V&MSK[ScrMode].R4)<<11);
             ChrGenM = ((int)(V|~MSK[ScrMode].M4)<<11)|0x007FF;
             break;
    case  5: SprTab  = VRAM+((int)(V&MSK[ScrMode].R5)<<7)+((int)VDP[11]<<15);
             SprTabM = ((int)(V|~MSK[ScrMode].M5)<<7)|0x1807F;
             break;
    case  6: V&=0x3F;SprGen=VRAM+((int)V<<11);break;
    case  7: FGColor=V>>4;BGColor=V&0x0F;break;
    case 10: V&=0x07;
             ColTab=VRAM+((int)(VDP[3]&MSK[ScrMode].R3)<<6)+((int)V<<14);
             break;
    case 11: V&=0x03;
             SprTab=VRAM+((int)(VDP[5]&MSK[ScrMode].R5)<<7)+((int)V<<15);
             break;
    case 14: V&=VRAMPageMask;VPAGE=VRAM+((int)V<<14);
             break;
    case 15: V&=0x0F;break;
    case 16: V&=0x0F;PKey=1;break;
    case 17: V&=0xBF;break;
    case 25: VDP[25]=V;
             SetScreen();
             break;
    case 44: VDPWrite(V);break;
    case 46: VDPDraw(V);break;
  }

  /* Write value into a register */
  VDP[R]=V;
}

/** Printer() ************************************************/
/** Send a character to the printer.                        **/
/*************************************************************/
void Printer(uint8_t V)
{
}

/** PPIOut() *************************************************/
/** This function is called on each write to PPI to make    **/
/** key click sound, motor relay clicks, and so on.         **/
/*************************************************************/
void PPIOut(uint8_t New,uint8_t Old)
{
}

/** RTCIn() **************************************************/
/** Read value from a given RTC register.                   **/
/*************************************************************/
uint8_t RTCIn(uint8_t R)
{
  static time_t PrevTime;
  static struct tm TM;
  uint8_t J;
  time_t CurTime;

  /* Only 16 registers/mode */
  R&=0x0F;

  /* Bank mode 0..3 */
  J=RTCMode&0x03;

  if(R>12) J=R==13? RTCMode:NORAM;
  else
    if(J) J=RTC[J][R];
    else
    {
      /* Retrieve system time if any time passed */
      CurTime=time(NULL);
      if(CurTime!=PrevTime)
      {
        TM=*localtime(&CurTime);
        PrevTime=CurTime;
      }

      /* Parse contents of last retrieved TM */
      switch(R)
      {
        case 0:  J=TM.tm_sec%10;break;
        case 1:  J=TM.tm_sec/10;break;
        case 2:  J=TM.tm_min%10;break;
        case 3:  J=TM.tm_min/10;break;
        case 4:  J=TM.tm_hour%10;break;
        case 5:  J=TM.tm_hour/10;break;
        case 6:  J=TM.tm_wday;break;
        case 7:  J=TM.tm_mday%10;break;
        case 8:  J=TM.tm_mday/10;break;
        case 9:  J=(TM.tm_mon+1)%10;break;
        case 10: J=(TM.tm_mon+1)/10;break;
        case 11: J=(TM.tm_year-80)%10;break;
        case 12: J=((TM.tm_year-80)/10)%10;break;
        default: J=0x0F;break;
      }
    }

  /* Four upper bits are always high */
  return(J|0xF0);
}

/** LoopZ80() ************************************************/
/** Refresh screen, check keyboard and sprites. Call this   **/
/** function on each interrupt.                             **/
/*************************************************************/
uint16_t LoopZ80(Z80 *R)
{
  static uint8_t BFlag=0;
  static uint8_t BCount=0;
  static int  UCount=0;
  static uint8_t ACount=0;
  static uint8_t Drawing=0;
  int J;

  /* Flip HRefresh bit */
  VDPStatus[2]^=0x20;

  /* If HRefresh is now in progress... */
  if(!(VDPStatus[2]&0x20))
  {
    /* HRefresh takes most of the scanline */
    R->IPeriod=!ScrMode||(ScrMode==MAXSCREEN+1)? CPU_H240:CPU_H256;

    /* New scanline */
    ScanLine=ScanLine<(PALVideo? 312:261)? ScanLine+1:0;

    /* If first scanline of the screen... */
    if(!ScanLine)
    {
      /* Drawing now... */
      Drawing=1;

      /* Reset VRefresh bit */
      VDPStatus[2]&=0xBF;

      /* Refresh display */
      if(UCount>=100)
      {
        UCount-=100;
        PutImage();
      }
      UCount+=UPeriod;

      /* Blinking for TEXT80 */
      if(BCount) BCount--;
      else
      {
        BFlag=!BFlag;
        if(!VDP[13]) { XFGColor=FGColor;XBGColor=BGColor; }
        else
        {
          BCount=(BFlag? VDP[13]&0x0F:VDP[13]>>4)*10;
          if(BCount)
          {
            if(BFlag) { XFGColor=FGColor;XBGColor=BGColor; }
            else      { XFGColor=VDP[12]>>4;XBGColor=VDP[12]&0x0F; }
          }
        }
      }
    }

    /* Line coincidence is active at 0..255 */
    /* in PAL and 0..234/244 in NTSC        */
    J=PALVideo? 256:ScanLines212? 245:235;

    /* When reaching end of screen, reset line coincidence */
    if(ScanLine==J)
    {
      VDPStatus[1]&=0xFE;
      SetIRQ(~INT_IE1);
    }

    /* When line coincidence is active... */
    if(ScanLine<J)
    {
      /* Line coincidence processing */
      J=(((ScanLine+VScroll)&0xFF)-VDP[19])&0xFF;
      if(J==2)
      {
        /* Set HBlank flag on line coincidence */
        VDPStatus[1]|=0x01;
        /* Generate IE1 interrupt */
        if(VDP[0]&0x10) SetIRQ(INT_IE1);
      }
      else
      {
        /* Reset flag immediately if IE1 interrupt disabled */
        if(!(VDP[0]&0x10)) VDPStatus[1]&=0xFE;
      }
    }

    /* Return whatever interrupt is pending */
    R->IRequest=IRQPending? INT_IRQ:INT_NONE;
    return(R->IRequest);
  }

  /*********************************/
  /* We come here for HBlanks only */
  /*********************************/

  /* HBlank takes HPeriod-HRefresh */
  R->IPeriod=!ScrMode||(ScrMode==MAXSCREEN+1)? CPU_H240:CPU_H256;
  R->IPeriod=HPeriod-R->IPeriod;

  /* If last scanline of VBlank, see if we need to wait more */
  J=PALVideo? 313:262;
  if(ScanLine>=J-1)
  {
    J*=CPU_HPERIOD;
    if(VPeriod>J) R->IPeriod+=VPeriod-J;
  }

  /* If first scanline of the bottom border... */
  if(ScanLine==(ScanLines212? 212:192)) Drawing=0;

  /* If first scanline of VBlank... */
  J=PALVideo? (ScanLines212? 212+42:192+52):(ScanLines212? 212+18:192+28);
  if(!Drawing&&(ScanLine==J))
  {
    /* Set VBlank bit, set VRefresh bit */
    VDPStatus[0]|=0x80;
    VDPStatus[2]|=0x40;

    /* Generate VBlank interrupt */
    if(VDP[1]&0x20) SetIRQ(INT_IE0);
  }

  /* Run V9938 engine */
  LoopVDP();

  /* Refresh scanline, possibly with the overscan */
  if((UCount>=100)&&Drawing&&(ScanLine<256))
  {
    if(!ModeYJK||(ScrMode<7)||(ScrMode>8))
      (RefreshLine[ScrMode])(ScanLine);
    else
      if(ModeYAE) RefreshLine10(ScanLine);
      else RefreshLine12(ScanLine);
  }

  /* Every few scanlines, update sound */
  if(!(ScanLine&0x07))
  {
    /* Compute number of microseconds */
    J = (int)(1000000L*(CPU_HPERIOD<<3)/CPU_CLOCK);

    /* Update AY8910 state */
    Loop8910(&PSG,J);

    /* Flush changes to sound channels */
    Sync8910(&PSG,AY8910_FLUSH);

    // fmsx-libretro: do not sync SCC & FM-PAC every 8 scanlines; causes interference
  }

  if(OPTION(MSX_NUKEYKT))
    NukeYKT_Sync2413(&OPLL_NukeYKT, CPU_HPERIOD);

  /* Keyboard, sound, and other stuff always runs at line 192    */
  /* This way, it can't be shut off by overscan tricks (Maarten) */
  if(ScanLine==192)
  {
    /* Clear 5th Sprite fields (wrong place to do it?) */
    VDPStatus[0]=(VDPStatus[0]&~0x40)|0x1F;

    /* Check sprites and set Collision bit */
    if(!(VDPStatus[0]&0x20)&&CheckSprites()) VDPStatus[0]|=0x20;

    // fmsx-libretro: keep sync SCC & FM-PAC at scanline 192 (version 4.9 & earlier)
    SyncSCC(&SCChip,SCC_FLUSH);
    if(!OPTION(MSX_NUKEYKT))
      Sync2413(&OPLL,YM2413_FLUSH);

    /* Apply RAM-based cheats */
    if(CheatsON&&CheatCount) ApplyCheats();

    /* Check joystick */
    JoyState=Joystick();

    /* Check mouse in joystick port #1 */
    if(JOYTYPE(0)>=JOY_MOUSTICK)
    {
      /* Get new mouse state */
      MouState[0]=Mouse(0);
      /* Merge mouse buttons into joystick buttons */
      JoyState|=(MouState[0]>>12)&0x0030;
      /* If mouse-as-joystick... */
      if(JOYTYPE(0)==JOY_MOUSTICK)
      {
        J=MouState[0]&0xFF;
        JoyState|=J>OldMouseX[0]? 0x0008:J<OldMouseX[0]? 0x0004:0;
        OldMouseX[0]=J;
        J=(MouState[0]>>8)&0xFF;
        JoyState|=J>OldMouseY[0]? 0x0002:J<OldMouseY[0]? 0x0001:0;
        OldMouseY[0]=J;
      }
    }

    /* Check mouse in joystick port #2 */
    if(JOYTYPE(1)>=JOY_MOUSTICK)
    {
      /* Get new mouse state */
      MouState[1]=Mouse(1);
      /* Merge mouse buttons into joystick buttons */
      JoyState|=(MouState[1]>>4)&0x3000;
      /* If mouse-as-joystick... */
      if(JOYTYPE(1)==JOY_MOUSTICK)
      {
        J=MouState[1]&0xFF;
        JoyState|=J>OldMouseX[1]? 0x0800:J<OldMouseX[1]? 0x0400:0;
        OldMouseX[1]=J;
        J=(MouState[1]>>8)&0xFF;
        JoyState|=J>OldMouseY[1]? 0x0200:J<OldMouseY[1]? 0x0100:0;
        OldMouseY[1]=J;
      }
    }

    /* If any autofire options selected, run autofire counter */
    if(OPTION(MSX_AUTOSPACE|MSX_AUTOFIREA|MSX_AUTOFIREB))
      if((ACount=(ACount+1)&0x07)>3)
      {
        /* Autofire spacebar if needed */
        if(OPTION(MSX_AUTOSPACE)) KBD_RES(' ');
        /* Autofire FIRE-A if needed */
        if(OPTION(MSX_AUTOFIREA)) JoyState&=~(JST_FIREA|(JST_FIREA<<8));
        /* Autofire FIRE-B if needed */
        if(OPTION(MSX_AUTOFIREB)) JoyState&=~(JST_FIREB|(JST_FIREB<<8));
      }

    /* Exit emulation if requested */
    if(ExitNow) return(INT_QUIT);
  }

  /* Return whatever interrupt is pending */
  R->IRequest=IRQPending? INT_IRQ:INT_NONE;
  return(R->IRequest);
}

/** CheckSprites() *******************************************/
/** Check for sprite collisions.                            **/
/*************************************************************/
int CheckSprites(void)
{
  unsigned int I,J,LS,LD;
  uint8_t DH,DV,*S,*D,*PS,*PD,*T;

  /* Must be showing sprites */
  if(SpritesOFF||!ScrMode||(ScrMode>=MAXSCREEN+1)) return(0);

  /* Find bottom/top scanlines */
  DH = ScrMode>3? 216:208;
  LD = 255-(Sprites16x16? 16:8);
  LS = ScanLines212? 211:191;

  /* Find valid, displayed sprites */
  for(I=J=0,S=SprTab;(I<32)&&(S[0]!=DH);++I,S+=4)
    if((S[0]<LS)||(S[0]>LD)) J|=1<<I;

  if(Sprites16x16)
  {
    for(S=SprTab;J;J>>=1,S+=4)
      if(J&1)
        for(I=J>>1,D=S+4;I;I>>=1,D+=4)
          if(I&1)
          {
            DV=S[0]-D[0];
            if((DV<16)||(DV>240))
	    {
              DH=S[1]-D[1];
              if((DH<16)||(DH>240))
	      {
                PS=SprGen+((int)(S[2]&0xFC)<<3);
                PD=SprGen+((int)(D[2]&0xFC)<<3);
                if(DV<16) PD+=DV; else { DV=256-DV;PS+=DV; }
                if(DH>240) { DH=256-DH;T=PS;PS=PD;PD=T; }
                while(DV<16)
                {
                  LS=((unsigned int)*PS<<8)+*(PS+16);
                  LD=((unsigned int)*PD<<8)+*(PD+16);
                  if(LD&(LS>>DH)) break;
                  else { ++DV;++PS;++PD; }
                }
                if(DV<16) return(1);
              }
            }
          }
  }
  else
  {
    for(S=SprTab;J;J>>=1,S+=4)
      if(J&1)
        for(I=J>>1,D=S+4;I;I>>=1,D+=4)
          if(I&1) 
          {
            DV=S[0]-D[0];
            if((DV<8)||(DV>248))
            {
              DH=S[1]-D[1];
              if((DH<8)||(DH>248))
              {
                PS=SprGen+((int)S[2]<<3);
                PD=SprGen+((int)D[2]<<3);
                if(DV<8) PD+=DV; else { DV=256-DV;PS+=DV; }
                if(DH>248) { DH=256-DH;T=PS;PS=PD;PD=T; }
                while((DV<8)&&!(*PD&(*PS>>DH))) { ++DV;++PS;++PD; }
                if(DV<8) return(1);
              }
            }
          }
  }

  /* No collisions */
  return(0);
}

/** StateID() ************************************************/
/** Compute 16bit emulation state ID used to identify .STA  **/
/** files.                                                  **/
/*************************************************************/
uint16_t StateID(void)
{
  uint16_t ID;
  int J,I;

  ID=0x0000;

  /* Add up cartridge ROMs, BIOS, BASIC, ExtBIOS, and DiskBIOS uint8_ts */
  for(I=0;I<MAXSLOTS;++I)
    if(ROMData[I]) for(J=0;J<(ROMMask[I]+1)*0x2000;++J) ID+=I^ROMData[I][J];
  if(MemMap[0][0][0]&&(MemMap[0][0][0]!=EmptyRAM))
    for(J=0;J<0x8000;++J) ID+=MemMap[0][0][0][J];
  if(MemMap[3][1][0]&&(MemMap[3][1][0]!=EmptyRAM))
    for(J=0;J<0x4000;++J) ID+=MemMap[3][1][0][J];
  if(MemMap[3][1][2]&&(MemMap[3][1][2]!=EmptyRAM))
    for(J=0;J<0x4000;++J) ID+=MemMap[3][1][2][J];

  return(ID);
}

/** MakeFileName() *******************************************/
/** Make a copy of the file name, replacing the extension.  **/
/** Returns allocated new name or 0 on failure.             **/
/*************************************************************/
char *MakeFileName(const char *Name,const char *Ext)
{
  char *Result,*P1,*P2,*P3;

  Result = malloc(strlen(Name)+strlen(Ext)+1);
  if(!Result) return(0);
  strcpy(Result,Name);

  /* Locate where extension and filename actually start */
  P1 = strrchr(Result,'.');
  P2 = strrchr(Result,'/');
  P3 = strrchr(Result,'\\');
  P2 = P3 && (P3>P2)? P3:P2;
  P3 = strrchr(Result,':');
  P2 = P3 && (P3>P2)? P3:P2;

  if(P1 && (!P2 || (P1>P2))) strcpy(P1,Ext);
  else strcat(Result,Ext);

  return(Result);
}

/** ChangeTape() *********************************************/
/** Change tape image. ChangeTape(0) closes current image.  **/
/** Returns 1 on success, 0 on failure.                     **/
/*************************************************************/
uint8_t ChangeTape(const char *FileName)
{
  tape_type = NO_TAPE;

  /* Close previous tape image, if open */
  if(CasStream) { rfclose(CasStream);CasStream=0; }

  /* If opening a new tape image... */
  if(FileName)
  {
    /* Try read+append first, then read-only */
    CasStream = rfopen(FileName,"r+b");
    CasStream = CasStream? CasStream:rfopen(FileName,"rb");

    if (CasStream)
    {
        int tape_len, pos = 0;
        char *tape_contents;

        rfseek(CasStream,0,SEEK_END);
        tape_len = rftell(CasStream);
        filestream_rewind(CasStream);

        tape_contents = (char*)malloc(tape_len);
        if (rfread(tape_contents, 1, tape_len, CasStream) != tape_len)
        {
           free(tape_contents);
           return 0;
        }
        while (pos + TAPE_HEADER_LEN <= tape_len)
        {
           if (!memcmp(&tape_contents[pos], ASCII_HEADER, TAPE_HEADER_LEN))
           {
              tape_type = ASCII_TAPE;
              break;
           }
           else if (!memcmp(&tape_contents[pos], BINARY_HEADER, TAPE_HEADER_LEN))
           {
              tape_type = BINARY_TAPE;
              break;
           }
           else if (!memcmp(&tape_contents[pos], BASIC_HEADER, TAPE_HEADER_LEN))
           {
              tape_type = BASIC_TAPE;
              break;
           }
           pos++;
        }
        free(tape_contents);
    }
    RewindTape();
  }

  /* Done */
  return(!FileName||CasStream);
}

/** RewindTape() *********************************************/
/** Rewind currently open tape.                              **/
/*************************************************************/
void RewindTape(void) { if(CasStream) filestream_rewind(CasStream); }

/** ChangePrinter() ******************************************/
/** Change printer output to a given file. The previous     **/
/** file is closed. ChangePrinter(0) redirects output to    **/
/** stdout.                                                 **/
/*************************************************************/
void ChangePrinter(const char *FileName)
{
}

/** ChangeDisk() *********************************************/
/** Change disk image in a given drive. Closes current disk **/
/** image if Name=0 was given. Creates a new disk image if  **/
/** Name="" was given. Returns 1 on success or 0 on failure.**/
/*************************************************************/
uint8_t ChangeDisk(uint8_t N,const char *FileName)
{
  uint8_t *P;

  /* We only have MAXDRIVES drives */
  if(N>=MAXDRIVES) return(0);

  /* Reset FDC, in case it was running a command */
  Reset1793(&FDC,FDD,WD1793_KEEP);

  /* Eject disk if requested */
  if(!FileName) { EjectFDI(&FDD[N]);return(1); }

  /* If FileName not empty, try loading disk image */
  if(*FileName&&LoadFDI(&FDD[N],FileName,FMT_AUTO))
  {
    /* Done */
    return(1);
  }

  /* If failed opening existing image, create a new 720kB disk image */
  P = FormatFDI(&FDD[N],FMT_MSXDSK);

  /* If FileName not empty, treat it as directory, otherwise new disk */
  if(P&&!(*FileName? DSKLoad(FileName,P,"MSX-DISK"):DSKCreate(P,"MSX-DISK")))
  { EjectFDI(&FDD[N]);return(0); }

  FDD[N].Dirty = 1;

  /* Done */
  return(!!P);
}

/** ApplyMCFCheat() ******************************************/
/** Apply given MCF cheat entry. Returns 0 on failure or 1  **/
/** on success.                                             **/
/*************************************************************/
int ApplyMCFCheat(int N)
{
  int Status;

  /* Must be a valid MSX-specific entry */
  if((N<0)||(N>=MCFCount)||(MCFEntries[N].Addr>0xFFFF)||(MCFEntries[N].Size>2))
    return(0);

  /* Switch cheats off for now and remove all present cheats */
  Status = Cheats(CHTS_QUERY);
  Cheats(CHTS_OFF);
  ResetCheats();

  /* Insert cheat codes from the MCF entry as RAM-based cheat */
  CheatCodes[0].Addr = MCFEntries[N].Addr | 0x01000000;
  CheatCodes[0].Data = MCFEntries[N].Data;
  CheatCodes[0].Size = MCFEntries[N].Size;
  CheatCodes[0].Orig = RdZ80(CheatCodes[0].Addr&0xFFFF);
  if(CheatCodes[0].Size>1)
    CheatCodes[0].Orig |= (int)(RdZ80((CheatCodes[0].Addr+1)&0xFFFF)<<8);

  sprintf(
    (char *)CheatCodes[0].Text,
    CheatCodes[0].Size>1? "%04X-%04X":"%04X-%02X",
    CheatCodes[0].Addr,
    CheatCodes[0].Data
  );

  /* Have one cheat code now */
  CheatCount = 1;

  /* Turn cheats back on, if they were on */
  Cheats(Status);

  /* Done */
  return(CheatCount);
}

char* GetMCFNoteAndValue(int N, int *Value)
{
  if (Value) *Value = MCFEntries[N].Data;
  return MCFEntries[N].Note;
}

/** AddCheat() ***********************************************/
/** Add a new cheat. Returns 0 on failure or the number of  **/
/** cheats on success.                                      **/
/*************************************************************/
int AddCheat(const char *Cheat)
{
  static const char *Hex = "0123456789ABCDEF";
  unsigned int A,D;
  char *P;
  int J,N;

  /* Table full: no more cheats */
  if(CheatCount>=MAXCHEATS) return(0);

  /* Check cheat length and decode */
  N=strlen(Cheat);

  if(((N==13)||(N==11))&&(Cheat[8]=='-'))
  {
    for(J=0,A=0;J<8;J++)
    {
      P=strchr(Hex,toupper(Cheat[J]));
      if(!P) return(0); else A=(A<<4)|(P-Hex);
    }
    for(J=9,D=0;J<N;J++)
    {
      P=strchr(Hex,toupper(Cheat[J]));
      if(!P) return(0); else D=(D<<4)|(P-Hex);
    }
  }
  else if(((N==9)||(N==7))&&(Cheat[4]=='-'))
  {
    for(J=0,A=0x0100;J<4;J++)
    {
      P=strchr(Hex,toupper(Cheat[J]));
      if(!P) return(0); else A=(A<<4)|(P-Hex);
    }
    for(J=5,D=0;J<N;J++)
    {
      P=strchr(Hex,toupper(Cheat[J]));
      if(!P) return(0); else D=(D<<4)|(P-Hex);
    }
  }
  else
  {
    /* Cannot parse this cheat */
    return(0);
  }

  /* Add cheat */
  strlcpy((char *)CheatCodes[CheatCount].Text, Cheat, sizeof(CheatCodes[CheatCount].Text));
  if(N==13)
  {
    CheatCodes[CheatCount].Addr = A;
    CheatCodes[CheatCount].Data = D&0xFFFF;
    CheatCodes[CheatCount].Size = 2;
  }
  else
  {
    CheatCodes[CheatCount].Addr = A;
    CheatCodes[CheatCount].Data = D&0xFF;
    CheatCodes[CheatCount].Size = 1;
  }

  /* Successfully added a cheat! */
  return(++CheatCount);
}

/** ResetCheats() ********************************************/
/** Remove all cheats.                                      **/
/*************************************************************/
void ResetCheats(void) { Cheats(CHTS_OFF);CheatCount=0; }

/** ApplyCheats() ********************************************/
/** Apply RAM-based cheats. Returns the number of applied   **/
/** cheats.                                                 **/
/*************************************************************/
int ApplyCheats(void)
{
  int J,I;

  /* For all current cheats that look like 01AAAAAA-DD/DDDD... */
  for(J=I=0;J<CheatCount;++J)
    if((CheatCodes[J].Addr>>24)==0x01)
    {
      WrZ80(CheatCodes[J].Addr&0xFFFF,CheatCodes[J].Data&0xFF);
      if(CheatCodes[J].Size>1)
        WrZ80((CheatCodes[J].Addr+1)&0xFFFF,CheatCodes[J].Data>>8);
      ++I;
    }

  /* Return number of applied cheats */
  return(I);
}

/** Cheats() *************************************************/
/** Toggle cheats on (1), off (0), inverse state (2) or     **/
/** query (3).                                              **/
/*************************************************************/
int Cheats(int Switch)
{
  uint8_t *P,*Base;
  int J,Size;

  switch(Switch)
  {
    case CHTS_ON:
    case CHTS_OFF:    if(Switch==CheatsON) return(CheatsON);
    case CHTS_TOGGLE: Switch=!CheatsON;break;
    default:          return(CheatsON);
  }

  /* Find valid cartridge */
  for(J=1;(J<=2)&&!ROMData[J];++J);

  /* Must have ROM */
  if(J>2) return(Switch=CHTS_OFF);

  /* Compute ROM address and size */
  Base = ROMData[J];
  Size = ((int)ROMMask[J]+1)<<14;

  /* If toggling cheats... */
  if(Switch!=CheatsON)
  {
    /* If enabling cheats... */
    if(Switch)
    {
      /* Patch ROM with the cheat values */
      for(J=0;J<CheatCount;++J)
        if(!(CheatCodes[J].Addr>>24)&&(CheatCodes[J].Addr+CheatCodes[J].Size<=Size))
        {
          P = Base + CheatCodes[J].Addr;
          CheatCodes[J].Orig = P[0];
          P[0] = CheatCodes[J].Data;
          if(CheatCodes[J].Size>1)
          {
            CheatCodes[J].Orig |= (int)P[1]<<8;
            P[1] = CheatCodes[J].Data>>8;
          }
        }
    }
    else
    {
      /* Restore original ROM values */
      for(J=0;J<CheatCount;++J)
        if(!(CheatCodes[J].Addr>>24)&&(CheatCodes[J].Addr+CheatCodes[J].Size<=Size))
        {
          P = Base + CheatCodes[J].Addr;
          P[0] = CheatCodes[J].Orig;
          if(CheatCodes[J].Size>1)
            P[1] = CheatCodes[J].Orig>>8;
        }
        else if((CheatCodes[J].Addr>>24)==0x01) // restore RAM-based values
        {
          WrZ80(CheatCodes[J].Addr&0xFFFF,CheatCodes[J].Orig&0xFF);
          if(CheatCodes[J].Size>1)
            WrZ80((CheatCodes[J].Addr+1)&0xFFFF,CheatCodes[J].Orig>>8);
        }
    }

    /* Done toggling cheats */
    CheatsON = Switch;
  }

  /* Done */
  return(CheatsON);
}

/** GuessROM() ***********************************************/
/** Guess MegaROM mapper of a ROM.                          **/
/*************************************************************/
int GuessROM(const uint8_t *Buf,int Size)
{
  int J,I,K,Result,ROMCount[MAXMAPPERS];
  char S[256];
  RFILE *F;

  /* No result yet */
  Result = -1;

  /* Change to the program directory */
  if(ProgDir && chdir(ProgDir)) { }

  /* Try opening file with CRCs */
  if((F = rfopen("CARTS.CRC","rb")))
  {
    /* Compute ROM's CRC */
    for(J=K=0;J<Size;++J) K+=Buf[J];

    /* Scan file comparing CRCs */
    while(rfgets(S,sizeof(S)-4,F))
      if(sscanf(S,"%08X %d",&J,&I)==2)
        if(K==J) { Result=I;break; }

    /* Done with the file */
    rfclose(F);
  }

  /* Try opening file with SHA1 sums */
  if((Result<0) && (F=rfopen("CARTS.SHA","rb")))
  {
    char S1[41],S2[41];
    SHA1 C;

    /* Compute ROM's SHA1 */
    ResetSHA1(&C);
    InputSHA1(&C,Buf,Size);
    if(ComputeSHA1(&C) && OutputSHA1(&C,S1,sizeof(S1)))
    {
      while(rfgets(S,sizeof(S)-4,F))
        if((sscanf(S,"%40s %d",S2,&J)==2) && !strcmp(S1,S2))
        { Result=J;break; }
    }

    /* Done with the file */
    rfclose(F);
  }

  /* We are now back to working directory */
  if(WorkDir && chdir(WorkDir)) { }

  /* If found ROM by CRC or SHA1, we are done */
  if(Result>=0) return(Result);

  /* Clear all counters */
  for(J=0;J<MAXMAPPERS;++J) ROMCount[J]=1;
  /* Generic 8kB mapper is default */
  ROMCount[MAP_GEN8]+=1;
  /* ASCII 16kB preferred over ASCII 8kB */
  ROMCount[MAP_ASCII8]-=1;

  /* Count occurences of characteristic addresses */
  for(J=0;J<Size-2;++J)
  {
    I=Buf[J]+((int)Buf[J+1]<<8)+((int)Buf[J+2]<<16);
    switch(I)
    {
      case 0x500032: ROMCount[MAP_KONAMI5]++;break;
      case 0x900032: ROMCount[MAP_KONAMI5]++;break;
      case 0xB00032: ROMCount[MAP_KONAMI5]++;break;
      case 0x400032: ROMCount[MAP_KONAMI4]++;break;
      case 0x800032: ROMCount[MAP_KONAMI4]++;break;
      case 0xA00032: ROMCount[MAP_KONAMI4]++;break;
      case 0x680032: ROMCount[MAP_ASCII8]++;break;
      case 0x780032: ROMCount[MAP_ASCII8]++;break;
      case 0x600032: ROMCount[MAP_KONAMI4]++;
                     ROMCount[MAP_ASCII8]++;
                     ROMCount[MAP_ASCII16]++;
                     break;
      case 0x700032: ROMCount[MAP_KONAMI5]++;
                     ROMCount[MAP_ASCII8]++;
                     ROMCount[MAP_ASCII16]++;
                     break;
      case 0x77FF32: ROMCount[MAP_ASCII16]++;break;
    }
  }

  /* Find which mapper type got more hits */
  for(I=0,J=0;J<MAXMAPPERS;++J)
    if(ROMCount[J]>ROMCount[I]) I=J;

  /* Return the most likely mapper type */
  return(I);
}

/** LoadFNT() ************************************************/
/** Load fixed 8x8 font used in text screen modes when      **/
/** MSX_FIXEDFONT option is enabled. LoadFNT(0) frees the   **/
/** font buffer. Returns 1 on success, 0 on failure.        **/
/*************************************************************/
uint8_t LoadFNT(const char *FileName)
{
  RFILE *F;

  /* Drop out if no new font requested */
  if(!FileName) { FreeMemory(FontBuf);FontBuf=0;return(1); }
  /* Try opening font file */
  if(!(F=rfopen(FileName,"rb"))) return(0);
  /* Allocate memory for 256 8x8 characters, if needed */
  if(!FontBuf) FontBuf=GetMemory(256*8);
  /* Drop out if failed memory allocation */
  if(!FontBuf) { rfclose(F);return(0); }
  /* Read font, ignore short reads */
  rfread(FontBuf,1,256*8,F);
  /* Done */
  rfclose(F);
  return(1);
}

/** LoadROM() ************************************************/
/** Load a file, allocating memory as needed. Returns addr. **/
/** of the allocated space or 0 if failed.                  **/
/*************************************************************/
uint8_t *LoadROM(const char *Name,int Size,uint8_t *Buf)
{
  char path[512];
  RFILE *F;
  uint8_t *P;
  int J;

  /* Can't give address without size! */
  if(Buf&&!Size) return(0);

#if defined( VITA ) || defined(__PS3__)
  if(!(F=rfopen(Name,"rb")))
  {
     fill_pathname_join(path, ProgDir, Name, sizeof(path));
     /* Open file */
     if(!(F=rfopen(path,"rb")))
        return(0);
  }
#else
  /* Open file */
  if(!(F=rfopen(Name,"rb")))
     return(0);
#endif

  /* Determine data size, if wasn't given */
  if(!Size)
  {
    /* Determine size via ftell() or by reading entire [GZIPped] stream */
    if(!rfseek(F,0,SEEK_END))
       Size = rftell(F);
    else
    {
      /* Read file in 16kB increments */
      while((J = rfread(EmptyRAM,1,0x4000,F))==0x4000)
         Size+=J;
      if(J>0)
         Size+=J;
      /* Clean up the EmptyRAM! */
      memset(EmptyRAM,NORAM,0x4000);
    }
    /* Rewind file to the beginning */
    filestream_rewind(F);
  }

  /* Allocate memory */
  P=Buf? Buf:GetMemory(Size);
  if(P)
  {
     /* Read data */
     if((J = rfread(P,1,Size,F))!=Size)
     {
        if(!Buf)
           FreeMemory(P);
     }
  }

  /* Done */
  rfclose(F);
  return(P);
}

/** LoadCart() ***********************************************/
/** Load cartridge into given slot. Returns cartridge size  **/
/** in 16kB pages on success, 0 on failure.                 **/
/*************************************************************/
int LoadCart(const char *FileName,int Slot,int Type)
{
  int64_t Len;
  int C1, C2, Pages, ROM64, BASIC;
  uint8_t *P,PS,SS;
  char *T;
  RFILE *F;

  /* Slot number must be valid */
  if((Slot<0)||(Slot>=MAXSLOTS))
     return 0;

  /* Find primary/secondary slots */
  for(PS=0;PS<4;++PS)
  {
    for(SS=0;(SS<4)&&(CartMap[PS][SS]!=Slot);++SS);
    if(SS<4) break;
  }
  /* Drop out if slots not found */
  if(PS>=4)
     return 0;

  /* If there is a SRAM in this cartridge slot... */
  if(SRAMData[Slot]&&SaveSRAM[Slot]&&SRAMName[Slot])
  {
    /* Open .SAV file */
    if(!(F = rfopen(SRAMName[Slot],"wb")))
       SaveSRAM[Slot]=0;
    else
    {
      /* Write .SAV file */
      switch(ROMType[Slot])
      {
        case MAP_ASCII8:
        case MAP_FMPAC:
          if(rfwrite(SRAMData[Slot],1,0x2000,F)!=0x2000)
             SaveSRAM[Slot]=0;
          break;
        case MAP_ASCII16:
          if(rfwrite(SRAMData[Slot],1,0x0800,F)!=0x0800)
             SaveSRAM[Slot]=0;
          break;
        case MAP_GMASTER2:
          if(rfwrite(SRAMData[Slot],1,0x1000,F)!=0x1000)
             SaveSRAM[Slot]=0;
          if(rfwrite(SRAMData[Slot]+0x2000,1,0x1000,F)!=0x1000)
             SaveSRAM[Slot]=0;
          break;
      }

      /* Done with .SAV file */
      rfclose(F);
    }
    /* Done saving SRAM */
  }

  /* If ejecting cartridge... */
  if(!FileName)
  {
    if(ROMData[Slot])
    {
      /* Free memory if present */
      FreeMemory(ROMData[Slot]);
      ROMData[Slot] = 0;
      ROMMask[Slot] = 0;
      /* Set memory map to dummy RAM */
      for(C1=0;C1<8;++C1) MemMap[PS][SS][C1]=EmptyRAM;
      /* Restart MSX */
      ResetMSX(Mode,RAMPages,VRAMPages);
      /* Cartridge ejected */
    }

    /* Nothing else to do */
    return 0;
  }

  /* Try opening file */
  if(!(F = rfopen(FileName,"rb")))
     return 0;

  /* Determine size via ftell() or by reading entire [GZIPped] stream */
  if(!rfseek(F,0,SEEK_END))
     Len = rftell(F);
  else
  {
    /* Read file in 16kB increments */
    for(Len=0;(C2 = rfread(EmptyRAM,1,0x4000,F))==0x4000;Len+=C2);
    if(C2>0) Len+=C2;
    /* Clean up the EmptyRAM! */
    memset(EmptyRAM,NORAM,0x4000);
  }

  /* Rewind file */
  filestream_rewind(F);

  /* Length in 8kB pages */
  Len = Len>>13;

  /* Calculate 2^n closest to number of 8kB pages */
  for(Pages=1;Pages<Len;Pages<<=1);

  /* Check "AB" signature in a file */
  ROM64=0;
  C1    = rfgetc(F);
  C2    = rfgetc(F);

  /* Maybe this is a flat 64kB ROM? */
  if((C1!='A')||(C2!='B'))
    if(rfseek(F,0x4000,SEEK_SET)>=0)
    {
      C1    = rfgetc(F);
      C2    = rfgetc(F);
      ROM64=(C1=='A')&&(C2=='B');
    }

  /* Maybe it is the last 16kB page that contains "AB" signature? */
  if((Len>=2)&&((C1!='A')||(C2!='B')))
    if(rfseek(F,0x2000*(Len-2),SEEK_SET)>=0)
    {
      C1=rfgetc(F);
      C2=rfgetc(F);
    }

  /* If we can't find "AB" signature, drop out */
  if((C1!='A')||(C2!='B'))
  {
    rfclose(F);
    return(0);
  }

  /* Done with the file */
  rfclose(F);

  /* Assign ROMMask for MegaROMs */
  ROMMask[Slot]=!ROM64&&(Len>4)? (Pages-1):0x00;
  /* Allocate space for the ROM */
  ROMData[Slot]=P=GetMemory(Pages<<13);
  if(!P)
    return(0);

  /* Try loading ROM */
  if(!LoadROM(FileName,Len<<13,P))
    return(0);

  /* Mirror ROM if it is smaller than 2^n pages */
  if(Len<Pages)
    memcpy(P+Len*0x2000,P+(Len-Pages/2)*0x2000,(Pages-Len)*0x2000); 

  /* Detect ROMs containing BASIC code */
  BASIC=(P[0]=='A')&&(P[1]=='B')&&!(P[2]||P[3])&&(P[8]||P[9]);

  /* Set memory map depending on the ROM size */
  switch(Len)
  {
    case 1:
      /* 8kB ROMs are mirrored 8 times: 0:0:0:0:0:0:0:0 */
      if(!BASIC)
      {
        MemMap[PS][SS][0]=P;
        MemMap[PS][SS][1]=P;
        MemMap[PS][SS][2]=P;
        MemMap[PS][SS][3]=P;
      }
      MemMap[PS][SS][4]=P;
      MemMap[PS][SS][5]=P;
      if(!BASIC)
      {
        MemMap[PS][SS][6]=P;
        MemMap[PS][SS][7]=P;
      }
      break;

    case 2:
      /* 16kB ROMs are mirrored 4 times: 0:1:0:1:0:1:0:1 */
      if(!BASIC)
      {
        MemMap[PS][SS][0]=P;
        MemMap[PS][SS][1]=P+0x2000;
        MemMap[PS][SS][2]=P;
        MemMap[PS][SS][3]=P+0x2000;
      }
      MemMap[PS][SS][4]=P;
      MemMap[PS][SS][5]=P+0x2000;
      if(!BASIC)
      {
        MemMap[PS][SS][6]=P;
        MemMap[PS][SS][7]=P+0x2000;
      }
      break;

    case 3:
    case 4:
      /* 24kB and 32kB ROMs are mirrored twice: 0:1:0:1:2:3:2:3 */
      MemMap[PS][SS][0]=P;
      MemMap[PS][SS][1]=P+0x2000;
      MemMap[PS][SS][2]=P;
      MemMap[PS][SS][3]=P+0x2000;
      MemMap[PS][SS][4]=P+0x4000;
      MemMap[PS][SS][5]=P+0x6000;
      MemMap[PS][SS][6]=P+0x4000;
      MemMap[PS][SS][7]=P+0x6000;
      break;

    default:
      if(ROM64)
      {
        /* 64kB ROMs are loaded to fill slot: 0:1:2:3:4:5:6:7 */
        MemMap[PS][SS][0]=P;
        MemMap[PS][SS][1]=P+0x2000;
        MemMap[PS][SS][2]=P+0x4000;
        MemMap[PS][SS][3]=P+0x6000;
        MemMap[PS][SS][4]=P+0x8000;
        MemMap[PS][SS][5]=P+0xA000;
        MemMap[PS][SS][6]=P+0xC000;
        MemMap[PS][SS][7]=P+0xE000;
      }
      break;
  }

  /* Guess MegaROM mapper type if not given */
  if((Type>=MAP_GUESS)&&(ROMMask[Slot]+1>4))
  {
    Type=GuessROM(P,Len<<13);
    if(Slot<MAXCARTS) SETROMTYPE(Slot,Type);
  }
  else if (Type==MAP_GMASTER2 && Slot<MAXCARTS) SETROMTYPE(Slot,Type); // required to enable GM2 in slot A/B

  /* Save MegaROM type */
  ROMType[Slot]=Type;

  /* For Generic/16kB carts, set ROM pages as 0:1:N-2:N-1 */
  if((Type==MAP_GEN16)&&(ROMMask[Slot]+1>4))
    SetMegaROM(Slot,0,1,ROMMask[Slot]-1,ROMMask[Slot]);

  /* If cartridge may need a SRAM... */
  if(MAP_SRAM(Type))
  {
    /* Free previous SRAM resources */
    FreeMemory(SRAMData[Slot]);
    FreeMemory((uint8_t*)SRAMName[Slot]);

    /* Get SRAM memory */
    SRAMData[Slot]=GetMemory(0x4000);
    if(!SRAMData[Slot])
      SRAMData[Slot]=EmptyRAM;
    else
      memset(SRAMData[Slot],NORAM,0x4000);

    /* Generate SRAM file name and load SRAM contents */
    if((SRAMName[Slot]=(char *)GetMemory(strlen(FileName)+5)))
    {
      /* Compose SRAM file name */
      strcpy(SRAMName[Slot],FileName);
      T = (uint8_t*)(const char*)strrchr(SRAMName[Slot],'.');
      if(T)
         strcpy((char*)T,".sav");
      else
         strcat(SRAMName[Slot],".sav");

      /* Try opening file... */
      if((F=rfopen(SRAMName[Slot],"rb")))
      {
        /* Read SRAM file */
        Len=rfread(SRAMData[Slot],1,0x4000,F);
        rfclose(F);
        /* Print information if needed */
        /* Mirror data according to the mapper type */
        P=SRAMData[Slot];
        switch(Type)
        {
          case MAP_FMPAC:
            memset(P+0x2000,NORAM,0x2000);
            P[0x1FFE]=FMPAC_MAGIC&0xFF;
            P[0x1FFF]=FMPAC_MAGIC>>8;
            break;
          case MAP_GMASTER2:
            memcpy(P+0x2000,P+0x1000,0x1000);
            memcpy(P+0x3000,P+0x1000,0x1000);
            memcpy(P+0x1000,P,0x1000);
            break;
          case MAP_ASCII16:
            memcpy(P+0x0800,P,0x0800);
            memcpy(P+0x1000,P,0x0800);
            memcpy(P+0x1800,P,0x0800);
            memcpy(P+0x2000,P,0x0800);
            memcpy(P+0x2800,P,0x0800);
            memcpy(P+0x3000,P,0x0800);
            memcpy(P+0x3800,P,0x0800);
            break;
        }
      }
    }
  }

  /* Done setting up cartridge */
  ResetMSX(Mode,RAMPages,VRAMPages);

  /* Done loading cartridge */
  return(Pages);
}

/** LoadCHT() ************************************************/
/** Load cheats from .CHT file. Cheat format is either      **/
/** 00XXXXXX-XX (one uint8_t) or 00XXXXXX-XXXX (two uint8_ts) for **/
/** ROM-based cheats and XXXX-XX or XXXX-XXXX for RAM-based **/
/** cheats. Returns the number of cheats on success, 0 on   **/
/** failure.                                                **/
/*************************************************************/
int LoadCHT(const char *Name)
{
  char Buf[256],S[16];
  int Status;
  RFILE *F;

  /* Open .CHT text file with cheats */
  F = rfopen(Name,"rb");
  if(!F) return(0);

  /* Switch cheats off for now and remove all present cheats */
  Status = Cheats(CHTS_QUERY);
  Cheats(CHTS_OFF);
  ResetCheats();

  /* Try adding cheats loaded from file */
  while(!rfeof(F))
    if(rfgets(Buf,sizeof(Buf),F) && (sscanf(Buf,"%13s",S)==1))
      AddCheat(S);

  /* Done with the file */
  rfclose(F);

  /* Turn cheats back on, if they were on */
  Cheats(Status);

  /* Done */
  return(CheatCount);
}

/** LoadPAL() ************************************************/
/** Load new palette from .PAL file. Returns number of      **/
/** loaded colors on success, 0 on failure.                 **/
/*************************************************************/
int LoadPAL(const char *Name)
{
  static const char *Hex = "0123456789ABCDEF";
  char S[256],*P,*T,*H;
  RFILE *F;
  int J,I;

  if(!(F=rfopen(Name,"rb"))) return(0);

  for(J=0;(J<16)&&rfgets(S,sizeof(S),F);++J)
  {
    /* Skip white space and optional '#' character */
    for(P=S;*P&&(*P<=' ');++P);
    if(*P=='#') ++P;
    /* Parse six hexadecimal digits */
    for(T=P,I=0;*T&&(H=strchr(Hex,toupper(*T)));++T) I=(I<<4)+(H-Hex);
    /* If we have got six digits, parse and set color */
    if(T-P==6) SetColor(J,I>>16,(I>>8)&0xFF,I&0xFF);
  }

  rfclose(F);
  return(J);
}

/** LoadMCF() ************************************************/
/** Load cheats from .MCF file. Returns number of loaded    **/
/** cheat entries or 0 on failure.                          **/
/*************************************************************/
int LoadMCF(const char *Name)
{
  MCFCount = LoadFileMCF(Name,MCFEntries,sizeof(MCFEntries)/sizeof(MCFEntries[0]));
  return(MCFCount);
}

#define SaveSTRUCT(Name) \
  if(Size+sizeof(Name)>MaxSize) return(0); \
  else { memcpy(Buf+Size,&(Name),sizeof(Name));Size+=sizeof(Name); }

#define SaveARRAY(Name) \
  if(Size+sizeof(Name)>MaxSize) return(0); \
  else { memcpy(Buf+Size,(Name),sizeof(Name));Size+=sizeof(Name); }

#define SaveDATA(Name,DataSize) \
  if(Size+(DataSize)>MaxSize) return(0); \
  else { memcpy(Buf+Size,(Name),(DataSize));Size+=(DataSize); }

#define LoadSTRUCT(Name) \
  if(Size+sizeof(Name)>MaxSize) return(0); \
  else { memcpy(&(Name),Buf+Size,sizeof(Name));Size+=sizeof(Name); }

#define SkipSTRUCT(Name) \
  if(Size+sizeof(Name)>MaxSize) return(0); \
  else Size+=sizeof(Name)

#define LoadARRAY(Name) \
  if(Size+sizeof(Name)>MaxSize) return(0); \
  else { memcpy((Name),Buf+Size,sizeof(Name));Size+=sizeof(Name); }

#define LoadDATA(Name,DataSize) \
  if(Size+(DataSize)>MaxSize) return(0); \
  else { memcpy((Name),Buf+Size,(DataSize));Size+=(DataSize); }

#define SkipDATA(DataSize) \
  if(Size+(DataSize)>MaxSize) return(0); \
  else Size+=(DataSize)

/** SaveState() **********************************************/
/** Save emulation state to a memory buffer. Returns size   **/
/** on success, 0 on failure.                               **/
/*************************************************************/
unsigned int SaveState(unsigned char *Buf,unsigned int MaxSize)
{
  unsigned int State[256],Size;
  int J,I,K;

  /* No data written yet */
  Size = 0;

  /* Fill out hardware state */
  J=0;
  memset(State,0,sizeof(State));
  State[J++] = VDPData;
  State[J++] = PLatch;
  State[J++] = ALatch;
  State[J++] = VAddr;
  State[J++] = VKey;
  State[J++] = PKey;
  State[J++] = 0;          /* was WKey (deprecated) */
  State[J++] = IRQPending;
  State[J++] = ScanLine;
  State[J++] = RTCReg;
  State[J++] = RTCMode;
  State[J++] = KanLetter;
  State[J++] = KanCount;
  State[J++] = IOReg;
  State[J++] = PSLReg;
  State[J++] = FMPACKey;

  /* Memory setup */
  for(I=0;I<4;++I)
  {
    State[J++] = SSLReg[I];
    State[J++] = PSL[I];
    State[J++] = SSL[I];
    State[J++] = EnWrite[I];
    State[J++] = RAMMapper[I];
  }

  /* Cartridge setup */
  for(I=0;I<MAXSLOTS;++I)
  {
    State[J++] = ROMType[I];
    for(K=0;K<4;++K) State[J++]=ROMMapper[I][K];
  }

  /* SCC setup */
  if (!(Mode&MSX_NO_MEGARAM))
  {
    State[J++] = SCCOn[0];
    State[J++] = SCCOn[1];
    State[J++] = SCCIMode[0];
    State[J++] = SCCIMode[1];
  }

  /* Write out data structures */
  SaveSTRUCT(CPU);
  SaveSTRUCT(PPI);
  SaveSTRUCT(VDP);
  SaveARRAY(VDPStatus);
  SaveARRAY(Palette);
  SaveSTRUCT(PSG);
  SaveSTRUCT(OPLL);
  SaveSTRUCT(SCChip);
  SaveARRAY(State);
  SaveDATA(RAMData,RAMPages*0x4000);
  SaveDATA(VRAM,VRAMPages*0x4000);
  if (!(Mode&MSX_NO_MEGARAM) && SCCIRAM)
    SaveDATA(SCCIRAM,16*0x2000);
  SaveSTRUCT(OPLL_NukeYKT);

  /* Return amount of data written */
  return(Size);
}

/** LoadState() **********************************************/
/** Load emulation state from a memory buffer. Returns size **/
/** on success, 0 on failure.                               **/
/*************************************************************/
unsigned int LoadState(unsigned char *Buf,unsigned int MaxSize)
{
  int State[256],J,I,K;
  unsigned int Size;

  /* No data read yet */
  Size = 0;

  /* Load hardware state */
  LoadSTRUCT(CPU);
  LoadSTRUCT(PPI);
  LoadSTRUCT(VDP);
  LoadARRAY(VDPStatus);
  LoadARRAY(Palette);
  LoadSTRUCT(PSG);
  LoadSTRUCT(OPLL);
  LoadSTRUCT(SCChip);
  LoadARRAY(State);
  LoadDATA(RAMData,RAMPages*0x4000);
  LoadDATA(VRAM,VRAMPages*0x4000);
  if (!(Mode&MSX_NO_MEGARAM) && SCCIRAM)
    LoadDATA(SCCIRAM,16*0x2000);
  LoadSTRUCT(OPLL_NukeYKT);

  /* Parse hardware state */
  J=0;
  VDPData    = State[J++];
  PLatch     = State[J++];
  ALatch     = State[J++];
  VAddr      = State[J++];
  VKey       = State[J++];
  PKey       = State[J++];
  J++;                      /* was WKey = State[J++]; */
  IRQPending = State[J++];
  ScanLine   = State[J++];
  RTCReg     = State[J++];
  RTCMode    = State[J++];
  KanLetter  = State[J++];
  KanCount   = State[J++];
  IOReg      = State[J++];
  PSLReg     = State[J++];
  FMPACKey   = State[J++];

  /* Memory setup */
  for(I=0;I<4;++I)
  {
    SSLReg[I]       = State[J++];
    PSL[I]          = State[J++];
    SSL[I]          = State[J++];
    EnWrite[I]      = State[J++];
    RAMMapper[I]    = State[J++];
  }

  /* Cartridge setup */
  for(I=0;I<MAXSLOTS;++I)
  {
    ROMType[I] = State[J++];
    for(K=0;K<4;++K) ROMMapper[I][K]=State[J++];

    /* Correction for older states */
    if(ROMType[I]==MAP_FMPAC)
      ROMMapper[I][1]=ROMMapper[I][0]|1;
    else if((ROMType[I]==MAP_ASCII16)||(ROMType[I]==MAP_GEN16))
    {
      ROMMapper[I][1]=ROMMapper[I][0]|1;
      ROMMapper[I][3]=ROMMapper[I][2]|1;
    }
  }

  /* SCC setup */
  if (!(Mode&MSX_NO_MEGARAM))
  {
    SCCOn[0]    = State[J++];
    SCCOn[1]    = State[J++];
    SCCIMode[0] = State[J++];
    SCCIMode[1] = State[J++];
  }

  /* Set RAM mapper pages */
  if(RAMMask)
    for(I=0;I<4;++I)
    {
      RAMMapper[I]       &= RAMMask;
      MemMap[3][2][I*2]   = RAMData+RAMMapper[I]*0x4000;
      MemMap[3][2][I*2+1] = MemMap[3][2][I*2]+0x2000;
    }

  /* Set ROM mapper pages */
  for(I=0;I<MAXSLOTS;++I)
    if(ROMData[I]&&ROMMask[I])
      SetMegaROM(I,ROMMapper[I][0],ROMMapper[I][1],ROMMapper[I][2],ROMMapper[I][3]);

  /* Set main address space pages */
  for(I=0;I<4;++I)
  {
    RAM[2*I]   = MemMap[PSL[I]][SSL[I]][2*I];
    RAM[2*I+1] = MemMap[PSL[I]][SSL[I]][2*I+1];
  }

  /* Set palette */
  for(I=0;I<16;++I)
    SetColor(I,(Palette[I]>>16)&0xFF,(Palette[I]>>8)&0xFF,Palette[I]&0xFF);

  /* Set screen mode and VRAM table addresses */
  SetScreen();

  /* Set some other variables */
  VPAGE    = VRAM+((int)VDP[14]<<14);
  FGColor  = VDP[7]>>4;
  BGColor  = VDP[7]&0x0F;
  XFGColor = FGColor;
  XBGColor = BGColor;

  /* All sound channels could have been changed */
  PSG.Changed     = (1<<AY8910_CHANNELS)-1;
  SCChip.Changed  = (1<<SCC_CHANNELS)-1;
  SCChip.WChanged = (1<<SCC_CHANNELS)-1;
  OPLL.Changed    = (1<<YM2413_CHANNELS)-1;

  /* Return amount of data read */
  return(Size);
}
