#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIST="$ROOT/dist"
VER="${ALPHASNIPE_VERSION:-1.0.0}"
mkdir -p "$DIST"

linux_only=0
win_only=0
for a in "$@"; do
  case "$a" in
    --linux-only) linux_only=1 ;;
    --windows-only) win_only=1 ;;
  esac
done

package_linux() {
  echo "==> Linux x86_64"
  cmake -S "$ROOT" -B "$ROOT/build-linux" -DCMAKE_BUILD_TYPE=Release
  cmake --build "$ROOT/build-linux" -j"$(nproc)"
  STAGE="$DIST/AlphaSnipe-${VER}-linux-x86_64"
  rm -rf "$STAGE"
  mkdir -p "$STAGE/share/icons" "$STAGE/share/applications"
  cp "$ROOT/build-linux/alphasnipe" "$STAGE/alphasnipe"
  cp "$ROOT/build-linux/DejaVuSans.ttf" "$STAGE/DejaVuSans.ttf"
  cp "$ROOT/README.md" "$STAGE/README.md"
  cp "$ROOT/LICENSE" "$STAGE/LICENSE"
  cp "$ROOT/assets/alphasnipe.desktop" "$STAGE/share/applications/"
  cp "$ROOT/assets/alphasnipe.png" "$STAGE/share/icons/alphasnipe.png"
  chmod +x "$STAGE/alphasnipe"
  tar -C "$DIST" -czf "$DIST/AlphaSnipe-${VER}-linux-x86_64.tar.gz" "$(basename "$STAGE")"

  DEB="$DIST/deb"
  rm -rf "$DEB"
  mkdir -p "$DEB/DEBIAN" "$DEB/usr/bin" "$DEB/usr/share/applications" \
           "$DEB/usr/share/icons/hicolor/256x256/apps" "$DEB/usr/share/doc/alphasnipe" \
           "$DEB/usr/share/alphasnipe"
  cat > "$DEB/DEBIAN/control" <<EOF
Package: alphasnipe
Version: ${VER}
Section: graphics
Priority: optional
Architecture: amd64
Depends: libx11-6, libxext6
Maintainer: AlphaSnipe <alphasnipe@local>
Description: AlphaSnipe screenshot utility
 Capture rectangle, freeform, window or fullscreen snips, annotate, copy and save.
EOF
  cp "$ROOT/build-linux/alphasnipe" "$DEB/usr/bin/alphasnipe"
  cp "$ROOT/build-linux/DejaVuSans.ttf" "$DEB/usr/share/alphasnipe/DejaVuSans.ttf"
  cp "$ROOT/assets/alphasnipe.desktop" "$DEB/usr/share/applications/alphasnipe.desktop"
  cp "$ROOT/assets/alphasnipe.png" "$DEB/usr/share/icons/hicolor/256x256/apps/alphasnipe.png"
  cp "$ROOT/README.md" "$DEB/usr/share/doc/alphasnipe/README.md"
  cp "$ROOT/LICENSE" "$DEB/usr/share/doc/alphasnipe/copyright"
  chmod 755 "$DEB/usr/bin/alphasnipe"
  dpkg-deb --build "$DEB" "$DIST/alphasnipe_${VER}_amd64.deb"
  echo "    $DIST/AlphaSnipe-${VER}-linux-x86_64.tar.gz"
  echo "    $DIST/alphasnipe_${VER}_amd64.deb"
}

package_windows() {
  echo "==> Windows x86_64 (MinGW)"
  cmake -S "$ROOT" -B "$ROOT/build-win" \
        -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/mingw-w64.cmake" \
        -DCMAKE_BUILD_TYPE=Release
  cmake --build "$ROOT/build-win" -j"$(nproc)"
  STAGE="$DIST/AlphaSnipe-${VER}-windows-x86_64"
  rm -rf "$STAGE"
  mkdir -p "$STAGE"
  cp "$ROOT/build-win/AlphaSnipe.exe" "$STAGE/AlphaSnipe.exe"
  cp "$ROOT/build-win/DejaVuSans.ttf" "$STAGE/DejaVuSans.ttf"
  cp "$ROOT/README.md" "$STAGE/README.md"
  cp "$ROOT/LICENSE" "$STAGE/LICENSE"
  (cd "$DIST" && zip -r "AlphaSnipe-${VER}-windows-x86_64.zip" "$(basename "$STAGE")")
  echo "    $DIST/AlphaSnipe-${VER}-windows-x86_64.zip"
}

if [[ $win_only -eq 0 ]]; then package_linux; fi
if [[ $linux_only -eq 0 ]]; then package_windows; fi

echo "==> Done. Artifacts in $DIST"
ls -lh "$DIST"/*.{tar.gz,zip,deb} 2>/dev/null || ls -lh "$DIST"
