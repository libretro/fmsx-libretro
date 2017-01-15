LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

GIT_VERSION := " $(shell git rev-parse --short HEAD || echo unknown)"
ifneq ($(GIT_VERSION)," unknown")
	LOCAL_CFLAGS += -DGIT_VERSION=\"$(GIT_VERSION)\"
endif

CORE_DIR     := ../..
LIBRETRO_DIR := ..

LOCAL_MODULE    := retro

ifeq ($(TARGET_ARCH),arm)
LOCAL_CFLAGS += -DANDROID_ARM
LOCAL_ARM_MODE := arm
endif

ifeq ($(TARGET_ARCH),x86)
LOCAL_CFLAGS +=  -DANDROID_X86
endif

ifeq ($(TARGET_ARCH),mips)
LOCAL_CFLAGS += -DANDROID_MIPS -D__mips__ -D__MIPSEL__
endif

CORE_DIR	= ..
EMULIB	= $(CORE_DIR)/EMULib
FMSXDIR = $(CORE_DIR)/fMSX
LIBZ80	= $(CORE_DIR)/Z80

include $(CORE_DIR)/Makefile.common

LOCAL_SRC_FILES    += $(SOURCES_C)
LOCAL_CFLAGS += -O2 -std=gnu99 -ffast-math -D__LIBRETRO__ -DUNIX -DFMSX -DBPS16 -DBPP16 -DFRONTEND_SUPPORTS_RGB565 -DNDEBUG=1 $(INCFLAGS)

include $(BUILD_SHARED_LIBRARY)
