# Validation

Every test asserts a closed form, an exact property or a published test-case behavior, not a number taken from this code.

- **Gravity-wave speed** (`tests/plane_gravity_wave.lms`): H = 100 m: c = sqrt(g H) = 31.3209 m/s. A standing wave cos(kx)
  returns -cos(kx) after T/2 = pi/(c k) (error 5e-4 of amplitude at 200 points per wavelength, pure numerical dispersion),
  a travelling wave (u = (c/H) h') moves 300 km in 300 km / c to 2.5 km, west or east, linear or nonlinear; deeper
  layers are faster by sqrt(H); mass exact, linear energy conserved to 1e-4.
- **Spherical gravity modes** (`tests/sphere_gravity_modes.lms`): on a non-rotating sphere Y_n^m oscillates at
  sqrt(n(n+1) g H)/a; after a quarter, half and 1.5 periods the coefficient is 0, -1, -1 of its start (n = 2,5,8,11, m = 0..3, 1 m on
  1000 m, tolerances 1e-2, 3e-3, 6e-3 = the amplitude-dependent frequency shift); mass exact, energy 1e-8.
- **Williamson test case 2** (`tests/sphere_williamson2.lms`, `examples/williamson2.lms`): steady geostrophic flow with
  u0 = 2 pi a/12 days, h0 = 2.94e4/g, tilts 0, 0.05, pi/2-0.05, pi/2: normalized l2 and l-infinity height errors after 5 days
  are 1e-14 (spectral truncation represents the solution exactly; the asserted bound is 1e-6, Williamson's models reach 1e-5..1e-7).
  Reference values (equator height h0 = 2998.0 m, poles 1092.7 m, maximum wind 38.6 m/s, mean 2363.02 m) and a negative control (tilted flow on an
  untilted planet drifts: l2 > 1e-3 in one day).
- **Williamson test case 6** (`tests/sphere_rossby_haurwitz.lms`, `examples/rossby_haurwitz.lms`): wave 4 with
  h0 = 8000 m, omega = K = 7.848e-6. Mass exact (1e-9), energy within 1e-5 (T21) / 1e-8 (T42 14 days), potential enstrophy
  within 1e-4. There is no closed-form shallow-water phase speed (the Rossby-Haurwitz wave solves the non-divergent equations
  only, and wave 4 is weakly unstable: Thuburn and Li 2000); the oracle is the resolution-converged drift of the m = 4 crest
  at 45 N: 79.3 (T21, dt 1200 s), 79.1 (T42, dt 600 s), 79.0 (T63, dt 450 s) degrees in 7 days = 11.3 deg/day, asserted as
  79.1 +- 0.7 at T21, T21 vs T42 within 0.5 deg at day 4, and halving dt moves the phase < 0.01 deg. The barotropic
  non-divergent estimate nu = (R(3+R)w - 2 Omega)/((1+R)(2+R)) = 12.2 deg/day is a ~7% overestimate for the free-surface
  wave (asserted 0.88 < ratio < 0.98 over 3 days). The instantaneous drift is not uniform (10.8 deg/day on day 1, 11.7 on
  day 4): the free-surface wave is not a single steadily propagating mode. Steps are 62-65% of the RK4 limit (1845 s at
  T21, 967 s at T42, 655 s at T63 for this state); before this change they ran at 93-98% of it.
- **Geostrophic adjustment** (`tests/plane_adjustment.lms`, `examples/geostrophic_adjustment.lms`, `tests/sphere_adjustment.lms`):
  channel with f0 = 1e-4, H = 1000 m (Ld = 990.5 km), Gaussian bump width ~400 km varying in y; the time mean over 10 inertial
  periods equals the potential-vorticity prediction h_n = h0_n/(1 + (n pi Ld/Ly)^2) in cosine modes, center 0.3018 m vs 0.3024 m
  (tolerance 1.5e-2 m at 5 rows); the discrete PV anomaly is conserved to 1e-3. On the sphere a Gaussian low at 45 N spins up
  cyclonic winds (positive vorticity, eastward flow to the south, westward to the north), mirrored in the south.
- **Rossby waves** (`tests/plane_rossby.lms`): c = -beta/(k^2 + l^2 + 1/Ld^2) = -3.359 m/s for the 4000 x 2000 km channel;
  in 20 days the crest moves 5804 km west, simulated within 21 km (0.4%); no drift without beta; doubling beta doubles the speed.
- **Reproducibility** (`tests/determinism.lms`, the rerun in `test.sh`): identical bits for equal seeds, different for other seeds; 17
  CSV files byte-identical on rerun. Native tests (`tests/core.cpp`): FFT vs DFT, Gauss-Legendre exactness to degree 2n-1,
  orthonormality of the Legendre functions, spectral round trip to 1e-12, solid-body vorticity 2 u0 sin(lat)/a exact.
