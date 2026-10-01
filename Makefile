# Project Name
TARGET = grainfall

# Sources
CPP_SOURCES = src/main.cpp src/grain_engine.cpp src/midi_clock.cpp

# Library Locations
LIBDAISY_DIR = lib/libDaisy
DAISYSP_DIR = lib/DaisySP

# C++17 for the project code (libDaisy defaults to gnu++14)
CPP_STANDARD = -std=gnu++17

# Homebrew on Apple Silicon installs OpenOCD scripts under /opt/homebrew
OCD_DIR ?= $(shell brew --prefix 2>/dev/null || echo /usr/local)/share/openocd/scripts

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

# Build libDaisy and DaisySP (needed once after cloning, and after updating submodules)
.PHONY: libs
libs:
	$(MAKE) -C $(LIBDAISY_DIR)
	$(MAKE) -C $(DAISYSP_DIR)
