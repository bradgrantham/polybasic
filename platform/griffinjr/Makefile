# Griffin Jr. build: build/griffinjr/basic.elf, a program for the BIOS monitor.
#
#   make -C platform/griffinjr            build
#   make -C platform/griffinjr test       run the phase 1 tests in the emulator
#   make -C platform/griffinjr bench      time the loop benchmarks in the emulator
#
# Needs the Griffin Jr. tree (SDK, firmware and emulator, all built: `make all`
# there) and its cross toolchain.

JR      ?= $(HOME)/trees/other-m68k
XTOOLS  ?= $(HOME)/x-tools/m68k-unknown-elf/bin
CROSS    = $(XTOOLS)/m68k-unknown-elf-
CXX      = $(CROSS)g++
CC       = $(CROSS)gcc
SIZE     = $(CROSS)size
STRIP    = $(CROSS)strip

TOP      = ../..
B        = $(TOP)/build/griffinjr
SDK      = $(JR)/sdk

# -O2 rather than -Os: about 35 KB more code for 1.3 to 1.6 times the speed.
# -fno-ivopts because GCC 15.2 for the 68000 miscompiles format_number in
# core/number.cpp at -O2 with it (a digit after the decimal point comes out
# wrong); see STATUS.md.
OPT     ?= -O2 -fno-ivopts
CXXFLAGS = -m68000 -std=c++23 $(OPT) -Wall -Wextra -fno-exceptions -fno-rtti \
           -fno-threadsafe-statics -ffunction-sections -fdata-sections \
           -I$(JR)/generated -I$(SDK)
# The SDK's link line (sdk/README.md) with libm added, and through gcc as the SDK
# does: g++ would add libstdc++ and a second libc after the group.  The linker script
# includes ../generated/user.ld, found through -L.
LDFLAGS  = -m68000 -nostartfiles -T $(SDK)/user.ld -L$(JR)/sdk -Wl,--gc-sections \
           -Wl,--no-warn-rwx-segments -L$(SDK)/build -Wl,-Map,$(B)/basic.map
LDLIBS   = -Wl,--start-group -lm -lc -lsys -Wl,--end-group -lgcc

SOURCES  = core/exec.cpp core/program.cpp core/number.cpp \
           dialects/msfamily.cpp dialects/msbasic.cpp \
           platform/griffinjr/main.cpp
OBJECTS  = $(SOURCES:%.cpp=$(B)/%.o)
HEADERS  = $(TOP)/core/basic.h $(TOP)/core/ops.h $(TOP)/core/platform.h \
           $(TOP)/dialects/msfamily.h

all: $(B)/basic.elf

$(B)/%.o: $(TOP)/%.cpp $(HEADERS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# BASIC.ELF is what goes on a volume: stripped, as the loader reads only the
# program headers.
$(B)/basic.elf: $(OBJECTS) $(SDK)/build/crt0.o $(SDK)/build/libsys.a $(SDK)/user.ld
	$(CC) $(LDFLAGS) -o $@ $(SDK)/build/crt0.o $(OBJECTS) $(LDLIBS)
	$(STRIP) -o $(B)/BASIC.ELF $@
	$(SIZE) $@

test: $(B)/basic.elf
	$(TOP)/tests/run-griffinjr.sh

bench: $(B)/basic.elf
	$(TOP)/tests/run-griffinjr.sh --bench

clean:
	rm -rf $(B)

.PHONY: all test bench clean
