# Performance controls

The renderer is CPU based. The New 3DS CPUs consume a shared queue within each batch rather than waking once per stereo cell. A core finishing a cheap ray can immediately claim another, avoiding the fixed half-batch split when ray costs differ. Mono rendering can use both CPUs too. Results are joined before scene edits or image writes. Formula edits rebuild cached constants and the enabled-stage list. Geometry queries omit orbit-trap calculations; one coloring query is made at a hit. Custom expressions fold finite constant subexpressions, preserving invalid-domain errors.

## Stereo slider and image transfer

Stereo enable permits 3D; the physical slider and nonzero eye separation determine whether it is active. The 2D stop uses one eye, a 2D display framebuffer, and a one-eye adaptive cost estimate. Small slider readings below 2% are treated as off, with a 4% activation threshold and filtering of small strength fluctuations. Changing the stereo enable setting at the 2D stop preserves the render. Active slider adjustments use a continuous coarse scan, then restart clean refinement after 250 ms without a strength change because the eye rays have changed.

Mono exports share the same retained image rather than copying every rendered pixel into a second buffer. Each physical top-screen double buffer is copied only when its cached image revision changes; switching 2D/3D invalidates that transfer cache. Completed images avoid repeated RGB conversion and CPU framebuffer copies.

## Defaults and practical controls

- **BATCH SIZE**: maximum 1–32 jobs per batch; the time controller can lower this. Stereo needs at least two jobs. An expensive job can still exceed the soft frame budget.
- **ADAPT RESOLUTION**: moving previews choose 4–32 pixel blocks based on measured ray cost. **TARGET REFRESH FPS** is a target for coarse image refresh, not a promised display or fractal frame rate. Manual PREVIEW BLOCK applies when disabled. Releasing movement begins clean refinement.
- **FAST MOVE LIGHTING**: uses orbit color and fog while moving, skipping normal, AO, shadow and specular calculations. Geometry is unchanged; lighting returns when movement stops.
- **FILES** shows cumulative rays, distance queries, reused cells and batches. RESET COUNTERS clears those totals.
- **SURFACE SLOWDOWN**: scales translation by `clamp(distance / slowdownDistance, minimumFraction, 1)`. Enabled by default; the base move speed and X boost still apply. It does not alter turning speed or guarantee collision avoidance.

## Experimental controls (default off)

| Control | Mechanism | Tradeoff |
| --- | --- | --- |
| TEMPORAL EXPERIMENT | Reproject a bounded ring of recent traced surface points into the new camera, depth-test their splats, and trace uncovered cells | Holes, stale lighting, stretched splats and missed occluders are possible. Use small camera movements. |
| STEREO REUSE EXP | With temporal enabled, project each eye's points into the other eye | Can reduce work but introduce incorrect occlusion or stereo artifacts. Disable for accurate stereo. |
| HISTORY FRAMES / REFRESH EVERY N | Expire old points and force periodic fresh cell samples | Longer history and less frequent refresh save work at the cost of stale detail. N=1 always retraces. |
| ADAPTIVE TILES EXP | During fine still-preview passes, keep cells whose surrounding depth and color agree; sample every eighth cell regardless | Thin geometry and small details can be missed. This is a heuristic, not a mathematical error bound. |
| FIXED FOVEATION EXP | During movement, use twice the block width outside a fixed central region | Coarser peripheral detail; there is no eye tracking. |
| RELAXATION EXP | Multiply march advances by up to 1.5; if consecutive estimated spheres fail the overlap check, backtrack to the conservative step | Extra fallback queries can be slower. Arbitrary fractal estimators are not certified distance bounds; artifacts remain possible. 1 disables it. |

**Quality mode uses fresh independent eye rays, ordinary step sizes, full lighting and no reuse or tile skipping.** Camera movement exits quality mode. Formula, color, slider and other scene changes clear temporal history. Reprojected points never become new history records; only freshly traced hits do.

## Validation and measuring

