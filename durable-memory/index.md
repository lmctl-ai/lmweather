# lmweather durable memory

- `01-purpose-and-place.md` — what lmweather is, why not WRF, relation to the family.
- `02-libraries-and-licenses.md` — no third-party code; what was written from published methods.
- `03-models-and-limits.md` — the spherical spectral model and the beta-plane C-grid model: equations, numerics, limits.
- `04-schema-v0.md` — the `wx` API and its conventions.
- `05-validation.md` — the known answers the tests assert, with measured numbers and tolerances.
- `06-build-run-test.md` — measurements and the checks in `test.sh`.
- `07-ci.md` — GitHub Actions and releases.
- `08-examples.md` — the example programs and what they taught.
- `09-open-items.md` — what is not done.
- `10-resource-limits.md` — the total memory budget (`LMWEATHER_MAX_MEMORY_MB`) and how it is counted.

Compiler >=0.2.420 resolves std from the pinned SDK bundle by default; host schema bytes remain host-only, with full and `--without-std` parity plus a standard-only runtime probe in `test.sh`.
