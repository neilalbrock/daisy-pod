# Daisy Pod firmware experiments
#
# Each app lives in apps/<name>/ and builds to build/<name>/<name>.{elf,bin,hex}.
# Pick one with APP=<name>, e.g. `make APP=starter program-dfu`.
APP ?= grainfall
APPS = $(patsubst apps/%/,%,$(sort $(dir $(wildcard apps/*/*.cpp))))

ifeq ($(filter list new all-apps clean-all web m4l,$(MAKECMDGOALS)),)
ifeq ($(filter $(APP),$(APPS)),)
$(error No app "$(APP)" in apps/. Available: $(APPS))
endif
endif

TARGET = $(APP)

# Every .cpp in the app folder, plus shared code in common/. Object files are
# named after the source file alone, so file names must be unique across both.
CPP_SOURCES = $(wildcard apps/$(APP)/*.cpp) $(wildcard common/*.cpp)
C_INCLUDES = -Icommon

# libDaisy assigns BUILD_DIR = build unconditionally, hence the override
override BUILD_DIR = build/$(APP)

# Library Locations
LIBDAISY_DIR = lib/libDaisy
DAISYSP_DIR = lib/DaisySP

# C++17 for the project code (libDaisy defaults to gnu++14)
CPP_STANDARD = -std=gnu++17

# Homebrew on Apple Silicon installs OpenOCD scripts under /opt/homebrew
OCD_DIR ?= $(shell brew --prefix 2>/dev/null || echo /usr/local)/share/openocd/scripts

# Per-app overrides, e.g. APP_TYPE = BOOT_NONE
-include apps/$(APP)/app.mk

# Every app runs from SRAM via the Daisy bootloader (flash it once with
# `make program-boot`), so none is held to the 128 KB internal flash and
# switching apps never overwrites the bootloader.
APP_TYPE ?= BOOT_SRAM

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

# libDaisy creates BUILD_DIR with a plain mkdir, so build/ must exist first
$(BUILD_DIR): | build
build:
	mkdir $@

# Build before flashing. libDaisy's program and debug recipes read
# ./build/$(TARGET).elf, so point that at the app's ELF.
program-dfu: $(BUILD_DIR)/$(TARGET).bin
program debug: build/$(APP).elf
build/$(APP).elf: $(BUILD_DIR)/$(TARGET).elf
	ln -sf $(APP)/$(APP).elf $@

.PHONY: list new all-apps clean-all libs web m4l

# List the apps; * marks the one APP selects
list:
	@$(foreach a,$(APPS),echo "$(if $(filter $(a),$(APP)),*, ) $(a)";)

# Start a new app from a copy of the starter: make new APP=<name>
new:
	@test "$(origin APP)" = "command line" || { echo "Usage: make new APP=<name>"; exit 1; }
	@test ! -e apps/$(APP) || { echo "apps/$(APP) already exists"; exit 1; }
	cp -R apps/starter apps/$(APP)

# Build every app, to catch breakage from changes in common/
all-apps:
	@for a in $(APPS); do $(MAKE) --no-print-directory APP=$$a all || exit 1; done

clean-all:
	-rm -fR build

# Build libDaisy and DaisySP (needed once after cloning, and after updating submodules)
libs:
	$(MAKE) -C $(LIBDAISY_DIR)
	$(MAKE) -C $(DAISYSP_DIR)

# Serve the web control surface (web/) on localhost, where Web MIDI is allowed
WEB_PORT ?= 8000
web:
	@python3 web/serve.py $(WEB_PORT)

# Max for Live devices (m4l/): one per app with a web view, from its
# parameter list, which a host build of the app's web_view.cpp prints
M4L_APPS = $(patsubst apps/%/web_view.cpp,%,$(wildcard apps/*/web_view.cpp))
HOST_CXX ?= c++
m4l: $(M4L_APPS:%=build/m4l/dump_%) m4l/node_modules
	node m4l/tools/build.mjs $(foreach a,$(M4L_APPS),$(a)=build/m4l/dump_$(a))

.SECONDEXPANSION:
build/m4l/dump_%: m4l/tools/dump_params.cpp $$(wildcard apps/$$*/*.cpp apps/$$*/*.h) $(wildcard common/*.cpp common/*.h)
	@mkdir -p build/m4l
	$(HOST_CXX) -std=gnu++17 -O1 -w -fconstexpr-steps=100000000 -DAPP_$(shell echo $* | tr a-z A-Z) \
		-Icommon -Iapps/$* $(filter-out %/main.cpp,$(wildcard apps/$*/*.cpp)) $(wildcard common/*.cpp) $< -o $@

m4l/node_modules: m4l/package.json
	cd m4l && npm install
	@touch $@
