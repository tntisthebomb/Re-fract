# Re-fract

A customizable stereoscopic fractal laboratory for New Nintendo 3DS homebrew, with a charcoal, rust, scratched-metal UI inspired by mid-2000s grunge software.

## Features

- 36 presets across Mandelbulb, Mandelbox, Julia, Menger, Sierpinski and hybrid families. Some entries are parameter variations, not separate mathematical families.
- Optional infinite periodic world repetition with separate X/Y/Z spacing; zero spacing leaves that axis unwrapped.
- Up to 12 editable formula stages: folds, spherical power, scaling, rotations, offsets, absolute/sort operations, Menger and tetrahedral transforms, and custom expressions.
- Separate off-axis left/right cameras with slider-controlled stereo strength and adjustable convergence. Slider zero uses one render for both eyes.
- 4x/8x/16x coarse previews, interlaced cell scheduling, and progressive refinement to 400x240 per eye. Camera motion refreshes the coarse scan continuously; releasing controls restarts a clean refinement.
- Optional full-resolution quality mode with doubled ray-step limit, tighter hit tolerance and 1/2/4 subpixel samples.
- Configurable iterations, bailout, distance estimator, derivative scale, step safety, ray steps, lighting, ambient occlusion, shadows, palettes, fog, exposure, camera and stereo geometry.
- New 3DS high-speed request and optional CPU 2 worker. Both CPUs claim jobs from a shared queue in mono and stereo modes, with automatic serial fallback. Batch size adapts to measured work cost.
- Completed single-sample images support trace-free gradient edits, with parallel recoloring at fine resolution and one color evaluation per coarse block. Polynomial expression Julia formulas have direct derivative kernels; edited formulas retain the general interpreter.
- Zoom-aware hit precision and optional motion/detail iteration limits; an experimental open-space distance cache for moving previews.
- Optional one-bounce indirect lighting with sky color and bounded secondary tracing.
- Two configurable RGB point lights, optional point shadows, sampled depth of field and stationary progressive lighting.
- On-console frozen-view benchmarks with CSV exports and separate moving-preview tests.
- Optional GPU surface-cache navigation: capture a completed traced view, then use PICA200 triangles/depth testing and independent stereo projections to move without retracing. Hidden geometry is absent; lighting/colors are baked. Resume CPU rendering to refresh the cache, or enable automatic recapture after movement stops.
- Optional algebraic Mandelbulb math for powers 2/4/8/16 with standard angular multipliers. Quality mode retains the original trigonometric path.
- Eight SD scene slots and PPM stereo-pair export. Saved scenes include custom expressions and all settings.

## Build / install

Install devkitPro's `3ds-dev` package (devkitARM, libctru and tools), then run:

```sh
make -j2
```

Copy `Re-fract.3dsx` and `Re-fract.smdh` into `sd:/3ds/Re-fract/` and launch through Homebrew Launcher on a homebrew-enabled console. The Actions **Build and test** workflow also builds a downloadable `Re-fract-program` artifact when successful. The `Re-fract-3dsx-and-cia` Actions artifact also includes `Re-fract.cia` for installation on a homebrew-enabled New 3DS. Its HOME Menu banner is a textured 3D cube with an eight-second looping turn. The CIA requests New 3DS memory, 804 MHz CPU, L2 cache and access to CPU 2. Its application title ID is `000400000F7AC700`.

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

**COLOR** contains named palettes, editable start/end colors (six hexadecimal RGB digits, e.g. FF8040), gradient scale/offset/repetition, a gradient preview strip, and lighting. Editing an endpoint enables the custom gradient. D-pad down scrolls through additional pages; the page count appears above the rows.

**RENDER** includes surface-aware movement, enabled by default. Speed scales with the distance estimate at the camera, reaching full speed at SLOWDOWN DISTANCE. MIN SPEED FRACTION keeps movement possible on/inside a surface. The setting affects forward, sideways and vertical movement; look speed is unchanged. X multiplies the resulting speed. This is navigation assistance, not collision detection, and custom formula distance estimates may be conservative or inaccurate.

For the experimental GPU mode, enable **RENDER → GPU SURFACE CACHE**, allow a stationary single-sample render to finish, then select **CAPTURE GPU SURFACE**. Final render block size must be no larger than GPU MESH SPACING; disable adaptive tile skipping for capture. Move and adjust stereo normally. **RESUME CPU RENDER** traces the current view again; scene edits also return to CPU rendering. GPU NEAR CLIP controls close-surface clipping, and GPU EDGE REJECTION trades fewer holes for more triangles across uneven depth. FILES reports triangle count and GPU drawing time. PPM exports require a fresh CPU render of the current camera. GPU speed/appearance and switching still need hardware validation.

