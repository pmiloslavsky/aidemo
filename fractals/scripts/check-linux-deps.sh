#!/usr/bin/env bash
# Fails if the binary directly needs a shared library outside the system set
# the release promises to rely on (glibc, X11, GL, udev).
set -euo pipefail

bin="$1"
allowed='^(libc|libm|libdl|libpthread|librt|ld-linux-x86-64|libX11|libXrandr|libXcursor|libXi|libXext|libXrender|libGL|libOpenGL|libGLX|libEGL|libudev)\.so'

echo "Direct shared-library deps of $bin:"
bad=0
while read -r lib; do
  if [[ "$lib" =~ $allowed ]]; then
    echo "  ok   $lib"
  else
    echo "  BAD  $lib"
    bad=1
  fi
done < <(readelf -d "$bin" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p')

if [ "$bad" -ne 0 ]; then
  echo "error: unexpected shared-library dependency (expected SFML, TGUI, libstdc++ and cudart to be static)" >&2
  exit 1
fi
