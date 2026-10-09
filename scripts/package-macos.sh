#!/usr/bin/env bash
# Run this on macOS (or in GitHub Actions macos-latest).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIST="$ROOT/dist"
VER="${ALPHASNIPE_VERSION:-1.0.0}"
mkdir -p "$DIST"

cmake -S "$ROOT" -B "$ROOT/build-macos" -DCMAKE_BUILD_TYPE=Release
cmake --build "$ROOT/build-macos" -j"$(sysctl -n hw.ncpu)"

APP="$ROOT/build-macos/AlphaSnipe.app"
if [[ ! -d "$APP" ]]; then
  # Fallback if CMake did not produce a bundle name as expected
  APP=$(find "$ROOT/build-macos" -maxdepth 2 -name "*.app" | head -n1)
fi
if [[ -z "${APP}" || ! -d "$APP" ]]; then
  echo "App bundle not found; wrapping the executable."
  BIN=$(find "$ROOT/build-macos" -type f -perm -111 -name "alphasnipe" -o -name "AlphaSnipe" | head -n1)
  APP="$DIST/AlphaSnipe.app"
  rm -rf "$APP"
  mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
  cp "$BIN" "$APP/Contents/MacOS/AlphaSnipe"
  cp "$ROOT/assets/DejaVuSans.ttf" "$APP/Contents/Resources/DejaVuSans.ttf"
  cp "$ROOT/assets/alphasnipe.png" "$APP/Contents/Resources/alphasnipe.png"
  cat > "$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>AlphaSnipe</string>
  <key>CFBundleIdentifier</key><string>app.alphasnipe</string>
  <key>CFBundleVersion</key><string>${VER}</string>
  <key>CFBundleShortVersionString</key><string>${VER}</string>
  <key>CFBundleExecutable</key><string>AlphaSnipe</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>LSMinimumSystemVersion</key><string>10.15</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>NSCameraUsageDescription</key><string>Screen capture requires screen recording permission.</string>
</dict>
</plist>
EOF
fi

# Copy font next to the binary / into Resources
RES="$APP/Contents/Resources"
mkdir -p "$RES" "$APP/Contents/MacOS"
cp -f "$ROOT/assets/DejaVuSans.ttf" "$RES/DejaVuSans.ttf" || true
cp -f "$ROOT/assets/DejaVuSans.ttf" "$APP/Contents/MacOS/DejaVuSans.ttf" || true

# Optional icns from png if iconutil/sips exist
if command -v sips >/dev/null && command -v iconutil >/dev/null; then
  ICONSET="$ROOT/build-macos/alphasnipe.iconset"
  rm -rf "$ICONSET"
  mkdir -p "$ICONSET"
  for s in 16 32 64 128 256 512; do
    sips -z $s $s "$ROOT/assets/alphasnipe.png" --out "$ICONSET/icon_${s}x${s}.png" >/dev/null
    sips -z $((s*2)) $((s*2)) "$ROOT/assets/alphasnipe.png" --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null
  done
  iconutil -c icns "$ICONSET" -o "$RES/alphasnipe.icns"
fi

chmod +x "$APP/Contents/MacOS/"*
OUT="$DIST/AlphaSnipe-${VER}-macos-universal.zip"
# zip the .app
(
  cd "$(dirname "$APP")"
  zip -r "$OUT" "$(basename "$APP")"
)
echo "Wrote $OUT"
