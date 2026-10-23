# Resource limits

Why: Sphere and Plane objects live in arenas until exit (no `free`), so a DSL loop that keeps
creating them grows without bound. On 2026-10-23 a runaway lm* host (lmnum) reached 16 GB RSS and
filled the 24 GB WSL machine; every host now has a total budget.

- **Total memory budget**, default 4096 MiB, set with `LMWEATHER_MAX_MEMORY_MB` (positive whole
  MiB; anything else exits 2 with the usage text). Shown in the usage message.
- `cpp/memory_budget.{hpp,cpp}` (identical copy in every lm* host, linked into the `lmweather`
  executable only, not into `weather_core` or `core_tests`) replaces the global
  `operator new`/`delete` and counts block sizes from the C allocator.
- There is no third-party code and no `malloc`: every buffer is a `std::vector`/`std::string`, so
  all of it is counted without `charge()`:
  - Sphere: Legendre tables `P_`/`H_` (nlat x (N+1)(N+2)/2 doubles each), spectral state
    (3 x (N+1)^2 complex), Coriolis grid, and the per-step RK/transform temporaries. T170 is a
    512 x 256 grid.
  - Plane: `h_`, `u_`, `v_` (3 x nx*ny doubles, 192 MB at the 8 million cell cap) and the RK4
    stage copies during `run`.
  - `fields()` grids, `save_csv` text (built in one string, ~70 bytes per cell) and VM values.
  - `run` keeps no snapshots: step counts are capped (2e8) but cost only time, not memory.
- Exceeding it throws `membudget::Exceeded` (a `std::bad_alloc`, fixed-buffer message);
  `translate()` turns it into a `dslvm::Error` with the same message, and main catches
  `std::exception` around load/run (budget inside the VM). The first failure prints
  `lmweather: memory budget exceeded: ... (set LMWEATHER_MAX_MEMORY_MB ...)` on stderr, the JSON
  line carries the message, exit 3. 64 MiB of headroom is granted once for the error path.
- Per-object caps stay (T <= 170, plane <= 8e6 cells, 100000 objects per arena); the budget
  bounds their sum.
- Not covered: stack and static data, and the 64 MiB reserve after the first failure.
- No `setrlimit`: not portable to macOS static builds, and it would crash instead of reporting.
- Measured (x86_64 Linux, WSL): a loop of `wx.plane(4000, 2000, ...)` stops at 64 MiB in 0.16 s
  and at the default 4096 MiB after 12 s with 4092 MiB peak RSS (4090 MiB tracked at the failure);
  a loop of `wx.sphere(170)` stops at 64 MiB in 0.5 s. `test.sh` runs both loops at 64 MiB
  everywhere and the plane loop at the default budget on Linux under `ulimit -v 6291456`.
