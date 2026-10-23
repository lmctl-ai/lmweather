# lmweather

**Idealized weather and climate dynamics for lmscript in one static executable: rotating shallow water on the sphere (spectral transform method, Williamson test cases) and on a beta-plane (C-grid finite differences).**

| | | |
| :---: | :---: | :---: |
| <img src="docs/images/rh_anim.gif" width="300"><br>[Rossby-Haurwitz wave 4 drifting east, animated](examples/gallery/rh_anim.lms)<br><sub><code>lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmweather/main/examples/gallery/rh_anim.lms</code></sub> | <img src="docs/images/jet_instability.png" width="300"><br>[Mid-latitude jet instability and vortex roll-up](examples/gallery/jet_instability.lms)<br><sub><code>lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmweather/main/examples/gallery/jet_instability.lms</code></sub> | <img src="docs/images/equatorial_kelvin.png" width="300"><br>[Equatorial Kelvin wave and off-equatorial Rossby lobes](examples/gallery/equatorial_kelvin.lms)<br><sub><code>lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmweather/main/examples/gallery/equatorial_kelvin.lms</code></sub> |
| <img src="docs/images/rossby_haurwitz.png" width="300"><br>[Williamson TC6 wave 4 on a T42 Gaussian grid](examples/gallery/rossby_haurwitz.lms)<br><sub><code>lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmweather/main/examples/gallery/rossby_haurwitz.lms</code></sub> | <img src="docs/images/geostrophic_adjustment.png" width="300"><br>[Rossby geostrophic adjustment and balanced jets](examples/gallery/geostrophic_adjustment.lms)<br><sub><code>lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmweather/main/examples/gallery/geostrophic_adjustment.lms</code></sub> | <img src="docs/images/rossby_wave.png" width="300"><br>[Westward Rossby wave dispersion on a beta-plane](examples/gallery/rossby_wave.lms)<br><sub><code>lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmweather/main/examples/gallery/rossby_wave.lms</code></sub> |
| <img src="docs/images/gravity_pulse.png" width="300"><br>[Shallow-water gravity pulse and Hovmoller cone](examples/gallery/gravity_pulse.lms)<br><sub><code>lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmweather/main/examples/gallery/gravity_pulse.lms</code></sub> | <img src="docs/images/williamson2.png" width="300"><br>[Williamson TC2 cross-polar tilted geostrophic flow](examples/gallery/williamson2.lms)<br><sub><code>lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmweather/main/examples/gallery/williamson2.lms</code></sub> | <img src="docs/images/planetary_jets.png" width="300"><br>[Planetary rotation and Rhines-scale banded jets](examples/gallery/planetary_jets.lms)<br><sub><code>lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmweather/main/examples/gallery/planetary_jets.lms</code></sub> |

