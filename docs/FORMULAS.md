# Formula reference

Each distance query starts with `z = point`, derivative bound `dr = 1`, and `c = point` (Mandelbrot mode) or the configured Julia constant. Enabled stages run in order for each iteration. Iteration stops when the orbit exceeds bailout or reaches the configured limit.

| Operation | A | B | C |
| --- | --- | --- | --- |
| Box fold | Fold limit | Unused | Unused |
| Sphere fold | Minimum radius | Fixed radius | Unused |
| Bulb power | Power 1.1..16 | Polar-angle multiplier -4..4 | Azimuth-angle multiplier -4..4 |
| Scale + C | Uniform scale | Multiplier for Mandelbrot point / Julia constant | Unused |
| Rotate XYZ | X radians | Y radians | Z radians |
| Offset | X translation | Y translation | Z translation |
| Absolute | No parameters | | |
| Sort XYZ | No parameters | | |
| Menger | Scale >=1.1 | XY offset factor | Z offset factor |
| Tetra fold | No parameters | | |
| Expression | Three separately editable expressions | | |

Sphere fold applies a uniform scale of `fixedRadius^2/minRadius^2` inside the minimum sphere, an inversion `fixedRadius^2/r^2` between spheres, and identity outside. Box fold is `2*clamp(z,-limit,limit)-z`, independently on each component. Bulb power is the spherical-coordinate power transform; a separate Scale + C stage adds the point/Julia constant. All three expression outputs use the **same incoming vector**, so X/Y/Z updates are simultaneous.

Menger applies absolute/sort folds, scales, subtracts `(B,B,0)*(A-1)`, then subtracts `C*(A-1)` from Z when Z is above half that offset. Tetra fold reflects across the three planes `x+y=0`, `x+z=0`, `y+z=0` when their sums are negative.

## Expression language

Variables: `x y z cx cy cz`; constant: `pi`.

Operators: `+ - * / ^`, parentheses and unary `+/-`. Multiplication must be explicit. Exponentiation is right-associative; `-2^2` means `-(2^2)`.

Functions: `sin`, `cos`, `abs`, `sqrt`, `log`, `exp`, `min(a,b)`, `max(a,b)`, `pow(a,b)`. Angles are radians. Function and variable names are lowercase.

Examples:

```text
X: x*x-y*y-z*z+cx
Y: 2*x*y+cy
Z: 2*x*z+cz
```

```text
X: x*cos(0.2)-y*sin(0.2)+cx
Y: x*sin(0.2)+y*cos(0.2)+cy
Z: z*1.8+cz
```

Limits: 256 characters per output, 128 compiled instructions, 32 stack values and nesting depth 24. The parser rejects malformed input before replacing the current expression. Division near zero, negative square roots, invalid logs, non-finite results and unsupported real powers fail at runtime and color affected pixels magenta. Negative bases support constant integer exponents only.

Dual-number evaluation tracks partial derivatives with respect to all six variables. Jacobian Frobenius norms provide a local upper bound on transform stretch; the propagated bound includes dependence on the Mandelbrot point. This can be conservative and make custom formulas slower than equivalent built-ins. It is not a proof that arbitrary user expressions define a valid fractal distance estimator. Discontinuous and strongly distorted formulas may still show artifacts.

## Distance controls

- **Log distance** uses the bulb-style `0.5*log(r)*r/dr` estimator.
- With log disabled, terminal shape **0** uses `r/dr` (folded-box families).
- Terminal shape **1** uses a signed box estimator with terminal radius as half extent (Menger).
- Terminal shape **2** uses four tetrahedral plane distances (Sierpinski).
- A nonzero terminal shape overrides log distance.
- **Derivative scale** divides estimated distance by an additional factor >=1.
- **Step safety** multiplies distance before advancing a ray. Lower it if surfaces are skipped.
- **Hit epsilon** controls the surface threshold. It scales with distance to avoid wasting work on far-away subpixel detail. Quality mode halves the threshold.

For corridor experiments, load CORRIDOR BOX and adjust the **Scale + C** scale, then sphere-fold minimum radius and box-fold limit. SPHERE NETWORK starts with a smaller minimum radius and lower scale. No single parameter is guaranteed to reproduce an unseen reference.

## Hardware checklist

Additional presets 18–23:

