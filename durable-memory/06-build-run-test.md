# Build, run, test

Measured 2026-10-23, x86_64 Linux (WSL2), 4 cores, GCC, Ninja.

- Clean Release build about 15 s (no third-party sources). Stripped Linux binary links `libstdc++`/`libgcc` statically and dynamic glibc (`>= 2.35`, verified via `readelf -d` allowlist).
- Speed: T42, 14 days at dt = 900 s (1344 RK4 steps) takes about 6 s; T21, 7 days at 1800 s about 0.3 s; the C-grid plane
  60 x 30 for 20 days about 0.8 s.
- `./test.sh` (18 checks, about 45 s): sdk pin; no-dependencies check; build; native ctest (`tests/core.cpp`); `--version`
  exactly `lmweather X.Y.Z`; schema equals the registrations; embedded schema byte-identical; host registry equals compiler
  signatures; notices; runtime linkage allowlist; 13 programs (8 under `tests/`, 5 under `examples/`) compile and run and pass their own
  asserts; 17 CSV files byte-identical on rerun; 22 error snippets exit 3 with a message naming the problem; the two
  lmweather+lmplot pipelines when `lmplot` is on `PATH` or `LMWEATHER_TEST_NETWORK=1`; dry run writes nothing; usage exit codes.
- Run a program by hand: `lmscript examples/williamson2.lms` (compiles, binds to `lmweather` on PATH).

## Native host flag contract

The host accepts only the lmcode option family, with no retired aliases. The full gate checks exact schema/signature output, new loader routing and rejection before file effects in scratch HOME. SDK20325f7 is synchronized through the owner script for actual source-compiler v6 capabilities; package metadata alone is insufficient. Internal DSLB and .dslb names remain.
