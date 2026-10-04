# Performance controls

The renderer is CPU based. The New 3DS worker handles half a batch rather than waking once per stereo cell. Mono rendering can use both CPUs too. Results are joined before scene edits or image writes. Formula edits rebuild cached constants and the enabled-stage list. Geometry queries omit orbit-trap calculations; one coloring query is made at a hit. Custom expressions fold finite constant subexpressions, preserving invalid-domain errors.

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

GPU mesh extraction/caching and GPU-based stereo warping are not implemented. They require a separate geometry/rasterization path and hardware profiling; this update does not claim GPU ray marching or compute shaders.

Scene format v2 saves the new settings and still reads v1 scenes. Older application builds cannot read v2 saves.