**FORMULA → ALGEBRAIC BULB EXP** is off by default. It replaces inverse trigonometry and sine/cosine with repeated angle doubling for supported bulbs; fractional powers and altered theta/phi multipliers retain the generic path. Small floating-point differences can amplify during repeated iteration. Quality mode bypasses this experiment.

**FORMULA** edits global iteration settings and adds stages. **STAGE** edits the selected stage, changes its operation, reorders, duplicates or deletes it. Changing an operation resets its parameters to useful defaults. **FILES** saves/loads `scene-0.rfs` through `scene-7.rfs` in `sd:/3ds/Re-fract/`; saving replaces that slot. Export replaces `image-N-left.ppm` and `image-N-right.ppm`. An export made before rendering completes contains the current partial frame.

## Presets and expressions

See [formula reference](docs/FORMULAS.md) for exact controls and expression syntax. Try **CORRIDOR BOX**, **SPHERE NETWORK** and **INVERTED BOX** when exploring the corridor / connected-bulb description. These are adjustable Mandelbox variations, not a confirmed identification of that reference.

## Performance and validation

This is a CPU distance-estimation ray marcher. Specialized built-in operations avoid the expression VM. Active stages, sphere-fold constants, angular multipliers, rotation matrices and camera basis are precomputed, integer bulb powers use multiplication, orbit traps and bailout checks use squared lengths, and normals use four distance evaluations. Retained image buffers allow partial updates without retracing untouched cells. Geometry-only distance queries skip orbit coloring; coloring is evaluated at the hit. Constant expression subtrees are folded at compile time. Formula code and evaluation stacks have fixed limits; there is no expression parsing in the ray loop.

See [optimization controls and limitations](docs/PERFORMANCE.md). Moving previews can use cheaper orbit coloring without normal, AO or shadow queries. Experimental reprojection, cross-eye reuse, adaptive tiles, fixed center detail and relaxed marching are individually configurable and default off. Quality mode disables all of these approximations. The target refresh rate guides adaptive block size; it does not guarantee that rate.

