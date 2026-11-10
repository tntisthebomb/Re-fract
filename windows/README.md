# Re-fract for Windows

A port of the Nintendo 3DS fractal ray-marcher to Windows (and Linux, for testing). It uses SDL2 and
OpenGL 3.3 only. The menu, bitmap-font panel, controls and scene format are the same as the 3DS app
(branch `codex/corridors-lighting`: 40 presets, Kleinian fold / inversion stages, point lights, DOF, GI and
progressive lighting, adaptive sky/empty-space skipping, depth prepass, upscaling, multi-stop colour wheel
gradients, emission/bloom, JPEG/MPO screenshots, benchmarks): the 320x240 touch-screen panel is drawn beside
the render view, and the 3DS buttons are mapped to the keyboard and mouse.

Two render backends are available and switchable at runtime (RENDER tab, `BACKEND`):

* **GPU** (default): a GLSL ray-marcher generated from the current formula. Needs OpenGL 3.3 only, so it
  runs on any normal GPU driver (for example an RTX 5070 Ti with the standard NVIDIA driver). No CUDA needed.
  It is used only while `GpuRenderer::supports(scene)` says it reproduces every enabled feature. If the scene
  needs something the GPU path does not implement, that scene renders on the CPU backend and the status line
  shows `CPU RENDER: <reason>`; it switches back to the GPU automatically once the scene is supported again.
  The `BACKEND` row keeps your preference (`GPU`, `GPU>CPU` while falling back, or `CPU`).
* **CPU**: the original 3DS renderer (progressive interlaced preview, auto refine, quality mode), run on a
  worker thread pool using every hardware thread. Used automatically if the GPU backend cannot start.

## Controls

| 3DS | Keyboard |
|---|---|
| Circle pad (move) | `W` `A` `S` `D` |
| C-stick (look) | Arrow keys; or hold the **right mouse button** and drag |
| L / R (turn) | `Q` / `E` |
| ZL / ZR (down / up) | `Z` / `C` |
| X (3x speed) | Left Shift |
| D-pad (menu up/down/left/right) | `I` `K` `J` `L` (or numpad `8` `2` `4` `6`) |
| A (activate / type a value) | `Enter` |
| B (reset camera to preset) | `B` or `Backspace` |
| Y (quality / still mode) | `Y` |
| SELECT (next tab) | `Space` |
| Touch screen | Left mouse click on the panel: tab strip, rows, left edge = decrease, right edge = increase. Hovering a row selects it |
| Value scrubbing | Mouse wheel over the panel adjusts the selected row exactly like D-pad left/right (wheel up = increase). `Shift` = 10x step, `Ctrl` = 0.1x step on number rows (integer rows keep 1x). Choices cycle, on/off rows toggle once per notch, action rows ignore the wheel. The wheel outside the panel does nothing |
| START (screenshot on the new 3DS build) | `F12` (see Screenshots); `Esc` exits the program (also the `EXIT APP` row, FILES tab) |
| 3D slider | `[` / `]` change stereo strength (also the `STEREO STRENGTH` row) |

Extras: `Tab` hides/shows the panel, `F11` toggles borderless fullscreen, `F12` saves a screenshot set. Moving the camera
leaves quality mode, exactly like the 3DS. Text entry (`A` on a number or expression row) opens a modal box:
type, `Enter` to accept, `Esc` to cancel, `Ctrl+V` pastes.

## RENDER tab, DISPLAY rows (at the end of the tab)

* `BACKEND` GPU / CPU.
* `RENDER RESOLUTION`: 400x240 (3DS), 640x360, 854x480, 1280x720, 1600x900, 1920x1080, 2560x1440, 3840x2160,
  MATCH WINDOW (follows the render view), or CUSTOM. `CUSTOM WIDTH` / `CUSTOM HEIGHT` (any integer; select
  CUSTOM automatically). The only limits are what the hardware can do: `GL_MAX_TEXTURE_SIZE` and about 70% of
  the free host RAM (about 300 bytes per pixel for the CPU renderer, 24 for the GPU path). A request beyond
  that is scaled down keeping the aspect ratio and the status line says `RESOLUTION LIMITED BY GPU/RAM TO WxH`;
  an allocation failure falls back to 1280x720 with a status message instead of crashing. The image is
  letterboxed to keep its aspect ratio.
* `MOTION SCALE` 1, 1/2, 1/4: GPU render scale while moving; full resolution once still.
* `FULLSCREEN`, `WINDOW SIZE` (presets), `PANEL SCALE` (auto or 1-4x integer), `SCALING FILTER` (nearest/linear).
* `STEREO MODE` OFF / SIDE BY SIDE / ANAGLYPH R/C and `STEREO STRENGTH` (0..1). Eye separation and convergence
  are the usual `EYE SEPARATION` / `CONVERGENCE` rows.
* Supersampling uses `QUALITY SAMPLES` (any integer >= 1) in quality mode (`Y`). Up to 4 samples use the 2x2 grid, more use the R2 low-discrepancy sequence (`samplePattern` in `engine.hpp`, shared by CPU and GPU).

