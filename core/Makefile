# Toolchain-agnostic build of TonicCore and its headless test with any C11 compiler.
#   make            -> build/libtoniccore.a, build/playcheck
#   make check      -> run playcheck against ../files/out/ttlv
#   make aicheck    -> run Ed's AI headless on bar (needs ../files/out/ttlv/bar.ttai)
#   make mechcheck  -> type 0/4 mechanics on hand-made geometry (no game data needed)
#   make crashcheck -> Totalski: the skier rides into a tree and crashes (needs ../files/out/ttlv/Totalski.*)
#   make viewcheck  -> the viewer module (tc_viewer) headless on bar: scripted keys, draw lists, title readout
#   make CC=arm-vita-eabi-gcc AR=arm-vita-eabi-ar lib   (cross-compile just the library)
CC ?= cc
AR ?= ar
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Iinclude
LDLIBS += -lm
BUILD := build
SRC := $(wildcard src/*.c)
OBJ := $(SRC:src/%.c=$(BUILD)/%.o)

all: lib $(BUILD)/playcheck $(BUILD)/aicheck $(BUILD)/mechcheck $(BUILD)/crashcheck $(BUILD)/viewcheck
lib: $(BUILD)/libtoniccore.a

$(BUILD)/%.o: src/%.c $(wildcard include/tonic/*.h) $(wildcard src/*.h) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/libtoniccore.a: $(OBJ)
	$(AR) rcs $@ $^

$(BUILD)/playcheck: tests/playcheck.c $(BUILD)/libtoniccore.a
	$(CC) $(CFLAGS) $< $(BUILD)/libtoniccore.a $(LDLIBS) -o $@

$(BUILD)/aicheck: tests/aicheck.c $(BUILD)/libtoniccore.a
	$(CC) $(CFLAGS) $< $(BUILD)/libtoniccore.a $(LDLIBS) -o $@

$(BUILD)/mechcheck: tests/mechcheck.c $(BUILD)/libtoniccore.a
	$(CC) $(CFLAGS) $< $(BUILD)/libtoniccore.a $(LDLIBS) -o $@

$(BUILD)/crashcheck: tests/crashcheck.c $(BUILD)/libtoniccore.a
	$(CC) $(CFLAGS) $< $(BUILD)/libtoniccore.a $(LDLIBS) -o $@

$(BUILD)/viewcheck: tests/viewcheck.c $(BUILD)/libtoniccore.a
	$(CC) $(CFLAGS) $< $(BUILD)/libtoniccore.a $(LDLIBS) -o $@

$(BUILD):
	mkdir -p $(BUILD)

check: $(BUILD)/playcheck
	$(BUILD)/playcheck ../files/out/ttlv

aicheck: $(BUILD)/aicheck
	$(BUILD)/aicheck ../files/out/ttlv bar

mechcheck: $(BUILD)/mechcheck
	$(BUILD)/mechcheck

crashcheck: $(BUILD)/crashcheck
	$(BUILD)/crashcheck ../files/out/ttlv

viewcheck: $(BUILD)/viewcheck
	$(BUILD)/viewcheck ../files/out/ttlv bar

clean:
	rm -rf $(BUILD)

.PHONY: all lib check aicheck mechcheck crashcheck viewcheck clean
