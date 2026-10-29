# SDL3 frontend for TonicCore (Linux, and anything else SDL_GPU runs on). See docs/SDL3.md.
#   make            -> build/opentonic (builds ../core and, without a system SDL3, the pinned SDL below)
#   make sdl3       -> only fetch and build the pinned SDL3 into build/sdl3
#   make clean      -> remove our objects and shaders (keeps the SDL3 build)
#   make distclean  -> remove build/ entirely
# Needs: a C11 compiler, glslangValidator (shaders -> SPIR-V), and for the pinned SDL3: git, cmake.
# third_party/pl_mpeg.h (intro movie; MIT, github.com/phoboslab/pl_mpeg c871f2b) is vendored; -isystem keeps its
# warnings out of ours.
CC ?= cc
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -MMD -MP
GLSLANG ?= glslangValidator
BUILD := build
CORE := ../core
CORE_LIB := $(CORE)/build/libtoniccore.a

# A system SDL3 when pkg-config knows one; otherwise this release, built statically into build/sdl3.
SDL_TAG := release-3.4.8
SDL_SRC := $(BUILD)/SDL
SDL_PREFIX := $(abspath $(BUILD)/sdl3)
ifeq ($(shell pkg-config --exists 'sdl3 >= 3.4.0' && echo yes),yes)
SDL_DEP :=
SDL_CFLAGS := $(shell pkg-config --cflags sdl3)
SDL_LIBS := $(shell pkg-config --libs sdl3)
else
SDL_DEP := $(SDL_PREFIX)/lib/libSDL3.a
SDL_CFLAGS := -I$(SDL_PREFIX)/include
# Deferred (=): only expanded at link time, after the pinned SDL3 is installed.
SDL_LIBS = $(shell PKG_CONFIG_PATH=$(SDL_PREFIX)/lib/pkgconfig pkg-config --static --libs sdl3)
endif

SRC := $(wildcard src/*.c)
OBJ := $(SRC:src/%.c=$(BUILD)/obj/%.o)
# SPIR-V embedded as C arrays (glslangValidator --vn): build/shaders/<name>.h
SHADERS := level_vert level_frag level_flat_frag ui_vert ui_frag overlay_vert overlay_frag bar_frag shadow_vert shadow_frag
SHADER_H := $(SHADERS:%=$(BUILD)/shaders/%.h)

all: $(BUILD)/opentonic

$(BUILD)/opentonic: $(OBJ) $(CORE_LIB) $(SDL_DEP)
	$(CC) $(CFLAGS) $(OBJ) $(CORE_LIB) $(SDL_LIBS) -lm -o $@

$(BUILD)/obj/%.o: src/%.c $(SHADER_H) $(SDL_DEP) | $(BUILD)/obj
	$(CC) $(CFLAGS) -I$(CORE)/include -I$(BUILD)/shaders -isystem third_party $(SDL_CFLAGS) -DTONIC_REPO_DIR='"$(abspath ..)"' -c $< -o $@

# The core has its own Makefile; always ask it (it knows when it is up to date).
$(CORE_LIB): FORCE
	$(MAKE) -C $(CORE) lib CC="$(CC)"

$(BUILD)/shaders/level_vert.h: shaders/level.vert shaders/level_common.glsl | $(BUILD)/shaders
	$(GLSLANG) -V --target-env vulkan1.0 -Ishaders -S vert --vn level_vert -o $@ $<
$(BUILD)/shaders/level_frag.h: shaders/level.frag shaders/level_common.glsl | $(BUILD)/shaders
	$(GLSLANG) -V --target-env vulkan1.0 -Ishaders -S frag --vn level_frag -o $@ $<
$(BUILD)/shaders/level_flat_frag.h: shaders/level.frag shaders/level_common.glsl | $(BUILD)/shaders
	$(GLSLANG) -V --target-env vulkan1.0 -Ishaders -S frag -DFLAT --vn level_flat_frag -o $@ $<
$(BUILD)/shaders/ui_vert.h: shaders/ui.vert | $(BUILD)/shaders
	$(GLSLANG) -V --target-env vulkan1.0 -S vert --vn ui_vert -o $@ $<
$(BUILD)/shaders/ui_frag.h: shaders/ui.frag | $(BUILD)/shaders
	$(GLSLANG) -V --target-env vulkan1.0 -S frag --vn ui_frag -o $@ $<
$(BUILD)/shaders/overlay_vert.h: shaders/overlay.vert | $(BUILD)/shaders
	$(GLSLANG) -V --target-env vulkan1.0 -S vert --vn overlay_vert -o $@ $<
$(BUILD)/shaders/overlay_frag.h: shaders/overlay.frag | $(BUILD)/shaders
	$(GLSLANG) -V --target-env vulkan1.0 -S frag --vn overlay_frag -o $@ $<
$(BUILD)/shaders/bar_frag.h: shaders/bar.frag | $(BUILD)/shaders
	$(GLSLANG) -V --target-env vulkan1.0 -S frag --vn bar_frag -o $@ $<
$(BUILD)/shaders/shadow_vert.h: shaders/shadow.vert | $(BUILD)/shaders
	$(GLSLANG) -V --target-env vulkan1.0 -S vert --vn shadow_vert -o $@ $<
$(BUILD)/shaders/shadow_frag.h: shaders/shadow.frag | $(BUILD)/shaders
	$(GLSLANG) -V --target-env vulkan1.0 -S frag --vn shadow_frag -o $@ $<

sdl3: $(SDL_PREFIX)/lib/libSDL3.a

# Static SDL3 from the pinned tag. Its audio/video backends are picked by the headers present at configure time
# (see docs/SDL3.md for the packages); SDL loads the libraries themselves at runtime. On Linux SDL stops at any missing
# X11 extension; the two we don't need (screensaver inhibit falls back to D-Bus, XTest is unused) are switched off
# when their headers aren't installed (libxss-dev, libxtst-dev).
SDL_CMAKE_FLAGS := -DSDL_X11_XSCRNSAVER=$(if $(shell pkg-config --exists xscrnsaver && echo y),ON,OFF) \
                   -DSDL_X11_XTEST=$(if $(shell pkg-config --exists xtst && echo y),ON,OFF)
$(SDL_PREFIX)/lib/libSDL3.a:
	[ -d $(SDL_SRC) ] || git clone --depth 1 --branch $(SDL_TAG) https://github.com/libsdl-org/SDL.git $(SDL_SRC)
	cmake -S $(SDL_SRC) -B $(BUILD)/sdl3-build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$(SDL_PREFIX) \
	  -DCMAKE_INSTALL_LIBDIR=lib -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
	  $(SDL_CMAKE_FLAGS)
	cmake --build $(BUILD)/sdl3-build --parallel
	cmake --install $(BUILD)/sdl3-build

$(BUILD)/obj $(BUILD)/shaders:
	mkdir -p $@

clean:
	rm -rf $(BUILD)/obj $(BUILD)/shaders $(BUILD)/opentonic

distclean:
	rm -rf $(BUILD)

-include $(OBJ:.o=.d)

.PHONY: all sdl3 clean distclean FORCE
