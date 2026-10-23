# Open items

- Semi-implicit time stepping on the sphere (long steps), semi-Lagrangian advection, and a Robert-Asselin or RK-IMEX option.
- Forcing and friction, Held-Suarez-like thermal relaxation; a shape-preserving
  (monotone, semi-Lagrangian) tracer scheme: the spectral tracer rings around sharp edges.
- Williamson test cases 3 (jet), 4 (forced), 7 (analyzed initial data); Galewsky barotropic instability.
- Two-layer or Boussinesq vertical structure, a moist warm-bubble LES-like toy; real-data initialization (needs GRIB/NetCDF, out of scope).
- Plane: free-slip walls only (add periodic-y and no-slip), variable depth, a nonlinear conservation-friendly (Arakawa-Lamb) scheme.
- Output: more diagnostics (potential vorticity maps, divergence), restart files, lat-lon regridding of the plane CSV to the sphere.
- Threads for speed (the spectral transforms parallelize over latitudes; the order of sums would have to stay fixed for bitwise reproducibility).
