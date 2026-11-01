CC      ?= cc
CFLAGS  ?= -O2 -std=c11 -Wall -Wextra -Werror -march=native
UNAME_S := $(shell uname -s)
ARCH    := $(shell uname -m)

CORE_C   = src/raster.c src/render.c src/texgen.c src/mesh.c src/assets.c \
           src/anim.c src/physics.c src/audio.c src/percept.c src/persist.c \
           src/greyport.c
CORE_OBJ = $(CORE_C:.c=.o)

ifeq ($(ARCH),x86_64)
CORE_OBJ += src/fill_x86_64.o
CFLAGS   += -DCASM_HAVE_ASM_FILL=1
endif
ifeq ($(ARCH),arm64)
CORE_OBJ += src/fill_arm64.o
CFLAGS   += -DCASM_HAVE_ASM_FILL=1
endif
ifeq ($(ARCH),aarch64)
CORE_OBJ += src/fill_arm64.o
CFLAGS   += -DCASM_HAVE_ASM_FILL=1
endif

ifeq ($(UNAME_S),Linux)
PLAY_SRC  = src/play.c src/platform_x11.c src/audio_device_linux.c
PLAY_LIBS = -lX11 -lasound -lpthread
endif
ifeq ($(UNAME_S),Darwin)
PLAY_SRC  = src/play.c src/platform_macos.m src/audio_device_macos.c
PLAY_LIBS = -framework Cocoa -framework AudioToolbox
endif

HEADERS = src/engine.h src/greyport.h src/platform.h

all: casm-ps15 casm-ps15-play

src/%.o: src/%.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

src/%.o: src/%.S
	$(CC) $(CFLAGS) -c $< -o $@

src/%.o: src/%.s
	$(CC) $(CFLAGS) -x assembler-with-cpp -c $< -o $@

casm-ps15: src/main.c $(CORE_OBJ) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ src/main.c $(CORE_OBJ) -lm

casm-ps15-play: $(PLAY_SRC) $(CORE_OBJ) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $(PLAY_SRC) $(CORE_OBJ) -lm $(PLAY_LIBS)

run: casm-ps15
	./casm-ps15

vistest: tools/vistest.c $(CORE_OBJ) $(HEADERS)
	$(CC) $(CFLAGS) -Isrc -o $@ tools/vistest.c $(CORE_OBJ) -lm

test: tools/test_engine.c $(CORE_OBJ) $(HEADERS)
	$(CC) $(CFLAGS) -Isrc -o test-engine tools/test_engine.c $(CORE_OBJ) -lm
	./test-engine

clean:
	rm -f casm-ps15 casm-ps15-play vistest test-engine src/*.o \
	      frame.ppm frame.png audio.wav save.dat

.PHONY: clean run all test
