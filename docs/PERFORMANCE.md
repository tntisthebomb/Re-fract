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
