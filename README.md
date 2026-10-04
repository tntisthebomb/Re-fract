# Re-fract

A customizable stereoscopic fractal laboratory for New Nintendo 3DS homebrew, with a charcoal, rust, scratched-metal UI inspired by mid-2000s grunge software.

## Features

- 18 presets across Mandelbulb, Mandelbox, Julia, Menger, Sierpinski and hybrid families. Some entries are parameter variations, not separate mathematical families.
- Up to 12 editable formula stages: folds, spherical power, scaling, rotations, offsets, absolute/sort operations, Menger and tetrahedral transforms, and custom expressions.
- Separate off-axis left/right cameras with slider-controlled stereo strength and adjustable convergence. Slider zero uses one render for both eyes.
- 4x/8x/16x coarse previews, interlaced cell scheduling, and progressive refinement to 400x240 per eye. Camera motion refreshes the coarse scan continuously; releasing controls restarts a clean refinement.
- Optional full-resolution quality mode with doubled ray-step limit, tighter hit tolerance and 1/2/4 subpixel samples.
- Configurable iterations, bailout, distance estimator, derivative scale, step safety, ray steps, lighting, ambient occlusion, shadows, palettes, fog, exposure, camera and stereo geometry.
- New 3DS high-speed request and optional right-eye worker on CPU 2. If unavailable, it falls back to serial rendering. Parallel eye work is used at 4x or finer to avoid waking a worker for the smallest coarse jobs.
- Eight SD scene slots and PPM stereo-pair export. Saved scenes include custom expressions and all settings.

## Build / install

Install devkitPro's `3ds-dev` package (devkitARM, libctru and tools), then run:

```sh
make -j2
```

Copy `Re-fract.3dsx` and `Re-fract.smdh` into `sd:/3ds/Re-fract/` and launch through Homebrew Launcher on a homebrew-enabled console. The Actions **Build and test** workflow also builds a downloadable `Re-fract-3dsx` artifact when successful. The `Re-fract-3dsx-and-cia` Actions artifact also includes `Re-fract.cia` for installation on a homebrew-enabled New 3DS. Its HOME Menu banner is a textured 3D cube with an eight-second looping turn. The CIA requests New 3DS memory, 804 MHz CPU, L2 cache and access to CPU 2. Its application title ID is `000400000F7AC700`.

## Controls

| Control | Action |
| --- | --- |
| Circle Pad | Move forward/back and strafe |
| C-stick | Look |
| L / R | Turn left/right |
| ZL / ZR | Move down/up |
| Hold X | Move three times faster |
| D-pad up/down | Select a menu row; scrolls in pages of eight |
| D-pad left/right | Adjust value / change stage operation |
| A | Type an exact numeric value, edit an expression, or run selected action |
| Touch tabs | Switch editor tab |
| Touch middle of row | Select and activate |
| Touch left/right edge of row | Select and decrease/increase |
| SELECT | Next tab |
| Y | Toggle still quality mode |
| B | Reset camera to the preset's starting view |
| 3D slider | Set stereo strength |
| START | Exit |

Selecting a preset loads its formula and starting camera. Rendering/material settings are retained except far clip and convergence, which fit the new view. Scene edits restart rendering. Moving immediately exits still quality mode so controls remain usable.

**FORMULA** edits global iteration settings and adds stages. **STAGE** edits the selected stage, changes its operation, reorders, duplicates or deletes it. Changing an operation resets its parameters to useful defaults. **FILES** saves/loads `scene-0.rfs` through `scene-7.rfs` in `sd:/3ds/Re-fract/`; saving replaces that slot. Export replaces `image-N-left.ppm` and `image-N-right.ppm`. An export made before rendering completes contains the current partial frame.

## Presets and expressions

See [formula reference](docs/FORMULAS.md) for exact controls and expression syntax. Try **CORRIDOR BOX**, **SPHERE NETWORK** and **INVERTED BOX** when exploring the corridor / connected-bulb description. These are adjustable Mandelbox variations, not a confirmed identification of that reference.

## Performance and validation

This is a CPU distance-estimation ray marcher. Specialized built-in operations avoid the expression VM. Rotation matrices and camera basis are precomputed, integer bulb powers use multiplication, orbit traps and bailout checks use squared lengths, and normals use four distance evaluations. Retained image buffers allow partial updates without retracing untouched cells. Formula code and evaluation stacks have fixed limits; there is no expression parsing in the ray loop.

The render budget is **soft**: input is checked between stereo cells, and an expensive cell can exceed the selected budget. Custom expressions, high iteration counts, AO, shadow rays and supersampling cost significantly more. Interlacing lowers work per update but does not reduce the work required for a finished image. There is no promised frame rate; actual New 3DS timings must be measured on hardware. Old 3DS falls back to a 16x preview and serial rendering.

Desktop tests cover expression parsing/derivatives/domain errors, every preset's finite distance samples, scene serialization and failed-load isolation, stereo convergence, genuine eye differences, zero-slider identity and refinement completion. Run:

```sh
cmake -S . -B host-build -DCMAKE_BUILD_TYPE=Release
cmake --build host-build
ctest --test-dir host-build --output-on-failure
# Optional preset renders and UI preview in PPM:
./host-build/core_tests host-build/preset
```

Without CMake:

```sh
mkdir -p host-build
g++ -std=c++17 -O2 -Wall -Wextra -Werror -Iinclude source/expression.cpp source/formula.cpp source/renderer.cpp source/storage.cpp source/ui.cpp tests/core.cpp -o host-build/core_tests
./host-build/core_tests
```

The initial source was developed with desktop validation. Console build status is reported by Actions; startup, keyboard applets, stereo comfort, worker affinity and sustained performance still require hardware testing.

## Artwork and animated banner

The supplied eye artwork is used in `icon.png` (48x48 launcher icon) and `assets/icon-art.png` (256x256 cube texture). It is resized with Lanczos filtering without cropping. Every cube face carries the same image. The cube is genuine CGFX geometry with a native looping skeletal animation; the eight-second yaw curve is encoded directly to avoid quaternion/Euler discontinuities. Banner audio is silent.

GitHub Actions authors the cube with our `tools/build_banner.py`, converts it through a pinned [pycgfx](https://github.com/skyfloogle/pycgfx) checkout, packages the CGFX with [bannertool](https://github.com/Epicpkmn11/bannertool), then creates the CIA with [makerom](https://github.com/3DSGuy/Project_CTR). Converter source is downloaded at build time rather than redistributed in this repository. No Nintendo banner model or audio is included. The `Re-fract-banner` artifact includes the model, animation, banner and manifest for inspection.

To build locally on Linux x86_64 (Python 3.12 and the 3DS toolchain required):

```sh
python3 -m pip install gltflib==1.0.13 Pillow==11.3.0
git clone https://github.com/skyfloogle/pycgfx.git .tools/pycgfx
git -C .tools/pycgfx checkout 1f78850086f3a77c41e07162e842f97a5bf3c18a
bash tools/install_cia_tools.sh
python3 tools/build_banner.py --converter .tools/pycgfx
.tools/bin/bannertool makebanner -ci build/banner/cube.cgfx -a build/banner/silence.wav -o build/banner/cube.bnr
make cia MAKEROM=.tools/bin/makerom
```

The model's dimensions fit the HOME Menu banner camera, and the generator checks the 512 KiB CGFX size limit. The actual HOME Menu appearance and looping playback still need console validation. No additional flat banner image is needed. The in-app UI texture remains procedural.

The project currently has no assigned license. Choose one before distributing code under a specific license.