The render budget is **soft**: input is checked between bounded batches, and an expensive batch can exceed the selected budget. Custom expressions, high iteration counts, AO, shadow rays and supersampling cost significantly more. Interlacing lowers work per update but does not reduce the work required for a finished image. There is no promised frame rate; actual New 3DS timings must be measured on hardware. Old 3DS falls back to a 16x preview and serial rendering.

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
g++ -pthread -std=c++17 -O2 -Wall -Wextra -Werror -Iinclude source/expression.cpp source/formula.cpp source/renderer.cpp source/storage.cpp source/ui.cpp tests/core.cpp -o host-build/core_tests
./host-build/core_tests
```

The initial source was developed with desktop validation. Console build status is reported by Actions; startup, keyboard applets, stereo comfort, worker affinity and sustained performance still require hardware testing.

## Artwork and animated banner

The supplied eye artwork is used in `icon.png` (48x48 launcher icon) and `assets/icon-art.png` (256x256 cube texture). It is resized with Lanczos filtering without cropping. Every cube face carries the same image. The cube is genuine CGFX geometry with a native looping skeletal animation; the eight-second yaw curve is encoded directly to avoid quaternion/Euler discontinuities. Banner audio is silent.

GitHub Actions authors the cube with our `tools/build_banner.py`, converts it through a pinned [pycgfx](https://github.com/skyfloogle/pycgfx) checkout, packages the CGFX with [bannertool](https://github.com/Epicpkmn11/bannertool), then creates the CIA with [makerom](https://github.com/3DSGuy/Project_CTR). Converter source is downloaded at build time rather than redistributed in this repository. No Nintendo banner model or audio is included. The `Re-fract-banner` artifact includes the model, animation, banner and manifest for inspection.

To build locally on Ubuntu 24.04 x86_64 (Python 3.12 and the 3DS toolchain required; the released makerom needs glibc 2.38 or newer):

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

## Corridor and lighting pass

The twelve new presets include Ruby Chambers, Blue Sphere Vault, Gold Box Corridor, Menger Colonnade, Twisted Box Hall, Inverted Bubble Hall, Tetra Gallery, Julia Bulb Arcade, Bulb Garden, Absolute Bulb / 5, Box-Bulb / 4 and Negative Box / Deep. They provide starting camera positions and color/render settings. The corridor presets use editable periodic worlds; they approximate the attached styles rather than reconstructing the original formulas or materials. Loading a new preset applies its settings, including turning indirect lighting off.

For close-ups, enable **ZOOM PRECISION EXP**; try **MIN HIT EPSILON 0.000001** and **PIXEL TOLERANCE 0.25**. **ADAPT DETAIL EXP** optionally reduces moving iterations and adds stationary detail near the camera. These controls can increase rendering cost and change the surface; they are not an unlimited deep-zoom solution.

Under COLOR, **INDIRECT SAMPLES 1** enables a bounded diffuse-bounce/skylight approximation. Start with **INDIRECT STEPS 16**, **INDIRECT STRENGTH 0.5**, **INDIRECT RANGE 2**. Set samples to zero to disable it. Four samples are slower; quality samples multiply this work. Fast moving lighting skips indirect tracing.

Under FILES, run **BENCH CURRENT VIEW** or **BENCH MOVE PREVIEW**. Each evaluates a frozen 1,500-position mono grid, honors the selected quality/sample settings, and writes `sdmc:/3ds/Re-fract/benchmark-N.csv` for the selected scene slot. The benchmark pauses normal CPU rendering. It reports active tracing throughput, not displayed FPS. Use the moving benchmark with a completed distance cache to compare that experiment.

Scenes now save as format v6 and still load v1–v5. Previous app versions cannot load v6 scenes. See [PERFORMANCE.md](docs/PERFORMANCE.md) for limits and hardware checks.

## Point lights, depth of field and progressive lighting

**COLOR → EDIT POINT LIGHT** selects one of two lights. Enable **POINT LIGHT ENABLED**; adjust position, color, intensity and range. **CAMERA RELATIVE LIGHT** makes X/Y/Z offsets mean right/up/forward from the camera (default position 0,1,0). Turn it off to use world coordinates. **POINT SHADOWS** uses RENDER → SHADOW STEPS; start with 16. **SUN STRENGTH** controls the existing directional light; a small value helps local lights stand out. Fast moving lighting skips all surface lighting; disable that shortcut if you need lit navigation. GPU captures bake point lighting and need recapture after movement, including camera-relative lights.

**RENDER → DEPTH OF FIELD** enables a sampled thin lens while stationary. **LENS APERTURE** is the lens radius in world units: start at 0.03–0.05. **FOCUS DISTANCE** is forward depth from the camera, not distance along a slanted ray. **LENS SAMPLES 4** is a starting point; more samples smooth blur and cost more rays. **FOCUS AT CONVERGENCE** matches the stereo convergence distance. Movement uses pinhole rays. Depth of field cannot be captured into the GPU surface cache.

**COLOR → PROGRESSIVE LIGHTING** adds repeated stationary passes with varied indirect-light samples and floating-point accumulation before display clipping. Enabling it in the menu sets INDIRECT SAMPLES to 1 if they were zero. Start with **LIGHTING PASSES 8–16**. It also improves lens sampling when depth of field is active. Camera movement, scene edits and stereo changes restart accumulation. FILES shows completed lighting passes; rendering stops at the selected limit. This remains the existing bounded diffuse-bounce approximation, not full path tracing. GPU capture and cached material recoloring are disabled for accumulated images.

These features are off by default and can increase rendering time substantially. The CIA/3DSX builds are tested; final performance and stereo comfort still require console testing. Run the portable `optics_preview` target with an output prefix to reproduce point-light, lens and progressive examples.

### Color and screenshot update

COLOR now offers a touch hue/saturation wheel with brightness, 2–5 gradient stops,
a bounded orbit mapping and Auto Fit Gradient. Color Emission and optional stationary
Bloom Halo make materials glow. FILES displays the most recent completed resolution
pass time and retains the last realtime sweep time/scale independently of still renders.
Progressive lighting defaults to a 4X lighting block rather than waiting for full resolution;
choose 8X for faster feedback or 1X/Quality for a finished image. Numerical ranges are
substantially wider; mathematical and memory constraints still apply.

START saves JPEG screenshots and, with active stereo, a 3D MPO in
`sdmc:/3ds/Re-fract/screenshots/`. Files include a top-only JPEG and a JPEG of both
screens. No Camera album registration is required. FILES -> Exit App replaces START
as the exit action. Scene files save as v7 and read v1–v6. See docs/PERFORMANCE.md.

Sky Color now changes the visible background and fog. For a faster stationary image,
set RENDER -> STILL BLOCK 2, AUTO REFINE ON, QUALITY MODE OFF and UPSCALING BICUBIC.
Use LIGHTING BLOCK 2 too if progressive lighting is enabled. Screenshots preserve the
upscaled stereo output. Display settings save in scene format v8; old scenes still load.

Optional RENDER -> ADAPT EMPTY SPACE sparsely probes confirmed sky during stationary
refinement/progressive passes. Try SKY PROBE EVERY N 8; motion and Quality Mode stay
dense. This saves void rays but can miss tiny geometry, so it defaults off. Saves are v9.
