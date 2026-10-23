# Schema v0

One context, `wx: Wx`. Object types: `Sphere`, `Plane`. `schema/lmweather.dsl.json` is generated from the `method(r, ...)`
registrations in `cpp/main.cpp` by `scripts/gen-schema.py`; `test.sh` fails when it is stale and diffs it against
`lmscript --dsl-signatures`.

Conventions: SI units. Angles at the API (lat, lon of `*_at`, `wave_crest` results, CSV columns) are degrees; `tilt`, `williamson2(alpha)`
take radians; expressions use radians. Builder methods return the receiver. Setting state (`williamson2`, `williamson6`,
`fields`, `mode`, `perturb`) resets the time to 0 and, for the sphere, marks the new height as the reference for `error_l2/linf`
(Williamson's normalized errors against the initial height); `mark_reference()` re-marks it. `perturb` adds to the present
height (use after `fields`). Expression constants: sphere `a, omega, g, pi, e`; plane `lx, ly, H, g, f0, beta, pi, e`
(`H` is the depth set before `fields`; the `h` expression of `Plane.fields` is the *anomaly* added to H). The DSL has no
exponent literals (`1e-5` is invalid): write `0.00001`; it has no `exp`/`log` either: `pow(2.718281828459045, x)`.
`wave_crest(m, lat)` and `Plane.zonal_crest` return a crest of the m-th zonal harmonic modulo its wavelength.

`Plane.depth` after fields/perturb/run is an error (it would reset them). Plane rows `j` and columns `i` index cells from the
south-west corner. CSV outputs are space separated, `%.10g`, rows grouped by latitude/y with blank lines between rows
(gnuplot `splot ... with pm3d` with `set pm3d map`): sphere `lon lat h u v zeta`, plane `x y h u v zeta` (u, v at centers).
`save_zonal_mean`: `lat h_mean u_mean`; `save_row/column`: `x h` / `y h`.

Errors: a diverging integration stops with "blew up (not finite): use a smaller dt"; out-of-range arguments name the argument.
