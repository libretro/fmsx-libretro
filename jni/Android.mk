LOCAL_PATH := $(call my-dir)

CORE_DIR := $(LOCAL_PATH)/..
EMULIB   := $(CORE_DIR)/EMULib
FMSXDIR  := $(CORE_DIR)/fMSX
LIBZ80   := $(CORE_DIR)/Z80
NUKEYKT  := $(CORE_DIR)/NukeYKT

include $(CORE_DIR)/Makefile.common

COREFLAGS := -std=gnu99 -ffast-math -D__LIBRETRO__ $(COREDEFINES) -DFRONTEND_SUPPORTS_RGB565 $(INCFLAGS)

GIT_VERSION := " $(shell git rev-parse --short HEAD || echo unknown)"
ifneq ($(GIT_VERSION)," unknown")
  COREFLAGS += -DGIT_VERSION=\"$(GIT_VERSION)\"
endif

include $(CLEAR_VARS)
LOCAL_MODULE    := retro
LOCAL_SRC_FILES := $(SOURCES_C)
LOCAL_CFLAGS    := $(COREFLAGS)
LOCAL_LDFLAGS   := -Wl,-version-script=$(CORE_DIR)/link.T
include $(BUILD_SHARED_LIBRARY)
