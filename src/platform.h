#ifndef CASM_PLATFORM_H
#define CASM_PLATFORM_H

#include <stdint.h>
#include "engine.h"

/* Platform layer for the windowed host: X11 on Linux, Cocoa on macOS.
 * One API, two implementations. The window is a fixed 960x540 canvas
 * receiving the engine's 2x-upscaled framebuffer. */

enum {
    K_FWD         = 1u << 0,
    K_BACK        = 1u << 1,
    K_LEFT        = 1u << 2,
    K_RIGHT       = 1u << 3,
    K_SPRINT      = 1u << 4,
    K_CROUCH      = 1u << 5,
    K_JUMP        = 1u << 6,
    K_GRAPPLE     = 1u << 7,
    K_TOGGLE_VIEW = 1u << 8,
    K_QUIT        = 1u << 9,
    /* Live PS1.5 graphics engine showcase & camera controls */
    K_HUD         = 1u << 10,  /* 1 / F1: toggle engine telemetry HUD */
    K_SNAP_MODE   = 1u << 11,  /* 2 / F2: cycle PS1.5 / PS1 / Off vertex snap */
    K_UV_MODE     = 1u << 12,  /* 3 / F3: toggle PS1.5 perspective vs PS1 affine */
    K_DAC_MODE    = 1u << 13,  /* 4 / F4: cycle Bayer RGB555 / Flat555 / RGB888 */
    K_WIRE_MODE   = 1u << 14,  /* 5 / F5: toggle wireframe debug overlay */
    K_LOOK_L      = 1u << 15,
    K_LOOK_R      = 1u << 16,
    K_LOOK_U      = 1u << 17,
    K_LOOK_D      = 1u << 18,
    /* Modern stealth-action combat & SIGINT gadget inputs */
    K_FIRE        = 1u << 19,  /* LMB / X / Return: fire HK USP */
    K_AIM         = 1u << 20,  /* RMB / Q: over-the-shoulder precision ADS */
    K_USE         = 1u << 21,  /* R: hack terminal / reload / new op */
    K_MELEE       = 1u << 22,  /* F: silent CQC takedown */
    K_VISOR       = 1u << 23,  /* N / 6 / F6: SIGINT optic visor mode */
    K_CRT         = 1u << 24,  /* 7 / F7: SIMD 2x CRT scanline filter */
    K_DOSSIER     = 1u << 25   /* M / 8: live mission debrief dossier */
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
