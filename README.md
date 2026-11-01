# CASM-PS15
<img width="2688" height="864" alt="media-generation-casm-ps15-banner-2026-0-d36aab50-7839-4648-863a-359becf32907" src="https://github.com/user-attachments/assets/530b6e7c-72d9-4c4f-a507-0e9ecfd6ef9d" />


A from-scratch game engine written in C11 with hand-written assembly in the
pixel-rate inner loops, built around a software renderer that targets the
**PS1.5** specification: the tactile rendering character of 32-bit espionage
action games, one deliberate step past hardware limits — crisp low internal
resolution, ordered-dithered 15-bit color, half-pixel vertex snapping,
8-pixel perspective-correct spans, `1/z` depth buffering, hardware-accurate PS1
ABR semi-transparency modes, real-time planar water reflections, and painted
pixel-art procedural textures — paired with **modern third-person stealth-action
systems** underneath.

Rendering behavior is bit-identical on every machine because nothing is
delegated to a GPU or shader compiler. Zero external dependencies beyond `libc`
and `libm` (plus X11/ALSA on Linux or Cocoa/CoreAudio on macOS for the windowed
host).

## The PS1.5 rendering spec

- **480×270 internal framebuffer**, integer-upscaled ×2 to 960×540 on output
  (with live toggleable **Sony Trinitron CRT 25% odd-scanline filter**)
- **Subpixel vertex snapping (`SNAP_PS15`)** to a 0.5-pixel grid (subtle tactile
  shimmer without polygon tearing), switchable at runtime to `SNAP_PS1` wobble
  or smooth `SNAP_OFF`
- **8-pixel perspective-correct span subdivision** (`u/z`, `v/z`, `1/z` with a
  single division every 8 pixels and linear interpolation across the span) for
  **both** opaque and semi-transparent geometry
- **`1/z` inverse-depth buffer** with Sutherland-Hodgman near-plane clipping,
  `RF_MIRROR_Y` winding flip for planar water mirrors, and view-space
  bounding-sphere frustum culling
- **RGB555 color quantization** with 4×4 Bayer ordered dithering and 2× Gouraud
  overbright modulation (`128` = neutral `1.0×`, `255` = `2.0×` specular punch)
- **SIMD-accelerated PS1 ABR semi-transparency & color-key cutouts** —
  `BLEND_HALF` (`0.5B+0.5F` canal water), `BLEND_ADD` (`B+F` volumetric cones &
  lamps), `BLEND_SUB` (`B-F` grounded contact shadows), `BLEND_QUARTER`
  (`B+0.25F` wet reflections & 3D rain), and `TEX_MASK_BIT` zero-sort grating cutouts
- **16 procedural 64×64 pixel-art textures** — weathered brick, wet aggregate
  concrete, riveted steel, urban night pixel camo, shaded forehead/balaclava/ruby
  mosaic visor + gunmetal boom-mic face, 4-quadrant MOLLE tactical gear atlas,
  glowing industrial windows, caution-striped seawalls, color-keyed steel
  grating, radial shadow decals, gold-bordered **NSA eagle shoulder crest**,
  brushed gunmetal-titanium **HK USP** slide & baffles, tileable harbor waves,
  volumetric searchlight beams, espionage CRT terminals, and **Regime Three-Bars
  banners, trooper respirator mask & weathered Mecha hull**
- **Diegetic Amber Visor HUD & Crimson SIGINT Night-Optic Mode** — top compass
  strip, light-reactive segmented body pictogram, 7-segment ammo & curved
  magazine gauge, `OP CLOCK`, and 12-bar `SIG` acoustic spectrum

## Architecture

C11 owns the engine and simulation. Hand-written assembly (`src/fill_arm64.S` on
AArch64 NEON, `src/fill_x86_64.s` on x86_64 SSE2) owns the **five** pixel-rate hot
loops that execute millions of times per frame (~310 FPS at ~9,100 submitted
triangles/frame on a single core):

1. `ps15_fill32` — 64-byte unrolled SIMD framebuffer/skyline clear
2. `ps15_upscale2x` — dual-row SIMD `zip1`/`zip2` / `punpckldq` 480×270 → 960×540 upscaler
3. `ps15_upscale2x_crt` — dual-row SIMD 2× upscaler with 25% odd-row Trinitron scanline attenuation
4. `ps15_span_tex64` — register-resident opaque textured Gouraud + fog + `1/z` + Bayer RGB555 span kernel
5. `ps15_span_abr64` — register-resident 4-mode PS1 ABR semi-transparent textured Gouraud + fog + `1/z` + Bayer RGB555 span kernel

Every assembly routine has a bit-identical portable C counterpart (`ps15_c_*`),
verified across thousands of randomized spans by `make test` (36 checks).

