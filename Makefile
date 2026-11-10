# Lightfoot build. `make sim` builds the Mac simulator, `make device` the static
# aarch64 binary, `make package` the SD autorun zip. zig is the cross-compiler.

HOSTCXX  ?= c++
HOSTCC   ?= cc
ZIG      ?= zig
TARGET   ?= aarch64-linux-musl
PYTHON   ?= python3
PRODUCT  := Lightfoot

TK       := deps/mpc-ui-toolkit/src
TA       := deps/tinyalsa
INC      := -I Sources -I $(TK) -I deps
CXXSTD   := -std=c++17
WARN     := -Wall -Wextra -Wno-unused-parameter
CDEFS    := -D_DEFAULT_SOURCE

HOST_CXXFLAGS := $(CXXSTD) $(WARN) -g -O1 $(INC) -DLIGHTFOOT_HOST=1
HOST_CFLAGS   := -std=c11 $(WARN) -g -O1 $(CDEFS) -I $(TK)
SDL_CFLAGS    := $(shell sdl2-config --cflags 2>/dev/null)
SDL_LIBS      := $(shell sdl2-config --libs 2>/dev/null)

DEV_CXX      := $(ZIG) c++ -target $(TARGET)
DEV_CC       := $(ZIG) cc -target $(TARGET)
DEV_CXXFLAGS := $(CXXSTD) $(WARN) -O2 $(INC) -I $(TA)/include -DLIGHTFOOT_DEVICE=1 -fno-exceptions
DEV_CFLAGS   := -std=c11 $(WARN) -O2 $(CDEFS) -I $(TK)

BUILD := build
VERSION := $(shell git describe --tags --always --dirty 2>/dev/null || echo dev)
HOST_CXXFLAGS += -DLIGHTFOOT_VERSION='"$(VERSION)"'
DEV_CXXFLAGS  += -DLIGHTFOOT_VERSION='"$(VERSION)"'

APP_SRC  := $(shell find Sources/Game Sources/Audio Sources/Shared -name '*.cpp' 2>/dev/null)
HOST_SRC := $(shell find Sources/Platform/Host -name '*.cpp' 2>/dev/null)
DEV_SRC  := $(shell find Sources/Platform/Device -name '*.cpp' 2>/dev/null)
TEST_SRC := $(shell find Tests -name '*.cpp' 2>/dev/null)

TK_COMMON := ui.c widgets.c input.c filebrowser.c
TK_DEVICE := app.c screen.c surface.c
TA_SRC    := pcm.c pcm_hw.c

HOST_APP_OBJ := $(patsubst %.cpp,$(BUILD)/host/%.o,$(APP_SRC))
HOST_SIM_OBJ := $(patsubst %.cpp,$(BUILD)/host/%.o,$(HOST_SRC))
HOST_TK_OBJ  := $(patsubst %.c,$(BUILD)/host/tk/%.o,$(TK_COMMON))
HOST_TEST_OBJ:= $(patsubst %.cpp,$(BUILD)/host/%.o,$(TEST_SRC))

DEV_APP_OBJ  := $(patsubst %.cpp,$(BUILD)/device/%.o,$(APP_SRC))
DEV_MAIN_OBJ := $(patsubst %.cpp,$(BUILD)/device/%.o,$(DEV_SRC))
DEV_TK_OBJ   := $(patsubst %.c,$(BUILD)/device/tk/%.o,$(TK_COMMON) $(TK_DEVICE))
DEV_TA_OBJ   := $(patsubst %.c,$(BUILD)/device/tinyalsa/%.o,$(TA_SRC))

SIM     := $(BUILD)/sim/lightfoot-sim
DEVICE  := $(BUILD)/device/lightfoot
TESTBIN := $(BUILD)/test/lightfoot-test

.PHONY: all sim device package shots test clean manual hooks screens run

all: sim device

sim: $(SIM)
device: $(DEVICE)
run: $(SIM)
	$(SIM)

$(SIM): $(HOST_APP_OBJ) $(HOST_SIM_OBJ) $(HOST_TK_OBJ)
	@mkdir -p $(dir $@)
	$(HOSTCXX) $(HOST_CXXFLAGS) -o $@ $^ $(SDL_LIBS)

$(DEVICE): $(DEV_APP_OBJ) $(DEV_MAIN_OBJ) $(DEV_TK_OBJ) $(DEV_TA_OBJ)
	@mkdir -p $(dir $@)
	$(DEV_CXX) -static -O2 -s -o $@ $^ -lpthread

$(BUILD)/host/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(HOSTCXX) $(HOST_CXXFLAGS) $(SDL_CFLAGS) -MMD -MP -c -o $@ $<

$(BUILD)/host/tk/%.o: $(TK)/%.c
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOST_CFLAGS) -MMD -MP -c -o $@ $<

$(BUILD)/device/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(DEV_CXX) $(DEV_CXXFLAGS) -MMD -MP -c -o $@ $<

$(BUILD)/device/tk/%.o: $(TK)/%.c
	@mkdir -p $(dir $@)
	$(DEV_CC) $(DEV_CFLAGS) -MMD -MP -c -o $@ $<

$(BUILD)/device/tinyalsa/%.o: $(TA)/src/%.c
	@mkdir -p $(dir $@)
	$(DEV_CC) -std=gnu11 -O2 -w -I $(TA)/include -I $(TA)/src -MMD -MP -c -o $@ $<

# Headless screenshots of every screen state, for review and the manual.
shots: $(SIM)
	@mkdir -p $(BUILD)/shots
	$(SIM) --shots $(BUILD)/shots

$(TESTBIN): $(HOST_APP_OBJ) $(HOST_TEST_OBJ) $(HOST_TK_OBJ) $(filter-out $(BUILD)/host/Sources/Platform/Host/SimMain.o,$(HOST_SIM_OBJ))
	@mkdir -p $(dir $@)
	$(HOSTCXX) $(HOST_CXXFLAGS) -o $@ $^ $(SDL_LIBS)

test: $(TESTBIN) shots
	$(TESTBIN)

# The manual: MANUAL.md is the source, the PDF is generated and ships on the card.
MANUAL_PDF := $(BUILD)/manual/$(PRODUCT)-Manual.pdf
SCREENS := 00-title 01-how-to 02-playing 03-perfect 05-paused 06-game-over 07-scores 08-songs 08b-song-info 09-song-run

screens: shots
	@mkdir -p docs/screens
	@for n in $(SCREENS); do cp $(BUILD)/shots/$$n.png docs/screens/$$n.png; done

$(MANUAL_PDF): MANUAL.md docs/manual.json tools/build-manual.py screens
	$(PYTHON) tools/check-manual.py MANUAL.md
	$(PYTHON) tools/build-manual.py MANUAL.md $(BUILD)/manual $(VERSION)
	@test -f $(MANUAL_PDF) && cp $(MANUAL_PDF) $(PRODUCT)-Manual.pdf || true

manual: $(MANUAL_PDF)

hooks:
	git config core.hooksPath tools/hooks
	@echo "hooks installed: tools/hooks"

package: $(DEVICE) manual
	tools/package.sh $(DEVICE) $(BUILD)/pkg $(wildcard $(MANUAL_PDF))

clean:
	rm -rf $(BUILD)

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
