# CASM-PS15
<img width="2688" height="864" alt="media-generation-casm-ps15-banner-2026-0-d36aab50-7839-4648-863a-359becf32907" src="https://github.com/user-attachments/assets/530b6e7c-72d9-4c4f-a507-0e9ecfd6ef9d" />


A from-scratch game engine written in C with hand-written assembly in the
hot loops, built around a software renderer that targets the **PS1.5**
specification: the rendering character of the 32-bit era, one deliberate
step past it — low internal resolution, quantized color, vertex snapping,
flat-shaded geometry with painted procedural textures — with modern
simulation underneath. Rendering behavior is identical on every machine
because nothing is delegated to a GPU or a shader compiler.

Zero dependencies beyond libc and libm.

## The PS1.5 rendering spec

- **480×270 framebuffer**, integer-upscaled ×2 to 960×540 on output
- **Vertex snapping** to the low-resolution grid (the era's signature
  wobble, kept subtle), with near-plane clipping
- **RGB555 color quantization** with Bayer ordered dithering
- **Textured triangles** with per-vertex lighting, depth fog, and a
  z-buffer; no texture filtering
- **Procedural textures only** — brick, concrete, metal, camo and face
  sets are generated in code at 64×64; the engine ships no image assets
- **Point-light rigs** over a hemisphere ambient: warm practical pools
  against deep blue-grey shadow masses

## Architecture

C owns the engine. Assembly owns only the inner loops that run millions
of times per frame — currently the framebuffer fill, in x86_64
(`src/fill_x86_64.s`) and AArch64 (`src/fill_arm64.S`) forms — and
every assembly routine has a portable C equivalent, so the engine
builds and runs correctly on any target.

| Module | Contents |
| --- | --- |
| `raster.c` | Framebuffer, sky, textured triangle rasterizer, glow and HUD primitives |
| `render.c` | Camera transform, vertex snapping, projection, mesh and humanoid rendering, light rigs |
| `texgen.c` | Procedural texture generation (brick, concrete, metal, camo, face) |
| `mesh.c` | Box/plane geometry and a seven-part articulated humanoid |
| `anim.c` | Keyframed animation clips (idle, walk, run), sampling and blending |
| `physics.c` | AABB world collision, gravity, and a grapple with rope constraint |
| `audio.c` | Synthesized audio mixer — tones, filtered noise, footsteps, lamp hum — with WAV export |
| `percept.c` | Guard perception: line-of-sight tests, waypoint patrols, noise-driven alert levels |
| `persist.c` | Checksummed save states with corruption detection |
| `assets.c` | File asset loading: Wavefront OBJ meshes, PPM textures (P3/P6) |
| `platform_x11.c` / `platform_macos.m` | Window, input and presentation: X11 on Linux, Cocoa on macOS |
| `audio_device_linux.c` / `audio_device_macos.c` | Live audio output: ALSA on Linux, CoreAudio on macOS |
| `play.c` | The windowed, playable host (first/third person) |
| `fill_x86_64.s` / `fill_arm64.S` | The assembly fill loop, per architecture, each with a C equivalent |

All randomness runs through a SplitMix64 generator, so a given build
produces the same frames, the same audio mix, and the same simulation
results on every machine.

## Build and run

```sh
make
./casm-ps15        # headless demonstration + ledger
./casm-ps15-play   # windowed, playable
```

In the windowed host: **WASD + mouse** move and look, **Shift** sprint,
**Ctrl** crouch, **Space** jump, **E** grapple (press fires at the
lamp/tower in the view cone, hold keeps the rope, release detaches),
**V** toggles first/third person, **Esc** quits. Live synthesized audio
(lamp hum, footsteps, grapple, alarm) plays through the platform audio
device; the third-person camera is collision-resolved against the
world. File assets under `assets/` (an OBJ crate, a PPM brick texture)
are loaded at startup, with procedural generation as the fallback.

The default build is a headless demonstration scene — a night street of
warehouses and a watchtower under sodium light, an operator crossing it
on foot and by grapple, and a patrolling guard — which simulates and
renders 900 frames, then writes its outputs and prints a cost ledger:

- `frame.ppm` — the final frame, upscaled to 960×540
- `audio.wav` — the synthesized mix: lamp hum, footsteps, the grapple
- `save.dat` — a save state, written and read back for verification

The ledger reports triangles per frame, mean render time, the
operator's route and grapple, the guard's peak alert level, and the
persistence check — performance claims are printed by the run itself,
on whatever hardware runs it.

## Status

In active development. The renderer and the core simulation modules
(raster, render, animation, physics, audio, perception, persistence)
are working and exercised end-to-end by the demonstration scene, and
the windowed host (`casm-ps15-play`) is playable on Linux (X11) and
macOS (Cocoa) with live audio on both platforms.
