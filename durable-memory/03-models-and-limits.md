# Models and limits

## Sphere (spectral transform method)

Equations (a radius, Omega rotation, Phi = g h, mu = sin(lat), U = u cos(lat), V = v cos(lat), eta = zeta + f,
E = (U^2 + V^2) / (2 (1 - mu^2)), D(A, B) = (1/(a (1-mu^2))) dA/dlon + (1/a) dB/dmu):

    d eta / dt   = - D(U eta, V eta)
    d delta / dt =   D(V eta, - U eta) - laplacian(Phi + E)
    d Phi / dt   = - D(U Phi, V Phi)

- Unknowns are the spectral coefficients of zeta, delta and Phi in orthonormal associated Legendre functions
  (triangular truncation T, `legendre_column`: Condon-Shortley phase omitted, recurrence of the EGS/ECMWF type).
  Winds follow from the stream function and velocity potential. Products are formed on the Gaussian grid
  (nlon = smallest power of two >= 3T+1, nlat = nlon/2: T21 64x32, T42 128x64, T85 256x128) so quadratic terms are
  exactly integrated (no aliasing). `D` is applied to grid products in spectral space by Gauss-Legendre quadrature with
  the (1 - mu^2) dP/dmu identity, i.e. the transforms are the standard Orszag/Machenhauer ones.
- Time stepping: classical RK4, fixed step (`run_dt`) or `stable_dt()` = 1.5 / omega_max with
  omega_max = (T/a)(sqrt(g h_max) + |v|_max) + 2 Omega (RK4 is stable to |omega dt| < 2.83). Explicit: the step is limited
  by gravity waves (T42, h ~ 8000 m: dt <= ~900 s), the price paid for avoiding a semi-implicit solver.
- Optional del^4 hyperdiffusion `diffusion(nu4)`, applied after each step as an exponential factor (relative vorticity,
  divergence: eigenvalue (n(n+1)-2)^2/a^4 so rigid rotation is not damped; height: (n(n+1))^2/a^4). Default 0.
- The rotation axis can be tilted (`tilt(alpha)`: f = 2 Omega (-cos lon cos lat sin alpha + sin lat cos alpha)), as in
  Williamson's test cases 2 and 3: the flow is rotated *together with f*; forgetting this makes TC2 unsteady (the test
  suite shows the wrong-f case drifting by >1e-3 in a day).
- Exact invariants: the (0,0) coefficient of Phi has zero tendency, so `mean_height` is conserved to rounding (1e-12).
  Energy `energy()` = area mean of h|v|^2/2 + g h^2/2; potential enstrophy = mean of eta^2/(2h).
- Diagnostics at a point (`height_at`, `u_at`, ...) synthesize the spectral series exactly (no interpolation).
- Limits: no orography, no forcing/dissipation other than hyperdiffusion, no moisture or vertical structure, truncation
  at most T170, and no semi-Lagrangian or implicit option (long dt not possible).

## Plane (C-grid finite differences)

h at cell centers, u at west faces, v at south faces (rows 0 and ny are walls, v = 0), f = f0 + beta (y - Ly/2),
periodic in x, free slip (du/dy = 0) at the walls. Vector-invariant form:

    du/dt = (f + zeta) v_bar - d(g h + K)/dx - r u + nu lap(u)
    dv/dt = -(f + zeta) u_bar - d(g h + K)/dy - r v + nu lap(v)
    dh/dt = -div(h u)      (flux form: total mass conserved to rounding)

zeta lives at corners; the Coriolis/vorticity terms average the four surrounding velocities (Sadourny-type). `linear()` replaces
h by the resting depth H in the fluxes and drops zeta and K (the equations linearized about rest). Second-order in space,
RK4 in time; `stable_dt()` = safety * 2 / (c sqrt(1/dx^2 + 1/dy^2) + ...), safety 0.5.

- Discrete conservation: mass exactly; in the linear case the potential vorticity anomaly zeta - f0 (h - H)/H is conserved at every
  interior corner (with h averaged over the four cells around it) by the discrete equations (`pv_anomaly_rms`); energy
  to the time-stepping error.
- Numerical dispersion: gravity waves have omega = (2c/dx) sin(k dx/2), i.e. a relative error (k dx)^2/24; the tests use
  >= 100 points per wavelength.
- Limits: uniform grid, beta-plane approximation (the Coriolis parameter only varies linearly), no topography, no
  forcing, `perturb` noise is smoothed white noise (three 1-2-1 passes), Laplacian viscosity only.

## Random numbers

`perturb` uses splitmix64 and an Irwin-Hall (sum of 12 uniforms - 6) normal so that the stream is identical on all platforms.
