#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$HERE"
BUILD_DIR="${BUILD_DIR:-build}"
HOST="$BUILD_DIR/lmweather"
UNAME_S="$(uname -s)"
case "$UNAME_S" in MINGW*|MSYS*|CYGWIN*) HOST="$HOST.exe";; esac
WORK="$(mktemp -d "${TMPDIR:-/tmp}/lmweather-test-XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
# Native Windows tools (node, the host) cannot address git-bash /tmp or /d/a paths.
case "$UNAME_S" in MINGW*|MSYS*|CYGWIN*) WORK="$(cd "$WORK" && pwd -W | tr '\134' '/')";; esac
pass=0
ok(){ pass=$((pass+1)); echo "ok $pass - $1"; }
fail(){ echo "FAIL: $*" >&2; exit 1; }
sha(){ if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -c1-64; else shasum -a 256 "$1" | cut -c1-64; fi; }

# lmscript resolves cli="lmweather" on PATH first, so programs bind to the binary under test.
PATH="$(cd "$BUILD_DIR" 2>/dev/null && pwd || echo "$HERE/$BUILD_DIR"):$PATH"
export PATH
LMSCRIPT="$(scripts/select-compiler.sh)"
scripts/sync-sdk.sh --verify >/dev/null || fail "SDK header pin mismatch"
ok "SDK header matches sdk.lock"
scripts/sync-deps.sh --verify >/dev/null || fail "third_party pin mismatch"
ok "no third-party sources"

GEN=""
if [ ! -f "$BUILD_DIR/CMakeCache.txt" ] && command -v ninja >/dev/null 2>&1; then GEN="-G Ninja"; fi
# shellcheck disable=SC2086
cmake -S . -B "$BUILD_DIR" $GEN -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$BUILD_DIR" --parallel "${BUILD_JOBS:-4}" >"$WORK/build.log" 2>&1 || { tail -40 "$WORK/build.log" >&2; fail "build"; }
ok "build"
ctest --test-dir "$BUILD_DIR" --output-on-failure || fail "native physics core tests"
ok "native physics core regression tests"
want_version="lmweather $(sed -n 's/^project(lmweather VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
[ "$("$HOST" --version)" = "$want_version" ] || fail "--version is not exactly '$want_version'"
ok "--version prints exactly '$want_version'"
python3 scripts/gen-schema.py >/dev/null && git diff --quiet -- schema/lmweather.dsl.json 2>/dev/null || [ ! -d .git ] || fail "schema/lmweather.dsl.json is stale: run scripts/gen-schema.py"
ok "schema matches the registrations in cpp/main.cpp"
"$HOST" --dsl-schema --json >"$WORK/schema.json" || fail "embedded schema print"
cmp -s "$WORK/schema.json" schema/lmweather.dsl.json || fail "embedded schema differs from schema/lmweather.dsl.json"
ok "--dsl-schema --json prints byte-identical embedded schema bytes"
diff -u <("$LMSCRIPT" --dsl-signatures schema/lmweather.dsl.json) <("$HOST" --dsl-signatures) || fail "host/compiler signatures differ"
ok "host registry matches compiler signatures"
"$HOST" --notices >"$WORK/notices.txt" || fail "--notices"
grep -q "Lattice Systems" "$WORK/notices.txt" || fail "--notices missing third-party text"
cmp -s "$WORK/notices.txt" THIRD_PARTY_NOTICES || fail "--notices differs from THIRD_PARTY_NOTICES"
ok "--notices prints THIRD_PARTY_NOTICES byte for byte"

case "$UNAME_S" in
  Linux) if ldd "$HOST" >/dev/null 2>&1; then fail "executable is dynamically linked"; fi; ok "executable is fully static (no dynamic loader)";;
  Darwin) bad="$(otool -L "$HOST" | tail -n +2 | grep -v -E '/usr/lib/(libSystem|libc\+\+|libresolv)' || true)"
          [ -z "$bad" ] || fail "non-system dylib dependencies: $bad"; ok "only system libraries linked";;
  MINGW*|MSYS*|CYGWIN*)
    if command -v objdump >/dev/null 2>&1; then
      bad="$(objdump -p "$HOST" | grep 'DLL Name' | grep -i -E 'libstdc|libgcc|winpthread|libgomp' || true)"
      [ -z "$bad" ] || fail "MinGW runtime DLLs are imported: $bad"
      ok "no MinGW runtime DLL imports"
    else ok "static-link check skipped (no objdump)"; fi;;
  *) ok "static-link check skipped on $UNAME_S";;
esac