| Module | Contents |
| --- | --- |
| `src/raster.c` | Framebuffer, 3D starfield/moon sky dome, planar-gradient scanline rasterizer, ABR blend modes, diegetic amber visor HUD & SIGINT filter |
| `src/render.c` | Frustum culling, RGB Gouraud point/spot/rim lighting, planar water mirror & animated water plane, 11-part humanoid, Patrol Mecha & 3D projection |
| `src/texgen.c` | 16 deterministic 64×64 painted procedural textures (NSA crest, gunmetal USP, harbor water, CRT terminal, Regime 3-bars & Mecha armor) |
| `src/mesh.c` | Tiled/subdivided architectural primitives, sculpted NSA espionage operator, Regime Trooper, 3.55 m Type-1 Patrol Mecha & 3-prong grapple claw |
| `src/anim.c` | Hierarchical keyframe clips (`CLIP_IDLE`, `CLIP_WALK`, `CLIP_RUN`, `CLIP_AIM` Center-Axis-Relock, `CLIP_CROUCH`) & crossfades |
| `src/greyport.c` | Flooded Greyport harbor canal (planar reflections), viaduct, crane, warehouses, shootable sodium lamps, intel terminals & hitscan raycasts |
| `src/physics.c` | Collision-resolved Third-Person Over-the-Shoulder camera (`tps_camera_step`), raycast-anything grapple, crouch-slide, wall-run & ledge auto-mantle |
| `src/audio.c` | Deterministic synthesizer — hum, footsteps, grapple winch, suppressed HK USP, glass shatter, visor chime, terminal hack & heavy Mecha steps |
| `src/percept.c` | 3D raycasts (`ray_hit_aabb`, `ray_hit_sphere`), light-reactive Trooper AI, acoustic bullet-ping lures & subsystem-damageable Patrol Mecha AI |
| `src/persist.c` | Versioned binary save states with CRC32 integrity verification |
| `src/assets.c` | Hardened Wavefront OBJ and PPM (P3/P6) loaders with fallback to procedural assets |
| `src/platform_x11.c` / `src/platform_macos.m` | Windowing, raw mouse-delta capture, mouse buttons (`LMB`/`RMB`), keyboard input & 60 Hz presentation |
| `src/audio_device_linux.c` / `src/audio_device_macos.c` | Live low-latency audio streaming (ALSA on Linux, AudioToolbox on macOS) |
| `src/play.c` | Modern Third-Person Over-the-Shoulder stealth-action sandbox with Handler Debrief Dossier |
| `src/fill_arm64.S` / `src/fill_x86_64.s` | Hand-written AArch64 NEON and x86_64 SSE2 pixel/span/ABR/CRT kernels |
| `tools/test_engine.c` / `tools/vistest.c` | 36-part automated verification suite (NEON/SSE2/C bit parity) and 8-shot diagnostic visual harness |

## Build, test, and run

```sh
make               # build casm-ps15 (headless demo) and casm-ps15-play (windowed)
make test          # run 36-part ASM/C bit-parity & gameplay/physics invariant suite
make vistest       # compile 8-shot showcase camera harness (./vistest <outdir>)
./casm-ps15        # simulate & render 900 frames + print performance ledger
./casm-ps15-play   # launch interactive Third-Person Greyport dockyard operation
```

### Interactive controls (`casm-ps15-play`)

- **WASD + Mouse** (click window to lock cursor, **Tab** to release): 360° camera-relative movement & over-the-shoulder orbit
- **RMB / Q** (hold): tight right-shoulder precision ADS aim
- **LMB / X / Return**: fire suppressed HK USP (trooper headshots, shootable sodium lamps, wall-ping lures, Patrol Mecha `SENSOR`/`ACTUATOR`/`COOLANT`)
- **E** (hold): physical raycast-anything grapple (**Ctrl / C** locks rope length for pure pendulum swing)
- **Space**: coyote/buffered jump, wall-jump off vertical warehouse facades, and automatic waist/chest ledge vaulting
- **Shift + C / Ctrl**: sprint into tactical crouch-slide
- **F**: silent close-quarters takedown behind unaware trooper
- **R**: hack classified Intel Terminal / reload HK USP / deploy next operation
- **N** (or **6**): toggle Crimson SIGINT Night-Optic Visor
- **M** (or **8**): toggle NSA Handler Debrief Dossier
- **V**: toggle front-orbit character showcase camera
- **1–5, 7**: engine showcase toggles (`1` Tech stats, `2` Vertex snap, `3` Perspective/Affine UV, `4` Bayer RGB555 DAC, `5` Wireframe, `7` Trinitron CRT scanlines)
- **Esc**: quit
