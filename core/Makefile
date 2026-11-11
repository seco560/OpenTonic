# Toolchain-agnostic build of TonicCore and its headless test with any C11 compiler.
#   make            -> build/libtoniccore.a, build/playcheck
#   make check      -> run playcheck against ../files/out/ttlv
#   make aicheck    -> run Ed's AI headless on bar (needs ../files/out/ttlv/bar.ttai)
#   make mechcheck  -> type 0/4 mechanics on hand-made geometry (no game data needed)
#   make crashcheck -> Totalski: the skier rides into a tree and crashes (needs ../files/out/ttlv/Totalski.*)
#   make skicheck   -> Totalski, skymap: random rides; how many stop against walls (needs their .ttlv / .ttai)
#   make viewcheck  -> the viewer module (tc_viewer) headless on bar: scripted keys, draw lists, title readout
#   make portalcheck -> Ed walks into portals on sud0, Land and north (docs/PORTALS.md)
#   make sectorcheck -> every level's sectors against the game's own data (tc_sector.h)
#   make camcheck   -> the play camera (tc_cam.h) on hand-made geometry, then a walk's line of sight on a few levels
#   make castcheck  -> the level persos' own AI (tc_cast.h) on sud0, north, Land: awake, skipped primitives, pickups
#   make movecheck  -> what each level perso does with Ed near it: rows to diff between builds (docs/ENGINE.md "Perso motion")
#   make replaycheck -> sessions recorded and replayed headless, then any keepers in ../files/recordings (docs/RECORDING.md)
#   make debugcheck -> the debug menu (tc_debug.h) on sud0: its moves, a power switched off, reload, a replay
#   make checkall   -> THE GATE: every check above in turn, one line each, logs in build/logs; fails if any does. Also the
#                      Special Edition's sectors when ../files/out/se/ttlv is there. Imported levels (out/Imported) are
#                      passed by (tests/testlevels.h). Run it before and after a change.
#   make replay     -> build/replay: a recording's info, timeline, csv, check
#   make CC=arm-vita-eabi-gcc AR=arm-vita-eabi-ar lib   (cross-compile just the library)
#   make CUSTOM=0 ...  -> the same without custom gameplay (src/custom: ours, not the game's; include/tonic/custom/tcx.h),
#                      into build-nocustom: `make CUSTOM=0 checkall` shows the game doesn't lean on any of it
CC ?= cc
AR ?= ar
CFLAGS ?= -O2
# -ffp-contract=off: no fused multiply-adds, so floats round as the exe's do (x87, single precision) on every platform;
# tools/exe/oracle.py checks the ports against the exe with it (and Package.swift has it too).
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -ffp-contract=off -Iinclude
LDLIBS += -lm
# Custom gameplay (src/custom, tcx_*): in by default; CUSTOM=0 leaves it out (tcx.h's calls become no-ops).
CUSTOM ?= 1
ifeq ($(CUSTOM),0)
BUILD := build-nocustom
CFLAGS += -DTC_NO_CUSTOM
SRC := $(wildcard src/*.c)
else
BUILD := build
SRC := $(wildcard src/*.c) $(wildcard src/custom/*.c)
endif
OBJ := $(SRC:src/%.c=$(BUILD)/%.o)

all: lib $(BUILD)/playcheck $(BUILD)/aicheck $(BUILD)/mechcheck $(BUILD)/crashcheck $(BUILD)/skicheck $(BUILD)/viewcheck $(BUILD)/portalcheck $(BUILD)/sectorcheck $(BUILD)/camcheck $(BUILD)/castcheck $(BUILD)/movecheck $(BUILD)/replaycheck $(BUILD)/debugcheck $(BUILD)/replay
lib: $(BUILD)/libtoniccore.a

# Makefile: a change of flags (as -ffp-contract) rebuilds everything.
$(BUILD)/%.o: src/%.c $(wildcard include/tonic/*.h) $(wildcard include/tonic/custom/*.h) $(wildcard src/*.h) Makefile | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/libtoniccore.a: $(OBJ)
	$(AR) rcs $@ $^

$(BUILD)/playcheck: tests/playcheck.c tests/testlevels.h $(BUILD)/libtoniccore.a
	$(CC) $(CFLAGS) $< $(BUILD)/libtoniccore.a $(LDLIBS) -o $@

$(BUILD)/aicheck: tests/aicheck.c $(BUILD)/libtoniccore.a
	$(CC) $(CFLAGS) $< $(BUILD)/libtoniccore.a $(LDLIBS) -o $@

$(BUILD)/mechcheck: tests/mechcheck.c $(BUILD)/libtoniccore.a
	$(CC) $(CFLAGS) $< $(BUILD)/libtoniccore.a $(LDLIBS) -o $@

$(BUILD)/crashcheck: tests/crashcheck.c $(BUILD)/libtoniccore.a
	$(CC) $(CFLAGS) $< $(BUILD)/libtoniccore.a $(LDLIBS) -o $@

$(BUILD)/skicheck: tests/skicheck.c $(BUILD)/libtoniccore.a
	$(CC) $(CFLAGS) $< $(BUILD)/libtoniccore.a $(LDLIBS) -o $@

$(BUILD)/viewcheck: tests/viewcheck.c $(BUILD)/libtoniccore.a
	$(CC) $(CFLAGS) $< $(BUILD)/libtoniccore.a $(LDLIBS) -o $@

$(BUILD)/portalcheck: tests/portalcheck.c $(BUILD)/libtoniccore.a
	$(CC) $(CFLAGS) $< $(BUILD)/libtoniccore.a $(LDLIBS) -o $@

$(BUILD)/sectorcheck: tests/sectorcheck.c tests/testlevels.h $(BUILD)/libtoniccore.a
	$(CC) $(CFLAGS) $< $(BUILD)/libtoniccore.a $(LDLIBS) -o $@

$(BUILD)/camcheck: tests/camcheck.c $(BUILD)/libtoniccore.a
	$(CC) $(CFLAGS) $< $(BUILD)/libtoniccore.a $(LDLIBS) -o $@
$(BUILD)/castcheck: tests/castcheck.c $(BUILD)/libtoniccore.a
	$(CC) $(CFLAGS) $< $(BUILD)/libtoniccore.a $(LDLIBS) -o $@

$(BUILD)/movecheck: tests/movecheck.c $(BUILD)/libtoniccore.a
	$(CC) $(CFLAGS) $< $(BUILD)/libtoniccore.a $(LDLIBS) -o $@

$(BUILD)/replaycheck: tests/replaycheck.c $(BUILD)/libtoniccore.a
	$(CC) $(CFLAGS) $< $(BUILD)/libtoniccore.a $(LDLIBS) -o $@
$(BUILD)/debugcheck: tests/debugcheck.c $(BUILD)/libtoniccore.a
	$(CC) $(CFLAGS) $< $(BUILD)/libtoniccore.a $(LDLIBS) -o $@
$(BUILD)/tapecheck: tests/tapecheck.c $(BUILD)/libtoniccore.a
	$(CC) $(CFLAGS) $< $(BUILD)/libtoniccore.a $(LDLIBS) -o $@

$(BUILD)/replay: tests/replay.c $(BUILD)/libtoniccore.a
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

skicheck: $(BUILD)/skicheck
	$(BUILD)/skicheck ../files/out/ttlv Totalski
	$(BUILD)/skicheck ../files/out/ttlv skymap

viewcheck: $(BUILD)/viewcheck
	$(BUILD)/viewcheck ../files/out/ttlv bar

portalcheck: $(BUILD)/portalcheck
	$(BUILD)/portalcheck ../files/out/ttlv

sectorcheck: $(BUILD)/sectorcheck
	$(BUILD)/sectorcheck ../files/out/ttlv

camcheck: $(BUILD)/camcheck
	$(BUILD)/camcheck ../files/out/ttlv

castcheck: $(BUILD)/castcheck
	$(BUILD)/castcheck ../files/out/ttlv

movecheck: $(BUILD)/movecheck
	$(BUILD)/movecheck ../files/out/ttlv

tapecheck: $(BUILD)/tapecheck
	$(BUILD)/tapecheck ../files/out/ttlv ../files/out/demos

replay: $(BUILD)/replay

RECORDINGS := $(wildcard ../files/recordings/*.ttrec)
replaycheck: $(BUILD)/replaycheck $(BUILD)/replay
	$(BUILD)/replaycheck ../files/out/ttlv
ifneq ($(RECORDINGS),)
	$(BUILD)/replay check -d ../files/out/ttlv $(RECORDINGS)
endif

debugcheck: $(BUILD)/debugcheck
	$(BUILD)/debugcheck ../files/out/ttlv

# The gate. Each check runs whatever the others did; a red line names its log.
GATE := check mechcheck aicheck crashcheck skicheck viewcheck portalcheck sectorcheck camcheck castcheck movecheck replaycheck debugcheck
SE_TTLV := ../files/out/se/ttlv
checkall: all
	@mkdir -p $(BUILD)/logs; failed=""; \
	for t in $(GATE); do \
		if $(MAKE) --no-print-directory $$t > $(BUILD)/logs/$$t.log 2>&1; then echo "  ok    $$t"; \
		else echo "  FAIL  $$t   ($(BUILD)/logs/$$t.log)"; failed="$$failed $$t"; fi; \
	done; \
	if [ -d $(SE_TTLV) ]; then \
		if $(BUILD)/sectorcheck $(SE_TTLV) > $(BUILD)/logs/sectorcheck-se.log 2>&1; then echo "  ok    sectorcheck (Special Edition)"; \
		else echo "  FAIL  sectorcheck (Special Edition)   ($(BUILD)/logs/sectorcheck-se.log)"; failed="$$failed sectorcheck-se"; fi; \
	fi; \
	if [ -n "$$failed" ]; then echo "checkall: FAILED:$$failed"; exit 1; fi; echo "checkall: all ok"

clean:
	rm -rf $(BUILD)

.PHONY: all lib checkall check aicheck mechcheck crashcheck skicheck viewcheck portalcheck sectorcheck camcheck castcheck movecheck replaycheck debugcheck tapecheck replay clean