# Programs: compile every test and example, run them (they assert their own numbers).
mkdir -p build/examples
n=0
for src in tests/*.lms examples/*.lms; do
  name="$(basename "$src" .lms)"
  "$LMSCRIPT" "$src" "build/$name.dslb" >"$WORK/$name.log" 2>&1 || { cat "$WORK/$name.log" >&2; fail "$name compile"; }
  n=$((n+1))
done
ok "$n programs compile as bytecode v5"
for src in tests/*.lms examples/*.lms; do
  name="$(basename "$src" .lms)"
  "$HOST" --dsl "build/$name.dslb" >"$WORK/$name.out" 2>"$WORK/$name.err" || { cat "$WORK/$name.out" "$WORK/$name.err" >&2; fail "$name run"; }
  grep -q '"ok": true' "$WORK/$name.out" || { cat "$WORK/$name.out" >&2; fail "$name did not report ok"; }
done
ok "$n programs run and pass their own assertions"

# Determinism: the CSV files the examples write are identical on rerun.
for f in build/*.csv build/examples/*.csv; do echo "$(sha "$f") $f"; done >"$WORK/before.txt"
for src in tests/determinism.lms examples/*.lms; do
  name="$(basename "$src" .lms)"
  "$HOST" --dsl "build/$name.dslb" >/dev/null 2>&1 || fail "$name rerun"
done
for f in build/*.csv build/examples/*.csv; do echo "$(sha "$f") $f"; done >"$WORK/after.txt"
cmp -s "$WORK/before.txt" "$WORK/after.txt" || fail "result files differ on rerun"
ok "all $(wc -l <"$WORK/after.txt" | tr -d ' ') result files are byte-identical on rerun"

# Error paths: each snippet must fail the run with exit 3 and a message naming the problem.
expect_error() { # name, snippet, expected message fragment
  cat >"$WORK/$1.lms" <<LMS
p = _dsl_ cli="lmweather";
$2
end _dsl_
savedsl(p, "$WORK/$1.dslb");
LMS
  "$LMSCRIPT" "$WORK/$1.lms" >/dev/null 2>"$WORK/$1.cerr" || { cat "$WORK/$1.cerr" >&2; fail "$1 compile"; }
  set +e; out="$("$HOST" --dsl "$WORK/$1.dslb" 2>/dev/null)"; code=$?; set -e
  [ "$code" = 3 ] || fail "$1: exit code $code, expected 3 ($out)"
  echo "$out" | grep -q "$3" || fail "$1: message lacks '$3': $out"
}
expect_error bad_truncation 'let s = wx.sphere(2);' "truncation"
expect_error bad_planet 'let s = wx.planet(10, 0.0, 0.0001, 9.8);' "radius"
expect_error bad_grid 'let p = wx.plane(2, 2, 1.0, 1.0);' "4..4000"
expect_error bad_domain 'let p = wx.plane(10, 10, 0.0, 1.0);' "domain"
expect_error bad_expression 'let s = wx.sphere(10).fields("2 +* 3", "0", "0");' "expression"
expect_error unknown_name 'let s = wx.sphere(10).fields("foo", "0", "0");' "unknown name"
expect_error mode_range 'let s = wx.sphere(10).mode(40, 0, 1.0, 100.0);' "truncation"
expect_error bad_wave 'let s = wx.sphere(10).fields("1000", "0", "0").perturb(1.0, 1, 0);' "max_wave"
expect_error zero_height 'let s = wx.sphere(10).fields("0", "0", "0"); let t = s.stable_dt();' "must be positive"
expect_error unstable_dt 'let p = wx.plane(20, 10, 1000000.0, 500000.0).depth(100.0).fields("cos(x / 100000.0)", "0", "0"); p.run_dt(5000000.0, 50000.0);' "stability limit"
expect_error unstable_sphere_dt 'let s = wx.sphere(21).williamson2(0.0); s.run_dt(86400.0, 86400.0);' "stability limit"
expect_error blow_up 'let p = wx.plane(20, 10, 1000000.0, 500000.0).depth(100.0).fields("-150.0 * cos(2.0 * pi * x / lx)", "0", "0"); p.run(50000000.0);' "blew up"
expect_error nan_longitude 'let s = wx.sphere(10).fields("1000", "0", "0"); let h = s.height_at(10.0, sqrt(-1.0));' "longitude"
expect_error wrapped_int 'let s = wx.sphere(10).mode(4294967298, 0, 1.0, 100.0);' "out of range"
expect_error infinite_depth 'let p = wx.plane(10, 10, 1000000.0, 500000.0).depth(pow(10.0, 400.0));' "depth"
expect_error depth_late 'let p = wx.plane(10, 10, 1000000.0, 500000.0).perturb(1.0, 1).depth(100.0);' "before fields"
expect_error negative_depth 'let p = wx.plane(10, 10, 1000000.0, 500000.0).depth(-5.0);' "depth"
expect_error negative_viscosity 'let p = wx.plane(10, 10, 1000000.0, 500000.0).viscosity(-1.0);' "viscosity"
expect_error outside_domain 'let p = wx.plane(10, 10, 1000000.0, 500000.0); let h = p.h_at(2000000.0, 0.0);' "outside the domain"
expect_error bad_harmonic 'let p = wx.plane(10, 10, 1000000.0, 500000.0); let a = p.zonal_amplitude(9, 2);' "out of range"
expect_error bad_crest 'let s = wx.sphere(10).fields("1000", "0", "0"); let c = s.wave_crest(0, 45.0);' "zonal wavenumber"
expect_error pole_wind 'let s = wx.sphere(10).fields("1000", "0", "0"); let u = s.u_at(90.0, 0.0);' "inside"
expect_error bad_part 'let s = wx.sphere(10).fields("1000", "0", "0"); let c = s.mode_coefficient(1, 0, "abs");' "part must be"
expect_error no_rossby_radius 'let p = wx.plane(10, 10, 1000000.0, 500000.0); let l = p.rossby_radius();' "f0 is zero"
expect_error unwritable 'wx.save_text("/nonexistent-dir/x.csv", "x");' "cannot write"
expect_error assertfail 'wx.assert_close(1.0, 2.0, 0.1);' "assert_close failed"
expect_error assert_true 'wx.assert_true(false);' "assert_true failed"
# Expression strings: deep nesting used to overflow the parser's stack (SIGSEGV).
deep="$(printf '(%.0s' $(seq 300))1$(printf ')%.0s' $(seq 300))"
expect_error deep_expression "let s = wx.sphere(10).fields(\"$deep\", \"0\", \"0\");" "nested too deeply"
expect_error huge_number 'let s = wx.sphere(10).fields("1e400", "0", "0");' "out of range"
expect_error nonfinite_result 'let s = wx.sphere(10).fields("0", "0", "0"); let e = s.error_l2();' "not finite"
ok "errors stop with exit 3 and clear messages (30 cases)"

# Combined pipelines (lmweather simulates, lmplot draws). Need lmplot on PATH, or network for install=.
if command -v lmplot >/dev/null 2>&1 || [ "${LMWEATHER_TEST_NETWORK:-}" = 1 ]; then
  for name in rh_wave_plot rossby_wave_plot; do
    rm -f "build/examples/$name.png"
    "$LMSCRIPT" "examples/combo/$name.lms" >"$WORK/combo-$name.log" 2>&1 || { cat "$WORK/combo-$name.log" >&2; fail "combo example $name"; }
    [ -s "build/examples/$name.png" ] || fail "combo example $name wrote no PNG"
  done
  ok "lmweather + lmplot pipelines draw the Rossby-Haurwitz wave and the beta-plane Rossby wave"
else
  ok "combo examples skipped (no lmplot, LMWEATHER_TEST_NETWORK unset)"
fi

# Dry run loads/verifies bytecode without executing the simulation or writing CSV.
rm -f build/examples/williamson2_errors.csv
"$HOST" --dry-run --dsl build/williamson2.dslb >"$WORK/dry.out" || fail "dry run"
[ ! -e build/examples/williamson2_errors.csv ] || fail "dry run created CSV"
"$HOST" --dsl build/williamson2.dslb >/dev/null || fail "restore example CSV"
ok "dry run verifies without writing outputs"

for flags in '--unknown' '--version extra' '--dsl-schema --bogus' '--dry-run' '--dsl --bogus'; do
  set +e
  # Deliberate word splitting: each fixed test string is a CLI argument sequence.
  # shellcheck disable=SC2086
  "$HOST" $flags >/dev/null 2>&1
  code=$?
  set -e
  [ "$code" = 2 ] || fail "usage flags '$flags': exit $code"
done
ok "invalid CLI combinations use exit 2"

set +e; "$HOST" --dsl "$WORK/does-not-exist.dslb" >/dev/null 2>&1; code=$?; set -e
[ "$code" = 3 ] || fail "missing bytecode exit code $code, expected 3"
set +e; "$HOST" >/dev/null 2>&1; code=$?; set -e
[ "$code" = 2 ] || fail "usage exit code $code, expected 2"
ok "usage and load errors use exit codes 2 and 3"

echo "all $pass checks passed"
