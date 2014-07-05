TARGET_NAME := fMSX

DEBUG     = 0
PATCH_Z80 = 0

ifneq ($(EMSCRIPTEN),)
   platform = emscripten
endif

ifeq ($(platform),)
platform = unix
ifeq ($(shell uname -a),)
   platform = win
else ifneq ($(findstring MINGW,$(shell uname -a)),)
   platform = win
else ifneq ($(findstring Darwin,$(shell uname -a)),)
   platform = osx
else ifneq ($(findstring win,$(shell uname -a)),)
   platform = win
endif
endif

# system platform
system_platform = unix
ifeq ($(shell uname -a),)
EXE_EXT = .exe
   system_platform = win
else ifneq ($(findstring Darwin,$(shell uname -a)),)
   system_platform = osx
else ifneq ($(findstring MINGW,$(shell uname -a)),)
   system_platform = win
endif

CC_AS ?= $(CC)

ifneq (,$(findstring unix,$(platform)))
   TARGET := $(TARGET_NAME)_libretro.so
   fpic := -fPIC
   SHARED := -shared -Wl,-version-script=link.T -Wl,-no-undefined
else ifneq (,$(findstring rpi,$(platform)))
   TARGET := $(TARGET_NAME)_libretro.so
   LDFLAGS += -shared -Wl,--version-script=libretro/link.T
   fpic = -fPIC
   SHARED := -shared -Wl,-version-script=link.T -Wl,-no-undefined
else ifeq ($(platform), osx)
   TARGET := $(TARGET_NAME)_libretro.dylib
   fpic := -fPIC
   SHARED := -dynamiclib
OSXVER = `sw_vers -productVersion | cut -c 4`
ifneq ($(OSXVER),9)
   fpic += -mmacosx-version-min=10.5
endif
else ifeq ($(platform), ios)
   TARGET := $(TARGET_NAME)_libretro_ios.dylib
   fpic := -fPIC
   SHARED := -dynamiclib
   CC = clang -arch armv7 -isysroot $(IOSSDK)
   CC_AS = perl ./tools/gas-preprocessor.pl $(CC)
   CXX = clang++ -arch armv7 -isysroot $(IOSSDK)
OSXVER = `sw_vers -productVersion | cut -c 4`
ifneq ($(OSXVER),9)
   CC += -miphoneos-version-min=5.0
   CXX += -miphoneos-version-min=5.0
   CC_AS += -miphoneos-version-min=5.0
   PLATFORM_DEFINES := -miphoneos-version-min=5.0
endif
else ifeq ($(platform), qnx)
   TARGET := $(TARGET_NAME)_libretro_qnx.so
   fpic := -fPIC
   SHARED := -lcpp -lm -shared -Wl,-version-script=link.T
	CC = qcc -Vgcc_ntoarmv7le
   CC_AS = qcc -Vgcc_ntoarmv7le
	CXX = QCC -Vgcc_ntoarmv7le_cpp
	AR = QCC -Vgcc_ntoarmv7le
   PLATFORM_DEFINES := -D__BLACKBERRY_QNX__ -fexceptions -marm -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=softfp
else ifeq ($(platform), ps3)
   TARGET := $(TARGET_NAME)_libretro_ps3.a
   CC = $(CELL_SDK)/host-win32/ppu/bin/ppu-lv2-gcc.exe
   CC_AS = $(CELL_SDK)/host-win32/ppu/bin/ppu-lv2-gcc.exe
   CXX = $(CELL_SDK)/host-win32/ppu/bin/ppu-lv2-g++.exe
   AR = $(CELL_SDK)/host-win32/ppu/bin/ppu-lv2-ar.exe
   PLATFORM_DEFINES := -D__CELLOS_LV2__
    STATIC_LINKING = 1
