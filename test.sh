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
python3 tests/check-host-flags.py schema/lmweather.dsl.json "$HOST" || fail "hard host flag cutover"
ctest --test-dir "$BUILD_DIR" --output-on-failure || fail "native physics core tests"
ok "native physics core regression tests"
want_version="lmweather $(sed -n 's/^project(lmweather VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
[ "$want_version" = "lmweather 0.1.3" ] || fail "CMakeLists.txt version is not 0.1.3 ($want_version)"
[ "$("$HOST" --version)" = "$want_version" ] || fail "--version is not exactly '$want_version'"
ok "--version prints exactly '$want_version'"
python3 scripts/gen-schema.py >/dev/null && git diff --quiet -- schema/lmweather.dsl.json 2>/dev/null || [ ! -e .git ] || fail "schema/lmweather.dsl.json is stale: run scripts/gen-schema.py"
ok "schema matches the registrations in cpp/main.cpp"
"$HOST" --lmcode-schema --json >"$WORK/schema.json" || fail "embedded schema print"
cmp -s "$WORK/schema.json" schema/lmweather.dsl.json || fail "embedded schema differs from schema/lmweather.dsl.json"
ok "--lmcode-schema --json prints byte-identical embedded schema bytes"
diff -u <("$LMSCRIPT" --lmcode-signatures schema/lmweather.dsl.json) <("$HOST" --lmcode-signatures) || fail "host/compiler signatures differ"
ok "host registry matches compiler signatures"
"$HOST" --notices >"$WORK/notices.txt" || fail "--notices"
grep -q "Lattice Systems" "$WORK/notices.txt" || fail "--notices missing third-party text"
grep -q "lmweather 0.1.3" "$WORK/notices.txt" || fail "--notices missing 0.1.3 version"
cmp -s "$WORK/notices.txt" THIRD_PARTY_NOTICES || fail "--notices differs from THIRD_PARTY_NOTICES"
ok "--notices prints THIRD_PARTY_NOTICES byte for byte"

