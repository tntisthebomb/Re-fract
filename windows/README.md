# Re-fract for Windows

A port of the Nintendo 3DS fractal ray-marcher to Windows (and Linux, for testing). It uses SDL2 and
OpenGL 3.3 only. The menu, bitmap-font panel, controls and scene format are the same as the 3DS app: the
320x240 touch-screen panel is drawn beside the render view, and the 3DS buttons are mapped to the keyboard.

Two render backends are available and switchable at runtime (RENDER tab, `BACKEND`):

* **GPU** (default): a GLSL ray-marcher generated from the current formula. Needs OpenGL 3.3 only, so it
  runs on any normal GPU driver (for example an RTX 5070 Ti with the standard NVIDIA driver). No CUDA needed.
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
| START (exit) | `Esc` |
| Touch screen | Left mouse click on the panel: tab strip, rows, left edge = decrease, right edge = increase. Mouse wheel over the panel moves the selection |
| 3D slider | `[` / `]` change stereo strength (also the `STEREO STRENGTH` row) |

Extras: `Tab` hides/shows the panel, `F11` toggles borderless fullscreen, `F12` saves `screenshot.png` of the render. Moving the camera
leaves quality mode, exactly like the 3DS. Text entry (`A` on a number or expression row) opens a modal box:
type, `Enter` to accept, `Esc` to cancel, `Ctrl+V` pastes.

## RENDER tab, DISPLAY rows (at the end of the tab)

* `BACKEND` GPU / CPU.
* `RENDER RESOLUTION`: 400x240 (3DS), 640x360, 854x480, 1280x720, 1600x900, 1920x1080, 2560x1440, 3840x2160,
  MATCH WINDOW (follows the render view), or CUSTOM. `CUSTOM WIDTH` / `CUSTOM HEIGHT` (64..7680 x 64..4320)
  select CUSTOM automatically. The image is letterboxed to keep its aspect ratio.
* `MOTION SCALE` 1, 1/2, 1/4: GPU render scale while moving; full resolution once still.
* `FULLSCREEN`, `WINDOW SIZE` (presets), `PANEL SCALE` (auto or 1-4x integer), `SCALING FILTER` (nearest/linear).
* `STEREO MODE` OFF / SIDE BY SIDE / ANAGLYPH R/C and `STEREO STRENGTH` (0..1). Eye separation and convergence
  are the usual `EYE SEPARATION` / `CONVERGENCE` rows.
* Supersampling uses the existing `QUALITY SAMPLES` (1/2/4) in quality mode (`Y`).

All other rows (presets, render, formula, stage editor, color, files) match the 3DS app.

## Files

Scene slots (`scene-N.rfs`), `EXPORT STEREO PPM` (`image-N-left.ppm` / `-right.ppm`, read back from the GPU
when that backend is active) and `SCREENSHOT PNG` (`image-N.png`) go to `%USERPROFILE%\Documents\Re-fract`
(Linux: `~/Re-fract`).

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

Other options: `--backend cpu|gpu`, `--res WxH`, `--preset N`, `--move N` (hold forward for N frames),
`--stereo off|sbs|ana`, `--window-shot out.ppm`, `--tab N --select N`. Use `LIBGL_ALWAYS_SOFTWARE=1
MESA_GL_VERSION_OVERRIDE=3.3` for software GL.
