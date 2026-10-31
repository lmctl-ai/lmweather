# Purpose and place

lmweather is the last of the *scripted scientific libraries* family (`../../simulation/ROADMAP.md`, procedure
`../../simulation/PROCEDURE.md`, feasibility `../../simulation/FEASIBILITY.md` section "lmweather"). FEASIBILITY ruled WRF out
(Fortran 90, NetCDF/HDF5, MPI, csh configure, 2.6-29 GB of static geographic data, nothing embeds, no MinGW) and asked for an own
reduced model: rotating shallow water on a sphere or beta-plane. This project delivers both:

- **Sphere**: spectral transform shallow-water model (the method behind ECMWF/NCAR dynamical cores), validated on
  Williamson et al. test cases 2 and 6 and on exact linear gravity-wave frequencies.
- **Plane**: Arakawa C-grid finite-difference beta-plane channel (periodic in x, walls in y), linear or nonlinear,
  validated on gravity-wave speed, Rossby adjustment and Rossby-wave dispersion.

Layout: `cpp/sphere.*` (FFT, Gauss-Legendre, Legendre functions, transforms, RK4 model), `cpp/plane.*` (C-grid model),
`cpp/main.cpp` (DSL registrations, context `wx: Wx`), `cpp/expr.hpp` (expression strings, copied from lmnum),
`tests/core.cpp` (native ctest), `tests/*.lms` and `examples/*.lms` (programs that assert), `data/` none.

Scripting only: `--lmcode [--dry-run]`, `--version` (exactly `lmweather X.Y.Z`), `--notices`, `--lmcode-schema [--json]`,
`--lmcode-signatures`; exit 2 usage, 3 load/run error. One thread, own code only: runs are reproducible bit for bit.