case "$UNAME_S" in
  Linux)
    if command -v readelf >/dev/null 2>&1; then
      needed="$(readelf -d "$HOST" | sed -n 's/.*(NEEDED).*Shared library: \[\(.*\)\]/\1/p')"
    else
      needed="$(ldd "$HOST" | awk '/=>/ {print $1} !/=>/ && /ld-linux/ {sub(/^[[:space:]]*/, ""); sub(/[[:space:]].*/, ""); print $0}' | xargs -n1 basename)"
    fi
    [ -n "$needed" ] || fail "expected dynamic glibc linkage on Linux, found no NEEDED entries"
    bad="$(printf '%s\n' "$needed" | grep -v -E '^(libc\.so\.6|libm\.so\.6|libpthread\.so\.0|libdl\.so\.2|librt\.so\.1|ld-linux.*)' || true)"
    [ -z "$bad" ] || fail "disallowed Linux dynamic libraries: $bad"
    ok "Linux binary links only dynamic glibc (${needed//$'\n'/ })";;
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
ok "$n programs compile as bytecode v6"
for src in tests/*.lms examples/*.lms; do
  name="$(basename "$src" .lms)"
  "$HOST" --lmcode "build/$name.dslb" >"$WORK/$name.out" 2>"$WORK/$name.err" || { cat "$WORK/$name.out" "$WORK/$name.err" >&2; fail "$name run"; }
  grep -q '"ok": true' "$WORK/$name.out" || { cat "$WORK/$name.out" >&2; fail "$name did not report ok"; }
done
ok "$n programs run and pass their own assertions"

# Determinism: the CSV files the examples write are identical on rerun.
for f in build/*.csv build/examples/*.csv; do echo "$(sha "$f") $f"; done >"$WORK/before.txt"
for src in tests/determinism.lms examples/*.lms; do
  name="$(basename "$src" .lms)"
  "$HOST" --lmcode "build/$name.dslb" >/dev/null 2>&1 || fail "$name rerun"
done
for f in build/*.csv build/examples/*.csv; do echo "$(sha "$f") $f"; done >"$WORK/after.txt"
cmp -s "$WORK/before.txt" "$WORK/after.txt" || fail "result files differ on rerun"
ok "all $(wc -l <"$WORK/after.txt" | tr -d ' ') result files are byte-identical on rerun"
# CSV layout: sphere rows are "lon lat h u v zeta q hs", plane rows "x y h u v zeta", blank line between latitude rows
[ "$(awk 'NF' build/determinism_sphere.csv | awk '{print NF}' | sort -u)" = 8 ] || fail "sphere CSV does not have 8 columns"
[ "$(awk 'NF' build/determinism_plane.csv | awk '{print NF}' | sort -u)" = 6 ] || fail "plane CSV does not have 6 columns"
[ "$(awk 'NF' build/determinism_sphere.csv | wc -l | tr -d ' ')" = 2048 ] || fail "T21 sphere CSV should have 64 x 32 rows"
# spectrum files: one line per total wavenumber 1..T with "n ke pe"; Parseval: the kinetic spectrum of a uniform 2000 m layer
# sums to kinetic_energy / H (the test asserts the energies, this checks the file)
[ "$(awk 'NF' build/spectrum_winds.csv | wc -l | tr -d ' ')" = 21 ] || fail "T21 spectrum should have 21 lines"
[ "$(awk '{print NF}' build/spectrum_winds.csv | sort -u)" = 3 ] || fail "spectrum lines should have 3 columns"
awk '{s += $2} END { if (s < 75.0 - 0.000001 || s > 75.0 + 0.000001) { print "kinetic spectrum sums to " s ", expected 75 = 15^2/3"; exit 1 } }' build/spectrum_solid.csv || fail "Parseval: the kinetic spectrum of solid-body rotation does not sum to u0^2/3"
ok "CSV layouts: sphere 8 columns (64 x 32 at T21), plane 6 columns, spectrum n ke pe"

# Error paths: each snippet must fail the run with exit 3 and a message naming the problem.
expect_error() { # name, snippet, expected message fragment
  cat >"$WORK/$1.lms" <<LMS
p = _dsl_ cli="lmweather";
$2
end _dsl_
savedsl(p, "$WORK/$1.dslb");
LMS
  "$LMSCRIPT" "$WORK/$1.lms" >/dev/null 2>"$WORK/$1.cerr" || { cat "$WORK/$1.cerr" >&2; fail "$1 compile"; }
  set +e; out="$("$HOST" --lmcode "$WORK/$1.dslb" 2>/dev/null)"; code=$?; set -e
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
# A step that was stable at the start of run_dt is re-checked every 64 steps: a deep low spins up winds that lower the limit.
expect_error accelerated 'let s = wx.sphere(21).fields("5960.0 - 1500.0 * pow(2.718281828459045, -((lon - 3.0) * (lon - 3.0) * cos(lat) * cos(lat) + lat * lat) / 0.1)", "0", "0"); s.run_dt(5.0 * 86400.0, 0.97 * s.stable_dt() * 1.8856180831641);' "the flow accelerated"
expect_error no_tracer 'let s = wx.sphere(10).fields("1000", "0", "0"); let q = s.tracer_at(0.0, 0.0);' "no tracer"
expect_error advect_no_tracer 'let s = wx.sphere(10).fields("1000", "0", "0").advect_only(); s.run(3600.0);' "set a tracer first"
expect_error tracer_nonfinite 'let s = wx.sphere(10).fields("1000", "0", "0").tracer("1 / (lat - lat)");' "not finite"
expect_error orography_nonfinite 'let s = wx.sphere(10).orography("1 / (lat - lat)");' "not finite"
ok "errors stop with exit 3 and clear messages (35 cases)"

# Memory budget: a runaway allocation loop stops with a run error (exit 3, message on stderr and in
# the JSON line) instead of exhausting the machine. A small budget everywhere; on Linux also the
# default 4096 MiB under a 6 GiB address-space limit (ulimit only here, never in the host).
budget_program() { # name, constructor call
  cat >"$WORK/$1.lms" <<LMS
p = _dsl_ cli="lmweather";
let i = 0;
while (i < 1000000) {
  let m = $2;
  i = i + 1;
}
end _dsl_
savedsl(p, "$WORK/$1.dslb");
LMS
  "$LMSCRIPT" "$WORK/$1.lms" >/dev/null || fail "$1.lms compile"
}
budget_program runaway 'wx.plane(4000, 2000, 1000000.0, 1000000.0)'
budget_program runaway_sphere 'wx.sphere(170)'
for prog in runaway runaway_sphere; do
  set +e; out="$(LMWEATHER_MAX_MEMORY_MB=64 "$HOST" --lmcode "$WORK/$prog.dslb" 2>"$WORK/$prog.err")"; code=$?; set -e
  [ "$code" = 3 ] || fail "$prog loop under LMWEATHER_MAX_MEMORY_MB=64: exit $code, expected 3 ($out)"
  echo "$out" | grep -q "memory budget exceeded" || fail "$prog loop: JSON lacks the budget message: $out"
  grep -q "LMWEATHER_MAX_MEMORY_MB" "$WORK/$prog.err" || fail "$prog loop: no budget message on stderr"
done
set +e; LMWEATHER_MAX_MEMORY_MB=-5 "$HOST" --lmcode "$WORK/runaway.dslb" >/dev/null 2>"$WORK/badenv.err"; code=$?; set -e
[ "$code" = 2 ] && grep -q "positive whole number" "$WORK/badenv.err" || fail "bad LMWEATHER_MAX_MEMORY_MB: exit $code"
if [ "$UNAME_S" = Linux ]; then
  set +e; out="$(ulimit -v 6291456; "$HOST" --lmcode "$WORK/runaway.dslb" 2>/dev/null)"; code=$?; set -e
  [ "$code" = 3 ] && echo "$out" | grep -q "limit 4096 MiB" || fail "runaway loop with the default budget under ulimit -v 6 GiB: exit $code ($out)"
  ok "memory budget: runaway plane and T170 sphere loops stop at 64 MiB, the plane loop also at the default 4096 MiB (under ulimit -v 6 GiB)"
else
  ok "memory budget: runaway plane and T170 sphere loops stop at LMWEATHER_MAX_MEMORY_MB=64 (default-budget run is Linux-only)"
fi

# README gallery: every linked example and image exists, images are valid PNG/GIF within the size limits.
magic_ok() { # file
  case "$1" in
    *.png) [ "$(head -c 8 "$1" | od -An -tx1 | tr -d ' \n')" = "89504e470d0a1a0a" ];;
    *.gif) [ "$(head -c 4 "$1")" = "GIF8" ];;
    *) return 1;;
  esac
}
nimg=0
for img in $(grep -o 'docs/images/[A-Za-z0-9_.-][A-Za-z0-9_.-]*' README.md | sort -u); do
  [ -f "$img" ] || fail "README references missing image $img"
  magic_ok "$img" || fail "$img is not a valid PNG/GIF"
  size=$(wc -c <"$img" | tr -d ' ')
  case "$img" in *.gif) limit=3000000;; *) limit=300000;; esac
  [ "$size" -le "$limit" ] || fail "$img is $size bytes (limit $limit)"
  nimg=$((nimg+1))
done
[ "$nimg" -ge 6 ] || fail "README gallery shows only $nimg images"
for ex in $(grep -o '(examples/[A-Za-z0-9_/.-]*\.lms)' README.md | tr -d '()' | sort -u); do
  [ -f "$ex" ] || fail "README links missing example $ex"
done
ok "README gallery: $nimg images valid and within size limits, every linked example exists"

# Gallery examples: lmweather computes, lmplot draws (needs lmplot on PATH, or network for install=; the GIF needs ffmpeg).
if command -v lmplot >/dev/null 2>&1 || [ "${LMWEATHER_TEST_NETWORK:-}" = 1 ]; then
  ng=0
  for src in examples/gallery/*.lms; do
    name="$(basename "$src" .lms)"
    case "$name" in *_anim) if ! command -v ffmpeg >/dev/null 2>&1; then echo "note: $name skipped (no ffmpeg on PATH)"; continue; fi;; esac
    timeout 300 "$LMSCRIPT" "$src" >"$WORK/gallery-$name.log" 2>&1 || { cat "$WORK/gallery-$name.log" >&2; fail "gallery example $name"; }
    ng=$((ng+1))
  done
  for img in images/*.png images/*.gif; do
    [ -s "$img" ] || continue
    magic_ok "$img" || fail "$img is not a valid PNG/GIF"
  done
  ok "$ng gallery examples run and wrote valid images (docs/images are refreshed by examples/render-all.sh)"
else
  ok "gallery examples skipped (no lmplot on PATH, LMWEATHER_TEST_NETWORK unset): docs/images not regenerated"
fi

# Dry run loads/verifies bytecode without executing the simulation or writing CSV.
rm -f build/examples/williamson2_errors.csv
"$HOST" --dry-run --lmcode build/williamson2.dslb >"$WORK/dry.out" || fail "dry run"
[ ! -e build/examples/williamson2_errors.csv ] || fail "dry run created CSV"
"$HOST" --lmcode build/williamson2.dslb >/dev/null || fail "restore example CSV"
ok "dry run verifies without writing outputs"

for flags in '--unknown' '--version extra' '--lmcode-schema --bogus' '--dry-run' '--lmcode --bogus'; do
  set +e
  # Deliberate word splitting: each fixed test string is a CLI argument sequence.
  # shellcheck disable=SC2086
  "$HOST" $flags >/dev/null 2>&1
  code=$?
  set -e
  [ "$code" = 2 ] || fail "usage flags '$flags': exit $code"
done
ok "invalid CLI combinations use exit 2"

set +e; "$HOST" --lmcode "$WORK/does-not-exist.dslb" >/dev/null 2>&1; code=$?; set -e
[ "$code" = 3 ] || fail "missing bytecode exit code $code, expected 3"
set +e; "$HOST" >/dev/null 2>&1; code=$?; set -e
[ "$code" = 2 ] || fail "usage exit code $code, expected 2"
ok "usage and load errors use exit codes 2 and 3"

# The published compiler injects std; the host embeds only its own schema bytes.
"$LMSCRIPT" --lmcode-signatures schema/lmweather.dsl.json --without-std > "$WORK/std-host-only.sig"
"$LMSCRIPT" --lmcode-signatures cpp/std/lmcode-std.schema.json > "$WORK/std-builtins.sig"
"$HOST" --lmcode-signatures > "$WORK/std-registry.sig"
python3 - "$WORK" <<'PYSTD'
import pathlib,sys,json
w=pathlib.Path(sys.argv[1])
read=lambda name:set((w/name).read_text().splitlines())
assert read('std-registry.sig')-read('std-builtins.sig') == read('std-host-only.sig'), 'host-only registry drift'
schema=str(pathlib.Path('schema/lmweather.dsl.json').resolve()).replace('\\','/')
out=str(w/'std-probe.dslb').replace('\\','/')
(w/'std-probe.lms').write_text('p = _lmcode_ schema='+json.dumps(schema)+';\nlet values = str_list().add("abc");\nif (str_len(values.at(0)) != 3) { let bad = int_list().at(0); }\nend _lmcode_\nsavedsl(p, '+json.dumps(out)+');\n')
PYSTD
"$LMSCRIPT" "$WORK/std-probe.lms" > "$WORK/std-probe.log" 2>&1 || { cat "$WORK/std-probe.log"; fail "implicit std compile"; }
"$HOST" --lmcode "$WORK/std-probe.dslb" > "$WORK/std-run.log" 2>&1 || { cat "$WORK/std-run.log"; fail "implicit std runtime"; }
ok "host-only signatures and implicit std runtime"

echo "all $pass checks passed"