else ifeq ($(platform), sncps3)
   TARGET := $(TARGET_NAME)_libretro_ps3.a
   CC = $(CELL_SDK)/host-win32/sn/bin/ps3ppusnc.exe
   CC_AS = $(CELL_SDK)/host-win32/sn/bin/ps3ppusnc.exe
   CXX = $(CELL_SDK)/host-win32/sn/bin/ps3ppusnc.exe
   AR = $(CELL_SDK)/host-win32/sn/bin/ps3snarl.exe
   PLATFORM_DEFINES := -D__CELLOS_LV2__
    STATIC_LINKING = 1
else ifeq ($(platform), psl1ght)
   TARGET := $(TARGET_NAME)_libretro_psl1ght.a
   CC = $(PS3DEV)/ppu/bin/ppu-gcc$(EXE_EXT)
   CC_AS = $(PS3DEV)/ppu/bin/ppu-gcc$(EXE_EXT)
   CXX = $(PS3DEV)/ppu/bin/ppu-g++$(EXE_EXT)
   AR = $(PS3DEV)/ppu/bin/ppu-ar$(EXE_EXT)
   PLATFORM_DEFINES := -D__CELLOS_LV2__
    STATIC_LINKING = 1
else ifeq ($(platform), psp1)
   TARGET := $(TARGET_NAME)_libretro_psp1.a
   CC = psp-gcc$(EXE_EXT)
   CC_AS = psp-gcc$(EXE_EXT)
   CXX = psp-g++$(EXE_EXT)
   AR = psp-ar$(EXE_EXT)
   PLATFORM_DEFINES := -DPSP
   CFLAGS += -G0
   CXXFLAGS += -G0
   STATIC_LINKING = 1
   EXTRA_INCLUDES := -I$(shell psp-config --pspsdk-path)/include
else ifeq ($(platform), xenon)
   TARGET := $(TARGET_NAME)_libretro_xenon360.a
   CC = xenon-gcc$(EXE_EXT)
   CC_AS = xenon-gcc$(EXE_EXT)
   CXX = xenon-g++$(EXE_EXT)
   AR = xenon-ar$(EXE_EXT)
   PLATFORM_DEFINES := -D__LIBXENON__
    STATIC_LINKING = 1
else ifeq ($(platform), ngc)
   TARGET := $(TARGET_NAME)_libretro_ngc.a
   CC = $(DEVKITPPC)/bin/powerpc-eabi-gcc$(EXE_EXT)
   CC_AS = $(DEVKITPPC)/bin/powerpc-eabi-gcc$(EXE_EXT)
   CXX = $(DEVKITPPC)/bin/powerpc-eabi-g++$(EXE_EXT)
   AR = $(DEVKITPPC)/bin/powerpc-eabi-ar$(EXE_EXT)
   PLATFORM_DEFINES += -DGEKKO -DHW_DOL -mrvl -mcpu=750 -meabi -mhard-float
    STATIC_LINKING = 1
else ifeq ($(platform), wii)
   TARGET := $(TARGET_NAME)_libretro_wii.a
   CC = $(DEVKITPPC)/bin/powerpc-eabi-gcc$(EXE_EXT)
   CC_AS = $(DEVKITPPC)/bin/powerpc-eabi-gcc$(EXE_EXT)
   CXX = $(DEVKITPPC)/bin/powerpc-eabi-g++$(EXE_EXT)
   AR = $(DEVKITPPC)/bin/powerpc-eabi-ar$(EXE_EXT)
   PLATFORM_DEFINES += -DGEKKO -DHW_RVL -mrvl -mcpu=750 -meabi -mhard-float
    STATIC_LINKING = 1
else ifneq (,$(findstring armv,$(platform)))
   TARGET := $(TARGET_NAME)_libretro.so
   fpic := -fPIC
   SHARED := -shared -Wl,-version-script=link.T
   CC = gcc
   CC_AS = gcc
   CXX = g++
ifneq (,$(findstring cortexa8,$(platform)))
   PLATFORM_DEFINES += -marm -mcpu=cortex-a8
else ifneq (,$(findstring cortexa9,$(platform)))
   PLATFORM_DEFINES += -marm -mcpu=cortex-a9