| Preset | Operation chain |
| --- | --- |
| MANDELBULB / 4 | Bulb power 4, scale 1 plus C, 10 iterations |
| MANDELBULB / 5 | Bulb power 5, scale 1 plus C, 10 iterations |
| ABSOLUTE BULB | Absolute, bulb power 3, scale 1 plus C, 10 iterations |
| ROTATED NEGATIVE BOX | Rotate (0.08, 0.12, 0.03), box fold 1, sphere radii 0.5/1, scale −1.5 plus C |
| TWISTED JULIA BULB | Rotate (0.03, 0.1, 0.05), bulb power 8, scale 1 plus Julia constant (0.25, −0.15, 0.1), 10 iterations |
| MENGER / WIDE CUT | Menger scale 3, XY offset 1, Z offset 0.7, box terminal, 6 iterations |

1. Launch a successful Actions build on New 3DS and confirm the top render and bottom menu appear.
2. Move with the Circle Pad and release it; confirm the preview eventually refines to `1X`.
3. Raise/lower the slider, test left/right depth and adjust convergence to a comfortable value.
4. Check whether status reports two CPU batches or single-CPU fallback. Compare render time with PARALLEL BATCHES on/off.
5. Edit a numeric value and an expression through the system keyboard, then return to rendering.
6. Save a scene, reload it, and export a finished stereo pair; inspect files on the SD card.
7. Try every preset and high-quality mode. Record sustained render times and behavior after sleep/resume.
8. Enable GPU SURFACE CACHE, finish a single-sample stationary render and capture it. Confirm geometry, orientation, depth testing, slider-at-2D behavior, independent stereo views, bottom UI and CPU resume. Check repeated switching and low-memory fallback. Holes/disocclusion and baked lighting are expected limitations.
9. Compare ALGEBRAIC BULB EXP on/off in live previews. Confirm quality mode uses the original math. Record GPU DRAW MS and UI FRAME / TRACE MS separately; these do not measure complete-fractal FPS.

## Additional corridor presets (24–35, zero-based)

| Preset | Editable construction |
| --- | --- |
| Ruby Chambers | Negative box scale −1.5, radii 0.5/1, 14 iterations, repeated every 12 units |
| Blue Sphere Vault | Negative box scale −1.8, radii 0.32/1, repeated every 12 |
| Gold Box Corridor | Box scale 2.8, radii 0.5/1, repeated every 12 |
| Menger Colonnade | Menger scale 3, 7 iterations, repeated every 3.2 |
| Twisted Box Hall | Rotate (0.03, 0.08, 0.02), box scale 2, repeated every 12 |
| Inverted Bubble Hall | Negative box scale −1.8, radii 0.12/1, repeated every 12 |
| Tetra Gallery | Tetra fold, scale 2, offset (−1,−1,−1), repeated every 5 |
| Julia Bulb Arcade | Bulb 8, Julia (0.25,−0.15,0.1), repeated every 3.5 |
| Bulb Garden | Bulb 8, 12 iterations, repeated every 3.5 |
| Absolute Bulb / 5 | Absolute fold, bulb 5, 12 iterations |
| Box-Bulb / 4 | Box fold 1, bulb 4, 10 iterations |
| Negative Box / Deep | Negative box −1.5, 20 iterations, closer starting camera |

Periodic cells are finite copies of the selected formula, not an analytically generated corridor or recovered source image. All stage values, repeat spacings, camera and gradients remain editable.

## Pseudo-Kleinian chambers (presets 36–39)

KLEINIAN CHAMBERS, KLEINIAN CORRIDOR, KLEINIAN DROPS and KLEINIAN GALLERY
use an anisotropic box fold followed by sphere inversion. They are the
pseudo-Kleinian artistic family, not a general quaternion/Mobius group solver.
The fold reflects each component as `2*clamp(z,-extent,extent)-z`.
SPHERE INVERSION uses `k=max(radius²/dot(z,z),1)` and multiplies both the
orbit and derivative by k. A small denominator guard keeps the singular origin finite.
Terminal shape 3 uses `max(length(z.xy)-terminalRadius,abs(length(z.xy)*z.z)/length(z))/dr`.
This is a heuristic distance estimator; step safety defaults to 0.45 for these scenes.

Adjust STAGES -> FOLD X/Y/Z and INVERSION RADIUS to change chambers and openings.
FORMULA -> TERMINAL RADIUS changes the final cross-section. More ITERATIONS reveal
smaller detail but cost rendering time. Start at 12; try 16–20 for still images.
The mathematical construction itself generates chambers without periodic-cell copies.
Existing lights, depth of field, progressive illumination, stereo and save/load work normally.
The four presets are built into the executable; no SD-card examples are required.
New operation IDs are appended, preserving all previous scene IDs and presets.
Older builds cannot load scenes containing these new operations/terminal shape.

Background: Knighty/Theli-at pseudo-Kleinian family, described by Mikael Hvidtfeldt
Christensen: https://blog.hvidtfeldts.net/index.php/2012/05/distance-estimated-3d-fractals-part-viii-epilogue/
