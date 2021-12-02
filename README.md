fmsx
====

This is a port of Marat Fayzullin's fMSX 6.0 (21-Feb-2021) to the libretro API.

Source : http://fms.komkon.org/fMSX/


## Recognized file extension
* .rom .mx1 .mx2 .ROM .MX1 .MX2 - for ROM images
* .dsk .DSK .fdi .FDI - for FAT12 360/720kB disk images
* .cas .CAS - for fMSX tape files


## Tape (cassette) software
Tapes are automatically started based on their detected type (binary, ASCII or BASIC).

Press F6 to rewind the tape, if that's needed.


## Cheats
"To make cheat codes for `Game.rom`, create `Game.cht` containing codes in `00AAAAAA-DD` and `00AAAAAA-DDDD` formats, one per line. 
Where `AAAAAA` is the ROM address and `DD` is the value to write there. For 16bit values, use `DDDD`.
The cheat file will be loaded automatically." ([fMSX site](https://fms.komkon.org/fMSX/fMSX.html#LABB), section "New in fMSX 4.0")
Press F7 to activate cheats; press F7 again to deactivate. 

Note: this .cht format is specific to fMSX and differs from [RetroArch's CHT files](https://github.com/libretro/libretro-database/blob/master/cht/Microsoft%20-%20MSX%20-%20MSX2%20-%20MSX2P%20-%20MSX%20Turbo%20R/)!
This core does not (yet) support [RetroArch cheat codes](https://docs.libretro.com/guides/cheat-codes/).
Neither Emulator Handled (lr-db for MSX does not even contain any that would apply; no `code` values) 
nor RetroArch Handled (would probably require support for loading content from memory and/or memory mapping).

A BlueMSX MCF named `Game.mcf` will also be loaded automatically. Press F7 repeatedly to active cheats one by one, or disable cheats.  


## Configuration options

Specify these in your RetroArch core options, either manually or via the RetroArch GUI.

|setting|meaning|choices<br>(*) indicates the default setting
|---|---|---
|`fmsx_mode`|MSX model|MSX2+*&vert;MSX1&vert;MSX2
|`fmsx_video_mode`|select 60Hz or 50Hz|NTSC*&vert;PAL
|`fmsx_mapper_type_mode`|ROM mapper - use if a ROM does not load|Guess*&vert;Generic 8kB&vert;Generic 16kB&vert;Konami5 8kB&vert;Konami4 8kB&vert;ASCII 8kB&vert;ASCII 16kB&vert;GameMaster2&vert;FMPAC
|`fmsx_ram_pages`|RAM size|Auto*&vert;64KB&vert;128KB&vert;256KB&vert;512KB&vert;4MB
|`fmsx_vram_pages`|Video-RAM size|Auto*&vert;32KB&vert;64KB&vert;128KB&vert;192KB
|`fmsx_simbdos`|Simulate BDOS DiskROM access calls|No*&vert;Yes
|`fmsx_autospace`|Autofire the spacebar|No*&vert;Yes
|`fmsx_allsprites`|Show all sprites - do not emulate VDP hardware limitation|No*&vert;Yes
|`fmsx_font`|load a fixed text font from  RetroArch's `system_directory`|standard*&vert;DEFAULT.FNT&vert;ITALIC.FNT&vert;INTERNAT.FNT&vert;CYRILLIC.FNT&vert;KOREAN.FNT&vert;JAPANESE.FNT


## BIOS

BIOS ROMs are loaded from RetroArch's `system_directory`. The screen will remain black if required ROMs are missing.

These BIOS ROMs are required for execution:
* MSX1: MSX.ROM
* MSX2: MSX2.ROM, MSX2EXT.ROM
* MSX2+: MSX2P.ROM, MSX2PEXT.ROM

Optional; loaded when found:
* DISK.ROM
* FMPAC.ROM
* KANJI.ROM
* MSXDOS2.ROM (MSX2/2+)
* PAINTER.ROM (MSX2/2+) - press space during boot to start
* RS232.ROM
* CMOS.ROM
* GMASTER2.ROM, GMASTER.ROM


## Mapping of controls

User 1:

* "Joystick": map RetroPad to MSX joystick A

|RetroPad|MSX
|---|---
|LEFT  | Stick Left
|UP    |   Stick Up
|DOWN  | Stick Down
|RIGHT |Stick Right
|A     |     Fire A
|B     |     Fire B
* "Joystick + Emulated Keyboard": map RetroPad to MSX joystick A, plus to a few MSX keyboard keys useful for gaming

|RetroPad|MSX
|---|---
|LEFT  | Stick Left
|UP    |   Stick Up
|DOWN  | Stick Down
|RIGHT |Stick Right
|A     |     Fire A
|B     |     Fire B
|X     |         F3
|Y     |      Space
|START |         F1
|SELECT|         F2
|L     |         F4
|R     |         F5
|L2    |      Graph
|R2    |       Ctrl
|L3    |      Enter
|R3    |     Escape
* "Emulated Keyboard": map RetroPad to MSX keyboard cursor, plus to a few _other_ MSX keyboard keys useful for gaming - where * indicates a difference with "Joystick + Emulated Keyboard"

|RetroPad|MSX
|---|---
|LEFT  | Arrow Left
|UP    |   Arrow Up
|DOWN  | Arrow Down
|RIGHT |Arrow Right
|A     |      Space *
|B     |      Enter *
|X     |          N *
|Y     |          M *
|START |         F1
|SELECT|         F4 *
|L     |         F2 *
|R     |         F3 *
|L2    |      Graph
|R2    |       Ctrl
|L3    |         F5 *
|R3    |     Escape
* "Keyboard": maps host keyboard to MSX keyboard. Only on (RetroArch) platforms with a real keyboard (Linux, Windows, etc). Don't forget to press Scroll Lock to enter Game Focus Mode!
  * MSX1 & 2: US/European keyboard map, cursors, numeric pad, F1-F5
  * MSX2+: Japanese keyboard map - see below
  * with these special keys for all 3 MSX models:

|Host             | MSX
|---|---
|ctrl             |CONTROL
|shift            |SHIFT
|left alt         |GRAPH
|ins              |INSERT
|del              |DELETE
|home             |HOME/CLS
|end              |SELECT
|pageup or pause  |STOP/BREAK
|pagedown         |COUNTRY


User 2:
* "Joystick": map RetroPad to MSX joystick B

### MSX1/2 US/European keyboard map
Enter accented characters (like &eacute;) with COUNTRY (page down) and graphical symbols with GRAPH (left alt).

### MSX2+ Japanese keyboard map
This is a typical MSX2+ with Japanese keyboard:
![Japanese keyboard](Japanese-MSX2+-keyboard.jpg)

How to use this:
* normal: bottomleft Roman letter/symbol
* shift: topleft symbol
* alt: topright symbol (without the box)
* KANA LOCK active: bottomright Japanese character
* KANA LOCK active with shift: middleright Japanese character

To (de)activate KANA LOCK, press page down (COUNTRY). It works just like caps lock: press and release to enable.

Best enable SCREEN 1 to appreciate the full 8px width of the Japanese characters; in screen 0 characters are only 6px wide.


## Technical details

Video: 16bpp RGB565 (PSP: BGR565, PS2: BGR555) 272x228 (544x228 in 512px MSX2 screen modes). This includes an 8px (16px) border; MSX native screen resolutions are:
- horizontal: 256 or 512 (textmode: 32, 40 or 80 columns)
- vertical: 192 or 212

Audio: rendered in 48kHz 16b mono.
fMSX emulates PSG, SCC and FM-PAC.

Framerate: NTSC (US/JP) implies 60Hz - thus 60FPS, PAL (EU) implies 50Hz (=50FPS). Gameplay and audio actually becomes 17% slower when switching from NTSC to PAL - just like on a real MSX.

### MSX1 colour palette

"To make a custom palette for `Game.rom`, create `Game.pal` [in the same directory] containing 16 #RRGGBB hex values, one per line. 
This palette file will be loaded automatically." ([fMSX site](https://fms.komkon.org/fMSX/fMSX.html#LABB), section "New in fMSX 4.0")

The fMSX default:
```
#000000
#000000
#20C020
#60E060
#2020E0
#4060E0
#A02020
#40C0E0
#E02020
#E06060
#C0C020
#C0C080
#208020
#C040A0
#A0A0A0
#E0E0E0
```

Note: [there is some discussion](https://github.com/openMSX/openMSX/issues/1024) about the 'right' mapping of TMS9918 colours to RGB. This is how fMSX does it.

An [example alternative palette](https://paulwratt.github.io/programmers-palettes/HW-MSX/HW-MSX.html) with less vibrant colours is:
```
#000000
#010101
#3eb849
#74d07d
#5955e0
#8076f1
#b95e51
#65dbef
#db6559
#ff897d
#ccc35e
#ded087
#3aa241
#b766b5
#cccccc
#ffffff
```

## Developers
Some information for developers wanting to upgrade to newer fMSX versions, or improve this port.

### Changes applied to the fMSX sources in this port
Some changes are applied to the fMSX core in order to make fmsx-libretro portable & efficient for its target audience. 

* portability refactorings, e.g. `register` flags removed, PS Vita file support, etc.
* (verbose) logging removed, including startup info & some debugging statements
* some whitespace
* `EMULib.c` `WaitJoystick()` removed; implemented another way in `libretro.c`
* RetroArch VFS (Virtual FileSystem) used (`rfopen` etc.); ZLIB code removed
* removed various pieces of code intended for older ports, like Borland C, Meego, etc.
* removed various pieces of code intended for other platforms (fMSX is part of a suite of emulators)
* removed `SndDriver`; implemented another way in `libretro.c`
* reimplemented state loading/saving
* switched MSB first/LSB first
* due to the fact that fmsx-libretro renders audio&video per frame:
    * delay invocation of `SyncSCC()`/`Sync2413()` to fix a sound interference bug
    * drop invocation of `PlayAllSound()`
    * `MSX.c` `LoopZ80()`: move `if(ExitNow) return(INT_QUIT)` downwards to support `autospace` option.

### non-ported/dropped fMSX features
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
* serial COM
* printer

The following fMSX code is not present in this core:
* fMSX/Unix-related code
* EMULib: `EMULib.c, Console(Mux).c/h, Hunt.c/h, Image.c, ImageMux.h, IPS.c/h, MIDIFreq.h, NetPlay.c/h, Record.c/h, Touch(Mux).c/h`
* fMSX: `fMSX.c, fMSX.html, Help.h, Menu.c, I8251.c/h`
* Z80: `(Con)Debug.c`

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
