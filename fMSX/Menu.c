/** fMSX: portable MSX emulator ******************************/
/**                                                         **/
/**                         Menu.c                          **/
/**                                                         **/
/** This file contains runtime menu code for configuring    **/
/** the emulator. It uses console functions from Console.h. **/
/**                                                         **/
/** Copyright (C) Marat Fayzullin 2005-2014                 **/
/**     You are not allowed to distribute this software     **/
/**     commercially. Please, notify me, if you make any    **/
/**     changes to this file.                               **/
/*************************************************************/
#include "MSX.h"
#include "Console.h"
#include "Sound.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define CLR_BACK   PIXEL(255,255,255)
#define CLR_BACK2  PIXEL(255,200,150)
#define CLR_BACK3  PIXEL(150,255,255)
#define CLR_BACK4  PIXEL(255,255,150)
#define CLR_TEXT   PIXEL(0,0,0)
#define CLR_WHITE  PIXEL(255,255,255)
#define CLR_ERROR  PIXEL(200,0,0)

static char SndNameBuf[256];

extern byte *MemMap[4][4][8];         /* [PPage][SPage][Adr] */
extern byte *EmptyRAM;                /* Dummy memory area   */

/** MenuMSX() ************************************************/
/** Invoke a menu system allowing to configure the emulator **/
/** and perform several common tasks.                       **/
/*************************************************************/
void MenuMSX(void)
{
  const char *P;
  char S[512];
  int I,J,K,N;

  /* Display and activate top menu */
  for(J=1;J;)
  {
    /* Compose menu */
    sprintf(S,
      "fMSX\n"
      "Load file\n"
      "Save file\n"
      "  \n"
      "Hardware model\n"
      "Input devices\n"
      "Cartridge slots\n"
      "Disk drives\n"
      "  \n"
      "Log soundtrack    %c\n"
      "Hit MIDI drums    %c\n"
      "  \n"
      "Use fixed font    %c\n"
      "Show all sprites  %c\n"
      "Patch DiskROM     %c\n"
      "  \n"
      "POKE &hFFFF,&hAA\n"
      "Rewind tape\n"
      "Reset emulator\n"
      "Quit emulator\n"
      "  \n"
      "Done\n",
      MIDILogging(MIDI_QUERY)? CON_CHECK:' ',
      OPTION(MSX_DRUMS)?       CON_CHECK:' ',
      OPTION(MSX_FIXEDFONT)?   CON_CHECK:' ',
      OPTION(MSX_ALLSPRITE)?   CON_CHECK:' ',
      OPTION(MSX_PATCHBDOS)?   CON_CHECK:' '
    );

    /* Replace all EOLNs with zeroes */
    for(I=0;S[I];I++) if(S[I]=='\n') S[I]='\0';
    /* Run menu */
    J=CONMenu(-1,-1,-1,-1,CLR_TEXT,CLR_BACK,S,J);
    /* Handle menu selection */
    switch(J)
    {
      case 1: /* Load cartridge, disk image, state, or font */
        /* Request file name */
        P=CONFile(CLR_TEXT,CLR_BACK3,".rom\0.rom.gz\0.mx1\0.mx1.gz\0.mx2\0.mx2.gz\0.dsk\0.dsk.gz\0.sta\0.sta.gz\0.cas\0.fnt\0.fnt.gz\0");
        /* Try loading file, show error on failure */
        if(P&&!LoadSTA(P)&&!LoadFile(P))
          CONMsg(-1,-1,-1,-1,CLR_BACK,CLR_ERROR,"Error","Cannot load file.\0\0");
        /* Exit top menu */
        J=0;
        break;

      case 2: /* Save state, printer output, or soundtrack */
        /* Run menu */
        switch(CONMenu(-1,-1,-1,-1,CLR_TEXT,CLR_BACK4,
          "Save File\0Emulation state\0Printer output\0MIDI soundtrack\0",1
        ))
        {
          case 1: /* Save state */
            /* Request file name */
            P=CONFile(CLR_TEXT,CLR_BACK2,".sta\0");
            /* Try saving state, show error on failure */
            if(P&&!SaveSTA(P))
              CONMsg(-1,-1,-1,-1,CLR_BACK,CLR_ERROR,"Error","Cannot save state.\0\0");
            /* Exit top menu */
            J=0;
            break;
          case 2: /* Printer output file */
            /* Request file name */
            P=CONFile(CLR_TEXT,CLR_BACK2,".prn\0.out\0.txt\0");
            /* Try changing printer output */
            if(P) ChangePrinter(P);
            /* Exit top menu */
            J=0;
            break;
          case 3: /* Soundtrack output file */
            /* Request file name */
            P=CONFile(CLR_TEXT,CLR_BACK2,".mid\0.rmi\0");
            if(P)
            {
              /* Try changing MIDI log output, show error on failure */
              if(strlen(P)+1>sizeof(SndNameBuf))
                CONMsg(-1,-1,-1,-1,CLR_BACK,CLR_ERROR,"Error","Name too long.\0\0");
              else
              {
                strcpy(SndNameBuf,P);
                SndName=SndNameBuf;
                InitMIDI(SndName);
                MIDILogging(MIDI_ON);
              }
            }
            /* Exit top menu */
            J=0;
            break;
        }
        break;

      case 4: /* Hardware model */
        for(K=1;K;)
        {
          /* Compose menu */
          sprintf(S,
            "Hardware Model\n"
            "MSX1 (TMS9918)    %c\n"
            "MSX2 (V9938)      %c\n"
            "MSX2+ (V9958)     %c\n"
            "  \n"
            "NTSC (US/Japan)   %c\n"
            "PAL (Europe)      %c\n"
            "  \n"
            "Main memory   %3dkB\n"
            "Video memory  %3dkB\n"
            "  \n"
            "Done\n",
            MODEL(MSX_MSX1)?       CON_CHECK:' ',
            MODEL(MSX_MSX2)?       CON_CHECK:' ',
            MODEL(MSX_MSX2P)?      CON_CHECK:' ',
            VIDEO(MSX_NTSC)?       CON_CHECK:' ',
            VIDEO(MSX_PAL)?        CON_CHECK:' ',
            RAMPages*16,
            VRAMPages*16
          );
          /* Replace all EOLNs with zeroes */
          for(I=0;S[I];I++) if(S[I]=='\n') S[I]='\0';
          /* Run menu */
          K=CONMenu(-1,-1,-1,-1,CLR_TEXT,CLR_BACK4,S,K);
          /* Handle menu selection */
          switch(K)
          {
            case 1:  if(!MODEL(MSX_MSX1))  ResetMSX((Mode&~MSX_MODEL)|MSX_MSX1,RAMPages,VRAMPages);break;
            case 2:  if(!MODEL(MSX_MSX2))  ResetMSX((Mode&~MSX_MODEL)|MSX_MSX2,RAMPages,VRAMPages);break;
            case 3:  if(!MODEL(MSX_MSX2P)) ResetMSX((Mode&~MSX_MODEL)|MSX_MSX2P,RAMPages,VRAMPages);break;
            case 5:  if(!VIDEO(MSX_NTSC))  ResetMSX((Mode&~MSX_VIDEO)|MSX_NTSC,RAMPages,VRAMPages);break;
            case 6:  if(!VIDEO(MSX_PAL))   ResetMSX((Mode&~MSX_VIDEO)|MSX_PAL,RAMPages,VRAMPages);break;
            case 8:  ResetMSX(Mode,RAMPages<32? RAMPages*2:4,VRAMPages);break;
            case 9:  ResetMSX(Mode,RAMPages,VRAMPages<32? VRAMPages*2:2);break;
            case 11: K=0;break;
          }
        }
        /* Exit top menu */
        J=0;
        break;

      case 5: /* Input devices */
        for(K=1;K;)
        {
          /* Compose menu */
          sprintf(S,
            "Input Devices\n"
            "SOCKET1:Empty       %c\n"
            "SOCKET1:Joystick    %c\n"
            "SOCKET1:JoyMouse    %c\n"
            "SOCKET1:Mouse       %c\n"
            "  \n"
            "SOCKET2:Empty       %c\n"
            "SOCKET2:Joystick    %c\n"
            "SOCKET2:JoyMouse    %c\n"
            "SOCKET2:Mouse       %c\n"
            "  \n"
            "Autofire on SPACE   %c\n"
            "Autofire on FIRE-A  %c\n"
            "Autofire on FIRE-B  %c\n"
            "  \n"
            "Done\n",
            JOYTYPE(0)==JOY_NONE?     CON_CHECK:' ',
            JOYTYPE(0)==JOY_STICK?    CON_CHECK:' ',
            JOYTYPE(0)==JOY_MOUSTICK? CON_CHECK:' ',
            JOYTYPE(0)==JOY_MOUSE?    CON_CHECK:' ',
            JOYTYPE(1)==JOY_NONE?     CON_CHECK:' ',
            JOYTYPE(1)==JOY_STICK?    CON_CHECK:' ',
            JOYTYPE(1)==JOY_MOUSTICK? CON_CHECK:' ',
            JOYTYPE(1)==JOY_MOUSE?    CON_CHECK:' ',
            OPTION(MSX_AUTOSPACE)?    CON_CHECK:' ',
            OPTION(MSX_AUTOFIREA)?    CON_CHECK:' ',
            OPTION(MSX_AUTOFIREB)?    CON_CHECK:' '
          );
          /* Replace all EOLNs with zeroes */
          for(I=0;S[I];I++) if(S[I]=='\n') S[I]='\0';
          /* Run menu */
          K=CONMenu(-1,-1,-1,-1,CLR_TEXT,CLR_BACK4,S,K);
          /* Handle menu selection */
          switch(K)
          {
            case 1:  SETJOYTYPE(0,JOY_NONE);break;
            case 2:  SETJOYTYPE(0,JOY_STICK);break;
            case 3:  SETJOYTYPE(0,JOY_MOUSTICK);break;
            case 4:  SETJOYTYPE(0,JOY_MOUSE);break;
            case 6:  SETJOYTYPE(1,JOY_NONE);break;
            case 7:  SETJOYTYPE(1,JOY_STICK);break;
            case 8:  SETJOYTYPE(1,JOY_MOUSTICK);break;
            case 9:  SETJOYTYPE(1,JOY_MOUSE);break;
            case 11: Mode^=MSX_AUTOSPACE;break;
            case 12: Mode^=MSX_AUTOFIREA;break;
            case 13: Mode^=MSX_AUTOFIREB;break;
            case 15: K=0;break;
          }
        }
        /* Exit top menu */
        J=0;
        break;

      case 6: /* Cartridge slots */
        /* Create slot selection menu */
        sprintf(S,
          "Cartridges\n"
          "Slot A:%c\n"
          "Slot B:%c\n",
          MemMap[1][0][2]!=EmptyRAM? CON_FILE:' ',
          MemMap[2][0][2]!=EmptyRAM? CON_FILE:' '
        );
        /* Replace all EOLNs with zeroes */
        for(I=0;S[I];I++) if(S[I]=='\n') S[I]='\0';
        /* Get cartridge slot number */
        N=CONMenu(-1,-1,-1,-1,CLR_TEXT,CLR_BACK4,S,1);
        /* Exit to top menu if cancelled */
        if(!N) break; else --N;
        /* Run slot-specific menu */
        for(K=1;K;)
        {
          /* Compose menu */
          sprintf(S,
            "Cartridge Slot %c\n"
            "Load cartridge\n"
            "Eject cartridge\n"
            "Guess MegaROM mapper  %c\n"
            "  \n"
            "Generic 8kB switch    %c\n"
            "Generic 16kB switch   %c\n"
            "Konami 5000h mapper   %c\n"
            "Konami 4000h mapper   %c\n"
            "ASCII 8kB mapper      %c\n"
            "ASCII 16kB mapper     %c\n"
            "Konami GameMaster2    %c\n"
            "Panasonic FMPAC       %c\n"
            "  \n"
            "Done\n",
            'A'+N,
            ROMGUESS(N)?              CON_CHECK:' ',
            ROMTYPE(N)==MAP_GEN8?     CON_CHECK:' ',
            ROMTYPE(N)==MAP_GEN16?    CON_CHECK:' ',
            ROMTYPE(N)==MAP_KONAMI5?  CON_CHECK:' ',
            ROMTYPE(N)==MAP_KONAMI4?  CON_CHECK:' ',
            ROMTYPE(N)==MAP_ASCII8?   CON_CHECK:' ',
            ROMTYPE(N)==MAP_ASCII16?  CON_CHECK:' ',
            ROMTYPE(N)==MAP_GMASTER2? CON_CHECK:' ',
            ROMTYPE(N)==MAP_FMPAC?    CON_CHECK:' '
          );
          /* Replace all EOLNs with zeroes */
          for(I=0;S[I];I++) if(S[I]=='\n') S[I]='\0';
          /* Run menu */
          K=CONMenu(-1,-1,-1,-1,CLR_TEXT,CLR_BACK4,S,K);
          /* Handle menu selection */
          switch(K)
          {
            case 1:
              /* Request file name */
              P=CONFile(CLR_TEXT,CLR_BACK3,".rom\0.rom.gz\0.mx1\0.mx1.gz\0.mx2\0.mx2.gz\0");
              /* Try loading file, show error on failure */
              if(P&&!LoadCart(P,N,ROMGUESS(N)|ROMTYPE(N)))
                CONMsg(-1,-1,-1,-1,CLR_BACK,CLR_ERROR,"Error","Cannot load file.\0\0");
              /* Exit to top menu */
              K=0;
              break;
            case 2:  LoadCart(0,N,ROMGUESS(N)|ROMTYPE(N));K=0;break;
            case 3:  Mode^=MSX_GUESSA<<N;break;
            case 5:
            case 6:
            case 7:
            case 8:
            case 9:
            case 10:
            case 11:
            case 12: SETROMTYPE(N,K-5);ResetMSX(Mode,RAMPages,VRAMPages);break;
            case 14: K=0;break;
          }
        }
        /* Exit top menu */
        J=0;
        break;

      case 7: /* Disk drives */
        /* Create drive selection menu */
        sprintf(S,
          "Disk Drives\n"
          "Drive A:%c\n"
          "Drive B:%c\n",
          FDD[0].Data? CON_FILE:' ',
          FDD[1].Data? CON_FILE:' '
        );
        /* Replace all EOLNs with zeroes */
        for(I=0;S[I];I++) if(S[I]=='\n') S[I]='\0';
        /* Get disk drive number */
        N=CONMenu(-1,-1,-1,-1,CLR_TEXT,CLR_BACK4,S,1);
        /* Exit to top menu if cancelled */
        if(!N) break; else --N;
        /* Create disk operations menu */
        sprintf(S,
          "Disk Drive %c:\n"
          "Load disk\n"
          "New disk\n"
          "Eject disk\n"
          " \n"
          "Save DSK image\n"
          "Save FDI image\n",
          'A'+N
        );
        /* Replace all EOLNs with zeroes */
        for(I=0;S[I];I++) if(S[I]=='\n') S[I]='\0';
        /* Run menu and handle menu selection */
        switch(CONMenu(-1,-1,-1,-1,CLR_TEXT,CLR_BACK4,S,1))
        {
          case 1: /* Load disk */
            P=CONFile(CLR_TEXT,CLR_BACK3,".dsk\0.dsk.gz\0.fdi\0.fdi.gz\0");
            if(P&&!ChangeDisk(N,P))
              CONMsg(-1,-1,-1,-1,CLR_BACK,CLR_ERROR,"Error","Cannot load disk image.\0\0");
            break;
          case 2: /* New disk */
            ChangeDisk(N,"");
            break;
          case 3: /* Eject disk */
            ChangeDisk(N,0);
            break;
          case 5: /* Save .DSK image */
            P=CONFile(CLR_TEXT,CLR_BACK2,".dsk\0");
            if(P&&!SaveFDI(&FDD[N],P,FMT_DSK))
              CONMsg(-1,-1,-1,-1,CLR_BACK,CLR_ERROR,"Error","Cannot save disk image.\0\0");
            break;
          case 6: /* Save .FDI image */
            P=CONFile(CLR_TEXT,CLR_BACK2,".fdi\0");
            if(P&&!SaveFDI(&FDD[N],P,FMT_FDI))
              CONMsg(-1,-1,-1,-1,CLR_BACK,CLR_ERROR,"Error","Cannot save disk image.\0\0");
            break;
        }
        /* Exit top menu */
        J=0;
        break;

      case 9: /* Log MIDI soundtrack */
        MIDILogging(MIDI_TOGGLE);
        break;
      case 10: /* Hit MIDI drums for noise */
        Mode^=MSX_DRUMS;
        break;
      case 12: /* Use fixed font */
        Mode^=MSX_FIXEDFONT;
        break;
      case 13: /* Show all sprites */
        Mode^=MSX_ALLSPRITE;
        break;
      case 14: /* Patch DiskROM routines */
        ResetMSX(Mode^MSX_PATCHBDOS,RAMPages,VRAMPages);
        break;
      case 16: /* POKE &hFFFF,&hAA */
        WrZ80(0xFFFF,0xAA);
        J=0;
        break;
      case 17: /* Rewind */
        RewindTape();
        J=0;
        break;
      case 18: /* Reset */
        ResetMSX(Mode,RAMPages,VRAMPages);
        J=0;
        break;
      case 19: /* Quit */
        ExitNow=1;
        J=0;
        break;
      case 21: /* Done */
        J=0;
        break;
    }
  }
}
