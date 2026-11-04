# 11 — Invocation envelope (2026-11-04 port)

`main()` answers `--lmcode FILE...` with the lmcode-cpp `InvocationResults` one-envelope frame
(`LMCODE_RETURN_BEGIN` / `END` lines around the JSON result), replacing the per-program
stdout `report_ok()` / `report_error()` lines. Structure after the identical lmphysics port:

- CLI usage mistakes (unknown flag, missing file, duplicate `--dry-run`, bad
  `LMWEATHER_MAX_MEMORY_MB`) print the usage text on stderr, then a `failure_frame("cli", ...)`
  frame, and exit 2. `check-host-flags.py` and the usage matrix in `test.sh` pin this.
- `results.registrations()` lists the files; every file is loaded and verified FIRST, so a broken
  later file cannot leave earlier CSV output behind.
- Each program: `results.select(index, reg, contract)`, run only when `!results.dry()`,
  `results.complete(index)`; on error `results.fail(index, phase, e)` and stop. Phases: `load`,
  `run`, `app` when the exception code is `APP_FAIL`.
- Dry mode is invocation-wide: `run_lmcode` under `lmscript --dry-run` supplies `dry: true` in
  `LMCODE_INVOCATION_FILE`; the standalone `--dry-run` flag requests the same mode when no
  metadata is present (and contradicting metadata is a cli failure). A dry invocation loads every
  program but runs none — simulations have no effect-free mode.
- A 1 MiB reserve (`reserve`) is dropped before recording any failure, so a program that hit the
  memory budget can still have its frame written.
- Versions: lmweather 0.1.4, vendored SDK at lmcode-cpp origin/main (invocation.hpp,
  result_contract.hpp added). The invocation file must carry `dry` as a boolean; the published
  compiler release does not, so `scripts/select-compiler.sh` now probes a real end-to-end run
  through a stub host and rejects those releases.
