CC      ?= cc
CFLAGS  ?= -O2 -std=c11 -Wall -Wextra -Werror -march=native
SRC      = src/main.c src/raster.c src/render.c src/texgen.c src/mesh.c \
           src/anim.c src/physics.c src/audio.c src/percept.c src/persist.c
ARCH     := $(shell uname -m)

ifeq ($(ARCH),x86_64)
SRC     += src/fill_x86_64.s
CFLAGS  += -DCASM_HAVE_ASM_FILL=1
endif

casm-ps15: $(SRC) src/engine.h
	$(CC) $(CFLAGS) -o $@ $(SRC) -lm

run: casm-ps15
	./casm-ps15

clean:
	rm -f casm-ps15 frame.ppm frame.png audio.wav save.dat

.PHONY: clean run
