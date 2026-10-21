# Toolchain-agnostic build of TonicCore and its headless test with any C11 compiler.
#   make            -> build/libtoniccore.a, build/playcheck
#   make check      -> run playcheck against ../../out/ttlv
#   make CC=arm-vita-eabi-gcc AR=arm-vita-eabi-ar lib   (cross-compile just the library)
CC ?= cc
AR ?= ar
CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Iinclude
LDLIBS += -lm
BUILD := build
SRC := $(wildcard src/*.c)
OBJ := $(SRC:src/%.c=$(BUILD)/%.o)

all: lib $(BUILD)/playcheck
lib: $(BUILD)/libtoniccore.a

$(BUILD)/%.o: src/%.c $(wildcard include/tonic/*.h) src/tc_internal.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/libtoniccore.a: $(OBJ)
	$(AR) rcs $@ $^

$(BUILD)/playcheck: tests/playcheck.c $(BUILD)/libtoniccore.a
	$(CC) $(CFLAGS) $< $(BUILD)/libtoniccore.a $(LDLIBS) -o $@

$(BUILD):
	mkdir -p $(BUILD)

check: $(BUILD)/playcheck
	$(BUILD)/playcheck ../../out/ttlv

clean:
	rm -rf $(BUILD)

.PHONY: all lib check clean