`core_tests` checks all presets, geometry/color distance agreement, batched/serial image equality, stereo projection, temporal invalidation, quality bypass, gradient behavior, camera slowdown, and v1/v2 scene compatibility. CI also runs address/undefined-behavior sanitizers and compiles the actual 3DS application.

`core_benchmark` traces the same 1,500 rays for Mandelbulb, Mandelbox, Menger and expression Julia, printing milliseconds and an image checksum. Pass a filename to write raw RGB samples for comparison. Compare builds with identical compiler flags and run several repetitions; desktop time is not a New 3DS FPS estimate. Timing instrumentation and an extra hit-color query can offset savings for some formulas. No universal speedup is claimed.

A depth-image GPU surface cache is implemented as an opt-in experiment (see below). Full volumetric mesh extraction and GPU ray marching remain unimplemented. Hardware performance has not been measured.

Scene format v4 saves GPU cache controls and the algebraic-bulb toggle; it still reads v1/v2/v3 scenes. Older application builds cannot read v4 saves.

## GPU opportunities

The current application still evaluates fractal geometry on the CPU. PICA200 has programmable vertex/geometry processing and a configurable, non-programmable fragment stage. A conventional per-pixel distance-estimator ray-marching shader is therefore unavailable.

GPU backend options:

- **Cached surface mesh**: sample/extract geometry on CPU, then use the GPU for stereo projection, triangles, depth testing, and lighting. Camera movement over cached geometry can avoid most repeated fractal evaluation. Formula edits and exploration outside the cache rebuild it; finite mesh resolution loses small detail.
- **Depth-image mesh (implemented, experimental)**: capture a completed CPU view, triangulate its samples, then rasterize the fixed cache from both eye cameras. Hidden surfaces are absent; depth jumps are rejected. It must be refreshed with CPU rendering.
- **Preview scaling and UI drawing**: render fewer CPU samples, upload a texture, and let the GPU enlarge/filter and composite it. This saves transfer/compositing work; it does not make each fractal evaluation cheaper. Upload and synchronization costs need hardware measurement.

