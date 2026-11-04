#!/usr/bin/env bash
# Select by source-check capability, not CLI version or schema support alone. The compiler writes no
# bytecode files; test.sh checks the v6 header and schema hash on the program files the host receives.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/lmweather-compiler-XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
# A native (mingw) python cannot see MSYS /tmp paths: hand it drive-letter paths when cygpath exists.
native() { if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s\n' "$1"; fi; }
candidate="${LMSCRIPT_BIN:-$(command -v lmscript || true)}"
printf '%s\n' '{"dsl.schema":2,"name":"compiler_probe","types":{},"functions":{},"context":{}}' > "$WORK/probe.schema.json"
# The probe host proves the compiler emits an invocation file the current SDK reads: a stub that
# answers --lmcode-schema and --lmcode with a minimal ok frame. A compiler whose run_lmcode writes
# no "dry": boolean (older releases) fails here, where --check alone would pass.
mkdir -p "$WORK/bin"
cat > "$WORK/bin/probehost" <<SH
#!/bin/sh
if [ "\$1" = "--lmcode-schema" ]; then cat "$WORK/probe.schema.json"; exit 0; fi
if [ "\$1" = "--lmcode" ]; then
  # The invocation file must carry the current envelope's boolean dry field (older compilers omit it).
  grep -q '"dry":\(true\|false\)' "\$LMCODE_INVOCATION_FILE" || { echo "probehost: invocation file lacks boolean dry" >&2; exit 3; }
  printf 'LMCODE_RETURN_BEGIN\n{"ok":true,"result":null}\nLMCODE_RETURN_END\n'
  exit 0
fi
exit 2
SH
chmod +x "$WORK/bin/probehost"
# Test source compiler capabilities; its package version may be historical.
# JSON string quoting also supports checkouts whose paths contain spaces/quotes.
python3 - "$(native "$WORK/probe.schema.json")" "$(native "$WORK")" <<'PY'
import json,pathlib,sys
work=pathlib.Path(sys.argv[2])
(work/'probe.lms').write_text('p = _lmcode_ schema='+json.dumps(sys.argv[1])+';\nlet n = str_len("implicit std");\nend _lmcode_\nr = run_lmcode(p, {command: ["probehost", "--lmcode", "$DSLFILE"]});\nprintln(json_stringify(r));\n')
PY
if [ -n "$candidate" ] &&
   "$candidate" --lmcode-signatures "$WORK/probe.schema.json" > /dev/null 2>&1 &&
   "$candidate" --check "$WORK/probe.lms" > /dev/null 2>&1 &&
   PATH="$WORK/bin:$PATH" "$candidate" "$WORK/probe.lms" > "$WORK/probe.run.out" 2>&1 &&
   grep -q '"ok":true' "$WORK/probe.run.out"
then
  printf '%s\n' "$candidate"
else
  if [ -n "${LMWEATHER_DEBUG:-}" ]; then
    echo "--- compiler probe diagnostics (candidate=$candidate)" >&2
    cat "$WORK/probe.lms" >&2 || true
    [ -n "$candidate" ] && "$candidate" --lmcode-signatures "$WORK/probe.schema.json" 2>&1 | head -3 >&2
    [ -n "$candidate" ] && "$candidate" --check "$WORK/probe.lms" 2>&1 | head -10 >&2
    ls -la "$WORK" >&2
  fi
  echo "error: no compiler with run_lmcode and the current invocation envelope found (older releases omit the dry flag); install @lmctl-ai/lmctl@staging, or set LMSCRIPT_BIN" >&2
  exit 2
fi
