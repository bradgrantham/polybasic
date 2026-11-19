# Host (POSIX, optionally SDL3) build.  The 68000 builds have their own makefiles under
# platform/; `make griffinjr` and `make test-griffinjr` call the Griffin Jr. one.

CXX      ?= c++
CXXFLAGS ?= -std=c++23 -O2 -g -Wall -Wextra -fno-exceptions -fno-rtti
BUILD     = build

CORE     = core/exec.cpp core/program.cpp core/number.cpp
DIALECTS = dialects/msfamily.cpp dialects/msbasic.cpp dialects/applesoft.cpp
MACHINES = machines/apple2/apple2.cpp machines/apple2/graphics.cpp machines/apple2/video.cpp \
           machines/common/png.cpp

# The graphics window needs SDL3.  Without it (or with SDL3=no) the build
# still does graphics, into --png files only.
SDL3 ?= $(shell pkg-config --exists sdl3 && echo yes)
ifeq ($(SDL3),yes)
DISPLAY  = platform/posix/display_sdl.cpp
SDL_CFLAGS := $(shell pkg-config --cflags sdl3)
SDL_LIBS   := $(shell pkg-config --libs sdl3)
else
DISPLAY  = platform/posix/display_none.cpp
endif
PLATFORM = platform/posix/main.cpp $(DISPLAY)
SOURCES  = $(CORE) $(DIALECTS) $(MACHINES) $(PLATFORM)
OBJECTS  = $(SOURCES:%.cpp=$(BUILD)/%.o)
HEADERS  = core/basic.h core/ops.h core/platform.h dialects/msfamily.h \
           machines/apple2/apple2.h machines/apple2/a2internal.h machines/apple2/font.h \
           machines/common/png.h

all: $(BUILD)/basic

$(BUILD)/basic: $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJECTS) $(SDL_LIBS)

$(BUILD)/%.o: %.cpp $(HEADERS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) -c -o $@ $<

test: $(BUILD)/basic
	tests/run.sh $(BUILD)/basic

# Parse every recovered Applesoft program (see tools/corpus.py).
CORPUS ?= ../../apple2-disks-unpacked
corpus: $(BUILD)/basic
	python3 tools/corpus.py $(CORPUS) --basic $(BUILD)/basic

# The Griffin Jr. build and its tests in the emulator (see platform/griffinjr/Makefile).
griffinjr:
	$(MAKE) -C platform/griffinjr
test-griffinjr:
	$(MAKE) -C platform/griffinjr test

clean:
	rm -rf $(BUILD)

.PHONY: all test corpus griffinjr test-griffinjr clean
