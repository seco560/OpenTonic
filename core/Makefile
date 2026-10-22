# Toolchain-agnostic build of TonicCore and its headless test with any C11 compiler.
#   make            -> build/libtoniccore.a, build/playcheck
#   make check      -> run playcheck against ../files/out/ttlv
#   make aicheck    -> run Ed's AI headless on bar (needs ../files/out/ttlv/bar.ttai)
#   make CC=arm-vita-eabi-gcc AR=arm-vita-eabi-ar lib   (cross-compile just the library)
CC ?= cc
AR ?= ar
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Iinclude
LDLIBS += -lm
BUILD := build
SRC := $(wildcard src/*.c)
OBJ := $(SRC:src/%.c=$(BUILD)/%.o)

all: lib $(BUILD)/playcheck $(BUILD)/aicheck
lib: $(BUILD)/libtoniccore.a

$(BUILD)/%.o: src/%.c $(wildcard include/tonic/*.h) $(wildcard src/*.h) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/libtoniccore.a: $(OBJ)
	$(AR) rcs $@ $^

$(BUILD)/playcheck: tests/playcheck.c $(BUILD)/libtoniccore.a
	$(CC) $(CFLAGS) $< $(BUILD)/libtoniccore.a $(LDLIBS) -o $@

$(BUILD)/aicheck: tests/aicheck.c $(BUILD)/libtoniccore.a
	$(CC) $(CFLAGS) $< $(BUILD)/libtoniccore.a $(LDLIBS) -o $@

$(BUILD):
	mkdir -p $(BUILD)

check: $(BUILD)/playcheck
	$(BUILD)/playcheck ../files/out/ttlv

aicheck: $(BUILD)/aicheck
	$(BUILD)/aicheck ../files/out/ttlv bar

clean:
	rm -rf $(BUILD)

.PHONY: all lib check aicheck clean
