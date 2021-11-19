fmsx
====

this is a port of fMSX 5.1 to the libretro API

source : http://fms.komkon.org/fMSX/


## Recognized file extension
* .rom .mx1 .mx2 .ROM .MX1 .MX2 - for ROM images
* .dsk .DSK - for 360/720kB disk images
* .cas .CAS - for fMSX tape files


## Configuration options

Specify these in your RetroArch core options:

    fmsx_mode=MSX2+*|MSX1|MSX2
    fmsx_video_mode=NTSC*|PAL
    fmsx_mapper_type_mode=Guess Mapper Type A*|Guess Mapper Type B|Generic 8kB|Generic 16kB|Konami5 8kB|Konami4 8kB|ASCII 8kB|ASCII 16kB|GameMaster2|FMPAC
    fmsx_ram_pages=Auto*|64KB|128KB|256KB|512KB
    fmsx_vram_pages=Auto*|32KB|64KB|128KB|192KB
    fmsx_simbdos=No*|Yes

A star (*) indicates this is the default setting.


## BIOS
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

Video: 16bpp RGB565 272x228 (544x228 in 512px MSX2 screen modes). This includes an 8px (16px) border; MSX native screen resolutions are:
- horizontal: 256 or 512 (textmode: 40 or 80 columns)
- vertical: 192 or 212

Audio: rendered in 48kHz 16b mono.

Framerate: NTSC (US/JP) implies 60Hz - thus 60FPS, PAL (EU) implies 50Hz (=50FPS). Gameplay and audio actually becomes 17% slower when switching from NTSC to PAL - just like on a real MSX.
