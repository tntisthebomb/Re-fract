# Rounded spheres and spiral detail: reference starting point

The supplied image visually suggests a negative-scale Mandelbox. This is an informed starting point, not an exact identification from the picture. In the app, load **NEGATIVE BOX**. It already has these stages in order:

| Stage | Control | Value |
| --- | --- | --- |
| BOX FOLD | FOLD LIMIT | 1 |
| SPHERE FOLD | MINIMUM RADIUS | 0.5 |
| SPHERE FOLD | FIXED RADIUS | 1 |
| SCALE + C | SCALE | -1.5 |
| SCALE + C | C MULTIPLIER | 1 |

Under **FORMULA**, use JULIA MODE off, LOG DISTANCE off, TERMINAL SHAPE 0, DERIVATIVE SCALE 1, BAILOUT 32, and ITERATIONS 16–24 for a still render. Use 10–12 iterations while exploring. The negative scale is important: **SPHERE NETWORK** uses positive scale and different minimum radius, so it is not the same starting formula.

Explore the interior with surface slowdown enabled. Camera position and viewing angle strongly determine whether you see spheres, tunnels, or a flat wall. Try nearby scales -1.45, -1.5, and -1.55; A accepts exact numbers. Change one parameter at a time. Sphere-fold minimum radius 0.4–0.6 is another useful small sweep. No exact camera position can be recovered from the image alone.

For a rough pale/gold material, enable CUSTOM GRADIENT under **COLOR**, set GRADIENT START to `707B8C`, GRADIENT END to `D8BE8A`, GRADIENT SCALE to 0.8, REPEAT GRADIENT off, and SPECULAR around 0.6. For still rendering try AO SAMPLES 2–3, SHADOW STEPS 16–24, RAY STEPS 128, and HIT EPSILON 0.001, then Y for quality. These settings are expensive; turn AO/shadows off for movement.

The reference has sophisticated illumination and possibly reflections/environment shading. This build has a simple directional light, local AO, shadow approximation, fog and a two-color orbit gradient. Geometry parameters alone cannot reproduce all of its metallic lighting and blue surroundings.

To repeat the whole structure, enable **FORMULA → WORLD REPEAT** and start X/Z SPACING at 16 and Y SPACING at 0. Set all three to 16 for repetition vertically as well. Raising iterations creates finer structure; world repetition creates additional copies. FAR CLIP 60–100 shows more distant copies, with a performance cost.
