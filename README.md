fmsx
====

This is a port of Marat Fayzullin's fMSX 6.0 (21-Feb-2021) to the libretro API.

Source : http://fms.komkon.org/fMSX/


## Recognized file extension
* .rom .mx1 .mx2 .ROM .MX1 .MX2 - for ROM images
* .dsk .DSK - for FAT12 360/720kB disk images
* .cas .CAS - for fMSX tape files


## Configuration options

Specify these in your RetroArch core options:

    fmsx_mode=MSX2+*|MSX1|MSX2
    fmsx_video_mode=NTSC*|PAL
    fmsx_mapper_type_mode=Guess Mapper Type A*|Guess Mapper Type B|Generic 8kB|Generic 16kB|Konami5 8kB|Konami4 8kB|ASCII 8kB|ASCII 16kB|GameMaster2|FMPAC
    fmsx_ram_pages=Auto*|64KB|128KB|256KB|512KB
    fmsx_vram_pages=Auto*|32KB|64KB|128KB|192KB
    fmsx_allsprites=No*|Yes
    fmsx_simbdos=No*|Yes

A star (*) indicates this is the default setting.


## BIOS
BIOS ROMs are loading from RetroArch's `system_directory`.

These BIOS ROMs are required for execution:
* MSX1: MSX.ROM
* MSX2: MSX2.ROM, MSX2EXT.ROM
* MSX2+: MSX2P.ROM, MSX2PEXT.ROM

Optional; loaded when found:
* DISK.ROM
* FMPAC.ROM
* KANJI.ROM
* MSXDOS2.ROM (MSX2/2+)
* PAINTER.ROM (MSX2/2+)
* RS232.ROM
* CMOS.ROM
* GMASTER2.ROM, GMASTER.ROM


## Technical details

Video: 16bpp RGB565 (PSP: BGR565, PS2: BGR555) 272x228 (544x228 in 512px MSX2 screen modes). This includes an 8px (16px) border; MSX native screen resolutions are:
- horizontal: 256 or 512 (textmode: 40 or 80 columns)
- vertical: 192 or 212

Audio: rendered in 48kHz 16b mono.
fMSX emulates PSG, SCC and FM-PAC.

Framerate: NTSC (US/JP) implies 60Hz - thus 60FPS, PAL (EU) implies 50Hz (=50FPS). Gameplay and audio actually becomes 17% slower when switching from NTSC to PAL - just like on a real MSX.

### MSX1 colour palette
This is how fMSX implements it by default:
* 0 : 000000
* 1 : 000000
* 2 : 20C020
* 3 : 60E060
* 4 : 2020E0
* 5 : 4060E0
* 6 : A02020
* 7 : 40C0E0
* 8 : E02020
* 9 : E06060
* A : C0C020
* B : C0C080
* C : 208020
* D : C040A0
* E : A0A0A0
* F : E0E0E0

Note: there is some discussion about the 'right' mapping of TMS9918 colours to RGB. This is how fMSX does it.


## Developers
Some information for developers wanting to upgrade to newer fMSX versions, or improve this port.

### Changes applied to the fMSX sources in this port
Some changes are applied to the fMSX core in order to make fmsx-libretro portable & efficient for its target audience. 

* portability refactorings, e.g. `register` flags removed, PS Vita file support, etc.
* (verbose) logging removed, including startup info & some debugging statements
* some whitespace
* `EMULib.c` `WaitJoystick()` is empty; implemented another way in `libretro.c`
* RetroArch VFS (Virtual FileSystem) used (`rfopen` etc.); ZLIB code removed
* removed various pieces of code intended for older ports, like Borland C, Meego, etc.
* removed various pieces of code intended for other platforms (fMSX is part of a suite of emulators)
* removed `SndDriver`; implemented another way in `libretro.c`
* reimplemented state loading/saving
* switched MSB first/LSB first
* due to the fact that fmsx-libretro renders audio&video per scanline:
    * delay invocation of `SyncSCC()`/`Sync2413()` to fix a sound interference bug
    * drop invocation of `PlayAllSound()`  
    * `MSX.c` `LoopZ80()`: move `if(ExitNow) return(INT_QUIT)` downwards to support `autospace` option.

### non-ported fMSX features
Mostly because RetroArch supports this out of the box, or because it falls out of scope.

* built-in debugger
* in-emulator options menu
* MIDI sound export ('MIDI logging')
* IPS patching
* custom shader-like effects
* cheat 'hunter'
* net play
* record & playback
* support for touch devices
* mouse in both joystick & real mode

The following fMSX code is not present in this core:
* fMSX/Unix-related code
* EMULib: `Console(Mux).c/h, Hunt.c/h, Image.c, ImageMux.h, IPS.c/h, MIDIFreq.h, NetPlay.c/h, Record.c/h, Touch(Mux).c/h`
* fMSX: `fMSX.c, fMSX.html, Help.h, Menu.c, Patch.c`

### non-ported/supported fMSX cmdline options
* all of these options:
  

    -verbose <level>    
    -skip <percent>     - always 0
    -help               
    -home <dirname>     
    -printer <filename>
    -serial <filename>  
    -diskb <filename>   
    -font <filename>    
    -logsnd <filename>  - MIDI logging removed
    -state <filename>   
    -joy <type>         - both joysticks always on. Both mouse modes unsupported.
    -sound [<quality>]  - always 48kHz
    -nosound
* All effects-related options
* All options based on a platform-specific `#define` (incl. `-static`, `-nosync` & `-sync`)