All other rows (presets, render, formula, stage editor, color, files) match the new 3DS app, including:
`STILL BLOCK` / `UPSCALING` (nearest, bilinear, bicubic), depth of field, zoom precision, depth prepass,
adaptive ray budget / empty space / sky edges, separate and progressive GI, point lights (2), sun strength,
sky colour, emission / bloom, `GRADIENT STOPS` (2..5) with `BOUNDED COLOR MAP`, `AUTO FIT GRADIENT`, distance
field cache, the trace benchmark (`BENCH CURRENT VIEW` writes `benchmark-N.csv`), and the new stage types
`KLEINIAN FOLD` (FOLD X/Y/Z, default 0.8) and `INVERSION` (INVERSION RADIUS, default 1). Loading preset 24..39
also loads its tuned `presetSettings()` (as on the 3DS). The colour rows open a colour wheel: drag with the
left mouse button on the wheel / brightness bar, `IJKL` (or arrows) hue and saturation, `Q`/`E` or the wheel for
brightness, `H` types a hex colour, `Enter` applies, `Esc`/`B` cancels.
Left out as 3DS-only: the GPU surface (mesh) cache rows, `CAPTURE GPU SURFACE`, `RESUME CPU RENDER`.

On the GPU backend the CPU-only conveniences (`UPSCALING` live re-scaling, instant recolour, `AUTO FIT GRADIENT`)
are not available; scenes using features the GPU renderer does not implement render on the CPU (see above).

## Parameter limits

**There are no artificial parameter limits.** Every number row accepts any finite value when typed (`Enter`),
and the D-pad / wheel step just moves by the row's step. Only conditions the math needs are enforced, and the
existing messages are shown when a value is rejected (`INVALID NUMBER / RANGE`): radii, spacings, epsilon,
safety, far clip, bailout, derivative scale, focal distance, ranges > 0; strengths >= 0; Bulb power > 1;
Menger scale > 1; sphere fold 0 < minimum <= fixed; field of view strictly between 0 and 180 degrees; camera
pitch within +-1.5 rad (stable basis); gradient stops 2..5; relaxation >= 1; integer counts >= 1 (or 0 where 0
means off). Steps, iterations, samples (quality / lens / GI / AO), shadow steps, batch size, interlace lanes,
lighting passes, target FPS and the number of stages are unbounded (stage storage and the CPU job batch are
dynamic). Block sizes (preview / still / lighting / prepass) are any integer >= 1 (D-pad doubles/halves) and
are capped at 2^20 only to keep index arithmetic in `int` range. Scene files are still validated structurally
(finite numbers, valid enums, correct format) and malformed files are rejected.

Extreme values can be slow, produce garbage, or hang the CPU renderer. `Esc` exits, `B` resets the view. The
CPU path renders in time-budgeted batches, so the UI keeps responding even with huge values; the GPU path
draws in time-budgeted strips to stay clear of the Windows TDR timeout.

Note: the 3DS `tests/core.cpp` expects a few limits to be enforced (tiny repeat spacing, > 64 GI samples,
unbounded accumulation passes, ...). Those assertions now fail by design; all other 48168 checks pass.

## Files

Scene slots (`scene-N.rfs`), `EXPORT STEREO PPM` (`image-N-left.ppm` / `-right.ppm`, read back from the GPU
when that backend is active), `SCREENSHOT PNG` (`image-N.png`) and benchmarks go to
`%USERPROFILE%\Documents\Re-fract` (Linux: `~/Re-fract`).

### Screenshots

`F12` or the `SAVE SCREENSHOT` row (as START on the 3DS) writes to `Re-fract\screenshots\shot-<time>.jpg`
(the render), `shot-<time>.mpo` (a 3D MPO, only while stereo is active and at full render size) and
`shot-<time>-screens.jpg` (render with the panel below it). JPEGs are limited to 16384 pixels per side by the encoder.

## Building on Windows

Requires Visual Studio 2022 (C++ workload) and CMake 3.21+. SDL2 is fetched and built statically
automatically (or an installed SDL2 found by `find_package` is used, and its DLL copied next to the exe).

    cmake -S windows -B build -G "Visual Studio 17 2022" -A x64
    cmake --build build --config Release

The result is `build\Release\Re-fract.exe` (static MSVC runtime, nothing else to install).

## CI artifact

`.github/workflows/windows.yml` builds on `windows-latest` and uploads `Re-fract-windows` (a ZIP with
`Re-fract.exe`). Download it from the workflow run's Artifacts section. A Linux job builds the same sources
and runs the headless self-test.

## Headless self-test

    xvfb-run -a Re-fract --selftest --frames 20 --preset 3 --screenshot out.ppm --panel-shot panel.ppm

Other options: `--backend cpu|gpu`, `--res WxH`, `--preset N` (0..39; 36..39 are the Kleinian chambers),
`--move N` (hold forward for N frames), `--stereo off|sbs|ana`, `--window-shot out.ppm`, `--tab N --select N`,
`--wheel N` (inject N wheel notches on the selected row at frame 2, to test scrubbing). Use `LIBGL_ALWAYS_SOFTWARE=1
MESA_GL_VERSION_OVERRIDE=3.3` for software GL.
