#ifndef CASM_PLATFORM_H
#define CASM_PLATFORM_H

#include <stdint.h>
#include "engine.h"

/* Platform layer for the windowed host: X11 on Linux, Cocoa on macOS.
 * One API, two implementations. The window is a fixed 960x540 canvas
 * receiving the engine's 2x-upscaled framebuffer. */

enum {
    K_FWD = 1u << 0, K_BACK = 1u << 1, K_LEFT = 1u << 2, K_RIGHT = 1u << 3,
    K_SPRINT = 1u << 4, K_CROUCH = 1u << 5, K_JUMP = 1u << 6,
    K_GRAPPLE = 1u << 7, K_TOGGLE_VIEW = 1u << 8, K_QUIT = 1u << 9
};

typedef struct {
    uint32_t keys;          /* held this frame */
    uint32_t pressed;       /* went down this frame */
    float mouse_dx, mouse_dy;
} Input;

typedef struct Plat Plat;

Plat *plat_open(const char *title, int w, int h);
int plat_poll(Plat *p, Input *in);                 /* 0 = quit requested */
void plat_present(Plat *p, const uint32_t *px);    /* 960x540, 0xRRGGBB */
void plat_close(Plat *p);

/* Audio device backend (ALSA on Linux, CoreAudio on macOS). */
int audev_start(AudioLive *live);                  /* 0 ok, -1 no device */
void audev_stop(void);
const char *audev_name(void);

#endif