endif
   PLATFORM_DEFINES += -marm
ifneq (,$(findstring neon,$(platform)))
   PLATFORM_DEFINES += -mfpu=neon
   HAVE_NEON = 1
endif
ifneq (,$(findstring softfloat,$(platform)))
   PLATFORM_DEFINES += -mfloat-abi=softfp
else ifneq (,$(findstring hardfloat,$(platform)))
   PLATFORM_DEFINES += -mfloat-abi=hard
endif
   PLATFORM_DEFINES += -DARM
else ifeq ($(platform), emscripten)
   TARGET := $(TARGET_NAME)_libretro_emscripten.bc
else
   TARGET := $(TARGET_NAME)_libretro.dll
   CC = gcc
   CC_AS = gcc
   CXX = g++
   SHARED := -shared -static-libgcc -static-libstdc++ -Wl,-no-undefined -Wl,-version-script=link.T
endif

ifeq ($(DEBUG), 1)
	CFLAGS += -O0 -g
	CXXFLAGS += -O0 -g
else ifeq ($(platform), emscripten)
	CFLAGS += -O2
	CXXFLAGS += -O2 -fno-exceptions -fno-rtti -DHAVE_STDINT_H
else
	CFLAGS += -O3
	CXXFLAGS += -O3 -fno-exceptions -fno-rtti -DHAVE_STDINT_H
endif


ifeq ($(PATCH_Z80), 1)
DEFINES += -DPATCH_Z80
OBJECTS += $(FMSXDIR)/Patch.o
endif

DEFINES += -D__LIBRETRO__ $(PLATFORM_DEFINES)

DEFINES += -DUNIX -DMITSHM -DZLIB -DFMSX -DLSB_FIRST -DBPS16 -DBPP16
#DEFINES += -DNO_WAVE_INTERPOLATION

CFLAGS   += $(fpic) $(DEFINES)
CFLAGS   += -w
#CFLAGS   += -Wall

CXXFLAGS += $(fpic) $(DEFINES)
CXXFLAGS   += -w
#CXXFLAGS   += -Wall

BASEDIR	= ./
EMULIB	= $(BASEDIR)/EMULib
FMSXDIR = $(BASEDIR)/fMSX
LIBZ80	= $(BASEDIR)/Z80


OBJECTS += libretro.o

# EMULib sound, console, and other utility functions
OBJECTS	+= $(EMULIB)/EMULib.o $(EMULIB)/Sound.o
OBJECTS += $(FMSXDIR)/MSX.o $(FMSXDIR)/V9938.o $(FMSXDIR)/I8251.o

OBJECTS += $(EMULIB)/SHA1.o $(EMULIB)/Floppy.o $(EMULIB)/FDIDisk.o
OBJECTS += $(LIBZ80)/Z80.o
OBJECTS += $(EMULIB)/I8255.o $(EMULIB)/YM2413.o $(EMULIB)/AY8910.o $(EMULIB)/SCC.o $(EMULIB)/WD1793.o


INCDIRS := -I. -I$(EMULIB) -I$(EMULIB)/Unix -I$(LIBZ80) -I$(FMSXDIR)
INCDIRS += $(EXTRA_INCLUDES)
CFLAGS +=

LIBS := -lz


all: $(TARGET)

$(TARGET): $(OBJECTS)
ifeq ($(STATIC_LINKING), 1)
	$(AR) rcs $@ $(OBJECTS)
else
	$(CXX) -o $@ $(SHARED) $(OBJECTS) $(LDFLAGS) $(LIBS)
endif

%.o: %.cpp
	$(CXX) -c -o $@ $< $(CXXFLAGS) $(INCDIRS)

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS) $(INCDIRS)

%.o: %.S
	$(CC_AS) -c -o $@ $< $(CFLAGS) $(INCDIRS)

clean-objects:
	rm -f $(OBJECTS)

clean:
	rm -f $(OBJECTS)
	rm -f $(TARGET)

.PHONY: $(TARGET) clean clean-objects

