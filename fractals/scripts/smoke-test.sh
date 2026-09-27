#!/usr/bin/env bash
# Renders tests/smoke/*.json with save_and_exit and checks the output. The
# binary is copied into an empty folder first, so this also covers first-run
# setup (FractalsData/, extracted assets, log) and, on a machine without an
# NVIDIA driver, the CPU fallback. Uses xvfb when there is no display.
#   scripts/smoke-test.sh build/linux-release/bin/fractals
set -euo pipefail

bin="$(realpath "$1")"
root="$(cd "$(dirname "$0")/.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
cp "$bin" "$work/fractals"

run=()
if [ -z "${DISPLAY:-}" ]; then
  run=(xvfb-run -a -s "-screen 0 2560x1440x24")
fi

status=0
for key in "$root"/tests/smoke/*.json; do
  name="$(basename "$key" .json)"
  cp "$key" "$work/$name.json"
  echo "=== $name"
  rc=0
  (cd "$work" && timeout 300 "${run[@]}" ./fractals save_and_exit "$name.json" "$name.png" hide) || rc=$?
  if [ "$rc" -ne 0 ]; then echo "FAIL: exit code $rc"; status=1; fi
  if [ -s "$work/$name.png" ]; then echo "ok   $name.png ($(stat -c %s "$work/$name.png") bytes)"
  else echo "FAIL: no $name.png"; status=1; fi
  if [ -s "$work/changed_key.json" ]; then echo "ok   changed_key.json"
  else echo "FAIL: no changed_key.json"; status=1; fi
  rm -f "$work/changed_key.json"
done

log="$work/FractalsData/fractals.log"
if grep -q '^CUDA' "$log"; then grep -E '^(CUDA|OpenGL)' "$log" | sed 's/^/log  /'
else echo "FAIL: no CUDA detection line in the log"; status=1; fi
if [ "$(ls "$work/FractalsData/themes" | wc -l)" -gt 0 ]; then echo "ok   assets extracted"
else echo "FAIL: themes not extracted"; status=1; fi

[ "$status" -eq 0 ] && echo "smoke test passed" || { echo "--- log"; cat "$log"; }
exit "$status"
