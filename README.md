fmsx
====

This is a port of Marat Fayzullin's fMSX 6.0 (21-Feb-2021) to the libretro API.

Source : http://fms.komkon.org/fMSX/


## Recognized file extensions
* .rom .mx1 .mx2 .ROM .MX1 .MX2 - for ROM images
* .dsk .DSK .fdi .FDI - for FAT12 360/720kB disk images
* .cas .CAS - for fMSX tape files
* .m3u .M3U - for multidisk software

The supplied location must exist and must be a readable file with one of the listed extensions. 
If, e.g., it points to a directory or non-existent file, 
no image is loaded and this core will boot into MSX-BASIC. It is not possible to insert a disk into a running core.


## Saving state
Some state is automatically persisted to these files at shutdown:
- [RTC](https://www.msx.org/wiki/Ricoh_RP-5C01) (real time clock, screen settings, etc.): to `CMOS.ROM` (system directory, 52B)
- **SRAM** in **ASCII8** & **ASCII16** mapper ROMs: to `<Game>.sav` (working directory, 8 or 2KiB)
- **FM-PAC SRAM**: to `FMPAC.sav` (working directory, 8KiB)
- **Konami Game Master 2 SRAM**: to `GMASTER2.sav` (working directory, 8KiB)

These files will only be created when the RTC resp. SRAM data is actually changed during gameplay.

SRAM only applies when the respective support ROM (FMPAC, GMASTER2) is loaded, and when the selected content 
supports that type of SRAM. Consult a game's manual to verify if SRAM saving is supported, and if so, what type.
The `FMPAC.ROM` and/or `GMASTER2.ROM` must be present in the RetroArch system directory and are to be provided by the user.

Many disk-based games, especially multi-disk games, support saving to disk.
To persist disk saves, set option "Save changes to .dsk image" (`fmsx_flush_disk`) to "Immediate" or "On close".

This core currently does _not_ support saving to RetroArch RTC `Game.rtc` or SRAM `Game.srm` files.

If changing your game disks for savegames is not preferred, opt instead for persisting state; 
by default Save State button is F2 and Load State is F4. RetroArch will save state to `Game.state` in the
configured `savestate_directory`.

For the state to be properly restored after a restart, this core must be started with **exactly** the same settings 
(MSX type, RAM size, etc.) & loaded files (ROMs, DSKs, CMOS, SRAM, etc).


## Tape (cassette) software
Tapes are automatically started based on their detected type (binary, ASCII or BASIC).

Press F6 to rewind the tape, if that's needed.


## Multidisk software
Create a textfile with extension `.m3u` and list one `.dsk` filename per line.
The file will be resolved relative to the directory location of the `.m3u`-file.
Absolute files are also supported; start the full path with `<drive>:` on Windows or `/` on other OSes.

Navigate through the disk images using RetroArch hotkeys; configure these settings:

    # keyboard settings
    input_disk_eject_toggle = ".."
    input_disk_prev = ".."
    input_disk_next = ".."
and/or

    # RetroPad settings
    input_disk_eject_toggle_btn = ".."
    input_disk_prev_btn = ".."
    input_disk_next_btn = ".."

Note: 
* these hotkeys are by default _unmapped_ in RetroArch
* images can only be swapped in the 'eject' state


## Creating empty disk images
An MSX computer will boot into an MSX-BASIC prompt when no cartridge or executable disk is inserted.
By default, attempts to do disk I/O will then result in 'Disk offline' errors.

To create an empty disk image, which can be written to from MSX-BASIC, set both of these options:
* "Create empty disk image when none loaded": Yes (option `fmsx_phantom_disk`)
* "Save changes to .dsk": Immediate or On close (option `fmsx_flush_disk`)
Without the latter setting, changes to the created disk will be lost.

This option can also be used to create User Disks for multi-disk games.

The following situations can arise:
* _no content selected when starting core_: changes will be lost; no filename is known
* _non-existent .cas, .rom, .m3u or .fdi selected_: changes will be lost
* _non-existent .dsk image selected_: .dsk image created on host filesystem
* _non-existent file listed in .m3u_: .dsk image created on host filesystem
* _RGUI > Quick Menu > Disk Control > Disk Image Append_: RGUI enforces an _existing_ file to be chosen.
  However, this can, e.g., be a zero-byte file. 
  As long as it has a .dsk file extension, a valid .dsk image file will be created & saved.


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

A restart is required after changing most of these options.  

|setting|meaning|choices<br>(*) indicates the default setting
|---|---|---
|`fmsx_mode`|MSX model|MSX2+*&vert;MSX1&vert;MSX2
|`fmsx_video_mode`|select 60Hz or 50Hz|NTSC*&vert;PAL&vert;Dynamic
|`fmsx_hires`|Support high resolution|Off*&vert;Interlaced&vert;Progressive
|`fmsx_overscan`|Support overscan|*No&vert;Yes
|`fmsx_mapper_type_mode`|ROM mapper - use if a ROM does not load|Guess*&vert;Generic 8kB&vert;Generic 16kB&vert;Konami5 8kB&vert;Konami4 8kB&vert;ASCII 8kB&vert;ASCII 16kB&vert;GameMaster2&vert;FMPAC
|`fmsx_ram_pages`|RAM size|Auto*&vert;64KB&vert;128KB&vert;256KB&vert;512KB&vert;4MB
|`fmsx_vram_pages`|Video-RAM size|Auto*&vert;32KB&vert;64KB&vert;128KB&vert;192KB
|`fmsx_load_game_master`|Load GMASTER(2).ROM when present (will start Game Master before the game)|No*&vert;Yes
|`fmsx_simbdos`|Simulate BDOS DiskROM access calls (faster, but does not support CALL FORMAT)|No*&vert;Yes
|`fmsx_autospace`|Autofire the spacebar|No*&vert;Yes
|`fmsx_allsprites`|Show all sprites - do not emulate VDP hardware limitation|No*&vert;Yes
|`fmsx_font`|load a fixed text font from  RetroArch's `system_directory`|standard*&vert;DEFAULT.FNT&vert;ITALIC.FNT&vert;INTERNAT.FNT&vert;CYRILLIC.FNT&vert;KOREAN.FNT&vert;JAPANESE.FNT
|`fmsx_flush_disk`|Save changes to .dsk image|Never*&vert;Immediate&vert;On close
|`fmsx_phantom_disk`|Create empty disk image when none loaded|No*&vert;Yes
|`fmsx_custom_keyboard_XXX`<br>where XXX is `up`,`down`,`left`,`right`,`a`,`b`,`y`,`x`,`start`,`select`,`l`,`r`,`l2`,`r2`,`l3`,`r3`|For User 1 Device Type 'Custom Keyboard', map RetroPad button to selected MSX keyboard key|left&vert;up&vert;right&vert;down&vert;<br>shift&vert;ctrl&vert;graph&vert;<br>backspace&vert;tab&vert;escape&vert;space&vert;capslock&vert;select&vert;home&vert;enter&vert;del&vert;insert&vert;country&vert;dead&vert;stop&vert;<br>f1&vert;f2&vert;f3&vert;f4&vert;f5&vert;<br>keypad0~9&vert;kp_multiply&vert;kp_plus&vert;kp_divide&vert;kp_minus&vert;kp_comma&vert;kp_period&vert;<br>backquote&vert;minus&vert;equals&vert;leftbracket&vert;rightbracket&vert;backslash&vert;semicolon&vert;quote&vert;comma&vert;period&vert;slash&vert;<br>0-9&vert;a-z&vert;<br>
|`fmsx_log_level`|Configure the amount of fMSX logging|Off*&vert;Info&vert;Debug&vert;Spam


## PAL vs. NTSC
Selecting `fmsx_video_mode` 'PAL' or 'NTSC', as stated in the fMSX manual, will _"set PAL/NTSC HBlank/VBlank periods"_ at startup.
Also, the RetroArch framerate will be set to 50 resp. 60Hz. The RetroArch region is synchronized with the framerate.

However, those two settings do not take into account the internal VDP (Video Display Processor) behaviour related to 
the maximum number of scanlines and the line coincidence threshold. Also, some games may request to switch the mode.

To synchronize that, select 'Dynamic'. Models MSX1 and MSX2+ will then start up at 60Hz and adapt when a game switches to 50Hz.
MSX2 by default starts up at 50Hz and likewise will switch to 60Hz.

In 'Dynamic' mode, press F8 to toggle between 50/PAL and 60/NTSC.

When switching modes, a notification will be shown.

The displayed number of scanlines (192 or 212) remains the same for PAL or NTSC. 

## BIOS
BIOS ROMs are loaded from RetroArch's `system_directory`. The screen will remain black if required ROMs are missing.

These BIOS ROMs are required for execution:
* MSX1: MSX.ROM
* MSX2: MSX2.ROM, MSX2EXT.ROM
* MSX2+: MSX2P.ROM, MSX2PEXT.ROM

Optional; loaded when found:
* DISK.ROM - required to be able to run .dsk and .m3u images. 
  A notification will be shown for 10s when loading such an image without a proper DISK.ROM in place.
* FMPAC.ROM
* KANJI.ROM
* MSXDOS2.ROM (MSX2/2+) - requires DISK.ROM too. When present, activates Disk BASIC 2.01.
* PAINTER.ROM (MSX2/2+) - press space during boot to start
* RS232.ROM - although serial COM I/O is removed in this core
* CMOS.ROM - not a real ROM; a dump of the RTC contents (Real Time Clock; 52 Bytes)
* [GMASTER2.ROM](https://www.generation-msx.nl/group/games-with-game-master-2-s-ram-support/25/), 
  [GMASTER.ROM](https://www.generation-msx.nl/group/games-with-game-master-support/26/) - Konami's Game Master 2 & 1 (only one ROM is loaded; GM2 attempted first)


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
* "Custom Keyboard": maps 16 RetroPad buttons to any of the 88 keys of the MSX keyboard. Configure this in the Options.
* "Keyboard": maps host keyboard to 88-key MSX keyboard. Only on (RetroArch) platforms with a real keyboard (Linux, Windows, etc). Don't forget to press Scroll Lock to enter Game Focus Mode!
  * MSX1 & 2: US/European keyboard map, cursors, numeric pad, F1-F5
  * MSX2+: Japanese JIS keyboard map - see below
  * with these special keys:

|Host             | MSX
|---|---
|ctrl             |CONTROL
|shift            |SHIFT
|left alt         |GRAPH
|ins              |INSERT
|del              |DELETE
|home             |HOME/CLS
|end              |SELECT
|pause            |STOP/BREAK
|pagedown         |CODE/COUNTRY
|pageup           |International: DEAD-key; accents `, ´, ^ and ¨<br/>JIS: _ (underscore) and ろ
|numpad enter     |numpad comma

User 2:
* "Joystick": map RetroPad to MSX joystick B

### MSX1/2 US/European keyboard map
Enter accented characters (like &eacute;) by holding CODE/COUNTRY (page down) together with a key or by preceding a key with DEAD (page up).
Enter graphical symbols by holding GRAPH (left alt) together with a key.

There is [more information about the MSX keymap](http://map.grauw.nl/articles/keymatrix.php).

### MSX2+ Japanese keyboard map
This is a typical MSX2+ with Japanese (JIS) keyboard layout:
![Japanese keyboard](Japanese-MSX2+-keyboard.jpg)

How to use this:
* normal: bottomleft Roman letter/symbol
* shift: topleft symbol
* left alt ('GRAPH'): topright symbol (without the box)
* KANA LOCK active: bottomright Japanese character
* KANA LOCK active with shift: middleright Japanese character

To (de)activate KANA LOCK, press page down (COUNTRY). It works just like caps lock: press and release to enable.

Best enable SCREEN 1 to appreciate the full 8px width of the Japanese characters; in screen 0 characters are only 6px wide.


## Logging
When running into problems running a game, you can use logging to collect information.

Setting the `fmsx_log_level` has the following effects:
* **Off** => (the default) no fMSX-specific logging; only RetroArch/libretro-related messages
* **Info** => fMSX (startup) information logged
* **Debug** => fMSX debug details logged
* **Spam** => same, plus a lot of 'unknown I/O PORT' messages

Note: set the RetroArch log level to **Info** or **Debug** to be able to see these fMSX log messages!

See https://docs.libretro.com/guides/generating-retroarch-logs/ how to do that.

Or do it manually: edit `~/.config/retroarch/retroarch.cfg`

    log_verbosity = "true"    # needs to be true to see logs from a libretro core. Will also enable a lot of RetroArch logging.
    libretro_log_level = "1"  # 0=debug, 1=info, 2=warn, 3=error

For the curious, here are the bitflags fMSX uses internally for various categories:
* Info:  `Verbose!=0` => fMSX (startup) info
* Debug: `Verbose&0x02` => VDP
* Debug: `Verbose&0x04` => disk & tape incl. FDIDisk.Verbose & WD1793.Verbose
* Debug: `Verbose&0x08` => RAM/ROM mapper
* (none) `Verbose&0x10` => n/a
* Spam:  `Verbose&0x20` => I/O 


## Limitations
Not supported:
* Turbo-R (fMSX does not implement that platform)
* Drive B
* Cartridge slot 2
* Printer 
* RS-232 serial COM
* Mouse
* FM-PAC drums
* FM-PAC instruments are replaced by triangle waves
* .. and anything that requires accurate timing, like the MSX2+ boot screen.


## Technical details
### Audio/Video
Video: 16bpp RGB565 (PSP: BGR565, PS2: BGR555) 272x228 (544x228 in 512px MSX2 screen modes). 
This includes an 8px border (16px horizontal in 512px modes).

MSX native screen resolutions are:
- horizontal: 240, 256 or 512 (textmode: 32, 40 or 80 columns)
- vertical: 192 or 212 (192-line mode adds 10px to vertical border top+bottom)

The MSX2 supports interlacing, which is used by some software. Combined with showing two alternating pages, this increases
the maximum vertical resolution to 424, at the expense of halving the effective FPS to 25 (PAL) or 30 (NTSC).
Setting `fmsx_hires` to "Interlaced" will approximate an interlaced screen. 
The approximation suffers from 'time-based aliasing', most prominent when this core's FPS is not equal to
that of the actual display. Setting `fmsx_hires` to "Progressive" is less authentic, but more pleasing to the eye.

Some games and demos create an overscan effect. On a real MSX, this can go up to 256 vertical lines (PAL) or 243 (NTSC).
A part of the overscan is then actually displayed _on top_ of the screen, replacing the top border.
This core supports limited overscan, when enabled (`fmsx_overscan`=Yes). The lines beyond 212 will be displayed at the 
bottom. No attempt is made to display them on top. Most software that does overscan will apply custom blanking, 
e.g., at line 224. 
Overscanned lines beyond those are not shown. The bottom border is not drawn when overscan is active.
A limitation: in text mode overscan, incorrect text characters are shown.

In theory, [an MSX2 can show 512x512 pixels](https://www.msx.org/forum/development/msx-development/here-you-can-see-all-msx2-vram-your-screen) 
by combining interlacing with overscan in PAL mode.

In hires mode, the vertical output resolution will be doubled. Combined with overscan, this can result into maximum 
528 vertical lines. RetroArch will automatically scale this to retain aspect ratio and window size.

Audio: rendered in 48kHz 16b signed mono.
fMSX emulates PSG, SCC and FM-PAC.

Framerate: NTSC (US/JP) implies 60Hz - thus 60FPS, PAL (EU) implies 50Hz (=50FPS). 
Gameplay and audio actually becomes 17% slower when switching from NTSC to PAL - just like on a real MSX.

### Memory layout
Unlike BlueMSX and openMSX, fMSX does not implement any or all specific models sold historically.
The memory and [slot](https://www.msx.org/wiki/Slots) layout of this 'derivative' MSX model differs for model MSX1 vs. MSX2/2+.

#### MSX1
|primary slot| |0|1|2|3| | | |Address range
|---|---|---|---|---|---|---|---|---|---
|subslot| |n/a     |n/a                                  |n/a |3-0    |3-1        |3-2         |3-3     |
|page|3|           |ROM content or empty                 |same|       |           |RAM mapper ^|        |0xC000-0xFFFF
|page|2|           |ROM content, Game Master 2 or empty  |same|FMPAC #|           |RAM mapper ^|        |0x8000-0xBFFF
|page|1|BASIC ^    |ROM content, Game Master 1/2 or empty|same|FMPAC #|disk ROM *#|RAM mapper  |RS-232 #|0x4000-0x7FFF
|page|0|BIOS ^     |ROM content or empty                 |same|       |           |RAM mapper  |        |0x0000-0x3FFF

Legend:
- \^: active page at startup
- \*: page selected in slot at startup
- \#: optional; ROM loaded if present

Regarding game ROM contents:
- In most cases, the game is mapped to slot 1 (external slot A). Slot 2 (B) is unused.
- [Game Master](https://www.generation-msx.nl/software/konami/game-master/470/), when present, is always mapped to slot 1.
- [Game Master 2](https://www.msx.org/wiki/Konami_Game_Master_2) (GM2), when present, is mapped to slot 1 except for Contra & Hai no Majutsushi.
For those 2 games, GMASTER2.ROM is mapped to slot 2. They only use GM2 for SRAM saves.
- The game (ROM content) is then mapped to the _other_ external slot, i.e., slot 2 or 1. 

#### MSX2/2+
For these two models, slot 0 is also expanded, and more optional roms are loaded when present.

|primary slot|   |0      |   |   |   |1                                    |2   |3             |               |            |        |Address range
|---         |---|---    |---|---|---|---                                  |--- |---           |---            |---         |---     |---
|subslot     |   |0-0    |0-1|0-2|0-3|n/a                                  |n/a |3-0           |3-1            |3-2         |3-3     |
|page        |3  |       |b  |   |   |ROM content or empty                 |same|a             |               |RAM mapper ^|        |0xC000-0xFFFF
|page        |2  |       |b  |c  |   |ROM content, Game Master 2 or empty  |same|a / MSXDOS2 *#|               |RAM mapper ^|        |0x8000-0xBFFF
|page        |1  |BASIC ^|b  |c  |   |ROM content, Game Master 1/2 or empty|same|a / MSXDOS2 *#|disk ROM *#    |RAM mapper  |RS-232 #|0x4000-0x7FFF
|page        |0  |BIOS ^ |b  |   |   |ROM content or empty                 |same|a             |extended BIOS *|RAM mapper  |        |0x0000-0x3FFF

Legend:
- \^: active page at startup
- \*: page selected in slot at startup
- \#: optional; ROM loaded if present
- a-c: fMSX loads some optional ROMs, if present. The ROMs are mapped in the order indicated by letters a~c.

If all three optional 'dynamically mapped' ROMs are loaded, they are mapped to the following subslots:
- \[3-0] MSXDOS2 (64k, mapped to p1-2)
- \[0-1] PAINTER.ROM (64k, all 4 pages)
- \[0-2] FMPAC.ROM (64k, mapped to p1-2)

Regarding game ROM contents, the same applies as described for MSX1 above.

The MSXDOS2 ROM (always in 3-0), when loaded, extends the DISK.ROM (in 3-1).

The RS232 ROM, when present, is always mapped to 3-3.


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

Colours are rendered as RGB565 (16 bit). Due to rounding down in conversion from RGB888 32 bit, the colours mentioned above
can lose some detail. E.g., `#2020E0` (blue) is converted for RetroArch to RGB565 (3,7,27) which, when shown on a 32b display, 
actually displays as `#181cd8`.


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
    * note: Save1793() is never invoked! The WD1793 FDC is supposed to be 'at rest' when saving state.
      This means: don't save state during disk writes. All other hard- & software state is fully captured.
      fMSX has the same behaviour.
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
