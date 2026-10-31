CC      ?= cc
CFLAGS  ?= -O2 -std=c11 -Wall -Wextra -Werror -march=native
UNAME_S := $(shell uname -s)
ARCH    := $(shell uname -m)

CORE     = src/raster.c src/render.c src/texgen.c src/mesh.c src/assets.c \
           src/anim.c src/physics.c src/audio.c src/percept.c src/persist.c

ifeq ($(ARCH),x86_64)
CORE    += src/fill_x86_64.s
CFLAGS  += -DCASM_HAVE_ASM_FILL=1
endif
ifeq ($(ARCH),arm64)
CORE    += src/fill_arm64.S
CFLAGS  += -DCASM_HAVE_ASM_FILL=1
endif
ifeq ($(ARCH),aarch64)
CORE    += src/fill_arm64.S
CFLAGS  += -DCASM_HAVE_ASM_FILL=1
endif

ifeq ($(UNAME_S),Linux)
PLAY_SRC  = src/play.c src/platform_x11.c src/audio_device_linux.c
PLAY_LIBS = -lX11 -lasound
endif
ifeq ($(UNAME_S),Darwin)
PLAY_SRC  = src/play.c src/platform_macos.m src/audio_device_macos.c
PLAY_LIBS = -framework Cocoa -framework AudioToolbox
endif

all: casm-ps15 casm-ps15-play

casm-ps15: src/main.c $(CORE) src/engine.h
	$(CC) $(CFLAGS) -o $@ src/main.c $(CORE) -lm

casm-ps15-play: $(PLAY_SRC) $(CORE) src/engine.h src/platform.h
	$(CC) $(CFLAGS) -o $@ $(PLAY_SRC) $(CORE) -lm $(PLAY_LIBS)

run: casm-ps15
	./casm-ps15

clean:
	rm -f casm-ps15 casm-ps15-play frame.ppm frame.png audio.wav save.dat

.PHONY: clean run all
