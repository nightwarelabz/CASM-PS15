# CASM-PS15 — C + Assembly PS1.5 Engine Trial

Chase's trial (2026-10-31): try building an engine in **C + assembly**,
specifically for the PlayStation 1.5 look DeBrief is aiming at —
better than PS1, not quite PS2, at the official banner's temperature.

This is a trial, not a replacement for C35. It earns its place by
rendering, or it doesn't.

## Doctrine

- **C owns the engine.** Renderer, scene, math, game logic.
- **Assembly owns only the hot inner loops** — the framebuffer fill /
  raster work that runs millions of times a frame. Whole-engine assembly
  would be unmaintainable and arch-locked.
- **Software renderer, fixed internal resolution.** The PS1.5 look is
  controlled exactly because nothing is left to GPU shader variance:
  - 480x270 framebuffer, integer-upscaled on presentation
  - Vertex snapping to the low-res grid (the PS1 wobble, kept subtle)
  - Flat shading, per-face light — sodium pools vs. blue-grey shadow
  - 15-bit-style color quantization (RGB555) + ordered dither
  - Fog by depth
- **Fixed-point math** in the transform path, like the era.
- Arch assembly is per-target: `src/fill_x86_64.s` today (G1 / this VM),
  an ARM64 twin is the M1 Air's version. The C fallback is always correct.

## Build & run

```sh
make
./casm-ps15        # renders 120 frames headless, writes frame.ppm, prints the ledger
```

`frame.ppm` is the last frame — a low-poly test scene: ground plane,
two warehouse blocks, a watchtower with a sodium lamp pool, in the
banner's palette (deep blue-grey shadow, sodium amber light).

## Ledger (the claim, measured)

The binary prints frames, triangles, mean ms/frame and fps for the
software render at 480x270 on whatever machine runs it.
