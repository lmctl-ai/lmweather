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

Errors: `run_dt` with a step above the RK4 limit for the present state (sphere 2 sqrt(2) / omega_max, plane `stable_dt` with
safety sqrt(2)/2) stops with "exceeds the RK4 stability limit" before stepping, because an unstable RK4 run gives wrong but
finite numbers for many steps; the limit is re-checked every 64 steps against the present winds and depth, and a flow that
accelerates past it (a deep low adjusting) stops with "... (the flow accelerated)"; the linear plane ignores the winds, its
limit is set by sqrt(g H) alone; a diverging integration (checked over all coefficients/fields every 64 steps and at the end)
stops with "blew up (not finite): use a smaller dt"; out-of-range or non-finite arguments name the argument (int arguments
are range-checked before narrowing, so 2^32 + 2 is not taken as 2).