Each command runs the example from any empty directory: lmweather integrates the dynamical core, [lmplot](https://github.com/lmctl-ai/lmplot)
draws, and the picture lands in `images/`. `examples/render-all.sh` regenerates `docs/images/` from a checkout.

It is a scripting host, not a command-line tool: write an [lmscript](https://lmctl.com/tools/lmscript/skill.html) `_dsl_` block,
compile it, run it with `lmweather --dsl`. No Fortran, no NetCDF, no MPI, no data files, no shared libraries. (It is not WRF; see *Scope*.)

[![CI](https://github.com/lmctl-ai/lmweather/actions/workflows/ci.yml/badge.svg)](https://github.com/lmctl-ai/lmweather/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

```
# rh.lms: the Rossby-Haurwitz wave (Williamson test case 6) drifts east; mass and energy are conserved
p = _dsl_ cli="lmweather" install="github:lmctl-ai/lmweather@latest";
let s = wx.sphere(42).williamson6();                 # T42: 128 x 64 Gaussian grid
let m0 = s.mean_height();
let e0 = s.energy();
s.run_dt(7.0 * 86400.0, 900.0);                      # seven days, 15 minute RK4 steps
wx.print("crest of wave 4 at 45N: " + str(s.wave_crest(4, 45.0)) + " deg");
wx.assert_close(s.mean_height(), m0, 0.000000001);
wx.assert_close(s.energy() / e0, 1.0, 0.0001);
s.save_csv("rh_day7.csv");                           # lon lat h u v zeta, ready for gnuplot pm3d
end _dsl_
savedsl(p, "rh.dslb");
system ${dsl_cli(p)} --dsl rh.dslb;
```

`lmscript rh.lms` finds `lmweather` on `PATH` or downloads the release binary for your platform on first use,
compiles against the schema built into it, and runs the program.

## What it does

SI units; latitudes and longitudes in degrees at the API, radians inside initial-condition expressions. Fields are set
from expression strings (`"5960 - 100 * exp(-(lon*lon + lat*lat) / 0.04)"`; variables `lon, lat` on the sphere and `x, y` on
the plane, plus a few named constants, see the schema notes) because the DSL has no closures. Runs are deterministic: same program,
same numbers, bit for bit (seeded noise through `perturb`).

| `wx.` | |
| --- | --- |
| `sphere(T)` `planet(T, radius, omega, g)` → `Sphere` | spectral model, triangular truncation T = 4..170, Gaussian grid (T42: 128 x 64), vorticity-divergence form, RK4, optional del^4 hyperdiffusion |
| `Sphere` set up | `.williamson2(alpha)` `.williamson6()` `.fields(h, u, v)` `.mode(n, m, amp, depth)` `.perturb(amp, seed, max_wave)` `.tilt(alpha)` `.diffusion(nu4)` `.mark_reference()` |
| `Sphere` run | `.run(seconds)` `.run_dt(seconds, dt)` `.stable_dt()` |
| `Sphere` diagnostics | `.time .mean_height .energy .enstrophy .max_wind .error_l2 .error_linf .height_at(lat, lon) .u_at .v_at .vorticity_at .mode_coefficient(n, m, "re"\|"im") .wave_crest(m, lat)`, `.save_csv(path)` `.save_zonal_mean(path)` |
| `plane(nx, ny, Lx, Ly)` → `Plane` | C-grid beta-plane channel: periodic in x, free-slip walls in y, RK4; linear or nonlinear |
| `Plane` set up | `.depth(H)` `.gravity(g)` `.coriolis(f0, beta)` `.linear()` `.nonlinear()` `.viscosity(nu)` `.drag(r)` `.fields(h_anomaly, u, v)` `.perturb(amp, seed)` |
| `Plane` run and diagnostics | `.run(seconds)` `.run_dt(seconds, dt)` `.stable_dt() .time .wave_speed .rossby_radius .mean_depth .energy .max_speed .pv_anomaly_rms .h_at .u_at .v_at .zonal_amplitude(m, row) .zonal_crest(m, row)`, `.save_csv(path)` `.save_row(j, path)` `.save_column(i, path)` |
| checks and output | `assert_close(a, b, tol)` `assert_true(bool)` `print(s)` `save_text(path, text)` `version()` |

Models, equations, discretization and limits: [durable-memory/03-models-and-limits.md](durable-memory/03-models-and-limits.md);
schema conventions: [durable-memory/04-schema-v0.md](durable-memory/04-schema-v0.md).

Combine it with other hosts in one script: [examples/gallery/spherical_modes.lms](examples/gallery/spherical_modes.lms) integrates
four spherical-harmonic gravity modes with lmweather and plots them against the analytical Laplace tidal frequencies with
[lmplot](https://github.com/lmctl-ai/lmplot) (two `_dsl_` blocks, two hosts).

<p align="center">
  <img src="docs/images/spherical_modes.png" width="680"><br>
  <a href="examples/gallery/spherical_modes.lms">Spherical-harmonic gravity modes Y_n^0 vs exact Laplace frequencies</a><br>
  <sub><code>lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmweather/main/examples/gallery/spherical_modes.lms</code></sub>
</p>

## Examples

| Program | Shows | Run it |
| --- | --- | --- |
| [williamson2.lms](examples/williamson2.lms) | steady geostrophic flow on a tilted axis: height errors ~1e-12 over 5 days | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmweather/main/examples/williamson2.lms` |
| [rossby_haurwitz.lms](examples/rossby_haurwitz.lms) | wave 4 over 14 days at T42: phase drift, energy and mass conservation | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmweather/main/examples/rossby_haurwitz.lms` |
| [geostrophic_adjustment.lms](examples/geostrophic_adjustment.lms) | channel adjustment to the state predicted by potential-vorticity conservation | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmweather/main/examples/geostrophic_adjustment.lms` |
| [rossby_wave.lms](examples/rossby_wave.lms) | westward Rossby wave at c = -beta / (k^2 + l^2 + 1/Ld^2) | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmweather/main/examples/rossby_wave.lms` |
| [gravity_pulse.lms](examples/gravity_pulse.lms) | a pulse splits into two that travel at sqrt(g H) | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmweather/main/examples/gravity_pulse.lms` |
| [gallery/rh_anim.lms](examples/gallery/rh_anim.lms) | 56-frame 14-day Rossby-Haurwitz wave 4 animation encoded with ffmpeg | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmweather/main/examples/gallery/rh_anim.lms` |
| [gallery/jet_instability.lms](examples/gallery/jet_instability.lms) | barotropic mid-latitude jet instability and synoptic vorticity roll-up at T42 | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmweather/main/examples/gallery/jet_instability.lms` |
| [gallery/equatorial_kelvin.lms](examples/gallery/equatorial_kelvin.lms) | Matsuno-Gill equatorial Kelvin wave packet and westward Rossby lobes | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmweather/main/examples/gallery/equatorial_kelvin.lms` |
| [gallery/rossby_haurwitz.lms](examples/gallery/rossby_haurwitz.lms) | Williamson TC6 global geopotential height map at day 7 | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmweather/main/examples/gallery/rossby_haurwitz.lms` |
| [gallery/geostrophic_adjustment.lms](examples/gallery/geostrophic_adjustment.lms) | Poincare wave radiation, PV-balanced height ridge, and opposing geostrophic jets | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmweather/main/examples/gallery/geostrophic_adjustment.lms` |
| [gallery/rossby_wave.lms](examples/gallery/rossby_wave.lms) | beta-plane Rossby wave 2D height anomaly and westward phase march | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmweather/main/examples/gallery/rossby_wave.lms` |
| [gallery/gravity_pulse.lms](examples/gallery/gravity_pulse.lms) | D'Alembert wave splitting and space-time Hovmoller characteristic diagram | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmweather/main/examples/gallery/gravity_pulse.lms` |
| [gallery/williamson2.lms](examples/gallery/williamson2.lms) | tilted cross-polar geostrophic balance held to machine precision | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmweather/main/examples/gallery/williamson2.lms` |
| [gallery/planetary_jets.lms](examples/gallery/planetary_jets.lms) | slow vs fast planetary rotation (`wx.planet`) and Rhines-scale zonal jets | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmweather/main/examples/gallery/planetary_jets.lms` |
| [gallery/spherical_modes.lms](examples/gallery/spherical_modes.lms) | spherical-harmonic gravity modes Y_n^0 checked against Laplace tidal frequencies | `lmctl autorun https://raw.githubusercontent.com/lmctl-ai/lmweather/main/examples/gallery/spherical_modes.lms` |

`tests/` asserts known answers: gravity-wave speed sqrt(gH) (standing and travelling waves, both directions, exact to
0.05%), the spherical-harmonic gravity-wave frequencies sqrt(n(n+1) g H)/a, Williamson test case 2 at four axis tilts,
Williamson test case 6 phase speed and conservation, geostrophic adjustment against the potential-vorticity prediction,
the Rossby wave speed to 1%, exact mass conservation, energy conservation, and bitwise reproducibility.

## Build

C++17 compiler, CMake 3.24; nothing else (no third-party sources).

```sh
cmake -S . -B build && cmake --build build     # about 15 seconds
./test.sh                                      # needs lmscript on PATH
```

Linux builds are fully static; macOS links only system libraries; Windows links the MinGW runtime statically.

## Scope

lmweather is a dynamical-core playground, not a forecast system. WRF needs Fortran, NetCDF, MPI and gigabytes of geographic
and analysis data, none of which can live in one self-contained executable, so it is not wrapped. What is here is the
shallow-water layer that numerical weather prediction methods are validated on: no moisture, radiation, boundary layer,
orography or real-data initialization. See [durable-memory/09-open-items.md](durable-memory/09-open-items.md).

## License

MIT ([LICENSE](LICENSE)); no third-party code ([THIRD_PARTY_NOTICES](THIRD_PARTY_NOTICES) cites the methods).