Primary references: [3DS fragment pipeline](https://3dbrew.org/wiki/Nintendo_OpenGL), [devkitPro textured GPU example](https://github.com/devkitPro/3ds-examples/blob/master/graphics/gpu/textured_cube/source/main.c).

## Infinite world repetition

**FORMULA → WORLD REPEAT** wraps the sampled world position into a centered periodic cell before fractal iteration. It repeats the complete fractal, rather than inserting a modulo operation inside every fractal iteration. X/Y/Z spacing is independent; zero leaves an axis unwrapped. Start NEGATIVE BOX with spacing 16 on X/Z and 0 on Y for an endless horizontal arrangement, or 16 on every axis for a volume of copies.

There is no outermost tile, but FAR CLIP and RAY STEPS still limit visible distance and work. Spacing that is too small crops the original shape at cell boundaries; increase it if seams/cut surfaces appear. Ray advances stop at cell boundaries before evaluating the next cell, and experimental step relaxation is bypassed for repeated worlds. This does not replace a distance estimator with a certified signed-distance field.

## Measured CPU optimization pass

Common Mandelbox and tetrahedral stage sequences now select straight-line kernels at formula validation. All stage parameters remain editable; extra transforms use the generic interpreter. The specialized and generic paths share the same terminal-distance rules and preserve intermediate validity checks and orbit traps. Differential tests include Julia mode, repetition, disabled stages and added rotations.

Shading skips specular math when its strength is zero, stops AO once its final clamped minimum is reached, and omits shadow queries when neither diffuse nor specular can contribute. Menu callbacks and displayed values are retained until edits, tab changes or relevant camera updates; the FILES telemetry refreshes four times per second. **UI FRAME / TRACE MS** reports actual application-frame and CPU trace durations, not completed-fractal FPS.

Desktop measurements compare commit `8ff163b9e2a7874e4781cfd33c2040a98250c05a` with this pass. Both binaries use GCC `-O3`, identical sample coordinates and 1,500 rays per preset. The script alternates binaries, discards two warmups, takes ten-run medians, and rejects changed pixel checksums. The lit workload uses six AO samples, 32 shadow steps and zero specular. Results are in [default workload](benchmark-desktop-default.csv) and [lit workload](benchmark-desktop-lit.csv). Preset IDs are 0 Mandelbulb, 6 Mandelbox, 7 Negative Box, 10 Menger, 12 tetrahedral and 16 expression Julia.

These are host timings, not New 3DS measurements. Tiny changes can be scheduling noise. The native UI/cache changes are not included in the portable benchmark. Broad LTO and math-errno flag experiments did not produce consistent improvements and were not enabled. Mandelbulb and Menger specializations were also not retained after comparison.

Reproduce a comparison with two builds of `tests/benchmark.cpp`:

```sh
python tools/compare_benchmarks.py path/to/before path/to/after --runs 10
python tools/compare_benchmarks.py path/to/before path/to/after --runs 10 --lit
```

## Repurposing programmable GPU stages

A separate experimental approach would calculate one ray sample per **vertex shader invocation**, then expand the sample into a small screen-space primitive with a geometry shader. Start with a 100x60 sample grid and a fixed Mandelbox kernel: box folding, sphere inversion and scaling fit the GPU's arithmetic better than a general expression evaluator. Supply camera/eye parameters as uniforms, cap ray steps and fractal iterations, and preserve the CPU quality path. The geometry shader can emit triangles, as demonstrated in devkitPro's [geometry example](https://github.com/devkitPro/3ds-examples/blob/master/graphics/gpu/geoshader/source/program.g.pica).

This is a proposed backend, not a shipped GPU renderer or a measured speedup. PICA shader storage, registers, flow control and reduced floating-point precision constrain it; primitive generation and divergence can erase expected gains. See the reverse-engineered [instruction set](https://3dbrew.org/wiki/Shader_Instruction_Set).

The fragment stage exposes fixed combiners and lighting configuration, rather than arbitrary executable fragment programs. Those operations can assist compositing and shading of supplied surfaces. Emulating an iterative distance estimator through many render-to-texture passes would incur repeated writes/reads and constrained state precision; it is a less promising first prototype than the vertex-sampling approach. The available combiner configuration is visible in [citro3d's API](https://github.com/devkitPro/citro3d/blob/master/include/c3d/texenv.h).

### Material cache
Completed stationary single-sample renders retain orbit trap, depth, and lighting at final-pass block anchors (the allocated cache is about 4.6 MB for both eyes). COLOR palette, gradient, exposure and fog edits reuse this cache without distance evaluations. Camera, formula, lighting and specular edits restart rendering. Motion, adaptive tile skipping and multisampled quality renders cannot use this shortcut. The cache preserves existing background/error pixels. It does not accelerate camera movement or geometry tracing.

## Parallel-loop and hardware-math pass

Independent rays already run through the two-core shared queue. Fine-resolution material recoloring now uses a parallel-for over disjoint block rows, backed by the same persistent worker. Both consumers join before edits, image transfer or queue reuse. Coarse recoloring stays on one CPU to avoid waking a worker for very little work. Sequential fractal iterations, expression stack instructions and march steps depend on previous values and cannot be converted to independent parallel iterations. Small memory fills stay local; starting parallel jobs for those would add synchronization to every batch.

The expression compiler recognizes the bytecode for `x*x-y*y-z*z+cx`, `2*x*y+cy`, and `2*x*z+cz`, and selects direct dual-number kernels. These preserve operation order, all six derivatives and intermediate overflow checks. Whitespace is immaterial; changes to the compiled operations fall back to the ordinary interpreter. Native-vs-interpreter tests cover individual derivatives, overflow, Julia/non-Julia use and rendered output.

Default previews no longer write unused depth/age buffers. The material cache writes one record per block instead of one per replicated pixel; coarse recoloring evaluates fog and gradient once per block. Pixel rows use contiguous fills. Per-ray profiling uses bounded 32-bit local counters, then publishes the 64-bit totals at completion. Idle panels and both bottom-screen double buffers are cached, and only newly written framebuffers are flushed to GSP. This follows [libctru's framebuffer/flush implementation](https://github.com/devkitPro/libctru/blob/master/libctru/source/gfx.c).

The console build explicitly targets VFPv2 with `-mfpu=vfp` and enables `-fno-math-errno`, permitting hardware square root without errno handling. Domain and finite-result validation remain enabled; there is no `-ffast-math`. Number parsing still checks `strtof`/`strtol` errno. ARM11 does not have the NEON floating-point SIMD extension: forcing NEON or desktop SIMD intrinsics into this build would generate unsupported instructions. VFP short-vector mode is not NEON and is not enabled: changing global floating-point state across compiled C++/libm calls would require a separate assembly kernel and careful hardware measurement. See [Arm's compiler guide](https://documentation-service.arm.com/static/5eb946b50f1c1e0dae6ee21e).

Host comparisons against `74024719e30a9718beefb395618a626ef22a30d3` use the existing alternating ten-run benchmark and reject changed pixel checksums. Results are in [default workload](benchmark-optimization-default.csv) and [lit workload](benchmark-optimization-lit.csv). The polynomial expression Julia is substantially faster on the host. Small timing differences for other formulas are not evidence of a console speedup. The benchmark measures tracing; panel caching, buffer writes and parallel recoloring are additional changes outside its timed workload. Attempted tiled and reversed framebuffer conversions were slower on the host and were discarded. The tested scalar conversion is retained.

This pass completes CPU/cache work within the existing backend. Volumetric mesh extraction and vertex-sampling ray-marching backends remain unimplemented; the next pass below adds a depth-image surface cache. No GPU speedup or globally optimal implementation is claimed. Actual frame budgets, worker scaling, VFP gains, display-cache behavior and stereo comfort still require New 3DS testing. Existing approximate modes remain opt-in.

## GPU surface-cache navigation

Enable **RENDER → GPU SURFACE CACHE**, finish a stationary single-sample render, then choose **CAPTURE GPU SURFACE**. Capture rejects partial/moving renders, multisampled quality renders, adaptive-tile results, empty meshes and source blocks larger than GPU MESH SPACING. Grid positions and colors come from the completed first-eye view. Independent grid rows use the persistent parallel worker; triangle assembly stays sequential. At spacing 4 the cache has at most 6,000 vertices and 11,682 triangles, stored with 16-bit indices.

The GPU uses a small PICA vertex shader to transform positions and pass vertex colors, fixed fragment combiners to display those colors, and hardware depth testing/rasterization. It renders left/right projections separately only when stereo is effectively active. GPU navigation does not call the CPU ray renderer. Camera slowdown still evaluates one distance estimate for navigation. Capture coordinates are relative to the original camera, limiting float24 cancellation at large world positions.

GPU MESH SPACING controls the sampling grid (4/8/16). GPU EDGE REJECTION bounds depth changes within a triangle: smaller values create more holes, larger values risk bridges across disconnected surfaces. GPU NEAR CLIP controls near-plane clipping separately from the CPU ray's hit epsilon. FILES shows mesh triangle count and GPU DRAW MS. This is GPU draw duration, not full application frame time or full-fractal refresh rate.

Use **RESUME CPU RENDER** to trace the new viewpoint, then capture again. Scene/formula/material edits automatically return to CPU rendering. Stereo-slider changes update the GPU projection without tracing. Image exports require CPU rendering of the current view; the app rejects exports of the stale capture viewpoint while navigating. The scene file stores cache settings, not the mesh or active navigation state. Allocation/setup failures retain a CPU fallback.

This is a finite visible-surface cache, not a volumetric fractal mesh. Newly exposed surfaces and the back of the object are missing, vertex colors interpolate, and lighting/fog/specular are baked into the capture. A new stereo eye can reveal holes because only the first eye supplied geometry. Moving far from the capture requires a new trace. The backend is compiled and its CPU projection/topology tested, but actual GPU rendering, display orientation, switching, sleep/resume, low-memory behavior and performance require hardware validation. It remains off by default.

Implementation follows devkitPro's [GPU triangle example](https://github.com/devkitPro/3ds-examples/blob/master/graphics/gpu/simple_tri/source/main.c), Citro3D's [frame/transfer lifecycle](https://github.com/devkitPro/citro3d/blob/master/source/renderqueue.c), and [tilted perspective/depth convention](https://github.com/devkitPro/citro3d/blob/master/source/maths/mtx_persptilt.c). Uploaded buffers are flushed once; each GPU frame explicitly flushes its command list instead of all linear memory. CPU and GPU top-screen swaps are kept separate; previous GPU work is synchronized before changing display mode or returning to CPU output.

## Algebraic bulb experiment

**FORMULA → ALGEBRAIC BULB EXP** uses normalized meridian/azimuth components and repeated complex angle doubling for standard powers 2, 4, 8 and 16. Both angular multipliers must be 1. Radial exponent/derivative tracking remain unchanged. Unsupported/custom powers and angle multipliers use the original implementation. Quality distance and coloring queries explicitly bypass the algebraic path.

The [host benchmark](benchmark-algebraic-bulb.csv) alternates the two modes over ten measured repetitions after two warmups, taking medians for 1,500 shaded rays per preset. Sampled Mandelbulb 8/2 and Julia Bulb traces take approximately 51–61% less desktop time. Mandelbulb 8 differs at 3 of 1,500 pixels by at most one channel value; the other two sampled images match. These measurements do not establish New 3DS gains or bounds on arbitrary zooms. Repeated iteration and finite-difference shading can amplify floating-point differences. The experiment is off by default.

Six additional presets bring the browser to 24 entries. Their editable operation chains are documented in FORMULAS.md. PPM exports now write the packed RGB image in one buffered operation rather than one call per pixel.

## Corridor, lighting and preview experiments

### Zoom precision and detail

ZOOM PRECISION EXP bounds hit tolerance by the world-space pixel footprint (`2*t*tan(FOV/2)/240 * PIXEL TOLERANCE`), with MIN HIT EPSILON as a lower bound and the original tolerance as a ceiling. It also shrinks normal-sampling offsets; those offsets remain large enough to represent coordinate changes in float. Defaults retain the original path. The editable HIT EPSILON lower limit is now 1e-6. Smaller tolerances can require many more march steps or reveal missed rays; this is a detail control, not a guaranteed speedup.

ADAPT DETAIL EXP uses at most MOVE ITERATIONS during movement. At rest, camera proximity adds one iteration per approximate halving below 0.25 world units, up to DETAIL ITERATION CAP (never below the base formula iterations). It leaves the saved formula iteration count unchanged. Camera proximity is only a heuristic: a distant target viewed with narrow FOV does not automatically receive extra iterations. Changing iterations changes geometry, so motion/stationary transitions can pop. It remains off by default and is incompatible with the distance cache, which is automatically bypassed.

### Sampled indirect lighting

COLOR → INDIRECT SAMPLES (0–4) controls a deterministic hemisphere sampling approximation. Secondary rays use INDIRECT STEPS (4–64) and INDIRECT RANGE (0.1–10). A secondary hit contributes its orbit-gradient color under approximate direct diffuse lighting; an unobstructed ray reaching the range limit contributes SKY COLOR. Exhausted or invalid rays contribute no light. INDIRECT STRENGTH scales the average contribution. The implementation has no recursion, stochastic temporal noise or further bounces. It omits secondary shadow testing, physical BRDF normalization, reflections and light transport beyond the finite range. It is an artistic one-bounce approximation, not an unbiased path tracer or full global illumination.

Indirect tracing preserves the primary geometry and records secondary distance queries. Fast moving lighting skips it. Quality antialiasing samples repeat the lighting calculation. RGB indirect illumination is retained in shade records and baked into GPU captures. Material recoloring restarts tracing when indirect samples are enabled, because the cached bounce colors would otherwise become stale. Samples default to zero; start with one, 16 steps, strength 0.5 and range 2. More samples usually cost more time.

### Experimental 3D distance cache

DISTANCE FIELD EXP builds a 33³ grid (35,937 distances, about 140 KiB) centered on the camera. FIELD GRID SPACING sets its sample spacing and extent. Four independent rows are built per UI frame, sharing the existing worker. The cache is only used once fully built; exact rendering continues while it warms. Formula/settings edits or moving out of its central region rebuild it. Adaptive iteration changes bypass it.

Only fast moving previews use the grid. A query far from a sampled surface uses the nearest stored estimate minus sample offset; queries within two grid spacings of sampled geometry, outside the grid or with invalid samples use the formula. All stationary, quality, coloring and normal queries retain formula evaluation. Arbitrary fractal distance estimators do not guarantee the Lipschitz behavior assumed by this approximation. Thin features, seams and overestimated distances can produce incorrect moving previews. The final stationary image does not use the approximation. This is not an occupancy proof or a volumetric mesh.

FILES shows cache progress. BENCH MOVE PREVIEW retains a completed cache snapshot when available; BENCH CURRENT VIEW bypasses it. Both freeze the scene, evaluate 1,500 mono sample positions in bounded batches, use the persistent worker when enabled and time active work with hardware ticks. QUALITY SAMPLES can produce more than 1,500 rays. CSV includes tracing time, rays/second, distance queries, checksum and major settings. Benchmark timing excludes UI, VBlank, image transfers and cache construction; it is not complete-scene FPS. CSV uses the selected scene slot and is replaced by the next benchmark for that slot. A benchmark pauses CPU image tracing and can be canceled from FILES.

The host experiment benchmark is in `tests/experiment_benchmark.cpp`; build its CMake target and run it to regenerate `benchmark-experiments.csv`. It takes five measured repetitions after one warmup for each grouped mode, so small timing differences are noisy. Cache construction is timed separately. Cached-preview changed-pixel counts and maximum channel differences are recorded; changed checksums are expected and must not be treated as lossless gains. Construction cost must be amortized across reused views. These desktop samples do not establish console speedups.

### GPU automatic recapture

Enable GPU AUTO RECAPTURE before capturing a completed single-sample view. While navigating, the GPU displays the finite old surface and the CPU renders the current view within the frame budget. Once movement stops and the new view finishes, a fresh mesh replaces the old one. This is full-view recapture, not selective hole filling, merging of multiple captures or concurrent independent CPU threads beyond the existing worker. Holes and baked lighting remain visible until replacement. More CPU work can lower navigation responsiveness. Shaders, targets and fixed-capacity vertex/index buffers are now reused between captures; GPU work is synchronized before buffer overwrites. Failure falls back to CPU display.

### Hardware checks and remaining research

Portable regression checks, serial/parallel benchmark agreement, exact-path image comparisons, ASan/UBSan and cross-compilation cover the new code. Test on New 3DS: cache warmup and recentering, moving cache artifacts, near-surface precision/detail transitions, indirect sample costs, repeated stereo GPU recaptures, edits during navigation, benchmark cancel/save, and sleep/resume. GPU execution remains unverified here. Scenes save as v6 and read v1–v5; older apps cannot read v6.

Vertex-shader ray marching, adaptive volumetric mesh extraction, predictive rendering, mathematically certified spatial bounds and self-similar geometry reuse remain research proposals. They were not added as nonfunctional options. They need dedicated formula/backend designs and hardware validation, and have no established benefit on this device. ARM11 still lacks NEON; this pass does not claim SIMD acceleration.

In the recorded cache samples, 0–513 of 1,500 moving-preview pixels changed, with maximum channel difference up to 69/255. Some scene timings were effectively unchanged. Keep this experiment off if those artifacts outweigh its modest potential benefit.

The reuse counter combines historical-ray reuse with distance-cache query hits; those are different units. Use the dedicated CSV query counts when comparing the distance-field experiment.


## Point lights, thin-lens optics and progressive accumulation

Two optional RGB point lights use a finite-range inverse-square-like falloff: intensity times `(1-distance/range)^2 / (1+distance^2)`. Camera-relative positions are resolved once per view in the right/up/forward basis; world positions remain fixed. Direct diffuse and specular contributions are cached separately. SUN STRENGTH scales the directional diffuse/specular terms while retaining the existing ambient term. Optional point shadows march toward each light using SHADOW STEPS, stop at periodic-cell boundaries, and treat exhausted rays as occluded. They can be expensive and can darken surfaces when the step limit is insufficient. Secondary diffuse bounces can receive point-light contributions, with the same optional point-shadow limits. Point lights are illumination sources, not visible emissive spheres or area lights.

DEPTH OF FIELD is a sampled thin lens. Lens offsets lie on a deterministic disk in the camera right/up plane. Each offset ray targets the same point on the forward-depth focus plane as its pinhole counterpart; stereo eye offsets and the off-axis projection are retained. Aperture is a world-space radius. Zero aperture preserves the pinhole ray. Lens sampling is disabled during movement independently of the fast-lighting switch. Stationary sample count is the greater of QUALITY SAMPLES and LENS SAMPLES, rather than their product; quarter-pixel positions repeat while lens positions vary. Raw radiance is averaged before display clipping for lens renders. Large apertures and few samples can produce structured blur/aliasing; there is no autofocus or physically calibrated f-stop model.

PROGRESSIVE LIGHTING repeats the final stationary resolution pass up to LIGHTING PASSES (1–128), varying deterministic hemisphere and lens sequences. Coarse refinement is performed first when AUTO REFINE is enabled; otherwise accumulation occurs at the selected preview block size. Each completed pass has equal weight. Floating-point RGB sums are allocated lazily (about 2.3 MiB for both eyes) and reset logically on motion, edits and eye-strength/stereo transitions. The first pass replaces stale sums, so old viewpoints are never blended. Progress reports the whole accumulation sequence after spatial refinement; FILES reports fully completed passes. After the limit, the image is idle. These are bounded one-bounce/skylight samples, with all previous approximation limits; accumulation reduces sampling variation, not systematic distance-estimator or light-transport errors.

Both new lens and accumulated images are rejected by surface capture and geometry-based material recoloring. Point-lit single-sample pinhole images can still be captured/recolored; changing light settings retraces and GPU lighting remains baked. Adaptive tile skipping is bypassed for lens and accumulated stationary rendering. Motion returns to the normal pinhole preview and temporal history path. Parallel jobs have deterministic per-pixel/pass sequences and produce the same accumulated stereo image as serial dispatch.

Batch budgeting now estimates cost per pixel job rather than per individual lens/quality ray; this avoids scheduling a full nominal batch of many-sample jobs based on a single-ray cost. One stereo job pair can still exceed the requested time budget on a difficult scene. Benchmarks use smaller job batches for large lens sample counts. They benchmark one view/sample group, not the full progressive sequence; CSV records lens/light/progressive settings for interpretation.

Scene format v6 appends OPTICS and two POINT records, validates all ranges, and reads v1–v5 with the new effects disabled. Previous versions cannot read v6. Optical state and lights are saved; accumulated pixels/sums are not. The console UI exposes both light entries without restarting a render merely to switch the light being edited. Color edits to lights and sky do not implicitly change the orbit-gradient mode.

Validation includes focus-plane convergence for both eyes, zero-aperture equivalence, camera/world light placement, bounded shadow work, HDR retention, lens movement bypass, exact progressive stereo coverage/averaging, serial/parallel agreement, reset behavior and legacy scene loading. All 36 default preset images and the UI fixture remain byte-identical with the new effects off. Run `optics_preview output-prefix` to reproduce visual examples; its reported times are desktop render costs, not console performance claims. On hardware, check light edits, shadow cost, focus/convergence comfort, movement interruption, accumulation limits, scene load/save, memory pressure and GPU capture rejection.

`benchmark-experiments.csv` remains the recorded corridor-pass snapshot. The optics preview utility reports current render costs and exports example scene settings; its costs include the full view/pass sequence and are desktop-only.

Lens and accumulated stationary samples are excluded from temporal surface history. Moving pinhole samples can still participate in the optional temporal experiment; accumulation resets before movement reuse.

## Color, glow and completed-pass timing

COLOR has 2–5 evenly spaced gradient stops. Tap any color to open the hue/saturation
wheel; the vertical slider changes brightness. A applies and B cancels. The D-pad
changes hue/saturation and L/R changes brightness. Light and sky colors use the same picker.
BOUNDED COLOR MAP transforms the orbit trap with t/(1+t), preventing traps above
one from all clipping to the endpoint. It defaults on in new scenes. Older scenes retain
legacy mapping; enable bounded mapping explicitly to update them.
AUTO FIT GRADIENT uses the 5th–95th percentiles of cached visible orbit values,
then adjusts scale/offset. Finish a single-sample pinhole render first; unavailable
with moving, multisample lens or accumulated GI records. Flat traps cannot be fitted.
Negative GRADIENT SCALE reverses the mapped direction. Repeat wraps the ramp.

COLOR EMISSION adds the material color independently of surface lighting, and
secondary indirect hits include that emission when GI is enabled. It is an artistic
approximation, not energy-conserving path tracing. BLOOM HALO adds a separable
13-pixel screen-space bright-pass blur after each stationary resolution/lighting pass.
Bloom is skipped during motion and cannot be GPU-captured or material-recolored;
bloom changes retrace the scene. Strength defaults to zero. Normal emission can recolor.

FILES -> LAST FRAME MS records wall-clock duration of the latest completed resolution
pass (all active eyes), including UI/vblank waits and stationary bloom. LAST FRAME
SCALE / EYES identifies the pass. LAST REALTIME FRAME MS / SCALE retain the most
recent completed moving sweep even after still refinement. This is a progressive sweep
while the camera moves, not a simultaneous snapshot of one camera pose. A canceled
pass never replaces the measurement. Scene/stereo/resolution changes restart timing.
Measurements start at the first tracing batch, not at scene invalidation.

Experimental ranges are widened: iterations 256, ray/shadow/GI steps 4096,
GI/AO samples 64, lighting passes 4096, exposure/sun/emission/bloom 100,
far clip 10000, gradient scale/offset +/-10000. Numeric A entry accepts the new
ranges. Basic mathematical, finite-value, memory and discrete layout constraints
remain enforced. Extreme values can cost much more time or break heuristic tracing.
Scenes now save as v7; v1–v6 load with legacy two-color mapping and no emission/bloom.

START now saves screenshots instead of exiting. FILES -> SAVE SCREENSHOT does the
same. Images go to sdmc:/3ds/Re-fract/screenshots/: shot-TIME.jpg is the top screen,
shot-TIME-screens.jpg includes both screens, and shot-TIME.mpo contains left/right
views when stereo is active (slider above 2D). No Camera album registration is used.
Stereo MPO uses MPF disparity entries and ordered left/right individual image numbers.
Screenshots can capture partial renders. GPU navigation uses synchronized target readback.
FILES -> EXIT APP returns home after joining the worker and synchronizing GPU cleanup.
The reported START crash has not been reproduced on hardware; START no longer enters
that exit path. Screenshot saving cancels the current timing sweep so SD I/O is not
included in the next completed-frame measurement. JPEG encoding uses stb_image_write
(nothings/stb, blob e4b32ed1bc32ef9c962acbf47a9d10af01939e08), its license is retained.

Progressive lighting now starts at COLOR -> LIGHTING BLOCK (default 4X), with
16 passes by default. 8X/16X trade spatial detail for faster visible convergence;
1X is the expensive full-resolution choice. QUALITY MODE forces 1X. Earlier coarse
refinement passes omit indirect rays and lens sampling. Each finished batch updates
the display immediately; movement still resets accumulation. With AUTO REFINE off,
accumulation uses the selected preview block directly. This improves feedback, but
GI remains CPU tracing and is not instantaneous realtime global illumination.

The vendored JPEG bit accumulator uses unsigned shifts to avoid signed-shift undefined behavior.
